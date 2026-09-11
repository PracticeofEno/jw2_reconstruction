# Elf PPO with protected economic policy, 2026-09-10

## Result

Candidate v106 is **not adopted**. It scores 37 wins and 11 losses in the
48-game deterministic development set, compared with retained v103's
38 wins, 7 losses and 3 timeouts. Tyrano improves by one win, but Demon
loses two. All 48 harvest upgrades finish at exactly the same frames as
v103. The economic protection works on these cases; overall gameplay has
not improved.

| Opponent | v103 wins / 12 | v106 wins / 12 |
|---|---:|---:|
| Primitive | 12 | 12 |
| Elf | 10 | 10 |
| Tyrano | 6 | 7 |
| Demon | 10 | 8 |

No illegal masks occur. The full evaluation has one execution diagnostic,
also present in its reused screen. Existing v103, v104 and v105 checkpoints,
the accepted teachers and the other races are preserved.

The final held-out sampling check scores 10 wins and 2 losses, matching
v103. Its opponent breakdown also matches: Primitive 3/3, Elf 1/3,
Tyrano 3/3 and Demon 3/3. It has no timeouts, illegal masks or execution
diagnostics. The baseline has one execution diagnostic. These twelve
cases use the previously declared policy RNG seeds and never enter PPO;
their candidate actor trajectories do not duplicate the training set.

All four cohorts are complete and were reported in chat before their ACK:

| Cohort | Stage | Result |
|---|---|---|
| 239 | Protected-economy PPO fit | v106; 23,435 decisions; 3 epochs |
| 240 | Deterministic screen | 10 wins / 2 losses |
| 241 | Full deterministic development | 37 wins / 11 losses; includes the screen |
| 242 | Fixed held-out sampling | 10 wins / 2 losses |

No training or evaluation remains running. The selected model stays v103
(38/48). v104 remains the source for a separately planned continuation
because its actor equals v103 and its critic is calibrated. v106 and all
game evidence are retained for diagnosis.

## Experiment

The full actor PPO pilot v105 reduced the paired development screen from
v103's 11/12 wins to 8/12 and again missed harvest research in one game.
This controlled experiment branches from v104, whose actor and shared
observation features equal v103 exactly, with an already calibrated critic.
It changes the trainable parameter scope while retaining the previous
PPO batch, initial optimizer, seed, reward, learning rate and update count.

The same 24 **v104 on-policy** sampled games contain 23,435 decisions, with
12 Tyrano opponents and four each Primitive, Elf and Demon. Reusing them for
this separate v104 branch isolates the scope change. No v105 trajectories,
teacher corrections or validation games are passed to PPO. Any subsequent
update from a changed actor requires another collection under that actor.

## Protected and trainable parameters

`ranker_commander_combat_ppo.freeze_economy` freezes all observation-processing
layers, the shared trunk, every autoregressive embedding, macro head 0,
site head 1, worker-policy head 6 and their adapters. It permits updates only
to heads/adapters 2–5 (squad, intent, anchor, rules of engagement), head/adapter
7 (new-unit rally squad), and the two value layers. Skill selection and
macro-based transfers remain frozen as part of head 0.

The guarantee is exact protected logits on **identical observations and
action prefixes**, verified against v103 across all training decisions.
It does not guarantee an identical opening in a running game. Combat can
change future observations, and worker head 6 is conditioned on preceding
combat actions, so a changed prefix can change its output even with fixed
parameters. Native gameplay therefore also checks harvest completion and
army size.

Frozen parameters use `requires_grad=False` and `grad=None`, preventing old
Adam momentum from moving them. The scope must be reapplied after loading a
checkpoint; weight files do not serialize `requires_grad`. A focused
regression test performs real PPO updates with nonzero frozen Adam momentum,
then exports/reloads weights and optimizer and repeats. Frozen weights,
optimizer states and protected conditional logits remain identical, while
combat and value weights actually change. The test passed.

## v106 fit (cohort 239)

Three epochs, 69 optimizer steps, minibatch 1024, learning rate 1e-5,
gamma 1.0, elapsed-frame GAE lambda 0.98 and original terminal/PBRS rewards.
The fixed KL reference remains v103, coefficient 0.0983333 at iteration 1.
All 24 receipts, actual races/start slots, legal masks, terminal outcomes,
source weight hashes and native selected log probabilities are revalidated.

