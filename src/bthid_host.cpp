/*
bthid_host.cpp - conexion directa con mouse y teclado Bluetooth, ver bthid_host.h

Por dispositivo: conectar el canal de control (PSM 0x11) y el de datos (0x13), elegir el protocolo
(SET_PROTOCOL) y leer reportes del canal de datos.

El formato de los reportes lo dice el descriptor HID que el dispositivo publica por SDP (hidparse.cpp): la
primera vez que conecta se lo pedimos y queda guardado con el dispositivo. Con descriptor usamos el protocolo
de reportes, el normal, el que entiende cualquier mouse. Sin descriptor (todavia no llego, o el SDP fallo)
pedimos el protocolo boot, de formato fijo:
  mouse:   A1 botones dx dy [rueda ...]
  teclado: A1 modificadores 00 tecla1..tecla6
aunque hay mouse que lo ignoran y siguen mandando sus reportes con numero (A1 02 botones x y...).
Si se corta (el mouse se duerme, se apaga, queda lejos) se reintenta cada pocos segundos. Ademas escuchamos
en los dos canales: la mayoria de los mouse no aceptan que el telefono los llame, son ellos los que llaman al
despertarse (HIDNormallyConnectable = no), y esa conexion entrante la adopta el dispositivo guardado.
*/

#include "bthid_host.h"
#include "hidparse.h"
#include <bt_sock.h>
#include <btdevice.h>
#include <btsdp.h>
#include <btmanclient.h>
#include <btnotifierapi.h>
#include <bt_subscribe.h>
#include <e32property.h>
#include <e32math.h>
#include <f32file.h>

const TUint  KHidCtrlPsm      = 0x11;
const TUint  KHidIntrPsm      = 0x13;
const TUint8 KHidpSetProtoBoot = 0x70; // HIDP SET_PROTOCOL, parametro 0 = boot
const TUint8 KHidpSetProtoReport = 0x71; // parametro 1 = reportes (los del descriptor)
const TUint  KHidServiceUuid  = 0x1124;  // servicio HID en el SDP
const TUint  KHidReconnAttr   = 0x0205;  // HIDReconnectInitiate: el dispositivo se reconecta solo
const TUint  KHidDescListAttr = 0x0206;  // atributo HIDDescriptorList
const TUint  KHidNormConnAttr = 0x020D;  // HIDNormallyConnectable: contesta si lo llama el telefono

enum // iFlags del dispositivo (se guardan con el)
{
	EFlagSdpKnown    = 1,  // ya leimos estos atributos
	EFlagReconnSelf  = 2,  // HIDReconnectInitiate
	EFlagNormConnect = 4,  // HIDNormallyConnectable
	EFlagWiiSideways = 8   // Wiimote: las flechas giradas (el control acostado), lo elige la app
};
const TInt   KMaxHidDesc      = 512;
const TUint8 KHidpDataInput   = 0xA1;  // HIDP DATA, reporte de entrada
const TInt   KRetryFirstUs    = 3000000;
const TInt   KRetryUs         = 8000000;
const TInt   KRetryPadUs      = 4000000;
const TInt   KRetrySlowUs     = 60000000; // despues de varios fallos: casi todos los mouse no contestan si los
                                          // llama el telefono, se conectan ellos al moverlos; llamar seguido
                                          // ocupa la radio y no los deja entrar
const TInt   KWaitIntrUs      = 5000000;  // conexion entrante: control llego, esperando el canal de datos
const TInt   KHciPageTimeout  = -6004;    // (el dispositivo no contesto: apagado o lejos)
const TInt   KHciAuthFailure  = -6005;
const TInt   KHciKeyMissing   = -6006;
const TInt   KL2capSecurity   = -6305;    // (L2CAP: fallo la seguridad; el de PS4 sin autenticacion)
const TInt   KFirstCallUs     = 60000000; // recien elegido: el primer minuto se llama solo a ese

class CBtHidDevice;

/* pide el descriptor de reportes HID por SDP (servicio 0x1124, atributo 0x0206) */
class CBtHidSdp : public CBase, public MSdpAgentNotifier, public MSdpAttributeValueVisitor
{
public:
	CBtHidSdp( CBtHidDevice &aDev ) : iDev( aDev ) {}
	~CBtHidSdp()
	{
		delete iAgent;
		delete iPattern;
		delete iAttrs;
	}
	void StartL( const TBTDevAddr &aAddr )
	{
		iAgent = CSdpAgent::NewL( *this, aAddr );
		iPattern = CSdpSearchPattern::NewL();
		iPattern->AddL( TUUID( KHidServiceUuid ));
		iAgent->SetRecordFilterL( *iPattern );
		iAgent->NextRecordRequestL();
	}

private:
	void NextRecordRequestComplete( TInt aError, TSdpServRecordHandle aHandle, TInt aTotal )
	{
		if( aError != KErrNone || aTotal <= 0 )
		{
			Finish( aError != KErrNone ? aError : KErrNotFound );
			return;
		}
		TRAPD( err,
			iAttrs = CSdpAttrIdMatchList::NewL();
			iAttrs->AddL( TAttrRange( KHidReconnAttr, KHidDescListAttr ));
			iAttrs->AddL( TAttrRange( KHidNormConnAttr ));
			iAgent->AttributeRequestL( aHandle, *iAttrs ));
		if( err != KErrNone )
			Finish( err );
	}
	void AttributeRequestResult( TSdpServRecordHandle, TSdpAttributeID aAttrID, CSdpAttrValue *aValue )
	{
		if( !aValue )
			return;
		iAttr = aAttrID;
		TRAP_IGNORE( aValue->AcceptVisitorL( *this ));
		delete aValue;
	}
	void AttributeRequestComplete( TSdpServRecordHandle, TInt aError )
	{
		Finish( iDesc.Length() ? KErrNone : aError != KErrNone ? aError : KErrNotFound );
	}
	// HIDDescriptorList = DES { DES { tipo (0x22 = reportes), descriptor } }: el descriptor es el string
	void VisitAttributeValueL( CSdpAttrValue &aValue, TSdpElementType aType )
	{
		if( iAttr == KHidDescListAttr && aType == ETypeString && !iDesc.Length())
			iDesc.Copy( aValue.Des().Left( iDesc.MaxLength()));
		else if( iAttr == KHidReconnAttr && aType == ETypeBoolean )
			iFlags |= aValue.Bool() ? EFlagReconnSelf : 0;
		else if( iAttr == KHidNormConnAttr && aType == ETypeBoolean )
			iFlags |= aValue.Bool() ? EFlagNormConnect : 0;
	}
	void StartListL( CSdpAttrValueList & ) {}
	void EndListL() {}
	void Finish( TInt aError );

	CBtHidDevice       &iDev;
	CSdpAgent          *iAgent;
	CSdpSearchPattern  *iPattern;
	CSdpAttrIdMatchList *iAttrs;
	TSdpAttributeID     iAttr;
	TBuf8<KMaxHidDesc>  iDesc;
	TInt                iFlags;
	TBool               iDone;
};

class CBtHidDevice : public CActive
{
public:
	CBtHidDevice( CBtHidHost &aHost, const TBTDevAddr &aAddr, TInt aType, const TDesC8 &aName, const TDesC8 &aDesc, TInt aFlags );
	// sin llamarlo: esperar a que se conecte el (si se reconecta solo); si no, llamarlo como siempre
	void Wait();
	~CBtHidDevice();
	void Start();
	void StartAfter( TInt aMicroSeconds );
	void AdoptCtrl( RSocket &aSock );
	void AdoptIntr( RSocket &aSock );
	const TBTDevAddr &Addr() const { return iAddr; }
	TInt Type() const { return iType; }
	const TDesC8 &Name() const { return iName; }
	// llamandolo ahora (la radio esta ocupada con el)
	TBool Calling() const { return iState == EConnCtrl || iState == EConnIntr; }
	TInt State() const { return iConnected ? EBtHidConnected : iStopped ? EBtHidStopped : iState == EWaitDevice
		? EBtHidWaiting : iState == EWaitRetry ? EBtHidDisconnected : EBtHidConnecting; }
	TInt Battery() const { return iConnected ? iPadBattery : -1; }
	TBool Charging() const { return iConnected && iPadCharging; }
	// desconectado a mano: no se lo llama (si llama el, se lo atiende igual)
	void Stop();
	void Resume();
	TInt Flags() const { return iFlags; }
	// los joysticks de PS3: se reconectan llamando ellos (al apretar PS) y no contestan si se los llama. (Los de PS4
	// al reves: al apretar PS esperan que los llame el telefono)
	TBool CallsBack() const
	{
		return iType == EBtHidGamepad && ( iPadKind == EBtHidPadPs3 || iName.FindF( _L8( "PS3" )) >= 0
			|| iName.FindF( _L8( "PLAYSTATION(R)3" )) >= 0 );
	}
	TInt Player() const { return iPlayer; }
	void SetPlayer( TInt aPlayer ) { iPlayer = aPlayer; }
	TInt Error() const { return iError; }
	const TDesC8 &Descriptor() const { return iDesc; }
	void DescriptorDone( TInt aError, const TDesC8 &aDesc, TInt aFlags );
	// joystick: estado, vibracion
	void PadState( TBtHidPadState &aState ) const;
	void SetRumble( TBool aOn );
	void Light() { PadOutput(); }   // (la luz de su numero, al cambiarlo de lugar)
	// Wiimote: por el nombre que da el cuadro de busqueda ("Nintendo RVL-CNT-01", el Plus "...-01-TR")
	TBool IsWii() const { return iType == EBtHidGamepad && ( iPadKind == EBtHidPadWii || iName.Find( _L8( "RVL-CNT" )) >= 0 ); }
	void WiiState( TBtHidWiiState &aState ) const;
	// Wiimote: las flechas giradas (el control acostado: la cruceta a la izquierda, derecha es arriba); fijo, no se
	// cambia al girar el control
	TBool WiiSideways() const { return ( iFlags & EFlagWiiSideways ) != 0; }
	void SetWiiSideways( TBool aOn ) { iFlags = aOn ? iFlags | EFlagWiiSideways : iFlags & ~EFlagWiiSideways; }

private:
	enum TState { EIdle, EConnCtrl, EConnIntr, ESetProto, EReading, EWaitRetry, EWaitIntr, EWaitDevice };

	void RunL();
	void DoCancel();
	TInt RunError( TInt aError );
	void SetProtocol();
	void ConnectChannel( RSocket &aSock, TUint aPsm );
	void CloseSockets();
	void Retry( TInt aErr );
	void ReleaseAll();
	void Report( const TDesC8 &aData );
	void PadReport( const TDesC8 &aData );
	void PadOutput();
	void PadAskFull();
	TBool LayoutReport( const TDesC8 &aData );
	void BootReport( const TDesC8 &aData );
	void KeyboardState( TUint8 aMods, const TUint8 *aKeys );
	void StartSdp();
	void LogLayout();
	void LogDescriptor();
	void WiiPin();
	void WiiStart();
	void WiiSend( const TUint8 *aData, TInt aLen, TInt aWaitUs = 0 );
	void WiiWrite( TUint32 aAddr, const TUint8 *aData, TInt aLen );
	void WiiRead( TUint32 aAddr, TInt aLen );
	void WiiMode();
	void WiiReport( const TDesC8 &aData );
	void WiiIr( const TUint8 *aIr );
	void WiiGyro( const TUint8 *aExt );
	void WiiAim();
	void Log( const char *aWhat, TInt aErr );

	CBtHidHost &iHost;
	TBTDevAddr  iAddr;
	TInt        iType;
	TState      iState;
	RSocket     iCtrl, iIntr;
	TBool       iCtrlOpen, iIntrOpen;
	TBool       iTimerOpen;
	RTimer      iTimer;
	TBuf8<100>  iBuf;            // (el reporte completo del DualShock 4 son 79 bytes)
	TBuf8<1>    iCmd;
	TBool       iConnected;
	TInt        iFails;
	TInt        iError;
	TInt        iRawLogged;
	TBuf8<32>   iName;
	TInt        iButtons;
	TUint8      iMods;
	TUint8      iKeys[6];
	TBuf8<KMaxHidDesc> iDesc;    // descriptor de reportes (vacio = todavia no lo tenemos)
	THidLayout  iLayout;
	TBool       iProtoBoot;      // en esta conexion pedimos boot (no teniamos descriptor)
	CBtHidSdp  *iSdp;
	TBool       iSdpTried;
	TBool       iDescLogged;
	TInt        iFlags;          // EFlag*: lo que dijo el SDP de como se reconecta
	TInt        iOtherLogged;    // para el log: reportes que no son de movimiento
	TInt        iLastBtnByte;    // para el log: cambios del byte de botones
	TInt        iBtnLogged;
	// joystick
	TInt        iPlayer;         // numero de jugador 1-4 (0 = no se conecto desde que arranco el servidor)
	TBuf8<8>    iPadCmd;         // el SET_REPORT que lo activa (DualShock 3)
	TBuf8<80>   iPadOut;         // luces y vibracion
	TInt        iPadKind;        // TBtHidPadKind, segun el formato de los reportes
	TUint32     iPadButtons;
	TUint8      iPadAxis[4];
	TUint8      iPadPressure[EBtHidPadButtons];
	TBool       iPadRumble;
	TInt        iPadReports;
	TUint8      iPadKeysDown[CBtHidHost::EPadKeyInputs]; // teclas del modo teclado apretadas (usos HID)
	TInt        iPadKeysCount;
	TInt        iPadStick;       // el stick izquierdo como flechas (bits arriba derecha abajo izquierda)
	TInt        iPadBattery;     // en % (-1 = no se sabe)
	TBool       iPadCharging;
	TBool       iStopped;        // desconectado a mano
	TBool       iPadFullAsked;   // (DualShock 4: ya se le pidio el reporte completo en esta conexion)
	TBool       iPadFullSeen;    // (y ya llego uno: al log)
	TInt        iPadBatLogged;   // la ultima bateria anotada en el log (-1 = ninguna)
	// Wiimote
	TInt        iWiiAuth;        // como se lo llama: 0 sin codigo, 1 con el de 1+2, 2 con el del boton rojo (WiiPin)
	TBool       iWiiAuthOk;      // (ese anduvo: no se cambia mas)
	TBool       iWiiUnpair;      // fallo la autenticacion: borrar la clave vieja antes del proximo intento
	TBuf8<24>   iWiiOut;
	TUint32     iWiiButtons;
	TInt        iWiiAccel[3], iWiiZero[3], iWiiOne[3]; // crudos, y la calibracion (0 g y 1 g) que trae el control
	TBool       iWiiMplus;       // el MotionPlus manda
	TInt        iWiiGyro[3];
	TBool       iWiiGyroFast[3];
	TInt        iWiiGyroZero[3];
	TInt        iWiiGyroSum[3], iWiiGyroMin[3], iWiiGyroMax[3], iWiiGyroN; // (para tomar el cero quieto)
	TInt        iWiiIrX[4], iWiiIrY[4], iWiiIrCount;
	TInt        iWiiCorner[4];   // que punto es cada esquina: arriba izq, arriba der, abajo der, abajo izq
	TReal       iWiiCornerX[4], iWiiCornerY[4]; // donde se vio (o se supuso) cada esquina en el ultimo reporte
	TInt        iWiiAim;
	TReal       iWiiAimX, iWiiAimY;
	TInt        iWiiShots;
	TReal       iWiiShotX, iWiiShotY;
	TInt        iWiiShotAim;
	TInt        iWiiLogged;      // para el log: respuestas de escritura con error
public:
	void PadKeys();              // el modo teclado: las teclas de lo apretado ahora
	void ReleasePadKeys();
};

void CBtHidSdp::Finish( TInt aError )
{
	if( iDone )
		return;
	iDone = ETrue;
	iDev.DescriptorDone( aError, iDesc, iFlags | EFlagSdpKnown ); // no nos borra aca: lo hace en la proxima conexion
}

CBtHidDevice::CBtHidDevice( CBtHidHost &aHost, const TBTDevAddr &aAddr, TInt aType, const TDesC8 &aName, const TDesC8 &aDesc, TInt aFlags )
	: CActive( EPriorityStandard ), iHost( aHost ), iAddr( aAddr ), iType( aType ), iState( EIdle ), iFlags( aFlags )
{
	iName.Copy( aName.Left( iName.MaxLength()));
	iDesc.Copy( aDesc.Left( iDesc.MaxLength()));
	if( iDesc.Length())
		HidParseDescriptor( iDesc, iLayout );
	CActiveScheduler::Add( this );
	iTimerOpen = iTimer.CreateLocal() == KErrNone;
	Mem::FillZ( iKeys, sizeof( iKeys ));
	iPadBattery = -1;
}

