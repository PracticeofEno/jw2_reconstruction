"""Read-only, exact-frame meat-item parity probe for original/rebuild P2P.

The driver must put both peers in the same game and make a neutral monster drop
food.  For pickup/consumption coverage, move an eligible damaged unit onto the
food tile while this probe is running.  This script never writes process memory
or injects input.

Usage:
  python .tmp_meat_live_probe.py ORIGINAL_PID REBUILD_PID REBUILD_BASE LAYOUT_JSON \
      --neutral-slot 21 --collector-slot 53 --timeout 120 \
      --jsonl .tmp_meat_live_trace.jsonl --summary .tmp_meat_live_summary.json
"""

import argparse
import ctypes
import hashlib
import json
import os
import struct
import sys
import time
from ctypes import wintypes


PROCESS_VM_READ = 0x0010
PROCESS_QUERY_INFORMATION = 0x0400
PROCESS_QUERY_LIMITED_INFORMATION = 0x1000

ORIGINAL_FRAME = 0x007071A4
ORIGINAL_RNG = (0x007071B8, 0x007071BC, 0x007071C0)
ORIGINAL_ACTIVE_UNIT_HEAD = 0x007071D4
ORIGINAL_LIFECYCLE_UNIT_HEAD = 0x007071DC
ORIGINAL_UNIT_POOL = 0x00A03FB8
ORIGINAL_UNIT_STRIDE = 0x1D0
ORIGINAL_UNIT_NEXT = 0x1CC

ORIGINAL_MAP_EFFECT_HEAD = 0x007071E0
ORIGINAL_MAP_EFFECT_POOL = 0x012CE970
ORIGINAL_MAP_EFFECT_STRIDE = 0x3C
ORIGINAL_MAP_EFFECT_LIMIT = 512

FOOD_EFFECT_IDS = {1, 2, 3, 4}
PAIR_HISTORY_LIMIT = 64
REQUIRED_IDENTICAL_CANDIDATES = 2

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
k32.OpenProcess.restype = wintypes.HANDLE
k32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
k32.ReadProcessMemory.restype = wintypes.BOOL
k32.ReadProcessMemory.argtypes = [
    wintypes.HANDLE,
    wintypes.LPCVOID,
    wintypes.LPVOID,
    ctypes.c_size_t,
    ctypes.POINTER(ctypes.c_size_t),
]
k32.CloseHandle.argtypes = [wintypes.HANDLE]
k32.QueryFullProcessImageNameW.restype = wintypes.BOOL
k32.QueryFullProcessImageNameW.argtypes = [
    wintypes.HANDLE,
    wintypes.DWORD,
    wintypes.LPWSTR,
    ctypes.POINTER(wintypes.DWORD),
]


def number(value):
    if isinstance(value, str):
        return int(value, 0)
    return int(value)


def u32(data, offset):
    return struct.unpack_from("<I", data, offset)[0]


def i32(data, offset):
    return struct.unpack_from("<i", data, offset)[0]


def u64(data, offset):
    return struct.unpack_from("<Q", data, offset)[0]


class Memory:
    def __init__(self, pid):
        self.pid = pid
        self.handle = k32.OpenProcess(
            PROCESS_VM_READ | PROCESS_QUERY_INFORMATION |
            PROCESS_QUERY_LIMITED_INFORMATION,
            False,
            pid,
        )
        if not self.handle:
            raise ctypes.WinError(ctypes.get_last_error())

    def close(self):
        if self.handle:
            k32.CloseHandle(self.handle)
            self.handle = None

    def read(self, address, size):
        buffer = (ctypes.c_ubyte * size)()
        transferred = ctypes.c_size_t()
        ok = k32.ReadProcessMemory(
            self.handle,
            ctypes.c_void_p(address),
            buffer,
            size,
            ctypes.byref(transferred),
        )
        if not ok or transferred.value != size:
            raise ctypes.WinError(ctypes.get_last_error())
        return bytes(buffer)

    def u32(self, address):
        return struct.unpack("<I", self.read(address, 4))[0]

    def image_path(self):
        capacity = wintypes.DWORD(32768)
        buffer = ctypes.create_unicode_buffer(capacity.value)
        if not k32.QueryFullProcessImageNameW(
                self.handle, 0, buffer, ctypes.byref(capacity)):
            raise ctypes.WinError(ctypes.get_last_error())
        return buffer.value


