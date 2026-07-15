param(
    [string]$WorkingDirectory = 'RankerOCPV_Win',
    [string]$OutputDirectory = '.tmp_upgrade_p2p_run_current',
    [int]$TribeIndex = 2,
    [int]$TimeoutSeconds = 180,
    [switch]$KeepProcesses
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$root = $PSScriptRoot
$working = Join-Path $root $WorkingDirectory
$output = Join-Path $root $OutputDirectory
$python = (Get-Command python -ErrorAction Stop).Source
$env:PYTHONUTF8 = '1'
$env:PYTHONDONTWRITEBYTECODE = '1'

$existing = @(Get-Process -Name ranker,ranker_rebuild -ErrorAction SilentlyContinue)
if ($existing.Count -ne 0) {
    throw ('Close existing ranker/ranker_rebuild instances before an isolated run: ' +
        (($existing | ForEach-Object { '{0}:{1}' -f $_.ProcessName,$_.Id }) -join ', '))
}

[IO.Directory]::CreateDirectory($output) | Out-Null
$layout = (& (Join-Path $root '.tmp_resolve_rebuild_layout.ps1') `
    -Executable (Join-Path $working 'ranker_rebuild.exe') `
    -LayoutProbe (Join-Path $root '.tmp_runtime_globals_layout_probe.exe') `
    -AllowExactExecutableLayoutReuse |
    ConvertFrom-Json)
$layoutPath = Join-Path $output 'resolved-layout.json'
[IO.File]::WriteAllText($layoutPath,
    ($layout | ConvertTo-Json -Depth 16), [Text.UTF8Encoding]::new($false))

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

function Invoke-PythonJson([string]$Script, [object[]]$Arguments) {
    $token = [Guid]::NewGuid().ToString('N')
    $stdout = Join-Path $output ('.python-{0}.out' -f $token)
    $stderr = Join-Path $output ('.python-{0}.err' -f $token)
    try {
        $process = Start-Process -FilePath $python `
            -ArgumentList (@($Script) + @($Arguments)) -WorkingDirectory $root `
            -RedirectStandardOutput $stdout -RedirectStandardError $stderr `
            -WindowStyle Hidden -Wait -PassThru
        $stdoutText = [IO.File]::ReadAllText(
            $stdout, [Text.UTF8Encoding]::new($false, $true))
        if ($process.ExitCode -ne 0) {
            $stderrText = [IO.File]::ReadAllText(
                $stderr, [Text.UTF8Encoding]::new($false, $true))
            throw "Python probe failed: $stderrText"
        }
        return $stdoutText | ConvertFrom-Json
    }
    finally {
        Remove-Item -LiteralPath $stdout,$stderr -Force -ErrorAction SilentlyContinue
    }
}

