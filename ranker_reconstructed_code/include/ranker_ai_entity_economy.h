#pragma once

// ENTCMD02 / act3 direct economy + combat entity-command contract
// (plan: docs/AI_PLAY_ENTCMD02_DIRECT_ECONOMY_PLAN.md).
//
// A separate contract from ENTCMD01 (ranker_ai_entity_control.h): magic
// RAI3, protocol 3, 128-byte header, no macro fields, eleven external
// commands, typed economy candidates (R+B+P+Q), an owner-scoped
// autoregressive economy ledger and a v2 outcome vocabulary.  The EntityKey
// registry, point geometry, attack-pair predicate, target rows and the combat
// order latch of ENTCMD01 are reused unchanged; everything economy-specific
// lives here.  RAI2 frames, SHD1 records and ENTCMD01 checkpoints are hard
// rejects on both peers.
//
// All of this is AI-controller metadata: it never feeds engine state, the
// gameplay packets or the P2P checksum.

#include "ranker_ai_entity_control.h"
#include "ranker_ai_expansion.h"
#include "ranker_game_session_tables.h"

#include <array>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace ranker {

// ---------------------------------------------------------------------------
// Contract versions and counts (plan section 12).
// ---------------------------------------------------------------------------

constexpr u16 kAiEntity2ProtocolVersion = 3;
constexpr u16 kAiEntity2WireHeaderBytes = 128;
constexpr char kAiEntity2WireMagic[4] = {'R', 'A', 'I', '3'};
constexpr char kAiEntity2ContractId[8] = {
    'E', 'N', 'T', 'C', 'M', 'D', '0', '2'};
// Version tuple: observation 5, global feature 10 (inherited), entity feature
// 3, entity action 3, wire semantic vocabulary 3, point geometry 1, economy
// candidate 1, outcome 3.  Feature/action/outcome 3 = the team-intent slot
// extension (docs/AI_PLAY_INTENT_SLOT_DESIGN_EASY.md): slot block, start
// candidates, per-row slot fields, assign + commander reply, slot outcome.
constexpr u16 kAiEntity2FeatureVersion = 3;
// Action v4 (2026-09-04 user rule): the personal command head has no STOP;
// the wire vocabulary is AiEntity2PolicyCommand (10 entries).
constexpr u16 kAiEntity2ActionVersion = 5;
constexpr u16 kAiEntity2SemanticVocabularyVersion = 3;
constexpr u16 kAiEntity2CandidateVersion = 1;
constexpr u16 kAiEntity2OutcomeVersion = 3;

// Engine (internal) command vocabulary size, AiEntity2Command below.
constexpr u32 kAiEntity2CommandCount = 11;
// Policy (wire) command vocabulary size, AiEntity2PolicyCommand below: the
// command mask bits, reply commands, last-attempt commands and shadow labels
// all use this vocabulary; STOP exists only as an engine command (slot STOP
// fan-out, watchdog recovery) and as an observed semantic order.
constexpr u32 kAiEntity2PolicyCommandCount = 10;
constexpr u32 kAiEntity2CommandMaskBits = (1u << kAiEntity2CommandCount) - 1u;
constexpr u32 kAiEntity2CandidateFeatureCount = 8;
constexpr u32 kAiEntity2QueueSlotCount = 5;
// Each of R/B/P/Q is capped at the wire row limit; C = R+B+P+Q <= 8192.
constexpr u32 kAiEntity2CandidateSegmentLimit = kAiEntityWireRowLimit;
constexpr u32 kAiEntity2CandidateLimit = 4u * kAiEntityWireRowLimit;
constexpr u32 kAiEntity2OwnPrefixBytes = 207;
// 36 (economy appendix) + 6 (slot_id u8, slot_order_relation u8, assign_mask u32).
constexpr u32 kAiEntity2OwnAppendixBytes = 42;
constexpr u32 kAiEntity2QueueSlotBytes = 16;
constexpr u32 kAiEntity2TargetRowBytes = 76;
constexpr u32 kAiEntity2CandidateRowBytes = 64;
// 3336 (global, budget, losses, reward material) + 288 (intent prefix: 4 slot
// blocks x 36, 8 start candidates x 8, slot command mask 16, slot cell mask
// 32, intent reward material 32).
constexpr u32 kAiEntity2FixedPrefixBytes = 3624;

// ---------------------------------------------------------------------------
// Team-intent slots (docs/AI_PLAY_INTENT_SLOT_DESIGN_EASY.md).  Combat rows
// (MELEE/RANGED) belong to exactly one of four slots; the commander head
// gives every slot one persistent (command, 8x8 cell); C++ derives the
// per-member packets, re-guides stopped members, and keeps SCOUT at
// capacity 1 through a same-tick assign ledger mirrored in Python.
// ---------------------------------------------------------------------------

constexpr u32 kAiEntity2SlotCount = 4;
constexpr u8 kAiEntity2SlotMain = 0;
constexpr u8 kAiEntity2SlotRaidA = 1;
constexpr u8 kAiEntity2SlotRaidB = 2;
constexpr u8 kAiEntity2SlotScout = 3;
constexpr u8 kAiEntity2SlotNone = 0xffu;
constexpr u32 kAiEntity2ScoutCapacity = 1;
// A member cannot be moved to another slot again within this many frames of
// its last assignment (assign mask 0): bounds slot-membership churn.
constexpr u32 kAiEntity2AssignCooldownFrames = 240;
constexpr u32 kAiEntity2SlotCommandCount = 7;
constexpr u32 kAiEntity2StartCandidateCount = 8;
constexpr u32 kAiEntity2SlotBlockBytes = 36;
constexpr u32 kAiEntity2StartCandidateBytes = 8;
constexpr u32 kAiEntity2IntentPrefixBytes =
    kAiEntity2SlotCount * kAiEntity2SlotBlockBytes +
    kAiEntity2StartCandidateCount * kAiEntity2StartCandidateBytes + 16 + 32 + 32;

