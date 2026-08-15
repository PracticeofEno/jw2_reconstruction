[CmdletBinding()]
param(
    [string]$InstallDir = "C:\Users\eno\Desktop\jw_resversing\RankerOCPV_Win",

    [string]$OutputDir = "debug_artifacts\relay_kit",

    [string]$ServerHost = "115.22.136.89",

    [int]$ServerPort = 19777,

    [string]$Python = "python",

    [switch]$NoZip
)

$ErrorActionPreference = "Stop"

function Test-ZipReadable {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    try {
        Add-Type -AssemblyName System.IO.Compression.FileSystem -ErrorAction SilentlyContinue
        $zip = [System.IO.Compression.ZipFile]::OpenRead($Path)
        $zip.Dispose()
        return $true
    } catch {
        return $false
    }
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = (Resolve-Path (Join-Path $scriptDir "..\..")).Path
$installPath = (Resolve-Path $InstallDir).Path
$exePath = Join-Path $installPath "ranker_rebuild.exe"
$iniPath = Join-Path $installPath "wizardnet_server.ini"

if (-not (Test-Path -LiteralPath $exePath -PathType Leaf)) {
    throw "ranker_rebuild.exe not found: $exePath"
}
if (-not (Test-Path -LiteralPath $iniPath -PathType Leaf)) {
    throw "wizardnet_server.ini not found: $iniPath"
}

if ([System.IO.Path]::IsPathRooted($OutputDir)) {
    $resolvedOutputDir = $OutputDir
} else {
    $resolvedOutputDir = Join-Path $repoRoot $OutputDir
}

$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$kitRoot = Join-Path $resolvedOutputDir "RankerRelay2PCTest_$timestamp"
$payloadDir = Join-Path $kitRoot "deployment_payload"
$toolsDir = Join-Path $kitRoot "ranker_reconstructed_server\tools"
New-Item -ItemType Directory -Force -Path $payloadDir | Out-Null
New-Item -ItemType Directory -Force -Path $toolsDir | Out-Null

$toolFiles = @(
    "collect_relay_client_report.ps1",
    "run_two_pc_release_gate.ps1",
    "relay_client_check.py",
    "export_relay_server_evidence.py",
    "relay_log_check.py",
    "relay_release_gate_check.py",
    "relay_server_summary_check.py"
)

Copy-Item -LiteralPath $exePath -Destination (Join-Path $payloadDir "ranker_rebuild.exe")
Copy-Item -LiteralPath $iniPath -Destination (Join-Path $payloadDir "wizardnet_server.ini")
foreach ($toolFile in $toolFiles) {
    Copy-Item -LiteralPath (Join-Path $scriptDir $toolFile) -Destination (Join-Path $toolsDir $toolFile)
}

$collectWrapper = @'
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("host", "joiner")]
    [string]$Role,

    [Parameter(Mandatory = $true)]
    [string]$Room,

    [int]$GameId = 0,

    [string]$InstallDir = "",

    [string]$Python = "__PYTHON__",

    [switch]$AllowRadmin
)

$ErrorActionPreference = "Stop"
$kitRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $InstallDir) {
    $InstallDir = Join-Path $kitRoot "deployment_payload"
}
$collector = Join-Path $kitRoot "ranker_reconstructed_server\tools\collect_relay_client_report.ps1"
$outputDir = Join-Path $kitRoot "relay_evidence"
$arguments = @(
    "-Role", $Role,
    "-Room", $Room,
    "-InstallDir", $InstallDir,
    "-ServerHost", "__SERVER_HOST__",
    "-ServerPort", "__SERVER_PORT__",
    "-OutputDir", $outputDir,
    "-Python", $Python
)
if ($GameId -gt 0) {
    $arguments += @("-GameId", [string]$GameId)
}
if ($AllowRadmin) {
    $arguments += "-AllowRadmin"
}
& $collector @arguments
exit $LASTEXITCODE
'@
$collectWrapper = $collectWrapper.Replace("__SERVER_HOST__", $ServerHost)
$collectWrapper = $collectWrapper.Replace("__SERVER_PORT__", [string]$ServerPort)
$collectWrapper = $collectWrapper.Replace("__PYTHON__", $Python)
Set-Content -LiteralPath (Join-Path $kitRoot "collect_live_report.ps1") -Value $collectWrapper -Encoding UTF8

