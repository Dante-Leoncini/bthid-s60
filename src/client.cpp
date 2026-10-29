/*
client.cpp - bthidhost.dll: la misma API que el hidsrv.dll de Hinkka (bthidclient.h), contra nuestro servidor

Ordinal 1 = GetHIDClientAPI, como en el de Hinkka: las apps que hacen RLibrary::Load + Lookup( 1 ) solo
cambian el nombre de la DLL. La diferencia es que los botones del mouse llegan apretados y soltados de verdad.
*/

#include <e32base.h>
#include "bthidclient.h"
#include "bthidmanager.h"
#include "bthidproto.h"

TInt BtHidStartServer()
{
	RProcess       server;
	TRequestStatus status;
	TInt           err;

	err = server.Create( KBtHidServerExe, KNullDesC );
	if( err != KErrNone )
		return err;
	server.Rendezvous( status );
	if( status != KRequestPending )
		server.Kill( KErrNone );
	else
		server.Resume();
	User::WaitForRequest( status );
	err = ( server.ExitType() == EExitPanic ) ? KErrGeneral : status.Int();
	server.Close();
	return err;
}

/* abrir la sesion arrancando el servidor si hace falta (dos intentos: puede arrancar otra app a la vez) */
template <class T> static TInt OpenSession( T &aSession )
{
	TInt err, retry;

	for( retry = 0; retry < 2; retry++ )
	{
		err = aSession.Create();
		if( err != KErrNotFound && err != KErrServerTerminated )
			return err;
		err = BtHidStartServer();
		if( err != KErrNone && err != KErrAlreadyExists )
			return err;
	}
	return aSession.Create();
}

/* ------------------------------------------------------------------ */

class RBtHidSession : public RSessionBase
{
public:
	TInt Create() { return CreateSession( KBtHidServerName, BtHidVersion(), 2 ); }
	void WaitEvent( TDes8 &aEvent, TRequestStatus &aStatus ) { SendReceive( EBtHidWaitEvent, TIpcArgs( &aEvent ), aStatus ); }
	void CancelWait() { SendReceive( EBtHidCancelWait, TIpcArgs()); }
};

class CBtHidClient : public MHIDSrvClient
{
public:
	TInt Connect()
	{
		return OpenSession( iSession );
	}
	void Close()
	{
		iSession.Close();
	}
	void EventReady( TRequestStatus *aStatus )
	{
		iSession.WaitEvent( iEvent, *aStatus );
	}
	void GetEvent( THIDEvent &aEvent )
	{
		aEvent = iEvent();
	}
	void EventReadyCancel()
	{
		iSession.CancelWait();
	}
	~CBtHidClient()
	{
		iSession.Close();
	}

private:
	RBtHidSession        iSession;
	TPckgBuf<THIDEvent>  iEvent;
};

EXPORT_C MHIDSrvClient *GetHIDClientAPI()
{
	return new CBtHidClient;
}

/* ------------------------------------------------------------------ */

class RBtHidManagerSession : public RBtHidManager
{
public:
	TInt Create() { return CreateSession( KBtHidServerName, BtHidVersion(), 2 ); }
};

EXPORT_C TInt RBtHidManager::Connect()
{
	return OpenSession( *static_cast<RBtHidManagerSession *>( this ));
}

EXPORT_C void RBtHidManager::Search( TRequestStatus &aStatus )
{
	SendReceive( EBtHidSearch, TIpcArgs(), aStatus );
}

EXPORT_C void RBtHidManager::CancelSearch()
{
	SendReceive( EBtHidCancelSearch, TIpcArgs());
}

EXPORT_C TInt RBtHidManager::ForgetAll()
{
	return SendReceive( EBtHidForgetAll, TIpcArgs());
}

EXPORT_C TInt RBtHidManager::Status( TBtHidStatusPckg &aStatus )
{
	return SendReceive( EBtHidStatus, TIpcArgs( &aStatus ));
}
