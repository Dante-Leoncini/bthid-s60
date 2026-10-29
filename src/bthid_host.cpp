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
const TUint  KHidDescListAttr = 0x0206;  // atributo HIDDescriptorList
const TInt   KMaxHidDesc      = 512;
const TUint8 KHidpDataInput   = 0xA1;  // HIDP DATA, reporte de entrada
const TInt   KRetryFirstUs    = 3000000;
const TInt   KRetryUs         = 8000000;
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
		TRAPD( err, iAgent->AttributeRequestL( aHandle, KHidDescListAttr ));
		if( err != KErrNone )
			Finish( err );
	}
	void AttributeRequestResult( TSdpServRecordHandle, TSdpAttributeID, CSdpAttrValue *aValue )
	{
		if( !aValue )
			return;
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
		if( aType == ETypeString && !iDesc.Length())
			iDesc.Copy( aValue.Des().Left( iDesc.MaxLength()));
	}
	void StartListL( CSdpAttrValueList & ) {}
	void EndListL() {}
	void Finish( TInt aError );

	CBtHidDevice       &iDev;
	CSdpAgent          *iAgent;
	CSdpSearchPattern  *iPattern;
	TBuf8<KMaxHidDesc>  iDesc;
	TBool               iDone;
};

class CBtHidDevice : public CActive
{
public:
	CBtHidDevice( CBtHidHost &aHost, const TBTDevAddr &aAddr, TInt aType, const TDesC8 &aName, const TDesC8 &aDesc );
	~CBtHidDevice();
	void Start();
	void StartAfter( TInt aMicroSeconds );
	void AdoptCtrl( RSocket &aSock );
	void AdoptIntr( RSocket &aSock );
	const TBTDevAddr &Addr() const { return iAddr; }
	TInt Type() const { return iType; }
	const TDesC8 &Name() const { return iName; }
	TInt State() const { return iConnected ? EBtHidConnected : iState == EWaitRetry ? EBtHidDisconnected : EBtHidConnecting; }
	TInt Error() const { return iError; }
	const TDesC8 &Descriptor() const { return iDesc; }
	void DescriptorDone( TInt aError, const TDesC8 &aDesc );

private:
	enum TState { EIdle, EConnCtrl, EConnIntr, ESetProto, EReading, EWaitRetry, EWaitIntr };

	void RunL();
	void DoCancel();
	TInt RunError( TInt aError );
	void SetProtocol();
	void ConnectChannel( RSocket &aSock, TUint aPsm );
	void CloseSockets();
	void Retry( TInt aErr );
	void ReleaseAll();
	void Report( const TDesC8 &aData );
	TBool LayoutReport( const TDesC8 &aData );
	void BootReport( const TDesC8 &aData );
	void KeyboardState( TUint8 aMods, const TUint8 *aKeys );
	void StartSdp();
	void LogLayout();
	void Log( const char *aWhat, TInt aErr );

	CBtHidHost &iHost;
	TBTDevAddr  iAddr;
	TInt        iType;
	TState      iState;
	RSocket     iCtrl, iIntr;
	TBool       iCtrlOpen, iIntrOpen;
	TBool       iTimerOpen;
	RTimer      iTimer;
	TBuf8<64>   iBuf;
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
};

void CBtHidSdp::Finish( TInt aError )
{
	if( iDone )
		return;
	iDone = ETrue;
	iDev.DescriptorDone( aError, iDesc ); // no nos borra aca: lo hace en la proxima conexion o al cerrar
}

CBtHidDevice::CBtHidDevice( CBtHidHost &aHost, const TBTDevAddr &aAddr, TInt aType, const TDesC8 &aName, const TDesC8 &aDesc )
	: CActive( EPriorityStandard ), iHost( aHost ), iAddr( aAddr ), iType( aType ), iState( EIdle )
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
	msg.Format( _L8( "%s %S %s (%d)" ), iType == EBtHidKeyboard ? "teclado" : "mouse", &addr, aWhat, aErr );
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
	if( iConnected )
	{
		Log( "desconectado", aErr );
		iConnected = EFalse;
	}
	else if( iFails < 20 ) // cada intento (despues se calla, para no llenar el log con un mouse apagado)
		Log( iState == EConnCtrl ? "fallo el canal de control" : iState == EConnIntr ? "fallo el canal de datos"
			: iState == EWaitIntr ? "llego el control pero no los datos" : "fallo la conexion", aErr );
	iFails++;
	iState = EWaitRetry;
	if( !iTimerOpen )
		return;
	iTimer.After( iStatus, iFails < 3 ? KRetryFirstUs : KRetryUs );
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
		Log( iProtoBoot ? "conectado (protocolo boot)" : "conectado (reportes del descriptor)", err );
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
	if( iCtrlOpen )
	{
		SetProtocol();
		return;
	}
	iConnected = ETrue;
	iFails = 0;
	iError = KErrNone;
	iProtoBoot = !iLayout.iValid;
	Log( "conectado (sin canal de control)", KErrNone );
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
	if( iDesc.Length() || iSdp || iSdpTried )
		return;
	iSdpTried = ETrue;
	iSdp = new CBtHidSdp( *this );
	if( !iSdp )
		return;
	TRAPD( err, iSdp->StartL( iAddr ));
	if( err != KErrNone )
		Log( "no se pudo pedir el descriptor HID", err );
}

