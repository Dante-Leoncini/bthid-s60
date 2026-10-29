/*
hidparse.h - lectura del descriptor de reportes HID: donde van los botones, el movimiento, la rueda y las teclas

Cada mouse y teclado arma sus reportes a su manera (con o sin numero de reporte, X e Y de 8, 12 o 16 bits, la
rueda en otro reporte...). El descriptor que publica por SDP (HIDDescriptorList) lo dice; con esto lo leemos
igual que una PC, en vez de suponer el formato boot.
*/
#ifndef HIDPARSE_H
#define HIDPARSE_H

#include <e32base.h>

struct THidField
{
	TUint8  iReportId; // 0 = el dispositivo no usa numeros de reporte
	TUint16 iBit;      // primer bit dentro del reporte (sin el byte A1 ni el del numero)
	TUint8  iBits;     // 0 = el campo no esta
	TBool   iSigned;
};

struct THidLayout
{
	TBool     iValid;          // hay mouse o teclado en el descriptor
	TBool     iUsesIds;
	THidField iButton[3];      // izquierdo, derecho, medio
	THidField iX, iY, iWheel;
	THidField iMods;           // los 8 modificadores (0xE0..0xE7) seguidos
	THidField iKeys;           // arreglo de teclas: iBits por tecla, iKeyCount teclas
	TInt      iKeyCount;
	TUint16   iReportBits[256]; // tamano de cada reporte de entrada, por numero
};

// arma el layout a partir del descriptor de reportes
void HidParseDescriptor( const TDesC8 &aDesc, THidLayout &aLayout );
// lee un campo (aPayload = el reporte sin el byte A1 ni el del numero de reporte)
TInt HidField( const TDesC8 &aPayload, const THidField &aField, TInt aIndex = 0 );

#endif // HIDPARSE_H
