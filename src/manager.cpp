/*
manager.cpp - bthidmgr.exe, la app "Bluetooth HID": buscar, ver y olvidar el mouse y el teclado del servidor,
y probarlos.

Opciones > Seleccionar perfil: los perfiles del joystick como teclado (que tecla manda cada boton de cada
joystick), se eligen, se crean y se editan desde la app (profiles.cpp guarda los archivos).

Opciones > Buscar dispositivo abre el cuadro de Bluetooth del telefono; el elegido queda guardado en el
servidor y se reconecta solo cada vez que una app lo usa. Opciones > Pruebas: la app recibe los eventos como
cualquier juego (GetHIDClientAPI). En la del mouse se arrastra un cuadrado con cualquiera de los tres botones y
la rueda le cambia el tamano; en la del teclado se ven las teclas apretadas y lo que se escribe. Opciones >
Joystick de PS3 muestra la direccion Bluetooth del telefono: la que se le graba por USB al joystick con
tools/ds3pair.py para que llame al telefono.

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
#include "version.h"
#include <apgcli.h>
#include <aknmessagequerydialog.h>
#include <e32property.h>
#include <bt_subscribe.h>
#include <aknlists.h>
#include <aknlistquerydialog.h>
#include <aknquerydialog.h>
#include <aknnotewrappers.h>
#include <aknpopupheadingpane.h>
#include <akntitle.h>
#include <eikmenub.h>
#include <eikspane.h>
#include "profiles.h"

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
	ETxtMenuAbout, ETxtAboutText, ETxtMenuPs3, ETxtPs3Text, ETxtPs3NoAddr,
	ETxtMenuPickPad, ETxtMenuForgetPads, ETxtMenuTestPads, ETxtPad, ETxtPadNone, ETxtPadHelp, ETxtPadRumble,
	ETxtMenuPadKeysOn, ETxtMenuPadKeysOff,
	ETxtMenuProfiles, ETxtProfTitle, ETxtProfInUse, ETxtProfUse, ETxtProfEdit, ETxtProfNew, ETxtProfRename,
	ETxtProfDelete, ETxtProfKey, ETxtProfClear, ETxtProfPad, ETxtProfName, ETxtProfExists, ETxtProfNoDelete,
	ETxtProfNoRename, ETxtProfAskDelete, ETxtProfUsing, ETxtProfPickPad, ETxtProfEditTitle, ETxtProfSaveErr,
	ETxtMenuAutoOn, ETxtMenuAutoOff, ETxtPadNoNum,
	ETxtSlotEmpty, ETxtSlotAddPad, ETxtSlotConnect, ETxtSlotDisconnect, ETxtSlotRemove, ETxtSlotStopped,
	ETxtSlotCalling, ETxtBattery, ETxtCharging, ETxtSlotHelp, ETxtSlotOptions, ETxtSlotMove, ETxtBatteryNone,
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
	_S( "About" ),
	_S( "Version %S\nBluetooth mouse and keyboard driver made by Dante Leoncini.\n\nDonations are welcome:\n<AknMessageQuery Link>leoncini.com.ar</AknMessageQuery Link>\n\nMIT license" ),
	_S( "PS3 controller" ),
	_S( "This phone's Bluetooth address:\n%S\n\nWrite it to the PS3 controller by USB from a computer, with ds3pair (github.com/Dante-Leoncini/bthid-s60, tools/PS3.md). Then unplug it and press PS." ),
	_S( "(turn Bluetooth on and try again)" ),
	_S( "Add controller" ),
	_S( "Remove controllers" ),
	_S( "Test controllers" ),
	_S( "Controller %d" ),
	_S( "none (Options > Add controller)" ),
	_S( "Left/right: controller. Center: vibration" ),
	_S( "Vibration: %S" ),
	_S( "Controllers as keyboard: yes" ),
	_S( "Controllers as keyboard: no" ),
	_S( "Choose profile" ),
	_S( "Controller profiles" ),
	_S( " (in use)" ),
	_S( "Assign" ),
	_S( "Edit" ),
	_S( "New profile" ),
	_S( "Rename" ),
	_S( "Delete" ),
	_S( "Change key" ),
	_S( "Not assigned" ),
	_S( "Other controller" ),
	_S( "Profile name:" ),
	_S( "There is already a profile with that name" ),
	_S( "The default profile can't be deleted" ),
	_S( "The default profile can't be renamed" ),
	_S( "Delete %S?" ),
	_S( "In use: %S (controllers as keyboard: yes)" ),
	_S( "Which controller?" ),
	_S( "%S: controller %d" ),
	_S( "Couldn't save (%d)" ),
	_S( "Start with the phone: yes" ),
	_S( "Start with the phone: no" ),
	_S( "Controller" ),
	_S( "empty" ),
	_S( "Add a controller here" ),
	_S( "Connect" ),
	_S( "Disconnect" ),
	_S( "Remove" ),
	_S( "   disconnected" ),
	_S( "   calling it... (press PS)" ),
	_S( "   battery %d%%" ),
	_S( "   charging" ),
	_S( "Up/down: choose. Center: options" ),
	_S( "Options" ),
	_S( "Move to controller %d" ),
	_S( "  battery ?" ),
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
	_S( "Acerca de" ),
	_S( "Version %S\nDriver de mouse y teclado Bluetooth hecho por Dante Leoncini.\n\nSe aceptan donaciones:\n<AknMessageQuery Link>leoncini.com.ar</AknMessageQuery Link>\n\nLicencia MIT" ),
	_S( "Joystick de PS3" ),
	_S( "Direcci\x00f3n Bluetooth de este tel\x00e9\x0066ono:\n%S\n\nGrabala en el joystick de PS3 por USB desde una computadora, con ds3pair (github.com/Dante-Leoncini/bthid-s60, tools/PS3.md). Despu\x00e9s desenchufalo y apret\x00e1 PS." ),
	_S( "(prend\x00e9 el Bluetooth y prob\x00e1 de nuevo)" ),
	_S( "Agregar joystick" ),
	_S( "Quitar joysticks" ),
	_S( "Probar joysticks" ),
	_S( "Joystick %d" ),
	_S( "ninguno (Opciones > Agregar joystick)" ),
	_S( "Izquierda/derecha: joystick. Centro: vibraci\x00f3n" ),
	_S( "Vibraci\x00f3n: %S" ),
	_S( "Joystick como teclado: s\x00ed" ),
	_S( "Joystick como teclado: no" ),
	_S( "Seleccionar perfil" ),
	_S( "Perfiles del joystick" ),
	_S( " (en uso)" ),
	_S( "Asignar" ),
	_S( "Editar" ),
	_S( "Nuevo perfil" ),
	_S( "Cambiar nombre" ),
	_S( "Borrar" ),
	_S( "Cambiar tecla" ),
	_S( "Dejar sin asignar" ),
	_S( "Otro joystick" ),
	_S( "Nombre del perfil:" ),
	_S( "Ya hay un perfil con ese nombre" ),
	_S( "El perfil predeterminado no se puede borrar" ),
	_S( "Al predeterminado no se le cambia el nombre" ),
	_S( "\x00bf\x0042orrar %S?" ),
	_S( "En uso: %S (joystick como teclado: s\x00ed)" ),
	_S( "\x00bfQu\x00e9 joystick?" ),
	_S( "%S: joystick %d" ),
	_S( "No se pudo guardar (%d)" ),
	_S( "Iniciar con el tel\x00e9\x0066ono: s\x00ed" ),
	_S( "Iniciar con el tel\x00e9\x0066ono: no" ),
	_S( "Joystick" ),
	_S( "vac\x00edo" ),
	_S( "Agregar un joystick ac\x00e1" ),
	_S( "Conectar" ),
	_S( "Desconectar" ),
	_S( "Quitar" ),
	_S( "   desconectado" ),
	_S( "   llam\x00e1ndolo... (apret\x00e1 PS)" ),
	_S( "   bater\x00ed\x0061 %d%%" ),
	_S( "   cargando" ),
	_S( "Arriba/abajo: elegir. Centro: opciones" ),
	_S( "Opciones" ),
	_S( "Pasar a joystick %d" ),
	_S( "  bater\x00ed\x0061 ?" ),
};

static TBool gSpanish;

static TPtrC Txt( TInt aId )
{
	return TPtrC( gSpanish ? KTextEs[aId] : KTextEn[aId] );
}

/* la direccion Bluetooth del telefono (la de *#2820#), que publica el stack Bluetooth; vacia si no la publico
   (el Bluetooth nunca se prendio) */
