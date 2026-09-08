# Commander observation extension, 2026-09-08

The actor input is **606 vector values and 12×16×16 map values**. The first 542 vector values and first nine map channels retain their previous meanings and numeric calculations. The unified model schema, including the separately implemented residual heads, is CRC `0x53DD6137`; native compact RLO is format 4, 4,446 bytes per record. Original model/rollout files remain unchanged. `src/ranker_ai_commander.cpp::append_commander_observation` implements the appended vector.

All features are calculated from the current pre-decision `CommanderView`, observed own state, remembered/public terrain, and explicit executor bookkeeping. The executor commits its subsequent changes after the policy's recorded observation. The actor receives no hidden opponent destination, command queue, economy, or current unseen unit state.

## Normalization and absence

In the tables, **L(s)** means `log1p(max(value,0))/log1p(s)` with **no upper clamp**. The reference value `s` maps to 1; values greater than `s` remain distinguishable. Ordinary fractions lie in [0,1]. Ages are `frame >= timestamp ? frame - timestamp : 0`, measured in simulation frames. New features use the same final binary16 quantization as the original vector before native inference and recording.

An absent collection produces zero count, age, and aggregate values. An empty squad produces fourteen zeros. Construction mean/minimum progress is zero when there are no controlled buildings under construction, distinguished by its count. If construction exists but the callback is absent or returns nonfinite progress, **both progress fields are -1**, explicitly unknown; the native integration supplies the callback. Finite progress is bounded to [0,1] because it is work completed / required work, not an unbounded count.

## Appended global vector: 542–563

| Index | Measurement | Encoding |
| --- | --- | --- |
| 542 | Income: cumulative gathered difference over the existing trailing ~220-frame history | L(1000) |
| 543 | Sum of own buildings' deferred production command counts | L(32) |
| 544 | Existing calculated queued population | L(180) |
| 545 | Completed own combat-unit count | L(180) |
| 546 | Completed own combat weight | L(20000) |
| 547 | Own combat investment, using existing commander investment accounting | L(20000) |
| 548 | Historical maximum simultaneously visible enemy combat count | L(180) |
| 549 | Eligible base berry tiles with no own current harvest assignment | L(32) |
| 550 | Own workers currently assigned to eligible base berry tiles | L(80) |
| 551 | Maximum own assignment count on one eligible base berry tile | L(8) |
| 552 | Own workers in harvest-wait command state `0x2d` | L(40) |
| 553 | `max(primary_resources - reserved_resources, 0)` | L(20000) |
| 554 | Resources reserved for pending builds | L(4000) |
| 555 | Active build reservation count | L(8) |
| 556 | Unacknowledged build reservation count | L(8) |
| 557 | Maximum age of an active build reservation | L(60000) |
| 558 | Maximum attempts count of an active build reservation | L(8) |
| 559 | Controlled buildings under construction | L(16) |
| 560 | Mean actual construction work progress | fraction; -1 unknown |
| 561 | Minimum actual construction work progress | fraction; -1 unknown |
| 562 | Distinct members of active merge reservations with matching generation identity | L(180) |
| 563 | Maximum age of an active merge reservation | L(700) |

Combat weight retains the existing `health + attack_power + defense_power` definition. This extension does not change the executor's combat heuristic or redefine old features.

**Own berry assignments:** the eligible tile set is the union of completed controlled HQ (`type 0x80`) windows, inclusive ±15 tiles in x/y, restricted to explored tiles with positive remembered resource amount. Overlapping windows count a tile once. For controlled workers whose current command state is `0x28..0x2d`, the target is the executor's remembered `harvest_tile`, or the observed destination tile if no assignment was recorded. Only targets in the eligible set count in fields 549–551. The calculation reads but does not change executor assignments. This mirrors the executor's own harvest-loop accounting and does **not** claim instantaneous engine tile occupancy or read hidden dynamic reservation bits. A tile assigned to a worker who is walking or returning cargo remains assigned; an idle worker outside this command range does not count. Field 552 counts all own waiting workers, including those outside HQ windows.

