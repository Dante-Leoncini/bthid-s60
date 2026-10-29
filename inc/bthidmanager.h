/*
bthidmanager.h - administrar los dispositivos del servidor bthidhost (lo usa la app "Bluetooth HID")

Los juegos no necesitan esto: cargan bthidhost.dll y usan la API de siempre (bthidclient.h, GetHIDClientAPI).
*/
#ifndef BTHIDMANAGER_H
#define BTHIDMANAGER_H

#include <e32base.h>

enum TBtHidDeviceType   { EBtHidUnknown = 0, EBtHidMouse = 1, EBtHidKeyboard = 2 };
enum TBtHidDeviceState  { EBtHidDisconnected = 0, EBtHidConnecting = 1, EBtHidConnected = 2 };

struct TBtHidDeviceInfo
{
	TUint8    iAddr[6];
	TInt      iType;   // TBtHidDeviceType
	TInt      iState;  // TBtHidDeviceState
	TInt      iError;  // ultimo error de conexion (0 = ninguno)
	TBuf8<32> iName;
};

struct TBtHidStatus
{
	enum { EMaxDevices = 2 };
	TInt             iCount;
	TInt             iClients; // apps conectadas que reciben eventos
	TBtHidDeviceInfo iDevice[EMaxDevices];
};
typedef TPckgBuf<TBtHidStatus> TBtHidStatusPckg;

class RBtHidManager : public RSessionBase
{
public:
	// arranca el servidor si no esta corriendo
	IMPORT_C TInt Connect();
	// abre el cuadro de busqueda de Bluetooth del telefono; termina cuando se elige uno (o se cancela) y el
	// elegido queda guardado y conectandose
	IMPORT_C void Search( TRequestStatus &aStatus );
	IMPORT_C void CancelSearch();
	IMPORT_C TInt ForgetAll();
	IMPORT_C TInt Status( TBtHidStatusPckg &aStatus );
};

#endif // BTHIDMANAGER_H
