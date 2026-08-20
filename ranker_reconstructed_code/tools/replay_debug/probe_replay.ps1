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
    [int]$OriginalDumpSize = 0,
    [Int64]$RebuildDumpRva = 0,
    [int]$RebuildDumpSize = 0,
    [switch]$DumpVisibilityPair,
    [switch]$StabilizeViewport,
    [int]$ViewOwner = -1,
    [int]$CameraX = [int]::MinValue,
    [int]$CameraY = [int]::MinValue,
    [int]$TraceOriginalUnitSpriteSlot = -1,
    [int]$TraceRebuildSpriteX = [int]::MinValue,
    [int]$TraceRebuildSpriteY = [int]::MinValue,
    [int]$TraceRebuildResourceType = -1,
    [int]$TraceRebuildResourceGroup = -1,
    [switch]$TraceLowHealthOverlayCalls,
    [switch]$TraceToken1ShadowSpriteCalls,
    [switch]$TraceUnitRampSpriteCalls,
    [switch]$TraceProjectileTrailLines,
    [switch]$TraceLinkedVisibilityCalls,
    [switch]$AlignPresentationRng,
    [switch]$CaptureRgb565Pair,
    [switch]$CaptureSingleCompositePair,
    [int]$WorldBarInjectionSlot = -1,
    [int]$WorldBarInjectionFrame = -1,
    [string]$CapturePngDirectory = '',
    [string]$CapturePngStem = ''
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
$runtimeRva = [Convert]::ToInt64(($layout.runtime_rva -replace '^0x', ''), 16)
$overlayRva = [Convert]::ToInt64(($layout.overlay_rva -replace '^0x', ''), 16)
$simulationFrameOffset = [Convert]::ToInt64(
    ($layout.loop_layout.simulation_frame -replace '^0x', ''), 16)
$renderCompositeRva = if ($null -ne $layout.render_gameplay_frame_composite_rva) {
    [Convert]::ToInt64(
        ($layout.render_gameplay_frame_composite_rva -replace '^0x', ''), 16)
} else { 0 }
$syncGameplayVisibilityRva = if ($null -ne $layout.sync_gameplay_visibility_rva) {
    [Convert]::ToInt64(
        ($layout.sync_gameplay_visibility_rva -replace '^0x', ''), 16)
} else { 0 }
$applyLinkedUnitVisibilityRva = if (
    $null -ne $layout.apply_linked_unit_visibility_rva) {
    [Convert]::ToInt64(
        ($layout.apply_linked_unit_visibility_rva -replace '^0x', ''), 16)
} else { 0 }
$drawResourceSpriteModeRva = if ($null -ne $layout.draw_resource_sprite_mode_rva) {
    [Convert]::ToInt64(
        ($layout.draw_resource_sprite_mode_rva -replace '^0x', ''), 16)
} else { 0 }
$drawResourceSpriteToken1ShadowRva = if (
    $null -ne $layout.draw_resource_sprite_token1_shadow_rva) {
    [Convert]::ToInt64((
        $layout.draw_resource_sprite_token1_shadow_rva -replace '^0x', ''), 16)
} else { 0 }
$drawResourceSpriteUnitRampToken1ShadowRva = if (
    $null -ne $layout.draw_resource_sprite_unit_ramp_token1_shadow_rva) {
    [Convert]::ToInt64((
        $layout.draw_resource_sprite_unit_ramp_token1_shadow_rva -replace '^0x', ''), 16)
} else { 0 }
$drawSpriteRenderTargetLine16Rva = if (
    $null -ne $layout.draw_sprite_render_target_line16_rva) {
    [Convert]::ToInt64(
        ($layout.draw_sprite_render_target_line16_rva -replace '^0x', ''), 16)
} else { 0 }
$spriteRenderStateRva = if ($null -ne $layout.sprite_render_state_rva) {
    [Convert]::ToInt64(
        ($layout.sprite_render_state_rva -replace '^0x', ''), 16)
} else { 0 }
$worldRenderCheckpointRva = if ($null -ne $layout.world_render_checkpoint_rva) {
    [Convert]::ToInt64(
        ($layout.world_render_checkpoint_rva -replace '^0x', ''), 16)
} else { 0 }
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
$cameraOverrideRequested =
    $CameraX -ne [int]::MinValue -or $CameraY -ne [int]::MinValue
if ($cameraOverrideRequested -and
    ($CameraX -eq [int]::MinValue -or $CameraY -eq [int]::MinValue)) {
    throw 'Specify both -CameraX and -CameraY.'
}
if ($cameraOverrideRequested -and -not $StabilizeViewport) {
    throw '-CameraX/-CameraY require -StabilizeViewport.'
}
if ($ViewOwner -lt -1 -or $ViewOwner -gt 7) {
    throw '-ViewOwner must be -1 (unchanged) or a player slot from 0 through 7.'
}
$rebuildSpriteTraceRequested =
    $TraceRebuildSpriteX -ne [int]::MinValue -or
    $TraceRebuildSpriteY -ne [int]::MinValue
if ($rebuildSpriteTraceRequested -and
    ($TraceRebuildSpriteX -eq [int]::MinValue -or
     $TraceRebuildSpriteY -eq [int]::MinValue)) {
    throw 'Specify both -TraceRebuildSpriteX and -TraceRebuildSpriteY.'
}
if (($TraceRebuildResourceType -ge 0) -xor
    ($TraceRebuildResourceGroup -ge 0)) {
    throw 'Specify both -TraceRebuildResourceType and -TraceRebuildResourceGroup.'
}
if ($rebuildSpriteTraceRequested -and $TraceRebuildResourceType -ge 0) {
    throw 'Select only one reconstructed sprite trace mode per probe.'
}
if ($TraceLowHealthOverlayCalls -and
    ($TraceOriginalUnitSpriteSlot -ge 0 -or $rebuildSpriteTraceRequested -or
     $TraceRebuildResourceType -ge 0 -or $TraceToken1ShadowSpriteCalls -or
     $TraceUnitRampSpriteCalls)) {
    throw 'Low-health overlay tracing consumes both processes; select only it.'
}
if ($TraceLinkedVisibilityCalls -and
    ($TraceOriginalUnitSpriteSlot -ge 0 -or $rebuildSpriteTraceRequested -or
     $TraceRebuildResourceType -ge 0 -or $TraceLowHealthOverlayCalls -or
     $TraceToken1ShadowSpriteCalls -or $TraceUnitRampSpriteCalls -or
     $TraceProjectileTrailLines -or
     $CaptureRgb565Pair -or $CaptureSingleCompositePair)) {
    throw 'Linked-visibility tracing consumes both processes; select only it.'
}
if ($TraceLinkedVisibilityCalls -and
    ($syncGameplayVisibilityRva -eq 0 -or
     $applyLinkedUnitVisibilityRva -eq 0)) {
    throw 'The resolved layout lacks linked-visibility trace RVAs.'
}
$capturePairRequested = $CaptureRgb565Pair -or $CaptureSingleCompositePair
$worldBarInjectionRequested =
    $WorldBarInjectionSlot -ge 0 -or $WorldBarInjectionFrame -ge 0
