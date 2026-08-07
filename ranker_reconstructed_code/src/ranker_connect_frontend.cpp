#include "ranker_connect_frontend.h"

#ifdef _WIN32

#include "ranker_frontend_layout.h"
#include "ranker_gameplay_sound.h"
#include "ranker_system_ui.h"
#include "ranker_text_tables.h"
#include "ranker_trc.h"
#include "ranker_winmain.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>

namespace ranker {
namespace {

constexpr DWORD kWindowStyleFullscreen = WS_POPUP;
constexpr DWORD kWindowStyleWindowed = WS_CHILD | WS_CLIPCHILDREN |
    WS_CLIPSIBLINGS;
constexpr DWORD kDescriptionButtonStyle = 0x5800000b;
constexpr COLORREF kConnectTextWhite = RGB(255, 255, 255);
constexpr COLORREF kConnectSoftWhite = RGB(250, 250, 250);
constexpr COLORREF kConnectBlack = RGB(0, 0, 0);
constexpr int kConnectFrontendClickOnlyButtonId = 0x7d6;
constexpr u32 kConnectFrontendWindowFontIndex = 1;
constexpr u32 kConnectFrontendDescriptionFontIndex = 0;
constexpr std::size_t kStartupDirectPlayInitializationFailureRow = 61;
constexpr std::array<u32, 4> kConnectFrontendEnabledSignature{0, 0, 0, 0};
constexpr std::array<u32, 4> kConnectFrontendModeDescriptionRecords{
    0x154, 0x159, 0x158, 0x155};

ConnectFrontendState g_connect_frontend_state;
bool g_layout_table_shutdown_registered = false;
bool g_background_shutdown_registered = false;
bool g_description_shutdown_registered = false;
std::array<bool, 7> g_button_shutdown_registered{};

struct ConnectButtonDefinition {
    std::size_t slot = 0;
    int id = 0;
    const char* text = "";
    u32 normal_record = 0;
    u32 pressed_record = 0;
};

const ConnectButtonDefinition kButtonDefinitions[] = {
    {0, kConnectFrontendWizardButtonId, "&Wizard soft net", 1, 2},
    {1, kConnectFrontendP2PButtonId, "&Peer to Peer", 0x0a, 0x0b},
    {2, kConnectFrontendFreeServerButtonId, "&Free Internet Game Server", 7, 8},
    {3, kConnectFrontendIpxButtonId, "Local Area Network (&IPX)", 4, 5},
    {4, kConnectFrontendDescriptionButtonId, "", 0x0d, 0},
    {5, kConnectFrontendOkButtonId, "", 0x0f, 0x0e},
    {6, kConnectFrontendCancelButtonId, "&Cancel", 0x11, 0x10},
};

struct FrontendLayoutTableOwner {
    FrontendLayoutRectTable table{};