/* ---------------------------------------------------------------- Wiimote
   Se conecta apretando 1+2 (o el boton rojo de adentro de la tapa): queda unos 20 s esperando y el telefono lo llama
   cada pocos segundos. Los originales andan sin codigo; los demas piden emparejar con un codigo binario que no se
   puede escribir en el cuadro del telefono: la direccion del Wiimote al reves (1+2) o la del telefono al reves (boton
   rojo; ese ademas lo deja emparejado). WiiPin deja el codigo en el registro Bluetooth del telefono y el stack lo
   contesta solo. Como no se sabe cual quiere, se prueba sin codigo, con el de 1+2 y con el del boton rojo, y el que
   anda queda.

   Reportes (todo por el canal de datos; los de salida, A2 + numero, llevan la vibracion en el bit 0 del primer byte):
     salida 11 LL: luces (0x10 la 1 ... 0x80 la 4). 12 TT MM: modo de reportes (TT 4 = continuo). 13 04 y 1A 04:
       prender la camara. 15 00: pedir el estado. 16 MM AAAAAA SS + 16 bytes: escribir (MM 4 = registros).
       17 MM AAAAAA SSSS: leer.
     entrada 20 BB BB LF 00 00 VV: estado (VV la bateria, 0xC8 llena); hay que volver a pedir el modo de reportes.
       21 BB BB SE AAAA + 16 bytes: lo leido (SE: los 4 bits bajos el error). 22 BB BB RR EE: respuesta a la escritura.
       37 BB BB XX YY ZZ + 10 de la camara + 6 de la extension: el que usamos, 100 por segundo.
     botones BB BB: [0] izquierda derecha abajo arriba + (bits 0-4), [1] 2 1 B A - (bits 0-4) y Home (bit 7). Los bits
       5 y 6 son los bits bajos del acelerometro.
   La camara en modo basico (10 bytes, 2 puntos cada 5): X1 Y1 [Y1 X1 Y2 X2 altos, 2 bits cada uno] X2 Y2; un punto
   que no ve viene en 0xFF. X crece para la derecha de lo que ve y Y para arriba (como wiiuse y cwiid). */

/* la camara ve mas o menos 33 x 23 grados: el punto al que apunta el control es el centro */
const TReal KWiiCamX = 512.0, KWiiCamY = 384.0;

/* los codigos del Wiimote al registro Bluetooth del telefono: el stack los contesta solo cuando el Wiimote pide
   autenticacion */
void CBtHidDevice::WiiPin()
{
	RBTRegServ     rs;
	RBTRegistry    reg;
	TRequestStatus st;
	TBTPinCode     pin;
	TBuf8<16>      phone;
	TInt           i;

	pin().iLength = 6;
	if( iWiiAuth == 1 )
	{
		for( i = 0; i < 6; i++ )
			pin().iPIN[i] = iAddr[5 - i];
	}
	else
	{
		if( RProperty::Get( KPropertyUidBluetoothCategory, KPropertyKeyBluetoothGetLocalDeviceAddress, phone ) != KErrNone
			|| phone.Length() != 6 )
		{
			Log( "wiimote: no se sabe la direccion del telefono", KErrNotFound );
			return;
		}
		for( i = 0; i < 6; i++ )
			pin().iPIN[i] = phone[5 - i];
	}
	if( rs.Connect() != KErrNone )
		return;
	if( reg.Open( rs ) == KErrNone )
	{
		if( iWiiUnpair )
		{
			iWiiUnpair = EFalse;
			reg.UnpairDevice( iAddr, st );
			User::WaitForRequest( st );
			Log( "wiimote: borrada la clave vieja", st.Int());
		}
		TBTNamelessDevice dev;
		dev.SetAddress( iAddr );
		dev.SetPassKey( pin );
		reg.ModifyDevice( dev, st );
		User::WaitForRequest( st );
		if( st.Int() == KErrNotFound )
		{
			CBTDevice *d = NULL;
			TRAPD( err, d = CBTDevice::NewL( iAddr ));
			if( err == KErrNone )
			{
				d->SetPassKey( pin );
				TRAP( err, reg.AddDeviceL( *d, st ));
				if( err == KErrNone )
					User::WaitForRequest( st );
				else
					st = err;
				delete d;
			}
		}
		Log( iWiiAuth == 1 ? "wiimote: codigo de 1+2 en el registro" : "wiimote: codigo del boton rojo en el registro", st.Int());
		reg.Close();
	}
	rs.Close();
}

/* un reporte de salida (A2 + aData) por el canal de datos, con la vibracion */
void CBtHidDevice::WiiSend( const TUint8 *aData, TInt aLen, TInt aWaitUs )
{
	TRequestStatus st;

	if( !iIntrOpen )
		return;
	iWiiOut.SetLength( 0 );
	iWiiOut.Append( 0xA2 );
	iWiiOut.Append( aData, Min( aLen, iWiiOut.MaxLength() - 1 ));
	if( iWiiOut.Length() > 2 )
		iWiiOut[2] = (TUint8)(( iWiiOut[2] & ~1 ) | ( iPadRumble ? 1 : 0 ));
	iIntr.Write( iWiiOut, st );
	User::WaitForRequest( st );
	// (la camara se queda en un estado raro si se le escribe muy seguido: 50 ms entre escrituras)
	if( aWaitUs )
		User::After( aWaitUs );
}

/* escribir hasta 16 bytes en los registros (aAddr con el 0x04000000 de "registro" se arma aca) */
void CBtHidDevice::WiiWrite( TUint32 aAddr, const TUint8 *aData, TInt aLen )
{
	TUint8 r[22];
	Mem::FillZ( r, sizeof( r ));
	r[0] = 0x16;
	r[1] = 0x04;
	r[2] = (TUint8)( aAddr >> 16 );
	r[3] = (TUint8)( aAddr >> 8 );
	r[4] = (TUint8)aAddr;
	r[5] = (TUint8)aLen;
	Mem::Copy( r + 6, aData, Min( aLen, 16 ));
	WiiSend( r, sizeof( r ), 50000 );
}

/* leer de la memoria del control (la calibracion) o de los registros (aAddr > 0xFFFF: la extension) */
void CBtHidDevice::WiiRead( TUint32 aAddr, TInt aLen )
{
	TUint8 r[7];
	r[0] = 0x17;
	r[1] = aAddr > 0xFFFF ? 0x04 : 0x00;
	r[2] = (TUint8)( aAddr >> 16 );
	r[3] = (TUint8)( aAddr >> 8 );
	r[4] = (TUint8)aAddr;
	r[5] = (TUint8)( aLen >> 8 );
	r[6] = (TUint8)aLen;
	WiiSend( r, sizeof( r ), 20000 );
}

/* botones + acelerometro + camara (basico) + extension, continuo */
void CBtHidDevice::WiiMode()
{
	static const TUint8 mode[3] = { 0x12, 0x04, 0x37 };
	WiiSend( mode, sizeof( mode ));
}

/* recien conectado: la luz, la calibracion del acelerometro, la camara, el MotionPlus (si tiene, contesta la
   lectura de su identificacion y se lo activa en WiiReport) y el modo de reportes */
void CBtHidDevice::WiiStart()
{
	static const TUint8 status[2] = { 0x15, 0x00 };
	static const TUint8 cam1[2] = { 0x13, 0x04 }, cam2[2] = { 0x1A, 0x04 };
	static const TUint8 on[1] = { 0x08 }, basic[1] = { 0x01 }, mplus[1] = { 0x55 };
	// sensibilidad: la que sugiere marcan en wiibrew (la ultima del bloque 1 mas alta = menos sensible)
	static const TUint8 block1[9] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x90, 0x00, 0xC0 };
	static const TUint8 block2[2] = { 0x40, 0x00 };
	TInt i;

	iPadKind = EBtHidPadWii;
	iWiiButtons = 0;
	iWiiMplus = EFalse;
	iWiiIrCount = 0;
	iWiiAim = 0;
	iWiiShots = 0;
	iWiiShotAim = 0;
	iWiiGyroN = 0;
	iWiiLogged = 0;
	for( i = 0; i < 4; i++ )
	{
		iWiiIrX[i] = iWiiIrY[i] = -1;
		iWiiCorner[i] = -1;
	}
	for( i = 0; i < 3; i++ )
	{
		iWiiAccel[i] = 512;
		iWiiGyro[i] = iWiiGyroZero[i] = 8063; // (quieto da ~0x1F7F)
		if( iWiiOne[i] <= iWiiZero[i] )
		{
			// hasta que llegue la del control (WiiReport, 0x21)
			iWiiZero[i] = 512;
			iWiiOne[i] = 616;
		}
	}
	PadOutput(); // la luz de su numero
	WiiSend( status, sizeof( status ), 20000 );
	WiiRead( 0x0016, 10 );
	WiiSend( cam1, sizeof( cam1 ), 50000 );
	WiiSend( cam2, sizeof( cam2 ), 50000 );
	WiiWrite( 0xB00030, on, 1 );
	WiiWrite( 0xB00000, block1, sizeof( block1 ));
	WiiWrite( 0xB0001A, block2, sizeof( block2 ));
	WiiWrite( 0xB00033, basic, 1 ); // basico: el unico que entra en el reporte 0x37 junto con la extension
	WiiWrite( 0xB00030, on, 1 );
	WiiWrite( 0xA600F0, mplus, 1 );
	WiiRead( 0xA600FA, 6 );
	WiiMode();
	Log( "wiimote: camara y modo de reportes pedidos", KErrNone );
}

void CBtHidDevice::WiiReport( const TDesC8 &aData )
{
	static const TUint8 wiiBits[2][8] = {
		{ EBtHidWiiLeft, EBtHidWiiRight, EBtHidWiiDown, EBtHidWiiUp, EBtHidWiiPlus, 0xFF, 0xFF, 0xFF },
		{ EBtHidWiiTwo, EBtHidWiiOne, EBtHidWiiB, EBtHidWiiA, EBtHidWiiMinus, 0xFF, 0xFF, EBtHidWiiHome } };
	const TUint8 *p = aData.Ptr();
	TInt    n = aData.Length(), i, k, id = p[1];
	TUint32 b = 0;

	if( n < 4 || id < 0x20 || id > 0x37 )
		return;
	for( k = 0; k < 2; k++ )
		for( i = 0; i < 8; i++ )
			if( wiiBits[k][i] != 0xFF && ( p[2 + k] & ( 1 << i )))
				b |= 1 << wiiBits[k][i];

	if( id == 0x20 && n >= 8 )
	{
		/* el estado: la bateria. Despues de este reporte no manda mas nada hasta que se le vuelve a pedir el modo */
		iPadBattery = p[7] >= 0xC8 ? 100 : p[7] * 100 / 0xC8;
		iPadCharging = EFalse;
		WiiMode();
	}
	else if( id == 0x21 && n >= 7 + 6 )
	{
		TInt err = p[4] & 0x0F, addr = ( p[5] << 8 ) | p[6];
		const TUint8 *d = p + 7;
		if( addr == 0x0016 && !err && n >= 7 + 8 )
		{
			/* la calibracion: 0 g y 1 g de X Y Z (8 bits altos, y los 2 bajos juntos en el cuarto byte) */
			for( i = 0; i < 3; i++ )
			{
				iWiiZero[i] = ( d[i] << 2 ) | (( d[3] >> ( 4 - 2 * i )) & 3 );
				iWiiOne[i] = ( d[4 + i] << 2 ) | (( d[7] >> ( 4 - 2 * i )) & 3 );
			}
			Log( "wiimote: calibracion del acelerometro", iWiiOne[2] - iWiiZero[2] );
		}
		else if( addr == 0x00FA )
		{
			/* la identificacion del MotionPlus (00 00 A6 20 00 05): activarlo como la extension */
			if( !err && d[2] == 0xA6 && d[3] == 0x20 )
			{
				static const TUint8 activate[1] = { 0x04 };
				Log( "wiimote: tiene MotionPlus, activandolo", KErrNone );
				WiiWrite( 0xA600FE, activate, 1 ); // (despues llega un 0x20, que vuelve a pedir el modo)
			}
			else
				Log( "wiimote: sin MotionPlus", err );
		}
	}
	else if( id == 0x22 && n >= 6 && p[5] && iWiiLogged < 10 )
	{
		iWiiLogged++;
		Log( "wiimote: error en el reporte", p[4] * 1000 + p[5] );
	}
	else if( id >= 0x31 && id != 0x32 && id != 0x34 && id != 0x36 && n >= 7 )
	{
		/* el acelerometro: 8 bits altos en XX YY ZZ y los bajos en los botones (X 2 bits, Y y Z 1) */
		iWiiAccel[0] = ( p[4] << 2 ) | (( p[2] >> 5 ) & 3 );
		iWiiAccel[1] = ( p[5] << 2 ) | (( p[3] >> 4 ) & 2 );
		iWiiAccel[2] = ( p[6] << 2 ) | (( p[3] >> 5 ) & 2 );
		if( id == 0x37 && n >= 23 )
		{
			WiiIr( p + 7 );
			WiiGyro( p + 17 );
		}
	}

	/* B (el gatillo) recien apretado: un disparo, donde apuntaba en este reporte */
	if(( b & ( 1 << EBtHidWiiB )) && !( iWiiButtons & ( 1 << EBtHidWiiB )))
	{
		iWiiShots++;
		iWiiShotAim = iWiiAim;
		iWiiShotX = iWiiAimX;
		iWiiShotY = iWiiAimY;
	}
	if( b != iWiiButtons && iBtnLogged < 40 )
	{
		TBuf8<48> msg;
		msg.Format( _L8( "wiimote: botones %04x" ), b );
		Log( (const char *)msg.PtrZ(), iWiiAim );
		iBtnLogged++;
	}
	iWiiButtons = b;

	/* como joystick (PadState y el modo teclado): A cruz, B R2, 1 cuadrado, 2 circulo, - select, + start, Home PS. Con
	   las flechas giradas (la opcion de la app): arriba es izquierda, abajo derecha, izquierda abajo, derecha arriba */
	{
		static const TUint8 padOf[EBtHidWiiButtons] = { EBtHidPadUp, EBtHidPadDown, EBtHidPadLeft, EBtHidPadRight,
			EBtHidPadCross, EBtHidPadR2, EBtHidPadSquare, EBtHidPadCircle, EBtHidPadSelect, EBtHidPadStart, EBtHidPadPS };
		static const TUint8 sidewaysOf[4] = { EBtHidPadLeft, EBtHidPadRight, EBtHidPadDown, EBtHidPadUp };
		const TBool sideways = WiiSideways();
		TUint32 pb = 0;
		for( i = 0; i < EBtHidWiiButtons; i++ )
			if( b & ( 1 << i ))
				pb |= 1 << ( i < 4 && sideways ? sidewaysOf[i] : padOf[i] );
		iPadButtons = pb;
		for( i = 0; i < EBtHidPadButtons; i++ )
			iPadPressure[i] = ( pb & ( 1 << i )) ? 255 : 0;
	}
	iPadReports++;
	if( iPadReports == 200 )
		iWiiAuthOk = ETrue; // anduvo un rato: esa forma de llamarlo queda
	/* la bateria cada 30 s (100 reportes por segundo) */
	if( iPadReports % 3000 == 0 )
	{
		static const TUint8 status[2] = { 0x15, 0x00 };
		WiiSend( status, sizeof( status ));
	}
	if( iPadBattery >= 0 && ( iPadBatLogged < 0 || iPadBattery / 10 != iPadBatLogged / 10 ))
	{
		iPadBatLogged = iPadBattery;
		Log( "bateria", iPadBattery );
	}
	PadKeys();
}

/* la camara en modo basico: 4 puntos, 2 cada 5 bytes */
void CBtHidDevice::WiiIr( const TUint8 *aIr )
{
	TInt k, i;

	iWiiIrCount = 0;
	for( k = 0; k < 2; k++ )
	{
		const TUint8 *q = aIr + 5 * k;
		TInt x[2], y[2];
		x[0] = q[0] | ((( q[2] >> 4 ) & 3 ) << 8 );
		y[0] = q[1] | ((( q[2] >> 6 ) & 3 ) << 8 );
		x[1] = q[3] | (( q[2] & 3 ) << 8 );
		y[1] = q[4] | ((( q[2] >> 2 ) & 3 ) << 8 );
		for( i = 0; i < 2; i++ )
		{
			TBool seen = y[i] < 768 && x[i] < 1023;
			iWiiIrX[2 * k + i] = seen ? x[i] : -1;
			iWiiIrY[2 * k + i] = seen ? y[i] : -1;
			if( seen )
				iWiiIrCount++;
		}
	}
	WiiAim();
}

