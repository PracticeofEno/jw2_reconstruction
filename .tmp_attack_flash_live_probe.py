"""Read-only live probe for the local A-attack target flash timer."""

import argparse
import ctypes
import json
import struct
import time
from ctypes import wintypes


PROCESS_VM_READ = 0x0010
PROCESS_QUERY_INFORMATION = 0x0400

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
k32.OpenProcess.restype = wintypes.HANDLE
k32.ReadProcessMemory.restype = wintypes.BOOL
k32.ReadProcessMemory.argtypes = [
    wintypes.HANDLE, wintypes.LPCVOID, wintypes.LPVOID,
    ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t),
]
k32.CloseHandle.argtypes = [wintypes.HANDLE]


def number(value):
    return int(value, 0) if isinstance(value, str) else int(value)


class Memory:
    def __init__(self, pid):
        self.handle = k32.OpenProcess(
            PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, False, pid)
        if not self.handle:
            raise ctypes.WinError(ctypes.get_last_error())

    def close(self):
        if self.handle:
            k32.CloseHandle(self.handle)
            self.handle = None

    def read(self, address, size):
        buffer = (ctypes.c_ubyte * size)()
        transferred = ctypes.c_size_t()
        if not k32.ReadProcessMemory(
                self.handle, ctypes.c_void_p(address), buffer, size,
                ctypes.byref(transferred)) or transferred.value != size:
            raise ctypes.WinError(ctypes.get_last_error())
        return bytes(buffer)

    def u32(self, address):
        return struct.unpack("<I", self.read(address, 4))[0]

    def u64(self, address):
        return struct.unpack("<Q", self.read(address, 8))[0]


def vector_pointers(mem, address):
    begin, end, capacity = struct.unpack("<QQQ", mem.read(address, 24))
    if begin == 0 or end < begin or capacity < end or (end - begin) % 8:
        return []
    count = (end - begin) // 8
    if count > 4096:
        raise RuntimeError(f"implausible unit vector count {count}")
    if count == 0:
        return []
    return list(struct.unpack(f"<{count}Q", mem.read(begin, count * 8)))


def find_unit_pointer(mem, movement, movement_layout, unit_layout, slot):
    for vector_name in ("active_units", "lifecycle_units"):
        address = movement + movement_layout[vector_name]
        for pointer in vector_pointers(mem, address):
            if pointer and mem.u32(pointer + unit_layout["runtime_slot"]) == slot:
                return pointer
    return None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("pid", type=int)
    parser.add_argument("image_base", type=lambda value: int(value, 0))
    parser.add_argument("layout_json")
    parser.add_argument("target_slot", type=int)
    parser.add_argument("--timeout", type=float, default=12.0)
    parser.add_argument("--interval", type=float, default=0.001)
    parser.add_argument("--summary")
    args = parser.parse_args()

    with open(args.layout_json, "r", encoding="utf-8-sig") as stream:
        layout = json.load(stream)
    unit_layout = {
        key: number(value) for key, value in layout["unit_layout"].items()
    }
    movement_layout = {
        key: number(value)
        for key, value in layout["movement_context_layout"].items()
    }
    loop_layout = {
        key: number(value) for key, value in layout["loop_layout"].items()
    }
    runtime = args.image_base + number(layout["runtime_rva"])
    movement = runtime + number(layout["movement_offset"])
    loop = args.image_base + number(layout["loop_rva"])
    frame_address = loop + loop_layout["simulation_frame"]

    mem = Memory(args.pid)
    try:
        started = time.monotonic()
        samples = []
        last_pair = None
        observed_flash = False
        completed = False
        while time.monotonic() - started < args.timeout:
            frame_before = mem.u32(frame_address)
            pointer = find_unit_pointer(
                mem, movement, movement_layout, unit_layout, args.target_slot)
            if pointer is None:
                time.sleep(args.interval)
                continue
            flags = mem.u32(pointer + unit_layout["draw_flags"])
            frame_after = mem.u32(frame_address)
            if frame_before != frame_after:
                continue
            pair = (frame_before, flags)
            if pair != last_pair:
                samples.append({
                    "frame": frame_before,
                    "flags": flags,
                    "low_timer": flags & 0x7F,
                    "red_phase": (flags & 0x82) == 0x82,
                })
                last_pair = pair
            if (flags & 0x7F) != 0 and (flags & 0x80) != 0:
                observed_flash = True
            if observed_flash and flags == 0x80:
                completed = True
                break
            time.sleep(args.interval)

        flash_rows = [row for row in samples if (row["flags"] & 0x80) != 0]
        timer_rows = [row for row in flash_rows if row["low_timer"] != 0]
        timer_values = []
        for row in timer_rows:
            value = row["flags"]
            if not timer_values or timer_values[-1] != value:
                timer_values.append(value)
        red_values = [value for value in timer_values if (value & 0x82) == 0x82]
        expected_tail = list(range(timer_values[0], 0x80, -1)) \
            if timer_values else []
        summary = {
            "probe": "rebuild local A-attack target draw_flags timer",
            "pid": args.pid,
            "target_slot": args.target_slot,
            "observed_flash": observed_flash,
            "completed_at_0x80": completed,
            "timer_values": timer_values,
            "red_phase_values": red_values,
            "contiguous_original_tail": timer_values == expected_tail,
            "original_result_state": "0x88",
            "pass": (
                observed_flash and completed and timer_values and
                timer_values == expected_tail and timer_values[0] <= 0x88 and
                timer_values[0] >= 0x82 and len(red_values) >= 1
            ),
            "samples": samples,
        }
        if args.summary:
            with open(args.summary, "w", encoding="utf-8") as stream:
                json.dump(summary, stream, ensure_ascii=False, indent=2)
        print(json.dumps(summary, ensure_ascii=False, indent=2))
        return 0 if summary["pass"] else 2
    finally:
        mem.close()


if __name__ == "__main__":
    raise SystemExit(main())
