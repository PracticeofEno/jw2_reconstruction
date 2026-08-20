#!/usr/bin/env python3
"""Capture the rebuilt logical RGB565 buffer after a completed composite."""

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
DBG_CONTINUE = 0x00010002
DBG_EXCEPTION_NOT_HANDLED = 0x80010001
PAGE_EXECUTE_READWRITE = 0x40
CONTEXT_AMD64_CONTROL_AND_INTEGER = 0x00100003


class ExceptionRecord(ctypes.Structure):
    _fields_ = [
        ("code", wintypes.DWORD), ("flags", wintypes.DWORD),
        ("record", ctypes.c_void_p), ("address", ctypes.c_void_p),
        ("parameter_count", wintypes.DWORD),
        ("information", ctypes.c_size_t * 15),
    ]


class ExceptionDebugInfo(ctypes.Structure):
    _fields_ = [("record", ExceptionRecord), ("first_chance", wintypes.DWORD)]


class DebugEventUnion(ctypes.Union):
    _fields_ = [("exception", ExceptionDebugInfo), ("raw", ctypes.c_byte * 160)]


class DebugEvent(ctypes.Structure):
    _fields_ = [
        ("code", wintypes.DWORD), ("process_id", wintypes.DWORD),
        ("thread_id", wintypes.DWORD), ("data", DebugEventUnion),
    ]


class Amd64ControlContext(ctypes.Structure):
    _fields_ = [
        ("p1_home", ctypes.c_uint64), ("p2_home", ctypes.c_uint64),
        ("p3_home", ctypes.c_uint64), ("p4_home", ctypes.c_uint64),
        ("p5_home", ctypes.c_uint64), ("p6_home", ctypes.c_uint64),
        ("context_flags", wintypes.DWORD), ("mx_csr", wintypes.DWORD),
        ("seg_cs", ctypes.c_uint16), ("seg_ds", ctypes.c_uint16),
        ("seg_es", ctypes.c_uint16), ("seg_fs", ctypes.c_uint16),
        ("seg_gs", ctypes.c_uint16), ("seg_ss", ctypes.c_uint16),
        ("eflags", wintypes.DWORD),
        ("dr0", ctypes.c_uint64), ("dr1", ctypes.c_uint64),
        ("dr2", ctypes.c_uint64), ("dr3", ctypes.c_uint64),
        ("dr6", ctypes.c_uint64), ("dr7", ctypes.c_uint64),
        ("rax", ctypes.c_uint64), ("rcx", ctypes.c_uint64),
        ("rdx", ctypes.c_uint64), ("rbx", ctypes.c_uint64),
        ("rsp", ctypes.c_uint64), ("rbp", ctypes.c_uint64),
        ("rsi", ctypes.c_uint64), ("rdi", ctypes.c_uint64),
        ("r8", ctypes.c_uint64), ("r9", ctypes.c_uint64),
        ("r10", ctypes.c_uint64), ("r11", ctypes.c_uint64),
        ("r12", ctypes.c_uint64), ("r13", ctypes.c_uint64),
        ("r14", ctypes.c_uint64), ("r15", ctypes.c_uint64),
        ("rip", ctypes.c_uint64),
        ("remaining", ctypes.c_byte * (0x4D0 - 0x100)),
    ]


