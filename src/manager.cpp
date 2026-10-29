/*
manager.cpp - bthidmgr.exe, la app "Bluetooth HID": buscar, ver y olvidar el mouse y el teclado del servidor,
y probarlos.

Opciones > Buscar dispositivo abre el cuadro de Bluetooth del telefono; el elegido queda guardado en el
servidor y se reconecta solo cada vez que una app lo usa. Opciones > Pruebas: la app recibe los eventos como
cualquier juego (GetHIDClientAPI). En la del mouse se arrastra un cuadrado con cualquiera de los tres botones y
la rueda le cambia el tamano; en la del teclado se ven las teclas apretadas y lo que se escribe.

Textos en ingles o en castellano segun el idioma del telefono.
*/

#include <aknapp.h>
#include <akndoc.h>
#include <aknappui.h>
#include <eikstart.h>
#include <coecntrl.h>
#include <eikenv.h>
#include <eikmenup.h>
#include <eikbtgpc.h>
#include <gdi.h>
#include <fbs.h>
#include <badesca.h>
#include <avkon.hrh>
#include <avkon.rsg>
#include <bthidmgr.rsg>
#include "bthidmanager.h"
#include "bthidclient.h"
#include "bthidmgr.hrh"
#include "cursor.h"

const TUid KUidBtHidMgr = { 0xE0D69652 };

/* ------------------------------------------------------------------ */
/* textos */

enum TTextId
{
	ETxtNoDevices, ETxtFindHint, ETxtKeyboard, ETxtMouse, ETxtConnected, ETxtConnecting, ETxtNotConnected,
	ETxtApps, ETxtSearchFailed, ETxtHint1, ETxtHint2, ETxtNoServer, ETxtServerDead,
	ETxtMenuFind, ETxtMenuTests, ETxtMenuTestMouse, ETxtMenuTestKeys, ETxtMenuForget, ETxtMenuExit,
	ETxtMouseHelp1, ETxtMouseHelp2, ETxtMouseSize, ETxtKeysHelp, ETxtKeysHeld, ETxtDown, ETxtUp, ETxtNoEvents,
	ETxtWaiting, ETxtNone, ETxtMenuPickMouse, ETxtMenuPickKeys, ETxtMenuForgetMouse, ETxtMenuForgetKeys,
	ETxtCount
};

static const TText *const KTextEn[ETxtCount] =
{
	_S( "No devices." ),
	_S( "Options > Find device" ),
	_S( "Keyboard" ),
	_S( "Mouse" ),
	_S( "   connected" ),
	_S( "   connecting..." ),
	_S( "   not connected (%d), retrying" ),
	_S( "Apps using it: %d" ),
	_S( "Search failed (%d)" ),
	_S( "Not connecting? Put the device" ),
	_S( "in pairing mode and Find again" ),
	_S( "The server won't start (%d)" ),
	_S( "The server is not responding" ),
	_S( "Find device" ),
	_S( "Tests" ),
	_S( "Test mouse" ),
	_S( "Test keyboard" ),
	_S( "Forget all" ),
	_S( "Exit" ),
	_S( "Drag the square with any button" ),
	_S( "Wheel: bigger / smaller" ),
	_S( "size %d" ),
	_S( "Type on the Bluetooth keyboard" ),
	_S( "Held: " ),
	_S( "down" ),
	_S( "up" ),
	_S( "no events (%d)" ),
	_S( "   waiting: move it or click" ),
	_S( "none" ),
	_S( "Choose mouse" ),
	_S( "Choose keyboard" ),
	_S( "Remove mouse" ),
	_S( "Remove keyboard" ),
};

