/*
manager.cpp - bthidmgr.exe, la app "Bluetooth HID": buscar, ver y olvidar el mouse y el teclado del servidor

Opciones > Buscar dispositivo abre el cuadro de Bluetooth del telefono; el elegido queda guardado en el
servidor y se reconecta solo cada vez que una app lo usa.
*/

#include <aknapp.h>
#include <akndoc.h>
#include <aknappui.h>
#include <eikstart.h>
#include <coecntrl.h>
#include <eikenv.h>
#include <gdi.h>
#include <badesca.h>
#include <avkon.hrh>
#include "bthidmanager.h"
#include "bthidmgr.hrh"

const TUid KUidBtHidMgr = { 0xE0D69652 };

/* ------------------------------------------------------------------ */

class CMgrView : public CCoeControl
{
public:
	void ConstructL( const TRect &aRect )
	{
		CreateWindowL();
		SetRect( aRect );
		ActivateL();
	}
	void SetLines( const CDesCArray *aLines ) { iLines = aLines; DrawDeferred(); }

private:
	void Draw( const TRect & ) const
	{
		CWindowGc &gc = SystemGc();
		const CFont *font = CEikonEnv::Static()->NormalFont();
		TInt y, i;

		gc.SetBrushStyle( CGraphicsContext::ESolidBrush );
		gc.SetBrushColor( KRgbWhite );
		gc.Clear( Rect());
		gc.UseFont( font );
		gc.SetPenColor( KRgbBlack );
		y = font->AscentInPixels() + 6;
		for( i = 0; iLines && i < iLines->Count(); i++ )
		{
			gc.DrawText( ( *iLines )[i], TPoint( 6, y ));
			y += font->HeightInPixels() + 4;
		}
		gc.DiscardFont();
	}

	const CDesCArray *iLines;
};

/* ------------------------------------------------------------------ */

class CMgrAppUi;

class CSearch : public CActive
{
public:
	CSearch( RBtHidManager &aMgr, CMgrAppUi &aUi ) : CActive( EPriorityStandard ), iMgr( aMgr ), iUi( aUi )
	{
		CActiveScheduler::Add( this );
	}
	~CSearch() { Cancel(); }
	void Start()
	{
		if( IsActive()) return;
		iMgr.Search( iStatus );
		SetActive();
	}
	TInt LastError() const { return iLast; }

private:
	void RunL();
	void DoCancel() { iMgr.CancelSearch(); }

	RBtHidManager &iMgr;
	CMgrAppUi     &iUi;
	TInt           iLast;
};

class CMgrAppUi : public CAknAppUi
{
public:
	void ConstructL()
	{
		BaseConstructL( EAknEnableSkin );
		iLines = new( ELeave ) CDesCArrayFlat( 8 );
		iView = new( ELeave ) CMgrView;
		iView->ConstructL( ClientRect());
		AddToStackL( iView );
		iConnectErr = iMgr.Connect();
		iSearch = new( ELeave ) CSearch( iMgr, *this );
		iTimer = CPeriodic::NewL( CActive::EPriorityStandard );
		iTimer->Start( 0, 1000000, TCallBack( Tick, this ));
	}
	~CMgrAppUi()
	{
		delete iTimer;
		delete iSearch;
		iMgr.Close();
		if( iView )
		{
			RemoveFromStack( iView );
			delete iView;
		}
		delete iLines;
	}
	void Refresh();

private:
	static TInt Tick( TAny *aPtr ) { static_cast<CMgrAppUi *>( aPtr )->Refresh(); return 1; }

	void HandleCommandL( TInt aCommand )
	{
		switch( aCommand )
		{
		case EBtHidCmdSearch:
			if( iConnectErr == KErrNone )
				iSearch->Start();
			break;
		case EBtHidCmdForget:
			if( iConnectErr == KErrNone )
				iMgr.ForgetAll();
			Refresh();
			break;
		case EAknSoftkeyExit:
		case EAknCmdExit:
		case EEikCmdExit:
			Exit();
			break;
		default:
			break;
		}
	}

	void AddLine( const TDesC &aText )
	{
		TRAP_IGNORE( iLines->AppendL( aText ));
	}

	RBtHidManager  iMgr;
	TInt           iConnectErr;
	CMgrView      *iView;
	CDesCArray    *iLines;
	CSearch       *iSearch;
	CPeriodic     *iTimer;
};

void CSearch::RunL()
{
	iLast = iStatus.Int();
	iUi.Refresh();
}

void CMgrAppUi::Refresh()
{
	TBtHidStatusPckg st;
	TBuf<96>         line;
	TInt             i;

	iLines->Reset();
	AddLine( _L( "Bluetooth HID" ));
	if( iConnectErr != KErrNone )
	{
		line.Format( _L( "No arranca el servidor (%d)" ), iConnectErr );
		AddLine( line );
		iView->SetLines( iLines );
		return;
	}
	if( iMgr.Status( st ) != KErrNone )
	{
		AddLine( _L( "El servidor no responde" ));
		iView->SetLines( iLines );
		return;
	}

	if( !st().iCount )
	{
		AddLine( _L( "Sin dispositivos." ));
		AddLine( _L( "Opciones > Buscar dispositivo" ));
	}
	for( i = 0; i < st().iCount; i++ )
	{
		const TBtHidDeviceInfo &d = st().iDevice[i];
		TBuf<32> name;
		name.Copy( d.iName );
		if( !name.Length())
			name.Format( _L( "%02x:%02x:%02x:%02x:%02x:%02x" ), d.iAddr[0], d.iAddr[1], d.iAddr[2], d.iAddr[3], d.iAddr[4], d.iAddr[5] );
		_LIT( KKeyboard, "Teclado" );
		_LIT( KMouse, "Mouse" );
		const TDesC &kind = d.iType == EBtHidKeyboard ? (const TDesC &)KKeyboard : (const TDesC &)KMouse;
		line.Format( _L( "%S: %S" ), &kind, &name );
		AddLine( line );
		if( d.iState == EBtHidConnected )
			line = _L( "   conectado" );
		else if( d.iState == EBtHidConnecting )
			line = _L( "   conectando..." );
		else
			line.Format( _L( "   sin conexion (%d), reintentando" ), d.iError );
		AddLine( line );
	}
	line.Format( _L( "Apps usandolo: %d" ), st().iClients );
	AddLine( line );
	if( iSearch->LastError() != KErrNone && iSearch->LastError() != KErrCancel )
	{
		line.Format( _L( "La busqueda fallo (%d)" ), iSearch->LastError());
		AddLine( line );
	}
	AddLine( _L( "Si el driver de Hinkka tiene el" ));
	AddLine( _L( "mouse conectado, desconectalo." ));
	iView->SetLines( iLines );
}

/* ------------------------------------------------------------------ */

class CMgrDocument : public CAknDocument
{
public:
	CMgrDocument( CEikApplication &aApp ) : CAknDocument( aApp ) {}
	CEikAppUi *CreateAppUiL() { return new( ELeave ) CMgrAppUi; }
};

class CMgrApplication : public CAknApplication
{
public:
	TUid AppDllUid() const { return KUidBtHidMgr; }
	CApaDocument *CreateDocumentL() { return new( ELeave ) CMgrDocument( *this ); }
};

LOCAL_C CApaApplication *NewApplication()
{
	return new CMgrApplication;
}

GLDEF_C TInt E32Main()
{
	return EikStart::RunApplication( NewApplication );
}
