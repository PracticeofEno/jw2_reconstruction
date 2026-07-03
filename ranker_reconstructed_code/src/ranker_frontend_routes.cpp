#include "ranker_frontend_routes.h"

namespace ranker {
namespace {

constexpr u32 kFrontendCreateGameRouteMessage = 0x408;

FrontendCreateGameRouteState g_frontend_create_game_route_state;

void record_post(FrontendCreateGameRouteState& state, u32 message, u32 wparam, u32 lparam,
    FrontendRoutePostMessageCallback post) {
    state.last_message = message;
    state.last_wparam = wparam;
    state.last_lparam = lparam;
    if (post != nullptr) {
        post(state, message, wparam, lparam);
    }
}

} // namespace

FrontendCreateGameRouteState& frontend_create_game_route_state() {
    return g_frontend_create_game_route_state;
}

void RequestNetworkAiCreateGameRoute(FrontendCreateGameRouteState& state,
    const char* profile_name, FrontendRoutePostMessageCallback post) {
    state.network_ai_profile_override = true;
    state.create_game_open_requested = false;
    state.route_mode = 6;
    state.profile_name = profile_name != nullptr ? profile_name : "";
    state.direct_route_requested = false;
    state.resume_requested = false;
    record_post(state, kFrontendCreateGameRouteMessage, 0, 0, post);
}

void RequestCreateGameRoute(FrontendCreateGameRouteState& state,
    FrontendRoutePostMessageCallback post) {
    state.direct_route_requested = false;
    state.resume_requested = true;
    record_post(state, kFrontendCreateGameRouteMessage, 0, 1, post);
}

void OpenCreateGameWindowRoute(FrontendCreateGameRouteState& state,
    FrontendRouteOpenCreateGameCallback open_create_game) {
    state.create_game_open_requested = true;
    if (open_create_game != nullptr) {
        open_create_game(state);
    }
}

}
