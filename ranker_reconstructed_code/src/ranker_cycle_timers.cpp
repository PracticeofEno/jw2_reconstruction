#include "ranker_cycle_timers.h"

#include <chrono>

namespace ranker {

void InitializeCycleTimerTable(CycleTimerTable& table, u32 slot_count) {
    table.slot_count = slot_count;
    table.entries.assign(slot_count, CycleTimerEntry{});
    ResetCycleTimerTable(table);
}

void ReleaseCycleTimerTable(CycleTimerTable& table) {
    table.entries.clear();
}

u64 ReadRankerTimestampCounter() {
    return static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
}

CycleTimerEntry* GetCycleTimerTableEntries(CycleTimerTable& table) {
    return table.entries.empty() ? nullptr : table.entries.data();
}

void StartCycleTimerSlot(CycleTimerTable& table, u32 slot) {
    if (slot < table.entries.size()) {
        table.entries[slot].start_ticks = ReadRankerTimestampCounter();
    }
}

void StopCycleTimerSlot(CycleTimerTable& table, u32 slot) {
    if (slot >= table.entries.size()) {
        return;
    }
    CycleTimerEntry& entry = table.entries[slot];
    const u64 now = ReadRankerTimestampCounter();
    if (now >= entry.start_ticks) {
        entry.accumulated_ticks += now - entry.start_ticks;
    }
    ++entry.sample_count;
}

u32 GetCycleTimerSlotCount(const CycleTimerTable& table) {
    return table.slot_count;
}

void ResetCycleTimerTable(CycleTimerTable& table) {
    for (CycleTimerEntry& entry : table.entries) {
        entry = CycleTimerEntry{};
    }
}

}
