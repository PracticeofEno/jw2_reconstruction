# Replay synchronization debugger

These tools run the same `.ply` in `ranker.exe` and `ranker_rebuild.exe`, stop
both simulations on aligned frames, and compare normalized runtime state. They
replace the former workspace-root `.tmp_*` scripts; generated layouts and run
results stay under the ignored `artifacts/` directory.

## Required inputs

- `RankerOCPV_Win/ranker.exe`: the unchanged original executable.
- `RankerOCPV_Win/ranker_rebuild.exe`: the reconstructed deployment executable.
- A replay under `RankerOCPV_Win/Replays`, preferably a captured
  `P2PDrop_*.ply` paired with its `P2PDrop_*.sync.csv` flight trace.
- A Release build directory whose object files produced the deployed rebuild.
- 64-bit Python and the MinGW `g++.exe` plus `nm.exe` used by the project.

The generated layout contains executable RVAs and is valid only for the SHA-256
recorded in that layout. The launch script refuses to compare a different
`ranker_rebuild.exe`.

## Prepare the current executable layout

From the repository root:

```powershell
ranker_reconstructed_code\tools\replay_debug\build_layout_probe.ps1 `
  -BuildDirectory ranker_reconstructed_code\build

ranker_reconstructed_code\tools\replay_debug\resolve_layout.ps1
```

Pass the Release build directory that produced the executable being examined.
`resolve_layout.ps1` reads symbols from the deployed `ranker_rebuild.exe` by
default and writes `artifacts/current_layout.json`. Build and deploy the current
rebuild before generating this file. To examine an undeployed candidate, pass
the same executable explicitly to both `resolve_layout.ps1 -Executable` and the
probe or trace command's `-RebuildExecutable` parameter.

## Probe one aligned frame

```powershell
ranker_reconstructed_code\tools\replay_debug\probe_replay.ps1 `
  -ReplayPath 'RankerOCPV_Win\Replays\P2PDrop_example.ply' `
  -TargetFrame 1000
```

Use `probe_replay_checkpoints.ps1` for coarse checkpoint searches and
`trace_replay_divergence.ps1` to continuously audit a narrowed frame interval.
Each command creates and later removes only
`RankerOCPV_Win/Replays/DebugReplay_Audit.ply`; it never changes
`ranker.exe` on disk and does not add instrumentation to the original game.
The original replay route is armed in process memory before the title is fully
drawn.  It matches the exact title and single-player call sites, so unrelated
startup or gameplay UI screens continue through the original input function.
If either process reaches the natural end of the replay before the requested
frame, probe output reports `terminal: true` and leaves `pass` unset. Choose an
earlier target rather than treating replay completion as a synchronization
difference.

### Compare the completed world render

Add `-CaptureRgb565Pair` to capture both logical RGB565 buffers at the same
next rendered frame. The original stops at `0x004d781d`, immediately after
fog rendering and before its first gameplay overlay. The rebuild stops at the
matching first-overlay callback entry. This excludes selection overlays, HUD
text, modal UI, and the software cursor without relying on window-capture
timing. The comparison uses the runtime interface theme's
`world_viewport_height` and also excludes the fixed top-left and lower-right
HUD regions. The raw surfaces and `rgb565-comparison.json` remain in the run's
ignored artifact directory.

When the replay camera does not show the relevant action, combine the capture
with `-StabilizeViewport -CameraX <x> -CameraY <y>`. Window PNGs are useful for
visual inspection, but they are not an exact-frame parity gate: suspending on
the same simulation counter can leave the two windows presenting different
previous frames.

Use `-ViewOwner 0..7` to switch both suspended replay processes from observer
slot 9 to the same ordinary player presentation owner. The probe reapplies the
owner after startup and leaves enough ticks for a 64-frame visibility clear
before the checkpoint, allowing partial visibility and fog to be compared
without stale observer-local bits.

## Evidence retention

Keep the `.ply` and `.sync.csv` pair for every unresolved mismatch. Once a root
cause is fixed, retain a focused regression test and the smallest replay needed
to reproduce it; bulk `artifacts/` output can be regenerated.
