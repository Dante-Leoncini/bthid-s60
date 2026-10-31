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
	EFlagNormConnect = 4   // HIDNormallyConnectable
};
const TInt   KMaxHidDesc      = 512;
const TUint8 KHidpDataInput   = 0xA1;  // HIDP DATA, reporte de entrada
const TInt   KRetryFirstUs    = 3000000;
const TInt   KRetryUs         = 8000000;
const TInt   KRetrySlowUs     = 60000000; // despues de varios fallos: casi todos los mouse no contestan si los
                                          // llama el telefono, se conectan ellos al moverlos; llamar seguido
                                          // ocupa la radio y no los deja entrar
const TInt   KWaitIntrUs      = 5000000;  // conexion entrante: control llego, esperando el canal de datos

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
	TInt State() const { return iConnected ? EBtHidConnected : iState == EWaitDevice ? EBtHidWaiting
		: iState == EWaitRetry ? EBtHidDisconnected : EBtHidConnecting; }
	TInt Flags() const { return iFlags; }
	TInt Player() const { return iPlayer; }
	void SetPlayer( TInt aPlayer ) { iPlayer = aPlayer; }
	TInt Error() const { return iError; }
	const TDesC8 &Descriptor() const { return iDesc; }
	void DescriptorDone( TInt aError, const TDesC8 &aDesc, TInt aFlags );
	// joystick: estado, vibracion
	void PadState( TBtHidPadState &aState ) const;
	void SetRumble( TBool aOn );

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
	TBool LayoutReport( const TDesC8 &aData );
	void BootReport( const TDesC8 &aData );
	void KeyboardState( TUint8 aMods, const TUint8 *aKeys );
	void StartSdp();
	void LogLayout();
	void LogDescriptor();
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
	sec.SetAuthentication( iType == EBtHidKeyboard );
	sec.SetEncryption( iType == EBtHidKeyboard );
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
	iState = EWaitRetry;
	if( !iTimerOpen )
		return;
	// recien cortado el dispositivo esta despierto: volver a llamarlo ya
	/* un joystick apagado: se lo sigue llamando cada 8 s un rato (al prenderlo conecta solo) y despues cada 30 s,
	   para no ocupar la radio todo el tiempo (el mouse llama el y necesita la radio libre) */
	if( iType == EBtHidGamepad )
		iTimer.After( iStatus, dropped ? 500000 : iFails < 20 ? KRetryUs : 30000000 );
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
		if( iType == EBtHidGamepad )
		{
			iHost.AssignPlayer( this );
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
	iFlags = aFlags;
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

void CBtHidDevice::PadReport( const TDesC8 &aData )
{
	TUint32 b = 0;
	TInt    i, kind = iPadKind;
	const TUint8 *p = aData.Ptr();
	TInt    n = aData.Length();

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
		Mem::FillZ( iPadPressure, sizeof( iPadPressure ));
		for( i = 0; i < 12; i++ )
			iPadPressure[press[i]] = p[15 + i];
	}
	else
	{
		/* DualShock 4: el corto (A1 01, 9 bytes) o el completo (A1 11 C0 00 ...) */
		const TUint8 *q;
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
	"# Joystick como teclado (Bluetooth HID): que tecla manda cada boton de cada joystick (1 a 4)\r\n"
	"# Se usa con Opciones > Joystick como teclado: si. Despues de editarlo, apagarlo y prenderlo otra vez.\r\n"
	"# Renglones: joystick boton tecla\r\n"
	"# Botones: arriba abajo izquierda derecha cruz circulo cuadrado triangulo l1 r1 l2 r2 select start l3 r3 ps\r\n"
	"#          stick_arriba stick_abajo stick_izquierda stick_derecha (el stick izquierdo)\r\n"
	"# Teclas: una letra o un numero, o enter esc espacio tab borrar arriba abajo izquierda derecha ctrl shift alt\r\n"
	"#         f1 ... f12 inicio fin pgarriba pgabajo insertar suprimir coma punto ninguna\r\n"
	"1 arriba arriba\r\n1 abajo abajo\r\n1 izquierda izquierda\r\n1 derecha derecha\r\n"
	"1 stick_arriba arriba\r\n1 stick_abajo abajo\r\n1 stick_izquierda izquierda\r\n1 stick_derecha derecha\r\n"
	"1 cruz z\r\n1 circulo x\r\n1 cuadrado a\r\n1 triangulo s\r\n1 l1 q\r\n1 r1 w\r\n1 l2 e\r\n1 r2 r\r\n"
	"1 select espacio\r\n1 start enter\r\n1 l3 1\r\n1 r3 2\r\n1 ps esc\r\n"
	"2 arriba i\r\n2 abajo k\r\n2 izquierda j\r\n2 derecha l\r\n"
	"2 stick_arriba i\r\n2 stick_abajo k\r\n2 stick_izquierda j\r\n2 stick_derecha l\r\n"
	"2 cruz v\r\n2 circulo b\r\n2 cuadrado f\r\n2 triangulo g\r\n2 l1 t\r\n2 r1 y\r\n2 l2 u\r\n2 r2 o\r\n"
	"2 select n\r\n2 start m\r\n2 l3 3\r\n2 r3 4\r\n2 ps borrar\r\n"
	"3 arriba 8\r\n3 abajo 2\r\n3 izquierda 4\r\n3 derecha 6\r\n"
	"3 stick_arriba 8\r\n3 stick_abajo 2\r\n3 stick_izquierda 4\r\n3 stick_derecha 6\r\n"
	"3 cruz c\r\n3 circulo d\r\n3 cuadrado h\r\n3 triangulo p\r\n3 select 5\r\n3 start 7\r\n"
	"4 arriba inicio\r\n4 abajo fin\r\n4 izquierda suprimir\r\n4 derecha pgabajo\r\n"
	"4 stick_arriba inicio\r\n4 stick_abajo fin\r\n4 stick_izquierda suprimir\r\n4 stick_derecha pgabajo\r\n"
	"4 cruz coma\r\n4 circulo punto\r\n4 cuadrado 9\r\n4 triangulo 0\r\n4 select tab\r\n4 start insertar\r\n" );

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
		{ "arriba", 82 }, { "ctrl", 224 }, { "shift", 225 }, { "alt", 226 }, { "ninguna", 0 } };
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
	}
	else if( f.Replace( fs, KPadKeysFile, EFileWrite ) == KErrNone )
	{
		/* la primera vez: el archivo con las teclas de siempre, para poder editarlo */
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

void CBtHidHost::SetPadRumble( TInt aPad, TBool aOn )
{
	CBtHidDevice *d = FindPad( iDev, EMaxDevices, aPad );
	if( d )
		d->SetRumble( aOn );
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
void CBtHidHost::AddDevice( const TBTDevAddr &aAddr, TInt aType, const TDesC8 &aName, const TDesC8 &aDesc, TInt aFlags, TBool aCall )
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
	if( aCall )
		iDev[slot]->StartAfter( 2000000 ); // recien elegido: esta en modo emparejar, hay que llamarlo
	else
		iDev[slot]->Wait();
}

void CBtHidHost::SelectDevice( TInt aType )
{
	if( IsActive())
		return;
	iSelectType = aType;
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
	AddDevice( iResp().BDAddr(), type, name, KNullDesC8, 0, ETrue );
	Save();
	iObserver.BtHidSearchDone( KErrNone );
}

void CBtHidHost::CancelSelect()
{
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
			dlen ? data.Mid( pos, dlen ) : KNullDesC8(), flags, EFalse );
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
		data.Append( (TUint8)iDev[i]->Flags());
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
