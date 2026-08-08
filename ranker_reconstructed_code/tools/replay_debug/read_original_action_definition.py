import ctypes
import json
import struct
import sys


pid = int(sys.argv[1], 0)
action = int(sys.argv[2], 0)

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
k32.OpenProcess.restype = ctypes.c_void_p
handle = k32.OpenProcess(0x0410, False, pid)
if not handle:
    raise ctypes.WinError(ctypes.get_last_error())


def read_u32(address):
    data = ctypes.create_string_buffer(4)
    transferred = ctypes.c_size_t()
    if not k32.ReadProcessMemory(
            handle, ctypes.c_void_p(address), data, 4,
            ctypes.byref(transferred)) or transferred.value != 4:
        raise ctypes.WinError(ctypes.get_last_error())
    return struct.unpack("<I", data.raw)[0]


try:
    raw_offset = read_u32(0x0120A518 + action * 4)
    fields = {
        "damage_amount": 0x0120A928,
        "frame_limit": 0x0120A92C,
        "tick_reset": 0x0120A930,
        "tick_threshold": 0x0120A934,
        "tick_release_limit": 0x0120A968,
    }
    result = {"action": action, "raw_offset": raw_offset}
    for name, base in fields.items():
        result[name] = read_u32(base + raw_offset)
    print(json.dumps(result))
finally:
    k32.CloseHandle(handle)
