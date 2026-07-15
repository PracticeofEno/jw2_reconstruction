$ErrorActionPreference = 'Stop'

$sourcePath = Join-Path $PSScriptRoot 'ranker_reconstructed_code/src/ranker_winmain.cpp'
$source = Get-Content -LiteralPath $sourcePath -Raw
$ownerAiSource = Get-Content -LiteralPath (Join-Path $PSScriptRoot `
    'ranker_reconstructed_code/src/ranker_owner_ai.cpp') -Raw

function Get-SourceSlice([string]$startMarker, [string]$endMarker) {
    $start = $source.IndexOf($startMarker, [System.StringComparison]::Ordinal)
    if ($start -lt 0) {
        throw "missing start marker: $startMarker"
    }
    $end = $source.IndexOf($endMarker, $start + $startMarker.Length,
        [System.StringComparison]::Ordinal)
    if ($end -lt 0) {
        throw "missing end marker after $startMarker`: $endMarker"
    }
    return $source.Substring($start, $end - $start)
}

function Assert-DirectProbe([string]$name, [string]$body) {
    if ($body -notmatch 'ProbeOwnerAiRoutePath\(') {
        throw "$name no longer performs the original direct path probe"
    }
    if ($body -match 'default_owner_ai_temporary_unit_path_probe\(') {
        throw "$name incorrectly allocates a temporary unit and consumes RNG"
    }
}

$fallbackProbe = Get-SourceSlice `
    'OwnerAiRoutePathProbeResult default_owner_ai_path_probe_fallback(' `
    'bool default_owner_ai_acquire_temporary_path_probe('
if ($fallbackProbe -notmatch 'definition\.lifecycle_class == 0' -or
    $fallbackProbe -notmatch 'result\.next_path_point = start') {
    throw 'route-helper acquire-failure fallback no longer matches original class-0 probe'
}

$temporaryProbe = Get-SourceSlice `
    'bool default_owner_ai_acquire_temporary_path_probe(' `
    'OwnerAiRoutePathProbeResult default_owner_ai_temporary_unit_path_probe('
if ($temporaryProbe -notmatch 'kOriginalTemporaryProbeOwner\s*=\s*0') {
    throw 'temporary path probe no longer uses the original fixed owner 0'
}
if ($temporaryProbe -notmatch
    'owner_unit_score\[kOriginalTemporaryProbeOwner\]') {
    throw 'temporary path probe cleanup accounting is not charged back to owner 0'
}

$anchor = Get-SourceSlice `
    'void default_owner_ai_refresh_placement_anchors(' `
    'void sync_default_owner_threat_points_from_ai('
$anchorTempCalls = ([regex]::Matches($anchor,
    'default_owner_ai_acquire_temporary_path_probe\(')).Count
if ($anchorTempCalls -ne 1) {
    throw "placement-anchor refresh must initialize exactly one reusable temp unit; got $anchorTempCalls"
}
if (([regex]::Matches($anchor,
        'static_cast<const UnitMovementUnit\s*\*>\(probe_lease\.unit\)')).Count -lt 2) {
    throw 'placement-anchor path passes no longer probe detached copies of the same temporary unit'
}
if ($anchor -match
        'ProbeOwnerAiRoutePath\(\s*\*movement,\s*\*probe_lease\.unit') {
    throw 'placement-anchor path probe mutates the live temporary fixed-pool unit'
}
if ($anchor -notmatch 'lifecycle, map_center_world, probe_lease') {
    throw 'placement-anchor scratch unit is not initialized at the map center'
}
if ($anchor -notmatch 'ai_owner\.support_mode \+= 5') {
    throw 'placement-anchor distance no longer uses the original cumulative +5 field'
}
if ($anchor -notmatch
        'FindOwnerRouteOrBuildingTargetForCurrentTargetOwner' -or
    $anchor -notmatch 'FindOwnerFallbackTargetForCurrentTargetOwner') {
    throw 'placement-anchor no longer performs the original live target-unit lookup'
}
if ($anchor -notmatch 'FindNearestNeutralUnitToPrimaryRouteTarget' -or
    $anchor -notmatch 'neutral_route_target_point') {
    throw 'placement-anchor lost original pre-target owner-8 neutral refresh'
}
if ($anchor -match 'default_owner_ai_find_anchor_source_unit') {
    throw 'placement-anchor restored a non-original primary-unit source fallback'
}
if ($anchor -notmatch 'path_probe\.path_tiles\.rbegin\(\)') {
    throw 'placement-anchor no longer translates the original reverse path buffer order'
}
if ($anchor -notmatch 'original_reverse_path' -or
    $anchor -notmatch 'SelectOwnerBestOpenPathWindowPoint' -or
    $anchor -notmatch 'ai_owner\.placement_record\[0\]') {
    throw 'placement-anchor lost original preferred-target open-path window refresh'
}
foreach ($field in @('support_target_owner', 'support_budget',
        'support_anchor', 'support_anchor_y')) {
    if ($anchor -notmatch [regex]::Escape("ai_owner.$field")) {
        throw "placement-anchor no longer mirrors original state field $field"
    }
}

