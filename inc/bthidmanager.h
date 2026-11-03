/*
bthidmanager.h - administrar los dispositivos del servidor bthidhost (lo usa la app "Bluetooth HID")

Los juegos no necesitan esto: cargan bthidhost.dll y usan la API de siempre (bthidclient.h, GetHIDClientAPI).
*/
#ifndef BTHIDMANAGER_H
#define BTHIDMANAGER_H

#include <e32base.h>

enum TBtHidDeviceType   { EBtHidUnknown = 0, EBtHidMouse = 1, EBtHidKeyboard = 2, EBtHidGamepad = 3 };
// EBtHidWaiting: el dispositivo se reconecta solo (HIDReconnectInitiate); el telefono no lo llama, lo espera.
// EBtHidStopped: desconectado a mano (desde la app): no se lo llama hasta que se pida conectarlo o llame el
enum TBtHidDeviceState  { EBtHidDisconnected = 0, EBtHidConnecting = 1, EBtHidConnected = 2, EBtHidWaiting = 3,
                          EBtHidStopped = 4 };
// lo que se le puede pedir a un dispositivo (RBtHidManager::DeviceCommand)
enum TBtHidDeviceCommand { EBtHidCmdDisconnect = 1, EBtHidCmdConnect = 2, EBtHidCmdRemove = 3,
                           // un joystick al lugar N (1-4): EBtHidCmdMoveToPad + N (si estaba ocupado, se intercambian)
                           EBtHidCmdMoveToPad = 0x10 };

struct TBtHidDeviceInfo
{
	TUint8    iAddr[6];
	TInt      iType;   // TBtHidDeviceType
	TInt      iState;  // TBtHidDeviceState
	TInt      iError;  // ultimo error de conexion (0 = ninguno)
	TBuf8<32> iName;
	TInt      iPlayer; // joystick: su lugar, el numero de jugador 1-4 (0 = todavia no tiene)
	TInt      iBattery;  // joystick: la bateria en % (-1 = no se sabe)
	TInt      iCharging; // joystick: enchufado (cargando)
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
enum TBtHidPadKind { EBtHidPadNone = 0, EBtHidPadUnknown = 1, EBtHidPadPs3 = 3, EBtHidPadPs4 = 4, EBtHidPadWii = 5 };
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

/* Wiimote (Nintendo RVL-CNT-01 y el Wii Remote Plus -TR): ocupa un lugar de joystick como los demas (PadState da
   sus botones como los de un joystick: A cruz, B R2, 1 cuadrado, 2 circulo, - select, + start, Home PS). Lo propio
   del Wiimote lo da WiiState: el acelerometro, el giroscopio (MotionPlus) y la camara infrarroja.

   Puntero: con 4 LEDs infrarrojos, uno en cada esquina de la pantalla, la camara los ve y se calcula a que punto de
   la pantalla apunta (la transformacion de perspectiva de los 4 puntos a las esquinas): no hace falta calibrar y
   vale desde cualquier lugar y con el control girado (hasta 75 grados). iAimX e iAimY van de 0 a 1: (0, 0) la
   esquina de arriba a la izquierda, (1, 1) la de abajo a la derecha; fuera de la pantalla dan menos de 0 o mas de 1.
   Si se pierden 1 o 2 LEDs (apuntando cerca del borde, la camara ve unos 33 x 23 grados) se siguen con el
   movimiento de los otros: iAim dice cuantos LEDs ve de verdad (4 = exacto). Cada vez que se aprieta B (el gatillo)
   iShots suma uno y iShotX/iShotY guardan donde apuntaba en ese reporte: un juego que pide el estado de vez en
   cuando no pierde disparos. */
enum TBtHidWiiButton
{
	EBtHidWiiUp, EBtHidWiiDown, EBtHidWiiLeft, EBtHidWiiRight, EBtHidWiiA, EBtHidWiiB, EBtHidWiiOne, EBtHidWiiTwo,
	EBtHidWiiMinus, EBtHidWiiPlus, EBtHidWiiHome,
	EBtHidWiiButtons
};

struct TBtHidWiiState
{
	TInt    iConnected;    // es un Wiimote y esta conectado
	TUint32 iButtons;      // apretados: bit ( 1 << TBtHidWiiButton )
	TInt    iAccel[3];     // X Y Z como vienen (10 bits, ~512 sin aceleracion)
	TReal32 iAccelG[3];    // en g, con la calibracion del control (quieto y boca arriba: Z = 1)
	TInt    iMotionPlus;   // tiene giroscopio (MotionPlus o Wii Remote Plus) y esta mandando
	TInt    iGyro[3];      // yaw roll pitch como vienen (14 bits, ~8000 quieto)
	TReal32 iGyroDps[3];   // en grados por segundo (el cero se toma solo cada vez que se lo deja quieto)
	TInt    iIrCount;      // cuantos puntos infrarrojos ve la camara (0-4)
	TInt    iIrX[4];       // donde: X 0-1023, Y 0-767, con Y para arriba (-1 = ese no lo ve)
	TInt    iIrY[4];
	TInt    iIrCorner[4];  // que punto (0-3) es cada esquina: arriba izq, arriba der, abajo der, abajo izq (-1 = ?)
	TInt    iAim;          // LEDs de las esquinas que ve de verdad (2-4); 0 = no se sabe a donde apunta
	TReal32 iAimX, iAimY;  // a donde apunta: 0-1 de izquierda a derecha y de arriba a abajo
	TInt    iShots;        // veces que se apreto B desde que se conecto
	TReal32 iShotX, iShotY; // a donde apuntaba la ultima vez que se apreto B
	TInt    iShotAim;      // y cuantos LEDs veia ahi (0 = no se sabia a donde apuntaba)
	TInt    iBattery;      // en % (-1 = no se sabe)
	TInt    iReports;      // reportes recibidos desde que se conecto
};
typedef TPckgBuf<TBtHidWiiState> TBtHidWiiStatePckg;

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
	// buscar un joystick para el lugar aSlot (1-4): ese va a ser su numero de jugador
	IMPORT_C void SearchPad( TRequestStatus &aStatus, TInt aSlot );
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
	// desconectar, conectar o quitar el dispositivo con esa direccion (6 bytes): TBtHidDeviceCommand
	IMPORT_C TInt DeviceCommand( const TDesC8 &aAddr, TInt aCommand );
	// el joystick aPad (0-3), si es un Wiimote: acelerometro, giroscopio, camara IR y a donde apunta
	IMPORT_C TInt WiiState( TInt aPad, TBtHidWiiStatePckg &aState );
	// buscar un Wiimote (apretando 1+2) sin el cuadro del telefono, para el lugar aSlot (1-4); KErrNotFound si en 40 s
	// no aparecio
	IMPORT_C void SearchWii( TRequestStatus &aStatus, TInt aSlot );
};

#endif // BTHIDMANAGER_H
