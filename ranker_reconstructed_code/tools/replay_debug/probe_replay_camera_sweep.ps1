param(
    [Parameter(Mandatory = $true)][string]$ReplayPath,
    [Parameter(Mandatory = $true)][int]$StartFrame,
    [Parameter(Mandatory = $true)][string[]]$CameraPoints,
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [string]$RepositoryRoot = '',
    [string]$LayoutPath = '',
    [string]$OriginalExecutable = 'RankerOCPV_Win\ranker.exe',
    [string]$RebuildExecutable = 'RankerOCPV_Win\ranker_rebuild.exe',
    [int]$TimeoutSeconds = 360,
    [switch]$WorldBeforeOverlay
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
$sourceCandidate = if ([IO.Path]::IsPathRooted($ReplayPath)) {
    $ReplayPath
} else {
    Join-Path $repositoryRoot $ReplayPath
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
$runtimeRva = [Convert]::ToInt64(($layout.runtime_rva -replace '^0x', ''), 16)
$overlayRva = [Convert]::ToInt64(($layout.overlay_rva -replace '^0x', ''), 16)
$renderCompositeRva = [Convert]::ToInt64((
    $layout.render_gameplay_frame_composite_rva -replace '^0x', ''), 16)
$spriteRenderStateRva = [Convert]::ToInt64(
    ($layout.sprite_render_state_rva -replace '^0x', ''), 16)
$worldRenderCheckpointRva = [Convert]::ToInt64(
    ($layout.world_render_checkpoint_rva -replace '^0x', ''), 16)
$simulationFrameOffset = [Convert]::ToInt64(
    ($layout.loop_layout.simulation_frame -replace '^0x', ''), 16)
$worldViewportHeightOffset = [Convert]::ToInt64((
    $layout.overlay_layout.world_viewport_height -replace '^0x', ''), 16)
$gameplaySoundOffset = [Convert]::ToInt64((
    $layout.gameplay_sound_offset -replace '^0x', ''), 16)
$variantSeedOffset = [Convert]::ToInt64((
    $layout.gameplay_sound_layout.variant_seed -replace '^0x', ''), 16)

$cameras = @()
foreach ($point in $CameraPoints) {
    if ($point -notmatch '^\s*(-?\d+)\s*,\s*(-?\d+)\s*$') {
        throw "Camera point must be X,Y: $point"
    }
    $cameras += ,@([int]$Matches[1], [int]$Matches[2])
}
if ($cameras.Count -eq 0) {
    throw 'Specify at least one camera point.'
}

$out = [IO.Path]::GetFullPath($OutputDirectory)
$artifactRoot = [IO.Path]::GetFullPath(
    (Join-Path $toolDirectory 'artifacts')).TrimEnd('\') + '\'
if (-not $out.StartsWith($artifactRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Output directory must be below replay_debug/artifacts: $out"
}
[IO.Directory]::CreateDirectory($out) | Out-Null

$original = $null
$rebuild = $null
$drivers = @()

function Resume-PairToFrame([int]$Frame, [string]$Phase, [double]$Delay) {
    $script:drivers = @(
        Start-Process python -ArgumentList @(
            (Join-Path $toolDirectory 'resume_to_frame.py'),
            [string]$original.Id, '0x007071A4', [string]$Frame,
            [string]$TimeoutSeconds, [string]$Delay) `
            -WorkingDirectory $repositoryRoot `
            -RedirectStandardOutput (Join-Path $out "original-$Phase-$Frame.out") `
            -RedirectStandardError (Join-Path $out "original-$Phase-$Frame.err") `
            -WindowStyle Hidden -PassThru
        Start-Process python -ArgumentList @(
            (Join-Path $toolDirectory 'resume_to_frame.py'),
            [string]$rebuild.Id, ('0x{0:X}' -f $rebuildFrameAddress),
            [string]$Frame, [string]$TimeoutSeconds, [string]$Delay) `
            -WorkingDirectory $repositoryRoot `
            -RedirectStandardOutput (Join-Path $out "rebuild-$Phase-$Frame.out") `
            -RedirectStandardError (Join-Path $out "rebuild-$Phase-$Frame.err") `
            -WindowStyle Hidden -PassThru
    )
    Wait-Process -Id $script:drivers.Id
    foreach ($driver in $script:drivers) {
        $driver.Refresh()
        if ($null -ne $driver.ExitCode -and $driver.ExitCode -ne 0) {
            throw "Frame driver failed in $Phase at $Frame (exit $($driver.ExitCode))."
        }
    }
    $frames = (& python (Join-Path $toolDirectory 'replay_pair_control.py') frames `
        $original.Id $rebuild.Id ('0x{0:X}' -f $rebuildBase) `
        ('0x{0:X}' -f $loopRva) $layoutPath | ConvertFrom-Json)
    if ([int]$frames.original -ne $Frame -or [int]$frames.rebuild -ne $Frame) {
        throw "Frame mismatch in ${Phase}: original=$($frames.original) rebuild=$($frames.rebuild) target=$Frame"
    }
}

function Set-PairCamera([int]$X, [int]$Y) {
    & python (Join-Path $toolDirectory 'replay_pair_control.py') stabilize `
        $original.Id $rebuild.Id ('0x{0:X}' -f $rebuildBase) `
        ('0x{0:X}' -f $loopRva) $layoutPath $X $Y | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to set camera $X,$Y."
    }
}

function Compare-SuspendedState(
        [int]$Frame, [bool]$IgnoreRenderQueues = $false) {
    $resultPath = Join-Path $out "state-result-$Frame.json"
    $journalPath = Join-Path $out "state-journal-$Frame.jsonl"
    $stopPath = Join-Path $out "state-unused-$Frame.stop"
    $env:NEXTDIV_CAPTURE_SUSPENDED_ONCE = '1'
    if ($IgnoreRenderQueues) {
        $env:NEXTDIV_IGNORE_RENDER_QUEUES = '1'
    }
    try {
        & python (Join-Path $toolDirectory 'compare_process_state.py') `
            $original.Id $rebuild.Id ('0x{0:X}' -f $rebuildBase) `
            $layoutPath $resultPath $journalPath $stopPath 90 | Out-Null
    }
    finally {
        Remove-Item Env:NEXTDIV_CAPTURE_SUSPENDED_ONCE `
            -ErrorAction SilentlyContinue
        Remove-Item Env:NEXTDIV_IGNORE_RENDER_QUEUES `
            -ErrorAction SilentlyContinue
    }
    if (-not (Test-Path -LiteralPath $resultPath)) {
        throw "State comparison at $Frame produced no result."
    }
    return Get-Content -LiteralPath $resultPath -Raw | ConvertFrom-Json
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

    Set-PairCamera $cameras[0][0] $cameras[0][1]
    & python (Join-Path $toolDirectory 'replay_pair_control.py') fast `
        $original.Id $rebuild.Id ('0x{0:X}' -f $rebuildBase) `
        ('0x{0:X}' -f $loopRva) $layoutPath | Out-Null
    $bootstrapFrame = [Math]::Min(20, [Math]::Max(1, $StartFrame - 5))
    Resume-PairToFrame $bootstrapFrame 'bootstrap' 0
    & python (Join-Path $toolDirectory 'replay_pair_control.py') fast `
        $original.Id $rebuild.Id ('0x{0:X}' -f $rebuildBase) `
        ('0x{0:X}' -f $loopRva) $layoutPath | Out-Null
    $fastFrame = [Math]::Max($bootstrapFrame, $StartFrame - 5)
    if ($fastFrame -gt $bootstrapFrame) {
        Resume-PairToFrame $fastFrame 'fast' 0
    }
    & python (Join-Path $toolDirectory 'replay_pair_control.py') pace `
        $original.Id $rebuild.Id ('0x{0:X}' -f $rebuildBase) `
        ('0x{0:X}' -f $loopRva) 500 500 $layoutPath | Out-Null
    Set-PairCamera $cameras[0][0] $cameras[0][1]
    Resume-PairToFrame $StartFrame 'stable' 0.02
    # Session startup republishes the serialized rebuild camera during its
    # first resumed frames. Reapply the diagnostic viewpoint at the suspended
    # hand-off, just as probe_replay.ps1 does after its bootstrap seek.
    Set-PairCamera $cameras[0][0] $cameras[0][1]
    # This first suspension is at the simulation-loop hand-off, not at the
    # known completed-render checkpoint used below.  The original may be
    # interrupted while its in-place world sort is only partially complete,
    # so compare simulation state here and reserve both render queues for the
    # completed-render comparisons.
    $initialState = Compare-SuspendedState $StartFrame $true
    if (-not [bool]$initialState.pass) {
        throw "Initial state differs at frame ${StartFrame}: $($initialState.first_difference)"
    }

    $worldHeightDump = Join-Path $out 'rebuild-world-viewport-height.bin'
    & (Join-Path $toolDirectory 'dump_process_range.ps1') `
        -ProcessId $rebuild.Id `
        -Address ($rebuildBase + $overlayRva + $worldViewportHeightOffset) `
        -Size 4 -OutputPath $worldHeightDump | Out-Null
    $worldHeight = [BitConverter]::ToUInt32(
        [IO.File]::ReadAllBytes($worldHeightDump), 0)
    if ($worldHeight -lt 1 -or $worldHeight -gt 600) {
        throw "Invalid world viewport height: $worldHeight"
    }

    for ($index = 0; $index -lt $cameras.Count; ++$index) {
        $cameraX = $cameras[$index][0]
        $cameraY = $cameras[$index][1]
        Set-PairCamera $cameraX $cameraY
        $presentationRngState = & python `
            (Join-Path $toolDirectory 'replay_pair_control.py') `
            presentation-rng $original.Id $rebuild.Id `
            ('0x{0:X}' -f $rebuildBase) ('0x{0:X}' -f $loopRva) `
            $layoutPath
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to align presentation RNG at camera $cameraX,$cameraY."
        }
        $presentationSeed = [uint32]((
            ($presentationRngState -join "`n") | ConvertFrom-Json).original)
        $targetFrame = $StartFrame + $index + 1

        $stem = ('camera-{0:D2}-{1}-{2}-f{3}' -f
            $index, $cameraX, $cameraY, $targetFrame)
        $originalRgb = Join-Path $out "$stem-original.rgb565"
        $rebuildRgb = Join-Path $out "$stem-rebuild.rgb565"
        $comparison = Join-Path $out "$stem-comparison.json"
        $originalCaptureOut = Join-Path $out "$stem-original.out"
        $originalCaptureErr = Join-Path $out "$stem-original.err"
        $rebuildCaptureOut = Join-Path $out "$stem-rebuild.out"
        $rebuildCaptureErr = Join-Path $out "$stem-rebuild.err"
        $originalCaptureMode = if ($WorldBeforeOverlay) {
            '--world-before-overlay'
        } else {
            '--composite-once'
        }
        $rebuildCaptureRva = if ($WorldBeforeOverlay) {
            $worldRenderCheckpointRva
        } else {
            $renderCompositeRva
        }
        $rebuildCaptureMode = if ($WorldBeforeOverlay) {
            '--entry-checkpoint'
        } else {
            '--camera-address'
        }
        $drivers = @(
            Start-Process python -ArgumentList @(
                (Join-Path $toolDirectory 'capture_original_rgb565_at_breakpoint.py'),
                [string]$original.Id, [string]$targetFrame, $originalRgb,
                '--surface-only', $originalCaptureMode, '--keep-alive',
                '--presentation-seed',
                ('0x{0:X}' -f [uint32]$presentationSeed)) `
                -WorkingDirectory $repositoryRoot `
                -RedirectStandardOutput $originalCaptureOut `
                -RedirectStandardError $originalCaptureErr `
                -WindowStyle Hidden -PassThru
            Start-Process python -ArgumentList @(
                (Join-Path $toolDirectory 'capture_rebuild_rgb565_at_breakpoint.py'),
                [string]$rebuild.Id, ('0x{0:X}' -f $rebuildBase),
                ('0x{0:X}' -f $rebuildFrameAddress), [string]$targetFrame,
                ('0x{0:X}' -f $rebuildCaptureRva),
                ('0x{0:X}' -f $spriteRenderStateRva), $rebuildRgb,
                $rebuildCaptureMode, ('0x{0:X}' -f (
                    $rebuildBase + $overlayRva +
                    [Convert]::ToInt64((
                        $layout.overlay_layout.camera_x -replace '^0x', ''), 16))),
                '--keep-alive', '--presentation-seed-address',
                ('0x{0:X}' -f (
                    $rebuildBase + $runtimeRva + $gameplaySoundOffset +
                    $variantSeedOffset)), '--presentation-seed',
                ('0x{0:X}' -f [uint32]$presentationSeed)) `
                -WorkingDirectory $repositoryRoot `
                -RedirectStandardOutput $rebuildCaptureOut `
                -RedirectStandardError $rebuildCaptureErr `
                -WindowStyle Hidden -PassThru
        )
        Wait-Process -Id $drivers.Id
        foreach ($driver in $drivers) {
            $driver.Refresh()
            if ($null -ne $driver.ExitCode -and $driver.ExitCode -ne 0) {
                $errorPath = if ($driver.Id -eq $drivers[0].Id) {
                    $originalCaptureErr
                } else { $rebuildCaptureErr }
                $errorText = if (Test-Path -LiteralPath $errorPath) {
                    Get-Content -LiteralPath $errorPath -Raw
                } else { '' }
                throw "Camera capture failed at $cameraX,$cameraY (exit $($driver.ExitCode)): $errorText"
            }
        }
        $originalMetadata = Get-Content -LiteralPath $originalCaptureOut -Raw |
            ConvertFrom-Json
        $rebuildMetadata = Get-Content -LiteralPath $rebuildCaptureOut -Raw |
            ConvertFrom-Json
        if ($originalMetadata.width -ne $rebuildMetadata.width -or
            $originalMetadata.height -ne $rebuildMetadata.height) {
            throw "Surface dimension mismatch at camera $cameraX,$cameraY."
        }
        if ([int]$originalMetadata.frame -ne [int]$rebuildMetadata.frame) {
            throw "Captured frame mismatch at camera $cameraX,$cameraY."
        }
        & python (Join-Path $toolDirectory 'compare_rgb565_dumps.py') `
            $originalRgb $rebuildRgb --width ([int]$originalMetadata.width) `
            --height ([int]$originalMetadata.height) --world-height $worldHeight `
            --output-json $comparison | Out-Null
        if ($LASTEXITCODE -gt 1) {
            throw "RGB565 comparison failed at camera $cameraX,$cameraY."
        }
        $pixels = Get-Content -LiteralPath $comparison -Raw | ConvertFrom-Json
        $state = Compare-SuspendedState $targetFrame
        [pscustomobject]@{
            camera_index = $index
            camera = @($cameraX, $cameraY)
            suspended_frame = $targetFrame
            state_pass = [bool]$state.pass
            first_difference = $state.first_difference
            compared = $pixels.compared
            mismatch = $pixels.mismatch
            original_sha256 = $pixels.original_sha256
            rebuild_sha256 = $pixels.rebuild_sha256
            comparison_path = $comparison
        } | ConvertTo-Json -Compress -Depth 8
        if (-not [bool]$state.pass -or [int64]$pixels.mismatch -ne 0) {
            break
        }
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
