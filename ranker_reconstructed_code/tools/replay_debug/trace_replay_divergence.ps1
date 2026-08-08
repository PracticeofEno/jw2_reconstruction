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
    [int]$TraceIntervalMs = 100
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
$out = [IO.Path]::GetFullPath($OutputDirectory)
[IO.Directory]::CreateDirectory($out) | Out-Null

$original = $null
$rebuild = $null
$drivers = @()
$audit = $null
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
    $rebuildFrameAddress = $rebuildBase + $loopRva + 0x1DC

    & python (Join-Path $toolDirectory 'replay_pair_control.py') pace `
        $original.Id $rebuild.Id ('0x{0:X}' -f $rebuildBase) `
        ('0x{0:X}' -f $loopRva) 1 0 | Out-Null

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

    & python (Join-Path $toolDirectory 'replay_pair_control.py') pace `
        $original.Id $rebuild.Id ('0x{0:X}' -f $rebuildBase) `
        ('0x{0:X}' -f $loopRva) $TraceIntervalMs $TraceIntervalMs | Out-Null
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