static const TText *const KTextEs[ETxtCount] =
{
	_S( "Sin dispositivos." ),
	_S( "Opciones > Buscar dispositivo" ),
	_S( "Teclado" ),
	_S( "Mouse" ),
	_S( "   conectado" ),
	_S( "   conectando..." ),
	_S( "   sin conexi\x00f3n (%d), reintentando" ),
	_S( "Apps us\x00e1ndolo: %d" ),
	_S( "La b\x00fasqueda fall\x00f3 (%d)" ),
	_S( "\x00bfNo conecta? Pon\x00e9 el dispositivo" ),
	_S( "en modo emparejar y busc\x00e1 de nuevo" ),
	_S( "No arranca el servidor (%d)" ),
	_S( "El servidor no responde" ),
	_S( "Buscar dispositivo" ),
	_S( "Pruebas" ),
	_S( "Probar mouse" ),
	_S( "Probar teclado" ),
	_S( "Olvidar todos" ),
	_S( "Salir" ),
	_S( "Arrastr\x00e1 el cuadrado con cualquier bot\x00f3n" ),
	_S( "Rueda: m\x00e1s grande / m\x00e1s chico" ),
	_S( "tama\x00f1o %d" ),
	_S( "Escrib\x00ed con el teclado Bluetooth" ),
	_S( "Apretadas: " ),
	_S( "abajo" ),
	_S( "arriba" ),
	_S( "sin eventos (%d)" ),
	_S( "   esperando: movelo o hac\x00e9 click" ),
	_S( "ninguno" ),
	_S( "Elegir mouse" ),
	_S( "Elegir teclado" ),
	_S( "Quitar mouse" ),
	_S( "Quitar teclado" ),
};

static TBool gSpanish;

static TPtrC Txt( TInt aId )
{
	return TPtrC( gSpanish ? KTextEs[aId] : KTextEn[aId] );
}

/* ------------------------------------------------------------------ */
/* teclas (codigos de uso HID) para la prueba del teclado */

static TInt UsageChar( TInt aUsage, TBool aShift )
{
	static const char kNum[]     = "1234567890";
	static const char kNumSh[]   = "!@#$%^&*()";
	static const char kPunct[]   = "-=[]\\#;'`,./"; // 0x2d..0x38
	static const char kPunctSh[] = "_+{}|~:\"~<>?";

	if( aUsage >= 0x04 && aUsage <= 0x1d )
		return ( aShift ? 'A' : 'a' ) + aUsage - 0x04;
	if( aUsage >= 0x1e && aUsage <= 0x27 )
		return ( aShift ? kNumSh : kNum )[aUsage - 0x1e];
	if( aUsage == 0x2c )
		return ' ';
	if( aUsage >= 0x2d && aUsage <= 0x38 )
		return ( aShift ? kPunctSh : kPunct )[aUsage - 0x2d];
	return 0;
}

static void UsageName( TInt aUsage, TDes &aName )
{
	static const TText *const kMods[8] = { _S( "Ctrl" ), _S( "Shift" ), _S( "Alt" ), _S( "Win" ),
		_S( "Ctrl Der" ), _S( "Shift Der" ), _S( "AltGr" ), _S( "Win Der" ) };
	TInt c = UsageChar( aUsage, EFalse );

	aName.Zero();
	if( aUsage >= 224 && aUsage <= 231 ) aName = TPtrC( kMods[aUsage - 224] );
	else if( aUsage == 0x2c ) aName = _L( "Space" );
	else if( c ) aName.Append( c );
	else if( aUsage == 0x28 ) aName = _L( "Enter" );
	else if( aUsage == 0x29 ) aName = _L( "Esc" );
	else if( aUsage == 0x2a ) aName = _L( "Backspace" );
	else if( aUsage == 0x2b ) aName = _L( "Tab" );
	else if( aUsage >= 0x3a && aUsage <= 0x45 ) aName.Format( _L( "F%d" ), aUsage - 0x3a + 1 );
	else if( aUsage == 0x4f ) aName = _L( "->" );
	else if( aUsage == 0x50 ) aName = _L( "<-" );
	else if( aUsage == 0x51 ) aName = _L( "v" );
	else if( aUsage == 0x52 ) aName = _L( "^" );
	else aName.Format( _L( "0x%02x" ), aUsage );
}

/* ------------------------------------------------------------------ */

enum TMgrMode { EModeStatus, EModeMouse, EModeKeys };

class CMgrView : public CCoeControl
{
public:
	~CMgrView()
	{
		delete iCursor;
		delete iCursorMask;
		delete iKeyLines;
	}

	void ConstructL( const TRect &aRect )
	{
		CreateWindowL();
		SetRect( aRect );
		iKeyLines = new( ELeave ) CDesCArrayFlat( 8 );
		TRAP_IGNORE( LoadCursorL());
		ResetTests();
		ActivateL();
	}

	void SetLines( const CDesCArray *aLines ) { iLines = aLines; if( iMode == EModeStatus ) DrawDeferred(); }
	void SetEventError( TInt aErr ) { iEventErr = aErr; }

	void SetMode( TMgrMode aMode )
	{
		iMode = aMode;
		ResetTests();
		DrawNow();
	}
	TMgrMode Mode() const { return iMode; }

