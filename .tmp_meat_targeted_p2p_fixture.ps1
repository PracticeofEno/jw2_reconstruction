param(
    [string]$MainScript = '.tmp_meat_targeted_p2p_regression.ps1',
    [string]$PairProbe = '.tmp_meat_targeted_pair_probe.py',
    [string]$PairFixture = '.tmp_meat_targeted_pair_probe_fixture.py',
    [string]$HistoryFixture = '.tmp_meat_live_probe_history_regression.py'
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$python = (Get-Command python -ErrorAction Stop).Source
$paths = @{}
foreach ($entry in @{
    main = $MainScript
    pair = $PairProbe
    pair_fixture = $PairFixture
    history_fixture = $HistoryFixture
}.GetEnumerator()) {
    $candidate = if ([IO.Path]::IsPathRooted($entry.Value)) {
        $entry.Value
    } else {
        Join-Path $root $entry.Value
    }
    if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
        throw "Missing targeted meat fixture input: $candidate"
    }
    $paths[$entry.Key] = [IO.Path]::GetFullPath($candidate)
}

$mainSource = [IO.File]::ReadAllText($paths.main)
$pairSource = [IO.File]::ReadAllText($paths.pair)

function Assert-Contains(
    [string]$Source, [string]$Needle, [string]$Description) {
    if (-not $Source.Contains($Needle)) {
        throw "Missing contract: $Description [$Needle]"
    }
}

function Assert-Matches(
    [string]$Source, [string]$Pattern, [string]$Description) {
    if (-not [Regex]::IsMatch(
        $Source, $Pattern,
        [Text.RegularExpressions.RegexOptions]::CultureInvariant)) {
        throw "Missing contract: $Description [$Pattern]"
    }
}

function Assert-NotContains(
    [string]$Source, [string]$Needle, [string]$Description) {
    if ($Source.Contains($Needle)) {
        throw "Forbidden contract: $Description [$Needle]"
    }
}

$tokens = $null
$parseErrors = $null
$null = [Management.Automation.Language.Parser]::ParseFile(
    $paths.main, [ref]$tokens, [ref]$parseErrors)
if (@($parseErrors).Count -ne 0) {
    throw ('PowerShell parse errors: ' +
        ((@($parseErrors) | ForEach-Object { $_.Message }) -join '; '))
}

# Dry-run must return before any process inspection/launch or output write.
$dryRunIndex = $mainSource.IndexOf('if (-not $Execute)')
$processGateIndex = $mainSource.IndexOf('$existing = @(Get-Process')
$launchIndex = $mainSource.IndexOf(
    "`$pair = & (Join-Path `$root '.tmp_route_fresh_pair.ps1')")
$outputCreateIndex = $mainSource.IndexOf('[IO.Directory]::CreateDirectory($output)')
if ($dryRunIndex -lt 0 -or $processGateIndex -le $dryRunIndex -or
    $launchIndex -le $dryRunIndex -or $outputCreateIndex -le $dryRunIndex) {
    throw 'Dry-run is not structurally isolated before process/output mutation.'
}

Assert-Contains $mainSource 'product_write_authorized = $false' `
    'dry-run explicitly denies product writes'
Assert-Contains $mainSource '[int]$ProbeTimeoutSeconds = 150' `
    '150 second exact probe default'
Assert-Contains $mainSource '[int]$CombatTimeoutSeconds = 120' `
    '120 second neutral combat allowance'
Assert-Contains $mainSource '$workers.Count -lt 2' `
    'two confirmed local attackers are sufficient'
Assert-Contains $mainSource '$issuedCount -lt 2' `
    'two issued attack orders are the coverage gate'
Assert-Contains $mainSource '[int]$_.type -eq 75' `
    'neutral type-75 selection'
Assert-Contains $mainSource "[int]`$_.owner -eq 8" `
    'neutral owner selection'
Assert-Contains $mainSource "'.tmp_meat_live_probe.py'" `
    'shared exact-frame meat probe'
Assert-Contains $mainSource "'--timeout', [string]`$ProbeTimeoutSeconds" `
    'probe receives the 120-150 second timeout'
Assert-Contains $mainSource 'Get-HotRegion $view 175' `
    'explicit attack hot action'
Assert-Contains $mainSource '[int]$_.effect_id -eq 2' `
    'meat effect id-2 generation'
Assert-Contains $mainSource 'Click-HotItem $selected 183' `
    'collector hot item 183/action 0x0d'
Assert-Contains $mainSource "`$collectorSlot 'marker'" `
    'paired marker probe'
Assert-Contains $mainSource '.area_marker_high' `
    'raw +0x0c bit-31 marker verification'
Assert-Contains $mainSource 'action = 0x0d' `
    'marker action identity'
Assert-Matches $mainSource `
    'Click-World\s+\$foodView[\s\S]{0,180}-Right' `
    'right-click route onto generated food'
Assert-Contains $mainSource "Wait-MeatTraceEvent `$meatTracePath 'pickup'" `
    'exact pickup event'
Assert-Contains $mainSource '[int]$pickupEvent.original.action_delta -gt 0' `
    'pickup increments action mode'
Assert-Contains $mainSource '[int]$pickupEvent.original.cargo_delta -eq 0' `
    'pickup does not contaminate cargo'
