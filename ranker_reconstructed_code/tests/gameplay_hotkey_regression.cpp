#include "ranker_cursor.h"
#include "ranker_gameplay_input_actions.h"
#include "ranker_input.h"
#include "ranker_screenshot.h"
#include "ranker_ui_overlay.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <atomic>
#include <cassert>
#include <thread>

namespace ranker {

// ranker_input.cpp is linked so the modifier/held-key transition is exercised
// directly. These unrelated mouse/cursor backends are not part of this test.
GameplayInputActionState& gameplay_input_action_state() {
    static GameplayInputActionState state{};
    return state;
}
void ResetGameplayInputSnapshotRing(GameplayInputActionState&) {}
bool PopGameplayInputSnapshot(GameplayInputActionState&) { return true; }
bool PushGameplayInputSnapshot(GameplayInputActionState&) { return true; }
bool PushGameplayInputSnapshot(
    GameplayInputActionState&, const GameplayInputSnapshot&) { return true; }
SoftwareCursorState& software_cursor_state() {
    static SoftwareCursorState state{};
    return state;
}
void SetGameCursorPointerPosition(i32, i32) {}
void RequestScreenshotCapture() {}
void SetContinuousScreenshotCapture(bool) {}

} // namespace ranker

namespace {

using namespace ranker;

void test_scan_code_and_modifier_pipeline() {
    assert(ResolveLegacyKeyboardScanCode(0x57u, 0x00110000u) == 0x11u); // W
    assert(ResolveLegacyKeyboardScanCode(0x4au, 0x00240000u) == 0x24u); // J
    assert(ResolveLegacyKeyboardScanCode(0x4eu, 0x00310000u) == 0x31u); // N
    assert(ResolveLegacyKeyboardScanCode(0x59u, 0x00150000u) == 0x15u); // Y
    assert(ResolveLegacyKeyboardScanCode(0x5au, 0x002c0000u) == 0x2cu); // Z
    assert(ResolveLegacyKeyboardScanCode(0x70u, 0x003b0000u) == 0x3bu); // F1
    assert(ResolveLegacyKeyboardScanCode(0x0du, 0x001c0000u) == 0x1cu); // Enter
    assert(ResolveLegacyKeyboardScanCode(0x41u, 0) == 0u);

    input_state() = InputState{};
    HandleKeyDown(0x11u, 0x1du); // Ctrl
    assert(input_state().ctrl_down && input_state().key_down[0x11] == 1);
    assert(input_state().events[0].code == 0x1du);
    HandleKeyUp(0x11u);
    assert(!input_state().ctrl_down);

    HandleKeyDown(0x10u, 0x2au); // Shift
    assert(input_state().shift_down);
    HandleKeyUp(0x10u);
    assert(!input_state().shift_down);

    HandleKeyDown(0x12u, 0x38u); // Alt
    assert(input_state().alt_down);
    HandleKeyUp(0x12u);
    assert(!input_state().alt_down);

    HandleKeyDown(0x25u, 0x4bu); // VK_LEFT + physical scan
    assert(input_state().key_down[0x25] == 1);
    assert(input_state().events[3].code == 0x4bu);
    HandleKeyUp(0x25u);
    assert(input_state().key_down[0x25] == 0);
}

void test_focus_loss_releases_every_held_key_and_modifier() {
    input_state() = InputState{};
    HandleKeyDown(0x11u, 0x1du); // Ctrl
    HandleKeyDown(0x10u, 0x2au); // Shift
    HandleAltKeyPress(0x12u, 0x38u); // Alt
    HandleKeyDown(0x25u, 0x4bu); // Left
    HandleKeyDown(0x57u, 0x11u); // W
    assert(HasQueuedInputEvent());

    ClearInputHeldKeysForFocusLoss();

    for (u8 down : input_state().key_down) {
        assert(down == 0);
    }
    assert(!input_state().shift_down);
    assert(!input_state().ctrl_down);
    assert(!input_state().alt_down);
    // Losing focus releases held state; it does not silently discard the
    // already published input records which the worker still owns.
    assert(HasQueuedInputEvent());
}

void test_atomic_held_key_bytes_preserve_layout_and_value_semantics() {
    static_assert(sizeof(InputAtomicByte) == sizeof(u8));
    static_assert(alignof(InputAtomicByte) == alignof(u8));

    InputState source{};
    source.key_down[0x10] = 1;
    source.key_down[0x25] = 1;
    source.shift_down = true;
    source.ctrl_down = true;

    InputState copied = source;
    source.key_down.fill(0);
    source.shift_down = false;
    source.ctrl_down = false;
    assert(copied.key_down[0x10] == 1);
    assert(copied.key_down[0x25] == 1);
    assert(copied.shift_down);
    assert(copied.ctrl_down);

    InputState assigned{};
    assigned = copied;
    assert(assigned.key_down[0x10] == 1);
    assert(assigned.key_down[0x25] == 1);
    assert(assigned.shift_down);
    assert(assigned.ctrl_down);
}

void test_window_key_writes_are_safe_during_gameplay_live_reads() {
    input_state() = InputState{};
    constexpr u32 kTransitionCount = 100000u;
    std::atomic<bool> reader_ready{false};
    std::atomic<bool> producer_done{false};
    std::atomic<u32> read_count{0};

    std::thread reader([&]() {
        reader_ready.store(true, std::memory_order_release);
        while (!producer_done.load(std::memory_order_acquire)) {
            // These are independent live globals in the original too; do not
            // require a cross-field snapshot, only valid one-byte values.
            const u8 held = input_state().key_down[0x10];
            const u8 shift = input_state().shift_down;
            assert(held <= 1);
            assert(shift <= 1);
            read_count.fetch_add(1, std::memory_order_relaxed);
        }
    });

    while (!reader_ready.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    for (u32 transition = 0; transition < kTransitionCount; ++transition) {
        HandleKeyDown(0x10u, 0x2au);
        HandleKeyUp(0x10u);
        if ((transition & 0xffu) == 0) {
            std::this_thread::yield();
        }
    }
    producer_done.store(true, std::memory_order_release);
    reader.join();

    assert(read_count.load(std::memory_order_relaxed) != 0);
    assert(input_state().key_down[0x10] == 0);
    assert(!input_state().shift_down);
}

void test_reset_flush_preserves_the_producer_owned_cursor() {
    input_state() = InputState{};
    assert(PushKeyboardInputEvent(0x11u));
    assert(PushKeyboardInputEvent(0x22u));
    const u32 published_head =
        input_state().head.load(std::memory_order_acquire);
    assert(published_head == 2u);

    ResetInputState();

    assert(input_state().head.load(std::memory_order_relaxed) ==
        published_head);
    assert(input_state().tail.load(std::memory_order_relaxed) ==
        published_head);
    assert(!HasQueuedInputEvent());

    assert(PushKeyboardInputEvent(0x33u));
    InputEvent event{};
    assert(PopInputEvent(event));
    assert(event.code == 0x33u);
    assert(!HasQueuedInputEvent());
}

void test_window_producer_and_gameplay_consumer_preserve_ring_order() {
    input_state() = InputState{};
    constexpr u32 kEventCount = 20000u;
    std::atomic<bool> producer_done{false};

    std::thread producer([&]() {
        for (u32 value = 1; value <= kEventCount; ++value) {
            while (!PushKeyboardInputEvent(value)) {
                std::this_thread::yield();
            }
        }
        producer_done.store(true, std::memory_order_release);
    });

    u32 received = 0;
    while (received < kEventCount) {
        InputEvent event{};
        if (PopInputEvent(event)) {
            ++received;
            assert(event.kind == InputEventKind::keyboard);
            assert(event.code == received);
            continue;
        }
        if (producer_done.load(std::memory_order_acquire) &&
            !HasQueuedInputEvent()) {
            break;
        }
        std::this_thread::yield();
    }
    producer.join();
    assert(received == kEventCount);
    assert(!HasQueuedInputEvent());
}

void test_original_scan_marker_and_record_marker_pipeline() {
    assert(ResolveUiOverlayGameplayHotkeyMarker(0x02) == '1');
    assert(ResolveUiOverlayGameplayHotkeyMarker(0x0b) == '0');
    assert(ResolveUiOverlayGameplayHotkeyMarker(0x11) == 'W');
    assert(ResolveUiOverlayGameplayHotkeyMarker(0x24) == 'J');
    assert(ResolveUiOverlayGameplayHotkeyMarker(0x31) == 'N');
    assert(ResolveUiOverlayGameplayHotkeyMarker(0x15) == 'Y');
    assert(ResolveUiOverlayGameplayHotkeyMarker(0x2c) == 'Z');
    assert(ResolveUiOverlayGameplayHotkeyMarker(0x3b) == 0);
    assert(ResolveUiOverlayGameplayHotkeyMarker(0x59) == 0);

    UiOverlayDrawRecord record{};
    record.icon_marker = 0x12340057u;
    UiOverlayHotRegion region{};
    region.record = record;
    region.hotkey = ResolveUiOverlayRecordHotkey(record.icon_marker);
    assert(region.hotkey == 'W');
    assert(ResolveUiOverlayOffscreenRecordMarker(record.icon_marker) == 0);

    assert(!IsUiOverlayHotkeyActionBlocked(0, true));
    assert(IsUiOverlayHotkeyActionBlocked(0, false));
    assert(IsUiOverlayHotkeyActionBlocked(0x02, true));
    assert(IsUiOverlayHotkeyActionBlocked(0x04, true));
    assert(IsUiOverlayHotkeyActionBlocked(0x10, true));
    assert(IsUiOverlayHotkeyActionBlocked(0x20, true));
}

void test_keyboard_routes_and_wm_char_deduplication() {
    using Route = UiOverlayGameplayKeyboardRoute;

    for (u32 scan = 0; scan <= 0x5a; ++scan) {
        const bool expected =
            (0x10 <= scan && scan <= 0x19) ||
            (0x1e <= scan && scan <= 0x28) ||
            (0x2b <= scan && scan <= 0x35);
        assert(OriginalGameplayScanRoutesToCommandHotkey(scan) == expected);
    }
    assert(ResolveUiOverlayGameplayKeyboardRoute(0x11, 0, false) ==
        Route::command_hotkey); // W
    assert(ResolveUiOverlayGameplayKeyboardRoute(0x24, 0, false) ==
        Route::command_hotkey); // J, not keypad-minus speed
    assert(ResolveUiOverlayGameplayKeyboardRoute(0x31, 0, false) ==
        Route::command_hotkey); // N, not keypad-plus speed
    assert(ResolveUiOverlayGameplayKeyboardRoute(0x15, 0, false) ==
        Route::command_hotkey); // Y survives the old ASCII 0x59 bound
    assert(ResolveUiOverlayGameplayKeyboardRoute(0x2c, 0, false) ==
        Route::command_hotkey); // Z

    // These marker-table bytes exist, but the original top-level jump table
    // returns before FUN_004e77a5 for brackets and F12.
    assert(ResolveUiOverlayGameplayHotkeyMarker(0x1a) == '[');
    assert(ResolveUiOverlayGameplayHotkeyMarker(0x1b) == ']');
    assert(ResolveUiOverlayGameplayHotkeyMarker(0x58) == 0xffu);
    assert(ResolveUiOverlayGameplayCommandMarker(0x1a, 0, false) == 0);
    assert(ResolveUiOverlayGameplayCommandMarker(0x1b, 0, false) == 0);
    assert(ResolveUiOverlayGameplayCommandMarker(0x58, 0, false) == 0);
    assert(ResolveUiOverlayGameplayKeyboardRoute(0x1a, 0, false) == Route::none);
    assert(ResolveUiOverlayGameplayKeyboardRoute(0x1b, 0, false) == Route::none);
    assert(ResolveUiOverlayGameplayKeyboardRoute(0x58, 0, false) == Route::none);

    assert(ResolveUiOverlayGameplayKeyboardRoute(0, 'W', false) == Route::none);
    assert(ResolveUiOverlayGameplayCommandMarker(0, 'W', false) == 0);
    assert(ResolveUiOverlayGameplayKeyboardRoute(0x02, 0, false) ==
        Route::control_group);
    assert(ResolveUiOverlayGameplayKeyboardRoute(0x3b, 0, false) ==
        Route::camera_bookmark);
    assert(ResolveUiOverlayGameplayKeyboardRoute(0x57, 0, false) ==
        Route::toggle_overlay);
    assert(ResolveUiOverlayGameplayKeyboardRoute(0x44, 0, false) ==
        Route::pause_menu);
    assert(ResolveUiOverlayGameplayKeyboardRoute(1, 0, false) ==
        Route::cancel_mode);
    assert(ResolveUiOverlayGameplayKeyboardRoute(0x1c, 0, false) == Route::none);
    assert(ResolveUiOverlayGameplayKeyboardRoute(0, '\r', false) ==
        Route::begin_chat);
    assert(ResolveUiOverlayGameplayKeyboardRoute(0x11, 0, true) == Route::none);
    assert(ResolveUiOverlayGameplayKeyboardRoute(0, 'w', true) ==
        Route::chat_character);
    assert(ResolveUiOverlayGameplayKeyboardRoute(1, 0, true) == Route::none);
    assert(ResolveUiOverlayGameplayKeyboardRoute(0, 0x1b, true) ==
        Route::chat_escape);

    assert(IsOriginalControlGroupDoubleTap(1400u, 1000u));
    assert(!IsOriginalControlGroupDoubleTap(1401u, 1000u));
    assert(IsOriginalControlGroupDoubleTap(0x00000050u, 0xfffffff0u));
}

} // namespace

int main() {
    test_scan_code_and_modifier_pipeline();
    test_focus_loss_releases_every_held_key_and_modifier();
    test_atomic_held_key_bytes_preserve_layout_and_value_semantics();
    test_window_key_writes_are_safe_during_gameplay_live_reads();
    test_reset_flush_preserves_the_producer_owned_cursor();
    test_window_producer_and_gameplay_consumer_preserve_ring_order();
    test_original_scan_marker_and_record_marker_pipeline();
    test_keyboard_routes_and_wm_char_deduplication();
    return 0;
}