	void HidEvent( const THIDEvent &aEv )
	{
		iEvents++;
		if( aEv.Type() == THIDEvent::EMouseEvent )
			MouseEvent( *aEv.Mouse());
		else if( aEv.Type() == THIDEvent::EKeyEvent )
			KeyEvent( aEv.Key()->ScanCode(), aEv.Key()->Type() == EEventHIDKeyDown );
		if( iMode != EModeStatus )
			DrawDeferred();
	}

private:
	/* el cursor de Whisk3D: color + mascara de 256 grises con el alfa del PNG */
	void LoadCursorL()
	{
		TBuf8<KCursorWidth * 3> line;
		TBuf8<KCursorWidth>     alpha;
		TInt                    x, y;

		iCursor = new( ELeave ) CFbsBitmap;
		User::LeaveIfError( iCursor->Create( TSize( KCursorWidth, KCursorHeight ), EColor16M ));
		iCursorMask = new( ELeave ) CFbsBitmap;
		User::LeaveIfError( iCursorMask->Create( TSize( KCursorWidth, KCursorHeight ), EGray256 ));
		for( y = 0; y < KCursorHeight; y++ )
		{
			line.SetLength( KCursorWidth * 3 );
			alpha.SetLength( KCursorWidth );
			for( x = 0; x < KCursorWidth; x++ )
			{
				const TUint8 *p = KCursorRgba + ( y * KCursorWidth + x ) * 4;
				line[x * 3]     = p[2]; // EColor16M va en BGR
				line[x * 3 + 1] = p[1];
				line[x * 3 + 2] = p[0];
				alpha[x]        = p[3];
			}
			iCursor->SetScanLine( line, y );
			iCursorMask->SetScanLine( alpha, y );
		}
	}

	void ResetTests()
	{
		TRect r = Rect();
		iPos = r.Center();
		iSquare = TRect( r.Center() - TPoint( 30, 30 ), TSize( 60, 60 ));
		iButtons = iGrab = 0;
		iMods = 0;
		iTyped.Zero();
		if( iKeyLines ) iKeyLines->Reset();
	}

	static TInt ButtonBit( TInt aValue )
	{
		return aValue == EMouseButtonRight ? 2 : aValue == EMouseButtonMiddle ? 4 : 1;
	}

	void MouseEvent( const TMouseEvent &aMouse )
	{
		TRect r = Rect();
		TInt  bit = ButtonBit( aMouse.iValue );

		switch( aMouse.iType )
		{
		case EEventRelativeXY:
			{
				TPoint old = iPos;
				iPos += aMouse.iPosition;
				if( iPos.iX < r.iTl.iX ) iPos.iX = r.iTl.iX;
				if( iPos.iY < r.iTl.iY ) iPos.iY = r.iTl.iY;
				if( iPos.iX >= r.iBr.iX ) iPos.iX = r.iBr.iX - 1;
				if( iPos.iY >= r.iBr.iY ) iPos.iY = r.iBr.iY - 1;
				if( iGrab )
					iSquare.Move( iPos - old );
			}
			break;
		case EEventButtonDown:
			iButtons |= bit;
			if( !iGrab && iSquare.Contains( iPos ))
				iGrab = bit;
			break;
		case EEventButtonUp:
			iButtons &= ~bit;
			if( iGrab == bit )
				iGrab = 0;
			break;
		case EEventRelativeWheel:
			{
				TPoint c = iSquare.Center();
				TInt   size = iSquare.Width() + aMouse.iValue * 8;
				if( size < 16 ) size = 16;
				if( size > r.Height() - 20 ) size = r.Height() - 20;
				iSquare = TRect( c - TPoint( size / 2, size / 2 ), TSize( size, size ));
			}
			break;
		default:
			break;
		}
	}

	void KeyEvent( TInt aUsage, TBool aDown )
	{
		TBuf<48> line, name;
		TInt     c;

		if( aUsage >= 224 && aUsage <= 231 )
		{
			if( aDown ) iMods |= 1 << ( aUsage - 224 );
			else iMods &= ~( 1 << ( aUsage - 224 ));
		}
		UsageName( aUsage, name );
		TPtrC state = Txt( aDown ? ETxtDown : ETxtUp );
		line.Format( _L( "%S  %S" ), &name, &state );
		if( iKeyLines->Count() >= 6 )
			iKeyLines->Delete( 0 );
		TRAP_IGNORE( iKeyLines->AppendL( line ));

		if( !aDown )
			return;
		c = UsageChar( aUsage, ( iMods & 0x22 ) != 0 );
		if( c && iTyped.Length() < iTyped.MaxLength())
			iTyped.Append( c );
		else if( aUsage == 0x2a && iTyped.Length())
			iTyped.SetLength( iTyped.Length() - 1 );
		else if( aUsage == 0x28 )
			iTyped.Zero();
	}

