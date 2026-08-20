import ctypes
import json
import struct
import sys


command = sys.argv[1]
original_pid = int(sys.argv[2], 0)
rebuild_pid = int(sys.argv[3], 0)
rebuild_base = int(sys.argv[4], 0)
loop_rva = int(sys.argv[5], 0)

if command == "pace":
    layout_path_index = 8
elif command == "view-owner":
    layout_path_index = 7
else:
    layout_path_index = 6
with open(sys.argv[layout_path_index], "r", encoding="utf-8-sig") as stream:
    layout = json.load(stream)
loop_layout = {key: int(value, 0)
               for key, value in layout["loop_layout"].items()}
overlay_layout = {key: int(value, 0)
                  for key, value in layout["overlay_layout"].items()}
input_layout = {key: int(value, 0)
                for key, value in layout["input_layout"].items()}
player_slots_layout = {key: int(value, 0)
                       for key, value in layout["player_slots_layout"].items()}
gameplay_sound_layout = {key: int(value, 0)
                         for key, value in layout["gameplay_sound_layout"].items()}
movement_context_layout = {
    key: int(value, 0)
    for key, value in layout["movement_context_layout"].items()
}
unit_layout = {key: int(value, 0)
               for key, value in layout["unit_layout"].items()}

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
k32.OpenProcess.restype = ctypes.c_void_p
k32.VirtualAllocEx.restype = ctypes.c_void_p


class Process:
    def __init__(self, pid, access):
        self.handle = k32.OpenProcess(access, False, pid)
        if not self.handle:
            raise ctypes.WinError(ctypes.get_last_error())

    def read(self, address, size):
        data = ctypes.create_string_buffer(size)
        transferred = ctypes.c_size_t()
        if not k32.ReadProcessMemory(
                self.handle, ctypes.c_void_p(address), data, size,
                ctypes.byref(transferred)) or transferred.value != size:
            raise ctypes.WinError(ctypes.get_last_error())
        return data.raw

    def read_u32(self, address):
        return struct.unpack("<I", self.read(address, 4))[0]

    def read_u64(self, address):
        return struct.unpack("<Q", self.read(address, 8))[0]

    def write(self, address, data):
        transferred = ctypes.c_size_t()
        if not k32.WriteProcessMemory(
                self.handle, ctypes.c_void_p(address), data, len(data),
                ctypes.byref(transferred)) or transferred.value != len(data):
            raise ctypes.WinError(ctypes.get_last_error())

    def write_u32s(self, address, values):
        self.write(address, struct.pack("<" + "I" * len(values), *values))

    def write_u64s(self, address, values):
        self.write(address, struct.pack("<" + "Q" * len(values), *values))

    def allocate(self, size):
        address = k32.VirtualAllocEx(
            self.handle, None, size, 0x3000, 0x04)
        if not address:
            raise ctypes.WinError(ctypes.get_last_error())
        return address

    def close(self):
        if self.handle:
            k32.CloseHandle(self.handle)
            self.handle = None