$gateWrapper = @'
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Room,

    [Parameter(Mandatory = $true)]
    [string]$HostLog,

    [Parameter(Mandatory = $true)]
    [string]$JoinerLog,

    [string]$ServerLog = "",

    [string]$ServerEvidence = "",

    [string]$ServerEvidenceDir = "",

    [Parameter(Mandatory = $true)]
    [string]$HostReport,

    [Parameter(Mandatory = $true)]
    [string]$JoinerReport,

    [int]$GameId = 0,

    [string]$Python = "__PYTHON__",

    [string]$Output = "",

    [string]$SummaryOutput = "",

    [switch]$AllowRadmin,

    [switch]$AllowSameMachine,

    [switch]$AllowSameNetwork
)

$ErrorActionPreference = "Stop"
$kitRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$gate = Join-Path $kitRoot "ranker_reconstructed_server\tools\run_two_pc_release_gate.ps1"
if (-not $ServerLog -and -not $ServerEvidence -and -not $ServerEvidenceDir) {
    throw "Either -ServerLog, -ServerEvidence, or -ServerEvidenceDir is required."
}
$arguments = @(
    "-Room", $Room,
    "-HostLog", $HostLog,
    "-JoinerLog", $JoinerLog,
    "-HostReport", $HostReport,
    "-JoinerReport", $JoinerReport,
    "-ServerHost", "__SERVER_HOST__",
    "-ServerPort", "__SERVER_PORT__",
    "-Python", $Python
)
if ($ServerEvidence) {
    $arguments += @("-ServerEvidence", $ServerEvidence)
} elseif ($ServerEvidenceDir) {
    $arguments += @("-ServerEvidenceDir", $ServerEvidenceDir)
} else {
    $arguments += @("-ServerLog", $ServerLog)
}
if ($GameId -gt 0) {
    $arguments += @("-GameId", [string]$GameId)
}
if ($AllowRadmin) {
    $arguments += "-AllowRadmin"
}
if ($AllowSameMachine) {
    $arguments += "-AllowSameMachine"
}
if ($AllowSameNetwork) {
    $arguments += "-AllowSameNetwork"
}
if ($Output) {
    $arguments += @("-Output", $Output)
}
if ($SummaryOutput) {
    $arguments += @("-SummaryOutput", $SummaryOutput)
}
& $gate @arguments
exit $LASTEXITCODE
'@
$gateWrapper = $gateWrapper.Replace("__SERVER_HOST__", $ServerHost)
$gateWrapper = $gateWrapper.Replace("__SERVER_PORT__", [string]$ServerPort)
$gateWrapper = $gateWrapper.Replace("__PYTHON__", $Python)
Set-Content -LiteralPath (Join-Path $kitRoot "run_final_gate.ps1") -Value $gateWrapper -Encoding UTF8

$installWrapper = @'
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$InstallDir
)

$ErrorActionPreference = "Stop"
$kitRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$payloadDir = Join-Path $kitRoot "deployment_payload"
$sourceExe = Join-Path $payloadDir "ranker_rebuild.exe"
$sourceIni = Join-Path $payloadDir "wizardnet_server.ini"
$targetDir = (Resolve-Path $InstallDir).Path
$targetExe = Join-Path $targetDir "ranker_rebuild.exe"
$targetIni = Join-Path $targetDir "wizardnet_server.ini"

