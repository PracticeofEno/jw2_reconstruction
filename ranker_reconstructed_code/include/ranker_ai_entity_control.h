#pragma once

// Entity-command RL contract (plan: docs/AI_PLAY_ENTITY_COMMAND_RL_PLAN.md).
//
// Phase A scope: stable EntityKey activation registry with a direct-control
// epoch, deterministic own/target entity row encoding on top of the v5
// observation, command/point/attack-pair hard masks with the fixed
// point-geometry v1, and the versioned `act2` binary wire contract
// (header + ACT_REQ/ACT_REPLY/OUTCOME/TERMINAL bodies).
//
// The registry is AI-controller metadata only: it never feeds engine state,
// packets or the P2P checksum.  Generations/epochs exist to reject stale
// policy references, not to rename engine unit ids.

#include "ranker_ai_observation.h"
#include "ranker_types.h"
#include "ranker_unit_movement.h"

#include <array>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace ranker {

// ---------------------------------------------------------------------------
// Contract versions and counts (plan section 11/12).  A checkpoint or peer
// that disagrees with any of these is hard-rejected, never adapted to.
// ---------------------------------------------------------------------------

constexpr u16 kAiEntityProtocolVersion = 2;
constexpr u16 kAiEntityObservationSchemaVersion = 5;
constexpr u16 kAiEntityGlobalFeatureVersion = 10;
constexpr u16 kAiEntityFeatureVersion = 1;
constexpr u16 kAiEntityActionVersion = 1;
constexpr u16 kAiEntitySemanticActionVersion = 2;
constexpr u16 kAiEntityPointGeometryVersion = 1;
// 8-byte ASCII contract id carried by every wire frame.
constexpr char kAiEntityContractId[8] = {
    'E', 'N', 'T', 'C', 'M', 'D', '0', '1'};

constexpr u32 kAiEntityGlobalFeatureCount = 802;
constexpr u32 kAiEntityMacroActionCount = 80;
constexpr u32 kAiEntityCommandCount = 7;
constexpr u32 kAiEntityPointTokenCount = 96;
constexpr u32 kAiEntityPointGlobalTokenCount = 64;
constexpr u32 kAiEntityPointGridWidth = 8;
constexpr u32 kAiEntityPointMaskWords = 3;
constexpr u32 kAiEntityMacroMaskWords = 3;
constexpr u32 kAiEntityOwnContinuousCount = 33;
constexpr u32 kAiEntityTargetContinuousCount = 14;
// Hard wire safety limit for own and target rows.  Exceeding it is an
// explicit unsupported-contract failure, never a silent truncation.
constexpr u32 kAiEntityWireRowLimit = 2048;

// Categorical vocabulary bounds (out-of-range values keep the raw word on the
// wire and set the matching OOB presence/kind bit; the network embeds UNK).
constexpr u32 kAiEntityCommandBaseStateLimit = 138;   // 0..137 known
constexpr u32 kAiEntityRenderClassLimit = 32;         // >=32: permissive/OOB
constexpr u32 kAiEntityMovementClassLimit = 5;        // 0..4 known
constexpr u32 kAiEntityMovementStateLimit = 8;        // small engine vocab
constexpr u32 kAiEntityDistanceCheckModeLimit = 2;    // 0/1 known

// Feature normalization constants (one place; recorded in checkpoint
// metadata as normalization id below).  Plan section 6.2.
constexpr char kAiEntityNormalizationId[] = "entnorm1";
constexpr float kAiEntityNormActionModeScale = 8.0f;
constexpr float kAiEntityNormSpeedScale = 8.0f;         // step_limit/period
constexpr float kAiEntityNormPowerScale = 256.0f;       // attack/defense
constexpr float kAiEntityNormRecoveryScale = 256.0f;    // recovery ticks
constexpr float kAiEntityNormLockoutScale = 256.0f;     // lockout ticks
constexpr float kAiEntityNormEffectTimerScale = 256.0f;
constexpr float kAiEntityNormOrderAgeScale = 256.0f;
constexpr float kAiEntityNormIssueAgeScale = 64.0f;
constexpr float kAiEntityNormIdleScale = 4.0f;
constexpr float kAiEntityNormProgressAgeScale = 48.0f;
constexpr float kAiEntityNormLevelScale = 16.0f;
constexpr float kAiEntityNormExperienceScale = 1024.0f;