def file_sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        while True:
            chunk = stream.read(1024 * 1024)
            if not chunk:
                break
            digest.update(chunk)
    return digest.hexdigest().upper()


def read_vector(mem, address, stride, limit):
    begin, end, capacity = struct.unpack("<QQQ", mem.read(address, 24))
    if (begin == 0 or end < begin or capacity < end or
            (end - begin) % stride != 0):
        return [], [begin, end, capacity]
    count = (end - begin) // stride
    if count > limit:
        raise RuntimeError(
            f"implausible vector at 0x{address:x}: count={count}, limit={limit}")
    if count == 0:
        return [], [begin, end, capacity]
    raw = mem.read(begin, count * stride)
    return [raw[index * stride:(index + 1) * stride]
            for index in range(count)], [begin, end, capacity]


def original_units(mem):
    rows = {}
    for head, list_name in (
            (ORIGINAL_ACTIVE_UNIT_HEAD, "active"),
            (ORIGINAL_LIFECYCLE_UNIT_HEAD, "lifecycle")):
        offset = mem.u32(head)
        seen = set()
        while offset and offset not in seen and len(seen) < 1024:
            if offset % ORIGINAL_UNIT_STRIDE != 0:
                raise RuntimeError(
                    f"unaligned original unit offset 0x{offset:x}")
            seen.add(offset)
            data = mem.read(ORIGINAL_UNIT_POOL + offset, ORIGINAL_UNIT_STRIDE)
            slot = offset // ORIGINAL_UNIT_STRIDE
            rows[slot] = {
                "slot": slot,
                "list": list_name,
                "pointer": ORIGINAL_UNIT_POOL + offset,
                "type": u32(data, 0x00),
                "owner": u32(data, 0x04),
                "area_marker_flags": u32(data, 0x0C),
                "type_flags": u32(data, 0x58),
                "max_health": u32(data, 0x10),
                "health": u32(data, 0x18),
                "action_mode": u32(data, 0x2C),
                "cargo": u32(data, 0x4C),
                "command_state": u32(data, 0x60),
                "command_flags": u32(data, 0x9C),
                "runtime_flags": u32(data, 0xA0),
                "x": i32(data, 0xB8),
                "y": i32(data, 0xBC),
            }
            offset = u32(data, ORIGINAL_UNIT_NEXT)
    return rows


