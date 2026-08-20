param(
    [string]$RepositoryRoot = '',
    [string]$Executable = 'RankerOCPV_Win\ranker_rebuild.exe',
    [string]$LayoutProbe = 'artifacts\runtime_globals_layout_probe.exe',
    [string]$OutputPath = 'artifacts\current_layout.json',
    [switch]$AllowExactExecutableLayoutReuse
)

$ErrorActionPreference = 'Stop'
$toolDirectory = $PSScriptRoot
$repositoryRoot = if ($RepositoryRoot) {
    (Resolve-Path -LiteralPath $RepositoryRoot).Path
} else {
    (Resolve-Path -LiteralPath (Join-Path $toolDirectory '..\..\..')).Path
}
$executablePath = if ([IO.Path]::IsPathRooted($Executable)) {
    $Executable
} else {
    Join-Path $repositoryRoot $Executable
}
$probePath = if ([IO.Path]::IsPathRooted($LayoutProbe)) {
    $LayoutProbe
} else {
    Join-Path $toolDirectory $LayoutProbe
}
$exe = (Resolve-Path -LiteralPath $executablePath).Path
$probe = (Resolve-Path -LiteralPath $probePath).Path
$nmCommand = Get-Command nm.exe -ErrorAction SilentlyContinue
$nm = if ($nmCommand) { $nmCommand.Source } else {
    $compilerRoot = Join-Path $env:LOCALAPPDATA `
        'Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe'
    Get-ChildItem $compilerRoot -Recurse -Filter nm.exe -ErrorAction Stop |
        Select-Object -First 1 -ExpandProperty FullName
}
$symbols = & $nm -C $exe

function Symbol-Rva([string]$Pattern) {
    $line = $symbols | Where-Object { $_ -match $Pattern } | Select-Object -First 1
    if (-not $line -or $line -notmatch '^([0-9a-fA-F]+)\s') {
        throw "Cannot resolve symbol pattern: $Pattern"
    }
    return [Convert]::ToInt64($Matches[1], 16) - 0x140000000
}

$codeRoot = Join-Path $repositoryRoot 'ranker_reconstructed_code'
$probeSource = Join-Path $toolDirectory 'runtime_globals_layout_probe.cpp'
$gameSources = @(Get-ChildItem `
    (Join-Path $codeRoot 'src'),(Join-Path $codeRoot 'include') -File)
$layoutSources = @($gameSources)
if (Test-Path -LiteralPath $probeSource) {
    $layoutSources += Get-Item -LiteralPath $probeSource
}
$newestLayoutSource = $layoutSources |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
$newestGameSource = $gameSources |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
$probeItem = Get-Item $probe
if (-not $AllowExactExecutableLayoutReuse -and
    $probeItem.LastWriteTime -lt $newestLayoutSource.LastWriteTime) {
    throw "Layout probe is stale; rebuild it after $($newestLayoutSource.Name)."
}
$exeItem = Get-Item $exe
if (-not $AllowExactExecutableLayoutReuse -and
    $exeItem.LastWriteTime -lt $newestGameSource.LastWriteTime) {
    throw "Executable is stale; rebuild and redeploy it after $($newestGameSource.Name)."
}
$layout = (& $probe | ConvertFrom-Json)
if ([int64]$layout.pointer_size -ne 8 -or
    [int64]$layout.bool_size -ne 1 -or
    [int64]$layout.vector_header_size -ne 24) {
    throw ('Unsupported probe ABI: pointer={0}, bool={1}, vector={2}' -f
        $layout.pointer_size, $layout.bool_size, $layout.vector_header_size)
}