// ---------------------------------------------------------------------------
// External command / wire categorical vocabularies (plan sections 6.1, 8).
// ---------------------------------------------------------------------------

enum class AiEntityCommand : u8 {
    keep_current_order = 0,
    move = 1,
    attack_move = 2,
    patrol = 3,
    attack_unit = 4,
    hold_position = 5,
    stop = 6,
};

// Fixed wire intent category.  Never a raw AiSemanticActionKind cast: the
// internal->wire switch lives in one function and unknown kinds encode as
// external_unknown only.
enum class AiEntityWireSemanticOrder : u8 {
    none = 0,
    external_unknown = 1,
    move = 2,
    attack_move = 3,
    patrol = 4,
    attack_unit = 5,
    hold = 6,
    stop = 7,
};

enum class AiEntityOrderStatus : u8 {
    none = 0,
    awaiting_apply = 1,
    active = 2,
    completed = 3,
    target_lost = 4,
    stalled = 5,
    interrupted = 6,
};

constexpr u8 kAiEntityLastAttemptNone = 255;

// OUTCOME result enum (plan section 11.1).
enum class AiEntityAttemptResult : u16 {
    kept = 0,
    deduped = 1,
    published = 2,
    rejected_mask = 3,
    rejected_stale = 4,
    planner_failed = 5,
    encode_failed = 6,
    not_due = 7,
    transaction_aborted = 8,
};

enum class AiEntityRejectCode : u16 {
    none = 0,
    out_of_range = 1,
    masked = 2,
    stale_source = 3,
    stale_target = 4,
    ownership = 5,
    inactive = 6,
    visibility = 7,
    hostility = 8,
    capability = 9,
    render_class = 10,
    terrain = 11,
    point = 12,
    planner = 13,
    encode = 14,
    internal_error = 15,
    transport_capacity = 16,
};

// ---------------------------------------------------------------------------
// EntityKey + activation registry (plan section 5.3).
// ---------------------------------------------------------------------------

struct AiEntityKey {
    u32 runtime_id = 0;
    u32 activation_generation = 0;

    friend bool operator==(const AiEntityKey& a, const AiEntityKey& b) {
        return a.runtime_id == b.runtime_id &&
            a.activation_generation == b.activation_generation;
    }
    friend bool operator!=(const AiEntityKey& a, const AiEntityKey& b) {
        return !(a == b);
    }
    // Lexicographic (runtime_id, generation): the canonical row order.
    friend bool operator<(const AiEntityKey& a, const AiEntityKey& b) {
        if (a.runtime_id != b.runtime_id) {
            return a.runtime_id < b.runtime_id;
        }
        return a.activation_generation < b.activation_generation;
    }
};

// Ownership identity, tracked separately from the object identity above.
// Any change bumps the record's control epoch (generation stays).
struct AiEntityControlSignature {
    u32 owner_id = 0;
    bool direct_eligible = false;
    u32 type_id = 0;
    u8 role = 0;                 // live-unit role vocabulary (see cpp)
    u32 capability_bits = 0;     // type_flags bits 4/5/9, masked
    u32 movement_class = 0;

    friend bool operator==(const AiEntityControlSignature& a,
        const AiEntityControlSignature& b) {
        return a.owner_id == b.owner_id &&
            a.direct_eligible == b.direct_eligible &&
            a.type_id == b.type_id && a.role == b.role &&
            a.capability_bits == b.capability_bits &&
            a.movement_class == b.movement_class;
    }
    friend bool operator!=(const AiEntityControlSignature& a,
        const AiEntityControlSignature& b) {
        return !(a == b);
    }
};

struct AiEntityRegistryRecord {
    u32 runtime_id = 0;
    u32 generation = 0;      // valid generations start at 1
    u32 control_epoch = 0;   // reset to 1 on every new activation
    bool engine_active = false;
    bool has_signature = false;
    AiEntityControlSignature signature{};
};

// Optional observer for Phase B state purges (order latch / last attempt).
struct AiEntityRegistryEvents {
    void* ctx = nullptr;
    // Fired when an active record's direct-control signature changed.
    void (*on_control_epoch_changed)(void* ctx, const AiEntityKey& key,
        u32 new_epoch) = nullptr;
    // Fired when an active record leaves the active set.
    void (*on_deactivated)(void* ctx, const AiEntityKey& key) = nullptr;
};

