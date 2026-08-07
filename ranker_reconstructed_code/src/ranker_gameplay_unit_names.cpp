#include "ranker_gameplay_unit_names.h"

#include "ranker_game_session_tables.h"
#include "ranker_indexed_text_table.h"
#include "ranker_runtime_resources.h"

#include <array>
#include <string_view>

namespace ranker {
namespace {

std::array<SessionUnitDefinitionNameField, kUnitDefinitionResourceCount>
    g_unit_name_overrides;

}

void ClearGameplayUnitNameOverrides() {
    g_unit_name_overrides.fill({});
}

void SetGameplayUnitNameOverride(
    u32 type_id, const SessionUnitDefinitionNameField& name) {
    g_unit_name_overrides[type_id] = name;
}

std::string GameplayUnitNameOrFallback(u32 type_id) {
    if (type_id < g_unit_name_overrides.size() &&
        g_unit_name_overrides[type_id].present) {
        return g_unit_name_overrides[type_id].text;
    }

    const std::string_view name =
        GetIndexedTextTableRow(StartupAuxiliaryIndexedTextTable(0), type_id);
    if (!name.empty()) {
        return std::string(name);
    }
    return "Unit " + std::to_string(type_id);
}

}
