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
- Diagnostic maps: 926
- Stored replays: 855

## Current executable evidence

- Skills with proven original/rebuild parity: 0
- Skills with replay but no proven comparison: 29
- Skills with fixture only: 0
- Skills missing a fixture: 0
- Unit attack bindings missing per-profile execution: 133
- Unit attack bindings parity proven: 0
- Catalog-unreachable attack fields: 33
- Unit/target-class parity cases: 0 / 435
- Construction bindings parity proven: 0 / 52
- Unit-production bindings parity proven: 0 / 69
- Morph cycles parity proven: 0 / 5

A matching replay alone does not prove parity; the next audit stage must
attach an aligned original/rebuild state comparison to every row.

## Unit/action rows not yet proven

| Unit | Action | Status | Fixture |
|---|---|---|---|
| 009 Chief | 00 Fall out | replay_only | (2) GP Skill A00 U009.trk |
| 009 Chief | 16 Sky Fallout | replay_only | (2) GP Skill A16 U009.trk |
| 014 Kalma | 19 Hide | replay_only | (2) GP Skill A19 U014.trk |
| 017 RedElf | 01 Thunder bolt | replay_only | (2) GP Skill A01 U017.trk |
| 018 WhiteElf | 02 Shield | replay_only | (2) GP Skill A02 U018.trk |
| 018 WhiteElf | 03 Mass temper | replay_only | (2) GP Skill A03 U018.trk |
| 019 Ranger | 04 Teleport | replay_only | (2) GP Skill A04 U019.trk |
| 019 Ranger | 05 Jump portal | replay_only | (2) GP Skill A05 U019.trk |
| 020 BlueElf | 06 Drop Stone | replay_only | (2) GP Skill A06 U020.trk |
| 021 Unicorn | 17 Blessing | replay_only | (2) GP Skill A17 U021.trk |
| 022 Pixie | 08 Fake | replay_only | (2) GP Skill A08 U022.trk |
| 023 ManaSpread | 09 Meteo | replay_only | (2) GP Skill A09 U023.trk |
| 027 AngelElf | 10 Resurrect | replay_only | (2) GP Skill A10 U027.trk |
| 028 GreenElf | 22 Entangle | replay_only | (2) GP Skill A22 U028.trk |
| 028 GreenElf | 23 Hurdle | replay_only | (2) GP Skill A23 U028.trk |
| 029 DarkElf | 19 Hide | replay_only | (2) GP Skill A19 U029.trk |
| 029 DarkElf | 20 Interlace | replay_only | (2) GP Skill A20 U029.trk |
| 029 DarkElf | 21 Exchange | replay_only | (2) GP Skill A21 U029.trk |
| 051 DeathEye | 24 Corrupt | replay_only | (2) GP Skill A24 U051.trk |
| 052 Phantom | 18 Shadow Force | replay_only | (2) GP Skill A18 U052.trk |
| 054 Kelpa | 11 Stone curse | replay_only | (2) GP Skill A11 U054.trk |
| 055 Warlock | 13 Noxious gas | replay_only | (2) GP Skill A13 U055.trk |
| 055 Warlock | 14 Quake | replay_only | (2) GP Skill A14 U055.trk |
| 058 Devil | 15 Rise death | replay_only | (2) GP Skill A15 U058.trk |
| 062 FemmeFatale | 25 Rebirth | replay_only | (2) GP Skill A25 U062.trk |
| 062 FemmeFatale | 26 Recharge | replay_only | (2) GP Skill A26 U062.trk |
| 062 FemmeFatale | 27 Absorb | replay_only | (2) GP Skill A27 U062.trk |
| 063 Nightmare | 29 Airquake | replay_only | (2) GP Skill A29 U063.trk |
| 090 BoneFighter | 12 Suicide | replay_only | (2) GP Skill A12 U090.trk |

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
