# Elf PPO against built-in AI, 2026-09-10

## Result

The first critic warmup and actor PPO update completed. Candidate v105 is **not
adopted**: its paired argmax screen regresses from v103's 11/12 to 8/12 wins.
The fixed held-out sampling comparison is tied at 10/12. Retain the established
v103 baseline (38/48 development wins); no full 48-game evaluation is run for
v105 because it misses the predeclared 9/12 screen gate.

| Evaluation | v103 baseline | v105 PPO candidate |
|---|---:|---:|
| Same deterministic screen | 11W / 1L | 8W / 4L |
| Fixed held-out policy RNG streams | 10W / 2L | 10W / 2L |
| Deterministic screen harvest completion | 12/12 | 11/12 |

All four comparison groups have zero timeouts and zero mask violations. The
baseline screen and each held-out sampling group have one execution diagnostic;
the v105 screen has zero. Sampling outcomes are a small descriptive comparison,
not statistical proof of equivalence or independent map generalization.

## Scope and admission

Start a bounded PPO pilot from the selected Elf neural v103. Preserve the
38/48 development baseline, all accepted teachers, Primitive, Demon and Tyrano.
The game engine, teacher, observation schema and action executor are unchanged.
The opponent remains the normal built-in AI, with no slowdown or second learned
policy. Cohort completion reports go to the chat terminal before campaign ACKs.

The existing `assess_bc_gate` and `ppo_admission(..., warm_start=True)` functions
admit the exact v103 checkpoint using its 38/48 gameplay versus teacher 40/48:
delta -4.17 percentage points, within the existing -5 point gameplay criterion.
The full BC gate is still false: teacher-forced macro accuracy is 77.30% and the
teacher corpus has 48 games, rather than the historical full-admission 400.
This is explicitly `bc_warm_start`, not a claim that all BC criteria passed.
Admission is stored separately; the original checkpoint metadata is unchanged.

## Validation correction: new layout seeds are aliases

The first 12-game argmax baseline used layout seeds 20261001 through 20261012.
It returned 9 wins, 2 losses and 1 timeout. Every game's complete actor trajectory
(vectors, maps, conditional masks and actions) is byte-identical to the prior
v103 development game at the same opponent/ordered starting positions.
Changing this layout seed did not produce independent evaluation trajectories.

That cohort is retained as a reproduction/data-quality check, excluded as an
independent adoption criterion. Before PPO fitting, `heldout_plan.json` declares
12 separate sampling-policy RNG seeds 202609105000 through 202609105011 on the
fixed SCREEN conditions (three per opponent). These policy RNG streams are
disjoint from training and their outcomes never enter optimization. Both the
baseline and candidate must be measured on these exact sampled cases. This is
held-out action sampling on the development map, not independent map
generalization. The new plan supersedes the original fresh-argmax requirement.

## Learning protocol

1. Collect 12 new v103 sampled games: 6 Tyrano, 2 each other opponent.
2. Warm up only `value1` and `value2`, three epochs, learning rate 1e-4,
   minibatch 1024. Use original terminal/PBRS full-return targets, preserving
   time-limit bootstrapping. Verify all actor/shared parameters are unchanged.
3. Collect 24 new v104 sampled games: 12 Tyrano, 4 each other opponent.
4. First actor PPO update: three epochs, learning rate 1e-5, minibatch 1024,
   gamma 1.0 and GAE lambda 0.98 per 32 simulation frames. Retain elapsed-frame
   discounts, original terminal outcomes, clipped PPO ratio [0.8, 1.2], and
   PBRS terminal-potential zeroing. Fixed v103 is the KL reference; coefficient
   starts at 0.1 and decays toward 0.05 over 30 updates. Critic Adam state carries
   over, with the policy update's learning rate applied explicitly.
5. Check post-update joint KL <=0.03 and clip fraction <=0.20. Use the same
   12-case deterministic screen, and expand to all 48 if at least 9/12 wins.
   Keep v103 unless development performance strictly improves without regression
   on the fixed held-out sampling cases. A completed update is not an adoption.

PPO uses newly sampled native transitions with actual actions and recorded joint
masked action probabilities. It does not fit DAgger labels or reuse teacher BC
trajectories as on-policy data. Every episode is checked against source weight
hash/version, own/opponent tribes, actual start slots, terminal results and
conditional action masks. Python re-evaluation must match the native selected
log probabilities and values within 0.005 before fitting.

## Completed cohorts

| Cohort | Stage | Result |
|---|---|---|
| 231 | New-layout-seed argmax baseline | 9W / 2L / 1 timeout; all 12 are prior trajectory aliases |
| 232 | v103 sampled critic collection | 4W / 8L; 11,674 decisions; masks/execution failures 0 |
| 233 | Value-only warmup, v104 | Fixed-cohort value loss 0.347063 to 0.103081; actor unchanged |
| 234 | v104 sampled actor collection | 16W / 8L; 23,435 decisions; masks 0; execution failures 3 |
| 235 | First actor PPO update, v105 | 3 epochs / 69 optimizer steps; post-update joint KL 0.002073; clip fraction 2.556% |
| 236 | v105 deterministic screen | 8W / 4L; below 9/12 expansion gate; no adoption |
| 237 | v103 held-out sampling | 10W / 2L; masks 0; execution failures 1 |
| 238 | v105 held-out sampling | 10W / 2L; masks 0; execution failures 1 |

