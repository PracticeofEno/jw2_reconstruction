import ctypes
import struct
import sys
import time

pid = int(sys.argv[1], 0)
address = int(sys.argv[2], 0)
target = int(sys.argv[3], 0)
settle = float(sys.argv[5]) if len(sys.argv) > 5 else 0.0

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
k32.OpenProcess.restype = ctypes.c_void_p
ntdll = ctypes.WinDLL("ntdll")
handle = k32.OpenProcess(0x0C38, False, pid)
if not handle:
    raise ctypes.WinError(ctypes.get_last_error())

def frame():
    data = ctypes.create_string_buffer(4)
    done = ctypes.c_size_t()
    if not k32.ReadProcessMemory(handle, ctypes.c_void_p(address), data, 4,
                                 ctypes.byref(done)):
        raise ctypes.WinError(ctypes.get_last_error())
    return struct.unpack("<I", data.raw)[0]

try:
    # Test drivers can overlap suspensions.  Resume repeatedly so a helper
    # terminated while holding a suspension cannot leave the target pinned.
    for _ in range(8):
        ntdll.NtResumeProcess(handle)
    deadline = time.monotonic() + (
        float(sys.argv[4]) if len(sys.argv) > 4 else 5.0)
    value = frame()
    while value < target and time.monotonic() < deadline:
        time.sleep(0.0002)
        value = frame()
    if value >= target and settle > 0:
        time.sleep(settle)
    ntdll.NtSuspendProcess(handle)
    print(value, flush=True)
finally:
    k32.CloseHandle(handle)
