param(
    [string]$ReplayPath = 'RankerOCPV_Win\Replays\DebugReplay_14.ply',
    [int]$FastFrame = 17993,
    [int]$EndFrame = 18015,
    [string]$OutputDirectory = ''
)

$ErrorActionPreference = 'Stop'
$toolDirectory = $PSScriptRoot
$repositoryRoot = (Resolve-Path -LiteralPath (Join-Path $toolDirectory '..\..\..')).Path
$source = (Resolve-Path -LiteralPath (Join-Path $repositoryRoot $ReplayPath)).Path
$replayRoot = (Resolve-Path -LiteralPath (
    Join-Path $repositoryRoot 'RankerOCPV_Win\Replays')).Path
$temporaryReplay = Join-Path $replayRoot 'DebugReplay_Audit.ply'
$out = if ($OutputDirectory) {
    [IO.Path]::GetFullPath($OutputDirectory)
} else {
    Join-Path $toolDirectory 'artifacts\runs\completed_type_write_trace'
}
[IO.Directory]::CreateDirectory($out) | Out-Null
Copy-Item -LiteralPath $source -Destination $temporaryReplay -Force

$layoutPath = Join-Path $toolDirectory 'artifacts\current_layout.json'
$layout = Get-Content -LiteralPath $layoutPath -Raw | ConvertFrom-Json
$expectedHash = $layout.sha256
$original = $null
$rebuild = $null
try {
    $launchText = & (Join-Path $toolDirectory 'launch_replay_pair.ps1') `
        -ReplayName 'DebugReplay_Audit.ply' -RepositoryRoot $repositoryRoot `
        -OriginalExecutable 'RankerOCPV_Win\ranker.exe' `
        -RebuildExecutable 'RankerOCPV_Win\ranker_rebuild.exe' `
        -ExpectedRebuildSha256 $expectedHash
    $pair = ($launchText -join "`n") | ConvertFrom-Json
    $original = Get-Process -Id ([int]$pair.original_pid)
    $rebuild = Get-Process -Id ([int]$pair.rebuild_pid)
    $rebuildBase = [Convert]::ToInt64(
        ($pair.rebuild_base -replace '^0x', ''), 16)
    $loopRva = [Convert]::ToInt64(($layout.loop_rva -replace '^0x', ''), 16)
    $frameOffset = [Convert]::ToInt64(
        ($layout.loop_layout.simulation_frame -replace '^0x', ''), 16)
    $rebuildFrameAddress = $rebuildBase + $loopRva + $frameOffset

    & python (Join-Path $toolDirectory 'replay_pair_control.py') fast `
        $original.Id $rebuild.Id ('0x{0:X}' -f $rebuildBase) `
        ('0x{0:X}' -f $loopRva) $layoutPath | Out-Null

    $drivers = @(
        Start-Process -FilePath python -ArgumentList @(
            (Join-Path $toolDirectory 'resume_to_frame.py'),
            [string]$original.Id, '0x007071A4', '20', '30', '0') `
            -WindowStyle Hidden -PassThru
        Start-Process -FilePath python -ArgumentList @(
            (Join-Path $toolDirectory 'resume_to_frame.py'),
            [string]$rebuild.Id, ('0x{0:X}' -f $rebuildFrameAddress),
            '20', '30', '0') -WindowStyle Hidden -PassThru
    )
    Wait-Process -Id $drivers.Id

    & python (Join-Path $toolDirectory 'replay_pair_control.py') fast `
        $original.Id $rebuild.Id ('0x{0:X}' -f $rebuildBase) `
        ('0x{0:X}' -f $loopRva) $layoutPath | Out-Null

    $drivers = @(
        Start-Process -FilePath python -ArgumentList @(
            (Join-Path $toolDirectory 'resume_to_frame.py'),
            [string]$original.Id, '0x007071A4', [string]$FastFrame,
            '360', '0') -WindowStyle Hidden -PassThru
        Start-Process -FilePath python -ArgumentList @(
            (Join-Path $toolDirectory 'resume_to_frame.py'),
            [string]$rebuild.Id, ('0x{0:X}' -f $rebuildFrameAddress),
            [string]$FastFrame, '360', '0') -WindowStyle Hidden -PassThru
    )
    Wait-Process -Id $drivers.Id

    & python (Join-Path $toolDirectory 'replay_pair_control.py') pace `
        $original.Id $rebuild.Id ('0x{0:X}' -f $rebuildBase) `
        ('0x{0:X}' -f $loopRva) 100 100 $layoutPath | Out-Null

    $traceOut = Join-Path $out 'trace.out'
    $traceErr = Join-Path $out 'trace.err'
    $rebuildOut = Join-Path $out 'rebuild.out'
    $rebuildErr = Join-Path $out 'rebuild.err'
    $trace = Start-Process -FilePath python -ArgumentList @(
        (Join-Path $toolDirectory 'trace_original_completed_type_write.py'),
        [string]$original.Id, [string]$FastFrame, [string]$EndFrame,
        (Join-Path $out 'trace.json')) -WorkingDirectory $repositoryRoot `
        -RedirectStandardOutput $traceOut -RedirectStandardError $traceErr `
        -WindowStyle Hidden -PassThru
    $driver = Start-Process -FilePath python -ArgumentList @(
        (Join-Path $toolDirectory 'resume_to_frame.py'),
        [string]$rebuild.Id, ('0x{0:X}' -f $rebuildFrameAddress),
        [string]($EndFrame + 1), '60', '0.02') `
        -WorkingDirectory $repositoryRoot `
        -RedirectStandardOutput $rebuildOut `
        -RedirectStandardError $rebuildErr -WindowStyle Hidden -PassThru
    Wait-Process -Id @($trace.Id, $driver.Id)
    Get-Content -LiteralPath (Join-Path $out 'trace.json') -Raw
    if ((Get-Item -LiteralPath $traceErr).Length -gt 0) {
        Get-Content -LiteralPath $traceErr
    }
}
finally {
    foreach ($process in @($original, $rebuild)) {
        if ($null -ne $process) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        }
    }
    Remove-Item -LiteralPath $temporaryReplay -Force -ErrorAction SilentlyContinue
}
