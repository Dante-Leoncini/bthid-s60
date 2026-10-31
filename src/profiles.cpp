/*
profiles.cpp - perfiles del joystick como teclado, ver profiles.h
*/
#include "profiles.h"
#include <f32file.h>

_LIT( KProfDir, "c:\\data\\bthid_perfiles\\" );
_LIT( KProfActive, "c:\\data\\bthid_perfiles\\activo.dat" );
_LIT( KServerKeys, "c:\\data\\bthid_teclas.txt" );
_LIT( KProfExt, ".txt" );
_LIT( KDefaultName, "Predeterminado" );

/* RFs::Close no existe en S60 3rd: el de RHandleBase */
static void CloseFs( RFs &aFs ) { static_cast<RHandleBase &>( aFs ).Close(); }

/* los botones: el nombre en el archivo (el mismo que lee el servidor) y para mostrar */
static const char *const KInputToken[EProfInputs] = { "arriba", "derecha", "abajo", "izquierda",
	"cruz", "circulo", "cuadrado", "triangulo", "l1", "r1", "l2", "r2", "select", "start", "l3", "r3", "ps",
	"stick_arriba", "stick_derecha", "stick_abajo", "stick_izquierda" };
static const TText *const KInputEs[EProfInputs] = { _S( "Arriba" ), _S( "Derecha" ), _S( "Abajo" ),
	_S( "Izquierda" ), _S( "Cruz (X)" ), _S( "C\x00edrculo" ), _S( "Cuadrado" ), _S( "Tri\x00e1ngulo" ), _S( "L1" ),
	_S( "R1" ), _S( "L2" ), _S( "R2" ), _S( "Select" ), _S( "Start" ), _S( "L3" ), _S( "R3" ), _S( "PS" ),
	_S( "Stick izq. arriba" ), _S( "Stick izq. derecha" ), _S( "Stick izq. abajo" ), _S( "Stick izq. izquierda" ) };
static const TText *const KInputEn[EProfInputs] = { _S( "Up" ), _S( "Right" ), _S( "Down" ), _S( "Left" ),
	_S( "Cross (X)" ), _S( "Circle" ), _S( "Square" ), _S( "Triangle" ), _S( "L1" ), _S( "R1" ), _S( "L2" ),
	_S( "R2" ), _S( "Select" ), _S( "Start" ), _S( "L3" ), _S( "R3" ), _S( "PS" ), _S( "Left stick up" ),
	_S( "Left stick right" ), _S( "Left stick down" ), _S( "Left stick left" ) };

