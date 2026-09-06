#pragma once

#include "ranker_ai_actions.h"
#include "ranker_ai_commander_model.h"
#include "ranker_ai_expansion.h"

#include <array>
#include <functional>
#include <map>
#include <vector>

namespace ranker {

enum class CommanderIntent : u8 { hold, defend, attack_move, siege, harass, hunt, scout, retreat };
enum class CommanderRoe : u8 { aggressive, normal, cautious };
struct CommanderPoint { i32 x = 0, y = 0; bool valid = false; };
struct CommanderProductionInfo {
    u32 cost = 0, population = 0, width = 1, height = 1;
};
struct CommanderServices {
    // Authoritative engine planner. Absent validators fail closed.
    std::function<bool(const AiSemanticAction&)> validator;
    std::function<CommanderProductionInfo(AiProductionRequestKind, u32)> production_info;
    std::function<u32(u32)> generation_of;
    // Controlled construction work elapsed / definition production ticks.
    // HP is not a progress proxy: damage may lower an almost finished site.
    std::function<float(u32)> construction_progress;
    std::function<void(const std::string&)> diagnostic;
    // Optional engine path/open-window query. Fraction runs from a to b.
    std::function<CommanderPoint(CommanderPoint, CommanderPoint, float)> route_point;
    u32 cumulative_gathered = 0, kills_investment = 0, losses_investment = 0;
    u32 public_enemy_tribe = 4, curriculum_stage = 2;
    bool autoscout = true;
    u32 packet_budget = 64;
    // Rule-commander variant id (0 = default). Nonzero ids derive randomized
    // opening/timing/target parameters (design 8.3) for opponent diversity.
    u32 teacher_variant = 0;
};
// Teacher variant parameters derived deterministically from a variant id.
struct CommanderTeacherParams {
    u32 opening_velocis = 2;   // velocis before the first tower (2..8)
    u32 tower_frame = 0;       // earliest frame for the first tower (2000..5000)
    float attack_ratio = 1.5f; // own weight over remembered enemy weight to attack (0.8..1.5)
    i32 expansion_shift = 0;   // frames added to the expansion clock (-3000..3000)
    u32 harass_period = 0;     // 0 = no worker harassment, else period in frames (2000..6000)
    u8 target_priority = 0;    // 0 nearest building, 1 enemy expansion, 2 enemy workers
};
CommanderTeacherParams CommanderTeacherVariant(u32 variant);
struct CommanderGhost {
    AiObservedUnit seen;
    u32 last_seen_frame = 0;
    bool visible_now = false;
    u32 generation = 0;
};
struct CommanderUnitState {
    u32 slot = kInvalidUnitRuntimeSlotIndex, type = 0;
    u32 generation = 0;
    u8 squad = 0; // MAIN, GUARD, RAID, WORKERS
    u32 investment = 0, health = 0, born_frame = 0, last_seen = 0, last_progress = 0;
    i32 x = 0, y = 0;
    bool completed = false, regrouping = false, hunting = false;
    AiSemanticAction last_order;
    u32 last_order_frame = 0, applied_intent_serial = 0;
    i32 harvest_tile = -1;
    CommanderPoint post_build_harvest;
};
struct CommanderSquadState {
    CommanderIntent intent = CommanderIntent::hold;
    CommanderRoe roe = CommanderRoe::normal;
    u8 anchor = 1;
    u32 serial = 1, changed_decision = 0;
    float decision_weight = 0;
    bool automatic_retreat = false, arrived = false;
};
struct CommanderBuildReservation {
    AiSemanticAction order;
    u32 cost = 0, issued_frame = 0, attempts = 1;
    bool acknowledged = false;
    u32 source_generation = 0;
};
struct CommanderMergeReservation {
    std::vector<u32> units;
    std::vector<u32> generations;
    u8 squad = 0;
    u32 investment = 0, started_frame = 0;
    CommanderPoint center;
    bool issued = false;
};
struct CommanderReceipt {
    AiSemanticAction order;
    u32 frame = 0, before_count = 0, before_level = 0;
    u32 source_generation = 0;
};
struct CommanderState {
    std::map<u32, CommanderUnitState> units;
    std::map<u32, CommanderGhost> ghosts;
    std::array<CommanderSquadState, 3> squads{};
    std::vector<CommanderBuildReservation> builds;
    std::vector<CommanderMergeReservation> merges;
    std::vector<CommanderReceipt> receipts;
    std::vector<std::pair<u32, u32>> income_history;
    std::vector<u32> damage_frames;
    std::array<u8, 64> last_research{};
    CommanderMask last_mask{};
    CommanderAction previous_action{};
    // Our own executed TRANSFER actions (38..41), measured in simulation frames.
    std::array<u32, 4> last_transfer_frame{};
    CommanderPoint threat;
    u32 threat_frame = 0, last_economy_interrupt = 0, last_damage_interrupt = 0;
    std::map<u32, u32> damage_interrupt_frames;
    u32 last_decision_frame = 0, decision_count = 0, last_view_frame = 0;
    u32 last_army_seen = 0, max_enemy_count = 0, waves_seen = 0, last_wave_frame = 0;
    bool army_near_before = false, initialized = false, pending_reflex_event = false;
    bool external_damage_pending = false;
    u32 external_damage_unit_id = 0;
    u8 worker_policy = 0, rally_squad = 0;
    u32 scout_id = 0, packet_window = 0, packets_in_window = 0;
    u32 mask_violations = 0, silent_rejections = 0;
    AiExpansionPlan expansion;
    u32 expansion_frame = 0;
    bool expansion_initialized = false;
    std::vector<std::vector<u32>> expansion_members;
    // Last frame each berry cluster site was inside our vision (sweep order).
    std::vector<u32> cluster_last_visible;
    std::vector<u8> build_open_cells;
    std::vector<u32> build_components;
    u32 build_component_width = 0;
    CommanderPoint route_destination;
    std::vector<CommanderPoint> route;
    std::array<u32, 8> start_path_lengths{}; // 0 unknown, UINT_MAX unreachable
    std::array<float, 512> static_map{};
    bool static_map_initialized = false;
    // Building placement plans are searched in full only on fixed decision
    // frames; executor ticks and interrupts reuse re-validated cached plans.
    std::array<std::vector<AiSemanticAction>, 16> cached_hq_plans, cached_tower_plans;
    std::array<std::vector<AiSemanticAction>, 10> cached_build_plans;
    u32 plan_cache_frame = 0, plan_cache_workers = 0, plan_cache_available = 0, plan_cache_terrain = 0;
    bool plan_cache_valid = false;
};
struct CommanderSquadView {
    std::vector<u32> members;
    CommanderPoint center;
    float weight = 0, investment = 0;
    CommanderIntent intent = CommanderIntent::hold;
    CommanderRoe roe = CommanderRoe::normal;
    u8 anchor = 1;
};
struct CommanderView {
    CommanderInput input;
    CommanderMask mask{};
    std::array<CommanderPoint, 16> anchors{};
    std::array<CommanderSquadView, 3> squads{};
    std::array<std::vector<AiSemanticAction>, 42> macro_plans;
    // H1b only affects HQ/tower construction, and is conditional on H1.
    std::array<std::vector<AiSemanticAction>, 16> hq_build_plans, tower_build_plans;
    std::vector<AiObservedUnit> own, visible_enemies, visible_neutrals;
    std::vector<u8> build_occupancy;
    std::vector<CommanderGhost> enemies, neutrals;
    CommanderServices services;
    std::array<float, 4> potential_components{};
    std::array<u32, 32> own_counts{}, pending_counts{};
    u32 frame = 0, event = 0, queued_population = 0, reserved_resources = 0;
    u32 worker_cap = 0, workers = 0, army_count = 0, near_enemies = 0;
    float own_weight = 0, enemy_weight = 0, income_rate = 0;
    bool decision_due = false;
    // Own start is a closed plateau whose single ground exit is anchor 2.
    bool closed_plateau = false;
};

CommanderView BuildCommanderView(CommanderState& state, const AiObservation& observation,
    const CommanderServices& services);
void CommanderLegalHeadMask(const CommanderView& view, const CommanderAction& prefix,
    std::size_t head, CommanderMask& mask);
CommanderAction CommanderTeacherAction(const CommanderState& state, const CommanderView& view);
// Invoke on f=1 mod8. Mutates only explicit executor state; caller commits it
// after the returned semantic actions are atomically published by the engine.
std::vector<AiSemanticAction> CommanderExecute(CommanderState& state,
    const AiObservation& observation, const CommanderView& view,
    const CommanderAction* new_decision = nullptr);

} // namespace ranker
