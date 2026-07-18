import ctypes
import hashlib
import json
import os
import struct
import sys
import time
import traceback
from ctypes import wintypes


ORIGINAL_PID = int(sys.argv[1])
REBUILD_PID = int(sys.argv[2])
REBUILD_BASE = int(sys.argv[3], 0)
LAYOUT_PATH = sys.argv[4]
RESULT_PATH = sys.argv[5]
JOURNAL_PATH = sys.argv[6]
STOP_PATH = sys.argv[7]
MAX_SECONDS = float(sys.argv[8]) if len(sys.argv) > 8 else 360.0
# The simulation-frame word advances at the front of the tick.  Capturing
# immediately after that write can freeze one peer between adjacent active
# units even when both executables are deterministic.  A normal P2P tick in
# this fixture is roughly 50 ms; wait through the unit/AI work and retain a
# snapshot only from the quiet tail of that tick.
SETTLE_SECONDS = 0.030
CANDIDATE_INTERVAL_SECONDS = 0.001
POLL_SECONDS = 0.0005
REQUIRED_IDENTICAL_CANDIDATES = 2
MIN_CANDIDATE_STABLE_SECONDS = 0.005
REQUIRED_CONSECUTIVE_DIVERGENCES = 3
# Two independently frozen, identical reads are required before a frame is
# retained.  Sampling every other frame offsets the extra freeze cost while
# still proving persistent deterministic differences within six frames.
SAMPLE_FRAME_INTERVAL = 2
SAMPLE_START_FRAME = 0


def number(value):
    return int(value, 0) if isinstance(value, str) else int(value)


with open(LAYOUT_PATH, "r", encoding="utf-8-sig") as stream:
    layout = json.load(stream)

runtime_rva = number(layout["runtime_rva"])
loop_rva = number(layout["loop_rva"])
random_offset = number(layout["frame_random_offset"])
movement_offset = number(layout["movement_offset"])
lifecycle_offset = number(layout["lifecycle_offset"])
production_runtime_offset = number(layout["production_runtime_offset"])
owner_counters_offset = number(layout["owner_counters_offset"])
map_effect_context_offset = number(layout["map_effect_context_offset"])
unit_effects_offset = number(layout["unit_effects_offset"])
player_slots_offset = number(layout["player_slots_offset"])
owner_ai_offset = number(layout["owner_ai_offset"])
owner_transport_routes_offset = number(layout["owner_transport_routes_offset"])
owner_strategic_targets_offset = number(layout["owner_strategic_targets_offset"])
# The typed queue array immediately precedes eight 0xf8-byte route records.
# Eight owners * 32 slots * sizeof(OwnerTransportQueueSlot=0x28).
owner_transport_queues_offset = owner_transport_routes_offset - 0x2800
unit_layout = {key: number(value)
               for key, value in layout["unit_layout"].items()}
context_layout = {key: number(value)
                  for key, value in layout["movement_context_layout"].items()}
lifecycle_layout = {key: number(value)
                    for key, value in layout["lifecycle_layout"].items()}
owner_counter_layout = {
    key: number(value)
    for key, value in layout["owner_counter_layout"].items()
}
loop_layout = {key: number(value)
               for key, value in layout["loop_layout"].items()}
map_effect_layout = {key: number(value)
                     for key, value in layout["map_effect_layout"].items()}
unit_effect_layout = {key: number(value)
                      for key, value in layout["unit_effect_layout"].items()}
player_slots_layout = {key: number(value)
                       for key, value in layout["player_slots_layout"].items()}
owner_ai_layout = {key: number(value)
                   for key, value in layout["owner_ai_layout"].items()}
owner_transport_route_layout = {
    key: number(value)
    for key, value in layout["owner_transport_route_layout"].items()
}
owner_strategic_target_layout = {
    key: number(value)
    for key, value in layout["owner_strategic_target_layout"].items()
}


def atomic_json(path, value):
    temporary = f"{path}.{os.getpid()}.{time.time_ns()}.tmp"
    try:
        with open(temporary, "w", encoding="utf-8", newline="\n") as stream:
            json.dump(value, stream, ensure_ascii=False, indent=2,
                      allow_nan=False)
            stream.write("\n")
        # The watcher may briefly hold the prior JSON without delete sharing
        # on Windows.  A unique temporary avoids stale-writer collisions and
        # bounded retries preserve the atomic replacement contract.
        for attempt in range(40):
            try:
                os.replace(temporary, path)
                return
            except PermissionError:
                if attempt == 39:
                    raise
                time.sleep(0.025)
    finally:
        try:
            if os.path.exists(temporary):
                os.remove(temporary)
        except OSError:
            pass


def canonical(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True,
                      separators=(",", ":"), allow_nan=False).encode("utf-8")


def digest(value):
    return hashlib.sha256(canonical(value)).hexdigest()


def u32(data, offset):
    return struct.unpack_from("<I", data, offset)[0]


def i32(data, offset):
    return struct.unpack_from("<i", data, offset)[0]


def u64(data, offset):
    return struct.unpack_from("<Q", data, offset)[0]


PROCESS_VM_READ = 0x0010
PROCESS_QUERY_INFORMATION = 0x0400
PROCESS_SUSPEND_RESUME = 0x0800
k32 = ctypes.WinDLL("kernel32", use_last_error=True)
k32.OpenProcess.restype = wintypes.HANDLE
k32.ReadProcessMemory.argtypes = [
    wintypes.HANDLE, wintypes.LPCVOID, wintypes.LPVOID, ctypes.c_size_t,
    ctypes.POINTER(ctypes.c_size_t),
]
k32.CloseHandle.argtypes = [wintypes.HANDLE]
ntdll = ctypes.WinDLL("ntdll")
ntdll.NtSuspendProcess.argtypes = [wintypes.HANDLE]
ntdll.NtSuspendProcess.restype = ctypes.c_long
ntdll.NtResumeProcess.argtypes = [wintypes.HANDLE]
ntdll.NtResumeProcess.restype = ctypes.c_long


class Memory:
    def __init__(self, pid):
        self.pid = pid
        self.handle = k32.OpenProcess(
            PROCESS_VM_READ | PROCESS_QUERY_INFORMATION |
            PROCESS_SUSPEND_RESUME, False, pid)
        if not self.handle:
            raise ctypes.WinError(ctypes.get_last_error())

    def close(self):
        if self.handle:
            k32.CloseHandle(self.handle)
            self.handle = None

    def read(self, address, size):
        buffer = (ctypes.c_ubyte * size)()
        transferred = ctypes.c_size_t()
        if not k32.ReadProcessMemory(
                self.handle, ctypes.c_void_p(address), buffer, size,
                ctypes.byref(transferred)):
            raise ctypes.WinError(ctypes.get_last_error())
        if transferred.value != size:
            raise RuntimeError(
                f"short read pid={self.pid} address={address:#x}: "
                f"{transferred.value}/{size}")
        return bytes(buffer)

    def suspend(self):
        status = ntdll.NtSuspendProcess(self.handle)
        if status < 0:
            raise OSError(
                f"NtSuspendProcess pid={self.pid} status={status & 0xffffffff:#x}")

    def resume(self):
        status = ntdll.NtResumeProcess(self.handle)
        if status < 0:
            raise OSError(
                f"NtResumeProcess pid={self.pid} status={status & 0xffffffff:#x}")

    def u32(self, address):
        return struct.unpack("<I", self.read(address, 4))[0]

    def i32(self, address):
        return struct.unpack("<i", self.read(address, 4))[0]


