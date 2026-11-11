"""
ds3pair.py - le graba a un joystick de PS3 (DualShock 3 / Sixaxis) por USB la direccion Bluetooth de su
"consola": el telefono. El DualShock 3 no se empareja como un mouse: al apretar PS llama el a la direccion
que tiene guardada. Con la del telefono, llama al telefono y el driver lo atiende.

    python ds3pair.py                       muestra los joysticks enchufados: su direccion y a quien llaman
    python ds3pair.py 00:1A:2B:3C:4D:5E     les graba esa direccion (la del telefono: en la app Bluetooth HID,
                                            Opciones > Joystick de PS3, o *#2820# en Nokia; va con o sin los ':')

Linux (/dev/hidraw, con sudo), sin instalar nada. El joystick va por el cable USB. En Windows el driver HID del
sistema no deja grabarla: hay que ponerle al joystick el driver WinUSB con Zadig y tener pyusb
(pip install pyusb libusb-package), y se le habla por libusb.
Muchos joysticks genericos arrancan en modo Xbox 360 (045e:028e) y asi no aceptan esto. En Linux, con sudo, se
intenta pasarlos a modo PS3: se saca un rato el driver xpad (el de Xbox) y se pide desenchufarlo y enchufarlo.

Cada vez que corre deja ds3pair.log en su carpeta: lo que mostro, cada pedido al joystick con lo que contesto
(bytes) o el error, que hay enchufado por USB y con que driver, y las versiones. Si no anda, se manda ese archivo.
"""
import ctypes
import os
import sys
import time
import traceback

SONY, DS3 = 0x054C, 0x0268
# genericos que hablan como el DualShock 3 (vendor, producto)
PADS = {(SONY, DS3), (0x2563, 0x0575), (0x0E8F, 0x3075), (0x2563, 0x0523)}
# el modo Xbox 360 de los genericos (y del de Microsoft)
XBOX = (0x045E, 0x028E)


def fmt(a):
    return ':'.join('%02X' % x for x in a)


def hx(b):
    return ' '.join('%02X' % x for x in b) if b is not None else '-'


LOG = None       # el archivo ds3pair.log
LOG_RUTA = None
INICIO = time.time()


def log(texto):
    """solo al log, no a la pantalla: el detalle, para saber que paso cuando falla"""
    if LOG:
        try:
            LOG.write('[%6.2f] %s\n' % (time.time() - INICIO, texto))
            LOG.flush()
        except Exception:
            pass


class Tee:
    """lo que se muestra en la pantalla va tambien al log"""

    def __init__(self, pantalla):
        self.pantalla = pantalla

    def write(self, s):
        self.pantalla.write(s)
        if LOG:
            try:
                LOG.write(s)
                LOG.flush()
            except Exception:
                pass

    def flush(self):
        self.pantalla.flush()

    def __getattr__(self, n):
        return getattr(self.pantalla, n)


def abrir_log():
    """ds3pair.log en la carpeta del script (o en la actual); se pisa cada vez que corre"""
    global LOG, LOG_RUTA
    import hashlib
    import platform

    script = os.path.abspath(sys.argv[0]) if sys.argv and sys.argv[0] else ''
    for carpeta in (os.path.dirname(script), os.getcwd()):
        ruta = os.path.join(carpeta or '.', 'ds3pair.log')
        try:
            LOG = open(ruta, 'w', encoding='utf-8')
            LOG_RUTA = ruta
            break
        except OSError:
            continue
    if not LOG:
        return
    if os.environ.get('SUDO_UID'):  # con sudo: que quede del usuario, para poder abrirlo y borrarlo
        try:
            os.chown(LOG_RUTA, int(os.environ['SUDO_UID']), int(os.environ.get('SUDO_GID', -1)))
        except (OSError, ValueError):
            pass
    try:
        version = hashlib.sha1(open(script, 'rb').read()).hexdigest()[:10]
    except Exception:
        version = '?'
    LOG.write('ds3pair.log - si no anduvo, mandar este archivo a quien te este ayudando\n')
    LOG.write('Tiene la direccion del telefono y la del joystick, lo enchufado por USB y las versiones.\n\n')
    log('fecha %s' % time.strftime('%Y-%m-%d %H:%M:%S'))
    log('ds3pair %s (sha1 del script)' % version)
    log('sistema %s, %s' % (platform.platform(), platform.machine()))
    log('python %s, %d bits' % (platform.python_version(), ctypes.sizeof(ctypes.c_void_p) * 8))
    log('comando ds3pair.py %s' % ' '.join(sys.argv[1:]))
    sys.stdout = Tee(sys.stdout)


