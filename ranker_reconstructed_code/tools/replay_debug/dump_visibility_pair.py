#!/usr/bin/env python3
"""Dump the original and reconstructed visibility layers at a suspended frame."""

from __future__ import annotations

import ctypes
import json
from pathlib import Path
import struct
import sys


PROCESS_VM_READ = 0x0010
PROCESS_QUERY_INFORMATION = 0x0400
ORIGINAL_LAYER_ADDRESSES = {
    "current": 0x00758D40,
    "previous": 0x00798D40,
    "owner": 0x007D8D40,
    "terrain_class_flags": 0x00E99E74,
}
EXPECTED_LAYER_BYTES = 0x10000 * 4


class Process:
    def __init__(self, pid: int):
        self.kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        self.kernel32.OpenProcess.restype = ctypes.c_void_p
        self.handle = self.kernel32.OpenProcess(
            PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, False, pid)
        if not self.handle:
            raise ctypes.WinError(ctypes.get_last_error())

    def read(self, address: int, size: int) -> bytes:
        data = ctypes.create_string_buffer(size)
        transferred = ctypes.c_size_t()
        if not self.kernel32.ReadProcessMemory(
                self.handle, ctypes.c_void_p(address), data, size,
                ctypes.byref(transferred)) or transferred.value != size:
            raise ctypes.WinError(ctypes.get_last_error())
        return data.raw

    def close(self) -> None:
        if self.handle:
            self.kernel32.CloseHandle(self.handle)
            self.handle = None


def parse_hex(value: str) -> int:
    return int(value, 0)


def dump_rebuild_vector(process: Process, address: int, expected_bytes: int,
                        output: Path) -> dict:
    begin, end, capacity = struct.unpack("<QQQ", process.read(address, 24))
    if begin == 0 or end < begin or capacity < end:
        raise ValueError(
            f"invalid vector at 0x{address:X}: "
            f"begin=0x{begin:X} end=0x{end:X} capacity=0x{capacity:X}")
    size = end - begin
    if size != expected_bytes:
        raise ValueError(
            f"unexpected visibility vector size at 0x{address:X}: "
            f"0x{size:X} (expected 0x{expected_bytes:X})")
    output.write_bytes(process.read(begin, size))
    return {
        "vector": f"0x{address:X}",
        "data": f"0x{begin:X}",
        "bytes": size,
        "output": str(output),
    }


def main() -> int:
    if len(sys.argv) != 6:
        raise SystemExit(
            "usage: dump_visibility_pair.py ORIGINAL_PID REBUILD_PID "
            "REBUILD_BASE LAYOUT_JSON OUTPUT_DIRECTORY")

    original = Process(parse_hex(sys.argv[1]))
    rebuild = Process(parse_hex(sys.argv[2]))
    rebuild_base = parse_hex(sys.argv[3])
    layout_path = Path(sys.argv[4])
    output_directory = Path(sys.argv[5])
    output_directory.mkdir(parents=True, exist_ok=True)
    layout = json.loads(layout_path.read_text(encoding="utf-8-sig"))

    runtime = rebuild_base + parse_hex(layout["runtime_rva"])
    visibility = runtime + parse_hex(layout["visibility_offset"])
    visibility_layout = {
        key: parse_hex(value)
        for key, value in layout["visibility_layout"].items()
    }

    result: dict[str, object] = {
        "original": {},
        "rebuild": {},
    }
    try:
        for name, address in ORIGINAL_LAYER_ADDRESSES.items():
            output = output_directory / f"original_visibility_{name}.bin"
            output.write_bytes(original.read(address, EXPECTED_LAYER_BYTES))
            result["original"][name] = {
                "address": f"0x{address:X}",
                "bytes": EXPECTED_LAYER_BYTES,
                "output": str(output),
            }

        width, height = struct.unpack("<II", rebuild.read(visibility, 8))
        if not (1 <= width <= 0x100 and 1 <= height <= 0x100):
            raise ValueError(
                f"unexpected rebuild visibility dimensions: {width}x{height}")
        result["rebuild"]["dimensions"] = [width, height]
        rebuild_layer_bytes = width * height * 4
        for name in ("current", "previous", "owner", "terrain_class_flags"):
            output = output_directory / f"rebuild_visibility_{name}.bin"
            result["rebuild"][name] = dump_rebuild_vector(
                rebuild, visibility + visibility_layout[name],
                rebuild_layer_bytes, output)

        print(json.dumps(result, indent=2))
        return 0
    finally:
        original.close()
        rebuild.close()


if __name__ == "__main__":
    raise SystemExit(main())
