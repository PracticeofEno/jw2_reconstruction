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

## Current module boundaries

- `ranker_startup_environment.*` owns startup diagnostics, executable/data
  directory discovery, CPU capability checks, setup-version validation, and
  the legacy CD-ROM fallback. Keep game/session state out of this module.
- `ranker_text_tables.*` owns startup table lookup and display-string
  formatting. Callers provide row numbers and fallbacks; they should not
  duplicate table-presence checks.
- `ranker_frontend_startup.*` owns bootstrap resources and registration of the
  reconstructed frontend window classes.
- `ranker_frontend_layout.*` owns reusable window-placement calculations. The
  pure placement helpers are covered by
  `frontend_window_placement_regression.cpp` so UI wiring does not need to
  duplicate coordinate arithmetic.
- `ranker_gameplay_unit_names.*` owns mutable session/script unit-name
  overrides and indexed-table fallback lookup.
- `ranker_gameplay_session_format.h` and `ranker_gameplay_terrain_layout.*`
  describe session/map data used to produce previews. `ranker_minimap_preview.*`
  owns preview rendering, and `ranker_display_constants.h` owns shared display
  dimensions. Lobby and startup code consume these modules instead of carrying
  private copies of the map tables and rendering pipeline.
- Compatibility entry points are split into `ranker_gameplay_aliases.cpp`,
  `ranker_zlib_aliases.cpp`, and `ranker_mfc_aliases.cpp`. They delegate to
  domain code and must not become a second implementation layer.
- MFC compatibility is split by responsibility: core helpers, debug support,
  exceptions, geometry, memory, object runtime, strings, time, files, data
  exchange, dialog templates, archive streams, collections, and common
  controls each have a `ranker_mfc_*.cpp` module. Window, document/view,
  control-bar, device-context, and OLE behavior remains in
  `ranker_mfc_runtime.cpp` until it can be extracted with equally clear
  boundaries.
- `ranker_winmain.cpp` remains the composition root for default callbacks and
  shared runtime state. New self-contained parsing, formatting, registry, or
  resource logic belongs in its subsystem module rather than this file.

## Current priority order

The tracked project contains roughly 236,000 non-third-party C/C++ lines. The
largest navigation bottlenecks are currently:

1. `ranker_winmain.cpp` (about 31,900 lines): extract session wiring, input/HUD
   adapters, and default callback groups after introducing narrow state
   boundaries.
2. `ranker_mfc_runtime.cpp` (about 28,900 lines): continue splitting cohesive
   window, document/view, control-bar, device-context, and OLE subsystems while
   preserving legacy MFC object layouts.
3. `ranker_unit_commands.cpp` (about 12,000 lines): separate the unit command
   state machine from owner production/strategy helpers.
4. `ranker_link_lobby.cpp` (about 7,000 lines): isolate packet codecs, session
   preparation, and UI routing without changing network ordering.
5. Repeated packet/archive readers and raw layout offsets: introduce checked,
   named accessors one format at a time.

Each extraction should be small enough to review independently and must leave a
green full build before the next extraction begins.