void CBtHidDevice::DescriptorDone( TInt aError, const TDesC8 &aDesc )
{
	if( aError != KErrNone || !aDesc.Length())
	{
		Log( "sin descriptor HID, queda el protocolo boot", aError );
		return;
	}
	iDesc.Copy( aDesc.Left( iDesc.MaxLength()));
	HidParseDescriptor( iDesc, iLayout );
	LogLayout();
	iHost.Save(); // la proxima conexion ya usa los reportes del descriptor
}

void CBtHidDevice::LogLayout()
{
	TBuf8<120> msg;
	msg.Format( _L8( "descriptor HID %d bytes: ids %d, x id %d bit %d/%d, y bit %d/%d, rueda id %d bit %d/%d, boton id %d bit %d" ),
		iDesc.Length(), iLayout.iUsesIds, iLayout.iX.iReportId, iLayout.iX.iBit, iLayout.iX.iBits, iLayout.iY.iBit, iLayout.iY.iBits,
		iLayout.iWheel.iReportId, iLayout.iWheel.iBit, iLayout.iWheel.iBits, iLayout.iButton[0].iReportId, iLayout.iButton[0].iBit );
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
	bytes = ( iLayout.iReportBits[id] + 7 ) / 8;
	if( !bytes || aData.Length() - start < bytes )
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
void CBtHidHost::AddDevice( const TBTDevAddr &aAddr, TInt aType, const TDesC8 &aName, const TDesC8 &aDesc )
{
	TInt i, slot = -1;

	for( i = 0; i < EMaxDevices; i++ )
	{
		if( iDev[i] && ( iDev[i]->Addr() == aAddr || ( aType && iDev[i]->Type() == aType )))
		{
			delete iDev[i];
			iDev[i] = NULL;
		}
		if( !iDev[i] && slot < 0 )
			slot = i;
	}
	if( slot < 0 )
		return;
	iDev[slot] = new CBtHidDevice( *this, aAddr, aType, aName, aDesc );
	if( iDev[slot] )
		iDev[slot]->StartAfter( 2000000 );
}

void CBtHidHost::SelectDevice()
{
	if( IsActive())
		return;
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

	if( iResp().IsValidDeviceClass())
	{
		TBTDeviceClass cls = iResp().DeviceClass();
		if( cls.MajorDeviceClass() == EMajorDevicePeripheral )
			type = ( cls.MinorDeviceClass() & EMinorDevicePeripheralKeyboard ) ? EBtHidKeyboard : EBtHidMouse;
	}
	if( iResp().IsValidDeviceName())
		name.Copy( iResp().DeviceName().Left( name.MaxLength()));
	iObserver.BtHidLog( type == EBtHidKeyboard ? "teclado elegido" : type == EBtHidMouse ? "mouse elegido" : "dispositivo elegido" );
	AddDevice( iResp().BDAddr(), type, name, KNullDesC8 );
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
		aStatus.iCount++;
	}
}

void CBtHidHost::DoCancel()
{
	iNotifier.CancelNotifier( KDeviceSelectionNotifierUid );
	iObserver.BtHidSearchDone( KErrCancel );
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
   nombre, el nombre, 2 de largo del descriptor HID y el descriptor. (La version vieja no tenia el byte de version
   ni el descriptor.) */
const TUint8 KSaveVersion = 0xB3;

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
	TBool v3 = data.Length() && data[0] == KSaveVersion;
	for( i = v3 ? 1 : 0; i + 8 <= data.Length(); )
	{
		TInt len = data[i + 7], dlen = 0;
		if( i + 8 + len > data.Length())
			break;
		if( v3 )
		{
			if( i + 10 + len > data.Length())
				break;
			dlen = data[i + 8 + len] | ( data[i + 9 + len] << 8 );
			if( i + 10 + len + dlen > data.Length())
				break;
		}
		AddDevice( TBTDevAddr( data.Mid( i, 6 )), data[i + 6], data.Mid( i + 8, len ),
			v3 ? data.Mid( i + 10 + len, dlen ) : KNullDesC8());
		i += 8 + len + ( v3 ? 2 + dlen : 0 );
	}
	delete buf;
}

void CBtHidHost::Save()
{
	RFs    fs;
	RFile  f;
	HBufC8 *buf = HBufC8::New( 1 + EMaxDevices * ( 10 + 32 + KMaxHidDesc ));
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