def main() -> int:
    if len(sys.argv) < 8:
        raise SystemExit(
            "usage: capture_rebuild_rgb565_at_breakpoint.py PID REBUILD_BASE "
            "FRAME_ADDRESS TARGET_FRAME COMPOSITE_RVA SPRITE_STATE_RVA "
            "OUTPUT.rgb565 [--entry-checkpoint [CAMERA_ADDRESS] | "
            "--camera-address CAMERA_ADDRESS] [--keep-alive] "
            "[--presentation-seed-address ADDRESS "
            "--presentation-seed SEED]")
    pid = int(sys.argv[1], 0)
    rebuild_base = int(sys.argv[2], 0)
    frame_address = int(sys.argv[3], 0)
    target_frame = int(sys.argv[4], 0)
    composite_address = rebuild_base + int(sys.argv[5], 0)
    sprite_state_address = rebuild_base + int(sys.argv[6], 0)
    output = Path(sys.argv[7]).resolve()
    entry_checkpoint = False
    camera_address = 0
    capture_arguments = list(sys.argv[8:])
    keep_alive = "--keep-alive" in capture_arguments
    capture_arguments = [
        argument for argument in capture_arguments
        if argument != "--keep-alive"
    ]
    presentation_seed_address = 0
    presentation_seed: int | None = None
    if "--presentation-seed-address" in capture_arguments:
        option_index = capture_arguments.index("--presentation-seed-address")
        if option_index + 1 >= len(capture_arguments):
            raise ValueError("--presentation-seed-address requires a value")
        presentation_seed_address = int(
            capture_arguments[option_index + 1], 0)
        del capture_arguments[option_index:option_index + 2]
    if "--presentation-seed" in capture_arguments:
        option_index = capture_arguments.index("--presentation-seed")
        if option_index + 1 >= len(capture_arguments):
            raise ValueError("--presentation-seed requires a value")
        presentation_seed = int(capture_arguments[option_index + 1], 0)
        del capture_arguments[option_index:option_index + 2]
    if (presentation_seed_address == 0) != (presentation_seed is None):
        raise ValueError(
            "presentation seed address and value must be supplied together")
    if capture_arguments:
        if capture_arguments[0] == "--entry-checkpoint":
            entry_checkpoint = True
            if len(capture_arguments) == 2:
                camera_address = int(capture_arguments[1], 0)
            elif len(capture_arguments) != 1:
                raise ValueError(
                    f"unknown capture option: {capture_arguments}")
        elif (capture_arguments[0] == "--camera-address" and
              len(capture_arguments) == 2):
            camera_address = int(capture_arguments[1], 0)
        else:
            raise ValueError(f"unknown capture option: {capture_arguments}")
    repository_root = Path(__file__).resolve().parents[3]
    artifact_root = (repository_root / "ranker_reconstructed_code" / "tools" /
                     "replay_debug" / "artifacts").resolve()
    try:
        output.relative_to(artifact_root)
    except ValueError as error:
        raise ValueError(
            f"raw output must stay below {artifact_root}: {output}") from error

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

    def read_u32(address: int) -> int:
        return struct.unpack("<I", read(address, 4))[0]

    def write(address: int, data: bytes) -> None:
        transferred = ctypes.c_size_t()
        if not kernel32.WriteProcessMemory(
                process, ctypes.c_void_p(address), data, len(data),
                ctypes.byref(transferred)) or transferred.value != len(data):
            raise ctypes.WinError(ctypes.get_last_error())

    def make_writable(address: int) -> None:
        old_protection = wintypes.DWORD()
        if not kernel32.VirtualProtectEx(
                process, ctypes.c_void_p(address), 1,
                PAGE_EXECUTE_READWRITE, ctypes.byref(old_protection)):
            raise ctypes.WinError(ctypes.get_last_error())

    camera_before: tuple[int, int] | None = None

    def capture_surface(frame: int, return_breakpoint: int = 0) -> None:
        target = read(sprite_state_address, 24)
        pixels, width, height, pitch = struct.unpack_from("<QIII", target)
        if not (1 <= width <= 4096 and 1 <= height <= 4096 and
                width <= pitch <= 16384 and pixels != 0):
            raise ValueError(
                f"invalid reconstructed target {width}x{height} "
                f"pitch={pitch} pixels=0x{pixels:X}")
        payload = bytearray(width * height * 2)
        for y in range(height):
            row = read(pixels + y * pitch * 2, width * 2)
            start = y * width * 2
            payload[start:start + len(row)] = row
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_bytes(payload)
        result = {
            "output": str(output),
            "frame": frame,
            "pixels": f"0x{pixels:X}",
            "pitch": pitch,
            "width": width,
            "height": height,
            "checkpoint_breakpoint": f"0x{composite_address:X}",
            "return_breakpoint": (
                f"0x{return_breakpoint:X}" if return_breakpoint else ""),
            "entry_checkpoint": entry_checkpoint,
            "presentation_seed": presentation_seed,
        }
        if camera_address:
            if camera_before is not None:
                result["camera_before"] = list(camera_before)
            result["camera"] = list(struct.unpack("<ii", read(camera_address, 8)))
        print(json.dumps(result))

    entry_byte = read(composite_address, 1)
    make_writable(composite_address)
    write(composite_address, b"\xCC")
    kernel32.FlushInstructionCache(
        process, ctypes.c_void_p(composite_address), 1)

    attached = False
    stepping_thread = 0
    return_address = 0
    return_byte = b""
    return_thread = 0
    captured = False
    captured_frame = 0
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
                if error == 121:
                    continue
                raise ctypes.WinError(error)
            continue_status = DBG_CONTINUE
            exception_code = 0
            exception_address = 0
            if event.code == EXCEPTION_DEBUG_EVENT:
                exception_code = event.data.exception.record.code
                exception_address = int(event.data.exception.record.address or 0)

            if (event.code == EXCEPTION_DEBUG_EVENT and
                    exception_code == EXCEPTION_BREAKPOINT and
                    return_address != 0 and
                    exception_address == return_address and
                    event.thread_id == return_thread):
                capture_surface(captured_frame, return_address)
                captured = True
                if keep_alive:
                    thread = kernel32.OpenThread(
                        0x001F03FF, False, event.thread_id)
                    if not thread:
                        raise ctypes.WinError(ctypes.get_last_error())
                    try:
                        context = Amd64ControlContext()
                        context.context_flags = CONTEXT_AMD64_CONTROL_AND_INTEGER
                        if not kernel32.GetThreadContext(
                                thread, ctypes.byref(context)):
                            raise ctypes.WinError(ctypes.get_last_error())
                        write(return_address, return_byte)
                        kernel32.FlushInstructionCache(
                            process, ctypes.c_void_p(return_address), 1)
                        context.rip -= 1
                        if not kernel32.SetThreadContext(
                                thread, ctypes.byref(context)):
                            raise ctypes.WinError(ctypes.get_last_error())
                        ntdll.NtSuspendProcess(process)
                    finally:
                        kernel32.CloseHandle(thread)
                else:
                    kernel32.TerminateProcess(process, 0)
            elif (event.code == EXCEPTION_DEBUG_EVENT and
                    exception_code == EXCEPTION_SINGLE_STEP and
                    event.thread_id == stepping_thread):
                write(composite_address, b"\xCC")
                kernel32.FlushInstructionCache(
                    process, ctypes.c_void_p(composite_address), 1)
                stepping_thread = 0
            elif (event.code == EXCEPTION_DEBUG_EVENT and
                  exception_code == EXCEPTION_BREAKPOINT and
                  exception_address == composite_address):
                thread = kernel32.OpenThread(0x001F03FF, False, event.thread_id)
                if not thread:
                    raise ctypes.WinError(ctypes.get_last_error())
                try:
                    context = Amd64ControlContext()
                    context.context_flags = CONTEXT_AMD64_CONTROL_AND_INTEGER
                    if not kernel32.GetThreadContext(thread, ctypes.byref(context)):
                        raise ctypes.WinError(ctypes.get_last_error())
                    frame = read_u32(frame_address)
                    if frame >= target_frame:
                        if entry_checkpoint:
                            capture_surface(frame)
                            captured = True
                            if keep_alive:
                                write(composite_address, entry_byte)
                                kernel32.FlushInstructionCache(
                                    process, ctypes.c_void_p(composite_address), 1)
                                context.rip -= 1
                                if not kernel32.SetThreadContext(
                                        thread, ctypes.byref(context)):
                                    raise ctypes.WinError(
                                        ctypes.get_last_error())
                                ntdll.NtSuspendProcess(process)
                            else:
                                kernel32.TerminateProcess(process, 0)
                        else:
                            if presentation_seed is not None:
                                write(presentation_seed_address, struct.pack(
                                    "<I", presentation_seed & 0xFFFFFFFF))
                            if camera_address:
                                camera_before = struct.unpack(
                                    "<ii", read(camera_address, 8))
                            return_address = struct.unpack(
                                "<Q", read(context.rsp, 8))[0]
                            return_byte = read(return_address, 1)
                            make_writable(return_address)
                            write(return_address, b"\xCC")
                            kernel32.FlushInstructionCache(
                                process, ctypes.c_void_p(return_address), 1)
                            write(composite_address, entry_byte)
                            kernel32.FlushInstructionCache(
                                process, ctypes.c_void_p(composite_address), 1)
                            context.rip -= 1
                            if not kernel32.SetThreadContext(
                                    thread, ctypes.byref(context)):
                                raise ctypes.WinError(ctypes.get_last_error())
                            return_thread = event.thread_id
                            captured_frame = frame
                    else:
                        write(composite_address, entry_byte)
                        kernel32.FlushInstructionCache(
                            process, ctypes.c_void_p(composite_address), 1)
                        context.rip -= 1
                        context.eflags |= 0x100
                        if not kernel32.SetThreadContext(
                                thread, ctypes.byref(context)):
                            raise ctypes.WinError(ctypes.get_last_error())
                        stepping_thread = event.thread_id
                finally:
                    kernel32.CloseHandle(thread)
            elif event.code == EXCEPTION_DEBUG_EVENT:
                continue_status = (DBG_CONTINUE if exception_code ==
                                   EXCEPTION_BREAKPOINT else
                                   DBG_EXCEPTION_NOT_HANDLED)

            kernel32.ContinueDebugEvent(
                event.process_id, event.thread_id, continue_status)
            if captured:
                return 0
        raise TimeoutError(
            f"composite breakpoint did not capture frame {target_frame}")
    finally:
        if not captured:
            try:
                write(composite_address, entry_byte)
                if return_address != 0 and return_byte:
                    write(return_address, return_byte)
                kernel32.FlushInstructionCache(process, None, 0)
            except OSError:
                pass
        if attached:
            kernel32.DebugActiveProcessStop(pid)
        kernel32.CloseHandle(process)


if __name__ == "__main__":
    raise SystemExit(main())
