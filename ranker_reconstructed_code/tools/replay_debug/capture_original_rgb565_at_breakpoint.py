#!/usr/bin/env python3
"""Capture Ranker's RGB565 buffer immediately after a completed render."""

from __future__ import annotations

import ctypes
from ctypes import wintypes
import json
from pathlib import Path
import struct
import sys
import time


EXCEPTION_DEBUG_EVENT = 1
EXCEPTION_BREAKPOINT = 0x80000003
EXCEPTION_SINGLE_STEP = 0x80000004
STATUS_WX86_BREAKPOINT = 0x4000001F
STATUS_WX86_SINGLE_STEP = 0x4000001E
DBG_CONTINUE = 0x00010002
DBG_EXCEPTION_NOT_HANDLED = 0x80010001
PAGE_EXECUTE_READWRITE = 0x40
WOW64_CONTEXT_CONTROL = 0x00010001
TARGET_ADDRESS = 0x004C11E2
COMPOSITE_ADDRESS = 0x004D7790
FRAME_ADDRESS = 0x007071A4


class ExceptionRecord(ctypes.Structure):
    _fields_ = [
        ("code", wintypes.DWORD),
        ("flags", wintypes.DWORD),
        ("record", ctypes.c_void_p),
        ("address", ctypes.c_void_p),
        ("parameter_count", wintypes.DWORD),
        ("information", ctypes.c_size_t * 15),
    ]


class ExceptionDebugInfo(ctypes.Structure):
    _fields_ = [
        ("record", ExceptionRecord),
        ("first_chance", wintypes.DWORD),
    ]


class DebugEventUnion(ctypes.Union):
    _fields_ = [
        ("exception", ExceptionDebugInfo),
        ("raw", ctypes.c_byte * 160),
    ]


class DebugEvent(ctypes.Structure):
    _fields_ = [
        ("code", wintypes.DWORD),
        ("process_id", wintypes.DWORD),
        ("thread_id", wintypes.DWORD),
        ("data", DebugEventUnion),
    ]


class FloatingSaveArea(ctypes.Structure):
    _fields_ = [
        ("control_word", wintypes.DWORD),
        ("status_word", wintypes.DWORD),
        ("tag_word", wintypes.DWORD),
        ("error_offset", wintypes.DWORD),
        ("error_selector", wintypes.DWORD),
        ("data_offset", wintypes.DWORD),
        ("data_selector", wintypes.DWORD),
        ("register_area", ctypes.c_byte * 80),
        ("cr0_npx_state", wintypes.DWORD),
    ]


class Wow64Context(ctypes.Structure):
    _fields_ = [
        ("context_flags", wintypes.DWORD),
        ("dr0", wintypes.DWORD),
        ("dr1", wintypes.DWORD),
        ("dr2", wintypes.DWORD),
        ("dr3", wintypes.DWORD),
        ("dr6", wintypes.DWORD),
        ("dr7", wintypes.DWORD),
        ("float_save", FloatingSaveArea),
        ("seg_gs", wintypes.DWORD),
        ("seg_fs", wintypes.DWORD),
        ("seg_es", wintypes.DWORD),
        ("seg_ds", wintypes.DWORD),
        ("edi", wintypes.DWORD),
        ("esi", wintypes.DWORD),
        ("ebx", wintypes.DWORD),
        ("edx", wintypes.DWORD),
        ("ecx", wintypes.DWORD),
        ("eax", wintypes.DWORD),
        ("ebp", wintypes.DWORD),
        ("eip", wintypes.DWORD),
        ("seg_cs", wintypes.DWORD),
        ("eflags", wintypes.DWORD),
        ("esp", wintypes.DWORD),
        ("seg_ss", wintypes.DWORD),
        ("extended_registers", ctypes.c_byte * 512),
    ]


