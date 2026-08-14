# Gameplay parity coverage ledger

This report is generated from the shipped TRC catalogs and current
diagnostic artifacts. Prepared fixtures are not counted as executed
original/rebuild simulation comparisons.

## Inventory

- Unit definitions: 170
- Special-action definitions: 46
- Attack/effect definitions: 61
- Unit/action bindings: 29
- Unit/attack-profile bindings: 166
- Player action-capability bindings: 636
- Player action selectors represented: 18
- Worker/building bindings: 52
- Producer/unit bindings: 69
- Player morph-cycle bindings: 5
- Diagnostic maps: 883
- Stored replays: 785

## Current executable evidence

- Skills with proven original/rebuild parity: 29
- Skills with replay but no proven comparison: 0
- Skills with fixture only: 0
- Skills missing a fixture: 0
- Unit attack bindings missing per-profile execution: 0
- Unit attack bindings parity proven: 133
- Catalog-unreachable attack fields: 33
- Unit/target-class parity cases: 435 / 435
- Construction bindings parity proven: 52 / 52
- Unit-production bindings parity proven: 69 / 69
- Morph cycles parity proven: 5 / 5

A matching replay alone does not prove parity; the next audit stage must
attach an aligned original/rebuild state comparison to every row.

## Unit/action rows not yet proven

| Unit | Action | Status | Fixture |
|---|---|---|---|

## Player-operable mechanic families

- `selection_and_mixed_selection` (direct)
- `move_path_and_queued_orders` (direct)
- `standard_attack_and_retarget` (direct)
- `class3_alternate_attack` (direct)
- `special_action_cast` (direct)
- `production_and_construction` (direct)
- `morph_and_variant_upgrade` (direct)
- `transport_load_unload` (direct)
- `support_recovery_and_status` (direct)
- `equipment_and_item_state` (direct)
- `resource_gather_and_dropoff` (direct)
- `death_decay_rebirth_and_spawn` (direct)
- `unit_and_map_effect_pool` (simulation consequence)
- `rng_area_scan_and_owner_relation` (simulation consequence)