/* el MotionPlus: yaw roll pitch de 14 bits (8 bajos en 0-2, 6 altos arriba de 3-5); en 3 y 4 los bits de "lento"
   (0 = gira rapido, otra escala); el bit 1 de 5 dice que son datos del MotionPlus. El cero se toma solo: medio
   segundo quieto (lento y moviendose menos de 40) */
void CBtHidDevice::WiiGyro( const TUint8 *aExt )
{
	TInt i;

	if( !( aExt[5] & 0x02 ))
		return;
	if( !iWiiMplus )
		Log( "wiimote: llegan los datos del MotionPlus", KErrNone );
	iWiiMplus = ETrue;
	iWiiGyro[0] = aExt[0] | (( aExt[3] & 0xFC ) << 6 );
	iWiiGyro[1] = aExt[1] | (( aExt[4] & 0xFC ) << 6 );
	iWiiGyro[2] = aExt[2] | (( aExt[5] & 0xFC ) << 6 );
	iWiiGyroFast[0] = !( aExt[3] & 0x02 );
	iWiiGyroFast[1] = !( aExt[4] & 0x02 );
	iWiiGyroFast[2] = !( aExt[3] & 0x01 );

	if( iWiiGyroFast[0] || iWiiGyroFast[1] || iWiiGyroFast[2] )
	{
		iWiiGyroN = 0;
		return;
	}
	for( i = 0; i < 3; i++ )
	{
		TInt v = iWiiGyro[i];
		if( !iWiiGyroN )
		{
			iWiiGyroSum[i] = 0;
			iWiiGyroMin[i] = iWiiGyroMax[i] = v;
		}
		iWiiGyroSum[i] += v;
		if( v < iWiiGyroMin[i] ) iWiiGyroMin[i] = v;
		if( v > iWiiGyroMax[i] ) iWiiGyroMax[i] = v;
	}
	iWiiGyroN++;
	for( i = 0; i < 3; i++ )
		if( iWiiGyroMax[i] - iWiiGyroMin[i] > 40 )
		{
			iWiiGyroN = 0; // se movio: de nuevo
			return;
		}
	if( iWiiGyroN >= 50 )
	{
		for( i = 0; i < 3; i++ )
			iWiiGyroZero[i] = iWiiGyroSum[i] / iWiiGyroN;
		iWiiGyroN = 0;
	}
}

/* a donde apunta. Las 4 esquinas en la camara, por el angulo alrededor del centro: se busca la de arriba a la
   izquierda y de ahi, en sentido antihorario, abajo izq, abajo der, arriba der. Con las 4 se calcula la transformacion de perspectiva del cuadrado (0,0)-(1,1)
   a esas 4 (Heckbert) y con la inversa, el centro de la camara en la pantalla. Si se pierden 1 o 2, cada punto que
   se ve es la esquina que estaba mas cerca en el reporte anterior y las que faltan se mueven lo mismo que esas */
void CBtHidDevice::WiiAim()
{
	TReal px[4], py[4];
	TInt  seen = 0, i, c;

	if( iWiiIrCount == 4 )
	{
		TReal cx = 0, cy = 0, ang[4];
		TInt  order[4], best = 0;
		TReal bestd = 100;
		for( i = 0; i < 4; i++ )
		{
			cx += iWiiIrX[i];
			cy += iWiiIrY[i];
		}
		cx /= 4;
		cy /= 4;
		for( i = 0; i < 4; i++ )
		{
			Math::ATan( ang[i], iWiiIrY[i] - cy, iWiiIrX[i] - cx );
			order[i] = i;
		}
		for( i = 1; i < 4; i++ ) // por angulo
			for( c = i; c > 0 && ang[order[c]] < ang[order[c - 1]]; c-- )
			{
				TInt t = order[c];
				order[c] = order[c - 1];
				order[c - 1] = t;
			}
		/* los lados largos son los horizontales: de las dos esquinas de donde sale un lado corto (en sentido
		   antihorario, arriba izq -> abajo izq), la de arriba a la izquierda es la mas alta mirando perpendicular a
		   los largos (vale girado hasta 75 grados). Si los lados se parecen mucho, la mas cerca de los 135 grados */
		TReal len[4];
		for( i = 0; i < 4; i++ )
		{
			TReal ex = iWiiIrX[order[( i + 1 ) % 4]] - iWiiIrX[order[i]], ey = iWiiIrY[order[( i + 1 ) % 4]] - iWiiIrY[order[i]];
			Math::Sqrt( len[i], ex * ex + ey * ey );
		}
		TReal pa = len[0] + len[2], pb = len[1] + len[3];
		if( pa - pb > 0.1 * pa || pb - pa > 0.1 * pb )
		{
			TInt  c0 = pa < pb ? 0 : 1;
			TReal hx = iWiiIrX[order[( c0 + 2 ) % 4]] - iWiiIrX[order[( c0 + 1 ) % 4]];
			TReal hy = iWiiIrY[order[( c0 + 2 ) % 4]] - iWiiIrY[order[( c0 + 1 ) % 4]];
			if( hx < 0 )
			{
				hx = -hx;
				hy = -hy;
			}
			// (-hy, hx): para arriba
			TReal s0 = ( iWiiIrX[order[c0]] - cx ) * -hy + ( iWiiIrY[order[c0]] - cy ) * hx;
			TReal s2 = ( iWiiIrX[order[c0 + 2]] - cx ) * -hy + ( iWiiIrY[order[c0 + 2]] - cy ) * hx;
			best = s0 >= s2 ? c0 : c0 + 2;
		}
		else
			for( i = 0; i < 4; i++ )
			{
				TReal d = ang[order[i]] - 3 * KPi / 4;
				if( d > KPi ) d -= 2 * KPi;
				if( d < -KPi ) d += 2 * KPi;
				if( d < 0 ) d = -d;
				if( d < bestd )
				{
					bestd = d;
					best = i;
				}
			}
		{
			static const TInt cornerOfStep[4] = { 0, 3, 2, 1 }; // arriba izq, abajo izq, abajo der, arriba der
			for( i = 0; i < 4; i++ )
				iWiiCorner[cornerOfStep[i]] = order[( best + i ) % 4];
		}
		for( c = 0; c < 4; c++ )
		{
			px[c] = iWiiIrX[iWiiCorner[c]];
			py[c] = iWiiIrY[iWiiCorner[c]];
		}
		seen = 4;
	}
	else if( iWiiAim && iWiiIrCount >= 2 )
	{
		TBool have[4] = { EFalse, EFalse, EFalse, EFalse };
		TReal mx = 0, my = 0;
		for( c = 0; c < 4; c++ )
			iWiiCorner[c] = -1;
		for( i = 0; i < 4; i++ )
		{
			TInt  nearest = -1;
			TReal neard = 200.0 * 200.0; // (mas lejos que eso en un reporte, no es esa)
			if( iWiiIrX[i] < 0 )
				continue;
			for( c = 0; c < 4; c++ )
			{
				TReal dx = iWiiIrX[i] - iWiiCornerX[c], dy = iWiiIrY[i] - iWiiCornerY[c];
				if( !have[c] && dx * dx + dy * dy < neard )
				{
					neard = dx * dx + dy * dy;
					nearest = c;
				}
			}
			if( nearest < 0 )
				continue;
			have[nearest] = ETrue;
			iWiiCorner[nearest] = i;
			px[nearest] = iWiiIrX[i];
			py[nearest] = iWiiIrY[i];
			mx += px[nearest] - iWiiCornerX[nearest];
			my += py[nearest] - iWiiCornerY[nearest];
			seen++;
		}
		if( seen < 2 )
		{
			iWiiAim = 0;
			return;
		}
		mx /= seen;
		my /= seen;
		for( c = 0; c < 4; c++ )
			if( !have[c] )
			{
				px[c] = iWiiCornerX[c] + mx;
				py[c] = iWiiCornerY[c] + my;
			}
	}
	else
	{
		iWiiAim = 0;
		for( c = 0; c < 4; c++ )
			iWiiCorner[c] = -1;
		return;
	}

	/* del cuadrado a las esquinas: (u, v) -> ( a u + b v + c, d u + e v + f ) / ( g u + h v + 1 ) */
	TReal dx1 = px[1] - px[2], dx2 = px[3] - px[2], dx3 = px[0] - px[1] + px[2] - px[3];
	TReal dy1 = py[1] - py[2], dy2 = py[3] - py[2], dy3 = py[0] - py[1] + py[2] - py[3];
	TReal den = dx1 * dy2 - dx2 * dy1;
	if( den > -1e-6 && den < 1e-6 )
	{
		iWiiAim = 0;
		return;
	}
	TReal g = ( dx3 * dy2 - dx2 * dy3 ) / den, h = ( dx1 * dy3 - dx3 * dy1 ) / den;
	TReal a = px[1] - px[0] + g * px[1], b = px[3] - px[0] + h * px[3], cc = px[0];
	TReal d = py[1] - py[0] + g * py[1], e = py[3] - py[0] + h * py[3], f = py[0];
	/* la inversa (la adjunta: la escala se cancela) aplicada al centro de la camara */
	TReal w = ( d * h - e * g ) * KWiiCamX + ( b * g - a * h ) * KWiiCamY + ( a * e - b * d );
	if( w > -1e-9 && w < 1e-9 )
	{
		iWiiAim = 0;
		return;
	}
	iWiiAimX = (( e - f * h ) * KWiiCamX + ( cc * h - b ) * KWiiCamY + ( b * f - cc * e )) / w;
	iWiiAimY = (( f * g - d ) * KWiiCamX + ( a - cc * g ) * KWiiCamY + ( cc * d - a * f )) / w;
	for( c = 0; c < 4; c++ )
	{
		iWiiCornerX[c] = px[c];
		iWiiCornerY[c] = py[c];
	}
	iWiiAim = seen;
}

void CBtHidDevice::WiiState( TBtHidWiiState &aState ) const
{
	TInt i;

	aState.iConnected = iConnected && iPadKind == EBtHidPadWii;
	aState.iButtons = iWiiButtons;
	for( i = 0; i < 3; i++ )
	{
		TInt one = iWiiOne[i] - iWiiZero[i];
		aState.iAccel[i] = iWiiAccel[i];
		aState.iAccelG[i] = one > 0 ? (TReal32)( iWiiAccel[i] - iWiiZero[i] ) / one : 0;
		aState.iGyro[i] = iWiiGyro[i];
		/* 8192/595 unidades por grado/s lento; rapido, 2000/440 veces mas */
		TReal dps = ( iWiiGyro[i] - iWiiGyroZero[i] ) / ( 8192.0 / 595.0 );
		aState.iGyroDps[i] = (TReal32)( iWiiGyroFast[i] ? dps * 2000.0 / 440.0 : dps );
	}
	aState.iMotionPlus = iWiiMplus;
	aState.iIrCount = iWiiIrCount;
	for( i = 0; i < 4; i++ )
	{
		aState.iIrX[i] = iWiiIrX[i];
		aState.iIrY[i] = iWiiIrY[i];
		aState.iIrCorner[i] = iWiiAim ? iWiiCorner[i] : -1;
	}
	aState.iAim = iWiiAim;
	aState.iAimX = (TReal32)iWiiAimX;
	aState.iAimY = (TReal32)iWiiAimY;
	aState.iShots = iWiiShots;
	aState.iShotX = (TReal32)iWiiShotX;
	aState.iShotY = (TReal32)iWiiShotY;
	aState.iShotAim = iWiiShotAim;
	aState.iBattery = iConnected ? iPadBattery : -1;
	aState.iReports = iPadReports;
}

CBtHidDevice::~CBtHidDevice()
{
	Cancel();
	ReleaseAll();
	CloseSockets();
	delete iSdp;
	if( iTimerOpen )
		iTimer.Close();
}

void CBtHidDevice::Log( const char *aWhat, TInt aErr )
{
	TBuf<16>  readable;
	TBuf8<16> addr;
	TBuf8<96> msg;

	iAddr.GetReadable( readable );
	addr.Copy( readable );
	msg.Format( _L8( "%s %S %s (%d)" ), iType == EBtHidKeyboard ? "teclado" : iType == EBtHidGamepad ? "joystick" : "mouse",
		&addr, aWhat, aErr );
	iHost.Observer().BtHidLog( (const char *)msg.PtrZ());
}

void CBtHidDevice::CloseSockets()
{
	if( iIntrOpen ) { iIntr.Close(); iIntrOpen = EFalse; }
	if( iCtrlOpen ) { iCtrl.Close(); iCtrlOpen = EFalse; }
}

/* que no quede nada apretado si se corta en medio de un arrastre o con una tecla abajo */
void CBtHidDevice::ReleaseAll()
{
	TInt i;

	if( iButtons )
	{
		iButtons = 0;
		iHost.Observer().BtHidMouse( 0, 0, 0, 0 );
	}
	for( i = 0; i < 8; i++ )
		if( iMods & ( 1 << i ))
			iHost.Observer().BtHidKey( 224 + i, EFalse );
	for( i = 0; i < 6; i++ )
		if( iKeys[i] > 1 )
			iHost.Observer().BtHidKey( iKeys[i], EFalse );
	iMods = 0;
	Mem::FillZ( iKeys, sizeof( iKeys ));
	ReleasePadKeys();
}

void CBtHidDevice::Start()
{
	TInt err;

	if( iStopped )
		return;
	/* el cuadro de busqueda esta abierto (llamar ocuparia la radio y no aparecerian los dispositivos), o se esta
	   llamando a otro: de a uno. Dos llamadas juntas fallan las dos (-6012) y un joystick recien elegido, esperando
	   en modo emparejar, no conectaba nunca mientras se llamaba a los otros (apagados) */
	TBool searching = iHost.Searching();
	if(( searching || iHost.RadioBusy( this )) && iTimerOpen )
	{
		Cancel();
		CloseSockets();
		iState = EWaitRetry;
		iTimer.After( iStatus, searching ? 3000000 : 1000000 );
		SetActive();
		return;
	}
	Cancel();
	CloseSockets();
	delete iSdp; // de la conexion anterior
	iSdp = NULL;
	err = iCtrl.Open( iHost.SocketServ(), KBTAddrFamily, KSockSeqPacket, KL2CAP );
	if( err == KErrNone )
	{
		iCtrlOpen = ETrue;
		err = iIntr.Open( iHost.SocketServ(), KBTAddrFamily, KSockSeqPacket, KL2CAP );
		if( err == KErrNone )
			iIntrOpen = ETrue;
	}
	if( err != KErrNone )
	{
		Retry( err );
		return;
	}
	iState = EConnCtrl;
	if( iFails < 20 )
		Log( "llamando al dispositivo", iFails );
	if( IsWii() && iWiiAuth )
		WiiPin();
	ConnectChannel( iCtrl, KHidCtrlPsm );
}

/* los que se reconectan solos (HIDReconnectInitiate, casi todos los mouse): no llamarlos, esperarlos. Llamar
   ocupa la radio ~5 s por intento y en ese rato la llamada del mouse no entra (era por lo que habia que apretar
   "conectar" en el mouse) */
void CBtHidDevice::Wait()
{
	/* los joysticks genericos dicen que se reconectan solos pero no siempre llaman: tambien los llamamos
	   (si llaman ellos, la conexion entrante los adopta igual) */
	if( !( iFlags & EFlagReconnSelf ) || iType == EBtHidGamepad )
	{
		StartAfter( 2000000 );
		return;
	}
	Cancel();
	CloseSockets();
	iState = EWaitDevice; // nada pendiente: la conexion la trae CBtHidListener (AdoptCtrl / AdoptIntr)
	Log( "esperando que se conecte", KErrNone );
}

void CBtHidDevice::Stop()
{
	Log( "desconectado a mano", KErrNone );
	iStopped = ETrue;
	Cancel();
	ReleaseAll();
	CloseSockets();
	iConnected = EFalse;
	iState = EWaitDevice; // (si llama el, se lo atiende)
}

void CBtHidDevice::Resume()
{
	iStopped = EFalse;
	iFails = 0;
	if( !iConnected )
	{
		Log( "conectar: llamandolo", KErrNone );
		Start();
	}
}

/* primera conexion un poco despues: recien elegido en el cuadro, la radio todavia esta terminando la busqueda */
void CBtHidDevice::StartAfter( TInt aMicroSeconds )
{
	Cancel();
	iState = EWaitRetry;
	if( !iTimerOpen )
	{
		Start();
		return;
	}
	iTimer.After( iStatus, aMicroSeconds );
	SetActive();
}

