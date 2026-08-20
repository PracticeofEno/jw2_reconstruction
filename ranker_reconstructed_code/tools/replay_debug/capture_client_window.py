#!/usr/bin/env python3
"""Capture a suspended Ranker client through its window DC."""

from __future__ import annotations

import argparse
import ctypes
import json
from pathlib import Path

from PIL import Image


class Rect(ctypes.Structure):
    _fields_ = [
        ("left", ctypes.c_long),
        ("top", ctypes.c_long),
        ("right", ctypes.c_long),
        ("bottom", ctypes.c_long),
    ]


class BitmapInfoHeader(ctypes.Structure):
    _fields_ = [
        ("size", ctypes.c_uint32),
        ("width", ctypes.c_long),
        ("height", ctypes.c_long),
        ("planes", ctypes.c_uint16),
        ("bit_count", ctypes.c_uint16),
        ("compression", ctypes.c_uint32),
        ("image_size", ctypes.c_uint32),
        ("x_pixels_per_meter", ctypes.c_long),
        ("y_pixels_per_meter", ctypes.c_long),
        ("colors_used", ctypes.c_uint32),
        ("colors_important", ctypes.c_uint32),
    ]


class BitmapInfo(ctypes.Structure):
    _fields_ = [
        ("header", BitmapInfoHeader),
        ("colors", ctypes.c_uint32 * 3),
    ]


def checked_handle(value: int | None, label: str) -> int:
    if not value:
        raise ctypes.WinError(ctypes.get_last_error())
    return value


def capture_client(hwnd: int, output: Path) -> tuple[int, int]:
    user32 = ctypes.WinDLL("user32", use_last_error=True)
    gdi32 = ctypes.WinDLL("gdi32", use_last_error=True)
    user32.GetClientRect.argtypes = [ctypes.c_void_p, ctypes.POINTER(Rect)]
    user32.GetClientRect.restype = ctypes.c_int
    user32.GetDC.argtypes = [ctypes.c_void_p]
    user32.GetDC.restype = ctypes.c_void_p
    user32.ReleaseDC.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    user32.ReleaseDC.restype = ctypes.c_int
    gdi32.CreateCompatibleDC.argtypes = [ctypes.c_void_p]
    gdi32.CreateCompatibleDC.restype = ctypes.c_void_p
    gdi32.CreateCompatibleBitmap.argtypes = [
        ctypes.c_void_p, ctypes.c_int, ctypes.c_int]
    gdi32.CreateCompatibleBitmap.restype = ctypes.c_void_p
    gdi32.SelectObject.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    gdi32.SelectObject.restype = ctypes.c_void_p
    gdi32.BitBlt.argtypes = [
        ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int,
        ctypes.c_int, ctypes.c_void_p, ctypes.c_int, ctypes.c_int,
        ctypes.c_uint32]
    gdi32.BitBlt.restype = ctypes.c_int
    gdi32.GetDIBits.argtypes = [
        ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32,
        ctypes.c_void_p, ctypes.POINTER(BitmapInfo), ctypes.c_uint32]
    gdi32.GetDIBits.restype = ctypes.c_int
    gdi32.DeleteObject.argtypes = [ctypes.c_void_p]
    gdi32.DeleteObject.restype = ctypes.c_int
    gdi32.DeleteDC.argtypes = [ctypes.c_void_p]
    gdi32.DeleteDC.restype = ctypes.c_int

    rect = Rect()
    if not user32.GetClientRect(ctypes.c_void_p(hwnd), ctypes.byref(rect)):
        raise ctypes.WinError(ctypes.get_last_error())
    width = rect.right - rect.left
    height = rect.bottom - rect.top
    if width <= 0 or height <= 0:
        raise ValueError(f"invalid client extent: {width}x{height}")

    window_dc = checked_handle(
        user32.GetDC(ctypes.c_void_p(hwnd)), "GetDC")
    memory_dc = checked_handle(gdi32.CreateCompatibleDC(window_dc),
                               "CreateCompatibleDC")
    bitmap = checked_handle(
        gdi32.CreateCompatibleBitmap(window_dc, width, height),
        "CreateCompatibleBitmap")
    previous = checked_handle(gdi32.SelectObject(memory_dc, bitmap),
                              "SelectObject")
    try:
        if not gdi32.BitBlt(
                memory_dc, 0, 0, width, height, window_dc, 0, 0,
                0x00CC0020):
            raise ctypes.WinError(ctypes.get_last_error())

        info = BitmapInfo()
        info.header.size = ctypes.sizeof(BitmapInfoHeader)
        info.header.width = width
        info.header.height = -height
        info.header.planes = 1
        info.header.bit_count = 32
        info.header.compression = 0
        pixels = ctypes.create_string_buffer(width * height * 4)
        copied = gdi32.GetDIBits(
            memory_dc, bitmap, 0, height, pixels, ctypes.byref(info), 0)
        if copied != height:
            raise ctypes.WinError(ctypes.get_last_error())

        image = Image.frombuffer(
            "RGB", (width, height), pixels.raw, "raw", "BGRX", 0, 1)
        image.save(output, format="PNG")
    finally:
        gdi32.SelectObject(memory_dc, previous)
        gdi32.DeleteObject(bitmap)
        gdi32.DeleteDC(memory_dc)
        user32.ReleaseDC(ctypes.c_void_p(hwnd), window_dc)
    return width, height


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("window", type=lambda value: int(value, 0))
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    repository_root = Path(__file__).resolve().parents[3]
    png_root = (repository_root / "debug_artifacts" / "png").resolve()
    output = args.output.resolve()
    try:
        output.relative_to(png_root)
    except ValueError as exc:
        raise ValueError(
            f"capture output must be below {png_root}: {output}") from exc
    output.parent.mkdir(parents=True, exist_ok=True)

    width, height = capture_client(args.window, output)
    print(json.dumps({
        "output": str(output),
        "width": width,
        "height": height,
    }))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