struct AiEntityRegistry {
    // Fixed pool slots (runtime_slot_index < kAiEntityWireRowLimit).
    std::array<AiEntityRegistryRecord, kAiEntityWireRowLimit> fixed{};
    // Detached records (free-list-exhausted spawns; runtime_slot_index is
    // not a pool slot).  Pointer-keyed sparse registry; its iteration order
    // is never used for serialization.
    std::unordered_map<const void*, AiEntityRegistryRecord> detached;
    AiEntityRegistryEvents events{};
    // Contract-fatal diagnosis (duplicate active runtime id, u32 wrap).
    bool contract_fatal = false;
    std::string fatal_reason;
};

void AiEntityRegistryReset(AiEntityRegistry& registry);
// Commits one inactive->active transition (idempotent while the record stays
// active; nested helper calls of the same activation do not re-commit).
void AiEntityRegistryCommitActivation(AiEntityRegistry& registry,
    const UnitMovementUnit& unit);
void AiEntityRegistryMarkDeactivated(AiEntityRegistry& registry,
    const UnitMovementUnit& unit);
// Every-frame audit: commits activations missed by the hooks, detects
// signature changes (control epoch bump) and deactivations, and diagnoses
// duplicate active runtime ids as contract-fatal.  Runs before observation
// generation each frame.
void AiEntityRegistryAuditFrame(AiEntityRegistry& registry,
    const std::vector<UnitMovementUnit*>& active_units);

const AiEntityRegistryRecord* AiEntityRegistryFindByUnit(
    const AiEntityRegistry& registry, const UnitMovementUnit& unit);
// Snapshot-side lookup: observation rows carry (id, runtime_slot_index) but
// no pointer.  Detached records are scanned (small; active-first).
const AiEntityRegistryRecord* AiEntityRegistryFindByObserved(
    const AiEntityRegistry& registry, u32 runtime_id, u32 runtime_slot_index);

AiEntityControlSignature AiEntityControlSignatureOf(
    const UnitMovementUnit& unit);

// ---------------------------------------------------------------------------
// Point geometry v1 (plan section 7).
// ---------------------------------------------------------------------------

struct AiEntityPointResolveResult {
    bool valid = false;
    i32 x = 0;
    i32 y = 0;
};

// Static entry predicate used by the point mask.  Mirrors
// legacy_movement_class_can_enter_cell with allow_command_shortcut=false and
// the runtime building-footprint bit (visibility_flags & 0x20000000) cleared
// before judging; without legacy layers falls back to IsPassableTerrainCell,
// rejecting static kMapCellBlockedTerrain and ignoring the dynamic
// kMapCellReservedByUnit bit.
bool AiEntityStaticCellEnterable(const UnitMovementMap& map,
    u32 movement_class, u32 tile_x, u32 tile_y);

// Per-snapshot static reachability cache for one movement class.
struct AiEntityReachability {
    u32 movement_class = 0;
    u32 width = 0;
    u32 height = 0;
    // Component label per tile; 0 = statically non-enterable.
    std::vector<u32> component;
};

AiEntityReachability BuildAiEntityReachability(const UnitMovementMap& map,
    u32 movement_class);

// Builds the 96-bit point mask for a unit standing at world (unit_x,unit_y).
// The source tile is a virtual flood seed even when static-invalid.
void BuildAiEntityPointMask(const UnitMovementMap& map,
    const AiEntityReachability& reach, i32 unit_x, i32 unit_y,
    std::array<u32, kAiEntityPointMaskWords>& mask_out);

// Deterministic token -> world point resolution (geometry version 1).
// Invalid tokens or masked-out points return valid=false.
AiEntityPointResolveResult ResolveAiEntityPointToken(const UnitMovementMap& map,
    const AiEntityReachability& reach, i32 unit_x, i32 unit_y, u32 token);

// ---------------------------------------------------------------------------
// Authoritative attack pair predicate (plan section 8).  Mask generation and
// the live receiver both call this one function.
// ---------------------------------------------------------------------------

