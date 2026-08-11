param(
    [int]$ReplayIndex = -1,
    [string]$ReplayPath = '',
    [Parameter(Mandatory = $true)][int[]]$Checkpoints,
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [string]$RepositoryRoot = '',
    [string]$LayoutPath = '',
    [string]$OriginalExecutable = 'RankerOCPV_Win\ranker.exe',
    [string]$RebuildExecutable = 'RankerOCPV_Win\ranker_rebuild.exe',
    [int]$TimeoutSeconds = 600
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
$temporaryReplay = Join-Path $replayRoot 'DebugReplay_Audit.ply'
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

function Get-ReplayTerminalResult(
        [int]$Frame, [string]$Phase,
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
        frame = $Frame
        pass = $null
        terminal = $true
        reason = ('replay ended before checkpoint suspension; ' +
            'phase={0} original_alive={1} rebuild_alive={2}' -f
            $Phase, $originalAlive, $rebuildAlive)
        first_difference = $null
        result_path = $null
    }
}

try {
    $launchText = & (Join-Path $toolDirectory 'launch_replay_pair.ps1') `
        -ReplayName 'DebugReplay_Audit.ply' `
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

    $lastFrame = 0
    foreach ($targetFrame in ($Checkpoints | Sort-Object -Unique)) {
        if ($targetFrame -le $lastFrame) {
            continue
        }
        & python (Join-Path $toolDirectory 'replay_pair_control.py') pace `
            $original.Id $rebuild.Id ('0x{0:X}' -f $rebuildBase) `
            ('0x{0:X}' -f $loopRva) 1 0 $layoutPath | Out-Null

        $fastTarget = [Math]::Max($lastFrame + 1, $targetFrame - 20)
        $drivers = @(
            Start-Process python -ArgumentList @(
                (Join-Path $toolDirectory 'resume_to_frame.py'),
                [string]$original.Id, '0x007071A4', [string]$fastTarget,
                [string]$TimeoutSeconds, '0') -WorkingDirectory $repositoryRoot `
                -RedirectStandardOutput (Join-Path $out "original-fast-$targetFrame.out") `
                -RedirectStandardError (Join-Path $out "original-fast-$targetFrame.err") `
                -WindowStyle Hidden -PassThru
            Start-Process python -ArgumentList @(
                (Join-Path $toolDirectory 'resume_to_frame.py'),
                [string]$rebuild.Id, ('0x{0:X}' -f $rebuildFrameAddress),
                [string]$fastTarget, [string]$TimeoutSeconds, '0') `
                -WorkingDirectory $repositoryRoot `
                -RedirectStandardOutput (Join-Path $out "rebuild-fast-$targetFrame.out") `
                -RedirectStandardError (Join-Path $out "rebuild-fast-$targetFrame.err") `
                -WindowStyle Hidden -PassThru
        )
        Wait-Process -Id $drivers.Id
        $terminal = Get-ReplayTerminalResult $targetFrame 'fast' $original $rebuild
        if ($null -ne $terminal) {
            $terminal | ConvertTo-Json -Compress -Depth 8
            break
        }
        & python (Join-Path $toolDirectory 'replay_pair_control.py') pace `
            $original.Id $rebuild.Id ('0x{0:X}' -f $rebuildBase) `
            ('0x{0:X}' -f $loopRva) 500 500 $layoutPath | Out-Null
        $drivers = @(
            Start-Process python -ArgumentList @(
                (Join-Path $toolDirectory 'resume_to_frame.py'),
                [string]$original.Id, '0x007071A4', [string]$targetFrame,
                '30', '0.02') -WorkingDirectory $repositoryRoot `
                -RedirectStandardOutput (Join-Path $out "original-stable-$targetFrame.out") `
                -RedirectStandardError (Join-Path $out "original-stable-$targetFrame.err") `
                -WindowStyle Hidden -PassThru
            Start-Process python -ArgumentList @(
                (Join-Path $toolDirectory 'resume_to_frame.py'),
                [string]$rebuild.Id, ('0x{0:X}' -f $rebuildFrameAddress),
                [string]$targetFrame, '30', '0.02') -WorkingDirectory $repositoryRoot `
                -RedirectStandardOutput (Join-Path $out "rebuild-stable-$targetFrame.out") `
                -RedirectStandardError (Join-Path $out "rebuild-stable-$targetFrame.err") `
                -WindowStyle Hidden -PassThru
        )
        Wait-Process -Id $drivers.Id
        $terminal = Get-ReplayTerminalResult $targetFrame 'stable' $original $rebuild
        if ($null -ne $terminal) {
            $terminal | ConvertTo-Json -Compress -Depth 8
            break
        }
        $frames = (& python (Join-Path $toolDirectory 'replay_pair_control.py') frames `
            $original.Id $rebuild.Id ('0x{0:X}' -f $rebuildBase) `
            ('0x{0:X}' -f $loopRva) $layoutPath | ConvertFrom-Json)
        if ([int]$frames.original -ne $targetFrame -or
            [int]$frames.rebuild -ne $targetFrame) {
            throw "Stable frame mismatch: original=$($frames.original), rebuild=$($frames.rebuild), target=$targetFrame"
        }

        $resultPath = Join-Path $out "result-$targetFrame.json"
        $journalPath = Join-Path $out "journal-$targetFrame.jsonl"
        $stopPath = Join-Path $out "unused-$targetFrame.stop"
        $env:NEXTDIV_CAPTURE_SUSPENDED_ONCE = '1'
        try {
            & python (Join-Path $toolDirectory 'compare_process_state.py') `
                $original.Id $rebuild.Id ('0x{0:X}' -f $rebuildBase) `
                $layoutPath $resultPath $journalPath $stopPath 90
        }
        finally {
            Remove-Item Env:NEXTDIV_CAPTURE_SUSPENDED_ONCE `
                -ErrorAction SilentlyContinue
        }
        if (-not (Test-Path -LiteralPath $resultPath)) {
            throw "Checkpoint $targetFrame produced no result."
        }
        $result = Get-Content -LiteralPath $resultPath -Raw | ConvertFrom-Json
        [pscustomobject]@{
            replay = $source
            replay_index = $ReplayIndex
            frame = $targetFrame
            pass = [bool]$result.pass
            reason = $result.reason
            first_difference = $result.first_difference
            result_path = $resultPath
        } | ConvertTo-Json -Compress -Depth 8
        if (-not [bool]$result.pass) {
            break
        }
        $lastFrame = $targetFrame
    }
}
finally {
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