static void PhoneAddress( TDes &aText )
{
	TBuf8<16> addr;

	aText.Zero();
	if( RProperty::Get( KPropertyUidBluetoothCategory, KPropertyKeyBluetoothGetLocalDeviceAddress, addr ) != KErrNone
		|| addr.Length() != 6 || addr == TPtrC8( (const TUint8 *)"\0\0\0\0\0\0", 6 ))
		return;
	aText.Format( _L( "%02X:%02X:%02X:%02X:%02X:%02X" ), addr[0], addr[1], addr[2], addr[3], addr[4], addr[5] );
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

enum TMgrMode { EModeStatus, EModeMouse, EModeKeys, EModePads };

/* los lugares de la pantalla principal: el mouse, el teclado y los joysticks 1-4 */
enum { ESlotMouse = 0, ESlotKeyboard, ESlotPad1, ESlots = ESlotPad1 + EBtHidMaxPads };

class MSlotObserver
{
public:
	virtual void SlotSelectedL( TInt aSlot ) = 0;
};

class CMgrView : public CCoeControl
{
public:
	~CMgrView()
	{
		delete iPadTimer;
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
	// los lugares: dos renglones por lugar (que es y como esta) y lo de abajo
	void SetSlots( const CDesCArray *aSlots, const CDesCArray *aFoot, MSlotObserver *aObs )
	{
		iSlots = aSlots;
		iFoot = aFoot;
		iObs = aObs;
		if( iMode == EModeStatus ) DrawDeferred();
	}
	TInt Selected() const { return iSel; }
	void Select( TInt aSlot ) { iSel = aSlot; DrawDeferred(); }
	void SetManager( RBtHidManager *aMgr ) { iMgr = aMgr; }

	/* prueba de los joysticks: el estado se pide 20 veces por segundo (no llegan como eventos) */
	static TInt PadTick( TAny *aPtr )
	{
		CMgrView *self = static_cast<CMgrView *>( aPtr );
		TUint now = User::NTickCount();
		if( self->iMgr )
			self->iMgr->PadState( self->iPad, self->iPadSt );
		if( now - self->iPadRateTick >= 1000 )
		{
			self->iPadRate = self->iPadSt().iReports - self->iPadRateBase;
			if( self->iPadRate < 0 ) self->iPadRate = 0;
			self->iPadRateBase = self->iPadSt().iReports;
			self->iPadRateTick = now;
		}
		self->DrawDeferred();
		return 1;
	}

	TKeyResponse OfferKeyEventL( const TKeyEvent &aKey, TEventCode aType )
	{
		if( iMode == EModeStatus && aType == EEventKey && iSlots )
		{
			if( aKey.iCode == EKeyUpArrow || aKey.iCode == EKeyDownArrow )
			{
				iSel = ( iSel + ( aKey.iCode == EKeyDownArrow ? 1 : ESlots - 1 )) % ESlots;
				DrawDeferred();
				return EKeyWasConsumed;
			}
			if(( aKey.iCode == EKeyDevice3 || aKey.iCode == EKeyEnter || aKey.iCode == EKeyOK ) && iObs )
			{
				iObs->SlotSelectedL( iSel );
				return EKeyWasConsumed;
			}
		}
		if( iMode != EModePads || aType != EEventKey )
			return EKeyWasNotConsumed;
		if( aKey.iCode == EKeyLeftArrow || aKey.iCode == EKeyRightArrow )
		{
			iPad = ( iPad + ( aKey.iCode == EKeyRightArrow ? 1 : EBtHidMaxPads - 1 )) % EBtHidMaxPads;
			Mem::FillZ( &iPadSt(), sizeof( TBtHidPadState ));
			iPadRateBase = 0;
			DrawDeferred();
			return EKeyWasConsumed;
		}
		if( aKey.iCode == EKeyDevice3 || aKey.iCode == EKeyEnter || aKey.iCode == EKeyOK )
		{
			if( iMgr )
				iMgr->SetPadRumble( iPad, !iPadSt().iRumble );
			return EKeyWasConsumed;
		}
		return EKeyWasNotConsumed;
	}
	void SetEventError( TInt aErr ) { iEventErr = aErr; }

	void SetMode( TMgrMode aMode )
	{
		iMode = aMode;
		ResetTests();
		if( aMode == EModePads && !iPadTimer )
		{
			TRAP_IGNORE( iPadTimer = CPeriodic::NewL( CActive::EPriorityStandard ));
			if( iPadTimer )
				iPadTimer->Start( 0, 50000, TCallBack( PadTick, this ));
		}
		else if( aMode != EModePads && iPadTimer )
		{
			delete iPadTimer;
			iPadTimer = NULL;
		}
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
		else if( iMode == EModePads )
			DrawPadsTest( gc, font );
		else
			DrawStatus( gc, font );
		gc.DiscardFont();
	}

	/* la pantalla principal: el titulo, los seis lugares (el elegido resaltado) y lo de abajo */
	void DrawStatus( CWindowGc &gc, const CFont *font ) const
	{
		TInt lh = font->HeightInPixels() + 1, i;
		TInt y = font->AscentInPixels() + 3;
		if( iSlots )
		{
			/* si no entra todo, se corre para que se vea el lugar elegido (y lo de abajo, en el ultimo) */
			TInt head = ( iLines ? iLines->Count() : 0 ) * lh, slot = 2 * lh + 3;
			TInt total = head + ESlots * slot + 2 + ( iFoot ? iFoot->Count() : 0 ) * lh + 4;
			TInt h = Rect().Height(), off = 0;
			TInt bottom = head + ( iSel + 1 ) * slot + 4;
			if( bottom > h ) off = bottom - h;
			if( iSel == ESlots - 1 && total > h ) off = total - h;
			y -= off;
		}
		y = DrawLines( gc, font, iLines, y );
		if( !iSlots )
			return;
		for( i = 0; i < ESlots && 2 * i + 1 < iSlots->Count(); i++ )
		{
			TRect r( TPoint( 1, y - font->AscentInPixels() - 2 ), TSize( Rect().Width() - 2, 2 * lh + 2 ));
			if( i == iSel )
			{
				gc.SetBrushStyle( CGraphicsContext::ESolidBrush );
				gc.SetBrushColor( TRgb( 30, 110, 220 ));
				gc.SetPenStyle( CGraphicsContext::ENullPen );
				gc.DrawRect( r );
				gc.SetPenStyle( CGraphicsContext::ESolidPen );
				gc.SetBrushStyle( CGraphicsContext::ENullBrush );
				gc.SetPenColor( KRgbWhite );
			}
			else
				gc.SetPenColor( KRgbBlack );
			gc.DrawText( ( *iSlots )[2 * i], TPoint( 4, y ));
			{
				/* "conectado" en verde; lo que sigue (la bateria), del color de siempre */
				TPtrC st = ( *iSlots )[2 * i + 1];
				TPtrC con = Txt( ETxtConnected );
				TRgb normal = i == iSel ? KRgbWhite : TRgb( 90, 90, 90 );
				if( st.Left( con.Length()) == con )
				{
					gc.SetPenColor( i == iSel ? TRgb( 140, 255, 140 ) : TRgb( 0, 150, 0 ));
					gc.DrawText( con, TPoint( 4, y + lh ));
					gc.SetPenColor( normal );
					gc.DrawText( st.Mid( con.Length()), TPoint( 4 + font->TextWidthInPixels( con ), y + lh ));
				}
				else
				{
					gc.SetPenColor( normal );
					gc.DrawText( st, TPoint( 4, y + lh ));
				}
			}
			y += 2 * lh + 3;
		}
		gc.SetPenColor( KRgbBlack );
		DrawLines( gc, font, iFoot, y + 2 );
	}

	TInt DrawLines( CWindowGc &gc, const CFont *font, const CDesCArray *aLines, TInt y ) const
	{
		gc.SetPenColor( KRgbBlack );
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

	/* la prueba de los joysticks: pestanas 1-4, el estado, los botones, los dos sticks y la presion */
	void DrawPadsTest( CWindowGc &gc, const CFont *font ) const
	{
		static const TText *const kNames[EBtHidPadButtons] = { _S( "Ar" ), _S( "De" ), _S( "Ab" ), _S( "Iz" ),
			_S( "X" ), _S( "O" ), _S( "Cu" ), _S( "Tr" ), _S( "L1" ), _S( "R1" ), _S( "L2" ), _S( "R2" ),
			_S( "Se" ), _S( "St" ), _S( "L3" ), _S( "R3" ), _S( "PS" ) };
		const TBtHidPadState &st = iPadSt();
		TRect    r = Rect();
		TInt     lh = font->HeightInPixels() + 2, y = 2, i;
		TBuf<80> text;

		/* pestanas */
		for( i = 0; i < EBtHidMaxPads; i++ )
		{
			TRect tab( TPoint( 4 + i * 34, y ), TSize( 30, lh ));
			gc.SetBrushStyle( i == iPad ? CGraphicsContext::ESolidBrush : CGraphicsContext::ENullBrush );
			gc.SetBrushColor( TRgb( 30, 110, 220 ));
			gc.SetPenColor( KRgbBlack );
			gc.DrawRect( tab );
			gc.SetPenColor( i == iPad ? KRgbWhite : KRgbBlack );
			text.Num( i + 1 );
			gc.DrawText( text, TPoint( tab.iTl.iX + 11, tab.iTl.iY + font->AscentInPixels() + 1 ));
		}
		gc.SetBrushStyle( CGraphicsContext::ENullBrush );
		gc.SetPenColor( KRgbBlack );
		y += lh + font->AscentInPixels() + 2;

		/* estado */
		text.Format( Txt( ETxtPad ), iPad + 1 );
		if( st.iKind == EBtHidPadNone )
		{
			TPtrC none = Txt( ETxtPadNone );
			text.Append( _L( ": " ));
			text.Append( none );
		}
		else
		{
			text.Append( st.iKind == EBtHidPadPs3 ? _L( ", PS3" ) : st.iKind == EBtHidPadPs4 ? _L( ", PS4" ) : _L( "" ));
			text.Append( st.iState == EBtHidConnected ? Txt( ETxtConnected ) : st.iState == EBtHidWaiting
				? Txt( ETxtWaiting ) : Txt( ETxtConnecting ));
		}
		gc.DrawText( text, TPoint( 4, y ));
		y += lh;
		text.Format( _L( "%d rep/s  %02x:%02x:%02x:%02x:%02x:%02x" ), iPadRate,
			st.iAddr[0], st.iAddr[1], st.iAddr[2], st.iAddr[3], st.iAddr[4], st.iAddr[5] );
		gc.DrawText( text, TPoint( 4, y ));
		y += lh;
		{
			TPtrC onoff( st.iRumble ? ( gSpanish ? _L( "si" ) : _L( "on" )) : _L( "no" ));
			text.Format( Txt( ETxtPadRumble ), &onoff );
		}
		gc.DrawText( text, TPoint( 4, y ));
		y += lh - font->AscentInPixels() + 2;

		/* botones: 9 por fila */
		for( i = 0; i < EBtHidPadButtons; i++ )
		{
			TInt  col = i % 9, row = i / 9, w = ( r.Width() - 8 ) / 9;
			TRect b( TPoint( 4 + col * w, y + row * ( lh + 2 )), TSize( w - 2, lh ));
			TBool on = ( st.iButtons & ( 1u << i )) != 0;
			gc.SetBrushStyle( on ? CGraphicsContext::ESolidBrush : CGraphicsContext::ENullBrush );
			gc.SetBrushColor( KRgbRed );
			gc.SetPenColor( KRgbBlack );
			gc.DrawRect( b );
			gc.SetBrushStyle( CGraphicsContext::ENullBrush );
			gc.DrawText( TPtrC( kNames[i] ), TPoint( b.iTl.iX + 2, b.iTl.iY + font->AscentInPixels() + 1 ));
		}
		y += 2 * ( lh + 2 ) + 4;

		/* sticks: dos circulos con el punto donde esta cada uno */
		{
			TInt rad = ( r.Width() - 24 ) / 4;
			if( rad > 40 ) rad = 40;
			for( i = 0; i < 2; i++ )
			{
				TPoint c( r.Width() / 4 + i * r.Width() / 2, y + rad );
				TInt   dx = ( st.iAxis[i * 2] - 128 ) * rad / 128, dy = ( st.iAxis[i * 2 + 1] - 128 ) * rad / 128;
				gc.SetPenColor( KRgbBlack );
				gc.DrawEllipse( TRect( c - TPoint( rad, rad ), TSize( rad * 2, rad * 2 )));
				gc.SetBrushStyle( CGraphicsContext::ESolidBrush );
				gc.SetBrushColor( TRgb( 30, 110, 220 ));
				gc.DrawEllipse( TRect( c + TPoint( dx - 5, dy - 5 ), TSize( 10, 10 )));
				gc.SetBrushStyle( CGraphicsContext::ENullBrush );
			}
			y += rad * 2 + 6;
		}

		/* presion: L2 y R2 anchas; con el DualShock 3, ademas la de los otros botones */
		for( i = 0; i < 2; i++ )
		{
			TInt  v = st.iPressure[i ? EBtHidPadR2 : EBtHidPadL2];
			TInt  w = r.Width() / 2 - 32;
			TRect bar( TPoint( 28 + i * r.Width() / 2, y ), TSize( w, lh - 4 ));
			gc.DrawText( i ? _L( "R2" ) : _L( "L2" ), TPoint( 4 + i * r.Width() / 2, y + font->AscentInPixels()));
			gc.DrawRect( bar );
			gc.SetBrushStyle( CGraphicsContext::ESolidBrush );
			gc.SetBrushColor( KRgbRed );
			gc.DrawRect( TRect( bar.iTl, TSize( v * w / 255, bar.Height())));
			gc.SetBrushStyle( CGraphicsContext::ENullBrush );
		}
		y += lh + 2;
		if( st.iKind == EBtHidPadPs3 )
		{
			static const TUint8 order[12] = { EBtHidPadUp, EBtHidPadRight, EBtHidPadDown, EBtHidPadLeft,
				EBtHidPadL1, EBtHidPadR1, EBtHidPadTriangle, EBtHidPadCircle, EBtHidPadCross, EBtHidPadSquare,
				EBtHidPadL2, EBtHidPadR2 };
			TInt w = ( r.Width() - 8 ) / 12, h = 30;
			for( i = 0; i < 12; i++ )
			{
				TInt  v = st.iPressure[order[i]];
				TRect bar( TPoint( 4 + i * w, y ), TSize( w - 3, h ));
				gc.DrawRect( bar );
				gc.SetBrushStyle( CGraphicsContext::ESolidBrush );
				gc.SetBrushColor( KRgbRed );
				gc.DrawRect( TRect( TPoint( bar.iTl.iX, bar.iBr.iY - v * h / 255 ), bar.iBr ));
				gc.SetBrushStyle( CGraphicsContext::ENullBrush );
			}
			y += h + 4;
		}
		gc.DrawText( Txt( ETxtPadHelp ), TPoint( 4, r.iBr.iY - font->DescentInPixels() - 2 ));
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
	RBtHidManager    *iMgr;
	CPeriodic        *iPadTimer;
	TInt              iPad;          // el joystick que se mira (0-3)
	TBtHidPadStatePckg iPadSt;
	TInt              iPadRate, iPadRateBase;
	TUint             iPadRateTick;
	const CDesCArray *iLines;
	const CDesCArray *iSlots;
	const CDesCArray *iFoot;
	MSlotObserver    *iObs;
	TInt              iSel;
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
	void StartPad( TInt aSlot )
	{
		if( IsActive()) return;
		iMgr.SearchPad( iStatus, aSlot );
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

/* ------------------------------------------------------------------ */
/* perfiles del joystick como teclado: la lista de perfiles y, al editar uno, la de botones con su tecla. El
   centro hace lo principal de cada lista (asignar el perfil, cambiar la tecla), lo demas esta en Opciones */

class CProfView : public CCoeControl, public MEikListBoxObserver
{
public:
	CProfView( MEikCommandObserver &aObs ) : iObs( aObs ) {}
	~CProfView()
	{
		delete iList;
		delete iItems;
	}

	void ConstructL( const TRect &aRect )
	{
		CreateWindowL();
		iItems = new( ELeave ) CDesCArrayFlat( 24 );
		iList = new( ELeave ) CAknSingleStyleListBox;
		iList->SetContainerWindowL( *this );
		iList->ConstructL( this, EAknListBoxSelectionList );
		iList->Model()->SetItemTextArray( iItems );
		iList->Model()->SetOwnershipType( ELbmDoesNotOwnItemArray );
		iList->CreateScrollBarFrameL( ETrue );
		iList->ScrollBarFrame()->SetScrollBarVisibilityL( CEikScrollBarFrame::EOff, CEikScrollBarFrame::EAuto );
		iList->SetListBoxObserver( this );
		SetRect( aRect );
		MakeVisible( EFalse );
		ActivateL();
	}

	CDesCArray &Items() { return *iItems; }
	TInt Current() const { return iList->CurrentItemIndex(); }
	void SetCommand( TInt aCommand ) { iCommand = aCommand; }

	// despues de cambiar los renglones
	void ItemsChangedL( TInt aCurrent )
	{
		iList->HandleItemAdditionL();
		if( aCurrent >= 0 && aCurrent < iItems->Count())
			iList->SetCurrentItemIndex( aCurrent );
		iList->DrawDeferred();
	}

	TKeyResponse OfferKeyEventL( const TKeyEvent &aKey, TEventCode aType )
	{
		if( aType == EEventKey && ( aKey.iCode == EKeyOK || aKey.iCode == EKeyEnter ))
		{
			iObs.ProcessCommandL( iCommand );
			return EKeyWasConsumed;
		}
		return iList->OfferKeyEventL( aKey, aType );
	}

	void HandleListBoxEventL( CEikListBox *, TListBoxEvent aEvent )
	{
		if( aEvent == EEventEnterKeyPressed || aEvent == EEventItemDoubleClicked )
			iObs.ProcessCommandL( iCommand ); // tocar un renglon en los de pantalla tactil
	}

private:
	TInt CountComponentControls() const { return 1; }
	CCoeControl *ComponentControl( TInt ) const { return iList; }
	void SizeChanged() { if( iList ) iList->SetRect( Rect()); }

	MEikCommandObserver    &iObs;
	CAknSingleStyleListBox *iList;
	CDesCArray             *iItems;
	TInt                    iCommand;
};

class CMgrAppUi : public CAknAppUi, public MSlotObserver
{
public:
	void ConstructL()
	{
		TLanguage lang = User::Language();
		gSpanish = lang == ELangSpanish || lang == ELangLatinAmericanSpanish;

		BaseConstructL( EAknEnableSkin );
		iLines = new( ELeave ) CDesCArrayFlat( 8 );
		iSlots = new( ELeave ) CDesCArrayFlat( 2 * ESlots );
		iFoot = new( ELeave ) CDesCArrayFlat( 4 );
		iView = new( ELeave ) CMgrView;
		iView->ConstructL( ClientRect());
		AddToStackL( iView );
		iConnectErr = iMgr.Connect();
		iView->SetManager( iConnectErr == KErrNone ? &iMgr : NULL );
		iSearch = new( ELeave ) CSearch( iMgr, *this );
		iEvents = new( ELeave ) CEvents( *iView );
		iView->SetEventError( iEvents->Start());
		iTimer = CPeriodic::NewL( CActive::EPriorityStandard );
		iTimer->Start( 0, 1000000, TCallBack( Tick, this ));
		iProfNames = new( ELeave ) CDesCArrayFlat( 8 );
		iProfView = new( ELeave ) CProfView( *this );
		iProfView->ConstructL( ClientRect());
	}
	~CMgrAppUi()
	{
		delete iTimer;
		if( iProfScreen != EProfNone )
			RemoveFromStack( iProfView );
		delete iProfView;
		delete iProfNames;
		delete iEvents;
		delete iSearch;
		iMgr.Close();
		if( iView )
		{
			RemoveFromStack( iView );
			delete iView;
		}
		delete iLines;
		delete iSlots;
		delete iFoot;
	}
	void Refresh();

	/* el centro sobre un lugar: lo que se puede hacer con el (agregar uno, conectar, desconectar, quitar) */
	void SlotSelectedL( TInt aSlot )
	{
		enum { EActAdd, EActConnect, EActDisconnect, EActRemove, EActMove1 };
		CDesCArrayFlat *items;
		TInt acts[4 + EBtHidMaxPads], n = 0, sel, k;
		if( iConnectErr != KErrNone || aSlot < 0 || aSlot >= ESlots )
			return;
		items = new( ELeave ) CDesCArrayFlat( 4 );
		CleanupStack::PushL( items );
		if( !iSlotUsed[aSlot] )
		{
			items->AppendL( Txt( aSlot == ESlotMouse ? ETxtMenuPickMouse : aSlot == ESlotKeyboard ? ETxtMenuPickKeys
				: ETxtSlotAddPad ));
			acts[n++] = EActAdd;
		}
		else
		{
			TInt st = iSlotState[aSlot];
			if( st != EBtHidConnected )
			{
				items->AppendL( Txt( ETxtSlotConnect ));
				acts[n++] = EActConnect;
			}
			if( st != EBtHidStopped )
			{
				items->AppendL( Txt( ETxtSlotDisconnect ));
				acts[n++] = EActDisconnect;
			}
			// un joystick: pasarlo a otro numero (si estaba ocupado, se intercambian)
			for( k = 0; k < EBtHidMaxPads && aSlot >= ESlotPad1; k++ )
			{
				TBuf<40> b;
				if( ESlotPad1 + k == aSlot )
					continue;
				b.Format( Txt( ETxtSlotMove ), k + 1 );
				items->AppendL( b );
				acts[n++] = EActMove1 + k;
			}
			items->AppendL( Txt( ETxtSlotRemove ));
			acts[n++] = EActRemove;
		}
		// (la lista se refresca sola cada segundo: lo de este lugar se copia antes de preguntar)
		TBuf8<6> addr = iSlotAddr[aSlot];
		TBuf<64> heading = ( *iSlots )[2 * aSlot].Left( 64 );
		sel = AskListL( *items, heading, 0 );
		CleanupStack::PopAndDestroy( items );
		if( sel < 0 || sel >= n )
			return;
		switch( acts[sel] )
		{
		case EActAdd:
			if( aSlot == ESlotMouse ) iSearch->Start( EBtHidMouse );
			else if( aSlot == ESlotKeyboard ) iSearch->Start( EBtHidKeyboard );
			else iSearch->StartPad( aSlot - ESlotPad1 + 1 );
			break;
		case EActConnect:
			iMgr.DeviceCommand( addr, EBtHidCmdConnect );
			break;
		case EActDisconnect:
			iMgr.DeviceCommand( addr, EBtHidCmdDisconnect );
			break;
		case EActRemove:
			iMgr.DeviceCommand( addr, EBtHidCmdRemove );
			break;
		default:
			if( acts[sel] >= EActMove1 )
			{
				iMgr.DeviceCommand( addr, EBtHidCmdMoveToPad + acts[sel] - EActMove1 + 1 );
				iView->Select( ESlotPad1 + acts[sel] - EActMove1 ); // (la seleccion va con el)
			}
			break;
		}
		Refresh();
	}

private:
	static TInt Tick( TAny *aPtr ) { static_cast<CMgrAppUi *>( aPtr )->Refresh(); return 1; }

	void DynInitMenuPaneL( TInt aResourceId, CEikMenuPane *aMenuPane )
	{
		if( aResourceId == R_MGR_MENU )
		{
			aMenuPane->SetItemTextL( EBtHidCmdPickMouse, Txt( ETxtMenuPickMouse ));
			aMenuPane->SetItemTextL( EBtHidCmdPickKeyboard, Txt( ETxtMenuPickKeys ));
			aMenuPane->SetItemTextL( EBtHidCmdPickPad, Txt( ETxtMenuPickPad ));
			aMenuPane->SetItemTextL( EBtHidCmdForgetPads, Txt( ETxtMenuForgetPads ));
			{
				/* el modo teclado: el texto dice como esta ahora */
				TBtHidStatusPckg st;
				TBool on = iConnectErr == KErrNone && iMgr.Status( st ) == KErrNone && st().iPadKeys;
				aMenuPane->SetItemTextL( EBtHidCmdPadKeys, Txt( on ? ETxtMenuPadKeysOn : ETxtMenuPadKeysOff ));
			}
			aMenuPane->SetItemTextL( EBtHidCmdProfiles, Txt( ETxtMenuProfiles ));
			aMenuPane->SetItemTextL( EBtHidCmdAutoStart, Txt( AutoStart() ? ETxtMenuAutoOn : ETxtMenuAutoOff ));
			aMenuPane->SetItemTextL( EBtHidCmdTests, Txt( ETxtMenuTests ));
			aMenuPane->SetItemTextL( EBtHidCmdForgetMouse, Txt( ETxtMenuForgetMouse ));
			aMenuPane->SetItemTextL( EBtHidCmdForgetKeyboard, Txt( ETxtMenuForgetKeys ));
			aMenuPane->SetItemTextL( EBtHidCmdPs3, Txt( ETxtMenuPs3 ));
			aMenuPane->SetItemTextL( EBtHidCmdAbout, Txt( ETxtMenuAbout ));
			aMenuPane->SetItemTextL( EAknCmdExit, Txt( ETxtMenuExit ));
		}
		else if( aResourceId == R_MGR_TESTS_MENU )
		{
			aMenuPane->SetItemTextL( EBtHidCmdTestMouse, Txt( ETxtMenuTestMouse ));
			aMenuPane->SetItemTextL( EBtHidCmdTestKeys, Txt( ETxtMenuTestKeys ));
			aMenuPane->SetItemTextL( EBtHidCmdTestPads, Txt( ETxtMenuTestPads ));
		}
		else if( aResourceId == R_MGR_PROF_MENU )
		{
			/* el predeterminado no se borra ni se le cambia el nombre */
			TInt cur = iProfView->Current();
			TBool def = cur <= 0 || cur >= iProfNames->Count() || !( *iProfNames )[cur].CompareF( Profiles::DefaultName());
			aMenuPane->SetItemTextL( EBtHidCmdProfUse, Txt( ETxtProfUse ));
			aMenuPane->SetItemTextL( EBtHidCmdProfEdit, Txt( ETxtProfEdit ));
			aMenuPane->SetItemTextL( EBtHidCmdProfNew, Txt( ETxtProfNew ));
			aMenuPane->SetItemTextL( EBtHidCmdProfRename, Txt( ETxtProfRename ));
			aMenuPane->SetItemTextL( EBtHidCmdProfDelete, Txt( ETxtProfDelete ));
			aMenuPane->SetItemDimmed( EBtHidCmdProfRename, def );
			aMenuPane->SetItemDimmed( EBtHidCmdProfDelete, def );
		}
		else if( aResourceId == R_MGR_EDIT_MENU )
		{
			aMenuPane->SetItemTextL( EBtHidCmdProfKey, Txt( ETxtProfKey ));
			aMenuPane->SetItemTextL( EBtHidCmdProfClear, Txt( ETxtProfClear ));
			aMenuPane->SetItemTextL( EBtHidCmdProfPad, Txt( ETxtProfPad ));
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

	/* el navegador del telefono (UID 0x10008D39) con la pagina: "4 <url>" = abrir esa direccion */
	static TInt OpenWeb( TAny * )
	{
		RApaLsSession ls;
		if( ls.Connect() == KErrNone )
		{
			TThreadId id;
			ls.StartDocument( _L( "4 http://leoncini.com.ar" ), TUid::Uid( 0x10008D39 ), id );
			ls.Close();
		}
		return KErrNone;
	}

	/* "Iniciar con el telefono": el servidor lo lee al arrancar con el telefono (server.cpp) */
	static TBool AutoStart()
	{
		RFs   fs;
		RFile f;
		TBuf8<1> b;
		TBool on = ETrue;
		if( fs.Connect() != KErrNone )
			return on;
		if( f.Open( fs, _L( "c:\\data\\bthid_autoinicio.txt" ), EFileRead | EFileShareAny ) == KErrNone )
		{
			if( f.Read( b ) == KErrNone && b.Length() && b[0] == '0' )
				on = EFalse;
			f.Close();
		}
		static_cast<RHandleBase &>( fs ).Close();
		return on;
	}
	static void SetAutoStart( TBool aOn )
	{
		RFs   fs;
		RFile f;
		if( fs.Connect() != KErrNone )
			return;
		if( f.Replace( fs, _L( "c:\\data\\bthid_autoinicio.txt" ), EFileWrite ) == KErrNone )
		{
			f.Write( aOn ? _L8( "1" ) : _L8( "0" ));
			f.Close();
		}
		static_cast<RHandleBase &>( fs ).Close();
	}

	/* ---- perfiles ---- */

	enum TProfScreen { EProfNone, EProfList, EProfEdit };

	void SetTitleL( const TDesC *aText )
	{
		CAknTitlePane *title = static_cast<CAknTitlePane *>( StatusPane()->ControlL( TUid::Uid( EEikStatusPaneUidTitle )));
		if( aText )
			title->SetTextL( *aText );
		else
			title->SetTextToDefaultL();
	}

	void SetScreenL( TProfScreen aScreen )
	{
		if( aScreen != EProfNone && iProfScreen == EProfNone )
		{
			iView->MakeVisible( EFalse );
			iProfView->MakeVisible( ETrue );
			AddToStackL( iProfView );
			Cba()->SetCommandSetL( R_AVKON_SOFTKEYS_OPTIONS_BACK );
			Cba()->DrawNow();
		}
		else if( aScreen == EProfNone && iProfScreen != EProfNone )
		{
			RemoveFromStack( iProfView );
			iProfView->MakeVisible( EFalse );
			iView->MakeVisible( ETrue );
			Cba()->SetCommandSetL( R_AVKON_SOFTKEYS_OPTIONS_EXIT );
			Cba()->DrawNow();
			SetTitleL( NULL );
			Refresh();
		}
		iProfScreen = aScreen;
		iEikonEnv->AppUiFactory()->MenuBar()->SetMenuTitleResourceId(
			aScreen == EProfList ? R_MGR_PROF_MENUBAR : aScreen == EProfEdit ? R_MGR_EDIT_MENUBAR : R_MGR_MENUBAR );
	}

	// la lista de perfiles, con el que esta en uso marcado (y elegido, si no se pide otro)
	void ShowProfilesL( const TDesC *aSelect )
	{
		CDesCArray &items = iProfView->Items();
		TBuf<32> active;
		TBuf<64> line;
		TInt i, sel = 0;
		TPtrC title = Txt( ETxtProfTitle );

		Profiles::ListL( *iProfNames );
		Profiles::Active( active );
		items.Reset();
		for( i = 0; i < iProfNames->Count(); i++ )
		{
			TPtrC name = ( *iProfNames )[i];
			line.Copy( _L( "\t" ));
			line.Append( name.Left( 32 ));
			if( !name.CompareF( active ))
			{
				line.Append( Txt( ETxtProfInUse ));
				if( !aSelect )
					sel = i;
			}
			if( aSelect && !name.CompareF( *aSelect ))
				sel = i;
			items.AppendL( line );
		}
		SetScreenL( EProfList );
		iProfView->SetCommand( EBtHidCmdProfUse );
		iProfView->ItemsChangedL( sel );
		SetTitleL( &title );
	}

	// los botones del joystick iProfPad del perfil que se edita, cada uno con su tecla
	void ShowEditL( TInt aSelect )
	{
		CDesCArray &items = iProfView->Items();
		TBuf<64> line;
		TInt in;

		items.Reset();
		for( in = 0; in < EProfInputs; in++ )
		{
			TPtrC button = Profiles::InputName( in, gSpanish );
			TPtrC key = Profiles::KeyName( Profiles::KeyOfUsage( iProf.iMap[iProfPad][in] ), gSpanish );
			line.Format( _L( "\t%S: %S" ), &button, &key );
			items.AppendL( line );
		}
		SetScreenL( EProfEdit );
		iProfView->SetCommand( EBtHidCmdProfKey );
		iProfView->ItemsChangedL( aSelect );
		line.Format( Txt( ETxtProfEditTitle ), &iProf.iName, iProfPad + 1 );
		SetTitleL( &line );
	}

	// elegir de una lista: el lugar elegido, o -1 si se cancelo
	TInt AskListL( CDesCArray &aItems, const TDesC &aHeading, TInt aCurrent )
	{
		TInt sel = aCurrent;
		CAknListQueryDialog *dlg = new( ELeave ) CAknListQueryDialog( &sel );
		dlg->PrepareLC( R_MGR_LIST_QUERY );
		dlg->SetItemTextArray( &aItems );
		dlg->SetOwnershipType( ELbmDoesNotOwnItemArray );
		dlg->QueryHeading()->SetTextL( aHeading );
		dlg->ListBox()->HandleItemAdditionL();
		if( aCurrent >= 0 && aCurrent < aItems.Count())
			dlg->ListBox()->SetCurrentItemIndex( aCurrent );
		return dlg->RunLD() ? sel : -1;
	}

	TBool AskPadL()
	{
		CDesCArrayFlat *items = new( ELeave ) CDesCArrayFlat( EBtHidMaxPads );
		TBuf<32> b;
		TInt i, pad;
		CleanupStack::PushL( items );
		for( i = 0; i < EBtHidMaxPads; i++ )
		{
			b.Format( Txt( ETxtPad ), i + 1 );
			items->AppendL( b );
		}
		pad = AskListL( *items, Txt( ETxtProfPickPad ), iProfPad );
		CleanupStack::PopAndDestroy( items );
		if( pad < 0 )
			return EFalse;
		iProfPad = pad;
		return ETrue;
	}

	// el nombre de un perfil: sin los caracteres que no van en un archivo y sin espacios en las puntas
	TBool AskNameL( TDes &aName )
	{
		CAknTextQueryDialog *dlg = CAknTextQueryDialog::NewL( aName );
		TInt i;
		dlg->PrepareLC( R_MGR_NAME_QUERY );
		dlg->SetPromptL( Txt( ETxtProfName ));
		dlg->SetMaxLength( 24 );
		if( !dlg->RunLD())
			return EFalse;
		for( i = aName.Length() - 1; i >= 0; i-- )
			if( aName[i] < ' ' || _L( "\\/:*?\"<>|." ).Locate( aName[i] ) >= 0 )
				aName.Delete( i, 1 );
		aName.Trim();
		return aName.Length() > 0;
	}

	void NoteL( const TDesC &aText, TBool aOk = EFalse )
	{
		if( aOk )
			( new( ELeave ) CAknConfirmationNote( ETrue ))->ExecuteLD( aText );
		else
			( new( ELeave ) CAknInformationNote( ETrue ))->ExecuteLD( aText );
	}

	// el servidor vuelve a leer las teclas (y prende el modo teclado si se pide)
	void ReloadKeys( TBool aTurnOn )
	{
		TBtHidStatusPckg st;
		if( iConnectErr != KErrNone || iMgr.Status( st ) != KErrNone )
			return;
		if( aTurnOn || st().iPadKeys )
			iMgr.SetPadKeys( ETrue );
	}

	// guarda el perfil que se edita; si es el que esta en uso, el servidor lo toma en el momento
	void SaveProfL()
	{
		TBuf<32> active;
		TBuf<48> err;
		TInt e = Profiles::Save( iProf );
		if( e != KErrNone )
		{
			err.Format( Txt( ETxtProfSaveErr ), e );
			NoteL( err );
			return;
		}
		Profiles::Active( active );
		if( !active.CompareF( iProf.iName ) && Profiles::Activate( iProf.iName ) == KErrNone )
			ReloadKeys( EFalse );
	}

	void ProfCommandL( TInt aCommand )
	{
		TInt cur = iProfView->Current();
		TBuf<32> name;
		TBuf<96> text;

		if( iProfScreen == EProfList )
		{
			if( cur < 0 || cur >= iProfNames->Count())
				cur = 0;
			name = ( *iProfNames )[cur].Left( name.MaxLength());
		}
		switch( aCommand )
		{
		case EBtHidCmdProfUse:
			if( Profiles::Activate( name ) == KErrNone )
			{
				ReloadKeys( ETrue );
				ShowProfilesL( &name );
				text.Format( Txt( ETxtProfUsing ), &name );
				NoteL( text, ETrue );
			}
			break;
		case EBtHidCmdProfEdit:
			if( Profiles::Load( name, iProf ) && AskPadL())
				ShowEditL( 0 );
			break;
		case EBtHidCmdProfNew:
			if( !AskNameL( name ))
				break;
			if( Profiles::Exists( name ))
			{
				NoteL( Txt( ETxtProfExists ));
				break;
			}
			Profiles::Empty( iProf, name ); // todas las teclas sin asignar
			SaveProfL();
			if( AskPadL())
				ShowEditL( 0 );
			else
				ShowProfilesL( &name );
			break;
		case EBtHidCmdProfRename:
		{
			TBuf<32> active, to = name;
			if( !name.CompareF( Profiles::DefaultName()))
			{
				NoteL( Txt( ETxtProfNoRename ));
				break;
			}
			if( !AskNameL( to ) || to == name )
				break;
			if( to.CompareF( name ) && Profiles::Exists( to ))
			{
				NoteL( Txt( ETxtProfExists ));
				break;
			}
			Profiles::Active( active );
			if( Profiles::Rename( name, to ) == KErrNone && !active.CompareF( name ))
				Profiles::Activate( to );
			ShowProfilesL( &to );
			break;
		}
		case EBtHidCmdProfDelete:
		{
			TBuf<32> active;
			if( !name.CompareF( Profiles::DefaultName()))
			{
				NoteL( Txt( ETxtProfNoDelete ));
				break;
			}
			text.Format( Txt( ETxtProfAskDelete ), &name );
			CAknQueryDialog *dlg = CAknQueryDialog::NewL();
			dlg->PrepareLC( R_MGR_CONFIRM_QUERY );
			dlg->SetPromptL( text );
			if( !dlg->RunLD())
				break;
			Profiles::Active( active );
			Profiles::Delete( name );
			if( !active.CompareF( name ) && Profiles::Activate( Profiles::DefaultName()) == KErrNone )
				ReloadKeys( EFalse ); // se borro el que estaba en uso: queda el predeterminado
			ShowProfilesL( NULL );
			break;
		}
		case EBtHidCmdProfKey:
		{
			CDesCArrayFlat *keys;
			TInt i, k;
			if( cur < 0 || cur >= EProfInputs )
				break;
			keys = new( ELeave ) CDesCArrayFlat( Profiles::KeyCount());
			CleanupStack::PushL( keys );
			for( i = 0; i < Profiles::KeyCount(); i++ )
				keys->AppendL( Profiles::KeyName( i, gSpanish ));
			k = AskListL( *keys, Profiles::InputName( cur, gSpanish ), Profiles::KeyOfUsage( iProf.iMap[iProfPad][cur] ));
			CleanupStack::PopAndDestroy( keys );
			if( k < 0 )
				break;
			iProf.iMap[iProfPad][cur] = (TUint8)Profiles::KeyUsage( k );
			SaveProfL();
			ShowEditL( cur );
			break;
		}
		case EBtHidCmdProfClear:
			if( cur < 0 || cur >= EProfInputs )
				break;
			iProf.iMap[iProfPad][cur] = 0;
			SaveProfL();
			ShowEditL( cur );
			break;
		case EBtHidCmdProfPad:
			if( AskPadL())
				ShowEditL( cur );
			break;
		}
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
		case EBtHidCmdAbout:
		{
			/* la version del driver (version.h, para saber cual tiene cada telefono), quien lo hizo y el
			   enlace a la pagina: tocarlo (o el centro) abre el navegador */
			TBuf<40>  title;
			TBuf<8>   ver;
			TBuf<300> text;
			CAknMessageQueryDialog *dlg;
			TCallBack web( OpenWeb, this );
			title.Copy( _L8( "Bluetooth HID " BTHID_VERSION ));
			ver.Copy( _L8( BTHID_VERSION ));
			text.Format( Txt( ETxtAboutText ), &ver );
			dlg = CAknMessageQueryDialog::NewL( text );
			dlg->PrepareLC( R_MGR_ABOUT );
			dlg->SetHeaderTextL( title );
			dlg->SetLink( web );
			dlg->RunLD();
			break;
		}
		case EBtHidCmdPs3:
		{
			/* la direccion del telefono, para grabarsela por USB a un joystick de PS3 (tools/ds3pair.py) */
			TBuf<48>  addr;
			TBuf<400> text;
			CAknMessageQueryDialog *dlg;
			PhoneAddress( addr );
			if( !addr.Length())
				addr = Txt( ETxtPs3NoAddr );
			text.Format( Txt( ETxtPs3Text ), &addr );
			dlg = CAknMessageQueryDialog::NewL( text );
			dlg->PrepareLC( R_MGR_ABOUT );
			dlg->SetHeaderTextL( Txt( ETxtMenuPs3 ));
			dlg->RunLD();
			break;
		}
		case EBtHidCmdTestMouse:
			SetMode( EModeMouse );
			break;
		case EBtHidCmdTestKeys:
			SetMode( EModeKeys );
			break;
		case EBtHidCmdTestPads:
			SetMode( EModePads );
			break;
		case EBtHidCmdPickPad:
			if( iConnectErr == KErrNone )
				iSearch->Start( EBtHidGamepad );
			break;
		case EBtHidCmdPadKeys:
			if( iConnectErr == KErrNone )
			{
				TBtHidStatusPckg st;
				if( iMgr.Status( st ) == KErrNone )
					iMgr.SetPadKeys( !st().iPadKeys );
			}
			Refresh();
			break;
		case EBtHidCmdForgetPads:
			if( iConnectErr == KErrNone )
				iMgr.Forget( EBtHidGamepad );
			Refresh();
			break;
		case EBtHidCmdForget:
			if( iConnectErr == KErrNone )
				iMgr.ForgetAll();
			Refresh();
			break;
		case EBtHidCmdProfiles:
			ShowProfilesL( NULL );
			break;
		case EBtHidCmdAutoStart:
			SetAutoStart( !AutoStart());
			break;
		case EBtHidCmdProfUse:
		case EBtHidCmdProfEdit:
		case EBtHidCmdProfNew:
		case EBtHidCmdProfRename:
		case EBtHidCmdProfDelete:
		case EBtHidCmdProfKey:
		case EBtHidCmdProfClear:
		case EBtHidCmdProfPad:
			ProfCommandL( aCommand );
			break;
		case EAknSoftkeyBack:
			if( iProfScreen == EProfEdit )
			{
				TBuf<32> name = iProf.iName;
				ShowProfilesL( &name );
			}
			else if( iProfScreen == EProfList )
				SetScreenL( EProfNone );
			else
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
	CDesCArray    *iSlots;      // dos renglones por lugar
	CDesCArray    *iFoot;
	TBool          iSlotUsed[ESlots];
	TInt           iSlotState[ESlots];
	TBuf8<6>       iSlotAddr[ESlots];
	CMgrView      *iView;
	CDesCArray    *iLines;
	CSearch       *iSearch;
	CEvents       *iEvents;
	CPeriodic     *iTimer;
	CProfView     *iProfView;
	CDesCArray    *iProfNames;  // los perfiles, en el orden de la lista
	TProfScreen    iProfScreen;
	TPadProfile    iProf;       // el que se edita
	TInt           iProfPad;    // y de que joystick
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
	TInt             i, slot;

	iLines->Reset();
	iSlots->Reset();
	iFoot->Reset();
	line.Copy( _L8( "Bluetooth HID " BTHID_VERSION ));
	AddLine( line );
	if( iConnectErr != KErrNone )
	{
		line.Format( Txt( ETxtNoServer ), iConnectErr );
		AddLine( line );
		iView->SetLines( iLines );
		iView->SetSlots( NULL, NULL, NULL );
		return;
	}
	if( iMgr.Status( st ) != KErrNone )
	{
		AddLine( Txt( ETxtServerDead ));
		iView->SetLines( iLines );
		iView->SetSlots( NULL, NULL, NULL );
		return;
	}

	/* cada lugar: los joysticks por su numero de jugador (los que todavia no tienen, en el primero libre), el mouse
	   (los guardados sin tipo cuentan como mouse) y el teclado */
	const TBtHidDeviceInfo *dev[ESlots];
	for( slot = 0; slot < ESlots; slot++ )
		dev[slot] = NULL;
	for( i = 0; i < st().iCount; i++ )
	{
		const TBtHidDeviceInfo *d = &st().iDevice[i];
		TInt type = d->iType ? d->iType : EBtHidMouse;
		if( type == EBtHidMouse ) dev[ESlotMouse] = d;
		else if( type == EBtHidKeyboard ) dev[ESlotKeyboard] = d;
		else if( type == EBtHidGamepad && d->iPlayer >= 1 && d->iPlayer <= EBtHidMaxPads && !dev[ESlotPad1 + d->iPlayer - 1] )
			dev[ESlotPad1 + d->iPlayer - 1] = d;
	}
	for( i = 0; i < st().iCount; i++ )
	{
		const TBtHidDeviceInfo *d = &st().iDevice[i];
		TBool placed = EFalse;
		if( d->iType != EBtHidGamepad )
			continue;
		for( slot = ESlotPad1; slot < ESlots; slot++ )
			if( dev[slot] == d ) placed = ETrue;
		for( slot = ESlotPad1; slot < ESlots && !placed; slot++ )
			if( !dev[slot] ) { dev[slot] = d; placed = ETrue; }
	}
	for( slot = 0; slot < ESlots; slot++ )
	{
		const TBtHidDeviceInfo *d = dev[slot];
		TBuf<32> kind, name;
		if( slot == ESlotMouse ) kind = Txt( ETxtMouse );
		else if( slot == ESlotKeyboard ) kind = Txt( ETxtKeyboard );
		else kind.Format( Txt( ETxtPad ), slot - ESlotPad1 + 1 );
		iSlotUsed[slot] = d != NULL;
		iSlotState[slot] = d ? d->iState : EBtHidDisconnected;
		iSlotAddr[slot].Zero();
		if( !d )
		{
			TPtrC none = Txt( ETxtSlotEmpty );
			line.Format( _L( "%S: %S" ), &kind, &none );
			TRAP_IGNORE( iSlots->AppendL( line ));
			TRAP_IGNORE( iSlots->AppendL( KNullDesC ));
			continue;
		}
		iSlotAddr[slot].Copy( d->iAddr, 6 );
		name.Copy( d->iName );
		if( !name.Length())
			name.Format( _L( "%02x:%02x:%02x:%02x:%02x:%02x" ), d->iAddr[0], d->iAddr[1], d->iAddr[2], d->iAddr[3], d->iAddr[4], d->iAddr[5] );
		line.Format( _L( "%S: %S" ), &kind, &name );
		TRAP_IGNORE( iSlots->AppendL( line ));
		if( d->iState == EBtHidConnected )
		{
			line = Txt( ETxtConnected );
			if( d->iType == EBtHidGamepad && d->iCharging )
				line.Append( Txt( ETxtCharging ));
			else if( d->iType == EBtHidGamepad && d->iBattery >= 0 )
			{
				TBuf<32> b;
				b.Format( Txt( ETxtBattery ), d->iBattery );
				line.Append( b );
			}
			else if( d->iType == EBtHidGamepad )
				line.Append( Txt( ETxtBatteryNone )); // (no la informa: algunos compatibles)
		}
		else if( d->iState == EBtHidStopped )
			line = Txt( ETxtSlotStopped );
		else if( d->iState == EBtHidWaiting )
			line = Txt( d->iType == EBtHidGamepad ? ETxtSlotStopped : ETxtWaiting );
		else if( d->iType == EBtHidGamepad )
			line = Txt( ETxtSlotCalling );
		else if( d->iState == EBtHidConnecting )
			line = Txt( ETxtConnecting );
		else
			line.Format( Txt( ETxtNotConnected ), d->iError );
		TRAP_IGNORE( iSlots->AppendL( line ));
	}
	TRAP_IGNORE( iFoot->AppendL( Txt( ETxtSlotHelp )));
	line.Format( Txt( ETxtApps ), st().iClients );
	TRAP_IGNORE( iFoot->AppendL( line ));
	if( iSearch->LastError() != KErrNone && iSearch->LastError() != KErrCancel )
	{
		line.Format( Txt( ETxtSearchFailed ), iSearch->LastError());
		TRAP_IGNORE( iFoot->AppendL( line ));
	}
	iView->SetLines( iLines );
	iView->SetSlots( iSlots, iFoot, this );
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
