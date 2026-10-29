/*
server.cpp - bthidhostsrv.exe: se queda con las conexiones Bluetooth del mouse y el teclado y reparte los
eventos a las apps conectadas (cada una con su cola), en el formato THIDEvent de siempre (bthidevent.h).

A diferencia del hidsrv de Hinkka, los botones llegan como apretado (EEventButtonDown) al apretar y soltado
(EEventButtonUp) al soltar, de verdad: se puede mantener apretado y arrastrar.

Arranca con la primera app que se conecta (bthidhost.dll o la app "Bluetooth HID") y se cierra un rato
despues de que se va la ultima.
*/

#include <e32base.h>
#include <f32file.h>
#include "bthidproto.h"
#include "bthidevent.h"
#include "bthid_host.h"

const TInt KQueueSize     = 128;       // eventos por app (si una app no lee, se pierden los mas viejos)
const TInt KShutdownDelay = 60000000;  // us sin apps conectadas antes de cerrar (y soltar el Bluetooth)

class CBtHidSession;

class CBtHidServer : public CServer2, public MBtHidObserver
{
public:
	static CBtHidServer *NewLC();
	~CBtHidServer();

	void AddSession();
	void DropSession( CBtHidSession *aSession );
	void Push( const THIDEvent &aEvent );
	CBtHidHost *Host() { return iHost; }
	void Search( const RMessage2 &aMessage );
	void CancelSearch( CBtHidSession *aSession );
	TInt Clients() const;

	// MBtHidObserver
	void BtHidMouse( TInt aDx, TInt aDy, TInt aWheel, TInt aButtons );
	void BtHidKey( TInt aUsage, TBool aDown );
	void BtHidSearchDone( TInt aError );
	void BtHidLog( const char *aMsg );

private:
	CBtHidServer();
	void ConstructL();
	CSession2 *NewSessionL( const TVersion &aVersion, const RMessage2 &aMessage ) const;
	static TInt Shutdown( TAny * );

	CBtHidHost    *iHost;
	CPeriodic     *iShutdown;
	TInt           iSessions;
	TInt           iButtons;
	RMessagePtr2   iSearchMsg;
	CBtHidSession *iSearchSession;
	TDblQue<CBtHidSession> iQueue;
	friend class CBtHidSession;
};

class CBtHidSession : public CSession2
{
public:
	CBtHidSession();
	~CBtHidSession();
	void Push( const THIDEvent &aEvent );
	TBool WantsEvents() const { return iWantsEvents; }

	TDblQueLink iLink;

private:
	void CreateL();
	void ServiceL( const RMessage2 &aMessage );
	void Deliver();
	CBtHidServer &Server() { return *static_cast<CBtHidServer *>( const_cast<CServer2 *>( CSession2::Server())); }

	THIDEvent    iEvents[KQueueSize];
	TInt         iHead, iCount;
	TBool        iWantsEvents;
	RMessagePtr2 iWait;
};

/* ------------------------------------------------------------------ */

CBtHidSession::CBtHidSession() {}

void CBtHidSession::CreateL()
{
	Server().AddSession();
	Server().iQueue.AddLast( *this );
}

CBtHidSession::~CBtHidSession()
{
	if( !iWait.IsNull())
		iWait.Complete( KErrCancel );
	iLink.Deque();
	Server().DropSession( this );
}

void CBtHidSession::Push( const THIDEvent &aEvent )
{
	if( !iWantsEvents )
		return;
	if( iCount == KQueueSize ) // lleno: tirar el mas viejo
	{
		iHead = ( iHead + 1 ) % KQueueSize;
		iCount--;
	}
	iEvents[( iHead + iCount ) % KQueueSize] = aEvent;
	iCount++;
	Deliver();
}

void CBtHidSession::Deliver()
{
	if( iWait.IsNull() || !iCount )
		return;
	TPckgC<THIDEvent> ev( iEvents[iHead] );
	TInt err = iWait.Write( 0, ev );
	iHead = ( iHead + 1 ) % KQueueSize;
	iCount--;
	iWait.Complete( err );
}

