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

    [int]$MinMembers = 2,

    [int]$MinDistinctPeerHosts = 0,

    [int]$MinDistinctPeerEndpoints = 2,

    [int]$MinBidirectionalMode1Members = 2,

    [string]$ServerHost = "115.22.136.89",

    [int]$ServerPort = 19777,

    [string]$Python = "python",

    [string]$Output = "",

    [string]$SummaryOutput = "",

    [switch]$AllowRadmin,

    [switch]$AllowSameMachine,

    [switch]$AllowSameNetwork,

    [switch]$AllowDifferentExeHash
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = (Resolve-Path (Join-Path $scriptDir "..\..")).Path
$gateScript = Join-Path $scriptDir "relay_release_gate_check.py"
if (-not $ServerLog -and -not $ServerEvidence -and -not $ServerEvidenceDir) {
    throw "Either -ServerLog, -ServerEvidence, or -ServerEvidenceDir is required."
}

$arguments = @(
    $gateScript,
    "--host-log", (Resolve-Path $HostLog).Path,
    "--joiner-log", (Resolve-Path $JoinerLog).Path,
    "--room", $Room,
    "--host-client-report", (Resolve-Path $HostReport).Path,
    "--joiner-client-report", (Resolve-Path $JoinerReport).Path,
    "--require-client-reports",
    "--server-host", $ServerHost,
    "--server-port", [string]$ServerPort,
    "--min-members", [string]$MinMembers,
    "--min-distinct-peer-hosts", [string]$MinDistinctPeerHosts,
    "--min-distinct-peer-endpoints", [string]$MinDistinctPeerEndpoints,
    "--min-bidirectional-mode1-members", [string]$MinBidirectionalMode1Members
)

if ($ServerEvidence) {
    $arguments += @("--server-evidence", (Resolve-Path $ServerEvidence).Path)
} elseif ($ServerEvidenceDir) {
    $arguments += @("--server-evidence-dir", (Resolve-Path $ServerEvidenceDir).Path)
} else {
    $arguments += @("--server-log", (Resolve-Path $ServerLog).Path)
}

if ($GameId -gt 0) {
    $arguments += @("--game-id", [string]$GameId)
}
if ($AllowRadmin) {
    $arguments += "--allow-radmin"
} else {
    $arguments += "--require-no-radmin"
}
if ($AllowSameMachine) {
    $arguments += "--allow-same-machine"
} else {
    $arguments += "--require-distinct-client-machines"
}
if ($AllowSameNetwork) {
    $arguments += "--allow-same-network"
} else {
    $arguments += "--require-distinct-client-networks"
}
if ($AllowDifferentExeHash) {
    $arguments += "--allow-different-exe-hash"
}
if ($Output) {
    $arguments += @("--output", $Output)
}
if ($SummaryOutput) {
    $arguments += @("--summary-output", $SummaryOutput)
}

Push-Location $repoRoot
try {
    & $Python @arguments
    $exitCode = $LASTEXITCODE
} finally {
    Pop-Location
}

exit $exitCode