enum class AiEntity2SlotCommand : u8 {
    keep = 0,
    move = 1,
    attack_move = 2,
    patrol = 3,
    // Persistent hunt intent (action v5): members with no tracking order are
    // sent ATTACK_UNIT at the nearest visible neutral monster they may
    // attack, and re-targeted after a kill / lost target.  The personal
    // ATTACK_UNIT head may still pick the monster (personal orders precede).
    hunt_neutral = 4,
    hold = 5,
    stop = 6,         // one-shot: clears the slot order and stops members
    // (v5) CLEAR was removed: an order ends only through STOP or a new order.
};

// slot_order_relation vocabulary (per own row).
constexpr u8 kAiEntity2SlotRelationNone = 0;        // no slot order / no slot
constexpr u8 kAiEntity2SlotRelationMatch = 1;       // latch derived from the slot order
constexpr u8 kAiEntity2SlotRelationDiffers = 2;     // personal order active
constexpr u8 kAiEntity2SlotRelationJustAssigned = 3;// assigned, adopt pending

struct AiEntity2SlotOrder {
    bool active = false;
    AiEntity2SlotCommand command = AiEntity2SlotCommand::keep;
    i32 cell = -1;                  // 8x8 global cell = global point token
    u32 issued_frame = 0;
};

// Persistent per-owner slot state (controller metadata only).
struct AiEntity2SlotState {
    std::unordered_map<u64, u8> membership;      // packed EntityKey -> slot id
    std::unordered_map<u64, u32> assigned_frame; // packed EntityKey -> frame
    std::array<AiEntity2SlotOrder, kAiEntity2SlotCount> orders{};
};

// Wire slot block (plan EASY §2 ①): what the commander sees per slot.
struct AiEntity2SlotBlock {
    u32 member_count = 0;
    i32 centroid_x = -1;
    i32 centroid_y = -1;
    u8 command = 0;                 // AiEntity2SlotCommand (0 when inactive)
    u8 active = 0;
    u16 reserved = 0;
    i32 cell = -1;
    u32 age_frames = 0;
    u32 pursuing = 0;               // members with a matching live latch
    u32 terminal = 0;               // members whose latch ended (completed/interrupted/stalled)
    u32 differing = 0;              // members on a personal order
};

struct AiEntity2StartCandidate {
    i32 cell = -1;                  // -1 = absent (maps carry 2..8 candidates)
    u8 explored = 0;
    u8 is_own = 0;
};

// Same-tick assign ledger (mirrored in tools/ai/ranker_entity2_contract.py):
// the SCOUT bit closes for every later row once an earlier row of this tick
// chose SCOUT and the capacity is used; leaving SCOUT never reopens it within
// the tick.
struct AiEntity2AssignLedger {
    u32 scout_taken = 0;
};
u32 AiEntity2DynamicAssignMask(const AiEntity2AssignLedger& ledger,
    u32 base_assign_mask, u32 scout_free_at_snapshot);
// Applies one row's assign choice (1..4 -> slot 0..3) if legal.
void AiEntity2AssignLedgerApply(AiEntity2AssignLedger& ledger, u8 assign);
constexpr char kAiEntity2CandidateSchemaId[] = "entcand1";
constexpr u32 kAiEntity2TypeSentinel = 0xffffffffu;
constexpr u32 kAiEntity2ProductionQueueLimit = 4;
// A worker with an armed hostile mobile unit inside this radius is
// "threatened": retreat MOVE (local tokens) and close ATTACK_UNIT open, the
// economy commands close (action v4 role table).
constexpr u32 kAiEntity2WorkerThreatRadiusPx = 320;
constexpr u32 kAiEntity2MobileTypeLimit = 0x60u;

// ---------------------------------------------------------------------------
// External vocabularies (plan sections 1, 5, 6, 7, 11.4, 12.4).
// ---------------------------------------------------------------------------

// Engine command vocabulary (latches, semantics, derived orders).  Not the
// wire vocabulary: see AiEntity2PolicyCommand.
enum class AiEntity2Command : u8 {
    keep_current_order = 0,
    move = 1,
    attack_move = 2,
    patrol = 3,
    attack_unit = 4,
    hold_position = 5,
    stop = 6,
    harvest = 7,
    build = 8,
    produce_unit = 9,
    research_upgrade = 10,
};

// Policy (wire) command vocabulary, action v4: what the personal command
// head chooses.  No STOP — a normal transition is a direct purpose order
// (another HARVEST, BUILD, a retreat MOVE, ...), and stuck units are reset
// by the C++ watchdog, never by a policy action.
enum class AiEntity2PolicyCommand : u8 {
    keep = 0,
    move = 1,
    attack_move = 2,
    patrol = 3,
    attack_unit = 4,
    hold = 5,
    harvest = 6,
    build = 7,
    produce_unit = 8,
    research_upgrade = 9,
};

// Policy <-> engine command mapping.  keep_current_order for an out-of-range
// policy value; 0xff (kAiEntityLastAttemptNone) for an engine command with
// no policy equivalent (stop).
AiEntity2Command AiEntity2EngineCommandOf(u8 policy_command);
u8 AiEntity2PolicyCommandOf(AiEntity2Command command);

enum class AiEntity2Role : u8 {
    melee = 0,
    ranged = 1,
    worker = 2,
    building = 3,
    transport = 4,
    other = 5,
};

