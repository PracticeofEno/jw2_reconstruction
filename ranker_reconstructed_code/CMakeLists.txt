cmake_minimum_required(VERSION 3.20)
project(ranker_reconstruction LANGUAGES C CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The deployed game runs timing-sensitive simulation loops.  Single-config
# generators otherwise default to an unoptimized build when no build type is
# supplied, which makes Fast Computer mode substantially slower than the
# original executable.  Keep explicit Debug requests available, but make a
# normal configure produce a deployment-suitable binary.
if (NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        Debug Release RelWithDebInfo MinSizeRel)
endif()

add_library(zlib113 STATIC
    third_party/zlib113/adler32.c
    third_party/zlib113/compress.c
    third_party/zlib113/crc32.c
    third_party/zlib113/deflate.c
    third_party/zlib113/infblock.c
    third_party/zlib113/infcodes.c
    third_party/zlib113/inffast.c
    third_party/zlib113/inflate.c
    third_party/zlib113/inftrees.c
    third_party/zlib113/infutil.c
    third_party/zlib113/trees.c
    third_party/zlib113/uncompr.c
    third_party/zlib113/zutil.c
)

target_include_directories(zlib113 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib113
)

if (MSVC)
    target_compile_definitions(zlib113 PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# Regression sources are intentionally excluded from the minimal rebuild tree.
option(RANKER_BUILD_REGRESSION_TESTS
    "Build the optional out-of-tree regression targets" OFF)
if (RANKER_BUILD_REGRESSION_TESTS)
    add_executable(directdraw_presentation_mode_regression
        tests/directdraw_presentation_mode_regression.cpp
    )
    target_include_directories(directdraw_presentation_mode_regression PRIVATE
        include
    )
    add_test(NAME directdraw_presentation_mode_regression
        COMMAND directdraw_presentation_mode_regression
    )

    add_executable(d3d9_cubic_presentation_regression
        tests/d3d9_cubic_presentation_regression.cpp
    )
    target_include_directories(d3d9_cubic_presentation_regression PRIVATE
        include
    )
    if (WIN32)
        target_sources(d3d9_cubic_presentation_regression PRIVATE
            src/ranker_d3d9_presentation.cpp
        )
        target_link_libraries(d3d9_cubic_presentation_regression PRIVATE
            d3d9
            ddraw
        )
    endif()
    add_test(NAME d3d9_cubic_presentation_regression
        COMMAND d3d9_cubic_presentation_regression
    )

    add_executable(unit_production_sound_regression
        tests/unit_production_sound_regression.cpp
        src/ranker_gameplay_sound.cpp
    )
    target_include_directories(unit_production_sound_regression PRIVATE
        include
    )
    add_test(NAME unit_production_sound_regression
        COMMAND unit_production_sound_regression
    )

    add_executable(under_attack_sound_regression
        tests/under_attack_sound_regression.cpp
        src/ranker_gameplay_sound.cpp
    )
    target_include_directories(under_attack_sound_regression PRIVATE
        include
    )
    add_test(NAME under_attack_sound_regression
        COMMAND under_attack_sound_regression
    )

    add_executable(audio_branch_matrix_regression
        tests/audio_branch_matrix_regression.cpp
        src/ranker_gameplay_sound.cpp
    )
    target_include_directories(audio_branch_matrix_regression PRIVATE
        include
    )
    add_test(NAME audio_branch_matrix_regression
        COMMAND audio_branch_matrix_regression
    )

    add_executable(gameplay_resource_sound_branch_regression
        tests/gameplay_resource_sound_branch_regression.cpp
        src/ranker_gameplay_frame_render.cpp
        src/ranker_gameplay_sound.cpp
        src/ranker_map_effects.cpp
        src/ranker_production_orders.cpp
        src/ranker_unit_commands.cpp
        src/ranker_unit_damage.cpp
        src/ranker_unit_equipment.cpp
        src/ranker_unit_movement.cpp
    )
    target_include_directories(gameplay_resource_sound_branch_regression PRIVATE
        include
    )
    set_property(TARGET gameplay_resource_sound_branch_regression PROPERTY
        INTERPROCEDURAL_OPTIMIZATION TRUE
    )
    if (MSVC)
        target_compile_options(gameplay_resource_sound_branch_regression PRIVATE /Gy)
        target_link_options(gameplay_resource_sound_branch_regression PRIVATE /OPT:REF)
    else()
        target_compile_options(gameplay_resource_sound_branch_regression PRIVATE
            -O1
            -ffunction-sections
            -fdata-sections
        )
        target_link_options(gameplay_resource_sound_branch_regression PRIVATE
            -Wl,--gc-sections
        )
    endif()
    add_test(NAME gameplay_resource_sound_branch_regression
        COMMAND gameplay_resource_sound_branch_regression
    )

    add_executable(owner_production_action_gate_regression
        tests/owner_production_action_gate_regression.cpp
        src/ranker_unit_commands.cpp
    )
    target_include_directories(owner_production_action_gate_regression PRIVATE
        include
    )
    set_property(TARGET owner_production_action_gate_regression PROPERTY
        INTERPROCEDURAL_OPTIMIZATION TRUE
    )
    if (MSVC)
        target_compile_options(owner_production_action_gate_regression PRIVATE /Gy)
        target_link_options(owner_production_action_gate_regression PRIVATE /OPT:REF)
    else()
        target_compile_options(owner_production_action_gate_regression PRIVATE
            -O1
            -ffunction-sections
            -fdata-sections
        )
        target_link_options(owner_production_action_gate_regression PRIVATE
            -Wl,--gc-sections
        )
    endif()
    add_test(NAME owner_production_action_gate_regression
        COMMAND owner_production_action_gate_regression
    )

    add_executable(evil_portal_construction_release_regression
        tests/evil_portal_construction_release_regression.cpp
        src/ranker_evil_portal_release.cpp
    )
    target_include_directories(evil_portal_construction_release_regression PRIVATE
        include
    )
    if (MSVC)
        target_compile_options(evil_portal_construction_release_regression PRIVATE /Gy)
        target_link_options(evil_portal_construction_release_regression PRIVATE /OPT:REF)
    else()
        target_compile_options(evil_portal_construction_release_regression PRIVATE
            -O1 -ffunction-sections -fdata-sections
        )
        target_link_options(evil_portal_construction_release_regression PRIVATE
            -Wl,--gc-sections
        )
    endif()
    add_test(NAME evil_portal_construction_release_regression
        COMMAND evil_portal_construction_release_regression
    )

    add_executable(owner_strategic_path_layer_regression
        tests/owner_strategic_path_layer_regression.cpp
        src/ranker_unit_commands.cpp
    )
    target_include_directories(owner_strategic_path_layer_regression PRIVATE
        include
    )
    set_property(TARGET owner_strategic_path_layer_regression PROPERTY
        INTERPROCEDURAL_OPTIMIZATION TRUE
    )
    if (MSVC)
        target_compile_options(owner_strategic_path_layer_regression PRIVATE /Gy)
        target_link_options(owner_strategic_path_layer_regression PRIVATE /OPT:REF)
    else()
        target_compile_options(owner_strategic_path_layer_regression PRIVATE
            -O1
            -ffunction-sections
            -fdata-sections
        )
        target_link_options(owner_strategic_path_layer_regression PRIVATE
            -Wl,--gc-sections
        )
    endif()
    add_test(NAME owner_strategic_path_layer_regression
        COMMAND owner_strategic_path_layer_regression
    )

    add_executable(meat_tooltip_amount_regression
        tests/meat_tooltip_amount_regression.cpp
    )
    target_include_directories(meat_tooltip_amount_regression PRIVATE
        include
    )
    add_test(NAME meat_tooltip_amount_regression
        COMMAND meat_tooltip_amount_regression
    )

    add_executable(neutral_attack_target_regression
        tests/neutral_attack_target_regression.cpp
    )
    target_include_directories(neutral_attack_target_regression PRIVATE
        include
    )
    add_test(NAME neutral_attack_target_regression
        COMMAND neutral_attack_target_regression
    )

    add_executable(active_queue_icon_payload_regression
        tests/active_queue_icon_payload_regression.cpp
    )
    target_include_directories(active_queue_icon_payload_regression PRIVATE
        include
    )
    add_test(NAME active_queue_icon_payload_regression
        COMMAND active_queue_icon_payload_regression
    )

    add_executable(hud_shortage_message_layout_regression
        tests/hud_shortage_message_layout_regression.cpp
    )
    target_include_directories(hud_shortage_message_layout_regression PRIVATE
        include
    )
    add_test(NAME hud_shortage_message_layout_regression
        COMMAND hud_shortage_message_layout_regression
    )

    add_executable(owner_palette_output_regression
        tests/owner_palette_output_regression.cpp
        src/ranker_map_brush.cpp
        src/ranker_palette_cache.cpp
        src/ranker_resource_store.cpp
        src/ranker_sprite_renderer.cpp
        src/ranker_trc.cpp
        src/ranker_ui_overlay.cpp
    )
    target_include_directories(owner_palette_output_regression PRIVATE
        include
    )
    target_link_libraries(owner_palette_output_regression PRIVATE
        zlib113
    )
    set_property(TARGET owner_palette_output_regression PROPERTY
        INTERPROCEDURAL_OPTIMIZATION TRUE
    )
    if (MSVC)
        target_compile_options(owner_palette_output_regression PRIVATE /Gy)
        target_link_options(owner_palette_output_regression PRIVATE /OPT:REF)
    else()
        target_compile_options(owner_palette_output_regression PRIVATE
            -O1
            -ffunction-sections
            -fdata-sections
        )
        target_link_options(owner_palette_output_regression PRIVATE
            -Wl,--gc-sections
        )
    endif()
    add_test(NAME owner_palette_output_regression
        COMMAND owner_palette_output_regression
    )

    add_executable(interface_background_resource_regression
        tests/interface_background_resource_regression.cpp
        src/ranker_trc.cpp
    )
    target_include_directories(interface_background_resource_regression PRIVATE
        include
    )
    target_link_libraries(interface_background_resource_regression PRIVATE
        zlib113
    )
    add_test(NAME interface_background_resource_regression
        COMMAND interface_background_resource_regression
    )

    add_executable(ui_layout_bucket_completion_regression
        tests/ui_layout_bucket_completion_regression.cpp
        src/ranker_ui_overlay.cpp
    )
    target_include_directories(ui_layout_bucket_completion_regression PRIVATE
        include
    )
    set_property(TARGET ui_layout_bucket_completion_regression PROPERTY
        INTERPROCEDURAL_OPTIMIZATION TRUE
    )
    if (MSVC)
        target_compile_options(ui_layout_bucket_completion_regression PRIVATE /Gy)
        target_link_options(ui_layout_bucket_completion_regression PRIVATE /OPT:REF)
    else()
        target_compile_options(ui_layout_bucket_completion_regression PRIVATE
            -O1
            -ffunction-sections
            -fdata-sections
        )
        target_link_options(ui_layout_bucket_completion_regression PRIVATE
            -Wl,--gc-sections
        )
    endif()
    add_test(NAME ui_layout_bucket_completion_regression
        COMMAND ui_layout_bucket_completion_regression
    )

    add_executable(tyrano_hud_rows_regression
        tests/tyrano_hud_rows_regression.cpp
        src/ranker_text_renderer.cpp
        src/ranker_ui_overlay.cpp
    )
    target_include_directories(tyrano_hud_rows_regression PRIVATE
        include
    )
    set_property(TARGET tyrano_hud_rows_regression PROPERTY
        INTERPROCEDURAL_OPTIMIZATION TRUE
    )
    if (MSVC)
        target_compile_options(tyrano_hud_rows_regression PRIVATE /Gy)
        target_link_options(tyrano_hud_rows_regression PRIVATE /OPT:REF)
    else()
        target_compile_options(tyrano_hud_rows_regression PRIVATE
            -O1
            -ffunction-sections
            -fdata-sections
        )
        target_link_options(tyrano_hud_rows_regression PRIVATE
            -Wl,--gc-sections
        )
    endif()
    add_test(NAME tyrano_hud_rows_regression
        COMMAND tyrano_hud_rows_regression
    )

    add_executable(live_session_definition_precedence_regression
        tests/live_session_definition_precedence_regression.cpp
    )
    target_include_directories(live_session_definition_precedence_regression PRIVATE
        include
    )
    add_test(NAME live_session_definition_precedence_regression
        COMMAND live_session_definition_precedence_regression
    )

    add_executable(post_init_unit_requirement_runtime_definition_regression
        tests/post_init_unit_requirement_runtime_definition_regression.cpp
    )
    target_include_directories(post_init_unit_requirement_runtime_definition_regression PRIVATE
        include
    )
    add_test(NAME post_init_unit_requirement_runtime_definition_regression
        COMMAND post_init_unit_requirement_runtime_definition_regression
    )

    if (WIN32)
        add_executable(win32_text_locked_surface_regression
            tests/win32_text_locked_surface_regression.cpp
            src/ranker_text_renderer.cpp
        )
        target_include_directories(win32_text_locked_surface_regression PRIVATE
            include
        )
        if (MSVC)
            target_compile_options(win32_text_locked_surface_regression PRIVATE /Gy)
            target_link_options(win32_text_locked_surface_regression PRIVATE /OPT:REF)
        else()
            target_compile_options(win32_text_locked_surface_regression PRIVATE
                -ffunction-sections
                -fdata-sections
            )
            target_link_options(win32_text_locked_surface_regression PRIVATE
                -Wl,--gc-sections
            )
        endif()
        target_link_libraries(win32_text_locked_surface_regression PRIVATE
            gdi32
            winmm
        )
        add_test(NAME win32_text_locked_surface_regression
            COMMAND win32_text_locked_surface_regression
        )
    endif()

    add_executable(berry_cursor_selector_regression
        tests/berry_cursor_selector_regression.cpp
        src/ranker_gameplay_context_cursor.cpp
    )
    target_include_directories(berry_cursor_selector_regression PRIVATE
        include
    )
    add_test(NAME berry_cursor_selector_regression
        COMMAND berry_cursor_selector_regression
    )

    add_executable(meat_pipeline_regression
        tests/meat_pipeline_regression.cpp
        src/ranker_map_effects.cpp
    )
    target_include_directories(meat_pipeline_regression PRIVATE
        include
    )
    add_test(NAME meat_pipeline_regression
        COMMAND meat_pipeline_regression
    )

    add_executable(map_effect_session_reset_regression
        tests/map_effect_session_reset_regression.cpp
        src/ranker_map_effects.cpp
    )
    target_include_directories(map_effect_session_reset_regression PRIVATE
        include
    )
    add_test(NAME map_effect_session_reset_regression
        COMMAND map_effect_session_reset_regression
    )

    if (WIN32)
        add_executable(trc_legacy_ansi_path_regression
            tests/trc_legacy_ansi_path_regression.cpp
            src/ranker_trc.cpp
        )
        target_include_directories(trc_legacy_ansi_path_regression PRIVATE
            include
        )
        target_link_libraries(trc_legacy_ansi_path_regression PRIVATE
            zlib113
        )
        add_test(NAME trc_legacy_ansi_path_regression
            COMMAND trc_legacy_ansi_path_regression
        )
    endif()

    add_executable(production_order_packet_owner_regression
        tests/production_order_packet_owner_regression.cpp
    )
    target_include_directories(production_order_packet_owner_regression PRIVATE
        include
    )
    add_test(NAME production_order_packet_owner_regression
        COMMAND production_order_packet_owner_regression
    )

    add_executable(production_order_rule_wrap_regression
        tests/production_order_rule_wrap_regression.cpp
        src/ranker_production_orders.cpp
    )
    target_include_directories(production_order_rule_wrap_regression PRIVATE
        include
    )
    if (MSVC)
        target_compile_options(production_order_rule_wrap_regression PRIVATE /Gy)
        target_link_options(production_order_rule_wrap_regression PRIVATE /OPT:REF)
    else()
        target_compile_options(production_order_rule_wrap_regression PRIVATE
            -ffunction-sections
            -fdata-sections
        )
        target_link_options(production_order_rule_wrap_regression PRIVATE
            -Wl,--gc-sections
        )
    endif()
    add_test(NAME production_order_rule_wrap_regression
        COMMAND production_order_rule_wrap_regression
    )

    add_executable(placement_preview_boundary_regression
        tests/placement_preview_boundary_regression.cpp
        src/ranker_gameplay_production_actions.cpp
    )
    target_include_directories(placement_preview_boundary_regression PRIVATE
        include
    )
    if (MSVC)
        target_compile_options(placement_preview_boundary_regression PRIVATE /Gy)
        target_link_options(placement_preview_boundary_regression PRIVATE /OPT:REF)
    else()
        target_compile_options(placement_preview_boundary_regression PRIVATE
            -ffunction-sections
            -fdata-sections
        )
        target_link_options(placement_preview_boundary_regression PRIVATE
            -Wl,--gc-sections
        )
    endif()
    add_test(NAME placement_preview_boundary_regression
        COMMAND placement_preview_boundary_regression
    )

    add_executable(selected_health_text_color_regression
        tests/selected_health_text_color_regression.cpp
    )
    target_include_directories(selected_health_text_color_regression PRIVATE
        include
    )
    add_test(NAME selected_health_text_color_regression
        COMMAND selected_health_text_color_regression
    )

    add_executable(selected_unit_name_slot_regression
        tests/selected_unit_name_slot_regression.cpp
    )
    target_include_directories(selected_unit_name_slot_regression PRIVATE
        include
    )
    add_test(NAME selected_unit_name_slot_regression
        COMMAND selected_unit_name_slot_regression
    )

    add_executable(world_unit_name_slot_regression
        tests/world_unit_name_slot_regression.cpp
        src/ranker_unit_animation.cpp
    )
    target_include_directories(world_unit_name_slot_regression PRIVATE
        include
    )
    if (MSVC)
        target_compile_options(world_unit_name_slot_regression PRIVATE /Gy)
        target_link_options(world_unit_name_slot_regression PRIVATE /OPT:REF)
    else()
        target_compile_options(world_unit_name_slot_regression PRIVATE
            -ffunction-sections
            -fdata-sections
        )
        target_link_options(world_unit_name_slot_regression PRIVATE
            -Wl,--gc-sections
        )
    endif()
    add_test(NAME world_unit_name_slot_regression
        COMMAND world_unit_name_slot_regression
    )

    add_executable(world_unit_bar_layout_regression
        tests/world_unit_bar_layout_regression.cpp
        src/ranker_unit_animation.cpp
    )
    target_include_directories(world_unit_bar_layout_regression PRIVATE
        include
    )
    if (MSVC)
        target_compile_options(world_unit_bar_layout_regression PRIVATE /Gy)
        target_link_options(world_unit_bar_layout_regression PRIVATE /OPT:REF)
    else()
        target_compile_options(world_unit_bar_layout_regression PRIVATE
            -ffunction-sections
            -fdata-sections
        )
        target_link_options(world_unit_bar_layout_regression PRIVATE
            -Wl,--gc-sections
        )
    endif()
    add_test(NAME world_unit_bar_layout_regression
        COMMAND world_unit_bar_layout_regression
    )

    add_executable(selected_info_draw_order_regression
        tests/selected_info_draw_order_regression.cpp
    )
    target_include_directories(selected_info_draw_order_regression PRIVATE
        include
    )
    add_test(NAME selected_info_draw_order_regression
        COMMAND selected_info_draw_order_regression
    )

    add_executable(fog_smoothing_mask_regression
        tests/fog_smoothing_mask_regression.cpp
        src/ranker_gameplay_visibility.cpp
    )
    target_include_directories(fog_smoothing_mask_regression PRIVATE
        include
    )
    if (MSVC)
        target_compile_options(fog_smoothing_mask_regression PRIVATE /Gy)
        target_link_options(fog_smoothing_mask_regression PRIVATE /OPT:REF)
    else()
        target_compile_options(fog_smoothing_mask_regression PRIVATE
            -ffunction-sections
            -fdata-sections
        )
        target_link_options(fog_smoothing_mask_regression PRIVATE
            -Wl,--gc-sections
        )
    endif()
    add_test(NAME fog_smoothing_mask_regression
        COMMAND fog_smoothing_mask_regression
    )

    add_executable(fog_viewport_composite_regression
        tests/fog_viewport_composite_regression.cpp
        src/ranker_gameplay_visibility.cpp
    )
    target_include_directories(fog_viewport_composite_regression PRIVATE
        include
    )
    if (MSVC)
        target_compile_options(fog_viewport_composite_regression PRIVATE /Gy)
        target_link_options(fog_viewport_composite_regression PRIVATE /OPT:REF)
    else()
        target_compile_options(fog_viewport_composite_regression PRIVATE
            -ffunction-sections
            -fdata-sections
        )
        target_link_options(fog_viewport_composite_regression PRIVATE
            -Wl,--gc-sections
        )
    endif()
    add_test(NAME fog_viewport_composite_regression
        COMMAND fog_viewport_composite_regression
    )

    add_executable(construction_progress_frame_parity_regression
        tests/construction_progress_frame_parity_regression.cpp
        src/ranker_unit_animation.cpp
    )
    target_include_directories(construction_progress_frame_parity_regression PRIVATE
        include
    )
    if (MSVC)
        target_compile_options(construction_progress_frame_parity_regression PRIVATE /Gy)
        target_link_options(construction_progress_frame_parity_regression PRIVATE /OPT:REF)
    else()
        target_compile_options(construction_progress_frame_parity_regression PRIVATE
            -ffunction-sections
            -fdata-sections
        )
        target_link_options(construction_progress_frame_parity_regression PRIVATE
            -Wl,--gc-sections
        )
    endif()
    add_test(NAME construction_progress_frame_parity_regression
        COMMAND construction_progress_frame_parity_regression
    )

    add_executable(gameplay_hotkey_regression
        tests/gameplay_hotkey_regression.cpp
        src/ranker_input.cpp
    )
    target_include_directories(gameplay_hotkey_regression PRIVATE
        include
    )
    if (MSVC)
        target_compile_options(gameplay_hotkey_regression PRIVATE /Gy)
        target_link_options(gameplay_hotkey_regression PRIVATE /OPT:REF)
    else()
        target_compile_options(gameplay_hotkey_regression PRIVATE
            -ffunction-sections
            -fdata-sections
        )
        target_link_options(gameplay_hotkey_regression PRIVATE
            -Wl,--gc-sections
        )
    endif()
    add_test(NAME gameplay_hotkey_regression
        COMMAND gameplay_hotkey_regression
    )

    add_executable(gameplay_input_snapshot_ring_regression
        tests/gameplay_input_snapshot_ring_regression.cpp
        src/ranker_gameplay_input_actions.cpp
        src/ranker_input.cpp
    )
    target_include_directories(gameplay_input_snapshot_ring_regression PRIVATE
        include
    )
    if (MSVC)
        target_compile_options(gameplay_input_snapshot_ring_regression PRIVATE /Gy)
        target_link_options(gameplay_input_snapshot_ring_regression PRIVATE /OPT:REF)
    else()
        target_compile_options(gameplay_input_snapshot_ring_regression PRIVATE
            -O1
            -ffunction-sections
            -fdata-sections
        )
        target_link_options(gameplay_input_snapshot_ring_regression PRIVATE
            -Wl,--gc-sections
        )
    endif()
    add_test(NAME gameplay_input_snapshot_ring_regression
        COMMAND gameplay_input_snapshot_ring_regression
    )

    add_executable(attack_target_red_flash_regression
        tests/attack_target_red_flash_regression.cpp
        src/ranker_gameplay_input_actions.cpp
        src/ranker_input.cpp
        src/ranker_unit_animation.cpp
    )
    target_include_directories(attack_target_red_flash_regression PRIVATE
        include
    )
    if (MSVC)
        target_compile_options(attack_target_red_flash_regression PRIVATE /Gy)
        target_link_options(attack_target_red_flash_regression PRIVATE /OPT:REF)
    else()
        target_compile_options(attack_target_red_flash_regression PRIVATE
            -O1
            -ffunction-sections
            -fdata-sections
        )
        target_link_options(attack_target_red_flash_regression PRIVATE
            -Wl,--gc-sections
        )
    endif()
    add_test(NAME attack_target_red_flash_regression
        COMMAND attack_target_red_flash_regression
    )

    add_executable(gameplay_hotkey_dispatch_regression
        tests/gameplay_hotkey_dispatch_regression.cpp
        src/ranker_control_group_persistence.cpp
        src/ranker_ui_overlay.cpp
    )
    target_include_directories(gameplay_hotkey_dispatch_regression PRIVATE
        include
    )
    set_property(TARGET gameplay_hotkey_dispatch_regression PROPERTY
        INTERPROCEDURAL_OPTIMIZATION TRUE
    )
    if (MSVC)
        target_compile_options(gameplay_hotkey_dispatch_regression PRIVATE /Gy)
        target_link_options(gameplay_hotkey_dispatch_regression PRIVATE /OPT:REF)
    else()
        target_compile_options(gameplay_hotkey_dispatch_regression PRIVATE
            -O1
            -ffunction-sections
            -fdata-sections
        )
        target_link_options(gameplay_hotkey_dispatch_regression PRIVATE
            -Wl,--gc-sections
        )
    endif()
    add_test(NAME gameplay_hotkey_dispatch_regression
        COMMAND gameplay_hotkey_dispatch_regression
    )

    add_executable(gameplay_chat_regression
        tests/gameplay_chat_regression.cpp
    )
    target_include_directories(gameplay_chat_regression PRIVATE
        include
    )
    add_test(NAME gameplay_chat_regression
        COMMAND gameplay_chat_regression
    )

    add_executable(edge_scroll_parity_regression
        tests/edge_scroll_parity_regression.cpp
        src/ranker_control_group_persistence.cpp
        src/ranker_resource_store.cpp
        src/ranker_ui_overlay.cpp
    )
    target_include_directories(edge_scroll_parity_regression PRIVATE
        include
    )
    set_property(TARGET edge_scroll_parity_regression PROPERTY
        INTERPROCEDURAL_OPTIMIZATION TRUE
    )
    if (MSVC)
        target_compile_options(edge_scroll_parity_regression PRIVATE /Gy)
        target_link_options(edge_scroll_parity_regression PRIVATE /OPT:REF)
    else()
        target_compile_options(edge_scroll_parity_regression PRIVATE
            -O1
            -ffunction-sections
            -fdata-sections
        )
        target_link_options(edge_scroll_parity_regression PRIVATE
            -Wl,--gc-sections
        )
    endif()
    add_test(NAME edge_scroll_parity_regression
        COMMAND edge_scroll_parity_regression
    )

    add_executable(gameplay_script_escape_regression
        tests/gameplay_script_escape_regression.cpp
        src/ranker_gameplay_script_dialog.cpp
    )
    target_include_directories(gameplay_script_escape_regression PRIVATE
        include
    )
    if (MSVC)
        target_compile_options(gameplay_script_escape_regression PRIVATE /Gy)
        target_link_options(gameplay_script_escape_regression PRIVATE /OPT:REF)
    else()
        target_compile_options(gameplay_script_escape_regression PRIVATE
            -ffunction-sections
            -fdata-sections
        )
        target_link_options(gameplay_script_escape_regression PRIVATE
            -Wl,--gc-sections
        )
    endif()
    add_test(NAME gameplay_script_escape_regression
        COMMAND gameplay_script_escape_regression
    )

    add_executable(control_group_persistence_regression
        tests/control_group_persistence_regression.cpp
        src/ranker_control_group_persistence.cpp
    )
    target_include_directories(control_group_persistence_regression PRIVATE
        include
    )
    add_test(NAME control_group_persistence_regression
        COMMAND control_group_persistence_regression
    )

    add_executable(movement_modifier_command_flag_regression
        tests/movement_modifier_command_flag_regression.cpp
        src/ranker_production_orders.cpp
        src/ranker_unit_commands.cpp
        src/ranker_unit_target_helpers.cpp
        src/ranker_unit_damage.cpp
        src/ranker_unit_equipment.cpp
        src/ranker_unit_action.cpp
        src/ranker_unit_movement.cpp
        src/ranker_unit_spatial_index.cpp
        src/ranker_unit_lifecycle.cpp
        src/ranker_gameplay_visibility.cpp
        src/ranker_map_effects.cpp
    )
    target_include_directories(movement_modifier_command_flag_regression PRIVATE
        include
    )
    set_property(TARGET movement_modifier_command_flag_regression PROPERTY
        INTERPROCEDURAL_OPTIMIZATION TRUE
    )
    if (MSVC)
        target_compile_options(movement_modifier_command_flag_regression PRIVATE /Gy)
        target_link_options(movement_modifier_command_flag_regression PRIVATE /OPT:REF)
    else()
        target_compile_options(movement_modifier_command_flag_regression PRIVATE
            -O1
            -ffunction-sections
            -fdata-sections
        )
        target_link_options(movement_modifier_command_flag_regression PRIVATE
            -Wl,--gc-sections
        )
    endif()
    add_test(NAME movement_modifier_command_flag_regression
        COMMAND movement_modifier_command_flag_regression
    )

    add_executable(unit_effect_intrusive_iteration_regression
        tests/unit_effect_intrusive_iteration_regression.cpp
        src/ranker_production_orders.cpp
        src/ranker_unit_commands.cpp
        src/ranker_unit_target_helpers.cpp
        src/ranker_unit_damage.cpp
        src/ranker_unit_equipment.cpp
        src/ranker_unit_action.cpp
        src/ranker_unit_movement.cpp
        src/ranker_unit_spatial_index.cpp
        src/ranker_unit_lifecycle.cpp
        src/ranker_gameplay_visibility.cpp
        src/ranker_map_effects.cpp
    )
    target_include_directories(unit_effect_intrusive_iteration_regression PRIVATE
        include
    )
    set_property(TARGET unit_effect_intrusive_iteration_regression PROPERTY
        INTERPROCEDURAL_OPTIMIZATION TRUE
    )
    if (MSVC)
        target_compile_options(unit_effect_intrusive_iteration_regression PRIVATE /Gy)
        target_link_options(unit_effect_intrusive_iteration_regression PRIVATE /OPT:REF)
    else()
        target_compile_options(unit_effect_intrusive_iteration_regression PRIVATE
            -O1
            -ffunction-sections
            -fdata-sections
        )
        target_link_options(unit_effect_intrusive_iteration_regression PRIVATE
            -Wl,--gc-sections
        )
    endif()
    add_test(NAME unit_effect_intrusive_iteration_regression
        COMMAND unit_effect_intrusive_iteration_regression
    )

    add_executable(unit_damage_construction_multiplier_regression
        tests/unit_damage_construction_multiplier_regression.cpp
        src/ranker_unit_damage.cpp
    )
    target_include_directories(unit_damage_construction_multiplier_regression PRIVATE
        include
    )
    add_test(NAME unit_damage_construction_multiplier_regression
        COMMAND unit_damage_construction_multiplier_regression
    )

    add_executable(scenario_serialized_duplicate_regression
        tests/scenario_serialized_duplicate_regression.cpp
    )
    target_include_directories(scenario_serialized_duplicate_regression PRIVATE
        include
    )
    add_test(NAME scenario_serialized_duplicate_regression
        COMMAND scenario_serialized_duplicate_regression
    )

endif()

# This parity harness is independent from the historical out-of-tree regression
# set above, whose source bundle is optional.  It can therefore be configured
# and run against a minimal reconstruction checkout by itself.
option(RANKER_BUILD_MIXED_UNIT_SKILL_PARITY_TEST
    "Build the ranker.exe mixed-unit skill parity regression" OFF)
if (RANKER_BUILD_MIXED_UNIT_SKILL_PARITY_TEST)
    enable_testing()
    add_executable(mixed_unit_skill_original_parity_regression
        tests/mixed_unit_skill_original_parity_regression.cpp
        src/ranker_gameplay_production_actions.cpp
    )
    target_include_directories(mixed_unit_skill_original_parity_regression PRIVATE
        include
    )
    set_property(TARGET mixed_unit_skill_original_parity_regression PROPERTY
        INTERPROCEDURAL_OPTIMIZATION TRUE
    )
    if (MSVC)
        target_compile_options(mixed_unit_skill_original_parity_regression PRIVATE /Gy)
        target_link_options(mixed_unit_skill_original_parity_regression PRIVATE /OPT:REF)
    else()
        target_compile_options(mixed_unit_skill_original_parity_regression PRIVATE
            -O1
            -ffunction-sections
            -fdata-sections
        )
        target_link_options(mixed_unit_skill_original_parity_regression PRIVATE
            -Wl,--gc-sections
        )
    endif()
    add_test(NAME mixed_unit_skill_original_parity_regression
        COMMAND mixed_unit_skill_original_parity_regression
    )
endif()

option(RANKER_BUILD_UDP_BURST_REGRESSION_TEST
    "Build the legacy UDP large-datagram queue regression" OFF)
if (RANKER_BUILD_UDP_BURST_REGRESSION_TEST)
    enable_testing()
    add_executable(legacy_udp_burst_regression
        tests/legacy_udp_burst_regression.cpp
        src/ranker_network.cpp
    )
    target_include_directories(legacy_udp_burst_regression PRIVATE include)
    if (MSVC)
        target_compile_options(legacy_udp_burst_regression PRIVATE /Gy)
        target_link_options(legacy_udp_burst_regression PRIVATE /OPT:REF)
    else()
        target_compile_options(legacy_udp_burst_regression PRIVATE
            -O1 -ffunction-sections -fdata-sections)
        target_link_options(legacy_udp_burst_regression PRIVATE
            -Wl,--gc-sections)
    endif()
    if (WIN32)
        target_link_libraries(legacy_udp_burst_regression PRIVATE wsock32 winmm)
    endif()
    add_test(NAME legacy_udp_burst_regression
        COMMAND legacy_udp_burst_regression)
endif()

option(RANKER_BUILD_MODE1_PARTIAL_ROUND_RECEIVE_TEST
    "Build the mode-1 partial-round transport receive regression" OFF)
if (RANKER_BUILD_MODE1_PARTIAL_ROUND_RECEIVE_TEST)
    enable_testing()
    add_executable(mode1_partial_round_receive_regression
        tests/mode1_partial_round_receive_regression.cpp
        src/ranker_reliable_packets.cpp
    )
    target_include_directories(mode1_partial_round_receive_regression PRIVATE
        include
    )
    add_test(NAME mode1_partial_round_receive_regression
        COMMAND mode1_partial_round_receive_regression)
endif()

option(RANKER_BUILD_MODE1_AUX_VECTOR_PARITY_TEST
    "Build the mode-1 subtype-08 aux-vector parity regression" OFF)
if (RANKER_BUILD_MODE1_AUX_VECTOR_PARITY_TEST)
    enable_testing()
    add_executable(mode1_aux_vector_parity_regression
        tests/mode1_aux_vector_parity_regression.cpp
    )
    target_include_directories(mode1_aux_vector_parity_regression PRIVATE
        include
    )
    add_test(NAME mode1_aux_vector_parity_regression
        COMMAND mode1_aux_vector_parity_regression)
endif()

option(RANKER_BUILD_EQUIPMENT_REPLACEMENT_PARITY_TEST
    "Build the equipment type-replacement visibility parity regression" OFF)
if (RANKER_BUILD_EQUIPMENT_REPLACEMENT_PARITY_TEST)
    enable_testing()
    add_executable(unit_equipment_type_replacement_visibility_regression
        tests/unit_equipment_type_replacement_visibility_regression.cpp
    )
    target_include_directories(
        unit_equipment_type_replacement_visibility_regression PRIVATE include)
    if (MSVC)
        target_compile_options(
            unit_equipment_type_replacement_visibility_regression PRIVATE /Gy)
        target_link_options(
            unit_equipment_type_replacement_visibility_regression PRIVATE /OPT:REF)
    else()
        target_compile_options(
            unit_equipment_type_replacement_visibility_regression PRIVATE
            -O1 -ffunction-sections -fdata-sections)
        target_link_options(
            unit_equipment_type_replacement_visibility_regression PRIVATE
            -Wl,--gc-sections)
    endif()
    add_test(NAME unit_equipment_type_replacement_visibility_regression
        COMMAND unit_equipment_type_replacement_visibility_regression)
endif()

option(RANKER_BUILD_SUBTYPE01_CANCEL_PARITY_TEST
    "Build the subtype-01 latest-production cancel parity regression" OFF)
if (RANKER_BUILD_SUBTYPE01_CANCEL_PARITY_TEST)
    enable_testing()
    add_executable(subtype01_latest_cancel_parity_regression
        tests/subtype01_latest_cancel_parity_regression.cpp
    )
    target_include_directories(
        subtype01_latest_cancel_parity_regression PRIVATE include)
    add_test(NAME subtype01_latest_cancel_parity_regression
        COMMAND subtype01_latest_cancel_parity_regression)
endif()

option(RANKER_BUILD_IME_INPUT_REGRESSION_TEST
    "Build the Korean IME process-key input regression" OFF)
if (RANKER_BUILD_IME_INPUT_REGRESSION_TEST)
    enable_testing()
    add_executable(ime_process_key_regression
        tests/ime_process_key_regression.cpp
        src/ranker_input.cpp
    )
    target_include_directories(ime_process_key_regression PRIVATE
        include
    )
    if (MSVC)
        target_compile_options(ime_process_key_regression PRIVATE /Gy)
        target_link_options(ime_process_key_regression PRIVATE /OPT:REF)
    else()
        target_compile_options(ime_process_key_regression PRIVATE
            -O1
            -ffunction-sections
            -fdata-sections
        )
        target_link_options(ime_process_key_regression PRIVATE
            -Wl,--gc-sections
        )
    endif()
    add_test(NAME ime_process_key_regression
        COMMAND ime_process_key_regression)
endif()

option(RANKER_BUILD_READABILITY_REGRESSION_TESTS
    "Build focused regressions for shared readability helpers" OFF)
if (RANKER_BUILD_READABILITY_REGRESSION_TESTS)
    enable_testing()
    add_executable(wrapped_u32_conversion_regression
        tests/wrapped_u32_conversion_regression.cpp
    )
    target_include_directories(wrapped_u32_conversion_regression PRIVATE
        include
    )
    add_test(NAME wrapped_u32_conversion_regression
        COMMAND wrapped_u32_conversion_regression)

    add_executable(frontend_window_placement_regression
        tests/frontend_window_placement_regression.cpp
        src/ranker_client_config.cpp
    )
    target_include_directories(frontend_window_placement_regression PRIVATE
        include
    )
    add_test(NAME frontend_window_placement_regression
        COMMAND frontend_window_placement_regression)
endif()

option(RANKER_BUILD_END_CONDITION_REGRESSION_TESTS
    "Build focused gameplay end-condition regressions" OFF)
if (RANKER_BUILD_END_CONDITION_REGRESSION_TESTS)
    enable_testing()
    add_executable(gameplay_use_map_end_condition_regression
        tests/gameplay_use_map_end_condition_regression.cpp
        src/ranker_gameplay_end_conditions.cpp
    )
    target_include_directories(gameplay_use_map_end_condition_regression PRIVATE
        include
    )
    add_test(NAME gameplay_use_map_end_condition_regression
        COMMAND gameplay_use_map_end_condition_regression)
endif()

option(RANKER_BUILD_GAME_LOOP_CATCHUP_REGRESSION_TESTS
    "Build focused catch-up presentation pacing regressions" OFF)
if (RANKER_BUILD_GAME_LOOP_CATCHUP_REGRESSION_TESTS)
    enable_testing()
    add_executable(game_loop_catchup_presentation_regression
        tests/game_loop_catchup_presentation_regression.cpp
    )
    target_include_directories(game_loop_catchup_presentation_regression PRIVATE
        include
    )
    add_test(NAME game_loop_catchup_presentation_regression
        COMMAND game_loop_catchup_presentation_regression)
endif()

option(RANKER_BUILD_GAMEPLAY_SELECTION_REGRESSION_TESTS
    "Build focused gameplay selection modifier regressions" OFF)
if (RANKER_BUILD_GAMEPLAY_SELECTION_REGRESSION_TESTS)
    enable_testing()
    add_executable(gameplay_selection_modifier_regression
        tests/gameplay_selection_modifier_regression.cpp
    )
    target_include_directories(gameplay_selection_modifier_regression PRIVATE
        include
    )
    if (MSVC)
        target_compile_options(gameplay_selection_modifier_regression PRIVATE /Gy)
        target_link_options(gameplay_selection_modifier_regression PRIVATE /OPT:REF)
    else()
        target_compile_options(gameplay_selection_modifier_regression PRIVATE
            -O1 -ffunction-sections -fdata-sections
        )
        target_link_options(gameplay_selection_modifier_regression PRIVATE
            -Wl,--gc-sections
        )
    endif()
    add_test(NAME gameplay_selection_modifier_regression
        COMMAND gameplay_selection_modifier_regression)
endif()

option(RANKER_BUILD_RENDER_144FPS_REGRESSION_TESTS
    "Build focused 144 FPS presentation and interpolation regressions" OFF)
if (RANKER_BUILD_RENDER_144FPS_REGRESSION_TESTS)
    enable_testing()
    add_executable(render_144fps_regression
        tests/render_144fps_regression.cpp
        src/ranker_palette_cache.cpp
        src/ranker_resource_store.cpp
        src/ranker_sprite_renderer.cpp
        src/ranker_trc.cpp
        src/ranker_unit_render_queue.cpp
        src/ranker_visual_animation.cpp
        src/ranker_visual_animation_archive.cpp
    )
    target_include_directories(render_144fps_regression PRIVATE
        include
    )
    target_link_libraries(render_144fps_regression PRIVATE
        zlib113
    )
    if (MSVC)
        target_compile_options(render_144fps_regression PRIVATE /Gy)
        target_link_options(render_144fps_regression PRIVATE /OPT:REF)
    else()
        target_compile_options(render_144fps_regression PRIVATE
            -ffunction-sections -fdata-sections
        )
        target_link_options(render_144fps_regression PRIVATE
            -Wl,--gc-sections
        )
    endif()
    add_test(NAME render_144fps_regression
        COMMAND render_144fps_regression)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME animation_144hz_generator_regression
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/animation_144hz_generator_regression.py)
endif()

option(RANKER_BUILD_REPLAY_SAVE_CONTROLS_REGRESSION_TESTS
    "Build focused replay result save-control regressions" OFF)
if (RANKER_BUILD_REPLAY_SAVE_CONTROLS_REGRESSION_TESTS)
    enable_testing()
    add_executable(replay_save_controls_regression
        tests/replay_save_controls_regression.cpp
        src/ranker_replay.cpp
    )
    target_include_directories(replay_save_controls_regression PRIVATE
        include
    )
    add_test(NAME replay_save_controls_regression
        COMMAND replay_save_controls_regression)
endif()

add_executable(ranker_rebuild WIN32
    resources/ranker_resources.rc
    src/main.cpp
    src/imports.cpp
    src/ranker_client_config.cpp
    src/ranker_winmain.cpp
    src/ranker_cd_audio.cpp
    src/ranker_directx.cpp
    src/ranker_d3d9_presentation.cpp
    src/ranker_cursor.cpp
    src/ranker_directplay.cpp
    src/ranker_reliable_packets.cpp
    src/ranker_network.cpp
    src/ranker_screenshot.cpp
    src/ranker_miles.cpp
    src/ranker_mixer.cpp
    src/ranker_palette_cache.cpp
    src/ranker_production_orders.cpp
    src/ranker_resource_store.cpp
    src/ranker_replay.cpp
    src/ranker_replay_archive.cpp
    src/ranker_p2p_flight_recorder.cpp
    src/ranker_player_slots.cpp
    src/ranker_dpg_archive.cpp
    src/ranker_owner_ai.cpp
    src/ranker_runtime_resources.cpp
    src/ranker_input.cpp
    src/ranker_control_group_persistence.cpp
    src/ranker_legacy_file.cpp
    src/ranker_setup_data.cpp
    src/ranker_startup_environment.cpp
    src/ranker_legacy_environment.cpp
    src/ranker_bitmap_resource.cpp
    src/ranker_bitmap_icon_collection.cpp
    src/ranker_raw_indexed_bitmap.cpp
    src/ranker_icon_strips.cpp
    src/ranker_bitmap_tile_sheet.cpp
    src/ranker_icon_marked_text.cpp
    src/ranker_image_controls.cpp
    src/ranker_custom_scroll.cpp
    src/ranker_string_selector.cpp
    src/ranker_connect_frontend.cpp
    src/ranker_wizard_login.cpp
    src/ranker_change_password.cpp
    src/ranker_ipx_lobby.cpp
    src/ranker_ipx_frontend.cpp
    src/ranker_free_server_lobby.cpp
    src/ranker_emoticon_popup.cpp
    src/ranker_ole_image_data.cpp
    src/ranker_mfc_runtime.cpp
    src/ranker_mfc_core.cpp
    src/ranker_mfc_data_exchange.cpp
    src/ranker_mfc_dialog_template.cpp
    src/ranker_mfc_debug.cpp
    src/ranker_mfc_exceptions.cpp
    src/ranker_mfc_file.cpp
    src/ranker_mfc_geometry.cpp
    src/ranker_mfc_memory.cpp
    src/ranker_mfc_object_runtime.cpp
    src/ranker_mfc_string.cpp
    src/ranker_mfc_time.cpp
    src/ranker_mfc_common_controls.cpp
    src/ranker_mfc_archive_stream.cpp
    src/ranker_mfc_collections.cpp
    src/ranker_ole_variant.cpp
    src/ranker_ole_datetime.cpp
    src/ranker_ole_typelib.cpp
    src/ranker_p2p_lobby.cpp
    src/ranker_link_lobby.cpp
    src/ranker_online_dialogs.cpp
    src/ranker_online_lobby.cpp
    src/ranker_search_lobby.cpp
    src/ranker_account_profile.cpp
    src/ranker_view_rank.cpp
    src/ranker_player_profile.cpp
    src/ranker_avatar_window.cpp
    src/ranker_barter_window.cpp
    src/ranker_memo_window.cpp
    src/ranker_figs_address_book.cpp
    src/ranker_create_game.cpp
    src/ranker_replay_dialogs.cpp
    src/ranker_change_lobby.cpp
    src/ranker_game_loop.cpp
    src/ranker_game_session_tables.cpp
    src/ranker_gameplay_script.cpp
    src/ranker_gameplay_unit_names.cpp
    src/ranker_gameplay_script_dialog.cpp
    src/ranker_gameplay_packets.cpp
    src/ranker_gameplay_sound.cpp
    src/ranker_gameplay_end_conditions.cpp
    src/ranker_gameplay_visibility.cpp
    src/ranker_gameplay_session_runtime.cpp
    src/ranker_gameplay_frame_render.cpp
    src/ranker_gameplay_result_screen.cpp
    src/ranker_gameplay_menu_control.cpp
    src/ranker_gameplay_session_flow.cpp
    src/ranker_gameplay_context_cursor.cpp
    src/ranker_gameplay_input_actions.cpp
    src/ranker_gameplay_production_actions.cpp
    src/ranker_gameplay_tooltips.cpp
    src/ranker_gameplay_terrain_layout.cpp
    src/ranker_minimap_preview.cpp
    src/ranker_frontend_layout.cpp
    src/ranker_frontend_startup.cpp
    src/ranker_frontend_stage_flow.cpp
    src/ranker_frontend_routes.cpp
    src/ranker_map_brush.cpp
    src/ranker_map_effects.cpp
    src/ranker_unit_damage.cpp
    src/ranker_evil_portal_release.cpp
    src/ranker_unit_action.cpp
    src/ranker_unit_support_effects.cpp
    src/ranker_unit_target_helpers.cpp
    src/ranker_unit_targeting.cpp
    src/ranker_unit_render_queue.cpp
    src/ranker_unit_animation.cpp
    src/ranker_visual_animation.cpp
    src/ranker_visual_animation_archive.cpp
    src/ranker_unit_movement.cpp
    src/ranker_unit_spatial_index.cpp
    src/ranker_unit_commands.cpp
    src/ranker_gameplay_aliases.cpp
    src/ranker_zlib_aliases.cpp
    src/ranker_mfc_aliases.cpp
    src/ranker_unit_equipment.cpp
    src/ranker_unit_lifecycle.cpp
    src/ranker_sprite_renderer.cpp
    src/ranker_win32_compat.cpp
    src/ranker_crt_runtime.cpp
    src/ranker_system_ui.cpp
    src/ranker_ui_screen.cpp
    src/ranker_ui_overlay.cpp
    src/ranker_text_renderer.cpp
    src/ranker_trc.cpp
    src/ranker_text_tables.cpp
    src/ranker_indexed_text_table.cpp
    src/ranker_cycle_timers.cpp
    src/stubs.cpp
)

add_custom_command(TARGET ranker_rebuild POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/config/ranker_client.ini
        $<TARGET_FILE_DIR:ranker_rebuild>/ranker_client.ini
    COMMENT "Copying ranker client display configuration"
)

add_custom_command(TARGET ranker_rebuild POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/animation_144hz/Jw2_09_144hz.rfa
        $<TARGET_FILE_DIR:ranker_rebuild>/Jw2_09_144hz.rfa
    COMMENT "Copying pre-generated 144 Hz unit animation archive"
)

target_include_directories(ranker_rebuild PRIVATE
    include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(ranker_rebuild PRIVATE zlib113)

if (WIN32)
    target_link_libraries(ranker_rebuild PRIVATE
        user32
        gdi32
        winmm
        ole32
        oleaut32
        comdlg32
        comctl32
        shell32
        shlwapi
        advapi32
        wsock32
        ddraw
        d3d9
        dsound
        dxguid
        gdiplus
        imm32
    )

    find_file(RANKER_ZLIB_RUNTIME_DLL
        NAMES zlib1.dll libz.dll zlib.dll
        PATHS ENV PATH
    )
    if (RANKER_ZLIB_RUNTIME_DLL)
        add_custom_command(TARGET ranker_rebuild POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${RANKER_ZLIB_RUNTIME_DLL}"
                "$<TARGET_FILE_DIR:ranker_rebuild>"
            VERBATIM
        )
    endif()

    add_custom_command(TARGET ranker_rebuild POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/main_menu_bink_fallback_565.bin"
            "$<TARGET_FILE_DIR:ranker_rebuild>"
        VERBATIM
    )
endif()
