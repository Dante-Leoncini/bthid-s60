"""
ds3pair.py - le graba a un joystick de PS3 (DualShock 3 / Sixaxis) por USB la direccion Bluetooth de su
"consola": el telefono. El DualShock 3 no se empareja como un mouse: al apretar PS llama el a la direccion
que tiene guardada. Con la del telefono, llama al telefono y el driver lo atiende.

    python ds3pair.py                       muestra los joysticks enchufados: su direccion y a quien llaman
    python ds3pair.py 00:1A:2B:3C:4D:5E     les graba esa direccion (la del telefono: en la app Bluetooth HID,
                                            Opciones > Joystick de PS3, o *#2820# en Nokia; va con o sin los ':')

Windows (API HID del sistema) o Linux (/dev/hidraw, con sudo), sin instalar nada. El joystick va por el cable USB.
Muchos joysticks genericos arrancan en modo Xbox 360 (045e:028e) y asi no aceptan esto. En Linux, con sudo, se
intenta pasarlos a modo PS3: se saca un rato el driver xpad (el de Xbox) y se pide desenchufarlo y enchufarlo.
"""
import ctypes
import os
import sys

SONY, DS3 = 0x054C, 0x0268
# genericos que hablan como el DualShock 3 (vendor, producto)
PADS = {(SONY, DS3), (0x2563, 0x0575), (0x0E8F, 0x3075), (0x2563, 0x0523)}
# el modo Xbox 360 de los genericos (y del de Microsoft)
XBOX = (0x045E, 0x028E)


def fmt(a):
    return ':'.join('%02X' % x for x in a)


def direccion(texto):
    """'00:1A:2B:3C:4D:5E', '00-1A-...' o '001a2b3c4d5e' (asi la muestra *#2820#) -> 6 bytes"""
    h = texto.replace(':', '').replace('-', '').replace(' ', '')
    if len(h) != 12:
        raise ValueError(texto)
    return bytes.fromhex(h)


def linux_usb():
    """(vendor, producto, nombre, carpeta en /sys) de cada cosa enchufada por USB"""
    import glob

    out = []
    for d in sorted(glob.glob('/sys/bus/usb/devices/*')):
        try:
            ven = int(open(d + '/idVendor').read(), 16)
            pro = int(open(d + '/idProduct').read(), 16)
        except (OSError, ValueError):
            continue
        nombre = []
        for f in ('manufacturer', 'product'):
            try:
                nombre.append(open(d + '/' + f).read().strip())
            except OSError:
                pass
        out.append((ven, pro, ' '.join(nombre), d))
    return out


def linux_joysticks(avisar):
    """(ruta en /dev, vendor, producto, nombre) de cada joystick de PS3 que hay en /dev/hidraw"""
    import glob

    out = []
    for dev in sorted(glob.glob('/sys/class/hidraw/hidraw*')):
        try:
            uevent = open(dev + '/device/uevent').read()
        except OSError:
            continue
        hidid = [l.split('=')[1] for l in uevent.splitlines() if l.startswith('HID_ID=')]
        if not hidid:
            continue
        _, ven, pro = hidid[0].split(':')
        ven, pro = int(ven, 16), int(pro, 16)
        name = [l.split('=', 1)[1] for l in uevent.splitlines() if l.startswith('HID_NAME=')]
        if (ven, pro) not in PADS:
            if avisar:
                print('(%s: %04x:%04x %s, no es de PS3)' % (os.path.basename(dev), ven, pro, name[0] if name else ''))
            continue
        out.append(('/dev/' + os.path.basename(dev), ven, pro, name[0] if name else ''))
    return out