// Engine target runtime flags the predicate tests (mirrored from
// ranker_unit_action.h so consumers do not have to pull that include chain;
// static_assert-checked against the engine constants in the cpp).
constexpr u32 kAiEntityTargetFlagTransient = 0x00000004;
constexpr u32 kAiEntityTargetFlagInactive = 0x00000080;
constexpr u32 kAiEntityTargetFlagClassBlocked = 0x20000000;

struct AiEntityPairSource {
    u32 runtime_id = 0;
    bool active_owned_alive = false;
    bool has_attack_capability = false;   // type_flags bit 5
    u32 distance_check_mode = 0;
    u32 attackable_class_mask = 0xffffffffu;
    u32 render_class2_terrain_gate = 1;   // nonzero skips the terrain gate
};

struct AiEntityPairTarget {
    u32 runtime_id = 0;
    bool active_alive = false;
    bool visible = false;
    bool non_friendly = false;
    u32 runtime_flags = 0;
    u32 render_class = 0;
    i32 x = 0;
    i32 y = 0;
};

// Live hooks the predicate needs beyond the snapshot rows.  In-game these
// wire to the engine (unit lookup + CheckUnitCanEnterTerrainCell); tests
// supply fixtures.
struct AiEntityLiveHooks {
    void* ctx = nullptr;
    // Live terrain entry check for the class-2 gate; called with the
    // target's 32px-aligned coordinates.  Null => gate passes.
    bool (*source_can_enter_cell)(void* ctx, u32 source_runtime_id,
        i32 x, i32 y) = nullptr;
};

struct AiEntityPairDecision {
    bool legal = false;
    AiEntityRejectCode reject = AiEntityRejectCode::none;
};

AiEntityPairDecision AiEntityEvaluateAttackPair(
    const AiEntityPairSource& source, const AiEntityPairTarget& target,
    const AiEntityLiveHooks& live);

// ---------------------------------------------------------------------------
// Entity rows + snapshot (plan sections 5, 6).
// ---------------------------------------------------------------------------

// Presence bit layout (plan section 11.1).
constexpr u8 kAiEntityPresenceDestination = 1u << 0;
constexpr u8 kAiEntityPresencePathTarget = 1u << 1;
constexpr u8 kAiEntityPresenceEngineTarget = 1u << 2;
constexpr u8 kAiEntityPresenceSemanticPoint = 1u << 3;
constexpr u8 kAiEntityPresenceRenderClassOob = 1u << 4;
constexpr u8 kAiEntityPresenceCommandBaseOob = 1u << 5;
constexpr u8 kAiEntityPresenceMovementStateOob = 1u << 6;
constexpr u8 kAiEntityPresenceMovementClassOob = 1u << 7;

// target_kind_bits layout.
constexpr u32 kAiEntityTargetKindMobile = 1u << 0;
constexpr u32 kAiEntityTargetKindBuilding = 1u << 1;
constexpr u32 kAiEntityTargetKindNeutral = 1u << 2;
constexpr u32 kAiEntityTargetKindRenderClassOob = 1u << 3;

// engine_order_match vocabulary.
constexpr u8 kAiEntityEngineOrderNoRecord = 0;
constexpr u8 kAiEntityEngineOrderMatch = 1;
constexpr u8 kAiEntityEngineOrderCleared = 2;
constexpr u8 kAiEntityEngineOrderDifferent = 3;

struct AiEntityOwnRow {
    AiEntityKey key;
    u32 control_epoch = 0;
    // Build-time world position (point-token resolution for receiver/shadow
    // use; the wire carries only the normalized features).
    i32 x = 0;
    i32 y = 0;
    u16 type_id = 0;
    u32 movement_class = 0;
    u32 distance_check_mode = 0;
    u8 role = 0;                 // 0 melee, 1 ranged
    u32 render_class = 0;
    u32 command_base_state = 0;
    u32 command_state_high_flags = 0;
    u32 unit_command_flags = 0;
    u32 movement_state = 0;
    u8 semantic_order = 0;       // AiEntityWireSemanticOrder
    u8 order_status = 0;         // AiEntityOrderStatus
    u8 presence_bits = 0;
    u8 engine_order_match = kAiEntityEngineOrderNoRecord;
    u8 last_attempt_command = kAiEntityLastAttemptNone;
    u8 last_attempt_result = kAiEntityLastAttemptNone;
    u16 last_reject_code = 0;
    i32 active_target_row = -1;
    u32 attackable_class_mask = 0xffffffffu;
    std::array<float, kAiEntityOwnContinuousCount> feature{};
    u32 command_mask = 0;        // low 7 bits
    std::array<u32, kAiEntityPointMaskWords> point_mask{};
};