	void Draw( const TRect & ) const
	{
		CWindowGc   &gc = SystemGc();
		const CFont *font = CEikonEnv::Static()->DenseFont();

		gc.SetBrushStyle( CGraphicsContext::ESolidBrush );
		gc.SetBrushColor( KRgbWhite );
		gc.Clear( Rect());
		gc.UseFont( font );
		gc.SetPenColor( KRgbBlack );
		if( iMode == EModeMouse )
			DrawMouseTest( gc, font );
		else if( iMode == EModeKeys )
			DrawKeysTest( gc, font );
		else
			DrawLines( gc, font, iLines, font->AscentInPixels() + 3 );
		gc.DiscardFont();
	}

	TInt DrawLines( CWindowGc &gc, const CFont *font, const CDesCArray *aLines, TInt y ) const
	{
		TInt i;
		for( i = 0; aLines && i < aLines->Count(); i++ )
		{
			gc.DrawText( ( *aLines )[i], TPoint( 4, y ));
			y += font->HeightInPixels() + 1;
		}
		return y;
	}

	void DrawButtons( CWindowGc &gc, const CFont *font ) const
	{
		_LIT( KNames, "LMR" );
		static const TInt bits[3] = { 1, 4, 2 }; // izquierdo, medio, derecho
		TInt i, w = 16, h = font->HeightInPixels() + 2;
		TInt x = Rect().iBr.iX - 3 * ( w + 2 ) - 2;

		for( i = 0; i < 3; i++ )
		{
			TRect b( TPoint( x + i * ( w + 2 ), 2 ), TSize( w, h ));
			gc.SetBrushStyle(( iButtons & bits[i] ) ? CGraphicsContext::ESolidBrush : CGraphicsContext::ENullBrush );
			gc.SetBrushColor( KRgbRed );
			gc.SetPenColor( KRgbBlack );
			gc.DrawRect( b );
			gc.SetBrushStyle( CGraphicsContext::ENullBrush );
			gc.DrawText( KNames().Mid( i, 1 ), TPoint( b.iTl.iX + 4, b.iTl.iY + font->AscentInPixels() + 1 ));
		}
	}

	void DrawMouseTest( CWindowGc &gc, const CFont *font ) const
	{
		TInt     y = font->AscentInPixels() + 3;
		TBuf<48> text;
		TRgb     fill = iGrab == 1 ? KRgbBlue : iGrab == 2 ? KRgbDarkGreen : iGrab == 4 ? TRgb( 255, 140, 0 ) : KRgbGray;

		// el cuadrado: gris suelto; mientras se arrastra, azul (izquierdo), verde (derecho) o naranja (medio)
		gc.SetBrushStyle( CGraphicsContext::ESolidBrush );
		gc.SetBrushColor( fill );
		gc.SetPenColor( KRgbBlack );
		gc.DrawRect( iSquare );
		gc.SetBrushStyle( CGraphicsContext::ENullBrush );

		gc.DrawText( Txt( ETxtMouseHelp1 ), TPoint( 4, y ));
		y += font->HeightInPixels() + 1;
		gc.DrawText( Txt( ETxtMouseHelp2 ), TPoint( 4, y ));
		y += font->HeightInPixels() + 1;
		if( iEventErr )
			text.Format( Txt( ETxtNoEvents ), iEventErr );
		else
			text.Format( Txt( ETxtMouseSize ), iSquare.Width());
		gc.DrawText( text, TPoint( 4, y ));
		DrawButtons( gc, font );

		if( iCursor && iCursorMask )
			gc.BitBltMasked( iPos, iCursor, TRect( TSize( KCursorWidth, KCursorHeight )), iCursorMask, EFalse );
	}

