param(
    [string]$WorkingDirectory = 'RankerOCPV_Win',
    [string]$OutputDirectory = '.tmp_upgrade_p2p_run_current',
    [ValidateRange(0, 3)]
    [int]$TribeIndex = 2,
    [ValidateRange(-1, 63)]
    [int]$UpgradeOrderOverride = -1,
    [ValidateRange(-1, 307)]
    [int]$UpgradeItemOverride = -1,
    [ValidateRange(-1, 169)]
    [int]$ProducerTypeOverride = -1,
    [int[]]$BuildSequence = @(),
    [switch]$AccelerateConstruction,
    [ValidateRange(-1, 17)]
    [int]$EffectSlotProbe = -1,
    [ValidateRange(-1, 169)]
    [int]$EffectTypeProbe = -1,
    [int64]$PrimaryResourceOverride = -1,
    [switch]$HarvestAfterCompletion,
    [int]$BerryMonitorSeconds = 35,
    [ValidateRange(-1, 169)]
    [int]$PostUpgradeProductionType = -1,
    [ValidateRange(-1, 169)]
    [int]$PostUpgradeProducerType = -1,
    [int]$PostProductionTimeoutSeconds = 120,
    [switch]$SkipPostProductionSecondaryCapWait,
    [switch]$PostProductionMove,
    [switch]$PostProductionAttackNeutral,
    [int]$PostProductionMoveSeconds = 10,
    [switch]$FullStateAudit,
    [int]$FullStateAuditSeconds = 240,
    [switch]$CaptureWorkerMenuOnly,
    [int]$TimeoutSeconds = 180,
    [switch]$CaptureActiveOnly,
    [switch]$CancelActive,
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
$producerTypes = @(96, 112, 128, 144)
# Pick one starting-base order per tribe whose original variant-0 cost is 300,
# so the default 400-resource session can exercise start through completion.
# The UI item is always 0xf4 + the raw production order id.
$upgradeItems = @(299, 256, 286, 302)
$upgradeOrders = @(55, 12, 42, 58)
$producerType = [int]$producerTypes[$TribeIndex]
$upgradeItem = [int]$upgradeItems[$TribeIndex]
$upgradeOrder = [int]$upgradeOrders[$TribeIndex]
if ($UpgradeOrderOverride -ge 0) {
    $upgradeOrder = $UpgradeOrderOverride
    $upgradeItem = if ($UpgradeItemOverride -ge 0) {
        $UpgradeItemOverride
    } else {
        0xf4 + $upgradeOrder
    }
}
if ($ProducerTypeOverride -ge 0) {
    $producerType = $ProducerTypeOverride
}

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
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern IntPtr OpenProcess(
        uint access, bool inheritHandle, int processId);
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern bool WriteProcessMemory(
        IntPtr process, IntPtr address, byte[] buffer,
        UIntPtr size, out UIntPtr written);
    [DllImport("kernel32.dll")]
    public static extern bool CloseHandle(IntPtr handle);
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

function Invoke-ContextAttackClickAndCaptureHover([int]$X, [int]$Y) {
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
    $lparam = [IntPtr](($X -band 0xffff) -bor (($Y -band 0xffff) -shl 16))
    [UpgradePairWindowInput]::SendMessage(
        $script:rebuildWindow, 0x200, [IntPtr]0, $lparam) | Out-Null
    [UpgradePairWindowInput]::SendMessage(
        $script:rebuildWindow, 0x204, [IntPtr]2, $lparam) | Out-Null
    try {
        Start-Sleep -Milliseconds 80
        return Get-Snapshot
    }
    finally {
        [UpgradePairWindowInput]::SendMessage(
            $script:rebuildWindow, 0x205, [IntPtr]0, $lparam) | Out-Null
    }
}

function Invoke-RightClick([int]$X, [int]$Y) {
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
    $lparam = [IntPtr](($X -band 0xffff) -bor (($Y -band 0xffff) -shl 16))
    [UpgradePairWindowInput]::SendMessage(
        $script:rebuildWindow, 0x204, [IntPtr]2, $lparam) | Out-Null
    Start-Sleep -Milliseconds 35
    [UpgradePairWindowInput]::SendMessage(
        $script:rebuildWindow, 0x205, [IntPtr]0, $lparam) | Out-Null
}

function Invoke-DragSelect([int]$Left, [int]$Top, [int]$Right, [int]$Bottom) {
    $rect = [UpgradePairWindowInput+Rect]::new()
    if ([UpgradePairWindowInput]::GetClientRect(
            $script:rebuildWindow, [ref]$rect)) {
        $width = [Math]::Max(1, $rect.Right - $rect.Left)
        $height = [Math]::Max(1, $rect.Bottom - $rect.Top)
        $scaleX = { param([int]$value) [Math]::Max(0,
            [Math]::Min($width - 1, [int][Math]::Floor(
                (([int64]$value * 2 + 1) * $width) / 1600))) }
        $scaleY = { param([int]$value) [Math]::Max(0,
            [Math]::Min($height - 1, [int][Math]::Floor(
                (([int64]$value * 2 + 1) * $height) / 1200))) }
        $Left = & $scaleX $Left
        $Right = & $scaleX $Right
        $Top = & $scaleY $Top
        $Bottom = & $scaleY $Bottom
    }
    $start = [IntPtr](($Left -band 0xffff) -bor (($Top -band 0xffff) -shl 16))
    $finish = [IntPtr](($Right -band 0xffff) -bor (($Bottom -band 0xffff) -shl 16))
    [UpgradePairWindowInput]::SendMessage(
        $script:rebuildWindow, 0x201, [IntPtr]1, $start) | Out-Null
    Start-Sleep -Milliseconds 120
    for ($step = 1; $step -le 10; ++$step) {
        $x = $Left + [Math]::Floor(($Right - $Left) * $step / 10)
        $y = $Top + [Math]::Floor(($Bottom - $Top) * $step / 10)
        $point = [IntPtr](($x -band 0xffff) -bor (($y -band 0xffff) -shl 16))
        [UpgradePairWindowInput]::SendMessage(
            $script:rebuildWindow, 0x200, [IntPtr]1, $point) | Out-Null
        Start-Sleep -Milliseconds 60
    }
    [UpgradePairWindowInput]::SendMessage(
        $script:rebuildWindow, 0x202, [IntPtr]0, $finish) | Out-Null
}

function Invoke-Move([int]$X, [int]$Y) {
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
    $lparam = [IntPtr](($X -band 0xffff) -bor (($Y -band 0xffff) -shl 16))
    [UpgradePairWindowInput]::SendMessage(
        $script:rebuildWindow, 0x200, [IntPtr]0, $lparam) | Out-Null
}

function Invoke-PhysicalAttackHotkey {
    [UpgradePairWindowInput]::SendMessage(
        $script:rebuildWindow, 0x0100, [IntPtr]0x41,
        [IntPtr][int64]0x001e0001) | Out-Null
    Start-Sleep -Milliseconds 35
    [UpgradePairWindowInput]::SendMessage(
        $script:rebuildWindow, 0x0101, [IntPtr]0x41,
        [IntPtr][int64]0xc01e0001) | Out-Null
}

function Set-ProcessU32([int]$ProcessId, [int64]$Address, [uint32]$Value) {
    $handle = [UpgradePairWindowInput]::OpenProcess(0x0420, $false, $ProcessId)
    if ($handle -eq [IntPtr]::Zero) {
        throw "OpenProcess($ProcessId) failed: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
    }
    try {
        $bytes = [BitConverter]::GetBytes($Value)
        [UIntPtr]$written = [UIntPtr]::Zero
        [UIntPtr]$byteCount = [UIntPtr]::new([uint64]4)
        if (-not [UpgradePairWindowInput]::WriteProcessMemory(
                $handle, [IntPtr]$Address, $bytes, $byteCount,
                [ref]$written) -or $written.ToUInt64() -ne 4) {
            throw "WriteProcessMemory(0x$($Address.ToString('X'))) failed: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
        }
    }
    finally {
        [UpgradePairWindowInput]::CloseHandle($handle) | Out-Null
    }
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
    for ($attempt = 0; $attempt -lt 8; ++$attempt) {
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
        Start-Sleep -Milliseconds 350
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
$postProductionMonitor = $null
$fullAuditProcess = $null
$fullAuditStop = $null
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
    if ($PrimaryResourceOverride -ge 0) {
        if ($PrimaryResourceOverride -gt [uint32]::MaxValue) {
            throw 'PrimaryResourceOverride exceeds uint32.'
        }
        $primaryValue = [uint32]$PrimaryResourceOverride
        Set-ProcessU32 $script:originalPid (0x00725244L + $owner * 4L) `
            $primaryValue
        $runtime = $script:rebuildBase + $runtimeRva
        $lifecycle = $runtime + $lifecycleOffset
        $production = $runtime + (Hex-ToInt64 $layout.production_runtime_offset)
        $lifecyclePrimary = Hex-ToInt64 $layout.lifecycle_layout.primary
        $productionPrimary = Hex-ToInt64 `
            $layout.production_runtime_layout.primary_resources
        Set-ProcessU32 $script:rebuildPid `
            ($lifecycle + $lifecyclePrimary + $owner * 4L) $primaryValue
        Set-ProcessU32 $script:rebuildPid `
            ($production + $productionPrimary + $owner * 4L) $primaryValue
        Start-Sleep -Milliseconds 120
        $initial = Get-Snapshot
    }
    if ($FullStateAudit) {
        $fullAuditResultPath = Join-Path $output 'next-divergence-result.json'
        $fullAuditJournalPath = Join-Path $output 'next-divergence-journal.jsonl'
        $fullAuditStop = Join-Path $output 'next-divergence-stop.marker'
        $fullAuditOut = Join-Path $output 'next-divergence.out'
        $fullAuditErr = Join-Path $output 'next-divergence.err'
        Remove-Item -LiteralPath $fullAuditResultPath,$fullAuditJournalPath,
            $fullAuditStop,$fullAuditOut,$fullAuditErr -Force `
            -ErrorAction SilentlyContinue
        $fullAuditProcess = Start-Process -FilePath $python -ArgumentList @(
            (Join-Path $root '.tmp_nextdiv_compact_audit_frame2.py'),
            [string]$script:originalPid, [string]$script:rebuildPid,
            ('0x{0:X}' -f $script:rebuildBase), $layoutPath,
            $fullAuditResultPath, $fullAuditJournalPath, $fullAuditStop,
            [string]$FullStateAuditSeconds) -WorkingDirectory $root `
            -RedirectStandardOutput $fullAuditOut `
            -RedirectStandardError $fullAuditErr -WindowStyle Hidden -PassThru
    }
    $buildEvidence = @()
    foreach ($buildTypeValue in @($BuildSequence)) {
        $buildType = [int]$buildTypeValue
        $existing = @($initial.active_units | Where-Object {
            [int]$_.owner -eq $owner -and [int]$_.type -eq $buildType -and
            [bool]$_.active -and -not [bool]$_.under_construction
        } | Select-Object -First 1)[0]
        if ($null -ne $existing) {
            $buildEvidence += [pscustomobject]@{
                type=$buildType; slot=[int]$existing.slot; reused=$true
            }
            continue
        }

        $workerType = 16 * $TribeIndex
        $workerDeadline = [DateTime]::UtcNow.AddSeconds(5)
        do {
            $worker = @($initial.active_units | Where-Object {
                [int]$_.owner -eq $owner -and [int]$_.type -eq $workerType -and
                [bool]$_.active -and -not [bool]$_.attached -and
                [string]$_.state -eq '0x00000001'
            } | Select-Object -First 1)[0]
            if ($null -eq $worker) {
                Start-Sleep -Milliseconds 100
                $initial = Get-Snapshot
            }
        } while ($null -eq $worker -and
            [DateTime]::UtcNow -lt $workerDeadline)
        if ($null -eq $worker) {
            throw "No detached type-$workerType worker for type-$buildType build."
        }
        $workerSelected = Select-Unit $initial $worker
        $buildOption = @($workerSelected.options | Where-Object {
            [int]$_.item -eq $buildType -and [bool]$_.enabled
        } | Select-Object -First 1)[0]
        if ($null -eq $buildOption) {
            throw "Type-$buildType has no enabled worker production option."
        }
        $categoryItem = 194 + [int]$buildOption.aux
        $buildMenu = $workerSelected
        $buildRegion = @($buildMenu.hot_regions | Where-Object {
            [int]$_.item -eq $buildType -and [bool]$_.enabled
        } | Select-Object -First 1)[0]
        if ($null -eq $buildRegion) {
            $category = Wait-HotRegion $workerSelected $categoryItem
            Invoke-Click ([int]$category[1].center[0]) ([int]$category[1].center[1])
            Start-Sleep -Milliseconds 180
            $buildMenu = Get-Snapshot
            $buildRegion = @($buildMenu.hot_regions | Where-Object {
                [int]$_.item -eq $buildType -and [bool]$_.enabled
            } | Select-Object -First 1)[0]
        }
        if ($null -eq $buildRegion) {
            $available = @($buildMenu.hot_regions | Where-Object {
                [int]$_.item -ge 96 -and [int]$_.item -lt 170 -and
                [bool]$_.enabled
            } | ForEach-Object { [int]$_.item }) -join ','
            throw "Type-$buildType build unavailable; enabled types=$available"
        }
        $priorSlots = @($buildMenu.active_units | Where-Object {
            [int]$_.owner -eq $owner -and [int]$_.type -eq $buildType
        } | ForEach-Object { [int]$_.slot })
        Invoke-Click ([int]$buildRegion.center[0]) ([int]$buildRegion.center[1])
        Start-Sleep -Milliseconds 150
        $placement = Get-Snapshot
        if ([int]$placement.placement.mode -ne 6) {
            throw "Type-$buildType did not enter placement mode."
        }
        $anchor = @($placement.active_units | Where-Object {
            [int]$_.owner -eq $owner -and
            [int]$_.type -eq [int]$producerTypes[$TribeIndex]
        } | Select-Object -First 1)[0]
        if ($null -eq $anchor) { $anchor = $worker }
        $mapCenterX = [int]$placement.visibility.map[0] * 16
        $mapCenterY = [int]$placement.visibility.map[1] * 16
        $stepX = if ([int]$anchor.world[0] -lt $mapCenterX) { 288 } else { -288 }
        $stepY = if ([int]$anchor.world[1] -lt $mapCenterY) { 96 } else { -96 }
        $candidateOffsets = @(
            @(0,0), @(96,0), @(-96,0), @(0,96), @(0,-96),
            @(192,0), @(-192,0), @(96,96), @(-96,-96),
            @(192,96), @(-192,-96))
        foreach ($radius in @(192,288,384,480,576,672,768)) {
            foreach ($dx in @(-$radius,0,$radius)) {
                foreach ($dy in @(-$radius,0,$radius)) {
                    if ($dx -ne 0 -or $dy -ne 0) {
                        $candidateOffsets += ,@($dx,$dy)
                    }
                }
            }
        }
        $validPreview = $null
        $validWorld = $null
        foreach ($candidate in $candidateOffsets) {
            $worldX = [int]$anchor.world[0] + $stepX + [int]$candidate[0]
            $worldY = [int]$anchor.world[1] + $stepY + [int]$candidate[1]
            if ($worldX -lt 64 -or $worldY -lt 64 -or
                $worldX -ge [int]$placement.visibility.map[0] * 32 - 64 -or
                $worldY -ge [int]$placement.visibility.map[1] * 32 - 64) {
                continue
            }
            $view = Center-World $placement $worldX $worldY
            $screenX = $worldX - [int]$view.camera[0]
            $screenY = $worldY - [int]$view.camera[1]
            Invoke-Move $screenX $screenY
            Start-Sleep -Milliseconds 120
            $preview = Get-Snapshot
            if ([bool]$preview.placement.valid -and
                @($preview.placement.cell_validity | Where-Object {
                    [int]$_ -eq 0
                }).Count -eq 0) {
                $validPreview = $preview
                $validWorld = @($worldX,$worldY)
                break
            }
        }
        if ($null -eq $validPreview) {
            throw "No valid placement for type-$buildType."
        }
        $placeX = [int]$validWorld[0] - [int]$validPreview.camera[0]
        $placeY = [int]$validWorld[1] - [int]$validPreview.camera[1]
        Invoke-Click $placeX $placeY

        $spawnDeadline = [DateTime]::UtcNow.AddSeconds(20)
        $spawned = $null
        do {
            Start-Sleep -Milliseconds 100
            $initial = Get-Snapshot
            $spawned = @($initial.active_units | Where-Object {
                [int]$_.owner -eq $owner -and [int]$_.type -eq $buildType -and
                [int]$_.slot -notin $priorSlots
            } | Select-Object -First 1)[0]
        } while ($null -eq $spawned -and
            [DateTime]::UtcNow -lt $spawnDeadline)
        if ($null -eq $spawned) {
            throw "Type-$buildType did not spawn."
        }

        $workStartDeadline = [DateTime]::UtcNow.AddSeconds(15)
        do {
            $builder = @($initial.active_units | Where-Object {
                [int]$_.id -eq [int]$worker.id
            } | Select-Object -First 1)[0]
            $spawned = @($initial.active_units | Where-Object {
                [int]$_.id -eq [int]$spawned.id
            } | Select-Object -First 1)[0]
            $workStarted = ($null -ne $builder -and [bool]$builder.attached) -or
                ($null -ne $spawned -and [int]$spawned.health[0] -gt 1)
            if (-not $workStarted) {
                Start-Sleep -Milliseconds 100
                $initial = Get-Snapshot
            }
        } while (-not $workStarted -and
            [DateTime]::UtcNow -lt $workStartDeadline)
        if (-not $workStarted) {
            throw "Type-$buildType construction work did not begin."
        }
        if ($AccelerateConstruction) {
            $nearComplete = [Math]::Max(1, [int]$spawned.health[1] - 1)
            Set-ProcessU32 $script:originalPid `
                (0x00A03FB8L + [int64]$spawned.id + 0x18L) ([uint32]$nearComplete)
            Set-ProcessU32 $script:rebuildPid `
                ((Hex-ToInt64 $spawned.address) +
                    (Hex-ToInt64 $layout.unit_layout.health)) ([uint32]$nearComplete)
        }
        $constructionWaitSeconds = if ($AccelerateConstruction) { 8 } else { 180 }
        $completeDeadline = [DateTime]::UtcNow.AddSeconds($constructionWaitSeconds)
        do {
            Start-Sleep -Milliseconds 100
            $initial = Get-Snapshot
            $completed = @($initial.active_units | Where-Object {
                [int]$_.id -eq [int]$spawned.id
            } | Select-Object -First 1)[0]
        } while (($null -eq $completed -or [bool]$completed.under_construction) -and
            [DateTime]::UtcNow -lt $completeDeadline)
        if ($null -eq $completed -or [bool]$completed.under_construction) {
            throw "Type-$buildType did not complete after progress alignment."
        }
        $buildEvidence += [pscustomobject]@{
            type=$buildType; slot=[int]$completed.slot; reused=$false
            world=@([int]$completed.world[0],[int]$completed.world[1])
            health=@([int]$completed.health[0],[int]$completed.health[1])
        }
        Start-Sleep -Milliseconds 500
        $initial = Get-Snapshot
    }
    if ($CaptureWorkerMenuOnly) {
        $workerType = 16 * $TribeIndex
        $worker = @($initial.active_units | Where-Object {
            [int]$_.owner -eq $owner -and [int]$_.type -eq $workerType -and
            [bool]$_.active -and -not [bool]$_.attached
        } | Select-Object -First 1)[0]
        if ($null -eq $worker) {
            throw "Owner $owner type-$workerType worker not found."
        }
        $workerSelected = Select-Unit $initial $worker
        $buildMenu = $workerSelected
        $publishedBuildItems = @($buildMenu.hot_regions | Where-Object {
            [int]$_.item -ge 96 -and [int]$_.item -lt 170 -and
            [bool]$_.enabled
        })
        if ($publishedBuildItems.Count -eq 0) {
            $category = Wait-HotRegion $workerSelected 194
            Invoke-Click ([int]$category[1].center[0]) ([int]$category[1].center[1])
            Start-Sleep -Milliseconds 250
            $buildMenu = Get-Snapshot
        }
        [IO.File]::WriteAllText(
            (Join-Path $output 'worker-build-menu.json'),
            ($buildMenu | ConvertTo-Json -Depth 32),
            [Text.UTF8Encoding]::new($false))
        [pscustomobject]@{
            pass = $true
            sha256 = $layout.sha256
            owner = $owner
            tribe = $TribeIndex
            worker_type = $workerType
            builds = $buildEvidence
            build_items = @($buildMenu.hot_regions | Where-Object {
                [int]$_.item -ge 96 -and [int]$_.item -lt 170
            } | ForEach-Object {
                [pscustomobject]@{
                    item = [int]$_.item
                    enabled = [bool]$_.enabled
                    center = @([int]$_.center[0], [int]$_.center[1])
                }
            })
            output = $output
        } | ConvertTo-Json -Depth 8
        return
    }
    $producer = @($initial.active_units | Where-Object {
        [int]$_.owner -eq $owner -and [int]$_.type -eq $producerType -and
        [bool]$_.active
    } | Select-Object -First 1)[0]
    if ($null -eq $producer) {
        throw "Owner $owner type-$producerType producer not found."
    }
    $selected = Select-Unit $initial $producer
    [IO.File]::WriteAllText(
        (Join-Path $output 'selected-before-action.json'),
        ($selected | ConvertTo-Json -Depth 32),
        [Text.UTF8Encoding]::new($false))
    $hot = Wait-HotRegion $selected $upgradeItem
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
        [string]$owner, [string]$upgradeOrder,
        [string]$TimeoutSeconds, $layoutPath,
        [string]$EffectSlotProbe, [string]$EffectTypeProbe) `
        -WorkingDirectory $root -RedirectStandardOutput $trace `
        -RedirectStandardError $traceErr -WindowStyle Hidden -PassThru
    Start-Sleep -Milliseconds 250
    Invoke-Click ([int]$region.center[0]) ([int]$region.center[1])
    Start-Sleep -Milliseconds 900
    $activeSnapshot = Get-Snapshot
    [IO.File]::WriteAllText(
        (Join-Path $output 'active-rebuild.json'),
        ($activeSnapshot | ConvertTo-Json -Depth 32),
        [Text.UTF8Encoding]::new($false))
    & (Join-Path $root '.tmp_capture_window.ps1') `
        -ProcessId $script:originalPid `
        -OutputPath (Join-Path $output 'active-original.png')
    & (Join-Path $root '.tmp_capture_window.ps1') `
        -ProcessId $script:rebuildPid `
        -OutputPath (Join-Path $output 'active-rebuild.png')

    if ($CaptureActiveOnly -and -not $CancelActive) {
        [pscustomobject]@{
            pass = $true
            sha256 = $layout.sha256
            owner = $owner
            tribe = $TribeIndex
            producer_type = $producerType
            producer_slot = [int]$producer.slot
            upgrade_item = $upgradeItem
            upgrade_order = $upgradeOrder
            selected = $activeSnapshot.selected
            progress_commands = $activeSnapshot.progress_commands
            hot_regions = $activeSnapshot.hot_regions
            output = $output
        } | ConvertTo-Json -Depth 32
        return
    }

    if ($CancelActive) {
        $activeRegion = @($activeSnapshot.hot_regions | Where-Object {
            [int]$_.item -eq 427 -and [int]$_.aux -eq $upgradeOrder -and
            [bool]$_.enabled
        } | Select-Object -First 1)[0]
        if ($null -eq $activeRegion) {
            throw "Active upgrade queue item 427/$upgradeOrder was not published."
        }
        Invoke-Click ([int]$activeRegion.center[0]) ([int]$activeRegion.center[1])
        Wait-Process -Id $monitor.Id -Timeout ($TimeoutSeconds + 10)
        $monitor.WaitForExit()
        $resultText = [IO.File]::ReadAllText(
            $trace, [Text.UTF8Encoding]::new($false, $true))
        if ([String]::IsNullOrWhiteSpace($resultText)) {
            throw ([IO.File]::ReadAllText($traceErr))
        }
        $result = $resultText | ConvertFrom-Json
        $activeOriginal = @($result.events | Where-Object {
            [int]$_.original.primary -eq 100 -and
            [int]$_.original.variant -eq 0 -and
            [int]$_.original.lock -ne 0
        }).Count -gt 0
        $activeRebuild = @($result.events | Where-Object {
            [int]$_.rebuild.primary -eq 100 -and
            [int]$_.rebuild.variant -eq 0 -and
            [int]$_.rebuild.lock -ne 0
        }).Count -gt 0
        $last = $result.last
        $cancelPass = ($null -eq $result.error -and
            $null -eq $result.first_divergence -and
            $activeOriginal -and $activeRebuild -and
            [int]$last.original.primary -eq 400 -and
            [int]$last.rebuild.primary -eq 400 -and
            [int]$last.original.secondary -eq [int]$last.rebuild.secondary -and
            [int]$last.original.variant -eq 0 -and
            [int]$last.rebuild.variant -eq 0 -and
            [int]$last.original.lock -eq 0 -and
            [int]$last.rebuild.lock -eq 0)
        [pscustomobject]@{
            pass = $cancelPass
            sha256 = $layout.sha256
            owner = $owner
            tribe = $TribeIndex
            producer_type = $producerType
            producer_slot = [int]$producer.slot
            upgrade_item = $upgradeItem
            upgrade_order = $upgradeOrder
            active_seen = @($activeOriginal, $activeRebuild)
            aligned_frame_count = [int]$result.aligned_frame_count
            first_divergence = $result.first_divergence
            baseline = $result.baseline
            last = $last
            output = $output
        } | ConvertTo-Json -Depth 32
        if (-not $cancelPass) {
            throw 'Active upgrade cancellation parity failed.'
        }
        return
    }

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

    $harvestResult = $null
    if ($HarvestAfterCompletion) {
        $completedSnapshot = Get-Snapshot
        $workerType = @(0, 16, 32, 48)[$TribeIndex]
        $worker = @($completedSnapshot.active_units | Where-Object {
            [int]$_.owner -eq $owner -and [int]$_.type -eq $workerType -and
            [bool]$_.active -and -not [bool]$_.attached
        } | Select-Object -First 1)[0]
        if ($null -eq $worker) {
            throw "Owner $owner type-$workerType worker not found after upgrade."
        }
        $workerSelected = Select-Unit $completedSnapshot $worker
        $resource = Invoke-PythonJson `
            (Join-Path $root '.tmp_find_nearest_resource_tile.py') @(
                [string]$script:rebuildPid,
                ('0x{0:X}' -f $script:rebuildBase),
                ('0x{0:X}' -f $runtimeRva),
                ('0x{0:X}' -f $movementOffset),
                [string]$worker.world[0], [string]$worker.world[1],
                $layoutPath)
        $berryView = Center-World $workerSelected `
            ([int]$resource.world[0]) ([int]$resource.world[1])
        $berryOut = Join-Path $output 'berry-after-upgrade.jsonl'
        $berryErr = Join-Path $output 'berry-after-upgrade.err'
        $berryMonitor = Start-Process -FilePath $python -ArgumentList @(
            (Join-Path $root '.tmp_newbuild_berry_pair_monitor.py'),
            [string]$script:originalPid, [string]$script:rebuildPid,
            ('0x{0:X}' -f $script:rebuildBase), [string]$worker.slot,
            [string]$BerryMonitorSeconds, [string]$resource.tile[0],
            [string]$resource.tile[1], [string]$owner,
            ('0x{0:X}' -f $runtimeRva), '0',
            ('0x{0:X}' -f $movementOffset),
            ('0x{0:X}' -f $lifecycleOffset), $layoutPath) `
            -WorkingDirectory $root -RedirectStandardOutput $berryOut `
            -RedirectStandardError $berryErr -WindowStyle Hidden -PassThru
        Start-Sleep -Milliseconds 250
        $screenX = [int]$resource.world[0] - [int]$berryView.camera[0]
        $screenY = [int]$resource.world[1] - [int]$berryView.camera[1]
        Invoke-RightClick $screenX $screenY
        Wait-Process -Id $berryMonitor.Id -Timeout ($BerryMonitorSeconds + 10)
        $alignedRows = @([IO.File]::ReadLines($berryOut) | ForEach-Object {
            if (-not [String]::IsNullOrWhiteSpace($_)) {
                $row = $_ | ConvertFrom-Json
                if ([string]$row.kind -eq 'aligned') { $row }
            }
        })
        $firstMismatch = @($alignedRows | Where-Object {
            $_.original.state -ne $_.rebuild.state -or
            [int]$_.original.cargo -ne [int]$_.rebuild.cargo -or
            [int]$_.original.primary -ne [int]$_.rebuild.primary -or
            [int]$_.original.secondary -ne [int]$_.rebuild.secondary -or
            [int]$_.original.resource_score -ne [int]$_.rebuild.resource_score -or
            [int]$_.original.tile_amount -ne [int]$_.rebuild.tile_amount -or
            -not [bool]$_.rng_equal -or -not [bool]$_.world_equal -or
            -not [bool]$_.path_equal -or -not [bool]$_.destination_equal -or
            -not [bool]$_.current_cell_equal -or -not [bool]$_.next_path_equal
        } | Select-Object -First 1)[0]
        $cargoValues = @($alignedRows | ForEach-Object {
            if ([int]$_.original.cargo -eq [int]$_.rebuild.cargo) {
                [int]$_.original.cargo
            }
        } | Sort-Object -Unique)
        $harvestResult = [ordered]@{
            pass = ($alignedRows.Count -gt 0 -and $null -eq $firstMismatch -and
                $cargoValues -contains 16)
            aligned_frame_count = $alignedRows.Count
            first_mismatch = $firstMismatch
            cargo_values = $cargoValues
            expected_first_batch = 16
            resource = $resource
            worker_slot = [int]$worker.slot
        }
        if (-not [bool]$harvestResult.pass) {
            throw 'Post-upgrade berry harvest parity failed.'
        }
    }

    $postProductionResult = $null
    if ($PostUpgradeProductionType -ge 0) {
        if ($PostUpgradeProducerType -lt 0) {
            throw 'PostUpgradeProducerType is required with PostUpgradeProductionType.'
        }
        $postSnapshot = Get-Snapshot
        $postProducer = @($postSnapshot.active_units | Where-Object {
            [int]$_.owner -eq $owner -and
            [int]$_.type -eq $PostUpgradeProducerType -and
            [bool]$_.active -and -not [bool]$_.under_construction
        } | Select-Object -First 1)[0]
        if ($null -eq $postProducer) {
            throw "Post-upgrade type-$PostUpgradeProducerType producer not found."
        }
        $postSelected = Select-Unit $postSnapshot $postProducer
        $postHot = Wait-HotRegion $postSelected $PostUpgradeProductionType
        $postSelected = $postHot[0]
        $postRegion = $postHot[1]
        $preOrderSlots = @($postSelected.active_units | ForEach-Object {
            [int]$_.slot
        })
        $postTrace = Join-Path $output 'post-upgrade-production-trace.json'
        $postTraceErr = Join-Path $output 'post-upgrade-production-trace.err'
        $postProductionMonitor = Start-Process -FilePath $python -ArgumentList @(
            (Join-Path $root '.tmp_production_completion_pair_monitor.py'),
            [string]$script:originalPid, [string]$script:rebuildPid,
            ('0x{0:X}' -f $script:rebuildBase), [string]$postProducer.slot,
            [string]$owner, [string]$PostUpgradeProductionType,
            [string]$PostProductionTimeoutSeconds,
            ('0x{0:X}' -f $runtimeRva),
            ('0x{0:X}' -f $movementOffset),
            ('0x{0:X}' -f $lifecycleOffset), $layoutPath,
            ($preOrderSlots -join ','), '1',
            [string]$PostUpgradeProductionType,
            $(if ($SkipPostProductionSecondaryCapWait) { 'false' } else { 'true' })) `
            -WorkingDirectory $root -RedirectStandardOutput $postTrace `
            -RedirectStandardError $postTraceErr -WindowStyle Hidden -PassThru
        Start-Sleep -Milliseconds 250
        Invoke-Click ([int]$postRegion.center[0]) ([int]$postRegion.center[1])
        Wait-Process -Id $postProductionMonitor.Id `
            -Timeout ($PostProductionTimeoutSeconds + 10)
        $postProductionMonitor.WaitForExit()
        $postText = [IO.File]::ReadAllText(
            $postTrace, [Text.UTF8Encoding]::new($false, $true))
        if ([String]::IsNullOrWhiteSpace($postText)) {
            throw ([IO.File]::ReadAllText($postTraceErr))
        }
        $postProductionResult = $postText | ConvertFrom-Json
        $secondaryCapPass = ($SkipPostProductionSecondaryCapWait -or
            [bool]$postProductionResult.secondary_cap_observed)
        $postPass = ([bool]$postProductionResult.completed -and
            [bool]$postProductionResult.completion_contract_matched -and
            [bool]$postProductionResult.post_spawn_separation.pass -and
            [bool]$postProductionResult.post_spawn_separation.rng_parity -and
            $secondaryCapPass)
        if (-not $postPass) {
            throw 'Post-upgrade unit production/secondary-cap parity failed.'
        }

        if ($PostProductionMove -or $PostProductionAttackNeutral) {
            $moveSnapshot = Get-Snapshot
            $spawnedSlot = [int]$postProductionResult.completion.rebuild.spawned_slot
            $spawnedUnit = @($moveSnapshot.active_units | Where-Object {
                [int]$_.slot -eq $spawnedSlot -and
                [int]$_.type -eq $PostUpgradeProductionType -and
                [int]$_.owner -eq $owner
            } | Select-Object -First 1)[0]
            if ($null -eq $spawnedUnit) {
                throw 'Post-upgrade spawned unit disappeared before box selection.'
            }
            $selectionInjection = Invoke-PythonJson `
                (Join-Path $root '.tmp_force_rebuild_selection.py') @(
                    [string]$script:rebuildPid,
                    ('0x{0:X}' -f $script:rebuildBase),
                    ('0x{0:X}' -f $overlayRva), $layoutPath,
                    [string]$spawnedUnit.id,
                    [string]$spawnedUnit.type,
                    [string]$spawnedUnit.owner)
            Start-Sleep -Milliseconds 300
            $selectedForMove = Get-Snapshot
            if (@($selectedForMove.selected.ids | Where-Object {
                    [int]$_ -eq [int]$spawnedUnit.id
                }).Count -eq 0) {
                throw 'Injected post-upgrade selection was not retained.'
            }
            $neutralTarget = $null
            if ($PostProductionAttackNeutral) {
                $neutralTarget = @($selectedForMove.active_units | Where-Object {
                    [int]$_.owner -ge 8 -and [int]$_.type -ge 70 -and
                    [int]$_.type -le 78 -and [bool]$_.active -and
                    [int]$_.health[0] -gt 0
                } | Sort-Object {
                    [Math]::Abs([int]$_.world[0] - [int]$spawnedUnit.world[0]) +
                    [Math]::Abs([int]$_.world[1] - [int]$spawnedUnit.world[1])
                } | Select-Object -First 1)[0]
                if ($null -eq $neutralTarget) {
                    throw 'No live neutral target for post-upgrade combat.'
                }
                $destinationX = [int]$neutralTarget.world[0]
                $destinationY = [int]$neutralTarget.world[1]
            } else {
                $mapMaxX = [int]$selectedForMove.visibility.map[0] * 32 - 96
                $mapMaxY = [int]$selectedForMove.visibility.map[1] * 32 - 96
                $direction = if ([int]$spawnedUnit.world[0] -lt $mapMaxX / 2) { 1 } else { -1 }
                $destinationX = [Math]::Max(96, [Math]::Min($mapMaxX,
                    [int]$spawnedUnit.world[0] + $direction * 512))
                $destinationY = [Math]::Max(96, [Math]::Min($mapMaxY,
                    [int]$spawnedUnit.world[1] + 192))
            }
            $destinationView = Center-World $selectedForMove $destinationX $destinationY
            if (@($destinationView.selected.ids | Where-Object {
                    [int]$_ -eq [int]$spawnedUnit.id
                }).Count -eq 0) {
                $selectionInjection = Invoke-PythonJson `
                    (Join-Path $root '.tmp_force_rebuild_selection.py') @(
                        [string]$script:rebuildPid,
                        ('0x{0:X}' -f $script:rebuildBase),
                        ('0x{0:X}' -f $overlayRva), $layoutPath,
                        [string]$spawnedUnit.id,
                        [string]$spawnedUnit.type,
                        [string]$spawnedUnit.owner)
                Start-Sleep -Milliseconds 250
                $destinationView = Get-Snapshot
            }
            if ($PostProductionAttackNeutral) {
                $hoverConfirmed = $false
                $targetVisibleAtDispatch = $false
                $hoverAttempts = @()
                for ($hoverAttempt = 0; $hoverAttempt -lt 6; ++$hoverAttempt) {
                    $liveNeutral = @($destinationView.active_units | Where-Object {
                        [int]$_.id -eq [int]$neutralTarget.id
                    } | Select-Object -First 1)[0]
                    if ($null -eq $liveNeutral) { break }
                    $destinationX = [int]$liveNeutral.world[0]
                    $destinationY = [int]$liveNeutral.world[1]
                    if ($hoverAttempt -gt 0) {
                        $destinationView = Center-World $destinationView `
                            $destinationX $destinationY
                        if (@($destinationView.selected.ids | Where-Object {
                                [int]$_ -eq [int]$spawnedUnit.id
                            }).Count -eq 0) {
                            $selectionInjection = Invoke-PythonJson `
                                (Join-Path $root '.tmp_force_rebuild_selection.py') @(
                                    [string]$script:rebuildPid,
                                    ('0x{0:X}' -f $script:rebuildBase),
                                    ('0x{0:X}' -f $overlayRva), $layoutPath,
                                    [string]$spawnedUnit.id,
                                    [string]$spawnedUnit.type,
                                    [string]$spawnedUnit.owner)
                            Start-Sleep -Milliseconds 180
                            $destinationView = Get-Snapshot
                        }
                    }
                    $neutralMini = @($destinationView.minimap_units | Where-Object {
                        [int]$_.id -eq [int]$neutralTarget.id
                    } | Select-Object -First 1)[0]
                    # Center-World and a possible selection reinjection both
                    # advance the live simulation.  Roaming neutrals can move
                    # beyond their narrow sprite bounds during that interval;
                    # use the post-center vector position, not the stale point
                    # captured before camera movement.
                    if ($null -ne $neutralMini) {
                        $destinationX = [int]$neutralMini.world[0]
                        $destinationY = [int]$neutralMini.world[1]
                    }
                    $commandX = $destinationX - [int]$destinationView.camera[0]
                    $commandY = $destinationY - [int]$destinationView.camera[1]
                    if ($null -ne $neutralMini -and
                        [int]$neutralMini.bounds[2] -gt 0) {
                        $commandX += [int]$neutralMini.bounds[0] +
                            [Math]::Floor([int]$neutralMini.bounds[2] / 2)
                        $commandY += [int]$neutralMini.bounds[1] +
                            [Math]::Floor([int]$neutralMini.bounds[3] / 2)
                    }
                    if ($null -eq $neutralMini -or
                        -not [bool]$neutralMini.visible -or
                        [bool]$neutralMini.hidden -or
                        -not [bool]$neutralMini.special_visibility) {
                        # A hidden neutral is a ground point until the selected
                        # unit supplies vision, matching the original target
                        # resolver.  Approach it before issuing A + click.
                        Invoke-RightClick `
                            ($destinationX - [int]$destinationView.camera[0]) `
                            ($destinationY - [int]$destinationView.camera[1])
                        Start-Sleep -Seconds 3
                        $destinationView = Get-Snapshot
                        continue
                    }
                    $targetVisibleAtDispatch = $true
                    # Resolve and capture the target while RBUTTONDOWN remains
                    # held. Releasing the button can legitimately clear hover
                    # after a roaming neutral leaves its narrow sprite bounds.
                    $hoverSnapshot = Invoke-ContextAttackClickAndCaptureHover `
                        $commandX $commandY
                    $hoverAttempts += [pscustomobject]@{
                        attempt=$hoverAttempt
                        target_id=[int]$neutralTarget.id
                        target_world=@($destinationX,$destinationY)
                        camera=@([int]$destinationView.camera[0],
                            [int]$destinationView.camera[1])
                        bounds=@($neutralMini.bounds)
                        command_point=@($commandX,$commandY)
                        visible=[bool]$neutralMini.visible
                        hidden=[bool]$neutralMini.hidden
                        special_visibility=[bool]$neutralMini.special_visibility
                        hover=$hoverSnapshot.hover
                        selected=$hoverSnapshot.selected
                    }
                    [IO.File]::WriteAllText(
                        (Join-Path $output 'post-upgrade-attack-attempts.json'),
                        ($hoverAttempts | ConvertTo-Json -Depth 12),
                        [Text.UTF8Encoding]::new($false))
                    if ([int]$hoverSnapshot.hover.unit -eq [int]$neutralTarget.id) {
                        $hoverConfirmed = $true
                        $destinationView = $hoverSnapshot
                        break
                    }
                    $destinationView = $hoverSnapshot
                }
            } else {
                $hoverConfirmed = $null
                $targetVisibleAtDispatch = $null
                $hoverAttempts = @()
                $commandX = $destinationX - [int]$destinationView.camera[0]
                $commandY = $destinationY - [int]$destinationView.camera[1]
                Invoke-RightClick $commandX $commandY
            }
            Start-Sleep -Seconds $PostProductionMoveSeconds
            $afterMove = Get-Snapshot
            [IO.File]::WriteAllText(
                (Join-Path $output 'post-upgrade-move-rebuild.json'),
                ($afterMove | ConvertTo-Json -Depth 32),
                [Text.UTF8Encoding]::new($false))
            $movedUnit = @($afterMove.active_units | Where-Object {
                [int]$_.slot -eq $spawnedSlot
            } | Select-Object -First 1)[0]
            $neutralAfter = if ($null -ne $neutralTarget) {
                @($afterMove.active_units | Where-Object {
                    [int]$_.id -eq [int]$neutralTarget.id
                } | Select-Object -First 1)[0]
            } else { $null }
            $initialWorld = @([int]$spawnedUnit.world[0],[int]$spawnedUnit.world[1])
            $finalWorld = @([int]$movedUnit.world[0],[int]$movedUnit.world[1])
            $movementDistance = [Math]::Abs($finalWorld[0] - $initialWorld[0]) +
                [Math]::Abs($finalWorld[1] - $initialWorld[1])
            $neutralDamaged = ($null -ne $neutralTarget -and
                ($null -eq $neutralAfter -or
                 [int]$neutralAfter.health[0] -lt [int]$neutralTarget.health[0]))
            $postProductionResult | Add-Member -NotePropertyName movement `
                -NotePropertyValue ([pscustomobject]@{
                    destination=@($destinationX,$destinationY)
                    initial_world=$initialWorld
                    final_world=$finalWorld
                    manhattan_distance=$movementDistance
                    final_state=[string]$movedUnit.state
                    selected_ids=@($selectedForMove.selected.ids)
                    selection_injection=$selectionInjection
                    hover_confirmed=$hoverConfirmed
                    target_visible_at_dispatch=$targetVisibleAtDispatch
                    attack_input=$(if ($PostProductionAttackNeutral) { 'context-right-click' } else { $null })
                    attack_attempts=$hoverAttempts
                    mode=$(if ($PostProductionAttackNeutral) { 'neutral-attack' } else { 'move' })
                    neutral_slot=$(if ($null -ne $neutralTarget) { [int]$neutralTarget.slot } else { -1 })
                    neutral_health_before=$(if ($null -ne $neutralTarget) { [int]$neutralTarget.health[0] } else { 0 })
                    neutral_health_after=$(if ($null -ne $neutralAfter) { [int]$neutralAfter.health[0] } else { 0 })
                    neutral_damaged=$neutralDamaged
                })
            if ($PostProductionAttackNeutral -and -not $neutralDamaged) {
                throw 'Post-upgrade produced unit did not damage the neutral target.'
            }
            if (-not $PostProductionAttackNeutral -and $movementDistance -lt 128) {
                throw 'Post-upgrade produced unit did not execute movement.'
            }
        }
    }

    $fullAuditResult = $null
    if ($FullStateAudit) {
        [IO.File]::WriteAllText($fullAuditStop, 'stop',
            [Text.UTF8Encoding]::new($false))
        $runningFullAudit = Get-Process -Id $fullAuditProcess.Id `
            -ErrorAction SilentlyContinue
        if ($null -ne $runningFullAudit) {
            Wait-Process -InputObject $runningFullAudit -Timeout 30
        }
        $fullAuditResult = Get-Content -Raw $fullAuditResultPath |
            ConvertFrom-Json
        if (-not [bool]$fullAuditResult.pass) {
            throw 'Full-state upgrade audit found a confirmed divergence.'
        }
    }

    [pscustomobject]@{
        pass = [bool]$result.pass
        sha256 = $layout.sha256
        owner = $owner
        tribe = $TribeIndex
        producer_type = $producerType
        producer_slot = [int]$producer.slot
        upgrade_item = $upgradeItem
        upgrade_order = $upgradeOrder
        aligned_frame_count = [int]$result.aligned_frame_count
        first_divergence = $result.first_divergence
        baseline = $result.baseline
        last = $result.last
        harvest_after_completion = $harvestResult
        post_upgrade_production = $postProductionResult
        full_state_audit = $fullAuditResult
        builds = $buildEvidence
        output = $output
        processes = $pair
    } | ConvertTo-Json -Depth 16
}
finally {
    if ($null -ne $fullAuditStop -and
        -not [IO.File]::Exists($fullAuditStop)) {
        [IO.File]::WriteAllText($fullAuditStop, 'stop',
            [Text.UTF8Encoding]::new($false))
    }
    if ($null -ne $fullAuditProcess -and -not $fullAuditProcess.HasExited) {
        Stop-Process -Id $fullAuditProcess.Id -Force -ErrorAction SilentlyContinue
    }
    if ($null -ne $monitor -and -not $monitor.HasExited) {
        Stop-Process -Id $monitor.Id -Force -ErrorAction SilentlyContinue
    }
    if ($null -ne $postProductionMonitor -and
        -not $postProductionMonitor.HasExited) {
        Stop-Process -Id $postProductionMonitor.Id -Force `
            -ErrorAction SilentlyContinue
    }
    if (-not $KeepProcesses) {
        Get-Process -Id $script:originalPid,$script:rebuildPid `
            -ErrorAction SilentlyContinue | Stop-Process -Force
    }
}
