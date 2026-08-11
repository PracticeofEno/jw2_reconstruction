#include "ranker_reconstructed_code/src/ranker_winmain.cpp"

#include <cstddef>
#include <iostream>

int main() {
    using ranker::GameplayTooltipDrawCommand;
    using ranker::GameplayTooltipState;
    using ranker::GameplayLoopState;
    using ranker::GameplayModalUiState;
    using ranker::GameplayResultAction;
    using ranker::GameplayResultScreenState;
    using ranker::Mode1GameplayPacketDispatchState;
    using ranker::GameplayVisibilityGrid;
    using ranker::MinimapRenderState;
    using ranker::MapEffectContext;
    using ranker::MapEffectInstance;
    using ranker::OwnerAiRuntimeState;
    using ranker::OwnerAiSlotRuntime;
    using ranker::OwnerSessionCounterTables;
    using ranker::OwnerStrategicTargetState;
    using ranker::OwnerTransportRouteState;
    using ranker::PaletteCacheState;
    using ranker::PlayerSlotRuntimeState;
    using ranker::ProductionOrderRuntimeState;
    using ranker::RuntimeGlobals;
    using ranker::UiOverlayCommandOption;
    using ranker::UiOverlayDrawRecord;
    using ranker::UiOverlayHotRegion;
    using ranker::UiOverlayHoverContext;
    using ranker::UiOverlayMinimapUnit;
    using ranker::UiOverlayMinimapMarker;
    using ranker::UiOverlayProgressCommand;
    using ranker::UiOverlayState;
    using ranker::UiOverlayTextCommand;
    using ranker::UnitMovementDefinition;
    using ranker::UnitMovementCell;
    using ranker::UnitMovementContext;
    using ranker::UnitMovementMap;
    using ranker::UnitMovementUnit;
    using ranker::UnitQueuedCommand;
    using ranker::UnitLifecycleContext;
    using ranker::UnitRenderItem;
    using ranker::UnitRenderQueueContext;
    using ranker::UnitEffectRuntime;
    using ranker::UnitEffectRuntimeState;
    using ranker::UnitEffectTrailSegment;
    std::cout
        << "{\"pointer_size\":" << sizeof(void*)
        << ",\"bool_size\":" << sizeof(bool)
        << ",\"vector_header_size\":" << sizeof(std::vector<u32>)
        << ",\"frame_random\":"
        << offsetof(RuntimeGlobals, gameplay_frame_random_state)
        << ",\"visibility\":"
        << offsetof(RuntimeGlobals, gameplay_visibility_grid)
        << ",\"movement\":"
        << offsetof(RuntimeGlobals, gameplay_movement_context)
        << ",\"lifecycle\":"
        << offsetof(RuntimeGlobals, gameplay_lifecycle_context)
        << ",\"production_runtime\":"
        << offsetof(RuntimeGlobals, gameplay_production_runtime)
        << ",\"active_session_definitions\":"
        << offsetof(RuntimeGlobals, active_session_definitions)
        << ",\"unit_reference_tables\":"
        << offsetof(RuntimeGlobals, unit_reference_tables)
        << ",\"session_definition_unit_records\":"
        << offsetof(ranker::SessionRuntimeDefinitionTableSet, unit_records)
        << ",\"runtime_definition_record_size\":"
        << sizeof(ranker::RuntimeDefinitionRecord)
        << ",\"runtime_definition_record_bytes\":"
        << offsetof(ranker::RuntimeDefinitionRecord, bytes)
        << ",\"unit_reference_completion_reverse\":"
        << offsetof(ranker::GameSessionUnitReferenceTables, completion_reverse)
        << ",\"unit_render_queue\":"
        << offsetof(RuntimeGlobals, gameplay_unit_render_queue)
        << ",\"map_effect_context\":"
        << offsetof(RuntimeGlobals, map_effect_context)
        << ",\"player_slots\":"
        << offsetof(RuntimeGlobals, gameplay_player_slots)
        << ",\"owner_ai\":"
        << offsetof(RuntimeGlobals, gameplay_owner_ai_state)
        << ",\"owner_transport_routes\":"
        << offsetof(RuntimeGlobals, gameplay_owner_transport_routes)
        << ",\"owner_strategic_targets\":"
        << offsetof(RuntimeGlobals, gameplay_owner_strategic_targets)
        << ",\"unit_effects\":"
        << offsetof(RuntimeGlobals, gameplay_unit_effect_runtime)
        << ",\"unit_commands\":"
        << offsetof(RuntimeGlobals, gameplay_unit_commands)
        << ",\"owner_counters\":"
        << offsetof(RuntimeGlobals, gameplay_owner_counters)
        << ",\"owner_counter_table6\":"
        << offsetof(OwnerSessionCounterTables, tables) +
            sizeof(OwnerSessionCounterTables{}.tables[0]) * 6
        << ",\"owner_counter_size\":" << sizeof(OwnerSessionCounterTables)
        << ",\"player_slots_size\":" << sizeof(PlayerSlotRuntimeState)
        << ",\"player_slots_slot_states\":"
        << offsetof(PlayerSlotRuntimeState, slot_states)
        << ",\"player_slots_relation_masks\":"
        << offsetof(PlayerSlotRuntimeState, owner_relation_masks)
        << ",\"player_slots_visibility_masks\":"
        << offsetof(PlayerSlotRuntimeState, owner_visibility_masks)
        << ",\"player_slots_start_x\":"
        << offsetof(PlayerSlotRuntimeState, owner_start_x)
        << ",\"player_slots_start_y\":"
        << offsetof(PlayerSlotRuntimeState, owner_start_y)
        << ",\"player_slots_nearest_hostile\":"
        << offsetof(PlayerSlotRuntimeState, nearest_hostile_slots)
        << ",\"player_slots_active_count\":"
        << offsetof(PlayerSlotRuntimeState, active_slot_count)
        << ",\"player_slots_local_player\":"
        << offsetof(PlayerSlotRuntimeState, local_player_slot)
        << ",\"player_slots_lobby_states\":"
        << offsetof(PlayerSlotRuntimeState, lobby_slot_states)
        << ",\"owner_ai_state_size\":" << sizeof(OwnerAiRuntimeState)
        << ",\"owner_ai_owners\":"
        << offsetof(OwnerAiRuntimeState, owners)
        << ",\"owner_ai_owner_faction_ids\":"
        << offsetof(OwnerAiRuntimeState, owner_faction_ids)
        << ",\"owner_ai_owner_unit_type_counts\":"
        << offsetof(OwnerAiRuntimeState, owner_unit_type_counts)
        << ",\"owner_ai_shared_grid_table\":"
        << offsetof(OwnerAiRuntimeState, shared_grid_table)
        << ",\"owner_ai_owner_population_used\":"
        << offsetof(OwnerAiRuntimeState, owner_population_used)
        << ",\"owner_ai_owner_population_reserved\":"
        << offsetof(OwnerAiRuntimeState, owner_population_reserved)
        << ",\"owner_ai_slot_size\":" << sizeof(OwnerAiSlotRuntime)
        << ",\"owner_ai_primary_target_owner\":"
        << offsetof(OwnerAiSlotRuntime, primary_target_owner)
        << ",\"owner_ai_support_target_owner\":"
        << offsetof(OwnerAiSlotRuntime, support_target_owner)
        << ",\"owner_ai_support_mode\":"
        << offsetof(OwnerAiSlotRuntime, support_mode)
        << ",\"owner_ai_support_anchor\":"
        << offsetof(OwnerAiSlotRuntime, support_anchor)
        << ",\"owner_ai_resource_budget_percent\":"
        << offsetof(OwnerAiSlotRuntime, resource_budget_percent)
        << ",\"owner_ai_profile_counter\":"
        << offsetof(OwnerAiSlotRuntime, profile_counter)
        << ",\"owner_ai_route_refresh_counter\":"
        << offsetof(OwnerAiSlotRuntime, route_refresh_counter)
        << ",\"owner_ai_script_cycle_counter\":"
        << offsetof(OwnerAiSlotRuntime, script_cycle_counter)
        << ",\"owner_ai_previous_script_cycle_counter\":"
        << offsetof(OwnerAiSlotRuntime, previous_script_cycle_counter)
        << ",\"owner_ai_transport_phase_state\":"
        << offsetof(OwnerAiSlotRuntime, transport_phase_state)
        << ",\"owner_ai_last_timing_frame\":"
        << offsetof(OwnerAiSlotRuntime, last_timing_frame)
        << ",\"owner_ai_build_budget\":"
        << offsetof(OwnerAiSlotRuntime, build_budget)
        << ",\"owner_ai_production_budget\":"
        << offsetof(OwnerAiSlotRuntime, production_budget)
        << ",\"owner_ai_profile_age\":"
        << offsetof(OwnerAiSlotRuntime, profile_age)
        << ",\"owner_ai_production_pause_flag\":"
        << offsetof(OwnerAiSlotRuntime, production_pause_flag)
        << ",\"owner_ai_unit_demand\":"
        << offsetof(OwnerAiSlotRuntime, unit_demand)
        << ",\"owner_ai_unit_demand_shadow\":"
        << offsetof(OwnerAiSlotRuntime, unit_demand_shadow)
        << ",\"owner_ai_route_radius\":"
        << offsetof(OwnerAiSlotRuntime, route_radius)
        << ",\"owner_ai_primary_target_point\":"
        << offsetof(OwnerAiSlotRuntime, primary_target_point)
        << ",\"owner_ai_primary_target_flags\":"
        << offsetof(OwnerAiSlotRuntime, primary_target_flags)
        << ",\"owner_ai_neutral_route_target_point\":"
        << offsetof(OwnerAiSlotRuntime, neutral_route_target_point)
        << ",\"owner_ai_placement_record\":"
        << offsetof(OwnerAiSlotRuntime, placement_record)
        << ",\"owner_ai_profile_state_flag\":"
        << offsetof(OwnerAiSlotRuntime, profile_state_flag)
        << ",\"owner_transport_route_size\":"
        << sizeof(OwnerTransportRouteState)
        << ",\"owner_transport_route_count\":"
        << offsetof(OwnerTransportRouteState, route_count)
        << ",\"owner_strategic_target_size\":"
        << sizeof(OwnerStrategicTargetState)
        << ",\"owner_strategic_target_owner\":"
        << offsetof(OwnerStrategicTargetState, target_owner_id)
        << ",\"owner_strategic_blocked_mask\":"
        << offsetof(OwnerStrategicTargetState, blocked_owner_mask)
        << ",\"owner_strategic_preferred_target\":"
        << offsetof(OwnerStrategicTargetState, preferred_target)
        << ",\"unit_effect_state_size\":"
        << sizeof(UnitEffectRuntimeState)
        << ",\"unit_effect_trail_segments\":"
        << offsetof(UnitEffectRuntimeState, trail_segments)
        << ",\"unit_effect_trail_segment_size\":"
        << sizeof(UnitEffectTrailSegment)
        << ",\"unit_effect_trail_segment_id\":"
        << offsetof(UnitEffectTrailSegment, effect_id)
        << ",\"unit_effect_trail_segment_x0\":"
        << offsetof(UnitEffectTrailSegment, x0)
        << ",\"unit_effect_trail_segment_y0\":"
        << offsetof(UnitEffectTrailSegment, y0)
        << ",\"unit_effect_trail_segment_x1\":"
        << offsetof(UnitEffectTrailSegment, x1)
        << ",\"unit_effect_trail_segment_y1\":"
        << offsetof(UnitEffectTrailSegment, y1)
        << ",\"unit_effect_trail_segment_width\":"
        << offsetof(UnitEffectTrailSegment, width)
        << ",\"unit_effect_trail_segment_color\":"
        << offsetof(UnitEffectTrailSegment, color)
        << ",\"unit_effect_slots\":"
        << offsetof(UnitEffectRuntimeState, effect_slots)
        << ",\"unit_effect_active_indices\":"
        << offsetof(UnitEffectRuntimeState, active_effect_indices)
        << ",\"unit_effect_free_indices\":"
        << offsetof(UnitEffectRuntimeState, free_effect_indices)
        << ",\"unit_effect_frame\":"
        << offsetof(UnitEffectRuntimeState, frame_counter)
        << ",\"unit_effect_size\":" << sizeof(UnitEffectRuntime)
        << ",\"unit_effect_active\":"
        << offsetof(UnitEffectRuntime, active)
        << ",\"unit_effect_id\":"
        << offsetof(UnitEffectRuntime, effect_id)
        << ",\"unit_effect_flags\":"
        << offsetof(UnitEffectRuntime, flags)
        << ",\"unit_effect_tick\":"
        << offsetof(UnitEffectRuntime, tick)
        << ",\"unit_effect_frame_value\":"
        << offsetof(UnitEffectRuntime, frame)
        << ",\"unit_effect_amount\":"
        << offsetof(UnitEffectRuntime, amount)
        << ",\"unit_effect_source\":"
        << offsetof(UnitEffectRuntime, source_unit_id)
        << ",\"unit_effect_target\":"
        << offsetof(UnitEffectRuntime, target_unit_id)
        << ",\"unit_effect_linked\":"
        << offsetof(UnitEffectRuntime, linked_unit_id)
        << ",\"unit_effect_x\":"
        << offsetof(UnitEffectRuntime, x)
        << ",\"unit_effect_y\":"
        << offsetof(UnitEffectRuntime, y)
        << ",\"unit_effect_previous_x\":"
        << offsetof(UnitEffectRuntime, previous_x)
        << ",\"unit_effect_previous_y\":"
        << offsetof(UnitEffectRuntime, previous_y)
        << ",\"unit_effect_direction\":"
        << offsetof(UnitEffectRuntime, direction)
        << ",\"production_variant_counts\":"
        << offsetof(ProductionOrderRuntimeState, variant_counts)
        << ",\"production_lock_flags\":"
        << offsetof(ProductionOrderRuntimeState, lock_flags)
        << ",\"production_primary_resources\":"
        << offsetof(ProductionOrderRuntimeState, owner_primary_resources)
        << ",\"production_secondary_resources\":"
        << offsetof(ProductionOrderRuntimeState, owner_secondary_resources)
        << ",\"production_runtime_size\":"
        << sizeof(ProductionOrderRuntimeState)
        << ",\"palette_pixel_slots\":"
        << offsetof(PaletteCacheState, pixel_slots)
        << ",\"palette_size\":" << sizeof(PaletteCacheState)
        << ",\"size\":" << sizeof(RuntimeGlobals)
        << ",\"loop_simulation_frame\":"
        << offsetof(GameplayLoopState, simulation_frame_counter)
        << ",\"loop_present_frame\":"
        << offsetof(GameplayLoopState, present_frame_counter)
        << ",\"loop_frame_intervals\":"
        << offsetof(GameplayLoopState, frame_intervals)
        << ",\"loop_fixed_step_intervals\":"
        << offsetof(GameplayLoopState, fixed_step_intervals)
        << ",\"loop_fixed_step_repeat_counts\":"
        << offsetof(GameplayLoopState, fixed_step_repeat_counts)
        << ",\"loop_fixed_step_mode\":"
        << offsetof(GameplayLoopState, fixed_step_mode)
        << ",\"loop_modal_subloop_active\":"
        << offsetof(GameplayLoopState, modal_subloop_active)
        << ",\"loop_session_active\":"
        << offsetof(GameplayLoopState, session_active)
        << ",\"loop_pause_requested\":"
        << offsetof(GameplayLoopState, pause_loop_requested)
        << ",\"loop_leave_requested\":"
        << offsetof(GameplayLoopState, leave_requested)
        << ",\"loop_size\":" << sizeof(GameplayLoopState)
        << ",\"modal_main_menu_active\":"
        << offsetof(GameplayModalUiState, main_menu_active)
        << ",\"modal_exit_surrender_active\":"
        << offsetof(GameplayModalUiState, exit_surrender_active)
        << ",\"modal_quit_to_frontend_requested\":"
        << offsetof(GameplayModalUiState, quit_to_frontend_requested)
        << ",\"modal_surrender_requested\":"
        << offsetof(GameplayModalUiState, surrender_requested)
        << ",\"modal_worker_exit_requested\":"
        << offsetof(GameplayModalUiState, worker_exit_requested)
        << ",\"modal_end_session_requested\":"
        << offsetof(GameplayModalUiState, end_session_requested)
        << ",\"modal_size\":" << sizeof(GameplayModalUiState)
        << ",\"packet_dispatch_session_complete\":"
        << offsetof(Mode1GameplayPacketDispatchState, session_complete_requested)
        << ",\"packet_dispatch_local_player\":"
        << offsetof(Mode1GameplayPacketDispatchState, local_player_index)
        << ",\"packet_dispatch_last_subtype\":"
        << offsetof(Mode1GameplayPacketDispatchState, last_subtype)
        << ",\"packet_dispatch_player_reset_gate\":"
        << offsetof(Mode1GameplayPacketDispatchState, vote_completion_gate_open)
        << ",\"packet_dispatch_size\":"
        << sizeof(Mode1GameplayPacketDispatchState)
        << ",\"result_action_log\":"
        << offsetof(GameplayResultScreenState, action_log)
        << ",\"result_selected_tribe\":"
        << offsetof(GameplayResultScreenState, selected_tribe_index)
        << ",\"result_mode\":"
        << offsetof(GameplayResultScreenState, result_mode)
        << ",\"result_player_count\":"
        << offsetof(GameplayResultScreenState, player_count)
        << ",\"result_replay_controls\":"
        << offsetof(GameplayResultScreenState, replay_controls_available)
        << ",\"result_replay_base_zero\":"
        << offsetof(GameplayResultScreenState, replay_record_index_is_zero)
        << ",\"result_action_size\":" << sizeof(GameplayResultAction)
        << ",\"result_action_kind\":" << offsetof(GameplayResultAction, kind)
        << ",\"result_action_value0\":" << offsetof(GameplayResultAction, value0)
        << ",\"result_action_value1\":" << offsetof(GameplayResultAction, value1)
        << ",\"visibility_width\":"
        << offsetof(GameplayVisibilityGrid, width)
        << ",\"visibility_height\":"
        << offsetof(GameplayVisibilityGrid, height)
        << ",\"visibility_current\":"
        << offsetof(GameplayVisibilityGrid, current)
        << ",\"visibility_previous\":"
        << offsetof(GameplayVisibilityGrid, previous)
        << ",\"visibility_owner\":"
        << offsetof(GameplayVisibilityGrid, owner)
        << ",\"visibility_size\":" << sizeof(GameplayVisibilityGrid)
        << ",\"tooltip_draw_commands\":"
        << offsetof(GameplayTooltipState, draw_commands)
        << ",\"tooltip_scheduled_mode\":"
        << offsetof(GameplayTooltipState, scheduled_mode)
        << ",\"tooltip_cursor_x\":"
        << offsetof(GameplayTooltipState, cursor_x)
        << ",\"tooltip_cursor_y\":"
        << offsetof(GameplayTooltipState, cursor_y)
        << ",\"tooltip_current_unit_type\":"
        << offsetof(GameplayTooltipState, current_unit_type)
        << ",\"tooltip_current_object_id\":"
        << offsetof(GameplayTooltipState, current_object_id)
        << ",\"tooltip_current_payload\":"
        << offsetof(GameplayTooltipState, current_payload)
        << ",\"tooltip_hover_flags\":"
        << offsetof(GameplayTooltipState, hover_flags)
        << ",\"tooltip_current_text\":"
        << offsetof(GameplayTooltipState, current_text)
        << ",\"tooltip_secondary_text\":"
        << offsetof(GameplayTooltipState, secondary_text)
        << ",\"tooltip_requirement_text\":"
        << offsetof(GameplayTooltipState, requirement_text)
        << ",\"tooltip_cost_row_text\":"
        << offsetof(GameplayTooltipState, cost_row_text)
        << ",\"tooltip_size\":" << sizeof(GameplayTooltipState)
        << ",\"tooltip_command_size\":"
        << sizeof(GameplayTooltipDrawCommand)
        << ",\"tooltip_command_kind\":"
        << offsetof(GameplayTooltipDrawCommand, kind)
        << ",\"tooltip_command_value\":"
        << offsetof(GameplayTooltipDrawCommand, value)
        << ",\"tooltip_command_text\":"
        << offsetof(GameplayTooltipDrawCommand, text)
        << ",\"overlay_hover_context\":"
        << offsetof(UiOverlayState, hover_context)
        << ",\"overlay_mouse_x\":" << offsetof(UiOverlayState, mouse_x)
        << ",\"overlay_mouse_y\":" << offsetof(UiOverlayState, mouse_y)
        << ",\"overlay_camera_scroll_dirty\":"
        << offsetof(UiOverlayState, camera_scroll_dirty)
        << ",\"overlay_camera_edge_pointer_valid\":"
        << offsetof(UiOverlayState, camera_edge_pointer_valid)
        << ",\"overlay_camera_scroll_ramp\":"
        << offsetof(UiOverlayState, camera_scroll_ramp)
        << ",\"overlay_camera_edge_cursor_index\":"
        << offsetof(UiOverlayState, camera_edge_cursor_index)
        << ",\"overlay_hover_size\":" << sizeof(UiOverlayHoverContext)
        << ",\"overlay_hover_kind\":"
        << offsetof(UiOverlayHoverContext, kind)
        << ",\"overlay_hover_item\":"
        << offsetof(UiOverlayHoverContext, item_id)
        << ",\"overlay_hover_unit\":"
        << offsetof(UiOverlayHoverContext, unit_id)
        << ",\"overlay_hover_x\":" << offsetof(UiOverlayHoverContext, x)
        << ",\"overlay_hover_y\":" << offsetof(UiOverlayHoverContext, y)
        << ",\"overlay_progress_commands\":"
        << offsetof(UiOverlayState, progress_commands)
        << ",\"overlay_text_commands\":"
        << offsetof(UiOverlayState, text_commands)
        << ",\"overlay_queued_records\":"
        << offsetof(UiOverlayState, queued_records)
        << ",\"overlay_record_size\":" << sizeof(UiOverlayDrawRecord)
        << ",\"overlay_dispatched_records\":"
        << offsetof(UiOverlayState, dispatched_records)
        << ",\"overlay_icon_blit_requests\":"
        << offsetof(UiOverlayState, icon_blit_requests)
        << ",\"overlay_icon_blit_size\":"
        << sizeof(ranker::UiOverlayIconBlitRequest)
        << ",\"overlay_text_command_flushed\":"
        << offsetof(UiOverlayState, text_command_flushed)
        << ",\"overlay_progress_command_flushed\":"
        << offsetof(UiOverlayState, progress_command_flushed)
        << ",\"overlay_emit_sprite_draws\":"
        << offsetof(UiOverlayState, emit_sprite_draws)
        << ",\"overlay_text_size\":" << sizeof(UiOverlayTextCommand)
        << ",\"overlay_text_text\":"
        << offsetof(UiOverlayTextCommand, text)
        << ",\"overlay_text_x\":" << offsetof(UiOverlayTextCommand, x)
        << ",\"overlay_text_y\":" << offsetof(UiOverlayTextCommand, y)
        << ",\"overlay_text_color\":"
        << offsetof(UiOverlayTextCommand, color)
        << ",\"overlay_text_draw_font\":"
        << offsetof(UiOverlayTextCommand, draw_font)
        << ",\"overlay_text_metric_font\":"
        << offsetof(UiOverlayTextCommand, metric_font)
        << ",\"overlay_text_centered\":"
        << offsetof(UiOverlayTextCommand, centered)
        << ",\"overlay_text_right_aligned\":"
        << offsetof(UiOverlayTextCommand, right_aligned)
        << ",\"overlay_text_bottom_aligned\":"
        << offsetof(UiOverlayTextCommand, bottom_aligned)
        << ",\"overlay_progress_size\":"
        << sizeof(UiOverlayProgressCommand)
        << ",\"overlay_progress_left\":"
        << offsetof(UiOverlayProgressCommand, left)
        << ",\"overlay_progress_top\":"
        << offsetof(UiOverlayProgressCommand, top)
        << ",\"overlay_progress_right\":"
        << offsetof(UiOverlayProgressCommand, right)
        << ",\"overlay_progress_bottom\":"
        << offsetof(UiOverlayProgressCommand, bottom)
        << ",\"overlay_progress_numerator\":"
        << offsetof(UiOverlayProgressCommand, numerator)
        << ",\"overlay_progress_denominator\":"
        << offsetof(UiOverlayProgressCommand, denominator)
        << ",\"overlay_size\":" << sizeof(UiOverlayState)
        << ",\"overlay_command_options\":"
        << offsetof(UiOverlayState, command_options)
        << ",\"overlay_hot_regions\":"
        << offsetof(UiOverlayState, hot_regions)
        << ",\"overlay_selected_unit_ids\":"
        << offsetof(UiOverlayState, selected_unit_ids)
        << ",\"overlay_screen_width\":"
        << offsetof(UiOverlayState, screen_width)
        << ",\"overlay_screen_height\":"
        << offsetof(UiOverlayState, screen_height)
        << ",\"overlay_interface_theme_index\":"
        << offsetof(UiOverlayState, interface_theme_index)
        << ",\"overlay_minimap\":" << offsetof(UiOverlayState, minimap)
        << ",\"overlay_local_player_slot\":"
        << offsetof(UiOverlayState, local_player_slot)
        << ",\"overlay_camera_x\":" << offsetof(UiOverlayState, camera_x)
        << ",\"overlay_camera_y\":" << offsetof(UiOverlayState, camera_y)
        << ",\"overlay_camera_max_x\":"
        << offsetof(UiOverlayState, camera_max_x)
        << ",\"overlay_camera_max_y\":"
        << offsetof(UiOverlayState, camera_max_y)
        << ",\"overlay_placement_mode\":"
        << offsetof(UiOverlayState, placement_mode)
        << ",\"overlay_placement_definition\":"
        << offsetof(UiOverlayState, placement_definition_id)
        << ",\"overlay_placement_pointer_x\":"
        << offsetof(UiOverlayState, placement_pointer_x)
        << ",\"overlay_placement_pointer_y\":"
        << offsetof(UiOverlayState, placement_pointer_y)
        << ",\"overlay_placement_footprint_width\":"
        << offsetof(UiOverlayState, placement_footprint_width_tiles)
        << ",\"overlay_placement_footprint_height\":"
        << offsetof(UiOverlayState, placement_footprint_height_tiles)
        << ",\"overlay_placement_preview_valid\":"
        << offsetof(UiOverlayState, placement_preview_valid)
        << ",\"overlay_placement_cell_validity\":"
        << offsetof(UiOverlayState, placement_preview_cell_validity)
        << ",\"overlay_resource_amount\":"
        << offsetof(UiOverlayState, resource_amount)
        << ",\"overlay_resource_counter_x\":"
        << offsetof(UiOverlayState, resource_counter_x)
        << ",\"overlay_resource_counter_y\":"
        << offsetof(UiOverlayState, resource_counter_y)
        << ",\"overlay_population_counter_x\":"
        << offsetof(UiOverlayState, population_counter_x)
        << ",\"overlay_population_counter_y\":"
        << offsetof(UiOverlayState, population_counter_y)
        << ",\"overlay_population_used\":"
        << offsetof(UiOverlayState, population_used)
        << ",\"overlay_population_available\":"
        << offsetof(UiOverlayState, population_available)
        << ",\"overlay_selected_unit_id\":"
        << offsetof(UiOverlayState, selected_unit_id)
        << ",\"overlay_selected_unit_type\":"
        << offsetof(UiOverlayState, selected_unit_type)
        << ",\"overlay_selected_unit_owner\":"
        << offsetof(UiOverlayState, selected_unit_owner)
        << ",\"overlay_selected_unit_count\":"
        << offsetof(UiOverlayState, selected_unit_count)
        << ",\"overlay_selected_unit_command_mask\":"
        << offsetof(UiOverlayState, selected_unit_command_bit_mask)
        << ",\"overlay_selected_unit_name_text\":"
        << offsetof(UiOverlayState, selected_unit_name_text)
        << ",\"overlay_selected_production_category\":"
        << offsetof(UiOverlayState, selected_production_category)
        << ",\"command_option_size\":" << sizeof(UiOverlayCommandOption)
        << ",\"command_option_item\":"
        << offsetof(UiOverlayCommandOption, item_id)
        << ",\"command_option_aux\":"
        << offsetof(UiOverlayCommandOption, aux)
        << ",\"command_option_flags\":"
        << offsetof(UiOverlayCommandOption, flags)
        << ",\"command_option_icon\":"
        << offsetof(UiOverlayCommandOption, icon_marker)
        << ",\"command_option_hotkey\":"
        << offsetof(UiOverlayCommandOption, hotkey)
        << ",\"command_option_enabled\":"
        << offsetof(UiOverlayCommandOption, enabled)
        << ",\"hot_region_size\":" << sizeof(UiOverlayHotRegion)
        << ",\"hot_region_record\":"
        << offsetof(UiOverlayHotRegion, record)
        << ",\"hot_region_hotkey\":"
        << offsetof(UiOverlayHotRegion, hotkey)
        << ",\"hot_region_enabled\":"
        << offsetof(UiOverlayHotRegion, enabled)
        << ",\"draw_record_item\":"
        << offsetof(UiOverlayDrawRecord, item_id)
        << ",\"draw_record_aux\":" << offsetof(UiOverlayDrawRecord, aux)
        << ",\"draw_record_flags\":" << offsetof(UiOverlayDrawRecord, flags)
        << ",\"draw_record_x\":" << offsetof(UiOverlayDrawRecord, x)
        << ",\"draw_record_y\":" << offsetof(UiOverlayDrawRecord, y)
        << ",\"draw_record_width\":" << offsetof(UiOverlayDrawRecord, width)
        << ",\"draw_record_height\":" << offsetof(UiOverlayDrawRecord, height)
        << ",\"draw_record_icon\":"
        << offsetof(UiOverlayDrawRecord, icon_marker)
        << ",\"minimap_output_x\":" << offsetof(MinimapRenderState, output_x)
        << ",\"minimap_output_y\":" << offsetof(MinimapRenderState, output_y)
        << ",\"minimap_output_width\":"
        << offsetof(MinimapRenderState, output_width_pixels)
        << ",\"minimap_output_height\":"
        << offsetof(MinimapRenderState, output_height_pixels)
        << ",\"minimap_width\":"
        << offsetof(MinimapRenderState, minimap_width_pixels)
        << ",\"minimap_height\":"
        << offsetof(MinimapRenderState, minimap_height_pixels)
        << ",\"overlay_minimap_units\":"
        << offsetof(UiOverlayState, minimap_units)
        << ",\"overlay_minimap_markers\":"
        << offsetof(UiOverlayState, minimap_markers)
        << ",\"minimap_marker_size\":" << sizeof(UiOverlayMinimapMarker)
        << ",\"minimap_marker_kind\":"
        << offsetof(UiOverlayMinimapMarker, kind)
        << ",\"minimap_marker_x\":" << offsetof(UiOverlayMinimapMarker, x)
        << ",\"minimap_marker_y\":" << offsetof(UiOverlayMinimapMarker, y)
        << ",\"minimap_marker_width\":"
        << offsetof(UiOverlayMinimapMarker, width)
        << ",\"minimap_marker_height\":"
        << offsetof(UiOverlayMinimapMarker, height)
        << ",\"minimap_marker_color\":"
        << offsetof(UiOverlayMinimapMarker, color)
        << ",\"minimap_marker_item\":"
        << offsetof(UiOverlayMinimapMarker, item_id)
        << ",\"minimap_marker_owner\":"
        << offsetof(UiOverlayMinimapMarker, owner_id)
        << ",\"minimap_marker_valid\":"
        << offsetof(UiOverlayMinimapMarker, valid)
        << ",\"overlay_minimap_owner_colors\":"
        << offsetof(UiOverlayState, minimap_owner_colors)
        << ",\"overlay_minimap_owner_footprint_colors\":"
        << offsetof(UiOverlayState, minimap_owner_footprint_colors)
        << ",\"minimap_unit_size\":" << sizeof(UiOverlayMinimapUnit)
        << ",\"minimap_unit_id\":"
        << offsetof(UiOverlayMinimapUnit, unit_id)
        << ",\"minimap_unit_type\":"
        << offsetof(UiOverlayMinimapUnit, type_id)
        << ",\"minimap_unit_owner\":"
        << offsetof(UiOverlayMinimapUnit, owner_id)
        << ",\"minimap_unit_runtime_flags\":"
        << offsetof(UiOverlayMinimapUnit, runtime_flags)
        << ",\"minimap_unit_score\":"
        << offsetof(UiOverlayMinimapUnit, selection_score)
        << ",\"minimap_unit_world_x\":"
        << offsetof(UiOverlayMinimapUnit, world_x)
        << ",\"minimap_unit_world_y\":"
        << offsetof(UiOverlayMinimapUnit, world_y)
        << ",\"minimap_unit_bounds_left\":"
        << offsetof(UiOverlayMinimapUnit, bounds_left)
        << ",\"minimap_unit_bounds_top\":"
        << offsetof(UiOverlayMinimapUnit, bounds_top)
        << ",\"minimap_unit_bounds_width\":"
        << offsetof(UiOverlayMinimapUnit, bounds_width)
        << ",\"minimap_unit_bounds_height\":"
        << offsetof(UiOverlayMinimapUnit, bounds_height)
        << ",\"minimap_unit_footprint_width\":"
        << offsetof(UiOverlayMinimapUnit, footprint_width_tiles)
        << ",\"minimap_unit_footprint_height\":"
        << offsetof(UiOverlayMinimapUnit, footprint_height_tiles)
        << ",\"minimap_unit_visible\":"
        << offsetof(UiOverlayMinimapUnit, visible_to_local_player)
        << ",\"minimap_unit_hidden\":"
        << offsetof(UiOverlayMinimapUnit, hidden_from_minimap)
        << ",\"minimap_unit_special_visibility\":"
        << offsetof(UiOverlayMinimapUnit, special_visibility_gate_passed)
        << ",\"movement_map\":" << offsetof(UnitMovementContext, map)
        << ",\"movement_active_units\":"
        << offsetof(UnitMovementContext, active_units)
        << ",\"movement_free_units\":"
        << offsetof(UnitMovementContext, free_units)
        << ",\"movement_lifecycle_units\":"
        << offsetof(UnitMovementContext, lifecycle_units)
        << ",\"movement_context_size\":" << sizeof(UnitMovementContext)
        << ",\"movement_direction_lookup_8\":"
        << offsetof(UnitMovementContext, direction_lookup_8)
        << ",\"movement_map_width\":" << offsetof(UnitMovementMap, width)
        << ",\"movement_map_height\":" << offsetof(UnitMovementMap, height)
        << ",\"movement_map_stride\":"
        << offsetof(UnitMovementMap, stride_tiles)
        << ",\"movement_map_cells\":" << offsetof(UnitMovementMap, cells)
        << ",\"movement_map_size\":" << sizeof(UnitMovementMap)
        << ",\"movement_cell_size\":" << sizeof(UnitMovementCell)
        << ",\"movement_cell_flags\":" << offsetof(UnitMovementCell, flags)
        << ",\"unit_size\":" << sizeof(UnitMovementUnit)
        << ",\"queued_command_size\":" << sizeof(UnitQueuedCommand)
        << ",\"queued_command_state\":"
        << offsetof(UnitQueuedCommand, state)
        << ",\"queued_command_x\":" << offsetof(UnitQueuedCommand, x)
        << ",\"queued_command_y\":" << offsetof(UnitQueuedCommand, y)
        << ",\"queued_command_value\":" << offsetof(UnitQueuedCommand, value)
        << ",\"unit_id\":" << offsetof(UnitMovementUnit, id)
        << ",\"unit_runtime_slot\":"
        << offsetof(UnitMovementUnit, runtime_slot_index)
        << ",\"unit_type\":" << offsetof(UnitMovementUnit, type_id)
        << ",\"unit_owner\":" << offsetof(UnitMovementUnit, owner_id)
        << ",\"unit_type_flags\":" << offsetof(UnitMovementUnit, type_flags)
        << ",\"unit_command_state\":"
        << offsetof(UnitMovementUnit, command_state)
        << ",\"unit_command_flags\":"
        << offsetof(UnitMovementUnit, command_flags)
        << ",\"unit_runtime_flags\":"
        << offsetof(UnitMovementUnit, runtime_flags)
        << ",\"unit_draw_flags\":"
        << offsetof(UnitMovementUnit, draw_flags)
        << ",\"unit_command_lockout\":"
        << offsetof(UnitMovementUnit, command_lockout_ticks)
        << ",\"unit_action_mode\":" << offsetof(UnitMovementUnit, action_mode)
        << ",\"unit_action_mode_gate\":"
        << offsetof(UnitMovementUnit, action_mode_gate)
        << ",\"unit_command_value\":"
        << offsetof(UnitMovementUnit, command_value)
        << ",\"unit_spawn_type\":" << offsetof(UnitMovementUnit, spawn_type_id)
        << ",\"unit_queued_type\":"
        << offsetof(UnitMovementUnit, queued_production_type_id)
        << ",\"unit_x\":" << offsetof(UnitMovementUnit, x)
        << ",\"unit_y\":" << offsetof(UnitMovementUnit, y)
        << ",\"unit_destination_x\":"
        << offsetof(UnitMovementUnit, destination_x)
        << ",\"unit_destination_y\":"
        << offsetof(UnitMovementUnit, destination_y)
        << ",\"unit_path_target_x\":"
        << offsetof(UnitMovementUnit, path_target_x)
        << ",\"unit_path_target_y\":"
        << offsetof(UnitMovementUnit, path_target_y)
        << ",\"unit_current_cell_x\":"
        << offsetof(UnitMovementUnit, current_cell_x)
        << ",\"unit_current_cell_y\":"
        << offsetof(UnitMovementUnit, current_cell_y)
        << ",\"unit_next_path_x\":"
        << offsetof(UnitMovementUnit, next_path_x)
        << ",\"unit_next_path_y\":"
        << offsetof(UnitMovementUnit, next_path_y)
        << ",\"unit_movement_flags\":"
        << offsetof(UnitMovementUnit, movement_flags)
        << ",\"unit_animation_frame\":"
        << offsetof(UnitMovementUnit, animation_frame)
        << ",\"unit_cargo\":" << offsetof(UnitMovementUnit, cargo_amount)
        << ",\"unit_cargo_capacity\":"
        << offsetof(UnitMovementUnit, cargo_capacity)
        << ",\"unit_harvest_tile\":"
        << offsetof(UnitMovementUnit, harvest_tile_index)
        << ",\"unit_work_timer\":" << offsetof(UnitMovementUnit, work_timer)
        << ",\"unit_max_health\":" << offsetof(UnitMovementUnit, max_health)
        << ",\"unit_health\":" << offsetof(UnitMovementUnit, health)
        << ",\"unit_active_payload\":"
        << offsetof(UnitMovementUnit, active_command_payload)
        << ",\"unit_deferred_commands\":"
        << offsetof(UnitMovementUnit, deferred_commands)
        << ",\"unit_deferred_count\":"
        << offsetof(UnitMovementUnit, deferred_command_count)
        << ",\"unit_target\":" << offsetof(UnitMovementUnit, target)
        << ",\"unit_linked_unit\":" << offsetof(UnitMovementUnit, linked_unit)
        << ",\"unit_definition\":" << offsetof(UnitMovementUnit, definition)
        << ",\"unit_active\":" << offsetof(UnitMovementUnit, active)
        << ",\"unit_attached\":"
        << offsetof(UnitMovementUnit, attached_to_parent)
        << ",\"unit_footprint_registered\":"
        << offsetof(UnitMovementUnit, footprint_registered)
        << ",\"unit_under_construction\":"
        << offsetof(UnitMovementUnit, under_construction)
        << ",\"unit_production_reserved\":"
        << offsetof(UnitMovementUnit, production_reserved)
        << ",\"unit_render_queue_units\":"
        << offsetof(UnitRenderQueueContext, units)
        << ",\"unit_render_item_size\":" << sizeof(UnitRenderItem)
        << ",\"unit_render_item_type\":"
        << offsetof(UnitRenderItem, type_id)
        << ",\"unit_render_item_owner\":"
        << offsetof(UnitRenderItem, owner_id)
        << ",\"unit_render_item_runtime_slot\":"
        << offsetof(UnitRenderItem, runtime_slot_index)
        << ",\"unit_render_item_draw_flags\":"
        << offsetof(UnitRenderItem, draw_flags)
        << ",\"unit_render_item_max_health\":"
        << offsetof(UnitRenderItem, max_hit_points)
        << ",\"unit_render_item_health\":"
        << offsetof(UnitRenderItem, hit_points)
        << ",\"unit_render_item_stage_count\":"
        << offsetof(UnitRenderItem, construction_stage_count)
        << ",\"unit_render_item_progress\":"
        << offsetof(UnitRenderItem, construction_progress)
        << ",\"unit_render_item_progress_limit\":"
        << offsetof(UnitRenderItem, construction_progress_limit)
        << ",\"unit_render_item_progress_active\":"
        << offsetof(UnitRenderItem, cell_construction_progress_active)
        << ",\"map_effect_context_size\":" << sizeof(MapEffectContext)
        << ",\"map_effect_context_effects\":"
        << offsetof(MapEffectContext, effects)
        << ",\"map_effect_context_active_indices\":"
        << offsetof(MapEffectContext, active_effect_indices)
        << ",\"map_effect_context_free_indices\":"
        << offsetof(MapEffectContext, free_effect_indices)
        << ",\"map_effect_context_frame\":"
        << offsetof(MapEffectContext, frame_counter)
        << ",\"map_effect_instance_size\":" << sizeof(MapEffectInstance)
        << ",\"map_effect_instance_active\":"
        << offsetof(MapEffectInstance, active)
        << ",\"map_effect_instance_id\":"
        << offsetof(MapEffectInstance, id)
        << ",\"map_effect_instance_effect_id\":"
        << offsetof(MapEffectInstance, effect_id)
        << ",\"map_effect_instance_flags\":"
        << offsetof(MapEffectInstance, flags)
        << ",\"map_effect_instance_frame_timer\":"
        << offsetof(MapEffectInstance, frame_timer)
        << ",\"map_effect_instance_repeat_count\":"
        << offsetof(MapEffectInstance, repeat_count)
        << ",\"map_effect_instance_x\":"
        << offsetof(MapEffectInstance, x)
        << ",\"map_effect_instance_y\":"
        << offsetof(MapEffectInstance, y)
        << ",\"map_effect_instance_linked_unit\":"
        << offsetof(MapEffectInstance, linked_unit)
        << ",\"definition_production_cycle_period\":"
        << offsetof(UnitMovementDefinition, production_cycle_period)
        << ",\"definition_lifecycle_class\":"
        << offsetof(UnitMovementDefinition, lifecycle_class)
        << ",\"definition_transport_offset_x\":"
        << offsetof(UnitMovementDefinition, transport_offset_x)
        << ",\"definition_transport_offset_y\":"
        << offsetof(UnitMovementDefinition, transport_offset_y)
        << ",\"definition_passive_recovery_enabled\":"
        << offsetof(UnitMovementDefinition, passive_recovery_enabled)
        << ",\"definition_passive_recovery_flags\":"
        << offsetof(UnitMovementDefinition, passive_recovery_flags)
        << ",\"definition_passive_map_effect_seed\":"
        << offsetof(UnitMovementDefinition, passive_map_effect_seed)
        << ",\"definition_production_spawn_time\":"
        << offsetof(UnitMovementDefinition, production_spawn_time)
        << ",\"definition_footprint_width\":"
        << offsetof(UnitMovementDefinition, footprint_width_tiles)
        << ",\"definition_footprint_height\":"
        << offsetof(UnitMovementDefinition, footprint_height_tiles)
        << ",\"lifecycle_unit_kills\":"
        << offsetof(UnitLifecycleContext, owner_unit_kill_count)
        << ",\"lifecycle_building_kills\":"
        << offsetof(UnitLifecycleContext, owner_building_kill_count)
        << ",\"lifecycle_primary\":"
        << offsetof(UnitLifecycleContext, owner_primary_resources)
        << ",\"lifecycle_secondary\":"
        << offsetof(UnitLifecycleContext, owner_secondary_resources)
        << ",\"lifecycle_population_limit\":"
        << offsetof(UnitLifecycleContext, owner_population_limit)
        << ",\"lifecycle_population_used\":"
        << offsetof(UnitLifecycleContext, owner_population_used)
        << ",\"lifecycle_population_reserved\":"
        << offsetof(UnitLifecycleContext, owner_population_reserved)
        << ",\"lifecycle_unit_lost\":"
        << offsetof(UnitLifecycleContext, owner_unit_lost_count)
        << ",\"lifecycle_size\":" << sizeof(UnitLifecycleContext)
        << "}\n";
    return 0;
}
