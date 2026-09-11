# Elf neural followup, 2026-09-10

## Result

Elf neural v103 improves the paired native development screen from **7/12 to
11/12**. Full native validation finishes **38 wins, 7 losses, 3 timeouts / 48
conditions (79.17%)**. It is selected as the next Elf neural training baseline.
It passes the working foundation gate of at least 36/48 wins and at least 6/12
against each opponent. This is not a claim of equivalence to the 40/48 teacher.

| Opponent | v102 screen | v103 screen |
|---|---:|---:|
| Primitive | 2/3 | 3/3 |
| Elf | 2/3 | 2/3 |
| Tyrano | 1/3 | 3/3 |
| Demon | 2/3 | 3/3 |

| Opponent | v103 full wins | Losses | Timeouts |
|---|---:|---:|---:|
| Primitive | 12/12 | 0 | 0 |
| Elf | 10/12 | 1 | 1 |
| Tyrano | 6/12 | 5 | 1 |
| Demon | 10/12 | 1 | 1 |

The same v103 policy runs all 48 conditions. The original 12 screen receipts
are reused unchanged; only the other 36 games are added. All 48 games complete
harvest research. Full evaluation has zero mask violations and the same single
HQ build-expiration diagnostic already present in the screen.

The actor is the neural policy using argmax, with no teacher/controller acting
for it. The opponent is the normal built-in AI. This is a development benchmark:
these conditions were already used to develop/select teachers. It is not an
independent generalization test. The 36 TRAIN conditions also enter the eventual
48-condition benchmark and are disclosed as such.

## Failure evidence and intervention

The v102 screen lost jobs 5, 13, 26, 31, and 45. Four of these never completed
harvest research. Their research action was never legal before frame 6000;
whenever research did become legal in the other eight games, the policy selected
it immediately. This points to preceding budget/queue management rather than
failure to select an available research action. One failed Primitive opening
queued extra Wizards and built Halls/units before reserving research resources.

Native v102 TRAIN trajectories exposed a second failure: job 15 timed out with
maximum army 151. In 1,528 decisions the teacher requested MAIN attack-move while
the actor selected hold. The labels were generated on the actor's actual history,
with masks rebuilt for each teacher action prefix; the teacher did not execute.

Collected all 36 TRAIN scenarios in three balanced cohorts of 12. Retained the
accepted Elf teacher's 48 trajectories and the previous v101 TRAIN-only DAgger
corrections. No new SCREEN policy correction enters optimization. The source
teacher's SCREEN trajectories supply the same 12 validation scenarios as before.

Training: 84 episodes / 88,764 decisions, including 36,717 new v102 decisions.
Validation: 12 episodes / 10,641 decisions. No scenario overlap, duplicate actor
trajectories, or excluded questionable skill targets. Wins retain imitation
weight 1, teacher losses 0.25, and corrected policy losses/timeouts 0.5; original
terminal outcomes and return targets are unchanged.

BC starts explicitly from v102, runs 20 epochs with 3 CPU threads, minibatch 1024,
learning rate 1e-4, head-specific loss normalization, macro/squad rare weight 2,
and macro class power 0.5. Validation macro accuracy rises 72.02% to 77.30%;
non-NOOP macro recall changes 72.78% to 72.93%. These teacher-forced metrics are
not a gameplay adoption gate. The 10-epoch checkpoint is retained but unevaluated.

The game engine, teacher, observation schema, action legality and runtime remain
unchanged in this task. The improvement comes from training on broader native
failure corrections. Prior accepted teacher/source changes in the working tree
belong to earlier tasks and are preserved.

## Paired behavioral comparison

| Metric, same 12 scenarios | v102 | v103 |
|---|---:|---:|
| Wins | 7 | 11 |
| Harvest research completed | 8 | 12 |
| Games with opening pending Wizards never above 2 | 1 | 9 |
| Supply actions before frame 8000, summed | 39 | 35 |
| Timeouts | 0 | 0 |
| Action-mask violations | 0 | 0 |
| Silent execution failures | 0 | 1 |

All five previous losses become wins, but previously won Elf job 23 becomes a
loss. Examples: Tyrano job 26 maximum army 28 to 93; job 31 army 19 to 108.
Research changes from absent to frames 3705 and 4849 respectively.

The v103 screen's one execution failure is job 13, an HQ/expansion build expiration
at frame 30209, production 112. That game wins. The failure remains in diagnostics;
this task neither hides it nor changes original construction rules to avoid it.

