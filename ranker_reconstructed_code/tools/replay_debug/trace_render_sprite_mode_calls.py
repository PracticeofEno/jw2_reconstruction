#!/usr/bin/env python3
"""Trace resource-mode sprite calls made by one completed world composite."""

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
WOW64_CONTEXT_CONTROL_AND_INTEGER = 0x00010003
CONTEXT_AMD64_CONTROL_AND_INTEGER = 0x00100003
ORIGINAL_LOW_HEALTH_CALL_RETURN = 0x004C5968
ORIGINAL_RESOURCE_TABLE_BASE = 0x014BADC0
ORIGINAL_RESOURCE_TABLE_STRIDE = 0x20
ORIGINAL_PALETTE565_BASE = 0x0156E8D0
ORIGINAL_UNIT_POOL_BASE = 0x00A03FB8
ORIGINAL_CURRENT_UNIT_OFFSET = 0x0072C6B4
ORIGINAL_JW207_UNIT_START = 0x00758A10
ORIGINAL_LOW_HEALTH_FRAME_ENTRY = 0x0072C530
REBUILD_RESOURCE_STORE_STATE_RVA = 0x00C5B7A0
REBUILD_RESOURCE_STORE_ENTRY_SIZE = 64
REBUILD_PALETTE_CACHE_STATE_RVA = 0x00883B80
REBUILD_PALETTE_RAW_SLOTS_BYTES = 0x80000


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
        ("data_selector", wintypes.DWORD), ("register_area", ctypes.c_byte * 80),
        ("cr0_npx_state", wintypes.DWORD),
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


class Amd64Context(ctypes.Structure):
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


def signed32(value: int) -> int:
    value &= 0xFFFFFFFF
    return value - 0x100000000 if value & 0x80000000 else value


