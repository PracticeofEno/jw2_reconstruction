param(
    [switch]$SkipMinimal,
    [switch]$SkipFocused
)

$ErrorActionPreference = 'Stop'

$compiler = 'C:\ProgramData\mingw64\mingw64\bin\g++.exe'
$env:Path = "$(Split-Path $compiler);$env:Path"

$passed = 0
function Run-Test(
    [string]$Path,
    [switch]$Python,
    [switch]$PowerShell) {
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "Missing regression artifact: $Path"
    }
    Write-Output "=== $Path ==="
    if ($Python) {
        & python $Path
    }
    elseif ($PowerShell) {
        & $Path
    }
    else {
        & ".\$Path"
    }
    if ($LASTEXITCODE -ne 0) {
        throw "Regression failed: $Path (exit $LASTEXITCODE)"
    }
    $script:passed++
}

$staticTests = @(
    '.tmp_postuser_gameplay_regression.py',
    '.tmp_variant_definition_loader_regression.py',
    '.tmp_jw21x_loader_offset_regression.py',
    '.tmp_avatar_slot_gate_source_regression.py',
    '.tmp_packet_generic_callback_source_regression.py',
    '.tmp_subtype1a_cost_source_regression.py',
    '.tmp_subtype07_death_marker_source_regression.py',
    '.tmp_cure_support_threshold_source_regression.py',
    '.tmp_berry_frame_alignment_source_regression.py',
    '.tmp_combat_issued_slots_monitor_regression.py',
    '.tmp_meat_live_probe_history_regression.py',
    '.tmp_direct_sound_production_trace_replay_regression.py',
    '.tmp_lifecycle_visibility_source_regression.py'
)
foreach ($test in $staticTests) {
    Run-Test $test -Python
}

$sourceShapeTests = @(
    '.tmp_unit_bar_fill_height_regression.ps1',
    '.tmp_unit_cell_direct_sprite_overlay_regression.ps1',
    '.tmp_pair_progression_comparator_regression.ps1',
    '.tmp_combat_sparse_aligned_comparator_regression.ps1',
    '.tmp_combat_sampling_artifact_resolution_regression.ps1',
    '.tmp_combat_unstable_pretick_regression.ps1',
    '.tmp_nextdiv_local_feedback_source_regression.ps1',
    '.tmp_meat_targeted_p2p_fixture.ps1',
    '.tmp_production_completion_torn_sample_replay_regression.ps1',
    '.tmp_production_cancel_frame_alignment_regression.ps1',
    '.tmp_effect63_blend_factor_regression.ps1',
    '.tmp_action_lockout_range_order_source_regression.ps1',
    '.tmp_guard_anchor_lockout_death_source_regression.ps1',
    '.tmp_mode1_frame_gate_source_regression.ps1',
    '.tmp_projectile_trail_rng_source_regression.ps1',
    '.tmp_runtime_death_order_source_regression.ps1',
    '.tmp_unit_effect_render_queue_source_regression.ps1',
    '.tmp_progress_zero_duration_source_regression.ps1',
    '.tmp_path_state_metadata_order_source_regression.ps1',
    '.tmp_multi_selection_portrait_source_regression.ps1',
    '.tmp_special_action_overlay_source_regression.ps1',
    '.tmp_map_effect_pool_lifetime_source_regression.ps1',
    '.tmp_death_effect_reuse_regression.ps1',
    '.tmp_target_dead_phase_source_regression.ps1',
    '.tmp_map_effect_pickup_source_regression.ps1',
    '.tmp_map_effect_header_chain_source_regression.ps1',
    '.tmp_owner_ai_map_effect_active_list_source_regression.ps1',
    '.tmp_guard_active_tuple_refresh_source_regression.ps1',
    '.tmp_map_effect_equipment_publish_source_regression.ps1',
    '.tmp_visual_progress_pixel_probe_regression.ps1',
    '.tmp_ui_layout_bucket_source_regression.ps1',
    '.tmp_idle_destination_initialization_source_regression.ps1',
    '.tmp_lifecycle_placement_selector_source_regression.ps1',
    '.tmp_medusa_type44_active_chain_regression.ps1',
    '.tmp_reserved_tile_anchor_regression.ps1',
    '.tmp_owner_ai_path_probe_rng_source_regression.ps1',
    '.tmp_lifecycle_render_work_timer_source_regression.ps1',
    '.tmp_meat_amount_action_mode_source_regression.ps1'
)
foreach ($test in $sourceShapeTests) {
    Run-Test $test -PowerShell
}