enum class AiEntity2WireSemanticOrder : u8 {
    none = 0,
    external_unknown = 1,
    move = 2,
    attack_move = 3,
    patrol = 4,
    attack_unit = 5,
    hold = 6,
    stop = 7,
    harvest = 8,
    build = 9,
    produce_unit = 10,
    research_upgrade = 11,
};

enum class AiEntity2AttemptResult : u16 {
    kept = 0,
    deduped = 1,
    published = 2,
    rejected_mask = 3,
    rejected_stale = 4,
    planner_failed = 5,
    encode_failed = 6,
    rejected_conflict = 7,
    transaction_aborted = 8,
    controller_failed = 9,
};

enum class AiEntity2RejectCode : u16 {
    none = 0,
    out_of_range = 1,
    masked = 2,
    stale_source = 3,
    stale_target = 4,
    stale_candidate = 5,
    ownership = 6,
    inactive = 7,
    visibility = 8,
    hostility = 9,
    capability = 10,
    render_class = 11,
    terrain = 12,
    point = 13,
    depleted = 14,
    placement = 15,
    prerequisite = 16,
    resource_conflict = 17,
    population_conflict = 18,
    queue_conflict = 19,
    site_conflict = 20,
    research_conflict = 21,
    candidate_kind = 22,
    planner = 23,
    encode = 24,
    transport_capacity = 25,
    handler_rejected = 26,
    internal_error = 27,
    slot_conflict = 28,       // assign: capacity used earlier this tick
    slot_command = 29,        // commander: command/cell outside the slot mask
};

// ENTCMD01 pair rejects map onto the v2 vocabulary through one function.
AiEntity2RejectCode AiEntity2RejectOfPair(AiEntityRejectCode code);

enum class AiEntity2CandidateKind : u8 {
    resource = 0,
    build_site = 1,
    produce_unit = 2,
    research_upgrade = 3,
};

constexpr u8 kAiEntity2CandidateFlagExplored = 1u << 0;
constexpr u8 kAiEntity2CandidateFlagVisible = 1u << 1;
constexpr u8 kAiEntity2CandidateFlagExpansionSite = 1u << 2;
constexpr u8 kAiEntity2CandidateFlagActiveOrReserved = 1u << 3;
constexpr u8 kAiEntity2CandidateFlagAnySourceAvailable = 1u << 4;
constexpr u8 kAiEntity2CandidateFlagRemembered = 1u << 5;

constexpr u32 kAiEntity2CapMove = 1u << 0;
constexpr u32 kAiEntity2CapAttack = 1u << 1;
constexpr u32 kAiEntity2CapPatrol = 1u << 2;
constexpr u32 kAiEntity2CapHold = 1u << 3;
constexpr u32 kAiEntity2CapHarvest = 1u << 4;
constexpr u32 kAiEntity2CapBuild = 1u << 5;
constexpr u32 kAiEntity2CapProduce = 1u << 6;
constexpr u32 kAiEntity2CapResearch = 1u << 7;

constexpr u32 kAiEntity2StateCompleted = 1u << 0;
constexpr u32 kAiEntity2StateUnderConstruction = 1u << 1;
constexpr u32 kAiEntity2StateCargoNonzero = 1u << 2;
constexpr u32 kAiEntity2StateQueueFull = 1u << 3;
constexpr u32 kAiEntity2StateActiveEconomyOrder = 1u << 4;
constexpr u32 kAiEntity2StateOutstandingReservation = 1u << 5;

constexpr u8 kAiEntity2QueueKindEmpty = 0;
constexpr u8 kAiEntity2QueueKindProduce = 1;
constexpr u8 kAiEntity2QueueKindResearch = 2;
constexpr u8 kAiEntity2QueueStatusEmpty = 0;
constexpr u8 kAiEntity2QueueStatusEngineActive = 1;
constexpr u8 kAiEntity2QueueStatusEngineDeferred = 2;
constexpr u8 kAiEntity2QueueStatusAwaitingApply = 3;
constexpr u16 kAiEntity2QueueOriginUnknown = 0xffffu;

// Engine command states the economy tracker interprets (ranker_unit_commands.h).
constexpr u32 kAiEntity2StateBuildPlacementStart = 0x23u;
constexpr u32 kAiEntity2StateBuildConstruction = 0x24u;
constexpr u32 kAiEntity2StateBuildPlacementApproach = 0x25u;
constexpr u32 kAiEntity2StateHarvestFirst = 0x28u;
constexpr u32 kAiEntity2StateHarvestLast = 0x2du;
constexpr u32 kAiEntity2StateResearchStart = 0x4du;
constexpr u32 kAiEntity2StateResearchTimer = 0x4eu;
constexpr u32 kAiEntity2StateProductionSpawnStart = 0x50u;
constexpr u32 kAiEntity2StateProductionSpawnCycle = 0x51u;
// Deferred-queue internal command states (handle_resource_deferred_command).
constexpr u32 kAiEntity2DeferredProduceState = 0x10u;
constexpr u32 kAiEntity2DeferredResearchState = 0x17u;
constexpr u32 kAiEntity2CarryFlag = 0x4u;

bool AiEntity2CommandIsPoint(AiEntity2Command command);
bool AiEntity2CommandIsEconomy(AiEntity2Command command);
AiEntity2Command AiEntity2CommandOfKind(AiEntity2CandidateKind kind);
bool AiEntity2KindOfCommand(AiEntity2Command command,
    AiEntity2CandidateKind* out_kind);

// Candidate identity (plan section 7): (kind, key) pairs.
u64 AiEntity2ResourceKey(u32 compact_tile_index);
u64 AiEntity2BuildKey(u32 building_type, u32 tile_x, u32 tile_y);
u64 AiEntity2ProduceKey(u32 unit_type);
u64 AiEntity2ResearchKey(u32 order_id, u32 next_level);