def pointer_vector(memory, address, limit=4096):
    begin, end, capacity = struct.unpack("<QQQ", memory.read(address, 24))
    if (not begin or end < begin or capacity < end or
            (end - begin) % 8 != 0 or (end - begin) // 8 > limit):
        return []
    count = (end - begin) // 8
    if count == 0:
        return []
    return list(struct.unpack(
        f"<{count}Q", memory.read(begin, count * 8)))


def index_vector(memory, address, element_size, limit=4096):
    begin, end, capacity = struct.unpack("<QQQ", memory.read(address, 24))
    if (element_size not in (4, 8) or not begin or end < begin or
            capacity < end or (end - begin) % element_size != 0 or
            (end - begin) // element_size > limit):
        return []
    count = (end - begin) // element_size
    if count == 0:
        return []
    code = "I" if element_size == 4 else "Q"
    return list(struct.unpack(
        f"<{count}{code}", memory.read(begin, count * element_size)))


def normalized_original_unit(data):
    state = u32(data, 0x60)
    placement_state = (state & 0x00FFFFFF) in (0x23, 0x25)
    return {
        "type": u32(data, 0x00),
        "owner": u32(data, 0x04),
        "area_marker": u32(data, 0x0C),
        "state": state,
        "command_value": (u32(data, 0x68)
                          if (state & 0x00FFFFFF) in
                          (0x4D, 0x4E, 0x50, 0x51, 0x82, 0x83) else 0),
        "placement_command_value": u32(data, 0x68) if placement_state else 0,
        "spawn_type": u32(data, 0x68) if placement_state else 0,
        "previous_state": u32(data, 0x74),
        "pending": [u32(data, 0x84), u32(data, 0x88),
                    i32(data, 0x8C), i32(data, 0x90)],
        "active_payload": [u32(data, 0xE4), u32(data, 0xD8),
                           i32(data, 0xDC), i32(data, 0xE0)],
        "deferred_count": u32(data, 0x124),
        "deferred_first": [u32(data, 0x128), u32(data, 0x12C),
                           i32(data, 0x130), i32(data, 0x134)],
        "command_flags": u32(data, 0x9C),
        "runtime_flags": u32(data, 0xA0),
        "draw_flags": u32(data, 0xA4),
        "lockout": u32(data, 0xF4),
        "_raw_animation_or_work_timer": u32(data, 0x64),
        "animation_timer": u32(data, 0xEC),
        # Raw +0x2c is the held-meat/passive-recovery reserve for mobile
        # units and the construction/progress union for structures.  Include
        # it explicitly so a matching meat drop cannot hide a mismatched
        # pickup or right-click remainder on the collecting unit.
        "action_mode": u32(data, 0x2C),
        "cargo": u32(data, 0x4C),
        "variant": u32(data, 0x54),
        "raw_30_44_slots": [u32(data, 0x30), u32(data, 0x34),
                            u32(data, 0x38), u32(data, 0x3C),
                            u32(data, 0x40), u32(data, 0x44)],
        "path": [i32(data, 0x6C), i32(data, 0x70)],
        "destination": [i32(data, 0x78), i32(data, 0x7C)],
        "current_cell": [i32(data, 0xC0), i32(data, 0xC4)],
        "next": [i32(data, 0xC8), i32(data, 0xCC)],
        "anchor": [i32(data, 0xD0), i32(data, 0xD4)],
        "direction": u32(data, 0xA8),
        "movement_flags": u32(data, 0xAC),
        "turn_ticks": u32(data, 0xB4),
        "world": [i32(data, 0xB8), i32(data, 0xBC)],
        "accumulator": u32(data, 0x110),
        "residual": [i32(data, 0x114), i32(data, 0x118)],
        "interpolation_bits": [u32(data, 0x11C), u32(data, 0x120)],
        "max_health": u32(data, 0x10),
        "health": u32(data, 0x18),
        "_target_or_value": u32(data, 0x68),
    }


def original_lists(memory):
    rows = {}
    orders = {"active": [], "lifecycle": []}
    for list_name, head in (("active", 0x007071D4),
                            ("lifecycle", 0x007071DC)):
        offset = memory.u32(head)
        seen = set()
        while offset and offset not in seen and len(seen) < 4096:
            seen.add(offset)
            slot = offset // 0x1D0
            data = memory.read(0x00A03FB8 + offset, 0x1D0)
            row = normalized_original_unit(data)
            row["distance_check_mode"] = u32(data, 0x10C)
            definition_offset = memory.u32(
                0x0087C050 + row["type"] * 4)
            row["interaction_bounds"] = [
                memory.i32(0x0087C670 + definition_offset),
                memory.i32(0x0087C674 + definition_offset),
            ]
            row["list"] = list_name
            raw_timer = row.pop("_raw_animation_or_work_timer")
            if list_name == "active":
                row["animation_frame"] = raw_timer
            else:
                # Raw +0x64 is animation_frame while active, then is reused as
                # the lifecycle work timer after the unit changes lists.
                row["work_timer"] = raw_timer
            rows[slot] = row
            orders[list_name].append(slot)
            offset = u32(data, 0x1CC)
    valid_slots = set(rows)
    for row in rows.values():
        raw = row.pop("_target_or_value")
        candidate = raw // 0x1D0 if raw and raw % 0x1D0 == 0 else None
        row["target_slot"] = candidate if candidate in valid_slots else None
    return rows, orders


def normalized_rebuild_unit(data):
    type_id = u32(data, unit_layout["type"])
    state = u32(data, unit_layout["command_state"])
    action_mode_gate = u32(data, unit_layout["action_mode_gate"])
    placement_state = (state & 0x00FFFFFF) in (0x23, 0x25)
    queued_size = unit_layout["queued_command_size"]
    pending_offset = unit_layout["active_payload"] - queued_size
    active_offset = unit_layout["active_payload"]
    deferred_offset = unit_layout["deferred_commands"]
    def queued(offset):
        return [
            u32(data, offset + unit_layout["queued_command_state"]),
            u32(data, offset + unit_layout["queued_command_x"]),
            i32(data, offset + unit_layout["queued_command_y"]),
            i32(data, offset + unit_layout["queued_command_value"]),
        ]
    row = {
        "type": type_id,
        "owner": u32(data, unit_layout["owner"]),
        "area_marker": u32(data, 0x14),
        "state": state,
        "command_value": (u32(data, unit_layout["command_value"])
                          if (state & 0x00FFFFFF) in
                          (0x4D, 0x4E, 0x50, 0x51, 0x82, 0x83) else 0),
        "placement_command_value": (
            u32(data, unit_layout["command_value"]) if placement_state else 0),
        "spawn_type": (
            u32(data, unit_layout["spawn_type"]) if placement_state else 0),
        "previous_state": u32(data, 0x58),
        "pending": queued(pending_offset),
        "active_payload": queued(active_offset),
        "deferred_count": u32(data, unit_layout["deferred_count"]),
        "deferred_first": queued(deferred_offset),
        "command_flags": u32(data, unit_layout["command_flags"]),
        "runtime_flags": u32(data, unit_layout["runtime_flags"]),
        "draw_flags": u32(data, unit_layout["draw_flags"]),
        "lockout": u32(data, unit_layout["command_lockout"]),
        "_animation_frame": u32(data, unit_layout["animation_frame"]),
        "_work_timer": u32(data, unit_layout["work_timer"]),
        "animation_timer": u32(data, 0xE8),
        "action_mode": u32(data, unit_layout["action_mode"]),
        "cargo": u32(data, unit_layout["cargo"]),
        "variant": u32(data, 0x7C),
        # Original raw +0x30 is both attachment slot 0 and the structure
        # construction gate.  The typed build deliberately splits the alias;
        # reconstruct the observable raw six-word view for comparison.
        "raw_30_44_slots": [
            action_mode_gate if type_id >= 0x60 and action_mode_gate == 1
            else u32(data, 0x16C),
            u32(data, 0x170), u32(data, 0x174), u32(data, 0x178),
            u32(data, 0x18C), u32(data, 0x190),
        ],
        "path": [i32(data, unit_layout["path_target_x"]),
                 i32(data, unit_layout["path_target_y"])],
        "current_cell": [i32(data, 0xB4), i32(data, 0xB8)],
        "next": [i32(data, 0xCC), i32(data, 0xD0)],
        "anchor": [i32(data, 0xD4), i32(data, 0xD8)],
        "direction": u32(data, 0xE0),
        "movement_flags": u32(data, unit_layout["movement_flags"]),
        "distance_check_mode": u32(data, 0x144),
        "interaction_bounds": [
            i32(data, unit_layout["definition"] + 0xE8),
            i32(data, unit_layout["definition"] + 0xEC),
        ],
        "turn_ticks": u32(data, 0xF0),
        "world": [i32(data, unit_layout["x"]),
                  i32(data, unit_layout["y"])],
        "accumulator": u32(data, 0x108),
        "residual": [i32(data, 0x10C), i32(data, 0x110)],
        "interpolation_bits": [u32(data, 0x114), u32(data, 0x118)],
        "max_health": u32(data, unit_layout["max_health"]),
        "health": u32(data, unit_layout["health"]),
        "_target_pointer": u64(data, unit_layout["target"]),
    }
    if type_id >= 0x60:
        # Raw +0x78/+0x7c are cell palette/frame scratch for high types.
        # The typed runtime stores those immediately after destination_x/y.
        row["destination"] = [
            u32(data, unit_layout["destination_x"] + 8),
            u32(data, unit_layout["destination_x"] + 12),
        ]
    else:
        row["destination"] = [
            i32(data, unit_layout["destination_x"]),
            i32(data, unit_layout["destination_y"]),
        ]
    return row


def rebuild_lists(memory, movement):
    rows = {}
    orders = {"active": [], "lifecycle": []}
    pending = []
    pointer_slots = {}
    id_slots = {}
    for list_name, field in (("active", "active_units"),
                             ("lifecycle", "lifecycle_units")):
        pointers = pointer_vector(memory, movement + context_layout[field])
        for pointer in pointers:
            if not pointer:
                continue
            # Definition-derived fields used by the focused placement audit
            # live beyond the fixed runtime header at +0x278.
            data = memory.read(pointer, unit_layout["size"])
            slot = u32(data, unit_layout["runtime_slot"])
            pointer_slots[pointer] = slot
            pending.append((list_name, pointer, slot, data))
            orders[list_name].append(slot)
    for list_name, pointer, slot, data in pending:
        id_slots[u32(data, unit_layout["id"])] = slot
        row = normalized_rebuild_unit(data)
        row["list"] = list_name
        animation_frame = row.pop("_animation_frame")
        work_timer = row.pop("_work_timer")
        if list_name == "active":
            row["animation_frame"] = animation_frame
        else:
            row["work_timer"] = work_timer
        target = row.pop("_target_pointer")
        row["target_slot"] = pointer_slots.get(target)
        rows[slot] = row
    return rows, orders, pointer_slots, id_slots


ORIGINAL_UNIT_EFFECT_HEAD = 0x007071CC
ORIGINAL_UNIT_EFFECT_POOL = 0x01426C00
ORIGINAL_UNIT_EFFECT_STRIDE = 0xA8
ORIGINAL_MAP_EFFECT_HEAD = 0x007071E0
ORIGINAL_MAP_EFFECT_POOL = 0x012CE970
ORIGINAL_MAP_EFFECT_STRIDE = 0x3C


def original_reference_slot(raw):
    return (raw // 0x1D0
            if raw and raw % 0x1D0 == 0 else None)


def original_unit_effects(memory):
    rows = []
    seen = set()
    offset = memory.u32(ORIGINAL_UNIT_EFFECT_HEAD)
    while (offset and offset not in seen and
           offset % ORIGINAL_UNIT_EFFECT_STRIDE == 0 and len(rows) < 1024):
        seen.add(offset)
        data = memory.read(
            ORIGINAL_UNIT_EFFECT_POOL + offset, ORIGINAL_UNIT_EFFECT_STRIDE)
        effect_id = u32(data, 0x00)
        flags = u32(data, 0x08)
        raw_counter0c = u32(data, 0x0C)
        raw_state10 = u32(data, 0x10)
        source = u32(data, 0x18)
        target = u32(data, 0x1C)
        # Low-id effects alias the raw +0x10 word by phase.  During an
        # ordinary active projectile it is the remaining path budget, not an
        # animation frame.  Normalize the original pool to the reconstruction's
        # typed fields before comparing snapshots.
        if effect_id < 0x3D:
            if effect_id == 0x1E:
                tick = raw_state10
                frame = raw_counter0c
                effect_range = 0
            elif effect_id == 0x20 and flags & 0x400:
                tick = raw_counter0c
                frame = raw_state10
                effect_range = 0
            elif effect_id == 0x27:
                tick = raw_counter0c
                frame = 0
                effect_range = 0
            elif flags & 0x02:
                tick = raw_counter0c
                frame = 0
                effect_range = 0
            else:
                tick = raw_counter0c
                frame = 0
                effect_range = raw_state10 if not flags & 0x80 else 0
        else:
            tick = raw_counter0c
            frame = raw_state10
            effect_range = 0
        rows.append({
            "slot": offset // ORIGINAL_UNIT_EFFECT_STRIDE - 1,
            "effect_id": effect_id,
            "direction": u32(data, 0x04),
            "flags": flags,
            "tick": tick,
            "frame": frame,
            "range": effect_range,
            "amount": u32(data, 0x14),
            "source_slot": original_reference_slot(source),
            "target_slot": original_reference_slot(target),
            "x": i32(data, 0x20),
            "y": i32(data, 0x24),
        })
        offset = u32(data, 0xA4)
    return rows


def rebuild_unit_effects(memory, runtime, id_slots):
    state = runtime + unit_effects_offset
    size = unit_effect_layout["effect_size"]
    begin, end, capacity = struct.unpack(
        "<QQQ", memory.read(state + unit_effect_layout["slots"], 24))
    if (not begin or end < begin or capacity < end or
            (end - begin) % size != 0):
        return []
    count = (end - begin) // size
    rows = []
    for index in index_vector(
            memory, state + unit_effect_layout["active_indices"], 8):
        if index >= count:
            continue
        data = memory.read(begin + index * size, size)
        if not data[unit_effect_layout["active"]]:
            continue
        effect_id = u32(data, unit_effect_layout["id"])
        flags = u32(data, unit_effect_layout["flags"])
        source = u32(data, unit_effect_layout["source"])
        target = u32(data, unit_effect_layout["target"])
        typed_tick = u32(data, unit_effect_layout["tick"])
        typed_frame = u32(data, unit_effect_layout["effect_frame"])
        effect_range = u32(data, unit_effect_layout.get("range", 0x60))
        # Match original_unit_effects(): low-id effects in the 0x02 phase
        # expose raw +0x10 through a different union arm, so the typed
        # reconstruction's retained path budget is not semantic `range`.
        if effect_id >= 0x3D or flags & 0x80 or (
                effect_id < 0x3D and flags & 0x02):
            effect_range = 0
        if effect_id < 0x3D:
            if effect_id == 0x1E:
                tick = typed_tick
                frame = typed_frame
            elif effect_id == 0x20 and flags & 0x400:
                tick = typed_frame
                frame = typed_tick
            elif effect_id == 0x27:
                tick = typed_frame
                frame = 0
            else:
                tick = typed_tick
                frame = 0
        else:
            tick = typed_tick
            frame = typed_frame
        rows.append({
            "slot": index,
            "effect_id": effect_id,
            "direction": u32(data, unit_effect_layout["direction"]),
            "flags": flags,
            "tick": tick,
            "frame": frame,
            "range": effect_range,
            "amount": u32(data, unit_effect_layout["amount"]),
            "source_slot": id_slots.get(source) if source else None,
            "target_slot": id_slots.get(target) if target else None,
            "x": i32(data, unit_effect_layout["x"]),
            "y": i32(data, unit_effect_layout["y"]),
        })
    return rows


def original_map_effects(memory):
    rows = []
    seen = set()
    offset = memory.u32(ORIGINAL_MAP_EFFECT_HEAD)
    while (offset and offset not in seen and
           offset % ORIGINAL_MAP_EFFECT_STRIDE == 0 and len(rows) < 512):
        seen.add(offset)
        data = memory.read(
            ORIGINAL_MAP_EFFECT_POOL + offset, ORIGINAL_MAP_EFFECT_STRIDE)
        linked = u32(data, 0x10)
        rows.append({
            "slot": offset // ORIGINAL_MAP_EFFECT_STRIDE,
            "effect_id": u32(data, 0x00),
            "flags": u32(data, 0x0C),
            "linked_slot": original_reference_slot(linked),
            "x": i32(data, 0x24),
            "y": i32(data, 0x28),
            "frame_timer": u32(data, 0x2C),
            "repeat_count": u32(data, 0x30),
        })
        offset = u32(data, 0x38)
    return rows


def rebuild_map_effects(memory, runtime, pointer_slots):
    context = runtime + map_effect_context_offset
    size = map_effect_layout["instance_size"]
    begin, end, capacity = struct.unpack(
        "<QQQ", memory.read(context + map_effect_layout["effects"], 24))
    if (not begin or end < begin or capacity < end or
            (end - begin) % size != 0):
        return []
    count = (end - begin) // size
    rows = []
    for index in index_vector(
            memory, context + map_effect_layout["active_indices"], 4):
        if index >= count:
            continue
        data = memory.read(begin + index * size, size)
        if not data[map_effect_layout["instance_active"]]:
            continue
        linked = u64(data, map_effect_layout["instance_linked_unit"])
        rows.append({
            "slot": u32(data, map_effect_layout["instance_id"]),
            "effect_id": u32(data, map_effect_layout["instance_effect_id"]),
            "flags": u32(data, map_effect_layout["instance_flags"]),
            "linked_slot": pointer_slots.get(linked) if linked else None,
            "x": i32(data, map_effect_layout["instance_x"]),
            "y": i32(data, map_effect_layout["instance_y"]),
            "frame_timer": u32(
                data, map_effect_layout["instance_frame_timer"]),
            "repeat_count": u32(
                data, map_effect_layout["instance_repeat_count"]),
        })
    return rows


def original_owner_ai(memory, owners):
    result = {}
    for owner in owners:
        preferred = memory.u32(0x012334C8 + owner * 4)
        preferred_point = [memory.i32(0x012334E8 + owner * 8),
                           memory.i32(0x012334EC + owner * 8)]
        strategic_point = [memory.i32(0x01238EE8 + owner * 8),
                           memory.i32(0x01238EEC + owner * 8)]
        result[str(owner)] = {
            "script_halted": memory.u32(0x0122FF28 + owner * 4),
            "resource_budget_percent": memory.u32(0x01230628 + owner * 4),
            "profile_counter": memory.u32(0x01230928 + owner * 4),
            "script_cycle_counter": memory.u32(0x01233528 + owner * 4),
            "previous_script_cycle_counter": memory.i32(
                0x01233548 + owner * 4),
            "last_timing_frame": memory.u32(0x01233588 + owner * 4),
            "production_pause_flag": memory.u32(0x012393E8 + owner * 4),
            "profile_state_flag": memory.u32(0x01239C08 + owner * 4),
            "profile_age": memory.u32(0x01239C28 + owner * 4),
            "owner_faction": memory.u32(0x007251A4 + owner * 4),
            "population_used_internal": memory.u32(0x007259C4 + owner * 4),
            "population_reserved_internal": memory.u32(
                0x00725A14 + owner * 4),
            "slot_state": memory.u32(0x007251F4 + owner * 4),
            "lobby_state": memory.read(0x012448F0 + owner, 1)[0],
            "relation_mask": memory.u32(0x00725334 + owner * 4),
            "start": [memory.i32(0x007253D4 + owner * 8),
                      memory.i32(0x007253D8 + owner * 8)],
            "nearest_hostile": memory.i32(0x012334A8 + owner * 4),
            "primary_target_owner": memory.i32(0x012334A8 + owner * 4),
            "support_mode": memory.u32(0x01230588 + owner * 4),
            "support_anchor": [memory.i32(0x012305C8 + owner * 8),
                               memory.i32(0x012305CC + owner * 8)],
            "route_count": memory.u32(0x0122FFA8 + owner * 4),
            "preferred_target_slot": (
                (preferred - 0x00A03FB8) // 0x1D0
                if preferred >= 0x00A03FB8 and
                (preferred - 0x00A03FB8) % 0x1D0 == 0 else None),
            "preferred_target_point": preferred_point,
            "strategic_point": strategic_point,
            "has_preferred_target": preferred != 0,
            "has_strategic_point": (strategic_point[0] != -1 and
                                    strategic_point[1] != -1),
        }
    return result


def rebuild_owner_ai(memory, runtime, owners, pointer_slots):
    result = {}
    player = runtime + player_slots_offset
    ai = runtime + owner_ai_offset + owner_ai_layout["owners"]
    routes = runtime + owner_transport_routes_offset
    strategic = runtime + owner_strategic_targets_offset
    for owner in owners:
        slot = ai + owner * owner_ai_layout["slot_size"]
        route = routes + owner * owner_transport_route_layout["size"]
        target = strategic + owner * owner_strategic_target_layout["size"]
        preferred = memory.read(
            target + owner_strategic_target_layout["preferred_target"], 8)
        preferred = struct.unpack("<Q", preferred)[0]
        preferred_point = [memory.i32(target + 0x10),
                           memory.i32(target + 0x14)]
        strategic_point = [memory.i32(target + 0x18),
                           memory.i32(target + 0x1C)]
        result[str(owner)] = {
            "script_halted": memory.u32(slot),
            "resource_budget_percent": memory.u32(
                slot + owner_ai_layout["resource_budget_percent"]),
            "profile_counter": memory.u32(
                slot + owner_ai_layout["profile_counter"]),
            "script_cycle_counter": memory.u32(slot + 0x598),
            "previous_script_cycle_counter": memory.i32(slot + 0x59C),
            "last_timing_frame": memory.u32(slot + 0x5A4),
            "production_pause_flag": memory.u32(
                slot + owner_ai_layout["production_pause_flag"]),
            "profile_state_flag": memory.u32(
                slot + owner_ai_layout["profile_state_flag"]),
            "profile_age": memory.u32(slot + 0x65C),
            "owner_faction": memory.u32(
                runtime + owner_ai_offset +
                owner_ai_layout["owner_faction_ids"] + owner * 4),
            "population_used_internal": memory.u32(
                runtime + owner_ai_offset +
                owner_ai_layout["owner_population_used"] + owner * 4),
            "population_reserved_internal": memory.u32(
                runtime + owner_ai_offset +
                owner_ai_layout["owner_population_reserved"] + owner * 4),
            "slot_state": memory.read(
                player + player_slots_layout["slot_states"] + owner, 1)[0],
            "lobby_state": memory.read(
                player + player_slots_layout["lobby_states"] + owner, 1)[0],
            "relation_mask": memory.u32(
                player + player_slots_layout["relation_masks"] + owner * 4),
            "start": [memory.i32(
                          player + player_slots_layout["start_x"] + owner * 4),
                      memory.i32(
                          player + player_slots_layout["start_y"] + owner * 4)],
            "nearest_hostile": memory.i32(
                player + player_slots_layout["nearest_hostile"] + owner * 4),
            "primary_target_owner": memory.i32(
                slot + owner_ai_layout["primary_target_owner"]),
            "support_mode": memory.u32(
                slot + owner_ai_layout["support_mode"]),
            "support_anchor": [memory.i32(
                                   slot + owner_ai_layout["support_anchor"]),
                               memory.i32(
                                   slot + owner_ai_layout["support_anchor"] + 4)],
            "route_count": memory.u32(
                route + owner_transport_route_layout["route_count"]),
            "preferred_target_slot": pointer_slots.get(preferred),
            "preferred_target_point": preferred_point,
            "strategic_point": strategic_point,
            "has_preferred_target": bool(memory.read(target + 0x20, 1)[0]),
            "has_strategic_point": bool(memory.read(target + 0x21, 1)[0]),
        }
    return result


def original_transport_queues(memory, owners):
    result = {}
    for owner in owners:
        slots = {}
        for index in range(0x20):
            address = 0x012336C8 + owner * 0xB00 + index * 0x58
            state = memory.u32(address + 4)
            count = memory.u32(address)
            if state == 0 and count == 0:
                continue
            slots[str(index)] = {
                "count": count,
                "state": state,
                "completed": memory.u32(address + 8),
                "phase_ticks": memory.u32(address + 12),
                "aux": memory.u32(address + 16),
                "target": [memory.i32(address + 24),
                           memory.i32(address + 28)],
                "route": memory.u32(address + 32),
                "linked_group": memory.u32(address + 36),
            }
        result[str(owner)] = slots
    return result


def rebuild_transport_queues(memory, runtime, owners):
    result = {}
    queues = runtime + owner_transport_queues_offset
    for owner in owners:
        slots = {}
        for index in range(0x20):
            address = queues + owner * 0x500 + index * 0x28
            state = memory.u32(address + 4)
            count = memory.u32(address)
            if state == 0 and count == 0:
                continue
            slots[str(index)] = {
                "count": count,
                "state": state,
                "completed": memory.u32(address + 8),
                "phase_ticks": memory.u32(address + 12),
                "aux": memory.u32(address + 16),
                "target": [memory.i32(address + 24),
                           memory.i32(address + 28)],
                "route": memory.u32(address + 32),
                "linked_group": memory.u32(address + 36),
            }
        result[str(owner)] = slots
    return result


def original_economy(memory, owners):
    result = {}
    for owner in owners:
        result[str(owner)] = {
            "primary": memory.u32(0x00725244 + owner * 4),
            "secondary": memory.u32(0x00725294 + owner * 4),
            "population_limit": memory.u32(0x007226D8 + owner * 4),
            "population_used": memory.u32(0x007259C4 + owner * 4),
            "population_reserved": memory.u32(0x00725A14 + owner * 4),
            "resource_score": memory.u32(0x007072AC + owner * 4),
            "unit_kills": memory.u32(0x0070726C + owner * 4),
            "building_kills": memory.u32(0x0070728C + owner * 4),
            "unit_lost": memory.u32(0x0070722C + owner * 4),
            "building_lost": memory.u32(0x0070724C + owner * 4),
        }
    return result


def rebuild_economy(memory, runtime, owners):
    lifecycle = runtime + lifecycle_offset
    counters = runtime + owner_counters_offset
    result = {}
    for owner in owners:
        result[str(owner)] = {
            "primary": memory.u32(
                lifecycle + lifecycle_layout["primary"] + owner * 4),
            "secondary": memory.u32(
                lifecycle + lifecycle_layout["secondary"] + owner * 4),
            "population_limit": memory.u32(
                lifecycle + lifecycle_layout["population_limit"] + owner * 4),
            "population_used": memory.u32(
                lifecycle + lifecycle_layout["population_used"] + owner * 4),
            "population_reserved": memory.u32(
                lifecycle + lifecycle_layout["population_reserved"] + owner * 4),
            "resource_score": memory.u32(
                counters + owner_counter_layout["resource_score_table"] +
                owner * 4),
            "unit_kills": memory.u32(lifecycle + 0x248 + owner * 4),
            "building_kills": memory.u32(lifecycle + 0x288 + owner * 4),
            "unit_lost": memory.u32(lifecycle + 0x2C8 + owner * 4),
            "building_lost": memory.u32(lifecycle + 0x308 + owner * 4),
        }
    return result


def original_ai_demand(memory, owners):
    result = {}
    for owner in owners:
        result[str(owner)] = {
            "base_60_a9": [
                memory.u32(0x01230A28 + owner * 0x2A8 + unit_type * 4)
                for unit_type in range(0x60, 0xAA)
            ],
            "shadow_60_a9": [
                memory.u32(0x01231F68 + owner * 0x2A8 + unit_type * 4)
                for unit_type in range(0x60, 0xAA)
            ],
        }
    return result


def rebuild_ai_demand(memory, runtime, owners):
    owner_ai = runtime + owner_ai_offset + owner_ai_layout["owners"]
    result = {}
    for owner in owners:
        slot = owner_ai + owner * owner_ai_layout["slot_size"]
        result[str(owner)] = {
            "base_60_a9": [
                memory.u32(
                    slot + owner_ai_layout["unit_demand"] + unit_type * 4)
                for unit_type in range(0x60, 0xAA)
            ],
            "shadow_60_a9": [
                memory.u32(
                    slot + owner_ai_layout["unit_demand_shadow"] +
                    unit_type * 4)
                for unit_type in range(0x60, 0xAA)
            ],
        }
    return result


def first_difference(left, right, path="state"):
    if type(left) is not type(right):
        return path, left, right
    if isinstance(left, dict):
        # Rows first gives a directly actionable unit field when both a row
        # and a derived order/count summary change on the same tick.
        keys = sorted(set(left) | set(right), key=str)
        if path == "state":
            keys.sort(key=lambda key: {"rows": 0, "rng": 1}.get(key, 2))
        for key in keys:
            child = f"{path}.{key}"
            if key not in left or key not in right:
                return child, left.get(key), right.get(key)
            difference = first_difference(left[key], right[key], child)
            if difference is not None:
                return difference
        return None
    if isinstance(left, list):
        if len(left) != len(right):
            return f"{path}.length", len(left), len(right)
        for index, (left_item, right_item) in enumerate(zip(left, right)):
            difference = first_difference(
                left_item, right_item, f"{path}[{index}]")
            if difference is not None:
                return difference
        return None
    if left != right:
        return path, left, right
    return None


original = Memory(ORIGINAL_PID)
rebuild = Memory(REBUILD_PID)
runtime = REBUILD_BASE + runtime_rva
movement = runtime + movement_offset
random_state = runtime + random_offset
# GameplayRuntime::gameplay_visibility_grid offset for the deployed 6702
# diagnostic build.  The owner vector starts at GameplayVisibilityGrid +0x38.
# Unlike current/previous fog bits this plane is owner-relative and therefore
# is expected to match between the original host and rebuilt client.
visibility_grid = runtime + 0x9D480
FOCUS_RESIDUE_SLOT = 144
rebuild_focus_pointer = 0


def original_focus_residue(memory):
    data = memory.read(
        0x00A03FB8 + FOCUS_RESIDUE_SLOT * 0x1D0, 0x1D0)
    result = {
        "type": u32(data, 0x00),
        "owner": u32(data, 0x04),
        "state": u32(data, 0x60),
        "destination": [i32(data, 0x78), i32(data, 0x7C)],
        "path": [i32(data, 0x6C), i32(data, 0x70)],
        "next": [i32(data, 0xC8), i32(data, 0xCC)],
        "world": [i32(data, 0xB8), i32(data, 0xBC)],
        "animation_or_work": u32(data, 0x64),
        "animation_timer": u32(data, 0xEC),
        "command_lockout": u32(data, 0xF4),
    }
    tracked_values = (
        result["type"], result["owner"], result["state"],
        *result["destination"], *result["path"], *result["next"],
        *result["world"], result["animation_or_work"],
        result["animation_timer"], result["command_lockout"],
    )
    # Before fixed slot 144 has ever been allocated, the original exposes a
    # zero-filled raw row while the reconstruction has no pointer for it yet.
    # Both mean "no residue exists"; retaining zero-vs-null would make this
    # optional diagnostic abort before the first semantic unit snapshot.
    if not any(tracked_values):
        return None
    return result


def rebuild_focus_residue(memory, pointer):
    if not pointer:
        return None
    data = memory.read(pointer, unit_layout["size"])
    type_id = u32(data, unit_layout["type"])
    if type_id >= 0x60:
        destination = [
            u32(data, unit_layout["destination_x"] + 8),
            u32(data, unit_layout["destination_x"] + 12),
        ]
    else:
        destination = [
            i32(data, unit_layout["destination_x"]),
            i32(data, unit_layout["destination_y"]),
        ]
    return {
        "type": type_id,
        "owner": u32(data, unit_layout["owner"]),
        "state": u32(data, unit_layout["command_state"]),
        "destination": destination,
        "path": [i32(data, unit_layout["path_target_x"]),
                 i32(data, unit_layout["path_target_y"])],
        "next": [i32(data, 0xCC), i32(data, 0xD0)],
        "world": [i32(data, unit_layout["x"]),
                  i32(data, unit_layout["y"])],
        "animation_or_work": u32(data, unit_layout["animation_frame"]),
        "animation_timer": u32(data, 0xE8),
        "command_lockout": u32(data, unit_layout["command_lockout"]),
    }


def add_owner_visibility_words(name, memory, rows):
    if name == "original":
        width = 256
        height = 256
        owner_pointer = 0x007D8D40
    else:
        width = memory.u32(visibility_grid)
        height = memory.u32(visibility_grid + 4)
        owner_pointer = struct.unpack(
            "<Q", memory.read(visibility_grid + 0x38, 8))[0]
        if not owner_pointer:
            return
    for row in rows.values():
        world_x, world_y = row["current_cell"]
        tile_x = (world_x & 0xFFFFFFFF) >> 5
        tile_y = (world_y & 0xFFFFFFFF) >> 5
        if tile_x < width and tile_y < height:
            row["owner_visibility_cell"] = memory.u32(
                owner_pointer + (tile_y * width + tile_x) * 4)
        else:
            row["owner_visibility_cell"] = None

sides = {
    "original": {
        "memory": original,
        "frame_address": 0x007071A4,
        "last_seen": None,
        "changed_at": time.monotonic(),
        "next_candidate_at": time.monotonic(),
        "candidate": None,
        "candidate_digest": None,
        "candidate_repeats": 0,
        "candidate_started_at": None,
        "snapshots": {},
    },
    "rebuild": {
        "memory": rebuild,
        "frame_address": REBUILD_BASE + loop_rva +
                         loop_layout["simulation_frame"],
        "last_seen": None,
        "changed_at": time.monotonic(),
        "next_candidate_at": time.monotonic(),
        "candidate": None,
        "candidate_digest": None,
        "candidate_repeats": 0,
        "candidate_started_at": None,
        "snapshots": {},
    },
}


def capture_frozen_body(name, side):
    global rebuild_focus_pointer
    started = time.perf_counter()
    memory = side["memory"]
    frame_before = memory.u32(side["frame_address"])
    if name == "original":
        rng = [memory.u32(0x007071B8), memory.u32(0x007071BC),
               memory.u32(0x007071C0)]
        rows, orders = original_lists(memory)
        owners = sorted({row["owner"] for row in rows.values()
                         if row["owner"] < 8})
        economy = original_economy(memory, owners)
        ai_demand = original_ai_demand(memory, owners)
        owner_ai = original_owner_ai(memory, owners)
        transport_queues = original_transport_queues(memory, owners)
        unit_effects = original_unit_effects(memory)
        map_effects = original_map_effects(memory)
        # DAT_00710ec4 is production-completion effect slot 5.  Type 33,
        # owner 2 is the first post-target-selection recovery divergence.
        recovery_effect_2_33 = memory.i32(
            0x00710EC4 + 2 * 0x2A8 + 33 * 4)
        movement_effect_2_48 = memory.i32(
            0x00713944 + 2 * 0x2A8 + 48 * 4)
        focus_residue = original_focus_residue(memory)
    else:
        rng = [memory.u32(random_state), memory.u32(random_state + 4),
               memory.u32(random_state + 8)]
        rows, orders, pointer_slots, id_slots = rebuild_lists(memory, movement)
        for pointer, slot in pointer_slots.items():
            if slot == FOCUS_RESIDUE_SLOT:
                rebuild_focus_pointer = pointer
                break
        owners = sorted({row["owner"] for row in rows.values()
                         if row["owner"] < 8})
        economy = rebuild_economy(memory, runtime, owners)
        ai_demand = rebuild_ai_demand(memory, runtime, owners)
        owner_ai = rebuild_owner_ai(memory, runtime, owners, pointer_slots)
        transport_queues = rebuild_transport_queues(memory, runtime, owners)
        unit_effects = rebuild_unit_effects(memory, runtime, id_slots)
        map_effects = rebuild_map_effects(memory, runtime, pointer_slots)
        # ProductionOrderRuntimeState: completion_effect_totals begins at
        # +0x1d40 and each effect table is 0x1540 bytes.
        recovery_effect_2_33 = memory.i32(
            runtime + production_runtime_offset + 0x1D40 +
            5 * 0x1540 + 2 * 0x2A8 + 33 * 4)
        movement_effect_2_48 = memory.i32(
            runtime + production_runtime_offset + 0x1D40 +
            7 * 0x1540 + 2 * 0x2A8 + 48 * 4)
        focus_residue = rebuild_focus_residue(memory, rebuild_focus_pointer)
    add_owner_visibility_words(name, memory, rows)
    # Raw draw_flags is local pointer-command feedback.  FUN_004da02c writes
    # DAT_008629de[action] directly to the object hit by the peer that received
    # the physical input; the synchronized gameplay packet does not carry it.
    # Values 0x80..0x88 are the A-command red-flash countdown, while the low
    # values are the other selector feedback countdowns.  Compare all of them
    # in dedicated same-input rendering probes, never between different local
    # players in this cross-peer simulation audit.
    for row in rows.values():
        row["draw_flags"] = 0
    # Owner-AI planner storage is live only for PlayerSlotState::
    # player_controlled (raw value 1, the computer slot in this fixture).
    # Human-active slots retain implementation-specific reset sentinels.
    for owner_state in owner_ai.values():
        if owner_state["slot_state"] != 1:
            owner_state["primary_target_owner"] = None
            owner_state["support_mode"] = None
            owner_state["support_anchor"] = None
            owner_state["route_count"] = None
            owner_state["preferred_target_slot"] = None
            owner_state["preferred_target_point"] = None
            owner_state["strategic_point"] = None
            owner_state["has_preferred_target"] = None
            owner_state["has_strategic_point"] = None
    frame_after = memory.u32(side["frame_address"])
    if frame_before != frame_after:
        return None
    state = {
        "rng": rng,
        "rows": {str(slot): rows[slot] for slot in sorted(rows)},
        "active_order": orders["active"],
        "lifecycle_order": orders["lifecycle"],
        "economy": economy,
        "ai_demand": ai_demand,
        "owner_ai": owner_ai,
        "transport_queues": transport_queues,
        "unit_effects": unit_effects,
        "map_effects": map_effects,
        "recovery_effect_2_33": recovery_effect_2_33,
        "movement_effect_2_48": movement_effect_2_48,
        "focus_residue_144": focus_residue,
    }
    return {
        "side": name,
        "frame": frame_after,
        "state": state,
        "state_sha256": digest(state),
        "capture_ms": round((time.perf_counter() - started) * 1000.0, 3),
        "captured_ns": time.time_ns(),
    }


def capture(name, side):
    # Reading every active unit, AI table, and effect list takes long enough
    # for the simulation thread to cross several mutation sites.  Freeze this
    # peer for the read so list membership and row fields form one real state,
    # then resume immediately.  The quiet-tail delay above still selects the
    # comparable phase of each independently running peer.
    memory = side["memory"]
    memory.suspend()
    try:
        return capture_frozen_body(name, side)
    finally:
        memory.resume()


started = time.monotonic()
first_pair = None
last_pair = None
exact_pairs = 0
pair_gaps = []
previous_exact = None
pending_divergence = None
pending_rng_divergence = None
transient_divergence_count = 0
phase_skew_pair_count = 0
coverage = {
    "max_active_units": 0,
    "max_lifecycle_units": 0,
    "max_unit_effects": 0,
    "max_map_effects": 0,
    "frames_with_lifecycle": 0,
    "frames_with_unit_effects": 0,
    "frames_with_map_effects": 0,
    "frames_with_meat_map_effects": 0,
    "frames_with_mobile_action_mode": 0,
    "max_mobile_action_mode": 0,
    "frames_with_kill_or_loss": 0,
    "unit_types": set(),
    "command_states": set(),
    "unit_effect_ids": set(),
    "map_effect_ids": set(),
    "owners": set(),
    "owner_ai_digests": set(),
}
detail_path = os.path.splitext(RESULT_PATH)[0] + "-detail.json"
os.makedirs(os.path.dirname(os.path.abspath(RESULT_PATH)), exist_ok=True)


def update_coverage(state):
    coverage["max_active_units"] = max(
        coverage["max_active_units"], len(state["active_order"]))
    coverage["max_lifecycle_units"] = max(
        coverage["max_lifecycle_units"], len(state["lifecycle_order"]))
    coverage["max_unit_effects"] = max(
        coverage["max_unit_effects"], len(state["unit_effects"]))
    coverage["max_map_effects"] = max(
        coverage["max_map_effects"], len(state["map_effects"]))
    coverage["frames_with_lifecycle"] += bool(state["lifecycle_order"])
    coverage["frames_with_unit_effects"] += bool(state["unit_effects"])
    coverage["frames_with_map_effects"] += bool(state["map_effects"])
    coverage["frames_with_meat_map_effects"] += any(
        1 <= effect["effect_id"] <= 4 for effect in state["map_effects"])
    mobile_action_modes = [
        row["action_mode"] for row in state["rows"].values()
        if row["type"] < 0x60 and row["action_mode"] != 0
    ]
    coverage["frames_with_mobile_action_mode"] += bool(mobile_action_modes)
    if mobile_action_modes:
        coverage["max_mobile_action_mode"] = max(
            coverage["max_mobile_action_mode"], max(mobile_action_modes))
    coverage["frames_with_kill_or_loss"] += any(
        any(owner[key] for key in
            ("unit_kills", "building_kills", "unit_lost", "building_lost"))
        for owner in state["economy"].values())
    for row in state["rows"].values():
        coverage["unit_types"].add(row["type"])
        coverage["command_states"].add(row["state"])
        if row["owner"] < 8:
            coverage["owners"].add(row["owner"])
    coverage["unit_effect_ids"].update(
        effect["effect_id"] for effect in state["unit_effects"])
    coverage["map_effect_ids"].update(
        effect["effect_id"] for effect in state["map_effects"])
    coverage["owner_ai_digests"].add(digest(state["owner_ai"]))


def coverage_summary():
    return {
        key: (sorted(value) if isinstance(value, set) else value)
        for key, value in coverage.items()
        if key != "owner_ai_digests"
    } | {"owner_ai_distinct_states": len(coverage["owner_ai_digests"])}


def terminal_summary():
    if previous_exact is None:
        return None
    snapshot = previous_exact["original"]
    state = snapshot["state"]
    return {
        "frame": snapshot["frame"],
        "state_sha256": snapshot["state_sha256"],
        "rng": state["rng"],
        "active_unit_count": len(state["active_order"]),
        "lifecycle_unit_count": len(state["lifecycle_order"]),
        "unit_effect_count": len(state["unit_effects"]),
        "map_effect_count": len(state["map_effects"]),
        "economy": state["economy"],
        "owner_ai": state["owner_ai"],
        "owner_ai_sha256": digest(state["owner_ai"]),
    }

try:
    with open(JOURNAL_PATH, "w", encoding="utf-8", newline="\n") as journal:
        while time.monotonic() - started < MAX_SECONDS:
            if os.path.exists(STOP_PATH):
                summary = {
                    "pass": True,
                    "reason": "driver stop marker reached with no observed divergence",
                    "sha256": layout.get("sha256"),
                    "exact_pair_count": exact_pairs,
                    "first_exact_frame": first_pair,
                    "last_exact_frame": last_pair,
                    "pair_gaps": pair_gaps,
                    "semantic_coverage": coverage_summary(),
                    "terminal_exact_state": terminal_summary(),
                    "transient_divergence_count": transient_divergence_count,
                    "phase_skew_pair_count": phase_skew_pair_count,
                    "journal_path": JOURNAL_PATH,
                }
                atomic_json(RESULT_PATH, summary)
                break

            now = time.monotonic()
            for name, side in sides.items():
                # During large map transitions the rebuild can briefly replace
                # committed runtime backing while its unit catalog is loaded.
                # ReadProcessMemory then reports ERROR_PARTIAL_COPY even though
                # the process remains alive and the same address becomes valid
                # again a few milliseconds later.  Treat that as a torn sample,
                # just like the capture-level stability checks below.
                try:
                    frame = side["memory"].u32(side["frame_address"])
                except OSError:
                    side["next_candidate_at"] = (
                        time.monotonic() + CANDIDATE_INTERVAL_SECONDS)
                    continue
                if side["last_seen"] != frame:
                    # The frame counter advances before the original has
                    # necessarily finished every unit update.  Finalize only
                    # after observing the following frame; the retained
                    # candidate is the latest stable read of the completed
                    # preceding frame.
                    candidate = side["candidate"]
                    if (candidate is not None and
                            candidate["frame"] == side["last_seen"]):
                        side["snapshots"][candidate["frame"]] = candidate
                    side["last_seen"] = frame
                    side["changed_at"] = now
                    side["next_candidate_at"] = now + SETTLE_SECONDS
                    side["candidate"] = None
                    side["candidate_digest"] = None
                    side["candidate_repeats"] = 0
                    side["candidate_started_at"] = None
                if (frame <= 0 or now < side["next_candidate_at"]):
                    continue
                # Suspending both peers on every simulation frame perturbs
                # their independent wall-clock scheduling enough to expose
                # harmless AI/frontier phase skew.  Persistent deterministic
                # differences survive sparse sampling; transient frontiers
                # reconverge before the next eight-frame checkpoint.
                if (frame < SAMPLE_START_FRAME or
                        frame % SAMPLE_FRAME_INTERVAL != 0):
                    continue
                try:
                    snapshot = capture(name, side)
                except OSError:
                    side["next_candidate_at"] = (
                        time.monotonic() + CANDIDATE_INTERVAL_SECONDS)
                    continue
                if snapshot is None or snapshot["frame"] != frame:
                    side["next_candidate_at"] = (
                        time.monotonic() + CANDIDATE_INTERVAL_SECONDS)
                    continue
                snapshot_key = (
                    snapshot["frame"], snapshot["state_sha256"])
                if side["candidate_digest"] == snapshot_key:
                    side["candidate_repeats"] += 1
                else:
                    side["candidate_digest"] = snapshot_key
                    side["candidate_repeats"] = 1
                    side["candidate_started_at"] = time.monotonic()
                    side["candidate"] = None
                if (side["candidate_repeats"] >=
                        REQUIRED_IDENTICAL_CANDIDATES and
                        side["candidate_started_at"] is not None and
                        time.monotonic() - side["candidate_started_at"] >=
                        MIN_CANDIDATE_STABLE_SECONDS):
                    side["candidate"] = snapshot
                side["next_candidate_at"] = (
                    time.monotonic() + CANDIDATE_INTERVAL_SECONDS)

            common = sorted(set(sides["original"]["snapshots"]) &
                            set(sides["rebuild"]["snapshots"]))
            for frame in common:
                left = sides["original"]["snapshots"].pop(frame)
                right = sides["rebuild"]["snapshots"].pop(frame)
                # Frame 1 can be the peer-specific owner-AI initialization
                # transition.  The established compact parity contract begins
                # at completed simulation frame 2.
                if frame < 2:
                    journal.write(json.dumps({
                        "frame": frame,
                        "ignored_transitional": True,
                        "original_sha256": left["state_sha256"],
                        "rebuild_sha256": right["state_sha256"],
                    }, separators=(",", ":")) + "\n")
                    journal.flush()
                    continue
                difference = first_difference(left["state"], right["state"])
                exact = difference is None
                journal.write(json.dumps({
                    "frame": frame,
                    "exact": exact,
                    "original_sha256": left["state_sha256"],
                    "rebuild_sha256": right["state_sha256"],
                    "rng": left["state"]["rng"],
                    "capture_ms": [left["capture_ms"], right["capture_ms"]],
                }, separators=(",", ":")) + "\n")
                journal.flush()

                # The frame counter advances before the whole simulation tick
                # has completed.  Equal frame numbers from two independently
                # running peers can therefore describe different intra-frame
                # phases.  The gameplay RNG tuple is the phase barrier used by
                # the original/rebuild call-sequence tracer: only compare the
                # semantic snapshots after that tuple is aligned as well.
                if left["state"]["rng"] != right["state"]["rng"]:
                    phase_skew_pair_count += 1
                    journal.write(json.dumps({
                        "frame": frame,
                        "ignored_intra_frame_rng_skew": True,
                        "original_rng": left["state"]["rng"],
                        "rebuild_rng": right["state"]["rng"],
                        "difference": difference,
                        "original_owner_ai_2": left["state"]["owner_ai"].get("2"),
                        "rebuild_owner_ai_2": right["state"]["owner_ai"].get("2"),
                        "original_ai_demand_2": left["state"]["ai_demand"].get("2"),
                        "rebuild_ai_demand_2": right["state"]["ai_demand"].get("2"),
                    }, separators=(",", ":")) + "\n")
                    journal.flush()
                    if pending_rng_divergence is None:
                        pending_rng_divergence = {
                            "frame": frame,
                            "difference": difference,
                            "original_snapshot": left,
                            "rebuild_snapshot": right,
                            "previous_exact_pair": previous_exact,
                            "confirmation_count": 1,
                            "last_confirmation_frame": frame,
                        }
                        continue
                    if frame == pending_rng_divergence["last_confirmation_frame"]:
                        continue
                    pending_rng_divergence["confirmation_count"] += 1
                    pending_rng_divergence["last_confirmation_frame"] = frame
                    if (pending_rng_divergence["confirmation_count"] <
                            REQUIRED_CONSECUTIVE_DIVERGENCES):
                        continue
                    first = pending_rng_divergence
                    field, original_value, rebuild_value = first["difference"]
                    detail = {
                        "probe": "low-load same-frame persistent RNG audit",
                        "sha256": layout.get("sha256"),
                        "first_observed_divergence": {
                            "frame": first["frame"],
                            "field": field,
                            "original": original_value,
                            "rebuild": rebuild_value,
                            "original_rng": first["original_snapshot"]["state"]["rng"],
                            "rebuild_rng": first["rebuild_snapshot"]["state"]["rng"],
                        },
                        "previous_exact_pair": first["previous_exact_pair"],
                        "divergence_pair": {
                            "original": first["original_snapshot"],
                            "rebuild": first["rebuild_snapshot"],
                        },
                        "confirmation_divergence": {
                            "frame": frame,
                            "field": difference[0],
                            "original": difference[1],
                            "rebuild": difference[2],
                            "original_rng": left["state"]["rng"],
                            "rebuild_rng": right["state"]["rng"],
                        },
                        "coverage": {
                            "exact_pair_count": exact_pairs,
                            "first_exact_frame": first_pair,
                            "last_exact_frame": last_pair,
                            "pair_gaps": pair_gaps,
                            "semantic_coverage": coverage_summary(),
                        },
                    }
                    atomic_json(detail_path, detail)
                    atomic_json(RESULT_PATH, {
                        "pass": False,
                        "reason": "confirmed persistent same-frame RNG divergence",
                        "frame": first["frame"],
                        "field": field,
                        "original": original_value,
                        "rebuild": rebuild_value,
                        "original_rng": first["original_snapshot"]["state"]["rng"],
                        "rebuild_rng": first["rebuild_snapshot"]["state"]["rng"],
                        "confirmation_frame": frame,
                        "exact_pair_count": exact_pairs,
                        "first_exact_frame": first_pair,
                        "last_exact_frame": last_pair,
                        "pair_gaps": pair_gaps,
                        "semantic_coverage": coverage_summary(),
                        "terminal_exact_state": terminal_summary(),
                        "detail_path": detail_path,
                        "journal_path": JOURNAL_PATH,
                    })
                    raise SystemExit(2)
                    continue

                if pending_rng_divergence is not None:
                    transient_divergence_count += 1
                    pending_rng_divergence = None

                if difference is not None:
                    field, original_value, rebuild_value = difference
                    if pending_divergence is None:
                        pending_divergence = {
                            "frame": frame,
                            "field": field,
                            "original": original_value,
                            "rebuild": rebuild_value,
                            "original_snapshot": left,
                            "rebuild_snapshot": right,
                            "previous_exact_pair": previous_exact,
                            "confirmation_count": 1,
                            "last_confirmation_frame": frame,
                        }
                        journal.write(json.dumps({
                            "frame": frame,
                            "transient_divergence_candidate": True,
                            "field": field,
                            "original": original_value,
                            "rebuild": rebuild_value,
                            "rng": left["state"]["rng"],
                            "original_owner_ai_2":
                                left["state"]["owner_ai"].get("2"),
                            "rebuild_owner_ai_2":
                                right["state"]["owner_ai"].get("2"),
                            "original_ai_demand_2":
                                left["state"]["ai_demand"].get("2"),
                            "rebuild_ai_demand_2":
                                right["state"]["ai_demand"].get("2"),
                        }, separators=(",", ":")) + "\n")
                        journal.flush()
                        continue
                    if frame == pending_divergence["last_confirmation_frame"]:
                        continue
                    pending_divergence["confirmation_count"] += 1
                    pending_divergence["last_confirmation_frame"] = frame
                    if (pending_divergence["confirmation_count"] <
                            REQUIRED_CONSECUTIVE_DIVERGENCES):
                        continue
                    first = pending_divergence
                    detail = {
                        "probe": "low-load same-simulation-frame semantic audit",
                        "sha256": layout.get("sha256"),
                        "first_observed_divergence": {
                            "frame": first["frame"],
                            "field": first["field"],
                            "original": first["original"],
                            "rebuild": first["rebuild"],
                            "original_rng": first["original_snapshot"]["state"]["rng"],
                            "rebuild_rng": first["rebuild_snapshot"]["state"]["rng"],
                        },
                        "previous_exact_pair": first["previous_exact_pair"],
                        "divergence_pair": {
                            "original": first["original_snapshot"],
                            "rebuild": first["rebuild_snapshot"],
                        },
                        "confirmation_divergence": {
                            "frame": frame,
                            "field": field,
                            "original": original_value,
                            "rebuild": rebuild_value,
                            "original_rng": left["state"]["rng"],
                            "rebuild_rng": right["state"]["rng"],
                        },
                        "coverage": {
                            "exact_pair_count": exact_pairs,
                            "first_exact_frame": first_pair,
                            "last_exact_frame": last_pair,
                            "pair_gaps": pair_gaps,
                            "semantic_coverage": coverage_summary(),
                        },
                    }
                    atomic_json(detail_path, detail)
                    summary = {
                        "pass": False,
                        "reason": "confirmed three-sample same-simulation-frame divergence",
                        "frame": first["frame"],
                        "field": first["field"],
                        "original": first["original"],
                        "rebuild": first["rebuild"],
                        "original_rng": first["original_snapshot"]["state"]["rng"],
                        "rebuild_rng": first["rebuild_snapshot"]["state"]["rng"],
                        "confirmation_frame": frame,
                        "confirmation_field": field,
                        "exact_pair_count": exact_pairs,
                        "first_exact_frame": first_pair,
                        "last_exact_frame": last_pair,
                        "pair_gaps": pair_gaps,
                        "semantic_coverage": coverage_summary(),
                        "terminal_exact_state": terminal_summary(),
                        "detail_path": detail_path,
                        "journal_path": JOURNAL_PATH,
                    }
                    atomic_json(RESULT_PATH, summary)
                    raise SystemExit(2)

                if pending_divergence is not None:
                    transient_divergence_count += 1
                    journal.write(json.dumps({
                        "frame": frame,
                        "resolved_transient_divergence_frame":
                            pending_divergence["frame"],
                    }, separators=(",", ":")) + "\n")
                    journal.flush()
                    pending_divergence = None

                if last_pair is not None and frame != last_pair + 1:
                    pair_gaps.append([last_pair, frame])
                exact_pairs += 1
                first_pair = frame if first_pair is None else first_pair
                last_pair = frame
                previous_exact = {"original": left, "rebuild": right}
                update_coverage(left["state"])

            # Keep unmatched peer snapshots briefly for wall-time skew, then
            # discard them before they can grow into a large in-memory trace.
            leading = max(side["last_seen"] or 0 for side in sides.values())
            for side in sides.values():
                side["snapshots"] = {
                    frame: snapshot
                    for frame, snapshot in side["snapshots"].items()
                    if frame >= leading - 32
                }
            time.sleep(POLL_SECONDS)
        else:
            atomic_json(RESULT_PATH, {
                "pass": False,
                "reason": "compact audit timed out without stop marker",
                "sha256": layout.get("sha256"),
                "exact_pair_count": exact_pairs,
                "first_exact_frame": first_pair,
                "last_exact_frame": last_pair,
                "pair_gaps": pair_gaps,
                "semantic_coverage": coverage_summary(),
                "terminal_exact_state": terminal_summary(),
                "journal_path": JOURNAL_PATH,
            })
except SystemExit:
    raise
except Exception as error:
    atomic_json(RESULT_PATH, {
        "pass": False,
        "reason": "compact audit exception",
        "error": repr(error),
        "traceback": traceback.format_exc(),
        "exact_pair_count": exact_pairs,
        "first_exact_frame": first_pair,
        "last_exact_frame": last_pair,
        "pair_gaps": pair_gaps,
        "semantic_coverage": coverage_summary(),
        "terminal_exact_state": terminal_summary(),
        "journal_path": JOURNAL_PATH,
    })
    raise
finally:
    original.close()
    rebuild.close()
