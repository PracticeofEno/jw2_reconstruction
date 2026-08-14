# Gameplay parity inventory

`inventory_gameplay_matrix.py` reads the shipped gameplay catalogs and the
diagnostic maps/replays without changing them. It creates two deterministic
reports:

- `reports/gameplay_inventory.json`: machine-readable unit, skill, attack,
  mechanic, map, replay, and coverage records.
- `reports/gameplay_inventory.md`: a concise coverage ledger.

Run it from any directory:

```powershell
python ranker_reconstructed_code/tools/gameplay_parity/inventory_gameplay_matrix.py
```

Coverage levels are deliberately strict. A diagnostic `.trk` is only a
`fixture`; it becomes `executed` only when a matching `.ply` and a recorded
original/rebuild comparison result are present. The inventory therefore does
not mistake selector/UI tests or prepared maps for simulation parity evidence.

Generate player-owned, replay-driven fixtures for every catalog-bound special
action, then run continuous expanded-state comparisons in both executables:

```powershell
python ranker_reconstructed_code/tools/gameplay_parity/generate_all_skill_replays.py
python ranker_reconstructed_code/tools/gameplay_parity/run_skill_parity_suite.py
```

The suite requires both a gap-free exact comparison and observation of the
action's runtime unit-effect ID. A command that is rejected or never executes
is recorded as `not_exercised`, not as parity evidence. Results are written to
`parity_results.json`; full trace artifacts remain under the replay debugger's
ignored artifact directory.

The attack matrix uses five independent target-render-class replays for each
eight-unit batch. It includes every catalog attack source and every unit with
the explicit-attack capability, including the valid zero-index BuildMan hit
profile. Commands that the primary profile mask must reject remain part of the
matrix. Non-direct attacks require the expected effect/source/target link;
direct attacks require target health loss or an observed zero-damage cycle.

```powershell
python ranker_reconstructed_code/tools/gameplay_parity/generate_attack_batch_replays.py
python ranker_reconstructed_code/tools/gameplay_parity/run_attack_parity_suite.py
```

After all suites have run against the deployed rebuild, enforce the complete
manifest ledger with:

```powershell
python ranker_reconstructed_code/tools/gameplay_parity/audit_final_parity.py --root .
```

The audit checks 21,000+ canonical case identities, current rebuild hashes,
fixture replay hashes, exact results, and the small set of catalog combinations
that the original explicitly rejects as not player-reachable.  It writes
`reports/final_parity_audit.json` and `reports/final_parity_audit.md` and exits
nonzero on any missing or stale evidence.