void CBtHidSession::ServiceL( const RMessage2 &aMessage )
{
	switch( aMessage.Function())
	{
	case EBtHidWaitEvent:
		if( !iWait.IsNull())
		{
			aMessage.Complete( KErrInUse );
			return;
		}
		iWantsEvents = ETrue;
		iWait = aMessage;
		Deliver();
		return;
	case EBtHidCancelWait:
		if( !iWait.IsNull())
			iWait.Complete( KErrCancel );
		aMessage.Complete( KErrNone );
		return;
	case EBtHidSearch:
		Server().Search( aMessage );
		return;
	case EBtHidCancelSearch:
		Server().CancelSearch( this );
		aMessage.Complete( KErrNone );
		return;
	case EBtHidForgetAll:
		if( Server().Host())
			Server().Host()->ForgetAll();
		aMessage.Complete( KErrNone );
		return;
	case EBtHidStatus:
		{
			TBtHidStatusPckg st;
			st().iCount = 0; // sin Mem::FillZ: pondria en 0 el largo maximo de los TBuf8 de los nombres (USER 23)
			if( Server().Host())
				Server().Host()->GetStatus( st());
			st().iClients = Server().Clients();
			aMessage.WriteL( 0, st );
			aMessage.Complete( KErrNone );
		}
		return;
	default:
		aMessage.Complete( KErrNotSupported );
		return;
	}
}

/* ------------------------------------------------------------------ */

CBtHidServer::CBtHidServer()
	: CServer2( EPriorityStandard ), iQueue( _FOFF( CBtHidSession, iLink ))
{
}

CBtHidServer *CBtHidServer::NewLC()
{
	CBtHidServer *self = new( ELeave ) CBtHidServer;
	CleanupStack::PushL( self );
	self->ConstructL();
	return self;
}

void CBtHidServer::ConstructL()
{
	RFs      fs;
	TFileName file;

	StartL( KBtHidServerName );
	iShutdown = CPeriodic::NewL( EPriorityIdle );

	// los dispositivos elegidos se guardan en la carpeta privada del servidor (c:\private\e0d69650)
	User::LeaveIfError( fs.Connect());
	fs.CreatePrivatePath( EDriveC );
	fs.PrivatePath( file );
	BtHidCloseFs( fs );
	file.Insert( 0, _L( "c:" ));
	file.Append( _L( "devices.dat" ));
	iHost = CBtHidHost::NewL( *this, file );
}

CBtHidServer::~CBtHidServer()
{
	delete iHost;
	iHost = NULL;
	delete iShutdown;
}

CSession2 *CBtHidServer::NewSessionL( const TVersion &aVersion, const RMessage2 & ) const
{
	if( !User::QueryVersionSupported( BtHidVersion(), aVersion ))
		User::Leave( KErrNotSupported );
	return new( ELeave ) CBtHidSession;
}

void CBtHidServer::AddSession()
{
	iSessions++;
	iShutdown->Cancel();
}

void CBtHidServer::DropSession( CBtHidSession *aSession )
{
	if( iSearchSession == aSession )
	{
		iSearchSession = NULL;
		if( !iSearchMsg.IsNull())
			iSearchMsg.Complete( KErrCancel );
		if( iHost )
			iHost->CancelSelect();
	}
	if( --iSessions <= 0 && iShutdown )
	{
		iShutdown->Cancel();
		iShutdown->Start( KShutdownDelay, KShutdownDelay, TCallBack( Shutdown, this ));
	}
}

TInt CBtHidServer::Shutdown( TAny * )
{
	CActiveScheduler::Stop();
	return 0;
}

TInt CBtHidServer::Clients() const
{
	TInt n = 0;
	TDblQueIter<CBtHidSession> it( const_cast<TDblQue<CBtHidSession> &>( iQueue ));
	CBtHidSession *s;
	while(( s = it++ ) != NULL )
		if( s->WantsEvents()) n++;
	return n;
}

void CBtHidServer::Push( const THIDEvent &aEvent )
{
	TDblQueIter<CBtHidSession> it( iQueue );
	CBtHidSession *s;
	while(( s = it++ ) != NULL )
		s->Push( aEvent );
}

