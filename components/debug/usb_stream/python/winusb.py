# SPDX-FileCopyrightText: 2026 Boumedine Billal
# SPDX-License-Identifier: Apache-2.0

"""winusb.py: the PC end of usb_stream (Windows).

The board is a vendor-specific USB device that Windows binds to its own WinUSB driver; this module finds it by its
device interface GUID and reads its stream through winusb.dll (ctypes: nothing to install).

    dev = Device()            # waits up to 15 s for the board
    data = dev.read()         # the next bytes (b"" after 0.2 s without data)
    dev.write(b"r")
    dev.close()
"""
import ctypes
import ctypes.wintypes as wt
import time
import uuid

INTERFACE_GUID = "{0781A8D8-EC80-4BA1-ABAA-6C5533217DF0}"  # usb_stream.hpp
EP_IN, EP_OUT = 0x81, 0x01

_setupapi = ctypes.WinDLL("setupapi", use_last_error=True)
_winusb = ctypes.WinDLL("winusb", use_last_error=True)
_kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)


class _GUID(ctypes.Structure):
    _fields_ = [("Data1", wt.DWORD), ("Data2", wt.WORD), ("Data3", wt.WORD), ("Data4", ctypes.c_ubyte * 8)]


class _IfData(ctypes.Structure):  # SP_DEVICE_INTERFACE_DATA
    _fields_ = [("cbSize", wt.DWORD), ("InterfaceClassGuid", _GUID), ("Flags", wt.DWORD),
                ("Reserved", ctypes.c_size_t)]


_HANDLE = ctypes.c_void_p
_INVALID = ctypes.c_void_p(-1).value
_setupapi.SetupDiGetClassDevsW.restype = _HANDLE
_setupapi.SetupDiGetClassDevsW.argtypes = [ctypes.POINTER(_GUID), wt.LPCWSTR, wt.HWND, wt.DWORD]
_setupapi.SetupDiEnumDeviceInterfaces.argtypes = [_HANDLE, ctypes.c_void_p, ctypes.POINTER(_GUID), wt.DWORD,
                                                  ctypes.POINTER(_IfData)]
_setupapi.SetupDiGetDeviceInterfaceDetailW.argtypes = [_HANDLE, ctypes.POINTER(_IfData), ctypes.c_void_p, wt.DWORD,
                                                       ctypes.POINTER(wt.DWORD), ctypes.c_void_p]
_setupapi.SetupDiDestroyDeviceInfoList.argtypes = [_HANDLE]
_kernel32.CreateFileW.restype = _HANDLE
_kernel32.CreateFileW.argtypes = [wt.LPCWSTR, wt.DWORD, wt.DWORD, ctypes.c_void_p, wt.DWORD, wt.DWORD, _HANDLE]
_kernel32.CloseHandle.argtypes = [_HANDLE]
_winusb.WinUsb_Initialize.argtypes = [_HANDLE, ctypes.POINTER(_HANDLE)]
_winusb.WinUsb_Free.argtypes = [_HANDLE]
_winusb.WinUsb_SetPipePolicy.argtypes = [_HANDLE, ctypes.c_ubyte, wt.ULONG, wt.ULONG, ctypes.c_void_p]
_winusb.WinUsb_ReadPipe.argtypes = [_HANDLE, ctypes.c_ubyte, ctypes.c_void_p, wt.ULONG, ctypes.POINTER(wt.ULONG),
                                    ctypes.c_void_p]
_winusb.WinUsb_WritePipe.argtypes = _winusb.WinUsb_ReadPipe.argtypes
_winusb.WinUsb_FlushPipe.argtypes = [_HANDLE, ctypes.c_ubyte]


def _guid(s):
    u = uuid.UUID(s)
    g = _GUID(u.time_low, u.time_mid, u.time_hi_version)
    g.Data4[:] = list(u.bytes[8:])
    return g


def device_paths(guid=INTERFACE_GUID):
    """the paths of the present devices with this interface GUID"""
    g = _guid(guid)
    h = _setupapi.SetupDiGetClassDevsW(ctypes.byref(g), None, None, 0x02 | 0x10)  # DIGCF_PRESENT | INTERFACE
    if h in (None, _INVALID):
        return []
    paths = []
    try:
        i = 0
        while True:
            d = _IfData()
            d.cbSize = ctypes.sizeof(_IfData)
            if not _setupapi.SetupDiEnumDeviceInterfaces(h, None, ctypes.byref(g), i, ctypes.byref(d)):
                break
            need = wt.DWORD()
            _setupapi.SetupDiGetDeviceInterfaceDetailW(h, ctypes.byref(d), None, 0, ctypes.byref(need), None)
            buf = ctypes.create_string_buffer(need.value)
            # SP_DEVICE_INTERFACE_DETAIL_DATA_W: cbSize is the fixed part's size, 8 on 64-bit Windows
            ctypes.cast(buf, ctypes.POINTER(wt.DWORD))[0] = 8 if ctypes.sizeof(ctypes.c_void_p) == 8 else 6
            if _setupapi.SetupDiGetDeviceInterfaceDetailW(h, ctypes.byref(d), buf, need, None, None):
                paths.append(ctypes.wstring_at(ctypes.addressof(buf) + 4))
            i += 1
    finally:
        _setupapi.SetupDiDestroyDeviceInfoList(h)
    return paths


class Device:
    """the board's stream: read() and write() on its bulk endpoints"""

    def __init__(self, wait_s=15, chunk=1 << 20):
        t = time.time()
        paths = device_paths()
        while not paths and time.time() - t < wait_s:
            time.sleep(0.5)
            paths = device_paths()
        if not paths:
            raise SystemExit("no board on USB (WinUSB interface %s)" % INTERFACE_GUID)
        self.file = _kernel32.CreateFileW(paths[0], 0xC0000000, 0x3, None, 3, 0x40000080, None)
        # GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | WRITE, OPEN_EXISTING, OVERLAPPED | NORMAL (WinUSB needs it)
        if self.file in (None, _INVALID):
            raise OSError(ctypes.get_last_error(), "CreateFile " + paths[0])
        self.h = _HANDLE()
        if not _winusb.WinUsb_Initialize(self.file, ctypes.byref(self.h)):
            raise OSError(ctypes.get_last_error(), "WinUsb_Initialize")
        timeout = wt.ULONG(200)  # ms: read() returns b"" when nothing came
        _winusb.WinUsb_SetPipePolicy(self.h, EP_IN, 0x03, 4, ctypes.byref(timeout))  # PIPE_TRANSFER_TIMEOUT
        self.buf = ctypes.create_string_buffer(chunk)
        self.got = wt.ULONG()

    def read(self):
        """the next bytes (one transfer: up to a short packet or the chunk size); b"" after 0.2 s without data"""
        if not _winusb.WinUsb_ReadPipe(self.h, EP_IN, self.buf, len(self.buf), ctypes.byref(self.got), None):
            err = ctypes.get_last_error()
            if err == 121:  # ERROR_SEM_TIMEOUT
                return b""
            raise OSError(err, "WinUsb_ReadPipe")
        return self.buf.raw[:self.got.value]

    def write(self, data):
        n = wt.ULONG()
        if not _winusb.WinUsb_WritePipe(self.h, EP_OUT, data, len(data), ctypes.byref(n), None):
            raise OSError(ctypes.get_last_error(), "WinUsb_WritePipe")
        return n.value

    def close(self):
        if self.h:
            _winusb.WinUsb_Free(self.h)
            self.h = None
        if self.file:
            _kernel32.CloseHandle(self.file)
            self.file = None