void CBtHidDevice::ConnectChannel( RSocket &aSock, TUint aPsm )
{
	TBTSockAddr         addr;
	TBTServiceSecurity  sec;

	// los teclados exigen cifrado (y emparejar con codigo). A los mouse no les pedimos nada: muchos no usan
	// seguridad y cortaban la conexion; si el mouse la quiere, la pide el y el telefono empareja igual
	/* el joystick de PS4 tambien: recien elegido (en modo emparejar) no la pide, pero al reconectarse si (sin ella
	   abre los canales, no manda nada y corta con -6305, falla de autenticacion). Emparejado una vez, se reconecta
	   con la clave guardada. (El de PS3 no: se empareja por USB con ds3pair) */
	/* el Wiimote: segun el intento, sin codigo o con el que dejo WiiPin en el registro (sin cifrado) */
	const TBool auth = IsWii() ? iWiiAuth != 0 : iType == EBtHidKeyboard || ( iType == EBtHidGamepad && !CallsBack());
	sec.SetAuthentication( auth );
	sec.SetEncryption( auth && !IsWii());
	sec.SetAuthorisation( EFalse );
	addr.SetBTAddr( iAddr );
	addr.SetPort( aPsm );
	addr.SetSecurity( sec );
	aSock.Connect( addr, iStatus );
	SetActive();
}

void CBtHidDevice::Retry( TInt aErr )
{
	ReleaseAll();
	CloseSockets();
	iError = aErr;
	TBool dropped = iConnected;
	if( iConnected )
	{
		Log( "desconectado", aErr );
		iConnected = EFalse;
		iFails = 0;
	}
	if(( iFlags & EFlagReconnSelf ) && iType != EBtHidGamepad )
	{
		iState = EWaitDevice; // se reconecta solo: esperarlo
		return;
	}
	else if( iFails < 20 ) // cada intento (despues se calla, para no llenar el log con un mouse apagado)
		Log( iState == EConnCtrl ? "fallo el canal de control" : iState == EConnIntr ? "fallo el canal de datos"
			: iState == EWaitIntr ? "llego el control pero no los datos" : "fallo la conexion", aErr );
	iFails++;
	/* el Wiimote contesto pero no anduvo (o se corto enseguida: el Plus sin codigo abre los canales y se apaga): el
	   proximo intento, de otra forma (sin codigo, el de 1+2, el del boton rojo). Si no contesto (apagado, o no se
	   apreto 1+2) no se sabe nada: se sigue igual */
	TBool wiiNext = IsWii() && !iWiiAuthOk && aErr != KHciPageTimeout && aErr != KErrCouldNotConnect;
	if( wiiNext )
	{
		if(( aErr == KHciAuthFailure || aErr == KHciKeyMissing || aErr == KL2capSecurity ) && iWiiAuth )
			iWiiUnpair = ETrue;
		iWiiAuth = ( iWiiAuth + 1 ) % 3;
		Log( iWiiAuth == 0 ? "wiimote: el proximo, sin codigo" : iWiiAuth == 1 ? "wiimote: el proximo, con el codigo de 1+2"
			: "wiimote: el proximo, con el codigo del boton rojo", aErr );
	}
	/* desconectado a mano: no se lo llama, pero si llama el se lo atiende */
	if( iStopped )
	{
		iState = EWaitDevice;
		return;
	}
	/* el joystick de PS3 no contesta cuando se lo llama: al apretar PS llama el. Recien cortado se lo llama un par
	   de veces (por si fue un corte con el prendido); despues se lo espera con la radio libre */
	if( CallsBack() && iFails >= 3 )
	{
		iState = EWaitDevice;
		if( iFails == 3 )
			Log( "esperando que llame (apretar PS)", KErrNone );
		return;
	}
	iState = EWaitRetry;
	if( !iTimerOpen )
		return;
	// recien cortado el dispositivo esta despierto: volver a llamarlo ya
	/* un joystick de PS4 al apretar PS espera un rato que lo llame el telefono: se lo llama cada 4 s, siempre
	   (cada intento ocupa la radio ~5 s: un intervalo largo dejaba pasar ese rato y no reconectaba) */
	/* (el Wiimote con 1+2 espera solo unos 20 s: la otra forma de llamarlo, enseguida) */
	if( wiiNext )
		iTimer.After( iStatus, 1000000 );
	else if( iType == EBtHidGamepad )
		iTimer.After( iStatus, dropped ? 500000 : KRetryPadUs );
	else
		iTimer.After( iStatus, dropped ? 500000 : iFails < 3 ? KRetryFirstUs : iFails < 6 ? KRetryUs : KRetrySlowUs );
	SetActive();
}

void CBtHidDevice::RunL()
{
	TInt err = iStatus.Int();

	switch( iState )
	{
	case EWaitRetry:
		Start();
		break;
	case EConnCtrl:
		if( err != KErrNone ) { Retry( err ); break; }
		iState = EConnIntr;
		ConnectChannel( iIntr, KHidIntrPsm );
		break;
	case EConnIntr:
		if( err != KErrNone ) { Retry( err ); break; }
		SetProtocol();
		break;
	case EWaitIntr:
		Retry( KErrTimedOut ); // llego el canal de control pero nunca el de datos
		break;
	case ESetProto:
		// si el SET_PROTOCOL falla igual leemos: los mouse simples ya mandan el mismo formato
		iConnected = ETrue;
		iFails = 0;
		iError = KErrNone;
		iRawLogged = 0;
		Log( iType == EBtHidGamepad ? "conectado" : iProtoBoot ? "conectado (protocolo boot)" : "conectado (reportes del descriptor)", err );
		iPadReports = 0;
		iPadFullAsked = EFalse;
		iPadFullSeen = EFalse;
		iPadBattery = -1;
		iPadBatLogged = -1;
		if( iType == EBtHidGamepad )
		{
			// sin reportes todavia: los sticks al centro y nada apretado (no "clavado" arriba a la izquierda)
			Mem::Fill( iPadAxis, sizeof( iPadAxis ), 0x80 );
			iPadButtons = 0;
			Mem::FillZ( iPadPressure, sizeof( iPadPressure ));
			iHost.AssignPlayer( this );
			if( IsWii())
				WiiStart(); // (la luz, la camara, el MotionPlus y el modo de reportes)
			else if( !CallsBack())
			{
				// los de PS4: el original no manda nada hasta que se le habla. El pedido del reporte completo y la
				// luz en su formato lo despiertan (a los compatibles no les molesta)
				iPadKind = EBtHidPadPs4;
				PadAskFull();
			}
			if( !IsWii())
				PadOutput(); // la luz de su numero
		}
		LogDescriptor();
		iState = EReading;
		iIntr.Recv( iBuf, 0, iStatus );
		SetActive();
		StartSdp();
		break;
	case EReading:
		if( err != KErrNone ) { Retry( err ); break; }
		Report( iBuf );
		iIntr.Recv( iBuf, 0, iStatus );
		SetActive();
		break;
	default:
		break;
	}
}

void CBtHidDevice::SetProtocol()
{
	if( IsWii())
	{
		/* el Wiimote no usa el canal de control: directo a conectado (WiiStart le habla por el de datos) */
		TRequestStatus *st = &iStatus;
		iProtoBoot = EFalse;
		iState = ESetProto;
		SetActive();
		User::RequestComplete( st, KErrNone );
		return;
	}
	if( iType == EBtHidGamepad )
	{
		/* joystick: sin SET_PROTOCOL (el boot es de mouse y teclado). El DualShock 3 no manda nada hasta que
		   se lo activa con el feature 0xF4; al DualShock 4 y los genericos de PS4 no les hace nada */
		static const TUint8 enable[6] = { 0x53, 0xF4, 0x42, 0x03, 0x00, 0x00 };
		iProtoBoot = EFalse;
		iPadCmd.Copy( enable, sizeof( enable ));
		iState = ESetProto;
		iCtrl.Write( iPadCmd, iStatus );
		SetActive();
		return;
	}
	iProtoBoot = !iLayout.iValid;
	iCmd.SetLength( 1 );
	iCmd[0] = iProtoBoot ? KHidpSetProtoBoot : KHidpSetProtoReport;
	iState = ESetProto;
	iCtrl.Write( iCmd, iStatus );
	SetActive();
}

/* conexion entrante del dispositivo (CBtHidListener): primero llega el canal de control y despues el de datos */
void CBtHidDevice::AdoptCtrl( RSocket &aSock )
{
	Log( "llamo: canal de control", KErrNone );
	iStopped = EFalse; // (desconectado a mano y llamo el: se apreto PS, se lo quiere)
	Cancel();
	ReleaseAll();
	CloseSockets();
	iCtrl = aSock;
	iCtrlOpen = ETrue;
	iState = EWaitIntr;
	if( !iTimerOpen )
		return;
	iTimer.After( iStatus, KWaitIntrUs );
	SetActive();
}

void CBtHidDevice::AdoptIntr( RSocket &aSock )
{
	Log( "llamo: canal de datos", KErrNone );
	if( iState != EWaitIntr ) // datos sin control: raro, pero lo usamos igual
	{
		Cancel();
		ReleaseAll();
		CloseSockets();
	}
	else
		Cancel(); // el timer
	iIntr = aSock;
	iIntrOpen = ETrue;
	// cuando llama el dispositivo ya arranca en modo reportes (el que describe el descriptor): mandarle
	// SET_PROTOCOL ahi cortaba la conexion (-18). Solo si no tenemos descriptor le pedimos boot
	if( iCtrlOpen && ( !iLayout.iValid || iType == EBtHidGamepad ))
	{
		SetProtocol();
		return;
	}
	iConnected = ETrue;
	iFails = 0;
	iError = KErrNone;
	iRawLogged = 0;
	iProtoBoot = !iLayout.iValid;
	Log( "conectado (llamo el dispositivo)", KErrNone );
	LogDescriptor();
	iState = EReading;
	iIntr.Recv( iBuf, 0, iStatus );
	SetActive();
}

TInt CBtHidDevice::RunError( TInt aError )
{
	Retry( aError );
	return KErrNone;
}

void CBtHidDevice::DoCancel()
{
	if( iState == EWaitDevice )
		return; // nada pendiente
	if( iState == EWaitRetry || iState == EWaitIntr )
	{
		iTimer.Cancel();
		return;
	}
	if( iCtrlOpen ) iCtrl.CancelAll();
	if( iIntrOpen ) iIntr.CancelAll();
}

/* la primera vez que conecta: pedir el descriptor para saber el formato de sus reportes */
void CBtHidDevice::StartSdp()
{
	if(( iDesc.Length() && ( iFlags & EFlagSdpKnown )) || iSdp || iSdpTried )
		return;
	iSdpTried = ETrue;
	iSdp = new CBtHidSdp( *this );
	if( !iSdp )
		return;
	TRAPD( err, iSdp->StartL( iAddr ));
	if( err != KErrNone )
		Log( "no se pudo pedir el descriptor HID", err );
}

void CBtHidDevice::DescriptorDone( TInt aError, const TDesC8 &aDesc, TInt aFlags )
{
	TBuf8<80> msg;

	if( aError != KErrNone || !aDesc.Length())
	{
		Log( "sin descriptor HID, queda el protocolo boot", aError );
		return;
	}
	iFlags = ( aFlags & ~EFlagWiiSideways ) | ( iFlags & EFlagWiiSideways ); // (el giro lo elige la app, no el SDP)
	msg.Format( _L8( "SDP: se reconecta solo %d, contesta si lo llaman %d" ),
		( iFlags & EFlagReconnSelf ) != 0, ( iFlags & EFlagNormConnect ) != 0 );
	iHost.Observer().BtHidLog( (const char *)msg.PtrZ());
	if( !iDesc.Length())
	{
		iDesc.Copy( aDesc.Left( iDesc.MaxLength()));
		HidParseDescriptor( iDesc, iLayout );
		LogLayout();
	}
	iHost.Save(); // la proxima conexion ya usa los reportes del descriptor

	/* la primera vez el descriptor llega con el mouse ya conectado en boot, y en boot no manda la rueda:
	   pasarlo ya a los reportes del descriptor (en el canal de control, aparte de la lectura que esta
	   esperando en el de datos) */
	if( iConnected && iProtoBoot && iLayout.iValid && iCtrlOpen )
	{
		TBuf8<1>       cmd;
		TRequestStatus st;
		cmd.SetLength( 1 );
		cmd[0] = KHidpSetProtoReport;
		iCtrl.Write( cmd, st );
		User::WaitForRequest( st );
		if( st.Int() == KErrNone )
			iProtoBoot = EFalse;
		Log( "paso a los reportes del descriptor (rueda)", st.Int());
	}
}

/* el descriptor en hexa, una vez por arranque del servidor */
void CBtHidDevice::LogDescriptor()
{
	TBuf8<160> msg;
	TInt i;

	if( iDescLogged || !iDesc.Length())
		return;
	iDescLogged = ETrue;
	for( i = 0; i < iDesc.Length(); i++ )
	{
		if( i % 40 == 0 )
		{
			if( i )
				iHost.Observer().BtHidLog( (const char *)msg.PtrZ());
			msg.Format( _L8( "desc %03d:" ), i );
		}
		msg.AppendFormat( _L8( " %02x" ), iDesc[i] );
	}
	iHost.Observer().BtHidLog( (const char *)msg.PtrZ());
	LogLayout();
}

void CBtHidDevice::LogLayout()
{
	TBuf8<120> msg;
	msg.Format( _L8( "descriptor HID %d bytes: ids %d, x id %d bit %d/%d, y bit %d/%d, rueda id %d bit %d/%d, botones id %d bits %d %d %d (%d %d %d)" ),
		iDesc.Length(), iLayout.iUsesIds, iLayout.iX.iReportId, iLayout.iX.iBit, iLayout.iX.iBits, iLayout.iY.iBit, iLayout.iY.iBits,
		iLayout.iWheel.iReportId, iLayout.iWheel.iBit, iLayout.iWheel.iBits, iLayout.iButton[0].iReportId,
		iLayout.iButton[0].iBit, iLayout.iButton[1].iBit, iLayout.iButton[2].iBit,
		iLayout.iButton[0].iBits, iLayout.iButton[1].iBits, iLayout.iButton[2].iBits );
	iHost.Observer().BtHidLog( (const char *)msg.PtrZ());
	if( iLayout.iKeys.iBits )
	{
		msg.Format( _L8( "teclado: id %d, modificadores bit %d, %d teclas desde el bit %d" ),
			iLayout.iKeys.iReportId, iLayout.iMods.iBit, iLayout.iKeyCount, iLayout.iKeys.iBit );
		iHost.Observer().BtHidLog( (const char *)msg.PtrZ());
	}
}

void CBtHidDevice::Report( const TDesC8 &aData )
{
	// los primeros reportes de cada conexion van al log en hexa: cada dispositivo tiene su formato
	if( iRawLogged < 8 )
	{
		TBuf8<120> msg;
		TInt i;
		msg.Format( _L8( "reporte %d bytes:" ), aData.Length());
		for( i = 0; i < aData.Length() && i < 24; i++ )
			msg.AppendFormat( _L8( " %02x" ), aData[i] );
		iHost.Observer().BtHidLog( (const char *)msg.PtrZ());
		iRawLogged++;
	}
	if( aData.Length() < 2 || aData[0] != KHidpDataInput )
		return;
	if( IsWii())
	{
		WiiReport( aData );
		return;
	}
	if( iType == EBtHidGamepad )
	{
		PadReport( aData );
		return;
	}
	if( iLayout.iValid && LayoutReport( aData ))
		return;
	if( iLayout.iValid && !iProtoBoot )
		return; // en modo reportes, uno que el descriptor no conoce no es de mouse ni de teclado
	BootReport( aData );
}