// ---------------------------------------------------------------------------
// Snapshot rows (plan sections 6, 7).
// ---------------------------------------------------------------------------

struct AiEntity2QueueSlot {
    u8 kind = kAiEntity2QueueKindEmpty;
    u8 status = kAiEntity2QueueStatusEmpty;
    u16 origin_channel = 0;
    u32 object_id = 0;
    u32 origin_sequence = 0;
    u32 queue_ordinal = 0;
};

struct AiEntity2OwnAppendix {
    u32 capability_bits = 0;
    u32 queued_production_type_id = kAiEntity2TypeSentinel;
    u32 production_variant = 0;
    u32 deferred_command_count = 0;
    u32 walking_build_type_id = kAiEntity2TypeSentinel;
    i32 active_economy_candidate_row = -1;
    u32 source_state_bits = 0;
    float cargo_ratio = 0.0f;
    float queue_fill_ratio = 0.0f;
    // Team-intent fields (feature v3).
    u8 slot_id = kAiEntity2SlotNone;
    u8 slot_order_relation = kAiEntity2SlotRelationNone;
    u32 assign_mask = 0;            // bit s = may be assigned to slot s
    std::array<AiEntity2QueueSlot, kAiEntity2QueueSlotCount> queue{};
};

struct AiEntity2Candidate {
    u64 key = 0;
    u8 kind = 0;              // AiEntity2CandidateKind
    u8 flags = 0;
    u16 object_id = 0;
    i32 x = 0;                // world px (RESOURCE tile centre / BUILD anchor)
    i32 y = 0;
    u32 raw0 = 0;
    u32 raw1 = 0;
    u32 raw2 = 0;
    std::array<float, kAiEntity2CandidateFeatureCount> feature{};

    u32 footprint_width() const { return raw2 & 0xffu; }
    u32 footprint_height() const { return (raw2 >> 8) & 0xffu; }
    u32 placement_class() const { return (raw2 >> 16) & 0xffu; }
};

// Half-open tile rectangle of a BUILD candidate's footprint.
struct AiEntity2TileRect {
    i32 x0 = 0;
    i32 y0 = 0;
    i32 x1 = 0;
    i32 y1 = 0;
};
AiEntity2TileRect AiEntity2FootprintRectOf(const AiEntity2Candidate& candidate);
bool AiEntity2RectsOverlap(const AiEntity2TileRect& a,
    const AiEntity2TileRect& b);

// ---------------------------------------------------------------------------
// Session catalog + live hooks the observation cannot carry.
// ---------------------------------------------------------------------------

struct AiEntity2UnitCatalogEntry {
    u32 primary_cost = 0;
    u32 secondary_cost = 0;
    u32 population_cost = 0;
    u32 footprint_width = 0;      // 0 => fall back to AiBuildingFootprintOf
    u32 footprint_height = 0;
    // Owner-level prerequisites (tech tree) satisfied; bank/population are
    // ledger business and must NOT be folded in here.
    bool prerequisites_ok = true;
};

struct AiEntity2ResearchCatalogEntry {
    u32 next_level = 1;           // current completed level + 1
    u32 max_level = 0;            // 0 => unknown: feature 0, pair closed
    u32 primary_cost = 0;         // cost of next_level
    u32 secondary_cost = 0;
    // Sum of the costs already paid for the completed levels (reward
    // material index 7).
    u32 invested_primary = 0;
    u32 invested_secondary = 0;
    bool prerequisites_ok = true;
};

struct AiEntity2CatalogHooks {
    void* ctx = nullptr;
    // Primary (buildable building types), alternate (producible unit types)
    // and completion (research orders) references per unit type.  Null =>
    // no economy candidates at all.
    const GameSessionUnitReferenceTables* unit_references = nullptr;
    // Catalog cost/footprint/prerequisites of a unit or building type for an
    // owner; false => unknown type (never a candidate).
    bool (*unit_catalog)(void* ctx, u32 owner, u32 type_id,
        AiEntity2UnitCatalogEntry* out) = nullptr;
    // Research order for an owner; false => undefined order.
    bool (*research_catalog)(void* ctx, u32 owner, u32 order_id,
        AiEntity2ResearchCatalogEntry* out) = nullptr;
};

struct AiEntity2QueueOriginView {
    u32 channel = kUnitCommandOriginInvalidChannel;
    u32 sequence = 0;
};

struct AiEntity2EconomyLiveInput {
    void* ctx = nullptr;
    // Compact tile index the worker currently targets for harvesting
    // (engine harvest_tile_index resolved to the observation layout); false
    // when it targets none.
    bool (*unit_harvest_tile)(void* ctx, u32 runtime_id,
        u32* out_compact_tile_index) = nullptr;
    // Origin sidecar of the source's engine queue in execution order:
    // slot 0 = active payload, 1.. = deferred entries.  Returns the number
    // of slots written (<= cap).  Null => every engine entry is
    // controller-origin unknown (0xffff).
    u32 (*unit_queue_origins)(void* ctx, u32 runtime_id,
        AiEntity2QueueOriginView* out, u32 cap) = nullptr;
};

// ---------------------------------------------------------------------------
// Persistent controller state (plan sections 10, 11).  Combat orders reuse
// AiEntityActiveOrder + AiEntityOrderTrackFrame; HARVEST/BUILD get a
// persistent economy order, PRODUCE/RESEARCH an enqueue event with
// component claims.
// ---------------------------------------------------------------------------

