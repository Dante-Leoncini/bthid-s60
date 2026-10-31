/*
profiles.h - perfiles del joystick como teclado (la app "Bluetooth HID")

Un perfil dice que tecla manda cada boton de cada joystick (1 a 4) cuando el modo teclado esta prendido. Se
guardan en c:\data\bthid_perfiles\<nombre>.txt con el formato que lee el servidor ("joystick boton tecla"); el
activo se copia a c:\data\bthid_teclas.txt, que es el que usa el servidor. "Predeterminado" siempre esta y no se
borra.
*/
#ifndef PROFILES_H
#define PROFILES_H

#include <e32base.h>
#include <badesca.h>
#include "bthidmanager.h"

enum { EProfInputs = EBtHidPadButtons + 4 }; // los botones y el stick izquierdo como 4 direcciones

struct TPadProfile
{
	TBuf<32> iName;
	TUint8   iMap[EBtHidMaxPads][EProfInputs]; // uso HID de la tecla (0 = sin asignar)
};

class Profiles
{
public:
	static const TDesC &DefaultName();
	// los nombres, el predeterminado primero (lo crea si no esta)
	static void ListL( CDesCArray &aNames );
	static TBool Load( const TDesC &aName, TPadProfile &aProf );
	static TInt Save( const TPadProfile &aProf );
	static TInt Delete( const TDesC &aName );
	static TInt Rename( const TDesC &aOld, const TDesC &aNew );
	static TBool Exists( const TDesC &aName );
	static void Active( TDes &aName );
	// lo deja activo: lo copia a c:\data\bthid_teclas.txt (despues hay que avisarle al servidor)
	static TInt Activate( const TDesC &aName );
	static void Default( TPadProfile &aProf );
	static void Empty( TPadProfile &aProf, const TDesC &aName );

	// para mostrar: el nombre de cada boton y de cada tecla de la lista
	static TPtrC InputName( TInt aInput, TBool aSpanish );
	static TInt KeyCount();
	static TPtrC KeyName( TInt aKey, TBool aSpanish ); // aKey = lugar en la lista (0 = sin asignar)
	static TInt KeyUsage( TInt aKey );
	static TInt KeyOfUsage( TInt aUsage );
};

#endif // PROFILES_H
