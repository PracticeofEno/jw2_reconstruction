#!/usr/bin/env python3
"""Read a reconstructed unit's resolved animation draw command."""

from __future__ import annotations

import ctypes
from ctypes import wintypes
import hashlib
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
LINK_BASE = 0x140000000
UNIT_ANIMATION_DRAW = 0x14025A5F0
UNIT_FRAME_RESOURCE_LOOKUP = 0x1400E6430
RESOURCE_STORE_STATE = 0x140C5B7A0
RESOURCE_STORE_ENTRY_SIZE = 64
PALETTE_CACHE_STATE = 0x140883B80
PALETTE_RAW_SLOTS_BYTES = 0x80000
PALETTE_RAW_SLOT_BYTES = 0x400
PALETTE_PIXEL_SLOT_BYTES = 0x200


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


def signed_i32(value: int) -> int:
    return struct.unpack("<i", struct.pack("<I", value & 0xFFFFFFFF))[0]


def main() -> int:
    if len(sys.argv) != 9:
        raise SystemExit(
            "usage: trace_rebuild_unit_sprite_entry.py PID REBUILD_BASE "
            "FRAME_ADDRESS FRAME MODE FILTER_A FILTER_B OUTPUT.json")
    pid = int(sys.argv[1], 0)
    rebuild_base = int(sys.argv[2], 0)
    frame_address = int(sys.argv[3], 0)
    target_frame = int(sys.argv[4], 0)
    mode = sys.argv[5]
    filter_a = int(sys.argv[6], 0)
    filter_b = int(sys.argv[7], 0)
    output = Path(sys.argv[8]).resolve()
    repository_root = Path(__file__).resolve().parents[3]
    artifact_root = (repository_root / "ranker_reconstructed_code" / "tools" /
                     "replay_debug" / "artifacts").resolve()
    try:
        output.relative_to(artifact_root)
    except ValueError as error:
        raise ValueError(
            f"trace output must stay below {artifact_root}: {output}") from error

    if mode == "animation":
        target_link_address = UNIT_ANIMATION_DRAW
    elif mode == "resource":
        target_link_address = UNIT_FRAME_RESOURCE_LOOKUP
    else:
        raise ValueError(f"unknown trace mode: {mode}")
    target_address = rebuild_base + target_link_address - LINK_BASE
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

    original_byte = read(target_address, 1)
    old_protection = wintypes.DWORD()
    if not kernel32.VirtualProtectEx(
            process, ctypes.c_void_p(target_address), 1,
            PAGE_EXECUTE_READWRITE, ctypes.byref(old_protection)):
        raise ctypes.WinError(ctypes.get_last_error())
    write(target_address, b"\xCC")
    kernel32.FlushInstructionCache(process, ctypes.c_void_p(target_address), 1)

    attached = False
    stepping_thread = 0
    captured = False
    return_address = 0
    return_original_byte = b""
    return_thread = 0
    pending_result = None
    deadline = time.monotonic() + 30.0
    try:
        if not kernel32.DebugActiveProcess(pid):
            raise ctypes.WinError(ctypes.get_last_error())
        attached = True
        kernel32.DebugSetProcessKillOnExit(True)
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
                    return_address != 0 and exception_address == return_address and
                    event.thread_id == return_thread):
                thread = kernel32.OpenThread(0x001F03FF, False, event.thread_id)
                if not thread:
                    raise ctypes.WinError(ctypes.get_last_error())
                try:
                    context = Amd64ControlContext()
                    context.context_flags = CONTEXT_AMD64_CONTROL_AND_INTEGER
                    if not kernel32.GetThreadContext(thread, ctypes.byref(context)):
                        raise ctypes.WinError(ctypes.get_last_error())
                    entry_index = context.rax & 0xFFFFFFFF
                    entry_address = (rebuild_base + RESOURCE_STORE_STATE - LINK_BASE +
                                     entry_index * RESOURCE_STORE_ENTRY_SIZE)
                    entry = read(entry_address, RESOURCE_STORE_ENTRY_SIZE)
                    metadata = list(struct.unpack_from("<6I", entry, 0))
                    palette_slot = struct.unpack_from("<I", entry, 24)[0]
                    payload_begin, payload_end, payload_capacity = struct.unpack_from(
                        "<QQQ", entry, 32)
                    if payload_end < payload_begin or payload_end - payload_begin > 0x1000000:
                        raise ValueError(
                            f"invalid resource payload vector: "
                            f"0x{payload_begin:X}..0x{payload_end:X}")
                    payload = read(payload_begin, payload_end - payload_begin)
                    output.parent.mkdir(parents=True, exist_ok=True)
                    payload_path = output.with_suffix(".resource.bin")
                    payload_path.write_bytes(payload)
                    palette_base = rebuild_base + PALETTE_CACHE_STATE - LINK_BASE
                    raw_palette = read(
                        palette_base + palette_slot * PALETTE_RAW_SLOT_BYTES,
                        PALETTE_RAW_SLOT_BYTES)
                    pixel_palette = read(
                        palette_base + PALETTE_RAW_SLOTS_BYTES +
                        palette_slot * PALETTE_PIXEL_SLOT_BYTES,
                        PALETTE_PIXEL_SLOT_BYTES)
                    raw_palette_path = output.with_suffix(".palette-rgba.bin")
                    pixel_palette_path = output.with_suffix(".palette565.bin")
                    raw_palette_path.write_bytes(raw_palette)
                    pixel_palette_path.write_bytes(pixel_palette)
                    result = dict(pending_result or {})
                    result.update({
                        "resource_entry": entry_index,
                        "resource_metadata": metadata,
                        "palette_slot": palette_slot,
                        "payload_size": len(payload),
                        "payload_sha256": hashlib.sha256(payload).hexdigest(),
                        "payload_path": str(payload_path),
                        "payload_capacity": payload_capacity - payload_begin,
                        "palette_rgba_sha256":
                            hashlib.sha256(raw_palette).hexdigest(),
                        "palette_rgba_path": str(raw_palette_path),
                        "palette565_sha256":
                            hashlib.sha256(pixel_palette).hexdigest(),
                        "palette565_path": str(pixel_palette_path),
                    })
                    output.write_text(
                        json.dumps(result, indent=2), encoding="utf-8")
                    print(json.dumps(result))
                    captured = True
                    kernel32.TerminateProcess(process, 0)
                finally:
                    kernel32.CloseHandle(thread)
            elif (event.code == EXCEPTION_DEBUG_EVENT and
                    exception_code == EXCEPTION_SINGLE_STEP and
                    event.thread_id == stepping_thread):
                write(target_address, b"\xCC")
                kernel32.FlushInstructionCache(
                    process, ctypes.c_void_p(target_address), 1)
                stepping_thread = 0
            elif (event.code == EXCEPTION_DEBUG_EVENT and
                  exception_code == EXCEPTION_BREAKPOINT and
                  exception_address == target_address):
                thread = kernel32.OpenThread(0x001F03FF, False, event.thread_id)
                if not thread:
                    raise ctypes.WinError(ctypes.get_last_error())
                try:
                    context = Amd64ControlContext()
                    context.context_flags = CONTEXT_AMD64_CONTROL_AND_INTEGER
                    if not kernel32.GetThreadContext(thread, ctypes.byref(context)):
                        raise ctypes.WinError(ctypes.get_last_error())
                    frame = read_u32(frame_address)
                    matched = False
                    result = {"frame": frame, "mode": mode}
                    if mode == "animation":
                        command_pointer = context.rdx
                        command = read(command_pointer, 48)
                        screen_x = struct.unpack_from("<i", command, 28)[0]
                        screen_y = struct.unpack_from("<i", command, 32)[0]
                        matched = (screen_x == filter_a and screen_y == filter_b)
                        result.update({
                            "unit_pointer":
                                f"0x{struct.unpack_from('<Q', command, 0)[0]:X}",
                            "sequence": struct.unpack_from("<I", command, 8)[0],
                            "kind": struct.unpack_from("<I", command, 12)[0],
                            "resource_frame": struct.unpack_from("<I", command, 16)[0],
                            "animation_frame": struct.unpack_from("<I", command, 20)[0],
                            "direction_row": struct.unpack_from("<I", command, 24)[0],
                            "screen": [screen_x, screen_y],
                            "flipped": command[36] != 0,
                            "resource_draw_mode": struct.unpack_from(
                                "<I", command, 40)[0],
                        })
                    else:
                        type_id = context.rcx & 0xFFFFFFFF
                        image_group = context.rdx & 0xFFFFFFFF
                        matched = type_id == filter_a and image_group == filter_b
                        result.update({
                            "type_id": type_id,
                            "image_group": image_group,
                            "frame_index": context.r8 & 0xFFFFFFFF,
                        })
                    if frame >= target_frame and matched:
                        result.update({
                            "breakpoint": f"0x{target_address:X}",
                            "breakpoint_rva": f"0x{target_address - rebuild_base:X}",
                        })
                        if mode == "resource":
                            return_address = struct.unpack(
                                "<Q", read(context.rsp, 8))[0]
                            return_original_byte = read(return_address, 1)
                            write(return_address, b"\xCC")
                            kernel32.FlushInstructionCache(
                                process, ctypes.c_void_p(return_address), 1)
                            write(target_address, original_byte)
                            kernel32.FlushInstructionCache(
                                process, ctypes.c_void_p(target_address), 1)
                            context.rip -= 1
                            if not kernel32.SetThreadContext(
                                    thread, ctypes.byref(context)):
                                raise ctypes.WinError(ctypes.get_last_error())
                            return_thread = event.thread_id
                            pending_result = result
                        else:
                            output.parent.mkdir(parents=True, exist_ok=True)
                            output.write_text(
                                json.dumps(result, indent=2), encoding="utf-8")
                            print(json.dumps(result))
                            captured = True
                            kernel32.TerminateProcess(process, 0)
                    else:
                        write(target_address, original_byte)
                        kernel32.FlushInstructionCache(
                            process, ctypes.c_void_p(target_address), 1)
                        context.rip -= 1
                        context.eflags |= 0x100
                        if not kernel32.SetThreadContext(
                                thread, ctypes.byref(context)):
                            raise ctypes.WinError(ctypes.get_last_error())
                        stepping_thread = event.thread_id
                finally:
                    kernel32.CloseHandle(thread)
            elif event.code == EXCEPTION_DEBUG_EVENT:
                continue_status = DBG_CONTINUE if exception_code == (
                    EXCEPTION_BREAKPOINT) else DBG_EXCEPTION_NOT_HANDLED

            kernel32.ContinueDebugEvent(
                event.process_id, event.thread_id, continue_status)
            if captured:
                return 0
        raise TimeoutError(
            f"rebuild {mode} breakpoint did not hit ({filter_a},{filter_b}) "
            f"at frame >={target_frame}")
    finally:
        if not captured:
            try:
                write(target_address, original_byte)
                kernel32.FlushInstructionCache(
                    process, ctypes.c_void_p(target_address), 1)
            except OSError:
                pass
            if return_address != 0 and return_original_byte:
                try:
                    write(return_address, return_original_byte)
                    kernel32.FlushInstructionCache(
                        process, ctypes.c_void_p(return_address), 1)
                except OSError:
                    pass
        if attached:
            kernel32.DebugActiveProcessStop(pid)
        kernel32.CloseHandle(process)


if __name__ == "__main__":
    raise SystemExit(main())