def main() -> int:
    if len(sys.argv) < 4:
        raise SystemExit(
            "usage: capture_original_rgb565_at_breakpoint.py PID FRAME "
            "OUTPUT.rgb565 [--surface-only] [--world-before-overlay] "
            "[--composite-once] [--keep-alive] "
            "[--presentation-seed SEED]")
    pid = int(sys.argv[1], 0)
    target_frame = int(sys.argv[2], 0)
    output = Path(sys.argv[3]).resolve()
    capture_arguments = list(sys.argv[4:])
    presentation_seed: int | None = None
    if "--presentation-seed" in capture_arguments:
        option_index = capture_arguments.index("--presentation-seed")
        if option_index + 1 >= len(capture_arguments):
            raise ValueError("--presentation-seed requires a value")
        presentation_seed = int(capture_arguments[option_index + 1], 0)
        del capture_arguments[option_index:option_index + 2]
    options = set(capture_arguments)
    known_options = {
        "--surface-only", "--world-before-overlay", "--composite-once",
        "--keep-alive",
    }
    unknown_options = options - known_options
    if unknown_options:
        raise ValueError(f"unknown capture options: {sorted(unknown_options)}")
    surface_only = "--surface-only" in options
    composite_once = "--composite-once" in options
    keep_alive = "--keep-alive" in options
    if composite_once and "--world-before-overlay" in options:
        raise ValueError(
            "--composite-once and --world-before-overlay are mutually exclusive")
    target_address = (COMPOSITE_ADDRESS if composite_once else
                      (0x004D781D if "--world-before-overlay" in options else
                       TARGET_ADDRESS))
    repository_root = Path(__file__).resolve().parents[3]
    artifact_root = (repository_root / "ranker_reconstructed_code" / "tools" /
                     "replay_debug" / "artifacts").resolve()
    try:
        output.relative_to(artifact_root)
    except ValueError as exc:
        raise ValueError(
            f"raw output must be below {artifact_root}: {output}") from exc

    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    ntdll = ctypes.WinDLL("ntdll", use_last_error=True)
    kernel32.OpenProcess.restype = ctypes.c_void_p
    kernel32.OpenThread.restype = ctypes.c_void_p
    process = kernel32.OpenProcess(0x001F0FFF, False, pid)
    if not process:
        raise ctypes.WinError(ctypes.get_last_error())

    def read(address: int, size: int) -> bytes:
        data = ctypes.create_string_buffer(size)
        transferred = ctypes.c_size_t()
        if not kernel32.ReadProcessMemory(
                process, ctypes.c_void_p(address), data, size,
                ctypes.byref(transferred)) or transferred.value != size:
            raise ctypes.WinError(ctypes.get_last_error())
        return data.raw

    def write(address: int, data: bytes) -> None:
        transferred = ctypes.c_size_t()
        if not kernel32.WriteProcessMemory(
                process, ctypes.c_void_p(address), data, len(data),
                ctypes.byref(transferred)) or transferred.value != len(data):
            raise ctypes.WinError(ctypes.get_last_error())

    original_byte = read(target_address, 1)
    old_protection = wintypes.DWORD()
    if not kernel32.VirtualProtectEx(
            process, ctypes.c_void_p(target_address), 1,
            PAGE_EXECUTE_READWRITE, ctypes.byref(old_protection)):
        raise ctypes.WinError(ctypes.get_last_error())
    write(target_address, b"\xCC")
    kernel32.FlushInstructionCache(
        process, ctypes.c_void_p(target_address), 1)

    attached = False
    stepping_thread = 0
    return_address = 0
    return_byte = b""
    return_thread = 0
    captured_frame = 0
    camera_before: tuple[int, int] | None = None
    captured = False
    event_counts: dict[int, int] = {}
    exception_events: list[tuple[int, int]] = []
    deadline = time.monotonic() + 30.0
    try:
        if not kernel32.DebugActiveProcess(pid):
            raise ctypes.WinError(ctypes.get_last_error())
        attached = True
        kernel32.DebugSetProcessKillOnExit(not keep_alive)
        for _ in range(8):
            ntdll.NtResumeProcess(process)

        event = DebugEvent()
        while time.monotonic() < deadline:
            if not kernel32.WaitForDebugEvent(ctypes.byref(event), 1000):
                error = ctypes.get_last_error()
                if error == 121:  # ERROR_SEM_TIMEOUT
                    continue
                raise ctypes.WinError(error)
            continue_status = DBG_CONTINUE
            event_counts[event.code] = event_counts.get(event.code, 0) + 1
            exception_code = 0
            exception_address = 0
            if event.code == EXCEPTION_DEBUG_EVENT:
                exception_code = event.data.exception.record.code
                exception_address = int(
                    event.data.exception.record.address or 0)
                if len(exception_events) < 32:
                    exception_events.append(
                        (exception_code, exception_address))
                    print(
                        f"DEBUG_EXCEPTION code=0x{exception_code:08X} "
                        f"address=0x{exception_address:X}",
                        file=sys.stderr, flush=True)

            if (event.code == EXCEPTION_DEBUG_EVENT and
                    exception_code in (
                        EXCEPTION_BREAKPOINT, STATUS_WX86_BREAKPOINT) and
                    return_address != 0 and
                    exception_address == return_address and
                    event.thread_id == return_thread):
                frame = captured_frame
                pixels = struct.unpack("<I", read(0x01459070, 4))[0]
                pitch = struct.unpack("<I", read(0x0143FFF0, 4))[0]
                width = 800
                height = 600
                if not width <= pitch <= 16384:
                    raise ValueError(f"invalid original pitch: {pitch}")
                payload = bytearray(width * height * 2)
                for y in range(height):
                    row = read(pixels + y * pitch * 2, width * 2)
                    start = y * width * 2
                    payload[start:start + len(row)] = row
                output.parent.mkdir(parents=True, exist_ok=True)
                output.write_bytes(payload)
                camera_after = struct.unpack("<ii", read(0x007071A8, 8))
                print(json.dumps({
                    "output": str(output),
                    "frame": frame,
                    "pixels": f"0x{pixels:X}",
                    "pitch": pitch,
                    "width": width,
                    "height": height,
                    "camera_before": list(camera_before or (0, 0)),
                    "camera": list(camera_after),
                    "breakpoint": f"0x{target_address:X}",
                    "return_breakpoint": f"0x{return_address:X}",
                    "composite_once": True,
                    "presentation_seed": presentation_seed,
                }))
                captured = True
                if keep_alive:
                    thread = kernel32.OpenThread(
                        0x001F03FF, False, event.thread_id)
                    if not thread:
                        raise ctypes.WinError(ctypes.get_last_error())
                    try:
                        context = Wow64Context()
                        context.context_flags = WOW64_CONTEXT_CONTROL
                        if not kernel32.Wow64GetThreadContext(
                                thread, ctypes.byref(context)):
                            raise ctypes.WinError(ctypes.get_last_error())
                        write(return_address, return_byte)
                        kernel32.FlushInstructionCache(
                            process, ctypes.c_void_p(return_address), 1)
                        context.eip -= 1
                        if not kernel32.Wow64SetThreadContext(
                                thread, ctypes.byref(context)):
                            raise ctypes.WinError(ctypes.get_last_error())
                        ntdll.NtSuspendProcess(process)
                    finally:
                        kernel32.CloseHandle(thread)
                else:
                    kernel32.TerminateProcess(process, 0)
            elif (event.code == EXCEPTION_DEBUG_EVENT and
                    exception_code in (
                        EXCEPTION_SINGLE_STEP, STATUS_WX86_SINGLE_STEP) and
                    event.thread_id == stepping_thread):
                write(target_address, b"\xCC")
                kernel32.FlushInstructionCache(
                    process, ctypes.c_void_p(target_address), 1)
                stepping_thread = 0
            elif (event.code == EXCEPTION_DEBUG_EVENT and
                  exception_code in (
                      EXCEPTION_BREAKPOINT, STATUS_WX86_BREAKPOINT) and
                  exception_address == target_address):
                frame = struct.unpack("<I", read(FRAME_ADDRESS, 4))[0]
                if frame >= target_frame:
                    if composite_once:
                        if presentation_seed is not None:
                            write(0x007071C4, struct.pack(
                                "<I", presentation_seed & 0xFFFFFFFF))
                        thread = kernel32.OpenThread(
                            0x001F03FF, False, event.thread_id)
                        if not thread:
                            raise ctypes.WinError(ctypes.get_last_error())
                        try:
                            context = Wow64Context()
                            context.context_flags = WOW64_CONTEXT_CONTROL
                            if not kernel32.Wow64GetThreadContext(
                                    thread, ctypes.byref(context)):
                                raise ctypes.WinError(ctypes.get_last_error())
                            return_address = struct.unpack(
                                "<I", read(context.esp, 4))[0]
                            return_byte = read(return_address, 1)
                            return_protection = wintypes.DWORD()
                            if not kernel32.VirtualProtectEx(
                                    process, ctypes.c_void_p(return_address), 1,
                                    PAGE_EXECUTE_READWRITE,
                                    ctypes.byref(return_protection)):
                                raise ctypes.WinError(ctypes.get_last_error())
                            write(return_address, b"\xCC")
                            write(target_address, original_byte)
                            kernel32.FlushInstructionCache(process, None, 0)
                            camera_before = struct.unpack(
                                "<ii", read(0x007071A8, 8))
                            captured_frame = frame
                            return_thread = event.thread_id
                            context.eip -= 1
                            if not kernel32.Wow64SetThreadContext(
                                    thread, ctypes.byref(context)):
                                raise ctypes.WinError(ctypes.get_last_error())
                        finally:
                            kernel32.CloseHandle(thread)
                        kernel32.ContinueDebugEvent(
                            event.process_id, event.thread_id, DBG_CONTINUE)
                        continue
                    pixels = struct.unpack(
                        "<I", read(0x01459070, 4))[0]
                    pitch = struct.unpack(
                        "<I", read(0x0143FFF0, 4))[0]
                    width = 800
                    height = 600
                    if not width <= pitch <= 16384:
                        raise ValueError(f"invalid original pitch: {pitch}")
                    payload = bytearray(width * height * 2)
                    for y in range(height):
                        row = read(pixels + y * pitch * 2, width * 2)
                        start = y * width * 2
                        payload[start:start + len(row)] = row
                    output.parent.mkdir(parents=True, exist_ok=True)
                    output.write_bytes(payload)
                    tile_output = None
                    map1_output = None
                    map2_output = None
                    tile_count = 0
                    if not surface_only:
                        tile_pixels = struct.unpack(
                            "<I", read(0x010F462C, 4))[0]
                        tile_count = struct.unpack(
                            "<I", read(0x010F4628, 4))[0]
                        tile_output = output.with_suffix(
                            output.suffix + ".tiles")
                        tile_output.write_bytes(
                            read(tile_pixels, tile_count * 0x800))
                        map1_output = output.with_suffix(
                            output.suffix + ".map1")
                        map2_output = output.with_suffix(
                            output.suffix + ".map2")
                        map1_output.write_bytes(read(0x00E99E74, 0x40000))
                        map2_output.write_bytes(read(0x00ED9E74, 0x40000))
                    camera_x, camera_y = struct.unpack(
                        "<ii", read(0x007071A8, 8))
                    print(json.dumps({
                        "output": str(output),
                        "frame": frame,
                        "pixels": f"0x{pixels:X}",
                        "pitch": pitch,
                        "width": width,
                        "height": height,
                        "camera": [camera_x, camera_y],
                        "breakpoint": f"0x{target_address:X}",
                        "tile_output": str(tile_output) if tile_output else "",
                        "tile_count": tile_count,
                        "map1_output": str(map1_output) if map1_output else "",
                        "map2_output": str(map2_output) if map2_output else "",
                    }))
                    captured = True
                    if keep_alive:
                        write(target_address, original_byte)
                        kernel32.FlushInstructionCache(
                            process, ctypes.c_void_p(target_address), 1)
                        thread = kernel32.OpenThread(
                            0x001F03FF, False, event.thread_id)
                        if not thread:
                            raise ctypes.WinError(ctypes.get_last_error())
                        try:
                            context = Wow64Context()
                            context.context_flags = WOW64_CONTEXT_CONTROL
                            if not kernel32.Wow64GetThreadContext(
                                    thread, ctypes.byref(context)):
                                raise ctypes.WinError(ctypes.get_last_error())
                            context.eip -= 1
                            if not kernel32.Wow64SetThreadContext(
                                    thread, ctypes.byref(context)):
                                raise ctypes.WinError(ctypes.get_last_error())
                        finally:
                            kernel32.CloseHandle(thread)
                        ntdll.NtSuspendProcess(process)
                    else:
                        kernel32.TerminateProcess(process, 0)
                else:
                    write(target_address, original_byte)
                    kernel32.FlushInstructionCache(
                        process, ctypes.c_void_p(target_address), 1)
                    thread = kernel32.OpenThread(
                        0x001F03FF, False, event.thread_id)
                    if not thread:
                        raise ctypes.WinError(ctypes.get_last_error())
                    try:
                        context = Wow64Context()
                        context.context_flags = WOW64_CONTEXT_CONTROL
                        if not kernel32.Wow64GetThreadContext(
                                thread, ctypes.byref(context)):
                            raise ctypes.WinError(ctypes.get_last_error())
                        context.eip -= 1
                        context.eflags |= 0x100
                        if not kernel32.Wow64SetThreadContext(
                                thread, ctypes.byref(context)):
                            raise ctypes.WinError(ctypes.get_last_error())
                        stepping_thread = event.thread_id
                    finally:
                        kernel32.CloseHandle(thread)
            elif event.code == EXCEPTION_DEBUG_EVENT:
                continue_status = DBG_CONTINUE if (
                    exception_code in (
                        EXCEPTION_BREAKPOINT, STATUS_WX86_BREAKPOINT)) else (
                    DBG_EXCEPTION_NOT_HANDLED)

            kernel32.ContinueDebugEvent(
                event.process_id, event.thread_id, continue_status)
            if captured:
                return 0
        try:
            current_frame: int | str = struct.unpack(
                "<I", read(FRAME_ADDRESS, 4))[0]
        except OSError as exc:
            current_frame = f"unreadable:{exc}"
        raise TimeoutError(
            f"render-end breakpoint did not reach frame {target_frame}; "
            f"current={current_frame} events={event_counts} "
            f"exceptions={exception_events}")
    finally:
        if not captured:
            try:
                write(target_address, original_byte)
                if return_address != 0 and return_byte:
                    write(return_address, return_byte)
                kernel32.FlushInstructionCache(
                    process, ctypes.c_void_p(target_address), 1)
            except OSError:
                pass
        if attached:
            kernel32.DebugActiveProcessStop(pid)
        kernel32.CloseHandle(process)


if __name__ == "__main__":
    raise SystemExit(main())
