import ctypes
import json
import struct
import sys


command = sys.argv[1]
original_pid = int(sys.argv[2], 0)
rebuild_pid = int(sys.argv[3], 0)
rebuild_base = int(sys.argv[4], 0)
loop_rva = int(sys.argv[5], 0)

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
k32.OpenProcess.restype = ctypes.c_void_p


class Process:
    def __init__(self, pid, access):
        self.handle = k32.OpenProcess(access, False, pid)
        if not self.handle:
            raise ctypes.WinError(ctypes.get_last_error())

    def read_u32(self, address):
        data = ctypes.create_string_buffer(4)
        transferred = ctypes.c_size_t()
        if not k32.ReadProcessMemory(
                self.handle, ctypes.c_void_p(address), data, 4,
                ctypes.byref(transferred)) or transferred.value != 4:
            raise ctypes.WinError(ctypes.get_last_error())
        return struct.unpack("<I", data.raw)[0]

    def write_u32s(self, address, values):
        data = struct.pack("<" + "I" * len(values), *values)
        transferred = ctypes.c_size_t()
        if not k32.WriteProcessMemory(
                self.handle, ctypes.c_void_p(address), data, len(data),
                ctypes.byref(transferred)) or transferred.value != len(data):
            raise ctypes.WinError(ctypes.get_last_error())

    def close(self):
        if self.handle:
            k32.CloseHandle(self.handle)
            self.handle = None


original = Process(original_pid, 0x0438)
rebuild = Process(rebuild_pid, 0x0438)
loop = rebuild_base + loop_rva
try:
    if command == "pace":
        original_interval = int(sys.argv[6], 0)
        rebuild_interval = int(sys.argv[7], 0)
        original.write_u32s(0x00725B74, [original_interval] * 16)
        rebuild.write_u32s(loop + 0x140, [rebuild_interval] * 16)
        rebuild.write_u32s(loop + 0x180, [rebuild_interval] * 7)
        fixed_mode = rebuild.read_u32(loop + 0x1C8)
        if fixed_mode < 7:
            rebuild.write_u32s(loop + 0x19C + fixed_mode * 4, [1])
    elif command != "frames":
        raise ValueError(f"unknown command: {command}")
    print(json.dumps({
        "original": original.read_u32(0x007071A4),
        "rebuild": rebuild.read_u32(loop + 0x1DC),
    }))
finally:
    original.close()
    rebuild.close()
