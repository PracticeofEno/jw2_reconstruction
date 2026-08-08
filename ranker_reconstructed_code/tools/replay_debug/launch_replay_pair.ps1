param(
    [Parameter(Mandatory = $true)][string]$ReplayName,
    [string]$RepositoryRoot = '',
    [string]$WorkingDirectory = 'RankerOCPV_Win',
    [string]$OriginalExecutable = 'RankerOCPV_Win\ranker.exe',
    [string]$RebuildExecutable = 'RankerOCPV_Win\ranker_rebuild.exe',
    [string]$ExpectedRebuildSha256 = '',
    [int]$RouteTimeoutSeconds = 90,
    [int]$OriginalStartupSeconds = 6,
    [int]$RebuildStartupSeconds = 6
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = if ($RepositoryRoot) {
    (Resolve-Path -LiteralPath $RepositoryRoot).Path
} else {
    (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..\..')).Path
}

function Resolve-RepositoryPath([string]$Path) {
    $candidate = if ([IO.Path]::IsPathRooted($Path)) {
        $Path
    } else {
        Join-Path $repositoryRoot $Path
    }
    return (Resolve-Path -LiteralPath $candidate).Path
}

if (-not ('RecoveredReplayUi' -as [type])) {
    Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;

public static class RecoveredReplayUi {
    [StructLayout(LayoutKind.Sequential)]
    private struct RECT { public int Left, Top, Right, Bottom; }
    public delegate bool Callback(IntPtr handle, IntPtr parameter);
    [DllImport("user32.dll")]
    private static extern bool EnumWindows(Callback callback, IntPtr parameter);
    [DllImport("user32.dll")]
    private static extern bool EnumChildWindows(
        IntPtr parent, Callback callback, IntPtr parameter);
    [DllImport("user32.dll")]
    private static extern uint GetWindowThreadProcessId(
        IntPtr handle, out uint processId);
    [DllImport("user32.dll", CharSet = CharSet.Ansi)]
    private static extern int GetWindowText(
        IntPtr handle, StringBuilder text, int capacity);
    [DllImport("user32.dll")]
    private static extern int GetDlgCtrlID(IntPtr handle);
    [DllImport("user32.dll")]
    private static extern IntPtr GetParent(IntPtr handle);
    [DllImport("user32.dll")]
    private static extern bool GetClientRect(IntPtr handle, out RECT rect);
    [DllImport("user32.dll")]
    public static extern IntPtr SendMessage(
        IntPtr handle, uint message, IntPtr wparam, IntPtr lparam);
    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr handle);
    [DllImport("user32.dll", CharSet = CharSet.Ansi, EntryPoint = "SendMessageA")]
    private static extern IntPtr SendMessageText(
        IntPtr handle, uint message, IntPtr wparam, StringBuilder lparam);
    [DllImport("ntdll.dll")]
    private static extern int NtSuspendProcess(IntPtr processHandle);
    [DllImport("ntdll.dll")]
    private static extern int NtResumeProcess(IntPtr processHandle);

    public static void SuspendProcess(IntPtr processHandle) {
        int status = NtSuspendProcess(processHandle);
        if (status != 0) throw new InvalidOperationException(
            "NtSuspendProcess failed: 0x" + status.ToString("X8"));
    }

    public static void ResumeProcess(IntPtr processHandle) {
        int status = NtResumeProcess(processHandle);
        if (status != 0) throw new InvalidOperationException(
            "NtResumeProcess failed: 0x" + status.ToString("X8"));
    }

    public static IntPtr FindTop(uint processId, string title) {
        IntPtr result = IntPtr.Zero;
        EnumWindows((handle, parameter) => {
            uint owner;
            GetWindowThreadProcessId(handle, out owner);
            if (owner != processId) return true;
            var text = new StringBuilder(256);
            GetWindowText(handle, text, text.Capacity);
            if (text.ToString() == title) {
                result = handle;
                return false;
            }
            return true;
        }, IntPtr.Zero);
        return result;
    }

    public static string ListTop(uint processId) {
        var result = new StringBuilder();
        EnumWindows((handle, parameter) => {
            uint owner;
            GetWindowThreadProcessId(handle, out owner);
            if (owner != processId) return true;
            var text = new StringBuilder(256);
            GetWindowText(handle, text, text.Capacity);
            if (result.Length != 0) result.Append(" | ");
            result.Append("0x").Append(handle.ToInt64().ToString("X"));
            result.Append(":").Append(text);
            return true;
        }, IntPtr.Zero);
        return result.ToString();
    }

    public static IntPtr FindChild(IntPtr parent, int id) {
        IntPtr result = IntPtr.Zero;
        EnumChildWindows(parent, (handle, parameter) => {
            if (GetDlgCtrlID(handle) == id) {
                result = handle;
                return false;
            }
            return true;
        }, IntPtr.Zero);
        return result;
    }

    public static IntPtr FindDirectChild(IntPtr parent, string title) {
        IntPtr result = IntPtr.Zero;
        EnumChildWindows(parent, (handle, parameter) => {
            if (GetParent(handle) != parent) return true;
            var text = new StringBuilder(256);
            GetWindowText(handle, text, text.Capacity);
            if (text.ToString() == title) {
                result = handle;
                return false;
            }
            return true;
        }, IntPtr.Zero);
        return result;
    }

    public static string ListText(IntPtr list, int index) {
        int length = SendMessage(list, 0x018A, (IntPtr)index, IntPtr.Zero).ToInt32();
        if (length < 0) return "";
        var text = new StringBuilder(length + 1);
        SendMessageText(list, 0x0189, (IntPtr)index, text);
        return text.ToString();
    }

    public static void ClickLogical(
        IntPtr handle, int x, int y, int logicalWidth, int logicalHeight) {
        RECT rect;
        if (!GetClientRect(handle, out rect)) return;
        int width = Math.Max(1, rect.Right - rect.Left);
        int height = Math.Max(1, rect.Bottom - rect.Top);
        int clientX = (int)((long)x * width / logicalWidth);
        int clientY = (int)((long)y * height / logicalHeight);
        IntPtr packed = new IntPtr(
            ((clientY & 0xffff) << 16) | (clientX & 0xffff));
        SendMessage(handle, 0x0200, IntPtr.Zero, packed);
        SendMessage(handle, 0x0201, (IntPtr)1, packed);
        SendMessage(handle, 0x0202, IntPtr.Zero, packed);
    }

    public static void ClickClient(IntPtr handle, int x, int y) {
        IntPtr packed = new IntPtr(((y & 0xffff) << 16) | (x & 0xffff));
        SendMessage(handle, 0x0200, IntPtr.Zero, packed);
        SendMessage(handle, 0x0201, (IntPtr)1, packed);
        SendMessage(handle, 0x0202, IntPtr.Zero, packed);
    }
}
'@
}

function Wait-Top([int]$ProcessId, [string]$Title, [int]$Seconds = 30) {
    $deadline = [DateTime]::UtcNow.AddSeconds($Seconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        $handle = [RecoveredReplayUi]::FindTop([uint32]$ProcessId, $Title)
        if ($handle -ne [IntPtr]::Zero) { return $handle }
        Start-Sleep -Milliseconds 200
    }
    throw "Timed out waiting for '$Title' in PID $ProcessId."
}

function Find-ReplayDialog([Diagnostics.Process]$Process, [IntPtr]$MainWindow) {
    $dialog = [RecoveredReplayUi]::FindTop([uint32]$Process.Id, 'Replay')
    if ($dialog -eq [IntPtr]::Zero) {
        $dialog = [RecoveredReplayUi]::FindDirectChild($MainWindow, 'Replay')
    }
    return $dialog
}

function Select-Replay([Diagnostics.Process]$Process) {
    $main = Wait-Top $Process.Id 'The Ranker'
    [RecoveredReplayUi]::SetForegroundWindow($main) | Out-Null
    $startupSeconds = if ($Process.ProcessName -eq 'ranker') {
        $OriginalStartupSeconds
    } else {
        $RebuildStartupSeconds
    }
    Start-Sleep -Seconds $startupSeconds
    $dialog = [IntPtr]::Zero
    if ($Process.ProcessName -eq 'ranker') {
        & python "$PSScriptRoot\route_original_replay.py" $Process.Id |
            Out-Null
    }
    $routeDeadline = [DateTime]::UtcNow.AddSeconds($RouteTimeoutSeconds)
    while ($dialog -eq [IntPtr]::Zero -and
           [DateTime]::UtcNow -lt $routeDeadline) {
        # Title entry 3 from JW2_02 record 94 is [345,472]-[446,559].
        # Use the real pointer route so the title resources and animation
        # timer are released before replay playback starts.
        if ($Process.ProcessName -eq 'ranker') {
            # The injected one-shot route traverses both original UI screens
            # and restores the real input tick through a trampoline.
        }
        else {
            [RecoveredReplayUi]::ClickLogical($main, 395, 515, 800, 600)
        }
        Start-Sleep -Milliseconds 500
        $dialog = Find-ReplayDialog $Process $main
    }
    if ($dialog -eq [IntPtr]::Zero) {
        $windows = [RecoveredReplayUi]::ListTop([uint32]$Process.Id)
        throw "Timed out routing the title Replay entry for PID $($Process.Id). Windows: $windows"
    }
    $list = [RecoveredReplayUi]::FindChild($dialog, 0x70a)
    $ok = [RecoveredReplayUi]::FindChild($dialog, 0x70d)
    if ($list -eq [IntPtr]::Zero -or $ok -eq [IntPtr]::Zero) {
        throw "Replay controls missing for PID $($Process.Id)."
    }
    $count = [RecoveredReplayUi]::SendMessage(
        $list, 0x018B, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32()
    $index = -1
    for ($candidate = 0; $candidate -lt $count; ++$candidate) {
        if ([RecoveredReplayUi]::ListText($list, $candidate) -eq $ReplayName) {
            $index = $candidate
            break
        }
    }
    if ($index -lt 0) {
        $items = for ($candidate = 0; $candidate -lt $count; ++$candidate) {
            [RecoveredReplayUi]::ListText($list, $candidate)
        }
        throw "Replay '$ReplayName' missing for PID $($Process.Id): $($items -join ', ')"
    }
    [RecoveredReplayUi]::SendMessage(
        $list, 0x0186, [IntPtr]$index, [IntPtr]::Zero) | Out-Null
    $selectionCommand = [IntPtr]((1 -shl 16) -bor 0x70a)
    [RecoveredReplayUi]::SendMessage(
        $dialog, 0x0111, $selectionCommand, $list) | Out-Null
    Start-Sleep -Milliseconds 300
    [RecoveredReplayUi]::SendMessage(
        $ok, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
    $deadline = [DateTime]::UtcNow.AddSeconds(20)
    while ([DateTime]::UtcNow -lt $deadline) {
        if ((Find-ReplayDialog $Process $main) -eq [IntPtr]::Zero) {
            return $main
        }
        Start-Sleep -Milliseconds 200
    }
    throw "Replay selection did not close for PID $($Process.Id)."
}

$working = Resolve-RepositoryPath $WorkingDirectory
$originalPath = Resolve-RepositoryPath $OriginalExecutable
$rebuildPath = Resolve-RepositoryPath $RebuildExecutable
if ($ExpectedRebuildSha256) {
    $actualHash = (Get-FileHash -LiteralPath $rebuildPath -Algorithm SHA256).Hash
    if ($actualHash -ne $ExpectedRebuildSha256) {
        throw "Rebuild executable/layout hash mismatch: expected=$ExpectedRebuildSha256 actual=$actualHash"
    }
}
$original = $null
$rebuild = $null
try {
    $original = Start-Process -FilePath $originalPath `
        -WorkingDirectory $working -PassThru
    $originalWindow = Select-Replay $original
    [RecoveredReplayUi]::SuspendProcess($original.Handle)
    $rebuild = Start-Process -FilePath $rebuildPath `
        -WorkingDirectory $working -PassThru
    $rebuildWindow = Select-Replay $rebuild
    [RecoveredReplayUi]::SuspendProcess($rebuild.Handle)
    $original.Refresh()
    $rebuild.Refresh()
    $originalModule = @($original.Modules | Select-Object -First 1)[0]
    $rebuildModule = @($rebuild.Modules | Select-Object -First 1)[0]
    [pscustomobject]@{
        replay = $ReplayName
        original_pid = $original.Id
        original_base = ('0x{0:X}' -f $originalModule.BaseAddress.ToInt64())
        original_window = ('0x{0:X}' -f $originalWindow.ToInt64())
        rebuild_pid = $rebuild.Id
        rebuild_base = ('0x{0:X}' -f $rebuildModule.BaseAddress.ToInt64())
        rebuild_window = ('0x{0:X}' -f $rebuildWindow.ToInt64())
        suspended = $true
    } | ConvertTo-Json
}
catch {
    foreach ($process in @($original, $rebuild)) {
        if ($null -ne $process -and -not $process.HasExited) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        }
    }
    throw
}
