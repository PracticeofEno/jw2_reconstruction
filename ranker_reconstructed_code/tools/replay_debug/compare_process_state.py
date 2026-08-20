import ctypes
import hashlib
import json
import os
import struct
import sys
import time
import traceback
from ctypes import wintypes


IGNORE_COMPLETION_REVERSE_24 = (
    os.environ.get("NEXTDIV_IGNORE_COMPLETION_REVERSE_24") == "1")
IGNORE_WORLD_VIEW = (
    os.environ.get("NEXTDIV_IGNORE_WORLD_VIEW") == "1")
CAPTURE_SUSPENDED_ONCE = (
    os.environ.get("NEXTDIV_CAPTURE_SUSPENDED_ONCE") == "1")
IGNORE_RENDER_QUEUES = (
    os.environ.get("NEXTDIV_IGNORE_RENDER_QUEUES") == "1")
MAP_DEBUG_SLOTS = tuple(
    int(value, 0)
    for value in os.environ.get("NEXTDIV_MAP_DEBUG_SLOTS", "").split(",")
    if value.strip())


ORIGINAL_PID = int(sys.argv[1])
REBUILD_PID = int(sys.argv[2])
REBUILD_BASE = int(sys.argv[3], 0)
LAYOUT_PATH = sys.argv[4]
RESULT_PATH = sys.argv[5]
JOURNAL_PATH = sys.argv[6]
STOP_PATH = sys.argv[7]
MAX_SECONDS = float(sys.argv[8]) if len(sys.argv) > 8 else 360.0
# A full semantic snapshot takes roughly 20-30 ms on this host.  The frame
# counter is advanced near the beginning of the simulation tick, so a 10 ms
# settle can capture one process after its unit walk and the other before it.
# Wait past the heavy update body and retain the last completed-frame sample.
SETTLE_SECONDS = float(os.environ.get("NEXTDIV_SETTLE_SECONDS", "0.025"))
CANDIDATE_INTERVAL_SECONDS = float(
    os.environ.get("NEXTDIV_CANDIDATE_INTERVAL_SECONDS", "0.080"))
POLL_SECONDS = 0.002


def number(value):
    return int(value, 0) if isinstance(value, str) else int(value)


with open(LAYOUT_PATH, "r", encoding="utf-8-sig") as stream:
    layout = json.load(stream)

runtime_rva = number(layout["runtime_rva"])
loop_rva = number(layout["loop_rva"])
random_offset = number(layout["frame_random_offset"])
gameplay_sound_offset = number(layout["gameplay_sound_offset"])
gameplay_sound_layout = {
    key: number(value)
    for key, value in layout["gameplay_sound_layout"].items()
}
visibility_offset = number(layout["visibility_offset"])
movement_offset = number(layout["movement_offset"])
lifecycle_offset = number(layout["lifecycle_offset"])
production_runtime_offset = number(layout["production_runtime_offset"])
owner_counters_offset = number(layout["owner_counters_offset"])
unit_reference_tables_offset = number(
    layout["unit_reference_tables_offset"])
unit_reference_completion_reverse_offset = number(
    layout["unit_reference_completion_reverse_offset"])
UNIT_REFERENCE_PRIMARY_COUNT = 0xAA
UNIT_REFERENCE_COMPLETION_COUNT = 0x40
# The highest small-reference ID in the shipped JW2_09 records is 0x5A.
# Original storage immediately after index 0x5A belongs to unrelated globals,
# so compare the 0x5B semantic entries rather than reading an invented tail.
UNIT_REFERENCE_SMALL_COUNT = 0x5B
unit_reference_primary_reverse_offset = (
    unit_reference_completion_reverse_offset -
    UNIT_REFERENCE_PRIMARY_COUNT * 4)
unit_reference_small_reverse_offset = (
    unit_reference_completion_reverse_offset +
    UNIT_REFERENCE_COMPLETION_COUNT * 4)
unit_render_queue_offset = number(layout["unit_render_queue_offset"])
render_command_queue_offset = number(layout["render_command_queue_offset"])
map_effect_context_offset = number(layout["map_effect_context_offset"])
unit_effects_offset = number(layout["unit_effects_offset"])
overlay_rva = number(layout["overlay_rva"])
input_state_rva = number(layout["input_state_rva"])
programmatic_pointer_motion_pending_rva = number(
    layout["programmatic_pointer_motion_pending_rva"])
programmatic_pointer_motion_target_reached_rva = number(
    layout["programmatic_pointer_motion_target_reached_rva"])
programmatic_pointer_motion_x_rva = number(
    layout["programmatic_pointer_motion_x_rva"])
programmatic_pointer_motion_y_rva = number(
    layout["programmatic_pointer_motion_y_rva"])
player_slots_offset = number(layout["player_slots_offset"])
owner_ai_offset = number(layout["owner_ai_offset"])
owner_transport_routes_offset = number(layout["owner_transport_routes_offset"])
# OwnerTransportQueueState is eight owners * 32 slots * ten u32 fields and is
# laid out immediately before the route array in RuntimeGlobals.
OWNER_QUEUE_SLOT_COUNT = 32
OWNER_QUEUE_SLOT_SIZE = 40
owner_transport_queues_offset = (
    owner_transport_routes_offset - 8 * OWNER_QUEUE_SLOT_COUNT *
    OWNER_QUEUE_SLOT_SIZE)
owner_strategic_targets_offset = number(layout["owner_strategic_targets_offset"])
owner_ai_reserved_primary_cost_offset = number(
    layout["owner_ai_reserved_primary_cost_offset"])
unit_layout = {key: number(value)
               for key, value in layout["unit_layout"].items()}
context_layout = {key: number(value)
                  for key, value in layout["movement_context_layout"].items()}
movement_map_layout = {
    key: number(value) for key, value in layout["movement_map_layout"].items()
}
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
overlay_layout = {key: number(value)
                  for key, value in layout["overlay_layout"].items()}