struct AiEntityTargetRow {
    AiEntityKey key;
    u16 type_id = 0;
    u8 owner_id = 0;
    u8 role = 0;                 // 0 melee, 1 ranged, 2 noncombat
    u32 render_class = 0;
    u32 kind_bits = 0;
    std::array<float, kAiEntityTargetContinuousCount> feature{};
    // Build-time world position (used by the pair predicate; not serialized
    // beyond the normalized features).
    i32 x = 0;
    i32 y = 0;
};

struct AiEntitySnapshot {
    u32 owner = 0;
    u32 frame = 0;
    std::vector<AiEntityOwnRow> own;
    std::vector<AiEntityTargetRow> targets;
    // Row-major U x ceil(E/32) bitset; LSB-first target index mapping.
    std::vector<u32> attack_pair_mask;
    bool contract_error = false;
    std::string error;
};

// Per-source live pair inputs the observation cannot carry (runtime flags of
// targets, source class-2 profile gate).  Null callbacks degrade to the
// documented defaults (flags 0 / gate 1) — tests exercise both sides.
struct AiEntitySnapshotLiveInput {
    void* ctx = nullptr;
    // Runtime flags of a (target) unit; return false if unknown.
    bool (*unit_runtime_flags)(void* ctx, u32 runtime_id,
        u32* out_flags) = nullptr;
    // render_class2_terrain_gate of the source's selected damage profile.
    bool (*source_class2_gate)(void* ctx, u32 runtime_id,
        u32* out_gate) = nullptr;
    AiEntityLiveHooks pair_hooks{};
};

struct AiEntitySnapshotInput {
    const AiObservation* observation = nullptr;
    const AiEntityRegistry* registry = nullptr;
    // Live movement map for point masks; the observation tiles cannot
    // reproduce class 2/4 entry rules (plan section 6).
    const UnitMovementMap* movement_map = nullptr;
    AiEntitySnapshotLiveInput live{};
};

AiEntitySnapshot BuildAiEntitySnapshot(const AiEntitySnapshotInput& input);

// ---------------------------------------------------------------------------
// act2 binary wire contract (plan section 11.1).
// ---------------------------------------------------------------------------

constexpr u16 kAiEntityWireHeaderBytes = 96;
constexpr u32 kAiEntityWireMaxPayloadBytes = 16u * 1024u * 1024u;
constexpr char kAiEntityWireMagic[4] = {'R', 'A', 'I', '2'};

enum class AiEntityWireKind : u16 {
    hello = 1,
    ack = 2,
    act_req = 3,
    act_reply = 4,
    outcome = 5,
    terminal = 6,
    error = 7,
};

constexpr u16 kAiEntityWireFlagMacroDue = 1u << 0;
constexpr u16 kAiEntityWireFlagTerminated = 1u << 1;
constexpr u16 kAiEntityWireFlagTruncated = 1u << 2;

struct AiEntityWireHeader {
    u16 kind = 0;
    u16 flags = 0;
    u32 payload_bytes = 0;
    u32 owner = 0;
    u32 episode = 0;
    u32 frame = 0;
    u32 sequence = 0;
    u32 reply_to_sequence = 0;
    u32 own_rows = 0;
    u32 target_rows = 0;
    u32 global_count = kAiEntityGlobalFeatureCount;
    u32 macro_action_count = kAiEntityMacroActionCount;
    u32 command_count = kAiEntityCommandCount;
    u32 point_count = kAiEntityPointTokenCount;
    u32 payload_crc32 = 0;
    u32 entity_policy_version = 0;
    u32 macro_policy_version = 0;
};

// IEEE reflected CRC32 (poly 0xedb88320, init/xor-out 0xffffffff).
u32 AiEntityCrc32(const u8* data, std::size_t length);

void AiEntityWriteWireHeader(const AiEntityWireHeader& header,
    u8 (&out)[kAiEntityWireHeaderBytes]);
// Strict parse: magic/size/protocol/contract/versions/reserved are all
// hard-fail (framing error -> connection close per plan 11.2).
bool AiEntityParseWireHeader(const u8* data, std::size_t length,
    AiEntityWireHeader& out, std::string* error);