The value-only update changes exactly `value1.weight`, `value1.bias`,
`value2.weight`, `value2.bias`. Across all warmup decisions, maximum native/Python
joint-log-probability error is 0.000006914 and value error 0.000003517.
These are numerical/data checks, not gameplay improvements.

The actor update changes shared actor features, action heads/adapters/embeddings,
and both value layers. Fixed-cohort value loss changes 0.061274 to 0.039306.
Maximum native/Python action-log-probability error is 0.000008583; value error is
0.000002980. The post-update preflight passes. Its three native execution
failures are production-receipt checks: collected job 10 Ranger/type19 at frame
5577, job 13 RedElf/type17 at 6345, job 19 RedElf/type17 at 6137. All remain in
the unmodified outcome and diagnostic records.

Argmax regressions are Elf job 13 and Tyrano jobs 31 and 32; the previous loss
at Elf job 23 remains a loss. Tyrano job 31 no longer reaches a legal harvest
research window before frame 6000, never completes that research, and maximum
army falls from 108 to 30. Elf job 13 army falls from 151 to 65; Tyrano job 32
from 92 to 65, though both still complete harvest research. Small average KL
does not guarantee preservation of strategically critical individual choices.

The held-out baseline's diagnostic is Ranger production receipt failure at
frame 6585, held-out job 9. The candidate's is HQ/type112 build expiration at
18505, held-out job 8; that game wins. No diagnostic was removed or relabeled.

## Provenance and checks

Experiment folder: `debug_artifacts/commander/elf_ppo_builtin_20260910`.
Campaign: `debug_artifacts/commander/race_strength_20260909/training_run`.
`driver.py` handles audited collection, critic/PPO fitting and development
evaluation. `heldout.py` handles the fixed sampling-only validation.
All exact jobs, commands, checkpoint hashes, admission evidence and cohort
audits are recorded beside those helpers. Previous campaign files are frozen.

- Source v103: `elf/update_elf_bc_v103_20260910/policy.bin`,
  SHA256 `99ad3113a97a09cf55b62a17e0a89caeb8376af57d5ccea10bb9bc6184bc349d`.
- Critic v104: `elf/update_elf_ppo_critic_v104_20260910/policy.bin`,
  SHA256 `02545b5c09a1f6c731882634bf54f1d215bf00f61377046ab4aa3552f5118931`.
- PPO candidate v105: `elf/update_elf_ppo_v105_20260910/policy.bin`,
  SHA256 `d9a409099aa58732c119f8b0ef45a45b9b9d69b5b541c84f2961954e8ecedca7`.
- Runtime: `build/demon_foundation_v1_20260910/ranker_rebuild.exe`,
  SHA256 `745b522259b691d7f1c3048d3428e0a1960e001394790b06255d6e0010b275b1`.

Four directly related existing tests pass: elapsed-frame/PBRS returns; saved
joint masked PPO probabilities; critic warmup/actor freezing; Adam momentum
preservation and subsequent actor updates. No unrelated test suites or P2P
gameplay checks were run, and no executable was deployed.

## Current status

The pilot finishes at cohort 238, and all eight cohorts 231–238 have chat ACKs.
No training/evaluation process is left running. PPO is configured for the Elf
campaign, but this status does not imply a background worker is active.

`training_run/elf/ppo_pilot_20260910.json` records the non-adoption decision,
baseline, candidate, critic checkpoint and measured comparisons. The existing
`races["1"].best_neural_candidate` remains v103. The experiment folder's
`completion.json` verifies all 36 training rollouts, their unique actor
trajectories, unchanged checkpoint/metadata/cohort hashes, and that none of the
24 held-out trajectories is in the training set. No native teacher label
sidecars were used by PPO. All preserved contracts pass.

The saved next source is v104: the v103 actor unchanged, with its calibrated
critic and Adam state. A subsequent controlled experiment can protect the
economic policy/shared features and limit PPO changes to combat decisions;
that experiment is not implemented or executed in this pilot. Recollect
on-policy transitions for any subsequent changed actor.

Root `play_last_replay.cmd --dry-run` selects the latest completed experiment:
Elf v105 versus Tyrano, held-out job 8, layout seed 4, victory at frame 26171,
finished 2026-09-10 22:17:42 KST. This is a game from the **unadopted** PPO
candidate. The launcher/viewer are unchanged; dry-run verifies replay selection,
not a GUI playback. Historical teacher and other-race policies remain unchanged.
