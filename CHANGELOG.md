# Cambios

Lo nuevo en cada versión del driver Bluetooth HID para S60 3rd. La versión se ve en la app **Bluetooth HID**, en
Opciones > Acerca de.

## 0.8 — 2026-11-01

- **Pantalla principal nueva:** seis lugares fijos, Mouse, Teclado y Joystick 1 a 4. Cada uno dice si está vacío,
  qué dispositivo tiene y cómo está (conectado, desconectado o "llamándolo... apretá PS").
  - Arriba y abajo eligen un lugar y el centro abre sus opciones.
  - Si no entra todo en la pantalla, se desplaza para que siempre se vea el lugar elegido.
- **Opciones de cada lugar:**
  - En un lugar vacío, **Agregar un joystick acá**: el joystick toma ese número de jugador. En Mouse y Teclado,
    elegir uno.
  - **Conectar** y **Desconectar**. Un joystick desconectado a mano no se llama más, pero si se aprieta PS y él
    llama, se lo atiende.
  - **Pasar a joystick N:** cambia el número de un joystick. Si el lugar de destino estaba ocupado, los dos se
    intercambian, y la luz de cada uno cambia al color de su número.
  - **Quitar.**
- **Números de jugador guardados:** cada joystick conserva su número aunque se reinicie el teléfono.
- **Batería** de cada joystick, al lado de "conectado" (que se ve en verde): en % o "cargando". La mandan el
  DualShock 3 y el DualShock 4 original, en su reporte completo, que el driver le pide al conectarse. Algunos
  compatibles no la informan: ahí dice "batería ?". Cada cambio de 10 % queda en el log
  (`c:\data\bthid_log.txt`), para saber si un joystick que se apagó estaba sin batería.
- **Reconexión del joystick de PS4:** se lo llama con autenticación y cifrado, así queda emparejado y vuelve a
  conectar con solo apretar PS. Antes abría la conexión pero no mandaba nada y cortaba con -6305, "falla de
  autenticación". Después de actualizar hay que quitarlo y agregarlo de nuevo en modo emparejar (Share + PS); si
  el teléfono pide un código, es 0000.
- Un joystick de PS4 desconectado se llama cada 4 segundos, siempre. Al apretar PS espera unos segundos a que lo
  llame el teléfono, y con llamadas más espaciadas esa ventana se pasaba.
- **Búsqueda más confiable:** mientras está abierto el cuadro de búsqueda de dispositivos no se llama a ningún
  joystick. Esas llamadas ocupaban la radio y el segundo joystick no aparecía en la lista.

## 0.7 — 2026-11-01

- Un joystick de PS3 desconectado se llama un par de veces y después se lo espera con la radio libre: no
  contesta cuando lo llama el teléfono, y al apretar PS es él quien llama. En 0.7 esto también se aplicaba a los
  de PS4, que funcionan al revés: lo corrige 0.8.

## 0.6 — 2026-11-01

- **Joystick como teclado en todo el teléfono:** las teclas van a todas las apps y al menú del teléfono, no solo
  a las apps que usan la API vieja. A la app de adelante que lee el joystick directo, como un juego, no se le
  mandan.
- **Teclas del teléfono en los perfiles:** OK, las teclas de abajo de la pantalla, menú, llamar, colgar, C, `*`
  y `#`.
- **Joystick 1 por defecto:** X = OK; cuadrado, círculo y triángulo = 2, 3 y 4; Start = 0; R1 y L1 = las teclas
  de abajo de la pantalla; R2 y L2 = 5 y 6; Select = 7; PS = menú; R3 y L3 = 8 y 9. Los joysticks 2 a 4 mandan
  letras.
- Las flechas del joystick 2 son 8 2 4 6 y las del 3, I K J L (las del 2 pisaban sus propios botones).
- El ícono se ve bien en S60 3rd.
- El README explica el joystick como teclado y la tabla de teclas por defecto.

## 0.5 — 2026-10-31

- El número de jugador de cada joystick es por orden de conexión (el primero que se conecta es el 1) y lo
  conserva si se corta.
- El servidor arranca con el teléfono, con la opción **Iniciar con el teléfono** en la app.

## 0.4 — 2026-10-30

- **Perfiles del joystick como teclado,** editables desde la app:
  - Seleccionar perfil abre la lista; el centro o Asignar lo deja en uso.
  - Editar elige el joystick (1 a 4) y muestra cada botón con su tecla; el centro la cambia.
  - Nuevo perfil arranca con todo sin asignar.
  - El Predeterminado siempre está y no se borra.

## 0.3 — 2026-10-30

- **Joystick como teclado:** cada botón manda una tecla de `c:\data\bthid_teclas.txt` a las apps que usan la API
  vieja, no a las que leen el joystick.
- Los joysticks se llaman aunque digan que se reconectan solos.

## 0.2 — 2026-10-30

- **Joysticks:** de PS3 (DualShock 3) y PS4 (DualShock 4 y genéricos), hasta 4. Se eligen en la búsqueda; el de
  PS3 también se atiende cuando llama él.
- Botones, sticks, presión de cada botón, luces y vibración.
- **Prueba de joysticks** en la app, con pestañas 1 a 4.
- **Herramientas** para emparejar el de PS3: `tools/ds3pair.py`, que le graba por USB la dirección del teléfono
  (Windows o Linux), y `tools/PS3.md`. En la app, Opciones > Joystick de PS3 muestra esa dirección.

## 0.1 — 2026-10-30

- **Servidor de mouse y teclado Bluetooth,** con botones que se mantienen apretados de verdad (se puede
  arrastrar).
- `hidsrv.dll` compatible con el de Hinkka (mismo nombre, UID y ordinales): las apps viejas lo usan sin cambios,
  y el paquete se instala como actualización del suyo.
- Lee el descriptor HID de cada dispositivo, así cada mouse y teclado usa su formato, y la rueda anda desde la
  primera conexión.
- Atiende a los mouse que se reconectan solos al moverlos.
- Mouse y teclado cuentan como actividad: la pantalla no se oscurece ni se bloquea mientras se usan.
- **App Bluetooth HID:** elegir y quitar el mouse y el teclado, pruebas de los dos, textos en inglés y castellano,
  y Acerca de con la versión.
- Licencia MIT.
