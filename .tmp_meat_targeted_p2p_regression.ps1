param(
    [string]$WorkingDirectory = 'RankerOCPV_Win',
    [string]$OutputDirectory = '.tmp_meat_targeted_p2p_run',
    [int]$TribeIndex = 2,
    [int]$ProbeTimeoutSeconds = 150,
    [int]$CombatTimeoutSeconds = 120,
    [int]$PickupTimeoutSeconds = 30,
    [int]$DamageConsumeTimeoutSeconds = 55,
    [switch]$Execute
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$root = $PSScriptRoot
$working = Join-Path $root $WorkingDirectory
$output = Join-Path $root $OutputDirectory
$python = (Get-Command python -ErrorAction Stop).Source

$required = @(
    (Join-Path $working 'ranker.exe'),
    (Join-Path $working 'ranker_rebuild.exe'),
    (Join-Path $root '.tmp_runtime_globals_layout_probe.exe'),
    (Join-Path $root '.tmp_resolve_rebuild_layout.ps1'),
    (Join-Path $root '.tmp_route_fresh_pair.ps1'),
    (Join-Path $root '.tmp_afdd_overlay_snapshot.py'),
    (Join-Path $root '.tmp_combat_command_probe.py'),
    (Join-Path $root '.tmp_held_click_window.ps1'),
    (Join-Path $root '.tmp_meat_live_probe.py'),
    (Join-Path $root '.tmp_meat_live_probe_history_regression.py'),
    (Join-Path $root '.tmp_attack_flash_live_probe.py'),
    (Join-Path $root '.tmp_meat_targeted_pair_probe.py'),
    (Join-Path $root '.tmp_meat_targeted_pair_probe_fixture.py')
)
$missing = @($required | Where-Object { -not (Test-Path -LiteralPath $_) })
$timeoutContract =
    $ProbeTimeoutSeconds -ge 120 -and $ProbeTimeoutSeconds -le 150 -and
    $CombatTimeoutSeconds -ge 60 -and
    $CombatTimeoutSeconds -lt $ProbeTimeoutSeconds -and
    $PickupTimeoutSeconds -ge 10 -and
    $DamageConsumeTimeoutSeconds -ge 30 -and
    ($CombatTimeoutSeconds + $PickupTimeoutSeconds) -le
        ($ProbeTimeoutSeconds + 30)

$layout = $null
$layoutError = $null
if ($missing.Count -eq 0) {
    try {
        $layout = (& (Join-Path $root '.tmp_resolve_rebuild_layout.ps1') `
            -Executable (Join-Path $working 'ranker_rebuild.exe') `
            -LayoutProbe (Join-Path $root '.tmp_runtime_globals_layout_probe.exe') `
            -AllowExactExecutableLayoutReuse |
            ConvertFrom-Json)
    }
    catch {
        $layoutError = $_.Exception.Message
    }
}

$sharedProbeContract = $false
if (Test-Path -LiteralPath (Join-Path $root '.tmp_meat_live_probe.py')) {
    $sharedProbeSource = [IO.File]::ReadAllText(
        (Join-Path $root '.tmp_meat_live_probe.py'))
    $sharedProbeContract =
        $sharedProbeSource.Contains('"area_marker_flags"') -and
        $sharedProbeSource.Contains('"type_flags"') -and
        $sharedProbeSource.Contains('coverage["spawn"]') -and
        $sharedProbeSource.Contains('coverage["pickup"]') -and
        $sharedProbeSource.Contains('coverage["consume"]') -and
        $sharedProbeSource.Contains('coverage["cargo_contamination"]')
}

$phases = @(
    'launch a fresh original host and reconstructed client; enter P2P gameplay',
    'select at least two local type-32 attackers and a visible neutral type-75',
    'start a 120-150 second finalized exact-frame meat probe before combat',
    'press physical A (scan 0x1e), target the neutral, and verify 0x88..0x80 red flash',
    'issue explicit attack orders until type-75 death creates effect id 2 on both peers',
    'select collector; click hot item 183/action 0x0d; prove raw +0x0c bit 31 on both peers',
    'move collector onto food; prove effect removal and action-mode pickup without cargo',
    'route collector into hostile owner-0 units; prove real damage then action -1/HP +1',
    'report generation, pickup, and consumption as independent verdicts and clean fresh PIDs'
)

$dryRun = [ordered]@{
    mode = if ($Execute) { 'execute' } else { 'dry-run' }
    ready = ($missing.Count -eq 0 -and $null -ne $layout -and
        $timeoutContract -and $sharedProbeContract)
    product_write_authorized = $false
    output_directory = $output
    missing = $missing
    layout_error = $layoutError
    layout = $layout
    timeout_contract = [ordered]@{
        pass = $timeoutContract
        probe_seconds = $ProbeTimeoutSeconds
        combat_seconds = $CombatTimeoutSeconds
        pickup_seconds = $PickupTimeoutSeconds
        damage_consume_seconds = $DamageConsumeTimeoutSeconds
    }
    shared_probe_contract = $sharedProbeContract
    exact_frame_probe = '.tmp_meat_live_probe.py'
    paired_marker_damage_probe = '.tmp_meat_targeted_pair_probe.py'
    cleanup = 'preexisting ranker processes forbidden; stop only PIDs launched after the fresh-pair gate'
    phases = $phases
}

if (-not $Execute) {
    $dryRun | ConvertTo-Json -Depth 16
    return
}
if (-not $dryRun.ready) {
    throw ('Targeted meat P2P prerequisites are not ready: ' +
        ($dryRun | ConvertTo-Json -Depth 12 -Compress))
}

$existing = @(Get-Process ranker,ranker_rebuild -ErrorAction SilentlyContinue)
if ($existing.Count -ne 0) {
    throw ('Close existing ranker processes before isolated execution: ' +
        (($existing | ForEach-Object {
            '{0}:{1}' -f $_.ProcessName,$_.Id
        }) -join ', '))
}

[IO.Directory]::CreateDirectory($output) | Out-Null
$resolvedLayoutPath = Join-Path $output 'resolved-layout.json'
[IO.File]::WriteAllText(
    $resolvedLayoutPath, ($layout | ConvertTo-Json -Depth 16),
    [Text.UTF8Encoding]::new($false))
$env:PYTHONUTF8 = '1'
$env:PYTHONDONTWRITEBYTECODE = '1'

function Save-Json([object]$Value, [string]$Path) {
    [IO.File]::WriteAllText(
        $Path, ($Value | ConvertTo-Json -Depth 64),
        [Text.UTF8Encoding]::new($false))
}

function Hex-ToInt64([object]$Value) {
    $text = [string]$Value
    if ($text.StartsWith('0x', [StringComparison]::OrdinalIgnoreCase)) {
        return [Convert]::ToInt64($text.Substring(2), 16)
    }
    return [Convert]::ToInt64($text, 10)
}

$runtimeRva = Hex-ToInt64 $layout.runtime_rva
$overlayRva = Hex-ToInt64 $layout.overlay_rva
$loopRva = Hex-ToInt64 $layout.loop_rva
$movementOffset = Hex-ToInt64 $layout.movement_offset
$lifecycleOffset = Hex-ToInt64 $layout.lifecycle_offset
$visibilityOffset = Hex-ToInt64 $layout.visibility_offset

$script:originalPid = 0
$script:rebuildPid = 0
$script:rebuildBase = 0L
$script:rebuildWindow = [IntPtr]::Zero
$script:meatProbeProcess = $null
$script:attackFlashProbeProcess = $null
$script:launchStartedUtc = $null
$script:freshPids = @()

if (-not ('MeatTargetedWindowInput' -as [type])) {
    Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class MeatTargetedWindowInput {
    [StructLayout(LayoutKind.Sequential)]
    public struct Rect {
        public int Left, Top, Right, Bottom;
    }
    [DllImport("user32.dll")]
    public static extern bool GetClientRect(IntPtr handle, out Rect rect);
    [DllImport("user32.dll")]
    public static extern IntPtr SendMessage(
        IntPtr handle, uint message, IntPtr wparam, IntPtr lparam);
}
'@
}

function Invoke-PythonJson([string]$Script, [object[]]$Arguments) {
    $token = [Guid]::NewGuid().ToString('N')
    $stdout = Join-Path $output ('.python-json-{0}.out' -f $token)
    $stderr = Join-Path $output ('.python-json-{0}.err' -f $token)
    try {
        $process = Start-Process -FilePath $python `
            -ArgumentList (@($Script) + @($Arguments)) `
            -WorkingDirectory $root -WindowStyle Hidden -Wait -PassThru `
            -RedirectStandardOutput $stdout -RedirectStandardError $stderr
        $utf8 = [Text.UTF8Encoding]::new($false, $true)
        $stdoutText = [IO.File]::ReadAllText($stdout, $utf8)
        $stderrText = [IO.File]::ReadAllText($stderr, $utf8)
        if ($process.ExitCode -ne 0) {
            throw ('Python probe failed ({0}): {1}; {2}' -f
                $process.ExitCode, $Script, $stderrText)
        }
        return ($stdoutText | ConvertFrom-Json)
    }
    finally {
        Remove-Item -LiteralPath $stdout,$stderr -Force `
            -ErrorAction SilentlyContinue
    }
}

function Get-RebuildSnapshot([string]$SavePath = '') {
    $snapshot = Invoke-PythonJson `
        (Join-Path $root '.tmp_afdd_overlay_snapshot.py') @(
            [string]$script:rebuildPid,
            ('0x{0:X}' -f $script:rebuildBase),
            ('0x{0:X}' -f $runtimeRva),
            ('0x{0:X}' -f $overlayRva),
            ('0x{0:X}' -f $loopRva),
            ('0x{0:X}' -f $movementOffset),
            ('0x{0:X}' -f $lifecycleOffset),
            ('0x{0:X}' -f $visibilityOffset),
            $resolvedLayoutPath)
    if ($SavePath) { Save-Json $snapshot $SavePath }
    return $snapshot
}

function Get-CombatCommandProbe([int]$AttackerSlot, [int]$TargetSlot) {
    return Invoke-PythonJson `
        (Join-Path $root '.tmp_combat_command_probe.py') @(
            [string]$script:rebuildPid,
            ('0x{0:X}' -f $script:rebuildBase),
            ('0x{0:X}' -f $runtimeRva),
            ('0x{0:X}' -f $movementOffset),
            $resolvedLayoutPath,
            ('{0},{1}' -f $AttackerSlot,$TargetSlot))
}

function Get-PairedUnitCondition(
    [int]$Slot, [string]$Condition, [int]$TimeoutSeconds) {
    return Invoke-PythonJson `
        (Join-Path $root '.tmp_meat_targeted_pair_probe.py') @(
            [string]$script:originalPid,
            [string]$script:rebuildPid,
            ('0x{0:X}' -f $script:rebuildBase),
            $resolvedLayoutPath,
            [string]$Slot,
            '--condition', $Condition,
            '--timeout', [string]$TimeoutSeconds,
            '--interval', '0.005')
}

function Invoke-WindowClick(
    [int]$X, [int]$Y, [switch]$Right,
    [int]$HoldMilliseconds = 35) {
    $rect = [MeatTargetedWindowInput+Rect]::new()
    if ([MeatTargetedWindowInput]::GetClientRect(
            $script:rebuildWindow, [ref]$rect)) {
        $width = [Math]::Max(1, $rect.Right - $rect.Left)
        $height = [Math]::Max(1, $rect.Bottom - $rect.Top)
        # Snapshot coordinates are on the game's logical 800x600 surface.
        # Aim at the centre of the corresponding presented pixel so this is
        # identity at 800x600 and remains exact for scaled client windows.
        $X = [Math]::Max(0, [Math]::Min($width - 1, [int][Math]::Floor(
            (([int64]$X * 2 + 1) * $width) / (2 * 800))))
        $Y = [Math]::Max(0, [Math]::Min($height - 1, [int][Math]::Floor(
            (([int64]$Y * 2 + 1) * $height) / (2 * 600))))
    }
    $arguments = @{
        ProcessId = $script:rebuildPid
        WindowHandle = $script:rebuildWindow.ToInt64()
        X = $X
        Y = $Y
        HoldMilliseconds = $HoldMilliseconds
    }
    if ($Right) { $arguments.Right = $true }
    & (Join-Path $root '.tmp_held_click_window.ps1') @arguments
}

function Invoke-PhysicalAttackHotkey {
    # WM_KEYDOWN/UP with set-1 scan 0x1e.  The game consumes this scan from
    # lParam, which is the exact physical-A route used by the original.
    [MeatTargetedWindowInput]::SendMessage(
        $script:rebuildWindow, 0x0100, [IntPtr]0x41,
        [IntPtr][int64]0x001e0001) | Out-Null
    Start-Sleep -Milliseconds 35
    [MeatTargetedWindowInput]::SendMessage(
        $script:rebuildWindow, 0x0101, [IntPtr]0x41,
        [IntPtr][int64]0xc01e0001) | Out-Null
}

function Get-HotRegion([object]$Snapshot, [int]$Item) {
    return @($Snapshot.hot_regions | Where-Object {
        [int]$_.item -eq $Item -and [bool]$_.enabled
    } | Select-Object -First 1)[0]
}

function Wait-HotItemSnapshot(
    [object]$Snapshot, [int]$Item,
    [int]$TimeoutMilliseconds = 4000) {
    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMilliseconds)
    $current = $Snapshot
    while ($null -eq (Get-HotRegion $current $Item) -and
        [DateTime]::UtcNow -lt $deadline) {
        Start-Sleep -Milliseconds 75
        $current = Get-RebuildSnapshot
    }
    return $current
}

function Click-HotItem([object]$Snapshot, [int]$Item) {
    $current = Wait-HotItemSnapshot $Snapshot $Item
    $region = Get-HotRegion $current $Item
    if ($null -eq $region) {
        throw "Enabled HUD item $Item was not present."
    }
    Invoke-WindowClick -X ([int]$region.center[0]) `
        -Y ([int]$region.center[1])
    return $current
}

function Get-GameplayViewBottom([object]$Snapshot) {
    $screenHeight = [int]$Snapshot.screen[1]
    $tops = @($Snapshot.hot_regions | ForEach-Object {
        [int]$_.rect[1]
    } | Where-Object { $_ -gt 0 -and $_ -lt $screenHeight })
    if ($tops.Count -eq 0) { return $screenHeight }
    return [int](($tops | Measure-Object -Minimum).Minimum)
}

function Center-World([object]$Snapshot, [int]$WorldX, [int]$WorldY) {
    $mapWidth = [Math]::Max(1, [int]$Snapshot.visibility.map[0] * 32)
    $mapHeight = [Math]::Max(1, [int]$Snapshot.visibility.map[1] * 32)
    $left = [int]$Snapshot.minimap_layout.output[0]
    $top = [int]$Snapshot.minimap_layout.output[1]
    $width = [Math]::Max(1, [int]$Snapshot.minimap_layout.size[0])
    $height = [Math]::Max(1, [int]$Snapshot.minimap_layout.size[1])
    $x = $left + [Math]::Floor(
        [Math]::Min($mapWidth - 1, [Math]::Max(0, $WorldX)) *
        $width / $mapWidth)
    $y = $top + [Math]::Floor(
        [Math]::Min($mapHeight - 1, [Math]::Max(0, $WorldY)) *
        $height / $mapHeight)
    Invoke-WindowClick -X $x -Y $y
    Start-Sleep -Milliseconds 200
    return Get-RebuildSnapshot
}

function Get-UnitHitBounds([object]$Snapshot, [object]$Unit) {
    $mini = @($Snapshot.minimap_units | Where-Object {
        [int]$_.id -eq [int]$Unit.id
    } | Select-Object -First 1)[0]
    if ($null -eq $mini) { return @(0, 0, 32, 32) }
    $width = [int]$mini.bounds[2]
    $height = [int]$mini.bounds[3]
    if ($width -le 0 -or $height -le 0) {
        return @(0, 0,
            [Math]::Max(32, [int]$mini.footprint[0] * 32),
            [Math]::Max(32, [int]$mini.footprint[1] * 32))
    }
    return @([int]$mini.bounds[0], [int]$mini.bounds[1],
        $width, $height)
}

function Select-Unit([object]$Snapshot, [object]$Unit) {
    $targetId = [int]$Unit.id
    $current = $Snapshot
    for ($attempt = 1; $attempt -le 4; ++$attempt) {
        if ([int]$current.selected.id -eq $targetId) { return $current }
        $live = @($current.active_units | Where-Object {
            [int]$_.id -eq $targetId -and [bool]$_.active
        } | Select-Object -First 1)[0]
        if ($null -eq $live) {
            throw "Unit id $targetId is no longer active."
        }
        $centered = Center-World $current `
            ([int]$live.world[0]) ([int]$live.world[1])
        if ([int]$centered.selected.id -eq $targetId) { return $centered }
        $live = @($centered.active_units | Where-Object {
            [int]$_.id -eq $targetId -and [bool]$_.active
        } | Select-Object -First 1)[0]
        $bounds = Get-UnitHitBounds $centered $live
        $x = [int]$live.world[0] - [int]$centered.camera[0] +
            [int]$bounds[0] + [Math]::Floor([int]$bounds[2] / 2)
        $y = [int]$live.world[1] - [int]$centered.camera[1] +
            [int]$bounds[1] + [Math]::Floor([int]$bounds[3] / 2)
        if ($x -ge 0 -and $x -lt [int]$centered.screen[0] -and
            $y -ge 0 -and $y -lt (Get-GameplayViewBottom $centered)) {
            Invoke-WindowClick -X $x -Y $y
            Start-Sleep -Milliseconds (200 + 50 * $attempt)
            $selected = Get-RebuildSnapshot
            if ([int]$selected.selected.id -eq $targetId) {
                return $selected
            }
            $current = $selected
        }
        else {
            $current = Get-RebuildSnapshot
        }
    }
    throw "Unit id $targetId could not be selected."
}

function Click-World(
    [object]$Snapshot, [int]$WorldX, [int]$WorldY,
    [switch]$Right) {
    $x = $WorldX - [int]$Snapshot.camera[0]
    $y = $WorldY - [int]$Snapshot.camera[1]
    if ($x -lt 0 -or $x -ge [int]$Snapshot.screen[0] -or
        $y -lt 0 -or $y -ge (Get-GameplayViewBottom $Snapshot)) {
        throw "World point ($WorldX,$WorldY) is outside the gameplay view."
    }
    Invoke-WindowClick -X $x -Y $y -Right:$Right
}

function Issue-AttackOrder(
    [int]$AttackerSlot, [int]$TargetSlot,
    [int]$Attempts = 4) {
    $records = @()
    for ($attempt = 1; $attempt -le $Attempts; ++$attempt) {
        $snapshot = Get-RebuildSnapshot
        $attacker = @($snapshot.active_units | Where-Object {
            [int]$_.slot -eq $AttackerSlot -and [bool]$_.active
        } | Select-Object -First 1)[0]
        $target = @($snapshot.active_units | Where-Object {
            [int]$_.slot -eq $TargetSlot -and [bool]$_.active -and
            [int]$_.health[0] -gt 0
        } | Select-Object -First 1)[0]
        if ($null -eq $attacker -or $null -eq $target) { break }
        $selected = Select-Unit $snapshot $attacker
        $view = Center-World $selected `
            ([int]$target.world[0]) ([int]$target.world[1])
        $attackRegion = Get-HotRegion $view 175
        if ($null -eq $attackRegion) {
            $records += [ordered]@{
                attempt = $attempt; confirmed = $false
                reason = 'hot item 175 missing'
            }
            continue
        }
        # Exercise the user's exact path: physical A selects HUD object 0xaf.
        Invoke-PhysicalAttackHotkey
        Start-Sleep -Milliseconds 40
        $probe = Get-CombatCommandProbe $AttackerSlot $TargetSlot
        $probeTarget = $probe.units.PSObject.Properties[
            [string]$TargetSlot].Value
        if ($null -eq $probeTarget -or [int]$probeTarget.health -le 0) {
            break
        }
        $viewTarget = @($view.active_units | Where-Object {
            [int]$_.slot -eq $TargetSlot
        } | Select-Object -First 1)[0]
        $bounds = if ($null -eq $viewTarget) {
            @(0, 0, 32, 32)
        } else { Get-UnitHitBounds $view $viewTarget }
        $clickX = [int]$probeTarget.world[0] + [int]$bounds[0] +
            [Math]::Floor([int]$bounds[2] / 2)
        $clickY = [int]$probeTarget.world[1] + [int]$bounds[1] +
            [Math]::Floor([int]$bounds[3] / 2)
        try {
            Click-World $view $clickX $clickY
        }
        catch {
            $records += [ordered]@{
                attempt = $attempt; confirmed = $false
                reason = $_.Exception.Message
            }
            continue
        }
        $deadline = [DateTime]::UtcNow.AddSeconds(2)
        $confirmed = $false
        $observed = $null
        do {
            Start-Sleep -Milliseconds 50
            $after = Get-CombatCommandProbe $AttackerSlot $TargetSlot
            $observed = $after.units.PSObject.Properties[
                [string]$AttackerSlot].Value
            if ($null -ne $observed -and
                [int]$observed.target_slot -eq $TargetSlot) {
                $confirmed = $true
                break
            }
        } while ([DateTime]::UtcNow -lt $deadline)
        $records += [ordered]@{
            attempt = $attempt
            confirmed = $confirmed
            target_slot = if ($null -eq $observed) { -1 } else {
                [int]$observed.target_slot
            }
            world = @($clickX,$clickY)
        }
        if ($confirmed) {
            return [pscustomobject][ordered]@{
                pass = $true; attempts = $records
            }
        }
    }
    return [pscustomobject][ordered]@{
        pass = $false; attempts = $records
    }
}

function Start-PythonTrace(
    [string]$Script, [object[]]$Arguments,
    [string]$Stdout, [string]$Stderr) {
    return Start-Process -FilePath $python `
        -ArgumentList (@($Script) + @($Arguments)) `
        -WorkingDirectory $root -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput $Stdout -RedirectStandardError $Stderr
}

function Read-JsonLines([string]$Path) {
    $rows = @()
    if (-not (Test-Path -LiteralPath $Path)) { return $rows }
    $share = [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete
    $stream = [IO.FileStream]::new(
        $Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, $share)
    $reader = [IO.StreamReader]::new(
        $stream, [Text.UTF8Encoding]::new($false, $true), $true)
    try {
        while (-not $reader.EndOfStream) {
            $line = $reader.ReadLine()
            if ([String]::IsNullOrWhiteSpace($line)) { continue }
            # A concurrently appended final line may be incomplete.  Ignore it
            # on this poll; emit_jsonl flushes and the next poll reads it whole.
            try { $rows += ($line | ConvertFrom-Json) } catch { }
        }
    }
    finally {
        $reader.Dispose()
    }
    return $rows
}

function Find-MeatTraceEvent(
    [string]$Path, [string]$Kind, [int]$CollectorSlot = -1) {
    foreach ($row in @(Read-JsonLines $Path)) {
        if ([string]$row.kind -ne 'meat_event' -or -not [bool]$row.parity) {
            continue
        }
        if ($Kind -eq 'spawn') {
            $original = @($row.original.spawned | Where-Object {
                [int]$_.effect_id -eq 2
            } | Select-Object -First 1)[0]
            $rebuild = @($row.rebuild.spawned | Where-Object {
                [int]$_.effect_id -eq 2
            } | Select-Object -First 1)[0]
        }
        elseif ($Kind -eq 'pickup') {
            $original = @($row.original.pickups | Where-Object {
                [int]$_.slot -eq $CollectorSlot
            } | Select-Object -First 1)[0]
            $rebuild = @($row.rebuild.pickups | Where-Object {
                [int]$_.slot -eq $CollectorSlot
            } | Select-Object -First 1)[0]
        }
        elseif ($Kind -eq 'consume') {
            $original = @($row.original.consumed | Where-Object {
                [int]$_.slot -eq $CollectorSlot
            } | Select-Object -First 1)[0]
            $rebuild = @($row.rebuild.consumed | Where-Object {
                [int]$_.slot -eq $CollectorSlot
            } | Select-Object -First 1)[0]
        }
        else { throw "Unknown meat event kind: $Kind" }
        if ($null -ne $original -and $null -ne $rebuild) {
            return [pscustomobject][ordered]@{
                frame = [int]$row.frame
                previous_frame = [int]$row.previous_frame
                original = $original
                rebuild = $rebuild
            }
        }
    }
    return $null
}

function Wait-MeatTraceEvent(
    [string]$Path, [string]$Kind, [int]$TimeoutSeconds,
    [int]$CollectorSlot = -1) {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        $event = Find-MeatTraceEvent $Path $Kind $CollectorSlot
        if ($null -ne $event) { return $event }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    return $null
}

function Stop-FreshProcesses {
    $stopped = @()
    if ($null -ne $script:attackFlashProbeProcess) {
        $flashProbe = Get-Process -Id $script:attackFlashProbeProcess.Id `
            -ErrorAction SilentlyContinue
        if ($null -ne $flashProbe) {
            Stop-Process -InputObject $flashProbe -Force `
                -ErrorAction SilentlyContinue
            $stopped += $flashProbe.Id
        }
    }
    if ($null -ne $script:meatProbeProcess) {
        $probe = Get-Process -Id $script:meatProbeProcess.Id `
            -ErrorAction SilentlyContinue
        if ($null -ne $probe) {
            Stop-Process -InputObject $probe -Force -ErrorAction SilentlyContinue
            $stopped += $probe.Id
        }
    }
    $explicit = @($script:freshPids | Where-Object { $_ -gt 0 })
    $candidates = @(Get-Process ranker,ranker_rebuild `
        -ErrorAction SilentlyContinue | Where-Object {
            $_.Id -in $explicit -or
            ($null -ne $script:launchStartedUtc -and
             $_.StartTime.ToUniversalTime() -ge $script:launchStartedUtc)
        })
    foreach ($process in $candidates) {
        Stop-Process -InputObject $process -Force -ErrorAction SilentlyContinue
        $stopped += $process.Id
    }
    Start-Sleep -Milliseconds 200
    return [ordered]@{
        stopped_pids = @($stopped | Sort-Object -Unique)
        remaining_ranker_pids = @(
            Get-Process ranker,ranker_rebuild -ErrorAction SilentlyContinue |
            ForEach-Object { $_.Id })
    }
}

$results = [ordered]@{
    started_utc = [DateTime]::UtcNow.ToString('o')
    executable_sha256 = [string]$layout.sha256
    output_directory = $output
    processes = $null
    target = $null
    attackers = @()
    attack_orders = @()
    attack_flash = $null
    generation = [ordered]@{ pass = $false }
    marker = [ordered]@{ pass = $false; hot_item = 183; action = 0x0d }
    pickup = [ordered]@{ pass = $false }
    consume = [ordered]@{ pass = $false }
    cargo_clean = $false
    meat_probe = $null
    integrated_full_coverage_pass = $false
    failure = $null
    cleanup = $null
    pass = $false
}
$exitCode = 1

try {
    $script:launchStartedUtc = [DateTime]::UtcNow.AddSeconds(-1)
    $pair = & (Join-Path $root '.tmp_route_fresh_pair.ps1') `
        -WorkingDirectory $WorkingDirectory `
        -OriginalName 'OriginalMeatTargetedHost' `
        -RebuildName 'RebuildMeatTargetedClient' `
        -TribeIndex $TribeIndex -GameplayWaitSeconds 7
    $script:originalPid = [int]$pair.OriginalPid
    $script:rebuildPid = [int]$pair.RebuildPid
    $script:rebuildBase = Hex-ToInt64 $pair.RebuildBase
    $script:rebuildWindow = [IntPtr](Hex-ToInt64 $pair.RebuildWindow)
    $script:freshPids = @($script:originalPid,$script:rebuildPid)
    $results.processes = $pair

    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    $initial = $null
    do {
        try { $initial = Get-RebuildSnapshot } catch { $initial = $null }
        $ready = $null -ne $initial -and
            [int]$initial.frame.simulation -gt 0 -and
            @($initial.active_units).Count -gt 0
        if (-not $ready) { Start-Sleep -Milliseconds 250 }
    } while (-not $ready -and [DateTime]::UtcNow -lt $deadline)
    if (-not $ready) { throw 'Fresh pair did not enter gameplay in 30 seconds.' }
    Save-Json $initial (Join-Path $output 'initial-rebuild.json')

    $owner = [int]$initial.local_owner
    $workers = @($initial.active_units | Where-Object {
        [int]$_.owner -eq $owner -and [int]$_.type -eq 32 -and
        [bool]$_.active -and [int]$_.health[0] -gt 0
    })
    if ($workers.Count -lt 2) {
        throw "At least two live type-32 attackers are required; found $($workers.Count)."
    }
    $visibleIds = @($initial.minimap_units | Where-Object {
        [int]$_.owner -eq 8 -and [bool]$_.visible -and -not [bool]$_.hidden
    } | ForEach-Object { [int]$_.id })
    $neutralCandidates = @($initial.active_units | Where-Object {
        [int]$_.owner -eq 8 -and [int]$_.type -eq 75 -and
        [bool]$_.active -and [int]$_.health[0] -gt 0 -and
        [int]$_.id -in $visibleIds
    })
    if ($neutralCandidates.Count -eq 0) {
        throw 'No visible live neutral type-75 target was available.'
    }
    $neutral = @($neutralCandidates | Sort-Object @{ Expression = {
        $candidate = $_
        $best = [Int64]::MaxValue
        foreach ($worker in $workers) {
            $dx = [Int64]([int]$candidate.world[0] - [int]$worker.world[0])
            $dy = [Int64]([int]$candidate.world[1] - [int]$worker.world[1])
            $distance = $dx * $dx + $dy * $dy
            if ($distance -lt $best) { $best = $distance }
        }
        $best
    }} | Select-Object -First 1)[0]
    $attackers = @($workers | Sort-Object @{ Expression = {
        $dx = [Int64]([int]$_.world[0] - [int]$neutral.world[0])
        $dy = [Int64]([int]$_.world[1] - [int]$neutral.world[1])
        $dx * $dx + $dy * $dy
    }} | Select-Object -First 6)
    $results.target = [ordered]@{
        slot = [int]$neutral.slot
        id = [int]$neutral.id
        type = [int]$neutral.type
        health = @($neutral.health)
        world = @($neutral.world)
    }
    $results.attackers = @($attackers | ForEach-Object {
        [ordered]@{ slot=[int]$_.slot; id=[int]$_.id; world=@($_.world) }
    })

    $meatTracePath = Join-Path $output 'meat-trace.jsonl'
    $meatSummaryPath = Join-Path $output 'meat-summary.json'
    $meatStdout = Join-Path $output 'meat-probe.out'
    $meatStderr = Join-Path $output 'meat-probe.err'
    $meatArgs = @(
        [string]$script:originalPid, [string]$script:rebuildPid,
        ('0x{0:X}' -f $script:rebuildBase), $resolvedLayoutPath,
        '--neutral-slot', [string]$neutral.slot,
        '--timeout', [string]$ProbeTimeoutSeconds,
        '--interval', '0.005', '--jsonl', $meatTracePath,
        '--summary', $meatSummaryPath)
    foreach ($attacker in $attackers) {
        $meatArgs += @('--collector-slot', [string]$attacker.slot)
    }
    $script:meatProbeProcess = Start-PythonTrace `
        (Join-Path $root '.tmp_meat_live_probe.py') $meatArgs `
        $meatStdout $meatStderr
    Start-Sleep -Milliseconds 250

    $attackFlashSummaryPath = Join-Path $output 'attack-flash-summary.json'
    $attackFlashStdout = Join-Path $output 'attack-flash-probe.out'
    $attackFlashStderr = Join-Path $output 'attack-flash-probe.err'
    $script:attackFlashProbeProcess = Start-Process -FilePath $python `
        -ArgumentList @(
            (Join-Path $root '.tmp_attack_flash_live_probe.py'),
            [string]$script:rebuildPid,
            ('0x{0:X}' -f $script:rebuildBase),
            $resolvedLayoutPath,
            [string]$neutral.slot,
            '--timeout', '20',
            '--interval', '0.001',
            '--summary', $attackFlashSummaryPath) `
        -WorkingDirectory $root -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput $attackFlashStdout `
        -RedirectStandardError $attackFlashStderr
    Start-Sleep -Milliseconds 100

    foreach ($attacker in $attackers) {
        $order = Issue-AttackOrder ([int]$attacker.slot) ([int]$neutral.slot)
        $results.attack_orders += [ordered]@{
            slot = [int]$attacker.slot
            result = $order
        }
    }
    $issuedCount = @($results.attack_orders | Where-Object {
        [bool]$_.result.pass
    }).Count
    # A single type-32 attacker can finish this neutral before the remaining
    # explicit orders are issued.  The paired meat trace is authoritative for
    # the death/spawn transition, so require at least one confirmed UI order.
    if ($issuedCount -lt 1) {
        throw "Only $issuedCount explicit type-75 attack orders were confirmed."
    }

    if (-not $script:attackFlashProbeProcess.WaitForExit(20000)) {
        throw 'Physical A-attack red-flash probe timed out.'
    }
    if (-not (Test-Path -LiteralPath $attackFlashSummaryPath)) {
        throw 'Physical A-attack red-flash summary was not produced.'
    }
    $results.attack_flash = Get-Content $attackFlashSummaryPath -Raw |
        ConvertFrom-Json
    if (-not [bool]$results.attack_flash.pass) {
        throw 'Physical A-attack target did not complete the original red-flash timer.'
    }

    $spawn = Wait-MeatTraceEvent $meatTracePath 'spawn' `
        $CombatTimeoutSeconds
    if ($null -eq $spawn) {
        throw 'Effect id 2 did not spawn on both peers within combat timeout.'
    }
    $results.generation = [ordered]@{
        pass = $true
        frame = [int]$spawn.frame
        original = $spawn.original
        rebuild = $spawn.rebuild
        effect_id_is_meat =
            [int]$spawn.original.effect_id -eq 2 -and
            [int]$spawn.rebuild.effect_id -eq 2
    }

    $collectorSnapshot = Get-RebuildSnapshot
    $collectors = @($collectorSnapshot.active_units | Where-Object {
        [int]$_.slot -in @($attackers | ForEach-Object { [int]$_.slot }) -and
        [bool]$_.active -and [int]$_.health[0] -gt 0
    } | Sort-Object @{ Expression = {
        $dx = [Int64]([int]$_.world[0] - [int]$spawn.rebuild.x)
        $dy = [Int64]([int]$_.world[1] - [int]$spawn.rebuild.y)
        $dx * $dx + $dy * $dy
    }})
    if ($collectors.Count -eq 0) {
        throw 'No live collector remained after combat.'
    }

    # Combatants frequently finish stacked on the meat/neutral corpse.  A
    # click at the first candidate's sprite centre can therefore select a
    # different survivor even though the candidate is still alive.  Try the
    # remaining live candidates before classifying this as product failure.
    $collector = $null
    $selected = $null
    $collectorSelectionErrors = @()
    foreach ($candidate in $collectors) {
        try {
            $selected = Select-Unit (Get-RebuildSnapshot) $candidate
            $collector = $candidate
            break
        }
        catch {
            $collectorSelectionErrors += [ordered]@{
                slot = [int]$candidate.slot
                id = [int]$candidate.id
                error = $_.Exception.Message
            }
        }
    }
    if ($null -eq $collector -or $null -eq $selected) {
        throw ('No live collector could be selected after combat: ' +
            ($collectorSelectionErrors | ConvertTo-Json -Compress -Depth 4))
    }
    $collectorSlot = [int]$collector.slot
    $markerBefore = Get-PairedUnitCondition $collectorSlot 'snapshot' 10
    $selectedWithMarkerButton = Click-HotItem $selected 183
    $markerAfter = Get-PairedUnitCondition $collectorSlot 'marker' 15
    $markerPass = [bool]$markerAfter.matched -and
        [bool]$markerAfter.pair.original.area_marker_high -and
        [bool]$markerAfter.pair.rebuild.area_marker_high -and
        [int]$markerAfter.parity_mismatch_samples -eq 0
    $results.marker = [ordered]@{
        pass = $markerPass
        hot_item = 183
        action = 0x0d
        collector_slot = $collectorSlot
        selection_fallbacks = $collectorSelectionErrors
        before = $markerBefore
        after = $markerAfter
    }
    if (-not $markerPass) {
        throw 'Hot item 183 did not set area_marker_flags bit 31 on both peers.'
    }

    $routeSnapshot = Get-RebuildSnapshot
    $liveCollector = @($routeSnapshot.active_units | Where-Object {
        [int]$_.slot -eq $collectorSlot -and [bool]$_.active
    } | Select-Object -First 1)[0]
    $collectorSelected = Select-Unit $routeSnapshot $liveCollector
    $foodView = Center-World $collectorSelected `
        ([int]$spawn.rebuild.x) ([int]$spawn.rebuild.y)
    Click-World $foodView ([int]$spawn.rebuild.x) `
        ([int]$spawn.rebuild.y) -Right
    $pickupEvent = Wait-MeatTraceEvent $meatTracePath 'pickup' `
        $PickupTimeoutSeconds $collectorSlot
    if ($null -eq $pickupEvent) {
        throw 'Collector did not produce an exact-frame pickup event.'
    }
    $pickupState = Get-PairedUnitCondition $collectorSlot 'snapshot' 10
    $cargoBaseline = [int]$markerBefore.pair.original.cargo
    $pickupPass =
        [int]$pickupEvent.original.action_delta -gt 0 -and
        [int]$pickupEvent.rebuild.action_delta -gt 0 -and
        [int]$pickupEvent.original.cargo_delta -eq 0 -and
        [int]$pickupEvent.rebuild.cargo_delta -eq 0 -and
        [int]$pickupState.pair.original.action_mode -gt 0 -and
        [int]$pickupState.pair.rebuild.action_mode -gt 0 -and
        [int]$pickupState.pair.original.cargo -eq $cargoBaseline -and
        [int]$pickupState.pair.rebuild.cargo -eq $cargoBaseline
    $results.pickup = [ordered]@{
        pass = $pickupPass
        frame = [int]$pickupEvent.frame
        event = $pickupEvent
        state = $pickupState
        cargo_baseline = $cargoBaseline
    }
    if (-not $pickupPass) {
        throw 'Pickup changed cargo or failed to increase action_mode equally.'
    }

    $postPickup = Get-RebuildSnapshot
    $liveCollector = @($postPickup.active_units | Where-Object {
        [int]$_.slot -eq $collectorSlot -and [bool]$_.active
    } | Select-Object -First 1)[0]
    $hostiles = @($postPickup.active_units | Where-Object {
        [int]$_.owner -ge 0 -and [int]$_.owner -lt 8 -and
        [int]$_.owner -ne $owner -and [int]$_.type -eq 32 -and
        [bool]$_.active -and [int]$_.health[0] -gt 0
    } | Sort-Object @{ Expression = {
        $dx = [Int64]([int]$_.world[0] - [int]$liveCollector.world[0])
        $dy = [Int64]([int]$_.world[1] - [int]$liveCollector.world[1])
        $dx * $dx + $dy * $dy
    }})
    $hostile = @($hostiles | Select-Object -First 1)[0]
    if ($null -eq $hostile) {
        throw 'No hostile owner-0 type-32 unit was available for real damage.'
    }

    $selectedForDamage = Select-Unit $postPickup $liveCollector
    $hostileView = Center-World $selectedForDamage `
        ([int]$hostile.world[0]) ([int]$hostile.world[1])
    Click-World $hostileView ([int]$hostile.world[0]) `
        ([int]$hostile.world[1]) -Right
    $firstDamageWindow = [Math]::Min(35, $DamageConsumeTimeoutSeconds)
    $damageConsume = Get-PairedUnitCondition `
        $collectorSlot 'damage-consume' $firstDamageWindow
    $damageAttack = $null
    if (-not [bool]$damageConsume.matched) {
        $damageSnapshot = Get-RebuildSnapshot
        $visibleHostileIds = @($damageSnapshot.minimap_units | Where-Object {
            [int]$_.owner -ne $owner -and [int]$_.owner -lt 8 -and
            [bool]$_.visible -and -not [bool]$_.hidden
        } | ForEach-Object { [int]$_.id })
        $visibleHostile = @($damageSnapshot.active_units | Where-Object {
            [int]$_.owner -ne $owner -and [int]$_.owner -lt 8 -and
            [int]$_.type -eq 32 -and [bool]$_.active -and
            [int]$_.id -in $visibleHostileIds
        } | Select-Object -First 1)[0]
        if ($null -ne $visibleHostile) {
            $damageAttack = Issue-AttackOrder `
                $collectorSlot ([int]$visibleHostile.slot) 3
        }
        $remaining = [Math]::Max(
            10, $DamageConsumeTimeoutSeconds - $firstDamageWindow)
        $damageConsume = Get-PairedUnitCondition `
            $collectorSlot 'damage-consume' $remaining
    }
    $consumeEvent = Wait-MeatTraceEvent $meatTracePath 'consume' `
        10 $collectorSlot
    # The long-lived trace and the targeted condition probe finalize the same
    # peer frame independently, but their observation lifetimes differ.  A
    # far-away hostile can make the long trace expire before the targeted
    # probe sees the edge, while a fast consume can happen before the targeted
    # probe starts.  Accept either complete same-frame proof; do not require
    # the same transition to be observed twice.
    $consumeEventDamageObserved =
        $null -ne $consumeEvent -and
        [int]$consumeEvent.original.health_before -lt
            [int]$pickupState.pair.original.max_health -and
        [int]$consumeEvent.rebuild.health_before -lt
            [int]$pickupState.pair.rebuild.max_health
    $consumeEventPass =
        $null -ne $consumeEvent -and
        $consumeEventDamageObserved -and
        [int]$consumeEvent.original.action_delta -lt 0 -and
        [int]$consumeEvent.original.health_delta -eq
            -[int]$consumeEvent.original.action_delta -and
        [int]$consumeEvent.original.cargo_delta -eq 0 -and
        [int]$consumeEvent.rebuild.action_delta -lt 0 -and
        [int]$consumeEvent.rebuild.health_delta -eq
            -[int]$consumeEvent.rebuild.action_delta -and
        [int]$consumeEvent.rebuild.cargo_delta -eq 0 -and
        [int]$consumeEvent.original.action_before -eq
            [int]$consumeEvent.rebuild.action_before -and
        [int]$consumeEvent.original.action_after -eq
            [int]$consumeEvent.rebuild.action_after -and
        [int]$consumeEvent.original.health_before -eq
            [int]$consumeEvent.rebuild.health_before -and
        [int]$consumeEvent.original.health_after -eq
            [int]$consumeEvent.rebuild.health_after
    $pairedConsumePass =
        [bool]$damageConsume.matched -and
        [int]$damageConsume.parity_mismatch_samples -eq 0 -and
        [bool]$damageConsume.damage_observed -and
        [bool]$damageConsume.detail.parity -and
        [bool]$damageConsume.detail.damage_observed -and
        [bool]$damageConsume.detail.original_transition.matched -and
        [bool]$damageConsume.detail.rebuild_transition.matched -and
        [int]$damageConsume.detail.original_transition.action_delta -lt 0 -and
        [int]$damageConsume.detail.original_transition.health_delta -eq
            -[int]$damageConsume.detail.original_transition.action_delta -and
        [int]$damageConsume.detail.original_transition.cargo_delta -eq 0 -and
        [int]$damageConsume.detail.rebuild_transition.action_delta -lt 0 -and
        [int]$damageConsume.detail.rebuild_transition.health_delta -eq
            -[int]$damageConsume.detail.rebuild_transition.action_delta -and
        [int]$damageConsume.detail.rebuild_transition.cargo_delta -eq 0
    $consumePass = $consumeEventPass -or $pairedConsumePass
    $consumeCoverageSource = if ($consumeEventPass) {
        'trace'
    }
    elseif ($pairedConsumePass) {
        'paired_condition'
    }
    else {
        'none'
    }
    $results.consume = [ordered]@{
        pass = $consumePass
        hostile = [ordered]@{
            slot = [int]$hostile.slot
            id = [int]$hostile.id
            owner = [int]$hostile.owner
            type = [int]$hostile.type
            world = @($hostile.world)
        }
        explicit_attack = $damageAttack
        paired_damage_consume = $damageConsume
        exact_event_damage_observed = $consumeEventDamageObserved
        exact_event_pass = $consumeEventPass
        paired_condition_pass = $pairedConsumePass
        coverage_source = $consumeCoverageSource
        event = $consumeEvent
    }
    if (-not $consumePass) {
        throw 'Real damage followed by exact action -1 / HP +1 consumption was not observed.'
    }

    $probeDeadline = [DateTime]::UtcNow.AddSeconds(15)
    while ($null -ne (Get-Process -Id $script:meatProbeProcess.Id `
        -ErrorAction SilentlyContinue) -and
        [DateTime]::UtcNow -lt $probeDeadline) {
        Start-Sleep -Milliseconds 100
    }
    if (-not (Test-Path -LiteralPath $meatSummaryPath)) {
        throw 'Meat exact-frame probe did not produce its summary.'
    }
    $summary = Get-Content -Raw -LiteralPath $meatSummaryPath |
        ConvertFrom-Json
    $results.meat_probe = $summary
    $results.cargo_clean =
        -not [bool]$summary.coverage.cargo_contamination -and
        [int]$summary.parity.food_effect_mismatch_samples -eq 0 -and
        [int]$summary.parity.tracked_unit_mismatch_samples -eq 0 -and
        [int]$summary.parity.event_mismatches -eq 0

    $traceConsumeCoverage =
        [bool]$summary.coverage.consume -and
        [bool]$summary.verdict.consumption_decrements_action_and_heals
    $integratedConsumeCoverage =
        $traceConsumeCoverage -or $pairedConsumePass
    $results.consume.trace_coverage_pass = $traceConsumeCoverage
    $results.consume.integrated_coverage_pass = $integratedConsumeCoverage

    $results.generation.pass = [bool]$results.generation.pass -and
        [bool]$summary.coverage.neutral_transition -and
        [bool]$summary.coverage.spawn -and
        [bool]$summary.verdict.generation_matches
    $results.pickup.pass = [bool]$results.pickup.pass -and
        [bool]$summary.coverage.pickup -and
        [bool]$summary.verdict.pickup_uses_action_not_cargo
    $results.consume.pass = [bool]$results.consume.pass -and
        $integratedConsumeCoverage
    $results.integrated_full_coverage_pass =
        [bool]$summary.verdict.full_coverage_pass -or
        ($pairedConsumePass -and
         [bool]$summary.coverage.neutral_transition -and
         [bool]$summary.coverage.spawn -and
         [bool]$summary.coverage.pickup -and
         [bool]$summary.verdict.generation_matches -and
         [bool]$summary.verdict.pickup_uses_action_not_cargo -and
         [bool]$results.cargo_clean)
    $results.pass =
        [bool]$results.generation.pass -and
        [bool]$results.marker.pass -and
        [bool]$results.pickup.pass -and
        [bool]$results.consume.pass -and
        [bool]$results.cargo_clean -and
        [bool]$results.integrated_full_coverage_pass
    $exitCode = if ($results.pass) { 0 } else { 2 }
}
catch {
    $results.failure = [ordered]@{
        classification = 'targeted-harness-or-coverage-failure'
        message = $_.Exception.Message
        script_stack = $_.ScriptStackTrace
    }
    $exitCode = 1
}
finally {
    $results.cleanup = Stop-FreshProcesses
    $results.finished_utc = [DateTime]::UtcNow.ToString('o')
    Save-Json $results (Join-Path $output 'result.json')
}

$results | ConvertTo-Json -Depth 64
exit $exitCode
