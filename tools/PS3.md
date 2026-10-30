# Joysticks de PS3 (DualShock 3 y genéricos)

El DualShock 3 no se empareja desde la lista de Bluetooth: tiene grabada la dirección de su "consola" y, al
apretar PS, la llama él. Para que llame al teléfono hay que grabarle la dirección Bluetooth del teléfono por USB,
una vez por joystick. Eso hace `ds3pair.py`.

## Qué hace falta

- El teléfono con *Bluetooth HID* instalado.
- Una computadora con Linux (Python 3 ya viene instalado). En Windows anda solo con el DualShock 3 original
  (ver más abajo).
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

Si termina en `Sigue en modo Xbox`, ese joystick no se deja grabar desde la PC. Probar la búsqueda de Bluetooth
(más abajo): hubo genéricos que nunca salieron del modo Xbox por USB pero se dejaban encontrar por Bluetooth con
PS + Start.

### Si dice "no es de PS3"

Si `lsusb` muestra el joystick con otro número (no `054c:0268` ni `045e:028e`), se puede agregar su
vendor:producto a `PADS` en `ds3pair.py`.

## En Windows

Anda con el DualShock 3 original (aparece como "PLAYSTATION(R)3 Controller", `054C:0268`). Hace falta Python 3
([python.org](https://www.python.org/downloads/)), y se corre igual pero sin `sudo`:

```
python ds3pair.py 00:1A:2B:3C:4D:5E
```

Los genéricos en Windows casi siempre arrancan en modo Xbox 360 y ahí no aceptan la dirección: usar Linux.

## 3. Conectarlo

Desenchufar el USB y apretar PS: el joystick llama al teléfono. El driver (desde la versión 0.2) lo atiende,
le prende la luz de su número (1 a 4) y la app tiene la prueba en *Pruebas > Joysticks*.

## Si el genérico no acepta la dirección: buscarlo por Bluetooth

Muchos genéricos también se dejan encontrar en la búsqueda de Bluetooth, como un mouse, sin grabar nada por USB:

1. Desenchufar el cable USB.
2. Mantener PS + Start hasta que las luces titilen rápido (si no, probar PS + Select, o PS solo).
3. Enseguida, mientras titila: *Bluetooth HID > Opciones > Elegir mouse* en el teléfono. A los pocos segundos de
   titilar deja de dejarse encontrar; si no aparece, repetir desde el paso 2.

Suelen aparecer como **"Wireless Controller"**: en Bluetooth se hacen pasar por un joystick de PS4 (DualShock 4,
`054C:05C4`), no de PS3.

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

### Genéricos que en Bluetooth son de PS4

Con PS + Start (todas las luces titilando juntas y rápido) se dejan encontrar como "Wireless Controller", clase
`0x002508` (joystick), con servicio HID y PnP `054C:05C4` (DualShock 4).

- Conexión: la abre el host, en los canales HID 0x11 y 0x13, sin emparejar ni autenticación (probado desde
  Linux con un socket L2CAP sin seguridad: conecta y manda reportes enseguida). A veces el primer intento da
  "page timeout" y el siguiente conecta.
- Reportes: `A1 01` + 9 bytes, unos 200 por segundo: LX LY RX RY (0-255, 0x80 al centro); byte 4 con la cruceta
  en los 4 bits bajos (0-7 desde arriba en sentido horario, 8 = suelta) y cuadrado, cruz, círculo, triángulo en
  los bits 4-7; byte 5 L1 R1 L2 R2 Share Options L3 R3 (bits 0-7); byte 6 bit 0 = PS; bytes 7 y 8 la presión de L2
  y R2. (Es el formato del DualShock 4; en el clon que se probó, todo suelto da `80 80 80 80 08 00 00 00 00`.)
- Sin confirmar: después de la primera conexión, al volver a ponerlo a titilar ya no se dejó conectar desde la PC.
  Puede que guarde a quién se conectó y lo llame él, como el DualShock 4 (en ese caso el driver tiene que atender
  la llamada entrante).