    ~FrontendLayoutTableOwner() {
        ReleaseFrontendLayoutRectTable(table);
    }
};

void register_atexit_once(bool& registered, void (*callback)()) {
    if (!registered) {
        std::atexit(callback);
        registered = true;
    }
}

void destroy_connect_button_slot(std::size_t slot) {
    if (slot < g_connect_frontend_state.buttons.size()) {
        DestroyLegacyImageButtonControl(g_connect_frontend_state.buttons[slot]);
    }
}

void shutdown_global_layout_table() {
    DestroyIndexedTextTableContext(g_connect_frontend_state.layout_table);
}

void shutdown_global_background() {
    ReleaseBitmapMemoryResource(g_connect_frontend_state.background);
}

void shutdown_global_description() {
    ReleaseBitmapMemoryResource(g_connect_frontend_state.description_background);
}

void shutdown_global_wizard_button() {
    destroy_connect_button_slot(0);
}

void shutdown_global_p2p_button() {
    destroy_connect_button_slot(1);
}

void shutdown_global_free_server_button() {
    destroy_connect_button_slot(2);
}

void shutdown_global_ipx_button() {
    destroy_connect_button_slot(3);
}

void shutdown_global_description_button() {
    destroy_connect_button_slot(4);
}

void shutdown_global_ok_button() {
    destroy_connect_button_slot(5);
}

void shutdown_global_cancel_button() {
    destroy_connect_button_slot(6);
}

LRESULT CALLBACK connect_frontend_window_proc(HWND hwnd, UINT message, WPARAM wparam,
    LPARAM lparam) {
    return HandleConnectFrontendWindowMessage(g_connect_frontend_state, hwnd, message,
        wparam, lparam);
}

LRESULT CALLBACK connect_frontend_button_proc(HWND hwnd, UINT message, WPARAM wparam,
    LPARAM lparam) {
    return HandleConnectFrontendButtonMessage(g_connect_frontend_state, hwnd, message,
        wparam, lparam);
}

ConnectFrontendLayoutRect layout_at(const FrontendLayoutRectTable& table,
    std::size_t index) {
    if (table.rects != nullptr && index < table.count) {
        const FrontendLayoutRect& rect = table.rects[index];
        return {rect.x, rect.y, rect.width, rect.height};
    }
    return ConnectFrontendLayoutRect{};
}

std::size_t mode_index(ConnectFrontendMode mode) {
    return static_cast<std::size_t>(mode);
}

LegacyImageButtonControl* button_for_id(ConnectFrontendState& state, int id) {
    for (auto& button : state.buttons) {
        if (GetLegacyImageButtonWindow(button) != nullptr &&
            GetWindowLongPtrA(GetLegacyImageButtonWindow(button), GWLP_ID) == id) {
            return &button;
        }
    }
    return nullptr;
}

const LegacyImageButtonControl* button_for_id(const ConnectFrontendState& state,
    int id) {
    for (const auto& button : state.buttons) {
        if (GetLegacyImageButtonWindow(button) != nullptr &&
            GetWindowLongPtrA(GetLegacyImageButtonWindow(button), GWLP_ID) == id) {
            return &button;
        }
    }
    return nullptr;
}

ConnectFrontendMode mode_for_button_id(int id) {
    switch (id) {
    case kConnectFrontendP2PButtonId:
        return ConnectFrontendMode::PeerToPeer;
    case kConnectFrontendFreeServerButtonId:
        return ConnectFrontendMode::FreeInternetServer;
    case kConnectFrontendIpxButtonId:
        return ConnectFrontendMode::IpxLan;
    case kConnectFrontendWizardButtonId:
    default:
        return ConnectFrontendMode::WizardSoftNet;
    }
}

ConnectFrontendMode mode_from_active_transport(i32 mode) {
    switch (mode) {
    case 1:
        return ConnectFrontendMode::PeerToPeer;
    case 2:
        return ConnectFrontendMode::FreeInternetServer;
    case 3:
        return ConnectFrontendMode::IpxLan;
    default:
        return ConnectFrontendMode::WizardSoftNet;
    }
}

bool is_mode_button_id(int id) {
    return id == kConnectFrontendWizardButtonId ||
        id == kConnectFrontendP2PButtonId ||
        id == kConnectFrontendFreeServerButtonId ||
        id == kConnectFrontendIpxButtonId;
}

void set_button_proc(LegacyImageButtonControl& control) {
    HWND window = GetLegacyImageButtonWindow(control);
    if (window == nullptr) {
        return;
    }
    SetWindowLongPtrA(window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(connect_frontend_button_proc));
}

HFONT ui_font_or_default(u32 index) {
    HFONT font = GetUiFontHandle(index);
    if (font != nullptr) {
        return font;
    }
    return reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
}

void release_connect_resources(ConnectFrontendState& state) {
    ReleaseBitmapMemoryResource(state.background);
    ReleaseBitmapMemoryResource(state.description_background);
    for (auto& button : state.buttons) {
        ReleaseLegacyImageButtonControlWindow(button);
    }
    for (std::string& description : state.mode_descriptions) {
        description.clear();
        description.shrink_to_fit();
    }
}

void install_accelerators(ConnectFrontendState& state) {
    state.active_accelerators = LoadAcceleratorsA(state.instance,
        MAKEINTRESOURCEA(kConnectFrontendAcceleratorResourceId));
    state.active_accelerator_window = state.window;
    SetActiveAcceleratorState(state.active_accelerator_window,
        state.active_accelerators);
}

void restore_accelerators(ConnectFrontendState& state) {
    const RankerMainWindowStateSnapshot active = RankerMainWindowState();
    SetActiveAcceleratorState(nullptr, active.active_accelerators);
    DestroyAcceleratorTable(active.active_accelerators);
    SetActiveAcceleratorState(nullptr, nullptr);
    state.active_accelerators = nullptr;
    state.active_accelerator_window = nullptr;
}

void show_connect_message(ConnectFrontendState& state, const char* text,
    COLORREF color) {
    state.last_message = text == nullptr ? "" : text;
    if (state.callbacks.show_message != nullptr && state.window != nullptr) {
        state.callbacks.show_message(state.window, state.last_message.c_str(), color);
    }
}

const char* startup_message_row(std::size_t index, const char* fallback) {
    const auto& rows = startup_text_tables().message_rows.rows;
    if (index < rows.size() && !rows[index].empty()) {
        return rows[index].data();
    }
    return fallback;
}

std::string format_startup_message_u32(
    std::size_t index, const char* fallback, u32 value) {
    char buffer[512]{};
    std::snprintf(buffer, sizeof(buffer), startup_message_row(index, fallback), value);
    return buffer;
}

void draw_description_text(ConnectFrontendState& state, const DRAWITEMSTRUCT& draw) {
    StretchBitmapMemoryResourceToDc(state.description_background, draw.hDC, 0, 0);
    RECT text_rect{15, 12, 400, 150};
    SetTextColor(draw.hDC, kConnectTextWhite);
    SetBkColor(draw.hDC, kConnectBlack);
    SetBkMode(draw.hDC, TRANSPARENT);
    const std::string& text = state.mode_descriptions[mode_index(state.selected_mode)];
    DrawTextA(draw.hDC, text.empty() ? state.last_message.c_str() : text.c_str(), -1,
        &text_rect, 0);
}

void draw_mode_button(ConnectFrontendState& state, const DRAWITEMSTRUCT& draw) {
    LegacyImageButtonControl* button = button_for_id(state, draw.CtlID);
    if (button == nullptr) {
        return;
    }

    const bool selected = mode_for_button_id(draw.CtlID) == state.selected_mode;
    StretchBitmapMemoryResourceToDc(selected ? button->normal_bitmap :
        button->pressed_bitmap, draw.hDC, 0, 0);
}

void launch_selected_mode(ConnectFrontendState& state) {
    SetRankerMainWindowFrontendMode(static_cast<u32>(state.selected_mode));
    switch (state.selected_mode) {
    case ConnectFrontendMode::WizardSoftNet:
        if (!IsConnectFrontendModeEnabled(state, state.selected_mode)) {
            break;
        }
        if (state.callbacks.open_wizard_soft_net != nullptr) {
            state.callbacks.open_wizard_soft_net(state);
        }
        break;
    case ConnectFrontendMode::PeerToPeer: {
        if (!IsConnectFrontendModeEnabled(state, state.selected_mode)) {
            break;
        }
        char host_name[0x100]{};
        char address[0x100]{};
        if (!ResolveLocalHostDisplayAddress(host_name, sizeof(host_name), address,
                sizeof(address))) {
            break;
        }
        if (StartLegacyUdpSocket(address,
                static_cast<u16>(state.configuration.p2p_udp_port)) ==
            INVALID_SOCKET) {
            state.p2p_udp_failed = true;
            const std::string message = format_startup_message_u32(94,
                "Unable to initialize UDP port %u for Peer To Peer games.",
                state.configuration.p2p_udp_port);
            show_connect_message(state, message.c_str(), kConnectSoftWhite);
            break;
        }
        state.p2p_udp_failed = false;
        if (state.callbacks.open_p2p_lobby != nullptr) {
            state.callbacks.open_p2p_lobby(state);
        }
        break;
    }
    case ConnectFrontendMode::FreeInternetServer:
        if (IsConnectFrontendModeEnabled(state, state.selected_mode) &&
            state.callbacks.open_free_server != nullptr) {
            state.callbacks.open_free_server(state);
        }
        break;
    case ConnectFrontendMode::IpxLan:
        if (IsConnectFrontendModeEnabled(state, state.selected_mode) &&
            state.callbacks.open_ipx_lobby != nullptr) {
            state.callbacks.open_ipx_lobby(state);
        }
        break;
    }
}

void shutdown_network_mode(ConnectFrontendState& state) {
    if (state.callbacks.shutdown_network_mode != nullptr) {
        state.callbacks.shutdown_network_mode(state);
        return;
    }
    CloseLegacyUdpSocket();
}

void split_lines(const std::vector<u8>& record, std::vector<std::string>& rows) {
    rows.clear();
    std::string current;
    for (u8 ch : record) {
        if (ch == '\0') {
            break;
        }
        if (ch == '\r' || ch == '\n') {
            if (!current.empty()) {
                rows.push_back(current);
                current.clear();
            }
            continue;
        }
        current.push_back(static_cast<char>(ch));
    }
    if (!current.empty()) {
        rows.push_back(current);
    }
}

bool first_c_string_equals_ascii_case_insensitive(
    const std::vector<u8>& record, const char* expected) {
    if (expected == nullptr) {
        return false;
    }
    const auto end = std::find(record.begin(), record.end(), '\0');
    const std::size_t length = static_cast<std::size_t>(
        std::distance(record.begin(), end));
    if (length != std::strlen(expected)) {
        return false;
    }
    for (std::size_t i = 0; i < length; ++i) {
        const int left = std::tolower(static_cast<unsigned char>(record[i]));
        const int right = std::tolower(static_cast<unsigned char>(expected[i]));
        if (left != right) {
            return false;
        }
    }
    return true;
}

void load_connect_mode_description_records(ConnectFrontendState& state) {
    for (std::size_t i = 0; i < state.mode_descriptions.size(); ++i) {
        std::vector<u8> record;
        state.mode_descriptions[i].clear();
        if (!LoadTrcRecordAlloc("Jw2_19.trc",
                kConnectFrontendModeDescriptionRecords[i], record, 1)) {
            continue;
        }
        state.mode_descriptions[i].assign(
            reinterpret_cast<const char*>(record.data()));
    }
}

} // namespace

ConnectFrontendState& connect_frontend_state() {
    return g_connect_frontend_state;
}

void ConstructConnectFrontendLayoutTable(ConnectFrontendState& state) {
    InitializeConnectFrontendLayoutTable(state);
    RegisterConnectFrontendLayoutTableShutdown(state);
}

void InitializeConnectFrontendLayoutTable(ConnectFrontendState& state) {
    InitializeIndexedTextTableContext(state.layout_table);
}

void RegisterConnectFrontendLayoutTableShutdown(ConnectFrontendState&) {
    register_atexit_once(g_layout_table_shutdown_registered,
        shutdown_global_layout_table);
}

void ShutdownConnectFrontendLayoutTable(ConnectFrontendState& state) {
    DestroyIndexedTextTableContext(state.layout_table);
}

void InitializeConnectFrontendBackgroundResourceAndShutdown(
    ConnectFrontendState& state) {
    InitializeConnectFrontendBackgroundBitmap(state);
    RegisterConnectFrontendBackgroundShutdown(state);
}

void InitializeConnectFrontendBackgroundBitmap(ConnectFrontendState& state) {
    InitializeBitmapMemoryResource(state.background);
}

void RegisterConnectFrontendBackgroundShutdown(ConnectFrontendState&) {
    register_atexit_once(g_background_shutdown_registered,
        shutdown_global_background);
}

void ShutdownConnectFrontendBackgroundBitmap(ConnectFrontendState& state) {
    ReleaseBitmapMemoryResource(state.background);
}

void InitializeConnectFrontendDescriptionResourceAndShutdown(
    ConnectFrontendState& state) {
    InitializeConnectFrontendDescriptionBitmap(state);
    RegisterConnectFrontendDescriptionShutdown(state);
}

void InitializeConnectFrontendDescriptionBitmap(ConnectFrontendState& state) {
    InitializeBitmapMemoryResource(state.description_background);
}

void RegisterConnectFrontendDescriptionShutdown(ConnectFrontendState&) {
    register_atexit_once(g_description_shutdown_registered,
        shutdown_global_description);
}

void ShutdownConnectFrontendDescriptionBitmap(ConnectFrontendState& state) {
    ReleaseBitmapMemoryResource(state.description_background);
}

void InitializeConnectFrontendWizardButtonSupport(ConnectFrontendState& state) {
    InitializeConnectFrontendWizardButton(state);
    RegisterConnectFrontendWizardButtonShutdown(state);
}

void InitializeConnectFrontendWizardButton(ConnectFrontendState& state) {
    InitializeLegacyImageButtonControl(state.buttons[0]);
}

void RegisterConnectFrontendWizardButtonShutdown(ConnectFrontendState&) {
    register_atexit_once(g_button_shutdown_registered[0],
        shutdown_global_wizard_button);
}

void ShutdownConnectFrontendWizardButton(ConnectFrontendState& state) {
    DestroyLegacyImageButtonControl(state.buttons[0]);
}

void InitializeConnectFrontendP2PButtonSupport(ConnectFrontendState& state) {
    InitializeConnectFrontendP2PButton(state);
    RegisterConnectFrontendP2PButtonShutdown(state);
}

void InitializeConnectFrontendP2PButton(ConnectFrontendState& state) {
    InitializeLegacyImageButtonControl(state.buttons[1]);
}

void RegisterConnectFrontendP2PButtonShutdown(ConnectFrontendState&) {
    register_atexit_once(g_button_shutdown_registered[1],
        shutdown_global_p2p_button);
}

void ShutdownConnectFrontendP2PButton(ConnectFrontendState& state) {
    DestroyLegacyImageButtonControl(state.buttons[1]);
}

void InitializeConnectFrontendFreeServerButtonSupport(ConnectFrontendState& state) {
    InitializeConnectFrontendFreeServerButton(state);
    RegisterConnectFrontendFreeServerButtonShutdown(state);
}

void InitializeConnectFrontendFreeServerButton(ConnectFrontendState& state) {
    InitializeLegacyImageButtonControl(state.buttons[2]);
}

void RegisterConnectFrontendFreeServerButtonShutdown(ConnectFrontendState&) {
    register_atexit_once(g_button_shutdown_registered[2],
        shutdown_global_free_server_button);
}

void ShutdownConnectFrontendFreeServerButton(ConnectFrontendState& state) {
    DestroyLegacyImageButtonControl(state.buttons[2]);
}

void InitializeConnectFrontendIpxButtonSupport(ConnectFrontendState& state) {
    InitializeConnectFrontendIpxButton(state);
    RegisterConnectFrontendIpxButtonShutdown(state);
}

void InitializeConnectFrontendIpxButton(ConnectFrontendState& state) {
    InitializeLegacyImageButtonControl(state.buttons[3]);
}

void RegisterConnectFrontendIpxButtonShutdown(ConnectFrontendState&) {
    register_atexit_once(g_button_shutdown_registered[3],
        shutdown_global_ipx_button);
}

void ShutdownConnectFrontendIpxButton(ConnectFrontendState& state) {
    DestroyLegacyImageButtonControl(state.buttons[3]);
}

void InitializeConnectFrontendDescriptionButtonSupport(ConnectFrontendState& state) {
    InitializeConnectFrontendDescriptionButton(state);
    RegisterConnectFrontendDescriptionButtonShutdown(state);
}

void InitializeConnectFrontendDescriptionButton(ConnectFrontendState& state) {
    InitializeLegacyImageButtonControl(state.buttons[4]);
}

void RegisterConnectFrontendDescriptionButtonShutdown(ConnectFrontendState&) {
    register_atexit_once(g_button_shutdown_registered[4],
        shutdown_global_description_button);
}

void ShutdownConnectFrontendDescriptionButton(ConnectFrontendState& state) {
    DestroyLegacyImageButtonControl(state.buttons[4]);
}

void InitializeConnectFrontendOkButtonSupport(ConnectFrontendState& state) {
    InitializeConnectFrontendOkButton(state);
    RegisterConnectFrontendOkButtonShutdown(state);
}

void InitializeConnectFrontendOkButton(ConnectFrontendState& state) {
    InitializeLegacyImageButtonControl(state.buttons[5]);
}

void RegisterConnectFrontendOkButtonShutdown(ConnectFrontendState&) {
    register_atexit_once(g_button_shutdown_registered[5],
        shutdown_global_ok_button);
}

void ShutdownConnectFrontendOkButton(ConnectFrontendState& state) {
    DestroyLegacyImageButtonControl(state.buttons[5]);
}

void InitializeConnectFrontendCancelButtonSupport(ConnectFrontendState& state) {
    InitializeConnectFrontendCancelButton(state);
    RegisterConnectFrontendCancelButtonShutdown(state);
}

void InitializeConnectFrontendCancelButton(ConnectFrontendState& state) {
    InitializeLegacyImageButtonControl(state.buttons[6]);
}

void RegisterConnectFrontendCancelButtonShutdown(ConnectFrontendState&) {
    register_atexit_once(g_button_shutdown_registered[6],
        shutdown_global_cancel_button);
}

void ShutdownConnectFrontendCancelButton(ConnectFrontendState& state) {
    DestroyLegacyImageButtonControl(state.buttons[6]);
}

void ResumeConnectFrontendModalWorker(ConnectFrontendState& state) {
    state.modal_wait_active = false;
    if (state.callbacks.resume_worker_modal != nullptr) {
        state.callbacks.resume_worker_modal(state);
    }
}

void ResetConnectFrontendPlayerSlots(ConnectFrontendState& state) {
    for (auto& slot : state.player_slots) {
        slot.bytes.fill(0);
    }
}

void InitializeConnectFrontendHandshakeHeader(ConnectFrontendState& state,
    u32 version) {
    state.handshake.magic = kConnectFrontendMagicJwar;
    state.handshake.version = version;
}

void WaitLegacyConnectFrontendTicks(u32 milliseconds) {
    i32 current = 0;
    const i32 target = static_cast<i32>(GetTickCount() + milliseconds);
    while (current < target) {
        current = static_cast<i32>(GetTickCount());
    }
}

bool CreateConnectFrontendWindow(ConnectFrontendState& state, HWND parent,
    HINSTANCE instance, LPARAM return_context) {
    state.parent_window = parent;
    state.main_window = parent;
    state.instance = instance;
    state.return_context = return_context;
    if (async_com_state().active_network_transport_mode == 6) {
        SetRankerMainWindowFrontendMode(0);
    }
    state.selected_mode =
        mode_from_active_transport(async_com_state().active_network_transport_mode);
    state.visible = false;

    LoadConnectFrontendConfiguration(state);
    std::fill(state.enabled_signature.begin(), state.enabled_signature.end(),
        kConnectFrontendEnabledSignature);
    state.mode_signatures = state.enabled_signature;

    FrontendLayoutTableOwner layout;
    if (!LoadFrontendLayoutFromJw219TrcRecord(
            layout.table, kConnectFrontendLayoutTrcRecord)) {
        return false;
    }

    state.window_rect = layout_at(layout.table, 0);
    const POINT origin = IsWindow(parent)
        ? RankerCenteredChildFrontendWindowOrigin(parent,
              state.window_rect.width, state.window_rect.height)
        : RankerCenteredFrontendWindowOrigin(
              state.window_rect.width, state.window_rect.height);
    const DWORD style = IsWindow(parent) ? kWindowStyleWindowed : kWindowStyleFullscreen;
    state.window = CreateWindowExA(WS_EX_CONTROLPARENT, "Connect", "Connect", style,
        origin.x, origin.y, state.window_rect.width, state.window_rect.height, parent,
        nullptr, instance, nullptr);
    if (state.window == nullptr) {
        return false;
    }
    SetWindowLongPtrA(state.window, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(connect_frontend_window_proc));
    load_connect_mode_description_records(state);

    LoadBitmapMemoryResourceFromTrcRecord(state.background, "Jw2_19.trc",
        kConnectFrontendBackgroundBitmapRecord);
    LoadBitmapMemoryResourceFromTrcRecord(state.description_background, "Jw2_19.trc",
        kConnectFrontendDescriptionBitmapRecord);

    for (const ConnectButtonDefinition& def : kButtonDefinitions) {
        const ConnectFrontendLayoutRect rect = layout_at(layout.table, def.slot + 1);
        if (!CreateLegacyImageButtonWindow(state.buttons[def.slot], state.window,
                def.text, reinterpret_cast<HMENU>(static_cast<INT_PTR>(def.id)),
                rect.x, rect.y, rect.width, rect.height)) {
            release_connect_resources(state);
            return false;
        }
        LoadLegacyImageButtonBitmaps(state.buttons[def.slot], def.normal_record,
            def.pressed_record);
        if (def.id == kConnectFrontendDescriptionButtonId) {
            SetWindowLongPtrA(GetLegacyImageButtonWindow(state.buttons[def.slot]),
                GWL_STYLE, kDescriptionButtonStyle);
        }
        set_button_proc(state.buttons[def.slot]);
    }

    SendMessageA(state.window, WM_SETFONT,
        reinterpret_cast<WPARAM>(ui_font_or_default(kConnectFrontendWindowFontIndex)),
        TRUE);
    SendMessageA(GetLegacyImageButtonWindow(state.buttons[4]), WM_SETFONT,
        reinterpret_cast<WPARAM>(
            ui_font_or_default(kConnectFrontendDescriptionFontIndex)),
        TRUE);

    if (state.async_context != nullptr) {
        if (SUCCEEDED(InitAsyncComSubsystem(instance, state.async_context))) {
            RegisterAsyncComWindowCallback(state.window);
        } else {
            show_connect_message(state,
                startup_message_row(kStartupDirectPlayInitializationFailureRow,
                    "Unable to initialize DirectPlay."),
                kConnectSoftWhite);
        }
    }

    if (state.async_tcp_socket != nullptr) {
        ResetLegacyAsyncTcpSocketQueues(*state.async_tcp_socket);
    }
    InitializeConnectFrontendHandshakeHeader(state, state.configuration.session_version);
    if (state.icon_collection != nullptr) {
        LoadDefaultIconBitmapSet(*state.icon_collection);
    }

    install_accelerators(state);
    // Keep the frontend hidden until its bitmaps and child controls are ready.
    // Showing it at creation time exposes partially initialized frames during
    // the title-to-connect transition.
    ShowWindow(state.window, SW_SHOW);
    RedrawWindow(state.window, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE |
        RDW_UPDATENOW | RDW_ALLCHILDREN);
    state.visible = true;
    return true;
}

int CompareConnectFrontendModeSignature(const std::array<u32, 4>& left,
    const std::array<u32, 4>& right) {
    return std::memcmp(left.data(), right.data(), sizeof(u32) * left.size());
}

bool IsConnectFrontendModeEnabled(const ConnectFrontendState& state,
    ConnectFrontendMode mode) {
    const std::size_t index = mode_index(mode);
    if (index >= state.mode_signatures.size()) {
        return false;
    }
    return CompareConnectFrontendModeSignature(state.mode_signatures[index],
        state.enabled_signature[index]) == 0;
}

void RedrawConnectFrontendModeButtons(ConnectFrontendState& state) {
    constexpr int ids[] = {
        kConnectFrontendWizardButtonId,
        kConnectFrontendP2PButtonId,
        kConnectFrontendFreeServerButtonId,
        kConnectFrontendIpxButtonId,
        kConnectFrontendDescriptionButtonId,
    };
    for (int id : ids) {
        const LegacyImageButtonControl* button = button_for_id(state, id);
        if (button != nullptr && GetLegacyImageButtonWindow(*button) != nullptr) {
            RedrawWindow(GetLegacyImageButtonWindow(*button), nullptr, nullptr,
                RDW_INVALIDATE | RDW_NOERASE);
        }
    }
}

LRESULT HandleConnectFrontendWindowMessage(ConnectFrontendState& state, HWND hwnd,
    UINT message, WPARAM wparam, LPARAM lparam) {
    if (message >= WM_SYSKEYDOWN && message <= WM_SYSKEYUP &&
        state.main_window != nullptr) {
        SendMessageA(state.main_window, message, wparam, lparam);
    }

    switch (message) {
    case WM_DESTROY:
        restore_accelerators(state);
        release_connect_resources(state);
        state.window = nullptr;
        state.visible = false;
        return 0;
    case WM_PAINT:
        if (hwnd == state.window) {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(hwnd, &paint);
            StretchBitmapMemoryResourceToDc(state.background, dc, 0, 0);
            EndPaint(hwnd, &paint);
            return 0;
        }
        break;
    case WM_ERASEBKGND:
        if (hwnd == state.window) {
            StretchBitmapMemoryResourceToDc(state.background,
                reinterpret_cast<HDC>(wparam), 0, 0);
            return 1;
        }
        break;
    case WM_GETMINMAXINFO: {
        auto* limits = reinterpret_cast<MINMAXINFO*>(lparam);
        if (limits != nullptr) {
            limits->ptMaxTrackSize.x = state.window_rect.width +
                GetSystemMetrics(SM_CXFRAME) * 2;
            limits->ptMaxTrackSize.y = state.window_rect.height +
                GetSystemMetrics(SM_CYFRAME) * 2 +
                GetSystemMetrics(SM_CYCAPTION);
            limits->ptMaxSize = limits->ptMaxTrackSize;
        }
        break;
    }
    case WM_DRAWITEM: {
        auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
        if (draw == nullptr) {
            return 0;
        }
        if (is_mode_button_id(draw->CtlID)) {
            draw_mode_button(state, *draw);
            break;
        }
        if (draw->CtlID == kConnectFrontendDescriptionButtonId) {
            draw_description_text(state, *draw);
            break;
        }
        LegacyImageButtonControl* button = button_for_id(state, draw->CtlID);
        if (button != nullptr) {
            DrawLegacyImageButtonItem(*button, *draw);
        }
        break;
    }
    case WM_COMMAND: {
        const int id = LOWORD(wparam);
        const int notify = HIWORD(wparam);
        if (is_mode_button_id(id)) {
            state.selected_mode = mode_for_button_id(id);
            SetActiveNetworkTransportMode(static_cast<i32>(state.selected_mode));
            RedrawConnectFrontendModeButtons(state);
            if (notify == BN_DOUBLECLICKED) {
                launch_selected_mode(state);
            }
            break;
        }
        if (id == kConnectFrontendOkButtonId) {
            HandleDefaultFrontendUiClickSound();
            launch_selected_mode(state);
            break;
        }
        if (id == kConnectFrontendCancelButtonId) {
            HandleDefaultFrontendUiClickSound();
            shutdown_network_mode(state);
            ShutdownLegacyWinSock();
            if (state.window != nullptr) {
                DestroyWindow(state.window);
            }
            ResumeConnectFrontendModalWorker(state);
            break;
        }
        if (id == kConnectFrontendClickOnlyButtonId) {
            HandleDefaultFrontendUiClickSound();
            break;
        }
        break;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORSTATIC:
        SetTextColor(reinterpret_cast<HDC>(wparam), kConnectSoftWhite);
        SetBkColor(reinterpret_cast<HDC>(wparam), kConnectBlack);
        SetBkMode(reinterpret_cast<HDC>(wparam), OPAQUE);
        return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));
    }