def seguro(fn, *args):
    """los diagnosticos no pueden cortar el programa: si fallan, al log"""
    try:
        return fn(*args)
    except Exception:
        log('(%s fallo)\n%s' % (fn.__name__, traceback.format_exc()))


def correr(fn):
    abrir_log()
    pantalla = sys.stdout
    try:
        codigo = fn()
    except SystemExit as e:
        codigo = e.code if isinstance(e.code, int) else 1
    except Exception as e:
        log(traceback.format_exc())
        print('Error inesperado: %r' % e)
        codigo = 1
    log('salida %s' % codigo)
    if codigo and LOG_RUTA:
        print()
        print('Si no anduvo, manda este archivo a quien te este ayudando, tiene el detalle de lo que paso:')
        print('   ' + LOG_RUTA)
    if isinstance(pantalla, Tee):
        sys.stdout = pantalla.pantalla
    if LOG:
        LOG.close()
    return codigo


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
        r = subprocess.call(['modprobe', '-r', 'xpad'])
        log('modprobe -r xpad -> %d, xpad cargado: %s' % (r, os.path.isdir('/sys/module/xpad')))
        if r != 0:
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
        print('Sigue en modo Xbox: a este joystick no se le puede grabar la direccion desde la PC.')
        return []
    finally:
        try:
            os.remove(bloqueo)
        except OSError:
            pass
        subprocess.call(['modprobe', 'xpad'])


def linux_grabar(pads, nueva):
    """por /dev/hidraw: HIDIOCGFEATURE / HIDIOCSFEATURE, como el plugin sixaxis de BlueZ. El byte 0 va con el numero
    de reporte y lo leido llega tal cual lo manda el joystick: la direccion propia en los bytes 4..9 del F2 y a quien
    llama en los 2..7 del F5. Para grabar: F5 01 y la direccion (8 bytes)"""
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
        def get(rid, n):
            b = bytearray(n)
            b[0] = rid
            try:
                r = fcntl.ioctl(fd, ioc(3, 0x07, n), b)  # HIDIOCGFEATURE
                log('   GET feature %02X (%d) -> ioctl %s: %s' % (rid, n, r, hx(b)))
                return bytes(b)
            except OSError as e:
                log('   GET feature %02X (%d) -> error %s' % (rid, n, e))
                return None
        propia = get(0xF2, 18)
        llama = get(0xF5, 8)
        print('joystick %s (%04x:%04x %s): direccion %s, llama a %s' % (path, ven, pro, name,
              fmt(propia[4:10]) if propia else '?', fmt(llama[2:8]) if llama else '?'))
        if nueva:
            b = bytearray([0xF5, 0x01]) + nueva
            try:
                r = fcntl.ioctl(fd, ioc(3, 0x06, len(b)), b)  # HIDIOCSFEATURE
                log('   SET feature %s -> %s' % (hx(b), r))
                ok = True
            except OSError as e:
                ok = False
                log('   SET feature %s -> error %s' % (hx(b), e))
                print('   no lo acepto: %s' % e)
            llama = get(0xF5, 8)
            ok = ok and llama is not None and llama[2:8] == nueva
            print('   grabado %s -> ahora llama a %s' % ('bien' if ok else 'MAL', fmt(llama[2:8]) if llama else '?'))
            todo_bien = todo_bien and ok
        os.close(fd)
    if nueva and todo_bien:
        print('Listo: desenchufalo y apreta PS, llama al telefono.')
    return 0 if todo_bien else 1


