# -*- coding: utf-8 -*-
"""ENTCMD02 (act3) type-squad policy: role adapters, typed candidate
pointer, sequential economy sampling with an autoregressive ledger, the
team-intent commander head (one (command, cell) per slot) with per-row slot
moves (assign), exact conditional log-probabilities, role-balanced PPO and
SHD3 behaviour cloning.

Plan: docs/AI_PLAY_ENTCMD02_DIRECT_ECONOMY_PLAN.md sections 13-15 and
docs/AI_PLAY_INTENT_SLOT_DESIGN_EASY.md.

Decision order inside one tick (mirrored by the C++ receiver):

    commander: slot s = 0..3 -> (command, cell)      [independent per slot]
    compact control rows: one ordinary command per type, individual economy
    sources and scouts (economy sources retain canonical wire order):
        assign_i   ~ P(. | row ctx, dynamic assign mask)   (SCOUT ledger)
        gate/command/argument as before, the row context conditioned on the
        (new) order of the slot the row ends up in and on the economy ledger

log P(joint) = sum_s log P(slot_s) + sum_i [log P(assign_i) + log P(row_i)].
Forced choices (mask with one option) contribute log-prob 0 and are not
trainable.  PPO/BC recompute teacher-force every stored prefix and reuse the
stored dynamic masks (never regenerated from the snapshot).
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Sequence, Tuple

import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F

import ranker_entity2_contract as wire
import ranker_entity2_squads as squads

ARCHITECTURE_ID = "entv6-type-squads-mlp"
GAMMA_8 = 0.9998
LAMBDA_8 = 0.95
# Reward (plan 14.2 + intent shaping A), rebalanced 2026-09-04 (user
# decision): the terminal outcome is the dominant term, the war exchange is
# shaping (1 per 1000 unit value), and a small time cost makes a stalled
# truncation worse than a prompt finish.
WAR_SCALE = 1.0
WAR_BUILDING_WEIGHT = 2.0       # hostile building loss counts double
ECON_BETA = 1.0
ECON_Z_SCALE = 8000.0
ECON_POP_WEIGHT = 25.0
INTENT_BETA = 4.0   # v5: enemy-base discovery / approach worth up to +4 (user decision)
INTENT_DISTANCE_SCALE = 4096.0
TERMINAL_WIN_PAYOFF = 20.0      # ~a 20000-value army; a costly win still pays
TERMINAL_PAYOFF = {wire.TERMINAL_ONGOING: 0.0, wire.TERMINAL_WIN: TERMINAL_WIN_PAYOFF,
                   wire.TERMINAL_LOSS: -TERMINAL_WIN_PAYOFF, wire.TERMINAL_DRAW: 0.0}
TIME_COST_PER_FRAME = 0.0005 / 8.0   # -3.75 over a full 60000-frame game
REWARD_ID = "entrew5-intent4"
CHUNK_STEPS = 256

COMMAND_KEEP = wire.COMMAND_KEEP
COMMAND_COUNT = wire.COMMAND_COUNT
NON_KEEP_COUNT = COMMAND_COUNT - 1
POINT_COMMANDS = wire.POINT_COMMANDS
ECONOMY_COMMANDS = wire.ECONOMY_COMMANDS
SLOT_COUNT = wire.SLOT_COUNT
SLOT_COMMAND_COUNT = wire.SLOT_COMMAND_COUNT
CELL_COUNT = wire.GLOBAL_CELL_COUNT
ASSIGN_OPTIONS = SLOT_COUNT + 1     # keep + 4 slots

# Categorical vocabularies (UNK = last bucket).
TYPE_VOCAB = 0xAA + 1
MOVEMENT_CLASS_VOCAB = 6
DCM_VOCAB = 3
RENDER_VOCAB = 33
COMMAND_BASE_VOCAB = 139
MOVESTATE_VOCAB = 9
SEMANTIC_VOCAB = wire.SEMANTIC_COUNT + 1
STATUS_VOCAB = 8
MATCH_VOCAB = 5
LAST_CMD_VOCAB = COMMAND_COUNT + 1
LAST_RESULT_VOCAB = wire.RESULT_COUNT + 1
OWN_ROLE_VOCAB = wire.ROLE_COUNT
TARGET_ROLE_VOCAB = 3
OWNER_VOCAB = 9
CAND_KIND_VOCAB = wire.CAND_KIND_COUNT
SLOT_ID_VOCAB = SLOT_COUNT + 1      # 4 = none
RELATION_VOCAB = 4
CELL_VOCAB = CELL_COUNT + 1         # 0 = none, 1..64 = cell + 1
OWN_CAT_FIELDS = 13
TARGET_CAT_FIELDS = 4
OWN_EXTRA = 60            # appendix continuous block (see _own_extra)
CAND_FEAT = wire.CANDIDATE_FEATURE_COUNT + 6
SLOT_CONT = 8
LEDGER_CONTEXT = 3 + 4 + 4 * 8   # budget, kind counts, 4 x 8-d kind means
ROW_INTENT = 4 + 8                # slot command emb + cell emb of the row's slot
BUDGET_NORM = (1000.0, 1000.0, 100.0)
# Loss buckets beyond the six own roles.
BUCKET_ASSIGN = OWN_ROLE_VOCAB
BUCKET_COMMANDER = OWN_ROLE_VOCAB + 1


def _clampi(values: Sequence[int], vocab: int) -> List[int]:
    return [v if 0 <= v < vocab - 1 else vocab - 1 for v in values]


def _bits_of_words(rows: Sequence[Sequence[int]], count: int) -> torch.Tensor:
    """LSB-first bit expansion of per-row u32 word lists -> bool [R, count]
    (the wire parser already rejected nonzero bits above `count`)."""
    if not rows or count == 0:
        return torch.zeros(len(rows), count, dtype=torch.bool)
    words = np.asarray(rows, dtype="<u4").reshape(len(rows), -1)
    bits = np.unpackbits(words.view(np.uint8), axis=1, bitorder="little")
    return torch.from_numpy(np.ascontiguousarray(bits[:, :count]).astype(bool))


def _bits_of_ints(values: Sequence[int], count: int) -> torch.Tensor:
    """LSB-first bits of small per-row integers -> bool [R, count]."""
    if not values:
        return torch.zeros(0, count, dtype=torch.bool)
    arr = np.asarray(values, dtype=np.int64).reshape(-1, 1)
    bits = (arr >> np.arange(count, dtype=np.int64)) & 1
    return torch.from_numpy(bits.astype(bool))


@dataclass
class EntityStep:
    """One owner decision. U counts compact policy rows, not wire units."""
    global_feat: torch.Tensor          # [802 + 3]
    own_cat: torch.Tensor              # [U, 13] long
    own_feat: torch.Tensor             # [U, 33 + OWN_EXTRA]
    own_role: torch.Tensor             # [U] long
    active_cand_row: torch.Tensor      # [U] long (-1 none)
    command_mask: torch.Tensor         # [U, 11] bool (base)
    point_mask: torch.Tensor           # [U, 96] bool
    target_cat: torch.Tensor           # [E, 4] long
    target_feat: torch.Tensor          # [E, 14]
    pair_mask: torch.Tensor            # [U, E] bool
    cand_cat: torch.Tensor             # [C, 2] long (kind, object_id)
    cand_feat: torch.Tensor            # [C, CAND_FEAT]
    econ_mask: torch.Tensor            # [U, C] bool (base)
    candidates: List[wire.Candidate]
    spendable: Tuple[int, int, int]
    # Team intent (v3).
    slot_cont: torch.Tensor            # [4, SLOT_CONT]
    slot_command_cur: torch.Tensor     # [4] long (current slot command)
    slot_cell_cur: torch.Tensor        # [4] long (cell + 1, 0 none)
    slot_command_mask: torch.Tensor    # [4, 8] bool
    slot_cell_mask: torch.Tensor       # [4, 64] bool
    start_cells: torch.Tensor          # [8] long (cell + 1, 0 absent)
    start_flags: torch.Tensor          # [8, 2]
    own_slot: torch.Tensor             # [U] long (0..3, 4 = none)
    own_relation: torch.Tensor         # [U] long
    assign_mask: torch.Tensor          # [U, 4] bool (base)
    scout_free: int
    control_layout: Optional[squads.SquadLayout] = None
    control_feat: Optional[torch.Tensor] = None     # [U, 6] squad size/geometry/kind
    # Filled by sampling / teacher forcing (stored, never regenerated):
    dyn_command_mask: Optional[torch.Tensor] = None   # [U, 11] bool
    dyn_econ_mask: Optional[torch.Tensor] = None      # [U, C] bool
    dyn_assign_mask: Optional[torch.Tensor] = None    # [U, 4] bool
    budget_before: Optional[torch.Tensor] = None      # [U, 3] long
    command: Optional[torch.Tensor] = None            # [U] long
    argument: Optional[torch.Tensor] = None           # [U] long
    assign: Optional[torch.Tensor] = None             # [U] long (0 keep, 1..4)
    slot_command: Optional[torch.Tensor] = None       # [4] long
    slot_cell: Optional[torch.Tensor] = None          # [4] long (-1 none)
    trainable: Optional[torch.Tensor] = None          # [U] bool
    assign_trainable: Optional[torch.Tensor] = None   # [U] bool
    slot_trainable: Optional[torch.Tensor] = None     # [4] bool
    old_logp: Optional[torch.Tensor] = None           # [U]
    old_assign_logp: Optional[torch.Tensor] = None    # [U]
    old_slot_logp: Optional[torch.Tensor] = None      # [4]
    # SHD3 PREFIX_UNRESOLVED: rows at/after this index carry an economy-free
    # mask and reserve nothing (None for live samples).
    unresolved_from: Optional[int] = None
    behavior_value: float = 0.0
    reward: float = 0.0
    dt: float = 8.0
    terminal: bool = False
    truncated: bool = False
    cutoff: bool = False

    @property
    def u(self) -> int:
        return int(self.own_cat.shape[0])

    @property
    def e(self) -> int:
        return int(self.target_cat.shape[0])

    @property
    def c(self) -> int:
        return int(self.cand_cat.shape[0])


def _own_extra(req: Dict, i: int) -> List[float]:
    cap = req["own_capability_bits"][i]
    state = req["own_source_state_bits"][i]
    out = [float((cap >> b) & 1) for b in range(8)]
    out += [float((state >> b) & 1) for b in range(6)]
    out += [req["own_cargo_ratio"][i], req["own_queue_fill_ratio"][i],
            req["own_deferred_command_count"][i] / 4.0,
            1.0 if req["own_walking_build_type_id"][i] != wire.TYPE_SENTINEL else 0.0,
            1.0 if req["own_queued_production_type_id"][i] != wire.TYPE_SENTINEL else 0.0,
            1.0 if req["own_active_economy_candidate_row"][i] >= 0 else 0.0]
    for slot in req["queue_slots"][i]:
        kind = [0.0, 0.0, 0.0]
        kind[min(slot.kind, 2)] = 1.0
        status = [0.0, 0.0, 0.0, 0.0]
        status[min(slot.status, 3)] = 1.0
        out += kind + status + [slot.queue_ordinal / 4.0]
    assert len(out) == OWN_EXTRA
    return out


def step_from_request(request: Dict, header: wire.Header, *, grouped: bool = True) -> EntityStep:
    u = header.own_rows
    e = header.target_rows
    c = header.candidate_rows
    spend = (request["spendable_primary"], request["spendable_secondary"],
             request["spendable_population"])
    global_feat = list(request["global"]) + [
        min(spend[0] / BUDGET_NORM[0], 4.0), min(spend[1] / BUDGET_NORM[1], 4.0),
        min(spend[2] / BUDGET_NORM[2], 4.0)]
    own_cat_columns = [
        _clampi(request["own_type_id"], TYPE_VOCAB),
        _clampi(request["own_movement_class"], MOVEMENT_CLASS_VOCAB),
        _clampi(request["own_distance_check_mode"], DCM_VOCAB),
        _clampi(request["own_render_class"], RENDER_VOCAB),
        _clampi(request["own_command_base"], COMMAND_BASE_VOCAB),
        _clampi(request["own_movement_state"], MOVESTATE_VOCAB),
        _clampi(request["own_semantic_order"], SEMANTIC_VOCAB),
        _clampi(request["own_order_status"], STATUS_VOCAB),
        _clampi(request["own_engine_order_match"], MATCH_VOCAB),
        _clampi(request["own_last_attempt_command"], LAST_CMD_VOCAB),
        _clampi(request["own_last_attempt_result"], LAST_RESULT_VOCAB),
        [p & 0xFF for p in request["own_presence_bits"]],
        [0] * u,
    ]
    own_cat = [list(row) for row in zip(*own_cat_columns)] if u else []
    own_feat = [list(request["own_feature"][i]) + _own_extra(request, i) for i in range(u)]
    target_cat = []
    for i in range(e):
        target_cat.append([
            _clampi([request["target_type_id"][i]], TYPE_VOCAB)[0],
            _clampi([request["target_owner"][i]], OWNER_VOCAB)[0],
            _clampi([request["target_role"][i]], TARGET_ROLE_VOCAB)[0],
            _clampi([request["target_render_class"][i]], RENDER_VOCAB)[0],
        ])
    cand_cat = []
    cand_feat = []
    for cand in request["candidates"]:
        cand_cat.append([cand.kind, _clampi([cand.object_id], TYPE_VOCAB)[0]])
        cand_feat.append(list(cand.feature) +
                         [float((cand.flags >> b) & 1) for b in range(6)])
    command_mask = _bits_of_ints(list(request["command_mask"]), COMMAND_COUNT)
    point_mask = _bits_of_words(request["point_mask"], wire.POINT_COUNT)
    pair_mask = _bits_of_words(request["attack_pair_mask_words"], e) if u else \
        torch.zeros(0, e, dtype=torch.bool)
    econ_mask = _bits_of_words(request["economy_pair_mask_words"], c) if u else \
        torch.zeros(0, c, dtype=torch.bool)
    assign_mask = _bits_of_ints(list(request["own_assign_mask"]), SLOT_COUNT)
    own_slot = [slot_id if slot_id < SLOT_COUNT else SLOT_COUNT
                for slot_id in request["own_slot_id"]]
    own_relation = [min(relation, RELATION_VOCAB - 1)
                    for relation in request["own_slot_order_relation"]]
    slot_cont = []
    slot_command_cur = []
    slot_cell_cur = []
    slot_command_mask = _bits_of_ints(list(request["slot_command_mask"]), SLOT_COMMAND_COUNT)
    slot_cell_mask = _bits_of_words(request["slot_cell_mask_words"], CELL_COUNT)
    for block in request["slots"]:
        slot_cont.append([min(block.member_count / 16.0, 4.0),
                          min(max(block.centroid_x, 0) / INTENT_DISTANCE_SCALE, 2.0),
                          min(max(block.centroid_y, 0) / INTENT_DISTANCE_SCALE, 2.0),
                          float(block.active), min(block.age_frames / 256.0, 4.0),
                          min(block.pursuing / 16.0, 4.0), min(block.terminal / 16.0, 4.0),
                          min(block.differing / 16.0, 4.0)])
        slot_command_cur.append(block.command if block.active else 0)
        slot_cell_cur.append(block.cell + 1 if block.active and block.cell >= 0 else 0)
    start_cells = []
    start_flags = []
    for cand in request["start_candidates"]:
        start_cells.append(cand.cell + 1 if cand.cell >= 0 else 0)
        start_flags.append([float(cand.explored), float(cand.is_own)])
    step = EntityStep(
        global_feat=torch.tensor(global_feat, dtype=torch.float32),
        own_cat=torch.tensor(own_cat, dtype=torch.long).reshape(u, OWN_CAT_FIELDS),
        own_feat=torch.tensor(own_feat, dtype=torch.float32).reshape(
            u, wire.OWN_CONTINUOUS_COUNT + OWN_EXTRA),
        own_role=torch.tensor(list(request["own_role"]), dtype=torch.long),
        active_cand_row=torch.tensor(
            list(request["own_active_economy_candidate_row"]), dtype=torch.long),
        command_mask=command_mask,
        point_mask=point_mask,
        target_cat=torch.tensor(target_cat, dtype=torch.long).reshape(e, TARGET_CAT_FIELDS),
        target_feat=torch.tensor(
            [list(x) for x in request["target_feature"]], dtype=torch.float32
        ).reshape(e, wire.TARGET_CONTINUOUS_COUNT),
        pair_mask=pair_mask,
        cand_cat=torch.tensor(cand_cat, dtype=torch.long).reshape(c, 2),
        cand_feat=torch.tensor(cand_feat, dtype=torch.float32).reshape(c, CAND_FEAT),
        econ_mask=econ_mask,
        candidates=list(request["candidates"]),
        spendable=spend,
        slot_cont=torch.tensor(slot_cont, dtype=torch.float32),
        slot_command_cur=torch.tensor(slot_command_cur, dtype=torch.long),
        slot_cell_cur=torch.tensor(slot_cell_cur, dtype=torch.long),
        slot_command_mask=slot_command_mask,
        slot_cell_mask=slot_cell_mask,
        start_cells=torch.tensor(start_cells, dtype=torch.long),
        start_flags=torch.tensor(start_flags, dtype=torch.float32),
        own_slot=torch.tensor(own_slot, dtype=torch.long),
        own_relation=torch.tensor(own_relation, dtype=torch.long),
        assign_mask=assign_mask,
        scout_free=wire.scout_free_at_snapshot(request),
    )
    return squads.compact_step(step, request) if grouped else step


# ---------------------------------------------------------------------------
# Network
# ---------------------------------------------------------------------------


class Entity2Net(nn.Module):
    def __init__(self, hidden: int = 128, issue_prior: Optional[float] = None,
                 slot_keep_prior: Optional[float] = 0.98,
                 assign_keep_prior: Optional[float] = 0.98,
                 economy_issue_prior: Optional[float] = None):
        super().__init__()
        self.hidden = hidden
        h = hidden
        self.type_emb = nn.Embedding(TYPE_VOCAB, 16)
        self.movement_emb = nn.Embedding(MOVEMENT_CLASS_VOCAB, 4)
        self.dcm_emb = nn.Embedding(DCM_VOCAB, 2)
        self.render_emb = nn.Embedding(RENDER_VOCAB, 4)
        self.command_base_emb = nn.Embedding(COMMAND_BASE_VOCAB, 8)
        self.movestate_emb = nn.Embedding(MOVESTATE_VOCAB, 4)
        self.semantic_emb = nn.Embedding(SEMANTIC_VOCAB, 4)
        self.status_emb = nn.Embedding(STATUS_VOCAB, 4)
        self.match_emb = nn.Embedding(MATCH_VOCAB, 2)
        self.last_cmd_emb = nn.Embedding(LAST_CMD_VOCAB, 4)
        self.last_result_emb = nn.Embedding(LAST_RESULT_VOCAB, 4)
        self.own_role_emb = nn.Embedding(OWN_ROLE_VOCAB, 4)
        self.target_role_emb = nn.Embedding(TARGET_ROLE_VOCAB, 2)
        self.owner_emb = nn.Embedding(OWNER_VOCAB, 4)
        self.kind_emb = nn.Embedding(CAND_KIND_VOCAB, 4)
        # Team intent embeddings.
        self.slot_id_emb = nn.Embedding(SLOT_ID_VOCAB, 4)
        self.relation_emb = nn.Embedding(RELATION_VOCAB, 2)
        self.slot_cmd_emb = nn.Embedding(SLOT_COMMAND_COUNT, 4)
        self.cell_emb = nn.Embedding(CELL_VOCAB, 8)
        own_cat_dim = 16 + 4 + 2 + 4 + 8 + 4 + 4 + 4 + 2 + 4 + 4 + 4 + 8 + 4 + 2 + 12
        target_cat_dim = 16 + 4 + 2 + 4
        cand_cat_dim = 4 + 16
        self.global_tower = nn.Sequential(
            nn.Linear(wire.GLOBAL_COUNT + 3, h), nn.ReLU(), nn.Linear(h, h), nn.ReLU())
        self.own_encoder = nn.Sequential(
            nn.Linear(own_cat_dim + wire.OWN_CONTINUOUS_COUNT + OWN_EXTRA +
                      squads.CONTROL_FEATURE_COUNT, h),
            nn.ReLU(), nn.Linear(h, h), nn.ReLU())
        self.target_encoder = nn.Sequential(
            nn.Linear(target_cat_dim + wire.TARGET_CONTINUOUS_COUNT, h), nn.ReLU(),
            nn.Linear(h, h), nn.ReLU())
        self.cand_encoder = nn.Sequential(
            nn.Linear(cand_cat_dim + CAND_FEAT, h), nn.ReLU(), nn.Linear(h, h),
            nn.ReLU())
        self.slot_encoder = nn.Sequential(
            nn.Linear(SLOT_CONT + 4 + 8 + 4, h), nn.ReLU(), nn.Linear(h, h), nn.ReLU())
        self.start_encoder = nn.Linear(8 + 2, 16)
        self.active_cand_proj = nn.Linear(h, h)
        self.role_adapter = nn.ModuleList(
            [nn.Linear(h, h) for _ in range(OWN_ROLE_VOCAB)])
        self.kind_mean_proj = nn.Linear(h, 8)
        # static = [g, pool_own, pool_target, pool_cand, pool_slot, start_ctx]
        self.static_dim = h * 5 + 16
        self.context = nn.Sequential(
            nn.Linear(h + self.static_dim + LEDGER_CONTEXT + ROW_INTENT, h), nn.ReLU(),
            nn.Linear(h, h), nn.ReLU())
        self.gate_head = nn.Linear(h, 1)
        # Per-role additive gate bias (zero = neutral; checkpoints without it
        # load as zero).  A fresh net gets economy rows (worker / building) a
        # higher issue prior than fighters: harvest / build / produce must
        # happen often, random fighter commands only break the slot marches.
        self.gate_role_bias = nn.Embedding(OWN_ROLE_VOCAB, 1)
        with torch.no_grad():
            self.gate_role_bias.weight.zero_()
        if issue_prior is not None:
            prior = min(max(float(issue_prior), 1e-4), 1.0 - 1e-4)
            with torch.no_grad():
                self.gate_head.bias.fill_(math.log(prior / (1.0 - prior)))
            if economy_issue_prior is not None:
                econ = min(max(float(economy_issue_prior), 1e-4), 1.0 - 1e-4)
                delta = math.log(econ / (1.0 - econ)) - math.log(prior / (1.0 - prior))
                with torch.no_grad():
                    for role in (wire.ROLE_WORKER, wire.ROLE_BUILDING):
                        self.gate_role_bias.weight[role, 0] = delta
        self.command_head = nn.Linear(h, NON_KEEP_COUNT)
        self.command_emb = nn.Embedding(COMMAND_COUNT, 16)
        self.point_head = nn.Sequential(
            nn.Linear(h + 16, h), nn.ReLU(), nn.Linear(h, wire.POINT_COUNT))
        self.pointer_q = nn.Linear(h, h)
        self.pointer_k = nn.Linear(h, h)
        self.pointer_bias = nn.Sequential(nn.Linear(3, 32), nn.ReLU(), nn.Linear(32, 1))
        self.cand_q = nn.ModuleList([nn.Linear(h, h) for _ in range(CAND_KIND_VOCAB)])
        self.cand_k = nn.Linear(h, h)
        # Assign head (keep + 4 slots) per row.
        self.assign_head = nn.Linear(h, ASSIGN_OPTIONS)
        # Commander: per-slot context from [h_slot, static] -> command logits
        # and a command-conditioned 64-cell head with per-cell features
        # (cell embedding + explored/enemy-memory/start-candidate biases
        # come through the global tower; the cell embedding is shared).
        self.commander_tower = nn.Sequential(
            nn.Linear(h + self.static_dim, h), nn.ReLU(), nn.Linear(h, h), nn.ReLU())
        self.slot_command_head = nn.Linear(h, SLOT_COMMAND_COUNT)
        if slot_keep_prior is not None:
            prior = min(max(float(slot_keep_prior), 1e-4), 1.0 - 1e-4)
            with torch.no_grad():
                self.slot_command_head.bias.zero_()
                # KEEP logit such that softmax over 7 usable commands gives
                # ~prior to KEEP at initialisation.
                self.slot_command_head.bias[wire.SLOT_COMMAND_KEEP] = math.log(
                    prior / (1.0 - prior) * 6.0)
        if assign_keep_prior is not None:
            # Slot moves: a fresh net would move ~3/4 of the members every
            # tick (uniform over keep + 3 legal slots), re-deriving their
            # orders each time.  KEEP gets ~prior at initialisation.
            prior = min(max(float(assign_keep_prior), 1e-4), 1.0 - 1e-4)
            with torch.no_grad():
                self.assign_head.bias.zero_()
                self.assign_head.bias[0] = math.log(prior / (1.0 - prior) * 3.0)
        self.slot_cell_q = nn.Linear(h + 4, 8)
        self.value_head = nn.Sequential(
            nn.Linear(self.static_dim, h), nn.ReLU(), nn.Linear(h, 1))

    # ---- encoders ----
    def _encode_own(self, step: EntityStep) -> torch.Tensor:
        cat = step.own_cat
        presence = cat[:, 11]
        bits = torch.stack([((presence >> b) & 1).float() for b in range(8)], dim=-1)
        slot = step.own_slot
        slot_index = torch.clamp(slot, max=SLOT_COUNT - 1)
        cur_cmd = step.slot_command_cur[slot_index]
        cur_cell = step.slot_cell_cur[slot_index]
        none = (slot >= SLOT_COUNT).unsqueeze(-1)
        slot_order = torch.cat([self.slot_cmd_emb(cur_cmd), self.cell_emb(cur_cell)], -1)
        slot_order = slot_order.masked_fill(none, 0.0)
        parts = [self.type_emb(cat[:, 0]), self.movement_emb(cat[:, 1]),
                 self.dcm_emb(cat[:, 2]), self.render_emb(cat[:, 3]),
                 self.command_base_emb(cat[:, 4]), self.movestate_emb(cat[:, 5]),
                 self.semantic_emb(cat[:, 6]), self.status_emb(cat[:, 7]),
                 self.match_emb(cat[:, 8]), self.last_cmd_emb(cat[:, 9]),
                 self.last_result_emb(cat[:, 10]), self.own_role_emb(step.own_role),
                 bits, self.slot_id_emb(slot), self.relation_emb(step.own_relation),
                 slot_order, step.own_feat,
                 step.control_feat if step.control_feat is not None else
                 step.own_feat.new_zeros(step.u, squads.CONTROL_FEATURE_COUNT)]
        return self.own_encoder(torch.cat(parts, dim=-1))

    def _encode_target(self, step: EntityStep) -> torch.Tensor:
        cat = step.target_cat
        parts = [self.type_emb(cat[:, 0]), self.owner_emb(cat[:, 1]),
                 self.target_role_emb(cat[:, 2]), self.render_emb(cat[:, 3]),
                 step.target_feat]
        return self.target_encoder(torch.cat(parts, dim=-1))

    def _encode_cand(self, step: EntityStep) -> torch.Tensor:
        cat = step.cand_cat
        parts = [self.kind_emb(cat[:, 0]), self.type_emb(cat[:, 1]), step.cand_feat]
        return self.cand_encoder(torch.cat(parts, dim=-1))

    def _encode_slots(self, step: EntityStep) -> torch.Tensor:
        ids = torch.arange(SLOT_COUNT, dtype=torch.long)
        parts = [step.slot_cont, self.slot_cmd_emb(step.slot_command_cur),
                 self.cell_emb(step.slot_cell_cur), self.slot_id_emb(ids)]
        return self.slot_encoder(torch.cat(parts, dim=-1))

    def encode(self, step: EntityStep) -> Dict:
        """Static (prefix-independent) encodings and the value."""
        h = self.hidden
        g = self.global_tower(step.global_feat.unsqueeze(0))   # [1, h]
        u, e, c = step.u, step.e, step.c
        h_cand = self._encode_cand(step) if c > 0 else torch.zeros(0, h)
        pool_cand = h_cand.mean(0, keepdim=True) if c > 0 else torch.zeros(1, h)
        h_target = self._encode_target(step) if e > 0 else torch.zeros(0, h)
        pool_target = h_target.mean(0, keepdim=True) if e > 0 else torch.zeros(1, h)
        h_slot = self._encode_slots(step)                       # [4, h]
        pool_slot = h_slot.mean(0, keepdim=True)
        present = (step.start_cells > 0).float().unsqueeze(-1)  # [8, 1]
        start = self.start_encoder(torch.cat([self.cell_emb(step.start_cells),
                                              step.start_flags], -1)) * present
        start_ctx = start.sum(0, keepdim=True) / present.sum().clamp(min=1.0)
        if u > 0:
            h_own = self._encode_own(step)
            gathered = torch.zeros(u, h)
            has = step.active_cand_row >= 0
            if c > 0 and bool(has.any()):
                gathered[has] = h_cand[step.active_cand_row[has]]
            h_own = h_own + self.active_cand_proj(gathered)
            adapted = torch.zeros_like(h_own)
            for role in range(OWN_ROLE_VOCAB):
                sel = step.own_role == role
                if bool(sel.any()):
                    adapted[sel] = F.relu(self.role_adapter[role](h_own[sel]))
            h_own = h_own + adapted
            pool_own = h_own.mean(0, keepdim=True)
        else:
            h_own = torch.zeros(0, h)
            pool_own = torch.zeros(1, h)
        static = torch.cat([g, pool_own, pool_target, pool_cand, pool_slot, start_ctx],
                           dim=-1)                                # [1, static_dim]
        value = self.value_head(static).squeeze(-1).squeeze(-1)
        return {"g": g, "h_own": h_own, "h_target": h_target, "h_cand": h_cand,
                "h_slot": h_slot, "static": static, "value": value,
                "role": step.own_role, "u": u, "e": e, "c": c}

    # ---- commander ----
    def commander(self, enc: Dict) -> Dict:
        """Per-slot context and command logits ([4, h], [4, 8])."""
        static = enc["static"].expand(SLOT_COUNT, -1)
        ctx = self.commander_tower(torch.cat([enc["h_slot"], static], dim=-1))
        return {"ctx": ctx, "command_logits": self.slot_command_head(ctx)}

    def slot_cell_logits(self, ctx_slot: torch.Tensor, command: int) -> torch.Tensor:
        """64 cell logits for one slot given its chosen command."""
        cmd = self.slot_cmd_emb(torch.tensor([command], dtype=torch.long))
        q = self.slot_cell_q(torch.cat([ctx_slot.unsqueeze(0), cmd], -1))      # [1, 8]
        cells = self.cell_emb(torch.arange(1, CELL_COUNT + 1, dtype=torch.long))  # [64, 8]
        return (q @ cells.t()).squeeze(0) / math.sqrt(8.0)

    # ---- rows ----
    def ledger_context(self, enc: Dict, budget: Sequence[int],
                       kind_counts: Sequence[int],
                       kind_members: Sequence[Sequence[int]]) -> torch.Tensor:
        parts = [torch.tensor([min(budget[0] / BUDGET_NORM[0], 4.0),
                               min(budget[1] / BUDGET_NORM[1], 4.0),
                               min(budget[2] / BUDGET_NORM[2], 4.0)] +
                              [min(k / 8.0, 2.0) for k in kind_counts],
                              dtype=torch.float32)]
        for kind in range(CAND_KIND_VOCAB):
            members = list(kind_members[kind])
            if members and enc["c"] > 0:
                mean = enc["h_cand"][torch.tensor(members, dtype=torch.long)].mean(0)
                parts.append(self.kind_mean_proj(mean))
            else:
                parts.append(torch.zeros(8))
        return torch.cat(parts, dim=-1)

    def row_intent(self, slot: int, slot_command: int, slot_cell: int) -> torch.Tensor:
        """Embedding of the (new) order of the slot a row belongs to."""
        if slot >= SLOT_COUNT:
            return torch.zeros(ROW_INTENT)
        cmd = self.slot_cmd_emb(torch.tensor([slot_command], dtype=torch.long))[0]
        cell = self.cell_emb(torch.tensor([slot_cell + 1 if slot_cell >= 0 else 0],
                                          dtype=torch.long))[0]
        return torch.cat([cmd, cell], -1)

    def row_heads(self, enc: Dict, rows: torch.Tensor, ledger: torch.Tensor,
                  intent: torch.Tensor) -> Dict:
        """Head logits for the given row indices with their ledger context
        ([R, LEDGER_CONTEXT]) and slot intent ([R, ROW_INTENT])."""
        h_rows = enc["h_own"][rows]
        static = enc["static"].expand(h_rows.shape[0], -1)
        ctx = self.context(torch.cat([h_rows, static, ledger, intent], dim=-1))
        gate_logit = self.gate_head(ctx).squeeze(-1)
        if "role" in enc:
            gate_logit = gate_logit + self.gate_role_bias(enc["role"][rows]).squeeze(-1)
        return {"ctx": ctx,
                "gate_logit": gate_logit,
                "command_logits": self.command_head(ctx),
                "assign_logits": self.assign_head(ctx)}

    def point_logits(self, ctx: torch.Tensor, command: torch.Tensor) -> torch.Tensor:
        emb = self.command_emb(command)
        return self.point_head(torch.cat([ctx, emb], dim=-1))

    def attack_logits(self, enc: Dict, ctx: torch.Tensor, rows: torch.Tensor,
                      step: EntityStep) -> torch.Tensor:
        q = self.pointer_q(ctx)
        k = self.pointer_k(enc["h_target"])
        scores = q @ k.t() / math.sqrt(self.hidden)
        own_xy = step.own_feat[rows][:, 0:2]
        tgt_xy = step.target_feat[:, 0:2]
        delta = tgt_xy.unsqueeze(0) - own_xy.unsqueeze(1)
        dist = delta.norm(dim=-1, keepdim=True)
        bias = self.pointer_bias(torch.cat([delta, dist], dim=-1)).squeeze(-1)
        return scores + bias

    def cand_logits(self, enc: Dict, ctx: torch.Tensor, kind: int) -> torch.Tensor:
        q = self.cand_q[kind](ctx)
        k = self.cand_k(enc["h_cand"])
        return q @ k.t() / math.sqrt(self.hidden)


NEG_INF = -1e9


def _masked_log_softmax(logits: torch.Tensor, mask: torch.Tensor) -> torch.Tensor:
    return F.log_softmax(logits.masked_fill(~mask, NEG_INF), dim=-1)


def _entropy_from_logp(logp: torch.Tensor, mask: torch.Tensor) -> torch.Tensor:
    p = logp.exp() * mask.float()
    return -(p * logp.masked_fill(~mask, 0.0)).sum(-1)


# ---------------------------------------------------------------------------
# Ledgers in tensor form (mirror of wire.replay_ledger on a step).
# ---------------------------------------------------------------------------


class StepLedger:
    """Owner-scoped economy ledger over an EntityStep (same rules as the
    contract's EconomyLedger, plus the L_i bookkeeping).  Candidate
    availability is evaluated for the whole table at once (numpy) and cached
    until the next reservation, since one tick reserves only a few times."""

    def __init__(self, step: EntityStep):
        self.step = step
        self.remaining = [int(step.spendable[0]), int(step.spendable[1]),
                          int(step.spendable[2])]
        self.reserved_sites: List[Tuple[int, int, int, int]] = []
        self.reserved_keys: set = set()
        self.reserved_research: set = set()
        self.kind_counts = [0, 0, 0, 0]
        self.kind_members: List[List[int]] = [[], [], [], []]
        cands = step.candidates
        self.kind = np.array([cd.kind for cd in cands], dtype=np.int64)
        self.raw0 = np.array([cd.raw0 for cd in cands], dtype=np.int64)
        self.raw1 = np.array([cd.raw1 for cd in cands], dtype=np.int64)
        self.raw2 = np.array([cd.raw2 for cd in cands], dtype=np.int64)
        self.object_id = np.array([cd.object_id for cd in cands], dtype=np.int64)
        rects = np.array([cd.footprint_rect() if cd.kind == wire.CAND_BUILD_SITE
                          else (0, 0, 0, 0) for cd in cands], dtype=np.int64).reshape(-1, 4)
        self.rect_x0, self.rect_y0, self.rect_x1, self.rect_y1 = (
            rects[:, 0], rects[:, 1], rects[:, 2], rects[:, 3])
        self.reserved_indices: set = set()
        self._available: Optional[np.ndarray] = None

    def available_vector(self) -> np.ndarray:
        """bool [C]: candidate affordable/unreserved under the current state
        (identical rules to wire.EconomyLedger.candidate_available)."""
        if self._available is None:
            kind = self.kind
            avail = np.ones(len(kind), dtype=bool)
            non_resource = kind != wire.CAND_RESOURCE
            avail &= ~non_resource | ((self.raw0 <= self.remaining[0]) &
                                      (self.raw1 <= self.remaining[1]))
            produce = kind == wire.CAND_PRODUCE_UNIT
            avail &= ~produce | (self.raw2 <= self.remaining[2])
            build = kind == wire.CAND_BUILD_SITE
            for index in self.reserved_indices:
                avail[index] = False
            for rx0, ry0, rx1, ry1 in self.reserved_sites:
                overlap = (self.rect_x0 < rx1) & (rx0 < self.rect_x1) & \
                    (self.rect_y0 < ry1) & (ry0 < self.rect_y1)
                avail &= ~(build & overlap)
            research = kind == wire.CAND_RESEARCH_UPGRADE
            for order in self.reserved_research:
                avail &= ~(research & (self.object_id == order))
            self._available = avail
        return self._available

    def available(self, index: int) -> bool:
        return bool(self.available_vector()[index])

    def dynamic_masks(self, row: int) -> Tuple[torch.Tensor, torch.Tensor]:
        base_cmd = self.step.command_mask[row]
        c = self.step.c
        if c:
            dyn_pair_np = self.step.econ_mask[row].numpy() & self.available_vector()
            kinds = np.bincount(self.kind[dyn_pair_np], minlength=4) > 0
            dyn_pair = torch.from_numpy(dyn_pair_np)
        else:
            kinds = np.zeros(4, dtype=bool)
            dyn_pair = torch.zeros(0, dtype=torch.bool)
        dyn_cmd = base_cmd.clone()
        for kind, command in wire.COMMAND_OF_KIND.items():
            dyn_cmd[command] = bool(base_cmd[command]) and bool(kinds[kind])
        return dyn_cmd, dyn_pair

    def reserve(self, command: int, argument: int) -> None:
        if command not in ECONOMY_COMMANDS:
            return
        cand = self.step.candidates[argument]
        self.kind_counts[cand.kind] += 1
        self.kind_members[cand.kind].append(argument)
        if cand.kind == wire.CAND_RESOURCE:
            return
        self.remaining[0] -= cand.raw0
        self.remaining[1] -= cand.raw1
        if cand.kind == wire.CAND_BUILD_SITE:
            self.reserved_sites.append(cand.footprint_rect())
            self.reserved_keys.add((cand.kind, cand.key))
            self.reserved_indices.add(argument)
        elif cand.kind == wire.CAND_PRODUCE_UNIT:
            self.remaining[2] -= cand.raw2
        else:
            self.reserved_research.add(cand.object_id)
        self._available = None


class StepAssignLedger:
    def __init__(self, step: EntityStep):
        self.scout_free = step.scout_free
        self.scout_taken = 0

    def dynamic_mask(self, base: torch.Tensor) -> torch.Tensor:
        mask = base.clone()
        if self.scout_taken >= self.scout_free:
            mask[wire.SLOT_SCOUT] = False
        return mask

    def apply(self, assign: int) -> None:
        if assign == wire.SLOT_SCOUT + 1:
            self.scout_taken += 1


def _choice_legal(step: EntityStep, row: int, dyn_cmd: torch.Tensor,
                  dyn_pair: torch.Tensor, command: int, argument: int) -> bool:
    if not bool(dyn_cmd[command]):
        return False
    if command in wire.NO_ARGUMENT_COMMANDS:
        return argument == -1
    if command in POINT_COMMANDS:
        return 0 <= argument < wire.POINT_COUNT and bool(step.point_mask[row, argument])
    if command == wire.COMMAND_ATTACK_UNIT:
        return 0 <= argument < step.e and bool(step.pair_mask[row, argument])
    if command in ECONOMY_COMMANDS:
        if not 0 <= argument < step.c:
            return False
        if step.candidates[argument].kind != wire.KIND_OF_COMMAND[command]:
            return False
        return bool(dyn_pair[argument])
    return False


def attach_teacher_blocks(step: EntityStep, commands: torch.Tensor,
                          arguments: torch.Tensor, assigns: torch.Tensor,
                          unresolved_from: Optional[int] = None) -> None:
    """Fill the stored dynamic blocks by replaying the ledgers over a
    teacher-forced prefix (what the SHD3 writer stores).  Illegal choices
    consume nothing; rows at/after `unresolved_from` get an economy-free
    mask (PREFIX_UNRESOLVED)."""
    u = step.u
    ledger = StepLedger(step)
    assign_ledger = StepAssignLedger(step)
    step.unresolved_from = unresolved_from
    step.dyn_command_mask = torch.zeros(u, COMMAND_COUNT, dtype=torch.bool)
    step.dyn_econ_mask = torch.zeros(u, step.c, dtype=torch.bool)
    step.dyn_assign_mask = torch.zeros(u, SLOT_COUNT, dtype=torch.bool)
    step.budget_before = torch.zeros(u, 3, dtype=torch.long)
    for row in range(u):
        dyn_assign = assign_ledger.dynamic_mask(step.assign_mask[row])
        step.dyn_assign_mask[row] = dyn_assign
        assign = int(assigns[row])
        if _assign_legal(dyn_assign, assign):
            assign_ledger.apply(assign)
        if unresolved_from is not None and row >= unresolved_from:
            dyn_cmd = step.command_mask[row].clone()
            dyn_cmd[wire.COMMAND_HARVEST:] = False
            dyn_pair = torch.zeros(step.c, dtype=torch.bool)
        else:
            dyn_cmd, dyn_pair = ledger.dynamic_masks(row)
        step.dyn_command_mask[row] = dyn_cmd
        step.dyn_econ_mask[row] = dyn_pair
        step.budget_before[row] = torch.tensor(ledger.remaining, dtype=torch.long)
        if unresolved_from is not None and row >= unresolved_from:
            continue
        command = int(commands[row])
        argument = int(arguments[row])
        if command in ECONOMY_COMMANDS and _choice_legal(step, row, dyn_cmd, dyn_pair,
                                                         command, argument):
            ledger.reserve(command, argument)


def _assign_legal(dyn_assign: torch.Tensor, assign: int) -> bool:
    if assign == 0:
        return True
    return 1 <= assign <= SLOT_COUNT and bool(dyn_assign[assign - 1])


def _slot_choice_legal(step: EntityStep, slot: int, command: int, cell: int) -> bool:
    if not bool(step.slot_command_mask[slot, command]):
        return False
    if command in wire.SLOT_POINT_COMMANDS:
        return 0 <= cell < CELL_COUNT and bool(step.slot_cell_mask[slot, cell])
    return cell == -1


# ---------------------------------------------------------------------------
# Distributions
# ---------------------------------------------------------------------------


def _row_distribution(heads: Dict, index: int, dyn_cmd: torch.Tensor) -> Dict:
    can_issue = bool(dyn_cmd[1:].any())
    gate_logit = heads["gate_logit"][index]
    cmd_mask = dyn_cmd[1:]
    cmd_logp = _masked_log_softmax(heads["command_logits"][index],
                                   cmd_mask if can_issue else torch.ones_like(cmd_mask))
    return {"can_issue": can_issue, "logp_issue": F.logsigmoid(gate_logit),
            "logp_keep": F.logsigmoid(-gate_logit), "cmd_mask": cmd_mask,
            "cmd_logp": cmd_logp}


def _assign_distribution(heads: Dict, index: int, dyn_assign: torch.Tensor):
    """(log-probs over keep+4, mask, stochastic) for one row."""
    mask = torch.cat([torch.ones(1, dtype=torch.bool), dyn_assign])
    stochastic = bool(dyn_assign.any())
    logp = _masked_log_softmax(heads["assign_logits"][index], mask)
    return logp, mask, stochastic


def _argument_logp(net: Entity2Net, enc: Dict, step: EntityStep, row: int,
                   ctx: torch.Tensor, command: int, dyn_pair: torch.Tensor
                   ) -> Tuple[torch.Tensor, torch.Tensor]:
    rows = torch.tensor([row], dtype=torch.long)
    if command in POINT_COMMANDS:
        logits = net.point_logits(ctx.unsqueeze(0),
                                  torch.tensor([command], dtype=torch.long))[0]
        mask = step.point_mask[row]
        return _masked_log_softmax(logits, mask), mask
    if command == wire.COMMAND_ATTACK_UNIT:
        logits = net.attack_logits(enc, ctx.unsqueeze(0), rows, step)[0]
        mask = step.pair_mask[row]
        return _masked_log_softmax(logits, mask), mask
    kind = wire.KIND_OF_COMMAND[command]
    logits = net.cand_logits(enc, ctx.unsqueeze(0), kind)[0]
    kind_mask = step.cand_cat[:, 0] == kind
    mask = dyn_pair & kind_mask
    return _masked_log_softmax(logits, mask), mask


def _needs_argument(command: int) -> bool:
    return command not in wire.NO_ARGUMENT_COMMANDS


def _slot_logp_entropy(net: Entity2Net, step: EntityStep, com: Dict, slot: int,
                       command: int, cell: int) -> Tuple[torch.Tensor, torch.Tensor, bool]:
    """(log-prob, entropy, stochastic) of one slot's (command, cell)."""
    mask = step.slot_command_mask[slot]
    stochastic = bool(mask[1:].any())
    if not stochastic:
        return torch.zeros(()), torch.zeros(()), False
    cmd_logp = _masked_log_softmax(com["command_logits"][slot], mask)
    logp = cmd_logp[command]
    entropy = _entropy_from_logp(cmd_logp, mask)
    if command in wire.SLOT_POINT_COMMANDS:
        cell_logits = net.slot_cell_logits(com["ctx"][slot], command)
        cell_logp = _masked_log_softmax(cell_logits, step.slot_cell_mask[slot])
        logp = logp + cell_logp[cell]
        entropy = entropy + _entropy_from_logp(cell_logp, step.slot_cell_mask[slot])
    return logp, entropy, True


# ---------------------------------------------------------------------------
# Sampling (inference) — commander first, then rows with both ledgers.
# ---------------------------------------------------------------------------


@torch.no_grad()
def sample_actions(net: Entity2Net, step: EntityStep,
                   rng: Optional[torch.Generator] = None) -> Dict:
    enc = net.encode(step)
    com = net.commander(enc)
    slot_command = torch.zeros(SLOT_COUNT, dtype=torch.long)
    slot_cell = torch.full((SLOT_COUNT,), -1, dtype=torch.long)
    slot_logp = torch.zeros(SLOT_COUNT)
    slot_stochastic = torch.zeros(SLOT_COUNT, dtype=torch.bool)
    for slot in range(SLOT_COUNT):
        mask = step.slot_command_mask[slot]
        if not bool(mask[1:].any()):
            continue
        slot_stochastic[slot] = True
        cmd_logp = _masked_log_softmax(com["command_logits"][slot], mask)
        command = int(torch.multinomial(cmd_logp.exp(), 1, generator=rng).item())
        logp = cmd_logp[command]
        cell = -1
        if command in wire.SLOT_POINT_COMMANDS:
            cell_logp = _masked_log_softmax(net.slot_cell_logits(com["ctx"][slot], command),
                                            step.slot_cell_mask[slot])
            cell = int(torch.multinomial(cell_logp.exp(), 1, generator=rng).item())
            logp = logp + cell_logp[cell]
        slot_command[slot] = command
        slot_cell[slot] = cell
        slot_logp[slot] = logp
    # Effective slot order after the commander (KEEP keeps the current one).
    eff_cmd = [int(step.slot_command_cur[s]) for s in range(SLOT_COUNT)]
    eff_cell = [int(step.slot_cell_cur[s]) - 1 for s in range(SLOT_COUNT)]
    for s in range(SLOT_COUNT):
        c = int(slot_command[s])
        if c in wire.SLOT_POINT_COMMANDS:
            eff_cmd[s], eff_cell[s] = c, int(slot_cell[s])
        elif c in (wire.SLOT_COMMAND_HOLD, wire.SLOT_COMMAND_HUNT_NEUTRAL):
            eff_cmd[s], eff_cell[s] = c, -1
        elif c == wire.SLOT_COMMAND_STOP:
            eff_cmd[s], eff_cell[s] = 0, -1

    u = step.u
    ledger = StepLedger(step)
    assign_ledger = StepAssignLedger(step)
    commands = torch.zeros(u, dtype=torch.long)
    arguments = torch.full((u,), -1, dtype=torch.long)
    assigns = torch.zeros(u, dtype=torch.long)
    logps = torch.zeros(u)
    assign_logps = torch.zeros(u)
    dyn_cmds = torch.zeros(u, COMMAND_COUNT, dtype=torch.bool)
    dyn_pairs = torch.zeros(u, step.c, dtype=torch.bool)
    dyn_assigns = torch.zeros(u, SLOT_COUNT, dtype=torch.bool)
    budgets = torch.zeros(u, 3, dtype=torch.long)
    stochastic = torch.zeros(u, dtype=torch.bool)
    assign_stochastic = torch.zeros(u, dtype=torch.bool)
    # The row context sees the CURRENT slot's new order (assign is sampled
    # from the same context; a moved row adopts its new slot's order on the
    # C++ side).  Intents do not depend on the ledger, so they are built once.
    intents = torch.zeros(u, ROW_INTENT)
    for row in range(u):
        slot = int(step.own_slot[row])
        intents[row] = net.row_intent(slot, eff_cmd[slot] if slot < SLOT_COUNT else 0,
                                      eff_cell[slot] if slot < SLOT_COUNT else -1)
    # Heads are computed for every remaining row in one batch under the
    # current ledger state and recomputed only after a reservation changes
    # that state (a few times per tick), which gives exactly the per-row
    # sequential distribution teacher_forced_logp recomputes.
    heads = None
    heads_base = 0
    for row in range(u):
        if heads is None:
            ledger_ctx = net.ledger_context(enc, ledger.remaining, ledger.kind_counts,
                                            ledger.kind_members)
            heads = net.row_heads(enc, torch.arange(row, u, dtype=torch.long),
                                  ledger_ctx.unsqueeze(0).expand(u - row, -1),
                                  intents[row:u])
            heads_base = row
        index = row - heads_base
        dyn_cmd, dyn_pair = ledger.dynamic_masks(row)
        dyn_assign = assign_ledger.dynamic_mask(step.assign_mask[row])
        dyn_cmds[row] = dyn_cmd
        dyn_pairs[row] = dyn_pair
        dyn_assigns[row] = dyn_assign
        budgets[row] = torch.tensor(ledger.remaining, dtype=torch.long)
        stochastic[row] = bool(dyn_cmd[1:].any())
        a_logp, a_mask, a_stoch = _assign_distribution(heads, index, dyn_assign)
        if a_stoch:
            assign_stochastic[row] = True
            choice = int(torch.multinomial(a_logp.exp(), 1, generator=rng).item())
            assigns[row] = choice
            assign_logps[row] = a_logp[choice]
            assign_ledger.apply(choice)
        dist = _row_distribution(heads, index, dyn_cmd)
        if not dist["can_issue"]:
            continue
        issue = bool(torch.rand(1, generator=rng).item() < dist["logp_issue"].exp().item())
        if not issue:
            logps[row] = dist["logp_keep"]
            continue
        cmd_index = int(torch.multinomial(dist["cmd_logp"].exp(), 1, generator=rng).item())
        command = cmd_index + 1
        logp = dist["logp_issue"] + dist["cmd_logp"][cmd_index]
        argument = -1
        if _needs_argument(command):
            arg_logp, mask = _argument_logp(net, enc, step, row, heads["ctx"][index],
                                            command, dyn_pair)
            argument = int(torch.multinomial(arg_logp.exp(), 1, generator=rng).item())
            logp = logp + arg_logp[argument]
        commands[row] = command
        arguments[row] = argument
        logps[row] = logp
        if command in ECONOMY_COMMANDS:
            ledger.reserve(command, argument)
            heads = None    # ledger context changed for the rows after this one
    return {"command": commands, "argument": arguments, "assign": assigns,
            "slot_command": slot_command, "slot_cell": slot_cell,
            "logp": logps.detach(), "assign_logp": assign_logps.detach(),
            "slot_logp": slot_logp.detach(), "value": enc["value"].detach(),
            "dyn_command_mask": dyn_cmds, "dyn_econ_mask": dyn_pairs,
            "dyn_assign_mask": dyn_assigns, "budget_before": budgets,
            "stochastic": stochastic, "assign_stochastic": assign_stochastic,
            "slot_stochastic": slot_stochastic}


# ---------------------------------------------------------------------------
# Teacher-forced recompute under STORED masks.
# ---------------------------------------------------------------------------


def _effective_slot_orders(step: EntityStep, slot_command: torch.Tensor,
                           slot_cell: torch.Tensor) -> Tuple[List[int], List[int]]:
    eff_cmd = [int(step.slot_command_cur[s]) for s in range(SLOT_COUNT)]
    eff_cell = [int(step.slot_cell_cur[s]) - 1 for s in range(SLOT_COUNT)]
    for s in range(SLOT_COUNT):
        c = int(slot_command[s])
        if c in wire.SLOT_POINT_COMMANDS:
            eff_cmd[s], eff_cell[s] = c, int(slot_cell[s])
        elif c in (wire.SLOT_COMMAND_HOLD, wire.SLOT_COMMAND_HUNT_NEUTRAL):
            eff_cmd[s], eff_cell[s] = c, -1
        elif c == wire.SLOT_COMMAND_STOP:
            eff_cmd[s], eff_cell[s] = 0, -1
    return eff_cmd, eff_cell


def teacher_forced_logp(net: Entity2Net, step: EntityStep,
                        commands: torch.Tensor, arguments: torch.Tensor,
                        assigns: Optional[torch.Tensor] = None,
                        slot_command: Optional[torch.Tensor] = None,
                        slot_cell: Optional[torch.Tensor] = None,
                        enc: Optional[Dict] = None,
                        verify_ledger: bool = True) -> Dict:
    """Per-row/assign/slot log-probs and entropies under the stored dynamic
    masks.  The ledgers are replayed from the stored prefix; the replayed
    masks/budgets must equal the stored ones byte-for-byte or `ok` is False."""
    if enc is None:
        enc = net.encode(step)
    u = step.u
    if assigns is None:
        assigns = torch.zeros(u, dtype=torch.long)
    if slot_command is None:
        slot_command = torch.zeros(SLOT_COUNT, dtype=torch.long)
    if slot_cell is None:
        slot_cell = torch.full((SLOT_COUNT,), -1, dtype=torch.long)
    out = {"logp": torch.zeros(u), "entropy": torch.zeros(u),
           "gate_logp": torch.zeros(u),        # issue/keep part of logp
           "assign_logp": torch.zeros(u), "assign_entropy": torch.zeros(u),
           "slot_logp": torch.zeros(SLOT_COUNT), "slot_entropy": torch.zeros(SLOT_COUNT),
           "ok": True, "enc": enc}
    # Commander.
    com = net.commander(enc)
    for slot in range(SLOT_COUNT):
        command = int(slot_command[slot])
        cell = int(slot_cell[slot])
        if not _slot_choice_legal(step, slot, command, cell) and command != 0:
            out["ok"] = False
            continue
        if command == 0 and cell != -1:
            out["ok"] = False
            continue
        logp, entropy, stochastic = _slot_logp_entropy(net, step, com, slot, command, cell)
        if stochastic:
            out["slot_logp"][slot] = logp
            out["slot_entropy"][slot] = entropy
        elif command != 0:
            out["ok"] = False
    eff_cmd, eff_cell = _effective_slot_orders(step, slot_command, slot_cell)
    ledger = StepLedger(step)
    assign_ledger = StepAssignLedger(step)
    ledger_rows = []
    intent_rows = []
    for row in range(u):
        ledger_rows.append(net.ledger_context(enc, ledger.remaining, ledger.kind_counts,
                                              ledger.kind_members))
        slot = int(step.own_slot[row])
        intent_rows.append(net.row_intent(slot, eff_cmd[slot] if slot < SLOT_COUNT else 0,
                                          eff_cell[slot] if slot < SLOT_COUNT else -1))
        dyn_assign = assign_ledger.dynamic_mask(step.assign_mask[row])
        unresolved = step.unresolved_from is not None and row >= step.unresolved_from
        if verify_ledger:
            if unresolved:
                dyn_cmd = step.command_mask[row].clone()
                dyn_cmd[wire.COMMAND_HARVEST:] = False
                dyn_pair = torch.zeros(step.c, dtype=torch.bool)
            else:
                dyn_cmd, dyn_pair = ledger.dynamic_masks(row)
            if not torch.equal(dyn_cmd, step.dyn_command_mask[row]) or \
                    not torch.equal(dyn_pair, step.dyn_econ_mask[row]) or \
                    not torch.equal(torch.tensor(ledger.remaining, dtype=torch.long),
                                    step.budget_before[row]) or \
                    not torch.equal(dyn_assign, step.dyn_assign_mask[row]):
                out["ok"] = False
        assign = int(assigns[row])
        if _assign_legal(step.dyn_assign_mask[row], assign):
            assign_ledger.apply(assign)
        command = int(commands[row])
        argument = int(arguments[row])
        if not unresolved and command in ECONOMY_COMMANDS and _choice_legal(
                step, row, step.dyn_command_mask[row], step.dyn_econ_mask[row],
                command, argument):
            ledger.reserve(command, argument)
    if u == 0:
        return out
    rows = torch.arange(u, dtype=torch.long)
    heads = net.row_heads(enc, rows, torch.stack(ledger_rows, dim=0),
                          torch.stack(intent_rows, dim=0))
    for row in range(u):
        dyn_cmd = step.dyn_command_mask[row]
        dyn_pair = step.dyn_econ_mask[row]
        # Assign.
        a_logp, a_mask, a_stoch = _assign_distribution(heads, row, step.dyn_assign_mask[row])
        assign = int(assigns[row])
        if a_stoch:
            if not _assign_legal(step.dyn_assign_mask[row], assign):
                out["ok"] = False
            else:
                out["assign_logp"][row] = a_logp[assign]
                out["assign_entropy"][row] = _entropy_from_logp(a_logp, a_mask)
        elif assign != 0:
            out["ok"] = False
        # Command / argument.
        dist = _row_distribution(heads, row, dyn_cmd)
        if not dist["can_issue"]:
            continue
        command = int(commands[row])
        argument = int(arguments[row])
        p_issue = dist["logp_issue"].exp()
        gate_entropy = -(p_issue * dist["logp_issue"] +
                         (1 - p_issue) * dist["logp_keep"])
        if command == COMMAND_KEEP:
            out["logp"][row] = dist["logp_keep"]
            out["gate_logp"][row] = dist["logp_keep"]
            out["entropy"][row] = gate_entropy
            continue
        if not bool(dyn_cmd[command]):
            out["ok"] = False
            continue
        cmd_index = command - 1
        out["gate_logp"][row] = dist["logp_issue"]
        logp = dist["logp_issue"] + dist["cmd_logp"][cmd_index]
        entropy = gate_entropy + _entropy_from_logp(dist["cmd_logp"], dist["cmd_mask"])
        if _needs_argument(command):
            arg_logp, mask = _argument_logp(net, enc, step, row, heads["ctx"][row],
                                            command, dyn_pair)
            if argument < 0 or argument >= mask.shape[0] or not bool(mask[argument]):
                out["ok"] = False
                continue
            logp = logp + arg_logp[argument]
            entropy = entropy + _entropy_from_logp(arg_logp, mask)
        out["logp"][row] = logp
        out["entropy"][row] = entropy
    return out


# ---------------------------------------------------------------------------
# Reward / GAE (plan 14, 14.2 + intent shaping)
# ---------------------------------------------------------------------------


def econ_potential(material: Sequence[int]) -> float:
    z = sum(int(m) for m in material[0:9]) + ECON_POP_WEIGHT * int(material[9])
    return math.tanh(z / ECON_Z_SCALE)


def intent_potential(material: Sequence[int]) -> float:
    """Discovery + approach potential in [0, 1]: explored start-candidate
    fraction, enemy base known, army closeness to the remembered base."""
    explored, count, known, distance = (int(material[0]), int(material[1]),
                                        int(material[2]), int(material[3]))
    discovered = explored / count if count > 0 else 0.0
    closeness = 0.0
    if known and distance != 0xFFFFFFFF:
        closeness = 1.0 - min(distance / INTENT_DISTANCE_SCALE, 1.0)
    return 0.25 * discovered + 0.25 * float(bool(known)) + 0.5 * closeness


def step_reward(losses_prev: Sequence[int], losses_next: Sequence[int],
                phi_prev: Optional[float], phi_next: Optional[float], dt: float,
                terminal_payoff: float = 0.0, terminated: bool = False,
                intent_prev: Optional[float] = None,
                intent_next: Optional[float] = None) -> float:
    own_delta = (losses_next[0] - losses_prev[0]) + (losses_next[1] - losses_prev[1])
    hostile_delta = (losses_next[2] - losses_prev[2]) + \
        WAR_BUILDING_WEIGHT * (losses_next[3] - losses_prev[3])
    reward = WAR_SCALE * (hostile_delta - own_delta) / 1000.0
    gamma_dt = GAMMA_8 ** (dt / 8.0)
    if phi_prev is not None:
        next_phi = 0.0 if terminated else (phi_next if phi_next is not None else phi_prev)
        reward += ECON_BETA * (gamma_dt * next_phi - phi_prev)
    if intent_prev is not None:
        next_intent = 0.0 if terminated else (
            intent_next if intent_next is not None else intent_prev)
        reward += INTENT_BETA * (gamma_dt * next_intent - intent_prev)
    reward -= TIME_COST_PER_FRAME * dt
    return reward + terminal_payoff


def compute_gae(steps: Sequence[EntityStep], values: torch.Tensor,
                final_value: float) -> torch.Tensor:
    advantages = torch.zeros(len(steps))
    last = 0.0
    next_value = float(final_value)
    for t in reversed(range(len(steps))):
        step = steps[t]
        gamma_dt = GAMMA_8 ** (step.dt / 8.0)
        lambda_dt = LAMBDA_8 ** (step.dt / 8.0)
        nonterminal = 0.0 if step.terminal else 1.0
        delta = step.reward + gamma_dt * next_value * nonterminal - float(values[t])
        last = delta + gamma_dt * lambda_dt * nonterminal * last
        advantages[t] = last
        next_value = float(values[t])
    return advantages


# ---------------------------------------------------------------------------
# PPO (plan 14.1): role-balanced actor loss with assign + commander buckets.
# ---------------------------------------------------------------------------


def _prepare_episodes(episodes: Sequence[Dict]) -> Tuple[List[EntityStep], torch.Tensor,
                                                         torch.Tensor]:
    flat: List[EntityStep] = []
    advantages: List[torch.Tensor] = []
    returns: List[torch.Tensor] = []
    for episode in episodes:
        steps: List[EntityStep] = episode["steps"]
        if not steps:
            continue
        values = torch.tensor([s.behavior_value for s in steps], dtype=torch.float32)
        adv = compute_gae(steps, values, episode.get("final_value", 0.0))
        advantages.append(adv)
        returns.append(adv + values)
        flat.extend(steps)
    if not flat:
        return [], torch.zeros(0), torch.zeros(0)
    adv_all = torch.cat(advantages)
    ret_all = torch.cat(returns)
    norm = adv_all
    if adv_all.numel() > 1:
        norm = (adv_all - adv_all.mean()) / (adv_all.std() + 1e-8)
    return flat, norm, ret_all


def _clipped(ratio: torch.Tensor, adv: torch.Tensor, clip: float) -> torch.Tensor:
    return torch.min(ratio * adv, torch.clamp(ratio, 1 - clip, 1 + clip) * adv)


def ppo_update_batched_episodes(net: Entity2Net, optimizer, episodes: Sequence[Dict],
                                clip: float = 0.2, value_coef: float = 0.5,
                                entropy_coef: float = 0.003, minibatch: int = 32,
                                generator: Optional[torch.Generator] = None,
                                gate_prior: Optional[float] = None,
                                gate_kl_coef: float = 0.0, epochs: int = 1,
                                max_steps: int = 0) -> Dict:
    """One cohort update.  `max_steps` > 0 trains each epoch on a uniform
    random subset of that many steps (GAE is still computed over every
    step), bounding the wall-clock of a 60000-frame cohort."""
    flat, advantages, returns = _prepare_episodes(episodes)
    count = len(flat)
    stats = {"loss": 0.0, "actor": 0.0, "value": 0.0, "entropy": 0.0,
             "trained_steps": count, "invalid_records": 0, "epochs": epochs,
             "batches": 0, "sampled_steps": min(count, max_steps) if max_steps else count}
    if count == 0:
        return stats
    # Returns grow with the war stakes (a 60000-frame game loses tens of
    # thousands of unit value); the squared value error is measured in units
    # of the return spread so it never drowns the clipped actor term.
    return_scale = max(float(returns.std()), 1.0) if count > 1 else 1.0
    stats["return_scale"] = round(return_scale, 3)
    for _ in range(epochs):
        order = torch.randperm(count, generator=generator)
        if max_steps and count > max_steps:
            order = order[:max_steps]
        for start in range(0, count, minibatch):
            index = order[start:start + minibatch]
            actor_terms = []
            value_terms = []
            entropy_terms = []
            kl_terms = []
            for t in index.tolist():
                step = flat[t]
                res = teacher_forced_logp(net, step, step.command, step.argument,
                                          step.assign, step.slot_command, step.slot_cell)
                enc = res["enc"]
                value_terms.append(((enc["value"] - returns[t]) / return_scale) ** 2)
                if not res["ok"]:
                    stats["invalid_records"] += 1
                    continue
                adv = advantages[t]
                buckets = []
                bucket_entropy = []
                trainable = step.trainable
                if step.u > 0 and bool(trainable.any()):
                    ratio = torch.exp(torch.where(trainable, res["logp"] - step.old_logp,
                                                  torch.zeros_like(res["logp"])))
                    surrogate = _clipped(ratio, adv, clip)
                    for role in range(OWN_ROLE_VOCAB):
                        sel = trainable & (step.own_role == role)
                        if bool(sel.any()):
                            buckets.append(surrogate[sel].mean())
                            bucket_entropy.append(res["entropy"][sel].mean())
                a_train = step.assign_trainable
                if step.u > 0 and a_train is not None and bool(a_train.any()):
                    ratio = torch.exp(torch.where(a_train,
                                                  res["assign_logp"] - step.old_assign_logp,
                                                  torch.zeros_like(res["assign_logp"])))
                    surrogate = _clipped(ratio, adv, clip)
                    buckets.append(surrogate[a_train].mean())
                    bucket_entropy.append(res["assign_entropy"][a_train].mean())
                s_train = step.slot_trainable
                if s_train is not None and bool(s_train.any()):
                    ratio = torch.exp(torch.where(s_train,
                                                  res["slot_logp"] - step.old_slot_logp,
                                                  torch.zeros_like(res["slot_logp"])))
                    surrogate = _clipped(ratio, adv, clip)
                    buckets.append(surrogate[s_train].mean())
                    bucket_entropy.append(res["slot_entropy"][s_train].mean())
                if buckets:
                    actor_terms.append(torch.stack(buckets).mean())
                    entropy_terms.append(torch.stack(bucket_entropy).mean())
                if gate_prior is not None and step.u > 0 and bool(trainable.any()):
                    rows = torch.arange(step.u, dtype=torch.long)
                    heads = net.row_heads(enc, rows, torch.zeros(step.u, LEDGER_CONTEXT),
                                          torch.zeros(step.u, ROW_INTENT))
                    p = torch.sigmoid(heads["gate_logit"][trainable])
                    prior = min(max(gate_prior, 1e-4), 1 - 1e-4)
                    kl = p * torch.log(p / prior + 1e-9) + \
                        (1 - p) * torch.log((1 - p) / (1 - prior) + 1e-9)
                    kl_terms.append(kl.mean())
            if not value_terms:
                continue
            value_loss = torch.stack(value_terms).mean()
            actor_loss = -torch.stack(actor_terms).mean() if actor_terms else torch.zeros(())
            entropy = torch.stack(entropy_terms).mean() if entropy_terms else torch.zeros(())
            kl = torch.stack(kl_terms).mean() if kl_terms else torch.zeros(())
            loss = actor_loss + value_coef * value_loss - entropy_coef * entropy + \
                gate_kl_coef * kl
            optimizer.zero_grad()
            loss.backward()
            torch.nn.utils.clip_grad_norm_(net.parameters(), 1.0)
            optimizer.step()
            stats["loss"] += float(loss.detach())
            stats["actor"] += float(actor_loss.detach())
            stats["value"] += float(value_loss.detach())
            stats["entropy"] += float(entropy.detach())
            stats["batches"] += 1
    for key in ("loss", "actor", "value", "entropy"):
        if stats["batches"]:
            stats[key] /= stats["batches"]
    return stats


# ---------------------------------------------------------------------------
# SHD3 behaviour cloning (plan 15)
# ---------------------------------------------------------------------------


def bc_loss(net: Entity2Net, step: EntityStep,
            labels: Sequence[wire.ShadowLabel],
            slot_labels: Optional[Sequence[wire.ShadowSlotLabel]] = None,
            gate_weight: float = 1.0, slot_keep_weight: float = 1.0
            ) -> Optional[torch.Tensor]:
    """Weighted NLL of the teacher labels (rows, assigns, commander);
    EXCLUDED rows/labels are skipped and KEEP rows re-weighted by
    1/inclusion_probability.  `gate_weight` scales the issue/keep part of
    every row's log-probability (0 = clone only WHAT the teacher issues, not
    WHEN: the legacy teacher issues in ~2% of the legal ticks, and a cloned
    gate collapses to KEEP); `slot_keep_weight` likewise scales the
    commander's KEEP labels."""
    u = step.u
    commands = torch.zeros(u, dtype=torch.long)
    arguments = torch.full((u,), -1, dtype=torch.long)
    assigns = torch.zeros(u, dtype=torch.long)
    weights = torch.zeros(u)
    assign_weights = torch.zeros(u)
    for i, label in enumerate(labels):
        if label.label != wire.SHADOW_EXCLUDED:
            if label.label == wire.SHADOW_ISSUE:
                commands[i] = label.command
                arguments[i] = label.argument
            weights[i] = 1.0 / max(label.inclusion_probability, 1e-6)
        if label.assign_label != wire.SHADOW_EXCLUDED:
            if label.assign_label == wire.SHADOW_ISSUE:
                assigns[i] = label.assign
            assign_weights[i] = 1.0
    slot_command = torch.zeros(SLOT_COUNT, dtype=torch.long)
    slot_cell = torch.full((SLOT_COUNT,), -1, dtype=torch.long)
    slot_weights = torch.zeros(SLOT_COUNT)
    if slot_labels is not None:
        for s, label in enumerate(slot_labels):
            if label.label == wire.SHADOW_EXCLUDED:
                continue
            if label.label == wire.SHADOW_ISSUE:
                slot_command[s] = label.command
                slot_cell[s] = label.cell
                slot_weights[s] = 1.0
            else:
                slot_weights[s] = slot_keep_weight
    res = teacher_forced_logp(net, step, commands, arguments, assigns, slot_command,
                              slot_cell)
    if not res["ok"]:
        return None
    stochastic = step.dyn_command_mask[:, 1:].any(-1) if u else torch.zeros(0, dtype=torch.bool)
    weights = weights * stochastic.float()
    assign_weights = assign_weights * step.dyn_assign_mask.any(-1).float() if u else \
        assign_weights
    slot_weights = slot_weights * step.slot_command_mask[:, 1:].any(-1).float()
    # Row term: gate part scaled by gate_weight; the command/argument part
    # (nonzero for ISSUE rows only) always counts.  KEEP rows carry only the
    # gate part, so they drop out entirely at gate_weight 0.
    choice_logp = res["logp"] - res["gate_logp"]
    row_logp = gate_weight * res["gate_logp"] + choice_logp
    if gate_weight == 0.0:
        weights = weights * (commands != COMMAND_KEEP).float()
    total = float(weights.sum() + assign_weights.sum() + slot_weights.sum())
    if total <= 0.0:
        return None
    nll = -(row_logp * weights).sum() - (res["assign_logp"] * assign_weights).sum() - \
        (res["slot_logp"] * slot_weights).sum()
    return nll / total


def set_issue_prior(net: Entity2Net, issue_prior: float,
                    economy_issue_prior: Optional[float] = None) -> None:
    """Re-initialise the row gate to a flat prior P(issue) = issue_prior:
    zero weights (no context dependence yet) and the prior as bias.  PPO
    learns the timing from reward; the cloned heads keep what to issue."""
    prior = min(max(float(issue_prior), 1e-4), 1.0 - 1e-4)
    with torch.no_grad():
        net.gate_head.weight.zero_()
        net.gate_head.bias.fill_(math.log(prior / (1.0 - prior)))
        net.gate_role_bias.weight.zero_()
        if economy_issue_prior is not None:
            econ = min(max(float(economy_issue_prior), 1e-4), 1.0 - 1e-4)
            delta = math.log(econ / (1.0 - econ)) - math.log(prior / (1.0 - prior))
            for role in (wire.ROLE_WORKER, wire.ROLE_BUILDING):
                net.gate_role_bias.weight[role, 0] = delta


# ---------------------------------------------------------------------------
# Synthetic fixtures + selftest
# ---------------------------------------------------------------------------


def _synthetic_step(slots: bool = False) -> EntityStep:
    body, header = wire._slot_fixture_request() if slots else wire._fixture_request()
    payload = wire.pack_act_request(body)
    header.payload_bytes = len(payload)
    header.payload_crc32 = wire.crc32(payload)
    request = wire.parse_act_request(header, payload)
    # Primitive distribution/ledger tests intentionally exercise raw rows;
    # test_ranker_entity2_squads covers the production compact boundary.
    return step_from_request(request, header, grouped=False)


def attach_sample(step: EntityStep, sample: Dict) -> None:
    step.command = sample["command"]
    step.argument = sample["argument"]
    step.assign = sample["assign"]
    step.slot_command = sample["slot_command"]
    step.slot_cell = sample["slot_cell"]
    step.old_logp = sample["logp"]
    step.old_assign_logp = sample["assign_logp"]
    step.old_slot_logp = sample["slot_logp"]
    step.behavior_value = float(sample["value"])
    step.dyn_command_mask = sample["dyn_command_mask"]
    step.dyn_econ_mask = sample["dyn_econ_mask"]
    step.dyn_assign_mask = sample["dyn_assign_mask"]
    step.budget_before = sample["budget_before"]
    step.trainable = sample["stochastic"].clone()
    step.assign_trainable = sample["assign_stochastic"].clone()
    step.slot_trainable = sample["slot_stochastic"].clone()


def selftest() -> None:
    torch.manual_seed(0)
    net = Entity2Net(hidden=32)
    rng = torch.Generator().manual_seed(1)
    step = _synthetic_step()
    assert step.u == 2 and step.e == 1 and step.c == 5

    # 1. Economy fixture: sampled choices legal; recompute reproduces the
    #    sampled log-probs exactly; commander is forced KEEP (masks = KEEP).
    found_build = False
    for trial in range(200):
        sample = sample_actions(net, step, rng)
        assert not bool(sample["slot_stochastic"].any())
        for row in range(step.u):
            assert _choice_legal(step, row, sample["dyn_command_mask"][row],
                                 sample["dyn_econ_mask"][row],
                                 int(sample["command"][row]), int(sample["argument"][row]))
        s2 = _synthetic_step()
        attach_sample(s2, sample)
        res = teacher_forced_logp(net, s2, s2.command, s2.argument, s2.assign,
                                  s2.slot_command, s2.slot_cell)
        assert res["ok"], "stored ledger block did not replay"
        assert torch.allclose(res["logp"], sample["logp"], atol=1e-5)
        assert torch.isfinite(res["entropy"]).all()
        if int(sample["command"][0]) == wire.COMMAND_BUILD:
            found_build = True
            assert not bool(sample["dyn_command_mask"][1][wire.COMMAND_RESEARCH_UPGRADE])
            assert not bool(sample["dyn_command_mask"][1][wire.COMMAND_PRODUCE_UNIT])
            assert sample["budget_before"][1].tolist() == [100, 0, 3]
            assert not bool(sample["stochastic"][1]) and float(sample["logp"][1]) == 0.0
    assert found_build, "the worker never sampled BUILD in 200 trials"

    # 2. Slot fixture: commander samples legal (command, cell); assign
    #    ledger keeps SCOUT closed (full); recompute reproduces everything.
    sstep = _synthetic_step(slots=True)
    assert sstep.u == 4 and sstep.scout_free == 0
    seen_issue = False
    seen_assign = False
    for trial in range(200):
        sample = sample_actions(net, sstep, rng)
        for slot in range(SLOT_COUNT):
            c = int(sample["slot_command"][slot])
            cell = int(sample["slot_cell"][slot])
            assert c == 0 and cell == -1 or _slot_choice_legal(sstep, slot, c, cell)
            seen_issue = seen_issue or c != 0
        for row in range(sstep.u):
            assert _assign_legal(sample["dyn_assign_mask"][row], int(sample["assign"][row]))
            assert not (int(sample["assign"][row]) == wire.SLOT_SCOUT + 1)
            seen_assign = seen_assign or int(sample["assign"][row]) != 0
            # Disobedience: A (row 2) never gets a personal point command.
            if row == 2:
                assert int(sample["command"][row]) not in POINT_COMMANDS
        s3 = _synthetic_step(slots=True)
        attach_sample(s3, sample)
        res = teacher_forced_logp(net, s3, s3.command, s3.argument, s3.assign,
                                  s3.slot_command, s3.slot_cell)
        assert res["ok"]
        assert torch.allclose(res["logp"], sample["logp"], atol=1e-5)
        assert torch.allclose(res["assign_logp"], sample["assign_logp"], atol=1e-5)
        assert torch.allclose(res["slot_logp"], sample["slot_logp"], atol=1e-5)
    assert seen_issue and seen_assign
    # Tampered stored assign mask / slot choice -> invalid.
    sample = sample_actions(net, sstep, rng)
    s4 = _synthetic_step(slots=True)
    attach_sample(s4, sample)
    s4.dyn_assign_mask[2, wire.SLOT_SCOUT] = True
    assert not teacher_forced_logp(net, s4, s4.command, s4.argument, s4.assign,
                                   s4.slot_command, s4.slot_cell)["ok"]
    s5 = _synthetic_step(slots=True)
    attach_sample(s5, sample)
    s5.slot_command[1] = wire.SLOT_COMMAND_STOP    # RAID_A inactive: STOP illegal
    s5.slot_cell[1] = -1
    assert not teacher_forced_logp(net, s5, s5.command, s5.argument, s5.assign,
                                   s5.slot_command, s5.slot_cell)["ok"]

    # 3. U=0 / E=0 / C=0 finite inference and value.
    empty = _synthetic_step()
    for name in ("own_cat", "own_feat", "own_role", "active_cand_row", "command_mask",
                 "point_mask", "own_slot", "own_relation"):
        setattr(empty, name, getattr(empty, name)[:0])
    empty.target_cat = empty.target_cat[:0]
    empty.target_feat = empty.target_feat[:0]
    empty.cand_cat = empty.cand_cat[:0]
    empty.cand_feat = empty.cand_feat[:0]
    empty.candidates = []
    empty.econ_mask = torch.zeros(0, 0, dtype=torch.bool)
    empty.pair_mask = torch.zeros(0, 0, dtype=torch.bool)
    empty.assign_mask = torch.zeros(0, SLOT_COUNT, dtype=torch.bool)
    out = sample_actions(net, empty, rng)
    assert out["command"].shape[0] == 0 and torch.isfinite(out["value"])

    # 4. Candidate permutation equivariance of the build pointer.
    net.eval()
    base = _synthetic_step()
    enc = net.encode(base)
    zero_l = torch.zeros(1, LEDGER_CONTEXT)
    zero_i = torch.zeros(1, ROW_INTENT)
    heads = net.row_heads(enc, torch.tensor([0]), zero_l, zero_i)
    _, dyn_pair = StepLedger(base).dynamic_masks(0)
    logp_a, _ = _argument_logp(net, enc, base, 0, heads["ctx"][0], wire.COMMAND_BUILD,
                               dyn_pair)
    perm = _synthetic_step()
    order = [0, 2, 1, 3, 4]
    perm.cand_cat = perm.cand_cat[order]
    perm.cand_feat = perm.cand_feat[order]
    perm.candidates = [perm.candidates[i] for i in order]
    perm.econ_mask = perm.econ_mask[:, order]
    enc_p = net.encode(perm)
    heads_p = net.row_heads(enc_p, torch.tensor([0]), zero_l, zero_i)
    _, dyn_pair_p = StepLedger(perm).dynamic_masks(0)
    logp_b, _ = _argument_logp(net, enc_p, perm, 0, heads_p["ctx"][0], wire.COMMAND_BUILD,
                               dyn_pair_p)
    assert torch.allclose(logp_a[order], logp_b, atol=1e-5)
    net.train()

    # 5. Reward / potentials: constant material -> pure discount term; the
    #    intent potential is bounded and rises with discovery/closeness.
    material = [400, 0, 100, 0, 500, 0, 0, 0, 0, 10]
    phi = econ_potential(material)
    tick_cost = TIME_COST_PER_FRAME * 8.0
    r = step_reward([0, 0, 0, 0], [0, 0, 0, 0], phi, phi, 8.0)
    assert abs(r - (ECON_BETA * (GAMMA_8 - 1.0) * phi - tick_cost)) < 1e-9
    assert abs(step_reward([0, 0, 0, 0], [0, 0, 1000, 0], None, None, 8.0) -
               (WAR_SCALE - tick_cost)) < 1e-9
    assert abs(step_reward([0, 0, 0, 0], [0, 0, 0, 1000], None, None, 8.0) -
               (WAR_SCALE * WAR_BUILDING_WEIGHT - tick_cost)) < 1e-9
    assert TERMINAL_PAYOFF[wire.TERMINAL_WIN] == TERMINAL_WIN_PAYOFF and \
        abs(TIME_COST_PER_FRAME * 60000.0 - 3.75) < 1e-9
    assert intent_potential([0, 4, 0, 0xFFFFFFFF]) == 0.0
    assert abs(intent_potential([4, 4, 0, 0xFFFFFFFF]) - 0.25) < 1e-9
    assert abs(intent_potential([4, 4, 1, 0]) - 1.0) < 1e-9
    assert 0.0 <= intent_potential([2, 4, 1, 2048]) <= 1.0
    r2 = step_reward([0] * 4, [0] * 4, None, None, 8.0, intent_prev=0.2, intent_next=0.7)
    assert abs(r2 - (INTENT_BETA * (GAMMA_8 * 0.7 - 0.2) - tick_cost)) < 1e-9

    # 6. PPO update on a short episode with slot fixtures runs and keeps
    #    finite parameters; rejected rows are excluded.
    episode_steps = []
    for t in range(3):
        s = _synthetic_step(slots=True)
        sample = sample_actions(net, s, rng)
        attach_sample(s, sample)
        s.reward = 0.1 * t
        if t == 1:
            s.trainable[2] = False
            s.old_logp[2] = -1e6
        episode_steps.append(s)
    episode_steps[-1].terminal = True
    optimizer = torch.optim.Adam(net.parameters(), lr=1e-3)
    stats = ppo_update_batched_episodes(net, optimizer, [{"steps": episode_steps,
                                                           "final_value": 0.0}],
                                        generator=rng, epochs=2)
    assert math.isfinite(stats["loss"]) and stats["batches"] == 2 and \
        stats["invalid_records"] == 0
    for p in net.parameters():
        assert torch.isfinite(p).all()

    # 7. GAE boundary.
    vals = torch.tensor([0.5, 0.5, 0.5])
    adv = compute_gae(episode_steps, vals, 100.0)
    assert abs(float(adv[-1]) - (episode_steps[-1].reward - 0.5)) < 1e-6

    # 8. BC loss with row + assign + commander labels; the stored blocks are
    #    the teacher-forced replay of the labels (as the SHD3 writer stores),
    #    so a stored block from a different prefix is rejected.
    s = _synthetic_step(slots=True)
    labels = [wire.ShadowLabel(wire.SHADOW_KEEP, 0, 0, -1, 1.0) for _ in range(s.u)]
    labels[0] = wire.ShadowLabel(wire.SHADOW_ISSUE, wire.COMMAND_BUILD, 0, 1, 1.0)
    labels[3] = wire.ShadowLabel(wire.SHADOW_KEEP, 0, 0, -1, 1.0, wire.SHADOW_ISSUE,
                                 wire.SLOT_RAID_A + 1)
    slot_labels = [wire.ShadowSlotLabel(), wire.ShadowSlotLabel(
        wire.SHADOW_ISSUE, wire.SLOT_COMMAND_MOVE, 9), wire.ShadowSlotLabel(),
        wire.ShadowSlotLabel()]
    attach_teacher_blocks(s, torch.tensor([wire.COMMAND_BUILD, 0, 0, 0]),
                          torch.tensor([1, -1, -1, -1]), torch.tensor([0, 0, 0, 2]))
    assert s.budget_before[1].tolist() == [100, 0, 3]
    loss = bc_loss(net, s, labels, slot_labels)
    assert loss is not None and torch.isfinite(loss)
    loss.backward()
    wrong = _synthetic_step(slots=True)
    attach_teacher_blocks(wrong, torch.zeros(4, dtype=torch.long),
                          torch.full((4,), -1), torch.zeros(4, dtype=torch.long))
    assert bc_loss(net, wrong, labels, slot_labels) is None
    print("ranker_entity2_ppo: selftest passed")


def main() -> int:
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    if args.selftest:
        selftest()
        return 0
    parser.print_help()
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
