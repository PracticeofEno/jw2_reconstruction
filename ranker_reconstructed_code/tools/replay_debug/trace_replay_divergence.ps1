param(
    [int]$ReplayIndex = -1,
    [string]$ReplayPath = '',
    [Parameter(Mandatory = $true)][int]$StartFrame,
    [Parameter(Mandatory = $true)][int]$EndFrame,
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [string]$RepositoryRoot = '',
    [string]$LayoutPath = '',
    [string]$OriginalExecutable = 'RankerOCPV_Win\ranker.exe',
    [string]$RebuildExecutable = 'RankerOCPV_Win\ranker_rebuild.exe',
    [int]$TimeoutSeconds = 600,
    [string]$AuditScript = 'compare_process_state.py',
    [int]$TraceIntervalMs = 100,
    [string]$TemporaryReplayName = 'DebugReplay_Audit.ply',
    [switch]$StabilizeViewport,
    [switch]$AlignPresentationRng
)

$ErrorActionPreference = 'Stop'
$toolDirectory = $PSScriptRoot
$repositoryRoot = if ($RepositoryRoot) {
    (Resolve-Path -LiteralPath $RepositoryRoot).Path
} else {
    (Resolve-Path -LiteralPath (Join-Path $toolDirectory '..\..\..')).Path
}
$replayRoot = (Resolve-Path -LiteralPath `
    (Join-Path $repositoryRoot 'RankerOCPV_Win\Replays')).Path
$sourceCandidate = if ($ReplayPath) {
    if ([IO.Path]::IsPathRooted($ReplayPath)) {
        $ReplayPath
    } else {
        Join-Path $repositoryRoot $ReplayPath
    }
} elseif ($ReplayIndex -ge 0) {
    Join-Path $replayRoot "Debug_replays\$ReplayIndex.ply"
} else {
    throw 'Specify either ReplayPath or a non-negative ReplayIndex.'
}
$source = (Resolve-Path -LiteralPath $sourceCandidate).Path
$replayPrefix = $replayRoot.TrimEnd('\') + '\'
if (-not $source.StartsWith(
        $replayPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Replay source escaped RankerOCPV_Win/Replays: $source"
}
if ([IO.Path]::GetFileName($TemporaryReplayName) -ne $TemporaryReplayName -or
    [IO.Path]::GetExtension($TemporaryReplayName) -ine '.ply') {
    throw "TemporaryReplayName must be a replay basename ending in .ply: $TemporaryReplayName"
}
$temporaryReplay = Join-Path $replayRoot $TemporaryReplayName
Copy-Item -LiteralPath $source -Destination $temporaryReplay -Force

$layoutPath = if ($LayoutPath) {
    (Resolve-Path -LiteralPath $LayoutPath).Path
} else {
    (Resolve-Path -LiteralPath `
        (Join-Path $toolDirectory 'artifacts\current_layout.json')).Path
}
$layout = Get-Content -LiteralPath $layoutPath -Raw | ConvertFrom-Json
$loopRva = [Convert]::ToInt64(($layout.loop_rva -replace '^0x', ''), 16)
$simulationFrameOffset = [Convert]::ToInt64(
    ($layout.loop_layout.simulation_frame -replace '^0x', ''), 16)
$out = [IO.Path]::GetFullPath($OutputDirectory)
[IO.Directory]::CreateDirectory($out) | Out-Null

