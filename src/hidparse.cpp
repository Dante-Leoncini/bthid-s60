/*
hidparse.cpp - lectura del descriptor de reportes HID, ver hidparse.h

Recorre los items del descriptor (globales: pagina, tamano, cantidad, numero de reporte, minimo logico;
locales: usos; principales: Input) y va sumando la posicion de cada campo de entrada en su reporte. Nos
quedamos con lo que usa un mouse o un teclado: botones 1..3, X, Y, rueda, modificadores y el arreglo de teclas.
*/

#include "hidparse.h"

enum { EMaxUsages = 16 };

static void SetOnce( THidField &aDst, TInt aId, TInt aBit, TInt aBits, TBool aSigned )
{
	if( aDst.iBits )
		return; // el primero que aparece (un mouse con dos X raras se queda con la primera)
	aDst.iReportId = (TUint8)aId;
	aDst.iBit      = (TUint16)aBit;
	aDst.iBits     = (TUint8)aBits;
	aDst.iSigned   = aSigned;
}

void HidParseDescriptor( const TDesC8 &aDesc, THidLayout &aLayout )
{
	TInt  usagePage = 0, reportSize = 0, reportCount = 0, reportId = 0, logMin = 0;
	TUint usages[EMaxUsages];
	TInt  nUsages = 0, usageMin = -1;
	TInt  i = 0, k;

	Mem::FillZ( &aLayout, sizeof( aLayout ));
	while( i < aDesc.Length())
	{
		TUint8 b = aDesc[i++];
		TInt   size, type, tag, svalue;
		TUint  value = 0;

		if( b == 0xFE ) // item largo: no se usan en HID, saltearlo
		{
			if( i + 1 >= aDesc.Length())
				break;
			i += 2 + aDesc[i];
			continue;
		}
		size = b & 3;
		if( size == 3 )
			size = 4;
		type = ( b >> 2 ) & 3;
		tag  = b >> 4;
		if( i + size > aDesc.Length())
			break;
		for( k = 0; k < size; k++ )
			value |= (TUint)aDesc[i + k] << ( 8 * k );
		i += size;
		svalue = size == 1 ? (TInt8)value : size == 2 ? (TInt16)value : (TInt)value;

		if( type == 1 ) // global
		{
			switch( tag )
			{
			case 0: usagePage = value; break;
			case 1: logMin = svalue; break;
			case 7: reportSize = value; break;
			case 8: reportId = value & 0xff; aLayout.iUsesIds = ETrue; break;
			case 9: reportCount = value; break;
			default: break;
			}
		}
		else if( type == 2 ) // local
		{
			if( tag == 0 && nUsages < EMaxUsages )
				usages[nUsages++] = size == 4 ? value : ( (TUint)usagePage << 16 ) | value;
			else if( tag == 1 )
				usageMin = size == 4 ? value & 0xffff : value;
		}
		else if( type == 0 ) // principal
		{
			if( tag == 8 ) // Input
			{
				TInt pos = aLayout.iReportBits[reportId];
				if( !( value & 1 )) // datos (no constante: los rellenos no nos importan)
				{
					if( !( value & 2 )) // arreglo: las teclas apretadas del teclado
					{
						if( usagePage == 7 && !aLayout.iKeys.iBits )
						{
							SetOnce( aLayout.iKeys, reportId, pos, reportSize, EFalse );
							aLayout.iKeyCount = reportCount;
						}
					}
					else
					{
						for( k = 0; k < reportCount; k++ )
						{
							TInt  page = usagePage, bit = pos + k * reportSize;
							TUint u = 0;
							TBool sgn = logMin < 0;
							if( nUsages )
							{
								TUint full = usages[k < nUsages ? k : nUsages - 1];
								page = full >> 16;
								u = full & 0xffff;
							}
							else if( usageMin >= 0 )
								u = usageMin + k;

							if( page == 9 && u >= 1 && u <= 3 )
								SetOnce( aLayout.iButton[u - 1], reportId, bit, reportSize, EFalse );
							else if( page == 1 && u == 0x30 )
								SetOnce( aLayout.iX, reportId, bit, reportSize, sgn );
							else if( page == 1 && u == 0x31 )
								SetOnce( aLayout.iY, reportId, bit, reportSize, sgn );
							else if( page == 1 && u == 0x38 )
								SetOnce( aLayout.iWheel, reportId, bit, reportSize, sgn );
							else if( page == 7 && u == 0xE0 && reportSize == 1 )
								SetOnce( aLayout.iMods, reportId, bit, 8, EFalse ); // E0..E7 van seguidos
						}
					}
				}
				aLayout.iReportBits[reportId] = (TUint16)( pos + reportSize * reportCount );
			}
			// cualquier item principal (Input, Output, Feature, Collection) termina los usos locales
			nUsages = 0;
			usageMin = -1;
		}
	}
	aLayout.iValid = aLayout.iX.iBits || aLayout.iKeys.iBits;
}

TInt HidField( const TDesC8 &aPayload, const THidField &aField, TInt aIndex )
{
	TUint v = 0;
	TInt  i, first = aField.iBit + aIndex * aField.iBits;

	if( !aField.iBits || aField.iBits > 32 )
		return 0;
	for( i = 0; i < aField.iBits; i++ )
	{
		TInt bit = first + i;
		if( bit / 8 >= aPayload.Length())
			return 0;
		if( aPayload[bit / 8] & ( 1 << ( bit & 7 )))
			v |= 1u << i;
	}
	if( aField.iSigned && aField.iBits < 32 && ( v & ( 1u << ( aField.iBits - 1 ))))
		v |= ~0u << aField.iBits;
	return (TInt)v;
}