def linux_diagnostico():
    """al log: los modulos, cada cosa USB con el driver de cada interfaz, y cada hidraw"""
    import glob

    log('usuario %d (0 = sudo)' % os.geteuid())
    try:
        log('kernel %s' % open('/proc/sys/kernel/osrelease').read().strip())
    except OSError:
        pass
    log('modulos: ' + ', '.join('%s %s' % (m, 'si' if os.path.isdir('/sys/module/' + m) else 'no')
                                for m in ('xpad', 'hid_sony', 'hid_generic', 'usbhid')))
    for ven, pro, nombre, d in linux_usb():
        if ven == 0x1D6B:  # los hubs de la PC
            continue
        drivers = []
        for i in sorted(glob.glob(d + ':*')):
            try:
                drivers.append('%s=%s' % (i.rsplit(':', 1)[1], os.path.basename(os.readlink(i + '/driver'))))
            except OSError:
                drivers.append('%s=sin driver' % i.rsplit(':', 1)[1])
        log('usb %s %04x:%04x %s [%s]' % (os.path.basename(d), ven, pro, nombre, ', '.join(drivers)))
    for dev in sorted(glob.glob('/sys/class/hidraw/hidraw*')):
        try:
            uevent = dict(l.split('=', 1) for l in open(dev + '/device/uevent').read().splitlines() if '=' in l)
        except OSError:
            continue
        log('%s %s %s' % (os.path.basename(dev), uevent.get('HID_ID', '?'), uevent.get('HID_NAME', '')))
        try:
            _, ven, pro = uevent['HID_ID'].split(':')
            if (int(ven, 16), int(pro, 16)) in PADS:
                desc = open(dev + '/device/report_descriptor', 'rb').read()
                log('   descriptor (%d bytes): %s' % (len(desc), hx(desc)))
        except (KeyError, ValueError, OSError):
            pass


def linux_dmesg():
    """al log: lo ultimo que dijo el kernel del USB y de los joysticks (hace falta sudo)"""
    import subprocess

    try:
        out = subprocess.run(['dmesg'], stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=10)
    except Exception as e:
        log('dmesg: %r' % e)
        return
    if out.returncode:
        log('dmesg -> %d: %s' % (out.returncode, out.stderr.decode('utf-8', 'replace').strip()))
    lineas = [l for l in out.stdout.decode('utf-8', 'replace').splitlines()
              if any(p in l.lower() for p in ('usb', 'hid', 'xpad', 'sony', 'input', 'xbox'))
              and 'serialnumber' not in l.lower()]
    log('dmesg (ultimas %d de %d):' % (min(len(lineas), 40), len(lineas)))
    for l in lineas[-40:]:
        log('   ' + l)


def linux_main(nueva):
    seguro(linux_diagnostico)
    try:
        return linux_pasos(nueva)
    finally:
        seguro(linux_dmesg)


def linux_pasos(nueva):
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
    sys.exit(correr(lambda: linux_main(leer_direccion())))

import ctypes.wintypes as W

# Windows: el driver HID del sistema solo deja pedir y mandar los reportes que el joystick declara en su descriptor,
# y el DualShock 3 no declara el F2 ni el F5 (los manda igual, Linux los pide sin mirar el descriptor). Por eso se
# le habla por libusb: hace falta cambiarle el driver por WinUSB con Zadig y tener pyusb (ver tools/PS3.md).

hid = ctypes.WinDLL('hid', use_last_error=True)
setupapi = ctypes.WinDLL('setupapi')
kernel32 = ctypes.WinDLL('kernel32')


def libusb_pads():
    """los joysticks de PS3 que ve libusb (pip install pyusb libusb-package); None si pyusb no esta instalado"""
    try:
        import libusb_package
    except ImportError:
        return None
    try:
        return [d for d in libusb_package.find(find_all=True) if (d.idVendor, d.idProduct) in PADS]
    except Exception as e:
        print('(libusb no anda: %s)' % e)
        return []