/* un reporte segun el descriptor. EFalse si no es uno de los que conoce (numero o tamano) */
TBool CBtHidDevice::LayoutReport( const TDesC8 &aData )
{
	TInt  id = 0, start = 1, bytes, b, dx = 0, dy = 0, wheel = 0, buttons = iButtons;
	TBool mouse = EFalse;

	if( iLayout.iUsesIds )
	{
		id = aData[1];
		start = 2;
	}
	// un reporte que el descriptor no conoce no es nuestro. Mas corto que lo declarado si vale: hay mouse que no
	// mandan el ultimo byte (la rueda) si no se movio; lo que falta queda en 0 (HidField)
	bytes = ( iLayout.iReportBits[id] + 7 ) / 8;
	if( !bytes || aData.Length() <= start )
		return EFalse;
	TPtrC8 p = aData.Mid( start );

	for( b = 0; b < 3; b++ )
	{
		const THidField &f = iLayout.iButton[b];
		if( !f.iBits || f.iReportId != id )
			continue;
		mouse = ETrue;
		if( HidField( p, f ))
			buttons |= 1 << b;
		else
			buttons &= ~( 1 << b );
	}
	if( iLayout.iX.iBits && iLayout.iX.iReportId == id ) { mouse = ETrue; dx = HidField( p, iLayout.iX ); }
	if( iLayout.iY.iBits && iLayout.iY.iReportId == id ) { mouse = ETrue; dy = HidField( p, iLayout.iY ); }
	if( iLayout.iWheel.iBits && iLayout.iWheel.iReportId == id ) { mouse = ETrue; wheel = HidField( p, iLayout.iWheel ); }
	if( iOtherLogged < 30 && id != iLayout.iX.iReportId )
	{
		TBuf8<120> msg;
		TInt i;
		iOtherLogged++;
		msg.Format( _L8( "reporte %d:" ), id );
		for( i = 0; i < aData.Length() && i < 24; i++ )
			msg.AppendFormat( _L8( " %02x" ), aData[i] );
		iHost.Observer().BtHidLog( (const char *)msg.PtrZ());
	}
	// cada cambio del primer byte del reporte (los botones en casi todos los mouse) va al log, con el reporte
	if( iBtnLogged < 30 && p.Length() && p[0] != iLastBtnByte )
	{
		TBuf8<120> msg;
		TInt i;
		iLastBtnByte = p[0];
		iBtnLogged++;
		msg.Format( _L8( "botones %d -> mascara %d:" ), id, buttons );
		for( i = 0; i < aData.Length() && i < 24; i++ )
			msg.AppendFormat( _L8( " %02x" ), aData[i] );
		iHost.Observer().BtHidLog( (const char *)msg.PtrZ());
	}
	if( mouse )
	{
		iButtons = buttons;
		iHost.Observer().BtHidMouse( dx, dy, wheel, buttons );
	}

	if( iLayout.iKeys.iBits && iLayout.iKeys.iReportId == id )
	{
		TUint8 mods = iMods, keys[6];
		TInt   k;
		if( iLayout.iMods.iBits && iLayout.iMods.iReportId == id )
			mods = (TUint8)HidField( p, iLayout.iMods );
		for( k = 0; k < 6; k++ )
			keys[k] = k < iLayout.iKeyCount ? (TUint8)HidField( p, iLayout.iKeys, k ) : 0;
		KeyboardState( mods, keys );
	}
	return ETrue;
}

void CBtHidDevice::BootReport( const TDesC8 &aData )
{
	// tipo sin saber (el cuadro no dio la clase del dispositivo): el reporte boot del teclado es de 9 bytes
	if( !iType )
		iType = ( aData.Length() == 9 && aData[2] == 0 ) ? EBtHidKeyboard : EBtHidMouse;

	if( iType == EBtHidKeyboard )
	{
		if( aData.Length() >= 9 )
			KeyboardState( aData[1], aData.Ptr() + 3 );
		return;
	}
	if( aData.Length() < 4 )
		return;
	iButtons = aData[1] & 7;
	iHost.Observer().BtHidMouse( (TInt8)aData[2], (TInt8)aData[3], aData.Length() >= 5 ? (TInt8)aData[4] : 0, iButtons );
}

void CBtHidDevice::KeyboardState( TUint8 aMods, const TUint8 *aKeys )
{
	TUint8 keys[6];
	TInt   i, j;

	Mem::Copy( keys, aKeys, sizeof( keys ));
	if( keys[0] == 1 ) // ErrorRollOver: demasiadas teclas juntas, el reporte no dice cuales
		return;

	for( i = 0; i < 8; i++ )
		if(( aMods ^ iMods ) & ( 1 << i ))
			iHost.Observer().BtHidKey( 224 + i, ( aMods >> i ) & 1 );

	for( i = 0; i < 6; i++ ) // soltadas
	{
		if( iKeys[i] <= 1 ) continue;
		for( j = 0; j < 6 && keys[j] != iKeys[i]; j++ ) ;
		if( j == 6 ) iHost.Observer().BtHidKey( iKeys[i], EFalse );
	}
	for( i = 0; i < 6; i++ ) // apretadas
	{
		if( keys[i] <= 1 ) continue;
		for( j = 0; j < 6 && iKeys[j] != keys[i]; j++ ) ;
		if( j == 6 ) iHost.Observer().BtHidKey( keys[i], ETrue );
	}

	iMods = aMods;
	Mem::Copy( iKeys, keys, sizeof( iKeys ));
}

/* ------------------------------------------------------------------ */

/* escucha un canal HID (control o datos) para las conexiones que inicia el dispositivo */
/* ---------------------------------------------------------------- joysticks
   Reportes (canal de datos, A1 = DATA | INPUT):
     DualShock 3:  A1 01 + 48 bytes. [3] select L3 R3 start arriba derecha abajo izquierda, [4] L2 R2 L1 R1 triangulo
                   circulo cruz cuadrado, [5] bit 0 PS, [7..10] LX LY RX RY, [15..26] la presion de arriba, derecha,
                   abajo, izquierda, L2, R2, L1, R1, triangulo, circulo, cruz, cuadrado.
     DualShock 4 y genericos de PS4, el corto: A1 01 + 9 bytes. [2..5] LX LY RX RY, [6] la cruceta en los 4 bits
                   bajos (0 = arriba y sigue en sentido horario, 8 = suelta) y cuadrado cruz circulo triangulo en los
                   altos, [7] L1 R1 L2 R2 share options L3 R3, [8] bit 0 PS, [9] [10] la presion de L2 y R2.
     DualShock 4, el completo: A1 11 C0 00 + lo mismo desde el stick izquierdo. */
static void PadDpad( TUint32 &aButtons, TInt aHat )
{
	static const TUint8 dirs[8] = { 1, 3, 2, 6, 4, 12, 8, 9 }; // bits arriba(1) derecha(2) abajo(4) izquierda(8)
	if( aHat < 0 || aHat > 7 )
		return;
	if( dirs[aHat] & 1 ) aButtons |= 1 << EBtHidPadUp;
	if( dirs[aHat] & 2 ) aButtons |= 1 << EBtHidPadRight;
	if( dirs[aHat] & 4 ) aButtons |= 1 << EBtHidPadDown;
	if( dirs[aHat] & 8 ) aButtons |= 1 << EBtHidPadLeft;
}

/* DualShock 4: pedirle el feature 0x02 (la calibracion) lo pasa del reporte corto (sin bateria) al completo. El
   original ademas no manda nada hasta que se le habla: por eso se pide tambien al conectar. Una vez por conexion */
void CBtHidDevice::PadAskFull()
{
	static const TUint8 get[2] = { 0x43, 0x02 }; // GET_REPORT feature 0x02
	TRequestStatus st;

	if( iPadFullAsked || !iConnected || !iCtrlOpen )
		return;
	iPadFullAsked = ETrue;
	iPadCmd.Copy( get, sizeof( get ));
	iCtrl.Write( iPadCmd, st );
	User::WaitForRequest( st );
	Log( "pidiendo el reporte completo (bateria)", st.Int());
	/* la respuesta (por el canal de control): al log, para saber si este joystick lo tiene (algunos compatibles no).
	   Se espera hasta 300 ms */
	if( iTimerOpen )
	{
		TBuf8<64>      resp;
		TSockXfrLength len;
		TRequestStatus rst, tst;
		RTimer         t;
		if( t.CreateLocal() == KErrNone )
		{
			iCtrl.RecvOneOrMore( resp, 0, rst, len );
			t.After( tst, 300000 );
			User::WaitForRequest( rst, tst );
			if( rst == KRequestPending )
			{
				iCtrl.CancelRecv();
				User::WaitForRequest( rst );
				Log( "sin respuesta al pedido del reporte completo", KErrTimedOut );
			}
			else
			{
				t.Cancel();
				User::WaitForRequest( tst );
				TBuf8<120> msg;
				msg.Format( _L8( "respuesta %d bytes:" ), resp.Length());
				for( TInt k = 0; k < resp.Length() && k < 16; k++ )
					msg.AppendFormat( _L8( " %02x" ), resp[k] );
				iHost.Observer().BtHidLog( (const char *)msg.PtrZ());
			}
			t.Close();
		}
	}
}

void CBtHidDevice::PadReport( const TDesC8 &aData )
{
	TUint32 b = 0;
	TInt    i, kind = iPadKind;
	const TUint8 *p = aData.Ptr();
	TInt    n = aData.Length();

	/* un Wiimote sin nombre (el cuadro no lo dio): sus reportes son del 0x20 al 0x37, que los DualShock no usan */
	if( n <= 23 && p[1] >= 0x20 && p[1] <= 0x37 && iPadKind != EBtHidPadPs3 )
	{
		Log( "es un wiimote", p[1] );
		if( !iName.Length())
		{
			iName.Copy( _L8( "Nintendo RVL-CNT-01" ));
			iHost.Save();
		}
		WiiStart();
		WiiReport( aData );
		return;
	}
	if( n >= 27 && p[1] == 0x01 && n > 11 )
	{
		/* DualShock 3 */
		static const TUint8 bits1[8] = { EBtHidPadSelect, EBtHidPadL3, EBtHidPadR3, EBtHidPadStart,
			EBtHidPadUp, EBtHidPadRight, EBtHidPadDown, EBtHidPadLeft };
		static const TUint8 bits2[8] = { EBtHidPadL2, EBtHidPadR2, EBtHidPadL1, EBtHidPadR1,
			EBtHidPadTriangle, EBtHidPadCircle, EBtHidPadCross, EBtHidPadSquare };
		static const TUint8 press[12] = { EBtHidPadUp, EBtHidPadRight, EBtHidPadDown, EBtHidPadLeft,
			EBtHidPadL2, EBtHidPadR2, EBtHidPadL1, EBtHidPadR1, EBtHidPadTriangle, EBtHidPadCircle,
			EBtHidPadCross, EBtHidPadSquare };
		iPadKind = EBtHidPadPs3;
		for( i = 0; i < 8; i++ )
		{
			if( p[3] & ( 1 << i )) b |= 1 << bits1[i];
			if( p[4] & ( 1 << i )) b |= 1 << bits2[i];
		}
		if( p[5] & 1 ) b |= 1 << EBtHidPadPS;
		for( i = 0; i < 4; i++ )
			iPadAxis[i] = p[7 + i];
		/* la bateria: [31] 0..5 (0xEE cargando, 0xEF cargado) */
		if( n > 31 )
		{
			TInt v = p[31];
			iPadCharging = v == 0xEE || v == 0xEF;
			iPadBattery = v == 0xEF ? 100 : v == 0xEE ? -1 : v <= 5 ? v * 20 : -1;
		}
		Mem::FillZ( iPadPressure, sizeof( iPadPressure ));
		for( i = 0; i < 12; i++ )
			iPadPressure[press[i]] = p[15 + i];
	}
	else if( iLayout.iUsesIds && iLayout.iHat.iBits && iLayout.iPadBtn.iBits && n >= 3 && p[1] != 0x01 && p[1] != 0x11
		&& p[1] == iLayout.iPadBtn.iReportId )
	{
		/* UN JOYSTICK GENERICO (los Bluetooth de Android: no son DualShock): se lee con su descriptor HID. Los ejes X Y
		   (el stick izquierdo) y Z Rz (el derecho), la cruceta (el hat) y los botones en el orden de Android: 1 A (la
		   cruz), 2 B (el circulo), 4 X (el cuadrado), 5 Y (el triangulo), 7 / 8 L1 / R1, 9 / 10 L2 / R2, 11 Select,
		   12 Start, 13 Home (PS), 14 / 15 L3 / R3. (Si alguno no coincide, el juego deja cambiarlo) */
		static const TInt8 btn[16] = { EBtHidPadCross, EBtHidPadCircle, -1, EBtHidPadSquare, EBtHidPadTriangle, -1,
			EBtHidPadL1, EBtHidPadR1, EBtHidPadL2, EBtHidPadR2, EBtHidPadSelect, EBtHidPadStart, EBtHidPadPS,
			EBtHidPadL3, EBtHidPadR3, -1 };
		const TPtrC8 datos = aData.Mid( 2 );
		const THidField *ejes[4] = { &iLayout.iX, &iLayout.iY, &iLayout.iZ, &iLayout.iRz };
		if( iPadKind != EBtHidPadUnknown )
			Log( "joystick generico: se lee con su descriptor", p[1] );
		iPadKind = EBtHidPadUnknown;
		for( i = 0; i < 4; i++ )
		{
			const THidField &f = *ejes[i];
			TInt v = 0x80;
			if( f.iBits && f.iReportId == p[1] )
			{
				v = HidField( datos, f );
				if( f.iBits > 8 ) v >>= f.iBits - 8;      // (a 8 bits)
				else if( f.iBits < 8 ) v <<= 8 - f.iBits;
				if( f.iSigned ) v += 0x80;                // (los que van de -128 a 127)
				if( v < 0 ) v = 0;
				if( v > 255 ) v = 255;
			}
			iPadAxis[i] = (TUint8)v;
		}
		PadDpad( b, HidField( datos, iLayout.iHat ));
		for( i = 0; i < iLayout.iPadBtnCount && i < 16; i++ )
			if( btn[i] >= 0 && HidField( datos, iLayout.iPadBtn, i ))
				b |= 1 << btn[i];
		for( i = 0; i < EBtHidPadButtons; i++ )
			iPadPressure[i] = ( b & ( 1 << i )) ? 255 : 0;
	}
	else
	{
		/* DualShock 4: el corto (A1 01, 9 bytes) o el completo (A1 11 C0 00 ...) */
		const TUint8 *q;
		if( p[1] == 0x01 && n >= 11 )
			PadAskFull();
		if( p[1] == 0x11 && !iPadFullSeen )
		{
			TBuf8<120> msg;
			iPadFullSeen = ETrue;
			msg.Format( _L8( "reporte completo %d bytes:" ), n );
			for( TInt k = 0; k < n && k < 40; k++ )
				msg.AppendFormat( _L8( " %02x" ), p[k] );
			iHost.Observer().BtHidLog( (const char *)msg.PtrZ());
		}
		if( p[1] == 0x01 && n >= 11 ) q = p + 2;
		else if( p[1] == 0x11 && n >= 13 ) q = p + 4;
		else return;
		iPadKind = EBtHidPadPs4;
		for( i = 0; i < 4; i++ )
			iPadAxis[i] = q[i];
		PadDpad( b, q[4] & 0x0F );
		if( q[4] & 0x10 ) b |= 1 << EBtHidPadSquare;
		if( q[4] & 0x20 ) b |= 1 << EBtHidPadCross;
		if( q[4] & 0x40 ) b |= 1 << EBtHidPadCircle;
		if( q[4] & 0x80 ) b |= 1 << EBtHidPadTriangle;
		{
			static const TUint8 bits[8] = { EBtHidPadL1, EBtHidPadR1, EBtHidPadL2, EBtHidPadR2,
				EBtHidPadSelect, EBtHidPadStart, EBtHidPadL3, EBtHidPadR3 };
			for( i = 0; i < 8; i++ )
				if( q[5] & ( 1 << i )) b |= 1 << bits[i];
		}
		if( q[6] & 1 ) b |= 1 << EBtHidPadPS;
		/* la bateria (solo en el completo): los 4 bits bajos de [29] de 0 a 10 (11 cargado), el bit 4 enchufado */
		if( p[1] == 0x11 && n >= 4 + 30 )
		{
			TInt v = q[29];
			TInt level = v & 0x0F;
			iPadCharging = ( v & 0x10 ) != 0;
			iPadBattery = level >= 10 ? 100 : level * 10;
		}
		/* presion: la de L2 y R2 es analogica; los demas, 255 apretado */
		for( i = 0; i < EBtHidPadButtons; i++ )
			iPadPressure[i] = ( b & ( 1 << i )) ? 255 : 0;
		iPadPressure[EBtHidPadL2] = q[7];
		iPadPressure[EBtHidPadR2] = q[8];
	}

	if( b != iPadButtons && iBtnLogged < 40 )
	{
		TBuf8<48> msg;
		msg.Format( _L8( "botones %08x" ), b );
		Log( (const char *)msg.PtrZ(), iPadKind );
		iBtnLogged++;
	}
	iPadButtons = b;
	iPadReports++;
	/* la bateria al log cuando cambia de a 10 % (para saber si un joystick se apago por la bateria) */
	if( iPadBattery >= 0 && ( iPadBatLogged < 0 || iPadBattery / 10 != iPadBatLogged / 10 ))
	{
		iPadBatLogged = iPadBattery;
		Log( iPadCharging ? "bateria (cargando)" : "bateria", iPadBattery );
	}
	PadKeys();
	if( iPadKind != kind )
		PadOutput(); // recien se sabe cual es: su luz (el DualShock 4 la quiere en su formato)
}

