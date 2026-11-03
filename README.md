# Bluetooth HID para S60

Mouse y teclado Bluetooth para Symbian S60 3rd/5th edition, pensado como reemplazo del "Bluetooth HID
support" de Hinkka (hidsrv.dll, 2008).

El de Hinkka no manda el estado de los botones del mouse: por cada reporte manda un "click" (apretado y
soltado en el mismo instante), asi que no se puede mantener un boton apretado ni arrastrar. Este se conecta
directo al dispositivo, lee los reportes tal cual vienen y manda apretado al apretar y soltado al soltar.

## Instalar

Instalar `sis/bthid_0.10.sisx` en el telefono (en la memoria del telefono, C:) y reiniciarlo. La app **Bluetooth
HID** muestra en su pantalla principal los lugares Mouse, Teclado y Joystick 1 a 4: arriba y abajo eligen uno y el
centro abre sus opciones (agregar, conectar, desconectar, pasar a otro numero, quitar). Lo nuevo de cada version esta
en [CHANGELOG.md](CHANGELOG.md).

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
  rueda le cambia el tamano) y una de teclado (teclas apretadas, modificadores y lo que se escribe).
  *Opciones > Joystick de PS3* muestra la direccion Bluetooth del telefono, la que se le graba por USB a un
  joystick de PS3 con `tools/ds3pair.py` (instrucciones en `tools/PS3.md`). La app esta en ingles y en castellano,
  segun el idioma del telefono.

- `hidsrv.dll`: el mismo cliente con el nombre y el UID (`0xA00054C3`) del de Hinkka, para las apps viejas que lo
  cargan (ScummVM, Quake 3...): andan sin cambios y con los botones arreglados.

El paquete usa el UID del "Bluetooth HID support" de Hinkka (`0xA00054C4`) con una version mas alta: se instala
como su actualizacion y lo reemplaza.

## El joystick como teclado

Con *Opciones > Joystick como teclado: si*, los botones de los joysticks (DualShock 3 y 4, hasta 4) mandan teclas.
Llegan a TODAS las apps y al menu del telefono, como si fueran las del teclado del telefono (el servidor las inyecta
al sistema con `RWsSession::SimulateRawEvent`, por eso pide la capacidad `SwEvent`). A la app que esta adelante, si
lee los joysticks directo (`RBtHidManager::PadState`), no se le mandan: asi un juego que usa el joystick no recibe todo
dos veces.

Las teclas por defecto (el perfil *Predeterminado*):

| Boton | Joystick 1 | Joystick 2 | Joystick 3 | Joystick 4 |
|---|---|---|---|---|
| Cruceta y stick izquierdo (arriba, abajo, izquierda, derecha) | flechas del telefono | 8 2 4 6 | I K J L | Inicio Fin Suprimir AvPag |
| Cruz (X) | **OK** (el centro) | A | N | A |
| Cuadrado | 2 | B | O | B |
| Circulo | 3 | C | P | C |
| Triangulo | 4 | D | Q | D |
| Start | 0 | E | R | E |
| R1 | tecla de abajo derecha | F | S | F |
| L1 | tecla de abajo izquierda | G | T | G |
| R2 | 5 | H | U | H |
| L2 | 6 | I | V | I |
| Select | 7 | J | W | J |
| PS | tecla de menu (cambiar de app) | K | X | K |
| R3 | 8 | L | Y | L |
| L3 | 9 | M | Z | M |

Con el joystick 1 se maneja el telefono entero: el menu, las listas, las teclas de abajo de la pantalla y los
numeros. Si en el telefono quedo el archivo de teclas o el *Predeterminado* de una version anterior sin tocar, se
reemplazan solos por estos; los perfiles editados no se tocan.

