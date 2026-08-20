#!/usr/bin/env python3
"""Compare the original and reconstructed logical RGB565 frame buffers."""

from __future__ import annotations

import collections
import ctypes
import json
import struct
import sys


class Process:
    def __init__(self, pid: int):
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel32.OpenProcess.restype = ctypes.c_void_p
        self._kernel32 = kernel32
        self.handle = kernel32.OpenProcess(0x0410, False, pid)
        if not self.handle:
            raise ctypes.WinError(ctypes.get_last_error())

    def read(self, address: int, size: int) -> bytes:
        data = ctypes.create_string_buffer(size)
        transferred = ctypes.c_size_t()
        if not self._kernel32.ReadProcessMemory(
                self.handle, ctypes.c_void_p(address), data, size,
                ctypes.byref(transferred)) or transferred.value != size:
            raise ctypes.WinError(ctypes.get_last_error())
        return data.raw

    def close(self) -> None:
        if self.handle:
            self._kernel32.CloseHandle(self.handle)
            self.handle = None


def read_rows(process: Process, pixels: int, pitch: int,
              width: int, height: int) -> list[int]:
    rows: list[int] = []
    for y in range(height):
        raw = process.read(pixels + y * pitch * 2, width * 2)
        rows.extend(struct.unpack(f"<{width}H", raw))
    return rows


def components(pixel: int) -> tuple[int, int, int]:
    return ((pixel >> 11) & 0x1F, (pixel >> 5) & 0x3F, pixel & 0x1F)


def main() -> int:
    if len(sys.argv) != 5:
        raise SystemExit(
            "usage: compare_rgb565_surfaces.py ORIGINAL_PID REBUILD_PID "
            "REBUILD_BASE SPRITE_STATE_RVA")
    original = Process(int(sys.argv[1], 0))
    rebuild = Process(int(sys.argv[2], 0))
    try:
        original_pixels = struct.unpack(
            "<I", original.read(0x01459070, 4))[0]
        original_pitch = struct.unpack(
            "<I", original.read(0x0143FFF0, 4))[0]

        rebuild_base = int(sys.argv[3], 0)
        sprite_state = rebuild_base + int(sys.argv[4], 0)
        target = rebuild.read(sprite_state, 24)
        rebuild_pixels, width, height, rebuild_pitch = struct.unpack_from(
            "<QIII", target)
        if not (1 <= width <= 4096 and 1 <= height <= 4096 and
                width <= rebuild_pitch <= 16384):
            raise ValueError(
                f"invalid rebuild target {width}x{height} pitch={rebuild_pitch}")
        if original_pitch < width or original_pitch > 16384:
            raise ValueError(f"invalid original pitch: {original_pitch}")

        lhs = read_rows(
            original, original_pixels, original_pitch, width, height)
        rhs = read_rows(
            rebuild, rebuild_pixels, rebuild_pitch, width, height)
        world_height = min(height, 444)
        pair_counts: collections.Counter[tuple[int, int]] = collections.Counter()
        component_deltas: collections.Counter[tuple[int, int, int]] = (
            collections.Counter())
        mismatches: list[dict[str, int | list[int]]] = []
        compared = 0
        exact = 0
        for y in range(world_height):
            for x in range(width):
                if x < 220 and y < 80:
                    continue
                index = y * width + x
                left = lhs[index]
                right = rhs[index]
                compared += 1
                if left == right:
                    exact += 1
                    continue
                pair_counts[(left, right)] += 1
                lc = components(left)
                rc = components(right)
                delta = tuple(rc[i] - lc[i] for i in range(3))
                component_deltas[delta] += 1
                if len(mismatches) < 32:
                    mismatches.append({
                        "x": x,
                        "y": y,
                        "original": left,
                        "rebuild": right,
                        "delta": list(delta),
                    })

        result = {
            "dimensions": [width, height],
            "world_height": world_height,
            "original": {
                "pixels": f"0x{original_pixels:X}",
                "pitch": original_pitch,
            },
            "rebuild": {
                "pixels": f"0x{rebuild_pixels:X}",
                "pitch": rebuild_pitch,
            },
            "compared": compared,
            "exact": exact,
            "mismatch": compared - exact,
            "exact_ratio": exact / compared if compared else 1.0,
            "component_deltas": [
                {"delta": list(delta), "count": count}
                for delta, count in component_deltas.most_common(20)
            ],
            "pixel_pairs": [
                {"original": left, "rebuild": right, "count": count}
                for (left, right), count in pair_counts.most_common(20)
            ],
            "first_mismatches": mismatches,
        }
        print(json.dumps(result, indent=2))
        return 0
    finally:
        original.close()
        rebuild.close()


if __name__ == "__main__":
    raise SystemExit(main())
