/*
bthidproto.h - protocolo entre bthidhost.dll y bthidhostsrv.exe (interno)
*/
#ifndef BTHIDPROTO_H
#define BTHIDPROTO_H

#include <e32base.h>

_LIT( KBtHidServerName, "BtHidHostSrv" );
_LIT( KBtHidServerExe,  "bthidhostsrv.exe" );

const TInt KBtHidVerMajor = 1;
const TInt KBtHidVerMinor = 0;
const TInt KBtHidVerBuild = 0;

enum TBtHidOpcode
{
	EBtHidWaitEvent,    // async: arg0 = TPckgBuf<THIDEvent>, se completa con el proximo evento
	EBtHidCancelWait,
	EBtHidSearch,       // async: cuadro de busqueda, arg0 = tipo (0 = el que diga el dispositivo)
	EBtHidCancelSearch,
	EBtHidForgetAll,
	EBtHidStatus,       // arg0 = TBtHidStatusPckg
	EBtHidForget,       // arg0 = tipo
	EBtHidPadState,     // arg0 = joystick (0-3), arg1 = TBtHidPadStatePckg
	EBtHidPadRumble     // arg0 = joystick, arg1 = prendida
};

// arranca bthidhostsrv.exe y espera a que este listo (si ya corre, devuelve KErrNone igual)
TInt BtHidStartServer();

inline TVersion BtHidVersion() { return TVersion( KBtHidVerMajor, KBtHidVerMinor, KBtHidVerBuild ); }

#endif // BTHIDPROTO_H