if ($worldBarInjectionRequested -and
    ($WorldBarInjectionSlot -lt 0 -or $WorldBarInjectionFrame -lt 1)) {
    throw 'Specify both -WorldBarInjectionSlot and a positive -WorldBarInjectionFrame.'
}
if ($WorldBarInjectionSlot -ge 2048) {
    throw '-WorldBarInjectionSlot must be in [0, 2047].'
}
if ($worldBarInjectionRequested -and
    $WorldBarInjectionFrame -gt $TargetFrame) {
    throw '-WorldBarInjectionFrame cannot exceed -TargetFrame.'
}
if ($capturePairRequested -and
    ($TraceOriginalUnitSpriteSlot -ge 0 -or $rebuildSpriteTraceRequested -or
     $TraceRebuildResourceType -ge 0 -or $TraceLowHealthOverlayCalls -or
     $TraceToken1ShadowSpriteCalls -or $TraceUnitRampSpriteCalls -or
     $TraceProjectileTrailLines -or
     $TraceLinkedVisibilityCalls)) {
    throw 'Completed RGB565 capture and sprite tracing each consume the process; select one.'
}
if ($CaptureRgb565Pair -and
    ($worldRenderCheckpointRva -eq 0 -or $spriteRenderStateRva -eq 0)) {
    throw 'The resolved layout lacks world-render checkpoint or sprite-state RVAs.'
}
if ($CaptureSingleCompositePair -and
    ($renderCompositeRva -eq 0 -or $spriteRenderStateRva -eq 0)) {
    throw 'The resolved layout lacks composite-render or sprite-state RVAs.'
}
if ($TraceLowHealthOverlayCalls -and
    ($renderCompositeRva -eq 0 -or $drawResourceSpriteModeRva -eq 0)) {
    throw 'The resolved layout lacks the composite or resource-mode draw RVA.'
}
if ($TraceToken1ShadowSpriteCalls -and
    ($renderCompositeRva -eq 0 -or $drawResourceSpriteToken1ShadowRva -eq 0)) {
    throw 'The resolved layout lacks the composite or token-1-shadow draw RVA.'
}
if ($TraceUnitRampSpriteCalls -and
    ($renderCompositeRva -eq 0 -or
     $drawResourceSpriteUnitRampToken1ShadowRva -eq 0)) {
    throw 'The resolved layout lacks the composite or unit-ramp draw RVA.'
}
if ($TraceProjectileTrailLines -and
    ($renderCompositeRva -eq 0 -or $drawSpriteRenderTargetLine16Rva -eq 0)) {
    throw 'The resolved layout lacks the composite or target-line draw RVA.'
}

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
    $rebuildFrameAddress = $rebuildBase + $loopRva + $simulationFrameOffset

    $viewOwnerArguments = @()
    if ($ViewOwner -ge 0) {
        $viewOwnerArguments = @(
            (Join-Path $toolDirectory 'replay_pair_control.py'), 'view-owner',
            [string]$original.Id, [string]$rebuild.Id,
            ('0x{0:X}' -f $rebuildBase), ('0x{0:X}' -f $loopRva),
            [string]$ViewOwner, $layoutPath)
        $viewOwnerState = & python @viewOwnerArguments
        if ($LASTEXITCODE -ne 0) {
            throw 'Failed to select the shared replay view owner.'
        }
        $viewOwnerState | Set-Content -LiteralPath `
            (Join-Path $out 'view-owner.json') -Encoding utf8
    }

    if ($StabilizeViewport) {
        $stabilizeArguments = @(
            (Join-Path $toolDirectory 'replay_pair_control.py'), 'stabilize',
            [string]$original.Id, [string]$rebuild.Id,
            ('0x{0:X}' -f $rebuildBase), ('0x{0:X}' -f $loopRva),
            $layoutPath)
        if ($cameraOverrideRequested) {
            $stabilizeArguments += @([string]$CameraX, [string]$CameraY)
        }
        $viewportState = & python @stabilizeArguments
        if ($LASTEXITCODE -ne 0) {
            throw 'Failed to stabilize the replay viewport.'
        }
        $viewportState | Set-Content -LiteralPath `
            (Join-Path $out 'viewport-stabilization.json') -Encoding utf8
    }

    & python (Join-Path $toolDirectory 'replay_pair_control.py') fast `
        $original.Id $rebuild.Id ('0x{0:X}' -f $rebuildBase) `
        ('0x{0:X}' -f $loopRva) $layoutPath | Out-Null

    # A replay starts as observer slot 9. For an ordinary player's fog, leave
    # enough post-initialization ticks for the original 64-frame visibility
    # clear to discard every stale observer-local bit.
    $settleFrames = if ($ViewOwner -ge 0) { 80 } else { 20 }
    $fastTarget = [Math]::Max(1, $TargetFrame - $settleFrames)
    if ($worldBarInjectionRequested) {
        $fastTarget = [Math]::Min($fastTarget, $WorldBarInjectionFrame)
    }
    $bootstrapTarget = [Math]::Min($settleFrames, $fastTarget)
    if ($fastTarget -gt $bootstrapTarget) {
        # Replay/session initialization republishes the imported timing mode
        # during its first simulated frames.  An early `fast` write is then
        # silently lost and a long one-shot probe advances at real-time speed.
        # Cross the initialization boundary first, then reapply the diagnostic
        # fast mode before the long seek (the checkpoint probe already does
        # this naturally between its first and later checkpoints).
        $bootstrapOriginalOut = Join-Path $out 'original-bootstrap.out'
        $bootstrapOriginalErr = Join-Path $out 'original-bootstrap.err'
        $bootstrapRebuildOut = Join-Path $out 'rebuild-bootstrap.out'
        $bootstrapRebuildErr = Join-Path $out 'rebuild-bootstrap.err'
        $drivers = @(
            Start-Process -FilePath python -ArgumentList @(
                (Join-Path $toolDirectory 'resume_to_frame.py'),
                [string]$original.Id, '0x007071A4', [string]$bootstrapTarget,
                '30', '0') -WorkingDirectory $repositoryRoot `
                -RedirectStandardOutput $bootstrapOriginalOut `
                -RedirectStandardError $bootstrapOriginalErr `
                -WindowStyle Hidden -PassThru
            Start-Process -FilePath python -ArgumentList @(
                (Join-Path $toolDirectory 'resume_to_frame.py'),
                [string]$rebuild.Id, ('0x{0:X}' -f $rebuildFrameAddress),
                [string]$bootstrapTarget, '30', '0') `
                -WorkingDirectory $repositoryRoot `
                -RedirectStandardOutput $bootstrapRebuildOut `
                -RedirectStandardError $bootstrapRebuildErr `
                -WindowStyle Hidden -PassThru
        )
        Wait-Process -Id $drivers.Id
        $terminal = Get-ReplayTerminalResult 'bootstrap' $original $rebuild
        if ($null -ne $terminal) {
            $terminal | ConvertTo-Json -Depth 8
            return
        }
        & python (Join-Path $toolDirectory 'replay_pair_control.py') fast `
            $original.Id $rebuild.Id ('0x{0:X}' -f $rebuildBase) `
            ('0x{0:X}' -f $loopRva) $layoutPath | Out-Null
    }
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
    if ($ViewOwner -ge 0) {
        # Replay startup may publish the imported observer owner during the
        # first resumed frame. Restore the diagnostic owner before the final
        # visibility interval.
        $viewOwnerState = & python @viewOwnerArguments
        if ($LASTEXITCODE -ne 0) {
            throw 'Failed to restore the shared replay view owner after fast-forward.'
        }
        $viewOwnerState | Set-Content -LiteralPath `
            (Join-Path $out 'view-owner-after-fast.json') -Encoding utf8
    }
    if ($StabilizeViewport) {
        # The reconstructed frontend can finish publishing its imported
        # primary camera during the first resumed frame. Reapply the shared
        # diagnostic viewport after fast-forward initialization has settled.
        $viewportState = & python @stabilizeArguments
        if ($LASTEXITCODE -ne 0) {
            throw 'Failed to stabilize the replay viewport after fast-forward.'
        }
        $viewportState | Set-Content -LiteralPath `
            (Join-Path $out 'viewport-stabilization-after-fast.json') `
            -Encoding utf8
    }
    if ($AlignPresentationRng) {
        $presentationRngState = & python `
            (Join-Path $toolDirectory 'replay_pair_control.py') presentation-rng `
            $original.Id $rebuild.Id ('0x{0:X}' -f $rebuildBase) `
            ('0x{0:X}' -f $loopRva) $layoutPath
        if ($LASTEXITCODE -ne 0) {
            throw 'Failed to align the presentation RNG after fast-forward.'
        }
        $presentationRngState | Set-Content -LiteralPath `
            (Join-Path $out 'presentation-rng-after-fast.json') -Encoding utf8
    }
    & python (Join-Path $toolDirectory 'replay_pair_control.py') pace `
        $original.Id $rebuild.Id ('0x{0:X}' -f $rebuildBase) `
        ('0x{0:X}' -f $loopRva) 100 100 $layoutPath | Out-Null

    $stableTarget = if ($worldBarInjectionRequested) {
        $WorldBarInjectionFrame
    } else {
        $TargetFrame
    }
    $originalStableOut = Join-Path $out 'original-stable.out'
    $originalStableErr = Join-Path $out 'original-stable.err'
    $rebuildStableOut = Join-Path $out 'rebuild-stable.out'
    $rebuildStableErr = Join-Path $out 'rebuild-stable.err'
    $drivers = @(
        Start-Process -FilePath python -ArgumentList @(
            (Join-Path $toolDirectory 'resume_to_frame.py'),
            [string]$original.Id, '0x007071A4', [string]$stableTarget,
            '30', '0.02') -WorkingDirectory $repositoryRoot `
            -RedirectStandardOutput $originalStableOut `
            -RedirectStandardError $originalStableErr `
            -WindowStyle Hidden -PassThru
        Start-Process -FilePath python -ArgumentList @(
            (Join-Path $toolDirectory 'resume_to_frame.py'),
            [string]$rebuild.Id, ('0x{0:X}' -f $rebuildFrameAddress),
            [string]$stableTarget, '30', '0.02') -WorkingDirectory $repositoryRoot `
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
        ('0x{0:X}' -f $loopRva) $layoutPath | ConvertFrom-Json)
    if ([int]$frames.original -ne $stableTarget -or
        [int]$frames.rebuild -ne $stableTarget) {
        throw "Stable frame mismatch: original=$($frames.original), rebuild=$($frames.rebuild), target=$stableTarget"
    }
    if ($worldBarInjectionRequested) {
        $worldBarState = & python `
            (Join-Path $toolDirectory 'replay_pair_control.py') unit-world-bar `
            $original.Id $rebuild.Id ('0x{0:X}' -f $rebuildBase) `
            ('0x{0:X}' -f $loopRva) $layoutPath `
            $WorldBarInjectionSlot 1
        if ($LASTEXITCODE -ne 0) {
            throw 'Failed to inject the shared unit world-bar flag.'
        }
        $worldBarState | Set-Content -LiteralPath `
            (Join-Path $out 'world-bar-injection.json') -Encoding utf8
        if ($stableTarget -lt $TargetFrame) {
            $drivers = @(
                Start-Process -FilePath python -ArgumentList @(
                    (Join-Path $toolDirectory 'resume_to_frame.py'),
                    [string]$original.Id, '0x007071A4', [string]$TargetFrame,
                    '30', '0.02') -WorkingDirectory $repositoryRoot `
                    -RedirectStandardOutput (Join-Path $out 'original-after-injection.out') `
                    -RedirectStandardError (Join-Path $out 'original-after-injection.err') `
                    -WindowStyle Hidden -PassThru
                Start-Process -FilePath python -ArgumentList @(
                    (Join-Path $toolDirectory 'resume_to_frame.py'),
                    [string]$rebuild.Id, ('0x{0:X}' -f $rebuildFrameAddress),
                    [string]$TargetFrame, '30', '0.02') `
                    -WorkingDirectory $repositoryRoot `
                    -RedirectStandardOutput (Join-Path $out 'rebuild-after-injection.out') `
                    -RedirectStandardError (Join-Path $out 'rebuild-after-injection.err') `
                    -WindowStyle Hidden -PassThru
            )
            Wait-Process -Id $drivers.Id
            $terminal = Get-ReplayTerminalResult 'post-injection' $original $rebuild
            if ($null -ne $terminal) {
                $terminal | ConvertTo-Json -Depth 8
                return
            }
            $frames = (& python `
                (Join-Path $toolDirectory 'replay_pair_control.py') frames `
                $original.Id $rebuild.Id ('0x{0:X}' -f $rebuildBase) `
                ('0x{0:X}' -f $loopRva) $layoutPath | ConvertFrom-Json)
            if ([int]$frames.original -ne $TargetFrame -or
                [int]$frames.rebuild -ne $TargetFrame) {
                throw "Post-injection frame mismatch: original=$($frames.original), rebuild=$($frames.rebuild), target=$TargetFrame"
            }
        }
    }
    if ($AlignPresentationRng) {
        # Camera-shake effect 0x4b consumes two presentation-RNG values per
        # completed render, not per simulation tick.  Independently driven
        # clients can render a different number of intermediate frames, so
        # realign immediately before the single completed-render comparison.
        if ($StabilizeViewport) {
            $viewportState = & python @stabilizeArguments
            if ($LASTEXITCODE -ne 0) {
                throw 'Failed to restabilize the replay viewport before capture.'
            }
            $viewportState | Set-Content -LiteralPath `
                (Join-Path $out 'viewport-stabilization-before-capture.json') `
                -Encoding utf8
        }
        $presentationRngState = & python `
            (Join-Path $toolDirectory 'replay_pair_control.py') presentation-rng `
            $original.Id $rebuild.Id ('0x{0:X}' -f $rebuildBase) `
            ('0x{0:X}' -f $loopRva) $layoutPath
        if ($LASTEXITCODE -ne 0) {
            throw 'Failed to realign the presentation RNG before capture.'
        }
        $presentationRngState | Set-Content -LiteralPath `
            (Join-Path $out 'presentation-rng-before-capture.json') -Encoding utf8
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

    $rebuildDumpPath = ''
    if ($RebuildDumpRva -ne 0 -and $RebuildDumpSize -gt 0) {
        $rebuildDumpPath = Join-Path $out 'rebuild-memory.bin'
        & (Join-Path $toolDirectory 'dump_process_range.ps1') `
            -ProcessId $rebuild.Id -Address ($rebuildBase + $RebuildDumpRva) `
            -Size $RebuildDumpSize -OutputPath $rebuildDumpPath | Out-Null
    }

    $originalCapturePath = ''
    $rebuildCapturePath = ''
    if ($CapturePngDirectory) {
        $pngRoot = [IO.Path]::GetFullPath(
            (Join-Path $repositoryRoot 'debug_artifacts\png'))
        $captureDirectory = if ([IO.Path]::IsPathRooted($CapturePngDirectory)) {
            [IO.Path]::GetFullPath($CapturePngDirectory)
        } else {
            [IO.Path]::GetFullPath(
                (Join-Path $repositoryRoot $CapturePngDirectory))
        }
        $pngPrefix = $pngRoot.TrimEnd('\') + '\'
        if (-not $captureDirectory.StartsWith(
                $pngPrefix, [StringComparison]::OrdinalIgnoreCase)) {
            throw "PNG capture directory escaped debug_artifacts/png: $captureDirectory"
        }
        [IO.Directory]::CreateDirectory($captureDirectory) | Out-Null
        $stem = if ($CapturePngStem) {
            $CapturePngStem
        } else {
            'probe_{0}_frame{1}' -f
                [IO.Path]::GetFileNameWithoutExtension($source), $TargetFrame
        }
        $originalCapturePath = Join-Path $captureDirectory ($stem + '_original.png')
        $rebuildCapturePath = Join-Path $captureDirectory ($stem + '_rebuild.png')
        $originalWindow = [RecoveredReplayUi]::FindTop(
            [uint32]$original.Id, 'The Ranker')
        $rebuildWindow = [RecoveredReplayUi]::FindTop(
            [uint32]$rebuild.Id, 'The Ranker')
        if ($originalWindow -eq [IntPtr]::Zero -or
            $rebuildWindow -eq [IntPtr]::Zero) {
            throw 'Could not resolve suspended gameplay windows for PNG capture.'
        }
        & python (Join-Path $toolDirectory 'capture_client_window.py') `
            ('0x{0:X}' -f $originalWindow.ToInt64()) $originalCapturePath
        & python (Join-Path $toolDirectory 'capture_client_window.py') `
            ('0x{0:X}' -f $rebuildWindow.ToInt64()) $rebuildCapturePath
    }

    $visibilityDumpDirectory = ''
    if ($DumpVisibilityPair) {
        $visibilityDumpDirectory = Join-Path $out 'visibility'
        & python (Join-Path $toolDirectory 'dump_visibility_pair.py') `
            $original.Id $rebuild.Id ('0x{0:X}' -f $rebuildBase) `
            $layoutPath $visibilityDumpDirectory | Out-Null
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
    $originalRgb565Path = ''
    $rebuildRgb565Path = ''
    $rgb565ComparisonPath = ''
    $rgb565OriginalFrame = $null
    $rgb565RebuildFrame = $null
    $rgb565WorldHeight = $null
    $capturePresentationSeed = $null
    if ($capturePairRequested) {
        if ($AlignPresentationRng) {
            # compare_process_state attaches to and briefly resumes both
            # processes after the earlier alignment.  A render completed in
            # that audit window consumes DAT_007071c4 even when simulation
            # state stays on the same frame, so align once more at the actual
            # hand-off to the single-composite capture debuggers.
            $presentationRngState = & python `
                (Join-Path $toolDirectory 'replay_pair_control.py') presentation-rng `
                $original.Id $rebuild.Id ('0x{0:X}' -f $rebuildBase) `
                ('0x{0:X}' -f $loopRva) $layoutPath
            if ($LASTEXITCODE -ne 0) {
                throw 'Failed to realign the presentation RNG at capture hand-off.'
            }
            $capturePresentationSeed = [uint32]((
                ($presentationRngState -join "`n") | ConvertFrom-Json).original)
            $presentationRngState | Set-Content -LiteralPath `
                (Join-Path $out 'presentation-rng-immediately-before-capture.json') `
                -Encoding utf8
        }
        $captureTargetFrame = if ($CaptureSingleCompositePair) {
            $TargetFrame
        } else {
            $TargetFrame + 1
        }
        $worldViewportHeightOffset = if (
            $null -ne $layout.overlay_layout.world_viewport_height) {
            [Convert]::ToInt64((
                $layout.overlay_layout.world_viewport_height -replace '^0x', ''), 16)
        } else { 0 }
        if ($worldViewportHeightOffset -eq 0) {
            throw 'The resolved layout lacks the world viewport height offset.'
        }
        $worldHeightDump = Join-Path $out 'rebuild-world-viewport-height.bin'
        & (Join-Path $toolDirectory 'dump_process_range.ps1') `
            -ProcessId $rebuild.Id `
            -Address ($rebuildBase + $overlayRva + $worldViewportHeightOffset) `
            -Size 4 -OutputPath $worldHeightDump | Out-Null
        $rgb565WorldHeight = [BitConverter]::ToUInt32(
            [IO.File]::ReadAllBytes($worldHeightDump), 0)
        if ($rgb565WorldHeight -lt 1 -or $rgb565WorldHeight -gt 600) {
            throw "Invalid runtime world viewport height: $rgb565WorldHeight"
        }
        $originalRgb565Path = Join-Path $out 'original-completed.rgb565'
        $rebuildRgb565Path = Join-Path $out 'rebuild-completed.rgb565'
        $rgb565ComparisonPath = Join-Path $out 'rgb565-comparison.json'
        $originalRgbOut = Join-Path $out 'original-rgb565.out'
        $originalRgbErr = Join-Path $out 'original-rgb565.err'
        $rebuildRgbOut = Join-Path $out 'rebuild-rgb565.out'
        $rebuildRgbErr = Join-Path $out 'rebuild-rgb565.err'
        $originalCaptureArguments = @(
            (Join-Path $toolDirectory `
                'capture_original_rgb565_at_breakpoint.py'),
            [string]$original.Id, [string]$captureTargetFrame,
            $originalRgb565Path, '--surface-only',
            $(if ($CaptureSingleCompositePair) {
                '--composite-once'
            } else {
                '--world-before-overlay'
            }))
        $rebuildCaptureArguments = @(
            (Join-Path $toolDirectory `
                'capture_rebuild_rgb565_at_breakpoint.py'),
            [string]$rebuild.Id, ('0x{0:X}' -f $rebuildBase),
            ('0x{0:X}' -f $rebuildFrameAddress),
            [string]$captureTargetFrame,
            ('0x{0:X}' -f $(if ($CaptureSingleCompositePair) {
                $renderCompositeRva
            } else {
                $worldRenderCheckpointRva
            })),
            ('0x{0:X}' -f $spriteRenderStateRva),
            $rebuildRgb565Path)
        if (-not $CaptureSingleCompositePair) {
            $rebuildCaptureArguments += '--entry-checkpoint'
        } else {
            $rebuildCaptureArguments += '--camera-address'
        }
        $rebuildCaptureArguments += ('0x{0:X}' -f (
            $rebuildBase + $overlayRva +
            [Convert]::ToInt64((
                $layout.overlay_layout.camera_x -replace '^0x', ''), 16)))
        if ($CaptureSingleCompositePair -and
            $null -ne $capturePresentationSeed) {
            $originalCaptureArguments += @(
                '--presentation-seed',
                ('0x{0:X}' -f [uint32]$capturePresentationSeed))
            $gameplaySoundOffset = [Convert]::ToInt64((
                $layout.gameplay_sound_offset -replace '^0x', ''), 16)
            $variantSeedOffset = [Convert]::ToInt64((
                $layout.gameplay_sound_layout.variant_seed -replace '^0x', ''), 16)
            $rebuildCaptureArguments += @(
                '--presentation-seed-address',
                ('0x{0:X}' -f (
                    $rebuildBase + $runtimeRva + $gameplaySoundOffset +
                    $variantSeedOffset)),
                '--presentation-seed',
                ('0x{0:X}' -f [uint32]$capturePresentationSeed))
        }
        $drivers = @(
            Start-Process -FilePath python -ArgumentList $originalCaptureArguments `
                -WorkingDirectory $repositoryRoot `
                -RedirectStandardOutput $originalRgbOut `
                -RedirectStandardError $originalRgbErr `
                -WindowStyle Hidden -PassThru
            Start-Process -FilePath python -ArgumentList $rebuildCaptureArguments `
                -WorkingDirectory $repositoryRoot `
                -RedirectStandardOutput $rebuildRgbOut `
                -RedirectStandardError $rebuildRgbErr `
                -WindowStyle Hidden -PassThru
        )
        Wait-Process -Id $drivers.Id
        foreach ($driver in $drivers) {
            $driver.Refresh()
            if ($null -ne $driver.ExitCode -and $driver.ExitCode -ne 0) {
                $errorPath = if ($driver.Id -eq $drivers[0].Id) {
                    $originalRgbErr
                } else { $rebuildRgbErr }
                $errorText = if (Test-Path -LiteralPath $errorPath) {
                    Get-Content -LiteralPath $errorPath -Raw
                } else { '' }
                throw "Completed RGB565 capture failed (exit $($driver.ExitCode)): $errorText"
            }
        }
        if (-not (Test-Path -LiteralPath $originalRgb565Path) -or
            -not (Test-Path -LiteralPath $rebuildRgb565Path)) {
            throw 'Completed RGB565 capture produced no surface dump.'
        }
        $originalRgbMetadata = Get-Content -LiteralPath $originalRgbOut -Raw |
            ConvertFrom-Json
        $rebuildRgbMetadata = Get-Content -LiteralPath $rebuildRgbOut -Raw |
            ConvertFrom-Json
        $rgb565OriginalFrame = [int]$originalRgbMetadata.frame
        $rgb565RebuildFrame = [int]$rebuildRgbMetadata.frame
        if ($rgb565OriginalFrame -ne $rgb565RebuildFrame) {
            throw ('Completed RGB565 frame mismatch: original={0}, rebuild={1}' -f
                $rgb565OriginalFrame, $rgb565RebuildFrame)
        }
        & python (Join-Path $toolDirectory 'compare_rgb565_dumps.py') `
            $originalRgb565Path $rebuildRgb565Path `
            --width ([int]$originalRgbMetadata.width) `
            --height ([int]$originalRgbMetadata.height) `
            --world-height $rgb565WorldHeight `
            --output-json $rgb565ComparisonPath
        $rgb565Exit = $LASTEXITCODE
        if ($rgb565Exit -gt 1 -or
            -not (Test-Path -LiteralPath $rgb565ComparisonPath)) {
            throw "Completed RGB565 comparison failed (exit $rgb565Exit)."
        }
    }
    $originalUnitSpriteTracePath = ''
    if ($TraceOriginalUnitSpriteSlot -ge 0) {
        $originalUnitSpriteTracePath = Join-Path $out `
            ('original-unit-{0}-sprite-entry.json' -f `
                $TraceOriginalUnitSpriteSlot)
        & python (Join-Path $toolDirectory `
            'trace_original_unit_sprite_entry.py') `
            $original.Id $TargetFrame $TraceOriginalUnitSpriteSlot `
            $originalUnitSpriteTracePath
        if ($LASTEXITCODE -ne 0 -or
            -not (Test-Path -LiteralPath $originalUnitSpriteTracePath)) {
            throw 'Original unit sprite-entry trace failed.'
        }
    }
    $rebuildUnitSpriteTracePath = ''
    if ($rebuildSpriteTraceRequested) {
        $rebuildUnitSpriteTracePath = Join-Path $out `
            ('rebuild-unit-sprite-{0}-{1}.json' -f `
                $TraceRebuildSpriteX, $TraceRebuildSpriteY)
        & python (Join-Path $toolDirectory `
            'trace_rebuild_unit_sprite_entry.py') `
            $rebuild.Id ('0x{0:X}' -f $rebuildBase) `
            ('0x{0:X}' -f $rebuildFrameAddress) $TargetFrame `
            animation $TraceRebuildSpriteX $TraceRebuildSpriteY `
            $rebuildUnitSpriteTracePath
        if ($LASTEXITCODE -ne 0 -or
            -not (Test-Path -LiteralPath $rebuildUnitSpriteTracePath)) {
            throw 'Reconstructed unit sprite-entry trace failed.'
        }
    }
    if ($TraceRebuildResourceType -ge 0) {
        $rebuildUnitSpriteTracePath = Join-Path $out `
            ('rebuild-unit-resource-{0}-{1}.json' -f `
                $TraceRebuildResourceType, $TraceRebuildResourceGroup)
        & python (Join-Path $toolDirectory `
            'trace_rebuild_unit_sprite_entry.py') `
            $rebuild.Id ('0x{0:X}' -f $rebuildBase) `
            ('0x{0:X}' -f $rebuildFrameAddress) $TargetFrame `
            resource $TraceRebuildResourceType $TraceRebuildResourceGroup `
            $rebuildUnitSpriteTracePath
        if ($LASTEXITCODE -ne 0 -or
            -not (Test-Path -LiteralPath $rebuildUnitSpriteTracePath)) {
            throw 'Reconstructed unit resource lookup trace failed.'
        }
    }
    $originalLowHealthTracePath = ''
    $rebuildLowHealthTracePath = ''
    $originalLinkedVisibilityTracePath = ''
    $rebuildLinkedVisibilityTracePath = ''
    if ($TraceLinkedVisibilityCalls) {
        $originalLinkedVisibilityTracePath = Join-Path $out `
            'original-linked-visibility-calls.json'
        $rebuildLinkedVisibilityTracePath = Join-Path $out `
            'rebuild-linked-visibility-calls.json'
        $originalTraceOut = Join-Path $out 'original-linked-visibility-trace.out'
        $originalTraceErr = Join-Path $out 'original-linked-visibility-trace.err'
        $rebuildTraceOut = Join-Path $out 'rebuild-linked-visibility-trace.out'
        $rebuildTraceErr = Join-Path $out 'rebuild-linked-visibility-trace.err'
        $traceTool = Join-Path $toolDirectory `
            'trace_render_sprite_mode_calls.py'
        $drivers = @(
            Start-Process -FilePath python -ArgumentList @(
                $traceTool, 'original', [string]$original.Id, '0',
                '0x007071A4', [string]$TargetFrame,
                '0x004D5BC0', '0x004D5F01',
                $originalLinkedVisibilityTracePath, 'visibility') `
                -WorkingDirectory $repositoryRoot `
                -RedirectStandardOutput $originalTraceOut `
                -RedirectStandardError $originalTraceErr `
                -WindowStyle Hidden -PassThru
            Start-Process -FilePath python -ArgumentList @(
                $traceTool, 'rebuild', [string]$rebuild.Id,
                ('0x{0:X}' -f $rebuildBase),
                ('0x{0:X}' -f $rebuildFrameAddress), [string]$TargetFrame,
                ('0x{0:X}' -f ($rebuildBase + $syncGameplayVisibilityRva)),
                ('0x{0:X}' -f ($rebuildBase + $applyLinkedUnitVisibilityRva)),
                $rebuildLinkedVisibilityTracePath, 'visibility') `
                -WorkingDirectory $repositoryRoot `
                -RedirectStandardOutput $rebuildTraceOut `
                -RedirectStandardError $rebuildTraceErr `
                -WindowStyle Hidden -PassThru
        )
        foreach ($driver in $drivers) {
            Wait-Process -Id $driver.Id -Timeout 45
            $driver.WaitForExit()
            $driver.Refresh()
            $driverErrorPath = if ($driver.Id -eq $drivers[0].Id) {
                $originalTraceErr
            } else { $rebuildTraceErr }
            $expectedTracePath = if ($driver.Id -eq $drivers[0].Id) {
                $originalLinkedVisibilityTracePath
            } else { $rebuildLinkedVisibilityTracePath }
            if (-not (Test-Path -LiteralPath $expectedTracePath) -or
                ($null -ne $driver.ExitCode -and $driver.ExitCode -ne 0)) {
                $errorText = if (Test-Path -LiteralPath $driverErrorPath) {
                    Get-Content -LiteralPath $driverErrorPath -Raw
                } else { '' }
                throw "Linked-visibility trace failed (exit $($driver.ExitCode)): $errorText"
            }
        }
    }
    if ($TraceLowHealthOverlayCalls) {
        $originalLowHealthTracePath = Join-Path $out `
            'original-low-health-overlay-calls.json'
        $rebuildLowHealthTracePath = Join-Path $out `
            'rebuild-low-health-overlay-calls.json'
        $originalTraceOut = Join-Path $out 'original-low-health-trace.out'
        $originalTraceErr = Join-Path $out 'original-low-health-trace.err'
        $rebuildTraceOut = Join-Path $out 'rebuild-low-health-trace.out'
        $rebuildTraceErr = Join-Path $out 'rebuild-low-health-trace.err'
        $traceTool = Join-Path $toolDirectory `
            'trace_render_sprite_mode_calls.py'
        $drivers = @(
            Start-Process -FilePath python -ArgumentList @(
                $traceTool, 'original', [string]$original.Id, '0',
                '0x007071A4', [string]$TargetFrame,
                '0x004D7790', '0x005066FD', $originalLowHealthTracePath) `
                -WorkingDirectory $repositoryRoot `
                -RedirectStandardOutput $originalTraceOut `
                -RedirectStandardError $originalTraceErr `
                -WindowStyle Hidden -PassThru
            Start-Process -FilePath python -ArgumentList @(
                $traceTool, 'rebuild', [string]$rebuild.Id,
                ('0x{0:X}' -f $rebuildBase),
                ('0x{0:X}' -f $rebuildFrameAddress), [string]$TargetFrame,
                ('0x{0:X}' -f ($rebuildBase + $renderCompositeRva)),
                ('0x{0:X}' -f ($rebuildBase + $drawResourceSpriteModeRva)),
                $rebuildLowHealthTracePath) `
                -WorkingDirectory $repositoryRoot `
                -RedirectStandardOutput $rebuildTraceOut `
                -RedirectStandardError $rebuildTraceErr `
                -WindowStyle Hidden -PassThru
        )
        Wait-Process -Id $drivers.Id
        foreach ($driver in $drivers) {
            $driver.Refresh()
            if ($null -ne $driver.ExitCode -and $driver.ExitCode -ne 0) {
                $errorPath = if ($driver.Id -eq $drivers[0].Id) {
                    $originalTraceErr
                } else { $rebuildTraceErr }
                $errorText = if (Test-Path -LiteralPath $errorPath) {
                    Get-Content -LiteralPath $errorPath -Raw
                } else { '' }
                throw "Low-health overlay trace failed (exit $($driver.ExitCode)): $errorText"
            }
        }
    }
    if ($TraceToken1ShadowSpriteCalls) {
        $originalToken1ShadowTracePath = Join-Path $out `
            'original-token1-shadow-sprite-calls.json'
        $rebuildToken1ShadowTracePath = Join-Path $out `
            'rebuild-token1-shadow-sprite-calls.json'
        $originalTraceOut = Join-Path $out 'original-token1-shadow-trace.out'
        $originalTraceErr = Join-Path $out 'original-token1-shadow-trace.err'
        $rebuildTraceOut = Join-Path $out 'rebuild-token1-shadow-trace.out'
        $rebuildTraceErr = Join-Path $out 'rebuild-token1-shadow-trace.err'
        $traceTool = Join-Path $toolDirectory `
            'trace_render_sprite_mode_calls.py'
        $drivers = @(
            Start-Process -FilePath python -ArgumentList @(
                $traceTool, 'original', [string]$original.Id, '0',
                '0x007071A4', [string]$TargetFrame,
                '0x004D7790', '0x0050627E', $originalToken1ShadowTracePath,
                'unit') `
                -WorkingDirectory $repositoryRoot `
                -RedirectStandardOutput $originalTraceOut `
                -RedirectStandardError $originalTraceErr `
                -WindowStyle Hidden -PassThru
            Start-Process -FilePath python -ArgumentList @(
                $traceTool, 'rebuild', [string]$rebuild.Id,
                ('0x{0:X}' -f $rebuildBase),
                ('0x{0:X}' -f $rebuildFrameAddress), [string]$TargetFrame,
                ('0x{0:X}' -f ($rebuildBase + $renderCompositeRva)),
                ('0x{0:X}' -f ($rebuildBase + $drawResourceSpriteToken1ShadowRva)),
                $rebuildToken1ShadowTracePath, 'unit',
                ('0x{0:X}' -f ($rebuildBase + $spriteRenderStateRva))) `
                -WorkingDirectory $repositoryRoot `
                -RedirectStandardOutput $rebuildTraceOut `
                -RedirectStandardError $rebuildTraceErr `
                -WindowStyle Hidden -PassThru
        )
        Wait-Process -Id $drivers.Id
        foreach ($driver in $drivers) {
            $driver.Refresh()
            if ($null -ne $driver.ExitCode -and $driver.ExitCode -ne 0) {
                $errorPath = if ($driver.Id -eq $drivers[0].Id) {
                    $originalTraceErr
                } else { $rebuildTraceErr }
                $errorText = if (Test-Path -LiteralPath $errorPath) {
                    Get-Content -LiteralPath $errorPath -Raw
                } else { '' }
                throw "Token-1-shadow sprite trace failed (exit $($driver.ExitCode)): $errorText"
            }
        }
    }
    if ($TraceUnitRampSpriteCalls) {
        $originalUnitRampTracePath = Join-Path $out `
            'original-unit-ramp-sprite-calls.json'
        $rebuildUnitRampTracePath = Join-Path $out `
            'rebuild-unit-ramp-sprite-calls.json'
        $originalTraceOut = Join-Path $out 'original-unit-ramp-trace.out'
        $originalTraceErr = Join-Path $out 'original-unit-ramp-trace.err'
        $rebuildTraceOut = Join-Path $out 'rebuild-unit-ramp-trace.out'
        $rebuildTraceErr = Join-Path $out 'rebuild-unit-ramp-trace.err'
        $traceTool = Join-Path $toolDirectory `
            'trace_render_sprite_mode_calls.py'
        $drivers = @(
            Start-Process -FilePath python -ArgumentList @(
                $traceTool, 'original', [string]$original.Id, '0',
                '0x007071A4', [string]$TargetFrame,
                '0x004D7790', '0x004D2F5A', $originalUnitRampTracePath,
                'unit') `
                -WorkingDirectory $repositoryRoot `
                -RedirectStandardOutput $originalTraceOut `
                -RedirectStandardError $originalTraceErr `
                -WindowStyle Hidden -PassThru
            Start-Process -FilePath python -ArgumentList @(
                $traceTool, 'rebuild', [string]$rebuild.Id,
                ('0x{0:X}' -f $rebuildBase),
                ('0x{0:X}' -f $rebuildFrameAddress), [string]$TargetFrame,
                ('0x{0:X}' -f ($rebuildBase + $renderCompositeRva)),
                ('0x{0:X}' -f (
                    $rebuildBase + $drawResourceSpriteUnitRampToken1ShadowRva)),
                $rebuildUnitRampTracePath, 'unit',
                ('0x{0:X}' -f ($rebuildBase + $spriteRenderStateRva))) `
                -WorkingDirectory $repositoryRoot `
                -RedirectStandardOutput $rebuildTraceOut `
                -RedirectStandardError $rebuildTraceErr `
                -WindowStyle Hidden -PassThru
        )
        Wait-Process -Id $drivers.Id
        foreach ($driver in $drivers) {
            $driver.Refresh()
            if ($null -ne $driver.ExitCode -and $driver.ExitCode -ne 0) {
                $errorPath = if ($driver.Id -eq $drivers[0].Id) {
                    $originalTraceErr
                } else { $rebuildTraceErr }
                $errorText = if (Test-Path -LiteralPath $errorPath) {
                    Get-Content -LiteralPath $errorPath -Raw
                } else { '' }
                throw "Unit-ramp sprite trace failed (exit $($driver.ExitCode)): $errorText"
            }
        }
    }
    $originalProjectileTrailTracePath = ''
    $rebuildProjectileTrailTracePath = ''
    if ($TraceProjectileTrailLines) {
        $originalProjectileTrailTracePath = Join-Path $out `
            'original-projectile-trail-lines.json'
        $rebuildProjectileTrailTracePath = Join-Path $out `
            'rebuild-projectile-trail-lines.json'
        $originalTraceOut = Join-Path $out 'original-projectile-trail-trace.out'
        $originalTraceErr = Join-Path $out 'original-projectile-trail-trace.err'
        $rebuildTraceOut = Join-Path $out 'rebuild-projectile-trail-trace.out'
        $rebuildTraceErr = Join-Path $out 'rebuild-projectile-trail-trace.err'
        $traceTool = Join-Path $toolDirectory `
            'trace_render_sprite_mode_calls.py'
        $drivers = @(
            Start-Process -FilePath python -ArgumentList @(
                $traceTool, 'original', [string]$original.Id, '0',
                '0x007071A4', [string]$TargetFrame,
                '0x004D7790', '0x0050853D',
                $originalProjectileTrailTracePath, 'line') `
                -WorkingDirectory $repositoryRoot `
                -RedirectStandardOutput $originalTraceOut `
                -RedirectStandardError $originalTraceErr `
                -WindowStyle Hidden -PassThru
            Start-Process -FilePath python -ArgumentList @(
                $traceTool, 'rebuild', [string]$rebuild.Id,
                ('0x{0:X}' -f $rebuildBase),
                ('0x{0:X}' -f $rebuildFrameAddress), [string]$TargetFrame,
                ('0x{0:X}' -f ($rebuildBase + $renderCompositeRva)),
                ('0x{0:X}' -f ($rebuildBase + $drawSpriteRenderTargetLine16Rva)),
                $rebuildProjectileTrailTracePath, 'line') `
                -WorkingDirectory $repositoryRoot `
                -RedirectStandardOutput $rebuildTraceOut `
                -RedirectStandardError $rebuildTraceErr `
                -WindowStyle Hidden -PassThru
        )
        Wait-Process -Id $drivers.Id
        foreach ($driver in $drivers) {
            $driver.Refresh()
            if ($null -ne $driver.ExitCode -and $driver.ExitCode -ne 0) {
                $errorPath = if ($driver.Id -eq $drivers[0].Id) {
                    $originalTraceErr
                } else { $rebuildTraceErr }
                $errorText = if (Test-Path -LiteralPath $errorPath) {
                    Get-Content -LiteralPath $errorPath -Raw
                } else { '' }
                throw "Projectile-trail line trace failed (exit $($driver.ExitCode)): $errorText"
            }
        }
    }
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
        rebuild_dump_path = $rebuildDumpPath
        visibility_dump_directory = $visibilityDumpDirectory
        original_capture_path = $originalCapturePath
        rebuild_capture_path = $rebuildCapturePath
        original_rgb565_path = $originalRgb565Path
        rebuild_rgb565_path = $rebuildRgb565Path
        rgb565_comparison_path = $rgb565ComparisonPath
        rgb565_original_frame = $rgb565OriginalFrame
        rgb565_rebuild_frame = $rgb565RebuildFrame
        rgb565_world_height = $rgb565WorldHeight
        original_unit_sprite_trace_path = $originalUnitSpriteTracePath
        rebuild_unit_sprite_trace_path = $rebuildUnitSpriteTracePath
        original_low_health_trace_path = $originalLowHealthTracePath
        rebuild_low_health_trace_path = $rebuildLowHealthTracePath
        original_linked_visibility_trace_path = $originalLinkedVisibilityTracePath
        rebuild_linked_visibility_trace_path = $rebuildLinkedVisibilityTracePath
        original_projectile_trail_trace_path = $originalProjectileTrailTracePath
        rebuild_projectile_trail_trace_path = $rebuildProjectileTrailTracePath
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
