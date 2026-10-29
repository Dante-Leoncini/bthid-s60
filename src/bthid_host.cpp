/*
bthid_host.cpp - conexion directa con mouse y teclado Bluetooth, ver bthid_host.h

Por dispositivo: conectar el canal de control (PSM 0x11) y el de datos (0x13), pedir el protocolo boot
(SET_PROTOCOL) y leer reportes del canal de datos. En boot los reportes tienen formato fijo:
  mouse:   A1 botones dx dy [rueda ...]
  teclado: A1 modificadores 00 tecla1..tecla6
Si se corta (el mouse se duerme, se apaga, queda lejos) se reintenta cada pocos segundos. Ademas escuchamos
en los dos canales: la mayoria de los mouse no aceptan que el telefono los llame, son ellos los que llaman al
despertarse (HIDNormallyConnectable = no), y esa conexion entrante la adopta el dispositivo guardado.
*/

#include "bthid_host.h"
#include <bt_sock.h>
#include <btdevice.h>
#include <f32file.h>

const TUint  KHidCtrlPsm      = 0x11;
const TUint  KHidIntrPsm      = 0x13;
const TUint8 KHidpSetProtoBoot = 0x70; // HIDP SET_PROTOCOL, parametro 0 = boot
const TUint8 KHidpDataInput   = 0xA1;  // HIDP DATA, reporte de entrada
const TInt   KRetryFirstUs    = 3000000;
const TInt   KRetryUs         = 8000000;
const TInt   KWaitIntrUs      = 5000000;  // conexion entrante: control llego, esperando el canal de datos

class CBtHidDevice : public CActive
{
public:
	CBtHidDevice( CBtHidHost &aHost, const TBTDevAddr &aAddr, TInt aType, const TDesC8 &aName );
	~CBtHidDevice();
	void Start();
	void AdoptCtrl( RSocket &aSock );
	void AdoptIntr( RSocket &aSock );
	const TBTDevAddr &Addr() const { return iAddr; }
	TInt Type() const { return iType; }
	const TDesC8 &Name() const { return iName; }
	TInt State() const { return iConnected ? EBtHidConnected : iState == EWaitRetry ? EBtHidDisconnected : EBtHidConnecting; }
	TInt Error() const { return iError; }

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
	void Keyboard( const TDesC8 &aData );
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
	TBuf8<32>   iName;
	TInt        iButtons;
	TUint8      iMods;
	TUint8      iKeys[6];
};

CBtHidDevice::CBtHidDevice( CBtHidHost &aHost, const TBTDevAddr &aAddr, TInt aType, const TDesC8 &aName )
	: CActive( EPriorityStandard ), iHost( aHost ), iAddr( aAddr ), iType( aType ), iState( EIdle )
{
	iName.Copy( aName.Left( iName.MaxLength()));
	CActiveScheduler::Add( this );
	iTimerOpen = iTimer.CreateLocal() == KErrNone;
	Mem::FillZ( iKeys, sizeof( iKeys ));
}

CBtHidDevice::~CBtHidDevice()
{
	Cancel();
	ReleaseAll();
	CloseSockets();
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

void CBtHidDevice::ConnectChannel( RSocket &aSock, TUint aPsm )
{
	TBTSockAddr         addr;
	TBTServiceSecurity  sec;

	sec.SetAuthentication( ETrue ); // si no esta emparejado, el telefono pide el codigo (los mouse: 0000)
	sec.SetEncryption( ETrue );     // los teclados lo exigen
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
		Log( "conectado", err );
		iState = EReading;
		iIntr.Recv( iBuf, 0, iStatus );
		SetActive();
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
	iCmd.SetLength( 1 );
	iCmd[0] = KHidpSetProtoBoot;
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

void CBtHidDevice::Report( const TDesC8 &aData )
{
	if( aData.Length() < 2 || aData[0] != KHidpDataInput )
		return;

	// tipo sin saber (el cuadro no dio la clase del dispositivo): el reporte boot del teclado es de 9 bytes
	if( !iType )
		iType = ( aData.Length() == 9 && aData[2] == 0 ) ? EBtHidKeyboard : EBtHidMouse;

	if( iType == EBtHidKeyboard )
	{
		Keyboard( aData );
		return;
	}
	if( aData.Length() < 4 )
		return;
	iButtons = aData[1] & 7;
	iHost.Observer().BtHidMouse( (TInt8)aData[2], (TInt8)aData[3], aData.Length() >= 5 ? (TInt8)aData[4] : 0, iButtons );
}

void CBtHidDevice::Keyboard( const TDesC8 &aData )
{
	TUint8 mods, keys[6];
	TInt   i, j;

	if( aData.Length() < 9 )
		return;
	mods = aData[1];
	for( i = 0; i < 6; i++ )
		keys[i] = aData[3 + i];
	if( keys[0] == 1 ) // ErrorRollOver: demasiadas teclas juntas, el reporte no dice cuales
		return;

	for( i = 0; i < 8; i++ )
		if(( mods ^ iMods ) & ( 1 << i ))
			iHost.Observer().BtHidKey( 224 + i, ( mods >> i ) & 1 );

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

	iMods = mods;
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
			sec.SetAuthentication( ETrue );
			sec.SetEncryption( ETrue );
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
void CBtHidHost::AddDevice( const TBTDevAddr &aAddr, TInt aType, const TDesC8 &aName )
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
	iDev[slot] = new CBtHidDevice( *this, aAddr, aType, aName );
	if( iDev[slot] )
		iDev[slot]->Start();
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
	AddDevice( iResp().BDAddr(), type, name );
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

/* archivo: por dispositivo 6 bytes de direccion, 1 de tipo, 1 de largo del nombre y el nombre */
void CBtHidHost::Load()
{
	RFs   fs;
	RFile f;
	TBuf8<40 * EMaxDevices> data;
	TInt  i;

	if( fs.Connect() != KErrNone )
		return;
	if( f.Open( fs, iFile, EFileRead ) == KErrNone )
	{
		f.Read( data );
		f.Close();
	}
	BtHidCloseFs( fs );
	for( i = 0; i + 8 <= data.Length(); )
	{
		TInt len = data[i + 7];
		if( i + 8 + len > data.Length())
			break;
		AddDevice( TBTDevAddr( data.Mid( i, 6 )), data[i + 6], data.Mid( i + 8, len ));
		i += 8 + len;
	}
}

void CBtHidHost::Save()
{
	RFs   fs;
	RFile f;
	TBuf8<40 * EMaxDevices> data;
	TInt  i;

	for( i = 0; i < EMaxDevices; i++ )
	{
		if( !iDev[i] ) continue;
		data.Append( iDev[i]->Addr().Des());
		data.Append( (TUint8)iDev[i]->Type());
		data.Append( (TUint8)iDev[i]->Name().Length());
		data.Append( iDev[i]->Name());
	}
	if( fs.Connect() != KErrNone )
		return;
	if( f.Replace( fs, iFile, EFileWrite ) == KErrNone )
	{
		f.Write( data );
		f.Close();
	}
	BtHidCloseFs( fs );
}
