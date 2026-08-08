param(
    [string]$Config = (Join-Path $PSScriptRoot "config.json"),
    [string]$ListenAddress = "",
    [int]$Port = 0
)

$arguments = @((Join-Path $PSScriptRoot "server.py"), "--config", $Config)
if ($ListenAddress) {
    $arguments += @("--host", $ListenAddress)
}
if ($Port -gt 0) {
    $arguments += @("--port", $Port)
}
python @arguments