$original = $null
$rebuild = $null
$drivers = @()
$audit = $null
try {
    $launchText = & (Join-Path $toolDirectory 'launch_replay_pair.ps1') `
        -ReplayName $TemporaryReplayName `
        -RepositoryRoot $repositoryRoot `
        -OriginalExecutable $OriginalExecutable `
        -RebuildExecutable $RebuildExecutable `
        -ExpectedRebuildSha256 $layout.sha256
    $pair = ($launchText -join "`n") | ConvertFrom-Json
    $original = Get-Process -Id ([int]$pair.original_pid)
    $rebuild = Get-Process -Id ([int]$pair.rebuild_pid)
    $rebuildBase = [Convert]::ToInt64(
        ($pair.rebuild_base -replace '^0x', ''), 16)
    $rebuildFrameAddress = $rebuildBase + $loopRva + $simulationFrameOffset

    & python (Join-Path $toolDirectory 'replay_pair_control.py') fast `
        $original.Id $rebuild.Id ('0x{0:X}' -f $rebuildBase) `
        ('0x{0:X}' -f $loopRva) $layoutPath | Out-Null

    $fastTarget = [Math]::Max(1, $StartFrame - 20)
    $drivers = @(
        Start-Process python -ArgumentList @(
            (Join-Path $toolDirectory 'resume_to_frame.py'),
            [string]$original.Id, '0x007071A4', [string]$fastTarget,
            [string]$TimeoutSeconds, '0') -WorkingDirectory $repositoryRoot `
            -RedirectStandardOutput (Join-Path $out 'original-fast.out') `
            -RedirectStandardError (Join-Path $out 'original-fast.err') `
            -WindowStyle Hidden -PassThru
        Start-Process python -ArgumentList @(
            (Join-Path $toolDirectory 'resume_to_frame.py'),
            [string]$rebuild.Id, ('0x{0:X}' -f $rebuildFrameAddress),
            [string]$fastTarget, [string]$TimeoutSeconds, '0') `
            -WorkingDirectory $repositoryRoot `
            -RedirectStandardOutput (Join-Path $out 'rebuild-fast.out') `
            -RedirectStandardError (Join-Path $out 'rebuild-fast.err') `
            -WindowStyle Hidden -PassThru
    )
    Wait-Process -Id $drivers.Id
    $fastFrames = (& python `
        (Join-Path $toolDirectory 'replay_pair_control.py') frames `
        $original.Id $rebuild.Id ('0x{0:X}' -f $rebuildBase) `
        ('0x{0:X}' -f $loopRva) $layoutPath | ConvertFrom-Json)
    if ([int]$fastFrames.original -lt $fastTarget -or
        [int]$fastFrames.rebuild -lt $fastTarget) {
        throw "Fast-forward target $fastTarget was not reached: original=$($fastFrames.original), rebuild=$($fastFrames.rebuild)"
    }

    & python (Join-Path $toolDirectory 'replay_pair_control.py') pace `
        $original.Id $rebuild.Id ('0x{0:X}' -f $rebuildBase) `
        ('0x{0:X}' -f $loopRva) $TraceIntervalMs $TraceIntervalMs `
        $layoutPath | Out-Null
    $drivers = @(
        Start-Process python -ArgumentList @(
            (Join-Path $toolDirectory 'resume_to_frame.py'),
            [string]$original.Id, '0x007071A4', [string]$StartFrame,
            '30', '0.02') -WorkingDirectory $repositoryRoot `
            -RedirectStandardOutput (Join-Path $out 'original-start.out') `
            -RedirectStandardError (Join-Path $out 'original-start.err') `
            -WindowStyle Hidden -PassThru
        Start-Process python -ArgumentList @(
            (Join-Path $toolDirectory 'resume_to_frame.py'),
            [string]$rebuild.Id, ('0x{0:X}' -f $rebuildFrameAddress),
            [string]$StartFrame, '30', '0.02') -WorkingDirectory $repositoryRoot `
            -RedirectStandardOutput (Join-Path $out 'rebuild-start.out') `
            -RedirectStandardError (Join-Path $out 'rebuild-start.err') `
            -WindowStyle Hidden -PassThru
    )
    Wait-Process -Id $drivers.Id
    $startFrames = (& python `
        (Join-Path $toolDirectory 'replay_pair_control.py') frames `
        $original.Id $rebuild.Id ('0x{0:X}' -f $rebuildBase) `
        ('0x{0:X}' -f $loopRva) $layoutPath | ConvertFrom-Json)
    if ([int]$startFrames.original -lt $StartFrame -or
        [int]$startFrames.rebuild -lt $StartFrame) {
        throw "Trace start frame $StartFrame was not reached: original=$($startFrames.original), rebuild=$($startFrames.rebuild)"
    }

    if ($StabilizeViewport) {
        $viewportState = & python `
            (Join-Path $toolDirectory 'replay_pair_control.py') stabilize `
            $original.Id $rebuild.Id ('0x{0:X}' -f $rebuildBase) `
            ('0x{0:X}' -f $loopRva) $layoutPath
        if ($LASTEXITCODE -ne 0) {
            throw 'Failed to stabilize the replay viewport.'
        }
        $viewportState | Set-Content -LiteralPath `
            (Join-Path $out 'viewport-stabilization.json') -Encoding utf8
    }
    if ($AlignPresentationRng) {
        $presentationRngState = & python `
            (Join-Path $toolDirectory 'replay_pair_control.py') presentation-rng `
            $original.Id $rebuild.Id ('0x{0:X}' -f $rebuildBase) `
            ('0x{0:X}' -f $loopRva) $layoutPath
        if ($LASTEXITCODE -ne 0) {
            throw 'Failed to align the presentation RNG at trace start.'
        }
        $presentationRngState | Set-Content -LiteralPath `
            (Join-Path $out 'presentation-rng.json') -Encoding utf8
    }

    $resultPath = Join-Path $out 'result.json'
    $journalPath = Join-Path $out 'journal.jsonl'
    $stopPath = Join-Path $out 'stop.flag'
    $audit = Start-Process python -ArgumentList @(
        (Join-Path $toolDirectory $AuditScript),
        [string]$original.Id, [string]$rebuild.Id,
        ('0x{0:X}' -f $rebuildBase), $layoutPath, $resultPath,
        $journalPath, $stopPath, [string]$TimeoutSeconds
    ) -WorkingDirectory $repositoryRoot `
        -RedirectStandardOutput (Join-Path $out 'audit.out') `
        -RedirectStandardError (Join-Path $out 'audit.err') `
        -WindowStyle Hidden -PassThru

    # Give the memory auditor time to open both suspended processes and build
    # its first normalized snapshot before either frame driver resumes them.
    Start-Sleep -Seconds 2

    $drivers = @(
        Start-Process python -ArgumentList @(
            (Join-Path $toolDirectory 'resume_to_frame.py'),
            [string]$original.Id, '0x007071A4', [string]$EndFrame,
            [string]$TimeoutSeconds, '0.02') -WorkingDirectory $repositoryRoot `
            -RedirectStandardOutput (Join-Path $out 'original-trace.out') `
            -RedirectStandardError (Join-Path $out 'original-trace.err') `
            -WindowStyle Hidden -PassThru
        Start-Process python -ArgumentList @(
            (Join-Path $toolDirectory 'resume_to_frame.py'),
            [string]$rebuild.Id, ('0x{0:X}' -f $rebuildFrameAddress),
            [string]$EndFrame, [string]$TimeoutSeconds, '0.02') `
            -WorkingDirectory $repositoryRoot `
            -RedirectStandardOutput (Join-Path $out 'rebuild-trace.out') `
            -RedirectStandardError (Join-Path $out 'rebuild-trace.err') `
            -WindowStyle Hidden -PassThru
    )

    while (-not (Test-Path -LiteralPath $resultPath) -and
           ($drivers | Where-Object { -not $_.HasExited }).Count -gt 0 -and
           -not $audit.HasExited) {
        Start-Sleep -Milliseconds 100
        $audit.Refresh()
        foreach ($driver in $drivers) { $driver.Refresh() }
    }
    if (-not (Test-Path -LiteralPath $resultPath)) {
        [IO.File]::WriteAllText($stopPath, 'stop')
        $audit.WaitForExit(15000) | Out-Null
    }
    if (-not (Test-Path -LiteralPath $resultPath)) {
        throw 'Continuous audit produced no result.'
    }
    Get-Content -LiteralPath $resultPath -Raw
}
finally {
    foreach ($driver in $drivers) {
        if ($null -ne $driver -and -not $driver.HasExited) {
            Stop-Process -Id $driver.Id -Force -ErrorAction SilentlyContinue
        }
    }
    if ($null -ne $audit -and -not $audit.HasExited) {
        Stop-Process -Id $audit.Id -Force -ErrorAction SilentlyContinue
    }
    foreach ($process in @($original, $rebuild)) {
        if ($null -ne $process -and -not $process.HasExited) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        }
    }
    Remove-Item -LiteralPath $temporaryReplay -Force `
        -ErrorAction SilentlyContinue
}