/* las teclas: el nombre en el archivo, para mostrar y el uso HID (la pagina de teclado) */
struct TKeyInfo { const char *iToken; const TText *iEs; const TText *iEn; TUint8 iUsage; };
static const TKeyInfo KKeys[] =
{
	{ "ninguna", _S( "Sin asignar" ), _S( "Not assigned" ), 0 },
	{ "a", _S( "A" ), _S( "A" ), 4 }, { "b", _S( "B" ), _S( "B" ), 5 }, { "c", _S( "C" ), _S( "C" ), 6 },
	{ "d", _S( "D" ), _S( "D" ), 7 }, { "e", _S( "E" ), _S( "E" ), 8 }, { "f", _S( "F" ), _S( "F" ), 9 },
	{ "g", _S( "G" ), _S( "G" ), 10 }, { "h", _S( "H" ), _S( "H" ), 11 }, { "i", _S( "I" ), _S( "I" ), 12 },
	{ "j", _S( "J" ), _S( "J" ), 13 }, { "k", _S( "K" ), _S( "K" ), 14 }, { "l", _S( "L" ), _S( "L" ), 15 },
	{ "m", _S( "M" ), _S( "M" ), 16 }, { "n", _S( "N" ), _S( "N" ), 17 }, { "o", _S( "O" ), _S( "O" ), 18 },
	{ "p", _S( "P" ), _S( "P" ), 19 }, { "q", _S( "Q" ), _S( "Q" ), 20 }, { "r", _S( "R" ), _S( "R" ), 21 },
	{ "s", _S( "S" ), _S( "S" ), 22 }, { "t", _S( "T" ), _S( "T" ), 23 }, { "u", _S( "U" ), _S( "U" ), 24 },
	{ "v", _S( "V" ), _S( "V" ), 25 }, { "w", _S( "W" ), _S( "W" ), 26 }, { "x", _S( "X" ), _S( "X" ), 27 },
	{ "y", _S( "Y" ), _S( "Y" ), 28 }, { "z", _S( "Z" ), _S( "Z" ), 29 },
	{ "1", _S( "1" ), _S( "1" ), 30 }, { "2", _S( "2" ), _S( "2" ), 31 }, { "3", _S( "3" ), _S( "3" ), 32 },
	{ "4", _S( "4" ), _S( "4" ), 33 }, { "5", _S( "5" ), _S( "5" ), 34 }, { "6", _S( "6" ), _S( "6" ), 35 },
	{ "7", _S( "7" ), _S( "7" ), 36 }, { "8", _S( "8" ), _S( "8" ), 37 }, { "9", _S( "9" ), _S( "9" ), 38 },
	{ "0", _S( "0" ), _S( "0" ), 39 },
	{ "enter", _S( "Enter" ), _S( "Enter" ), 40 }, { "esc", _S( "Esc" ), _S( "Esc" ), 41 },
	{ "espacio", _S( "Espacio" ), _S( "Space" ), 44 }, { "borrar", _S( "Borrar" ), _S( "Backspace" ), 42 },
	{ "tab", _S( "Tab" ), _S( "Tab" ), 43 },
	{ "arriba", _S( "Flecha arriba" ), _S( "Up arrow" ), 82 }, { "abajo", _S( "Flecha abajo" ), _S( "Down arrow" ), 81 },
	{ "izquierda", _S( "Flecha izquierda" ), _S( "Left arrow" ), 80 },
	{ "derecha", _S( "Flecha derecha" ), _S( "Right arrow" ), 79 },
	{ "ctrl", _S( "Ctrl" ), _S( "Ctrl" ), 224 }, { "shift", _S( "Shift" ), _S( "Shift" ), 225 },
	{ "alt", _S( "Alt" ), _S( "Alt" ), 226 },
	{ "f1", _S( "F1" ), _S( "F1" ), 58 }, { "f2", _S( "F2" ), _S( "F2" ), 59 }, { "f3", _S( "F3" ), _S( "F3" ), 60 },
	{ "f4", _S( "F4" ), _S( "F4" ), 61 }, { "f5", _S( "F5" ), _S( "F5" ), 62 }, { "f6", _S( "F6" ), _S( "F6" ), 63 },
	{ "f7", _S( "F7" ), _S( "F7" ), 64 }, { "f8", _S( "F8" ), _S( "F8" ), 65 }, { "f9", _S( "F9" ), _S( "F9" ), 66 },
	{ "f10", _S( "F10" ), _S( "F10" ), 67 }, { "f11", _S( "F11" ), _S( "F11" ), 68 }, { "f12", _S( "F12" ), _S( "F12" ), 69 },
	{ "inicio", _S( "Inicio" ), _S( "Home" ), 74 }, { "fin", _S( "Fin" ), _S( "End" ), 77 },
	{ "pgarriba", _S( "Re P\x00e1g" ), _S( "Page up" ), 75 }, { "pgabajo", _S( "Av P\x00e1g" ), _S( "Page down" ), 78 },
	{ "insertar", _S( "Insertar" ), _S( "Insert" ), 73 }, { "suprimir", _S( "Suprimir" ), _S( "Delete" ), 76 },
	{ "coma", _S( "Coma" ), _S( "Comma" ), 54 }, { "punto", _S( "Punto" ), _S( "Period" ), 55 },
	{ "menos", _S( "Menos" ), _S( "Minus" ), 45 }, { "igual", _S( "Igual" ), _S( "Equals" ), 46 },
};
static const TInt KNumKeys = sizeof( KKeys ) / sizeof( KKeys[0] );

/* el predeterminado: las mismas teclas que pone el servidor si no hay archivo */
static const char KDefault[] =
	"1 arriba arriba\n1 abajo abajo\n1 izquierda izquierda\n1 derecha derecha\n"
	"1 stick_arriba arriba\n1 stick_abajo abajo\n1 stick_izquierda izquierda\n1 stick_derecha derecha\n"
	"1 cruz z\n1 circulo x\n1 cuadrado a\n1 triangulo s\n1 l1 q\n1 r1 w\n1 l2 e\n1 r2 r\n"
	"1 select espacio\n1 start enter\n1 l3 1\n1 r3 2\n1 ps esc\n"
	"2 arriba i\n2 abajo k\n2 izquierda j\n2 derecha l\n"
	"2 stick_arriba i\n2 stick_abajo k\n2 stick_izquierda j\n2 stick_derecha l\n"
	"2 cruz v\n2 circulo b\n2 cuadrado f\n2 triangulo g\n2 l1 t\n2 r1 y\n2 l2 u\n2 r2 o\n"
	"2 select n\n2 start m\n2 l3 3\n2 r3 4\n2 ps borrar\n"
	"3 arriba 8\n3 abajo 2\n3 izquierda 4\n3 derecha 6\n"
	"3 stick_arriba 8\n3 stick_abajo 2\n3 stick_izquierda 4\n3 stick_derecha 6\n"
	"3 cruz c\n3 circulo d\n3 cuadrado h\n3 triangulo p\n3 select 5\n3 start 7\n"
	"4 arriba inicio\n4 abajo fin\n4 izquierda suprimir\n4 derecha pgabajo\n"
	"4 stick_arriba inicio\n4 stick_abajo fin\n4 stick_izquierda suprimir\n4 stick_derecha pgabajo\n"
	"4 cruz coma\n4 circulo punto\n4 cuadrado 9\n4 triangulo 0\n4 select tab\n4 start insertar\n";

