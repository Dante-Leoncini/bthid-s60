/*
bthidmanager.h - administrar los dispositivos del servidor bthidhost (lo usa la app "Bluetooth HID")

Los juegos no necesitan esto: cargan bthidhost.dll y usan la API de siempre (bthidclient.h, GetHIDClientAPI).
*/
#ifndef BTHIDMANAGER_H
#define BTHIDMANAGER_H

#include <e32base.h>

enum TBtHidDeviceType   { EBtHidUnknown = 0, EBtHidMouse = 1, EBtHidKeyboard = 2, EBtHidGamepad = 3 };
// EBtHidWaiting: el dispositivo se reconecta solo (HIDReconnectInitiate); el telefono no lo llama, lo espera
enum TBtHidDeviceState  { EBtHidDisconnected = 0, EBtHidConnecting = 1, EBtHidConnected = 2, EBtHidWaiting = 3 };

struct TBtHidDeviceInfo
{
	TUint8    iAddr[6];
	TInt      iType;   // TBtHidDeviceType
	TInt      iState;  // TBtHidDeviceState
	TInt      iError;  // ultimo error de conexion (0 = ninguno)
	TBuf8<32> iName;
	TInt      iPlayer; // joystick: numero de jugador 1-4, por orden de conexion (0 = no se conecto todavia)
};

struct TBtHidStatus
{
	enum { EMaxDevices = 6 }; // el mouse, el teclado y hasta 4 joysticks
	TInt             iCount;
	TInt             iClients; // apps conectadas que reciben eventos
	TInt             iPadKeys; // los joysticks tambien mandan teclas (modo teclado, para las apps viejas)
	TBtHidDeviceInfo iDevice[EMaxDevices];
};
typedef TPckgBuf<TBtHidStatus> TBtHidStatusPckg;

/* joysticks (DualShock 3 y 4 y los genericos que hablan como ellos): hasta 4, numerados por orden */
enum { EBtHidMaxPads = 4 };
// las teclas del TELEFONO en el modo teclado (no estan en la pagina de teclado HID, que llega hasta 0xE7): el
// servidor las manda como las del teclado del telefono (ok, las de abajo de la pantalla, la de menu...)
enum TBtHidPhoneKey
	{
	EBtHidPhoneOk = 0xF0, EBtHidPhoneSoftLeft, EBtHidPhoneSoftRight, EBtHidPhoneMenu, EBtHidPhoneYes, EBtHidPhoneNo,
	EBtHidPhoneClear, EBtHidPhoneStar, EBtHidPhoneHash
	};
enum TBtHidPadKind { EBtHidPadNone = 0, EBtHidPadUnknown = 1, EBtHidPadPs3 = 3, EBtHidPadPs4 = 4 };
enum TBtHidPadButton
{
	EBtHidPadUp, EBtHidPadRight, EBtHidPadDown, EBtHidPadLeft,
	EBtHidPadCross, EBtHidPadCircle, EBtHidPadSquare, EBtHidPadTriangle,
	EBtHidPadL1, EBtHidPadR1, EBtHidPadL2, EBtHidPadR2,
	EBtHidPadSelect, EBtHidPadStart, EBtHidPadL3, EBtHidPadR3, EBtHidPadPS,
	EBtHidPadButtons
};

struct TBtHidPadState
{
	TInt    iKind;                       // TBtHidPadKind (EBtHidPadNone = no hay joystick con ese numero)
	TInt    iState;                      // TBtHidDeviceState
	TUint32 iButtons;                    // apretados: bit ( 1 << TBtHidPadButton )
	TUint8  iAxis[4];                    // LX LY RX RY, 0-255 (128 al centro)
	TUint8  iPressure[EBtHidPadButtons]; // cuanto se aprieta cada boton (0-255); el DS4 solo lo tiene en L2/R2
	TInt    iRumble;                     // vibrando
	TInt    iReports;                    // reportes recibidos desde que se conecto
	TUint8  iAddr[6];
};
typedef TPckgBuf<TBtHidPadState> TBtHidPadStatePckg;

class RBtHidManager : public RSessionBase
{
public:
	// arranca el servidor si no esta corriendo
	IMPORT_C TInt Connect();
	// abre el cuadro de busqueda de Bluetooth del telefono; termina cuando se elige uno (o se cancela) y el
	// elegido queda guardado y conectandose
	IMPORT_C void Search( TRequestStatus &aStatus );
	// lo mismo, eligiendo que es (EBtHidMouse / EBtHidKeyboard): reemplaza al que habia de ese tipo
	IMPORT_C void Search( TRequestStatus &aStatus, TInt aType );
	IMPORT_C void CancelSearch();
	IMPORT_C TInt ForgetAll();
	// olvidar el mouse o el teclado
	IMPORT_C TInt Forget( TInt aType );
	IMPORT_C TInt Status( TBtHidStatusPckg &aStatus );
	// el joystick aPad (0-3): botones, sticks, presion de cada boton
	IMPORT_C TInt PadState( TInt aPad, TBtHidPadStatePckg &aState );
	// prender o apagar la vibracion del joystick aPad
	IMPORT_C TInt SetPadRumble( TInt aPad, TBool aOn );
	// modo teclado: cada boton de los joysticks manda una tecla (c:\data\bthid_teclas.txt) a las apps que usan la
	// API del hidsrv (bthidclient.h); las que piden el estado del joystick (PadState) no reciben esas teclas
	IMPORT_C TInt SetPadKeys( TBool aOn );
};

#endif // BTHIDMANAGER_H
