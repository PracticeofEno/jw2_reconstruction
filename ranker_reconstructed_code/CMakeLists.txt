cmake_minimum_required(VERSION 3.20)
project(ranker_reconstruction LANGUAGES C CXX RC)

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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

if (BUILD_TESTING)
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

    add_executable(production_order_packet_owner_regression
        tests/production_order_packet_owner_regression.cpp
    )
    target_include_directories(production_order_packet_owner_regression PRIVATE
        include
    )
    add_test(NAME production_order_packet_owner_regression
        COMMAND production_order_packet_owner_regression
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
endif()

add_executable(ranker_rebuild WIN32
    resources/ranker_resources.rc
    src/main.cpp
    src/imports.cpp
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
    src/ranker_player_slots.cpp
    src/ranker_dpg_archive.cpp
    src/ranker_owner_ai.cpp
    src/ranker_runtime_resources.cpp
    src/ranker_input.cpp
    src/ranker_control_group_persistence.cpp
    src/ranker_legacy_file.cpp
    src/ranker_setup_data.cpp
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
    src/ranker_frontend_layout.cpp
    src/ranker_frontend_startup.cpp
    src/ranker_frontend_stage_flow.cpp
    src/ranker_frontend_routes.cpp
    src/ranker_map_brush.cpp
    src/ranker_map_effects.cpp
    src/ranker_unit_damage.cpp
    src/ranker_unit_action.cpp
    src/ranker_unit_support_effects.cpp
    src/ranker_unit_target_helpers.cpp
    src/ranker_unit_targeting.cpp
    src/ranker_unit_render_queue.cpp
    src/ranker_unit_animation.cpp
    src/ranker_unit_movement.cpp
    src/ranker_unit_spatial_index.cpp
    src/ranker_unit_commands.cpp
    src/ranker_original_aliases.cpp
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