Assert-Contains $mainSource '[int]$_.type -eq 32' `
    'hostile type-32 damage source'
Assert-Contains $mainSource "`$collectorSlot 'damage-consume'" `
    'diagnostic damage followed by consume probe'
Assert-Contains $mainSource '$consumeEventDamageObserved' `
    'exact trace proves real pre-consume damage'
Assert-Contains $mainSource '$pairedConsumePass =' `
    'targeted paired condition is independent consume evidence'
Assert-Contains $mainSource `
    '$consumePass = $consumeEventPass -or $pairedConsumePass' `
    'either finalized exact-frame probe can prove consumption'
Assert-Contains $mainSource "'paired_condition'" `
    'consume result records the exact-frame coverage source'
Assert-Contains $mainSource '$integratedConsumeCoverage' `
    'trace and targeted consume coverage are integrated'
Assert-Contains $mainSource '[int]$consumeEvent.original.action_delta -lt 0' `
    'consume decrements action mode'
Assert-Contains $mainSource `
    '-[int]$consumeEvent.original.action_delta' `
    'consume heals exactly the action decrement'
Assert-Contains $mainSource '[int]$consumeEvent.original.cargo_delta -eq 0' `
    'consume does not contaminate cargo'
foreach ($verdict in @('generation','marker','pickup','consume')) {
    Assert-Contains $mainSource ("`$results.$verdict.pass") `
        "independent $verdict verdict"
}
Assert-Contains $mainSource '$summary.coverage.cargo_contamination' `
    'probe-wide cargo non-contamination verdict'
Assert-Contains $mainSource 'finally {' 'unconditional cleanup path'
Assert-Contains $mainSource '$results.cleanup = Stop-FreshProcesses' `
    'all fresh PIDs are cleaned'
Assert-Contains $mainSource 'Get-Process ranker,ranker_rebuild' `
    'ranker PID gate and cleanup inventory'
Assert-Contains $mainSource '[IO.FileShare]::ReadWrite' `
    'live JSONL reader permits the probe writer'
Assert-Contains $mainSource '[IO.FileShare]::Delete' `
    'live JSONL reader uses non-exclusive sharing'

foreach ($forbidden in @(
    'ranker_reconstructed_code', 'Copy-Item', 'Set-Content',
    'Add-Content', 'Out-File', 'Move-Item')) {
    Assert-NotContains $mainSource $forbidden `
        'targeted harness must not mutate products or shared fixtures'
}

Assert-Contains $pairSource 'MEAT.new_stable_pair_histories()' `
    'shared finalized-frame history join'
Assert-Contains $pairSource 'MEAT.capture_stable_pair(' `
    'same-frame pair capture'
Assert-Contains $pairSource '"area_marker_flags": row["area_marker_flags"]' `
    'raw +0x0c marker field'
Assert-Contains $pairSource 'row["area_marker_flags"] & 0x80000000' `
    'marker bit-31 extraction'
Assert-Contains $pairSource '"type_flags": row["type_flags"]' `
    'separate type-flags field'
Assert-Contains $pairSource 'before["health"] < before["max_health"]' `
    'consume requires genuine prior damage'
Assert-Contains $pairSource 'health_delta == -action_delta' `
    'HP/action consume equality'
Assert-Contains $pairSource 'cargo_delta == 0' `
    'cargo non-contamination'
Assert-Contains $pairSource 'damage_observed |= damaged_now' `
    'damage is latched before consume'
Assert-Contains $pairSource 'actual_hash != expected_hash' `
    'live executable/layout hash binding'

$savedNoBytecode = $env:PYTHONDONTWRITEBYTECODE
$env:PYTHONDONTWRITEBYTECODE = '1'
try {
    $syntaxCode =
        'import ast, pathlib, sys; ' +
        '[ast.parse(pathlib.Path(p).read_bytes(), filename=p) ' +
        'for p in sys.argv[1:]]'
    $null = & $python -c $syntaxCode $paths.pair $paths.pair_fixture
    if ($LASTEXITCODE -ne 0) {
        throw "Python syntax validation failed with exit code $LASTEXITCODE."
    }

    $pairFixtureOutput = (& $python $paths.pair_fixture 2>&1) -join "`n"
    if ($LASTEXITCODE -ne 0 -or
        -not $pairFixtureOutput.Contains('MEAT_TARGETED_PAIR_FIXTURE_PASS')) {
        throw "Targeted pair fixture failed: $pairFixtureOutput"
    }
    $historyOutput = (& $python $paths.history_fixture 2>&1) -join "`n"
    if ($LASTEXITCODE -ne 0 -or
        -not $historyOutput.Contains('MEAT_LIVE_HISTORY_JOIN_PASS')) {
        throw "Shared history fixture failed: $historyOutput"
    }
}
finally {
    $env:PYTHONDONTWRITEBYTECODE = $savedNoBytecode
}

Write-Output (
    'MEAT_TARGETED_P2P_FIXTURE_PASS ' +
    'parse=yes dry_run_isolated=yes attackers=2 neutral=75 effect=2 ' +
    'marker=raw0c/bit31 pickup=action/no-cargo ' +
    'consume=damaged/action-1/hp+1/no-cargo exact_pair=yes cleanup=yes')
