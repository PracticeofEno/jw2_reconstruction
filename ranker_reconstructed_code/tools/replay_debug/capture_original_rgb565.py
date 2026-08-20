#!/usr/bin/env python3
"""Capture the original Ranker's logical RGB565 frame buffer."""

from __future__ import annotations

import ctypes
import json
from pathlib import Path
import struct
import sys


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit(
            "usage: capture_original_rgb565.py ORIGINAL_PID OUTPUT.rgb565")
    pid = int(sys.argv[1], 0)
    output = Path(sys.argv[2]).resolve()
    repository_root = Path(__file__).resolve().parents[3]
    artifact_root = (repository_root / "ranker_reconstructed_code" / "tools" /
                     "replay_debug" / "artifacts").resolve()
    try:
        output.relative_to(artifact_root)
    except ValueError as exc:
        raise ValueError(
            f"raw output must be below {artifact_root}: {output}") from exc

    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.OpenProcess.restype = ctypes.c_void_p
    handle = kernel32.OpenProcess(0x0410, False, pid)
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())

    def read(address: int, size: int) -> bytes:
        data = ctypes.create_string_buffer(size)
        transferred = ctypes.c_size_t()
        if not kernel32.ReadProcessMemory(
                handle, ctypes.c_void_p(address), data, size,
                ctypes.byref(transferred)) or transferred.value != size:
            raise ctypes.WinError(ctypes.get_last_error())
        return data.raw

    try:
        pixels = struct.unpack("<I", read(0x01459070, 4))[0]
        pitch = struct.unpack("<I", read(0x0143FFF0, 4))[0]
        width = 800
        height = 600
        if not width <= pitch <= 16384:
            raise ValueError(f"invalid original pitch: {pitch}")
        frame = bytearray(width * height * 2)
        for y in range(height):
            row = read(pixels + y * pitch * 2, width * 2)
            offset = y * width * 2
            frame[offset:offset + len(row)] = row
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_bytes(frame)
        constants = {
            "pixel_mode": struct.unpack("<I", read(0x01450834, 4))[0],
            "green_mask": struct.unpack("<I", read(0x0143FFC8, 4))[0],
            "red_mask_a": struct.unpack("<I", read(0x01458050, 4))[0],
            "red_mask_b": struct.unpack("<I", read(0x0144B82C, 4))[0],
            "red_shift": struct.unpack("<I", read(0x01446020, 4))[0],
            "green_shift": struct.unpack("<I", read(0x01446024, 4))[0],
        }
        print(json.dumps({
            "output": str(output),
            "pixels": f"0x{pixels:X}",
            "pitch": pitch,
            "width": width,
            "height": height,
            "constants": constants,
        }))
        return 0
    finally:
        kernel32.CloseHandle(handle)


if __name__ == "__main__":
    raise SystemExit(main())
