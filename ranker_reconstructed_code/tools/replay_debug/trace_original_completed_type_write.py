#!/usr/bin/env python3
"""Trace original writes that increment DAT_00707430[0][0]."""

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
WOW64_CONTEXT_FULL = 0x00010007

FRAME_ADDRESS = 0x007071A4
COUNT_ADDRESS = 0x00707430
UNIT_POOL_BASE = 0x00A03FB8

BREAKPOINTS = {
    0x004CE46C: "creation_register_footprint_inc",
    0x004CFA12: "active_list_rebuild_inc",
}


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
            "usage: trace_original_completed_type_write.py PID START_FRAME "
            "END_FRAME OUTPUT.json")
    pid = int(sys.argv[1], 0)
    start_frame = int(sys.argv[2], 0)
    end_frame = int(sys.argv[3], 0)
    output = Path(sys.argv[4]).resolve()
    repository_root = Path(__file__).resolve().parents[3]
    artifact_root = (repository_root / "ranker_reconstructed_code" / "tools" /
                     "replay_debug" / "artifacts").resolve()
    output.relative_to(artifact_root)

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

    originals = {address: read(address, 1) for address in BREAKPOINTS}
    for address in BREAKPOINTS:
        old_protection = wintypes.DWORD()
        if not kernel32.VirtualProtectEx(
                process, ctypes.c_void_p(address), 1,
                PAGE_EXECUTE_READWRITE, ctypes.byref(old_protection)):
            raise ctypes.WinError(ctypes.get_last_error())
        write(address, b"\xCC")
        kernel32.FlushInstructionCache(process, ctypes.c_void_p(address), 1)

    attached = False
    stepping: dict[int, int] = {}
    hits: list[dict[str, object]] = []
    deadline = time.monotonic() + 40.0
    suspended = False
    try:
        if not kernel32.DebugActiveProcess(pid):
            raise ctypes.WinError(ctypes.get_last_error())
        attached = True
        kernel32.DebugSetProcessKillOnExit(False)
        for _ in range(8):
            ntdll.NtResumeProcess(process)

        event = DebugEvent()
        while time.monotonic() < deadline:
            if not kernel32.WaitForDebugEvent(ctypes.byref(event), 20):
                error = ctypes.get_last_error()
                if error != 121:
                    raise ctypes.WinError(error)
                if read_u32(FRAME_ADDRESS) > end_frame:
                    ntdll.NtSuspendProcess(process)
                    suspended = True
                    break
                continue

            continue_status = DBG_CONTINUE
            exception_code = 0
            exception_address = 0
            if event.code == EXCEPTION_DEBUG_EVENT:
                exception_code = event.data.exception.record.code
                exception_address = int(event.data.exception.record.address or 0)

            if (event.code == EXCEPTION_DEBUG_EVENT and
                    exception_code in (EXCEPTION_SINGLE_STEP, STATUS_WX86_SINGLE_STEP) and
                    event.thread_id in stepping):
                address = stepping.pop(event.thread_id)
                write(address, b"\xCC")
                kernel32.FlushInstructionCache(process, ctypes.c_void_p(address), 1)
            elif (event.code == EXCEPTION_DEBUG_EVENT and
                  exception_code in (EXCEPTION_BREAKPOINT, STATUS_WX86_BREAKPOINT) and
                  exception_address in BREAKPOINTS):
                address = exception_address
                write(address, originals[address])
                kernel32.FlushInstructionCache(process, ctypes.c_void_p(address), 1)
                thread = kernel32.OpenThread(0x001F03FF, False, event.thread_id)
                if not thread:
                    raise ctypes.WinError(ctypes.get_last_error())
                try:
                    context = Wow64Context()
                    context.context_flags = WOW64_CONTEXT_FULL
                    if not kernel32.Wow64GetThreadContext(thread, ctypes.byref(context)):
                        raise ctypes.WinError(ctypes.get_last_error())
                    context.eip -= 1
                    effective = (context.ebx + context.eax * 4 + COUNT_ADDRESS) & 0xFFFFFFFF
                    frame = read_u32(FRAME_ADDRESS)
                    if start_frame <= frame <= end_frame and effective == COUNT_ADDRESS:
                        stack = list(struct.unpack("<" + "I" * 16, read(context.esp, 64)))
                        unit_type = read_u32(UNIT_POOL_BASE + context.esi)
                        unit_owner = read_u32(UNIT_POOL_BASE + context.esi + 4)
                        hits.append({
                            "frame": frame,
                            "site": BREAKPOINTS[address],
                            "instruction": f"0x{address:08X}",
                            "count_before": read_u32(COUNT_ADDRESS),
                            "eax_type": context.eax,
                            "ebx_owner_stride": context.ebx,
                            "esi_unit_offset": context.esi,
                            "esi_unit_type": unit_type,
                            "esi_unit_owner": unit_owner,
                            "registers": {
                                "eax": context.eax, "ebx": context.ebx,
                                "ecx": context.ecx, "edx": context.edx,
                                "esi": context.esi, "edi": context.edi,
                                "ebp": context.ebp, "esp": context.esp,
                            },
                            "stack": [f"0x{value:08X}" for value in stack],
                        })
                    context.eflags |= 0x100
                    if not kernel32.Wow64SetThreadContext(thread, ctypes.byref(context)):
                        raise ctypes.WinError(ctypes.get_last_error())
                    stepping[event.thread_id] = address
                finally:
                    kernel32.CloseHandle(thread)
            elif event.code == EXCEPTION_DEBUG_EVENT:
                continue_status = DBG_CONTINUE if exception_code in (
                    EXCEPTION_BREAKPOINT, STATUS_WX86_BREAKPOINT) else DBG_EXCEPTION_NOT_HANDLED

            kernel32.ContinueDebugEvent(
                event.process_id, event.thread_id, continue_status)

        if not suspended:
            ntdll.NtSuspendProcess(process)
            suspended = True
        result = {
            "pid": pid,
            "start_frame": start_frame,
            "end_frame": end_frame,
            "stopped_frame": read_u32(FRAME_ADDRESS),
            "final_count": read_u32(COUNT_ADDRESS),
            "hits": hits,
        }
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(result, indent=2), encoding="utf-8")
        print(json.dumps(result))
        return 0
    finally:
        for address, original in originals.items():
            try:
                write(address, original)
                kernel32.FlushInstructionCache(process, ctypes.c_void_p(address), 1)
            except OSError:
                pass
        if attached:
            kernel32.DebugActiveProcessStop(pid)
        kernel32.CloseHandle(process)


if __name__ == "__main__":
    raise SystemExit(main())
