import ctypes
import struct
import sys
import time

pid = int(sys.argv[1])
status_wait_seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 0.0
route_mode = sys.argv[3] if len(sys.argv) > 3 else "synthetic"
title_only = route_mode in ("title-only", "real-input")
target = 0x00504B10
modal_pump_return = 0x00504ADD
title_modal_return = 0x0042CCFC
single_player_tick_return = 0x0042CE42
original = bytes.fromhex(
    "558bec81ec84000000535657518dbd7cffffffb921000000b8ccccccccf3ab59"
)

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
kernel32.OpenProcess.restype = ctypes.c_void_p
kernel32.VirtualAllocEx.restype = ctypes.c_void_p
process = kernel32.OpenProcess(0x043A, False, pid)
if not process:
    raise ctypes.WinError(ctypes.get_last_error())


def read_process_memory(address, size):
    result = ctypes.create_string_buffer(size)
    transferred = ctypes.c_size_t()
    if not kernel32.ReadProcessMemory(
        process, ctypes.c_void_p(address), result, size,
        ctypes.byref(transferred)
    ) or transferred.value != size:
        raise ctypes.WinError(ctypes.get_last_error())
    return result.raw


def write_process_memory(address, data):
    transferred = ctypes.c_size_t()
    if not kernel32.WriteProcessMemory(
        process, ctypes.c_void_p(address), data, len(data),
        ctypes.byref(transferred)
    ) or transferred.value != len(data):
        raise ctypes.WinError(ctypes.get_last_error())


def expected_call(call_site, destination):
    return b"\xE8" + struct.pack("<i", destination - (call_site + 5))


