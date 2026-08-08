param(
    [int]$ReplayIndex = -1,
    [string]$ReplayPath = '',
    [Parameter(Mandatory = $true)][int]$TargetFrame,
    [string]$RepositoryRoot = '',
    [string]$LayoutPath = '',
    [string]$OriginalExecutable = 'RankerOCPV_Win\ranker.exe',
    [string]$RebuildExecutable = 'RankerOCPV_Win\ranker_rebuild.exe',
    [string]$OutputDirectory = '',
    [int]$TimeoutSeconds = 360,
    [int]$OriginalActionDefinitionIndex = -1,
    [Int64]$OriginalDumpAddress = 0,
    [int]$OriginalDumpSize = 0
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
$debugRoot = (Resolve-Path -LiteralPath `
    (Join-Path $replayRoot 'Debug_replays')).Path
$sourceCandidate = if ($ReplayPath) {
    if ([IO.Path]::IsPathRooted($ReplayPath)) {
        $ReplayPath
    } else {
        Join-Path $repositoryRoot $ReplayPath
    }
} elseif ($ReplayIndex -ge 0) {
    Join-Path $debugRoot ("$ReplayIndex.ply")
} else {
    throw 'Specify either ReplayPath or a non-negative ReplayIndex.'
}
$source = (Resolve-Path -LiteralPath $sourceCandidate).Path
$debugPrefix = $replayRoot.TrimEnd('\') + '\'
if (-not $source.StartsWith(
        $debugPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Replay source escaped RankerOCPV_Win/Replays: $source"
}
$temporaryReplayName = 'DebugReplay_Audit.ply'
$temporaryReplay = [IO.Path]::GetFullPath(
    (Join-Path $replayRoot $temporaryReplayName))
if (-not $temporaryReplay.StartsWith(
        $replayRoot.TrimEnd('\') + '\',
        [StringComparison]::OrdinalIgnoreCase)) {
    throw "Temporary replay escaped Replays: $temporaryReplay"
}
Copy-Item -LiteralPath $source -Destination $temporaryReplay -Force

$layoutPath = if ($LayoutPath) {
    (Resolve-Path -LiteralPath $LayoutPath).Path
} else {
    (Resolve-Path -LiteralPath `
        (Join-Path $toolDirectory 'artifacts\current_layout.json')).Path
}
$layout = Get-Content -LiteralPath $layoutPath -Raw | ConvertFrom-Json
$loopRva = [Convert]::ToInt64(($layout.loop_rva -replace '^0x', ''), 16)
$out = if ($OutputDirectory) {
    [IO.Path]::GetFullPath($OutputDirectory)
} else {
    Join-Path $toolDirectory ('artifacts\runs\probe_{0}_{1}' -f
        [IO.Path]::GetFileNameWithoutExtension($source), $TargetFrame)
}
[IO.Directory]::CreateDirectory($out) | Out-Null

$original = $null
$rebuild = $null
$drivers = @()

function Get-ReplayTerminalResult(
        [string]$Phase,
        [Diagnostics.Process]$OriginalProcess,
        [Diagnostics.Process]$RebuildProcess) {
    $OriginalProcess.Refresh()
    $RebuildProcess.Refresh()
    $originalAlive = -not $OriginalProcess.HasExited
    $rebuildAlive = -not $RebuildProcess.HasExited
    if ($originalAlive -and $rebuildAlive) {
        return $null
    }
    return [pscustomobject]@{
        replay = $source
        replay_index = $ReplayIndex
        target_frame = $TargetFrame
        pass = $null
        terminal = $true
        reason = ('replay ended before target suspension; ' +
            'phase={0} original_alive={1} rebuild_alive={2}' -f
            $Phase, $originalAlive, $rebuildAlive)
        first_difference = $null
        result_path = $null
        detail_path = $null
    }
}

