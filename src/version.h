/*
version.h - la version del driver: la muestra la app en Acerca de y el servidor en su log al arrancar.
Subirla con cada cambio que se instale en los telefonos. (La version del paquete .sis es otra: tiene que
ser mayor que la del "Bluetooth HID support" de Hinkka para instalarse como su actualizacion.)
*/
#ifndef BTHID_VERSION_H
#define BTHID_VERSION_H

#define BTHID_VERSION "0.13" // 0.2: joysticks de PS3 y PS4 (y genericos), hasta 4. 0.3: joystick como teclado.
                            // 0.6: el modo teclado manda las teclas al sistema (todas las apps y el menu)
                            // 0.7: a los joysticks de PS3 desconectados se los espera (llaman ellos al apretar PS)
                            // 0.8: lugares fijos (joystick 1-4, mouse, teclado) en la pantalla principal, con la bateria;
                            //      desconectar, conectar y quitar cada uno; al de PS4 se lo llama siempre, cada 4 s
                            // 0.9: al de PS4 original se le habla al conectar (si no, no manda nada); la app se adapta al girar la pantalla
                            // 0.10: Wiimote (botones, acelerometro, MotionPlus, camara IR y punteria con 4 LEDs)
                            // 0.11: la pantalla principal y la prueba de joysticks se manejan con el dedo (N8, 5800...)
                            // 0.12: Wiimote: las flechas no giran solas al girar el control; opcion para girarlas
                            // 0.13: los joysticks genericos (los de Android, no DualShock): se leen con su descriptor

#endif