original = Process(original_pid, 0x0438)
rebuild = Process(rebuild_pid, 0x0438)
loop = rebuild_base + loop_rva
try:
    if command == "pace":
        original_interval = int(sys.argv[6], 0)
        rebuild_interval = int(sys.argv[7], 0)
        # Replay speed 4 is the original's ordinary "Fastest" bucketed
        # mode.  Make this explicit after a fast-forward run so the requested
        # interval produces one comparable render opportunity per simulation
        # frame in both clients.
        original.write_u32s(0x0124072C, [4])
        original.write_u32s(0x00725B74, [original_interval] * 16)
        rebuild.write_u32s(loop + loop_layout["fixed_step_mode"], [4])
        rebuild.write_u32s(loop + loop_layout["frame_intervals"],
                           [rebuild_interval] * 16)
        rebuild.write_u32s(loop + loop_layout["fixed_step_intervals"],
                           [rebuild_interval] * 7)
    elif command == "fast":
        # UpdateGameplayFramePhaseFlags uses mode 0's fixed-step path to run
        # up to 200 simulation ticks before presenting.  The previous helper
        # left both clients in mode 4 and therefore rendered every skipped
        # frame, making distant replay checkpoints unnecessarily expensive.
        # Original globals are DAT_0124072c (mode), DAT_00725bc0 (seven fixed
        # intervals), and DAT_00725bdc (seven repeat limits).
        original.write_u32s(0x0124072C, [0])
        original.write_u32s(0x00725BC0, [1] * 7)
        original.write_u32s(0x00725BDC, [200])
        rebuild.write_u32s(loop + loop_layout["fixed_step_mode"], [0])
        rebuild.write_u32s(loop + loop_layout["fixed_step_intervals"], [1] * 7)
        rebuild.write_u32s(loop + loop_layout["fixed_step_repeat_counts"], [200])
    elif command == "stabilize":
        overlay = rebuild_base + int(layout["overlay_rva"], 0)
        input_state = rebuild_base + int(layout["input_state_rva"], 0)
        if len(sys.argv) == 9:
            camera = [int(sys.argv[7], 0), int(sys.argv[8], 0)]
        elif len(sys.argv) == 7:
            camera = [
                original.read_u32(0x007071A8),
                original.read_u32(0x007071AC),
            ]
        else:
            raise ValueError(
                "stabilize accepts either no camera override or CAMERA_X CAMERA_Y")
        screen_width = rebuild.read_u32(
            overlay + overlay_layout["screen_width"])
        screen_height = rebuild.read_u32(
            overlay + overlay_layout["screen_height"])
        if not 1 <= screen_width <= 16384 or not 1 <= screen_height <= 16384:
            raise ValueError(
                f"invalid rebuild viewport: {screen_width}x{screen_height}")
        mouse = [screen_width // 2, screen_height // 2]

        # Keep the original camera authoritative unless the caller requested a
        # diagnostic viewpoint. Centering both stored input positions prevents
        # wall-clock edge scrolling from moving either viewport while the frame
        # drivers advance independently.
        original.write_u32s(0x014594A8, mouse)
        original.write_u32s(0x007071A8, camera)
        rebuild.write_u32s(
            input_state + input_layout["mouse_x"], mouse)
        rebuild.write_u32s(
            overlay + overlay_layout["mouse_x"], mouse)
        rebuild.write_u32s(
            overlay + overlay_layout["camera_x"], camera)
        print(json.dumps({
            "camera": camera,
            "mouse": mouse,
            "viewport": [screen_width, screen_height],
        }))
        sys.exit(0)
    elif command == "view-owner":
        owner = int(sys.argv[6], 0)
        if not 0 <= owner < 8:
            raise ValueError(f"view owner must be in [0, 7], got {owner}")
        runtime = rebuild_base + int(layout["runtime_rva"], 0)
        player_slots = runtime + int(layout["player_slots_offset"], 0)

        # DAT_00725100 is the original's authoritative presentation owner.
        # The reconstructed frame synchronizes render/visibility consumers
        # from PlayerSlotRuntimeState::local_player_slot on the next tick.
        original.write_u32s(0x00725100, [owner])
        rebuild.write_u32s(
            player_slots + player_slots_layout["local_player"], [owner])
        print(json.dumps({
            "original": original.read_u32(0x00725100),
            "rebuild": rebuild.read_u32(
                player_slots + player_slots_layout["local_player"]),
        }))
        sys.exit(0)
    elif command == "presentation-rng":
        runtime = rebuild_base + int(layout["runtime_rva"], 0)
        gameplay_sound = runtime + int(layout["gameplay_sound_offset"], 0)
        seed = original.read_u32(0x007071C4)
        rebuild.write_u32s(
            gameplay_sound + gameplay_sound_layout["variant_seed"], [seed])
        print(json.dumps({
            "original": seed,
            "rebuild": rebuild.read_u32(
                gameplay_sound + gameplay_sound_layout["variant_seed"]),
        }))
        sys.exit(0)
    elif command == "unit-world-bar":
        slot = int(sys.argv[7], 0)
        enabled = int(sys.argv[8], 0) != 0
        if not 0 <= slot < 2048:
            raise ValueError(f"unit slot must be in [0, 2047], got {slot}")

        original_address = 0x00A03FB8 + slot * 0x1D0 + 0x08
        original_before = original.read_u32(original_address)
        original_after = ((original_before | 0x80) if enabled else
                          (original_before & ~0x80))
        original.write_u32s(original_address, [original_after])

        runtime = rebuild_base + int(layout["runtime_rva"], 0)
        movement = runtime + int(layout["movement_offset"], 0)
        rebuild_pointer = None
        for field in ("active_units", "lifecycle_units"):
            vector = movement + movement_context_layout[field]
            begin = rebuild.read_u64(vector)
            end = rebuild.read_u64(vector + 8)
            if end < begin or (end - begin) % 8 != 0:
                raise ValueError(f"invalid {field} vector bounds")
            count = (end - begin) // 8
            if count > 4096:
                raise ValueError(f"invalid {field} vector count: {count}")
            for index in range(count):
                pointer = rebuild.read_u64(begin + index * 8)
                if (pointer and rebuild.read_u32(
                        pointer + unit_layout["runtime_slot"]) == slot):
                    rebuild_pointer = pointer
                    break
            if rebuild_pointer is not None:
                break
        if rebuild_pointer is None:
            raise ValueError(f"rebuild unit slot {slot} is not live")

        rebuild_address = (rebuild_pointer +
                           unit_layout["scenario_string_slot"])
        rebuild_before = rebuild.read_u32(rebuild_address)
        rebuild_after = ((rebuild_before | 0x80) if enabled else
                         (rebuild_before & ~0x80))
        rebuild.write_u32s(rebuild_address, [rebuild_after])

        selected_ids_before = []
        selected_ids_after = []
        if enabled:
            # The reconstruction republishes raw +0x08 bit 0x80 from the
            # overlay selection vector before each render preparation.  Add
            # this runtime id as well as setting the raw bit so a diagnostic
            # selection survives until the ordinary death callback removes it.
            overlay = rebuild_base + int(layout["overlay_rva"], 0)
            selected_vector = (overlay +
                               overlay_layout["selected_unit_ids"])
            selected_begin = rebuild.read_u64(selected_vector)
            selected_end = rebuild.read_u64(selected_vector + 8)
            selected_capacity = rebuild.read_u64(selected_vector + 16)
            if (selected_end < selected_begin or
                    selected_capacity < selected_end or
                    (selected_end - selected_begin) % 4 != 0):
                raise ValueError("invalid selected-unit vector bounds")
            selected_count = (selected_end - selected_begin) // 4
            if selected_count > 64:
                raise ValueError(
                    f"invalid selected-unit vector count: {selected_count}")
            selected_ids_before = [
                rebuild.read_u32(selected_begin + index * 4)
                for index in range(selected_count)
            ]
            unit_id = rebuild.read_u32(
                rebuild_pointer + unit_layout["id"])
            if unit_id not in selected_ids_before:
                if not selected_begin or selected_end + 4 > selected_capacity:
                    new_capacity_count = max(16, selected_count + 1)
                    new_begin = rebuild.allocate(new_capacity_count * 4)
                    if selected_ids_before:
                        rebuild.write_u32s(new_begin, selected_ids_before)
                    selected_begin = new_begin
                    selected_end = new_begin + selected_count * 4
                    selected_capacity = new_begin + new_capacity_count * 4
                    rebuild.write_u64s(selected_vector, [
                        selected_begin, selected_end, selected_capacity])
                rebuild.write_u32s(selected_end, [unit_id])
                selected_end += 4
                rebuild.write_u64s(selected_vector + 8, [selected_end])
            selected_ids_after = [
                rebuild.read_u32(selected_begin + index * 4)
                for index in range((selected_end - selected_begin) // 4)
            ]
        print(json.dumps({
            "slot": slot,
            "enabled": enabled,
            "original": [original_before, original.read_u32(original_address)],
            "rebuild": [rebuild_before, rebuild.read_u32(rebuild_address)],
            "rebuild_selected_ids": [selected_ids_before, selected_ids_after],
            "original_local_owner": original.read_u32(0x00725100),
            "original_local_relation_mask": original.read_u32(
                0x00725334 + original.read_u32(0x00725100) * 4),
        }))
        sys.exit(0)
    elif command != "frames":
        raise ValueError(f"unknown command: {command}")
    print(json.dumps({
        "original": original.read_u32(0x007071A4),
        "rebuild": rebuild.read_u32(loop + loop_layout["simulation_frame"]),
    }))
finally:
    original.close()
    rebuild.close()