void CBtHidServer::Search( const RMessage2 &aMessage )
{
	if( !iHost )
	{
		aMessage.Complete( KErrNotReady );
		return;
	}
	if( !iSearchMsg.IsNull())
	{
		aMessage.Complete( KErrInUse );
		return;
	}
	iSearchMsg = aMessage;
	iSearchSession = static_cast<CBtHidSession *>( aMessage.Session());
	iHost->SelectDevice();
}

void CBtHidServer::CancelSearch( CBtHidSession *aSession )
{
	if( iSearchSession == aSession && iHost )
		iHost->CancelSelect();
}

void CBtHidServer::BtHidSearchDone( TInt aError )
{
	iSearchSession = NULL;
	if( !iSearchMsg.IsNull())
		iSearchMsg.Complete( aError );
}

static void MouseEvent( THIDEvent &aEv, TMouseEventType aType, TInt aValue, TInt aX, TInt aY )
{
	aEv.SetType( THIDEvent::EMouseEvent );
	TMouseEvent *m = aEv.Mouse();
	m->iType = aType;
	m->iValue = aValue;
	m->iPosition.iX = aX;
	m->iPosition.iY = aY;
}

void CBtHidServer::BtHidMouse( TInt aDx, TInt aDy, TInt aWheel, TInt aButtons )
{
	static const TInt ids[3] = { EMouseButtonLeft, EMouseButtonRight, EMouseButtonMiddle };
	THIDEvent ev;
	TInt b;

	if( aDx || aDy )
	{
		MouseEvent( ev, EEventRelativeXY, 0, aDx, aDy );
		Push( ev );
	}
	if( aWheel )
	{
		MouseEvent( ev, EEventRelativeWheel, aWheel, 0, 0 );
		Push( ev );
	}
	for( b = 0; b < 3; b++ )
	{
		if(!(( aButtons ^ iButtons ) & ( 1 << b )))
			continue;
		MouseEvent( ev, ( aButtons >> b ) & 1 ? EEventButtonDown : EEventButtonUp, ids[b], 0, 0 );
		Push( ev );
	}
	iButtons = aButtons;
}

void CBtHidServer::BtHidKey( TInt aUsage, TBool aDown )
{
	THIDEvent ev;
	ev.SetType( THIDEvent::EKeyEvent );
	THIDKeyEvent *k = ev.Key();
	k->iType = aDown ? EEventHIDKeyDown : EEventHIDKeyUp;
	k->iScanCode = aUsage;
	Push( ev );
}

/* log en c:\data\bthid_log.txt: solo conexiones y errores (pocos) */
void CBtHidServer::BtHidLog( const char *aMsg )
{
	_LIT( KLog, "c:\\data\\bthid_log.txt" );
	RFs   fs;
	RFile f;
	TInt  pos = 0;

	if( fs.Connect() != KErrNone )
		return;
	if( f.Open( fs, KLog, EFileWrite ) != KErrNone && f.Create( fs, KLog, EFileWrite ) != KErrNone )
	{
		BtHidCloseFs( fs );
		return;
	}
	f.Seek( ESeekEnd, pos );
	f.Write( TPtrC8( (const TUint8 *)aMsg, User::StringLength( (const TUint8 *)aMsg )));
	f.Write( _L8( "\r\n" ));
	f.Close();
	BtHidCloseFs( fs );
}

/* ------------------------------------------------------------------ */

static void RunServerL()
{
	CActiveScheduler *sched = new( ELeave ) CActiveScheduler;
	CleanupStack::PushL( sched );
	CActiveScheduler::Install( sched );

	User::LeaveIfError( User::RenameThread( KBtHidServerName ));
	CBtHidServer::NewLC();
	RProcess::Rendezvous( KErrNone );
	CActiveScheduler::Start();

	CleanupStack::PopAndDestroy( 2, sched );
}

GLDEF_C TInt E32Main()
{
	__UHEAP_MARK;
	CTrapCleanup *cleanup = CTrapCleanup::New();
	TInt err = KErrNoMemory;
	if( cleanup )
	{
		TRAP( err, RunServerL());
		delete cleanup;
	}
	__UHEAP_MARKEND;
	return err;
}
