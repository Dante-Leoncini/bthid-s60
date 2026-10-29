/*
bthid_host.cpp - conexion directa con mouse y teclado Bluetooth, ver bthid_host.h

Por dispositivo: conectar el canal de control (PSM 0x11) y el de datos (0x13), pedir el protocolo boot
(SET_PROTOCOL) y leer reportes del canal de datos. En boot los reportes tienen formato fijo:
  mouse:   A1 botones dx dy [rueda ...]
  teclado: A1 modificadores 00 tecla1..tecla6
Si se corta (el mouse se duerme, se apaga, queda lejos) se reintenta cada pocos segundos.
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

class CBtHidDevice : public CActive
{
public:
	CBtHidDevice( CBtHidHost &aHost, const TBTDevAddr &aAddr, TInt aType, const TDesC8 &aName );
	~CBtHidDevice();
	void Start();
	const TBTDevAddr &Addr() const { return iAddr; }
	TInt Type() const { return iType; }
	const TDesC8 &Name() const { return iName; }
	TInt State() const { return iConnected ? EBtHidConnected : iState == EWaitRetry ? EBtHidDisconnected : EBtHidConnecting; }
	TInt Error() const { return iError; }

private:
	enum TState { EIdle, EConnCtrl, EConnIntr, ESetProto, EReading, EWaitRetry };

	void RunL();
	void DoCancel();
	TInt RunError( TInt aError );
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
	else if( iFails == 0 )
		Log( "no se pudo conectar, reintentando", aErr );
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
		iCmd.SetLength( 1 );
		iCmd[0] = KHidpSetProtoBoot;
		iState = ESetProto;
		iCtrl.Write( iCmd, iStatus );
		SetActive();
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

TInt CBtHidDevice::RunError( TInt aError )
{
	Retry( aError );
	return KErrNone;
}

void CBtHidDevice::DoCancel()
{
	if( iState == EWaitRetry )
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
}

CBtHidHost::~CBtHidHost()
{
	TInt i;

	Cancel();
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
	fs.Close();
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
	fs.Close();
}