def libusb_grabar(devs, nueva):
    """GET_REPORT / SET_REPORT por el control, como sixpair: el numero de reporte va en wValue y lo leido llega tal
    cual lo manda el joystick (la direccion propia en 4..9 del F2, a quien llama en 2..7 del F5). Cada pedido se
    prueba con los largos y formatos de sixpair, del kernel y de BlueZ; la direccion propia es solo para mostrar.
    Devuelve 'bien', 'driver' (no tiene WinUSB) o 'rechazo' (el joystick no contesto los pedidos)"""
    import usb.core

    def falla_driver(e):
        # libusb: -3 sin permiso, -5 no encontrado, -12 no soportado (sigue con el driver HID)
        return isinstance(e, NotImplementedError) or getattr(e, 'backend_error_code', None) in (-3, -5, -12)

    def pedir(d, *intentos):
        """(lo que devolvio el primer intento que anduvo, None) o (None, el ultimo error)"""
        err = None
        for args in intentos:
            que = '%02X %02X %04X %s' % (args[0], args[1], args[2],
                                         args[4] if isinstance(args[4], int) else hx(args[4]))
            try:
                r = d.ctrl_transfer(*args, timeout=2000)
                r = bytes(r) if not isinstance(r, int) else r
                log('   ctrl %s -> %s' % (que, r if isinstance(r, int) else '%d bytes: %s' % (len(r), hx(r))))
                return r, None
            except (usb.core.USBError, NotImplementedError, ValueError) as e:
                log('   ctrl %s -> error %r (libusb %s, errno %s)' % (que, e, getattr(e, 'backend_error_code', '-'),
                                                                     getattr(e, 'errno', '-')))
                err = e
        return None, err

    estado = 'bien'
    for k, d in enumerate(devs):
        nombre = 'joystick %d (%04x:%04x)' % (k + 1, d.idVendor, d.idProduct)
        log('%s: bus %s dir %s, bcdDevice %s' % (nombre, getattr(d, 'bus', '?'), getattr(d, 'address', '?'),
                                                 hex(getattr(d, 'bcdDevice', 0))))
        propia, e1 = pedir(d, (0xA1, 0x01, 0x03F2, 0, 17), (0xA1, 0x01, 0x03F2, 0, 18))
        llama, e2 = pedir(d, (0xA1, 0x01, 0x03F5, 0, 8))
        print('%s: direccion %s, llama a %s' % (nombre, fmt(propia[4:10]) if propia and len(propia) >= 10 else '?',
              fmt(llama[2:8]) if llama and len(llama) >= 8 else '?'))
        errores = []
        for que, e in (('su direccion', e1), ('a quien llama', e2)):
            if e:
                print('   no contesto %s: %s' % (que, e))
                errores.append(e)
        if nueva:
            r, e3 = pedir(d, (0x21, 0x09, 0x03F5, 0, bytes([0x01, 0x00]) + nueva),   # como sixpair
                          (0x21, 0x09, 0x03F5, 0, bytes([0xF5, 0x01]) + nueva))      # como BlueZ
            if e3:
                print('   no acepto la direccion: %s' % e3)
                errores.append(e3)
            llama, _ = pedir(d, (0xA1, 0x01, 0x03F5, 0, 8))
            if e3:
                ok = False
            elif llama and len(llama) >= 8:
                ok = llama[2:8] == nueva
            else:
                ok = True  # lo acepto, pero no deja leerlo para comprobar
            print('   grabado %s -> ahora llama a %s' % ('bien' if ok else 'MAL',
                  fmt(llama[2:8]) if llama and len(llama) >= 8 else '? (no se pudo comprobar)'))
            fallo = not ok
        else:
            fallo = propia is None and llama is None
        if fallo and estado != 'driver':
            estado = 'driver' if any(falla_driver(e) for e in errores) else 'rechazo'
    if nueva and estado == 'bien':
        print('Listo: desenchufalo y apreta PS, llama al telefono.')
    return estado


class GUID(ctypes.Structure):
    _fields_ = [('Data1', W.DWORD), ('Data2', W.WORD), ('Data3', W.WORD), ('Data4', ctypes.c_ubyte * 8)]


class SP_DEVICE_INTERFACE_DATA(ctypes.Structure):
    _fields_ = [('cbSize', W.DWORD), ('InterfaceClassGuid', GUID), ('Flags', W.DWORD), ('Reserved', ctypes.c_void_p)]