/* modo teclado: las teclas de los botones apretados y del stick izquierdo (como flechas, con un margen para que no
   titile en el borde). Se mandan solo los cambios; dos botones con la misma tecla cuentan una vez */
void CBtHidDevice::PadKeys()
{
	TUint8 want[CBtHidHost::EPadKeyInputs];
	TInt   n = 0, i, j, pad;

	if( !iHost.PadKeys())
	{
		ReleasePadKeys();
		return;
	}
	pad = iHost.PadIndex( this );
	if( pad < 0 || pad >= EBtHidMaxPads )
		return;

	/* el stick: entra a 64/192 y sale a 96/160 */
	{
		TInt s = iPadStick, x = iPadAxis[0], y = iPadAxis[1];
		if( y < 64 ) s |= 1; else if( y > 96 ) s &= ~1;
		if( x > 192 ) s |= 2; else if( x < 160 ) s &= ~2;
		if( y > 192 ) s |= 4; else if( y < 160 ) s &= ~4;
		if( x < 64 ) s |= 8; else if( x > 96 ) s &= ~8;
		iPadStick = s;
	}
	if( IsWii())
	{
		/* el Wiimote, sus teclas: Home menu, - y + las de abajo de la pantalla, A ok, B 1 2 los numeros 1 2 3. Con las
		   flechas giradas (la opcion de la app, para usarlo acostado): derecha es arriba. No depende de como se lo
		   tenga: girar el control no cambia las flechas */
		static const TUint8 keys[EBtHidWiiButtons] = { 82, 81, 80, 79, EBtHidPhoneOk, 30, 31, 32,
			EBtHidPhoneSoftLeft, EBtHidPhoneSoftRight, EBtHidPhoneMenu };
		static const TUint8 sideways[4] = { 80, 79, 81, 82 }; // arriba abajo izquierda derecha -> izq der abajo arriba
		const TBool rotated = WiiSideways();
		for( i = 0; i < EBtHidWiiButtons; i++ )
		{
			TInt k = ( iWiiButtons & ( 1u << i )) ? ( i < 4 && rotated ? sideways[i] : keys[i] ) : 0;
			if( !k ) continue;
			for( j = 0; j < n && want[j] != k; j++ ) {}
			if( j == n ) want[n++] = (TUint8)k;
		}
	}
	else
	for( i = 0; i < CBtHidHost::EPadKeyInputs; i++ )
	{
		TBool on = i < EBtHidPadButtons ? ( iPadButtons & ( 1u << i )) != 0 : ( iPadStick & ( 1 << ( i - EBtHidPadButtons ))) != 0;
		TInt  k = on ? iHost.PadKey( pad, i ) : 0;
		if( !k ) continue;
		for( j = 0; j < n && want[j] != k; j++ ) {}
		if( j == n ) want[n++] = (TUint8)k;
	}
	/* soltar las que ya no estan y apretar las nuevas */
	for( i = 0; i < iPadKeysCount; i++ )
	{
		for( j = 0; j < n && want[j] != iPadKeysDown[i]; j++ ) {}
		if( j == n ) iHost.Observer().BtHidPadKey( iPadKeysDown[i], EFalse );
	}
	for( i = 0; i < n; i++ )
	{
		for( j = 0; j < iPadKeysCount && iPadKeysDown[j] != want[i]; j++ ) {}
		if( j == iPadKeysCount ) iHost.Observer().BtHidPadKey( want[i], ETrue );
	}
	Mem::Copy( iPadKeysDown, want, n );
	iPadKeysCount = n;
}

void CBtHidDevice::ReleasePadKeys()
{
	TInt i;
	for( i = 0; i < iPadKeysCount; i++ )
		iHost.Observer().BtHidPadKey( iPadKeysDown[i], EFalse );
	iPadKeysCount = 0;
	iPadStick = 0;
}

/* CRC32 (el de zip) para el reporte de salida del DualShock 4 por Bluetooth */
static TUint32 PadCrc32( TUint32 aCrc, const TUint8 *aData, TInt aLen )
{
	TInt i, k;
	aCrc = ~aCrc;
	for( i = 0; i < aLen; i++ )
	{
		aCrc ^= aData[i];
		for( k = 0; k < 8; k++ )
			aCrc = ( aCrc >> 1 ) ^ ( 0xEDB88320u & ( 0u - ( aCrc & 1 )));
	}
	return ~aCrc;
}

/* la luz de su numero y la vibracion. DualShock 3: SET_REPORT output por el canal de control. DualShock 4: el
   reporte 0x11 por el canal de datos, con el color de su numero y el CRC32 al final (lo exige por Bluetooth).
   Mientras no se sabe cual es (antes del primer reporte) se manda el del 3, que los de PS4 ignoran */
void CBtHidDevice::PadOutput()
{
	static const TUint8 colors[4][3] = { { 0, 0, 64 }, { 64, 0, 0 }, { 0, 64, 0 }, { 64, 0, 32 } };
	TInt           pad = iHost.PadIndex( this );
	TRequestStatus st;

	if( !iConnected || !iCtrlOpen || !iIntrOpen )
		return;
	if( pad < 0 ) pad = 0;
	if( pad > 3 ) pad = 3;

	if( IsWii())
	{
		/* Wiimote: la luz de su numero (la vibracion va en el bit 0 de todos los reportes, WiiSend) */
		TUint8 r[2] = { 0x11, (TUint8)( 0x10 << pad ) };
		WiiSend( r, sizeof( r ));
		return;
	}
	// (un joystick generico no entiende la luz ni la vibracion de PlayStation: no se le manda nada)
	if( iPadKind == EBtHidPadUnknown )
		return;
	if( iPadKind == EBtHidPadPs4 )
	{
		TUint8  r[79];
		TUint32 crc;
		Mem::FillZ( r, sizeof( r ));
		r[0] = 0xA2;               // DATA | OUTPUT
		r[1] = 0x11;
		r[2] = 0xC0;               // HID + CRC
		r[4] = 0x07;               // motores + luz + parpadeo
		r[7] = iPadRumble ? 0xC0 : 0;  // motor chico
		r[8] = iPadRumble ? 0xFF : 0;  // motor grande
		r[9] = colors[pad][0];
		r[10] = colors[pad][1];
		r[11] = colors[pad][2];
		crc = PadCrc32( 0, r, 75 );  // del 0xA2 hasta antes del CRC
		r[75] = (TUint8)crc;
		r[76] = (TUint8)( crc >> 8 );
		r[77] = (TUint8)( crc >> 16 );
		r[78] = (TUint8)( crc >> 24 );
		iPadOut.Copy( r, sizeof( r ));
		iIntr.Write( iPadOut, st );
	}
	else
	{
		static const TUint8 led[5] = { 0xFF, 0x27, 0x10, 0x00, 0x32 };
		TUint8 r[50];
		TInt   i;
		Mem::FillZ( r, sizeof( r ));
		r[0] = 0x52;               // SET_REPORT output
		r[1] = 0x01;
		r[3] = iPadRumble ? 0xFE : 0;  // motor chico: duracion y prendido
		r[4] = iPadRumble ? 1 : 0;
		r[5] = iPadRumble ? 0xFE : 0;  // motor grande: duracion y fuerza
		r[6] = iPadRumble ? 0xFF : 0;
		r[11] = (TUint8)( 2 << pad ); // la luz 1..4
		for( i = 0; i < 4; i++ )
			Mem::Copy( r + 12 + i * 5, led, 5 );
		iPadOut.Copy( r, sizeof( r ));
		iCtrl.Write( iPadOut, st );
	}
	User::WaitForRequest( st );
}

void CBtHidDevice::SetRumble( TBool aOn )
{
	iPadRumble = aOn ? ETrue : EFalse;
	PadOutput();
	Log( iPadRumble ? "vibracion prendida" : "vibracion apagada", iPadKind );
}

void CBtHidDevice::PadState( TBtHidPadState &aState ) const
{
	aState.iKind = iPadKind ? iPadKind : EBtHidPadUnknown;
	aState.iState = State();
	aState.iButtons = iPadButtons;
	Mem::Copy( aState.iAxis, iPadAxis, sizeof( iPadAxis ));
	Mem::Copy( aState.iPressure, iPadPressure, sizeof( iPadPressure ));
	aState.iRumble = iPadRumble;
	aState.iReports = iPadReports;
	Mem::Copy( aState.iAddr, iAddr.Des().Ptr(), 6 );
}

/* ---------------------------------------------------------------- modo teclado
   c:\data\bthid_teclas.txt: un renglon "joystick boton tecla". Si no esta, se escribe con estas teclas (para
   emuladores y juegos retro) y se puede editar. El prendido/apagado se guarda en c:\private\e0d69650\teclas.dat */
_LIT( KPadKeysFile, "c:\\data\\bthid_teclas.txt" );
_LIT( KPadKeysOnFile, "c:\\private\\e0d69650\\teclas.dat" );
_LIT8( KPadKeysDefault,
	"# Joystick como teclado (Bluetooth HID 0.6): que tecla manda cada boton de cada joystick (1 a 4)\r\n"
	"# Se usa con Opciones > Joystick como teclado: si. Las teclas llegan a todas las apps y al menu del telefono\r\n"
	"# (menos a la que esta adelante si lee el joystick directo). Despues de editarlo, apagarlo y prenderlo.\r\n"
	"# Renglones: joystick boton tecla\r\n"
	"# Botones: arriba abajo izquierda derecha cruz circulo cuadrado triangulo l1 r1 l2 r2 select start l3 r3 ps\r\n"
	"#          stick_arriba stick_abajo stick_izquierda stick_derecha (el stick izquierdo)\r\n"
	"# Teclas: una letra o un numero, o enter esc espacio tab borrar arriba abajo izquierda derecha ctrl shift alt\r\n"
	"#         f1 ... f12 inicio fin pgarriba pgabajo insertar suprimir coma punto ninguna\r\n"
	"#         y las del telefono: ok soft_izq soft_der menu verde rojo tecla_c asterisco numeral\r\n"
	"1 arriba arriba\r\n1 abajo abajo\r\n1 izquierda izquierda\r\n1 derecha derecha\r\n"
	"1 stick_arriba arriba\r\n1 stick_abajo abajo\r\n1 stick_izquierda izquierda\r\n1 stick_derecha derecha\r\n"
	"1 cruz ok\r\n1 cuadrado 2\r\n1 circulo 3\r\n1 triangulo 4\r\n1 start 0\r\n1 r1 soft_der\r\n1 l1 soft_izq\r\n"
	"1 r2 5\r\n1 l2 6\r\n1 select 7\r\n1 ps menu\r\n1 r3 8\r\n1 l3 9\r\n"
	"2 arriba 8\r\n2 abajo 2\r\n2 izquierda 4\r\n2 derecha 6\r\n"
	"2 stick_arriba 8\r\n2 stick_abajo 2\r\n2 stick_izquierda 4\r\n2 stick_derecha 6\r\n"
	"2 cruz a\r\n2 cuadrado b\r\n2 circulo c\r\n2 triangulo d\r\n2 start e\r\n2 r1 f\r\n2 l1 g\r\n"
	"2 r2 h\r\n2 l2 i\r\n2 select j\r\n2 ps k\r\n2 r3 l\r\n2 l3 m\r\n"
	"3 arriba i\r\n3 abajo k\r\n3 izquierda j\r\n3 derecha l\r\n"
	"3 stick_arriba i\r\n3 stick_abajo k\r\n3 stick_izquierda j\r\n3 stick_derecha l\r\n"
	"3 cruz n\r\n3 cuadrado o\r\n3 circulo p\r\n3 triangulo q\r\n3 start r\r\n3 r1 s\r\n3 l1 t\r\n"
	"3 r2 u\r\n3 l2 v\r\n3 select w\r\n3 ps x\r\n3 r3 y\r\n3 l3 z\r\n"
	"4 arriba inicio\r\n4 abajo fin\r\n4 izquierda suprimir\r\n4 derecha pgabajo\r\n"
	"4 stick_arriba inicio\r\n4 stick_abajo fin\r\n4 stick_izquierda suprimir\r\n4 stick_derecha pgabajo\r\n"
	"4 cruz a\r\n4 cuadrado b\r\n4 circulo c\r\n4 triangulo d\r\n4 start e\r\n4 r1 f\r\n4 l1 g\r\n"
	"4 r2 h\r\n4 l2 i\r\n4 select j\r\n4 ps k\r\n4 r3 l\r\n4 l3 m\r\n" );

/* el archivo de las versiones 0.3 a 0.5 sin tocar (el joystick 1 con letras) o el de las primeras 0.6 (la cruz era
   el 1 y Start el OK; las flechas del joystick 2 pisaban sus botones): se cambia por el de ahora */
static TBool PadKeysViejo( const TDesC8 &aText )
{
	if( aText.Find( _L8( "1 cruz z" )) >= 0 && aText.Find( _L8( "1 circulo x" )) >= 0 && aText.Find( _L8( "1 ps esc" )) >= 0 )
		return ETrue;
	if( aText.Find( _L8( "1 cruz 1" )) >= 0 && aText.Find( _L8( "1 start ok" )) >= 0 )
		return ETrue;
	// (y el de la 0.6 con las flechas del joystick 2 en I K J L, que pisaban sus botones L2, Select, PS y R3)
	return aText.Find( _L8( "2 arriba i" )) >= 0 && aText.Find( _L8( "2 l2 i" )) >= 0;
}

static TInt PadInputByName( const TDesC8 &aName )
{
	static const char *const names[CBtHidHost::EPadKeyInputs] = { "arriba", "derecha", "abajo", "izquierda",
		"cruz", "circulo", "cuadrado", "triangulo", "l1", "r1", "l2", "r2", "select", "start", "l3", "r3", "ps",
		"stick_arriba", "stick_derecha", "stick_abajo", "stick_izquierda" };
	TInt i;
	for( i = 0; i < CBtHidHost::EPadKeyInputs; i++ )
		if( !aName.Compare( TPtrC8( (const TUint8 *)names[i] )))
			return i;
	return -1;
}

/* el uso HID de la pagina de teclado (el mismo que manda un teclado Bluetooth) */
static TInt PadKeyByName( const TDesC8 &aName )
{
	static const struct { const char *name; TUint8 usage; } keys[] = {
		{ "enter", 40 }, { "esc", 41 }, { "borrar", 42 }, { "tab", 43 }, { "espacio", 44 }, { "menos", 45 },
		{ "igual", 46 }, { "coma", 54 }, { "punto", 55 }, { "insertar", 73 }, { "inicio", 74 }, { "pgarriba", 75 },
		{ "suprimir", 76 }, { "fin", 77 }, { "pgabajo", 78 }, { "derecha", 79 }, { "izquierda", 80 }, { "abajo", 81 },
		{ "arriba", 82 }, { "ctrl", 224 }, { "shift", 225 }, { "alt", 226 }, { "ninguna", 0 },
		/* las del telefono (bthidmanager.h) */
		{ "ok", EBtHidPhoneOk }, { "soft_izq", EBtHidPhoneSoftLeft }, { "soft_der", EBtHidPhoneSoftRight },
		{ "menu", EBtHidPhoneMenu }, { "verde", EBtHidPhoneYes }, { "rojo", EBtHidPhoneNo },
		{ "tecla_c", EBtHidPhoneClear }, { "asterisco", EBtHidPhoneStar }, { "numeral", EBtHidPhoneHash } };
	TInt i;
	if( aName.Length() == 1 )
	{
		TUint c = aName[0];
		if( c >= 'a' && c <= 'z' ) return 4 + c - 'a';
		if( c >= '1' && c <= '9' ) return 30 + c - '1';
		if( c == '0' ) return 39;
	}
	if( aName.Length() >= 2 && aName.Length() <= 3 && aName[0] == 'f' )
	{
		TLex8 lex( aName.Mid( 1 ));
		TInt  f;
		if( lex.Val( f ) == KErrNone && f >= 1 && f <= 12 )
			return 58 + f - 1;
	}
	for( i = 0; i < (TInt)( sizeof( keys ) / sizeof( keys[0] )); i++ )
		if( !aName.Compare( TPtrC8( (const TUint8 *)keys[i].name )))
			return keys[i].usage;
	return -1;
}

