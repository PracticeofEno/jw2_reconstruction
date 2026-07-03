# Ranker Reconstructed Source

This folder contains the clean buildable C/C++ reconstruction of `ranker.exe`.

Included:

- `src/`
- `include/`
- `resources/`
- `third_party/zlib113/`
- `CMakeLists.txt`
- `build.ps1`

Not included:

- original installed game data files
- Ghidra exports
- analysis notes
- verification scripts
- previous build artifacts

Build:

```powershell
.\build.ps1
```

Required external build tools:

- CMake
- MinGW-w64 GCC/G++
- Ninja, as used by CMake's Ninja generator

The rebuilt executable is written to:

```text
build\ranker_rebuild.exe
```

The full verification workspace remains in the original `reconstruction`
folder.