struct AiEntity2EconomyOrder {
    AiEntityKey source;
    u32 controller_owner = 0;
    u32 control_epoch = 0;
    AiEntity2Command command = AiEntity2Command::harvest;
    u8 candidate_kind = 0;
    u64 candidate_key = 0;
    u32 object_id = 0;              // resource compact tile index / building type
    i32 x = 0;                      // resource tile centre / exact anchor (px)
    i32 y = 0;
    u32 footprint_width = 0;        // BUILD
    u32 footprint_height = 0;
    u32 primary_cost = 0;           // BUILD cost claim (until bank debit)
    u32 secondary_cost = 0;
    bool cost_claimed = false;
    bool site_claimed = false;
    AiEntityKey spawned_building;   // BUILD: key of the spawned structure
    u32 issued_frame = 0;
    u32 applied_frame = 0;
    AiEntityPacketOrigin ordered_packet;
    u32 delivery_seen_frame = 0xffffffffu;
    u32 escape_frames = 0;          // consecutive AWAITING escape frames
    u32 idle_frames = 0;            // consecutive ACTIVE out-of-family frames
    AiEntityOrderStatus status = AiEntityOrderStatus::awaiting_apply;
};

enum class AiEntity2EventStatus : u8 {
    awaiting_apply = 0,
    engine_queued = 1,      // origin seen in the engine queue
    completed = 2,
    handler_rejected = 3,
};

struct AiEntity2EconomyEvent {
    AiEntityKey source;
    u32 controller_owner = 0;
    u32 control_epoch = 0;
    AiEntity2Command command = AiEntity2Command::produce_unit;
    u32 object_id = 0;              // unit type / research order id
    u32 level_at_issue = 0;         // RESEARCH: owner level when issued
    u32 primary_cost = 0;
    u32 secondary_cost = 0;
    u32 population_cost = 0;
    bool resource_claimed = false;
    bool population_claimed = false;
    bool queue_claimed = false;
    bool research_claimed = false;
    AiEntityPacketOrigin origin;
    u32 issued_frame = 0;
    u32 queue_ordinal = 0;          // reserved execution ordinal (1..4)
    u32 missing_frames = 0;
    bool origin_ever_seen = false;
    AiEntity2EventStatus status = AiEntity2EventStatus::awaiting_apply;
};

struct AiEntity2LastAttempt {
    u32 controller_owner = 0;
    u32 control_epoch = 0;
    u32 request_sequence = 0;
    u8 requested_command = 0;
    u32 attempt_frame = 0;
    AiEntity2AttemptResult result = AiEntity2AttemptResult::kept;
    AiEntity2RejectCode reject_code = AiEntity2RejectCode::none;
};

struct AiEntity2OrderStore {
    // Keyed by packed EntityKey.
    std::unordered_map<u64, AiEntityActiveOrder> combat;
    std::unordered_map<u64, AiEntity2EconomyOrder> economy;
    std::vector<AiEntity2EconomyEvent> events;
    std::unordered_map<u64, AiEntity2LastAttempt> attempts;
};

// ---------------------------------------------------------------------------
// Snapshot (plan sections 5-9).
// ---------------------------------------------------------------------------

struct AiEntity2Snapshot {
    u32 owner = 0;
    u32 frame = 0;
    std::vector<AiEntityOwnRow> own;            // role = AiEntity2Role
    std::vector<AiEntity2OwnAppendix> own_appendix;
    std::vector<AiEntityTargetRow> targets;
    std::vector<AiEntity2Candidate> candidates; // R, then B, P, Q
    u32 resource_rows = 0;
    u32 build_rows = 0;
    u32 produce_rows = 0;
    u32 research_rows = 0;
    // Row-major U x ceil(E/32) and U x ceil(C/32), LSB-first.
    std::vector<u32> attack_pair_mask;
    std::vector<u32> economy_pair_mask;
    u32 spendable_primary = 0;
    u32 spendable_secondary = 0;
    u32 spendable_population = 0;
    std::array<u64, 10> economy_reward_material{};
    // Team-intent prefix (feature v3).
    std::array<AiEntity2SlotBlock, kAiEntity2SlotCount> slots{};
    std::array<AiEntity2StartCandidate, kAiEntity2StartCandidateCount>
        start_candidates{};
    std::array<u32, kAiEntity2SlotCount> slot_command_mask{};
    std::array<std::array<u32, 2>, kAiEntity2SlotCount> slot_cell_mask{};
    // Reward-only intent material: explored start candidates, start
    // candidate count, enemy base known (0/1), army centroid -> nearest
    // remembered enemy building distance in px (0xffffffff = unknown).
    std::array<u64, 4> intent_reward_material{};
    u32 scout_free_at_snapshot = 0;   // SCOUT capacity left (base assign mask)
    bool contract_error = false;
    std::string error;

    u32 candidate_rows() const {
        return resource_rows + build_rows + produce_rows + research_rows;
    }
    u32 economy_words_per_row() const { return (candidate_rows() + 31u) / 32u; }
    u32 attack_words_per_row() const {
        return (static_cast<u32>(targets.size()) + 31u) / 32u;
    }
    bool attack_pair_bit(u32 row, u32 target) const;
    bool economy_pair_bit(u32 row, u32 candidate) const;
    // Row index of a (kind,key) candidate or -1.
    i32 candidate_row_of(u8 kind, u64 key) const;
};

struct AiEntity2SnapshotInput {
    const AiObservation* observation = nullptr;
    const AiEntityRegistry* registry = nullptr;
    const UnitMovementMap* movement_map = nullptr;
    AiEntitySnapshotLiveInput live{};
    AiEntity2CatalogHooks catalog{};
    AiEntity2EconomyLiveInput economy_live{};
    // Persistent controller state (null => no latch/event/reservation).
    const AiEntity2OrderStore* orders = nullptr;
    // Team-intent slot state (null => every combat row is a MAIN member with
    // no slot order; assign masks still open).
    const AiEntity2SlotState* slots = nullptr;
    AiExpansionConfig expansion{};
    // Extra exact BUILD sites appended to the candidate table (SHD2 teacher
    // taps: the teacher's own placement must be addressable).  Each entry
    // is (building_type, tile_x, tile_y); dedupes with the generated sites.
    struct ExtraBuildSite {
        u32 building_type = 0;
        u32 tile_x = 0;
        u32 tile_y = 0;
    };
    std::vector<ExtraBuildSite> extra_build_sites;
};