$direction = Get-SourceSlice `
    'u32 default_owner_ai_direction8_between_tiles(' `
    'UnitMovementPoint default_owner_ai_direction8_tile_delta('
if ($direction -notmatch 'CalculatePointDirectionFromLookup') {
    throw 'placement-anchor direction no longer uses JW2_07 record-1 lookup'
}

$maintenance = Get-SourceSlice `
    'void run_default_owner_ai_maintenance(' `
    'bool default_gameplay_script_object_alive('
$configureIndex = $maintenance.IndexOf(
    'configure_default_unit_movement_callbacks(',
    [System.StringComparison]::Ordinal)
$tickIndex = $maintenance.IndexOf('TickOwnerAiMaintenance(',
    [System.StringComparison]::Ordinal)
if ($configureIndex -lt 0 -or $tickIndex -lt 0 -or
    $configureIndex -gt $tickIndex) {
    throw 'owner AI can run before the first-frame JW2_07 direction lookup is installed'
}

$longPath = Get-SourceSlice `
    'UnitMovementPoint default_owner_ai_select_production_placement_target_point(' `
    'struct DefaultOwnerPlacementAnchorClearanceContext'
if ($longPath -notmatch
        'path_probe\.path_tiles\[kOriginalLongPathForwardIndex\]') {
    throw 'second anchor path lost original reverse-buffer index 34 translation'
}
if ($longPath -notmatch
        'kOriginalLongPathThreshold\s*=\s*0x23' -or
    $longPath -notmatch
        'path_probe\.path_tiles\.size\(\)\s*>\s*kOriginalLongPathThreshold\s*\+\s*1u') {
    throw 'second anchor path lost original path_count 35/36 boundary'
}
if ($longPath -notmatch
        'path_probe\.reachable\s*&&\s*path_probe\.direct_path') {
    throw 'second anchor path no longer gates the direct branch on found=1/direct=1'
}
if ($ownerAiSource -match
        'void ResetOwnerAiSlotRuntime\([\s\S]{0,3000}support_anchor_y\s*=') {
    throw 'owner AI reset incorrectly overwrites original preserved anchor Y'
}

$placementPath = Get-SourceSlice `
    'bool default_owner_ai_placement_path_probe(' `
    'bool default_owner_ai_placement_target_refresh('
Assert-DirectProbe 'placement path availability' $placementPath

$targetRefresh = Get-SourceSlice `
    'bool default_owner_ai_placement_target_refresh(' `
    'bool default_owner_ai_placement_producer_path_probe('
Assert-DirectProbe 'blocked placement-target refresh' $targetRefresh

$producerProbe = Get-SourceSlice `
    'bool default_owner_ai_placement_producer_path_probe(' `
    'bool default_owner_ai_route_helper_path_score('
Assert-DirectProbe 'placement producer selection' $producerProbe

$routeHelper = Get-SourceSlice `
    'bool default_owner_ai_route_helper_path_score(' `
    'u32 default_owner_ai_target_owner('
if ($routeHelper -notmatch 'default_owner_ai_temporary_unit_path_probe\(') {
    throw 'route-helper score lost its original temporary-unit path probe'
}
if ($routeHelper -notmatch 'candidate_world, \{10, 10\}') {
    throw 'route-helper scratch unit is not initialized at original world point 10,10'
}

$temporaryRouteProbe = Get-SourceSlice `
    'OwnerAiRoutePathProbeResult default_owner_ai_temporary_unit_path_probe(' `
    'bool default_owner_ai_anchor_clearance_blocked('
if ($temporaryRouteProbe -notmatch
        'static_cast<const UnitMovementUnit\s*\*>\(lease\.unit\)') {
    throw 'route-helper path probe no longer protects live scratch next-path fields'
}
if ($temporaryRouteProbe -match
        'ProbeOwnerAiRoutePath\(\s*\*movement,\s*\*lease\.unit') {
    throw 'route-helper path probe mutates the live scratch fixed-pool unit'
}

Write-Output 'OWNER_AI_PATH_PROBE_RNG_SOURCE_PASS temp_owner=0 anchor_temp=1x2copy anchor_step=+5 reverse_path=1 long_index=34 target=live direction=record1-first-frame state=xy placement_direct=3 route_helper_temp=1copy@10,10'