	void DrawKeysTest( CWindowGc &gc, const CFont *font ) const
	{
		static const TText *const kMods[8] = { _S( "Ctrl " ), _S( "Shift " ), _S( "Alt " ), _S( "Win " ),
			_S( "Ctrl " ), _S( "Shift " ), _S( "AltGr " ), _S( "Win " ) };
		TInt      y = font->AscentInPixels() + 3, i;
		TBuf<80>  held;
		TBuf<100> typed;
		TRect     box;

		gc.DrawText( Txt( ETxtKeysHelp ), TPoint( 4, y ));
		y += font->HeightInPixels() + 4;

		// lo escrito, en un recuadro, con un cursor al final
		box = TRect( TPoint( 4, y - font->AscentInPixels() - 2 ), TSize( Rect().Width() - 8, font->HeightInPixels() + 4 ));
		gc.SetPenColor( KRgbDarkGray );
		gc.DrawRect( box );
		gc.SetPenColor( KRgbBlack );
		typed = iTyped.Right( 40 );
		typed.Append( '_' );
		gc.DrawText( typed, TPoint( 7, y ));
		y += font->HeightInPixels() + 6;

		held = Txt( ETxtKeysHeld );
		for( i = 0; i < 8; i++ )
			if( iMods & ( 1 << i ))
				held.Append( TPtrC( kMods[i] ));
		gc.DrawText( held, TPoint( 4, y ));
		y += font->HeightInPixels() + 4;

		if( iEventErr )
		{
			TBuf<32> err;
			err.Format( Txt( ETxtNoEvents ), iEventErr );
			gc.DrawText( err, TPoint( 4, y ));
			y += font->HeightInPixels() + 1;
		}
		DrawLines( gc, font, iKeyLines, y );
	}

