# Bluetooth HID para S60

Mouse y teclado Bluetooth para Symbian S60 3rd/5th edition, pensado como reemplazo del "Bluetooth HID
support" de Hinkka (hidsrv.dll, 2008).

El de Hinkka no manda el estado de los botones del mouse: por cada reporte manda un "click" (apretado y
soltado en el mismo instante), asi que no se puede mantener un boton apretado ni arrastrar. Este se conecta
directo al dispositivo, lee los reportes tal cual vienen y manda apretado al apretar y soltado al soltar.

## Que se instala

- `bthidhostsrv.exe`: servidor sin interfaz. Se queda con la conexion Bluetooth del mouse y el teclado
  (canales HID L2CAP 0x11 y 0x13) y reparte los eventos a las apps conectadas. El formato de los reportes lo
  saca del descriptor HID que el dispositivo publica por SDP (`hidparse.cpp`); sin descriptor usa el protocolo
  boot. Arranca solo con la primera app; si hay dispositivos guardados sigue corriendo y escuchando, para que el
  mouse se reconecte solo al moverlo (la mayoria no contesta si los llama el telefono). Guarda los dispositivos en
  `c:\private\e0d69650\devices.dat` y deja un log corto en `c:\data\bthid_log.txt`.
- `bthidhost.dll`: la misma API que el `hidsrv.dll` de Hinkka (`bthidclient.h`, `bthidevent.h`), con
  `GetHIDClientAPI` en el ordinal 1. Ademas trae `RBtHidManager` (`bthidmanager.h`) para administrar los
  dispositivos.
- `bthidmgr.exe`: la app "Bluetooth HID" del menu. *Opciones > Buscar dispositivo* abre el cuadro de
  Bluetooth del telefono; el elegido queda guardado y se reconecta solo. Hasta un mouse y un teclado a la vez.
  *Opciones > Pruebas* tiene una prueba de mouse (arrastrar un cuadrado con cualquiera de los tres botones, la
  rueda le cambia el tamano) y una de teclado (teclas apretadas, modificadores y lo que se escribe). La app
  esta en ingles y en castellano, segun el idioma del telefono.

Si el driver de Hinkka tiene el mouse conectado, este no lo puede tomar: desconectarlo ahi primero. Los dos
paquetes pueden estar instalados a la vez.

## Usarlo desde una app

Igual que con el de Hinkka, cambiando el nombre de la DLL (y dejando el de Hinkka como respaldo):

```cpp
RLibrary lib;
if( lib.Load( _L( "bthidhost.dll" )) != KErrNone )   // Bluetooth HID: apretado/soltado reales
	User::LeaveIfError( lib.Load( _L( "hidsrv.dll" ))); // Hinkka: clicks sueltos
MHIDSrvClient *client = (MHIDSrvClient *)lib.Lookup( 1 )();
client->Connect();
client->EventReady( &iStatus ); // y en RunL: client->GetEvent( ev )
```

Los eventos son los mismos `THIDEvent`: `EEventRelativeXY` (dx, dy en `iPosition`), `EEventRelativeWheel`,
`EEventButtonDown`/`EEventButtonUp` (`iValue` = `EMouseButtonLeft`/`Right`/`Middle`) y las teclas con el codigo
de uso HID (`EEventHIDKeyDown`/`Up`, `iScanCode`).

## Compilar

Con el SDK de Symbian^3 y RVCT 2.2 (como xash3d-fwgs-symbian): `sbs -b group/bld.inf -c armv5_urel`, y
`sis/bthid.pkg` para el paquete. Solo usa APIs que ya estan en S60 3rd (comprobado contra el SDK de FP2).

UIDs: servidor `0xE0D69650`, DLL `0xE0D69651`, app y paquete `0xE0D69652`.