def original_food_effects(mem):
    rows = []
    offset = mem.u32(ORIGINAL_MAP_EFFECT_HEAD)
    seen = set()
    while offset and offset not in seen and len(seen) < ORIGINAL_MAP_EFFECT_LIMIT:
        if (offset % ORIGINAL_MAP_EFFECT_STRIDE != 0 or
                offset // ORIGINAL_MAP_EFFECT_STRIDE >= ORIGINAL_MAP_EFFECT_LIMIT):
            raise RuntimeError(
                f"invalid original map-effect offset 0x{offset:x}")
        seen.add(offset)
        data = mem.read(
            ORIGINAL_MAP_EFFECT_POOL + offset, ORIGINAL_MAP_EFFECT_STRIDE)
        effect_id = u32(data, 0x00)
        if effect_id in FOOD_EFFECT_IDS:
            linked_offset = u32(data, 0x10)
            linked_slot = None
            if linked_offset and linked_offset % ORIGINAL_UNIT_STRIDE == 0:
                linked_slot = linked_offset // ORIGINAL_UNIT_STRIDE
            rows.append({
                "slot": offset // ORIGINAL_MAP_EFFECT_STRIDE,
                "effect_id": effect_id,
                "flags": u32(data, 0x0C),
                "linked_slot": linked_slot,
                "x": i32(data, 0x24),
                "y": i32(data, 0x28),
                "frame_timer": u32(data, 0x2C),
                "repeat_count": u32(data, 0x30),
            })
        offset = u32(data, 0x38)
    return rows


class RebuildReader:
    def __init__(self, mem, image_base, layout):
        self.mem = mem
        self.base = image_base
        self.layout = layout
        self.runtime = image_base + number(layout["runtime_rva"])
        self.random = self.runtime + number(layout["frame_random_offset"])
        self.loop = image_base + number(layout["loop_rva"])
        self.movement = self.runtime + number(layout["movement_offset"])
        self.map_effect = self.runtime + number(
            layout["map_effect_context_offset"])
        self.unit = {key: number(value)
                     for key, value in layout["unit_layout"].items()}
        self.movement_layout = {
            key: number(value)
            for key, value in layout["movement_context_layout"].items()
        }
        self.effect = {key: number(value)
                       for key, value in layout["map_effect_layout"].items()}
        self.loop_layout = {key: number(value)
                            for key, value in layout["loop_layout"].items()}

    def frame(self):
        return self.mem.u32(
            self.loop + self.loop_layout["simulation_frame"])

    def rng(self):
        return [self.mem.u32(self.random + offset) for offset in (0, 4, 8)]

    def units(self):
        pointers = []
        list_by_pointer = {}
        for key, list_name in (("active_units", "active"),
                               ("lifecycle_units", "lifecycle")):
            raw, _ = read_vector(
                self.mem,
                self.movement + self.movement_layout[key],
                8,
                4096,
            )
            for entry in raw:
                pointer = struct.unpack("<Q", entry)[0]
                if pointer and pointer not in list_by_pointer:
                    pointers.append(pointer)
                    list_by_pointer[pointer] = list_name

        rows = {}
        size = self.unit["size"]
        definition = self.unit["definition"]
        for pointer in pointers:
            data = self.mem.read(pointer, size)
            slot = u32(data, self.unit["runtime_slot"])
            rows[slot] = {
                "slot": slot,
                "list": list_by_pointer[pointer],
                "pointer": pointer,
                "type": u32(data, self.unit["type"]),
                "owner": u32(data, self.unit["owner"]),
                "area_marker_flags": u32(data, self.unit["owner"] - 4),
                "type_flags": u32(data, self.unit["type_flags"]),
                "max_health": u32(data, self.unit["max_health"]),
                "health": u32(data, self.unit["health"]),
                "action_mode": u32(data, self.unit["action_mode"]),
                "cargo": u32(data, self.unit["cargo"]),
                "command_state": u32(data, self.unit["command_state"]),
                "command_flags": u32(data, self.unit["command_flags"]),
                "runtime_flags": u32(data, self.unit["runtime_flags"]),
                "x": i32(data, self.unit["x"]),
                "y": i32(data, self.unit["y"]),
                "lifecycle_class": u32(
                    data,
                    definition + self.unit["definition_lifecycle_class"],
                ),
                "passive_recovery_enabled": u32(
                    data,
                    definition +
                    self.unit["definition_passive_recovery_enabled"],
                ),
                "passive_recovery_flags": u32(
                    data,
                    definition + self.unit["definition_passive_recovery_flags"],
                ),
                "passive_map_effect_seed": u32(
                    data,
                    definition +
                    self.unit["definition_passive_map_effect_seed"],
                ),
            }
        return rows

    def food_effects(self, units):
        raw_instances, _ = read_vector(
            self.mem,
            self.map_effect + self.effect["effects"],
            self.effect["instance_size"],
            4096,
        )
        raw_indices, _ = read_vector(
            self.mem,
            self.map_effect + self.effect["active_indices"],
            4,
            4096,
        )
        active_indices = [struct.unpack("<I", row)[0]
                          for row in raw_indices]
        pointer_to_slot = {row["pointer"]: slot for slot, row in units.items()}
        rows = []
        for index in active_indices:
            if index >= len(raw_instances):
                raise RuntimeError(
                    f"active map-effect index {index} >= {len(raw_instances)}")
            data = raw_instances[index]
            if not data[self.effect["instance_active"]]:
                continue
            effect_id = u32(data, self.effect["instance_effect_id"])
            if effect_id not in FOOD_EFFECT_IDS:
                continue
            linked = u64(data, self.effect["instance_linked_unit"])
            rows.append({
                "slot": u32(data, self.effect["instance_id"]),
                "effect_id": effect_id,
                "flags": u32(data, self.effect["instance_flags"]),
                "linked_slot": pointer_to_slot.get(linked),
                "x": i32(data, self.effect["instance_x"]),
                "y": i32(data, self.effect["instance_y"]),
                "frame_timer": u32(
                    data, self.effect["instance_frame_timer"]),
                "repeat_count": u32(
                    data, self.effect["instance_repeat_count"]),
            })
        return rows


def meat_unit_projection(row):
    if row is None:
        return None
    return {
        key: row[key]
        for key in (
            "type", "owner", "max_health", "health",
            "action_mode", "cargo", "x", "y")
    }


def explicit_meat_track_slots(collector_slots, neutral_slot):
    slots = set(collector_slots)
    if neutral_slot is not None:
        slots.add(neutral_slot)
    return slots


def effect_projection(row):
    return tuple(row[key] for key in (
        "slot", "effect_id", "flags", "linked_slot", "x", "y",
        "frame_timer", "repeat_count"))


def snapshots_diff(previous, current, tracked_slots=None):
    previous_effects = {row["slot"]: row for row in previous["effects"]}
    current_effects = {row["slot"]: row for row in current["effects"]}
    spawned = [current_effects[slot]
               for slot in current_effects.keys() - previous_effects.keys()]
    removed = [previous_effects[slot]
               for slot in previous_effects.keys() - current_effects.keys()]

    unit_changes = []
    consumed = []
    changed_slots = previous["units"].keys() & current["units"].keys()
    if tracked_slots is not None:
        changed_slots &= set(tracked_slots)
    for slot in changed_slots:
        before = previous["units"][slot]
        after = current["units"][slot]
        action_delta = after["action_mode"] - before["action_mode"]
        cargo_delta = after["cargo"] - before["cargo"]
        health_delta = after["health"] - before["health"]
        if action_delta or cargo_delta or health_delta:
            change = {
                "slot": slot,
                "owner": after["owner"],
                "type": after["type"],
                "position": [after["x"], after["y"]],
                "action_before": before["action_mode"],
                "action_after": after["action_mode"],
                "action_delta": action_delta,
                "cargo_before": before["cargo"],
                "cargo_after": after["cargo"],
                "cargo_delta": cargo_delta,
                "health_before": before["health"],
                "health_after": after["health"],
                "health_delta": health_delta,
            }
            unit_changes.append(change)
            if (action_delta < 0 and health_delta == -action_delta and
                    cargo_delta == 0):
                consumed.append(change)

    pickups = []
    contaminations = []
    if removed:
        for change in unit_changes:
            nearest = min(
                removed,
                key=lambda effect: max(
                    abs(change["position"][0] - effect["x"]),
                    abs(change["position"][1] - effect["y"]),
                ),
            )
            distance = max(
                abs(change["position"][0] - nearest["x"]),
                abs(change["position"][1] - nearest["y"]),
            )
            if distance <= 0x80:
                correlated = dict(change)
                correlated["nearest_effect_slot"] = nearest["slot"]
                correlated["nearest_effect_repeat"] = nearest["repeat_count"]
                correlated["effect_distance"] = distance
                correlated["action_plus_heal_delta"] = (
                    change["action_delta"] + max(change["health_delta"], 0))
                if (change["action_delta"] > 0 and
                        change["cargo_delta"] == 0):
                    pickups.append(correlated)
                if change["cargo_delta"] > 0:
                    contaminations.append(correlated)

    return {
        "spawned": sorted(spawned, key=lambda row: row["slot"]),
        "removed": sorted(removed, key=lambda row: row["slot"]),
        "pickups": sorted(pickups, key=lambda row: row["slot"]),
        "consumed": sorted(consumed, key=lambda row: row["slot"]),
        "cargo_contaminations": sorted(
            contaminations, key=lambda row: row["slot"]),
    }


def event_projection(event):
    return {
        "spawned": [effect_projection(row) for row in event["spawned"]],
        "removed": [effect_projection(row) for row in event["removed"]],
        "pickups": [(row["slot"], row["action_delta"],
                     row["cargo_delta"], row["health_delta"],
                     row["nearest_effect_slot"], row["effect_distance"])
                    for row in event["pickups"]],
        "consumed": [(row["slot"], row["action_delta"],
                      row["cargo_delta"], row["health_delta"])
                     for row in event["consumed"]],
        "cargo_contaminations": [
            (row["slot"], row["action_delta"], row["cargo_delta"],
             row["nearest_effect_slot"], row["effect_distance"])
            for row in event["cargo_contaminations"]
        ],
    }


def capture_stable_original(original, attempts=8):
    """Capture one original snapshot without crossing its frame boundary."""
    for _ in range(attempts):
        frame_before = original.u32(ORIGINAL_FRAME)
        rng = [original.u32(address) for address in ORIGINAL_RNG]
        unit_rows = original_units(original)
        effect_rows = original_food_effects(original)
        frame_after = original.u32(ORIGINAL_FRAME)
        if frame_before == frame_after:
            return {
                "frame": frame_before,
                "rng": rng,
                "units": unit_rows,
                "effects": effect_rows,
            }
    return None


def capture_stable_rebuild(rebuild, attempts=8):
    """Capture one rebuild snapshot without crossing its frame boundary."""
    for _ in range(attempts):
        frame_before = rebuild.frame()
        rng = rebuild.rng()
        unit_rows = rebuild.units()
        effect_rows = rebuild.food_effects(unit_rows)
        frame_after = rebuild.frame()
        if frame_before == frame_after:
            return {
                "frame": frame_before,
                "rng": rng,
                "units": unit_rows,
                "effects": effect_rows,
            }
    return None


def new_stable_pair_histories():
    return {
        side: {
            "last_seen": None,
            "candidate": None,
            "candidate_signature": None,
            "candidate_repeats": 0,
            "finalized": {},
        }
        for side in ("original", "rebuild")
    }


def observe_stable_candidate(history, snapshot):
    """Retain a repeated quiescent read and finalize it on the next frame.

    The simulation frame counter can advance before all of that frame's unit
    updates are visible.  A frame-N snapshot is therefore only eligible for a
    peer comparison after a stable frame-N+1 snapshot has been observed.
    The RNG tuple can settle before the last movement writes.  Require two
    byte-for-byte identical semantic reads of N so an intra-tick frontier is
    never promoted merely because the frame counter advanced immediately
    after it.
    """
    if snapshot is None:
        return
    frame = snapshot["frame"]
    last_seen = history["last_seen"]
    if last_seen != frame:
        candidate = history["candidate"]
        if candidate is not None and candidate["frame"] == last_seen:
            history["finalized"][last_seen] = candidate
        history["last_seen"] = frame
        history["candidate"] = None
        history["candidate_signature"] = None
        history["candidate_repeats"] = 0

    signature = json.dumps(
        [snapshot["rng"], snapshot["units"], snapshot["effects"]],
        ensure_ascii=False, sort_keys=True, separators=(",", ":"))
    if signature == history["candidate_signature"]:
        history["candidate_repeats"] += 1
    else:
        history["candidate_signature"] = signature
        history["candidate_repeats"] = 1
        history["candidate"] = None
    if history["candidate_repeats"] >= REQUIRED_IDENTICAL_CANDIDATES:
        history["candidate"] = snapshot


def bound_finalized_histories(histories):
    for side in ("original", "rebuild"):
        finalized = histories[side]["finalized"]
        if len(finalized) > PAIR_HISTORY_LIMIT:
            newest = sorted(finalized)[-PAIR_HISTORY_LIMIT:]
            histories[side]["finalized"] = {
                frame: finalized[frame] for frame in newest
            }


def capture_stable_pair(original, rebuild, histories, attempts=12):
    """Pair only finalized, independently stable same-frame snapshots.

    Reading both complete simulations inside one tick is too expensive on the
    live 66-unit map.  Each side keeps its latest candidate for frame N and
    finalizes it only after independently observing frame N+1.  The short
    finalized histories then tolerate peer wall-time skew without admitting a
    mid-tick candidate into a comparison.
    """
    for _ in range(attempts):
        original_snapshot = capture_stable_original(original, attempts=2)
        observe_stable_candidate(histories["original"], original_snapshot)

        rebuild_snapshot = capture_stable_rebuild(rebuild, attempts=2)
        observe_stable_candidate(histories["rebuild"], rebuild_snapshot)

        original_finalized = histories["original"]["finalized"]
        rebuild_finalized = histories["rebuild"]["finalized"]
        common = original_finalized.keys() & rebuild_finalized.keys()
        if common:
            frame = min(common)
            pair = {
                "frame": frame,
                "original": original_finalized[frame],
                "rebuild": rebuild_finalized[frame],
            }
            for side in ("original", "rebuild"):
                finalized = histories[side]["finalized"]
                histories[side]["finalized"] = {
                    key: value for key, value in finalized.items()
                    if key > frame
                }
            bound_finalized_histories(histories)
            return pair

        # Bound memory while retaining enough skew tolerance for GUI load.
        bound_finalized_histories(histories)
    return None


def emit_jsonl(stream, record):
    if stream is not None:
        stream.write(json.dumps(record, ensure_ascii=False,
                                separators=(",", ":")) + "\n")
        stream.flush()


def pair_phase_aligned(pair):
    """The frame counter alone is not a completed-tick phase barrier."""
    return pair["original"]["rng"] == pair["rebuild"]["rng"]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("original_pid", type=int)
    parser.add_argument("rebuild_pid", type=int)
    parser.add_argument("rebuild_base", type=lambda value: int(value, 0))
    parser.add_argument("layout_json")
    parser.add_argument("--neutral-slot", type=int)
    parser.add_argument("--collector-slot", type=int, action="append", default=[])
    parser.add_argument("--timeout", type=float, default=120.0)
    parser.add_argument("--interval", type=float, default=0.01)
    parser.add_argument("--jsonl")
    parser.add_argument("--summary")
    parser.add_argument("--allow-hash-mismatch", action="store_true")
    parser.add_argument("--strict-coverage", action="store_true")
    args = parser.parse_args()

    with open(args.layout_json, "r", encoding="utf-8-sig") as stream:
        layout = json.load(stream)

    original = Memory(args.original_pid)
    rebuild_memory = Memory(args.rebuild_pid)
    jsonl = (open(args.jsonl, "w", encoding="utf-8")
             if args.jsonl else None)
    try:
        rebuild_path = rebuild_memory.image_path()
        actual_hash = file_sha256(rebuild_path)
        expected_hash = str(layout.get("sha256", "")).upper()
        if (expected_hash and actual_hash != expected_hash and
                not args.allow_hash_mismatch):
            raise RuntimeError(
                "rebuild/layout hash mismatch: "
                f"process={actual_hash}, layout={expected_hash}")

        rebuild = RebuildReader(
            rebuild_memory, args.rebuild_base, layout)
        if rebuild_memory.read(args.rebuild_base, 2) != b"MZ":
            raise RuntimeError(
                f"0x{args.rebuild_base:x} is not the rebuild image base")

        started = time.monotonic()
        previous = None
        last_frame = None
        exact_samples = 0
        effect_mismatches = 0
        unit_mismatches = 0
        event_mismatches = 0
        phase_skew_samples = 0
        neutral_death_frame = None
        neutral_last_position = None
        coverage = {
            "spawn": False,
            "spawn_any": False,
            "pickup": False,
            "consume": False,
            "cargo_contamination": False,
            "neutral_transition": False,
        }
        events = []
        histories = new_stable_pair_histories()

        while time.monotonic() - started < args.timeout:
            pair = capture_stable_pair(original, rebuild, histories)
            if pair is None:
                time.sleep(args.interval)
                continue
            if not pair_phase_aligned(pair):
                phase_skew_samples += 1
                emit_jsonl(jsonl, {
                    "kind": "phase_skew",
                    "frame": pair["frame"],
                    "original_rng": pair["original"]["rng"],
                    "rebuild_rng": pair["rebuild"]["rng"],
                })
                continue
            frame = pair["frame"]
            if frame == last_frame:
                time.sleep(args.interval)
                continue
            last_frame = frame
            exact_samples += 1

            original_effect_projection = [
                effect_projection(row) for row in pair["original"]["effects"]]
            rebuild_effect_projection = [
                effect_projection(row) for row in pair["rebuild"]["effects"]]
            effect_equal = (original_effect_projection ==
                            rebuild_effect_projection)
            if not effect_equal:
                effect_mismatches += 1

            track_slots = explicit_meat_track_slots(
                args.collector_slot, args.neutral_slot)

            mismatched_slots = []
            for slot in sorted(track_slots):
                original_row = meat_unit_projection(
                    pair["original"]["units"].get(slot))
                rebuild_row = meat_unit_projection(
                    pair["rebuild"]["units"].get(slot))
                if original_row != rebuild_row:
                    mismatched_slots.append(slot)
            if mismatched_slots:
                unit_mismatches += 1

            if not effect_equal or mismatched_slots:
                emit_jsonl(jsonl, {
                    "kind": "parity_mismatch",
                    "frame": frame,
                    "food_effects_equal": effect_equal,
                    "original_effects": pair["original"]["effects"],
                    "rebuild_effects": pair["rebuild"]["effects"],
                    "unit_slots": mismatched_slots,
                    "units": {
                        str(slot): {
                            "original": pair["original"]["units"].get(slot),
                            "rebuild": pair["rebuild"]["units"].get(slot),
                        }
                        for slot in mismatched_slots
                    },
                })

            if previous is not None:
                neutral_died = False
                if args.neutral_slot is not None:
                    slot = args.neutral_slot
                    before_o = previous["original"]["units"].get(slot)
                    after_o = pair["original"]["units"].get(slot)
                    before_r = previous["rebuild"]["units"].get(slot)
                    after_r = pair["rebuild"]["units"].get(slot)

                    def died(before, after):
                        if before is None:
                            return False
                        if after is None:
                            return True
                        return ((before["list"] == "active" and
                                 after["list"] == "lifecycle") or
                                ((before["command_state"] & 0x10000000) == 0 and
                                 (after["command_state"] & 0x10000000) != 0))

                    neutral_died = died(before_o, after_o) and died(before_r, after_r)
                    if neutral_died:
                        coverage["neutral_transition"] = True
                        neutral_death_frame = frame
                        neutral_last_position = [before_o["x"], before_o["y"]]

                original_event = snapshots_diff(
                    previous["original"], pair["original"], track_slots)
                rebuild_event = snapshots_diff(
                    previous["rebuild"], pair["rebuild"], track_slots)
                projected_original = event_projection(original_event)
                projected_rebuild = event_projection(rebuild_event)
                has_event = any(projected_original[key]
                                for key in projected_original) or any(
                                    projected_rebuild[key]
                                    for key in projected_rebuild)
                if has_event:
                    event_equal = projected_original == projected_rebuild
                    if not event_equal:
                        event_mismatches += 1
                    record = {
                        "kind": "meat_event",
                        "frame": frame,
                        "previous_frame": previous["frame"],
                        "parity": event_equal,
                        "neutral_death_transition": neutral_died,
                        "original": original_event,
                        "rebuild": rebuild_event,
                    }
                    events.append(record)
                    emit_jsonl(jsonl, record)
                    if event_equal:
                        coverage["spawn_any"] |= bool(original_event["spawned"])
                        spawn_correlated = bool(original_event["spawned"])
                        if args.neutral_slot is not None:
                            spawn_correlated = (
                                neutral_death_frame is not None and
                                frame - neutral_death_frame <= 16 and
                                neutral_last_position is not None and
                                any(max(
                                    abs(effect["x"] - neutral_last_position[0]),
                                    abs(effect["y"] - neutral_last_position[1]),
                                ) <= 0x200
                                    for effect in original_event["spawned"]))
                        coverage["spawn"] |= spawn_correlated
                        coverage["pickup"] |= bool(original_event["removed"] and
                                                   original_event["pickups"])
                        coverage["consume"] |= bool(original_event["consumed"])
                    coverage["cargo_contamination"] |= bool(
                        original_event["cargo_contaminations"] or
                        rebuild_event["cargo_contaminations"])

            previous = pair
            if (coverage["spawn"] and coverage["pickup"] and
                    coverage["consume"]):
                break
            time.sleep(args.interval)

        summary = {
            "probe": "read-only exact-frame neutral meat generation/pickup/consume parity",
            "pids": {
                "original": args.original_pid,
                "rebuild": args.rebuild_pid,
            },
            "rebuild": {
                "path": rebuild_path,
                "base": f"0x{args.rebuild_base:x}",
                "process_sha256": actual_hash,
                "layout_sha256": expected_hash,
                "hash_match": not expected_hash or actual_hash == expected_hash,
            },
            "exact_frame_samples": exact_samples,
            "last_frame": last_frame,
            "coverage": coverage,
            "parity": {
                "food_effect_mismatch_samples": effect_mismatches,
                "tracked_unit_mismatch_samples": unit_mismatches,
                "event_mismatches": event_mismatches,
                "phase_skew_samples": phase_skew_samples,
            },
            "verdict": {
                "generation_matches": coverage["spawn"] and
                    effect_mismatches == 0 and event_mismatches == 0,
                "pickup_uses_action_not_cargo": coverage["pickup"] and
                    not coverage["cargo_contamination"] and
                    unit_mismatches == 0 and event_mismatches == 0,
                "consumption_decrements_action_and_heals": coverage["consume"] and
                    unit_mismatches == 0 and event_mismatches == 0,
                "full_coverage_pass": (
                    coverage["spawn"] and coverage["pickup"] and
                    coverage["consume"] and
                    not coverage["cargo_contamination"] and
                    effect_mismatches == 0 and unit_mismatches == 0 and
                    event_mismatches == 0),
            },
            "event_count": len(events),
        }
        if args.summary:
            with open(args.summary, "w", encoding="utf-8") as stream:
                json.dump(summary, stream, ensure_ascii=False, indent=2)
                stream.write("\n")
        print(json.dumps(summary, ensure_ascii=False, indent=2))
        if exact_samples == 0:
            return 2
        if args.strict_coverage and not summary["verdict"]["full_coverage_pass"]:
            return 3
        return 0
    finally:
        if jsonl is not None:
            jsonl.close()
        original.close()
        rebuild_memory.close()


if __name__ == "__main__":
    sys.exit(main())