try:
    transferred = ctypes.c_size_t()
    current = read_process_memory(target, len(original))
    if current != original:
        raise RuntimeError(
            f"unsupported ranker.exe prologue at 0x{target:08X}: "
            f"{current.hex()}"
        )

    route_calls = {
        0x0042CCF7: 0x00504AB0,
        0x0042CE3D: target,
    }
    for call_site, destination in route_calls.items():
        expected = expected_call(call_site, destination)
        current = read_process_memory(call_site, len(expected))
        if current != expected:
            raise RuntimeError(
                f"unsupported ranker.exe route call at 0x{call_site:08X}: "
                f"{current.hex()}"
            )

    cave = kernel32.VirtualAllocEx(process, None, 0x1000, 0x3000, 0x40)
    if not cave:
        raise ctypes.WinError(ctypes.get_last_error())
    guard = cave + 0x200

    # Ghidra shows that JW2_02.TRC record 94 returns title entry one through
    # RunUiScreenModalPump (return 0x0042CCFC).  Its dispatcher opens record 95,
    # whose direct input-tick call returns at 0x0042CE42; entry one there sends
    # WM_USER+9/action 2 to open the Replay load dialog.  Match both call sites
    # explicitly so startup, message, and gameplay screens cannot consume the
    # two synthetic activations.
    code = bytearray()
    labels = {}
    fixups = []

    def mark(label):
        labels[label] = len(code)

    def jump(opcode, label):
        code.extend(opcode)
        fixups.append((len(code), label))
        code.extend(b"\0" * 4)

    code += b"\x8B\x04\x24"  # mov eax, [esp]
    code += b"\xA3" + struct.pack("<I", guard + 4)
    code += b"\x3D" + struct.pack("<I", modal_pump_return)
    jump(b"\x0F\x85", "check_single_player")  # jne
    code += b"\x8B\x44\x24\x60"  # mov eax, [esp + 0x60]
    code += b"\xA3" + struct.pack("<I", guard + 8)
    code += b"\x3D" + struct.pack("<I", title_modal_return)
    jump(b"\x0F\x85", "trampoline")
    code += b"\xA1" + struct.pack("<I", guard)
    code += b"\x85\xC0"  # test eax, eax
    jump(b"\x0F\x85", "trampoline")
    code += b"\xC7\x05" + struct.pack("<I", guard) + struct.pack("<I", 1)
    jump(b"\xE9", "activate_entry_one")

    mark("check_single_player")
    code += b"\x81\x3C\x24" + struct.pack("<I", single_player_tick_return)
    jump(b"\x0F\x85", "trampoline")
    code += b"\xA1" + struct.pack("<I", guard)
    code += b"\x83\xF8\x01"  # cmp eax, 1
    jump(b"\x0F\x85", "trampoline")
    code += b"\xA1" + struct.pack("<I", guard + 12)
    code += b"\x85\xC0"  # test eax, eax
    jump(b"\x0F\x84", "trampoline")  # je
    code += b"\xC7\x05" + struct.pack("<I", guard) + struct.pack("<I", 2)

    mark("activate_entry_one")
    code += b"\x8B\x44\x24\x04\xC7\x00" + struct.pack("<I", 1)
    code += bytes.fromhex("8b442408 c70000000000 b001 c20800")

    mark("trampoline")
    code += original
    code += b"\xE9" + struct.pack(
        "<i", (target + len(original)) - (cave + len(code) + 5)
    )

    for displacement_offset, label in fixups:
        destination_offset = labels[label]
        struct.pack_into(
            "<i", code, displacement_offset,
            destination_offset - (displacement_offset + 4)
        )

    if not kernel32.WriteProcessMemory(
        process, ctypes.c_void_p(cave), bytes(code), len(code),
        ctypes.byref(transferred)
    ):
        raise ctypes.WinError(ctypes.get_last_error())
    old_protection = ctypes.c_ulong()
    if not kernel32.VirtualProtectEx(
        process, ctypes.c_void_p(target), 8, 0x40,
        ctypes.byref(old_protection)
    ):
        raise ctypes.WinError(ctypes.get_last_error())
    patch = b"\xE9" + struct.pack("<i", cave - (target + 5)) + b"\x90\x90\x90"
    if not kernel32.WriteProcessMemory(
        process, ctypes.c_void_p(target), patch, len(patch),
        ctypes.byref(transferred)
    ):
        raise ctypes.WinError(ctypes.get_last_error())
    kernel32.FlushInstructionCache(process, None, 0)
    print(
        f"original replay route armed pid={pid} cave=0x{cave:X} "
        "title=1 single_player=1"
    )

    # The single-player screen invokes its input tick before its first draw.
    # Let it complete at least one real draw/present cycle before activating
    # Replay; otherwise the original modal transition can remain black.
    phase_deadline = time.monotonic() + 60.0
    while time.monotonic() < phase_deadline:
        state, last_return = struct.unpack(
            "<II", read_process_memory(guard, 8)
        )
        if state == 1 and last_return == single_player_tick_return:
            break
        time.sleep(0.01)
    else:
        raise RuntimeError("original title route did not reach single-player UI")

    if title_only:
        if route_mode == "real-input":
            main_window = struct.unpack(
                "<I", read_process_memory(0x0143FF6C, 4)
            )[0]
            user32 = ctypes.WinDLL("user32", use_last_error=True)
            if not user32.PostMessageW(main_window, 0x0100, 0x52, 0):
                raise ctypes.WinError(ctypes.get_last_error())
            if not user32.PostMessageW(main_window, 0x0101, 0x52, 0):
                raise ctypes.WinError(ctypes.get_last_error())
            print(
                f"original replay accelerator posted hwnd=0x{main_window:X}"
            )
        print("original replay route stopped at single-player UI")
        sys.exit(0)

    time.sleep(1.0)
    write_process_memory(guard + 12, struct.pack("<I", 1))

    activation_deadline = time.monotonic() + 15.0
    while time.monotonic() < activation_deadline:
        state = struct.unpack("<I", read_process_memory(guard, 4))[0]
        if state == 2:
            break
        time.sleep(0.01)
    else:
        raise RuntimeError("original single-player route did not activate Replay")

    if status_wait_seconds > 0:
        time.sleep(status_wait_seconds)
        state, last_return, last_modal_return = struct.unpack(
            "<III", read_process_memory(guard, 12)
        )
        print(
            f"original replay route state={state} "
            f"last_return=0x{last_return:08X} "
            f"last_modal_return=0x{last_modal_return:08X}"
        )
finally:
    kernel32.CloseHandle(process)
