#!/usr/bin/env python3
"""Read the original unit sprite entry at its normal world-blit call."""

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
STATUS_WX86_BREAKPOINT = 0x4000001F
STATUS_WX86_SINGLE_STEP = 0x4000001E
DBG_CONTINUE = 0x00010002
DBG_EXCEPTION_NOT_HANDLED = 0x80010001
PAGE_EXECUTE_READWRITE = 0x40
WOW64_CONTEXT_CONTROL = 0x00010001
TARGET_ADDRESS = 0x004D2F5A
FRAME_ADDRESS = 0x007071A4
CURRENT_UNIT_OFFSET_ADDRESS = 0x0072C6B4
SPRITE_ENTRY_ADDRESS = 0x0072C6D8
SPRITE_SCREEN_POSITION_ADDRESS = 0x0072C6DC
ORIGINAL_UNIT_STRIDE = 0x1D0
ORIGINAL_UNIT_POOL_BASE = 0x00A03FB8
ORIGINAL_UNIT_RESOURCE_BASES = 0x00AEBFB8
ORIGINAL_UNIT_RESOURCE_BASE_STRIDE = 0x38
ORIGINAL_RESOURCE_TABLE_BASE = 0x014BADC0
ORIGINAL_RESOURCE_TABLE_STRIDE = 0x20


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


class FloatingSaveArea(ctypes.Structure):
    _fields_ = [
        ("control_word", wintypes.DWORD), ("status_word", wintypes.DWORD),
        ("tag_word", wintypes.DWORD), ("error_offset", wintypes.DWORD),
        ("error_selector", wintypes.DWORD), ("data_offset", wintypes.DWORD),
        ("data_selector", wintypes.DWORD),
        ("register_area", ctypes.c_byte * 80), ("cr0_npx_state", wintypes.DWORD),
    ]


class Wow64Context(ctypes.Structure):
    _fields_ = [
        ("context_flags", wintypes.DWORD),
        ("dr0", wintypes.DWORD), ("dr1", wintypes.DWORD),
        ("dr2", wintypes.DWORD), ("dr3", wintypes.DWORD),
        ("dr6", wintypes.DWORD), ("dr7", wintypes.DWORD),
        ("float_save", FloatingSaveArea),
        ("seg_gs", wintypes.DWORD), ("seg_fs", wintypes.DWORD),
        ("seg_es", wintypes.DWORD), ("seg_ds", wintypes.DWORD),
        ("edi", wintypes.DWORD), ("esi", wintypes.DWORD),
        ("ebx", wintypes.DWORD), ("edx", wintypes.DWORD),
        ("ecx", wintypes.DWORD), ("eax", wintypes.DWORD),
        ("ebp", wintypes.DWORD), ("eip", wintypes.DWORD),
        ("seg_cs", wintypes.DWORD), ("eflags", wintypes.DWORD),
        ("esp", wintypes.DWORD), ("seg_ss", wintypes.DWORD),
        ("extended_registers", ctypes.c_byte * 512),
    ]