class SP_DEVINFO_DATA(ctypes.Structure):
    _fields_ = [('cbSize', W.DWORD), ('ClassGuid', GUID), ('DevInst', W.DWORD), ('Reserved', ctypes.c_void_p)]


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
setupapi.SetupDiEnumDeviceInfo.argtypes = [ctypes.c_void_p, W.DWORD, ctypes.POINTER(SP_DEVINFO_DATA)]
setupapi.SetupDiGetDeviceRegistryPropertyW.argtypes = [ctypes.c_void_p, ctypes.POINTER(SP_DEVINFO_DATA), W.DWORD,
                                                       ctypes.POINTER(W.DWORD), ctypes.c_void_p, W.DWORD,
                                                       ctypes.POINTER(W.DWORD)]
kernel32.CreateFileW.restype = W.HANDLE
kernel32.CreateFileW.argtypes = [W.LPCWSTR, W.DWORD, W.DWORD, ctypes.c_void_p, W.DWORD, W.DWORD, W.HANDLE]
kernel32.CloseHandle.argtypes = [W.HANDLE]
hid.HidD_GetAttributes.argtypes = [W.HANDLE, ctypes.POINTER(HIDD_ATTRIBUTES)]
hid.HidD_GetPreparsedData.argtypes = [W.HANDLE, ctypes.POINTER(ctypes.c_void_p)]
hid.HidD_FreePreparsedData.argtypes = [ctypes.c_void_p]
hid.HidP_GetCaps.argtypes = [ctypes.c_void_p, ctypes.POINTER(HIDP_CAPS)]
hid.HidD_GetFeature.argtypes = [W.HANDLE, ctypes.c_void_p, W.ULONG]
hid.HidD_SetFeature.argtypes = [W.HANDLE, ctypes.c_void_p, W.ULONG]


def windows_drivers():
    """al log: cada cosa USB enchufada con su driver (WinUSB, HidUsb, xusb22...), para ver si Zadig le puso
    WinUSB al joystick y no a otra cosa"""
    info = setupapi.SetupDiGetClassDevsW(None, 'USB', None, 0x06)  # PRESENT | ALLCLASSES
    if not info or info == ctypes.c_void_p(-1).value:
        log('setupapi: no lista los USB (error %d)' % ctypes.get_last_error())
        return

    def prop(data, p):
        buf = ctypes.create_unicode_buffer(512)
        if not setupapi.SetupDiGetDeviceRegistryPropertyW(info, ctypes.byref(data), p, None, buf,
                                                          ctypes.sizeof(buf), None):
            return ''
        return buf.value

    for i in range(300):
        data = SP_DEVINFO_DATA()
        data.cbSize = ctypes.sizeof(data)
        if not setupapi.SetupDiEnumDeviceInfo(info, i, ctypes.byref(data)):
            break
        hwid = prop(data, 1)                          # SPDRP_HARDWAREID: USB\VID_054C&PID_0268&REV_0100
        if not hwid:
            continue
        log('usb %s | %s | driver %s' % (hwid, prop(data, 0x0C) or prop(data, 0), prop(data, 4) or 'ninguno'))
    setupapi.SetupDiDestroyDeviceInfoList(info)


def windows_diagnostico():
    try:
        from importlib.metadata import version
    except ImportError:
        version = None
    for paquete in ('pyusb', 'libusb-package'):
        try:
            log('%s %s' % (paquete, version(paquete)))
        except Exception as e:
            log('%s: no esta instalado (%s)' % (paquete, e.__class__.__name__))
    try:
        import libusb_package
        for d in libusb_package.find(find_all=True):
            log('libusb ve %04x:%04x clase %02x' % (d.idVendor, d.idProduct, getattr(d, 'bDeviceClass', 0)))
    except ImportError:
        pass
    except Exception as e:
        log('libusb no anda: %r' % e)
    seguro(windows_drivers)


def joysticks():
    """(handle, largo de los reportes feature, vendor, producto) de cada joystick de PS3 con el driver HID"""
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
            log('HID %04x:%04x' % (attr.VendorID, attr.ProductID))
            kernel32.CloseHandle(h)
            continue
        pre = ctypes.c_void_p()
        caps = HIDP_CAPS()
        hid.HidD_GetPreparsedData(h, ctypes.byref(pre))
        hid.HidP_GetCaps(pre, ctypes.byref(caps))
        hid.HidD_FreePreparsedData(pre)
        log('HID %04x:%04x de PS3: entrada %d, salida %d, feature %d bytes' % (attr.VendorID, attr.ProductID,
            caps.InputReportByteLength, caps.OutputReportByteLength, caps.FeatureReportByteLength))
        out.append((h, max(caps.FeatureReportByteLength, 64), attr.VendorID, attr.ProductID))
    setupapi.SetupDiDestroyDeviceInfoList(info)
    return out


