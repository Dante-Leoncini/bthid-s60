/*
manager.cpp - bthidmgr.exe, la app "Bluetooth HID": buscar, ver y olvidar el mouse y el teclado del servidor

Opciones > Buscar dispositivo abre el cuadro de Bluetooth del telefono; el elegido queda guardado en el
servidor y se reconecta solo cada vez que una app lo usa. Abajo hay una prueba: la app recibe los eventos como
cualquier juego (GetHIDClientAPI) y muestra los botones apretados, el movimiento, la rueda y la ultima tecla.
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
#include "bthidclient.h"
#include "bthidmgr.hrh"

const TUid KUidBtHidMgr = { 0xE0D69652 };

/* ------------------------------------------------------------------ */

/* lo que muestra la prueba */
struct TMgrTest
{
	TInt   iButtons;   // bits 0 izquierdo, 1 derecho, 2 medio
	TPoint iPos;       // punto que se mueve con el mouse (dentro del recuadro)
	TInt   iWheel;
	TInt   iKey;       // ultima tecla (codigo HID), 0 = ninguna
	TBool  iKeyDown;
	TInt   iEvents;
	TInt   iError;     // si no se pudo conectar para recibir eventos
};

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
	void SetTest( const TMgrTest *aTest ) { iTest = aTest; }

	/* recuadro de la prueba: la mitad de abajo de la pantalla */
	TRect TestRect() const
	{
		TRect r = Rect();
		r.iTl.iY = r.iTl.iY + r.Height() / 2;
		r.Shrink( 6, 6 );
		return r;
	}

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
		if( iTest )
			DrawTest( gc, font );
		gc.DiscardFont();
	}

	void DrawTest( CWindowGc &gc, const CFont *font ) const
	{
		_LIT( KNames, "IMD" ); // izquierdo, medio, derecho (en el orden del mouse)
		static const TInt bits[3] = { 1, 4, 2 };
		TRect    area = TestRect();
		TBuf<64> text;
		TInt     i, bw = 28, bh = 20;

		gc.SetBrushStyle( CGraphicsContext::ENullBrush );
		gc.SetPenColor( KRgbDarkGray );
		gc.DrawRect( area );

		// botones arriba a la izquierda del recuadro: llenos mientras estan apretados
		for( i = 0; i < 3; i++ )
		{
			TRect b( TPoint( area.iTl.iX + 4 + i * ( bw + 4 ), area.iTl.iY + 4 ), TSize( bw, bh ));
			TBool down = ( iTest->iButtons & bits[i] ) != 0;
			gc.SetBrushStyle( down ? CGraphicsContext::ESolidBrush : CGraphicsContext::ENullBrush );
			gc.SetBrushColor( KRgbRed );
			gc.SetPenColor( KRgbBlack );
			gc.DrawRect( b );
			gc.SetBrushStyle( CGraphicsContext::ENullBrush );
			gc.DrawText( KNames().Mid( i, 1 ), TPoint( b.iTl.iX + bw / 2 - 4, b.iTl.iY + font->AscentInPixels() + 2 ));
		}

		// el punto del mouse
		gc.SetBrushStyle( CGraphicsContext::ESolidBrush );
		gc.SetBrushColor( KRgbBlue );
		gc.SetPenColor( KRgbBlue );
		gc.DrawEllipse( TRect( iTest->iPos - TPoint( 3, 3 ), TSize( 7, 7 )));
		gc.SetBrushStyle( CGraphicsContext::ENullBrush );
		gc.SetPenColor( KRgbBlack );

		if( iTest->iError )
			text.Format( _L( "sin eventos (%d)" ), iTest->iError );
		else if( iTest->iKey )
		{
			_LIT( KDown, "abajo" );
			_LIT( KUp, "arriba" );
			const TDesC &state = iTest->iKeyDown ? (const TDesC &)KDown : (const TDesC &)KUp;
			text.Format( _L( "rueda %d  tecla 0x%02x %S  (%d)" ), iTest->iWheel, iTest->iKey, &state, iTest->iEvents );
		}
		else
			text.Format( _L( "rueda %d  eventos %d" ), iTest->iWheel, iTest->iEvents );
		gc.DrawText( text, TPoint( area.iTl.iX + 4, area.iBr.iY - 4 ));
	}

	const CDesCArray *iLines;
	const TMgrTest   *iTest;
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

