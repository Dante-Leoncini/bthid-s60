/*
bthid_host.h - conexion directa con mouse y teclado Bluetooth (corre dentro de bthidhostsrv.exe)

Hablamos con el dispositivo por L2CAP (canales HID de control 0x11 y de datos 0x13), lo pasamos al protocolo
boot y leemos los reportes tal cual vienen: la mascara real de botones del mouse y las teclas apretadas del
teclado. (El hidsrv de Hinkka, en cambio, convierte cada reporte en un "click" y no se puede mantener apretado.)
*/
#ifndef BTHID_HOST_H
#define BTHID_HOST_H

#include <e32base.h>
#include <es_sock.h>
#include <f32file.h>
#include <bttypes.h>
#include <btextnotifiers.h>
#include "bthidmanager.h"

/* RFs::Close no existe en S60 3rd (ahi es el de RHandleBase): el SDK de Symbian^3 lo declara en RFs y si se
   importa ese el exe no carga en el telefono */
inline void BtHidCloseFs( RFs &aFs ) { static_cast<RHandleBase &>( aFs ).Close(); }

class MBtHidObserver
{
public:
	// movimiento y botones del mouse. aButtons = mascara de los apretados (bit 0 izquierdo, 1 derecho, 2 medio)
	virtual void BtHidMouse( TInt aDx, TInt aDy, TInt aWheel, TInt aButtons ) = 0;
	// tecla apretada/soltada: codigo de uso HID de la pagina de teclado (4 = 'a'...), modificadores 224..231
	virtual void BtHidKey( TInt aUsage, TBool aDown ) = 0;
	// termino el cuadro de busqueda (KErrCancel si no se eligio nada)
	virtual void BtHidSearchDone( TInt aError ) = 0;
	// avisos para el log
	virtual void BtHidLog( const char *aMsg ) = 0;
};

class CBtHidDevice;
class CBtHidListener;

class CBtHidHost : public CActive
{
public:
	enum { EMaxDevices = TBtHidStatus::EMaxDevices };

	// aSaveFile: donde se guardan los dispositivos elegidos (se reconectan solos al arrancar)
	static CBtHidHost *NewL( MBtHidObserver &aObserver, const TDesC &aSaveFile );
	~CBtHidHost();

	TInt DeviceCount() const;
	// abre el cuadro de busqueda de dispositivos Bluetooth del telefono; el elegido se conecta y se guarda
	void SelectDevice();
	void CancelSelect();
	void ForgetAll();
	void GetStatus( TBtHidStatus &aStatus ) const;

	// conexion entrante (CBtHidListener) en el canal aPsm: la adopta el dispositivo elegido con esa direccion
	void Incoming( TUint aPsm, RSocket &aSock, const TBTDevAddr &aAddr );

	// guardar los dispositivos (y sus descriptores HID) para la proxima vez
	void Save();

	RSocketServ &SocketServ() { return iSs; }
	MBtHidObserver &Observer() { return iObserver; }

private:
	CBtHidHost( MBtHidObserver &aObserver );
	void ConstructL( const TDesC &aSaveFile );
	void RunL();
	void DoCancel();
	void AddDevice( const TBTDevAddr &aAddr, TInt aType, const TDesC8 &aName, const TDesC8 &aDesc );
	void Load();

	MBtHidObserver               &iObserver;
	RSocketServ                   iSs;
	RNotifier                     iNotifier;
	TBool                         iNotifierOpen;
	TBTDeviceSelectionParamsPckg  iSel;
	TBTDeviceResponseParamsPckg   iResp;
	CBtHidDevice                 *iDev[EMaxDevices];
	CBtHidListener               *iListenCtrl, *iListenIntr;
	TFileName                     iFile;
};

#endif // BTHID_HOST_H