- Maximum native/Python joint log-probability discrepancy: 8.5831e-6.
- Maximum native/Python value discrepancy: 2.9802e-6.
- Full-batch value half-squared loss: 0.0612740 to 0.0487681.
- Post-update joint approximate KL: 0.0000678079.
- Post-update clip fraction: 0.000384041 (0.0384%).
- Changed fixed-prefix argmax decisions by head: `[0, 0, 11, 1, 14, 1, 0, 0]`.
- Every frozen parameter and protected conditional logit is exact; exported
  and reloaded weights reproduce the same measurements.

These are fit diagnostics, not evidence of improved win rate.

## Deterministic screen (cohort 240)

v106 wins 10/12 (Primitive 3, Elf 2, Tyrano 3, Demon 2), versus v103's
11/12 and the earlier v105's 8/12. No timeouts or illegal masks; one execution
diagnostic. Every screen game completes harvest research at exactly the same
frame as v103. Thus the opening economic regression seen in v105 is absent
on these cases. The screen qualifies for the predeclared full evaluation.

The sole changed screen outcome versus v103 is Demon game 39, win to loss.
At frame 4929 with identical actor observations, the probability of issuing
a MAIN order changes from 0.507335 to 0.499766; the argmax instead selects
no new squad command. The baseline issues attack-move toward visible enemy
army anchor 7 while the existing order is hold at anchor 2. This identifies
the first behavioral divergence, not a proven single cause of the loss.
The detailed paired trace is `game39_first_order.json`. Small average KL
does not guarantee preservation of important discrete choices.

## Full development comparison (cohort 241)

The expanded evaluation adds 36 games to the unchanged 12 screen receipts.
`full_comparison.json` checks every paired seed, tribe, ordered starting pair
and policy RNG seed against the pinned v103 baseline. All 48 harvest research
completion frames are identical between policies.

Six outcomes differ. Tyrano game 34 changes loss to win; Demon games 39 and
47 change win to loss. The three old timeouts (Elf 17, Tyrano 29 and Demon
37) become losses, not wins, and are never relabeled as improvements.

In Tyrano 34, the first changed action is a newly issued MAIN hold command
at route anchor 1, frame 5681, versus no new squad command. Peak army later
increases from 55 to 123. In Demon 47, the first change is an attack-move
target from the nearest known enemy building (anchor 6) to the friendly
route staging point (anchor 1), frame 21953. In Demon 39 the first change
is MAIN order suppression, described above. All first divergences have
identical preceding actor observations. These paired traces narrow the
remaining problem to squad-command timing and destination selection;
they do not prove that isolated first commands explain the final outcomes.

Freezing every squad-selection update would also remove the first observed
beneficial change in Tyrano 34. A follow-up should collect additional
combat experience for both Tyrano and Demon and assess preservation of
successful commands and enemy-building targeting, instead of assuming
that simply freezing another entire head will retain the improvement.

## Predeclared evaluation

Use the same 12 deterministic development games; at least 9/12 wins expands
to all 48, reusing the identical 12 receipts. Replace v103 only with strictly
more than 38/48 wins, at least 6/12 against each opponent, and at least 10/12
on the fixed held-out policy RNG streams from the first pilot. Their v103
baseline receipts are reused unchanged. This remains development-map
validation with a small held-out sampling comparison, not independent map
generalization. No self-play, other-race training or game-rule changes.

Artifacts are under `debug_artifacts/commander/elf_ppo_combat_20260910`.
The candidate is
`debug_artifacts/commander/race_strength_20260909/training_run/elf/update_elf_ppo_combat_v106_20260910/policy.bin`,
SHA-256 `6a710decb31d686839aa04760d79d9c9bb64c11726ee158c425fbfa94636463f`.

`completion.json` pins the experiment registry and comparison artifacts.
`final_verification.json` records source/checkpoint preservation and the
root `play_last_replay.cmd --dry-run` selection. The command opens the most
recent completed **candidate** match, not necessarily the selected model's
match. In this experiment the last match is v106 versus Demon, held sampling
job 9, seed 11, a win at frame 48857. The existing viewer preserves the
recorded commander side's commands with its built-in controller disabled;
the opponent remains the normal built-in AI. No viewer GUI is launched by
the dry-run verification.