def linux_sacar_de_xbox(xbox):
    """un generico en modo Xbox 360: lo toma el driver xpad, le habla como Windows y el joystick se queda en ese
    modo. La PS3 no le dice nada de eso y el joystick pasa a modo PS3. Aca se hace lo mismo: se saca xpad y se
    bloquea un rato (en /run, que se borra solo al reiniciar), se pide desenchufarlo y enchufarlo, y se espera a que
    aparezca como joystick de PS3. Al terminar xpad vuelve."""
    import subprocess
    import time

    for ven, pro, nombre, d in xbox:
        print('%s (%04x:%04x %s): esta en modo Xbox 360, asi no acepta la direccion' % (os.path.basename(d), ven, pro,
              nombre))
    if os.geteuid() != 0:
        print('Para intentar pasarlo a modo PS3 hay que correr esto con sudo.')
        return []
    bloqueo = '/run/modprobe.d/ds3pair-sin-xpad.conf'
    try:
        if not os.path.isdir('/run/modprobe.d'):
            os.makedirs('/run/modprobe.d')
        with open(bloqueo, 'w') as f:
            f.write('blacklist xpad\n')  # que no se vuelva a cargar solo al enchufarlo
        if subprocess.call(['modprobe', '-r', 'xpad']) != 0:
            print('No se pudo sacar el driver xpad (esta en uso, o viene dentro del kernel).')
            return []
        print()
        print('Desenchufa el joystick, espera 5 segundos y enchufalo de nuevo (sin apretar nada)...')
        antes = set(u[:3] for u in linux_usb())
        desenchufado = False
        volvio = None  # cuando volvio a aparecer en modo Xbox
        fin = time.time() + 180
        while time.time() < fin:
            time.sleep(0.5)
            pads = linux_joysticks(False)
            if pads:
                print('   ahora esta en modo PS3')
                return pads
            ahora = set(u[:3] for u in linux_usb())
            for ven, pro, nombre in ahora - antes:
                print('   enchufado: %04x:%04x %s' % (ven, pro, nombre))
            hay_xbox = any(u[:2] == XBOX for u in ahora)
            if not hay_xbox and not desenchufado:
                desenchufado = True
                print('   desenchufado')
            if hay_xbox and desenchufado and volvio is None:
                volvio = time.time()
                print('   volvio en modo Xbox: espero 30 segundos a ver si cambia solo...')
            if volvio and time.time() - volvio > 30:
                break
            antes = ahora
        print('Sigue en modo Xbox: este joystick no se deja pasar a modo PS3 desde la PC. Probar por Bluetooth:')
        print('desenchufarlo, mantener PS + Start hasta que titile y buscarlo desde el telefono (ver tools/PS3.md).')
        return []
    finally:
        try:
            os.remove(bloqueo)
        except OSError:
            pass
        subprocess.call(['modprobe', 'xpad'])


def linux_grabar(pads, nueva):
    """por /dev/hidraw: HIDIOCGFEATURE / HIDIOCSFEATURE (el byte 0 es el numero de reporte, como en Windows)"""
    import fcntl

    def ioc(dirn, nr, size):
        return (dirn << 30) | (size << 16) | (ord('H') << 8) | nr

    todo_bien = True
    for path, ven, pro, name in pads:
        try:
            fd = os.open(path, os.O_RDWR)
        except OSError as e:
            print('joystick %s (%04x:%04x %s): no se puede abrir (%s): correrlo con sudo' % (path, ven, pro, name,
                  e.strerror))
            todo_bien = False
            continue
        n = 64
        def get(rid):
            b = bytearray(n)
            b[0] = rid
            try:
                fcntl.ioctl(fd, ioc(3, 0x07, n), b)  # HIDIOCGFEATURE
                return bytes(b)
            except OSError:
                return None
        propia = get(0xF2)
        llama = get(0xF5)
        print('joystick %s (%04x:%04x %s): direccion %s, llama a %s' % (path, ven, pro, name,
              fmt(propia[5:11]) if propia else '?', fmt(llama[3:9]) if llama else '?'))
        if nueva:
            b = bytearray(8)
            b[0], b[1], b[2] = 0xF5, 0x01, 0x00
            b[3:9] = nueva
            try:
                fcntl.ioctl(fd, ioc(3, 0x06, len(b)), b)  # HIDIOCSFEATURE
                ok = True
            except OSError as e:
                ok = False
                print('   no lo acepto: %s' % e)
            llama = get(0xF5)
            print('   grabado %s -> ahora llama a %s' % ('bien' if ok else 'MAL', fmt(llama[3:9]) if llama else '?'))
            todo_bien = todo_bien and ok
        os.close(fd)
    if nueva and todo_bien:
        print('Listo: desenchufalo y apreta PS, llama al telefono.')
    return 0 if todo_bien else 1