Los perfiles se editan en la app (*Opciones > Perfiles del joystick*): cada boton de cada joystick con una tecla de
teclado (letras, numeros, flechas, F1..F12...) o del telefono (OK, las de abajo de la pantalla, menu, llamar, colgar,
C, * y #). El que esta en uso queda en `c:\data\bthid_teclas.txt` (renglones `joystick boton tecla`).

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

## Wiimote

El control de Wii (Nintendo RVL-CNT-01 y el Wii Remote Plus, RVL-CNT-01-TR) se agrega en un lugar de joystick vacio
con *Agregar un Wiimote aca* y apretando **1+2** en el Wiimote (o el boton rojo de adentro de la tapa de las pilas).
Mientras parpadean las luces (tantas como bateria tiene) esta esperando; el cuadro de busqueda del telefono no lo
encuentra, por eso lo busca el servidor: hasta 40 s, sin pedir los nombres (con nombres la busqueda se trababa) y
alternando la busqueda general y la limitada. Si el Bluetooth esta apagado pregunta si prenderlo. Todo lo que ve va
al log. Despues queda guardado: el telefono lo llama cada pocos segundos y para conectarlo otra vez basta apretar 1+2. Los Wiimote originales se conectan sin codigo; los demas piden emparejar con un codigo binario
(la direccion del Wiimote al reves con 1+2, la del telefono al reves con el boton rojo) que el servidor deja en el
registro Bluetooth del telefono para que se conteste solo. Se prueban las tres formas y la que anda queda.

En *Pruebas > Probar joysticks*, con un Wiimote, arriba y abajo cambian entre sus dos pruebas:

1. **Botones y movimiento:** los 11 botones, el acelerometro en g (calibrado con los datos que trae el control) y,
   si tiene MotionPlus (o es un Wii Remote Plus), el giroscopio en grados por segundo. El cero del giroscopio se toma
   solo cada vez que el control queda quieto medio segundo.
2. **Infrarrojo:** lo que ve la camara (hasta 4 puntos, cada uno con su numero de esquina) y la pantalla con la mira
   y los ultimos disparos (B).

**Punteria con 4 LEDs:** un LED infrarrojo (940 nm) en cada esquina de la pantalla. Con los 4 a la vista se calcula
la transformacion de perspectiva de la camara a la pantalla y se sabe a que punto apunta, sin calibrar, desde
cualquier lugar y con el control girado hasta 75 grados. La camara ve unos 33 x 23 grados: para que entren las 4
esquinas al apuntar a un borde hay que estar lejos (unas 3 veces el ancho de la pantalla). Si se pierden 1 o 2 LEDs
se siguen con el movimiento de los otros (menos exacto).

Desde una app, con `RBtHidManager::WiiState( pad, estado )` (`bthidmanager.h`):

```cpp
TBtHidWiiStatePckg w;
mgr.WiiState( 0, w );                 // el joystick 1
if( w().iAim )                        // ve los LEDs: a donde apunta, 0..1 (0,0 arriba a la izquierda)
	Mira( w().iAimX * ancho, w().iAimY * alto );
if( w().iShots != iUltimo )           // se apreto B: donde apuntaba en ese momento
{
	iUltimo = w().iShots;
	Disparo( w().iShotX * ancho, w().iShotY * alto );
}
```

`iAccelG` (g), `iGyroDps` (grados/s, con `iMotionPlus`) y los puntos de la camara (`iIrX`, `iIrY`) tambien estan
ahi. `PadState` lo da como un joystick mas: A = cruz, B = R2, 1 = cuadrado, 2 = circulo, - = Select, + = Start,
Home = PS.

En el modo teclado el Wiimote tiene sus teclas: Home = menu, - y + = las teclas de abajo de la pantalla, A = OK, B,
1 y 2 = los numeros 1, 2 y 3, la cruceta = las flechas. Acostado (de costado e inclinado hacia uno) la cruceta gira:
derecha es arriba. Con la pantalla en horizontal las flechas de todos los joysticks se compensan para que arriba
siga siendo arriba.

## Compilar

Con el SDK de Symbian^3 y RVCT 2.2 (como xash3d-fwgs-symbian): `sbs -b group/bld.inf -c armv5_urel`, y
`sis/bthid.pkg` para el paquete. Solo usa APIs que ya estan en S60 3rd (comprobado contra el SDK de FP2).

UIDs: servidor `0xE0D69650`, DLL `0xE0D69651`, app `0xE0D69652`, `hidsrv.dll` `0xA00054C3` y paquete `0xA00054C4`
(los dos ultimos, los del de Hinkka).

## Licencia

MIT (ver `LICENSE`). Los headers `inc/bthidclient.h` e `inc/bthidevent.h` son la API publica del de Hinkka,
incluidos sin cambios para que las apps que ya lo usan sigan andando; esos dos no entran en la licencia.
