# Bluetooth HID para S60

Mouse y teclado Bluetooth para Symbian S60 3rd/5th edition, pensado como reemplazo del "Bluetooth HID
support" de Hinkka (hidsrv.dll, 2008).

El de Hinkka no manda el estado de los botones del mouse: por cada reporte manda un "click" (apretado y
soltado en el mismo instante), asi que no se puede mantener un boton apretado ni arrastrar. Este se conecta
directo al dispositivo, lee los reportes tal cual vienen y manda apretado al apretar y soltado al soltar.

## Instalar

Instalar `sis/bthid_0.13.sisx` en el telefono (en la memoria del telefono, C:) y reiniciarlo. La app **Bluetooth
HID** muestra en su pantalla principal los lugares Mouse, Teclado y Joystick 1 a 4: arriba y abajo eligen uno y el
centro abre sus opciones (agregar, conectar, desconectar, pasar a otro numero, quitar). En las pantallas tactiles
(N8, 5800, ...) se toca el lugar y se arrastra la lista con el dedo. Lo nuevo de cada version esta en
[CHANGELOG.md](CHANGELOG.md).

El paquete esta firmado con un certificado propio y el servidor pide capacidades de sistema (SwEvent,
WriteDeviceData...): el telefono tiene que aceptarlo. En Symbian^3 / Anna / Belle (N8) hace falta el parche
*Install Server* de ROMPatcher+ activo al instalar. Si ya hay una version anterior firmada distinto y el telefono
dice "no es una actualizacion valida", desinstalar la vieja primero.

**Si el telefono pide un codigo** al conectar un mouse, un teclado o un joystick: casi siempre es `0000` (cuatro
ceros). Probar con ese.

## Conectar un joystick de PS3 (DualShock 3)

Cada joystick ocupa un lugar (Joystick 1 a 4) en la pantalla principal de la app.

El de PS3 no se empareja desde la lista: tiene guardada la direccion de su "consola" y, al apretar PS, la llama el.
Hay que grabarle la del telefono por el cable USB, una vez, desde una computadora, con
[`ds3pair.py`](https://raw.githubusercontent.com/Dante-Leoncini/bthid-s60/master/tools/ds3pair.py) (abrir el
enlace y guardarlo con *Guardar como...*).

1. **La direccion del telefono:** en la app, *Opciones > Joystick de PS3*; o en cualquier Nokia, marcando
   `*#2820#` en la pantalla de inicio, como un numero (no hace falta apretar llamar). Es algo como
   `00:1A:2B:3C:4D:5E`, o `001a2b3c4d5e` sin los dos puntos: `ds3pair.py` acepta las dos formas.
2. **Grabarla en el joystick,** enchufado a la computadora por USB:
   - **Linux** (Python ya viene instalado): en una terminal, en la carpeta de `ds3pair.py`:

     ```
     sudo python3 ds3pair.py 00:1A:2B:3C:4D:5E
     ```

   - **Windows:** con el driver que trae Windows no se puede (el joystick no declara ese pedido y Windows lo
     rechaza: sale `direccion ?` y `grabado MAL`). Hay que cambiarle el driver una vez:
     1. Instalar Python 3 de [python.org](https://www.python.org/downloads/), marcando *Add python.exe to PATH*.
     2. En una ventana de comandos (*cmd*): `pip install pyusb libusb-package`
     3. Con el joystick enchufado, abrir [Zadig](https://zadig.akeo.ie/): en *Options* marcar *List All Devices*,
        elegir el joystick ("PLAYSTATION(R)3 Controller" o el nombre del generico), a la derecha de la flecha verde
        elegir **WinUSB** y apretar *Replace Driver*.
     4. En la carpeta de `ds3pair.py`: `python ds3pair.py 00:1A:2B:3C:4D:5E`

     Con WinUSB el joystick deja de andar como joystick en esa PC (al telefono no le importa). Como volverlo atras
     esta en [tools/PS3.md](tools/PS3.md).

   Tiene que terminar en `grabado bien -> ahora llama a ...`.
3. **Conectarlo:** desenchufar el cable y apretar PS. El joystick llama al telefono y se conecta solo.

**Si dice "esta en modo Xbox 360":** muchos genericos, enchufados a una PC, se presentan como un mando de Xbox 360
(`045e:028e`) y asi no aceptan la direccion. En Linux, `ds3pair.py` intenta pasarlo a modo PS3 (pide desenchufarlo y
enchufarlo). Si sigue en modo Xbox, a ese joystick no se le puede grabar la direccion.

**Si no anda:** cada vez que corre, `ds3pair.py` deja un archivo `ds3pair.log` en su carpeta (al final dice
donde). Tiene el detalle de lo que paso: que le pidio al joystick y que contesto, que hay enchufado por USB y con que
driver, y las versiones de Windows o Linux y de Python. Mandalo a quien te este ayudando, o adjuntalo en un
[issue](https://github.com/Dante-Leoncini/bthid-s60/issues). Tiene la direccion del telefono y la del joystick.

Mas detalle, los problemas comunes y el protocolo: [tools/PS3.md](tools/PS3.md).

## Joystick de PS4 (DualShock 4): sin confirmar

**No esta confirmado que ande.** El driver tiene soporte para el de PS4, pero no lo pudimos comprobar nosotros. Un
usuario conto que le funciono. Si lo probas, avisa si anduvo o no, y con que telefono.

Para probarlo:

1. En la pantalla principal, elegir un lugar de joystick vacio, apretar el centro y *Agregar un joystick aca*.
2. Poner el joystick a emparejar: el original con **Share + PS** hasta que la luz parpadee de a dos veces.
3. Elegirlo en la lista del telefono (suele aparecer como "Wireless Controller"). Si pide un codigo, casi siempre es
   `0000`.

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

Con *Opciones > Joystick como teclado: si*, los botones de los joysticks (hasta 4) mandan teclas.
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
