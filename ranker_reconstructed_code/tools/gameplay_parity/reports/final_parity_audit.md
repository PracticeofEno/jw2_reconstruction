# Final gameplay parity audit

- Result: PASS
- Current rebuild SHA-256: `A22E3DD14B5BE706BD7381BD449BAFF206D1E1F5812DF0BDDF7BCCABAE0B7AF2`
- Expected player-operable cases: 21369
- Stored canonical result rows: 21369

| Suite | Expected | Exact | Explicitly unreachable | Missing/other |
|---|---:|---:|---:|---:|
| area_toggle | 73 | 73 | 0 | 0 |
| attack_bindings | 166 | 133 | 33 | 0 |
| attack_target_classes | 435 | 435 | 0 | 0 |
| construction | 52 | 52 | 0 | 0 |
| definition_group | 180 | 180 | 0 | 0 |
| direct_commands | 964 | 964 | 0 | 0 |
| equipment_apply | 8499 | 8499 | 0 | 0 |
| equipment_toggle | 9924 | 9924 | 0 | 0 |
| guard_idle | 101 | 101 | 0 | 0 |
| harvest | 4 | 4 | 0 | 0 |
| linked_release | 4 | 4 | 0 | 0 |
| morph_cycle | 5 | 5 | 0 | 0 |
| move_patrol | 91 | 91 | 0 | 0 |
| primary_point_action | 66 | 66 | 0 | 0 |
| production_order_cancel | 188 | 188 | 0 | 0 |
| production_orders | 54 | 54 | 0 | 0 |
| secondary_commands | 54 | 54 | 0 | 0 |
| skills | 29 | 29 | 0 | 0 |
| transport_cycle | 192 | 192 | 0 | 0 |
| transport_unload | 219 | 212 | 7 | 0 |
| unit_production | 69 | 69 | 0 | 0 |
