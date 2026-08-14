#!/usr/bin/env python3
"""Canonical identity and atomic storage helpers for parity evidence."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any


_IDENTITY_FIELDS: dict[str, tuple[str, ...]] = {
    "skill": ("unit_id", "action_id"),
    "attack": ("unit_id", "variant", "attack_id"),
    "attack_target_class": ("unit_id", "target_render_class"),
    "equipment_apply": ("effect_id", "source_unit_id"),
    "equipment_toggle": ("operation", "effect_id", "source_unit_id"),
    "unit_production": ("producer_unit_id", "produced_unit_id"),
    "construction": ("builder_unit_id", "building_unit_id"),
    "morph_cycle": ("source_unit_id", "morph_unit_id"),
    "move_patrol": ("unit_id",),
    "secondary_command": ("action_selector", "unit_id"),
    "transport_unload": ("carrier_unit_id", "passenger_unit_id"),
    "transport_cycle": ("carrier_unit_id", "passenger_unit_id"),
    "direct_action": ("action_selector", "unit_id"),
    "linked_release": ("case_id",),
    "definition_group_order": ("source_unit_id", "target_unit_id"),
    "production_order": ("source_unit_id", "order_id"),
    "production_order_cancel": ("case_id",),
    "direct_command": ("case_id",),
}


def record_key(case: dict[str, Any]) -> tuple[Any, ...]:
    """Return the stable, suite-independent identity of one evidence row."""
    kind = case.get("kind")
    fields = _IDENTITY_FIELDS.get(kind)
    if fields is None:
        case_id = case.get("case_id")
        if case_id is None:
            raise ValueError(f"no parity result identity is defined for {kind!r}")
        fields = ("case_id",)
    return (kind, *(case.get(field) for field in fields))


def atomic_json(path: Path, value: Any) -> None:
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(
        json.dumps(value, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    temporary.replace(path)
