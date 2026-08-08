param(
    [string]$RepositoryRoot = '',
    [Parameter(Mandatory = $true)][string]$BuildDirectory
)

$ErrorActionPreference = 'Stop'

$toolDirectory = $PSScriptRoot
$repositoryRoot = if ($RepositoryRoot) {
    (Resolve-Path -LiteralPath $RepositoryRoot).Path
} else {
    (Resolve-Path -LiteralPath (Join-Path $toolDirectory '..\..\..')).Path
}
$project = Join-Path $repositoryRoot 'ranker_reconstructed_code'
$build = if ([IO.Path]::IsPathRooted($BuildDirectory)) {
    (Resolve-Path -LiteralPath $BuildDirectory).Path
} else {
    (Resolve-Path -LiteralPath `
        (Join-Path $repositoryRoot $BuildDirectory)).Path
}
$gxxCommand = Get-Command g++.exe -ErrorAction SilentlyContinue
$gxx = if ($gxxCommand) { $gxxCommand.Source } else {
    $compilerRoot = Join-Path $env:LOCALAPPDATA `
        'Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe'
    Get-ChildItem $compilerRoot -Recurse -Filter 'g++.exe' |
        Select-Object -First 1 -ExpandProperty FullName
}
if (-not $gxx) {
    throw 'g++.exe was not found.'
}

$artifactDirectory = Join-Path $toolDirectory 'artifacts'
[IO.Directory]::CreateDirectory($artifactDirectory) | Out-Null
$source = Join-Path $toolDirectory 'runtime_globals_layout_probe.cpp'
$object = Join-Path $artifactDirectory 'runtime_globals_layout_probe.obj'
$executable = Join-Path $artifactDirectory 'runtime_globals_layout_probe.exe'
$compileArgs = @(
    '-O3', '-DNDEBUG', '-std=gnu++17',
    "-I$repositoryRoot",
    "-I$(Join-Path $project 'include')",
    "-I$project",
    "-I$(Join-Path $project 'third_party\zlib113')",
    '-c', $source, '-o', $object
)
& $gxx @compileArgs
if ($LASTEXITCODE -ne 0) {
    throw "Layout probe compile failed with exit code $LASTEXITCODE."
}

$ninjaPath = Join-Path $build 'build.ninja'
$linkLine = Select-String -LiteralPath $ninjaPath `
    -Pattern '^build ranker_rebuild\.exe:' |
    Select-Object -First 1 -ExpandProperty Line
if (-not $linkLine) {
    throw "ranker_rebuild.exe link rule was not found in $ninjaPath."
}
$objects = [regex]::Matches($linkLine, '[^\s]+\.obj') |
    ForEach-Object { $_.Value } |
    Where-Object {
        $_ -notmatch '(^|/)(resources|src/(main|ranker_winmain)\.cpp\.obj$)'
    } |
    ForEach-Object {
        [IO.Path]::GetFullPath((Join-Path $build $_))
    }
if ($objects.Count -eq 0) {
    throw "No target object files were resolved from $ninjaPath."
}
$libraries = @(
    (Join-Path $build 'libzlib113.a'),
    '-luser32', '-lgdi32', '-lwinmm', '-lole32', '-loleaut32',
    '-lcomdlg32', '-lcomctl32', '-lshell32', '-lshlwapi', '-ladvapi32',
    '-lwsock32', '-lddraw', '-ld3d9', '-ldsound', '-ldxguid', '-lgdiplus', '-limm32',
    '-lkernel32', '-lwinspool', '-luuid'
)
$linkArgs = @('-O3', '-DNDEBUG', $object) + $objects + $libraries +
    @('-o', $executable)
& $gxx @linkArgs
if ($LASTEXITCODE -ne 0) {
    throw "Layout probe link failed with exit code $LASTEXITCODE."
}

$probe = Get-Item -LiteralPath $executable
$hash = (Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash
Write-Output `
    "RUNTIME_GLOBALS_LAYOUT_PROBE_BUILT size=$($probe.Length) sha256=$hash"
