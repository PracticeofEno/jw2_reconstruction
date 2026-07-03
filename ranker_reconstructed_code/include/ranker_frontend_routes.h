#pragma once

#include "ranker_types.h"

#include <string>

namespace ranker {

struct FrontendCreateGameRouteState {
    bool network_ai_profile_override = false;
    bool create_game_open_requested = false;
    bool direct_route_requested = false;
    bool resume_requested = false;
    u32 route_mode = 0;
    u32 last_message = 0;
    u32 last_wparam = 0;
    u32 last_lparam = 0;
    std::string profile_name;
};

using FrontendRoutePostMessageCallback = void (*)(FrontendCreateGameRouteState& state,
    u32 message, u32 wparam, u32 lparam);
using FrontendRouteOpenCreateGameCallback = void (*)(FrontendCreateGameRouteState& state);

FrontendCreateGameRouteState& frontend_create_game_route_state();
void RequestNetworkAiCreateGameRoute(FrontendCreateGameRouteState& state,
    const char* profile_name = nullptr, FrontendRoutePostMessageCallback post = nullptr);
void RequestCreateGameRoute(FrontendCreateGameRouteState& state,
    FrontendRoutePostMessageCallback post = nullptr);
void OpenCreateGameWindowRoute(FrontendCreateGameRouteState& state,
    FrontendRouteOpenCreateGameCallback open_create_game = nullptr);

}
