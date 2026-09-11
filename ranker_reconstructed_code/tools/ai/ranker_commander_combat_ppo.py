"""Restrict PPO updates to squad commands/rally and the critic.

Heads 0/1 select macros/sites; head 6 selects worker behavior. Freeze their
parameters, the shared observation features, and every prefix embedding.
This preserves these heads on identical observations AND recorded prefixes.
Head 6 follows combat heads, so changed prefixes can still change its output;
combat can also change future observations and economic choices in a game.
Reapply the scope after loading a checkpoint: requires_grad is not serialized.
"""
from ranker_commander_model import ARCHITECTURE, HEAD_SIZES

COMBAT_HEADS = (2, 3, 4, 5, 7)
PROTECTED_HEADS = (0, 1, 6)


def freeze_economy(policy):
    if policy.architecture != ARCHITECTURE or policy.head_sizes != HEAD_SIZES:
        raise ValueError("combat PPO scope requires the current commander architecture")
    prefixes = ("value1.", "value2.") + tuple(
        prefix for head in COMBAT_HEADS
        for prefix in (f"heads.{head}.", f"head_adapters.{head}."))
    groups = {"trainable": [], "frozen": []}
    for name, parameter in policy.named_parameters():
        trainable = name.startswith(prefixes)
        parameter.requires_grad_(trainable)
        if not trainable:
            # None prevents existing Adam momentum from changing this weight.
            parameter.grad = None
        groups["trainable" if trainable else "frozen"].append(name)
    return groups