AiEntity2Snapshot BuildAiEntity2Snapshot(const AiEntity2SnapshotInput& input);

// Role of an observed controlled unit in the v2 vocabulary.
AiEntity2Role AiEntity2RoleOf(const AiObservedUnit& unit);
// Fixed switch (plan 11.4): internal kind -> wire order; unknown kinds encode
// as external_unknown only.
AiEntity2WireSemanticOrder AiEntity2WireSemanticOrderOf(
    AiSemanticActionKind kind);
AiEntity2WireSemanticOrder AiEntity2WireSemanticOrderOfCommand(
    AiEntity2Command command);

// ---------------------------------------------------------------------------
// Same-tick economy ledger (plan section 10).  Byte-for-byte mirror of
// tools/ai/ranker_entity2_contract.py EconomyLedger / replay_ledger.
// ---------------------------------------------------------------------------

struct AiEntity2Ledger {
    u32 remaining_primary = 0;
    u32 remaining_secondary = 0;
    u32 remaining_population = 0;
    std::vector<AiEntity2TileRect> reserved_sites;
    std::vector<u64> reserved_site_keys;
    std::vector<u32> reserved_research;
};

void AiEntity2LedgerInit(AiEntity2Ledger& ledger,
    const AiEntity2Snapshot& snapshot);
// Whether candidate `index` is still affordable/unclaimed under the ledger
// (base pair bit is the caller's business).
bool AiEntity2LedgerCandidateAvailable(const AiEntity2Ledger& ledger,
    const AiEntity2Snapshot& snapshot, u32 index);
// Dynamic command mask and economy pair words of one row.
void AiEntity2LedgerDynamicMasks(const AiEntity2Ledger& ledger,
    const AiEntity2Snapshot& snapshot, u32 row, u32* out_command_mask,
    std::vector<u32>& out_pair_words);
// Full legality of (command, argument) at a row under the given dynamic
// masks (mirror of command_legal).
bool AiEntity2ChoiceLegal(const AiEntity2Snapshot& snapshot, u32 row,
    u32 dynamic_command_mask, const std::vector<u32>& dynamic_pair_words,
    AiEntity2Command command, i32 argument);
void AiEntity2LedgerReserve(AiEntity2Ledger& ledger,
    const AiEntity2Snapshot& snapshot, AiEntity2Command command,
    i32 argument);
// The conflict code a base-legal but dynamically-illegal candidate earns.
AiEntity2RejectCode AiEntity2LedgerConflictOf(const AiEntity2Ledger& ledger,
    const AiEntity2Snapshot& snapshot, u32 index);

struct AiEntity2LedgerReplay {
    std::vector<u32> dynamic_command_mask;              // per row
    std::vector<std::array<u32, 3>> remaining_budget;   // per row
    std::vector<u32> dynamic_economy_pair_mask;         // row-major words
    // Rows whose choice was legal under their own dynamic mask.
    std::vector<u8> choice_legal;
    // Slot extension: per row the dynamic assign mask (SCOUT capacity ledger).
    std::vector<u32> dynamic_assign_mask;
    std::vector<u8> assign_legal;
};

constexpr u32 kAiEntity2NoUnresolvedRow = 0xffffffffu;

AiEntity2LedgerReplay AiEntity2ReplayLedger(const AiEntity2Snapshot& snapshot,
    const std::vector<u8>& command, const std::vector<i32>& argument,
    u32 unresolved_from = kAiEntity2NoUnresolvedRow,
    const std::vector<u8>* assign = nullptr);

// Slot command legality against the snapshot masks (commander side).
bool AiEntity2SlotChoiceLegal(const AiEntity2Snapshot& snapshot, u32 slot,
    u8 command, i32 cell);
// Which slot commands need a cell argument.
bool AiEntity2SlotCommandIsPoint(AiEntity2SlotCommand command);

// Per-member derivation decision (plan EASY §2 ⑤ / §4 (i)): whether a
// combat member of a slot with a live point order needs a derived packet
// this tick.  `arrived` = the member's tile lies inside the slot cell.
struct AiEntity2SlotMemberView {
    bool has_personal_issue = false;     // non-KEEP command this tick
    bool slot_changed = false;           // slot order (re)issued this tick
    bool just_assigned = false;          // joined the slot this tick
    bool latch_matches_slot = false;     // AWAITING/ACTIVE latch from this slot order
    bool latch_terminal = false;         // completed/interrupted/stalled/lost
    bool has_latch = false;
    bool arrived = false;
};
bool AiEntity2SlotMemberNeedsOrder(const AiEntity2SlotOrder& order,
    const AiEntity2SlotMemberView& member);

// A row is a stochastic (trainable) choice iff any non-KEEP command is legal
// under its dynamic mask.
bool AiEntity2RowStochastic(u32 dynamic_command_mask);

// ---------------------------------------------------------------------------
// RAI3 wire (plan section 12).
// ---------------------------------------------------------------------------

constexpr u16 kAiEntity2WireFlagTerminated = 1u << 0;
constexpr u16 kAiEntity2WireFlagTruncated = 1u << 1;

