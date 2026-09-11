# Strong-teacher neural learning, 2026-09-10

The user authorized separate Elf, Primitive and Demon neural training from the
accepted teachers. Tyrano remains frozen. This is behavior cloning (BC); PPO
and self-play are not enabled in this initial round.

## Frozen inputs

`debug_artifacts/commander/teacher_bc_20260910/contract.json` pins the teacher
summaries, original runtime binaries, trainer implementation, incumbent policies
and best-teacher registries. `previous_state.json` and `previous_focus.json`
preserve the state before resuming all three races.

| Race | Accepted teacher | Source decisions | Teacher development score |
|---|---|---:|---:|
| Elf | followup combined v2 | 51,350 | 40/48 |
| Primitive | matchup v3 | 45,961 | 42/48 |
| Demon | foundation v1 | 46,287 | 43/48 |

These scores belong to handwritten teachers, not neural policies. Only these
accepted teacher runs enter the initial corpus; earlier weak teacher variants
are excluded. Each receipt is checked against the RLO, actual starting slots,
own-race observations, terminal result, runtime identity and legal action masks.
The Elf strategy profile is also checked against its original runtime/log.

## First round

For each race, use the established opponent/ordered-start-position split:
36 training games and 12 validation games. Deduplicate exact actor trajectories.
The 12 validation conditions were used during teacher development and therefore
are development validation, not an independent generalization benchmark.
Losing teacher trajectories retain their loss labels and receive BC weight 0.25;
winning trajectories receive weight 1. Returns are not relabeled as wins.

Warm-start from the preserved race-specific incumbent, fit 20 epochs with
head-specific BC weighting and learning rate 1e-4, and save separate version-101
candidates plus an epoch-10 checkpoint. Versions are race-local; exact identity
also includes the race, path and SHA-256. No candidate overwrites an incumbent.

Evaluate each final candidate in 12 native games against normal builtin AI,
using `build/demon_foundation_v1_20260910/ranker_rebuild.exe`. The learned actor
runs without `-AITEACHER`, a strategy controller, or opponent slowdown. Check
loaded version/hash and race in actual records. Trap-only remains eliminated.
Teacher-forced action accuracy is reported separately from gameplay wins.

The helper `debug_artifacts/commander/teacher_bc_20260910/workflow.py` provides
`init`, `fit --race N`, `eval --race N`, and `ack --race N --kind fit|eval`.
The parent reports each completed cohort in the chat before invoking `ack`;
the next cohort cannot start until that acknowledgement. No Windows notification
or autonomous chat-acknowledgement process is used.

## Follow-up criteria

Use policy failures on training conditions to collect native DAgger labels from
the current accepted teacher, then refit and repeat the development screen.
Keep validation conditions out of DAgger fitting. Expand diverse teacher data
where the first corpus does not cover observed failures. Only promising neural
candidates receive a full 48-condition comparison; promotion requires actual
gameplay evidence. PPO follows a working neural foundation, and self-play follows
the user's current priority of winning against builtin AI.

## Results

The first fits completed for all three races (cohorts 211-213):

| Race | Training decisions | Major macro-action recall, before -> after |
|---|---:|---:|
| Elf | 40,709 | 34.6% -> 65.7% |
| Primitive | 35,455 | 21.4% -> 64.8% |
| Demon | 34,958 | 14.6% -> 76.3% |

This recall measures teacher-forced validation labels; it is not a win rate.
Elf's first native screen (214) was **2W/10L**, with wins against Elf and Tyrano.
Primitive's first screen (215) was **11W/1L**: P3/E3/T3/D2. Both screens had zero
mask violations and zero silent rejections. Their native loaded version was 101.
Demon's first screen (216) was **7W/5L**: P2/E2/T2/D1, with zero mask violations.
Its one silent-rejection event was an expansion DemonDen build request expiring
at frame 8553 in job 32; the raw count is retained. It completed harvest research
in all 12 games. Its failed games often lagged the teacher in expansion/army size.

`diagnose.py` preserves paired teacher/policy trajectory summaries. Elf finished
harvest research in only 3/12 games, versus 12/12 for its teacher. In job 0 it
issued 22 supply-building actions versus the teacher's 4 and never completed
harvest research. Primitive finished harvest research in all 12 games.