// Full ACT_REQ (and TERMINAL, which prepends u32 terminal_outcome) body.
struct AiEntityActRequestBody {
    std::array<float, kAiEntityGlobalFeatureCount> global{};
    // elapsed/64, deadline_remaining/64 (already normalized).
    std::array<float, 2> macro_gate{};
    std::array<u32, kAiEntityMacroMaskWords> macro_mask{};
    // own unit, own building, hostile unit, hostile building (frozen mask).
    std::array<u64, 4> cumulative_losses{};
    AiEntitySnapshot snapshot;
};

// Exact payload byte size: 3260 + 207*U + 76*E + 4*U*ceil(E/32)
// (+4 for the TERMINAL outcome prefix).  Overflow-checked; returns 0 on
// contract violation (row limits).
u64 AiEntityActRequestPayloadBytes(u32 own_rows, u32 target_rows,
    bool terminal);

std::vector<u8> EncodeAiEntityActRequestPayload(
    const AiEntityActRequestBody& body);
std::vector<u8> EncodeAiEntityTerminalPayload(
    const AiEntityActRequestBody& body, u32 terminal_outcome);

struct AiEntityReplyBody {
    i32 macro = 0;
    i32 macro_target = -1;
    std::vector<u8> command;   // per own row, external index 0..6
    std::vector<i32> point;    // -1 unless point command
    std::vector<i32> target;   // -1 unless attack_unit
};

std::vector<u8> EncodeAiEntityReplyPayload(const AiEntityReplyBody& body);
// Shape/range validation only (enum ranges, -1 rules are receiver checks).
bool DecodeAiEntityReplyPayload(const u8* data, std::size_t length,
    u32 own_rows, AiEntityReplyBody& out, std::string* error);

struct AiEntityOutcomeBody {
    u16 macro_result = static_cast<u16>(AiEntityAttemptResult::not_due);
    u16 macro_reject_code = 0;
    u8 macro_trainable = 0;
    std::vector<u16> entity_result;
    std::vector<u16> entity_reject_code;
    std::vector<u32> trainable_mask;   // ceil(U/32) words, LSB-first
};

std::vector<u8> EncodeAiEntityOutcomePayload(const AiEntityOutcomeBody& body);
bool DecodeAiEntityOutcomePayload(const u8* data, std::size_t length,
    u32 own_rows, AiEntityOutcomeBody& out, std::string* error);

// HELLO owner record (48 bytes each, plan section 11.1).
struct AiEntityHelloOwnerRecord {
    u32 owner = 0;
    u32 frozen_hostile_owner_mask = 0;
    u32 requested_entity_version = 0xffffffffu;
    u32 requested_macro_version = 0xffffffffu;
    std::array<u8, 32> requested_checkpoint_sha256{};
};

struct AiEntityHelloBody {
    u32 max_payload_bytes = kAiEntityWireMaxPayloadBytes;
    u32 reply_timeout_ms = 5000;
    u32 run_mode = 0;   // evaluation=0 / training=1
    u32 controlled_owner_mask = 0;
    std::vector<AiEntityHelloOwnerRecord> owners;   // owner id ascending
};

std::vector<u8> EncodeAiEntityHelloPayload(const AiEntityHelloBody& body);
bool DecodeAiEntityHelloPayload(const u8* data, std::size_t length,
    AiEntityHelloBody& out, std::string* error);

std::vector<u8> EncodeAiEntityErrorPayload(u16 code,
    const std::string& message);

// ---------------------------------------------------------------------------
// Wire semantic-order translation (one function; plan section 6.1).
// ---------------------------------------------------------------------------

enum class AiSemanticActionKind : u32;   // ranker_ai_actions.h
AiEntityWireSemanticOrder AiEntityWireSemanticOrderOf(
    AiSemanticActionKind kind);

// ---------------------------------------------------------------------------
// Direct order latch (plan section 9).  Two records per entity: the active
// intent (AWAITING_APPLY -> ACTIVE -> terminal states) and the last attempt
// feedback — overwriting one status could not express "old attack kept + new
// MOVE rejected".  All of this is AI-controller metadata; it never feeds the
// engine command payloads or the P2P checksum.
// ---------------------------------------------------------------------------