struct AiEntity2WireHeader {
    u16 kind = 0;                    // AiEntityWireKind
    u16 flags = 0;
    u32 payload_bytes = 0;
    u32 owner = 0;
    u32 episode = 0;
    u32 frame = 0;
    u32 sequence = 0;
    u32 reply_to_sequence = 0;
    u32 own_rows = 0;
    u32 target_rows = 0;
    u32 resource_rows = 0;
    u32 build_rows = 0;
    u32 produce_rows = 0;
    u32 research_rows = 0;
    u32 payload_crc32 = 0;
    u32 policy_version = 0;

    u32 candidate_rows() const {
        return resource_rows + build_rows + produce_rows + research_rows;
    }
};

void AiEntity2WriteWireHeader(const AiEntity2WireHeader& header,
    u8 (&out)[kAiEntity2WireHeaderBytes]);
bool AiEntity2ParseWireHeader(const u8* data, std::size_t length,
    AiEntity2WireHeader& out, std::string* error);

// Exact payload byte size (plan 12.2 + slot extension): 3624 + 329U + 76E +
// 64C + 4U(ceil(E/32)+ceil(C/32)) (+4 for TERMINAL).  0 on cap violation.
u64 AiEntity2ActRequestPayloadBytes(u32 own_rows, u32 target_rows,
    u32 candidate_rows, bool terminal);

struct AiEntity2ActRequestBody {
    std::array<float, kAiEntityGlobalFeatureCount> global{};
    std::array<u64, 4> cumulative_losses{};
    AiEntity2Snapshot snapshot;
};

std::vector<u8> EncodeAiEntity2ActRequestPayload(
    const AiEntity2ActRequestBody& body);
std::vector<u8> EncodeAiEntity2TerminalPayload(
    const AiEntity2ActRequestBody& body, u32 terminal_outcome);
// Strict inverse (tests / tooling): sizes, CRC-free; contract semantics are
// re-checked (candidate order, high bits, finite floats).
bool DecodeAiEntity2ActRequestPayload(const u8* data, std::size_t length,
    const AiEntity2WireHeader& header, bool terminal,
    AiEntity2ActRequestBody& out, u32* out_terminal_outcome,
    std::string* error);

struct AiEntity2ReplyBody {
    std::vector<u8> command;
    std::vector<i32> argument;
    // Slot extension (action v3): per row 0 = keep slot, 1..4 = move to slot
    // 0..3; per slot the commander's command (AiEntity2SlotCommand) and cell
    // (0..63 for point commands, -1 otherwise).
    std::vector<u8> assign;
    std::array<u8, kAiEntity2SlotCount> slot_command{};
    std::array<i32, kAiEntity2SlotCount> slot_cell{{-1, -1, -1, -1}};
};

// Hard argument domain per command (framing-level, plan 12.3).
bool AiEntity2ArgumentDomainOk(AiEntity2Command command, i32 argument,
    u32 target_rows, u32 candidate_rows);
std::vector<u8> EncodeAiEntity2ReplyPayload(const AiEntity2ReplyBody& body);
bool DecodeAiEntity2ReplyPayload(const u8* data, std::size_t length,
    const AiEntity2WireHeader& header, AiEntity2ReplyBody& out,
    std::string* error);

struct AiEntity2OutcomeBody {
    std::vector<u16> result;
    std::vector<u16> reject_code;
    std::vector<u32> trainable_mask;   // ceil(U/32) words
    // Slot extension (outcome v3): per-slot commander result and, per row,
    // whether the assign choice was applied and is trainable.
    std::array<u16, kAiEntity2SlotCount> slot_result{};
    std::array<u16, kAiEntity2SlotCount> slot_reject_code{};
    u32 slot_trainable_bits = 0;
    std::vector<u32> assign_trainable_mask;   // ceil(U/32) words
};

std::vector<u8> EncodeAiEntity2OutcomePayload(const AiEntity2OutcomeBody& body);
bool DecodeAiEntity2OutcomePayload(const u8* data, std::size_t length,
    u32 own_rows, AiEntity2OutcomeBody& out, std::string* error);

struct AiEntity2HelloOwnerRecord {
    u32 owner = 0;
    u32 frozen_hostile_owner_mask = 0;
    u32 requested_policy_version = 0xffffffffu;
    std::array<u8, 32> requested_checkpoint_sha256{};
};

struct AiEntity2HelloBody {
    u32 max_payload_bytes = kAiEntityWireMaxPayloadBytes;
    u32 reply_timeout_ms = 5000;
    u32 run_mode = 0;
    u32 controlled_owner_mask = 0;
    std::vector<AiEntity2HelloOwnerRecord> owners;
};

std::vector<u8> EncodeAiEntity2HelloPayload(const AiEntity2HelloBody& body);
bool DecodeAiEntity2HelloPayload(const u8* data, std::size_t length,
    AiEntity2HelloBody& out, std::string* error);

// ---------------------------------------------------------------------------
// Economy order / event tracking (plan section 11).
// ---------------------------------------------------------------------------

constexpr u32 kAiEntity2EventMissingFrames = 8;
constexpr u32 kAiEntity2EventAbsoluteFrames = 256;
constexpr u32 kAiEntity2EconomyIdleFrames = 4;

struct AiEntity2EconomyOrderFrameView {
    bool source_alive_active = false;
    bool control_epoch_matches = false;
    u32 command_base_state = 0;
    bool carrying = false;            // command_flags & 4
    // HARVEST: the latched resource tile's known amount is zero.
    bool resource_depleted = false;
    // BUILD: a controlled structure of the latched type stands on the anchor.
    bool spawned_present = false;
    bool spawned_completed = false;
    AiEntityKey spawned_key;
    // AWAITING_APPLY delivery state (origin sidecar), as for combat orders.
    bool delivery_origin_seen = false;
    bool consumer_passed_sequence = false;
    bool origin_replaced = false;
    bool acknowledged_matching = false;
};