static void Parse( const TDesC8 &aText, TPadProfile &aProf )
{
	TLex8 lines( aText );
	Mem::FillZ( aProf.iMap, sizeof( aProf.iMap ));
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
		TInt pad, in = -1, key = -1, i;
		if( !a.Length() || a[0] == '#' || !b.Length() || !c.Length())
			continue;
		pad = a[0] - '1';
		bn.Copy( b.Left( bn.MaxLength()));
		cn.Copy( c.Left( cn.MaxLength()));
		bn.LowerCase();
		cn.LowerCase();
		for( i = 0; i < EProfInputs; i++ )
			if( !bn.Compare( TPtrC8( (const TUint8 *)KInputToken[i] ))) in = i;
		for( i = 0; i < KNumKeys; i++ )
			if( !cn.Compare( TPtrC8( (const TUint8 *)KKeys[i].iToken ))) key = i;
		if( pad >= 0 && pad < EBtHidMaxPads && in >= 0 && key >= 0 )
			aProf.iMap[pad][in] = KKeys[key].iUsage;
	}
}

static HBufC8 *Serialize( const TPadProfile &aProf )
{
	HBufC8 *buf = HBufC8::New( 512 + EBtHidMaxPads * EProfInputs * 32 );
	TInt pad, in;
	if( !buf )
		return NULL;
	TPtr8 p = buf->Des();
	TBuf8<32> name;
	name.Copy( aProf.iName );
	p.Append( _L8( "# Bluetooth HID, perfil del joystick como teclado: " ));
	p.Append( name );
	p.Append( _L8( "\r\n# Renglones: joystick boton tecla (se edita desde la app: Opciones > Perfiles del joystick)\r\n" ));
	for( pad = 0; pad < EBtHidMaxPads; pad++ )
		for( in = 0; in < EProfInputs; in++ )
		{
			TInt k = Profiles::KeyOfUsage( aProf.iMap[pad][in] );
			if( k <= 0 )
				continue;
			p.AppendFormat( _L8( "%d %s %s\r\n" ), pad + 1, KInputToken[in], KKeys[k].iToken );
		}
	return buf;
}

static void FileName( const TDesC &aName, TFileName &aFile )
{
	aFile = KProfDir;
	aFile.Append( aName );
	aFile.Append( KProfExt );
}

static TInt WriteFile( const TDesC &aFile, const TDesC8 &aData )
{
	RFs   fs;
	RFile f;
	TInt  err = fs.Connect();
	if( err != KErrNone )
		return err;
	fs.MkDirAll( KProfDir );
	err = f.Replace( fs, aFile, EFileWrite );
	if( err == KErrNone )
	{
		err = f.Write( aData );
		f.Close();
	}
	CloseFs( fs );
	return err;
}

const TDesC &Profiles::DefaultName()
{
	return KDefaultName;
}

void Profiles::Default( TPadProfile &aProf )
{
	aProf.iName = KDefaultName;
	Parse( TPtrC8( (const TUint8 *)KDefault, sizeof( KDefault ) - 1 ), aProf );
}

void Profiles::Empty( TPadProfile &aProf, const TDesC &aName )
{
	aProf.iName = aName.Left( aProf.iName.MaxLength());
	Mem::FillZ( aProf.iMap, sizeof( aProf.iMap ));
}

TBool Profiles::Exists( const TDesC &aName )
{
	RFs fs;
	TFileName file;
	TEntry e;
	TBool ok = EFalse;
	FileName( aName, file );
	if( fs.Connect() == KErrNone )
	{
		ok = fs.Entry( file, e ) == KErrNone;
		CloseFs( fs );
	}
	return ok;
}

void Profiles::ListL( CDesCArray &aNames )
{
	RFs   fs;
	CDir *dir = NULL;
	TFileName pattern;
	TInt  i;

	aNames.Reset();
	if( !Exists( KDefaultName ))
	{
		TPadProfile p;
		Default( p );
		Save( p );
	}
	aNames.AppendL( KDefaultName );
	User::LeaveIfError( fs.Connect());
	pattern = KProfDir;
	pattern.Append( _L( "*" ));
	pattern.Append( KProfExt );
	if( fs.GetDir( pattern, KEntryAttNormal, ESortByName, dir ) == KErrNone && dir )
	{
		for( i = 0; i < dir->Count(); i++ )
		{
			TParsePtrC parse(( *dir )[i].iName );
			TPtrC name = parse.Name();
			if( !name.CompareF( KDefaultName ))
				continue;
			TRAP_IGNORE( aNames.AppendL( name ));
		}
	}
	delete dir;
	CloseFs( fs );
}