constexpr u32 kAiEntityOriginInvalidChannel = 0xffffffffu;
// Delivery-based INTERRUPTED window and the absolute apply timeout.
constexpr u32 kAiEntityAwaitDeliveryFrames = 8;
constexpr u32 kAiEntityAwaitAbsoluteFrames = 256;
constexpr u32 kAiEntityIdleInterruptFrames = 4;
constexpr u32 kAiEntityStallFrames = 48;
constexpr u32 kAiEntityProgressEpsilonPx = 8;
constexpr u32 kAiEntityCompletionRadiusPx = 32;

// Ordered-packet provenance of an engine command payload (the AI-only
// sidecar).  INVALID means "not from an ordered packet this controller sent"
// — a same-content ACK with an INVALID/missing origin never activates.
struct AiEntityPacketOrigin {
    u32 channel = kAiEntityOriginInvalidChannel;
    u32 sequence = 0;

    bool valid() const { return channel != kAiEntityOriginInvalidChannel; }
    friend bool operator==(const AiEntityPacketOrigin& a,
        const AiEntityPacketOrigin& b) {
        return a.channel == b.channel && a.sequence == b.sequence;
    }
    friend bool operator!=(const AiEntityPacketOrigin& a,
        const AiEntityPacketOrigin& b) {
        return !(a == b);
    }
};

struct AiEntityActiveOrder {
    AiEntityKey source;
    u32 controller_owner = 0;
    u32 control_epoch = 0;
    u8 command = 0;                 // external command index 1..6
    AiEntityKey target;             // ATTACK_UNIT only (invalid key otherwise)
    i32 target_x = 0;               // resolved absolute point (point commands)
    i32 target_y = 0;
    u32 issued_frame = 0;           // ordered-packet accept success frame
    u32 applied_frame = 0;          // matching engine pending-command ACK frame
    AiEntityPacketOrigin ordered_packet;
    u32 delivery_seen_frame = 0xffffffffu;
    u32 last_issue_frame = 0;
    u32 idle_candidate_frames = 0;
    i32 last_progress_x = 0;
    i32 last_progress_y = 0;
    u32 last_progress_frame = 0;
    AiEntityOrderStatus status = AiEntityOrderStatus::awaiting_apply;
};

struct AiEntityLastAttempt {
    u32 controller_owner = 0;
    u32 control_epoch = 0;
    u32 request_sequence = 0;
    u8 requested_command = 0;       // external 0..6
    u32 attempt_frame = 0;
    AiEntityAttemptResult result = AiEntityAttemptResult::kept;
    AiEntityRejectCode reject_code = AiEntityRejectCode::none;
};

struct AiEntityOrderStore {
    // Keyed by packed EntityKey (runtime_id << 32 | generation).
    std::unordered_map<u64, AiEntityActiveOrder> orders;
    std::unordered_map<u64, AiEntityLastAttempt> attempts;
};

u64 AiEntityPackKey(const AiEntityKey& key);

// Per-simulation-frame tracker inputs for one order's source unit, filled by
// the live integration (or a test fixture).  The tracker itself never touches
// engine state.
struct AiEntityOrderFrameView {
    // Source validity gates (outer): false => purge / epoch purge.
    bool source_alive_active = false;
    bool control_epoch_matches = false;
    // ATTACK_UNIT target validity (generation/visibility/hostility/class).
    bool target_valid = false;
    i32 unit_x = 0;
    i32 unit_y = 0;
    u32 command_base_state = 0;     // masked base state
    bool idle = false;              // base state in {0,1}
    u32 attack_recovery = 0;        // current recovery counter
    // Canonical active-payload comparison (§9): NO_RECORD only before any
    // engine payload was ever observed for this slot.
    u8 engine_order_match = kAiEntityEngineOrderNoRecord;
    // ATTACK_UNIT: engine target key equals the semantic target.
    bool engine_target_matches = false;
    // ATTACK_MOVE contact exception: ValidateUnitActionTarget(current engine
    // target).valid && .in_range.
    bool engine_target_valid_in_range = false;
    // ATTACK_UNIT stall gate: semantic target valid but actually out of reach.
    bool target_out_of_reach = false;
    // AWAITING_APPLY delivery state (from the origin sidecar):
    bool delivery_origin_seen = false;     // exact origin consumed, matching pending
    bool consumer_passed_sequence = false; // reliable consumer at/past sequence
    bool origin_replaced = false;          // pending/active origin became a different one
    bool acknowledged_matching = false;    // ACK fired: payload+origin exact match
};

