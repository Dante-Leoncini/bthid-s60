# Joysticks de PS3 (DualShock 3 y genéricos)

El DualShock 3 no se empareja desde la lista de Bluetooth: tiene grabada la dirección de su "consola" y, al
apretar PS, la llama él. Para que llame al teléfono hay que grabarle la dirección Bluetooth del teléfono por USB,
una vez por joystick. Eso hace `ds3pair.py`.

## 1. La dirección del teléfono

En Nokia: marcar `*#2820#`. Aparece algo como `00:1A:2B:3C:4D:5E`.

## 2. Grabarla en el joystick

Con el joystick enchufado por USB:

```
python ds3pair.py                       # muestra los joysticks: su dirección y a quién llaman
python ds3pair.py 00:1A:2B:3C:4D:5E     # les graba la dirección del teléfono
```

- **Windows**: anda con el DualShock 3 original (aparece como "PLAYSTATION(R)3 Controller", `054C:0268`). Muchos
  genéricos, enchufados a una PC con Windows, se ponen en **modo Xbox 360** (`045E:028E`) y ahí no aceptan el
  comando.
- **Linux** (con `sudo`): el driver `hid-sony` los arranca como lo hace la PS3, y los genéricos suelen aparecer como
  de PS3. Mirar con `lsusb` cómo aparece: `054c:0268` (Sony) o un genérico como `2563:0575` (ShanWan) va bien;
  `045e:028e` es el modo Xbox.

  ```
  lsusb
  sudo python3 ds3pair.py
  sudo python3 ds3pair.py 00:1A:2B:3C:4D:5E
  ```

  Si un joystick aparece como "no es de PS3", su vendor:producto se agrega a `PADS` en `ds3pair.py`.

Tiene que terminar en `grabado bien -> ahora llama a <la dirección del teléfono>`.

## 3. Conectarlo

Desenchufar el USB y apretar PS: el joystick llama al teléfono. El driver (desde la versión 0.2) lo atiende,
le prende la luz de su número (1 a 4) y la app tiene la prueba en *Pruebas > Joysticks*.

## Si el genérico no acepta la dirección

Algunos genéricos también se dejan encontrar en la búsqueda de Bluetooth como un mouse (manteniendo PS, o PS +
Start / PS + Select, hasta que las luces titilen rápido). Si aparece en *Bluetooth HID > Opciones > Elegir mouse*,
anotar el nombre: el driver puede llamarlo desde el teléfono sin grabar nada por USB.

## El protocolo (lo que hace el driver)

- Conexión: el joystick abre los canales HID de control (PSM 0x11) y de datos (0x13), sin autenticación.
- Activarlo: SET_REPORT feature `0xF4` por el canal de control: `53 F4 42 03 00 00`.
- Reporte de entrada (canal de datos), `A1 01` + 48 bytes: botones en los bytes 3-5 (5 = PS), sticks en 7-10
  (LX LY RX RY, 0-255), presión de cada botón en 15-26 (arriba, derecha, abajo, izquierda, L2, R2, L1, R1,
  triángulo, círculo, cruz, cuadrado).
- Luces y vibración: SET_REPORT output `52 01` + reporte: byte 3/4 duración/fuerza del motor chico, 5/6 del
  grande, byte 11 las luces (0x02 = luz 1 ... 0x10 = luz 4).
