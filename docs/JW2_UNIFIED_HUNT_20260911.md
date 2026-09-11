# Unified policy hunting and meat collection

The Commander now uses its existing `HUNT` intent for hunting. The temporary
automatic idle-hunt mission, saved mission restoration, and separate idle-hunt
state have been removed. Regional reconnaissance from the preceding exploration
work remains in place.

## Target selection and meat

- Armed neutral monsters and monsters stronger than the squad are legal targets.
  A currently visible, living neutral must still have a render class that at least
  one squad member can damage. Distance and estimated attack effort rank targets;
  superiority, harmlessness, and nearby player enemies do not mask out HUNT.
- A HUNT squad assigns each visible, unclaimed meat drop to one pickup-capable
  fighter whose `action_mode` is zero. This field is the held meat reserve;
  worker `cargo_amount` is unrelated. Assignments persist through native pickup,
  and expire on mission change, unit identity change, lost drop, or timeout.
- An empty collector receives the existing native `pickup_move` command. A
  fighter already holding meat attacks neutrals with `attack_unit`, without
  enabling the auto-pickup marker. Actual pickup orders and later observed reserve
  gains are logged separately. Order counts include retries, not just collections.
- Collection is a stage of HUNT and does not create a separate idle mission.
  A meat-only opportunity can keep HUNT legal after the last visible prey dies.
  No original combat, meat, RNG, or P2P simulation rules were changed.

## Learning and provenance

`-AIDAGGER -AIHUNTLABELS` records a separate native counterfactual label while the
learner continues acting. A settled, mostly idle MAIN squad may receive a HUNT
label; an active attack, scout, retreat, merge, or transfer is preserved. Neutral
strength does not gate the label. The label retains the actor's economic prefix,
and each conditional mask is recomputed natively under the label's prefix.

`tools/ai/ranker_commander_idle_hunt.py` fits only the MAIN squad output and HUNT
intent output, including their adapter output rows. Shared features, other output
rows, and economic heads stay exact. Training uses separate supervised labels;
the actor's original actions, probabilities, and rewards are not rewritten or
treated as counterfactual PPO samples. Native reevaluation is required before
using a candidate for subsequent learning. A fitting metric is not a win rate.

Experimental artifacts are under
`debug_artifacts/commander/hunt_policy_unified_20260911` and
`debug_artifacts/commander/hunt_policy_unified_v2_20260911`. The contracts pin source,
runtime, and starting models. Each completed game checks owner/race, weight
version and SHA, conditional action legality, native/Python log probability and
value parity, and replay evidence for each logged pickup order. Existing model
champion records and the deployed replay viewer are separate from this experiment.

## Direct verification

The four-race reconnaissance/hunting fixture covers risky targets, loaded hunters,
one empty collector per drop, linked acquisition continuity, post-collection
release, new retreat missions, fog/claimed-drop filtering, packet budgets, and
the fact that label generation never overrides a live actor. The existing march,
regional exploration, sweep coverage, and launch parsing checks are also run.

## Measured development results

These are small fixed-condition development screens against the built-in AI,
not independent estimates of the previous 48-game benchmark. Race IDs are
Primitive 0, Elf 1, Tyrano 2, Demon 3. Each four-game screen covers the same
opponents, start pairs, game seeds, and independent policy seeds.

| Cohort | Runtime / weights | Primitive | Elf | Tyrano | Demon |
| --- | --- | --- | --- | --- | --- |
| 281 | Unified executor, incumbent weights; native label collection | 4/4 | 3/4 | 2/4 | 2/4 |
| 282 | Same executor; idle HUNT supervised correction | 4/4 | 3/4 | 4/4 | 4/4 |
| 283 | Linked final-drop fix; Demon continuation candidate v523 | 1/1 | 1/1 | 1/1 | 2/4 |
| 284 | Final runtime; confirm selected Demon v503 | — | — | — | 4/4 |

Cohort 282 observed meat reserve increases 163 / 131 / 111 / 7 times respectively.
Each logged pickup command was verified in the saved gameplay packets and had a
zero held reserve when issued. A reserve increase is an observed follow-up to a
reservation, not an instrumentation event in the original meat pipeline. Command
retries are counted separately. All cohort 282 masks passed and no command
receipt failures were recorded. Native/Python log probability and value errors
were below 0.005 for every game. The four unchanged Primitive control games had
exactly identical complete gameplay packet streams with label recording on/off
(10,695 / 11,188 / 8,161 / 11,129 packets).

Demon v503 learned HUNT entry but frequently cancelled it with HOLD on the next
decision. A narrow v513 fit preserved all non-HUNT logits but corrected only
17/69 recorded cancellations; it was not sent to native evaluation. The v523
candidate additionally trained the HUNT adapter output row. Native cohort 283
reduced cancellations within 64 frames from 69 to 28 and increased observed meat
reserve gains from 7 to 9, but wins fell from 4/4 to 2/4. It was rejected. This
remaining Demon continuity issue is not claimed solved by the entry correction.

The final runtime additionally preserves HUNT legality while the last meat drop
is linked to its existing empty collector. A new unit cannot claim a drop already
linked to another unit. Cohort 283's three other-race games specifically check
this common executor change; they are not additional four-game model screens.

Final confirmation of Demon v503 on that runtime is cohort 284: 4/4 wins, 70
policy HUNT commands, 39 verified empty-collector pickup commands, and 7 observed
reserve increases. There were zero mask violations or command receipt failures.
Selected experimental continuation models are Primitive v270, Elf v501,
Tyrano v502, and Demon v503. The final runtime SHA256 is
`61638e9f6ca330e8c4116533c5dc1dbb49ee6f324c58ed4d276f230fdda07407`.
Final status, checkpoint hashes, and precise report references are recorded in
`debug_artifacts/commander/hunt_policy_unified_v2_20260911/completion.json`.
Champion records and deployed executables are unchanged. This work used
supervised behavior correction, not PPO; the next PPO round must collect fresh
stochastic trajectories with the selected runtime and exact per-race checkpoint.
Deterministic development/label trajectories must not be reused as PPO data.