void CBtHidHost::LoadPadKeys()
{
	RFs    fs;
	RFile  f;
	HBufC8 *buf = NULL;
	TInt   size = 0;

	Mem::FillZ( iPadKeyMap, sizeof( iPadKeyMap ));
	if( fs.Connect() != KErrNone )
		return;
	if( f.Open( fs, KPadKeysFile, EFileRead ) == KErrNone )
	{
		if( f.Size( size ) == KErrNone && size > 0 && size < 16384 && ( buf = HBufC8::New( size )) != NULL )
		{
			TPtr8 p = buf->Des();
			f.Read( p );
		}
		f.Close();
		if( buf && PadKeysViejo( *buf ))
		{
			delete buf;
			buf = NULL;
		}
	}
	if( !buf && f.Replace( fs, KPadKeysFile, EFileWrite ) == KErrNone )
	{
		/* la primera vez (o el de una version vieja sin tocar): el archivo con las teclas de ahora, para editarlo */
		f.Write( KPadKeysDefault );
		f.Close();
		buf = KPadKeysDefault().Alloc();
	}
	BtHidCloseFs( fs );
	if( !buf )
		return;

	TLex8 lines( *buf );
	while( !lines.Eos())
	{
		lines.Mark();
		while( !lines.Eos() && lines.Peek() != '\n' )
			lines.Inc();
		TPtrC8 line = lines.MarkedToken();
		if( !lines.Eos())
			lines.Inc();
		TLex8 w( line );
		TPtrC8 a = w.NextToken(), b = w.NextToken(), c = w.NextToken();
		TBuf8<24> bn, cn;
		TInt pad, in, key;
		if( !a.Length() || a[0] == '#' || !b.Length() || !c.Length())
			continue;
		pad = a[0] - '1';
		bn.Copy( b.Left( bn.MaxLength()));
		cn.Copy( c.Left( cn.MaxLength()));
		bn.LowerCase();
		cn.LowerCase();
		in = PadInputByName( bn );
		key = PadKeyByName( cn );
		if( pad >= 0 && pad < EBtHidMaxPads && in >= 0 && key >= 0 )
			iPadKeyMap[pad][in] = (TUint8)key;
	}
	delete buf;
}

void CBtHidHost::SetPadKeys( TBool aOn )
{
	RFs   fs;
	RFile f;
	TInt  i;

	iPadKeys = aOn ? ETrue : EFalse;
	if( iPadKeys )
		LoadPadKeys(); // por si se edito el archivo
	else
		for( i = 0; i < EMaxDevices; i++ )
			if( iDev[i] )
				iDev[i]->ReleasePadKeys();
	iObserver.BtHidLog( iPadKeys ? "joysticks como teclado: si" : "joysticks como teclado: no" );
	if( fs.Connect() == KErrNone )
	{
		if( f.Replace( fs, KPadKeysOnFile, EFileWrite ) == KErrNone )
		{
			TBuf8<1> b;
			b.Append( (TUint8)iPadKeys );
			f.Write( b );
			f.Close();
		}
		BtHidCloseFs( fs );
	}
}

TInt CBtHidHost::PadIndex( const CBtHidDevice *aDev ) const
{
	return aDev && aDev->Player() > 0 ? aDev->Player() - 1 : -1;
}

/* recien conectado: si no tenia numero (primera vez desde que arranco el servidor), el mas chico libre */
void CBtHidHost::AssignPlayer( CBtHidDevice *aDev )
{
	TInt n, i;
	if( aDev->Player() > 0 )
		return;
	for( n = 1; n <= EBtHidMaxPads; n++ )
	{
		for( i = 0; i < EMaxDevices; i++ )
			if( iDev[i] && iDev[i] != aDev && iDev[i]->Type() == EBtHidGamepad && iDev[i]->Player() == n )
				break;
		if( i == EMaxDevices )
		{
			aDev->SetPlayer( n );
			iObserver.BtHidLog( n == 1 ? "joystick: jugador 1" : n == 2 ? "joystick: jugador 2" : n == 3 ? "joystick: jugador 3" : "joystick: jugador 4" );
			return;
		}
	}
}

static CBtHidDevice *FindPad( CBtHidDevice *const *aDev, TInt aCount, TInt aPad )
{
	TInt i;
	for( i = 0; i < aCount; i++ )
		if( aDev[i] && aDev[i]->Type() == EBtHidGamepad && aDev[i]->Player() == aPad + 1 )
			return aDev[i];
	return NULL;
}

void CBtHidHost::GetPadState( TInt aPad, TBtHidPadState &aState ) const
{
	CBtHidDevice *d = FindPad( iDev, EMaxDevices, aPad );
	if( d )
		d->PadState( aState );
}

void CBtHidHost::GetWiiState( TInt aPad, TBtHidWiiState &aState ) const
{
	CBtHidDevice *d = FindPad( iDev, EMaxDevices, aPad );
	if( d && d->IsWii())
		d->WiiState( aState );
}

void CBtHidHost::SetPadRumble( TInt aPad, TBool aOn )
{
	CBtHidDevice *d = FindPad( iDev, EMaxDevices, aPad );
	if( d )
		d->SetRumble( aOn );
}

/* buscar un Wiimote (sin el cuadro del telefono): hasta 40 s de busquedas, alternando la general (GIAC) y la limitada
   (LIAC). Sin pedir los nombres en la busqueda: con nombres se quedaba esperando para siempre (algun dispositivo que
   no contesta el nombre). Todo lo que se ve va al log; el Wiimote se reconoce por la clase (periferico, joystick: el
   de PS4 es gamepad) y despues se le pide el nombre para confirmarlo. Cada pedido tiene su tiempo: si no contesta,
   se cancela y se sigue */
class CBtHidWiiFinder : public CActive
{
public:
	CBtHidWiiFinder( CBtHidHost &aHost ) : CActive( EPriorityStandard ), iHost( aHost )
	{
		CActiveScheduler::Add( this );
		iWatch = CPeriodic::New( EPriorityStandard );
	}
	~CBtHidWiiFinder()
	{
		Cancel();
		delete iWatch;
		if( iOpen ) iRes.Close();
		if( iSsOpen ) iSs.Close();
		if( iNotifierOpen ) iNotifier.Close();
	}
	void Start( TInt aSlot )
	{
		iSlot = aSlot;
		iRound = 0;
		iUntil.UniversalTime();
		iUntil += TTimeIntervalMicroSeconds32( 40000000 );
		/* primero el Bluetooth prendido: el aviso del telefono "Activar Bluetooth?" (si ya esta prendido vuelve enseguida).
		   Con el apagado no se puede ni abrir la busqueda (-44) */
		if( !iNotifierOpen )
			iNotifierOpen = iNotifier.Connect() == KErrNone;
		if( iNotifierOpen )
		{
			iPhase = EPower;
			iNotifier.StartNotifierAndGetResponse( iStatus, KPowerModeSettingNotifierUid, iPowerParam, iPowerResult );
			SetActive();
			return;
		}
		Open();
	}

private:
	enum { EPower, EInquiry, EName };

	void Open()
	{
		/* con su propia sesion del servidor de sockets: la del host esta llena de pedidos pendientes */
		TInt err = KErrNone;
		if( !iSsOpen )
		{
			err = iSs.Connect();
			iSsOpen = err == KErrNone;
		}
		/* (recien prendido, el Bluetooth tarda un poco: hasta 3 s) */
		for( TInt i = 0; i < 6 && iSsOpen && !iOpen; i++ )
		{
			err = iRes.Open( iSs, KBTAddrFamily, KBTLinkManager );
			iOpen = err == KErrNone;
			if( !iOpen && err == KErrHardwareNotAvailable )
				User::After( 500000 );
			else
				break;
		}
		if( !iOpen )
		{
			TBuf8<48> msg;
			msg.Format( _L8( "wiimote: no se pudo abrir la busqueda (%d)" ), err );
			iHost.Observer().BtHidLog( (const char *)msg.PtrZ());
			iHost.Observer().BtHidSearchDone( err != KErrNone ? err : KErrNotReady );
			return;
		}
		iHost.Observer().BtHidLog( "buscando un wiimote (apretar 1+2)" );
		Round();
	}

	/* si el pedido no termina en aUs, se cancela (RunL recibe KErrCancel) */
	void Watch( TInt aUs )
	{
		if( !iWatch )
			return;
		iWatch->Cancel();
		iWatch->Start( aUs, aUs, TCallBack( Expired, this ));
	}
	static TInt Expired( TAny *aPtr )
	{
		CBtHidWiiFinder *self = static_cast<CBtHidWiiFinder *>( aPtr );
		self->iWatch->Cancel();
		self->iTimedOut = ETrue;
		self->iRes.Cancel();
		return 0;
	}
	void Round()
	{
		TBuf8<48> msg;
		iPhase = EInquiry;
		iTimedOut = EFalse;
		iAddr = TInquirySockAddr();
		iAddr.SetIAC( iRound & 1 ? KLIAC : KGIAC );
		iAddr.SetAction( KHostResInquiry | KHostResIgnoreCache );
		msg.Format( _L8( "wiimote: busqueda %d (%s)" ), iRound + 1, iRound & 1 ? "limitada" : "general" );
		iHost.Observer().BtHidLog( (const char *)msg.PtrZ());
		iRes.GetByAddress( iAddr, iEntry, iStatus );
		SetActive();
		Watch( 15000000 );
	}
	void AskName( const TBTDevAddr &aAddr )
	{
		iPhase = EName;
		iTimedOut = EFalse;
		iCandidate = aAddr;
		iAddr = TInquirySockAddr();
		iAddr.SetBTAddr( aAddr );
		iAddr.SetAction( KHostResName | KHostResIgnoreCache );
		iRes.GetByAddress( iAddr, iEntry, iStatus );
		SetActive();
		Watch( 6000000 );
	}
	void Log( const char *aWhat, const TBTDevAddr &aAddr, TInt aMajor, TInt aMinor, const TDesC8 &aName, TInt aErr )
	{
		TBuf<16>   readable;
		TBuf8<16>  addr;
		TBuf8<120> msg;
		aAddr.GetReadable( readable );
		addr.Copy( readable );
		msg.Format( _L8( "%s %S clase %02x/%02x nombre \"%S\" (%d)" ), aWhat, &addr, aMajor, aMinor, &aName, aErr );
		iHost.Observer().BtHidLog( (const char *)msg.PtrZ());
	}
	void RunL()
	{
		TInt err = iStatus.Int();
		if( iWatch )
			iWatch->Cancel();
		if( iPhase == EPower )
		{
			iNotifier.CancelNotifier( KPowerModeSettingNotifierUid );
			if( err != KErrNone || !iPowerResult())
			{
				iHost.Observer().BtHidLog( "wiimote: el bluetooth sigue apagado" );
				iHost.Observer().BtHidSearchDone( KErrCancel );
				return;
			}
			Open();
			return;
		}
		if( iPhase == EName )
		{
			/* el nombre del candidato: si es un Wiimote (o no contesto el nombre, con la clase alcanza), listo */
			TBuf8<32> name;
			if( err == KErrNone )
				name.Copy( iEntry().iName.Left( name.MaxLength()));
			Log( "nombre de", iCandidate, iMajor, iMinor, name, iTimedOut ? KErrTimedOut : err );
			if( err != KErrNone || name.Find( _L8( "RVL-CNT" )) >= 0 || name.FindF( _L8( "Nintendo" )) >= 0 )
			{
				if( name.Find( _L8( "RVL-CNT" )) < 0 )
					name.Copy( _L8( "Nintendo RVL-CNT-01" ));
				iHost.WiiFound( iCandidate, name, iSlot );
				return;
			}
			NextRound(); // otro joystick: se sigue buscando
			return;
		}
		if( err == KErrNone )
		{
			TInquirySockAddr &a = TInquirySockAddr::Cast( iEntry().iAddr );
			Log( "visto", a.BTAddr(), a.MajorClassOfDevice(), a.MinorClassOfDevice(), KNullDesC8, KErrNone );
			if( a.MajorClassOfDevice() == 0x05 && ( a.MinorClassOfDevice() & 0x0F ) == 0x01 )
			{
				iMajor = a.MajorClassOfDevice();
				iMinor = a.MinorClassOfDevice();
				AskName( a.BTAddr());
				return;
			}
			iRes.Next( iEntry, iStatus );
			SetActive();
			Watch( 15000000 );
			return;
		}
		/* termino una busqueda (o fallo, o no contesto): otra, de la otra clase, hasta los 40 s */
		if( iTimedOut )
			iHost.Observer().BtHidLog( "wiimote: la busqueda no termino, cancelada" );
		else if( err != KErrHostResNoMoreResults )
		{
			TBuf8<48> msg;
			msg.Format( _L8( "wiimote: la busqueda fallo (%d)" ), err );
			iHost.Observer().BtHidLog( (const char *)msg.PtrZ());
		}
		NextRound();
	}
	void NextRound()
	{
		TTime now;
		now.UniversalTime();
		if( now > iUntil )
		{
			iHost.Observer().BtHidLog( "wiimote: no aparecio" );
			iHost.Observer().BtHidSearchDone( KErrNotFound );
			return;
		}
		iRound++;
		Round();
	}
	void DoCancel()
	{
		if( iWatch )
			iWatch->Cancel();
		if( iPhase == EPower )
			iNotifier.CancelNotifier( KPowerModeSettingNotifierUid );
		else
			iRes.Cancel();
	}

	CBtHidHost       &iHost;
	RSocketServ       iSs;
	TBool             iSsOpen;
	RNotifier         iNotifier;
	TBool             iNotifierOpen;
	TPckgBuf<TBool>   iPowerParam, iPowerResult;
	RHostResolver     iRes;
	TBool             iOpen;
	CPeriodic        *iWatch;
	TBool             iTimedOut;
	TInt              iPhase;
	TInquirySockAddr  iAddr;
	TNameEntry        iEntry;
	TBTDevAddr        iCandidate;
	TInt              iMajor, iMinor;
	TInt              iSlot, iRound;
	TTime             iUntil;
};

void CBtHidHost::SelectWii( TInt aSlot )
{
	if( Searching())
		return;
	if( !iWiiFinder )
		iWiiFinder = new CBtHidWiiFinder( *this );
	if( iWiiFinder )
		iWiiFinder->Start( aSlot );
}

/* lo encontro CBtHidWiiFinder: como uno elegido en el cuadro (se lo llama ya, esta en modo emparejar) */
void CBtHidHost::WiiFound( const TBTDevAddr &aAddr, const TDesC8 &aName, TInt aSlot )
{
	iObserver.BtHidLog( "wiimote encontrado" );
	AddDevice( aAddr, EBtHidGamepad, aName, KNullDesC8, 0, ETrue, aSlot );
	Save();
	iObserver.BtHidSearchDone( KErrNone );
}

TBool CBtHidHost::Searching() const
{
	return IsActive() || ( iWiiFinder && iWiiFinder->IsActive());
}

class CBtHidListener : public CActive
{
public:
	CBtHidListener( CBtHidHost &aHost, TUint aPsm ) : CActive( EPriorityStandard ), iHost( aHost ), iPsm( aPsm )
	{
		CActiveScheduler::Add( this );
	}
	~CBtHidListener()
	{
		Cancel();
		if( iBlankOpen ) iBlank.Close();
		if( iListenOpen ) iListen.Close();
	}
	void Start()
	{
		TBTSockAddr        addr;
		TBTServiceSecurity sec;
		TInt               err;
		TBuf8<64>          msg;

		err = iListen.Open( iHost.SocketServ(), KBTAddrFamily, KSockSeqPacket, KL2CAP );
		if( err == KErrNone )
		{
			iListenOpen = ETrue;
			sec.SetUid( TUid::Uid( 0xE0D69650 ));
			sec.SetAuthentication( EFalse ); // el dispositivo la pide si la quiere (los teclados si)
			sec.SetEncryption( EFalse );
			sec.SetAuthorisation( EFalse );
			addr.SetPort( iPsm );
			addr.SetSecurity( sec );
			err = iListen.Bind( addr );
		}
		if( err == KErrNone )
			err = iListen.Listen( 2 );
		if( err != KErrNone )
		{
			// KErrInUse: otro (el hidsrv de Hinkka, el HID del sistema en Symbian^3) ya escucha este canal
			msg.Format( _L8( "no se puede escuchar el canal 0x%x (%d)" ), iPsm, err );
			iHost.Observer().BtHidLog( (const char *)msg.PtrZ());
			return;
		}
		msg.Format( _L8( "escuchando el canal 0x%x" ), iPsm );
		iHost.Observer().BtHidLog( (const char *)msg.PtrZ());
		Accept();
	}

private:
	void Accept()
	{
		if( iBlankOpen ) { iBlank.Close(); iBlankOpen = EFalse; }
		if( iBlank.Open( iHost.SocketServ()) != KErrNone )
			return;
		iBlankOpen = ETrue;
		iListen.Accept( iBlank, iStatus );
		SetActive();
	}
	void RunL()
	{
		if( iStatus.Int() != KErrNone )
		{
			TBuf8<48> msg;
			msg.Format( _L8( "error esperando llamadas en 0x%x (%d)" ), iPsm, iStatus.Int());
			iHost.Observer().BtHidLog( (const char *)msg.PtrZ());
		}
		if( iStatus.Int() == KErrNone )
		{
			TBTSockAddr remote;
			iBlank.RemoteName( remote );
			iBlankOpen = EFalse; // el socket ahora es del dispositivo (o lo cierra Incoming)
			iHost.Incoming( iPsm, iBlank, remote.BTAddr());
		}
		Accept();
	}
	void DoCancel() { iListen.CancelAccept(); }