input_layout = {key: number(value)
                for key, value in layout["input_layout"].items()}
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
production_runtime_layout = {
    key: number(value)
    for key, value in layout["production_runtime_layout"].items()
}
unit_render_queue_entry_layout = {
    key: number(value)
    for key, value in layout["unit_render_queue_entry_layout"].items()
}
unit_render_item_layout = {
    key: number(value)
    for key, value in layout["unit_render_item_layout"].items()
}
render_command_queue_layout = {
    key: number(value)
    for key, value in layout["render_command_queue_layout"].items()
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


def u32_words(data, offset, count):
    return list(struct.unpack_from(f"<{count}I", data, offset))


PROCESS_VM_READ = 0x0010
PROCESS_QUERY_INFORMATION = 0x0400
k32 = ctypes.WinDLL("kernel32", use_last_error=True)
k32.OpenProcess.restype = wintypes.HANDLE
k32.ReadProcessMemory.argtypes = [
    wintypes.HANDLE, wintypes.LPCVOID, wintypes.LPVOID, ctypes.c_size_t,
    ctypes.POINTER(ctypes.c_size_t),
]
k32.CloseHandle.argtypes = [wintypes.HANDLE]


class Memory:
    def __init__(self, pid):
        self.pid = pid
        self.handle = k32.OpenProcess(
            PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, False, pid)
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


def struct_vector(memory, address, element_size, limit=4096):
    begin, end, capacity = struct.unpack("<QQQ", memory.read(address, 24))
    if (element_size <= 0 or end < begin or capacity < end or
            (end - begin) % element_size != 0 or
            (end - begin) // element_size > limit):
        return None
    count = (end - begin) // element_size
    if count == 0:
        return []
    if not begin:
        return None
    data = memory.read(begin, count * element_size)
    return [data[index * element_size:(index + 1) * element_size]
            for index in range(count)]


def normalized_original_unit(data):
    type_id = u32(data, 0x00)
    state = u32(data, 0x60)
    equipment_start = 1 if type_id >= 0x60 else 0
    return {
        "type": type_id,
        "control_group": u32(data, 0x08) & 0x0F,
        # Raw +0x08 bit 0x80 gates the world HP/secondary bars. It is local
        # render state rather than synchronized simulation state, but omitting
        # it allowed a visually stale selected-unit bar to pass every aligned
        # replay comparison as "exact".
        "world_bar_selection": u32(data, 0x08) & 0x80,
        # Subtype 0x19 owns raw +0x48.  This slot controls the name rendered
        # above the unit, so it belongs in gameplay/world-render parity even
        # though ordinary recorded matches rarely publish that packet type.
        "string_slot": u32(data, 0x48),
        "owner": u32(data, 0x04),
        "area_marker_flags": u32(data, 0x0C),
        "max_secondary": u32(data, 0x14),
        "runtime_stat_1c": u32(data, 0x1C),
        "runtime_stat_20": u32(data, 0x20),
        "secondary": u32(data, 0x24),
        "runtime_stat_28": u32(data, 0x28),
        "action_mode": u32(data, 0x2C),
        "action_mode_gate": u32(data, 0x30) if type_id >= 0x60 else 0,
        "equipment": [
            u32(data, 0x30 + index * 4)
            for index in range(equipment_start, 6)
        ],
        "elite_progress": u32(data, 0x50),
        "status_timer": u32(data, 0x54),
        "production_variant": u32(data, 0x54),
        "type_flags": u32(data, 0x58),
        "command_bits0": u32(data, 0x5C),
        "state": state,
        "command_value": (u32(data, 0x68)
                          if (state & 0x00FFFFFF) in
                          (0x4D, 0x4E, 0x50, 0x51, 0x82, 0x83) else 0),
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
        # Raw +0xA4 selects the world-sprite draw mode. Like selection it is
        # local render state, but original/rebuild replay parity must compare
        # it so a transient highlight or blend-mode mismatch cannot hide.
        "world_draw_flags": u32(data, 0xA4),
        "lockout": u32(data, 0xF4),
        "_raw_animation_or_work_timer": u32(data, 0x64),
        "animation_timer": u32(data, 0xEC),
        "cargo": u32(data, 0x4C),
        "path": [i32(data, 0x6C), i32(data, 0x70)],
        "destination": [i32(data, 0x78), i32(data, 0x7C)],
        "current_cell": [i32(data, 0xC0), i32(data, 0xC4)],
        "next": [i32(data, 0xC8), i32(data, 0xCC)],
        "anchor": [i32(data, 0xD0), i32(data, 0xD4)],
        "direction": u32(data, 0xA8),
        "movement_flags": u32(data, 0xAC),
        "movement_state": u32(data, 0xB0),
        "turn_ticks": u32(data, 0xB4),
        "world": [i32(data, 0xB8), i32(data, 0xBC)],
        "accumulator": u32(data, 0x110),
        "residual": [i32(data, 0x114), i32(data, 0x118)],
        "interpolation_bits": [u32(data, 0x11C), u32(data, 0x120)],
        "linked_effect": u32(data, 0xF0),
        "saved_type_flags": u32(data, 0xF8),
        "placement_scratch": u32(data, 0xFC),
        "distance_mode": u32(data, 0x10C),
        "script_bits": u32(data, 0xE8),
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
    equipment_start = 1 if type_id >= 0x60 else 0
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
        "control_group": u32(data, 0x10) & 0x0F,
        "world_bar_selection": (
            u32(data, unit_layout["scenario_string_slot"]) & 0x80),
        "string_slot": u32(data, unit_layout["string_slot"]),
        "owner": u32(data, unit_layout["owner"]),
        "area_marker_flags": u32(data, 0x14),
        "max_secondary": u32(data, 0x15C),
        "runtime_stat_1c": u32(data, 0x154),
        "runtime_stat_20": u32(data, 0x158),
        "secondary": u32(data, 0x160),
        "runtime_stat_28": u32(data, 0x164),
        "action_mode": u32(data, unit_layout["action_mode"]),
        "action_mode_gate": (
            u32(data, unit_layout["action_mode_gate"]) if type_id >= 0x60 else 0),
        "equipment": [
            u32(data, 0x17C + index * 4)
            for index in range(equipment_start, 6)
        ],
        "elite_progress": u32(data, 0x88),
        "status_timer": u32(data, 0x168),
        "production_variant": u32(data, 0x7C),
        "type_flags": u32(data, unit_layout["type_flags"]),
        "command_bits0": u32(data, 0x30),
        "state": state,
        "command_value": (u32(data, unit_layout["command_value"])
                          if (state & 0x00FFFFFF) in
                          (0x4D, 0x4E, 0x50, 0x51, 0x82, 0x83) else 0),
        "previous_state": u32(data, 0x58),
        "pending": queued(pending_offset),
        "active_payload": queued(active_offset),
        "deferred_count": u32(data, unit_layout["deferred_count"]),
        "deferred_first": queued(deferred_offset),
        "command_flags": u32(data, unit_layout["command_flags"]),
        "runtime_flags": u32(data, unit_layout["runtime_flags"]),
        "world_draw_flags": u32(data, unit_layout["draw_flags"]),
        "lockout": u32(data, unit_layout["command_lockout"]),
        "_animation_frame": u32(data, unit_layout["animation_frame"]),
        "_work_timer": u32(data, unit_layout["work_timer"]),
        "animation_timer": u32(data, 0xE8),
        "cargo": u32(data, unit_layout["cargo"]),
        "path": [i32(data, unit_layout["path_target_x"]),
                 i32(data, unit_layout["path_target_y"])],
        "current_cell": [i32(data, 0xB4), i32(data, 0xB8)],
        "next": [i32(data, 0xCC), i32(data, 0xD0)],
        "anchor": [i32(data, 0xD4), i32(data, 0xD8)],
        "direction": u32(data, 0xE0),
        "movement_flags": u32(data, unit_layout["movement_flags"]),
        "movement_state": u32(data, 0xEC),
        "turn_ticks": u32(data, 0xF0),
        "world": [i32(data, unit_layout["x"]),
                  i32(data, unit_layout["y"])],
        "accumulator": u32(data, 0x108),
        "residual": [i32(data, 0x10C), i32(data, 0x110)],
        "interpolation_bits": [u32(data, 0x114), u32(data, 0x118)],
        "linked_effect": u32(data, 0x130),
        "saved_type_flags": u32(data, 0x24),
        "placement_scratch": u32(data, 0xF4),
        "distance_mode": u32(data, 0x144),
        "script_bits": u32(data, 0xE50),
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
    # Original OBB references remain meaningful after a unit leaves the
    # active/lifecycle chains.  Mirror that fixed-pool identity for effect and
    # linked-unit normalization without publishing free nodes as live rows.
    for pointer in pointer_vector(
            memory, movement + context_layout["free_units"]):
        if not pointer:
            continue
        data = memory.read(pointer, 0x280)
        slot = u32(data, unit_layout["runtime_slot"])
        pointer_slots.setdefault(pointer, slot)
        id_slots.setdefault(u32(data, unit_layout["id"]), slot)
    return rows, orders, pointer_slots, id_slots


def original_unit_render_queue(memory, rows):
    # FUN_004d8297 clears the count/sorted flag at the start of a present pass;
    # its 0x004d8297 store clears count before the 0x004d82a1 sorted-byte
    # store, so count==0/ready==1 is an in-flight reset rather than a complete
    # empty queue.
    # FUN_0042a890 publishes sorted=1 only after every visible unit/effect and
    # decoration has been queued. Ignore an in-flight queue snapshot.
    ready = memory.read(0x0083F5F4, 1)[0] != 0
    count = memory.u32(0x0083F5F0)
    if count == 0:
        ready = False
    if count > 3000:
        return {"ready": ready, "entries": None}
    result = []
    unit_layers = (5, 5, 5, 8, 5)
    effect_layers = (3, 3, 3, 8, 3, 5)
    for index in range(count):
        entry = memory.read(0x012DA850 + index * 0x20, 0x20)
        raw_unit_offset = u32(entry, 0x04)
        if raw_unit_offset == 0 or raw_unit_offset % 0x1D0 != 0:
            continue
        slot = raw_unit_offset // 0x1D0
        row = rows.get(slot)
        if row is None:
            continue
        type_id = row["type"]
        definition = 0x0087C2F8 + type_id * 0x24BC
        definition_render_class = memory.u32(definition + 0x17C)
        if row["list"] == "active":
            render_class = definition_render_class
            expected_layer = (unit_layers[render_class]
                if render_class < len(unit_layers) else None)
        else:
            # ProcessVisibleEffectRenderQueue forces class 5 unless raw state
            # +0x60 carries 0x40000000, exactly like the reconstructed queue.
            render_class = (definition_render_class
                if (row["state"] & 0x40000000) != 0 else 5)
            expected_layer = (effect_layers[render_class]
                if render_class < len(effect_layers) else None)
        layer = u32(entry, 0x00)
        # The original queue is shared with terrain/decorations. Their payload
        # can coincidentally be a valid 0x1d0 unit-slot multiple, so pointer
        # shape alone produces false unit entries. Require the exact unit or
        # lifecycle-effect dispatch layer for this row as well.
        if expected_layer is None or layer != expected_layer:
            continue
        result.append({
            "slot": slot,
            "type": type_id,
            "render_class": render_class,
            "layer": layer,
            "sort_key": u32(entry, 0x08),
        })
    return {"ready": ready, "entries": result}


def rebuild_unit_render_queue(memory, runtime):
    command_queue = runtime + render_command_queue_offset
    ready = memory.read(
        command_queue + render_command_queue_layout["sorted"], 1)[0] != 0
    queue = runtime + unit_render_queue_offset
    entry_layout = unit_render_queue_entry_layout
    entries = struct_vector(
        memory, queue + entry_layout["vector"], entry_layout["size"], 3000)
    if entries is None:
        return {"ready": ready, "entries": None}
    return {"ready": ready, "entries": [{
        "slot": u32(entry, entry_layout["runtime_slot"]),
        "type": u32(entry, entry_layout["type"]),
        "render_class": u32(entry, entry_layout["render_class"]),
        "layer": u32(entry, entry_layout["layer"]),
        "sort_key": u32(entry, entry_layout["sort_key"]),
    } for entry in entries]}


def original_world_render_queue(memory, rows):
    """Capture the complete original sorted world-command queue.

    FUN_004d824d writes seven meaningful dwords into each 0x20-byte entry.
    FUN_004d8050 later loads payload through ESI, screen Y through EDX, and
    screen X through EBX before dispatching the class callback.  Keeping the
    insertion index and published sorted order makes equal-key ordering bugs
    visible as well as missing commands.  FUN_0042a890 sets DAT_0083f5f4
    before it enters the recursive quicksort, so that byte alone is not a
    completion fence: a suspended process can expose a valid but only partly
    sorted index array.  Reject that in-flight snapshot below by checking the
    unsigned key order that the original quicksort guarantees on return.
    """
    ready = memory.read(0x0083F5F4, 1)[0] != 0
    count = memory.u32(0x0083F5F0)
    # FUN_004d8297 writes count=0 at 0x004d8297 before sorted=0 at
    # 0x004d82a1.  Reject a suspension in that two-instruction reset window.
    if count == 0:
        ready = False
    if count > 3000:
        return {"ready": ready, "entries": None, "sorted_indices": None}
    entries = []
    for index in range(count):
        entry = memory.read(0x012DA850 + index * 0x20, 0x20)
        class_id = u32(entry, 0x00)
        payload = u32(entry, 0x04)
        unit_slot = None
        if (class_id in (3, 5, 6, 8) and payload != 0 and
                payload % 0x1D0 == 0 and payload // 0x1D0 in rows):
            unit_slot = payload // 0x1D0
        effect_slot = None
        if (class_id in (1, 7, 9) and payload != 0 and
                payload % ORIGINAL_UNIT_EFFECT_STRIDE == 0):
            effect_slot = payload // ORIGINAL_UNIT_EFFECT_STRIDE - 1
        map_effect_slot = None
        if (class_id == 4 and payload % ORIGINAL_MAP_EFFECT_STRIDE == 0):
            map_effect_slot = payload // ORIGINAL_MAP_EFFECT_STRIDE
        entries.append({
            "insertion_index": index,
            "class": class_id,
            "payload": payload,
            "sort_key": u32(entry, 0x08),
            "sprite_entry": u32(entry, 0x0C),
            # FUN_004d8050 loads raw +0x10 through EDX and raw +0x14 through
            # EBX.  The draw callbacks use those registers as X and Y,
            # respectively (the decompiler's fastcall labels are reversed).
            "screen_x": i32(entry, 0x10),
            "screen_y": i32(entry, 0x14),
            "packed_flags": u32(entry, 0x18),
            "unit_slot": unit_slot,
            "effect_slot": effect_slot,
            "map_effect_slot": map_effect_slot,
        })
    sorted_indices = [memory.u32(0x012D7970 + index * 4)
                      for index in range(count)]
    if any(index >= count for index in sorted_indices):
        sorted_indices = None
    elif any(entries[sorted_indices[index]]["sort_key"] >
             entries[sorted_indices[index + 1]]["sort_key"]
             for index in range(count - 1)):
        ready = False
        sorted_indices = None
    return {
        "ready": ready,
        "entries": entries,
        "sorted_indices": sorted_indices,
    }


def rebuild_world_render_queue(memory, runtime):
    queue = runtime + render_command_queue_offset
    layout = render_command_queue_layout
    ready = memory.read(queue + layout["sorted"], 1)[0] != 0
    commands = struct_vector(
        memory, queue + layout["commands"], layout["command_size"], 3000)
    if commands is None:
        return {"ready": ready, "entries": None, "sorted_indices": None}
    sorted_indices = index_vector(
        memory, queue + layout["sorted_indices"], 8, 3000)
    if len(sorted_indices) != len(commands) or any(
            index >= len(commands) for index in sorted_indices):
        sorted_indices = None
    effect_slots_begin, effect_slots_end, _ = struct.unpack(
        "<QQQ", memory.read(
            runtime + unit_effects_offset + unit_effect_layout["slots"], 24))
    effect_size = unit_effect_layout["effect_size"]
    entries = []
    for index, command in enumerate(commands):
        unit_item_pointer = u64(command, layout["command_unit_item"])
        unit_slot = (memory.u32(
            unit_item_pointer + unit_render_item_layout["runtime_slot"])
            if unit_item_pointer else None)
        effect_pointer = u64(command, layout["command_effect"])
        effect_slot = None
        if (effect_pointer and effect_size > 0 and
                effect_slots_begin <= effect_pointer < effect_slots_end and
                (effect_pointer - effect_slots_begin) % effect_size == 0):
            effect_slot = (effect_pointer - effect_slots_begin) // effect_size
        class_id = u32(command, layout["command_class"])
        payload = u32(command, layout["command_payload"])
        entries.append({
            "insertion_index": index,
            "class": class_id,
            "payload": payload,
            "sort_key": u32(command, layout["command_sort_key"]),
            "sprite_entry": u32(command, layout["command_sprite_entry"]),
            "sprite_draw_mode": u32(
                command, layout["command_sprite_draw_mode"]),
            "screen_y": i32(command, layout["command_screen_y"]),
            "screen_x": i32(command, layout["command_screen_x"]),
            "packed_flags": u32(command, layout["command_packed_flags"]),
            "sprite_draw_mode_valid": command[
                layout["command_sprite_draw_mode_valid"]] != 0,
            "has_unit_context": u64(
                command, layout["command_unit_context"]) != 0,
            "has_unit_item": u64(command, layout["command_unit_item"]) != 0,
            "has_effect_context": u64(
                command, layout["command_effect_context"]) != 0,
            "has_effect": u64(command, layout["command_effect"]) != 0,
            "draw_variant": u32(command, layout["command_draw_variant"]),
            "unit_slot": unit_slot,
            "effect_slot": effect_slot,
            "map_effect_slot": payload if class_id == 4 else None,
        })
    return {
        "ready": ready,
        "entries": entries,
        "sorted_indices": sorted_indices,
    }


def semantic_world_render_entries(queue, camera_x, camera_y, side):
    entries = queue.get("entries")
    indices = queue.get("sorted_indices")
    if entries is None or indices is None:
        return None
    result = []
    for index in indices:
        entry = entries[index]
        class_id = entry["class"]
        semantic = {
            "sort_key": entry["sort_key"],
            "world_x": entry["screen_x"] + camera_x,
            "world_y": entry["screen_y"] + camera_y,
        }
        if entry.get("unit_slot") is not None:
            semantic["kind"] = "unit"
            semantic["slot"] = entry["unit_slot"]
        elif class_id in (1, 7, 9):
            semantic["kind"] = "unit_effect"
            semantic["slot"] = entry.get("effect_slot")
        elif class_id == 4:
            semantic["kind"] = "map_effect"
            semantic["slot"] = entry.get("map_effect_slot")
        elif class_id == 2:
            semantic["kind"] = "terrain_type1" if (
                (entry["sort_key"] & 0x3FFFF) >= 0x20000
                if side == "original" else entry["payload"] == 0
            ) else "terrain_type3"
            semantic["flags"] = entry["packed_flags"]
        elif class_id == 10:
            semantic["kind"] = "brush_edge"
            semantic["flags"] = entry["packed_flags"]
        else:
            semantic["kind"] = f"class_{class_id}"
        result.append(semantic)
    return result


FOLLOW_POOL_DEBUG_SLOTS = (113, 171, 177, 223, 241)


def original_follow_pool_debug(memory):
    result = {}
    for slot in FOLLOW_POOL_DEBUG_SLOTS:
        data = memory.read(0x00A03FB8 + slot * 0x1D0, 0x1D0)
        result[str(slot)] = {
            "type": u32(data, 0x00),
            "owner": u32(data, 0x0C),
            "state": u32(data, 0x60),
            "target_or_value": u32(data, 0x68),
            "command_flags": u32(data, 0x9C),
            "runtime_flags": u32(data, 0xA0),
            "world": [i32(data, 0xB8), i32(data, 0xBC)],
            "previous_link": u32(data, 0x1C8),
            "next_link": u32(data, 0x1CC),
        }
    return result


def rebuild_follow_pool_debug(memory, movement):
    result = {}
    for list_name, field in (("active", "active_units"),
                             ("lifecycle", "lifecycle_units"),
                             ("free", "free_units")):
        for pointer in pointer_vector(memory, movement + context_layout[field]):
            if not pointer:
                continue
            data = memory.read(pointer, unit_layout["size"])
            slot = u32(data, unit_layout["runtime_slot"])
            if slot not in FOLLOW_POOL_DEBUG_SLOTS:
                continue
            result[str(slot)] = {
                "list": list_name,
                "id": u32(data, unit_layout["id"]),
                "type": u32(data, unit_layout["type"]),
                "owner": u32(data, unit_layout["owner"]),
                "state": u32(data, unit_layout["command_state"]),
                "command_value": u32(data, unit_layout["command_value"]),
                "command_flags": u32(data, unit_layout["command_flags"]),
                "runtime_flags": u32(data, unit_layout["runtime_flags"]),
                "active": bool(data[unit_layout["active"]]),
                "world": [i32(data, unit_layout["x"]),
                          i32(data, unit_layout["y"])],
                "target_pointer": u64(data, unit_layout["target"]),
            }
    return result


VISIBILITY_DEBUG_SOURCE_SLOTS = (3, 9, 12, 38, 39, 178, 211)


def visibility_debug_slots(rows):
    slots = set(VISIBILITY_DEBUG_SOURCE_SLOTS)
    for slot in VISIBILITY_DEBUG_SOURCE_SLOTS:
        row = rows.get(slot)
        if row is not None and row.get("target_slot") is not None:
            slots.add(row["target_slot"])
    return sorted(slot for slot in slots if slot in rows)


def original_visibility_debug(memory, rows):
    result = {}
    for slot in visibility_debug_slots(rows):
        row = rows[slot]
        x, y = row["current_cell"]
        tile_x = (x & 0xFFFFFFFF) >> 5
        tile_y = (y & 0xFFFFFFFF) >> 5
        if tile_x >= 0x100 or tile_y >= 0x100:
            continue
        offset = tile_y * 0x400 + tile_x * 4
        result[str(slot)] = {
            "cell": [x, y],
            "tile": [tile_x, tile_y],
            "current": memory.u32(0x00758D40 + offset),
            "previous": memory.u32(0x00798D40 + offset),
            "owner": memory.u32(0x007D8D40 + offset),
        }
    return result


def rebuild_visibility_debug(memory, rows):
    grid = runtime + visibility_offset
    width = memory.u32(grid)
    height = memory.u32(grid + 4)
    layer_pointers = {
        "current": u64(memory.read(grid + 0x08, 8), 0),
        "previous": u64(memory.read(grid + 0x20, 8), 0),
        "owner": u64(memory.read(grid + 0x38, 8), 0),
    }
    result = {}
    for slot in visibility_debug_slots(rows):
        row = rows[slot]
        x, y = row["current_cell"]
        tile_x = (x & 0xFFFFFFFF) >> 5
        tile_y = (y & 0xFFFFFFFF) >> 5
        if tile_x >= width or tile_y >= height:
            continue
        index = tile_y * width + tile_x
        result[str(slot)] = {
            "cell": [x, y],
            "tile": [tile_x, tile_y],
            **{
                name: (memory.u32(pointer + index * 4) if pointer else None)
                for name, pointer in layer_pointers.items()
            },
        }
    return result


REACH_DEBUG_SLOTS = tuple(dict.fromkeys(
    (18, 21, 22, 39, 99, 151, 153, 174, 178, 211) + MAP_DEBUG_SLOTS))


def original_reach_debug(memory, rows):
    result = {}
    for slot in REACH_DEBUG_SLOTS:
        row = rows.get(slot)
        if row is None:
            continue
        type_id = row["type"]
        definition = 0x0087C2F8 + type_id * 0x24BC
        definition_index_offset = memory.u32(0x0087C050 + type_id * 4)
        profile = memory.u32(definition + 0x1A0)
        # FUN_004c1e85 indexes the runtime row-offset table first, then
        # addresses fields relative to the live JW2_12 storage globals.
        action_offset = memory.u32(0x011D87B4 + profile * 4)
        result[str(slot)] = {
            "type": type_id,
            "render_class": memory.u32(definition + 0x17C),
            "lifecycle_class": memory.u32(definition + 0x14C),
            "movement_class": memory.u32(definition + 0x17C),
            "footprint": [memory.u32(definition + 0x330),
                          memory.u32(definition + 0x334)],
            "profile": profile,
            "range": memory.u32(definition + 0x1B0),
            "range3": memory.u32(definition + 0x1B4),
            "interaction_range_base": memory.u32(
                0x0087C490 + definition_index_offset),
            "ability_timer_period": memory.u32(definition + 0x13DC),
            "action_recovery_base": memory.u32(definition + 0x1A4),
            "bounds": [memory.i32(definition + offset)
                       for offset in (0x360, 0x364, 0x368, 0x36C)],
            "center": [memory.i32(definition + offset)
                       for offset in (0x360, 0x364, 0x368, 0x36C)],
            "interaction": [memory.i32(definition + offset)
                            for offset in (0x370, 0x374, 0x378, 0x37C)],
            "profile_row_offset": action_offset,
            "profile_distance_gate": memory.u32(0x011D89F4 + action_offset),
            "profile_target_mask": memory.u32(0x011D89FC + action_offset),
        }
    return result


def rebuild_reach_debug(memory, rows, pointer_slots):
    result = {}
    pointers_by_slot = {slot: pointer for pointer, slot in pointer_slots.items()}
    for slot in REACH_DEBUG_SLOTS:
        row = rows.get(slot)
        pointer = pointers_by_slot.get(slot)
        if row is None or pointer is None:
            continue
        definition = pointer + unit_layout["definition"]
        result[str(slot)] = {
            "type": row["type"],
            "typed_footprint_registered": bool(memory.read(
                pointer + unit_layout["footprint_registered"], 1)[0]),
            "render_class": memory.u32(definition + 136),
            "lifecycle_class": memory.u32(
                definition + unit_layout["definition_lifecycle_class"]),
            "movement_class": memory.u32(definition),
            "footprint": [memory.u32(
                definition + unit_layout["definition_footprint_width"]),
                          memory.u32(
                definition + unit_layout["definition_footprint_height"])],
            "profile": memory.u32(definition + 456),
            "range": memory.u32(definition + 440),
            "range3": memory.u32(definition + 444),
            "ability_timer_period": memory.u32(definition + 348),
            "action_cycle_ticks": memory.u32(definition + 464),
            "action_recovery_base": memory.u32(definition + 432),
            "range_bonus_per_count": memory.u32(definition + 448),
            "range_bonus_cap": memory.u32(definition + 452),
            "interaction_range_base": memory.u32(
                definition + unit_layout[
                    "definition_effect_adjusted_interaction_range_base"]),
            "bounds": [memory.i32(definition + offset)
                       for offset in (192, 196, 200, 204)],
            "center": [memory.i32(definition + offset)
                       for offset in (208, 212, 216, 220)],
            "interaction": [memory.i32(definition + offset)
                            for offset in (224, 228, 232, 236)],
        }
    return result


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
        source = u32(data, 0x18)
        target = u32(data, 0x1C)
        rows.append({
            "slot": offset // ORIGINAL_UNIT_EFFECT_STRIDE - 1,
            "effect_id": u32(data, 0x00),
            "direction": u32(data, 0x04),
            "flags": u32(data, 0x08),
            "tick": u32(data, 0x0C),
            "frame": u32(data, 0x10),
            "amount": u32(data, 0x14),
            "source_slot": original_reference_slot(source),
            "target_slot": original_reference_slot(target),
            "x": i32(data, 0x20),
            "y": i32(data, 0x24),
            # Raw +0x30/+0x34 is a mode-dependent union. FUN_004f1ee8 feeds it
            # to the high-ID projectile-trail renderer as a previous point,
            # while the ordinary low-ID stepper FUN_004f2dd3 mutates the same
            # words as its two Bresenham accumulators. Keep the neutral raw
            # names here; rebuild_unit_effects selects the typed semantic field.
            "previous_x": i32(data, 0x30),
            "previous_y": i32(data, 0x34),
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
        # Original raw +0x10 is a union.  Generic low-id projectiles, including
        # the active effect-0x17 chain path, use it
        # as the remaining path budget while active (0x004ec7ba DEC); the
        # typed reconstruction stores that value in `range` and reserves
        # `frame` for render animation.  Once startup/impact/afterimage mode
        # is entered, raw +0x10 again corresponds to typed `frame`.
        default_path_exclusions = {
            0x00, 0x01, 0x08, 0x0C, 0x0D, 0x11, 0x12, 0x13,
            0x1A, 0x1B, 0x1D, 0x1E, 0x22, 0x23, 0x24, 0x26,
        }
        active_default_path = (
            effect_id < 0x3D and
            effect_id not in default_path_exclusions and
            (flags & (0x02 | 0x80 | 0x400)) == 0
        )
        raw10_offset = (unit_effect_layout["direction"] + 4
                        if active_default_path
                        else unit_effect_layout["effect_frame"])
        # Raw +0x30/+0x34 is the same mode-dependent union for both low- and
        # high-ID effects. In particular, selected action 1 / effect 0x3e
        # enters FUN_004f2c04, which seeds these words with the X/Y Bresenham
        # accumulators; after its first path step they are 0/1, not the prior
        # world point. The typed runtime keeps that raw union in
        # accumulator_x/y and stores a reconstruction-only previous point
        # separately. The one high-ID trail path that reads the raw words as
        # coordinates (effect 0x69, FUN_004f1ee8) mirrors those coordinates
        # into accumulator_x/y during initialization as well.
        raw30_offset = unit_effect_layout["accumulator_x"]
        raw34_offset = unit_effect_layout["accumulator_y"]
        rows.append({
            "slot": index,
            "effect_id": effect_id,
            "direction": u32(data, unit_effect_layout["direction"]),
            "flags": flags,
            "tick": u32(data, unit_effect_layout["tick"]),
            "frame": u32(data, raw10_offset),
            "amount": u32(data, unit_effect_layout["amount"]),
            "source_slot": id_slots.get(source) if source else None,
            "target_slot": id_slots.get(target) if target else None,
            "x": i32(data, unit_effect_layout["x"]),
            "y": i32(data, unit_effect_layout["y"]),
            "previous_x": i32(data, raw30_offset),
            "previous_y": i32(data, raw34_offset),
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


def original_unit_reference_tables(memory):
    return {
        "primary": u32_words(memory.read(
            0x0123CE9C, UNIT_REFERENCE_PRIMARY_COUNT * 4),
            0, UNIT_REFERENCE_PRIMARY_COUNT),
        "completion": u32_words(memory.read(
            0x0123D144, UNIT_REFERENCE_COMPLETION_COUNT * 4),
            0, UNIT_REFERENCE_COMPLETION_COUNT),
        "small": u32_words(memory.read(
            0x0123D244, UNIT_REFERENCE_SMALL_COUNT * 4),
            0, UNIT_REFERENCE_SMALL_COUNT),
    }


def rebuild_unit_reference_tables(memory, runtime):
    base = runtime + unit_reference_tables_offset
    return {
        "primary": u32_words(memory.read(
            base + unit_reference_primary_reverse_offset,
            UNIT_REFERENCE_PRIMARY_COUNT * 4),
            0, UNIT_REFERENCE_PRIMARY_COUNT),
        "completion": u32_words(memory.read(
            base + unit_reference_completion_reverse_offset,
            UNIT_REFERENCE_COMPLETION_COUNT * 4),
            0, UNIT_REFERENCE_COMPLETION_COUNT),
        "small": u32_words(memory.read(
            base + unit_reference_small_reverse_offset,
            UNIT_REFERENCE_SMALL_COUNT * 4),
            0, UNIT_REFERENCE_SMALL_COUNT),
    }


def original_owner_ai(memory, owners):
    result = {}
    for owner in owners:
        preferred = memory.u32(0x012334C8 + owner * 4)
        route_unit = memory.u32(0x0122FFC8 + owner * 0x18)
        result[str(owner)] = {
            "completion_reverse_24": memory.u32(0x0123D144 + 24 * 4),
            "script_halted": memory.u32(0x0122FF28 + owner * 4),
            "script_cycle_counter": memory.u32(0x01233528 + owner * 4),
            "previous_script_cycle_counter": memory.i32(
                0x01233548 + owner * 4),
            "transport_phase_state": memory.u32(0x01233568 + owner * 4),
            "last_timing_frame": memory.u32(0x01233588 + owner * 4),
            "profile_age": memory.u32(0x01239C28 + owner * 4),
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
            "preferred_target_point": [
                memory.i32(0x012334E8 + owner * 8),
                memory.i32(0x012334EC + owner * 8)],
            "strategic_point": [
                memory.i32(0x01238EE8 + owner * 8),
                memory.i32(0x01238EEC + owner * 8)],
            "route_primary_slot": (
                (route_unit - 0x00A03FB8) // 0x1D0
                if route_unit >= 0x00A03FB8 and
                (route_unit - 0x00A03FB8) % 0x1D0 == 0 else None),
            "route_primary_world": (
                [memory.i32(route_unit + 0xB8),
                 memory.i32(route_unit + 0xBC)] if route_unit else None),
            "preferred_target_slot": (
                (preferred - 0x00A03FB8) // 0x1D0
                if preferred >= 0x00A03FB8 and
                (preferred - 0x00A03FB8) % 0x1D0 == 0 else None),
        }
        if IGNORE_COMPLETION_REVERSE_24:
            result[str(owner)].pop("completion_reverse_24", None)
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
        route_unit = struct.unpack("<Q", memory.read(route, 8))[0]
        preferred = memory.read(
            target + owner_strategic_target_layout["preferred_target"], 8)
        preferred = struct.unpack("<Q", preferred)[0]
        result[str(owner)] = {
            "completion_reverse_24": memory.u32(
                runtime + unit_reference_tables_offset +
                unit_reference_completion_reverse_offset + 24 * 4),
            "script_halted": memory.u32(slot),
            "script_cycle_counter": memory.u32(slot + 1432),
            "previous_script_cycle_counter": memory.i32(slot + 1436),
            "transport_phase_state": memory.u32(slot + 1440),
            "last_timing_frame": memory.u32(slot + 1444),
            "profile_age": memory.u32(slot + 1628),
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
            "preferred_target_point": [
                memory.i32(target + 16), memory.i32(target + 20)],
            "strategic_point": [
                memory.i32(target + 24), memory.i32(target + 28)],
            "route_primary_slot": pointer_slots.get(route_unit),
            "route_primary_world": (
                [memory.i32(route_unit + unit_layout["x"]),
                 memory.i32(route_unit + unit_layout["y"])]
                if route_unit else None),
            "preferred_target_slot": pointer_slots.get(preferred),
        }
        if IGNORE_COMPLETION_REVERSE_24:
            result[str(owner)].pop("completion_reverse_24", None)
    return result


def original_transport_queue_unit_slot(value):
    if value == 0:
        return None
    offset = value - 0x00A03FB8
    if offset < 0 or offset % 0x1D0 != 0:
        return f"raw:0x{value:08x}"
    return offset // 0x1D0


def rebuild_transport_queue_unit_slot(value):
    if value == 0:
        return None
    if value % 0x1D0 != 0:
        return f"id:0x{value:08x}"
    return value // 0x1D0


def original_transport_queues(memory, owners):
    result = {}
    for owner in owners:
        owner_base = 0x012336C8 + owner * 0xB00
        slots = []
        for index in range(OWNER_QUEUE_SLOT_COUNT):
            raw = memory.read(owner_base + index * 0x58, 40)
            words = u32_words(raw, 0, 10)
            # match_value and linked_group contain raw fixed-pool pointers in
            # the 32-bit original.  Normalize both to fixed-pool slots instead
            # of dropping them: route-helper production consumes match_value,
            # so a stale/missing binding is synchronized gameplay state.
            slots.append(words[0:5] + [
                original_transport_queue_unit_slot(words[5]),
            ] + words[6:9] + [
                original_transport_queue_unit_slot(words[9]),
            ])
        result[str(owner)] = slots
    return result


def rebuild_transport_queues(memory, runtime, owners):
    result = {}
    queues = runtime + owner_transport_queues_offset
    owner_size = OWNER_QUEUE_SLOT_COUNT * OWNER_QUEUE_SLOT_SIZE
    for owner in owners:
        raw = memory.read(queues + owner * owner_size, owner_size)
        result[str(owner)] = []
        for index in range(OWNER_QUEUE_SLOT_COUNT):
            words = u32_words(raw, index * OWNER_QUEUE_SLOT_SIZE, 10)
            # The typed reconstruction stores the corresponding stable unit
            # IDs (original fixed-pool offsets), which normalize to the same
            # slot numbers as the original pointers above.
            result[str(owner)].append(words[0:5] + [
                rebuild_transport_queue_unit_slot(words[5]),
            ] + words[6:9] + [
                rebuild_transport_queue_unit_slot(words[9]),
            ])
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
        base = memory.read(0x01230A28 + owner * 0x2A8, 0x2A8)
        shadow = memory.read(0x01231F68 + owner * 0x2A8, 0x2A8)
        base_values = u32_words(base, 0, 0xAA)
        shadow_values = u32_words(shadow, 0, 0xAA)
        result[str(owner)] = {
            "base_00_a9": base_values,
            "base_60_a9": base_values[0x60:0xAA],
            # The low-type half is consumed by the same desired-count sum as
            # the extended half.  Omitting it hid owner-AI production choices
            # even while all subsequently serialized unit rows still matched.
            "shadow_00_a9": shadow_values,
            "shadow_60_a9": shadow_values[0x60:0xAA],
        }
    return result


def rebuild_ai_demand(memory, runtime, owners):
    owner_ai = runtime + owner_ai_offset + owner_ai_layout["owners"]
    result = {}
    for owner in owners:
        slot = owner_ai + owner * owner_ai_layout["slot_size"]
        slot_bytes = memory.read(slot, owner_ai_layout["slot_size"])
        base_offset = owner_ai_layout["unit_demand"]
        shadow_offset = owner_ai_layout["unit_demand_shadow"]
        base_values = u32_words(slot_bytes, base_offset, 0xAA)
        shadow_values = u32_words(slot_bytes, shadow_offset, 0xAA)
        result[str(owner)] = {
            "base_00_a9": base_values,
            "base_60_a9": base_values[0x60:0xAA],
            "shadow_00_a9": shadow_values,
            "shadow_60_a9": shadow_values[0x60:0xAA],
        }
    return result


def original_planning_debug(memory, owners):
    result = {}
    for owner in owners:
        counts = u32_words(
            memory.read(0x00707430 + owner * 0x2A8, 0x2A8), 0, 0xAA)
        shared_grid = u32_words(
            memory.read(0x01239408 + owner * 0x100, 0x100), 0, 0x40)
        result[str(owner)] = {
            "faction": memory.u32(0x007251A4 + owner * 4),
            "primary_budget": memory.u32(0x0122FF88 + owner * 4),
            "route_load_percent": memory.u32(0x01230508 + owner * 4),
            "resource_budget_percent": memory.u32(0x01230628 + owner * 4),
            "profile_counter": memory.u32(0x01230928 + owner * 4),
            "production_pause": memory.u32(0x012393E8 + owner * 4),
            "reserved_primary_cost": memory.u32(0x01230A08 + owner * 4),
            "strategic_queue_load_percent": memory.u32(
                0x01238F28 + owner * 4),
            "primary_target_flags": memory.u32(0x01238F48 + owner * 4),
            "route0_desired_count_base": memory.u32(
                0x01230208 + owner * 0x18),
            "route0_priority": memory.u32(0x01230388 + owner * 0x18),
            "route0_flags": memory.u32(0x01230448 + owner * 0x18),
            "shared_grid": shared_grid,
            "owned_nonzero": {
                str(unit_type): count for unit_type, count in enumerate(counts)
                if count != 0
            },
        }
    return result


def rebuild_planning_debug(memory, runtime, owners):
    state = runtime + owner_ai_offset
    owner_slots = state + owner_ai_layout["owners"]
    result = {}
    for owner in owners:
        slot = owner_slots + owner * owner_ai_layout["slot_size"]
        route = (runtime + owner_transport_routes_offset +
                 owner * owner_transport_route_layout["size"])
        count_base = (state + owner_ai_layout["owner_unit_type_counts"] +
                      owner * 0xAA * 4)
        counts = u32_words(memory.read(count_base, 0x2A8), 0, 0xAA)
        shared_grid = u32_words(memory.read(
            state + owner_ai_layout["shared_grid_table"] + owner * 0x100,
            0x100), 0, 0x40)
        result[str(owner)] = {
            "faction": memory.u32(
                state + owner_ai_layout["owner_faction_ids"] + owner * 4),
            "primary_budget": memory.u32(slot + 0x0C),
            "route_load_percent": memory.u32(slot + 0x14),
            "resource_budget_percent": memory.u32(
                slot + owner_ai_layout["resource_budget_percent"]),
            "profile_counter": memory.u32(
                slot + owner_ai_layout["profile_counter"]),
            "production_pause": memory.u32(
                slot + owner_ai_layout["production_pause_flag"]),
            "reserved_primary_cost": memory.u32(
                runtime + owner_ai_reserved_primary_cost_offset + owner * 4),
            "strategic_queue_load_percent": memory.u32(slot + 0x5D8),
            "primary_target_flags": memory.u32(
                slot + owner_ai_layout["primary_target_flags"]),
            # OwnerTransportRouteTarget is 0x28 bytes on the 64-bit rebuild;
            # target 0 stores the scalar maintenance inputs at +0x18..+0x24.
            "route0_desired_count_base": memory.u32(route + 0x18),
            "route0_priority": memory.u32(route + 0x20),
            "route0_flags": memory.u32(route + 0x24),
            "shared_grid": shared_grid,
            "owned_nonzero": {
                str(unit_type): count for unit_type, count in enumerate(counts)
                if count != 0
            },
            "population_used": memory.u32(
                state + owner_ai_layout["owner_population_used"] + owner * 4),
            "population_reserved": memory.u32(
                state + owner_ai_layout["owner_population_reserved"] + owner * 4),
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

sides = {
    "original": {
        "memory": original,
        "frame_address": 0x007071A4,
        "last_seen": None,
        "changed_at": time.monotonic(),
        "next_candidate_at": time.monotonic(),
        "candidate": None,
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
        "snapshots": {},
    },
}


def capture(name, side):
    started = time.perf_counter()
    memory = side["memory"]
    frame_before = memory.u32(side["frame_address"])
    if name == "original":
        rng = [memory.u32(0x007071B8), memory.u32(0x007071BC),
               memory.u32(0x007071C0)]
        presentation_rng = memory.u32(0x007071C4)
        local_view_owner = memory.u32(0x00725100)
        world_view_owner = [local_view_owner,
                            memory.read(0x012448F0 + local_view_owner, 1)[0]
                            if local_view_owner < 8 else 0xFF]
        owner_visibility_masks = [
            memory.u32(0x00725384 + owner * 4) for owner in range(8)]
        world_view = [memory.i32(0x007071A8), memory.i32(0x007071AC),
                      memory.u32(0x0143FFF0), memory.u32(0x01440004)]
        camera_debug = {
            "presentation_rng": presentation_rng,
            "mouse": [memory.i32(0x014594A8), memory.i32(0x014594AC)],
            "message_mouse": [memory.i32(0x014594BC) & 0xffff,
                              (memory.i32(0x014594BC) >> 16) & 0xffff],
            "cursor_change_depth": memory.read(0x0086AC70, 1)[0],
            "pointer_updates_suppressed": memory.read(0x0145965D, 1)[0],
            "edge_mask": memory.u32(0x00869E2C),
            "scroll_ramp": memory.u32(0x00868144),
            "scroll_tick_bucket": memory.u32(0x008678F0),
            "current_tick_ms": memory.u32(0x0162EA48),
            "replay_timing_enabled": memory.u32(0x01242A20),
            "scroll_dirty": memory.u32(0x00868140),
        }
        rows, orders = original_lists(memory)
        render_queue = original_unit_render_queue(memory, rows)
        world_render_queue = original_world_render_queue(memory, rows)
        owners = sorted({row["owner"] for row in rows.values()
                         if row["owner"] < 8})
        economy = original_economy(memory, owners)
        production = original_production_state(memory)
        ai_demand = original_ai_demand(memory, owners)
        owner_ai = original_owner_ai(memory, owners)
        unit_effects = original_unit_effects(memory)
        map_effects = original_map_effects(memory)
        unit_reference_tables = original_unit_reference_tables(memory)
        reach_debug = original_reach_debug(memory, rows)
        planning_debug = original_planning_debug(memory, owners)
        transport_queues = original_transport_queues(memory, owners)
        visibility_debug = original_visibility_debug(memory, rows)
        movement_map_debug = original_movement_map_debug(memory, rows)
        follow_pool_debug = original_follow_pool_debug(memory)
    else:
        rng = [memory.u32(random_state), memory.u32(random_state + 4),
               memory.u32(random_state + 8)]
        presentation_rng = memory.u32(
            runtime + gameplay_sound_offset +
            gameplay_sound_layout["variant_seed"])
        player_slots = runtime + player_slots_offset
        local_view_owner = memory.u32(
            player_slots + player_slots_layout["local_player"])
        world_view_owner = [
            local_view_owner,
            memory.read(player_slots + player_slots_layout["slot_states"] +
                        local_view_owner, 1)[0]
            if local_view_owner < 8 else 0xFF,
        ]
        owner_visibility_masks = [
            memory.u32(player_slots +
                       player_slots_layout["visibility_masks"] + owner * 4)
            for owner in range(8)
        ]
        overlay = REBUILD_BASE + overlay_rva
        input_state = REBUILD_BASE + input_state_rva
        world_view = [
            memory.i32(overlay + overlay_layout["camera_x"]),
            memory.i32(overlay + overlay_layout["camera_y"]),
            memory.u32(overlay + overlay_layout["screen_width"]),
            memory.u32(overlay + overlay_layout["screen_height"]),
        ]
        camera_debug = {
            "presentation_rng": presentation_rng,
            "mouse": [
                memory.i32(overlay + overlay_layout["mouse_x"]),
                memory.i32(overlay + overlay_layout["mouse_y"]),
            ],
            "edge_mask": memory.u32(
                overlay + overlay_layout["camera_edge_cursor_index"]),
            "scroll_ramp": memory.u32(
                overlay + overlay_layout["camera_scroll_ramp"]),
            "scroll_tick_bucket": memory.u32(
                overlay + overlay_layout["camera_scroll_tick_bucket"]),
            "current_tick_ms": memory.u32(
                overlay + overlay_layout["current_tick_ms"]),
            "replay_timing_enabled": memory.read(
                overlay + overlay_layout["replay_timing_enabled"], 1)[0],
            "scroll_dirty": memory.read(
                overlay + overlay_layout["camera_scroll_dirty"], 1)[0],
            "edge_pointer_valid": memory.read(
                overlay + overlay_layout["camera_edge_pointer_valid"], 1)[0],
            "input_mouse": [
                memory.i32(input_state + input_layout["mouse_x"]),
                memory.i32(input_state + input_layout["mouse_y"]),
            ],
            "input_pointer_motion_seen": memory.read(
                input_state + input_layout["pointer_motion_seen"], 1)[0],
            "programmatic_pointer_motion_pending": memory.read(
                REBUILD_BASE + programmatic_pointer_motion_pending_rva, 1)[0],
            "programmatic_pointer_motion_target_reached": memory.read(
                REBUILD_BASE + programmatic_pointer_motion_target_reached_rva,
                1)[0],
            "programmatic_pointer_motion_target": [
                memory.i32(REBUILD_BASE + programmatic_pointer_motion_x_rva),
                memory.i32(REBUILD_BASE + programmatic_pointer_motion_y_rva),
            ],
        }
        rows, orders, pointer_slots, id_slots = rebuild_lists(memory, movement)
        render_queue = rebuild_unit_render_queue(memory, runtime)
        world_render_queue = rebuild_world_render_queue(memory, runtime)
        owners = sorted({row["owner"] for row in rows.values()
                         if row["owner"] < 8})
        economy = rebuild_economy(memory, runtime, owners)
        production = rebuild_production_state(memory, runtime)
        ai_demand = rebuild_ai_demand(memory, runtime, owners)
        owner_ai = rebuild_owner_ai(memory, runtime, owners, pointer_slots)
        unit_effects = rebuild_unit_effects(memory, runtime, id_slots)
        map_effects = rebuild_map_effects(memory, runtime, pointer_slots)
        unit_reference_tables = rebuild_unit_reference_tables(memory, runtime)
        reach_debug = rebuild_reach_debug(memory, rows, pointer_slots)
        planning_debug = rebuild_planning_debug(memory, runtime, owners)
        transport_queues = rebuild_transport_queues(memory, runtime, owners)
        visibility_debug = rebuild_visibility_debug(memory, rows)
        movement_map_debug = rebuild_movement_map_debug(memory, rows)
        follow_pool_debug = rebuild_follow_pool_debug(memory, movement)
    world_render_queue["semantic_entries"] = semantic_world_render_entries(
        world_render_queue, world_view[0], world_view[1], name)
    # Owner 8+ rows include neutral map placeholders.  The original keeps stale
    # target pointers in some of those slots, while the rebuild clears them.
    # Normalise only fields that are neither live simulation nor world-render
    # state.  In particular, type 164's animation frame must remain visible to
    # parity checks even though it is absent from the P2P checksum.
    for row in rows.values():
        # Raw queue slots retain old bytes after the live count reaches zero.
        # Only deferred_count entries are semantically active; comparing the
        # first inactive slot creates a false deterministic divergence.
        if row["deferred_count"] == 0:
            row["deferred_first"] = [0, 0, 0, 0]
        # State 0x50's population/resource failure notification stores a
        # one-shot flag in original raw +0x38.  It is set only for the local
        # presentation owner (FUN_004cdecc failure branches) and may therefore
        # differ between replay viewpoints without affecting synchronized
        # simulation.  The flag can remain through state 0x51 until completion.
        if (row["state"] & 0x00FFFFFF) in (0x50, 0x51):
            equipment_index = 1 if row["type"] >= 0x60 else 2
            if equipment_index < len(row["equipment"]):
                row["equipment"][equipment_index] = 0
        if row["owner"] >= 8:
            row["target_slot"] = None
    frame_after = memory.u32(side["frame_address"])
    if frame_before != frame_after:
        return None
    state = {
        "rng": rng,
        "world_view_owner": world_view_owner,
        "owner_visibility_masks": owner_visibility_masks,
        "world_view": world_view,
        "rows": {str(slot): rows[slot] for slot in sorted(rows)},
        "active_order": orders["active"],
        "lifecycle_order": orders["lifecycle"],
        "economy": economy,
        "production": production,
        "ai_demand": ai_demand,
        "owner_ai": owner_ai,
        "transport_queues": transport_queues,
        "unit_reference_tables": unit_reference_tables,
        "unit_effects": unit_effects,
        "map_effects": map_effects,
    }
    return {
        "side": name,
        "frame": frame_after,
        "state": state,
        "state_sha256": digest(state),
        "reach_debug": reach_debug,
        "visibility_debug": visibility_debug,
        "movement_map_debug": movement_map_debug,
        "planning_debug": planning_debug,
        "follow_pool_debug": follow_pool_debug,
        "render_queue_debug": render_queue,
        "world_render_queue_debug": world_render_queue,
        # Raw input/camera driver values are diagnostic only. They depend on
        # wall-clock message delivery and must not make deterministic gameplay
        # state unequal, but retaining them makes startup/edge-scroll failures
        # actionable when world_view is the first semantic difference.
        "camera_debug": camera_debug,
        "capture_ms": round((time.perf_counter() - started) * 1000.0, 3),
        "captured_ns": time.time_ns(),
    }


def quake_camera_shake_active(state):
    """Return whether effect 0x4b can move the presentation camera now."""
    return any(
        effect.get("effect_id") == 0x4B and
        (effect.get("flags", 0) & 0x80) != 0 and
        (effect.get("flags", 0) & 0x02) == 0
        for effect in state.get("unit_effects", []))


def snapshot_difference(left, right):
    left_state = left["state"]
    right_state = right["state"]
    if IGNORE_WORLD_VIEW:
        # Camera scrolling is driven by absolute 31-ms buckets, not by the
        # deterministic simulation frame.  Fresh processes can enter gameplay
        # in different wall-clock buckets even when their simulation state is
        # aligned.  Simulation traces may therefore omit only the viewport;
        # camera_debug remains in the detail artifact and dedicated sequential
        # camera checkpoints still compare the complete state.
        left_state = {
            key: value for key, value in left_state.items()
            if key != "world_view"
        }
        right_state = {
            key: value for key, value in right_state.items()
            if key != "world_view"
        }
    difference = first_difference(left_state, right_state)
    compared_render_queue = False
    compared_world_render_queue = False
    if difference is None and not IGNORE_RENDER_QUEUES:
        left_queue = left.get("render_queue_debug", {})
        right_queue = right.get("render_queue_debug", {})
        if (left_queue.get("ready") and right_queue.get("ready") and
                left_queue.get("entries") is not None and
                right_queue.get("entries") is not None):
            compared_render_queue = True
            difference = first_difference(
                left_queue["entries"], right_queue["entries"],
                "state.render_queue")
    if (difference is None and not IGNORE_RENDER_QUEUES and
            not quake_camera_shake_active(left_state) and
            not quake_camera_shake_active(right_state)):
        left_queue = left.get("world_render_queue_debug", {})
        right_queue = right.get("world_render_queue_debug", {})
        if (left_queue.get("ready") and right_queue.get("ready") and
                left_queue.get("semantic_entries") is not None and
                right_queue.get("semantic_entries") is not None):
            compared_world_render_queue = True
            difference = first_difference(
                left_queue["semantic_entries"],
                right_queue["semantic_entries"],
                "state.world_render_queue")
    return difference, compared_render_queue, compared_world_render_queue


started = time.monotonic()
first_pair = None
last_pair = None
exact_pairs = 0
render_queue_compared_pairs = 0
world_render_queue_compared_pairs = 0
pair_gaps = []
previous_exact = None
baseline_mismatch = None
pending_divergence = None
pending_rng_divergence = None
consecutive_rng_unaligned = 0
transient_divergences = 0
coverage = {
    "min_active_units": None,
    "max_active_units": 0,
    "min_lifecycle_units": None,
    "max_lifecycle_units": 0,
    "max_unit_effects": 0,
    "max_map_effects": 0,
    "frames_with_lifecycle": 0,
    "frames_with_unit_effects": 0,
    "frames_with_map_effects": 0,
    "frames_with_kill_or_loss": 0,
    "max_world_render_commands": 0,
    "frames_with_world_render_queue": 0,
    "frames_with_map_effect_render": 0,
    "frames_with_brush_edge_render": 0,
    "world_render_kinds": set(),
    "world_render_kind_max_counts": {},
    "unit_types": set(),
    "command_states": set(),
    "unit_effect_ids": set(),
    "unit_effect_links": set(),
    "map_effect_ids": set(),
    "unit_health_ranges": {},
    "player_unit_state_ranges": {},
    "economy_ranges": {},
    "owners": set(),
    "owner_ai_digests": set(),
}
detail_path = os.path.splitext(RESULT_PATH)[0] + "-detail.json"
pending_detail_path = os.path.splitext(RESULT_PATH)[0] + "-pending-detail.json"
os.makedirs(os.path.dirname(os.path.abspath(RESULT_PATH)), exist_ok=True)


def update_coverage(snapshot):
    state = snapshot["state"]
    active_count = len(state["active_order"])
    lifecycle_count = len(state["lifecycle_order"])
    coverage["min_active_units"] = (
        active_count if coverage["min_active_units"] is None else
        min(coverage["min_active_units"], active_count))
    coverage["min_lifecycle_units"] = (
        lifecycle_count if coverage["min_lifecycle_units"] is None else
        min(coverage["min_lifecycle_units"], lifecycle_count))
    coverage["max_active_units"] = max(
        coverage["max_active_units"], active_count)
    coverage["max_lifecycle_units"] = max(
        coverage["max_lifecycle_units"], lifecycle_count)
    coverage["max_unit_effects"] = max(
        coverage["max_unit_effects"], len(state["unit_effects"]))
    coverage["max_map_effects"] = max(
        coverage["max_map_effects"], len(state["map_effects"]))
    coverage["frames_with_lifecycle"] += bool(state["lifecycle_order"])
    coverage["frames_with_unit_effects"] += bool(state["unit_effects"])
    coverage["frames_with_map_effects"] += bool(state["map_effects"])
    coverage["frames_with_kill_or_loss"] += any(
        any(owner[key] for key in
            ("unit_kills", "building_kills", "unit_lost", "building_lost"))
        for owner in state["economy"].values())
    world_entries = snapshot.get(
        "world_render_queue_debug", {}).get("semantic_entries")
    if world_entries is not None:
        coverage["frames_with_world_render_queue"] += bool(world_entries)
        coverage["max_world_render_commands"] = max(
            coverage["max_world_render_commands"], len(world_entries))
        frame_kind_counts = {}
        for entry in world_entries:
            kind = entry.get("kind", "unknown")
            coverage["world_render_kinds"].add(kind)
            frame_kind_counts[kind] = frame_kind_counts.get(kind, 0) + 1
        coverage["frames_with_map_effect_render"] += bool(
            frame_kind_counts.get("map_effect"))
        coverage["frames_with_brush_edge_render"] += bool(
            frame_kind_counts.get("brush_edge"))
        for kind, count in frame_kind_counts.items():
            coverage["world_render_kind_max_counts"][kind] = max(
                coverage["world_render_kind_max_counts"].get(kind, 0), count)
    for owner, values in state["economy"].items():
        owner_ranges = coverage["economy_ranges"].setdefault(str(owner), {})
        for key, value in values.items():
            value_range = owner_ranges.setdefault(
                key, {"min": value, "max": value})
            value_range["min"] = min(value_range["min"], value)
            value_range["max"] = max(value_range["max"], value)
    for row in state["rows"].values():
        coverage["unit_types"].add(row["type"])
        coverage["command_states"].add(row["state"])
        if row["owner"] < 8:
            coverage["owners"].add(row["owner"])
    for slot, row in state["rows"].items():
        health = coverage["unit_health_ranges"].setdefault(
            str(slot), {
                "min": row["health"],
                "max": row["health"],
                "types": set(),
                "owners": set(),
            })
        health["min"] = min(health["min"], row["health"])
        health["max"] = max(health["max"], row["health"])
        health["types"].add(row["type"])
        health["owners"].add(row["owner"])
        if row["owner"] < 8:
            unit_range = coverage["player_unit_state_ranges"].setdefault(
                str(slot), {
                    "types": set(),
                    "owners": set(),
                    "lists": set(),
                    "states": set(),
                    "world_x": {"min": row["world"][0], "max": row["world"][0]},
                    "world_y": {"min": row["world"][1], "max": row["world"][1]},
                    "cargo": {"min": row["cargo"], "max": row["cargo"]},
                    "secondary": {
                        "min": row["secondary"], "max": row["secondary"]},
                    "max_health": {
                        "min": row["max_health"], "max": row["max_health"]},
                    "max_secondary": {
                        "min": row["max_secondary"],
                        "max": row["max_secondary"]},
                    "runtime_stat_1c": {
                        "min": row["runtime_stat_1c"],
                        "max": row["runtime_stat_1c"]},
                    "runtime_stat_20": {
                        "min": row["runtime_stat_20"],
                        "max": row["runtime_stat_20"]},
                    "runtime_stat_28": {
                        "min": row["runtime_stat_28"],
                        "max": row["runtime_stat_28"]},
                    "elite_progress": {
                        "min": row["elite_progress"],
                        "max": row["elite_progress"]},
                    "status_timer": {
                        "min": row["status_timer"],
                        "max": row["status_timer"]},
                    "command_flags": {
                        "min": row["command_flags"],
                        "max": row["command_flags"]},
                    "command_flag_values": set(),
                    "action_mode": {
                        "min": row["action_mode"], "max": row["action_mode"]},
                    "production_variants": set(),
                    "deferred_count": {
                        "min": row["deferred_count"],
                        "max": row["deferred_count"]},
                    "equipment": set(),
                })
            unit_range["types"].add(row["type"])
            unit_range["owners"].add(row["owner"])
            unit_range["lists"].add(row["list"])
            unit_range["states"].add(row["state"])
            for axis, value in (("world_x", row["world"][0]),
                                ("world_y", row["world"][1]),
                                ("cargo", row["cargo"]),
                                ("secondary", row["secondary"]),
                                ("max_health", row["max_health"]),
                                ("max_secondary", row["max_secondary"]),
                                ("runtime_stat_1c", row["runtime_stat_1c"]),
                                ("runtime_stat_20", row["runtime_stat_20"]),
                                ("runtime_stat_28", row["runtime_stat_28"]),
                                ("elite_progress", row["elite_progress"]),
                                ("status_timer", row["status_timer"]),
                                ("command_flags", row["command_flags"]),
                                ("action_mode", row["action_mode"]),
                                ("deferred_count", row["deferred_count"])):
                unit_range[axis]["min"] = min(unit_range[axis]["min"], value)
                unit_range[axis]["max"] = max(unit_range[axis]["max"], value)
            unit_range["production_variants"].add(row["production_variant"])
            unit_range["command_flag_values"].add(row["command_flags"])
            unit_range["equipment"].add(tuple(row["equipment"]))
    coverage["unit_effect_ids"].update(
        effect["effect_id"] for effect in state["unit_effects"])
    coverage["unit_effect_links"].update(
        (effect["effect_id"], effect["source_slot"], effect["target_slot"])
        for effect in state["unit_effects"])
    coverage["map_effect_ids"].update(
        effect["effect_id"] for effect in state["map_effects"])
    coverage["owner_ai_digests"].add(digest(state["owner_ai"]))


def coverage_summary():
    def sorted_coverage(values):
        # Effect links may legitimately contain a null source or target.  A
        # native tuple sort cannot compare None with integer slot numbers.
        return sorted(
            values,
            key=lambda value: json.dumps(
                value, sort_keys=True, separators=(",", ":")))

    result = {
        key: (sorted_coverage(value) if isinstance(value, set) else value)
        for key, value in coverage.items()
        if key not in ("owner_ai_digests", "unit_health_ranges",
                       "player_unit_state_ranges", "economy_ranges")
    }
    result["unit_health_ranges"] = {
        slot: {
            key: (sorted_coverage(value) if isinstance(value, set) else value)
            for key, value in row.items()
        }
        for slot, row in coverage["unit_health_ranges"].items()
    }
    result["player_unit_state_ranges"] = {
        slot: {
            key: (sorted_coverage(value) if isinstance(value, set) else value)
            for key, value in row.items()
        }
        for slot, row in coverage["player_unit_state_ranges"].items()
    }
    result["economy_ranges"] = coverage["economy_ranges"]
    result["owner_ai_distinct_states"] = len(coverage["owner_ai_digests"])
    return result


def map_debug_tiles(rows):
    tiles = set()
    for slot in MAP_DEBUG_SLOTS:
        row = rows.get(slot)
        if row is None:
            continue
        for field in ("world", "current_cell", "path", "next", "destination"):
            point = row.get(field)
            if not isinstance(point, list) or len(point) != 2:
                continue
            tile_x = (point[0] & 0xFFFFFFFF) >> 5
            tile_y = (point[1] & 0xFFFFFFFF) >> 5
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    if tile_x + dx >= 0 and tile_y + dy >= 0:
                        tiles.add((tile_x + dx, tile_y + dy))
    return sorted(tiles)


def original_movement_map_debug(memory, rows):
    result = {}
    for tile_x, tile_y in map_debug_tiles(rows):
        if tile_x >= 0x100 or tile_y >= 0x100:
            continue
        # The legacy grids have a 0x100-cell row stride (0x400 bytes).
        offset = tile_y * 0x400 + tile_x * 4
        result[f"{tile_x},{tile_y}"] = {
            "source": memory.u32(0x00E99E74 + offset),
            "terrain": memory.u32(0x00F19E74 + offset),
            "brush": memory.u32(0x00758D40 + offset),
        }
    return result


def rebuild_movement_map_debug(memory, rows):
    map_address = movement + context_layout["map"]
    width = memory.u32(map_address + movement_map_layout["width"])
    height = memory.u32(map_address + movement_map_layout["height"])
    stride = memory.u32(map_address + movement_map_layout["stride"])
    # The map vector stores 12-byte structs, so read its begin pointer rather
    # than treating it as the pointer vectors used by the unit lists.
    cells_begin = u64(memory.read(
        map_address + movement_map_layout["cells"], 8), 0)
    cell_size = movement_map_layout["cell_size"]
    result = {}
    for tile_x, tile_y in map_debug_tiles(rows):
        if tile_x >= width or tile_y >= height or cells_begin == 0:
            continue
        cell = memory.read(
            cells_begin + (tile_y * stride + tile_x) * cell_size, cell_size)
        result[f"{tile_x},{tile_y}"] = {
            "source": u32(cell, 8),
            "terrain": u32(cell, movement_map_layout["cell_flags"]),
            "brush": u32(cell, 4),
        }
    return result


def sparse_production_state(variant_bytes, lock_bytes, completed_words,
                            effect_words, order_2b_words):
    return {
        "variants": [
            [owner, order, variant_bytes[owner * 64 + order]]
            for owner in range(8) for order in range(64)
            if variant_bytes[owner * 64 + order] != 0
        ],
        "locks": [
            [owner, order, lock_bytes[owner * 64 + order]]
            for owner in range(8) for order in range(64)
            if lock_bytes[owner * 64 + order] != 0
        ],
        "completed_types": [
            [owner, unit_type, completed_words[owner * 170 + unit_type]]
            for owner in range(8) for unit_type in range(170)
            if completed_words[owner * 170 + unit_type] != 0
        ],
        "completion_effects": [
            [effect, owner, unit_type,
             effect_words[(effect * 8 + owner) * 170 + unit_type]]
            for effect in range(18) for owner in range(8)
            for unit_type in range(170)
            if effect_words[(effect * 8 + owner) * 170 + unit_type] != 0
        ],
        "order_2b_bonus": order_2b_words,
    }


def original_production_state(memory):
    # These original live tables are also the serialized primary-record
    # regions: owner/order cells at DAT_00708970, completed-type DWORDs at
    # DAT_00707430, order-2b totals at DAT_0070A434, and the 18 effect tables
    # at DAT_0070A484.
    cells = memory.read(0x00708970, 8 * 64 * 4)
    variants = bytes(cells[index * 4] for index in range(8 * 64))
    locks = bytes(cells[index * 4 + 1] for index in range(8 * 64))
    completed = u32_words(memory.read(0x00707430, 8 * 170 * 4), 0, 8 * 170)
    effects = u32_words(
        memory.read(0x0070A484, 18 * 8 * 170 * 4),
        0, 18 * 8 * 170)
    order_2b = u32_words(memory.read(0x0070A434, 8 * 4), 0, 8)
    return sparse_production_state(
        variants, locks, completed, effects, order_2b)


def rebuild_production_state(memory, runtime):
    production = runtime + production_runtime_offset
    variants = memory.read(
        production + production_runtime_layout["variant_counts"], 8 * 64)
    locks = memory.read(
        production + production_runtime_layout["lock_flags"], 8 * 64)
    # After the two byte tables and the 8x64x2 opaque cells, typed completed
    # counts start at +0x800 and the 18 effect tables at +0x1D40.
    completed = u32_words(
        memory.read(production + 0x800, 8 * 170 * 4), 0, 8 * 170)
    effects = u32_words(
        memory.read(production + 0x1D40, 18 * 8 * 170 * 4),
        0, 18 * 8 * 170)
    order_2b = u32_words(memory.read(production + 0x19C00, 8 * 4), 0, 8)
    return sparse_production_state(
        variants, locks, completed, effects, order_2b)


def terminal_summary():
    if previous_exact is None:
        return None
    snapshot = previous_exact["original"]
    state = snapshot["state"]
    player_units = {}
    for slot, row in state["rows"].items():
        if row["owner"] >= 8:
            continue
        player_units[str(slot)] = {
            key: row.get(key)
            for key in (
                "type", "owner", "list", "state", "previous_state",
                "string_slot",
                "health", "max_health", "max_secondary", "secondary",
                "runtime_stat_1c", "runtime_stat_20", "runtime_stat_28",
                "elite_progress", "status_timer", "action_mode",
                "production_variant", "equipment", "cargo",
                "command_flags", "runtime_flags", "type_flags",
                "area_marker_flags",
                "distance_mode", "script_bits", "animation_frame",
                "work_timer", "lockout",
                "deferred_count", "deferred_first", "linked_effect",
                "active_payload", "target_slot", "world",
                "path", "current_cell", "next", "anchor",
                "movement_flags", "movement_state",
            )
        }
    return {
        "frame": snapshot["frame"],
        "state_sha256": snapshot["state_sha256"],
        "rng": state["rng"],
        "active_unit_count": len(state["active_order"]),
        "lifecycle_unit_count": len(state["lifecycle_order"]),
        "unit_effect_count": len(state["unit_effects"]),
        "map_effect_count": len(state["map_effects"]),
        "economy": state["economy"],
        "production": state["production"],
        "transport_queues": state["transport_queues"],
        "owner_ai_sha256": digest(state["owner_ai"]),
        "player_units": player_units,
        "unit_effects": state["unit_effects"],
        "map_effects": state["map_effects"],
    }

try:
    with open(JOURNAL_PATH, "w", encoding="utf-8", newline="\n") as journal:
        while time.monotonic() - started < MAX_SECONDS:
            if os.path.exists(STOP_PATH):
                # A controlled final-state audit intentionally suspends both
                # peers on the same completed frame.  There is no following
                # frame to promote the stable candidates, so compare those
                # candidates directly before producing the stop summary.
                if exact_pairs == 0:
                    left = sides["original"]["candidate"]
                    right = sides["rebuild"]["candidate"]
                    difference = None
                    if (left is not None and right is not None and
                            left["frame"] == right["frame"]):
                        (difference, compared_render_queue,
                         compared_world_render_queue) = snapshot_difference(
                             left, right)
                        render_queue_compared_pairs += int(compared_render_queue)
                        world_render_queue_compared_pairs += int(
                            compared_world_render_queue)
                    if (left is not None and right is not None and
                            left["frame"] == right["frame"] and
                            difference is None):
                        frame = left["frame"]
                        exact_pairs = 1
                        first_pair = frame
                        last_pair = frame
                        previous_exact = {"original": left, "rebuild": right}
                        update_coverage(left)
                    elif difference is not None:
                        baseline_mismatch = {
                            "frame": left["frame"],
                            "field": difference[0],
                            "original": difference[1],
                            "rebuild": difference[2],
                        }
                        atomic_json(pending_detail_path, {
                            "probe": "suspended terminal baseline mismatch",
                            "sha256": layout.get("sha256"),
                            "divergence_pair": {
                                "original": left,
                                "rebuild": right,
                            },
                            "first_difference": baseline_mismatch,
                        })
                established_baseline = exact_pairs > 0
                summary = {
                    "pass": established_baseline,
                    "reason": (
                        "driver stop marker reached with no observed divergence"
                        if established_baseline else
                        "driver stopped before an exact expanded-state baseline"),
                    "sha256": layout.get("sha256"),
                    "exact_pair_count": exact_pairs,
                    "first_exact_frame": first_pair,
                    "last_exact_frame": last_pair,
                    "pair_gaps": pair_gaps,
                    "render_queue_compared_pairs": render_queue_compared_pairs,
                    "world_render_queue_compared_pairs":
                        world_render_queue_compared_pairs,
                    "semantic_coverage": coverage_summary(),
                    "terminal_exact_state": terminal_summary(),
                    "journal_path": JOURNAL_PATH,
                }
                if baseline_mismatch is not None:
                    summary["baseline_mismatch"] = baseline_mismatch
                atomic_json(RESULT_PATH, summary)
                break

            now = time.monotonic()
            for name, side in sides.items():
                frame = side["memory"].u32(side["frame_address"])
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
                if (frame <= 0 or now < side["next_candidate_at"]):
                    continue
                snapshot = capture(name, side)
                if snapshot is None or snapshot["frame"] != frame:
                    side["next_candidate_at"] = (
                        time.monotonic() + CANDIDATE_INTERVAL_SECONDS)
                    continue
                side["candidate"] = snapshot
                side["next_candidate_at"] = (
                    time.monotonic() + CANDIDATE_INTERVAL_SECONDS)

            if CAPTURE_SUSPENDED_ONCE:
                left = sides["original"]["candidate"]
                right = sides["rebuild"]["candidate"]
                if (left is not None and right is not None and
                        left["frame"] == right["frame"]):
                    (difference, compared_render_queue,
                     compared_world_render_queue) = snapshot_difference(
                         left, right)
                    atomic_json(detail_path, {
                        "probe": "suspended pair one-shot comparison",
                        "sha256": layout.get("sha256"),
                        "divergence_pair": {
                            "original": left,
                            "rebuild": right,
                        },
                        "first_difference": (
                            None if difference is None else {
                                "frame": left["frame"],
                                "field": difference[0],
                                "original": difference[1],
                                "rebuild": difference[2],
                            }),
                    })
                    atomic_json(RESULT_PATH, {
                        "pass": difference is None,
                        "reason": (
                            "suspended pair expanded state is exact"
                            if difference is None else
                            "suspended pair expanded state differs"),
                        "sha256": layout.get("sha256"),
                        "frame": left["frame"],
                        "render_queue_compared": compared_render_queue,
                        "world_render_queue_compared":
                            compared_world_render_queue,
                        "first_difference": (
                            None if difference is None else {
                                "field": difference[0],
                                "original": difference[1],
                                "rebuild": difference[2],
                            }),
                        "detail_path": detail_path,
                    })
                    raise SystemExit(0 if difference is None else 2)

            common = sorted(set(sides["original"]["snapshots"]) &
                            set(sides["rebuild"]["snapshots"]))
            for frame in common:
                left = sides["original"]["snapshots"].pop(frame)
                right = sides["rebuild"]["snapshots"].pop(frame)
                (difference, compared_render_queue,
                 compared_world_render_queue) = snapshot_difference(
                     left, right)
                render_queue_compared_pairs += int(compared_render_queue)
                world_render_queue_compared_pairs += int(
                    compared_world_render_queue)
                exact = difference is None
                rng_aligned = (left["state"]["rng"] == right["state"]["rng"])
                journal_row = {
                    "frame": frame,
                    "exact": exact,
                    "rng_aligned": rng_aligned,
                    "original_sha256": left["state_sha256"],
                    "rebuild_sha256": right["state_sha256"],
                    "rng": left["state"]["rng"],
                    "rebuild_rng": right["state"]["rng"],
                    "world_view": left["state"]["world_view"],
                    "rebuild_world_view": right["state"]["world_view"],
                    "presentation_rng":
                        left["camera_debug"]["presentation_rng"],
                    "rebuild_presentation_rng":
                        right["camera_debug"]["presentation_rng"],
                    "capture_ms": [left["capture_ms"], right["capture_ms"]],
                    "render_queue_compared": compared_render_queue,
                    "world_render_queue_compared":
                        compared_world_render_queue,
                    "command_states": sorted({
                        row["state"]
                        for row in left["state"].get("rows", {}).values()
                    }),
                    # Initial scenario records do not carry the transient
                    # flags that choose the less common world-sprite paths.
                    # Retain only units exercising one of those paths so a
                    # long replay journal stays compact while still exposing
                    # the exact frame and camera target needed for a pixel
                    # capture.
                    "visual_modes": [
                        {
                            "slot": slot,
                            "type": row.get("type"),
                            "owner": row.get("owner"),
                            "world": row.get("world"),
                            "bar": row.get("world_bar_selection"),
                            "area": row.get("area_marker_flags"),
                            "command_bits": row.get("command_bits0"),
                            "command_flags": row.get("command_flags"),
                            "runtime_flags": row.get("runtime_flags"),
                            "draw_flags": row.get("world_draw_flags"),
                        }
                        for slot, row in sorted(
                            left["state"].get("rows", {}).items(),
                            key=lambda item: int(item[0]))
                        if (
                            (row.get("world_bar_selection", 0) & 0x80) or
                            (row.get("area_marker_flags", 0) & 0x80000000) or
                            (row.get("command_bits0", 0) & 0x80) or
                            (row.get("command_flags", 0) & 0x003c0040) or
                            (row.get("runtime_flags", 0) & 0x00041070) or
                            (row.get("world_draw_flags", 0) & 0x82)
                        )
                    ],
                    "unit_effects": [
                        {
                            "slot": effect.get("slot"),
                            "id": effect.get("effect_id"),
                            "source": effect.get("source_slot"),
                            "target": effect.get("target_slot"),
                            "x": effect.get("x"),
                            "y": effect.get("y"),
                        }
                        for effect in left["state"].get("unit_effects", [])
                    ],
                    "map_effect_ids": [
                        effect.get("effect_id")
                        for effect in left["state"].get("map_effects", [])
                    ],
                }
                if difference is not None:
                    journal_row["first_difference"] = {
                        "field": difference[0],
                        "original": difference[1],
                        "rebuild": difference[2],
                    }
                    left_rows = left["state"].get("rows", {})
                    right_rows = right["state"].get("rows", {})
                    changed_slots = [
                        slot for slot in sorted(
                            set(left_rows) | set(right_rows),
                            key=lambda value: int(value))
                        if left_rows.get(slot) != right_rows.get(slot)
                    ]
                    # Keep enough surrounding unit state to diagnose a spawn,
                    # death, or command transition whose first list difference
                    # is merely the newly allocated/retired slot.
                    journal_row["changed_rows"] = {
                        slot: {
                            "original": left_rows.get(slot),
                            "rebuild": right_rows.get(slot),
                        }
                        for slot in changed_slots[:32]
                    }
                journal.write(json.dumps(
                    journal_row, separators=(",", ":")) + "\n")
                journal.flush()

                # A same frame number is not by itself a stable simulation
                # boundary: both executables increment it near the beginning
                # of the tick.  Different shared-RNG triples prove that the
                # captures landed in different intra-tick phases, so neither
                # confirm nor clear a pending deterministic divergence with
                # such a pair.
                if not rng_aligned:
                    # Paired fixture setup suspends and resumes two independent
                    # processes.  Until one fully identical completed frame
                    # proves a common baseline, a same-number capture may
                    # still straddle that handoff.  Do not promote startup
                    # skew into a product divergence; a run that never reaches
                    # an exact baseline still fails with exact_pair_count zero.
                    if exact_pairs == 0:
                        pending_rng_divergence = None
                        consecutive_rng_unaligned = 0
                        continue
                    consecutive_rng_unaligned += 1
                    if pending_rng_divergence is None:
                        pending_rng_divergence = {
                            "frame": frame,
                            "left": left,
                            "right": right,
                        }
                    if consecutive_rng_unaligned >= 3:
                        first = pending_rng_divergence
                        first_left = first["left"]
                        first_right = first["right"]
                        detail = {
                            "probe": "low-load same-simulation-frame semantic audit",
                            "sha256": layout.get("sha256"),
                            "first_observed_divergence": {
                                "frame": first["frame"],
                                "field": "state.rng",
                                "original": first_left["state"]["rng"],
                                "rebuild": first_right["state"]["rng"],
                                "original_rng": first_left["state"]["rng"],
                                "rebuild_rng": first_right["state"]["rng"],
                            },
                            "previous_exact_pair": previous_exact,
                            "divergence_pair": {
                                "original": first_left,
                                "rebuild": first_right,
                            },
                            "coverage": {
                                "exact_pair_count": exact_pairs,
                                "first_exact_frame": first_pair,
                                "last_exact_frame": last_pair,
                                "pair_gaps": pair_gaps,
                                "semantic_coverage": coverage_summary(),
                            },
                            "confirmation_pair": {
                                "original": left,
                                "rebuild": right,
                            },
                        }
                        atomic_json(detail_path, detail)
                        atomic_json(RESULT_PATH, {
                            "pass": False,
                            "reason": "persistent same-frame RNG divergence",
                            "frame": first["frame"],
                            "field": "state.rng",
                            "original": first_left["state"]["rng"],
                            "rebuild": first_right["state"]["rng"],
                            "original_rng": first_left["state"]["rng"],
                            "rebuild_rng": first_right["state"]["rng"],
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

                pending_rng_divergence = None
                consecutive_rng_unaligned = 0

                if difference is not None:
                    if exact_pairs == 0:
                        if baseline_mismatch is None:
                            baseline_mismatch = {
                                "frame": frame,
                                "field": difference[0],
                                "original": difference[1],
                                "rebuild": difference[2],
                            }
                            atomic_json(pending_detail_path, {
                                "probe": "first RNG-aligned baseline mismatch",
                                "sha256": layout.get("sha256"),
                                "divergence_pair": {
                                    "original": left,
                                    "rebuild": right,
                                },
                                "first_difference": baseline_mismatch,
                            })
                        continue
                    # ReadProcessMemory observes a process without stopping its
                    # simulation thread.  The frame counter advances near the
                    # start of a tick, so a latest valid capture can still land
                    # before the late owner-AI/unit phases on one peer and after
                    # them on the other.  A real deterministic fork persists;
                    # an intra-frame observation skew resynchronizes at the next
                    # comparable completed frame.  Hold the first mismatch until
                    # a second mismatching frame confirms it.
                    if pending_divergence is None:
                        pending_divergence = {
                            "frame": frame,
                            "difference": difference,
                            "left": left,
                            "right": right,
                        }
                        # Preserve the first RNG-aligned state mismatch even
                        # if a later intra-frame RNG skew prevents the normal
                        # two-pair confirmation path from serializing it.
                        atomic_json(pending_detail_path, {
                            "probe": "first RNG-aligned pending divergence",
                            "sha256": layout.get("sha256"),
                            "previous_exact_pair": previous_exact,
                            "divergence_pair": {
                                "original": left,
                                "rebuild": right,
                            },
                            "first_difference": {
                                "frame": frame,
                                "field": difference[0],
                                "original": difference[1],
                                "rebuild": difference[2],
                            },
                        })
                        continue

                    confirmation_pair = {
                        "original": left,
                        "rebuild": right,
                    }
                    first = pending_divergence
                    frame = first["frame"]
                    difference = first["difference"]
                    left = first["left"]
                    right = first["right"]
                    field, original_value, rebuild_value = difference
                    detail = {
                        "probe": "low-load same-simulation-frame semantic audit",
                        "sha256": layout.get("sha256"),
                        "first_observed_divergence": {
                            "frame": frame,
                            "field": field,
                            "original": original_value,
                            "rebuild": rebuild_value,
                            "original_rng": left["state"]["rng"],
                            "rebuild_rng": right["state"]["rng"],
                        },
                        "previous_exact_pair": previous_exact,
                        "divergence_pair": {
                            "original": left,
                            "rebuild": right,
                        },
                        "coverage": {
                            "exact_pair_count": exact_pairs,
                            "first_exact_frame": first_pair,
                            "last_exact_frame": last_pair,
                            "pair_gaps": pair_gaps,
                            "semantic_coverage": coverage_summary(),
                        },
                        "confirmation_pair": confirmation_pair,
                    }
                    atomic_json(detail_path, detail)
                    summary = {
                        "pass": False,
                        "reason": "first observed same-simulation-frame divergence",
                        "frame": frame,
                        "field": field,
                        "original": original_value,
                        "rebuild": rebuild_value,
                        "original_rng": left["state"]["rng"],
                        "rebuild_rng": right["state"]["rng"],
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
                    transient_divergences += 1
                    journal.write(json.dumps({
                        "resynchronized_after_frame":
                            pending_divergence["frame"],
                        "at_frame": frame,
                    }, separators=(",", ":")) + "\n")
                    journal.flush()
                    pending_divergence = None

                if last_pair is not None and frame != last_pair + 1:
                    pair_gaps.append([last_pair, frame])
                exact_pairs += 1
                first_pair = frame if first_pair is None else first_pair
                last_pair = frame
                previous_exact = {"original": left, "rebuild": right}
                update_coverage(left)

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
