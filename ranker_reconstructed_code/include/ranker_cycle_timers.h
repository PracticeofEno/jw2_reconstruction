#pragma once

#include "ranker_types.h"

#include <vector>

namespace ranker {

struct CycleTimerEntry {
    u64 accumulated_ticks = 0;
    u64 start_ticks = 0;
    u32 sample_count = 0;
};

struct CycleTimerTable {
    u32 slot_count = 0;
    std::vector<CycleTimerEntry> entries;
};

void InitializeCycleTimerTable(CycleTimerTable& table, u32 slot_count);
void ReleaseCycleTimerTable(CycleTimerTable& table);
u64 ReadRankerTimestampCounter();
CycleTimerEntry* GetCycleTimerTableEntries(CycleTimerTable& table);
void StartCycleTimerSlot(CycleTimerTable& table, u32 slot);
void StopCycleTimerSlot(CycleTimerTable& table, u32 slot);
u32 GetCycleTimerSlotCount(const CycleTimerTable& table);
void ResetCycleTimerTable(CycleTimerTable& table);

}
