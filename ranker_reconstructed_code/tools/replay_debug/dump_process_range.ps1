param(
    [Parameter(Mandatory = $true)][int]$ProcessId,
    [Parameter(Mandatory = $true)][Int64]$Address,
    [Parameter(Mandatory = $true)][int]$Size,
    [Parameter(Mandatory = $true)][string]$OutputPath
)

$ErrorActionPreference = 'Stop'

if (-not ('ProcessRangeReader' -as [type])) {
    Add-Type @'
using System;
using System.Runtime.InteropServices;

public static class ProcessRangeReader {
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern IntPtr OpenProcess(
        uint desiredAccess, bool inheritHandle, uint processId);
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool ReadProcessMemory(
        IntPtr process, IntPtr address, byte[] buffer, int size,
        out IntPtr bytesRead);
    [DllImport("kernel32.dll")]
    private static extern bool CloseHandle(IntPtr handle);

    public static byte[] Read(uint processId, long address, int size) {
        const uint PROCESS_VM_READ = 0x0010;
        const uint PROCESS_QUERY_INFORMATION = 0x0400;
        IntPtr handle = OpenProcess(
            PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, false, processId);
        if (handle == IntPtr.Zero) {
            throw new System.ComponentModel.Win32Exception(
                Marshal.GetLastWin32Error(), "OpenProcess failed");
        }
        try {
            byte[] buffer = new byte[size];
            IntPtr bytesRead;
            if (!ReadProcessMemory(handle, new IntPtr(address), buffer, size,
                    out bytesRead) || bytesRead.ToInt64() != size) {
                throw new System.ComponentModel.Win32Exception(
                    Marshal.GetLastWin32Error(), "ReadProcessMemory failed");
            }
            return buffer;
        } finally {
            CloseHandle(handle);
        }
    }
}
'@
}

$bytes = [ProcessRangeReader]::Read(
    [uint32]$ProcessId, $Address, $Size)
$resolved = if ([IO.Path]::IsPathRooted($OutputPath)) {
    [IO.Path]::GetFullPath($OutputPath)
} else {
    [IO.Path]::GetFullPath((Join-Path (Get-Location) $OutputPath))
}
[IO.File]::WriteAllBytes($resolved, $bytes)
[pscustomobject]@{
    process_id = $ProcessId
    address = ('0x{0:X}' -f $Address)
    size = $Size
    output = $resolved
} | ConvertTo-Json