Therefore correction and validation are race-specific. `corrective.py` collects
current teacher labels on the version-101 actor's actual TRAIN states at indices
1,6,11,12,17,22,24,29,34,37,42,47. The 12 SCREEN indices remain excluded. It
checks the recorded teacher labels under their own conditional action masks.
Correction fitting mixes these records with the original accepted corpus and uses
version 102. Elf's rare-action weight is reduced to 2 (first fit: 8); other races
retain 8. Actual gameplay is evaluated afterwards. Elf changes both data and
weighting, so its outcome is attributed to the combined correction recipe, not
to either change alone. Elf collection (217) contains 11,338 native teacher labels
from 12 policy-controlled TRAIN games (1W/11L); its correction corpus has 48
training episodes and 52,047 decisions, with the original 12 validation games intact.
The Elf correction fit (218) raised major-action recall from 65.7% to 72.8% and
macro accuracy including NOOP from 58.6% to 72.0%. Its actual paired screen (219)
improved **2/12 -> 7/12**, with P2/E2/T1/D2 wins, no truncation, zero mask violations
and zero silent rejections. Harvest completion increased **3/12 -> 8/12**.
In job 0, supply actions decreased 22 -> 7, harvest completed at frame 3737,
and the former loss became a win. Version 102, SHA-256
`80946d4863277fca2781245f01db7d24cda427a2843362b013e87c0506075ff7`, is retained
as the next Elf training candidate; this 12-condition result is not full admission.

`teacher_variant_actor_feature_audit.json` verifies that the teacher, native
DAgger actor and ordinary evaluation share the same actor-visible numeric
teacher parameters (features 536-541). The 0x10000000 routing flag is cleared
before those parameters are derived; the label-collection flag does not introduce
a different strategy-context observation in this campaign.

Demon's TRAIN-only DAgger collection (220) produced 12,509 teacher labels from
12 policy games (7W/5L). Three expansion-build expirations occurred in job 6;
`demon_collection_execution_events.json` preserves the raw lines and counts.
Its correction corpus has 48 training games and 47,467 decisions. Unlike Elf,
Demon retains its original rare-action weight of 8.
Demon's correction fit (221) raised major-action recall 76.3% -> 78.7% and
macro accuracy 49.4% -> 58.9%. Its actual paired screen (222) improved
**7/12 -> 12/12**, P3/E3/T3/D3, with no truncation, mask violations or silent
rejections. Version 102 SHA-256:
`6ad6aa05729365913318a92b841b354a6c02902c6a22d170918c83478798d923`.

`validate_full.py` expands selected screens with at least 9 wins to all 48 conditions,
reusing the exact 12 screen receipts and running 36 additional games. The working
foundation gate is at least 36/48 wins and at least 6/12 against each opponent.
This remains development validation, including conditions used for BC fitting.
Primitive version 101 (11/12) and corrected Demon version 102 (12/12) are the
selected full-validation candidates; Elf remains a 12-condition candidate.

Primitive's full native evaluation (223) completed at **41W/6L/1 timeout in 48**,
with P11/E12/T9/D9 wins. It passes the working foundation gate (at least 36 wins,
at least 6 per opponent). All observations/actions and loaded version/hash passed
the native audit, with zero mask violations and two recorded execution rejections.
The exact first 12 receipts were reused and checked unchanged. This matches the
development conditions of the accepted teacher; it is not an unseen-map claim.
Selected Primitive version 101 SHA-256:
`9a48bfed84c9f66a88519e939fa6a7b26655884126802de34db48d8cd4fd784e`.
Both execution rejections were expansion-base build expirations in the timed-out
Tyrano game 35; see `primitive_full_execution_events.json`.

Demon's full native evaluation (224) completed at **37W/10L/1 timeout in 48**,
with P12/E9/T8/D8 wins. It also passes the working foundation gate. There were
zero mask violations and two expansion-base build expirations (job 20, timeout;
job 22, win), retained in `demon_full_execution_events.json`. Its 12/12 screen
was an optimistic subset: the full 37/48 result is the broader development score.

| Selected neural candidate | Broadest completed development evaluation | Foundation gate |
|---|---|---|
| Elf v102 | 7W/5L in 12; paired improvement from 2/12 | Full validation pending |
| Primitive v101 | 41W/6L/1 timeout in 48 | Passed |
| Demon v102 | 37W/10L/1 timeout in 48 | Passed |

This milestone contains five completed BC fits and 156 newly executed native
games (including corrective collection and evaluation), plus the audit/reuse of
144 pre-existing accepted-teacher games. Cohorts 211-224 were individually
reported in the chat before acknowledgement. PPO and self-play have not started.
The milestone ends with no active training/evaluation process. Next work is
additional Elf corrective learning and cautious PPO pilots from the validated
Primitive/Demon foundations; expansion-build expirations remain diagnostic leads.

Per-race `*_fit_result.json`, `*_eval_result.json`, and
`*_evaluation_audit.json` in the helper directory are written only after their
respective stages succeed. Candidate weights and complete dataset provenance
live under `race_strength_20260909/training_run/<race>/update_strong_teacher_bc_20260910`.
Corrected weights live in `update_strong_teacher_bc_corrected_20260910`.

At milestone completion, `finalize.py` writes each race's
`training_run/<race>/neural_learning_20260910.json`. Use its `selected` policy
or `state.races[tribe].best_neural_candidate` as the next training source. The
historical `state.races[tribe].policy` and original contract weights are deliberately
preserved and are not pointers to the newly fitted candidates. A selected
12-game candidate still needs full validation before foundation admission.