def linux_main(nueva):
    pads = linux_joysticks(True)
    if not pads:
        xbox = [u for u in linux_usb() if u[:2] == XBOX]
        if xbox:
            pads = linux_sacar_de_xbox(xbox)
            if not pads:
                return 1
    if not pads:
        print('No hay ningun joystick de PS3 enchufado por USB (lsusb dice que hay?).')
        return 1
    return linux_grabar(pads, nueva)


def leer_direccion():
    """la direccion que se pasa al correrlo, o None para solo mirar"""
    if len(sys.argv) < 2:
        return None
    try:
        return direccion(sys.argv[1])
    except ValueError:
        print('La direccion va asi: 00:1A:2B:3C:4D:5E (o 001a2b3c4d5e, como la muestra *#2820#)')
        sys.exit(1)


if sys.platform != 'win32':
    sys.exit(linux_main(leer_direccion()))

import ctypes.wintypes as W

hid = ctypes.WinDLL('hid')
setupapi = ctypes.WinDLL('setupapi')
kernel32 = ctypes.WinDLL('kernel32')


class GUID(ctypes.Structure):
    _fields_ = [('Data1', W.DWORD), ('Data2', W.WORD), ('Data3', W.WORD), ('Data4', ctypes.c_ubyte * 8)]


class SP_DEVICE_INTERFACE_DATA(ctypes.Structure):
    _fields_ = [('cbSize', W.DWORD), ('InterfaceClassGuid', GUID), ('Flags', W.DWORD), ('Reserved', ctypes.c_void_p)]


class HIDD_ATTRIBUTES(ctypes.Structure):
    _fields_ = [('Size', W.ULONG), ('VendorID', W.USHORT), ('ProductID', W.USHORT), ('VersionNumber', W.USHORT)]


class HIDP_CAPS(ctypes.Structure):
    _fields_ = [('Usage', W.USHORT), ('UsagePage', W.USHORT), ('InputReportByteLength', W.USHORT),
                ('OutputReportByteLength', W.USHORT), ('FeatureReportByteLength', W.USHORT),
                ('Reserved', W.USHORT * 17), ('rest', W.USHORT * 10)]


setupapi.SetupDiGetClassDevsW.restype = ctypes.c_void_p
setupapi.SetupDiGetClassDevsW.argtypes = [ctypes.POINTER(GUID), W.LPCWSTR, W.HWND, W.DWORD]
setupapi.SetupDiEnumDeviceInterfaces.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.POINTER(GUID), W.DWORD,
                                                 ctypes.POINTER(SP_DEVICE_INTERFACE_DATA)]
setupapi.SetupDiGetDeviceInterfaceDetailW.argtypes = [ctypes.c_void_p, ctypes.POINTER(SP_DEVICE_INTERFACE_DATA),
                                                      ctypes.c_void_p, W.DWORD, ctypes.POINTER(W.DWORD), ctypes.c_void_p]
setupapi.SetupDiDestroyDeviceInfoList.argtypes = [ctypes.c_void_p]
kernel32.CreateFileW.restype = W.HANDLE
kernel32.CreateFileW.argtypes = [W.LPCWSTR, W.DWORD, W.DWORD, ctypes.c_void_p, W.DWORD, W.DWORD, W.HANDLE]
kernel32.CloseHandle.argtypes = [W.HANDLE]
hid.HidD_GetAttributes.argtypes = [W.HANDLE, ctypes.POINTER(HIDD_ATTRIBUTES)]
hid.HidD_GetPreparsedData.argtypes = [W.HANDLE, ctypes.POINTER(ctypes.c_void_p)]
hid.HidD_FreePreparsedData.argtypes = [ctypes.c_void_p]
hid.HidP_GetCaps.argtypes = [ctypes.c_void_p, ctypes.POINTER(HIDP_CAPS)]
hid.HidD_GetFeature.argtypes = [W.HANDLE, ctypes.c_void_p, W.ULONG]
hid.HidD_SetFeature.argtypes = [W.HANDLE, ctypes.c_void_p, W.ULONG]


