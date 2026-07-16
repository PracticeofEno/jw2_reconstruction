#include "ranker_gameplay_input_actions.h"
#include "ranker_gameplay_packets.h"
#include "ranker_reliable_packets.h"

#ifdef _WIN32
#include "ranker_cursor.h"
#include "ranker_screenshot.h"
#endif

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <thread>

namespace ranker {

// The focused target links the production input translation units directly.
// These unrelated backends are referenced by sections outside the ring path
// on PE/COFF linkers, so provide inert test-local endpoints for them.
Mode1ReliableRuntimeState& mode1_reliable_state() {
    static Mode1ReliableRuntimeState state{};
    return state;
}

void ResetMode1GameplayVoteCompletionGate() {}

#ifdef _WIN32
SoftwareCursorState& software_cursor_state() {
    static SoftwareCursorState state{};
    return state;
}

void SetGameCursorPointerPosition(i32, i32) {}
void RequestScreenshotCapture() {}
void SetContinuousScreenshotCapture(bool) {}
#endif

} // namespace ranker

namespace {

using namespace ranker;

#define REQUIRE(condition)                                                     \
    do {                                                                       \
        if (!(condition)) {                                                    \
            std::cerr << "FAIL line " << __LINE__ << ": " #condition "\n"; \
            std::exit(1);                                                      \
        }                                                                      \
    } while (false)

GameplayInputSnapshot make_snapshot(u32 sequence) {
    GameplayInputSnapshot snapshot{};
    snapshot.field0 = sequence;
    snapshot.field1 = sequence ^ 0xa5a55a5au;
    snapshot.field2 = sequence * 3u;
    snapshot.field3 = ~sequence;
    snapshot.field4 = sequence + 0x10203040u;
    return snapshot;
}

bool snapshot_matches(
    const GameplayInputSnapshot& snapshot, u32 sequence) {
    const GameplayInputSnapshot expected = make_snapshot(sequence);
    return snapshot.field0 == expected.field0 &&
        snapshot.field1 == expected.field1 &&
        snapshot.field2 == expected.field2 &&
        snapshot.field3 == expected.field3 &&
        snapshot.field4 == expected.field4;
}

void test_threaded_spsc_publication_preserves_every_snapshot() {
    GameplayInputActionState state{};
    constexpr u32 kSnapshotCount = 50000;
    std::atomic<bool> begin{false};
    std::atomic<bool> mismatch{false};

    std::thread producer([&] {
        while (!begin.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        for (u32 sequence = 0; sequence < kSnapshotCount; ++sequence) {
            state.live_snapshot = make_snapshot(sequence);
            while (!PushGameplayInputSnapshot(state)) {
                std::this_thread::yield();
            }
        }
    });

    std::thread consumer([&] {
        begin.store(true, std::memory_order_release);
        for (u32 sequence = 0; sequence < kSnapshotCount; ++sequence) {
            while (!PopGameplayInputSnapshot(state)) {
                std::this_thread::yield();
            }
            if (!snapshot_matches(state.current_snapshot, sequence)) {
                mismatch.store(true, std::memory_order_relaxed);
            }
        }
    });

    producer.join();
    consumer.join();

    REQUIRE(!mismatch.load(std::memory_order_relaxed));
    REQUIRE(state.snapshot_read_offset.load(std::memory_order_acquire) ==
        state.snapshot_write_offset.load(std::memory_order_acquire));
}

void test_reset_flushes_only_the_consumer_cursor() {
    GameplayInputActionState state{};
    for (u32 sequence = 0; sequence < 7; ++sequence) {
        state.live_snapshot = make_snapshot(sequence);
        REQUIRE(PushGameplayInputSnapshot(state));
    }

    const u32 producer_cursor =
        state.snapshot_write_offset.load(std::memory_order_acquire);
    REQUIRE(producer_cursor != 0);
    state.snapshot_side_flag = true;

    ResetGameplayInputSnapshotRing(state);

    REQUIRE(state.snapshot_write_offset.load(std::memory_order_acquire) ==
        producer_cursor);
    REQUIRE(state.snapshot_read_offset.load(std::memory_order_acquire) ==
        producer_cursor);
    REQUIRE(!state.snapshot_side_flag);
    REQUIRE(!PopGameplayInputSnapshot(state));

    constexpr u32 kPostResetSequence = 0x1234u;
    state.live_snapshot = make_snapshot(kPostResetSequence);
    REQUIRE(PushGameplayInputSnapshot(state));
    REQUIRE(PopGameplayInputSnapshot(state));
    REQUIRE(snapshot_matches(state.current_snapshot, kPostResetSequence));
    REQUIRE(!PopGameplayInputSnapshot(state));
}

void reset_global_input_streams() {
    ResetInputEventState();
    InputEvent event{};
    while (PopInputEvent(event)) {
    }
}

struct CursorOnlyProbe {
    u32 pre = 0;
    u32 set = 0;
    u32 restore = 0;
    u32 post = 0;
    u32 finalize = 0;
};

CursorOnlyProbe g_cursor_only_probe;

u32 g_feedback_unit = 0;
u32 g_feedback_draw_flags = 0;

void capture_unit_draw_feedback(
    GameplayInputActionState&, u32 unit_offset, u32 draw_flags) {
    g_feedback_unit = unit_offset;
    g_feedback_draw_flags = draw_flags;
}

void test_attack_action_publishes_original_red_flash_timer() {
    GameplayInputActionState state{};
    InitializeOriginalGameplayInputActionTables(state);
    state.callbacks.apply_unit_draw_flags = capture_unit_draw_feedback;
    state.selected_unit_offset = 0x1234u;
    state.multi_select_count = 1;

    GameplayActionUnitState unit{};
    unit.offset = state.selected_unit_offset;
    unit.command_state = 0x1eu;
    state.units.push_back(unit);
    GameplayActionUnitState target{};
    target.offset = 0x5678u;
    target.command_state = 0x17u;
    state.units.push_back(target);

    g_feedback_unit = 0;
    g_feedback_draw_flags = 0;
    REQUIRE(ApplyGameplayInputActionDrawFeedback(
        state, 0x05u, target.offset));
    REQUIRE(state.units[0].command_state == 0x1eu);
    REQUIRE(state.units[0].draw_flags == 0u);
    REQUIRE(state.units[1].command_state == 0x17u);
    REQUIRE(state.units[1].draw_flags == 0x88u);
    REQUIRE(g_feedback_unit == target.offset);
    REQUIRE(g_feedback_draw_flags == 0x88u);

    g_feedback_unit = 0;
    g_feedback_draw_flags = 0;
    REQUIRE(!ApplyGameplayInputActionDrawFeedback(
        state, 0x04u, state.selected_unit_offset));
    REQUIRE(state.units[0].draw_flags == 0u);
    REQUIRE(g_feedback_unit == 0u);
    REQUIRE(g_feedback_draw_flags == 0u);

    // The low seven bits are the original eight-tick lifetime; 0x80 remains
    // set while bit 0x02 alternates through the red blitter phases.
    u32 flags = state.units[1].draw_flags;
    u32 red_phase_count = 0;
    for (u32 tick = 0; tick < 8; ++tick) {
        REQUIRE((flags & 0x7fu) != 0);
        --flags;
        if ((flags & 0x82u) == 0x82u) {
            ++red_phase_count;
        }
    }
    REQUIRE(flags == 0x80u);
    REQUIRE(red_phase_count == 4u);
}

void cursor_only_pre(GameplayInputActionState&) {
    ++g_cursor_only_probe.pre;
}

void cursor_only_set(GameplayInputActionState&) {
    ++g_cursor_only_probe.set;
}

void cursor_only_restore(GameplayInputActionState&) {
    ++g_cursor_only_probe.restore;
}

void cursor_only_post(GameplayInputActionState&) {
    ++g_cursor_only_probe.post;
}

void cursor_only_finalize(GameplayInputActionState&) {
    ++g_cursor_only_probe.finalize;
}

bool cursor_only_skip_drain(GameplayInputActionState&) {
    return true;
}

void test_cursor_only_pump_preserves_deterministic_input_batch() {
    reset_global_input_streams();
    REQUIRE(PushKeyboardInputEvent(0x31u));

    GameplayInputActionState state{};
    state.callbacks.pre_cursor_update = cursor_only_pre;
    state.callbacks.set_game_cursor_index = cursor_only_set;
    state.callbacks.restore_game_cursor = cursor_only_restore;
    state.callbacks.post_cursor_update = cursor_only_post;
    state.callbacks.finalize_cursor_frame = cursor_only_finalize;

    g_cursor_only_probe = {};
    state.cursor_mode = 1;
    PumpGameplayCursorFrameOnly(state);
    REQUIRE(HasQueuedInputEvent());
    REQUIRE(g_cursor_only_probe.pre == 1);
    REQUIRE(g_cursor_only_probe.set == 1);
    REQUIRE(g_cursor_only_probe.restore == 0);
    REQUIRE(g_cursor_only_probe.post == 1);
    REQUIRE(g_cursor_only_probe.finalize == 1);

    state.cursor_mode = 0;
    PumpGameplayCursorFrameOnly(state);
    REQUIRE(HasQueuedInputEvent());
    REQUIRE(g_cursor_only_probe.pre == 2);
    REQUIRE(g_cursor_only_probe.set == 1);
    REQUIRE(g_cursor_only_probe.restore == 1);
    REQUIRE(g_cursor_only_probe.post == 2);
    REQUIRE(g_cursor_only_probe.finalize == 2);

    state.keyboard_filter_active = true;
    PumpGameplayCursorFrameOnly(state);
    REQUIRE(HasQueuedInputEvent());
    REQUIRE(g_cursor_only_probe.pre == 2);
    REQUIRE(g_cursor_only_probe.set == 1);
    REQUIRE(g_cursor_only_probe.restore == 1);
    REQUIRE(g_cursor_only_probe.post == 2);
    REQUIRE(g_cursor_only_probe.finalize == 3);

    state.modal_route_blocked = true;
    PumpGameplayCursorFrameOnly(state);
    REQUIRE(HasQueuedInputEvent());
    REQUIRE(g_cursor_only_probe.pre == 3);
    REQUIRE(g_cursor_only_probe.set == 1);
    REQUIRE(g_cursor_only_probe.restore == 2);
    REQUIRE(g_cursor_only_probe.post == 3);
    REQUIRE(g_cursor_only_probe.finalize == 4);

    state.modal_route_blocked = false;
    state.callbacks.can_skip_input_drain = cursor_only_skip_drain;
    PumpGameplayInputAndCursorFrame(state);
    REQUIRE(HasQueuedInputEvent());
    REQUIRE(g_cursor_only_probe.pre == 4);
    REQUIRE(g_cursor_only_probe.set == 1);
    REQUIRE(g_cursor_only_probe.restore == 3);
    REQUIRE(g_cursor_only_probe.post == 4);
    REQUIRE(g_cursor_only_probe.finalize == 5);
    reset_global_input_streams();
}

void test_full_main_ring_does_not_publish_an_orphan_snapshot() {
    reset_global_input_streams();
    GameplayInputActionState& gameplay = gameplay_input_action_state();

    for (u32 index = 0; index + 1 < kInputEventQueueSize; ++index) {
        REQUIRE(PushKeyboardInputEvent(index + 1));
    }
    const u32 snapshot_write_before =
        gameplay.snapshot_write_offset.load(std::memory_order_acquire);

    input_state().mouse_x = 17;
    input_state().mouse_y = 29;
    REQUIRE(!PushMouseInputEvent(0x0200u, 0x55u, 0, 0));
    REQUIRE(gameplay.snapshot_write_offset.load(std::memory_order_acquire) ==
        snapshot_write_before);

    InputEvent event{};
    for (u32 index = 0; index + 1 < kInputEventQueueSize; ++index) {
        REQUIRE(PopInputEvent(event));
        REQUIRE(event.kind == InputEventKind::keyboard);
    }
    REQUIRE(!PopInputEvent(event));
    REQUIRE(!PopGameplayInputSnapshot(gameplay));

    input_state().mouse_x = 31;
    input_state().mouse_y = 47;
    REQUIRE(PushMouseInputEvent(0x0200u, 0x66u, 0, 0));
    REQUIRE(PopInputEvent(event));
    REQUIRE(event.kind == InputEventKind::mouse);
    REQUIRE(gameplay.current_snapshot.field1 == 0x66u);
    REQUIRE(gameplay.current_snapshot.field2 == 31u);
    REQUIRE(gameplay.current_snapshot.field3 == 47u);
    REQUIRE(!PopInputEvent(event));
    REQUIRE(!PopGameplayInputSnapshot(gameplay));
}

void test_threaded_mouse_event_and_snapshot_streams_stay_paired() {
    reset_global_input_streams();
    GameplayInputActionState& gameplay = gameplay_input_action_state();
    constexpr u32 kEventCount = 30000;
    std::atomic<bool> begin{false};
    std::atomic<bool> consumer_done{false};
    std::atomic<bool> mismatch{false};

    std::thread producer([&] {
        while (!begin.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        for (u32 sequence = 1; sequence <= kEventCount; ++sequence) {
            input_state().mouse_x = sequence;
            input_state().mouse_y = sequence ^ 0x55aau;
            while (!PushMouseInputEvent(0x0200u, sequence, 0, 0)) {
                std::this_thread::yield();
            }
        }
    });

    std::thread consumer([&] {
        begin.store(true, std::memory_order_release);
        InputEvent event{};
        for (u32 sequence = 1; sequence <= kEventCount; ++sequence) {
            while (!PopInputEvent(event)) {
                std::this_thread::yield();
            }
            if (event.kind != InputEventKind::mouse ||
                event.code != sequence ||
                event.x != static_cast<i32>(sequence) ||
                event.y != static_cast<i32>(sequence ^ 0x55aau) ||
                gameplay.current_snapshot.field1 != sequence ||
                gameplay.current_snapshot.field2 != sequence ||
                gameplay.current_snapshot.field3 != (sequence ^ 0x55aau)) {
                mismatch.store(true, std::memory_order_relaxed);
            }
        }
        consumer_done.store(true, std::memory_order_release);
    });

    // The gameplay worker clears its unrelated live pointer scratch during
    // ordinary frame handling.  Mouse publication must use a producer-local
    // snapshot so those clears cannot tear a queued payload.
    std::thread live_snapshot_resetter([&] {
        while (!begin.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        while (!consumer_done.load(std::memory_order_acquire)) {
            ResetGameplayInputPointerState(gameplay);
            std::this_thread::yield();
        }
    });

    producer.join();
    consumer.join();
    live_snapshot_resetter.join();

    REQUIRE(!mismatch.load(std::memory_order_relaxed));
    REQUIRE(!HasQueuedInputEvent());
    REQUIRE(!PopGameplayInputSnapshot(gameplay));
}

void test_concurrent_resets_do_not_split_paired_publication() {
    reset_global_input_streams();
    GameplayInputActionState& gameplay = gameplay_input_action_state();
    constexpr u32 kEventCount = 100000;
    std::atomic<bool> begin{false};
    std::atomic<bool> producer_done{false};
    std::atomic<bool> mismatch{false};
    u32 received = 0;
    u32 reset_count = 0;

    std::thread producer([&] {
        while (!begin.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        for (u32 sequence = 1; sequence <= kEventCount; ++sequence) {
            const u32 x = sequence & 0x7fffu;
            const u32 y = (sequence ^ 0x33ccu) & 0x7fffu;
            const u32 lparam = x | (y << 16);
            const auto publish_button = [&] {
                return (sequence & 1u) != 0
                    ? HandleLeftButtonDown(0, lparam)
                    : HandleLeftButtonUp(0, lparam);
            };
            while (!publish_button()) {
                std::this_thread::yield();
            }
        }
        producer_done.store(true, std::memory_order_release);
    });

    std::thread consumer([&] {
        begin.store(true, std::memory_order_release);
        InputEvent event{};
        u32 iteration = 0;
        while (!producer_done.load(std::memory_order_acquire) ||
            HasQueuedInputEvent()) {
            ++iteration;
            if ((iteration % 5u) == 0u) {
                ResetInputEventState();
                ++reset_count;
                continue;
            }
            if (!PopInputEvent(event)) {
                std::this_thread::yield();
                continue;
            }
            ++received;
            if (event.kind != InputEventKind::mouse ||
                gameplay.current_snapshot.field1 != event.code ||
                gameplay.current_snapshot.field2 !=
                    static_cast<u32>(event.x) ||
                gameplay.current_snapshot.field3 !=
                    static_cast<u32>(event.y)) {
                mismatch.store(true, std::memory_order_relaxed);
            }
        }
    });

    producer.join();
    consumer.join();

    REQUIRE(!mismatch.load(std::memory_order_relaxed));
    REQUIRE(reset_count != 0);
    REQUIRE(received != 0);
    REQUIRE(!HasQueuedInputEvent());
    REQUIRE(!PopGameplayInputSnapshot(gameplay));
}

} // namespace

int main() {
    static_assert(sizeof(GameplayInputSnapshotCursor) == sizeof(u32));
    static_assert(alignof(GameplayInputSnapshotCursor) == alignof(u32));

    test_threaded_spsc_publication_preserves_every_snapshot();
    test_attack_action_publishes_original_red_flash_timer();
    test_reset_flushes_only_the_consumer_cursor();
    test_cursor_only_pump_preserves_deterministic_input_batch();
    test_full_main_ring_does_not_publish_an_orphan_snapshot();
    test_threaded_mouse_event_and_snapshot_streams_stay_paired();
    test_concurrent_resets_do_not_split_paired_publication();
    std::cout << "GAMEPLAY_INPUT_SNAPSHOT_RING_PASS snapshot-count=50000"
              << " paired-mouse-count=30000 live-scratch=isolated"
              << " main-full=no-orphan"
              << " cursor-only=no-input-drain+filter-modal-safe"
              << " concurrent-reset-count=100000"
              << " reset=consumer-tail-only abi=u32\n";
    return 0;
}
