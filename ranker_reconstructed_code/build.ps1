$ErrorActionPreference = 'Stop'

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$CMake = 'C:\Program Files\CMake\bin\cmake.exe'
$Build = Join-Path $Root 'build'

# A copied workspace can contain a CMake cache whose absolute source/build
# paths belong to the previous machine or account.  Preserve that cache and
# configure a local build directory instead of failing or deleting artifacts.
$Cache = Join-Path $Build 'CMakeCache.txt'
if (Test-Path $Cache) {
    $HomeLine = Get-Content $Cache |
        Where-Object { $_ -like 'CMAKE_HOME_DIRECTORY:INTERNAL=*' } |
        Select-Object -First 1
    if ($HomeLine) {
        $CachedRoot = $HomeLine.Substring($HomeLine.IndexOf('=') + 1)
        if (-not [String]::Equals(
                [IO.Path]::GetFullPath($CachedRoot).TrimEnd('\'),
                [IO.Path]::GetFullPath($Root).TrimEnd('\'),
                [StringComparison]::OrdinalIgnoreCase)) {
            $Build = Join-Path $Root 'build-local'
        }
    }
}

if (-not (Test-Path $CMake)) {
    $cmd = Get-Command cmake -ErrorAction SilentlyContinue
    if ($cmd) {
        $CMake = $cmd.Source
    }
}

if (-not (Test-Path $CMake)) {
    throw 'cmake.exe not found. Install Kitware.CMake with winget or add CMake to PATH.'
}

$WinLibsRoot = Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe'
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
$Ninja = Get-Command ninja -ErrorAction SilentlyContinue
if (-not $Ninja) {
    $WingetPackages = Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Packages'
    if (Test-Path $WingetPackages) {
        $NinjaPath = Get-ChildItem $WingetPackages -Recurse -Filter ninja.exe -ErrorAction SilentlyContinue |
            Select-Object -First 1 -ExpandProperty FullName
        if ($NinjaPath) {
            $env:Path = "$(Split-Path -Parent $NinjaPath);$env:Path"
        }
    }
}

$env:Path = "$CompilerBin;C:\Program Files\CMake\bin;$env:Path"

& $CMake -S $Root -B $Build -G Ninja `
    -DCMAKE_C_COMPILER="$Gcc" `
    -DCMAKE_CXX_COMPILER="$Gxx" `
    -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed with exit code $LASTEXITCODE."
}

& $CMake --build $Build --config Release
if ($LASTEXITCODE -ne 0) {
    throw "CMake build failed with exit code $LASTEXITCODE."
}

Write-Output "Built executable: $(Join-Path $Build 'ranker_rebuild.exe')"