def joysticks():
    """(ruta, handle, largo de los reportes feature) de cada DualShock 3 enchufado por USB"""
    guid = GUID()
    hid.HidD_GetHidGuid(ctypes.byref(guid))
    info = setupapi.SetupDiGetClassDevsW(ctypes.byref(guid), None, None, 0x12)  # PRESENT | DEVICEINTERFACE
    out = []
    i = 0
    while True:
        data = SP_DEVICE_INTERFACE_DATA()
        data.cbSize = ctypes.sizeof(data)
        if not setupapi.SetupDiEnumDeviceInterfaces(info, None, ctypes.byref(guid), i, ctypes.byref(data)):
            break
        i += 1
        need = W.DWORD()
        setupapi.SetupDiGetDeviceInterfaceDetailW(info, ctypes.byref(data), None, 0, ctypes.byref(need), None)
        buf = ctypes.create_string_buffer(need.value)
        ctypes.cast(buf, ctypes.POINTER(W.DWORD))[0] = 8 if ctypes.sizeof(ctypes.c_void_p) == 8 else 6
        if not setupapi.SetupDiGetDeviceInterfaceDetailW(info, ctypes.byref(data), buf, need, None, None):
            continue
        path = ctypes.wstring_at(ctypes.addressof(buf) + 4)
        h = kernel32.CreateFileW(path, 0xC0000000, 3, None, 3, 0, None)  # leer+escribir, compartido, OPEN_EXISTING
        if h in (None, W.HANDLE(-1).value):
            continue
        attr = HIDD_ATTRIBUTES()
        attr.Size = ctypes.sizeof(attr)
        hid.HidD_GetAttributes(h, ctypes.byref(attr))
        if (attr.VendorID, attr.ProductID) not in PADS:
            kernel32.CloseHandle(h)
            continue
        pre = ctypes.c_void_p()
        caps = HIDP_CAPS()
        hid.HidD_GetPreparsedData(h, ctypes.byref(pre))
        hid.HidP_GetCaps(pre, ctypes.byref(caps))
        hid.HidD_FreePreparsedData(pre)
        out.append((path, h, max(caps.FeatureReportByteLength, 64)))
    setupapi.SetupDiDestroyDeviceInfoList(info)
    return out


def get_feature(h, n, rid):
    b = (ctypes.c_ubyte * n)()
    b[0] = rid
    if not hid.HidD_GetFeature(h, b, n):
        return None
    return bytes(b)


def main():
    nueva = leer_direccion()
    pads = joysticks()
    if not pads:
        print('No hay ningun joystick de PS3 enchufado por USB. (Si Windows lo ve como un mando de Xbox 360,')
        print('esta en modo Xbox y asi no acepta la direccion: probar en Linux, ver tools/PS3.md.)')
        return 1
    for k, (path, h, n) in enumerate(pads):
        # (en Windows el byte 0 es el numero de reporte: todo va uno mas que en sixpair, el de Linux)
        propia = get_feature(h, n, 0xF2)   # la direccion del joystick: bytes 5..10
        llama = get_feature(h, n, 0xF5)    # a quien llama: bytes 3..8
        print('joystick %d: direccion %s, llama a %s' % (k + 1, fmt(propia[5:11]) if propia else '?',
                                                       fmt(llama[3:9]) if llama else '?'))
        if nueva:
            b = (ctypes.c_ubyte * n)()
            b[0], b[1], b[2] = 0xF5, 0x01, 0x00
            for j in range(6):
                b[3 + j] = nueva[j]
            # (el reporte F5: el numero, 0x01 0x00 y la direccion)
            ok = hid.HidD_SetFeature(h, b, n)
            llama = get_feature(h, n, 0xF5)
            print('   grabado %s -> ahora llama a %s' % ('bien' if ok else 'MAL', fmt(llama[3:9]) if llama else '?'))
        kernel32.CloseHandle(h)
    return 0


if __name__ == '__main__':
    sys.exit(main())
