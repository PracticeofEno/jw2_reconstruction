[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("host", "joiner")]
    [string]$Role,

    [Parameter(Mandatory = $true)]
    [string]$Room,

    [int]$GameId = 0,

    [string]$InstallDir = "C:\Users\eno\Desktop\jw_resversing\RankerOCPV_Win",

    [string]$ServerHost = "115.22.136.89",

    [int]$ServerPort = 19777,

    [string]$OutputDir = "debug_artifacts\relay_evidence",

    [string]$Python = "python",

    [switch]$AllowRadmin,

    [switch]$SkipNetwork
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = (Resolve-Path (Join-Path $scriptDir "..\..")).Path
$checkScript = Join-Path $scriptDir "relay_client_check.py"
$installPath = (Resolve-Path $InstallDir).Path
$logPath = Join-Path $installPath "Jw2.log"
$exePath = Join-Path $installPath "ranker_rebuild.exe"
$iniPath = Join-Path $installPath "wizardnet_server.ini"
if ([System.IO.Path]::IsPathRooted($OutputDir)) {
    $resolvedOutputDir = $OutputDir
} else {
    $resolvedOutputDir = Join-Path $repoRoot $OutputDir
}
New-Item -ItemType Directory -Force -Path $resolvedOutputDir | Out-Null

$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$safeRoom = $Room -replace '[^A-Za-z0-9_.-]', '_'
$outputPath = Join-Path $resolvedOutputDir "$($env:COMPUTERNAME)_$Role`_$safeRoom`_$timestamp.json"

$arguments = @(
    $checkScript,
    "--log", $logPath,
    "--role", $Role,
    "--room", $Room,
    "--server-host", $ServerHost,
    "--server-port", [string]$ServerPort,
    "--exe", $exePath,
    "--ini", $iniPath,
    "--require-mode1",
    "--forbid-direct-transport",
    "--require-deployment",
    "--require-live-process",
    "--output", $outputPath
)

if ($GameId -gt 0) {
    $arguments += @("--game-id", [string]$GameId)
}
if (-not $AllowRadmin) {
    $arguments += "--require-no-radmin"
}
if ($SkipNetwork) {
    $arguments += "--skip-network"
}

Push-Location $repoRoot
try {
    & $Python @arguments
    $exitCode = $LASTEXITCODE
} finally {
    Pop-Location
}

if ($exitCode -eq 0) {
    Write-Host "relay client report written: $outputPath"
}
exit $exitCode