if (-not (Test-Path -LiteralPath $sourceExe -PathType Leaf)) {
    throw "payload ranker_rebuild.exe is missing: $sourceExe"
}
if (-not (Test-Path -LiteralPath $sourceIni -PathType Leaf)) {
    throw "payload wizardnet_server.ini is missing: $sourceIni"
}
if (-not (Test-Path -LiteralPath (Join-Path $targetDir "ranker.exe") -PathType Leaf)) {
    throw "target folder does not look like RankerOCPV_Win; ranker.exe missing: $targetDir"
}

Copy-Item -LiteralPath $sourceExe -Destination $targetExe -Force
Copy-Item -LiteralPath $sourceIni -Destination $targetIni -Force

$sourceHash = (Get-FileHash -LiteralPath $sourceExe -Algorithm SHA256).Hash
$targetHash = (Get-FileHash -LiteralPath $targetExe -Algorithm SHA256).Hash
if ($sourceHash -ne $targetHash) {
    throw "ranker_rebuild.exe hash mismatch after copy"
}

[pscustomobject]@{
    InstallDir = $targetDir
    RankerRebuild = $targetExe
    WizardNetIni = $targetIni
    RankerRebuildSha256 = $targetHash
    WizardNetIniText = [System.IO.File]::ReadAllText($targetIni)
} | ConvertTo-Json -Compress
'@
Set-Content -LiteralPath (Join-Path $kitRoot "install_payload.ps1") -Value $installWrapper -Encoding UTF8

$manifest = [ordered]@{
    created_local = (Get-Date).ToString("o")
    server = "$($ServerHost):$($ServerPort)"
    source_install_dir = $installPath
    payload = @{
        ranker_rebuild_exe = @{
            path = "deployment_payload\ranker_rebuild.exe"
            sha256 = (Get-FileHash -LiteralPath $exePath -Algorithm SHA256).Hash
            size = (Get-Item -LiteralPath $exePath).Length
        }
        wizardnet_server_ini = @{
            path = "deployment_payload\wizardnet_server.ini"
            sha256 = (Get-FileHash -LiteralPath $iniPath -Algorithm SHA256).Hash
            text = [System.IO.File]::ReadAllText($iniPath)
        }
    }
    tools = $toolFiles
    wrappers = @(
        "install_payload.ps1",
        "collect_live_report.ps1",
        "run_final_gate.ps1"
    )
}
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $kitRoot "manifest.json") -Encoding UTF8

$readme = @'
# Ranker Relay Two-PC Test Kit

Server: __SERVER_HOST__:__SERVER_PORT__

This kit contains the relay evidence tools plus the exact deployed
`ranker_rebuild.exe` and `wizardnet_server.ini` under `deployment_payload`.

Install or refresh the tested files in an existing full game folder:

```powershell
.\install_payload.ps1 -InstallDir C:\Path\To\RankerOCPV_Win
```

Live client report, while the game is running:

```powershell
.\collect_live_report.ps1 -Role host -Room ROOM_NAME -InstallDir C:\Path\To\RankerOCPV_Win
.\collect_live_report.ps1 -Role joiner -Room ROOM_NAME -InstallDir C:\Path\To\RankerOCPV_Win
```

Final gate, after the room closes and the server writes the matching relay
summary:

```powershell
.\run_final_gate.ps1 -Room ROOM_NAME -HostLog HOST_Jw2.log -JoinerLog JOINER_Jw2.log -ServerLog SERVER.err.log -HostReport HOST.json -JoinerReport JOINER.json
```

If `data.relay_evidence_dir` is configured on the server, use the generated
`relay_*_game*_ROOM_NAME.json` file directly:

```powershell
.\run_final_gate.ps1 -Room ROOM_NAME -HostLog HOST_Jw2.log -JoinerLog JOINER_Jw2.log -ServerEvidence SERVER_AUTO_EVIDENCE.json -HostReport HOST.json -JoinerReport JOINER.json
```

Or point at the automatic evidence directory and let the gate select the newest
JSON matching the room and optional game ID:

```powershell
.\run_final_gate.ps1 -Room ROOM_NAME -HostLog HOST_Jw2.log -JoinerLog JOINER_Jw2.log -ServerEvidenceDir SERVER_EVIDENCE_DIR -HostReport HOST.json -JoinerReport JOINER.json
```

Add `-Output RESULT.json -SummaryOutput SUMMARY.txt` to keep the full JSON
result and a compact pass/fail summary. The summary lists failing sections,
missing checks, the selected server evidence path, relay member/endpoint counts,
machine identity fingerprints, and LAN fingerprints.

If automatic evidence is unavailable and the full server log should not be
moved, export only the room evidence on the server:

```powershell
python .\ranker_reconstructed_server\tools\export_relay_server_evidence.py SERVER.err.log --room ROOM_NAME --output ROOM_server_evidence.json
.\run_final_gate.ps1 -Room ROOM_NAME -HostLog HOST_Jw2.log -JoinerLog JOINER_Jw2.log -ServerEvidence ROOM_server_evidence.json -HostReport HOST.json -JoinerReport JOINER.json
```

The final gate revalidates exported server evidence against the requested room,
game ID, member count, relay endpoint count, and bidirectional Mode1 thresholds.

The default final gate requires Radmin/Famatech off, the same
`ranker_rebuild.exe` SHA256 on both machines, a matching `wizardnet_server.ini`,
exactly one live `ranker_rebuild.exe` process per machine with one relay server
TCP connection,
no VPN/tunnel/virtual-marked interface providing the IPv4 default gateway,
client logs with encrypted Link relay send/receive and Mode1 relay send/receive
evidence (`crypto=yes` plus `wire_bytes=` on queued relay frames), two distinct
server-observed relay TCP endpoints, two bidirectional Mode1 relay members,
different hashed machine identity fingerprints, and different network evidence. Lab-only
relaxations are available through `-AllowRadmin`, `-AllowSameMachine`, and
`-AllowSameNetwork`; do not use those switches for final NAT proof.

For CGNAT tests where both clients may share one public server-observed IP, the
client reports use IPv4 gateway, gateway MAC, and prefix data as the LAN
fingerprint. The collector probes the default gateway by interface before
recording `GatewayMac`, so two routers both using an address such as
`192.168.0.1` can still be distinguished when their MACs differ.
'@
$readme = $readme.Replace("__SERVER_HOST__", $ServerHost)
$readme = $readme.Replace("__SERVER_PORT__", [string]$ServerPort)
Set-Content -LiteralPath (Join-Path $kitRoot "README_2PC_RELAY_TEST.md") -Value $readme -Encoding UTF8

$zipPath = $null
if (-not $NoZip) {
    $zipPath = "$kitRoot.zip"
    $archiveSucceeded = $false
    $lastArchiveError = $null
    for ($attempt = 1; $attempt -le 5; $attempt++) {
        if (Test-Path -LiteralPath $zipPath) {
            Remove-Item -LiteralPath $zipPath -Force
        }
        try {
            Compress-Archive -Path (Join-Path $kitRoot "*") -DestinationPath $zipPath -Force -ErrorAction Stop
            if (Test-ZipReadable -Path $zipPath) {
                $archiveSucceeded = $true
                break
            }
            $lastArchiveError = "zip was created but could not be opened"
        } catch {
            $lastArchiveError = $_
        }
        Start-Sleep -Milliseconds (250 * $attempt)
    }
    if (-not $archiveSucceeded) {
        throw "failed to create readable relay kit zip after retries: $lastArchiveError"
    }
}

[pscustomobject]@{
    KitRoot = $kitRoot
    ZipPath = $zipPath
    Server = "$($ServerHost):$($ServerPort)"
    RankerRebuildSha256 = (Get-FileHash -LiteralPath $exePath -Algorithm SHA256).Hash
    Manifest = (Join-Path $kitRoot "manifest.json")
} | ConvertTo-Json -Compress
