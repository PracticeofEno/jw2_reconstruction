# Readability refactor

This branch improves the reconstructed source without changing the behavior of
`ranker_rebuild.exe`. The original executable remains the behavioral reference.

## Non-negotiable invariants

- Preserve simulation update order, active-unit/effect iteration order, RNG call
  count/order, integer width, signedness, and wraparound behavior.
- Preserve packet, replay, archive, resource, and legacy object byte layouts.
- Preserve P2P drop flight recording and the `P2PDrop_*.ply` plus
  `P2PDrop_*.sync.csv` diagnostic contract.
- Keep original addresses and Ghidra symbols in nearby comments when they are
  evidence for reconstructed behavior. Human-readable names are the primary
  code vocabulary; addresses are traceability metadata.
- Deploy only `ranker.exe` and `ranker_rebuild.exe` under `RankerOCPV_Win`.

## Change tiers

1. **Mechanical:** file splitting, naming, duplicate helper removal, constants,
   comments, and build organization. These should not change generated state.
2. **Typed boundaries:** replace repeated raw byte/offset access with named view
   types while retaining layout assertions and exact serialization behavior.
3. **Behavioral reconstruction:** simplify control flow or correct inferred game
   behavior only with an original-code reference and focused parity evidence.

Behavioral changes must not be hidden inside mechanical refactor commits.

## Verification for each slice

1. Start from a clean tracked worktree.
2. Build `ranker_rebuild` in Release mode.
3. Run the focused regression tests for the touched subsystem.
4. For simulation-sensitive code, replay the same `.ply` and compare RNG,
   checksum, unit, and effect state through the affected frames.
5. For a P2P mismatch, preserve and analyze the automatic flight artifacts
   before changing the implementation.

## Naming rules

- Name functions by game intent (`FindNearestOwnedDropoffBuilding`) rather than
  decompiler shape (`FUN_004c1234`).
- Name numeric fields conservatively. Use `unknown_*` or `raw_*` plus the
  original offset until semantics are supported by evidence.
- Give masks, state values, record sizes, and table indices named constants.
- Use explicit helpers for legacy arithmetic and byte access. Do not rely on
  implementation-defined casts, host alignment, or host endianness.
- Keep compatibility thunks thin and separate from the domain implementation.

## Initial priority order

The tracked project contains roughly 236,000 non-third-party C/C++ lines. The
largest navigation bottlenecks are currently:

1. `ranker_mfc_runtime.cpp` (about 33,800 lines): split runtime subsystems while
   preserving legacy MFC object layouts.
2. `ranker_winmain.cpp` (about 32,600 lines): extract startup, session wiring,
   input/HUD adapters, and default callback groups.
3. `ranker_unit_commands.cpp` (about 12,000 lines): separate the unit command
   state machine from owner production/strategy helpers.
4. `ranker_original_aliases.cpp` (about 6,300 lines): split compatibility thunks
   by subsystem and keep them free of domain logic.
5. Repeated packet/archive readers and raw layout offsets: introduce checked,
   named accessors one format at a time.

Each extraction should be small enough to review independently and must leave a
green full build before the next extraction begins.