def main() -> int:
    if len(sys.argv) not in (9, 10, 11) or sys.argv[1] not in ("original", "rebuild"):
        raise SystemExit(
            "usage: trace_render_sprite_mode_calls.py original|rebuild PID "
            "REBUILD_BASE FRAME_ADDRESS TARGET_FRAME COMPOSITE_ADDRESS "
            "MODE_ADDRESS OUTPUT.json [sprite|line|unit|scan|visibility] "
            "[SPRITE_RENDER_STATE_ADDRESS]")
    architecture = sys.argv[1]
    pid = int(sys.argv[2], 0)
    rebuild_base = int(sys.argv[3], 0)
    frame_address = int(sys.argv[4], 0)
    target_frame = int(sys.argv[5], 0)
    composite_address = int(sys.argv[6], 0)
    mode_address = int(sys.argv[7], 0)
    output = Path(sys.argv[8]).resolve()
    call_kind = sys.argv[9] if len(sys.argv) >= 10 else "sprite"
    sprite_render_state_address = int(sys.argv[10], 0) if len(sys.argv) == 11 else 0
    if call_kind not in ("sprite", "line", "unit", "scan", "visibility"):
        raise ValueError(f"unknown trace call kind: {call_kind}")
    repository_root = Path(__file__).resolve().parents[3]
    artifact_root = (repository_root / "ranker_reconstructed_code" / "tools" /
                     "replay_debug" / "artifacts").resolve()
    try:
        output.relative_to(artifact_root)
    except ValueError as error:
        raise ValueError(f"trace output must stay below {artifact_root}") from error

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

    def make_writable(address: int) -> None:
        old = wintypes.DWORD()
        if not kernel32.VirtualProtectEx(
                process, ctypes.c_void_p(address), 1,
                PAGE_EXECUTE_READWRITE, ctypes.byref(old)):
            raise ctypes.WinError(ctypes.get_last_error())

    def resource_fingerprint(entry_index: int) -> dict[str, object]:
        if architecture == "original":
            entry = struct.unpack(
                "<8I", read(ORIGINAL_RESOURCE_TABLE_BASE +
                             entry_index * ORIGINAL_RESOURCE_TABLE_STRIDE,
                             ORIGINAL_RESOURCE_TABLE_STRIDE))
            payload_pointer = entry[6]
            payload_size = 0
            for _ in range(entry[1]):
                row_size = struct.unpack(
                    "<H", read(payload_pointer + payload_size, 2))[0]
                payload_size += 2 + row_size
            payload = read(payload_pointer, payload_size)
            palette = read(ORIGINAL_PALETTE565_BASE + entry[7] * 0x200, 0x200)
            return {
                "metadata": list(entry[:6]), "palette_slot": entry[7],
                "payload_size": payload_size,
                "payload_sha256": hashlib.sha256(payload).hexdigest(),
                "palette565_sha256": hashlib.sha256(palette).hexdigest(),
            }
        entry_address = (rebuild_base + REBUILD_RESOURCE_STORE_STATE_RVA +
                         entry_index * REBUILD_RESOURCE_STORE_ENTRY_SIZE)
        entry = read(entry_address, REBUILD_RESOURCE_STORE_ENTRY_SIZE)
        metadata = list(struct.unpack_from("<6I", entry, 0))
        palette_slot = struct.unpack_from("<I", entry, 24)[0]
        payload_begin, payload_end = struct.unpack_from("<QQ", entry, 32)
        if payload_end < payload_begin or payload_end - payload_begin > 0x1000000:
            raise ValueError(
                f"invalid resource payload 0x{payload_begin:X}..0x{payload_end:X}")
        payload = read(payload_begin, payload_end - payload_begin)
        palette = read(
            rebuild_base + REBUILD_PALETTE_CACHE_STATE_RVA +
            REBUILD_PALETTE_RAW_SLOTS_BYTES + palette_slot * 0x200, 0x200)
        return {
            "metadata": metadata, "palette_slot": palette_slot,
            "payload_size": len(payload),
            "payload_sha256": hashlib.sha256(payload).hexdigest(),
            "palette565_sha256": hashlib.sha256(palette).hexdigest(),
        }

    breakpoint_bytes = {
        composite_address: read(composite_address, 1),
        mode_address: read(mode_address, 1),
    }
    for address in breakpoint_bytes:
        make_writable(address)
        write(address, b"\xCC")
    kernel32.FlushInstructionCache(process, None, 0)

    attached = False
    stepping: dict[int, int] = {}
    # Linked visibility is produced during the simulation tick before the
    # per-frame visibility sync.  Start recording as soon as the debugger is
    # attached so the one-shot damage reaction is not missed while waiting for
    # the first composite breakpoint.
    recording = call_kind == "visibility"
    composite_frame = target_frame
    calls: list[dict[str, int | bool | str]] = []
    captured = False
    deadline = time.monotonic() + 30.0

    def resume_breakpoint(thread_id: int, address: int) -> None:
        thread = kernel32.OpenThread(0x001F03FF, False, thread_id)
        if not thread:
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            write(address, breakpoint_bytes[address])
            if architecture == "original":
                context = Wow64Context()
                context.context_flags = WOW64_CONTEXT_CONTROL_AND_INTEGER
                if not kernel32.Wow64GetThreadContext(thread, ctypes.byref(context)):
                    raise ctypes.WinError(ctypes.get_last_error())
                context.eip -= 1
                context.eflags |= 0x100
                if not kernel32.Wow64SetThreadContext(thread, ctypes.byref(context)):
                    raise ctypes.WinError(ctypes.get_last_error())
            else:
                context = Amd64Context()
                context.context_flags = CONTEXT_AMD64_CONTROL_AND_INTEGER
                if not kernel32.GetThreadContext(thread, ctypes.byref(context)):
                    raise ctypes.WinError(ctypes.get_last_error())
                context.rip -= 1
                context.eflags |= 0x100
                if not kernel32.SetThreadContext(thread, ctypes.byref(context)):
                    raise ctypes.WinError(ctypes.get_last_error())
            stepping[thread_id] = address
        finally:
            kernel32.CloseHandle(thread)

    def read_call(thread_id: int) -> dict[str, int | bool | str]:
        thread = kernel32.OpenThread(0x001F03FF, False, thread_id)
        if not thread:
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            if architecture == "original":
                context = Wow64Context()
                context.context_flags = WOW64_CONTEXT_CONTROL_AND_INTEGER
                if not kernel32.Wow64GetThreadContext(thread, ctypes.byref(context)):
                    raise ctypes.WinError(ctypes.get_last_error())
                caller = struct.unpack("<I", read(context.esp, 4))[0]
                if call_kind == "visibility":
                    source = int(context.esi)
                    linked = int(context.edi)
                    local_player_slot = struct.unpack(
                        "<I", read(0x00725100, 4))[0]
                    linked_owner = struct.unpack(
                        "<I", read(linked + 0xA03FBC, 4))[0]
                    return {
                        "frame": struct.unpack("<I", read(frame_address, 4))[0],
                        "caller": f"0x{caller:08X}",
                        "source_bias": f"0x{source:08X}",
                        "linked_bias": f"0x{linked:08X}",
                        "source_type": struct.unpack(
                            "<I", read(source + 0xA03FB8, 4))[0],
                        "source_owner": struct.unpack(
                            "<I", read(source + 0xA03FBC, 4))[0],
                        "source_x": struct.unpack(
                            "<i", read(source + 0xA04078, 4))[0],
                        "source_y": struct.unpack(
                            "<i", read(source + 0xA0407C, 4))[0],
                        "linked_type": struct.unpack(
                            "<I", read(linked + 0xA03FB8, 4))[0],
                        "linked_owner": linked_owner,
                        "linked_x": struct.unpack(
                            "<i", read(linked + 0xA04078, 4))[0],
                        "linked_y": struct.unpack(
                            "<i", read(linked + 0xA0407C, 4))[0],
                        "local_player_slot": local_player_slot,
                        "local_player_bit": struct.unpack(
                            "<I", read(0x0086B700 + local_player_slot * 4, 4))[0],
                        "linked_owner_visibility_mask": struct.unpack(
                            "<I", read(0x00725384 + linked_owner * 4, 4))[0],
                    }
                if call_kind == "line":
                    x0, y0, x1, y1 = struct.unpack(
                        "<4i", read(0x0086B648, 16))
                    return {
                        "x0": x0, "y0": y0, "x1": x1, "y1": y1,
                        "color": int(context.eax & 0xFFFF),
                        "caller": f"0x{caller:08X}",
                        "presentation_seed": struct.unpack(
                            "<I", read(0x007071C4, 4))[0],
                    }
                result: dict[str, int | bool | str | object] = {
                    "entry": int(context.eax), "x": signed32(context.edx),
                    "y": signed32(context.ebx), "mode": int(context.ecx),
                    "caller": f"0x{caller:08X}",
                    "low_health_call": caller == ORIGINAL_LOW_HEALTH_CALL_RETURN,
                }
                if call_kind in ("unit", "scan"):
                    unit_offset = struct.unpack(
                        "<I", read(ORIGINAL_CURRENT_UNIT_OFFSET, 4))[0]
                    result["unit_offset"] = unit_offset
                    result["unit_slot"] = unit_offset // 0x1D0
                    result["unit_type"] = struct.unpack(
                        "<I", read(ORIGINAL_UNIT_POOL_BASE + unit_offset, 4))[0]
                    result["unit_palette_ramp"] = read(0x00758A4C, 1)[0]
                    result["unit_ramp_source_palette565_sha256"] = hashlib.sha256(
                        read(ORIGINAL_PALETTE565_BASE, 0x200)).hexdigest()
                    result["resource"] = resource_fingerprint(int(context.eax))
                    return result
                unit_offset = struct.unpack(
                    "<I", read(ORIGINAL_CURRENT_UNIT_OFFSET, 4))[0]
                result.update({
                    "jw207_unit_start": struct.unpack(
                        "<I", read(ORIGINAL_JW207_UNIT_START, 4))[0],
                    "computed_overlay_frame": (
                        struct.unpack(
                            "<I", read(ORIGINAL_LOW_HEALTH_FRAME_ENTRY, 4))[0] -
                        struct.unpack(
                            "<I", read(ORIGINAL_JW207_UNIT_START, 4))[0]) &
                        0xFFFFFFFF,
                    "unit_offset": unit_offset,
                    "unit_raw_3c": struct.unpack(
                        "<I", read(ORIGINAL_UNIT_POOL_BASE + unit_offset + 0x3C, 4))[0],
                    "overlay_index": int(context.esi),
                })
                result["resource"] = resource_fingerprint(int(context.eax))
                return result
            context = Amd64Context()
            context.context_flags = CONTEXT_AMD64_CONTROL_AND_INTEGER
            if not kernel32.GetThreadContext(thread, ctypes.byref(context)):
                raise ctypes.WinError(ctypes.get_last_error())
            caller = struct.unpack("<Q", read(context.rsp, 8))[0]
            if call_kind == "visibility":
                visibility_context = int(context.rcx)
                source = int(context.rdx)
                linked = int(context.r8)
                players = struct.unpack(
                    "<Q", read(visibility_context + 8, 8))[0]
                linked_owner = struct.unpack("<I", read(linked, 4))[0]
                return {
                    "frame": struct.unpack("<I", read(frame_address, 4))[0],
                    "caller": f"0x{caller:016X}",
                    "context": f"0x{visibility_context:016X}",
                    "context_frame": struct.unpack(
                        "<I", read(visibility_context + 0x40, 4))[0],
                    "local_player_slot": struct.unpack(
                        "<I", read(visibility_context + 0x44, 4))[0],
                    "grid": f"0x{struct.unpack('<Q', read(visibility_context, 8))[0]:016X}",
                    "players": f"0x{players:016X}",
                    "source": f"0x{source:016X}",
                    "source_owner": struct.unpack("<I", read(source, 4))[0],
                    "source_type": struct.unpack("<I", read(source + 4, 4))[0],
                    "source_x": struct.unpack("<i", read(source + 0x48, 4))[0],
                    "source_y": struct.unpack("<i", read(source + 0x4C, 4))[0],
                    "source_center_x": struct.unpack(
                        "<i", read(source + 0x50, 4))[0],
                    "source_center_y": struct.unpack(
                        "<i", read(source + 0x54, 4))[0],
                    "source_terrain_probe_x": struct.unpack(
                        "<i", read(source + 0x68, 4))[0],
                    "source_terrain_probe_y": struct.unpack(
                        "<i", read(source + 0x6C, 4))[0],
                    "source_large_centered": bool(read(source + 0x80, 1)[0]),
                    "source_terrain_probe_valid": bool(
                        read(source + 0x83, 1)[0]),
                    "linked": f"0x{linked:016X}",
                    "linked_owner": linked_owner,
                    "linked_type": struct.unpack("<I", read(linked + 4, 4))[0],
                    "linked_x": struct.unpack("<i", read(linked + 0x48, 4))[0],
                    "linked_y": struct.unpack("<i", read(linked + 0x4C, 4))[0],
                    "linked_owner_visibility_mask": struct.unpack(
                        "<I", read(players + 0x28 + linked_owner * 4, 4))[0],
                }
            if call_kind == "line":
                return {
                    "x0": signed32(context.rdx),
                    "y0": signed32(context.r8),
                    "x1": signed32(context.r9),
                    "y1": struct.unpack("<i", read(context.rsp + 0x28, 4))[0],
                    "color": struct.unpack("<H", read(context.rsp + 0x30, 2))[0],
                    "caller": f"0x{caller:016X}",
                }
            result = {
                "entry": int(context.rcx & 0xFFFFFFFF),
                "x": signed32(context.rdx), "y": signed32(context.r8),
                "mode": int(context.r9 & 0xFFFFFFFF),
                "caller": f"0x{caller:016X}", "low_health_call": False,
            }
            if call_kind == "scan" and result["x"] == 322 and result["y"] == 292:
                store_base = rebuild_base + REBUILD_RESOURCE_STORE_STATE_RVA
                next_entry = struct.unpack(
                    "<I", read(store_base + 0x167600, 4))[0]
                entries = read(
                    store_base, next_entry * REBUILD_RESOURCE_STORE_ENTRY_SIZE)
                matches = []
                for entry_index in range(next_entry):
                    offset = entry_index * REBUILD_RESOURCE_STORE_ENTRY_SIZE
                    metadata = list(struct.unpack_from("<6I", entries, offset))
                    if metadata[0] != 333 or metadata[1] != 11:
                        continue
                    palette_slot = struct.unpack_from("<I", entries, offset + 24)[0]
                    payload_begin, payload_end = struct.unpack_from(
                        "<QQ", entries, offset + 32)
                    if payload_end < payload_begin or payload_end - payload_begin != 3550:
                        continue
                    payload = read(payload_begin, payload_end - payload_begin)
                    matches.append({
                        "entry": entry_index,
                        "metadata": metadata,
                        "palette_slot": palette_slot,
                        "payload_size": len(payload),
                        "payload_sha256": hashlib.sha256(payload).hexdigest(),
                    })
                result["resource_scan_next_entry"] = next_entry
                result["resource_scan_matches"] = matches
            if call_kind in ("unit", "scan"):
                if sprite_render_state_address != 0:
                    result["unit_palette_ramp"] = read(
                        sprite_render_state_address + 0x38, 1)[0]
                result["unit_ramp_source_palette565_sha256"] = hashlib.sha256(
                    read(rebuild_base + REBUILD_PALETTE_CACHE_STATE_RVA +
                         REBUILD_PALETTE_RAW_SLOTS_BYTES, 0x200)).hexdigest()
                result["resource"] = resource_fingerprint(
                    int(context.rcx & 0xFFFFFFFF))
                return result
            result.update({
                "jw207_unit_start": struct.unpack(
                    "<I", read(context.r15 + 0x118C, 4))[0],
                "animation_frame": struct.unpack(
                    "<I", read(context.rbx + 0x14, 4))[0],
            })
            result["resource"] = resource_fingerprint(
                int(context.rcx & 0xFFFFFFFF))
            return result
        finally:
            kernel32.CloseHandle(thread)

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

            is_breakpoint = exception_code in (
                EXCEPTION_BREAKPOINT, STATUS_WX86_BREAKPOINT)
            is_single_step = exception_code in (
                EXCEPTION_SINGLE_STEP, STATUS_WX86_SINGLE_STEP)
            if (event.code == EXCEPTION_DEBUG_EVENT and is_single_step and
                    event.thread_id in stepping):
                address = stepping.pop(event.thread_id)
                write(address, b"\xCC")
                kernel32.FlushInstructionCache(
                    process, ctypes.c_void_p(address), 1)
            elif (event.code == EXCEPTION_DEBUG_EVENT and is_breakpoint and
                    exception_address == composite_address):
                frame = struct.unpack("<I", read(frame_address, 4))[0]
                if recording:
                    if call_kind == "visibility" and frame <= target_frame + 1:
                        resume_breakpoint(event.thread_id, composite_address)
                        kernel32.ContinueDebugEvent(
                            event.process_id, event.thread_id, DBG_CONTINUE)
                        continue
                    visibility_snapshot: dict[str, int] = {}
                    if call_kind == "visibility" and calls:
                        center_x = int(calls[0]["source_x"]) >> 5
                        center_y = int(calls[0]["source_y"]) >> 5
                        if architecture == "original":
                            width = 256
                            current_begin = 0x00758D40
                        else:
                            grid = int(str(calls[0]["grid"]), 0)
                            width = struct.unpack("<I", read(grid, 4))[0]
                            current_begin = struct.unpack(
                                "<Q", read(grid + 8, 8))[0]
                        for sample_y in range(center_y - 3, center_y + 4):
                            for sample_x in range(center_x - 3, center_x + 4):
                                visibility_snapshot[f"{sample_x},{sample_y}"] = (
                                    struct.unpack(
                                        "<I", read(current_begin +
                                                    (sample_y * width + sample_x) * 4,
                                                    4))[0])
                    result = {
                        "architecture": architecture,
                        "frame": composite_frame,
                        "next_composite_frame": frame,
                        "composite_address": f"0x{composite_address:X}",
                        "mode_address": f"0x{mode_address:X}",
                        "call_kind": call_kind,
                        "call_count": len(calls),
                        "low_health_call_count": sum(
                            bool(call.get("low_health_call", False))
                            for call in calls),
                        "calls": calls,
                        "current_around_first_source_before_sync":
                            visibility_snapshot,
                    }
                    output.parent.mkdir(parents=True, exist_ok=True)
                    output.write_text(json.dumps(result, indent=2), encoding="utf-8")
                    print(json.dumps(result))
                    captured = True
                    kernel32.TerminateProcess(process, 0)
                else:
                    if frame >= target_frame:
                        recording = True
                        composite_frame = frame
                    resume_breakpoint(event.thread_id, composite_address)
            elif (event.code == EXCEPTION_DEBUG_EVENT and is_breakpoint and
                    exception_address == mode_address):
                frame = struct.unpack("<I", read(frame_address, 4))[0]
                if recording and (call_kind != "visibility" or
                                  frame > target_frame):
                    calls.append(read_call(event.thread_id))
                resume_breakpoint(event.thread_id, mode_address)
            elif event.code == EXCEPTION_DEBUG_EVENT:
                continue_status = (DBG_CONTINUE if is_breakpoint else
                                   DBG_EXCEPTION_NOT_HANDLED)

            kernel32.ContinueDebugEvent(
                event.process_id, event.thread_id, continue_status)
            if captured:
                return 0
        raise TimeoutError(
            f"sprite-mode trace did not complete target frame {target_frame}")
    finally:
        if not captured:
            try:
                for address, original_byte in breakpoint_bytes.items():
                    write(address, original_byte)
                kernel32.FlushInstructionCache(process, None, 0)
            except OSError:
                pass
        if attached:
            kernel32.DebugActiveProcessStop(pid)
        kernel32.CloseHandle(process)


if __name__ == "__main__":
    raise SystemExit(main())