def main() -> int:
    if len(sys.argv) != 5:
        raise SystemExit(
            "usage: trace_original_unit_sprite_entry.py PID FRAME SLOT OUTPUT.json")
    pid = int(sys.argv[1], 0)
    target_frame = int(sys.argv[2], 0)
    target_slot = int(sys.argv[3], 0)
    output = Path(sys.argv[4]).resolve()
    repository_root = Path(__file__).resolve().parents[3]
    artifact_root = (repository_root / "ranker_reconstructed_code" / "tools" /
                     "replay_debug" / "artifacts").resolve()
    try:
        output.relative_to(artifact_root)
    except ValueError as error:
        raise ValueError(
            f"trace output must stay below {artifact_root}: {output}") from error

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

    original_byte = read(TARGET_ADDRESS, 1)
    old_protection = wintypes.DWORD()
    if not kernel32.VirtualProtectEx(
            process, ctypes.c_void_p(TARGET_ADDRESS), 1,
            PAGE_EXECUTE_READWRITE, ctypes.byref(old_protection)):
        raise ctypes.WinError(ctypes.get_last_error())
    write(TARGET_ADDRESS, b"\xCC")
    kernel32.FlushInstructionCache(process, ctypes.c_void_p(TARGET_ADDRESS), 1)

    attached = False
    stepping_thread = 0
    captured = False
    deadline = time.monotonic() + 30.0
    expected_offset = target_slot * ORIGINAL_UNIT_STRIDE
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
                    exception_code in (EXCEPTION_SINGLE_STEP, STATUS_WX86_SINGLE_STEP) and
                    event.thread_id == stepping_thread):
                write(TARGET_ADDRESS, b"\xCC")
                kernel32.FlushInstructionCache(
                    process, ctypes.c_void_p(TARGET_ADDRESS), 1)
                stepping_thread = 0
            elif (event.code == EXCEPTION_DEBUG_EVENT and
                  exception_code in (EXCEPTION_BREAKPOINT, STATUS_WX86_BREAKPOINT) and
                  exception_address == TARGET_ADDRESS):
                frame = read_u32(FRAME_ADDRESS)
                unit_offset = read_u32(CURRENT_UNIT_OFFSET_ADDRESS)
                if frame >= target_frame and unit_offset == expected_offset:
                    screen_x, screen_y = struct.unpack(
                        "<ii", read(SPRITE_SCREEN_POSITION_ADDRESS, 8))
                    type_id = read_u32(ORIGINAL_UNIT_POOL_BASE + unit_offset)
                    resource_bases = list(struct.unpack(
                        "<" + "I" * 14,
                        read(ORIGINAL_UNIT_RESOURCE_BASES +
                             type_id * ORIGINAL_UNIT_RESOURCE_BASE_STRIDE,
                             ORIGINAL_UNIT_RESOURCE_BASE_STRIDE)))
                    sprite_entry = read_u32(SPRITE_ENTRY_ADDRESS)
                    resource_table = struct.unpack(
                        "<" + "I" * 8,
                        read(ORIGINAL_RESOURCE_TABLE_BASE +
                             sprite_entry * ORIGINAL_RESOURCE_TABLE_STRIDE,
                             ORIGINAL_RESOURCE_TABLE_STRIDE))
                    payload_pointer = resource_table[6]
                    payload_size = 0
                    for _ in range(resource_table[1]):
                        row_size = struct.unpack(
                            "<H", read(payload_pointer + payload_size, 2))[0]
                        payload_size += 2 + row_size
                    payload = read(payload_pointer, payload_size)
                    output.parent.mkdir(parents=True, exist_ok=True)
                    payload_path = output.with_suffix(".resource.bin")
                    payload_path.write_bytes(payload)
                    palette_slot = resource_table[7]
                    palette_pixels = read(
                        0x0156E8D0 + palette_slot * 0x200, 0x200)
                    palette_path = output.with_suffix(".palette565.bin")
                    palette_path.write_bytes(palette_pixels)
                    result = {
                        "frame": frame,
                        "unit_slot": target_slot,
                        "unit_offset": unit_offset,
                        "type_id": type_id,
                        "sprite_entry": sprite_entry,
                        "resource_group_bases": resource_bases,
                        "sprite_offsets_from_groups": [
                            sprite_entry - base if base != 0 else None
                            for base in resource_bases
                        ],
                        "resource_table_dwords": list(resource_table),
                        "resource_payload_pointer":
                            f"0x{resource_table[6]:X}",
                        "resource_payload_size": payload_size,
                        "resource_payload_sha256":
                            hashlib.sha256(payload).hexdigest(),
                        "resource_payload_path": str(payload_path),
                        "palette_slot": palette_slot,
                        "palette565_sha256":
                            hashlib.sha256(palette_pixels).hexdigest(),
                        "palette565_path": str(palette_path),
                        "screen": [screen_x, screen_y],
                        "breakpoint": f"0x{TARGET_ADDRESS:08X}",
                    }
                    output.parent.mkdir(parents=True, exist_ok=True)
                    output.write_text(json.dumps(result, indent=2), encoding="utf-8")
                    print(json.dumps(result))
                    captured = True
                    kernel32.TerminateProcess(process, 0)
                else:
                    write(TARGET_ADDRESS, original_byte)
                    kernel32.FlushInstructionCache(
                        process, ctypes.c_void_p(TARGET_ADDRESS), 1)
                    thread = kernel32.OpenThread(0x001F03FF, False, event.thread_id)
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
                continue_status = DBG_CONTINUE if exception_code in (
                    EXCEPTION_BREAKPOINT, STATUS_WX86_BREAKPOINT) else (
                    DBG_EXCEPTION_NOT_HANDLED)

            kernel32.ContinueDebugEvent(
                event.process_id, event.thread_id, continue_status)
            if captured:
                return 0
        raise TimeoutError(
            f"unit sprite breakpoint did not hit slot {target_slot} at frame "
            f">={target_frame}")
    finally:
        if not captured:
            try:
                write(TARGET_ADDRESS, original_byte)
                kernel32.FlushInstructionCache(
                    process, ctypes.c_void_p(TARGET_ADDRESS), 1)
            except OSError:
                pass
        if attached:
            kernel32.DebugActiveProcessStop(pid)
        kernel32.CloseHandle(process)


if __name__ == "__main__":
    raise SystemExit(main())
