$ErrorActionPreference = 'Stop'

$root = $PSScriptRoot
$auditPath = Join-Path $root '.tmp_nextdiv_compact_audit_frame2.py'
$inputPath = Join-Path $root `
    'ranker_reconstructed_code\src\ranker_gameplay_input_actions.cpp'

foreach ($path in @($auditPath, $inputPath)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Missing local-feedback audit source: $path"
    }
}

$audit = Get-Content -LiteralPath $auditPath -Raw
$input = Get-Content -LiteralPath $inputPath -Raw

if ($input -notmatch
        'unit->draw_flags\s*=\s*draw_flags;[\s\S]*apply_unit_draw_flags') {
    throw 'Gameplay input no longer proves draw_flags is direct local feedback.'
}
if ($audit -notmatch
        'for row in rows\.values\(\):\s*\r?\n\s*row\["draw_flags"\]\s*=\s*0') {
    throw 'Cross-peer state audit still compares peer-local draw feedback.'
}
if ($audit -match '0x80\s*<=\s*draw_flags\s*<=\s*0x88') {
    throw 'Cross-peer state audit only normalizes the A-command subset.'
}

Write-Output `
    'NEXTDIV_LOCAL_FEEDBACK_SOURCE_PASS draw_flags=local normalized=all'