class CEvents : public CActive
{
public:
	CEvents( CMgrAppUi &aUi ) : CActive( EPriorityStandard ), iUi( aUi ) { CActiveScheduler::Add( this ); }
	~CEvents()
	{
		Cancel();
		if( iClient )
		{
			iClient->Close();
			delete iClient;
		}
	}
	TInt Start()
	{
		TInt err;
		iClient = GetHIDClientAPI();
		if( !iClient )
			return KErrNoMemory;
		err = iClient->Connect();
		if( err != KErrNone )
			return err;
		iClient->EventReady( &iStatus );
		SetActive();
		return KErrNone;
	}

private:
	void RunL();
	void DoCancel() { iClient->EventReadyCancel(); }

	CMgrAppUi     &iUi;
	MHIDSrvClient *iClient;
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
		iView->SetTest( &iTest );
		iTest.iPos = iView->TestRect().Center();
		iEvents = new( ELeave ) CEvents( *this );
		iTest.iError = iEvents->Start();
		iTimer = CPeriodic::NewL( CActive::EPriorityStandard );
		iTimer->Start( 0, 1000000, TCallBack( Tick, this ));
	}
	~CMgrAppUi()
	{
		delete iTimer;
		delete iEvents;
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
	void HidEvent( const THIDEvent &aEv );

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
	CEvents       *iEvents;
	TMgrTest       iTest;
};

void CEvents::RunL()
{
	THIDEvent ev;
	if( iStatus.Int() != KErrNone )
		return; // el servidor se cerro
	iClient->GetEvent( ev );
	iUi.HidEvent( ev );
	iClient->EventReady( &iStatus );
	SetActive();
}

void CMgrAppUi::HidEvent( const THIDEvent &aEv )
{
	iTest.iEvents++;
	if( aEv.Type() == THIDEvent::EMouseEvent )
	{
		TMouseEvent *m = aEv.Mouse();
		TInt bit = m->iValue == EMouseButtonRight ? 2 : m->iValue == EMouseButtonMiddle ? 4 : 1;
		TRect area = iView->TestRect();
		switch( m->iType )
		{
		case EEventRelativeXY:
			iTest.iPos += m->iPosition;
			if( iTest.iPos.iX < area.iTl.iX ) iTest.iPos.iX = area.iTl.iX;
			if( iTest.iPos.iY < area.iTl.iY ) iTest.iPos.iY = area.iTl.iY;
			if( iTest.iPos.iX > area.iBr.iX ) iTest.iPos.iX = area.iBr.iX;
			if( iTest.iPos.iY > area.iBr.iY ) iTest.iPos.iY = area.iBr.iY;
			break;
		case EEventButtonDown: iTest.iButtons |= bit; break;
		case EEventButtonUp:   iTest.iButtons &= ~bit; break;
		case EEventRelativeWheel: iTest.iWheel += m->iValue; break;
		default: break;
		}
	}
	else if( aEv.Type() == THIDEvent::EKeyEvent )
	{
		iTest.iKey = aEv.Key()->ScanCode();
		iTest.iKeyDown = aEv.Key()->Type() == EEventHIDKeyDown;
	}
	iView->DrawDeferred();
}

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
	for( i = 0; i < st().iCount; i++ )
		if( st().iDevice[i].iState != EBtHidConnected )
		{
			AddLine( _L( "Si no conecta: mouse en modo" ));
			AddLine( _L( "emparejar y Buscar de nuevo" ));
			break;
		}
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
