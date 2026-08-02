# Workspace instructions

## Project objective

- This project reconstructs the original game executable, `ranker.exe`, from
  the OCPV folder.
- When necessary, use Ghidra MCP to analyze the original executable.
- The goal is to enable normal gameplay of the original P2P game without
  synchronization interruptions or disconnects.

## P2P synchronization flight recording

- During a live P2P game, `ranker_rebuild.exe` continuously keeps the ordered
  gameplay packets and the most recent 512 simulation frames of synchronization
  state in memory.
- The retained state includes the gameplay RNG seed, checksum components,
  active-unit identity/order/position/command/target/health/movement state, and
  active spell/effect slot state.
- On the first detected synchronization mismatch, the reconstructed executable
  must automatically preserve both artifacts under `RankerOCPV_Win/Replays`:
  - `P2PDrop_*.ply`: the map/session metadata and processed gameplay packets,
    used to inject the same commands at the same frames during replay.
  - `P2PDrop_*.sync.csv`: the pre-drop frame, unit, effect, RNG, and checksum
    flight trace used to locate the first divergent simulation state.
- Use the captured `.ply` to reproduce the same match in the original
  `ranker.exe` and reconstructed `ranker_rebuild.exe`. Compare aligned frame and
  object state to identify where reconstruction behavior first diverges.
- When a captured drop is caused by reconstruction behavior, analyze the
  corresponding original code, using Ghidra MCP when necessary, and correct the
  reconstructed implementation so P2P synchronization remains intact.
- Flight recording is diagnostic behavior of `ranker_rebuild.exe`; do not
  modify the original `ranker.exe` to add capture instrumentation.

## Ranker deployment filenames

- For every deployment under `RankerOCPV_Win`, the original executable is
  `ranker.exe`.
- The reconstructed deployment executable is `ranker_rebuild.exe`.
- Deploy, replace, verify, and report only those two executable names.
- Do not create, update, copy to, or present `ranker_rebuild_latest.exe` (or
  any other alternate executable name) as a deployment artifact unless the
  user explicitly overrides this rule in the current request.
- A backup file is not a deployment target. Do not restore or deploy a backup
  over either executable unless the user explicitly requests it.