Remaining non-wins: Elf jobs 17 (timeout), 23; Tyrano 25, 29 (timeout), 30, 33,
34, 35; Demon 37 (timeout), 44. All six Tyrano non-wins complete harvest research
between frames 3625 and 4017, so research omission no longer explains these
failures. Subsequent work should inspect combat losses, production/research
priorities and recovery against Tyrano, then validate on new conditions.
Elf job 17 ends with zero workers, zero army, zero income and 12 spendable
resources, rather than a large idle army. Previous v102 TRAIN timeout job 15
becomes a v103 win at frame 18481; that condition was included in corrections.

## Reproduction and provenance

Campaign root: `debug_artifacts/commander/race_strength_20260909/training_run`.
New orchestration/audits: `debug_artifacts/commander/elf_neural_followup_20260910`.
Previous immutable helpers/teacher source manifest:
`debug_artifacts/commander/teacher_bc_20260910`.

- Source: `elf/update_strong_teacher_bc_corrected_20260910/policy.bin`, v102,
  SHA256 `80946d4863277fca2781245f01db7d24cda427a2843362b013e87c0506075ff7`.
- Candidate: `elf/update_elf_bc_v103_20260910/policy.bin`, v103,
  SHA256 `99ad3113a97a09cf55b62a17e0a89caeb8376af57d5ccea10bb9bc6184bc349d`.
- Native runtime: `build/demon_foundation_v1_20260910/ranker_rebuild.exe`,
  SHA256 `745b522259b691d7f1c3048d3428e0a1960e001394790b06255d6e0010b275b1`.
- New corrections: `elf/update_elf_dagger_v102_20260910/evaluation/argmax/`,
  `group0.json`, `group1.json`, `group2.json`, and native `.rlo.teacher.bin` labels.
- Evaluation: candidate directory `evaluation/argmax/screen.json` and `full.json`.
- Exact commands, immutable pins, scenario splits, native audits and behavioral
  comparisons are retained in the new orchestration folder. Collection uses
  `-AIDAGGER -AITEACHERVAR:268435456`; evaluation uses neither flag and variant 0.
  The high variant flag does not change actor features 536–541 (previous audited
  native feature proof remains under `teacher_bc_20260910`).

## Cohort reports

Each completed cohort is reported in the chat terminal before writing its ACK
in `training_run/chat_reports.jsonl`. No Windows notifications are used.

| Cohort | Work | Result |
|---|---|---|
| 225 | v102 TRAIN corrections, group 0 | 7W / 4L / 1 timeout; masks 0; execution failures 1 |
| 226 | v102 TRAIN corrections, group 1 | 5W / 7L; masks 0; execution failures 0 |
| 227 | v102 TRAIN corrections, group 2 | 2W / 10L; masks 0; execution failures 0 |
| 228 | v103 BC fit | 20 epochs complete; all 96 source episodes audited |
| 229 | v103 paired screen | 11W / 1L; masks 0; execution failures 1 |
| 230 | v103 full development validation | 38W / 7L / 3 timeouts; masks 0; execution failures 1 |

Only Elf is active for this followup. Primitive/Demon policies and registries,
Tyrano, all accepted teachers and historical Elf weights remain preserved.
PPO and self-play are not started in this task.

## Completion and replay

All 96 dataset source rollouts, their 48 native correction-label sidecars,
source-phase manifests and runtimes are rehashed after evaluation. Native
receipt checks verify actual player/opponent tribes, ordered starting slots,
seeds, model version/hash, masks, terminal outcomes and elimination rules.
The preserved contract pins pass, and cohorts 225 through 230 are all ACKed.

Selected registry:
`training_run/elf/neural_learning_followup_20260910.json`.
`training_run/state.json` points `races["1"].best_neural_candidate` to v103.
Historical `neural_learning_20260910.json` and `races["1"].policy` remain historical
records; do not infer the latest candidate from those older paths.
The new helper folder's `completion.json` seals the selected model and audits.

Full receipt SHA256:
`76c777dc589f635f2d3278d75d79cca9a00895bc6feba482b98d03cf81361073`.
Screen receipt SHA256:
`bf32f2b17587d1e58e67b867be8d04a1845a6699d87353f96108a084e09b8ca9`.

Root `play_last_replay.cmd --dry-run` selects the last finished native match,
Elf v103 versus Demon, job 46, seed 5, victory at frame 22299, finished
2026-09-10 21:26:59 KST. The root launcher remains unchanged. The existing
replay viewer disables the recorded player's autonomous AI and retains the
opponent's built-in AI. This check verifies selection; no GUI replay was opened.
No executable was deployed. This round is complete and no training/evaluation
process remains running.