    return DefWindowProcA(hwnd, message, wparam, lparam);
}

LRESULT HandleConnectFrontendButtonMessage(ConnectFrontendState& state, HWND hwnd,
    UINT message, WPARAM wparam, LPARAM lparam) {
    if (message >= WM_SYSKEYDOWN && message <= WM_SYSKEYUP &&
        state.main_window != nullptr) {
        SendMessageA(state.main_window, message, wparam, lparam);
    }

    const int id = static_cast<int>(GetWindowLongPtrA(hwnd, GWLP_ID));
    LegacyImageButtonControl* button = nullptr;
    switch (id) {
    case kConnectFrontendWizardButtonId:
        button = &state.buttons[0];
        break;
    case kConnectFrontendP2PButtonId:
        button = &state.buttons[1];
        break;
    case kConnectFrontendFreeServerButtonId:
        button = &state.buttons[2];
        break;
    case kConnectFrontendIpxButtonId:
        button = &state.buttons[3];
        break;
    case kConnectFrontendDescriptionButtonId:
        button = &state.buttons[4];
        break;
    case kConnectFrontendOkButtonId:
        button = &state.buttons[5];
        break;
    case kConnectFrontendCancelButtonId:
        button = &state.buttons[6];
        break;
    default:
        break;
    }

    if (button != nullptr) {
        return CallWindowProcA(button->original_window_proc, hwnd, message, wparam,
            lparam);
    }
    return 0;
}