// Returns true while the record stays alive; false => erase (source purge).
bool AiEntity2TrackEconomyOrderFrame(AiEntity2EconomyOrder& order,
    const AiEntity2EconomyOrderFrameView& view, u32 frame);

struct AiEntity2EventFrameView {
    bool source_alive_active = false;
    bool control_epoch_matches = false;
    bool origin_in_pending = false;
    bool origin_in_active = false;
    bool origin_in_deferred = false;
    bool consumer_passed_sequence = false;
    // PRODUCE: the engine's production_reserved flag is set for the source.
    bool population_reserved_by_engine = false;
    // RESEARCH: owner level of the order now.
    u32 owner_research_level = 0;
    // RESEARCH: the source's active research order equals the event's.
    bool research_active_matching = false;
};

// Returns true while the record stays alive; false => erase.
bool AiEntity2TrackEventFrame(AiEntity2EconomyEvent& event,
    const AiEntity2EventFrameView& view, u32 frame);

// Decision-row evaluation for economy commands (plan 11: HARVEST/BUILD
// dedupe against the persistent latch; PRODUCE never dedupes; RESEARCH
// dedupes against an awaiting event of the same order).
struct AiEntity2EconomyDecisionInput {
    AiEntity2Command command = AiEntity2Command::keep_current_order;
    u8 candidate_kind = 0;
    u64 candidate_key = 0;
    u32 object_id = 0;
};

struct AiEntity2DecisionRowOutcome {
    AiEntity2AttemptResult result = AiEntity2AttemptResult::kept;
    AiEntity2RejectCode reject = AiEntity2RejectCode::none;
    bool needs_packet = false;
};

AiEntity2DecisionRowOutcome AiEntity2EvaluateEconomyRow(
    const AiEntity2OrderStore& store, const AiEntityKey& source,
    const AiEntity2EconomyDecisionInput& input);

// ---------------------------------------------------------------------------
// SHD2 shadow teacher labels (plan section 15.1).
// ---------------------------------------------------------------------------

enum class AiEntity2ShadowExcludeReason : u16 {
    none = 0,
    source_missing = 1,
    target_missing = 2,
    candidate_missing = 3,
    stale = 4,
    multiple_desired = 5,
    return_cargo = 6,
    prefix_unresolved = 7,
    mask_mismatch = 8,
};

constexpr u8 kAiEntity2ShadowExcludedCommand = 255;
// 'SHD4': SHD3 layout with action-v4 (policy vocabulary) labels.
// 'SHD5': action-v5 slot vocabulary (HUNT_NEUTRAL = 4, no CLEAR).
constexpr char kAiEntity2ShadowRecordMagic[4] = {'S', 'H', 'D', '5'};

struct AiEntity2ShadowLabel {
    u8 label = kAiEntityShadowKeep;
    u8 command = 0;
    u16 exclude_reason = 0;
    i32 argument = -1;
    float inclusion_probability = 1.0f;
    // Slot extension: teacher slot move for this row (0 keep, 1..4 -> slot
    // 0..3) with its own label state.
    u8 assign_label = kAiEntityShadowKeep;
    u8 assign = 0;
};

// Commander (per-slot) teacher label.
struct AiEntity2ShadowSlotLabel {
    u8 label = kAiEntityShadowKeep;      // KEEP / ISSUE / EXCLUDED
    u8 command = 0;                       // AiEntity2SlotCommand when ISSUE
    i32 cell = -1;
};

// Teacher intent (from the legacy executor state) mapped onto the slots.
struct AiEntity2ShadowTeacherIntent {
    // Desired slot order per slot (active=false => no order / KEEP).
    std::array<AiEntity2SlotOrder, kAiEntity2SlotCount> desired{};
    // Desired slot per own unit id (absent => keep current slot).
    std::unordered_map<u32, u8> desired_slot;
};

struct AiEntity2ShadowDesiredOrder {
    u32 unit_id = 0;
    AiSemanticActionKind kind{};
    u32 target_id = 0;
    i32 x = 0;
    i32 y = 0;
    u32 production_id = 0;
};

struct AiEntity2ShadowLatch {
    bool valid = false;
    u8 command = 0;
    AiEntityKey target{};
    i32 x = 0;
    i32 y = 0;
    u8 candidate_kind = 0;
    u64 candidate_key = 0;
};

struct AiEntity2ShadowState {
    std::unordered_map<u64, AiEntity2ShadowLatch> latches;
    u32 next_tick_frame = 0xffffffffu;
    u32 sequence = 0;
};

// Labels every own row and replays the teacher prefix through the ledger
// (out_replay); unresolvable economy teacher events mark the rest of the
// economy rows PREFIX_UNRESOLVED.
std::vector<AiEntity2ShadowLabel> BuildAiEntity2ShadowLabels(
    const AiEntity2Snapshot& snapshot, const UnitMovementMap* movement_map,
    const std::vector<AiEntity2ShadowDesiredOrder>& desired,
    AiEntity2ShadowState& state, AiEntity2LedgerReplay& out_replay,
    u32 max_point_error_px = kAiEntityShadowPointMaxErrorPx,
    const AiEntity2ShadowTeacherIntent* intent = nullptr,
    std::array<AiEntity2ShadowSlotLabel, kAiEntity2SlotCount>* out_slot_labels =
        nullptr);

// 'SHD3' record: SHD2 body + dynamic assign masks + assign labels + slot labels.
std::vector<u8> EncodeAiEntity2ShadowRecord(const AiEntity2WireHeader& header,
    const std::vector<u8>& payload,
    const std::vector<AiEntity2ShadowLabel>& labels,
    const AiEntity2LedgerReplay& replay,
    const std::array<AiEntity2ShadowSlotLabel, kAiEntity2SlotCount>& slot_labels);

}  // namespace ranker