if (-not ('UpgradePairWindowInput' -as [type])) {
    Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class UpgradePairWindowInput {
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

$script:originalPid = 0
$script:rebuildPid = 0
$script:rebuildBase = 0L
$script:rebuildWindow = [IntPtr]::Zero

function Get-Snapshot {
    return Invoke-PythonJson (Join-Path $root '.tmp_afdd_overlay_snapshot.py') @(
        [string]$script:rebuildPid,
        ('0x{0:X}' -f $script:rebuildBase),
        ('0x{0:X}' -f $runtimeRva),
        ('0x{0:X}' -f $overlayRva),
        ('0x{0:X}' -f $loopRva),
        ('0x{0:X}' -f $movementOffset),
        ('0x{0:X}' -f $lifecycleOffset),
        ('0x{0:X}' -f $visibilityOffset),
        $layoutPath)
}

function Invoke-Click([int]$X, [int]$Y) {
    $rect = [UpgradePairWindowInput+Rect]::new()
    if ([UpgradePairWindowInput]::GetClientRect(
            $script:rebuildWindow, [ref]$rect)) {
        $width = [Math]::Max(1, $rect.Right - $rect.Left)
        $height = [Math]::Max(1, $rect.Bottom - $rect.Top)
        $X = [Math]::Max(0, [Math]::Min($width - 1, [int][Math]::Floor(
            (([int64]$X * 2 + 1) * $width) / (2 * 800))))
        $Y = [Math]::Max(0, [Math]::Min($height - 1, [int][Math]::Floor(
            (([int64]$Y * 2 + 1) * $height) / (2 * 600))))
    }
    & (Join-Path $root '.tmp_held_click_window.ps1') `
        -ProcessId $script:rebuildPid `
        -WindowHandle $script:rebuildWindow.ToInt64() `
        -X $X -Y $Y -HoldMilliseconds 35
}

function Center-World([object]$Snapshot, [int]$WorldX, [int]$WorldY) {
    $mapWidth = [Math]::Max(1, [int]$Snapshot.visibility.map[0] * 32)
    $mapHeight = [Math]::Max(1, [int]$Snapshot.visibility.map[1] * 32)
    $left = [int]$Snapshot.minimap_layout.output[0]
    $top = [int]$Snapshot.minimap_layout.output[1]
    $width = [Math]::Max(1, [int]$Snapshot.minimap_layout.size[0])
    $height = [Math]::Max(1, [int]$Snapshot.minimap_layout.size[1])
    $x = $left + [Math]::Floor(
        [Math]::Min($mapWidth - 1, [Math]::Max(0, $WorldX)) * $width / $mapWidth)
    $y = $top + [Math]::Floor(
        [Math]::Min($mapHeight - 1, [Math]::Max(0, $WorldY)) * $height / $mapHeight)
    Invoke-Click $x $y
    Start-Sleep -Milliseconds 180
    return Get-Snapshot
}

function Select-Unit([object]$Snapshot, [object]$Unit) {
    $targetId = [int]$Unit.id
    $current = $Snapshot
    for ($attempt = 0; $attempt -lt 4; ++$attempt) {
        if ([int]$current.selected.id -eq $targetId) { return $current }
        $live = @($current.active_units | Where-Object {
            [int]$_.id -eq $targetId -and [bool]$_.active
        } | Select-Object -First 1)[0]
        $current = Center-World $current ([int]$live.world[0]) ([int]$live.world[1])
        $live = @($current.active_units | Where-Object {
            [int]$_.id -eq $targetId -and [bool]$_.active
        } | Select-Object -First 1)[0]
        $mini = @($current.minimap_units | Where-Object {
            [int]$_.id -eq $targetId
        } | Select-Object -First 1)[0]
        $bounds = if ($null -ne $mini -and [int]$mini.bounds[2] -gt 0) {
            @([int]$mini.bounds[0], [int]$mini.bounds[1],
              [int]$mini.bounds[2], [int]$mini.bounds[3])
        } else { @(0, 0, 64, 64) }
        $x = [int]$live.world[0] - [int]$current.camera[0] +
            $bounds[0] + [Math]::Floor($bounds[2] / 2)
        $y = [int]$live.world[1] - [int]$current.camera[1] +
            $bounds[1] + [Math]::Floor($bounds[3] / 2)
        Invoke-Click $x $y
        Start-Sleep -Milliseconds 250
        $current = Get-Snapshot
    }
    throw "Could not select producer unit $targetId."
}

function Get-HotRegion([object]$Snapshot, [int]$Item) {
    return @($Snapshot.hot_regions | Where-Object {
        [int]$_.item -eq $Item -and [bool]$_.enabled
    } | Select-Object -First 1)[0]
}

function Wait-HotRegion([object]$Snapshot, [int]$Item) {
    $deadline = [DateTime]::UtcNow.AddSeconds(4)
    $current = $Snapshot
    do {
        $region = Get-HotRegion $current $Item
        if ($null -ne $region) { return @($current, $region) }
        Start-Sleep -Milliseconds 80
        $current = Get-Snapshot
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Enabled hot item $Item was not published."
}

$monitor = $null
try {
    $pair = & (Join-Path $root '.tmp_route_fresh_pair.ps1') `
        -WorkingDirectory $WorkingDirectory `
        -OriginalName 'OriginalUpgradeParity' `
        -RebuildName 'RebuildUpgradeParity' `
        -TribeIndex $TribeIndex -GameplayWaitSeconds 7
    $script:originalPid = [int]$pair.OriginalPid
    $script:rebuildPid = [int]$pair.RebuildPid
    $script:rebuildBase = Hex-ToInt64 $pair.RebuildBase
    $script:rebuildWindow = [IntPtr](Hex-ToInt64 $pair.RebuildWindow)

    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        $initial = Get-Snapshot
        $ready = ([int]$initial.frame.simulation -gt 0 -and
            @($initial.active_units).Count -gt 0)
        if (-not $ready) { Start-Sleep -Milliseconds 250 }
    } while (-not $ready -and [DateTime]::UtcNow -lt $deadline)
    if (-not $ready) { throw 'Gameplay did not become ready.' }

    $owner = [int]$initial.local_owner
    $producer = @($initial.active_units | Where-Object {
        [int]$_.owner -eq $owner -and [int]$_.type -eq 128 -and [bool]$_.active
    } | Select-Object -First 1)[0]
    if ($null -eq $producer) { throw "Owner $owner type-128 producer not found." }
    $selected = Select-Unit $initial $producer
    $hot = Wait-HotRegion $selected 286
    $selected = $hot[0]
    $region = $hot[1]

    & (Join-Path $root '.tmp_capture_window.ps1') `
        -ProcessId $script:originalPid `
        -OutputPath (Join-Path $output 'before-original.png')
    & (Join-Path $root '.tmp_capture_window.ps1') `
        -ProcessId $script:rebuildPid `
        -OutputPath (Join-Path $output 'before-rebuild.png')

    $trace = Join-Path $output 'upgrade-trace.json'
    $traceErr = Join-Path $output 'upgrade-trace.err'
    $monitor = Start-Process -FilePath $python -ArgumentList @(
        (Join-Path $root '.tmp_upgrade_completion_pair_monitor.py'),
        [string]$script:originalPid, [string]$script:rebuildPid,
        ('0x{0:X}' -f $script:rebuildBase), [string]$producer.slot,
        [string]$owner, '42', [string]$TimeoutSeconds, $layoutPath) `
        -WorkingDirectory $root -RedirectStandardOutput $trace `
        -RedirectStandardError $traceErr -WindowStyle Hidden -PassThru
    Start-Sleep -Milliseconds 250
    Invoke-Click ([int]$region.center[0]) ([int]$region.center[1])
    Start-Sleep -Milliseconds 900
    & (Join-Path $root '.tmp_capture_window.ps1') `
        -ProcessId $script:originalPid `
        -OutputPath (Join-Path $output 'active-original.png')
    & (Join-Path $root '.tmp_capture_window.ps1') `
        -ProcessId $script:rebuildPid `
        -OutputPath (Join-Path $output 'active-rebuild.png')

    Wait-Process -Id $monitor.Id -Timeout ($TimeoutSeconds + 10)
    $monitor.WaitForExit()
    $monitor.Refresh()
    $resultText = [IO.File]::ReadAllText(
        $trace, [Text.UTF8Encoding]::new($false, $true))
    if ([String]::IsNullOrWhiteSpace($resultText)) {
        throw ([IO.File]::ReadAllText($traceErr))
    }
    $result = $resultText | ConvertFrom-Json
    # Start-Process can leave ExitCode unavailable on the first Process
    # wrapper after Wait-Process even though the redirected monitor emitted a
    # complete, passing result.  Treat the structured parity verdict as the
    # authority and retain the native exit code only as failure diagnostics.
    if (-not [bool]$result.pass) {
        $stderrText = [IO.File]::ReadAllText($traceErr)
        throw ("Upgrade monitor failed (exit={0}): {1}" -f
            $monitor.ExitCode, $stderrText)
    }

    & (Join-Path $root '.tmp_capture_window.ps1') `
        -ProcessId $script:originalPid `
        -OutputPath (Join-Path $output 'completed-original.png')
    & (Join-Path $root '.tmp_capture_window.ps1') `
        -ProcessId $script:rebuildPid `
        -OutputPath (Join-Path $output 'completed-rebuild.png')

    [pscustomobject]@{
        pass = [bool]$result.pass
        sha256 = $layout.sha256
        owner = $owner
        producer_slot = [int]$producer.slot
        aligned_frame_count = [int]$result.aligned_frame_count
        first_divergence = $result.first_divergence
        baseline = $result.baseline
        last = $result.last
        output = $output
        processes = $pair
    } | ConvertTo-Json -Depth 16
}
finally {
    if ($null -ne $monitor -and -not $monitor.HasExited) {
        Stop-Process -Id $monitor.Id -Force -ErrorAction SilentlyContinue
    }
    if (-not $KeepProcesses) {
        Get-Process -Id $script:originalPid,$script:rebuildPid `
            -ErrorAction SilentlyContinue | Stop-Process -Force
    }
}