	TMgrMode          iMode;
	const CDesCArray *iLines;
	TInt              iEventErr;
	TInt              iEvents;
	CFbsBitmap       *iCursor;
	CFbsBitmap       *iCursorMask;
	// prueba del mouse
	TPoint            iPos;
	TRect             iSquare;
	TInt              iButtons;  // bits 0 izquierdo, 1 derecho, 2 medio
	TInt              iGrab;     // boton con el que se esta arrastrando (0 = ninguno)
	// prueba del teclado
	TInt              iMods;     // bits de los modificadores (224..231)
	TBuf<200>         iTyped;
	CDesCArray       *iKeyLines;
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
	void Start( TInt aType )
	{
		if( IsActive()) return;
		iMgr.Search( iStatus, aType );
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
	CEvents( CMgrView &aView ) : CActive( EPriorityStandard ), iView( aView ) { CActiveScheduler::Add( this ); }
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
	void RunL()
	{
		THIDEvent ev;
		if( iStatus.Int() != KErrNone )
		{
			iView.SetEventError( iStatus.Int()); // el servidor se cerro
			return;
		}
		iClient->GetEvent( ev );
		iView.HidEvent( ev );
		iClient->EventReady( &iStatus );
		SetActive();
	}
	void DoCancel() { iClient->EventReadyCancel(); }

	CMgrView      &iView;
	MHIDSrvClient *iClient;
};

class CMgrAppUi : public CAknAppUi
{
public:
	void ConstructL()
	{
		TLanguage lang = User::Language();
		gSpanish = lang == ELangSpanish || lang == ELangLatinAmericanSpanish;

		BaseConstructL( EAknEnableSkin );
		iLines = new( ELeave ) CDesCArrayFlat( 8 );
		iView = new( ELeave ) CMgrView;
		iView->ConstructL( ClientRect());
		AddToStackL( iView );
		iConnectErr = iMgr.Connect();
		iSearch = new( ELeave ) CSearch( iMgr, *this );
		iEvents = new( ELeave ) CEvents( *iView );
		iView->SetEventError( iEvents->Start());
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

private:
	static TInt Tick( TAny *aPtr ) { static_cast<CMgrAppUi *>( aPtr )->Refresh(); return 1; }

	void DynInitMenuPaneL( TInt aResourceId, CEikMenuPane *aMenuPane )
	{
		if( aResourceId == R_MGR_MENU )
		{
			aMenuPane->SetItemTextL( EBtHidCmdPickMouse, Txt( ETxtMenuPickMouse ));
			aMenuPane->SetItemTextL( EBtHidCmdPickKeyboard, Txt( ETxtMenuPickKeys ));
			aMenuPane->SetItemTextL( EBtHidCmdTests, Txt( ETxtMenuTests ));
			aMenuPane->SetItemTextL( EBtHidCmdForgetMouse, Txt( ETxtMenuForgetMouse ));
			aMenuPane->SetItemTextL( EBtHidCmdForgetKeyboard, Txt( ETxtMenuForgetKeys ));
			aMenuPane->SetItemTextL( EAknCmdExit, Txt( ETxtMenuExit ));
		}
		else if( aResourceId == R_MGR_TESTS_MENU )
		{
			aMenuPane->SetItemTextL( EBtHidCmdTestMouse, Txt( ETxtMenuTestMouse ));
			aMenuPane->SetItemTextL( EBtHidCmdTestKeys, Txt( ETxtMenuTestKeys ));
		}
	}

	/* en las pruebas la tecla derecha es "Atras" y termina la prueba */
	void SetMode( TMgrMode aMode )
	{
		iView->SetMode( aMode );
		TRAP_IGNORE( Cba()->SetCommandSetL( aMode == EModeStatus ? R_AVKON_SOFTKEYS_OPTIONS_EXIT : R_AVKON_SOFTKEYS_BACK ));
		Cba()->DrawNow();
		if( aMode == EModeStatus )
			Refresh();
	}

	void HandleCommandL( TInt aCommand )
	{
		switch( aCommand )
		{
		case EBtHidCmdPickMouse:
		case EBtHidCmdPickKeyboard:
			if( iConnectErr == KErrNone )
				iSearch->Start( aCommand == EBtHidCmdPickKeyboard ? EBtHidKeyboard : EBtHidMouse );
			break;
		case EBtHidCmdForgetMouse:
		case EBtHidCmdForgetKeyboard:
			if( iConnectErr == KErrNone )
				iMgr.Forget( aCommand == EBtHidCmdForgetKeyboard ? EBtHidKeyboard : EBtHidMouse );
			Refresh();
			break;
		case EBtHidCmdTestMouse:
			SetMode( EModeMouse );
			break;
		case EBtHidCmdTestKeys:
			SetMode( EModeKeys );
			break;
		case EBtHidCmdForget:
			if( iConnectErr == KErrNone )
				iMgr.ForgetAll();
			Refresh();
			break;
		case EAknSoftkeyBack:
			SetMode( EModeStatus );
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
	CEvents       *iEvents;
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
		line.Format( Txt( ETxtNoServer ), iConnectErr );
		AddLine( line );
		iView->SetLines( iLines );
		return;
	}
	if( iMgr.Status( st ) != KErrNone )
	{
		AddLine( Txt( ETxtServerDead ));
		iView->SetLines( iLines );
		return;
	}

	// un renglon fijo para el mouse y otro para el teclado ("ninguno" si no hay), con su estado abajo
	for( TInt slot = EBtHidMouse; slot <= EBtHidKeyboard; slot++ )
	{
		TPtrC kind = Txt( slot == EBtHidKeyboard ? ETxtKeyboard : ETxtMouse );
		const TBtHidDeviceInfo *d = NULL;
		for( i = 0; i < st().iCount; i++ )
		{
			TInt type = st().iDevice[i].iType ? st().iDevice[i].iType : EBtHidMouse; // sin tipo = mouse
			if( type == slot )
				d = &st().iDevice[i];
		}
		if( !d )
		{
			TPtrC none = Txt( ETxtNone );
			line.Format( _L( "%S: %S" ), &kind, &none );
			AddLine( line );
			continue;
		}
		TBuf<32> name;
		name.Copy( d->iName );
		if( !name.Length())
			name.Format( _L( "%02x:%02x:%02x:%02x:%02x:%02x" ), d->iAddr[0], d->iAddr[1], d->iAddr[2], d->iAddr[3], d->iAddr[4], d->iAddr[5] );
		line.Format( _L( "%S: %S" ), &kind, &name );
		AddLine( line );
		if( d->iState == EBtHidConnected )
			AddLine( Txt( ETxtConnected ));
		else if( d->iState == EBtHidConnecting )
			AddLine( Txt( ETxtConnecting ));
		else if( d->iState == EBtHidWaiting )
			AddLine( Txt( ETxtWaiting ));
		else
		{
			line.Format( Txt( ETxtNotConnected ), d->iError );
			AddLine( line );
		}
	}
	line.Format( Txt( ETxtApps ), st().iClients );
	AddLine( line );
	if( iSearch->LastError() != KErrNone && iSearch->LastError() != KErrCancel )
	{
		line.Format( Txt( ETxtSearchFailed ), iSearch->LastError());
		AddLine( line );
	}
	for( i = 0; i < st().iCount; i++ )
		if( st().iDevice[i].iState != EBtHidConnected && st().iDevice[i].iState != EBtHidWaiting )
		{
			AddLine( Txt( ETxtHint1 ));
			AddLine( Txt( ETxtHint2 ));
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
