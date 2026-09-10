// Included by ranker_ai_commander.cpp. Opt-in Elf teacher; no game rule changes.
// Validated foundation: Elf versus normal Demon AI, 7/12 development wins.
// See docs/JW2_COMMANDER_ELF_FOCUS_20260909.md for provenance and limits.
static constexpr std::array<std::array<u32, 8>, 170> kElfTeacherPrerequisites{{
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x68u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x6bu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x6cu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x67u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x69u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x68u,0x6bu,0x6cu,0x69u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x67u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x69u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x67u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x78u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x78u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x7cu,0x7au,0x79u,0x78u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x78u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x7cu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x85u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x85u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x86u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x86u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x86u,0x88u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x89u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x86u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x85u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x85u,0x86u,0x88u,0x89u,0x8au,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x8au,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x85u,0x88u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x98u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x94u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x95u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x99u,0x95u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x98u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x97u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x9au,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x9bu,0x98u,0x95u,0x99u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x9bu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x9bu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x92u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x9bu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x68u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x68u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x64u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x60u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x60u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x64u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x65u,0x64u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x65u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x66u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x68u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x68u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x64u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x60u,0x62u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x72u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x72u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x70u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x70u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x76u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x74u,0x75u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x77u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x76u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x74u,0x75u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x78u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x70u,0x72u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x84u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x80u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x84u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x84u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x84u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x85u,0x86u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x85u,0x88u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x86u,0x88u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x80u,0x82u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x94u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x90u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x9au,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x94u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x96u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x9bu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x9au,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x94u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x94u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x90u,0x92u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0x90u,0x92u,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
    {{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}},
}};
// Shared composition core: public Primitive uses WhiteElf and refreshed enemy
// intel; public Demon retains BlueElf. No start-position dispatcher.
// Production uses the policy masks and original Jw2_09.trc prerequisites.
CommanderAction ElfMatchupTeacher(const CommanderState& s, const CommanderView& v) {
    const bool primitive = v.services.public_enemy_tribe == 0;
    const u32 frontline = primitive ? 0x12u : 0x14u;
    CommanderAction a{};
    const auto& z = v.input.vector;
    const auto& catalog = race_actions(v.services);
    const u32 race = v.services.own_tribe, recipe = v.services.teacher_variant & 3u;
    const u32 base = 0x60 + 16 * race, worker_type = 16 * race;
    const u32 cores[4] = {0x03, 0x11, 0x22, 0x31};
    const u32 supports[4] = {0x01, 0x13, 0x24, 0x36};
    const u32 sieges[4] = {0x06, 0x17, 0x2a, 0x35};
    const u32 core = cores[race], support = supports[race], siege = sieges[race];
    const float resources = std::expm1(z[8] * std::log1p(20000.0f));
    const u32 workers = v.workers + v.pending_counts[0], army = v.army_count;
    const auto done = [&](u32 t) { return v.own_counts[count_index(t)]; };
    const auto count = [&](u32 t) { return done(t) + v.pending_counts[count_index(t)]; };
    const auto macro = [&](u32 t) {
        for (u32 i = 1; i < 64; ++i)
            if ((catalog[i].kind == 1 || catalog[i].kind == 2) && catalog[i].type == t) return i;
        return 0u;
    };
    bool saving = false;
    std::function<void(u32, u32, u32)> need;
    need = [&](u32 t, u32 target, u32 depth) {
        if (a[0] || saving || depth > 12 || t >= 170 || count(t) >= target) return;
        const u32 slot = macro(t);
        if (!slot) return;
        const auto& spec = catalog[slot];
        for (u32 prerequisite : kElfTeacherPrerequisites[t]) {
            if (prerequisite == 0xffffffffu) break;
            if (!done(prerequisite)) { need(prerequisite, 1, depth + 1); return; }
        }
        if (spec.kind == 1 && !done(spec.producer)) { need(spec.producer, 1, depth + 1); return; }
        if (v.mask[slot]) { a[0] = u8(slot); return; }
        const auto cost = info(v.services, spec.kind == 2 ? AiProductionRequestKind::building :
            AiProductionRequestKind::unit, t).cost;
        if (resources + 0.5f < cost) saving = true;
    };
    auto goal = [&](u32 t, u32 n) {
        if (t == 0x11) n = ElfCemRedGoal(v,n);
        if (t == frontline) n = ElfCemFrontlineGoal(v,n);
        if (t == 0x17) n = ElfCemManaGoal(v,n);
        need(t,n,0);
    };
    auto legal = [&](u32 slot) { if (!a[0] && !saving && v.mask[slot]) a[0] = u8(slot); };
    // Research roles differ across races. In particular Demon slots 25/26
    // upgrade air units; its Skeleton/Kelpa ground line needs slots 23/31.
    auto research = [&](u32 slot, u32 wanted, u32 observation_index) {
        if (a[0] || saving) return;
        const auto& spec = catalog[slot];
        const u32 level = u32(std::round(z[observation_index] * spec.maximum));
        if (level >= wanted) return;
        for (const auto& u : v.own)
            if (order_count(u, AiSemanticActionKind::research, spec.type)) return;
        if (!done(spec.producer)) { goal(spec.producer, 1); return; }
        if (v.mask[slot]) a[0] = u8(slot);
        else if (resources < info(v.services, AiProductionRequestKind::research, spec.type).cost)
            saving = true;
    };
    const auto& main = v.squads[0];
    const auto& guard = v.squads[1];
    const float near_enemy = z[46] * 10000, remembered = z[36] * 20000;
    const bool danger = near_enemy > std::max(400.0f, z[33] * 10000 * 0.8f);
    const bool depleted = z[16] < .55f && (done(base) < 2 || z[17] < .35f);
    const bool expansion = depleted && done(base) < 3 && army >= 8 && !danger;
    const u32 first_hall = catalog[macro(core)].producer;


    // Elf diagnosis: default midgame armies consist almost entirely of RedElf,
    // without the Library, BlueElf frontline or ManaHall support. Prior v4
    // delays this composition behind redundant Rangers, towers and RedElf goals.
    if (z[24] * 16 < 3 && !v.pending_counts[18]) goal(base + 2, done(base + 2) + 1);
    if (workers < 8) goal(worker_type, 8);
    goal(0x74, 1);
    if (workers < 12) goal(worker_type, 12);
    goal(0x11, 2);
    research(22, 1, 58);
    goal(0x11, 4);
    if (workers < 16) goal(worker_type, std::min(v.worker_cap, 16u));
    goal(0x73, 1);
    // Fund support while the first hall continues building its queued RedElves.
    goal(0x76, 1);
    goal(0x15, 1);
    goal(0x17, 2);
    if (workers < ElfCemWorkerGoal(v,20u)) goal(worker_type, std::min(v.worker_cap, ElfCemWorkerGoal(v,20u)));
    goal(0x75, 1);
    goal(0x78, 1);
    research(25, 1, 62);
    research(26, 1, 63);
    goal(frontline, recipe == 1 ? 2 : 4);
    goal(0x11, recipe == 1 ? 6 : recipe == 2 ? 12 : 8);
    goal(0x17, recipe == 1 ? 6 : 4);
    goal(0x15, 2);
    if (army >= 12) goal(0x73, 2);
    if (!a[0] && !saving && !guard.members.empty() && !main.members.empty() &&
        v.frame - s.last_transfer_frame[1] >= 440) legal(39);
    if (expansion) goal(base, done(base) + 1);
    if (army >= 12 && !danger) {
        research(25, 2, 62); research(26, 2, 63);
        goal(0x7a, 1); legal(30);
    }
    goal(0x74, 2);
    goal(0x11, recipe == 1 ? 12 : 18);
    goal(frontline, recipe == 1 ? 4 : 8);
    goal(0x17, recipe == 1 ? 10 : 6);
    goal(0x15, 4);
    if (done(base) >= 2 && workers < ElfCemWorkerGoal(v,28u)) goal(worker_type, std::min(v.worker_cap, ElfCemWorkerGoal(v,28u)));
    if (army >= 24 && !danger) { research(25, 3, 62); research(26, 3, 63); }
    if (army >= 18 && !danger) { legal(24); legal(27); legal(34); }
    goal(0x11, 36); goal(frontline, 16); goal(0x17, 12);

    // Keep a single main force; decide from our strength and remembered sightings.
    bool attacking = s.squads[0].intent == CommanderIntent::attack_move ||
        s.squads[0].intent == CommanderIntent::siege;
    const float ratio = ElfCemAttackRatio(v,recipe == 2 ? 1.0f : 1.15f);
    const bool main_ready = main.weight >= .70f * v.own_weight;
    const bool desperate = (z[22] * 180 >= 155 || (v.frame > 18000 && z[9] < .03f)) && army >= 8;
    const bool enemy_plateau = z[513] > 0 || z[514] > 0;
    const bool siege_ready = !enemy_plateau || done(siege) >= 3 || army >= 38;
    const bool ready = main_ready && army >= ElfCemAttackArmy(v,24u) && siege_ready && z[47] < .5f &&
        v.own_weight >= 5000 && v.own_weight >= remembered * ratio + 1500;

    if (danger || army < 8 || (!desperate && v.own_weight < remembered * .8f)) attacking = false;
    else if ((ready || desperate) && s.decision_count - s.squads[0].changed_decision >= 12) attacking = true;
    const u8 home = v.closed_plateau && done(base + 3) && v.anchors[2].valid ? 2 : 1;
    u8 intent = u8(CommanderIntent::hold), anchor = home, roe = 0;
    if (attacking) {
        intent = u8(CommanderIntent::attack_move); roe = 1;
        anchor = v.anchors[6].valid ? 6 : v.anchors[12].valid ? 12 : 4;
        if (anchor == 6 && near(main.center, v.anchors[6], 640)) intent = u8(CommanderIntent::siege);
    } else if (expansion && v.anchors[3].valid && z[260] < .5f && near_enemy == 0 && army >= 16) {
        intent = u8(CommanderIntent::scout); anchor = 3;
    } else if (primitive && !danger && near_enemy == 0 && army >= 16 && z[47] >= .5f &&
        (v.anchors[4].valid || v.anchors[12].valid)) {
        // Expansion scouting has priority; stale enemy intel is a separate trip.
        intent = u8(CommanderIntent::scout);
        anchor = v.anchors[4].valid ? 4 : 12;
    }
    if (!attacking && !danger && near_enemy == 0 && army >= 4 && army < 16 &&
        z[30] < .6f && v.anchors[11].valid && near(v.anchors[11], v.anchors[0], 640) &&
        v.mask[kCommanderHeadOffsets[3] + u8(CommanderIntent::hunt)]) {
        intent = u8(CommanderIntent::hunt); anchor = 11; roe = 1;
    }
    if (!main.members.empty() && (u8(s.squads[0].intent) != intent ||
        s.squads[0].anchor != anchor || u8(s.squads[0].roe) != roe)) {
        a[2] = 1; a[3] = intent; a[4] = anchor; a[5] = roe;
    }
    if (a[0] == 12) a[1] = 3;
    else if (a[0] == 14) a[1] = home;
    a[6] = 0; a[7] = 0;
    if (s.decision_count % 4 == 0) {
        float best_skill = 0;
        for (u32 slot = 64; slot < kCommanderMacroCount; ++slot) {
            if (!v.mask[slot] || v.macro_plans[slot].empty()) continue;
            const auto& order = v.macro_plans[slot][0];
            if (order.unit_ids.empty()) continue;
            if (order.kind == AiSemanticActionKind::set_stance) {
                const auto* caster = find_unit(v.own, order.unit_ids[0]);
                if (!caster) continue;
                bool engaged = false;
                for (const auto& enemy : v.visible_enemies)
                    if (combat(enemy) && near(point(*caster), point(enemy), 400)) { engaged = true; break; }
                const bool wanted = engaged && hp(*caster) >= .45f;
                if (order.stance_on == wanted) {
                    const float score = wanted ? 180.0f : 1000.0f;
                    if (score > best_skill) { best_skill=score; a[0]=u8(slot); a[1]=0; }
                }
                continue;
            }
            if (order.kind != AiSemanticActionKind::use_ability) continue;
            const u32 ability = order.ability_id;
            const auto* source = find_unit(v.own, order.unit_ids[0]);
            if (!source || ability >= 32) continue;
            const auto& d = kAiSkillDefinitions[ability];
            const CommanderPoint target{order.target_x, order.target_y, true};
            float score = 0;
            if (ability == 1 || ability == 3 || ability == 6 || ability == 9 || ability == 11 ||
                ability == 13 || ability == 14 || ability == 16 || ability == 20 || ability == 22 ||
                ability == 24 || ability == 29) {
                // Select useful legal menu entries: proximity of the caster alone
                // does not make its own position a valid offensive objective.
                if (d.mode == 3) {
                    // Unit-only damage must target the actual enemy ID. A nearby
                    // hostile does not make a self or allied target hostile.
                    const auto* enemy = find_unit(v.visible_enemies, order.target_unit_id);
                    if (!enemy || enemy->render_class >= 32 ||
                        !(d.target_classes & (1u << enemy->render_class))) continue;
                }
                bool hostile_target = false;
                float enemies = 0, friends = 0;
                const i32 radius = std::max(48u, d.radius);
                for (const auto& enemy : v.visible_enemies) {
                    if (enemy.render_class >= 32 || !(d.target_classes & (1u << enemy.render_class))) continue;
                    if (near(point(enemy), target, 32)) hostile_target = true;
                    if (near(point(enemy), target, radius)) enemies += weight(enemy);
                }
                if (!hostile_target || distance(point(*source), target) > 700) continue;
                const bool area = ability == 3 || ability == 6 || ability == 9 ||
                    ability == 13 || ability == 14 || ability == 29;
                if (area) for (const auto& ally : v.own)
                    if (near(point(ally), target, radius)) friends += weight(ally);
                score = enemies - 1.5f * friends - d.health_cost;
                if (score < 100) continue;
            } else if (ability == 0 || ability == 2 || ability == 8 || ability == 17 || ability == 21) {
                const auto* ally = find_unit(v.own, order.target_unit_id);
                if (!ally || !combat(*ally)) continue;
                bool contact = false;
                for (const auto& enemy : v.visible_enemies)
                    if (combat(enemy) && near(point(*ally), point(enemy), 400)) { contact = true; break; }
                if (contact) score = 100 + (1 - hp(*ally)) * weight(*ally);
            } else if (ability == 10 || ability == 25 || ability == 28) {
                if (!order.target_unit_id) continue;
                score = 120; // Actual visible-corpse validity was checked by the menu.
            }
            if (score > best_skill) { best_skill = score; a[0] = u8(slot); a[1] = 0; }
        }
    }
    CommanderMask mask = v.mask;
    for (std::size_t h = 0; h < 8; ++h) {
        CommanderLegalHeadMask(v, a, h, mask);
        const auto off = kCommanderHeadOffsets[h];
        if (a[h] >= kCommanderHeadSizes[h] || !mask[off + a[h]]) {
            a[h] = 0;
            for (u32 i = 0; i < kCommanderHeadSizes[h]; ++i) if (mask[off + i]) { a[h] = u8(i); break; }
        }
    }
    return a;
}

