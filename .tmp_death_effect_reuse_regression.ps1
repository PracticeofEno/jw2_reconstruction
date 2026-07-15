$ErrorActionPreference = 'Stop'

$sourcePath = 'ranker_reconstructed_code\src\ranker_winmain.cpp'
$source = Get-Content -Raw -LiteralPath $sourcePath

function Get-FunctionBody([string]$startMarker, [string]$endMarker) {
    $start = $source.LastIndexOf($startMarker)
    $end = $source.IndexOf($endMarker, $start + $startMarker.Length)
    if ($start -lt 0 -or $end -le $start) {
        throw "Function range was not found: $startMarker"
    }
    return $source.Substring($start, $end - $start)
}

$sync = Get-FunctionBody `
    'void sync_default_map_effect_context_session_record()' `
    'void stamp_default_gameplay_save_title'
if (-not $sync.Contains('if (!effect.active)')) {
    throw 'Map-effect save must use active-list membership.'
}
if ($sync.Contains('effect.effect_id == 0')) {
    throw 'Map-effect save must not discard active type 0.'
}

$recover = Get-FunctionBody `
    'std::vector<u32> recover_default_session_active_map_effect_indices' `
    'void initialize_default_map_effect_context_from_session_records'
foreach ($fragment in @(
    'const bool occupancy_layer_loaded',
    'default_session_map_effect_slot_has_occupancy(record, index)',
    '(effect_id != 0 || occupancy_layer_loaded)')) {
    if (-not $recover.Contains($fragment)) {
        throw "Headerless type-0 recovery is incomplete: $fragment"
    }
}

$restore = Get-FunctionBody `
    'void initialize_default_map_effect_context_from_session_records' `
    'void initialize_default_gameplay_terrain_layer_from_session_records'
$fallback = $restore.IndexOf('if (!restored_header_lists)')
if ($fallback -lt 0) {
    throw 'Map-effect header fallback was not found.'
}
$headerRestore = $restore.Substring(0, $fallback)
if ($headerRestore.Contains('kGameplayMapEffectObjectTypeOffset')) {
    throw 'Authoritative active/free chains must not use type 0 as a sentinel.'
}
$activeMaterialization = $restore.Substring(
    $restore.IndexOf('for (u32 index : active_indices)'))
if ($activeMaterialization -match 'if\s*\(\s*effect_id\s*==\s*0\s*\)') {
    throw 'Active-list materialization must not skip effect type 0.'
}

$create = Get-FunctionBody `
    'UnitMovementUnit* default_unit_command_create_unit(UnitCommandContext&,' `
    'bool dispatch_default_unit_command_action_effect'
$probe = Get-FunctionBody `
    'bool default_owner_ai_acquire_temporary_path_probe(' `
    'void default_owner_ai_release_temporary_path_probe'
$pool = Get-FunctionBody `
    'void initialize_default_gameplay_original_unit_pool_slots()' `
    'void purge_default_inactive_movement_units'
$spawn = Get-FunctionBody `
    'UnitMovementUnit* spawn_default_gameplay_script_unit(' `
    'bool dispatch_default_gameplay_script_immediate_spawn'

foreach ($entry in @(
    @{ Name = 'production/create'; Body = $create },
    @{ Name = 'temporary probe'; Body = $probe },
    @{ Name = 'script spawn'; Body = $spawn })) {
    foreach ($fragment in @(
        'const i32 residual_next_path_x = unit->next_path_x',
        'const i32 residual_next_path_y = unit->next_path_y',
        'next_path_x = residual_next_path_x',
        'next_path_y = residual_next_path_y',
        'saved_path_target_x = residual_next_path_x',
        'saved_path_target_y = residual_next_path_y')) {
        if (-not $entry.Body.Contains($fragment)) {
            throw "$($entry.Name) does not preserve fixed-slot path residual: $fragment"
        }
    }
}

$scriptPlacement = $spawn.IndexOf('const bool placed = InitializePlacedUnitFromMapSlot')
$scriptResidualRestore = $spawn.IndexOf(
    'unit->next_path_x = residual_next_path_x')
$scriptFailure = $spawn.IndexOf('if (!placed)')
if ($scriptPlacement -lt 0 -or $scriptResidualRestore -le $scriptPlacement -or
    $scriptFailure -le $scriptResidualRestore) {
    throw 'Script spawn must restore raw +0xc8/+0xcc after placement on both outcomes.'
}

foreach ($fragment in @(
    'free_unit->next_path_x = script.objects[slot].next_path_x',
    'free_unit->next_path_y = script.objects[slot].next_path_y',
    'free_unit->saved_path_target_x = script.objects[slot].next_path_x',
    'free_unit->saved_path_target_y = script.objects[slot].next_path_y')) {
    if (-not $pool.Contains($fragment)) {
        throw "Serialized free-slot path residual is not materialized: $fragment"
    }
}

$compiler = Get-Command g++ -ErrorAction Stop
$cpp = '.tmp_map_effect_zero_id_runtime_regression.cpp'
$exe = '.tmp_map_effect_zero_id_runtime_regression.exe'
& $compiler.Source -std=c++17 -O2 -Wall -Wextra -pedantic `
    -I 'ranker_reconstructed_code\include' $cpp `
    'ranker_reconstructed_code\src\ranker_map_effects.cpp' -o $exe
if ($LASTEXITCODE -ne 0) {
    throw "Map-effect type-0 runtime regression did not compile ($LASTEXITCODE)."
}
& ".\$exe"
if ($LASTEXITCODE -ne 0) {
    throw "Map-effect type-0 runtime regression failed ($LASTEXITCODE)."
}

Write-Output `
    'DEATH_EFFECT_REUSE_SOURCE_PASS map_effect0=list/occupancy production/script/probe=path-residual'
