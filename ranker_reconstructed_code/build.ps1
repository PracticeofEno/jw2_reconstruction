$ErrorActionPreference = 'Stop'

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$CMake = 'C:\Program Files\CMake\bin\cmake.exe'

if (-not (Test-Path $CMake)) {
    $cmd = Get-Command cmake -ErrorAction SilentlyContinue
    if ($cmd) {
        $CMake = $cmd.Source
    }
}

if (-not (Test-Path $CMake)) {
    throw 'cmake.exe not found. Install Kitware.CMake with winget or add CMake to PATH.'
}

$WinLibsRoot = 'C:\Users\eno\AppData\Local\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe'
$Gxx = $null
$Gcc = $null

if (Test-Path $WinLibsRoot) {
    $Gxx = Get-ChildItem $WinLibsRoot -Recurse -Filter g++.exe |
        Select-Object -First 1 -ExpandProperty FullName
    $Gcc = Get-ChildItem $WinLibsRoot -Recurse -Filter gcc.exe |
        Select-Object -First 1 -ExpandProperty FullName
}

if (-not $Gxx) {
    $cmd = Get-Command g++ -ErrorAction SilentlyContinue
    if ($cmd) {
        $Gxx = $cmd.Source
    }
}

if (-not $Gcc) {
    $cmd = Get-Command gcc -ErrorAction SilentlyContinue
    if ($cmd) {
        $Gcc = $cmd.Source
    }
}

if (-not $Gxx -or -not $Gcc) {
    throw 'gcc/g++ not found. Install BrechtSanders.WinLibs.POSIX.UCRT with winget or add MinGW-w64 to PATH.'
}

$CompilerBin = Split-Path -Parent $Gxx
$env:Path = "$CompilerBin;C:\Program Files\CMake\bin;$env:Path"

& $CMake -S $Root -B (Join-Path $Root 'build') -G Ninja `
    -DCMAKE_C_COMPILER="$Gcc" `
    -DCMAKE_CXX_COMPILER="$Gxx"

& $CMake --build (Join-Path $Root 'build') --config Release