$result = [pscustomobject]@{
    executable = $exe
    sha256 = (Get-FileHash $exe -Algorithm SHA256).Hash
    abi_layout = [ordered]@{
        pointer_size = ('0x{0:X}' -f [int64]$layout.pointer_size)
        bool_size = ('0x{0:X}' -f [int64]$layout.bool_size)
        vector_header_size = ('0x{0:X}' -f [int64]$layout.vector_header_size)
    }
    runtime_rva = ('0x{0:X}' -f (Symbol-Rva '::g_runtime$'))
    overlay_rva = ('0x{0:X}' -f (Symbol-Rva '::g_ui_overlay_state$'))
    loop_rva = ('0x{0:X}' -f (Symbol-Rva '::g_gameplay_loop_state$'))
    cursor_rva = ('0x{0:X}' -f (Symbol-Rva '::g_cursor_state$'))
    input_state_rva = ('0x{0:X}' -f (Symbol-Rva '::g_input_state$'))
    programmatic_pointer_motion_pending_rva = ('0x{0:X}' -f
        (Symbol-Rva '::g_programmatic_pointer_motion_pending$'))
    programmatic_pointer_motion_target_reached_rva = ('0x{0:X}' -f
        (Symbol-Rva '::g_programmatic_pointer_motion_target_reached$'))
    programmatic_pointer_motion_x_rva = ('0x{0:X}' -f
        (Symbol-Rva '::g_programmatic_pointer_motion_x$'))
    programmatic_pointer_motion_y_rva = ('0x{0:X}' -f
        (Symbol-Rva '::g_programmatic_pointer_motion_y$'))
    gameplay_input_rva = ('0x{0:X}' -f
        (Symbol-Rva '::g_gameplay_input_action_state$'))
    tooltip_rva = ('0x{0:X}' -f (Symbol-Rva '::g_gameplay_tooltip_state$'))
    direct_sound_state_rva = ('0x{0:X}' -f
        (Symbol-Rva '::g_direct_sound_state$'))
    palette_rva = ('0x{0:X}' -f (Symbol-Rva '::g_palette_cache_state$'))
    sprite_render_state_rva = ('0x{0:X}' -f
        (Symbol-Rva '::g_sprite_render_state$'))
    render_gameplay_frame_composite_rva = ('0x{0:X}' -f
        (Symbol-Rva 'ranker::RenderGameplayFrameComposite\(ranker::GameplayFrameRenderContext&\)$'))
    sync_gameplay_visibility_rva = ('0x{0:X}' -f
        (Symbol-Rva '::sync_default_gameplay_visibility_and_render_inputs\(unsigned int, bool\)$'))
    apply_linked_unit_visibility_rva = ('0x{0:X}' -f
        (Symbol-Rva 'ranker::ApplyLinkedUnitVisibilityFromSource\(ranker::GameplayVisibilityContext&, ranker::GameplayVisibilityUnit const&, ranker::GameplayVisibilityUnit const&\)$'))
    draw_resource_sprite_mode_rva = ('0x{0:X}' -f
        (Symbol-Rva 'ranker::DrawResourceSpriteMode\(unsigned int, int, int, unsigned int\)$'))
    draw_resource_sprite_token1_shadow_rva = ('0x{0:X}' -f
        (Symbol-Rva 'ranker::DrawResourceSpriteToken1Shadow\(unsigned int, int, int\)$'))
    draw_resource_sprite_unit_ramp_token1_shadow_rva = ('0x{0:X}' -f
        (Symbol-Rva 'ranker::DrawResourceSpriteUnitRampToken1Shadow\(unsigned int, int, int\)$'))
    draw_sprite_render_target_line16_rva = ('0x{0:X}' -f
        (Symbol-Rva 'ranker::DrawSpriteRenderTargetLine16\(ranker::SpriteRenderTarget const&, int, int, int, int, unsigned short\)$'))
    world_render_checkpoint_rva = ('0x{0:X}' -f
        (Symbol-Rva '::default_gameplay_frame_draw_selection_overlay\(ranker::GameplayFrameRenderContext&\)$'))
    frame_random_offset = ('0x{0:X}' -f [int64]$layout.frame_random)
    gameplay_sound_offset = ('0x{0:X}' -f [int64]$layout.gameplay_sound)
    gameplay_sound_layout = [ordered]@{
        variant_seed = ('0x{0:X}' -f
            [int64]$layout.gameplay_sound_variant_seed)
    }
    visibility_offset = ('0x{0:X}' -f [int64]$layout.visibility)
    movement_offset = ('0x{0:X}' -f [int64]$layout.movement)
    lifecycle_offset = ('0x{0:X}' -f [int64]$layout.lifecycle)
    production_runtime_offset = ('0x{0:X}' -f [int64]$layout.production_runtime)
    active_session_definitions_offset = ('0x{0:X}' -f
        [int64]$layout.active_session_definitions)
    unit_reference_tables_offset = ('0x{0:X}' -f
        [int64]$layout.unit_reference_tables)
    unit_reference_completion_reverse_offset = ('0x{0:X}' -f
        [int64]$layout.unit_reference_completion_reverse)
    session_definition_layout = [ordered]@{
        unit_records = ('0x{0:X}' -f
            [int64]$layout.session_definition_unit_records)
        record_size = ('0x{0:X}' -f
            [int64]$layout.runtime_definition_record_size)
        record_bytes = ('0x{0:X}' -f
            [int64]$layout.runtime_definition_record_bytes)
    }
    unit_render_queue_offset = ('0x{0:X}' -f [int64]$layout.unit_render_queue)
    render_command_queue_offset = ('0x{0:X}' -f [int64]$layout.render_command_queue)
    map_effect_context_offset = ('0x{0:X}' -f [int64]$layout.map_effect_context)
    player_slots_offset = ('0x{0:X}' -f [int64]$layout.player_slots)
    owner_ai_offset = ('0x{0:X}' -f [int64]$layout.owner_ai)
    owner_transport_routes_offset = ('0x{0:X}' -f
        [int64]$layout.owner_transport_routes)
    owner_strategic_targets_offset = ('0x{0:X}' -f
        [int64]$layout.owner_strategic_targets)
    owner_ai_reserved_primary_cost_offset = ('0x{0:X}' -f
        [int64]$layout.owner_ai_reserved_primary_cost)
    unit_effects_offset = ('0x{0:X}' -f [int64]$layout.unit_effects)
    unit_commands_offset = ('0x{0:X}' -f [int64]$layout.unit_commands)
    owner_counters_offset = ('0x{0:X}' -f [int64]$layout.owner_counters)
    owner_counter_layout = [ordered]@{
        resource_score_table = ('0x{0:X}' -f [int64]$layout.owner_counter_table6)
        size = ('0x{0:X}' -f [int64]$layout.owner_counter_size)
    }
    player_slots_layout = [ordered]@{
        size = ('0x{0:X}' -f [int64]$layout.player_slots_size)
        slot_states = ('0x{0:X}' -f [int64]$layout.player_slots_slot_states)
        relation_masks = ('0x{0:X}' -f [int64]$layout.player_slots_relation_masks)
        visibility_masks = ('0x{0:X}' -f [int64]$layout.player_slots_visibility_masks)
        start_x = ('0x{0:X}' -f [int64]$layout.player_slots_start_x)
        start_y = ('0x{0:X}' -f [int64]$layout.player_slots_start_y)
        nearest_hostile = ('0x{0:X}' -f [int64]$layout.player_slots_nearest_hostile)
        active_count = ('0x{0:X}' -f [int64]$layout.player_slots_active_count)
        local_player = ('0x{0:X}' -f [int64]$layout.player_slots_local_player)
        lobby_states = ('0x{0:X}' -f [int64]$layout.player_slots_lobby_states)
    }
    owner_ai_layout = [ordered]@{
        state_size = ('0x{0:X}' -f [int64]$layout.owner_ai_state_size)
        owners = ('0x{0:X}' -f [int64]$layout.owner_ai_owners)
        owner_faction_ids = ('0x{0:X}' -f [int64]$layout.owner_ai_owner_faction_ids)
        owner_unit_type_counts = ('0x{0:X}' -f
            [int64]$layout.owner_ai_owner_unit_type_counts)
        shared_grid_table = ('0x{0:X}' -f [int64]$layout.owner_ai_shared_grid_table)
        owner_population_used = ('0x{0:X}' -f
            [int64]$layout.owner_ai_owner_population_used)
        owner_population_reserved = ('0x{0:X}' -f
            [int64]$layout.owner_ai_owner_population_reserved)
        slot_size = ('0x{0:X}' -f [int64]$layout.owner_ai_slot_size)
        primary_target_owner = ('0x{0:X}' -f [int64]$layout.owner_ai_primary_target_owner)
        support_target_owner = ('0x{0:X}' -f [int64]$layout.owner_ai_support_target_owner)
        support_mode = ('0x{0:X}' -f [int64]$layout.owner_ai_support_mode)
        support_anchor = ('0x{0:X}' -f [int64]$layout.owner_ai_support_anchor)
        resource_budget_percent = ('0x{0:X}' -f
            [int64]$layout.owner_ai_resource_budget_percent)
        profile_counter = ('0x{0:X}' -f [int64]$layout.owner_ai_profile_counter)
        production_pause_flag = ('0x{0:X}' -f
            [int64]$layout.owner_ai_production_pause_flag)
        unit_demand = ('0x{0:X}' -f [int64]$layout.owner_ai_unit_demand)
        unit_demand_shadow = ('0x{0:X}' -f [int64]$layout.owner_ai_unit_demand_shadow)
        route_radius = ('0x{0:X}' -f [int64]$layout.owner_ai_route_radius)
        primary_target_point = ('0x{0:X}' -f [int64]$layout.owner_ai_primary_target_point)
        primary_target_flags = ('0x{0:X}' -f [int64]$layout.owner_ai_primary_target_flags)
        neutral_route_target_point = ('0x{0:X}' -f
            [int64]$layout.owner_ai_neutral_route_target_point)
        placement_record = ('0x{0:X}' -f [int64]$layout.owner_ai_placement_record)
        profile_state_flag = ('0x{0:X}' -f [int64]$layout.owner_ai_profile_state_flag)
    }
    owner_transport_route_layout = [ordered]@{
        size = ('0x{0:X}' -f [int64]$layout.owner_transport_route_size)
        route_count = ('0x{0:X}' -f [int64]$layout.owner_transport_route_count)
    }
    owner_strategic_target_layout = [ordered]@{
        size = ('0x{0:X}' -f [int64]$layout.owner_strategic_target_size)
        target_owner = ('0x{0:X}' -f [int64]$layout.owner_strategic_target_owner)
        blocked_mask = ('0x{0:X}' -f [int64]$layout.owner_strategic_blocked_mask)
        preferred_target = ('0x{0:X}' -f [int64]$layout.owner_strategic_preferred_target)
    }
    unit_effect_layout = [ordered]@{
        state_size = ('0x{0:X}' -f [int64]$layout.unit_effect_state_size)
        trail_segments = ('0x{0:X}' -f
            [int64]$layout.unit_effect_trail_segments)
        trail_segment_size = ('0x{0:X}' -f
            [int64]$layout.unit_effect_trail_segment_size)
        trail_segment_id = ('0x{0:X}' -f
            [int64]$layout.unit_effect_trail_segment_id)
        trail_segment_x0 = ('0x{0:X}' -f
            [int64]$layout.unit_effect_trail_segment_x0)
        trail_segment_y0 = ('0x{0:X}' -f
            [int64]$layout.unit_effect_trail_segment_y0)
        trail_segment_x1 = ('0x{0:X}' -f
            [int64]$layout.unit_effect_trail_segment_x1)
        trail_segment_y1 = ('0x{0:X}' -f
            [int64]$layout.unit_effect_trail_segment_y1)
        trail_segment_width = ('0x{0:X}' -f
            [int64]$layout.unit_effect_trail_segment_width)
        trail_segment_color = ('0x{0:X}' -f
            [int64]$layout.unit_effect_trail_segment_color)
        slots = ('0x{0:X}' -f [int64]$layout.unit_effect_slots)
        active_indices = ('0x{0:X}' -f [int64]$layout.unit_effect_active_indices)
        free_indices = ('0x{0:X}' -f [int64]$layout.unit_effect_free_indices)
        frame = ('0x{0:X}' -f [int64]$layout.unit_effect_frame)
        effect_size = ('0x{0:X}' -f [int64]$layout.unit_effect_size)
        active = ('0x{0:X}' -f [int64]$layout.unit_effect_active)
        id = ('0x{0:X}' -f [int64]$layout.unit_effect_id)
        flags = ('0x{0:X}' -f [int64]$layout.unit_effect_flags)
        tick = ('0x{0:X}' -f [int64]$layout.unit_effect_tick)
        effect_frame = ('0x{0:X}' -f [int64]$layout.unit_effect_frame_value)
        amount = ('0x{0:X}' -f [int64]$layout.unit_effect_amount)
        source = ('0x{0:X}' -f [int64]$layout.unit_effect_source)
        target = ('0x{0:X}' -f [int64]$layout.unit_effect_target)
        linked = ('0x{0:X}' -f [int64]$layout.unit_effect_linked)
        x = ('0x{0:X}' -f [int64]$layout.unit_effect_x)
        y = ('0x{0:X}' -f [int64]$layout.unit_effect_y)
        previous_x = ('0x{0:X}' -f [int64]$layout.unit_effect_previous_x)
        previous_y = ('0x{0:X}' -f [int64]$layout.unit_effect_previous_y)
        accumulator_x = ('0x{0:X}' -f [int64]$layout.unit_effect_accumulator_x)
        accumulator_y = ('0x{0:X}' -f [int64]$layout.unit_effect_accumulator_y)
        direction = ('0x{0:X}' -f [int64]$layout.unit_effect_direction)
    }
    production_runtime_layout = [ordered]@{
        variant_counts = ('0x{0:X}' -f [int64]$layout.production_variant_counts)
        lock_flags = ('0x{0:X}' -f [int64]$layout.production_lock_flags)
        primary_resources = ('0x{0:X}' -f [int64]$layout.production_primary_resources)
        secondary_resources = ('0x{0:X}' -f [int64]$layout.production_secondary_resources)
        size = ('0x{0:X}' -f [int64]$layout.production_runtime_size)
    }
    palette_layout = [ordered]@{
        pixel_slots = ('0x{0:X}' -f [int64]$layout.palette_pixel_slots)
        size = ('0x{0:X}' -f [int64]$layout.palette_size)
    }
    runtime_size = ('0x{0:X}' -f [int64]$layout.size)
    loop_layout = [ordered]@{
        simulation_frame = ('0x{0:X}' -f [int64]$layout.loop_simulation_frame)
        present_frame = ('0x{0:X}' -f [int64]$layout.loop_present_frame)
        frame_intervals = ('0x{0:X}' -f [int64]$layout.loop_frame_intervals)
        fixed_step_intervals = ('0x{0:X}' -f
            [int64]$layout.loop_fixed_step_intervals)
        fixed_step_repeat_counts = ('0x{0:X}' -f
            [int64]$layout.loop_fixed_step_repeat_counts)
        fixed_step_mode = ('0x{0:X}' -f [int64]$layout.loop_fixed_step_mode)
        size = ('0x{0:X}' -f [int64]$layout.loop_size)
    }
    visibility_layout = [ordered]@{
        width = ('0x{0:X}' -f [int64]$layout.visibility_width)
        height = ('0x{0:X}' -f [int64]$layout.visibility_height)
        current = ('0x{0:X}' -f [int64]$layout.visibility_current)
        previous = ('0x{0:X}' -f [int64]$layout.visibility_previous)
        owner = ('0x{0:X}' -f [int64]$layout.visibility_owner)
        terrain_class_flags = ('0x{0:X}' -f
            [int64]$layout.visibility_terrain_class_flags)
        size = ('0x{0:X}' -f [int64]$layout.visibility_size)
    }
    tooltip_layout = [ordered]@{
        draw_commands = ('0x{0:X}' -f [int64]$layout.tooltip_draw_commands)
        scheduled_mode = ('0x{0:X}' -f [int64]$layout.tooltip_scheduled_mode)
        cursor_x = ('0x{0:X}' -f [int64]$layout.tooltip_cursor_x)
        cursor_y = ('0x{0:X}' -f [int64]$layout.tooltip_cursor_y)
        current_unit_type = ('0x{0:X}' -f [int64]$layout.tooltip_current_unit_type)
        current_object_id = ('0x{0:X}' -f [int64]$layout.tooltip_current_object_id)
        current_payload = ('0x{0:X}' -f [int64]$layout.tooltip_current_payload)
        hover_flags = ('0x{0:X}' -f [int64]$layout.tooltip_hover_flags)
        current_text = ('0x{0:X}' -f [int64]$layout.tooltip_current_text)
        secondary_text = ('0x{0:X}' -f [int64]$layout.tooltip_secondary_text)
        requirement_text = ('0x{0:X}' -f [int64]$layout.tooltip_requirement_text)
        cost_row_text = ('0x{0:X}' -f [int64]$layout.tooltip_cost_row_text)
        size = ('0x{0:X}' -f [int64]$layout.tooltip_size)
        command_size = ('0x{0:X}' -f [int64]$layout.tooltip_command_size)
        command_kind = ('0x{0:X}' -f [int64]$layout.tooltip_command_kind)
        command_value = ('0x{0:X}' -f [int64]$layout.tooltip_command_value)
        command_text = ('0x{0:X}' -f [int64]$layout.tooltip_command_text)
    }
    overlay_layout = [ordered]@{
        size = ('0x{0:X}' -f [int64]$layout.overlay_size)
        text_commands = ('0x{0:X}' -f [int64]$layout.overlay_text_commands)
        queued_records = ('0x{0:X}' -f [int64]$layout.overlay_queued_records)
        dispatched_records = ('0x{0:X}' -f [int64]$layout.overlay_dispatched_records)
        record_size = ('0x{0:X}' -f [int64]$layout.overlay_record_size)
        icon_blit_requests = ('0x{0:X}' -f [int64]$layout.overlay_icon_blit_requests)
        icon_blit_size = ('0x{0:X}' -f [int64]$layout.overlay_icon_blit_size)
        text_command_flushed = ('0x{0:X}' -f [int64]$layout.overlay_text_command_flushed)
        progress_command_flushed = ('0x{0:X}' -f [int64]$layout.overlay_progress_command_flushed)
        emit_sprite_draws = ('0x{0:X}' -f [int64]$layout.overlay_emit_sprite_draws)
        text_size = ('0x{0:X}' -f [int64]$layout.overlay_text_size)
        text_text = ('0x{0:X}' -f [int64]$layout.overlay_text_text)
        text_x = ('0x{0:X}' -f [int64]$layout.overlay_text_x)
        text_y = ('0x{0:X}' -f [int64]$layout.overlay_text_y)
        text_color = ('0x{0:X}' -f [int64]$layout.overlay_text_color)
        text_draw_font = ('0x{0:X}' -f [int64]$layout.overlay_text_draw_font)
        text_metric_font = ('0x{0:X}' -f [int64]$layout.overlay_text_metric_font)
        text_centered = ('0x{0:X}' -f [int64]$layout.overlay_text_centered)
        text_right_aligned = ('0x{0:X}' -f [int64]$layout.overlay_text_right_aligned)
        text_bottom_aligned = ('0x{0:X}' -f [int64]$layout.overlay_text_bottom_aligned)
        command_options = ('0x{0:X}' -f [int64]$layout.overlay_command_options)
        hot_regions = ('0x{0:X}' -f [int64]$layout.overlay_hot_regions)
        selected_unit_ids = ('0x{0:X}' -f [int64]$layout.overlay_selected_unit_ids)
        screen_width = ('0x{0:X}' -f [int64]$layout.overlay_screen_width)
        screen_height = ('0x{0:X}' -f [int64]$layout.overlay_screen_height)
        world_viewport_height = ('0x{0:X}' -f
            [int64]$layout.overlay_world_viewport_height)
        interface_theme_index = ('0x{0:X}' -f [int64]$layout.overlay_interface_theme_index)
        minimap = ('0x{0:X}' -f [int64]$layout.overlay_minimap)
        local_player_slot = ('0x{0:X}' -f [int64]$layout.overlay_local_player_slot)
        camera_x = ('0x{0:X}' -f [int64]$layout.overlay_camera_x)
        camera_y = ('0x{0:X}' -f [int64]$layout.overlay_camera_y)
        camera_max_x = ('0x{0:X}' -f [int64]$layout.overlay_camera_max_x)
        camera_max_y = ('0x{0:X}' -f [int64]$layout.overlay_camera_max_y)
        camera_scroll_dirty = ('0x{0:X}' -f [int64]$layout.overlay_camera_scroll_dirty)
        camera_edge_pointer_valid = ('0x{0:X}' -f [int64]$layout.overlay_camera_edge_pointer_valid)
        camera_scroll_ramp = ('0x{0:X}' -f [int64]$layout.overlay_camera_scroll_ramp)
        camera_scroll_tick_bucket = ('0x{0:X}' -f [int64]$layout.overlay_camera_scroll_tick_bucket)
        current_tick_ms = ('0x{0:X}' -f [int64]$layout.overlay_current_tick_ms)
        replay_timing_enabled = ('0x{0:X}' -f [int64]$layout.overlay_replay_timing_enabled)
        camera_edge_cursor_index = ('0x{0:X}' -f [int64]$layout.overlay_camera_edge_cursor_index)
        placement_mode = ('0x{0:X}' -f [int64]$layout.overlay_placement_mode)
        placement_definition = ('0x{0:X}' -f [int64]$layout.overlay_placement_definition)
        placement_pointer_x = ('0x{0:X}' -f [int64]$layout.overlay_placement_pointer_x)
        placement_pointer_y = ('0x{0:X}' -f [int64]$layout.overlay_placement_pointer_y)
        placement_footprint_width = ('0x{0:X}' -f [int64]$layout.overlay_placement_footprint_width)
        placement_footprint_height = ('0x{0:X}' -f [int64]$layout.overlay_placement_footprint_height)
        placement_preview_valid = ('0x{0:X}' -f [int64]$layout.overlay_placement_preview_valid)
        placement_cell_validity = ('0x{0:X}' -f [int64]$layout.overlay_placement_cell_validity)
        resource_amount = ('0x{0:X}' -f [int64]$layout.overlay_resource_amount)
        resource_counter_x = ('0x{0:X}' -f [int64]$layout.overlay_resource_counter_x)
        resource_counter_y = ('0x{0:X}' -f [int64]$layout.overlay_resource_counter_y)
        population_counter_x = ('0x{0:X}' -f [int64]$layout.overlay_population_counter_x)
        population_counter_y = ('0x{0:X}' -f [int64]$layout.overlay_population_counter_y)
        population_used = ('0x{0:X}' -f [int64]$layout.overlay_population_used)
        population_available = ('0x{0:X}' -f [int64]$layout.overlay_population_available)
        selected_unit_id = ('0x{0:X}' -f [int64]$layout.overlay_selected_unit_id)
        selected_unit_type = ('0x{0:X}' -f [int64]$layout.overlay_selected_unit_type)
        selected_unit_owner = ('0x{0:X}' -f [int64]$layout.overlay_selected_unit_owner)
        selected_unit_count = ('0x{0:X}' -f [int64]$layout.overlay_selected_unit_count)
        selected_unit_command_mask = ('0x{0:X}' -f [int64]$layout.overlay_selected_unit_command_mask)
        selected_unit_name_text = ('0x{0:X}' -f [int64]$layout.overlay_selected_unit_name_text)
        selected_production_category = ('0x{0:X}' -f [int64]$layout.overlay_selected_production_category)
        hover_context = ('0x{0:X}' -f [int64]$layout.overlay_hover_context)
        mouse_x = ('0x{0:X}' -f [int64]$layout.overlay_mouse_x)
        mouse_y = ('0x{0:X}' -f [int64]$layout.overlay_mouse_y)
        hover_size = ('0x{0:X}' -f [int64]$layout.overlay_hover_size)
        hover_kind = ('0x{0:X}' -f [int64]$layout.overlay_hover_kind)
        hover_item = ('0x{0:X}' -f [int64]$layout.overlay_hover_item)
        hover_unit = ('0x{0:X}' -f [int64]$layout.overlay_hover_unit)
        hover_x = ('0x{0:X}' -f [int64]$layout.overlay_hover_x)
        hover_y = ('0x{0:X}' -f [int64]$layout.overlay_hover_y)
        progress_commands = ('0x{0:X}' -f [int64]$layout.overlay_progress_commands)
        progress_size = ('0x{0:X}' -f [int64]$layout.overlay_progress_size)
        progress_left = ('0x{0:X}' -f [int64]$layout.overlay_progress_left)
        progress_top = ('0x{0:X}' -f [int64]$layout.overlay_progress_top)
        progress_right = ('0x{0:X}' -f [int64]$layout.overlay_progress_right)
        progress_bottom = ('0x{0:X}' -f [int64]$layout.overlay_progress_bottom)
        progress_numerator = ('0x{0:X}' -f [int64]$layout.overlay_progress_numerator)
        progress_denominator = ('0x{0:X}' -f [int64]$layout.overlay_progress_denominator)
    }
    command_option_layout = [ordered]@{
        size = ('0x{0:X}' -f [int64]$layout.command_option_size)
        item = ('0x{0:X}' -f [int64]$layout.command_option_item)
        aux = ('0x{0:X}' -f [int64]$layout.command_option_aux)
        flags = ('0x{0:X}' -f [int64]$layout.command_option_flags)
        icon = ('0x{0:X}' -f [int64]$layout.command_option_icon)
        hotkey = ('0x{0:X}' -f [int64]$layout.command_option_hotkey)
        enabled = ('0x{0:X}' -f [int64]$layout.command_option_enabled)
    }
    hot_region_layout = [ordered]@{
        size = ('0x{0:X}' -f [int64]$layout.hot_region_size)
        record = ('0x{0:X}' -f [int64]$layout.hot_region_record)
        hotkey = ('0x{0:X}' -f [int64]$layout.hot_region_hotkey)
        enabled = ('0x{0:X}' -f [int64]$layout.hot_region_enabled)
        item = ('0x{0:X}' -f [int64]$layout.draw_record_item)
        aux = ('0x{0:X}' -f [int64]$layout.draw_record_aux)
        flags = ('0x{0:X}' -f [int64]$layout.draw_record_flags)
        x = ('0x{0:X}' -f [int64]$layout.draw_record_x)
        y = ('0x{0:X}' -f [int64]$layout.draw_record_y)
        width = ('0x{0:X}' -f [int64]$layout.draw_record_width)
        height = ('0x{0:X}' -f [int64]$layout.draw_record_height)
        icon = ('0x{0:X}' -f [int64]$layout.draw_record_icon)
    }
    minimap_render_layout = [ordered]@{
        output_x = ('0x{0:X}' -f [int64]$layout.minimap_output_x)
        output_y = ('0x{0:X}' -f [int64]$layout.minimap_output_y)
        output_width = ('0x{0:X}' -f [int64]$layout.minimap_output_width)
        output_height = ('0x{0:X}' -f [int64]$layout.minimap_output_height)
        minimap_width = ('0x{0:X}' -f [int64]$layout.minimap_width)
        minimap_height = ('0x{0:X}' -f [int64]$layout.minimap_height)
    }
    minimap_unit_layout = [ordered]@{
        vector = ('0x{0:X}' -f [int64]$layout.overlay_minimap_units)
        owner_colors = ('0x{0:X}' -f [int64]$layout.overlay_minimap_owner_colors)
        owner_footprint_colors = ('0x{0:X}' -f [int64]$layout.overlay_minimap_owner_footprint_colors)
        size = ('0x{0:X}' -f [int64]$layout.minimap_unit_size)
        id = ('0x{0:X}' -f [int64]$layout.minimap_unit_id)
        type = ('0x{0:X}' -f [int64]$layout.minimap_unit_type)
        owner = ('0x{0:X}' -f [int64]$layout.minimap_unit_owner)
        runtime_flags = ('0x{0:X}' -f [int64]$layout.minimap_unit_runtime_flags)
        score = ('0x{0:X}' -f [int64]$layout.minimap_unit_score)
        world_x = ('0x{0:X}' -f [int64]$layout.minimap_unit_world_x)
        world_y = ('0x{0:X}' -f [int64]$layout.minimap_unit_world_y)
        bounds_left = ('0x{0:X}' -f [int64]$layout.minimap_unit_bounds_left)
        bounds_top = ('0x{0:X}' -f [int64]$layout.minimap_unit_bounds_top)
        bounds_width = ('0x{0:X}' -f [int64]$layout.minimap_unit_bounds_width)
        bounds_height = ('0x{0:X}' -f [int64]$layout.minimap_unit_bounds_height)
        footprint_width = ('0x{0:X}' -f [int64]$layout.minimap_unit_footprint_width)
        footprint_height = ('0x{0:X}' -f [int64]$layout.minimap_unit_footprint_height)
        visible = ('0x{0:X}' -f [int64]$layout.minimap_unit_visible)
        hidden = ('0x{0:X}' -f [int64]$layout.minimap_unit_hidden)
        special_visibility = ('0x{0:X}' -f [int64]$layout.minimap_unit_special_visibility)
    }
    minimap_marker_layout = [ordered]@{
        vector = ('0x{0:X}' -f [int64]$layout.overlay_minimap_markers)
        size = ('0x{0:X}' -f [int64]$layout.minimap_marker_size)
        kind = ('0x{0:X}' -f [int64]$layout.minimap_marker_kind)
        x = ('0x{0:X}' -f [int64]$layout.minimap_marker_x)
        y = ('0x{0:X}' -f [int64]$layout.minimap_marker_y)
        width = ('0x{0:X}' -f [int64]$layout.minimap_marker_width)
        height = ('0x{0:X}' -f [int64]$layout.minimap_marker_height)
        color = ('0x{0:X}' -f [int64]$layout.minimap_marker_color)
        item = ('0x{0:X}' -f [int64]$layout.minimap_marker_item)
        owner = ('0x{0:X}' -f [int64]$layout.minimap_marker_owner)
        valid = ('0x{0:X}' -f [int64]$layout.minimap_marker_valid)
    }
    unit_layout = [ordered]@{
        size = ('0x{0:X}' -f [int64]$layout.unit_size)
        queued_command_size = ('0x{0:X}' -f [int64]$layout.queued_command_size)
        queued_command_state = ('0x{0:X}' -f [int64]$layout.queued_command_state)
        queued_command_x = ('0x{0:X}' -f [int64]$layout.queued_command_x)
        queued_command_y = ('0x{0:X}' -f [int64]$layout.queued_command_y)
        queued_command_value = ('0x{0:X}' -f [int64]$layout.queued_command_value)
        id = ('0x{0:X}' -f [int64]$layout.unit_id)
        runtime_slot = ('0x{0:X}' -f [int64]$layout.unit_runtime_slot)
        type = ('0x{0:X}' -f [int64]$layout.unit_type)
        string_slot = ('0x{0:X}' -f [int64]$layout.unit_string_slot)
        scenario_string_slot = ('0x{0:X}' -f
            [int64]$layout.unit_scenario_string_slot)
        owner = ('0x{0:X}' -f [int64]$layout.unit_owner)
        type_flags = ('0x{0:X}' -f [int64]$layout.unit_type_flags)
        command_state = ('0x{0:X}' -f [int64]$layout.unit_command_state)
        command_flags = ('0x{0:X}' -f [int64]$layout.unit_command_flags)
        runtime_flags = ('0x{0:X}' -f [int64]$layout.unit_runtime_flags)
        draw_flags = ('0x{0:X}' -f [int64]$layout.unit_draw_flags)
        command_lockout = ('0x{0:X}' -f [int64]$layout.unit_command_lockout)
        action_mode = ('0x{0:X}' -f [int64]$layout.unit_action_mode)
        action_mode_gate = ('0x{0:X}' -f [int64]$layout.unit_action_mode_gate)
        command_value = ('0x{0:X}' -f [int64]$layout.unit_command_value)
        spawn_type = ('0x{0:X}' -f [int64]$layout.unit_spawn_type)
        queued_type = ('0x{0:X}' -f [int64]$layout.unit_queued_type)
        x = ('0x{0:X}' -f [int64]$layout.unit_x)
        y = ('0x{0:X}' -f [int64]$layout.unit_y)
        destination_x = ('0x{0:X}' -f [int64]$layout.unit_destination_x)
        destination_y = ('0x{0:X}' -f [int64]$layout.unit_destination_y)
        path_target_x = ('0x{0:X}' -f [int64]$layout.unit_path_target_x)
        path_target_y = ('0x{0:X}' -f [int64]$layout.unit_path_target_y)
        current_cell_x = ('0x{0:X}' -f [int64]$layout.unit_current_cell_x)
        current_cell_y = ('0x{0:X}' -f [int64]$layout.unit_current_cell_y)
        next_path_x = ('0x{0:X}' -f [int64]$layout.unit_next_path_x)
        next_path_y = ('0x{0:X}' -f [int64]$layout.unit_next_path_y)
        movement_flags = ('0x{0:X}' -f [int64]$layout.unit_movement_flags)
        animation_frame = ('0x{0:X}' -f [int64]$layout.unit_animation_frame)
        cargo = ('0x{0:X}' -f [int64]$layout.unit_cargo)
        cargo_capacity = ('0x{0:X}' -f [int64]$layout.unit_cargo_capacity)
        harvest_tile = ('0x{0:X}' -f [int64]$layout.unit_harvest_tile)
        work_timer = ('0x{0:X}' -f [int64]$layout.unit_work_timer)
        max_health = ('0x{0:X}' -f [int64]$layout.unit_max_health)
        health = ('0x{0:X}' -f [int64]$layout.unit_health)
        active_payload = ('0x{0:X}' -f [int64]$layout.unit_active_payload)
        deferred_commands = ('0x{0:X}' -f [int64]$layout.unit_deferred_commands)
        deferred_count = ('0x{0:X}' -f [int64]$layout.unit_deferred_count)
        target = ('0x{0:X}' -f [int64]$layout.unit_target)
        linked_unit = ('0x{0:X}' -f [int64]$layout.unit_linked_unit)
        definition = ('0x{0:X}' -f [int64]$layout.unit_definition)
        active = ('0x{0:X}' -f [int64]$layout.unit_active)
        attached = ('0x{0:X}' -f [int64]$layout.unit_attached)
        footprint_registered = ('0x{0:X}' -f [int64]$layout.unit_footprint_registered)
        under_construction = ('0x{0:X}' -f [int64]$layout.unit_under_construction)
        production_reserved = ('0x{0:X}' -f [int64]$layout.unit_production_reserved)
        definition_production_cycle_period = ('0x{0:X}' -f [int64]$layout.definition_production_cycle_period)
        definition_lifecycle_class = ('0x{0:X}' -f [int64]$layout.definition_lifecycle_class)
        definition_passive_recovery_enabled = ('0x{0:X}' -f [int64]$layout.definition_passive_recovery_enabled)
        definition_passive_recovery_flags = ('0x{0:X}' -f [int64]$layout.definition_passive_recovery_flags)
        definition_passive_map_effect_seed = ('0x{0:X}' -f [int64]$layout.definition_passive_map_effect_seed)
        definition_effect_adjusted_interaction_range_base = ('0x{0:X}' -f [int64]$layout.definition_effect_adjusted_interaction_range_base)
        definition_production_spawn_time = ('0x{0:X}' -f [int64]$layout.definition_production_spawn_time)
        definition_footprint_width = ('0x{0:X}' -f [int64]$layout.definition_footprint_width)
        definition_footprint_height = ('0x{0:X}' -f [int64]$layout.definition_footprint_height)
    }
    unit_render_item_layout = [ordered]@{
        vector = ('0x{0:X}' -f [int64]$layout.unit_render_queue_units)
        size = ('0x{0:X}' -f [int64]$layout.unit_render_item_size)
        type = ('0x{0:X}' -f [int64]$layout.unit_render_item_type)
        owner = ('0x{0:X}' -f [int64]$layout.unit_render_item_owner)
        runtime_slot = ('0x{0:X}' -f [int64]$layout.unit_render_item_runtime_slot)
        draw_flags = ('0x{0:X}' -f [int64]$layout.unit_render_item_draw_flags)
        max_health = ('0x{0:X}' -f [int64]$layout.unit_render_item_max_health)
        health = ('0x{0:X}' -f [int64]$layout.unit_render_item_health)
        stage_count = ('0x{0:X}' -f [int64]$layout.unit_render_item_stage_count)
        progress = ('0x{0:X}' -f [int64]$layout.unit_render_item_progress)
        progress_limit = ('0x{0:X}' -f [int64]$layout.unit_render_item_progress_limit)
        progress_active = ('0x{0:X}' -f [int64]$layout.unit_render_item_progress_active)
    }
    input_layout = [ordered]@{
        mouse_x = ('0x{0:X}' -f [int64]$layout.input_mouse_x)
        mouse_y = ('0x{0:X}' -f [int64]$layout.input_mouse_y)
        pointer_motion_seen = ('0x{0:X}' -f
            [int64]$layout.input_pointer_motion_seen)
    }
    unit_render_queue_entry_layout = [ordered]@{
        vector = ('0x{0:X}' -f [int64]$layout.unit_render_queue_entries)
        size = ('0x{0:X}' -f [int64]$layout.unit_render_queue_entry_size)
        type = ('0x{0:X}' -f [int64]$layout.unit_render_queue_entry_type)
        render_class = ('0x{0:X}' -f [int64]$layout.unit_render_queue_entry_class)
        layer = ('0x{0:X}' -f [int64]$layout.unit_render_queue_entry_layer)
        sort_key = ('0x{0:X}' -f [int64]$layout.unit_render_queue_entry_sort_key)
        runtime_slot = ('0x{0:X}' -f [int64]$layout.unit_render_queue_entry_runtime_slot)
    }
    render_command_queue_layout = [ordered]@{
        sorted = ('0x{0:X}' -f [int64]$layout.render_command_queue_sorted)
        commands = ('0x{0:X}' -f [int64]$layout.render_command_queue_commands)
        sorted_indices = ('0x{0:X}' -f [int64]$layout.render_command_queue_sorted_indices)
        command_size = ('0x{0:X}' -f [int64]$layout.render_command_size)
        command_class = ('0x{0:X}' -f [int64]$layout.render_command_class)
        command_payload = ('0x{0:X}' -f [int64]$layout.render_command_payload)
        command_sort_key = ('0x{0:X}' -f [int64]$layout.render_command_sort_key)
        command_sprite_entry = ('0x{0:X}' -f [int64]$layout.render_command_sprite_entry)
        command_sprite_draw_mode = ('0x{0:X}' -f [int64]$layout.render_command_sprite_draw_mode)
        command_screen_y = ('0x{0:X}' -f [int64]$layout.render_command_screen_y)
        command_screen_x = ('0x{0:X}' -f [int64]$layout.render_command_screen_x)
        command_packed_flags = ('0x{0:X}' -f [int64]$layout.render_command_packed_flags)
        command_sprite_draw_mode_valid = ('0x{0:X}' -f [int64]$layout.render_command_sprite_draw_mode_valid)
        command_unit_context = ('0x{0:X}' -f [int64]$layout.render_command_unit_context)
        command_unit_item = ('0x{0:X}' -f [int64]$layout.render_command_unit_item)
        command_effect_context = ('0x{0:X}' -f [int64]$layout.render_command_effect_context)
        command_effect = ('0x{0:X}' -f [int64]$layout.render_command_effect)
        command_draw_variant = ('0x{0:X}' -f [int64]$layout.render_command_draw_variant)
    }
    map_effect_layout = [ordered]@{
        context_size = ('0x{0:X}' -f [int64]$layout.map_effect_context_size)
        effects = ('0x{0:X}' -f [int64]$layout.map_effect_context_effects)
        active_indices = ('0x{0:X}' -f [int64]$layout.map_effect_context_active_indices)
        free_indices = ('0x{0:X}' -f [int64]$layout.map_effect_context_free_indices)
        frame = ('0x{0:X}' -f [int64]$layout.map_effect_context_frame)
        instance_size = ('0x{0:X}' -f [int64]$layout.map_effect_instance_size)
        instance_active = ('0x{0:X}' -f [int64]$layout.map_effect_instance_active)
        instance_id = ('0x{0:X}' -f [int64]$layout.map_effect_instance_id)
        instance_effect_id = ('0x{0:X}' -f [int64]$layout.map_effect_instance_effect_id)
        instance_flags = ('0x{0:X}' -f [int64]$layout.map_effect_instance_flags)
        instance_frame_timer = ('0x{0:X}' -f [int64]$layout.map_effect_instance_frame_timer)
        instance_repeat_count = ('0x{0:X}' -f [int64]$layout.map_effect_instance_repeat_count)
        instance_x = ('0x{0:X}' -f [int64]$layout.map_effect_instance_x)
        instance_y = ('0x{0:X}' -f [int64]$layout.map_effect_instance_y)
        instance_linked_unit = ('0x{0:X}' -f [int64]$layout.map_effect_instance_linked_unit)
    }
    movement_context_layout = [ordered]@{
        map = ('0x{0:X}' -f [int64]$layout.movement_map)
        active_units = ('0x{0:X}' -f [int64]$layout.movement_active_units)
        free_units = ('0x{0:X}' -f [int64]$layout.movement_free_units)
        lifecycle_units = ('0x{0:X}' -f [int64]$layout.movement_lifecycle_units)
        size = ('0x{0:X}' -f [int64]$layout.movement_context_size)
    }
    movement_map_layout = [ordered]@{
        width = ('0x{0:X}' -f [int64]$layout.movement_map_width)
        height = ('0x{0:X}' -f [int64]$layout.movement_map_height)
        stride = ('0x{0:X}' -f [int64]$layout.movement_map_stride)
        cells = ('0x{0:X}' -f [int64]$layout.movement_map_cells)
        size = ('0x{0:X}' -f [int64]$layout.movement_map_size)
        cell_size = ('0x{0:X}' -f [int64]$layout.movement_cell_size)
        cell_flags = ('0x{0:X}' -f [int64]$layout.movement_cell_flags)
    }
    lifecycle_layout = [ordered]@{
        primary = ('0x{0:X}' -f [int64]$layout.lifecycle_primary)
        secondary = ('0x{0:X}' -f [int64]$layout.lifecycle_secondary)
        population_limit = ('0x{0:X}' -f [int64]$layout.lifecycle_population_limit)
        population_used = ('0x{0:X}' -f [int64]$layout.lifecycle_population_used)
        population_reserved = ('0x{0:X}' -f [int64]$layout.lifecycle_population_reserved)
        owner_unit_kill_count = ('0x{0:X}' -f [int64]$layout.lifecycle_unit_kills)
        owner_building_kill_count = ('0x{0:X}' -f [int64]$layout.lifecycle_building_kills)
        owner_unit_lost_count = ('0x{0:X}' -f [int64]$layout.lifecycle_unit_lost)
        size = ('0x{0:X}' -f [int64]$layout.lifecycle_size)
    }
}

$json = $result | ConvertTo-Json -Depth 4
if ($OutputPath) {
    $resolvedOutput = if ([IO.Path]::IsPathRooted($OutputPath)) {
        [IO.Path]::GetFullPath($OutputPath)
    } else {
        [IO.Path]::GetFullPath((Join-Path $toolDirectory $OutputPath))
    }
    [IO.Directory]::CreateDirectory(
        [IO.Path]::GetDirectoryName($resolvedOutput)) | Out-Null
    [IO.File]::WriteAllText(
        $resolvedOutput, $json + [Environment]::NewLine,
        [Text.UTF8Encoding]::new($false))
    $resolvedOutput
}
else {
    $json
}
