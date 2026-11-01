/*
version.h - la version del driver: la muestra la app en Acerca de y el servidor en su log al arrancar.
Subirla con cada cambio que se instale en los telefonos. (La version del paquete .sis es otra: tiene que
ser mayor que la del "Bluetooth HID support" de Hinkka para instalarse como su actualizacion.)
*/
#ifndef BTHID_VERSION_H
#define BTHID_VERSION_H

#define BTHID_VERSION "0.8" // 0.2: joysticks de PS3 y PS4 (y genericos), hasta 4. 0.3: joystick como teclado.
                            // 0.6: el modo teclado manda las teclas al sistema (todas las apps y el menu)
                            // 0.7: a los joysticks de PS3 desconectados se los espera (llaman ellos al apretar PS)
                            // 0.8: lugares fijos (joystick 1-4, mouse, teclado) en la pantalla principal, con la bateria;
                            //      desconectar, conectar y quitar cada uno; al de PS4 se lo llama siempre, cada 4 s

#endif