try {
    $launchText = & (Join-Path $toolDirectory 'launch_replay_pair.ps1') `
        -ReplayName $temporaryReplayName -RepositoryRoot $repositoryRoot `
        -OriginalExecutable $OriginalExecutable `
        -RebuildExecutable $RebuildExecutable `
        -ExpectedRebuildSha256 $layout.sha256
    $pair = ($launchText -join "`n") | ConvertFrom-Json
    $original = Get-Process -Id ([int]$pair.original_pid)
    $rebuild = Get-Process -Id ([int]$pair.rebuild_pid)
    $rebuildBase = [Convert]::ToInt64(
        ($pair.rebuild_base -replace '^0x', ''), 16)
    $rebuildFrameAddress = $rebuildBase + $loopRva + 0x1DC

    & python (Join-Path $toolDirectory 'replay_pair_control.py') pace `
        $original.Id $rebuild.Id ('0x{0:X}' -f $rebuildBase) `
        ('0x{0:X}' -f $loopRva) 1 0 | Out-Null

    $fastTarget = [Math]::Max(1, $TargetFrame - 20)
    $originalFastOut = Join-Path $out 'original-fast.out'
    $originalFastErr = Join-Path $out 'original-fast.err'
    $rebuildFastOut = Join-Path $out 'rebuild-fast.out'
    $rebuildFastErr = Join-Path $out 'rebuild-fast.err'
    $drivers = @(
        Start-Process -FilePath python -ArgumentList @(
            (Join-Path $toolDirectory 'resume_to_frame.py'),
            [string]$original.Id, '0x007071A4', [string]$fastTarget,
            [string]$TimeoutSeconds, '0') -WorkingDirectory $repositoryRoot `
            -RedirectStandardOutput $originalFastOut `
            -RedirectStandardError $originalFastErr `
            -WindowStyle Hidden -PassThru
        Start-Process -FilePath python -ArgumentList @(
            (Join-Path $toolDirectory 'resume_to_frame.py'),
            [string]$rebuild.Id, ('0x{0:X}' -f $rebuildFrameAddress),
            [string]$fastTarget, [string]$TimeoutSeconds, '0') `
            -WorkingDirectory $repositoryRoot -RedirectStandardOutput $rebuildFastOut `
            -RedirectStandardError $rebuildFastErr `
            -WindowStyle Hidden -PassThru
    )
    Wait-Process -Id $drivers.Id
    $terminal = Get-ReplayTerminalResult 'fast' $original $rebuild
    if ($null -ne $terminal) {
        $terminal | ConvertTo-Json -Depth 8
        return
    }
    & python (Join-Path $toolDirectory 'replay_pair_control.py') pace `
        $original.Id $rebuild.Id ('0x{0:X}' -f $rebuildBase) `
        ('0x{0:X}' -f $loopRva) 100 100 | Out-Null

    $originalStableOut = Join-Path $out 'original-stable.out'
    $originalStableErr = Join-Path $out 'original-stable.err'
    $rebuildStableOut = Join-Path $out 'rebuild-stable.out'
    $rebuildStableErr = Join-Path $out 'rebuild-stable.err'
    $drivers = @(
        Start-Process -FilePath python -ArgumentList @(
            (Join-Path $toolDirectory 'resume_to_frame.py'),
            [string]$original.Id, '0x007071A4', [string]$TargetFrame,
            '30', '0.02') -WorkingDirectory $repositoryRoot `
            -RedirectStandardOutput $originalStableOut `
            -RedirectStandardError $originalStableErr `
            -WindowStyle Hidden -PassThru
        Start-Process -FilePath python -ArgumentList @(
            (Join-Path $toolDirectory 'resume_to_frame.py'),
            [string]$rebuild.Id, ('0x{0:X}' -f $rebuildFrameAddress),
            [string]$TargetFrame, '30', '0.02') -WorkingDirectory $repositoryRoot `
            -RedirectStandardOutput $rebuildStableOut `
            -RedirectStandardError $rebuildStableErr `
            -WindowStyle Hidden -PassThru
    )
    Wait-Process -Id $drivers.Id
    $terminal = Get-ReplayTerminalResult 'stable' $original $rebuild
    if ($null -ne $terminal) {
        $terminal | ConvertTo-Json -Depth 8
        return
    }
    $frames = (& python (Join-Path $toolDirectory 'replay_pair_control.py') frames `
        $original.Id $rebuild.Id ('0x{0:X}' -f $rebuildBase) `
        ('0x{0:X}' -f $loopRva) | ConvertFrom-Json)
    if ([int]$frames.original -ne $TargetFrame -or
        [int]$frames.rebuild -ne $TargetFrame) {
        throw "Stable frame mismatch: original=$($frames.original), rebuild=$($frames.rebuild), target=$TargetFrame"
    }

    $originalActionDefinition = $null
    if ($OriginalActionDefinitionIndex -ge 0) {
        $originalActionDefinition = (& python `
            (Join-Path $toolDirectory 'read_original_action_definition.py') `
            $original.Id $OriginalActionDefinitionIndex | ConvertFrom-Json)
    }

    $originalDumpPath = ''
    if ($OriginalDumpAddress -ne 0 -and $OriginalDumpSize -gt 0) {
        $originalDumpPath = Join-Path $out 'original-memory.bin'
        & (Join-Path $toolDirectory 'dump_process_range.ps1') `
            -ProcessId $original.Id -Address $OriginalDumpAddress `
            -Size $OriginalDumpSize -OutputPath $originalDumpPath | Out-Null
    }

    $resultPath = Join-Path $out 'result.json'
    $journalPath = Join-Path $out 'journal.jsonl'
    $stopPath = Join-Path $out 'unused.stop'
    $env:NEXTDIV_CAPTURE_SUSPENDED_ONCE = '1'
    try {
        & python (Join-Path $toolDirectory 'compare_process_state.py') `
            $original.Id $rebuild.Id ('0x{0:X}' -f $rebuildBase) `
            $layoutPath $resultPath $journalPath $stopPath 90
        $auditExit = $LASTEXITCODE
    } finally {
        Remove-Item Env:NEXTDIV_CAPTURE_SUSPENDED_ONCE -ErrorAction SilentlyContinue
    }
    if (-not (Test-Path -LiteralPath $resultPath)) {
        throw "One-shot audit produced no result (exit $auditExit)."
    }
    $result = Get-Content -LiteralPath $resultPath -Raw | ConvertFrom-Json
    [pscustomobject]@{
        replay = $source
        replay_index = $ReplayIndex
        target_frame = $TargetFrame
        pass = [bool]$result.pass
        reason = $result.reason
        first_difference = $result.first_difference
        result_path = $resultPath
        detail_path = $result.detail_path
        original_dump_path = $originalDumpPath
        original_action_definition = $originalActionDefinition
        original_pid = $original.Id
        rebuild_pid = $rebuild.Id
        rebuild_base = ('0x{0:X}' -f $rebuildBase)
    } | ConvertTo-Json -Depth 8
} finally {
    foreach ($driver in $drivers) {
        if ($null -ne $driver -and -not $driver.HasExited) {
            Stop-Process -Id $driver.Id -Force -ErrorAction SilentlyContinue
        }
    }
    foreach ($process in @($original, $rebuild)) {
        if ($null -ne $process -and -not $process.HasExited) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        }
    }
    Remove-Item -LiteralPath $temporaryReplay -Force `
        -ErrorAction SilentlyContinue
}
