#pragma once

#include "ranker_types.h"

#include <string>

namespace ranker {

struct SessionUnitDefinitionNameField;

// Runtime unit names may be changed by loaded session definitions and scripts.
// Keep those overrides separate from the immutable indexed text-table fallback.
void ClearGameplayUnitNameOverrides();
void SetGameplayUnitNameOverride(
    u32 type_id, const SessionUnitDefinitionNameField& name);
std::string GameplayUnitNameOrFallback(u32 type_id);

}