// Runs the §9 state machine for one simulation frame.  Returns true while
// the record stays alive; false means the caller must erase it (source
// purge).  TARGET_LOST and the other terminal states keep the record for the
// next observation.
bool AiEntityOrderTrackFrame(AiEntityActiveOrder& order,
    const AiEntityOrderFrameView& view, u32 frame);

// Decision-row evaluation (plan §9 KEEP / same-ISSUE rules): what a sampled
// action means against the current latch.  `needs_packet` is true only for a
// genuinely new ISSUE.
struct AiEntityDecisionRowOutcome {
    AiEntityAttemptResult result = AiEntityAttemptResult::kept;
    AiEntityRejectCode reject = AiEntityRejectCode::none;
    bool needs_packet = false;
};

struct AiEntityDecisionRowInput {
    u8 command = 0;                 // external 0..6
    AiEntityKey target;             // ATTACK_UNIT
    i32 point_x = 0;                // resolved absolute point (point commands)
    i32 point_y = 0;
    i32 unit_x = 0;
    i32 unit_y = 0;
    bool unit_idle = false;
};

AiEntityDecisionRowOutcome AiEntityEvaluateDecisionRow(
    const AiEntityActiveOrder* order, const AiEntityDecisionRowInput& input);

// ---------------------------------------------------------------------------
// Shadow teacher labels (plan sections 13.1 / 16.A.5).  The old micro stays
// the live controller; at the entity cadence its pre-dedupe desired orders
// become per-unit (KEEP / ISSUE command+point/target) labels.  Unsupported
// kinds, stale pointers and out-of-mask labels are excluded with an explicit
// reason — never rewritten to STOP/KEEP.
// ---------------------------------------------------------------------------

enum class AiEntityShadowExcludeReason : u16 {
    none = 0,
    unsupported_kind = 1,
    stale_target = 2,
    point_error = 3,
    masked = 4,
};

constexpr u8 kAiEntityShadowKeep = 0;
constexpr u8 kAiEntityShadowIssue = 1;
constexpr u8 kAiEntityShadowExcluded = 2;
constexpr u32 kAiEntityShadowPointMaxErrorPx = 64;
constexpr char kAiEntityShadowRecordMagic[4] = {'S', 'H', 'D', '1'};

struct AiEntityShadowDesiredOrder {
    u32 unit_id = 0;
    AiSemanticActionKind kind{};
    u32 target_id = 0;
    i32 x = 0;
    i32 y = 0;
};

struct AiEntityShadowLabel {
    u8 label = kAiEntityShadowKeep;
    u8 command = 0;              // external 0..6, meaningful when ISSUE
    u16 exclude_reason = 0;      // AiEntityShadowExcludeReason
    i32 point = -1;              // point token, point commands only
    i32 target = -1;             // target row, ATTACK_UNIT only
    float inclusion_probability = 1.0f;
};

struct AiEntityShadowLatch {
    bool valid = false;
    u8 command = 0;
    AiEntityKey target{};
    i32 x = 0;                   // resolved absolute point
    i32 y = 0;
};

struct AiEntityShadowState {
    // Keyed by packed EntityKey (runtime_id << 32 | generation).
    std::unordered_map<u64, AiEntityShadowLatch> latches;
    u32 next_tick_frame = 0xffffffffu;
    u32 sequence = 0;
};

std::vector<AiEntityShadowLabel> BuildAiEntityShadowLabels(
    const AiEntitySnapshot& snapshot, const UnitMovementMap* movement_map,
    const std::vector<AiEntityShadowDesiredOrder>& desired,
    AiEntityShadowState& state,
    u32 max_point_error_px = kAiEntityShadowPointMaxErrorPx);

// One dataset record: 'SHD1' + u32 size + wire header + ACT_REQ payload +
// u32 label count + 16-byte label rows.
std::vector<u8> EncodeAiEntityShadowRecord(const AiEntityWireHeader& header,
    const std::vector<u8>& payload,
    const std::vector<AiEntityShadowLabel>& labels);

}  // namespace ranker