TBool Profiles::Load( const TDesC &aName, TPadProfile &aProf )
{
	RFs    fs;
	RFile  f;
	TFileName file;
	HBufC8 *buf = NULL;
	TInt   size = 0;

	FileName( aName, file );
	if( fs.Connect() != KErrNone )
		return EFalse;
	if( f.Open( fs, file, EFileRead ) == KErrNone )
	{
		if( f.Size( size ) == KErrNone && size >= 0 && size < 16384 && ( buf = HBufC8::New( size + 1 )) != NULL )
		{
			TPtr8 p = buf->Des();
			f.Read( p );
		}
		f.Close();
	}
	CloseFs( fs );
	if( !buf )
	{
		if( !aName.CompareF( KDefaultName ))
		{
			Default( aProf );
			return ETrue;
		}
		return EFalse;
	}
	aProf.iName = aName.Left( aProf.iName.MaxLength());
	Parse( *buf, aProf );
	delete buf;
	return ETrue;
}

TInt Profiles::Save( const TPadProfile &aProf )
{
	TFileName file;
	HBufC8 *buf = Serialize( aProf );
	TInt err;
	if( !buf )
		return KErrNoMemory;
	FileName( aProf.iName, file );
	err = WriteFile( file, *buf );
	delete buf;
	return err;
}

TInt Profiles::Delete( const TDesC &aName )
{
	RFs fs;
	TFileName file;
	TInt err;
	if( !aName.CompareF( KDefaultName ))
		return KErrAccessDenied; // el predeterminado siempre esta
	FileName( aName, file );
	err = fs.Connect();
	if( err != KErrNone )
		return err;
	err = fs.Delete( file );
	CloseFs( fs );
	return err;
}

TInt Profiles::Rename( const TDesC &aOld, const TDesC &aNew )
{
	RFs fs;
	TFileName from, to;
	TInt err;
	if( !aOld.CompareF( KDefaultName ) || !aNew.CompareF( KDefaultName ))
		return KErrAccessDenied;
	FileName( aOld, from );
	FileName( aNew, to );
	err = fs.Connect();
	if( err != KErrNone )
		return err;
	err = fs.Rename( from, to );
	CloseFs( fs );
	return err;
}

void Profiles::Active( TDes &aName )
{
	RFs   fs;
	RFile f;
	TBuf8<64> b;
	aName = KDefaultName;
	if( fs.Connect() != KErrNone )
		return;
	if( f.Open( fs, KProfActive, EFileRead ) == KErrNone )
	{
		if( f.Read( b ) == KErrNone && b.Length())
		{
			TBuf<64> n;
			n.Copy( b );
			if( Exists( n ))
				aName = n.Left( aName.MaxLength());
		}
		f.Close();
	}
	CloseFs( fs );
}

TInt Profiles::Activate( const TDesC &aName )
{
	TPadProfile p;
	TBuf8<64> name;
	HBufC8 *buf;
	TInt err;
	if( !Load( aName, p ))
		return KErrNotFound;
	buf = Serialize( p );
	if( !buf )
		return KErrNoMemory;
	err = WriteFile( KServerKeys, *buf );
	delete buf;
	name.Copy( aName.Left( name.MaxLength()));
	if( err == KErrNone )
		err = WriteFile( KProfActive, name );
	return err;
}

TPtrC Profiles::InputName( TInt aInput, TBool aSpanish )
{
	if( aInput < 0 || aInput >= EProfInputs )
		return TPtrC();
	return TPtrC( aSpanish ? KInputEs[aInput] : KInputEn[aInput] );
}

TInt Profiles::KeyCount()
{
	return KNumKeys;
}

TPtrC Profiles::KeyName( TInt aKey, TBool aSpanish )
{
	if( aKey < 0 || aKey >= KNumKeys )
		aKey = 0;
	return TPtrC( aSpanish ? KKeys[aKey].iEs : KKeys[aKey].iEn );
}

TInt Profiles::KeyUsage( TInt aKey )
{
	return ( aKey > 0 && aKey < KNumKeys ) ? KKeys[aKey].iUsage : 0;
}

TInt Profiles::KeyOfUsage( TInt aUsage )
{
	TInt i;
	if( !aUsage )
		return 0;
	for( i = 1; i < KNumKeys; i++ )
		if( KKeys[i].iUsage == aUsage )
			return i;
	return 0;
}