	CBtHidHost &iHost;
	TUint       iPsm;
	RSocket     iListen, iBlank;
	TBool       iListenOpen, iBlankOpen;
};

void CBtHidHost::Incoming( TUint aPsm, RSocket &aSock, const TBTDevAddr &aAddr )
{
	TInt i;

	for( i = 0; i < EMaxDevices; i++ )
	{
		if( !iDev[i] || iDev[i]->Addr() != aAddr )
			continue;
		if( aPsm == KHidCtrlPsm )
			iDev[i]->AdoptCtrl( aSock );
		else
			iDev[i]->AdoptIntr( aSock );
		return;
	}
	/* uno que no esta en la lista y llama solo: un joystick de PS3 al que se le grabo la direccion del telefono
	   (tools/ds3pair.py). Se lo toma como joystick si hay lugar (hasta 4) */
	if( aPsm == KHidCtrlPsm )
	{
		TInt pads = 0, freeSlot = -1;
		for( i = 0; i < EMaxDevices; i++ )
		{
			if( iDev[i] && iDev[i]->Type() == EBtHidGamepad ) pads++;
			if( !iDev[i] && freeSlot < 0 ) freeSlot = i;
		}
		if( pads < EBtHidMaxPads && freeSlot >= 0 )
		{
			iObserver.BtHidLog( "llama un joystick que no estaba: lo agrego" );
			AddDevice( aAddr, EBtHidGamepad, _L8( "PS3" ), KNullDesC8, 0, EFalse );
			Save();
			for( i = 0; i < EMaxDevices; i++ )
				if( iDev[i] && iDev[i]->Addr() == aAddr )
				{
					iDev[i]->AdoptCtrl( aSock );
					return;
				}
		}
	}
	aSock.Close(); // no es un dispositivo elegido
	iObserver.BtHidLog( "conexion entrante de un dispositivo no elegido: rechazada" );
}

CBtHidHost *CBtHidHost::NewL( MBtHidObserver &aObserver, const TDesC &aSaveFile )
{
	CBtHidHost *self = new( ELeave ) CBtHidHost( aObserver );
	CleanupStack::PushL( self );
	self->ConstructL( aSaveFile );
	CleanupStack::Pop( self );
	return self;
}

CBtHidHost::CBtHidHost( MBtHidObserver &aObserver )
	: CActive( EPriorityStandard ), iObserver( aObserver )
{
	CActiveScheduler::Add( this );
}

void CBtHidHost::ConstructL( const TDesC &aSaveFile )
{
	User::LeaveIfError( iSs.Connect());
	iFile.Copy( aSaveFile.Left( iFile.MaxLength()));
	Load();
	{
		RFs   fs;
		RFile f;
		if( fs.Connect() == KErrNone )
		{
			if( f.Open( fs, KPadKeysOnFile, EFileRead ) == KErrNone )
			{
				TBuf8<1> b;
				if( f.Read( b ) == KErrNone && b.Length() && b[0] )
					iPadKeys = ETrue;
				f.Close();
			}
			BtHidCloseFs( fs );
		}
		if( iPadKeys )
			LoadPadKeys();
	}
	iListenCtrl = new( ELeave ) CBtHidListener( *this, KHidCtrlPsm );
	iListenIntr = new( ELeave ) CBtHidListener( *this, KHidIntrPsm );
	iListenCtrl->Start();
	iListenIntr->Start();
}

CBtHidHost::~CBtHidHost()
{
	TInt i;

	Cancel();
	delete iWiiFinder;
	delete iListenCtrl;
	delete iListenIntr;
	for( i = 0; i < EMaxDevices; i++ )
		delete iDev[i];
	if( iNotifierOpen )
		iNotifier.Close();
	iSs.Close();
}

TInt CBtHidHost::DeviceCount() const
{
	TInt i, n = 0;
	for( i = 0; i < EMaxDevices; i++ )
		if( iDev[i] ) n++;
	return n;
}

/* un dispositivo por tipo: elegir otro mouse reemplaza al anterior */
void CBtHidHost::AddDevice( const TBTDevAddr &aAddr, TInt aType, const TDesC8 &aName, const TDesC8 &aDesc, TInt aFlags, TBool aCall,
	TInt aPlayer )
{
	TInt i, slot = -1;

	for( i = 0; i < EMaxDevices; i++ )
	{
		if( iDev[i] && ( iDev[i]->Addr() == aAddr || ( aType && aType != EBtHidGamepad && iDev[i]->Type() == aType )))
		{
			delete iDev[i];
			iDev[i] = NULL;
		}
		if( !iDev[i] && slot < 0 )
			slot = i;
	}
	if( slot < 0 )
		return;
	iDev[slot] = new CBtHidDevice( *this, aAddr, aType, aName, aDesc, aFlags );
	if( !iDev[slot] )
		return;
	if( aType == EBtHidGamepad && aPlayer > 0 && aPlayer <= EBtHidMaxPads )
	{
		// (el lugar pedido: si otro lo tenia, queda sin numero hasta que se conecte)
		for( i = 0; i < EMaxDevices; i++ )
			if( iDev[i] && i != slot && iDev[i]->Type() == EBtHidGamepad && iDev[i]->Player() == aPlayer )
				iDev[i]->SetPlayer( 0 );
		iDev[slot]->SetPlayer( aPlayer );
	}
	if( aCall )
	{
		iDev[slot]->StartAfter( 2000000 ); // recien elegido: esta en modo emparejar, hay que llamarlo
		iFirstAddr = aAddr;
		iFirstUntil.UniversalTime();
		iFirstUntil += TTimeIntervalMicroSeconds32( KFirstCallUs );
	}
	else
		iDev[slot]->Wait();
}

/* se puede llamar a aDev? No si ya se esta llamando a otro, ni si hay uno recien elegido (el primer minuto, hasta
   que conecte) y no es este */
TBool CBtHidHost::RadioBusy( const CBtHidDevice *aDev ) const
{
	TInt  i;
	TBool first = EFalse;
	TTime now;

	now.UniversalTime();
	for( i = 0; i < EMaxDevices; i++ )
	{
		if( !iDev[i] || iDev[i] == aDev )
			continue;
		if( iDev[i]->Calling())
			return ETrue;
		if( now < iFirstUntil && iDev[i]->Addr() == iFirstAddr && iDev[i]->State() != EBtHidConnected
			&& iDev[i]->State() != EBtHidStopped )
			first = ETrue;
	}
	return first;
}

void CBtHidHost::SelectPad( TInt aSlot )
{
	SelectDevice( EBtHidGamepad );
	if( IsActive())
		iSelectSlot = aSlot;
}

void CBtHidHost::SelectDevice( TInt aType )
{
	if( IsActive())
		return;
	iSelectType = aType;
	iSelectSlot = 0;
	if( !iNotifierOpen )
	{
		if( iNotifier.Connect() != KErrNone )
		{
			iObserver.BtHidLog( "no se pudo abrir el cuadro de busqueda" );
			return;
		}
		iNotifierOpen = ETrue;
	}
	iSel = TBTDeviceSelectionParamsPckg();
	iNotifier.StartNotifierAndGetResponse( iStatus, KDeviceSelectionNotifierUid, iSel, iResp );
	SetActive();
}

void CBtHidHost::RunL()
{
	TInt      type = 0;
	TBuf8<32> name;

	iNotifier.CancelNotifier( KDeviceSelectionNotifierUid );
	if( iStatus.Int() != KErrNone || !iResp().IsValidBDAddr())
	{
		iObserver.BtHidSearchDone( iStatus.Int() != KErrNone ? iStatus.Int() : KErrCancel );
		return;
	}

	if( iSelectType )
		type = iSelectType; // lo eligio el usuario (Elegir mouse / Elegir teclado)
	else if( iResp().IsValidDeviceClass())
	{
		TBTDeviceClass cls = iResp().DeviceClass();
		if( cls.MajorDeviceClass() == EMajorDevicePeripheral )
		{
			TUint minor = cls.MinorDeviceClass();
			if(( minor & 0x0F ) == EMinorDevicePeripheralJoystick || ( minor & 0x0F ) == EMinorDevicePeripheralGamepad )
				type = EBtHidGamepad;
			else
				type = ( minor & EMinorDevicePeripheralKeyboard ) ? EBtHidKeyboard : EBtHidMouse;
		}
	}
	if( iResp().IsValidDeviceName())
		name.Copy( iResp().DeviceName().Left( name.MaxLength()));
	iObserver.BtHidLog( type == EBtHidKeyboard ? "teclado elegido" : type == EBtHidMouse ? "mouse elegido"
		: type == EBtHidGamepad ? "joystick elegido" : "dispositivo elegido" );
	AddDevice( iResp().BDAddr(), type, name, KNullDesC8, 0, ETrue, type == EBtHidGamepad ? iSelectSlot : 0 );
	iSelectSlot = 0;
	Save();
	iObserver.BtHidSearchDone( KErrNone );
}

void CBtHidHost::CancelSelect()
{
	if( iWiiFinder && iWiiFinder->IsActive())
	{
		iWiiFinder->Cancel();
		iObserver.BtHidSearchDone( KErrCancel );
	}
	Cancel();
}

void CBtHidHost::GetStatus( TBtHidStatus &aStatus ) const
{
	TInt i;

	aStatus.iCount = 0;
	for( i = 0; i < EMaxDevices; i++ )
	{
		TBtHidDeviceInfo &info = aStatus.iDevice[aStatus.iCount];
		if( !iDev[i] ) continue;
		Mem::Copy( info.iAddr, iDev[i]->Addr().Des().Ptr(), 6 );
		info.iType  = iDev[i]->Type();
		info.iState = iDev[i]->State();
		info.iError = iDev[i]->Error();
		info.iName  = iDev[i]->Name();
		info.iPlayer = iDev[i]->Type() == EBtHidGamepad ? iDev[i]->Player() : 0;
		info.iBattery = iDev[i]->Type() == EBtHidGamepad ? iDev[i]->Battery() : -1;
		info.iCharging = iDev[i]->Type() == EBtHidGamepad && iDev[i]->Charging();
		aStatus.iCount++;
	}
}

void CBtHidHost::DoCancel()
{
	iNotifier.CancelNotifier( KDeviceSelectionNotifierUid );
	iObserver.BtHidSearchDone( KErrCancel );
}

void CBtHidHost::Forget( TInt aType )
{
	TInt i;
	for( i = 0; i < EMaxDevices; i++ )
	{
		// los guardados sin tipo cuentan como mouse
		if( iDev[i] && ( iDev[i]->Type() == aType || ( !iDev[i]->Type() && aType == EBtHidMouse )))
		{
			delete iDev[i];
			iDev[i] = NULL;
		}
	}
	Save();
}

TInt CBtHidHost::DeviceCommand( const TBTDevAddr &aAddr, TInt aCommand )
{
	TInt i;
	for( i = 0; i < EMaxDevices; i++ )
	{
		if( !iDev[i] || iDev[i]->Addr() != aAddr )
			continue;
		if( aCommand == EBtHidCmdRemove )
		{
			delete iDev[i];
			iDev[i] = NULL;
			Save();
		}
		else if( aCommand == EBtHidCmdDisconnect )
			iDev[i]->Stop();
		else if( aCommand == EBtHidCmdConnect )
			iDev[i]->Resume();
		else if( aCommand == EBtHidCmdWiiArrows )
			return iDev[i]->WiiSideways() ? 1 : 0;
		else if( aCommand == EBtHidCmdWiiArrowsNormal || aCommand == EBtHidCmdWiiArrowsSideways )
		{
			iDev[i]->SetWiiSideways( aCommand == EBtHidCmdWiiArrowsSideways );
			Save();
		}
		else if( aCommand > EBtHidCmdMoveToPad && aCommand <= EBtHidCmdMoveToPad + EBtHidMaxPads
			&& iDev[i]->Type() == EBtHidGamepad )
		{
			// el que tenia ese lugar se lleva el de este; los dos cambian la luz
			TInt to = aCommand - EBtHidCmdMoveToPad, from = iDev[i]->Player(), j;
			for( j = 0; j < EMaxDevices; j++ )
				if( iDev[j] && j != i && iDev[j]->Type() == EBtHidGamepad && iDev[j]->Player() == to )
				{
					iDev[j]->SetPlayer( from );
					iDev[j]->Light();
				}
			iDev[i]->SetPlayer( to );
			iDev[i]->Light();
			Save();
		}
		return KErrNone;
	}
	return KErrNotFound;
}

void CBtHidHost::ForgetAll()
{
	TInt i;
	for( i = 0; i < EMaxDevices; i++ )
	{
		delete iDev[i];
		iDev[i] = NULL;
	}
	Save();
}

/* archivo: un byte de version (KSaveVersion) y por dispositivo: 6 bytes de direccion, 1 de tipo, 1 de largo del
   nombre, el nombre, 1 de flags (EFlag*), 2 de largo del descriptor HID y el descriptor. (La version sin byte
   de version tampoco tenia flags ni descriptor.) */
const TUint8 KSaveVersion = 0xB4; // 0xB3 no tenia el byte de flags (EFlag*) despues del nombre

void CBtHidHost::Load()
{
	RFs    fs;
	RFile  f;
	HBufC8 *buf = NULL;
	TInt   size = 0, i;

	if( fs.Connect() != KErrNone )
		return;
	if( f.Open( fs, iFile, EFileRead ) == KErrNone )
	{
		if( f.Size( size ) == KErrNone && size > 0 && size < 8192 && ( buf = HBufC8::New( size )) != NULL )
		{
			TPtr8 p = buf->Des();
			f.Read( p );
		}
		f.Close();
	}
	BtHidCloseFs( fs );
	if( !buf )
		return;

	const TDesC8 &data = *buf;
	TInt ver = !data.Length() ? 0 : data[0] == KSaveVersion ? 4 : data[0] == 0xB3 ? 3 : 2;
	for( i = ver > 2 ? 1 : 0; i + 8 <= data.Length(); )
	{
		TInt len = data[i + 7], flags = 0, dlen = 0, pos;
		if( i + 8 + len > data.Length())
			break;
		pos = i + 8 + len;
		if( ver == 4 )
			flags = data[pos++];
		if( ver > 2 )
		{
			if( pos + 2 > data.Length())
				break;
			dlen = data[pos] | ( data[pos + 1] << 8 );
			pos += 2;
			if( pos + dlen > data.Length())
				break;
		}
		AddDevice( TBTDevAddr( data.Mid( i, 6 )), data[i + 6], data.Mid( i + 8, len ),
			dlen ? data.Mid( pos, dlen ) : KNullDesC8(), flags & 0x0F, EFalse, ( flags >> 4 ) & 7 );
		i = pos + dlen;
	}
	delete buf;
}

void CBtHidHost::Save()
{
	RFs    fs;
	RFile  f;
	HBufC8 *buf = HBufC8::New( 1 + EMaxDevices * ( 11 + 32 + KMaxHidDesc ));
	TInt   i;

	if( !buf )
		return;
	TPtr8 data = buf->Des();
	data.Append( KSaveVersion );
	for( i = 0; i < EMaxDevices; i++ )
	{
		if( !iDev[i] ) continue;
		const TDesC8 &desc = iDev[i]->Descriptor();
		data.Append( iDev[i]->Addr().Des());
		data.Append( (TUint8)iDev[i]->Type());
		data.Append( (TUint8)iDev[i]->Name().Length());
		data.Append( iDev[i]->Name());
		// (los bits altos: el lugar del joystick, su numero de jugador)
		data.Append( (TUint8)(( iDev[i]->Flags() & 0x0F ) | ( iDev[i]->Type() == EBtHidGamepad ? ( iDev[i]->Player() & 7 ) << 4 : 0 )));
		data.Append( (TUint8)( desc.Length() & 0xff ));
		data.Append( (TUint8)( desc.Length() >> 8 ));
		data.Append( desc );
	}
	if( fs.Connect() == KErrNone )
	{
		if( f.Replace( fs, iFile, EFileWrite ) == KErrNone )
		{
			f.Write( data );
			f.Close();
		}
		BtHidCloseFs( fs );
	}
	delete buf;
}