## Appended squad vector: 564–605

Order is MAIN, GUARD, RAID. Squad `q` starts at `564 + 14*q`.

| Offset | Measurement | Encoding |
| --- | --- | --- |
| 0 | Member count | L(180) |
| 1 | Squad weight | L(20000) |
| 2 | Squad investment | L(20000) |
| 3 | RMS distance from current squad center, pixels | L(4096) |
| 4 | Maximum distance from current squad center, pixels | L(4096) |
| 5 | Fraction with executor `regrouping=true` | fraction |
| 6 | Fraction whose `applied_intent_serial` differs from current squad serial | fraction |
| 7 | Fraction blocked by existing executor `busy(unit)` predicate | fraction |
| 8 | Fraction with weapon recovery `command_lockout_ticks == 0` | fraction |
| 9 | Fraction below 25% of maximum HP | fraction |
| 10 | Maximum time since movement progress | L(60000) |
| 11 | Mean time since the executor last recorded an order | L(60000) |
| 12 | Time since current intent/anchor/ROE serial changed | L(60000) |
| 13 | Currently visible enemy combat weight within 320px of the squad center | L(20000) |

`CommanderSquadState.last_intent_frame` is initialized on the first view and stamped at every executor serial-changing path, including explicit directives, defense/retreat reflexes, and expiration of a temporary defense target. Repeating the same intent/anchor/ROE does not restart it. The timestamp is observation bookkeeping; it does not itself alter movement or serial rules. Weapon readiness and the `busy` predicate are separate because weapon recovery does not imply that the engine forbids movement. An unissued order has timestamp zero, so its order age is elapsed episode time; the separate unapplied-serial fraction provides execution acknowledgement information.

## Appended maps: channel indices 9–11

Spatial layout, cell indexing, and quantization are unchanged: NCHW 16×16; a cell spans 256×256 pixels, or 8×8 original tiles on the 128×128 map; values are rounded to `uint8/255` before inference. Cells outside a smaller map have zero values. Each cell uses the actual count of contributing in-bounds tiles.

| Channel | Calculation |
| --- | --- |
| 9 | Mean of each tile's full public `placement_class / 7.0` |
| 10 | Fraction of tiles currently visible |
| 11 | Fraction of tiles ever explored |

The public class is `alternate_flags` bits 26–28, also consumed by engine visibility as its terrain class. It is **not** reduced to a high/low predicate. Channel 9 is cached as static public map information; channels 10–11 refresh every view. The original class-positive map and blended visibility map remain unchanged, including their old limitations. The original static-map copy is explicitly limited to 512 values so extending the map array cannot overwrite its cache.

## Migration and learning evidence

For a preserved 542/9 policy, extend the first vector layer by 64 zero columns and the first convolution by three zero channels; initialize the separately added head residual output projections to zero. Verify output/action/log-probability preservation using the model's explicit legacy-prefix summation path. This is **function preservation**, not evidence that the model has learned any new feature.

Old rollouts generally cannot reconstruct own harvest assignments, unit regrouping/serial state, work progress, and reservation retry state. Do not zero-pad old rollouts and present the result as training that used these observations. Old static terrain may be reconstructible from its pinned map, but that does not restore the missing dynamic state. Collect fresh native rollouts with the function-preserving expanded incumbent to train and evaluate the new inputs. Keep old schema data, new schema data, and any explicit unknown/missing fields identifiable.

The direct native regression is `tests/ai_commander_observation_regression.cpp`. It checks unchanged legacy features under new-only state changes; own-assignment and hidden-enemy behavior; full terrain classes and separate visibility; unclipped income/spread; regrouping/serial/frame ages; reservations, work progress, merge identity and empty squads. Existing 14-context probe/regression schemas are updated to 606/format4/4,446 while their original behavior assertions remain.
