"""
ds3pair.py - le graba a un joystick de PS3 (DualShock 3 / Sixaxis) por USB la direccion Bluetooth de su
"consola": el telefono. El DualShock 3 no se empareja como un mouse: al apretar PS llama el a la direccion
que tiene guardada. Con la del telefono, llama al telefono y el driver lo atiende.

    python ds3pair.py                       muestra los joysticks enchufados: su direccion y a quien llaman
    python ds3pair.py 00:1A:2B:3C:4D:5E     les graba esa direccion (la del telefono: *#2820# en Nokia)

Windows (API HID del sistema) o Linux (/dev/hidraw, con sudo), sin instalar nada. El joystick va por el cable USB.
En Windows muchos joysticks genericos se ponen en modo Xbox 360 y no aceptan esto; en Linux el driver hid-sony
los arranca como la PS3 y suelen aparecer como de PS3: probar ahi.
"""
import ctypes
import os
import sys

SONY, DS3 = 0x054C, 0x0268
# genericos que hablan como el DualShock 3 (vendor, producto)
PADS = {(SONY, DS3), (0x2563, 0x0575), (0x0E8F, 0x3075), (0x2563, 0x0523)}


def fmt(a):
    return ':'.join('%02X' % x for x in a)


def linux_main(nueva):
    """lo mismo por /dev/hidraw: HIDIOCGFEATURE / HIDIOCSFEATURE (el byte 0 es el numero de reporte, como en Windows)"""
    import fcntl
    import glob

    def ioc(dirn, nr, size):
        return (dirn << 30) | (size << 16) | (ord('H') << 8) | nr

    encontrados = 0
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
            print('(%s: %04x:%04x %s, no es de PS3)' % (os.path.basename(dev), ven, pro, name[0] if name else ''))
            continue
        encontrados += 1
        path = '/dev/' + os.path.basename(dev)
        fd = os.open(path, os.O_RDWR)
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
        print('joystick %s (%04x:%04x %s): direccion %s, llama a %s' % (path, ven, pro, name[0] if name else '',
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
        os.close(fd)
    if not encontrados:
        print('No hay ningun joystick de PS3 enchufado por USB (lsusb dice que hay?).')
        return 1
    return 0


if sys.platform != 'win32':
    def main():
        nueva = None
        if len(sys.argv) > 1:
            nueva = bytes(int(x, 16) for x in sys.argv[1].replace('-', ':').split(':'))
            assert len(nueva) == 6, 'La direccion va asi: 00:1A:2B:3C:4D:5E'
        return linux_main(nueva)
    sys.exit(main())

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
    pads = joysticks()
    if not pads:
        print('No hay ningun joystick de PS3 enchufado por USB.')
        return 1
    nueva = None
    if len(sys.argv) > 1:
        try:
            nueva = bytes(int(x, 16) for x in sys.argv[1].replace('-', ':').split(':'))
            assert len(nueva) == 6
        except Exception:
            print('La direccion va asi: 00:1A:2B:3C:4D:5E')
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
