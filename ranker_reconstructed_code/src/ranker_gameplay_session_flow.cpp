#include "ranker_gameplay_session_flow.h"

#include "ranker_input.h"
#include "ranker_miles.h"
#include "ranker_p2p_lobby.h"
#include "ranker_resource_store.h"
#include "ranker_runtime_resources.h"
#include "ranker_trc.h"

#include <algorithm>
#include <array>
#include <cstdio>

namespace ranker {
namespace {

constexpr char kJw202ArchiveName[] = "JW2_02.TRC";
constexpr u32 kInvalidLoadedResource = 0xffffffffu;

void call(GameplaySessionFlowState& state, GameplaySessionFlowCallback callback) {
    if (callback != nullptr) {
        callback(state);
    }
}

void reset_input(GameplaySessionFlowState& state) {
    if (state.callbacks.reset_input != nullptr) {
        state.callbacks.reset_input(state);
        return;
    }
    ResetInputState();
}

void cleanup_after_close(GameplaySessionFlowState& state,
    GameplaySessionFlowCallback callback) {
    if (callback != nullptr) {
        callback(state);
        return;
    }
    ResetInputState();
}

void release_loaded_resources(GameplaySessionFlowState& state) {
    if (state.callbacks.release_loaded_resources != nullptr) {
        state.callbacks.release_loaded_resources(state);
        return;
    }
    if (state.loaded_resource_base_owned &&
        state.loaded_resource_base != kInvalidLoadedResource) {
        ReleaseResourceEntriesFrom(state.loaded_resource_base);
    }
    state.loaded_resource_base = kInvalidLoadedResource;
    state.loaded_palette_slot = kInvalidLoadedResource;
    state.loaded_resource_base_owned = false;
}

template <std::size_t N>
void copy_result_text(std::array<char, N>& dest, const char* text) {
    std::snprintf(dest.data(), dest.size(), "%s", text != nullptr ? text : "");
}

void write_p2p_result_file(GameplaySessionFlowState& state, int result) {
    if (state.callbacks.write_p2p_result_file != nullptr) {
        state.callbacks.write_p2p_result_file(state, result);
        return;
    }

#ifdef _WIN32
    P2PGameResultFileInput input{};
    P2PNetworkLaunchParameters& parameters = p2p_network_launch_parameters();
    const char* player_name = parameters.player_name[0] != '\0' ?
        parameters.player_name.data() : "Player";

    input.client_player_name = player_name;
    input.version_packed = LoadTrcRecord9Value();
    input.local_player_slot = 0;
    input.active_player_count = 1;
    input.connected_player_count = 1;
    copy_result_text(input.players[0].result_name, player_name);
    copy_result_text(input.players[0].network_name, player_name);
    input.players[0].selected_tribe = 0;
    input.players[0].faction = 0;
    input.players[0].ally_mask = 1;
    input.players[0].playing = result == 0;

    P2PGameEndReason end_reason = P2PGameEndReason::NoGameplayAbort;
    if (result == 0) {
        end_reason = P2PGameEndReason::NoError;
    }
    else if (result >= 0 &&
        result <= static_cast<int>(P2PGameEndReason::NetworkError)) {
        end_reason = static_cast<P2PGameEndReason>(result);
    }

    const P2PGameWinResult win_result = result == 0 ?
        static_cast<P2PGameWinResult>(state.p2p_win_result) :
        P2PGameWinResult::Draw;
    WriteP2PGameResultFile(input, win_result, end_reason);
#else
    (void)result;
#endif
}

void send_worker_modal(GameplaySessionFlowState& state,
    GameplaySessionFlowCallback callback, GameplayModalResult fallback_result) {
    if (callback != nullptr) {
        callback(state);
        return;
    }
    state.modal_result = fallback_result;
    state.worker_modal_pending = 0;
}

int call_int(GameplaySessionFlowState& state, GameplaySessionFlowIntCallback callback,
    int fallback) {
    return callback != nullptr ? callback(state) : fallback;
}

void dispatch(GameplaySessionFlowState& state, GameplaySessionFlowDispatchCallback callback,
    int result) {
    if (callback != nullptr) {
        callback(state, result);
    }
}

void set_primary_music_policy_mode(GameplaySessionFlowState& state, u32 mode) {
    if (state.callbacks.set_primary_music_policy_mode != nullptr) {
        state.callbacks.set_primary_music_policy_mode(state, mode);
        return;
    }
    SetPrimaryMilesMusicPolicyMode(mode);
}

void shutdown_primary_music_policy(GameplaySessionFlowState& state) {
    if (state.callbacks.shutdown_primary_music_policy != nullptr) {
        state.callbacks.shutdown_primary_music_policy(state);
        return;
    }
    ShutdownPrimaryMilesMusicPolicy();
}

u32 read_le_u32(const std::vector<u8>& data, std::size_t offset) {
    if (offset + 4 > data.size()) {
        return 0;
    }
    return static_cast<u32>(data[offset]) |
        (static_cast<u32>(data[offset + 1]) << 8) |
        (static_cast<u32>(data[offset + 2]) << 16) |
        (static_cast<u32>(data[offset + 3]) << 24);
}

bool load_save_record(GameplaySessionFlowState& state, const char* path,
    u32 record_index, std::vector<u8>& out) {
    out.clear();
    if (state.callbacks.load_save_record != nullptr) {
        return state.callbacks.load_save_record(path, record_index, out);
    }
    return LoadTrcRecordAlloc(path, record_index, out);
}

std::string title_from_record(const std::vector<u8>& data) {
    const char* bytes = reinterpret_cast<const char*>(data.data());
    std::size_t count = 0;
    while (count < data.size() && bytes[count] != '\0') {
        ++count;
    }
    return std::string(bytes, bytes + count);
}

void wait_for_worker_modal(GameplaySessionFlowState& state,
    GameplaySessionFlowCallback send_modal, GameplayModalResult fallback_result) {
    state.generic_ai_profile_mode = 1;
    call(state, state.callbacks.hide_cursor);
    state.worker_modal_pending = 1;
    call(state, state.callbacks.frame_boundary);
    send_worker_modal(state, send_modal, fallback_result);
    while (state.worker_modal_pending != 0) {
        if (state.callbacks.frame_boundary == nullptr) {
            break;
        }
        ++state.worker_modal_wait_iterations;
        call(state, state.callbacks.frame_boundary);
    }
    call(state, state.callbacks.frame_boundary);
}

bool configure_and_import_session(GameplaySessionFlowState& state) {
    call(state, state.callbacks.configure_display);
    call(state, state.callbacks.set_cursor);
    if (state.callbacks.import_session_bundle == nullptr) {
        return true;
    }
    return state.callbacks.import_session_bundle(state);
}

void run_imported_gameplay_session(GameplaySessionFlowState& state) {
    call(state, state.callbacks.start_session_from_slots);
    call(state, state.callbacks.pre_session_runtime);
    call(state, state.callbacks.post_session_runtime);
    call(state, state.callbacks.set_cursor);
    call(state, state.callbacks.show_cursor);
    call(state, state.callbacks.enter_session_ui);
    reset_input(state);
    state.setup_mode_flag = 0;
    call(state, state.callbacks.process_session_loop);
}

void finish_p2p_modal_result(GameplaySessionFlowState& state) {
    if (state.modal_result == GameplayModalResult::Cancel) {
        return;
    }
    if (!configure_and_import_session(state)) {
        write_p2p_result_file(state, 3);
        return;
    }
    run_imported_gameplay_session(state);
    write_p2p_result_file(state, 0);
}

void build_menu_from_templates(GameplayMenuControl& menu,
    const std::vector<GameplayMenuItemRectTemplate>& templates, i32 anchor_x,
    i32 anchor_y, u8 normal_color, u8 hover_color, u8 disabled_color) {
    const std::size_t count = std::min(menu.items.size(), templates.size());
    for (std::size_t i = 0; i < count; ++i) {
        GameplayMenuItem& item = menu.items[i];
        const GameplayMenuItemRectTemplate& rect = templates[i];
        item.left = anchor_x + rect.left;
        item.top = anchor_y + rect.top;
        item.right = anchor_x + rect.right;
        item.bottom = anchor_y + rect.bottom;
    }
    menu.normal_color = normal_color;
    menu.hover_color = hover_color;
    menu.disabled_color = disabled_color;
}

void patch_menu_item_sprites(GameplayMenuControl& menu, std::size_t index,
    u32 normal_entry, u32 alternate_entry) {
    if (index >= menu.items.size()) {
        return;
    }
    GameplayMenuItem& item = menu.items[index];
    item.shadow_sprite_entry = normal_entry;
    item.alternate_shadow_sprite_entry = alternate_entry;
}

i32 signed_resource_metadata(u32 value) {
    return static_cast<i32>(value);
}

} // namespace

void RunP2PGameplayFlow(GameplaySessionFlowState& state) {
    while (state.p2p_ready != 1) {
        reset_input(state);
        call(state, state.callbacks.set_cursor);
        const int result = call_int(state, state.callbacks.run_p2p_setup_menu, 5);
        if (result == 5) {
            call(state, state.callbacks.frame_boundary);
            return;
        }
        dispatch(state, state.callbacks.dispatch_p2p_setup_result, result);
    }
    wait_for_worker_modal(state, state.callbacks.send_p2p_game_flow_modal,
        GameplayModalResult::ContinueNetwork);
    finish_p2p_modal_result(state);
}

void RunLocalGameSetupLoop(GameplaySessionFlowState& state) {
    for (;;) {
        state.generic_ai_profile_mode = 0;
        state.setup_mode_flag = 1;
        state.local_setup_flag = 0;
        const int result = call_int(state, state.callbacks.run_local_setup_menu, 4);
        if (result == 4) {
            return;
        }
        dispatch(state, state.callbacks.dispatch_local_setup_result, result);
        set_primary_music_policy_mode(state, 2);
    }
}

void RunSinglePlayerGameplayFlow(GameplaySessionFlowState& state) {
    do {
        set_primary_music_policy_mode(state, 1);
        dispatch(state, state.callbacks.dispatch_local_setup_result, 1);
        state.generic_ai_profile_mode = 1;
        const GameplayModalResult result = ShowWorkerModalPauseDialog(state);
        if (result == GameplayModalResult::Cancel) {
            call(state, state.callbacks.set_cursor);
            call(state, state.callbacks.show_cursor);
            state.generic_ai_profile_mode = 0;
            state.setup_mode_flag = 1;
            state.local_setup_flag = 0;
            dispatch(state, state.callbacks.dispatch_local_setup_result, 2);
            set_primary_music_policy_mode(state, 2);
            return;
        }
        if (!configure_and_import_session(state)) {
            continue;
        }
        run_imported_gameplay_session(state);
    } while (!state.close_requested);

    shutdown_primary_music_policy(state);
    cleanup_after_close(state, state.callbacks.cleanup_after_close);
    call(state, state.callbacks.send_main_close);
    cleanup_after_close(state, state.callbacks.final_worker_cleanup);
}

void RunP2PGameplaySessionAfterModal(GameplaySessionFlowState& state) {
    wait_for_worker_modal(state, state.callbacks.send_p2p_game_flow_modal,
        GameplayModalResult::ContinueNetwork);
    finish_p2p_modal_result(state);
}

GameplayModalResult ShowWorkerModalPauseDialog(GameplaySessionFlowState& state) {
    wait_for_worker_modal(state, state.callbacks.send_worker_pause_modal,
        GameplayModalResult::ContinueLocal);
    return state.modal_result;
}

void ScanGameplaySaveSlotHeaders(GameplaySessionFlowState& state) {
    for (GameplaySaveSlotRecord& slot : state.save_slots) {
        std::vector<u8> header;
        if (!load_save_record(state, slot.path.c_str(), 0, header)) {
            slot.status = GameplaySaveSlotStatus::Empty;
            slot.title.clear();
            continue;
        }
        if (read_le_u32(header, 0) == kGameplaySaveMagicJwar &&
            read_le_u32(header, 4) == kGameplaySaveVersion) {
            std::vector<u8> title;
            if (load_save_record(state, slot.path.c_str(), 1, title)) {
                slot.status = GameplaySaveSlotStatus::Valid;
                slot.title = title_from_record(title);
                continue;
            }
        }
        slot.status = GameplaySaveSlotStatus::Invalid;
        slot.title.clear();
    }
}

int ShowGameplaySaveSlotActionMenu(GameplaySessionFlowState& state, u32 selected_slot) {
    state.selected_save_slot = selected_slot;
    call(state, state.callbacks.set_cursor);
    if (LoadJw202PaletteBoundResourceSequence(state, 8, 0x34) ==
        kInvalidLoadedResource) {
        return -1;
    }
    PrepareGameplaySaveSlotMenuLayout(state);
    build_menu_from_templates(state.save_menu, state.menu_rect_templates,
        state.menu_anchor_x, state.menu_anchor_y, 1, 0x41, 1);
    const u32 resource_base = state.loaded_resource_base;
    patch_menu_item_sprites(state.save_menu, 0, resource_base, resource_base);
    patch_menu_item_sprites(state.save_menu, 1,
        resource_base + 3 + selected_slot, resource_base + 3 + selected_slot);
    patch_menu_item_sprites(state.save_menu, 2, resource_base + 1, resource_base + 2);
    state.immediate_dialog_sound_selector = selected_slot & 1u;
    state.immediate_dialog_sound_world_delta = 0;
    call(state, state.callbacks.immediate_dialog_sound);
    RunGameplayMenuModalLoop(state.save_menu);
    release_loaded_resources(state);
    return static_cast<int>(state.save_menu.active_index);
}

int ShowGameplaySaveSlotListMenu(GameplaySessionFlowState& state) {
    call(state, state.callbacks.set_cursor);
    if (LoadJw202PaletteBoundResourceSequence(state, 8, 0x34) ==
        kInvalidLoadedResource) {
        return -1;
    }
    PrepareGameplaySaveSlotMenuLayout(state);
    build_menu_from_templates(state.save_menu, state.menu_rect_templates,
        state.menu_anchor_x, state.menu_anchor_y, 1, 1, 1);
    const u32 resource_base = state.loaded_resource_base;
    patch_menu_item_sprites(state.save_menu, 0, resource_base, resource_base);
    if (state.save_menu.items.size() > 1) {
        GameplayMenuItem& text_item = state.save_menu.items[1];
        text_item.text = state.save_slot_list_text;
        text_item.committed_text = state.save_slot_list_text;
    }
    patch_menu_item_sprites(state.save_menu, 2, resource_base + 1, resource_base + 2);
    RunGameplayMenuModalLoop(state.save_menu);
    if (state.save_menu.items.size() > 1) {
        state.save_slot_list_text = state.save_menu.items[1].text;
    }
    release_loaded_resources(state);
    return static_cast<int>(state.save_menu.active_index);
}

u32 LoadJw202ResourceSequenceFromCurrentBase(GameplaySessionFlowState& state,
    u32 record_count, u32 first_record) {
    const u32 resource_start = resource_store_state().next_entry;
    state.loaded_resource_base = resource_start;
    state.loaded_resource_base_owned = false;
    if (state.callbacks.load_resource_sequence != nullptr) {
        u32 first_resource = resource_start;
        const bool loaded = state.callbacks.load_resource_sequence(
            state, record_count, first_record, first_resource);
        if (!loaded) {
            state.loaded_resource_base = kInvalidLoadedResource;
            return kInvalidLoadedResource;
        }
        state.loaded_resource_base = first_resource;
        state.loaded_resource_base_owned = first_resource != kInvalidLoadedResource;
        return first_resource;
    }
    for (u32 i = 0; i < record_count; ++i) {
        if (LoadResourceTrcRecord(kJw202ArchiveName, first_record + i) ==
            kInvalidResourceEntry) {
            ReleaseResourceEntriesFrom(resource_start);
            state.loaded_resource_base = kInvalidLoadedResource;
            return kInvalidLoadedResource;
        }
    }
    state.loaded_resource_base_owned = record_count != 0;
    return resource_start;
}

u32 LoadJw202PaletteBoundResourceSequence(GameplaySessionFlowState& state,
    u32 record_count, u32 first_record) {
    const u32 resource_rewind = resource_store_state().next_entry;
    state.loaded_resource_base = kInvalidLoadedResource;
    state.loaded_palette_slot = kInvalidLoadedResource;
    state.loaded_resource_base_owned = false;
    if (state.callbacks.load_palette_resource_sequence != nullptr) {
        u32 first_resource = resource_rewind;
        u32 palette_slot = kInvalidLoadedResource;
        const bool loaded = state.callbacks.load_palette_resource_sequence(
            state, record_count, first_record, first_resource, palette_slot);
        if (!loaded) {
            state.loaded_resource_base = kInvalidLoadedResource;
            state.loaded_palette_slot = kInvalidLoadedResource;
            return kInvalidLoadedResource;
        }
        state.loaded_resource_base = first_resource;
        state.loaded_palette_slot = palette_slot;
        state.loaded_resource_base_owned = first_resource != kInvalidLoadedResource;
        return first_resource;
    }

    PaletteResourceSequenceResult sequence{};
    if (!LoadPaletteBoundResourceSequence(kJw202ArchiveName, first_record,
            record_count, &sequence)) {
        ReleaseResourceEntriesFrom(resource_rewind);
        return kInvalidLoadedResource;
    }
    state.loaded_resource_base = sequence.resource_start;
    state.loaded_palette_slot = sequence.palette.slot;
    state.loaded_resource_base_owned = true;
    return sequence.resource_start;
}

void PrepareGameplaySaveSlotMenuLayout(GameplaySessionFlowState& state) {
    const ResourceStoreEntry* entry = GetResourceEntry(state.loaded_resource_base);
    if (entry == nullptr) {
        state.menu_anchor_x = static_cast<i32>(state.screen_width >> 1);
        state.menu_anchor_y = static_cast<i32>(state.screen_height >> 1);
        return;
    }

    state.menu_anchor_x = static_cast<i32>(state.screen_width >> 1) -
        static_cast<i32>(entry->metadata[0] >> 1) +
        signed_resource_metadata(entry->metadata[2]);
    state.menu_anchor_y = static_cast<i32>(state.screen_height >> 1) -
        static_cast<i32>(entry->metadata[1] >> 1) +
        signed_resource_metadata(entry->metadata[3]);
}

} // namespace ranker