if (-not $SkipFocused) {
    $focusedTests = @(
        '.tmp_linked_fusion_full_link.exe',
        '.tmp_variant_effect_full_link.exe',
        '.tmp_variant_growth_full_link.exe',
        '.tmp_support_priority_full_link.exe',
        '.tmp_morph_runtime_full_link.exe',
        '.tmp_equipment_variant_rng_full_link.exe',
        '.tmp_nondamage_mutation_no_impact_full_link.exe',
        '.tmp_subtype01_authoritative_cancel_full_link.exe',
        '.tmp_corrective_consensus_wire_regression.exe',
        '.tmp_p2p_low_subtype_matrix_regression.exe',
        '.tmp_c6_nested_wire_regression.exe',
        '.tmp_action_lockout_range_order_regression.exe',
        '.tmp_guard_anchor_lockout_death_regression.exe',
        '.tmp_mode1_partial_round_regression.exe',
        '.tmp_mode1_active_mask_review.exe',
        '.tmp_mode1_reset_pending_review.exe',
        '.tmp_input_action_table_regression.exe',
        '.tmp_berry_contextual_right_click_regression.exe',
        '.tmp_berry_pointer_event_order_regression.exe',
        '.tmp_action_publisher_wire_full_link.exe',
        '.tmp_production_order_queue_edge_full_link.exe',
        '.tmp_selected_action_init_parity_full_link.exe',
        '.tmp_corpse_action_input_full_link.exe',
        '.tmp_avatar_slot_gate_full_link.exe',
        '.tmp_packet_generic_callback_full_link.exe',
        '.tmp_subtype1a_cost_death_full_link.exe',
        '.tmp_subtype07_death_marker_full_link.exe',
        '.tmp_cure_support_threshold_full_link.exe',
        '.tmp_unit_effect_render_queue_parity_regression.exe',
        '.tmp_overlay_runtime_regression_full_link.exe',
        '.tmp_owner_ai_map_effect_active_list_runtime_regression.exe',
        '.tmp_map_effect_payload_preservation_regression.exe',
        '.tmp_owner_ai_type2b_upgrade_gate_regression.exe',
        '.tmp_owner_ai_rally_target_summary_regression.exe',
        '.tmp_owner_ai_pressure_summary_regression.exe',
        '.tmp_owner_ai_threat_response_regression.exe',
        '.tmp_owner_ai_temp_probe_lifecycle_regression.exe',
        '.tmp_avatar_spawn_runtime_regression.exe',
        '.tmp_lifecycle_visibility_runtime_regression.exe',
        '.tmp_unit_bar_fill_height_runtime_regression.exe',
        '.tmp_guard_active_tuple_refresh_runtime_regression.exe',
        '.tmp_map_effect_equipment_publish_runtime_regression.exe',
        '.tmp_ui_layout_bucket_completion_regression.exe',
        '.tmp_top_right_hud_focused_regression.exe',
        '.tmp_owner_color_output_regression.exe',
        '.tmp_meat_amount_action_mode_runtime_regression.exe'
        '.tmp_lifecycle_class_selector_runtime_regression.exe'
        '.tmp_nearest_hostile_raw_slot_state_regression.exe'
        '.tmp_raw_map_start_slot_mirror_regression.exe'
        '.tmp_player_slot_lobby_runtime_split_regression.exe'
        '.tmp_raw_subtype15_slot_authority_regression.exe'
        '.tmp_p2p_inactive_transition_regression.current.exe'
        '.tmp_production_failure_sound_regression.current.exe'
        '.tmp_production_order_start_slot_reset_regression.current.exe'
        '.tmp_unit_sound_consumption_regression.exe'
        '.tmp_gameplay_context_cursor_regression.exe'
        '.tmp_selected_health_text_color_regression.exe'
        '.tmp_fog_smoothing_mask_regression.exe'
        '.tmp_placement_preview_cancel_focused_regression.exe'
        '.tmp_resource_item_ui_wire_regression.exe'
    )
    foreach ($test in $focusedTests) {
        Run-Test $test
    }
}

if (-not $SkipMinimal) {
    $minimalTests = @(
        '.tmp_idle_target_lifecycle_gate_regression.exe',
        '.tmp_pathfinder_greedy_regression_review.exe',
        '.tmp_type128_render_scratch_regression.exe',
        '.tmp_ee765_ui_focused_regression.exe',
        '.tmp_ee765_avatar_duration_regression.exe',
        '.tmp_construction_cancel_parity_regression.exe',
        '.tmp_berry_harvest_boundary_regression.exe',
        '.tmp_neutral_revival_parity_regression.exe',
        '.tmp_damage_reaction_flee_parity_regression.exe',
        '.tmp_world_render_parity_regression.exe',
        '.tmp_construction_progress_frame_parity_regression.exe',
        '.tmp_d77_progress_layout_regression.exe',
        '.tmp_owner_ai_snapshot_codec_regression.exe'
    )
    foreach ($test in $minimalTests) {
        Run-Test $test
    }
}

Write-Output "FINAL_REGRESSION_SUITE_PASS count=$passed"