def get_feature(h, n, rid):
    b = (ctypes.c_ubyte * n)()
    b[0] = rid
    if not hid.HidD_GetFeature(h, b, n):
        log('   HidD_GetFeature %02X (%d) -> error %d' % (rid, n, ctypes.get_last_error()))
        return None
    log('   HidD_GetFeature %02X (%d) -> %s' % (rid, n, hx(bytes(b))))
    return bytes(b)


def hid_grabar(pads, nueva):
    """por el driver HID de Windows (byte 0 = numero de reporte, lo leido llega tal cual: como en Linux). Con el
    DualShock 3 y los que son como el no anda: Windows no deja pedir el F2 ni el F5 porque no estan en el descriptor"""
    todo_bien = True
    for k, (h, n, ven, pro) in enumerate(pads):
        propia = get_feature(h, n, 0xF2)   # la direccion del joystick: bytes 4..9
        err = ctypes.get_last_error() if propia is None else 0
        llama = get_feature(h, n, 0xF5)    # a quien llama: bytes 2..7
        print('joystick %d (%04x:%04x): direccion %s, llama a %s%s' % (k + 1, ven, pro,
              fmt(propia[4:10]) if propia else '?', fmt(llama[2:8]) if llama else '?',
              '  (Windows no deja pedirlo: error %d)' % err if err else ''))
        todo_bien = todo_bien and propia is not None
        if nueva:
            b = (ctypes.c_ubyte * n)()
            b[0], b[1] = 0xF5, 0x01
            for j in range(6):
                b[2 + j] = nueva[j]
            ok = hid.HidD_SetFeature(h, b, n)
            err = ctypes.get_last_error() if not ok else 0
            log('   HidD_SetFeature %s -> %s (error %d)' % (hx(bytes(b)[:8]), ok, err))
            llama = get_feature(h, n, 0xF5)
            print('   grabado %s -> ahora llama a %s%s' % ('bien' if ok else 'MAL', fmt(llama[2:8]) if llama else '?',
                  '  (error %d)' % err if err else ''))
            todo_bien = todo_bien and ok
        kernel32.CloseHandle(h)
    if nueva and todo_bien:
        print('Listo: desenchufalo y apreta PS, llama al telefono.')
    return todo_bien


def main():
    nueva = leer_direccion()
    seguro(windows_diagnostico)
    usbs = libusb_pads()
    estado = libusb_grabar(usbs, nueva) if usbs else None
    if estado == 'bien':
        return 0
    if estado == 'rechazo':
        print()
        print('El joystick tiene el driver WinUSB, pero no contesto los pedidos. Probar desenchufarlo, enchufarlo en')
        print('otro puerto USB (directo a la PC, sin hub) y correr esto de nuevo. Si sigue igual, probar en Linux.')
        return 1
    pads = joysticks()
    if pads and hid_grabar(pads, nueva):
        return 0
    print()
    if not usbs and not pads:
        print('No hay ningun joystick de PS3 enchufado por USB.')
        if usbs is None:
            print('(Si ya le pusiste el driver WinUSB con Zadig, falta: pip install pyusb libusb-package)')
        print('(Si Windows lo ve como un mando de Xbox 360, esta en modo Xbox y asi no acepta la direccion:')
        print('ver tools/PS3.md.)')
        return 1
    print('Windows no deja grabarle la direccion con su driver. Hay que hacer esto una vez (ver tools/PS3.md):')
    if usbs is None:
        print('  1. pip install pyusb libusb-package')
    print('  %s. Zadig (zadig.akeo.ie): Options > List All Devices, elegir el joystick, WinUSB y Replace Driver'
          % ('2' if usbs is None else '1'))
    print('  y despues correr esto de nuevo.')
    return 1


if __name__ == '__main__':
    sys.exit(correr(main))
