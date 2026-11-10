# Joysticks de PS3 (DualShock 3 y genéricos)

El DualShock 3 no se empareja desde la lista de Bluetooth: tiene grabada la dirección de su "consola" y, al
apretar PS, la llama él. Para que llame al teléfono hay que grabarle la dirección Bluetooth del teléfono por USB,
una vez por joystick. Eso hace `ds3pair.py`.

## Qué hace falta

- El teléfono con *Bluetooth HID* instalado.
- Una computadora con Linux (Python 3 ya viene instalado) o con Windows (hay que instalar Python y cambiarle el
  driver al joystick una vez, ver más abajo).
- El cable USB del joystick (mini USB, el de cargarlo).
- [`ds3pair.py`](https://raw.githubusercontent.com/Dante-Leoncini/bthid-s60/master/tools/ds3pair.py): abrir el
  enlace y guardar la página (*Guardar como...*) con ese nombre.

## 1. La dirección del teléfono

Es un código de 12 números y letras (de la A a la F) que identifica al Bluetooth del teléfono, por ejemplo
`00:1A:2B:3C:4D:5E`. Es la del teléfono, no la del joystick. Se consigue de dos formas:

- **En la app**: *Bluetooth HID > Opciones > Joystick de PS3*. Si dice que no la encuentra, prender el Bluetooth
  del teléfono y volver a entrar.
- **Con el código `*#2820#`** (cualquier Nokia): en la pantalla de inicio, marcarlo con el teclado como si fuera
  un número de teléfono. No hace falta apretar la tecla de llamar: al marcar el último `#` aparece un cuadro con la
  dirección. A veces sale sin los dos puntos, como `001a2b3c4d5e`: es lo mismo, `ds3pair.py` acepta las dos formas.

## 2. Grabarla en el joystick (Linux)

1. Enchufar el joystick a la computadora con el cable USB.
2. Abrir una terminal en la carpeta donde quedó `ds3pair.py` (en la mayoría de los Linux: clic derecho en la carpeta
   > *Abrir en una terminal*).
3. Correr esto, con la dirección del teléfono en lugar de la del ejemplo:

   ```
   sudo python3 ds3pair.py 00:1A:2B:3C:4D:5E
   ```

   Pide la contraseña de la computadora (mientras se escribe no se ve nada; es normal).
4. Tiene que terminar en `grabado bien -> ahora llama a 00:1A:2B:3C:4D:5E` y `Listo: desenchufalo y apreta PS`.

Con varios joysticks enchufados les graba la dirección a todos. Para mirar sin grabar nada (la dirección de cada
joystick y a quién llama): `sudo python3 ds3pair.py`.

### Si dice "esta en modo Xbox 360"

Muchos genéricos, enchufados a una PC, arrancan como un mando de Xbox 360: `lsusb` los muestra como
`045e:028e Microsoft Corp. Xbox360 Controller`, aunque digan Sony. Así no aceptan la dirección.

En ese caso `ds3pair.py` intenta pasarlo a modo PS3. Saca un rato el driver de Xbox de Linux (`xpad`) y pide
desenchufar el joystick y enchufarlo de nuevo, sin apretar nada. Es lo que pasa en una PS3: nadie le habla como una
Xbox y el joystick cambia de modo. Si aparece como joystick de PS3, le graba la dirección enseguida. Al terminar el
driver de Xbox vuelve solo (y si se corta a la mitad, vuelve al reiniciar la computadora).

Si termina en `Sigue en modo Xbox`, a ese joystick no se le puede grabar la dirección desde la PC.

### Si dice "no es de PS3"

Si `lsusb` muestra el joystick con otro número (no `054c:0268` ni `045e:028e`), se puede agregar su
vendor:producto a `PADS` en `ds3pair.py`.

## En Windows

Con el driver que trae Windows no se puede grabar la dirección: el joystick de PS3 no declara ese pedido y Windows
lo rechaza. `ds3pair.py` muestra `direccion ?, llama a ?` y `grabado MAL` (error 87). Hay que cambiarle el driver
al joystick por uno genérico (WinUSB) una vez, con Zadig:

1. Instalar Python 3 de [python.org](https://www.python.org/downloads/). Al instalarlo, marcar
   *Add python.exe to PATH*.
2. Abrir una ventana de comandos (*cmd*) y correr:

   ```
   pip install pyusb libusb-package
   ```

3. Enchufar el joystick. Bajar Zadig de [zadig.akeo.ie](https://zadig.akeo.ie/) y abrirlo. En *Options* marcar
   *List All Devices*, elegir el joystick en la lista ("PLAYSTATION(R)3 Controller", o el nombre del genérico), a
   la derecha de la flecha verde elegir **WinUSB** y apretar *Replace Driver*.
4. En la carpeta de `ds3pair.py`, con la dirección del teléfono:

   ```
   python ds3pair.py 00:1A:2B:3C:4D:5E
   ```

   Tiene que terminar en `grabado bien`.

Con WinUSB el joystick deja de andar como joystick en esa PC (al teléfono no le importa). Para dejarlo como
estaba: *Administrador de dispositivos*, el joystick (ahora en *Dispositivos de bus serie universal*), clic derecho >
*Desinstalar el dispositivo*, marcar la casilla de quitar el controlador, y desenchufarlo y enchufarlo.

Los genéricos que Windows ve como un mando de Xbox 360 no aceptan la dirección ni así: probar en Linux.

## 3. Conectarlo

Desenchufar el USB y apretar PS: el joystick llama al teléfono. El driver (desde la versión 0.2) lo atiende,
le prende la luz de su número (1 a 4) y la app tiene la prueba en *Pruebas > Joysticks*.

## El protocolo (lo que hace el driver)

- Conexión: el joystick abre los canales HID de control (PSM 0x11) y de datos (0x13), sin autenticación.
- Activarlo: SET_REPORT feature `0xF4` por el canal de control: `53 F4 42 03 00 00`.
- Reporte de entrada (canal de datos), `A1 01` + 48 bytes: botones en los bytes 3-5 (5 = PS), sticks en 7-10
  (LX LY RX RY, 0-255), presión de cada botón en 15-26 (arriba, derecha, abajo, izquierda, L2, R2, L1, R1,
  triángulo, círculo, cruz, cuadrado).
- Luces y vibración: SET_REPORT output `52 01` + reporte: byte 3/4 duración/fuerza del motor chico, 5/6 del
  grande, byte 11 las luces (0x02 = luz 1 ... 0x10 = luz 4).
- Grabarle la dirección (por USB, lo que hace `ds3pair.py`): SET_REPORT feature `0xF5` = `01 00` + la dirección
  del teléfono. GET_REPORT `0xF5` devuelve a quién llama (bytes 2-7) y `0xF2` la dirección del joystick (4-9).