bool LoadConnectFrontendConfiguration(ConnectFrontendState& state) {
    if (state.configuration.loaded) {
        return true;
    }

    std::vector<u8> record;
    if (!LoadTrcRecordAlloc("Jw2_19.trc", kConnectFrontendConfigTrcRecord,
            record, 1)) {
        state.configuration.loaded = false;
        return false;
    }

    split_lines(record, state.configuration.rows);
    if (first_c_string_equals_ascii_case_insensitive(record, "NULL")) {
        state.configuration.rows.clear();
        state.configuration.loaded = false;
        return false;
    }

    IndexedTextTableContext indexed_table;
    InitializeIndexedTextTableContext(indexed_table);
    const bool indexed_loaded =
        LoadIndexedTextTableFromMemory(
            indexed_table, reinterpret_cast<const char*>(record.data()));
    if (!indexed_loaded) {
        DestroyIndexedTextTableContext(indexed_table);
        state.configuration.loaded = false;
        return false;
    }
    const auto indexed_text = [&](u32 index) {
        return std::string(GetIndexedTextTableRow(indexed_table, index));
    };
    const auto indexed_u32_or_zero = [&](u32 index) {
        const std::string value = indexed_text(index);
        if (value.empty()) {
            return 0u;
        }
        return static_cast<u32>(std::strtoul(value.c_str(), nullptr, 10));
    };

    state.configuration.wizard_title = indexed_text(0x17);
    state.configuration.p2p_title = indexed_text(0x18);
    state.configuration.patcher_executable_name = indexed_text(0x17);
    state.configuration.patcher_data_name = indexed_text(0x18);
    state.configuration.selected_mode_title = indexed_text(0x29);
    state.configuration.default_server_address = indexed_text(0x29);
    state.configuration.session_version = indexed_u32_or_zero(0x2a);
    state.configuration.server_port = indexed_u32_or_zero(0x2c);
    state.configuration.p2p_tcp_port = indexed_u32_or_zero(0x2d);
    state.configuration.free_server_port = state.configuration.p2p_tcp_port;
    state.configuration.p2p_udp_port = indexed_u32_or_zero(0x2e);
    state.configuration.max_players = indexed_u32_or_zero(0x30);
    if (const char* offset_text = std::getenv("RANKER_RECONSTRUCTED_PORT_OFFSET")) {
        const unsigned long offset = std::strtoul(offset_text, nullptr, 10);
        if (offset <= 0xffffu) {
            const auto shifted_port = [offset](u32 port) {
                return port != 0 && port + offset <= 0xffffu
                    ? static_cast<u32>(port + offset) : port;
            };
            state.configuration.p2p_tcp_port =
                shifted_port(state.configuration.p2p_tcp_port);
            state.configuration.p2p_udp_port =
                shifted_port(state.configuration.p2p_udp_port);
        }
    }
    DestroyIndexedTextTableContext(indexed_table);
    state.configuration.loaded = true;
    return true;
}

}

#endif
