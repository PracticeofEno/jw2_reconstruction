#pragma once

#include <array>
#include <cstdarg>
#include <cstddef>
#include <iosfwd>
#include <ctime>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <commctrl.h>
#include <objidl.h>
#include <ole2.h>
#include <richedit.h>
#include <shellapi.h>
#endif

namespace ranker {

using AfxDumpClientCallback = void (*)(const void* object, std::size_t bytes);
using AfxReportHookCallback = int (*)(int report_type, const char* message,
    int* result);
using MfcOleTermOrFreeLibCallback = void (*)(int terminate, int just_revoke);

struct MfcDumpContext {
    int depth = 0;
    void* file = nullptr;
};

struct AfxDebugState {
    bool initialized = false;
    bool trace_enabled = true;
    unsigned trace_flags = 0;
    AfxDumpClientCallback previous_dump_client = nullptr;
    AfxReportHookCallback previous_report_hook = nullptr;
};

struct MfcSimpleExceptionCompat {
    bool auto_delete = false;
    unsigned help_context = 0;
    bool string_initialized = false;
    bool has_message = false;
    std::array<char, 128> message{};
};

struct AfxExceptionLinkCompat {
    AfxExceptionLinkCompat* previous = nullptr;
    void* exception = nullptr;
};

struct AfxExceptionContextCompat {
    AfxExceptionLinkCompat* current = nullptr;
};

struct MfcFileExceptionCompat : MfcSimpleExceptionCompat {
    unsigned cause = 0;
    long os_error = 0;
    std::string file_name;
};

struct MfcTimeCompat {
    std::time_t value = 0;
};

struct MfcTimeSpanCompat {
    long long seconds = 0;
};

struct MfcCStringCompat {
    std::string text;
};

struct MfcCStringDataHeaderCompat {
    long references = 1;
    int data_length = 0;
    int alloc_length = 0;
};

using MfcCreateObjectCallback = void* (*)();

struct MfcRuntimeClassCompat {
    const char* class_name = "";
    int object_size = 0;
    unsigned schema = 0xffff;
    MfcCreateObjectCallback create_object = nullptr;
    MfcRuntimeClassCompat* base_class = nullptr;
    MfcRuntimeClassCompat* next_class = nullptr;
};

struct MfcObjectCompat {
    MfcRuntimeClassCompat* runtime_class = nullptr;
};

struct MfcPtrListNodeCompat {
    MfcPtrListNodeCompat* previous = nullptr;
    MfcPtrListNodeCompat* next = nullptr;
    void* value = nullptr;
};

struct MfcPtrListCompat {
    MfcPtrListNodeCompat* head = nullptr;
    MfcPtrListNodeCompat* tail = nullptr;
    int count = 0;
    int block_size = 10;
};

using MfcObListNodeCompat = MfcPtrListNodeCompat;
using MfcObListCompat = MfcPtrListCompat;

struct MfcCStringListNodeCompat {
    MfcCStringListNodeCompat* previous = nullptr;
    MfcCStringListNodeCompat* next = nullptr;
    MfcCStringCompat value;
};

struct MfcCStringListCompat {
    MfcCStringListNodeCompat* head = nullptr;
    MfcCStringListNodeCompat* tail = nullptr;
    int count = 0;
    int block_size = 10;
};

struct MfcByteArrayCompat {
    std::vector<unsigned char> values;
    int grow_by = 0;
};

struct MfcWordArrayCompat {
    std::vector<unsigned short> values;
    int grow_by = 0;
};

struct MfcDWordArrayCompat {
    std::vector<unsigned long> values;
    int grow_by = 0;
};

struct MfcUIntArrayCompat {
    std::vector<unsigned int> values;
    int grow_by = 0;
};

struct MfcPtrArrayCompat {
    std::vector<void*> values;
    int grow_by = 0;
};

struct MfcObArrayCompat {
    std::vector<void*> values;
    int grow_by = 0;
};

struct MfcCStringArrayCompat {
    std::vector<MfcCStringCompat> values;
    int grow_by = 0;
};

struct MfcPlexCompat {
    MfcPlexCompat* next = nullptr;
    std::vector<unsigned char> data;
};

struct MfcMapPtrToPtrAssocCompat {
    void* key = nullptr;
    void* value = nullptr;
};

struct MfcMapPtrToPtrCompat {
    std::unordered_map<void*, MfcMapPtrToPtrAssocCompat> entries;
    int hash_table_size = 17;
    int block_size = 10;
};

struct MfcMapWordToPtrAssocCompat {
    unsigned short key = 0;
    void* value = nullptr;
};

struct MfcMapWordToPtrCompat {
    std::unordered_map<unsigned short, MfcMapWordToPtrAssocCompat> entries;
    int hash_table_size = 17;
    int block_size = 10;
};

struct MfcMapPtrToWordAssocCompat {
    void* key = nullptr;
    unsigned short value = 0;
};

struct MfcMapPtrToWordCompat {
    std::unordered_map<void*, MfcMapPtrToWordAssocCompat> entries;
    int hash_table_size = 17;
    int block_size = 10;
};

struct MfcMapWordToObAssocCompat {
    unsigned short key = 0;
    void* value = nullptr;
};

struct MfcMapWordToObCompat {
    std::unordered_map<unsigned short, MfcMapWordToObAssocCompat> entries;
    int hash_table_size = 17;
    int block_size = 10;
};

struct MfcMapStringToPtrAssocCompat {
    std::string key;
    void* value = nullptr;
};

struct MfcMapStringToPtrCompat {
    std::unordered_map<std::string, MfcMapStringToPtrAssocCompat> entries;
    int hash_table_size = 17;
    int block_size = 10;
};

struct MfcMapStringToObAssocCompat {
    std::string key;
    void* value = nullptr;
};

struct MfcMapStringToObCompat {
    std::unordered_map<std::string, MfcMapStringToObAssocCompat> entries;
    int hash_table_size = 17;
    int block_size = 10;
};

struct MfcMapStringToStringAssocCompat {
    std::string key;
    std::string value;
};

struct MfcMapStringToStringCompat {
    std::unordered_map<std::string, MfcMapStringToStringAssocCompat> entries;
    int hash_table_size = 17;
    int block_size = 10;
};

#ifdef _WIN32
struct MfcWinThreadCompat;

struct MfcFileDialogCompat {
    bool open_dialog = true;
    OPENFILENAMEA ofn{};
    HWND owner = nullptr;
    std::array<char, MAX_PATH> file{};
    std::array<char, MAX_PATH> file_title{};
    std::string filter_storage;
    std::string default_extension;
    std::string title;
    std::string multi_select_cursor;
    unsigned template_id = 0;
};

struct MfcColorDialogCompat {
    CHOOSECOLORA cc{};
    HWND owner = nullptr;
    COLORREF color = 0;
    std::array<COLORREF, 16> custom_colors{};
};

struct MfcPrintDialogCompat {
    PRINTDLGA pd{};
};

struct MfcPrintDialogExCompat {
    DWORD result_action = 0;
};

struct MfcPrintInfoCompat {
    MfcPrintDialogCompat* print_dialog = nullptr;
    bool continue_printing = true;
    UINT current_page = 1;
};

struct MfcFontDialogCompat {
    CHOOSEFONTA cf{};
    LOGFONTA log_font{};
    std::string style_name;
};

struct MfcFindReplaceDialogCompat {
    FINDREPLACEA fr{};
    std::array<char, 256> find_text{};
    std::array<char, 256> replace_text{};
};

struct MfcBitmapButtonCompat {
    HWND window = nullptr;
    HBITMAP normal = nullptr;
    HBITMAP selected = nullptr;
    HBITMAP focus = nullptr;
    HBITMAP disabled = nullptr;
};

struct MfcCWndCompat : MfcObjectCompat {
    HWND window = nullptr;
    WNDPROC original_wnd_proc = nullptr;
    void* control_site = nullptr;
    void* control_container = nullptr;
    MfcWinThreadCompat* owner_thread = nullptr;
    std::string class_name;
    int dialog_control_id = 0;
    unsigned wnd_flags = 0;
    unsigned modal_flags = 0;
    int modal_result = 0;
    bool temporary = false;
};

struct MfcControlCompat : MfcCWndCompat {
    DWORD style = 0;
};

struct MfcCreateContextCompat {
    void* current_frame = nullptr;
    void* current_doc = nullptr;
    void* new_view_class = nullptr;
    void* new_doc_template = nullptr;
    void* last_view = nullptr;
};

struct MfcViewCompat : MfcCWndCompat {
    void* document = nullptr;
    void* active_frame = nullptr;
    void* preview_view = nullptr;
};

struct MfcFrameWndCompat : MfcCWndCompat {
    HACCEL accelerator = nullptr;
    MfcViewCompat* active_view = nullptr;
    std::vector<HWND> disabled_modal_windows;
    int modal_disable_count = 0;
    bool tracking = false;
    bool idle_pending = false;
    bool auto_menu_enable = true;
    bool in_help_mode = false;
    bool menu_tracking_active = false;
    bool layout_in_progress = false;
    unsigned idle_update_flags = 0;
    bool deferred_title_add_to_title = true;
    bool deferred_recalc_notify = false;
    UINT help_context = 0;
    UINT tracking_help_context = 0;
    UINT tracking_message_id = 0;
    UINT last_message_id = 0;
    UINT deferred_message_id = 0;
    RECT layout_rect{};
    std::string title;
    std::string status_text;
    HMENU menu = nullptr;
    HWND mdi_client = nullptr;
    std::vector<void*> control_bars;
    MfcRuntimeClassCompat* floating_frame_class = nullptr;
    void* active_document = nullptr;
    HMENU preview_saved_menu = nullptr;
    HACCEL preview_saved_accelerator = nullptr;
    HWND preview_saved_focus = nullptr;
    UINT preview_child_id = 0xe900;
    DWORD preview_control_bar_mask = 0;
    bool preview_mode_active = false;
    int window_number = 0;
    void* command_target = nullptr;
};

struct MfcPreviewStateCompat {
    UINT main_pane_id = 0xe900;
    HMENU saved_menu = nullptr;
    DWORD visible_control_bar_mask = 0;
    UINT preview_bar_id = 0;
    HWND saved_focus = nullptr;
    HACCEL saved_accelerator = nullptr;
};

struct MfcCtrlViewCompat : MfcViewCompat {
    std::string control_class_name;
    DWORD default_style = 0;
};

struct MfcScrollViewCompat : MfcViewCompat {
    int map_mode = 0;
    SIZE total_log{};
    SIZE total_dev{};
    SIZE page_dev{};
    SIZE line_dev{};
    bool center = false;
    bool inside_update = false;
};

struct MfcSplitterPaneInfoCompat {
    int min_size = 0;
    int ideal_size = 0;
    int current_size = -1;
};

struct MfcSplitterWndCompat : MfcCWndCompat {
    void* dynamic_view_class = nullptr;
    int max_rows = 1;
    int max_cols = 1;
    int row_count = 0;
    int col_count = 0;
    bool has_h_scroll = false;
    bool has_v_scroll = false;
    int cx_splitter = 4;
    int cy_splitter = 4;
    int cx_border_share = 1;
    int cy_border_share = 1;
    int cx_splitter_gap = 6;
    int cy_splitter_gap = 6;
    int cx_border = 0;
    int cy_border = 0;
    bool tracking = false;
    bool tracking2 = false;
    POINT track_offset{};
    RECT rect_limit{};
    RECT rect_tracker{};
    RECT rect_tracker2{};
    int hit_track = 0;
    std::vector<MfcSplitterPaneInfoCompat> col_info;
    std::vector<MfcSplitterPaneInfoCompat> row_info;
    std::vector<MfcCWndCompat*> panes;
};

struct MfcDockBarCompat;
struct MfcDockContextCompat;

struct MfcControlBarCompat : MfcCWndCompat {
    bool auto_delete = false;
    int cx_left_border = 0;
    int cx_right_border = 0;
    int cy_top_border = 0;
    int cy_bottom_border = 0;
    int cx_default_gap = 0;
    int mru_width = 0x7fff;
    int count = 0;
    void* item_data = nullptr;
    DWORD bar_style = 0;
    DWORD dock_style = 0;
    unsigned state_flags = 0;
    MfcCWndCompat* owner_frame = nullptr;
    MfcDockBarCompat* dock_bar = nullptr;
    MfcDockContextCompat* dock_context = nullptr;
    bool owns_dock_context = false;
    int status_hit = -1;
    bool tooltip_enabled = false;
};

struct MfcDockContextCompat : MfcObjectCompat {
    POINT start_point{};
    RECT last_tracker{};
    int last_tracker_cx = 0;
    int last_tracker_cy = 0;
    bool solid_tracker = false;
    RECT drag_rect{};
    RECT drag_rect_vertical{};
    RECT frame_rect{};
    RECT frame_rect_vertical{};
    MfcControlBarCompat* bar = nullptr;
    MfcCWndCompat* dock_site = nullptr;
    DWORD over_dock_style = 0;
    DWORD bar_style = 0;
    bool flip = false;
    bool force_frame = false;
    HDC tracking_dc = nullptr;
    int resize_hit_test = 0;
    bool dragging = false;
    UINT recent_dock_id = 0;
    RECT recent_dock_rect{};
    DWORD mru_dock_style = 0;
    POINT mru_float_pos{};
    POINT current_point{};
    bool tracking_loop = false;
};

struct MfcDockBarCompat : MfcControlBarCompat {
    bool floating = false;
    bool layout_suspended = false;
    RECT layout_rect{};
    std::vector<MfcControlBarCompat*> bars;
    std::vector<UINT> bar_ids;
};

struct MfcMiniFrameWndCompat : MfcFrameWndCompat {
    bool sys_menu_tracking = false;
    bool sys_menu_hot = false;
    bool active_caption = false;
    std::string mini_caption;
};

struct MfcMiniDockFrameWndCompat : MfcMiniFrameWndCompat {
    MfcDockBarCompat dock_bar;
    bool creating = false;
    DWORD dock_style = 0;
};

struct MfcDialogBarCompat : MfcControlBarCompat {
    SIZE dialog_size{};
    LPCSTR template_name = nullptr;
    void* occ_dialog_info = nullptr;
};

struct MfcToolBarCompat : MfcControlBarCompat {
    SIZE button_size{23, 22};
    SIZE image_size{16, 15};
    HINSTANCE image_instance = nullptr;
    HRSRC image_resource = nullptr;
    HBITMAP image_well = nullptr;
    bool button_layout_dirty = false;
    std::unordered_map<int, std::string> button_text;
};

struct MfcSizeParentParamsCompat {
    HDWP hdwp = nullptr;
    RECT rect{};
    SIZE size_total{};
    bool stretch = false;
};

struct MfcMenuCompat : MfcObjectCompat {
    HMENU menu = nullptr;
    bool temporary = false;
};

struct MfcDialogCompat : MfcCWndCompat {
    LPCSTR template_name = nullptr;
    UINT template_id = 0;
    HINSTANCE template_instance = nullptr;
    HWND parent = nullptr;
    void* init_param = nullptr;
    HGLOBAL modal_resource = nullptr;
    const DLGTEMPLATE* modal_template = nullptr;
    void* dialog_init = nullptr;
    HWND disabled_owner = nullptr;
    void* occ_dialog_info = nullptr;
    ULONG_PTR help_id = 0;
    bool modal_template_initialized = false;
    bool creation_failed = false;
};

struct MfcPropertyPageCompat : MfcDialogCompat {
    PROPSHEETPAGEA page{};
    HGLOBAL modified_template = nullptr;
    void* occ_dialog_info_page = nullptr;
    std::string caption;
    std::string header_title;
    std::string header_subtitle;
    LPCSTR original_template = nullptr;
    UINT original_template_id = 0;
    bool first_set_active = true;
    bool modified = false;
};

struct MfcPropertyPageExCompat : MfcPropertyPageCompat {};

struct MfcPropertySheetCompat : MfcCWndCompat {
    PROPSHEETHEADERA header{};
    std::vector<MfcPropertyPageCompat*> pages;
    std::vector<PROPSHEETPAGEA> page_storage;
    std::string caption;
    MfcCWndCompat* parent_window = nullptr;
    UINT initial_page = 0;
    bool stacked = true;
    bool modeless = false;
    int pressed_button = 0;
    DWORD create_style = 0;
    DWORD create_ex_style = 0;
};

struct MfcPropertySheetExCompat : MfcPropertySheetCompat {
    SIZE watermark_size{};
};

struct MfcDataExchangeCompat {
    bool save_and_validate = false;
    MfcCWndCompat* dialog = nullptr;
    HWND last_control = nullptr;
    bool edit_last_control = false;
};

struct MfcDialogTemplateCompat {
    HGLOBAL handle = nullptr;
    unsigned size = 0;
    bool no_font = true;
};

struct MfcRecentFileListCompat : MfcObjectCompat {
    unsigned start = 0;
    int max_size = 0;
    int max_display_length = -1;
    std::string section_name;
    std::string entry_format;
    std::vector<std::string> names;
};

struct MfcFileCompat : MfcObjectCompat {
    HANDLE handle = INVALID_HANDLE_VALUE;
    bool close_on_delete = false;
    std::string file_name;
};

struct MfcMirrorFileCompat : MfcFileCompat {
    std::string temp_name;
};

struct MfcFileStatusCompat {
    MfcTimeCompat creation_time;
    MfcTimeCompat modified_time;
    MfcTimeCompat access_time;
    unsigned long size = 0;
    unsigned char attribute = 0;
    std::array<char, MAX_PATH> full_name{};
};

struct MfcArchiveCompat : MfcObjectCompat {
    MfcFileCompat* file = nullptr;
    bool storing = false;
    bool loading = true;
    bool no_flush_on_delete = false;
    bool open = false;
    unsigned buffer_size = 4096;
    std::vector<unsigned char> buffer;
    std::size_t position = 0;
    unsigned object_schema = 0xffffffffU;
    unsigned next_object_tag = 1;
    std::unordered_map<const void*, unsigned> stored_object_tags;
    std::vector<void*> loaded_objects;
    std::unordered_map<const MfcRuntimeClassCompat*, unsigned> stored_class_tags;
    std::vector<MfcRuntimeClassCompat*> loaded_classes;
    std::unordered_map<const MfcRuntimeClassCompat*, unsigned> loaded_schemas;
};

struct MfcCDCCompat : MfcObjectCompat {
    HDC output_dc = nullptr;
    HDC attribute_dc = nullptr;
    bool printing = false;
};

struct MfcWindowDCCompat : MfcCDCCompat {
    HWND window = nullptr;
    PAINTSTRUCT paint{};
};

struct MfcRectTrackerCompat : MfcObjectCompat {
    RECT rect{};
    UINT style = 0;
    SIZE min_size{};
    int handle_size = 0;
    int resize_outside_size = 0;
    int border_size = 0;
    RECT last_rect{};
    SIZE last_size{};
    bool tracking = false;
    bool final_erase = false;
    MfcCWndCompat* clip_window = nullptr;
};

struct MfcGdiObjectCompat : MfcObjectCompat {
    HGDIOBJ object = nullptr;
    bool temporary = false;
};

struct MfcWindowHandleMapCompat {
    std::unordered_map<HWND, MfcCWndCompat*> permanent;
    std::unordered_map<HWND, MfcCWndCompat*> temporary;
};

struct MfcMenuHandleMapCompat {
    std::unordered_map<HMENU, MfcMenuCompat*> permanent;
    std::unordered_map<HMENU, MfcMenuCompat*> temporary;
};

struct MfcHandleMapCompat {
    std::unordered_map<void*, void*> permanent;
    std::unordered_map<void*, void*> temporary;
    int handle_offset = 0;
    MfcRuntimeClassCompat* runtime_class = nullptr;
    int handle_count = 1;
};

struct MfcOleControlSiteCompat;

using MfcOleInvokeHelperCallback = void (*)(
    MfcOleControlSiteCompat& site, LONG dispatch_id, WORD flags,
    unsigned short return_type, void* return_value,
    const unsigned char* param_info, va_list args);
using MfcOleSetPropertyCallback = void (*)(
    MfcOleControlSiteCompat& site, LONG dispatch_id,
    unsigned short value_type, va_list args);
using MfcOleGetAmbientPropertyCallback = bool (*)(
    MfcOleControlSiteCompat& site, LONG dispatch_id,
    unsigned short value_type, void* value);
using MfcOleSetControlSizeCallback = void (*)(
    MfcOleControlSiteCompat& site, int width, int height);

struct MfcOleControlSiteCompat {
    void* context = nullptr;
    MfcCWndCompat* control_window = nullptr;
    IUnknown* control_unknown = nullptr;
    MfcOleInvokeHelperCallback invoke_helper = nullptr;
    MfcOleSetPropertyCallback set_property = nullptr;
    MfcOleGetAmbientPropertyCallback get_ambient_property = nullptr;
    MfcOleSetControlSizeCallback set_control_size = nullptr;
};

struct MfcOleControlContainerCompat {
    void* context = nullptr;
    std::unordered_map<HWND, MfcOleControlSiteCompat*> sites;
};

struct MfcOccManagerCompat {
    void* context = nullptr;
    bool (*create_controls_from_template)(MfcOccManagerCompat& manager,
        MfcCWndCompat& window, const DLGTEMPLATE* dialog_template,
        void* occ_info) = nullptr;
    bool (*create_controls_from_resource)(MfcOccManagerCompat& manager,
        MfcCWndCompat& window, LPCSTR template_name,
        void* occ_info) = nullptr;
};

struct MfcAuxDataCompat {
    int icon_width = 0;
    int icon_height = 0;
    int scroll_width = 0;
    int scroll_height = 0;
    int pixels_per_inch_x = 96;
    int pixels_per_inch_y = 96;
    HBRUSH button_face_brush = nullptr;
    HBRUSH window_frame_brush = nullptr;
    COLORREF button_face = 0;
    COLORREF button_shadow = 0;
    COLORREF button_highlight = 0;
    COLORREF button_text = 0;
    COLORREF window_frame = 0;
    bool metrics_initialized = false;
    DWORD windows_version = 0;
    bool win32s_platform = false;
    bool win4_or_later = false;
    bool win31_compat = true;
    bool process_uses_win4 = false;
    bool border_adjusted_for_3d = false;
    HCURSOR wait_cursor = nullptr;
    HCURSOR arrow_cursor = nullptr;
    bool common_controls6 = false;
};

struct MfcCommandTargetCompat;

using MfcCommandHandlerCallback = bool (*)(
    MfcCommandTargetCompat& target, UINT id, int code, void* extra);

struct MfcMessageMapEntryCompat {
    UINT message = 0;
    UINT code = 0;
    UINT id_first = 0;
    UINT id_last = 0;
    void* handler = nullptr;
    UINT signature = 0;
    MfcCommandHandlerCallback callback = nullptr;
};

struct MfcMessageMapCompat {
    const MfcMessageMapCompat* base = nullptr;
    const MfcMessageMapEntryCompat* entries = nullptr;
};

struct MfcCommandHandlerInfoCompat {
    MfcCommandTargetCompat* target = nullptr;
    void* handler = nullptr;
};

struct MfcCommandTargetCompat : MfcObjectCompat {
    int reference_count = 1;
    void* dispatch_map = nullptr;
    void* connection_map = nullptr;
    bool automation_enabled = true;
    bool final_release_enabled = true;
    bool result_expected = true;
    const MfcMessageMapCompat* message_map = nullptr;
    MfcCommandTargetCompat* routing_target = nullptr;
    void* owner = nullptr;
};

enum class MfcCmdUIKind {
    Generic,
    Test,
    ToolBar,
};

struct MfcCmdUICompat {
    UINT id = 0;
    UINT index = 0;
    HMENU menu = nullptr;
    bool sub_menu = false;
    MfcCWndCompat* other = nullptr;
    bool changed = false;
    bool continue_routing = false;
    UINT menu_item_count = 0;
    UINT flags = 0;
    MfcCmdUIKind kind = MfcCmdUIKind::Generic;
};

struct MfcDocumentCompat;

struct MfcDocTemplateCompat : MfcCommandTargetCompat {
    unsigned id_resource = 0;
    std::string doc_strings;
    UINT container_resource = 0;
    UINT server_resource = 0;
    UINT ole_resource = 0;
    HMENU container_menu = nullptr;
    HACCEL container_accelerator = nullptr;
    HMENU server_menu = nullptr;
    HACCEL server_accelerator = nullptr;
    HMENU ole_menu = nullptr;
    HACCEL ole_accelerator = nullptr;
    void* doc_class = nullptr;
    void* frame_class = nullptr;
    void* view_class = nullptr;
    void* ole_frame_class = nullptr;
    void* ole_view_class = nullptr;
    MfcCommandTargetCompat* owner = nullptr;
    std::vector<MfcDocumentCompat*> documents;
    bool auto_delete = false;
};

struct MfcDocumentCompat : MfcCommandTargetCompat {
    std::string title;
    std::string path_name;
    MfcDocTemplateCompat* doc_template = nullptr;
    std::vector<MfcViewCompat*> views;
    bool modified = false;
    bool auto_delete = true;
    bool embedded = false;
};

struct MfcDocManagerCompat : MfcCommandTargetCompat {
    std::vector<MfcDocTemplateCompat*> templates;
};

struct MfcNewTypeDlgCompat : MfcDialogCompat {
    const std::vector<MfcDocTemplateCompat*>* templates = nullptr;
    MfcDocTemplateCompat* selected_template = nullptr;
};

struct MfcStaticCompat : MfcControlCompat {};
struct MfcButtonCompat : MfcControlCompat {};
struct MfcListBoxCompat : MfcControlCompat {};
struct MfcComboBoxCompat : MfcControlCompat {};
struct MfcEditCompat : MfcControlCompat {};
struct MfcScrollBarCompat : MfcControlCompat {};

struct MfcCheckDataCompat {
    int check = 0;
    bool enabled = true;
    LPARAM item_data = 0;
};

struct MfcCheckListStateCompat : MfcObjectCompat {
    HBITMAP bitmap = nullptr;
    SIZE check_size{0, 0};
};

struct MfcCheckListBoxCompat : MfcListBoxCompat {
    int check_style = 0;
    int item_height = 0;
};

struct MfcDragListBoxCompat : MfcControlCompat {
    int last_insert = -1;
};

struct MfcToolbarCtrlCompat : MfcControlCompat {};
struct MfcStatusBarCtrlCompat : MfcControlCompat {};
struct MfcListCtrlCompat : MfcControlCompat {};
struct MfcTreeCtrlCompat : MfcControlCompat {};
struct MfcSpinButtonCtrlCompat : MfcControlCompat {};
struct MfcSliderCtrlCompat : MfcControlCompat {};
struct MfcProgressCtrlCompat : MfcControlCompat {};
struct MfcHeaderCtrlCompat : MfcControlCompat {};
struct MfcHotKeyCtrlCompat : MfcControlCompat {};
struct MfcToolTipCtrlCompat : MfcControlCompat {};
struct MfcTabCtrlCompat : MfcControlCompat {};
struct MfcAnimateCtrlCompat : MfcControlCompat {};
struct MfcRichEditCtrlCompat : MfcControlCompat {};

struct MfcImageListCompat {
    HIMAGELIST handle = nullptr;
    bool owns_handle = true;
};

struct MfcArchiveStreamCompat {
    void* archive = nullptr;
    std::vector<unsigned char> buffer;
    ULONGLONG position = 0;
};

struct MfcHelpManagerCompat {
    void* user_context = nullptr;
    void (*route_help_command)(MfcHelpManagerCompat& manager,
        void (*fallback)(void*), void* context) = nullptr;
};

struct MfcWinThreadCompat : MfcObjectCompat {
    HANDLE thread = nullptr;
    unsigned thread_id = 0;
    bool auto_delete = true;
    void* thread_params = nullptr;
    unsigned (__stdcall *thread_proc)(void*) = nullptr;
    MSG current_message{};
    int quit_count = 0;
    int disable_pump_count = 0;
    HWND main_window = nullptr;
    HWND active_window = nullptr;
    POINT cursor_last{};
    UINT message_last = 0;
    LONG ole_object_count = 0;
    bool ole_user_control = false;
    bool ole_post_quit_disabled = false;
    MfcOleTermOrFreeLibCallback ole_term_or_free_lib = nullptr;
};

struct MfcWinAppCompat : MfcWinThreadCompat {
    HINSTANCE instance = nullptr;
    HINSTANCE previous_instance = nullptr;
    std::string command_line;
    int command_show = SW_SHOWNORMAL;
    std::string app_name;
    std::string exe_name;
    std::string help_file_path;
    std::string profile_name;
    bool help_mode = false;
    void* doc_manager = nullptr;
    HGLOBAL printer_dev_mode = nullptr;
    HGLOBAL printer_dev_names = nullptr;
    ULONG_PTR prompt_context = 0;
    int wait_cursor_count = 0;
    HCURSOR wait_cursor_restore = nullptr;
    std::string app_id;
    MfcHelpManagerCompat* help_manager = nullptr;
    MfcRecentFileListCompat* recent_file_list = nullptr;
    void* command_line_info = nullptr;
    bool use_registry = false;
    std::string registry_key;
    int preview_pages = 0;
    MfcCommandTargetCompat command_target;
};
#endif

void AfxDumpStaticObjectMessage(int static_object);
void AfxTraceOutput(const char* format, ...);
void AfxTraceWindowMessage(const char* label, const MSG& message);
void AfxTraceDdeMessage(const char* label, const MSG& message);
void InitializeGlobalDumpContextThunk();
void InitializeGlobalDumpContext();
MfcDumpContext& ConstructDumpContext(MfcDumpContext& context, void* file);
int DumpContextGetDepth(const MfcDumpContext& context);
void DumpContextSetDepth(MfcDumpContext& context, int depth);
int DumpContextDepthValue(int depth);
void DumpContextNoopInline();
void DumpContextOutputString(MfcDumpContext& context, const char* text);
void DumpContextFlush(MfcDumpContext& context);
MfcDumpContext& DumpContextWriteString(MfcDumpContext& context,
    const char* text);
MfcDumpContext& DumpContextWriteByte(MfcDumpContext& context, unsigned value);
MfcDumpContext& DumpContextWriteWord(MfcDumpContext& context, unsigned value);
MfcDumpContext& DumpContextWriteDWord(MfcDumpContext& context, DWORD value);
MfcDumpContext& DumpContextWriteLong(MfcDumpContext& context, long value);
MfcDumpContext& DumpContextWriteFloat(MfcDumpContext& context, float value);
MfcDumpContext& DumpContextWriteDouble(MfcDumpContext& context, double value);
MfcDumpContext& DumpContextWriteULong(MfcDumpContext& context,
    unsigned long value);
MfcDumpContext& DumpContextWritePointer(MfcDumpContext& context,
    const void* value);
MfcDumpContext& DumpContextWriteObjectPointer(MfcDumpContext& context,
    const void* object);
void DumpContextWriteObjectPointerThunk(MfcDumpContext& context,
    const void* object);
MfcDumpContext& DumpContextWriteHex(MfcDumpContext& context, DWORD value);
void DumpContextDumpBytes(MfcDumpContext& context, const char* prefix_format,
    const unsigned char* data, int count, int per_line);
MfcDumpContext& DumpContextWriteWideString(MfcDumpContext& context,
    const wchar_t* text);
void AfxAssertValidObject(const MfcObjectCompat* object,
    const char* file, int line);
void DumpOleVariant(MfcDumpContext& context, const VARIANTARG& variant);
void DumpOleSafeArrayElement(MfcDumpContext& context,
    const VARIANTARG& safe_array, LONG* indices);
void DumpOleSafeArray(MfcDumpContext& context, const VARIANTARG& safe_array);
void InitializeAfxDebugStateThunk();
void InitializeAfxDebugState();
void AfxDumpClientBridge(const void* object, std::size_t bytes);
int AfxReportHookBridge(int report_type, const char* message, int* result);
AfxDebugState* ConstructAfxDebugState(AfxDebugState* state);
void InitializeAfxDebugSupport();
void InitializeAfxDebugSupportNoop();
bool AfxAssertFailedLine(const char* file, int line);
void RegisterAfxDebugStateCleanup();
void CleanupAfxDebugStateAtExit();
bool EnsureAfxDebugState();
AfxDebugState* DeleteAfxDebugState(AfxDebugState* state, unsigned flags);
AfxDebugState* ConstructMfcNoTrackObjectBase(AfxDebugState* state);
void DestroyProcessLocalAfxDebugState(AfxDebugState* state);
AfxDebugState* GetProcessLocalAfxDebugState();
#ifdef _WIN32
void AfxTermLocalDataCompat(HINSTANCE instance, bool delete_all);
#endif
void InitializeMfcBaseModuleStateCompat(bool ole_enabled = true);
void MfcThreadSlotRuntimeDeleteSlot(int slot);
void DestroyPropPageFontInfoCompat(void* info);

bool GetSimpleExceptionErrorMessage(MfcSimpleExceptionCompat& exception,
    char* destination, int destination_chars, unsigned* help_context = nullptr);
[[noreturn]] void ThrowMfcMemoryException();
[[noreturn]] void ThrowMfcResourceException();
[[noreturn]] void MfcOleRuntime_005f4b9a(SCODE scode);
AfxExceptionContextCompat& AfxExceptionContextCompatState();
MfcRuntimeClassCompat* GetCObjectRuntimeClass();
MfcRuntimeClassCompat* MfcExceptionRuntimeThunk_005e8ca8();
MfcRuntimeClassCompat* AfxClassInitObject(MfcRuntimeClassCompat* runtime_class);
void MfcDebugDeleteClientBlock(void* memory);
void MfcDebugDeleteNormalBlock(void* memory);
#ifdef _WIN32
void MfcThreadSlotRuntime_005ead15(HLOCAL memory);
#endif
MfcObjectCompat& ConstructCObject(MfcObjectCompat& object);
void DestroyCObject(MfcObjectCompat& object);
MfcObjectCompat* DeleteCObjectScalarDtor(MfcObjectCompat* object,
    unsigned flags);
void CObjectSerializeNoop();
void* CObjectSerializeReturnArchive(void* object, void* archive);
MfcObjectCompat* AfxDynamicDownCast(const MfcRuntimeClassCompat* target,
    MfcObjectCompat* object);
MfcObjectCompat* AfxStaticDownCast(const MfcRuntimeClassCompat* target,
    MfcObjectCompat* object);
void* RuntimeClassCreateObject(MfcRuntimeClassCompat& runtime_class);
void* CreateObject(MfcRuntimeClassCompat* runtime_class);
void Dump(const MfcObjectCompat* object, MfcDumpContext* context = nullptr);
MfcSimpleExceptionCompat& ConstructSimpleException(
    MfcSimpleExceptionCompat& exception, bool auto_delete = false,
    unsigned help_context = 0);
void DestroyExceptionBase(MfcSimpleExceptionCompat& exception);
void DestroySimpleException(MfcSimpleExceptionCompat& exception);
MfcSimpleExceptionCompat* DeleteSimpleExceptionScalarDtor(
    MfcSimpleExceptionCompat* exception, unsigned flags);
MfcSimpleExceptionCompat& ConstructMemoryException(
    MfcSimpleExceptionCompat& exception);
void DestroyMemoryException(MfcSimpleExceptionCompat& exception);
MfcSimpleExceptionCompat* DeleteMemoryExceptionScalarDtor(
    MfcSimpleExceptionCompat* exception, unsigned flags);
MfcSimpleExceptionCompat& ConstructNotSupportedException(
    MfcSimpleExceptionCompat& exception);
void DestroyNotSupportedException(MfcSimpleExceptionCompat& exception);
MfcSimpleExceptionCompat* DeleteNotSupportedExceptionScalarDtor(
    MfcSimpleExceptionCompat* exception, unsigned flags);
MfcSimpleExceptionCompat& ConstructUserException(
    MfcSimpleExceptionCompat& exception, unsigned help_context,
    const char* message);
void DestroyUserException(MfcSimpleExceptionCompat& exception);
MfcSimpleExceptionCompat* DeleteUserExceptionScalarDtor(
    MfcSimpleExceptionCompat* exception, unsigned flags);
MfcFileExceptionCompat& ConstructFileException(MfcFileExceptionCompat& exception,
    unsigned cause, long os_error, const char* file_name);
void DestroyFileException(MfcFileExceptionCompat& exception);
MfcFileExceptionCompat* DeleteFileExceptionScalarDtor(
    MfcFileExceptionCompat* exception, unsigned flags);
bool SimpleExceptionGetAutoDelete(const MfcSimpleExceptionCompat& exception);
bool GetFileExceptionErrorMessage(const MfcFileExceptionCompat& exception,
    char* destination, int destination_chars, unsigned* help_context = nullptr);
[[noreturn]] void ThrowFileException(unsigned cause, long os_error,
    const char* file_name);
unsigned FileExceptionCauseFromErrno(int value);
unsigned FileExceptionCauseFromOsError(unsigned long value);
bool ValidateWideStringPointer(const wchar_t* text, std::size_t max_chars);
bool ValidateAnsiStringPointer(const char* text, std::size_t max_chars);
bool ValidateMemoryPointer(void* memory, std::size_t bytes, bool writable);
void InvokeMfcVirtualSlot40Value(MfcObjectCompat& object, unsigned value);
void InvokeMfcVirtualSlot40Value1(MfcObjectCompat& object);
void InvokeMfcVirtualSlot40Value16(MfcObjectCompat& object);
void InvokeMfcVirtualSlot40Value2048(MfcObjectCompat& object);
void InvokeMfcVirtualSlot40Value4(MfcObjectCompat& object);
void InvokeMfcVirtualSlot40Value2(MfcObjectCompat& object);
void InvokeMfcVirtualSlot40Value256(MfcObjectCompat& object);
void InvokeMfcVirtualSlot40Value128(MfcObjectCompat& object);
void InvokeMfcVirtualSlot40Value32(MfcObjectCompat& object);

MfcTimeCompat& ConstructMfcTimeFromFields(MfcTimeCompat& out, int year,
    int month, int day, int hour, int minute, int second,
    int daylight_savings = -1);
MfcTimeCompat& ConstructMfcTimeFromDosDateTime(MfcTimeCompat& out,
    unsigned dos_date, unsigned dos_time, int daylight_savings = -1);
#ifdef _WIN32
MfcTimeCompat& ConstructMfcTimeFromSystemTime(MfcTimeCompat& out,
    const SYSTEMTIME& value, int daylight_savings = -1);
MfcTimeCompat& ConstructMfcTimeFromFileTime(MfcTimeCompat& out,
    const FILETIME& value, int daylight_savings = -1);
bool GetCurrentMfcSystemTime(SYSTEMTIME& out);
#endif
MfcTimeCompat& SetMfcTimeToCurrentTime(MfcTimeCompat& out);
std::tm* GetMfcTimeGmtTm(const MfcTimeCompat& value, std::tm* out);
void DumpMfcTime(const MfcTimeCompat& value);
void SerializeMfcTime(std::string& archive, const MfcTimeCompat& value);
bool DeserializeMfcTime(const std::string& archive, MfcTimeCompat& out);
void DumpMfcTimeSpan(const MfcTimeSpanCompat& span);
void SerializeMfcTimeSpan(std::string& archive, const MfcTimeSpanCompat& span);
bool DeserializeMfcTimeSpan(const std::string& archive, MfcTimeSpanCompat& out);
long long MfcTimeSpanFromSecondsValue(long long seconds);
MfcTimeSpanCompat& ConstructMfcTimeSpanFromSeconds(
    MfcTimeSpanCompat& out, long long seconds);
MfcTimeSpanCompat& ConstructMfcTimeSpanFromParts(MfcTimeSpanCompat& out,
    int days, int hours, int minutes, int seconds);
MfcTimeSpanCompat& ConstructMfcTimeSpanCopy(MfcTimeSpanCompat& out,
    const MfcTimeSpanCompat& source);
MfcTimeSpanCompat& AssignMfcTimeSpan(MfcTimeSpanCompat& out,
    const MfcTimeSpanCompat& source);
long long MfcTimeSpanGetDays(const MfcTimeSpanCompat& span);
long long MfcTimeSpanGetTotalHours(const MfcTimeSpanCompat& span);
long long MfcTimeSpanGetHours(const MfcTimeSpanCompat& span);
long long MfcTimeSpanGetTotalMinutes(const MfcTimeSpanCompat& span);
long long MfcTimeSpanGetMinutes(const MfcTimeSpanCompat& span);
long long MfcTimeSpanGetTotalSeconds(const MfcTimeSpanCompat& span);
long long MfcTimeSpanGetSeconds(const MfcTimeSpanCompat& span);
MfcTimeSpanCompat SubtractMfcTimeSpans(const MfcTimeSpanCompat& left,
    const MfcTimeSpanCompat& right);
MfcTimeSpanCompat AddMfcTimeSpans(const MfcTimeSpanCompat& left,
    const MfcTimeSpanCompat& right);
MfcTimeSpanCompat& MfcTimeSpanAddAssign(MfcTimeSpanCompat& left,
    const MfcTimeSpanCompat& right);
MfcTimeSpanCompat& MfcTimeSpanSubtractAssign(MfcTimeSpanCompat& left,
    const MfcTimeSpanCompat& right);
bool MfcTimeSpanEquals(const MfcTimeSpanCompat& left,
    const MfcTimeSpanCompat& right);
bool MfcTimeSpanNotEquals(const MfcTimeSpanCompat& left,
    const MfcTimeSpanCompat& right);
bool MfcTimeSpanLessThan(const MfcTimeSpanCompat& left,
    const MfcTimeSpanCompat& right);
bool MfcTimeSpanGreaterThan(const MfcTimeSpanCompat& left,
    const MfcTimeSpanCompat& right);
bool MfcTimeSpanLessEqual(const MfcTimeSpanCompat& left,
    const MfcTimeSpanCompat& right);
bool MfcTimeSpanGreaterEqual(const MfcTimeSpanCompat& left,
    const MfcTimeSpanCompat& right);
std::time_t MfcTimeFromTimeValue(std::time_t value);
MfcTimeCompat& ConstructMfcTimeFromTimeT(MfcTimeCompat& out,
    std::time_t value);
MfcTimeCompat& ConstructMfcTimeCopy(MfcTimeCompat& out,
    const MfcTimeCompat& source);
MfcTimeCompat& AssignMfcTime(MfcTimeCompat& out,
    const MfcTimeCompat& source);
MfcTimeCompat& AssignMfcTimeFromTimeT(MfcTimeCompat& out,
    std::time_t value);
std::time_t MfcTimeGetTime(const MfcTimeCompat& time);
int MfcTimeGetYearLocal(const MfcTimeCompat& time);
int MfcTimeGetMonthLocal(const MfcTimeCompat& time);
int MfcTimeGetDayLocal(const MfcTimeCompat& time);
int MfcTimeGetHourLocal(const MfcTimeCompat& time);
int MfcTimeGetMinuteLocal(const MfcTimeCompat& time);
int MfcTimeGetSecondLocal(const MfcTimeCompat& time);
int MfcTimeGetDayOfWeekLocal(const MfcTimeCompat& time);
MfcTimeSpanCompat SubtractMfcTimes(const MfcTimeCompat& left,
    const MfcTimeCompat& right);
MfcTimeCompat SubtractMfcTimeSpanFromTime(const MfcTimeCompat& time,
    const MfcTimeSpanCompat& span);
MfcTimeCompat AddMfcTimeSpanToTime(const MfcTimeCompat& time,
    const MfcTimeSpanCompat& span);
MfcTimeCompat& MfcTimeAddAssignSpan(MfcTimeCompat& time,
    const MfcTimeSpanCompat& span);
MfcTimeCompat& MfcTimeSubtractAssignSpan(MfcTimeCompat& time,
    const MfcTimeSpanCompat& span);
bool MfcTimeEquals(const MfcTimeCompat& left, const MfcTimeCompat& right);
bool MfcTimeNotEquals(const MfcTimeCompat& left, const MfcTimeCompat& right);
bool MfcTimeLessThan(const MfcTimeCompat& left, const MfcTimeCompat& right);
bool MfcTimeGreaterThan(const MfcTimeCompat& left, const MfcTimeCompat& right);
bool MfcTimeLessEqual(const MfcTimeCompat& left, const MfcTimeCompat& right);
bool MfcTimeGreaterEqual(const MfcTimeCompat& left, const MfcTimeCompat& right);
std::string FormatMfcTimeSpan(const MfcTimeSpanCompat& span, const char* format);
std::string FormatMfcTimeSpanWithResource(const MfcTimeSpanCompat& span,
    const char* format);
std::string FormatMfcTimeLocal(const MfcTimeCompat& value, const char* format);
std::string FormatMfcTimeGmt(const MfcTimeCompat& value, const char* format);
std::string FormatMfcTimeLocalWithResource(const MfcTimeCompat& value,
    const char* format);
std::string FormatMfcTimeGmtWithResource(const MfcTimeCompat& value,
    const char* format);

MfcCStringCompat& FillCStringBuffer(MfcCStringCompat& text, char value,
    std::size_t count);
MfcCStringCompat& CopyBytesToCString(MfcCStringCompat& text, const void* data,
    std::size_t count);
MfcCStringCompat& ConvertWideStringToCString(MfcCStringCompat& text,
    const wchar_t* source, int source_chars);
MfcCStringCompat& LoadCStringResource(MfcCStringCompat& text, const char* resource_text);
MfcCStringCompat& AssignCStringChar(MfcCStringCompat& text, char value);
MfcCStringCompat& AssignCStringRepeatedChar(MfcCStringCompat& text, char value,
    int count);
int DeleteCStringRange(MfcCStringCompat& text, int index, int count);
int InsertCStringChar(MfcCStringCompat& text, int index, char value);
int InsertCStringText(MfcCStringCompat& text, int index, const char* value);
int ReplaceCStringChar(MfcCStringCompat& text, char old_value, char new_value);
int ReplaceCStringSubstring(MfcCStringCompat& text, const char* old_value,
    const char* new_value);
int RemoveCStringChar(MfcCStringCompat& text, char value);
MfcCStringCompat CStringMidFromIndex(const MfcCStringCompat& text, int index);
MfcCStringCompat CStringMid(const MfcCStringCompat& text, int index, int count);
MfcCStringCompat CStringRight(const MfcCStringCompat& text, int count);
MfcCStringCompat CStringLeft(const MfcCStringCompat& text, int count);
MfcCStringCompat CStringSpanIncluding(const MfcCStringCompat& text,
    const char* chars);
MfcCStringCompat CStringSpanExcluding(const MfcCStringCompat& text,
    const char* chars);
int ReverseFindCStringChar(const MfcCStringCompat& text, char value);
int FindCStringSubstring(const MfcCStringCompat& text, const char* needle,
    int start = 0);
MfcCStringCompat& FormatCStringV(MfcCStringCompat& text, const char* format,
    va_list args);
MfcCStringCompat& FormatCString(MfcCStringCompat& text, const char* format, ...);
MfcCStringCompat& FormatCStringFromResource(MfcCStringCompat& text,
    const char* format, ...);
MfcCStringCompat& FormatMessageCString(MfcCStringCompat& text,
    const char* format, ...);
MfcCStringCompat& FormatMessageCStringFromResource(MfcCStringCompat& text,
    const char* format, ...);
void TrimCStringRightChars(MfcCStringCompat& text, const char* chars);
void TrimCStringRightChar(MfcCStringCompat& text, char value);
void TrimCStringRightWhitespace(MfcCStringCompat& text);
void TrimCStringLeftChars(MfcCStringCompat& text, const char* chars);
void TrimCStringLeftChar(MfcCStringCompat& text, char value);
void TrimCStringLeftWhitespace(MfcCStringCompat& text);
const char* GetCStringNilData();
MfcCStringCompat& ConstructCStringCopy(MfcCStringCompat& destination,
    const MfcCStringCompat& source);
MfcCStringDataHeaderCompat CStringDataHeader(const MfcCStringCompat& text);
MfcCStringCompat& ConstructCStringEmpty(MfcCStringCompat& text);
MfcCStringCompat& ConstructCStringEmptyAndReturn(MfcCStringCompat& text);
MfcCStringCompat& ConstructCStringFromAnsiInline(MfcCStringCompat& text,
    const char* source);
MfcCStringCompat& AssignCStringAnsiInline(MfcCStringCompat& text,
    const char* source);
MfcCStringCompat& AssignCStringAnsiChecked(MfcCStringCompat& text,
    const char* source);
int CStringDataLength(const MfcCStringCompat& text);
int CStringAllocLength(const MfcCStringCompat& text);
bool CStringIsEmptyInline(const MfcCStringCompat& text);
const char* CStringGetStringPtr(const MfcCStringCompat& text);
int SafeAnsiStringLength(const char* text);
int CStringCompareDirectoryTraversalAnsi(const MfcCStringCompat& text,
    const char* other);
int CStringCompareBriefingArchiveAnsi(const MfcCStringCompat& text,
    const char* other);
int CStringCollateAnsi(const MfcCStringCompat& text, const char* other);
int CStringCollateNoCaseAnsi(const MfcCStringCompat& text, const char* other);
char CStringGetAt(const MfcCStringCompat& text, int index);
char CStringSubscript(const MfcCStringCompat& text, int index);
bool CStringEqualsCString(const MfcCStringCompat& left,
    const MfcCStringCompat& right);
bool CStringEqualsAnsi(const MfcCStringCompat& left, const char* right);
bool CStringAnsiEqualsCString(const char* left, const MfcCStringCompat& right);
bool CStringNotEqualsCString(const MfcCStringCompat& left,
    const MfcCStringCompat& right);
bool CStringNotEqualsAnsi(const MfcCStringCompat& left, const char* right);
bool CStringAnsiNotEqualsCString(const char* left,
    const MfcCStringCompat& right);
bool CStringLessThanCString(const MfcCStringCompat& left,
    const MfcCStringCompat& right);
bool CStringLessThanAnsi(const MfcCStringCompat& left, const char* right);
bool CStringAnsiLessThanCString(const char* left,
    const MfcCStringCompat& right);
bool CStringGreaterThanCString(const MfcCStringCompat& left,
    const MfcCStringCompat& right);
bool CStringGreaterThanAnsi(const MfcCStringCompat& left, const char* right);
bool CStringAnsiGreaterThanCString(const char* left,
    const MfcCStringCompat& right);
bool CStringLessEqualCString(const MfcCStringCompat& left,
    const MfcCStringCompat& right);
bool CStringLessEqualAnsi(const MfcCStringCompat& left, const char* right);
bool CStringAnsiLessEqualCString(const char* left,
    const MfcCStringCompat& right);
bool CStringGreaterEqualCString(const MfcCStringCompat& left,
    const MfcCStringCompat& right);
bool CStringGreaterEqualAnsi(const MfcCStringCompat& left, const char* right);
bool CStringAnsiGreaterEqualCString(const char* left,
    const MfcCStringCompat& right);
void AllocCStringBuffer(MfcCStringCompat& text, int length);
void FreeCStringData(void* data);
void ReleaseCString(MfcCStringCompat& text);
void ReleaseCStringData(void* data);
void EmptyCString(MfcCStringCompat& text);
void CStringCopyBeforeWrite(MfcCStringCompat& text);
void CStringAllocBeforeWrite(MfcCStringCompat& text, int length);
void ReleaseCStringBuffer(MfcCStringCompat& text);
void AllocCopyCString(const MfcCStringCompat& source, MfcCStringCompat& target,
    int copy_length, int copy_index, int extra_length);
MfcCStringCompat& ConstructCStringFromAnsiOrResource(MfcCStringCompat& text,
    const char* source);
MfcCStringCompat& ConstructCStringFromWide(MfcCStringCompat& text,
    const wchar_t* source);
MfcArchiveCompat& ConstructArchive(MfcArchiveCompat& archive,
    MfcFileCompat* file, unsigned mode, int buffer_size = 4096,
    void* buffer_start = nullptr);
void DestroyArchive(MfcArchiveCompat& archive);
void ArchiveClose(MfcArchiveCompat& archive);
void ArchiveReleaseBuffers(MfcArchiveCompat& archive);
unsigned ArchiveRead(MfcArchiveCompat& archive, void* buffer, unsigned bytes);
void ArchiveWrite(MfcArchiveCompat& archive, const void* buffer, unsigned bytes);
void ArchiveFlush(MfcArchiveCompat& archive);
void ArchiveEnsureReadBuffer(MfcArchiveCompat& archive, unsigned needed);
void ArchiveWriteCount(MfcArchiveCompat& archive, unsigned count);
unsigned ArchiveReadCount(MfcArchiveCompat& archive);
void ArchiveWriteCString(MfcArchiveCompat& archive,
    const MfcCStringCompat& text);
bool ArchiveReadCString(MfcArchiveCompat& archive, MfcCStringCompat& text);
void ArchiveReadCStringArray(MfcArchiveCompat& archive,
    MfcCStringCompat* values, int count);
MfcRuntimeClassCompat* ArchiveLoadRuntimeClass(MfcArchiveCompat& archive,
    unsigned* schema);
void ArchiveStoreRuntimeClass(MfcArchiveCompat& archive,
    const MfcRuntimeClassCompat& runtime_class);
void ArchiveWriteAnsiString(MfcArchiveCompat& archive, const char* text);
char* ArchiveReadLine(MfcArchiveCompat& archive, char* buffer, int max_chars);
char* ArchiveReadLineComplete(char* buffer, int length);
bool ArchiveReadStringLine(MfcArchiveCompat& archive, MfcCStringCompat& text);
bool ArchiveExceptionGetErrorMessage(unsigned cause, char* destination,
    int destination_chars, unsigned* help_context);
void ArchiveCheckObjectTagLimit(MfcArchiveCompat& archive);
void ArchiveWriteObject(MfcArchiveCompat& archive, MfcObjectCompat* object);
MfcObjectCompat* ArchiveReadObject(MfcArchiveCompat& archive,
    const MfcRuntimeClassCompat* expected_class = nullptr);
bool ArchiveIsStoring(const MfcArchiveCompat& archive);
bool ArchiveIsLoading(const MfcArchiveCompat& archive);
bool ArchiveIsByteSwapping(const MfcArchiveCompat& archive);
bool ArchiveIsBufferEmpty(const MfcArchiveCompat& archive);
unsigned ArchiveGetObjectSchema(const MfcArchiveCompat& archive);
void ArchiveSetObjectSchema(MfcArchiveCompat& archive, unsigned schema);
void ArchiveSetLoadParams(MfcArchiveCompat& archive, unsigned grow_by,
    unsigned hash_size);
void ArchiveSetStoreParams(MfcArchiveCompat& archive, unsigned hash_size);
MfcArchiveCompat& ArchiveWriteIntInline(MfcArchiveCompat& archive, int value);
MfcArchiveCompat& ArchiveWriteUIntInline(MfcArchiveCompat& archive,
    unsigned value);
MfcArchiveCompat& ArchiveWriteShortInline(MfcArchiveCompat& archive,
    short value);
MfcArchiveCompat& ArchiveWriteCharInline(MfcArchiveCompat& archive,
    char value);
MfcArchiveCompat& ArchiveWriteByteInline(MfcArchiveCompat& archive,
    unsigned char value);
MfcArchiveCompat& ArchiveWriteWordInline(MfcArchiveCompat& archive,
    unsigned short value);
MfcArchiveCompat& ArchiveWriteDWordInline(MfcArchiveCompat& archive,
    unsigned value);
MfcArchiveCompat& ArchiveWriteLongInline(MfcArchiveCompat& archive,
    long value);
MfcArchiveCompat& ArchiveWriteFloatInline(MfcArchiveCompat& archive,
    float value);
MfcArchiveCompat& ArchiveWriteDoubleInline(MfcArchiveCompat& archive,
    double value);
MfcArchiveCompat& ArchiveReadIntInline(MfcArchiveCompat& archive, int& value);
MfcArchiveCompat& ArchiveReadUIntInline(MfcArchiveCompat& archive,
    unsigned& value);
MfcArchiveCompat& ArchiveReadShortInline(MfcArchiveCompat& archive,
    short& value);
MfcArchiveCompat& ArchiveReadCharInline(MfcArchiveCompat& archive,
    char& value);
MfcArchiveCompat& ArchiveReadByteInline(MfcArchiveCompat& archive,
    unsigned char& value);
MfcArchiveCompat& ArchiveReadWordInline(MfcArchiveCompat& archive,
    unsigned short& value);
MfcArchiveCompat& ArchiveReadDWordInline(MfcArchiveCompat& archive,
    unsigned& value);
MfcArchiveCompat& ArchiveReadLongInline(MfcArchiveCompat& archive,
    long& value);
MfcArchiveCompat& ArchiveReadDoubleInline(MfcArchiveCompat& archive,
    double& value);
MfcArchiveCompat& ArchiveReadFloatInline(MfcArchiveCompat& archive,
    float& value);
MfcCStringCompat& ArchiveConstructEmptyCString(MfcCStringCompat& text);
void ArchiveNoopInline();
MfcArchiveCompat& ArchiveReadObjectPointerInline(MfcArchiveCompat& archive,
    MfcObjectCompat*& object);
unsigned ArchiveResetObjectSchema(MfcArchiveCompat& archive);
void ArchiveMapObject(MfcArchiveCompat& archive, MfcObjectCompat* object);
void ArchiveWriteClass(MfcArchiveCompat& archive,
    const MfcRuntimeClassCompat& runtime_class);
MfcRuntimeClassCompat* ArchiveReadClass(MfcArchiveCompat& archive,
    const MfcRuntimeClassCompat* expected_class = nullptr,
    unsigned* schema = nullptr, unsigned* object_tag = nullptr);
[[noreturn]] void ThrowArchiveException(unsigned cause,
    const char* detail = nullptr);
bool LoadLogFontFromResourceString(UINT resource_id, LOGFONTA& font,
    int pixels_per_inch_y = 0);
bool IsComboBoxWithStyle(HWND window, UINT combo_style);
bool WindowHasClassName(HWND window, const char* class_name);
HWND FindVisibleChildWindowAtPoint(HWND parent, LONG x, LONG y);
void SetWindowTextIfChanged(HWND window, const char* text);
void DeleteGdiObjectHandle(HGDIOBJ* handle);
void CloseFocusedComboBoxDropDown(HWND owner);
void UnlockAndFreeGlobal(HGLOBAL global);
int CriticalMemoryNewHandler(int requested_size);
void ArchiveSerializeCString(void* archive, MfcCStringCompat& text);
void AssignCopyCString(MfcCStringCompat& text, int length, const char* source);
MfcCStringCompat& AssignCStringCopy(MfcCStringCompat& text,
    const MfcCStringCompat& source);
MfcCStringCompat& AssignCStringAnsi(MfcCStringCompat& text, const char* source);
MfcCStringCompat& AssignCStringWide(MfcCStringCompat& text,
    const wchar_t* source);
void ConcatCopyCString(MfcCStringCompat& text, int left_length,
    const char* left, int right_length, const char* right);
MfcCStringCompat ConcatCStringStrings(const MfcCStringCompat& left,
    const MfcCStringCompat& right);
MfcCStringCompat ConcatCStringAndAnsi(const MfcCStringCompat& left,
    const char* right);
MfcCStringCompat ConcatAnsiAndCString(const char* left,
    const MfcCStringCompat& right);
void AppendCStringRaw(MfcCStringCompat& text, int length, const char* source);
MfcCStringCompat& AppendCStringAnsi(MfcCStringCompat& text, const char* source);
MfcCStringCompat& AppendCStringChar(MfcCStringCompat& text, char value);
MfcCStringCompat& AppendCString(MfcCStringCompat& text,
    const MfcCStringCompat& source);
char* CStringGetBuffer(MfcCStringCompat& text, int min_length);
void CStringReleaseBuffer(MfcCStringCompat& text, int new_length);
char* CStringGetBufferSetLength(MfcCStringCompat& text, int new_length);
void CStringFreeExtra(MfcCStringCompat& text);
char* CStringLockBuffer(MfcCStringCompat& text);
void CStringUnlockBuffer(MfcCStringCompat& text);
int CStringFindChar(const MfcCStringCompat& text, char value);
int CStringFindCharFrom(const MfcCStringCompat& text, char value, int start);
int CStringFindOneOf(const MfcCStringCompat& text, const char* chars);
void CStringMakeLower(MfcCStringCompat& text);
void CStringMakeUpper(MfcCStringCompat& text);
void CStringMakeReverse(MfcCStringCompat& text);
void CStringSetAt(MfcCStringCompat& text, int index, char value);
void CStringAnsiToOem(MfcCStringCompat& text);
void CStringOemToAnsi(MfcCStringCompat& text);
int WideCharToAnsiCounted(char* destination, const wchar_t* source,
    int destination_chars);
int AnsiToWideCounted(wchar_t* destination, const char* source,
    int destination_chars);
wchar_t* AfxAnsiToWideHelper(wchar_t* destination, const char* source,
    int destination_chars);
char* AfxWideToAnsiHelper(char* destination, const wchar_t* source,
    int destination_chars);
void ConstructCStringArrayElements(std::vector<MfcCStringCompat>& values,
    std::size_t count);
void DestroyCStringArrayElements(std::vector<MfcCStringCompat>& values);
void CopyCStringArrayElements(std::vector<MfcCStringCompat>& destination,
    const std::vector<MfcCStringCompat>& source, std::size_t count);
unsigned HashWideStringKey(const wchar_t* text);

void* PtrListGetPlexData(MfcPtrListCompat& list);
MfcPtrListCompat& ConstructPtrList(MfcPtrListCompat& list, int block_size);
void PtrListRemoveAll(MfcPtrListCompat& list);
void DestructPtrList(MfcPtrListCompat& list);
MfcPtrListNodeCompat* PtrListNewNode(MfcPtrListCompat& list,
    MfcPtrListNodeCompat* previous, MfcPtrListNodeCompat* next);
void PtrListFreeNode(MfcPtrListCompat& list, MfcPtrListNodeCompat* node);
MfcPtrListNodeCompat* PtrListAddHead(MfcPtrListCompat& list, void* value);
MfcPtrListNodeCompat* PtrListAddTail(MfcPtrListCompat& list, void* value);
void PtrListAddHeadList(MfcPtrListCompat& list,
    const MfcPtrListCompat& source);
void PtrListAddTailList(MfcPtrListCompat& list,
    const MfcPtrListCompat& source);
void* PtrListRemoveHead(MfcPtrListCompat& list);
void* PtrListRemoveTail(MfcPtrListCompat& list);
MfcPtrListNodeCompat* PtrListInsertBefore(MfcPtrListCompat& list,
    MfcPtrListNodeCompat* position, void* value);
MfcPtrListNodeCompat* PtrListInsertAfter(MfcPtrListCompat& list,
    MfcPtrListNodeCompat* position, void* value);
void PtrListRemoveAt(MfcPtrListCompat& list, MfcPtrListNodeCompat* position);
MfcPtrListNodeCompat* PtrListFind(MfcPtrListCompat& list, void* value,
    MfcPtrListNodeCompat* start_after = nullptr);
MfcPtrListNodeCompat* PtrListFindIndex(MfcPtrListCompat& list, int index);
void PtrListDump(const MfcPtrListCompat& list);
void PtrListAssertValid(const MfcPtrListCompat& list);
MfcPtrListCompat* DeletePtrListScalarDtor(MfcPtrListCompat* list,
    unsigned flags);
int PtrListGetCount(const MfcPtrListCompat& list);
bool PtrListIsEmpty(const MfcPtrListCompat& list);
void*& PtrListGetHeadRef(MfcPtrListCompat& list);
void* PtrListGetHead(const MfcPtrListCompat& list);
void*& PtrListGetTailRef(MfcPtrListCompat& list);
void* PtrListGetTail(const MfcPtrListCompat& list);
MfcPtrListNodeCompat* PtrListGetHeadPosition(const MfcPtrListCompat& list);
MfcPtrListNodeCompat* PtrListGetTailPosition(const MfcPtrListCompat& list);
void*& PtrListGetNextRef(MfcPtrListNodeCompat*& position);
void* PtrListGetNext(MfcPtrListNodeCompat*& position);
void*& PtrListGetPrevRef(MfcPtrListNodeCompat*& position);
void* PtrListGetPrev(MfcPtrListNodeCompat*& position);
void*& PtrListGetAtRef(MfcPtrListNodeCompat* position);
void* PtrListGetAt(MfcPtrListNodeCompat* position);
void PtrListSetAt(MfcPtrListNodeCompat* position, void* value);

int ObListGetCount(const MfcObListCompat& list);
bool ObListIsEmpty(const MfcObListCompat& list);
void*& ObListGetHeadRef(MfcObListCompat& list);
void* ObListGetHead(const MfcObListCompat& list);
void*& ObListGetTailRef(MfcObListCompat& list);
void* ObListGetTail(const MfcObListCompat& list);
MfcObListNodeCompat* ObListGetHeadPosition(const MfcObListCompat& list);
MfcObListNodeCompat* ObListGetTailPosition(const MfcObListCompat& list);
void*& ObListGetNextRef(MfcObListNodeCompat*& position);
void* ObListGetNext(MfcObListNodeCompat*& position);
void*& ObListGetPrevRef(MfcObListNodeCompat*& position);
void* ObListGetPrev(MfcObListNodeCompat*& position);
void*& ObListGetAtRef(MfcObListNodeCompat* position);
void* ObListGetAt(MfcObListNodeCompat* position);
void ObListSetAt(MfcObListNodeCompat* position, void* value);

MfcCStringListCompat& ConstructCStringList(MfcCStringListCompat& list,
    int block_size);
void CStringListRemoveAll(MfcCStringListCompat& list);
void DestructCStringList(MfcCStringListCompat& list);
MfcCStringListNodeCompat* CStringListAddHead(MfcCStringListCompat& list,
    const MfcCStringCompat& value);
MfcCStringListNodeCompat* CStringListAddTail(MfcCStringListCompat& list,
    const MfcCStringCompat& value);
MfcCStringListNodeCompat* CStringListAddHeadText(
    MfcCStringListCompat& list, const char* text);
MfcCStringListNodeCompat* CStringListAddTailText(
    MfcCStringListCompat& list, const char* text);
int CStringListGetCount(const MfcCStringListCompat& list);
bool CStringListIsEmpty(const MfcCStringListCompat& list);
MfcCStringCompat& CStringListGetHeadRef(MfcCStringListCompat& list);
MfcCStringCompat CStringListGetHead(const MfcCStringListCompat& list);
MfcCStringCompat& CStringListGetTailRef(MfcCStringListCompat& list);
MfcCStringCompat CStringListGetTail(const MfcCStringListCompat& list);
MfcCStringListNodeCompat* CStringListGetHeadPosition(
    const MfcCStringListCompat& list);
MfcCStringListNodeCompat* CStringListGetTailPosition(
    const MfcCStringListCompat& list);
MfcCStringCompat& CStringListGetNextRef(
    MfcCStringListNodeCompat*& position);
MfcCStringCompat CStringListGetNext(MfcCStringListNodeCompat*& position);
MfcCStringCompat& CStringListGetPrevRef(
    MfcCStringListNodeCompat*& position);
MfcCStringCompat CStringListGetPrev(MfcCStringListNodeCompat*& position);
MfcCStringCompat& CStringListGetAtRef(MfcCStringListNodeCompat* position);
MfcCStringCompat CStringListGetAt(MfcCStringListNodeCompat* position);
void CStringListSetAtText(MfcCStringListNodeCompat* position,
    const char* text);
void CStringListSetAtString(MfcCStringListNodeCompat* position,
    const MfcCStringCompat& value);

int ByteArrayGetSize(const MfcByteArrayCompat& array);
int ByteArrayGetUpperBound(const MfcByteArrayCompat& array);
void ByteArrayRemoveAll(MfcByteArrayCompat& array);
unsigned char ByteArrayGetAt(const MfcByteArrayCompat& array, int index);
void ByteArraySetAt(MfcByteArrayCompat& array, int index,
    unsigned char value);
unsigned char& ByteArrayElementAt(MfcByteArrayCompat& array, int index);
unsigned char* ByteArrayGetData(MfcByteArrayCompat& array);
const unsigned char* ByteArrayGetDataConst(const MfcByteArrayCompat& array);
unsigned char ByteArraySubscript(const MfcByteArrayCompat& array, int index);
unsigned char& ByteArraySubscriptRef(MfcByteArrayCompat& array, int index);
MfcByteArrayCompat& ByteArraySetSize(MfcByteArrayCompat& array,
    int new_size, int grow_by);
int ByteArrayAppend(MfcByteArrayCompat& destination,
    const MfcByteArrayCompat& source);
MfcByteArrayCompat& ByteArrayCopy(MfcByteArrayCompat& destination,
    const MfcByteArrayCompat& source);
MfcByteArrayCompat& ByteArrayFreeExtra(MfcByteArrayCompat& array);
MfcByteArrayCompat& ByteArraySetAtGrow(MfcByteArrayCompat& array, int index,
    unsigned char value);
MfcByteArrayCompat& ByteArrayInsertAt(MfcByteArrayCompat& array, int index,
    unsigned char value, int count);
MfcByteArrayCompat& ByteArrayRemoveAt(MfcByteArrayCompat& array, int index,
    int count);
MfcByteArrayCompat& ByteArrayInsertArrayAt(MfcByteArrayCompat& array,
    int index, const MfcByteArrayCompat& source);
void ByteArraySerialize(MfcByteArrayCompat& array, void* archive);
void ByteArrayAssertValid(const MfcByteArrayCompat& array);
MfcByteArrayCompat* DeleteByteArrayScalarDtor(MfcByteArrayCompat* array,
    unsigned flags);

int WordArrayGetSize(const MfcWordArrayCompat& array);
int WordArrayGetUpperBound(const MfcWordArrayCompat& array);
void WordArrayRemoveAll(MfcWordArrayCompat& array);
unsigned short WordArrayGetAt(const MfcWordArrayCompat& array, int index);
void WordArraySetAt(MfcWordArrayCompat& array, int index,
    unsigned short value);
unsigned short& WordArrayElementAt(MfcWordArrayCompat& array, int index);
unsigned short* WordArrayGetData(MfcWordArrayCompat& array);
const unsigned short* WordArrayGetDataConst(const MfcWordArrayCompat& array);
unsigned short WordArraySubscript(const MfcWordArrayCompat& array, int index);
unsigned short& WordArraySubscriptRef(MfcWordArrayCompat& array, int index);
MfcWordArrayCompat& WordArraySetSize(MfcWordArrayCompat& array,
    int new_size, int grow_by);
int WordArrayAppend(MfcWordArrayCompat& destination,
    const MfcWordArrayCompat& source);
MfcWordArrayCompat& WordArrayCopy(MfcWordArrayCompat& destination,
    const MfcWordArrayCompat& source);
MfcWordArrayCompat& WordArrayFreeExtra(MfcWordArrayCompat& array);
MfcWordArrayCompat& WordArraySetAtGrow(MfcWordArrayCompat& array, int index,
    unsigned short value);
MfcWordArrayCompat& WordArrayInsertAt(MfcWordArrayCompat& array, int index,
    unsigned short value, int count);
MfcWordArrayCompat& WordArrayRemoveAt(MfcWordArrayCompat& array, int index,
    int count);
MfcWordArrayCompat& WordArrayInsertArrayAt(MfcWordArrayCompat& array,
    int index, const MfcWordArrayCompat& source);
void WordArraySerialize(MfcWordArrayCompat& array, void* archive);
void WordArrayAssertValid(const MfcWordArrayCompat& array);
MfcWordArrayCompat* DeleteWordArrayScalarDtor(MfcWordArrayCompat* array,
    unsigned flags);

int DWordArrayGetSize(const MfcDWordArrayCompat& array);
int DWordArrayGetUpperBound(const MfcDWordArrayCompat& array);
void DWordArrayRemoveAll(MfcDWordArrayCompat& array);
unsigned long DWordArrayGetAt(const MfcDWordArrayCompat& array, int index);
void DWordArraySetAt(MfcDWordArrayCompat& array, int index,
    unsigned long value);
unsigned long& DWordArrayElementAt(MfcDWordArrayCompat& array, int index);
unsigned long* DWordArrayGetData(MfcDWordArrayCompat& array);
const unsigned long* DWordArrayGetDataConst(
    const MfcDWordArrayCompat& array);
unsigned long DWordArraySubscript(const MfcDWordArrayCompat& array,
    int index);
unsigned long& DWordArraySubscriptRef(MfcDWordArrayCompat& array,
    int index);
MfcDWordArrayCompat& DWordArraySetSize(MfcDWordArrayCompat& array,
    int new_size, int grow_by);
int DWordArrayAppend(MfcDWordArrayCompat& destination,
    const MfcDWordArrayCompat& source);
MfcDWordArrayCompat& DWordArrayCopy(MfcDWordArrayCompat& destination,
    const MfcDWordArrayCompat& source);
MfcDWordArrayCompat& DWordArrayFreeExtra(MfcDWordArrayCompat& array);
MfcDWordArrayCompat& DWordArraySetAtGrow(MfcDWordArrayCompat& array,
    int index, unsigned long value);
MfcDWordArrayCompat& DWordArrayInsertAt(MfcDWordArrayCompat& array,
    int index, unsigned long value, int count);
MfcDWordArrayCompat& DWordArrayRemoveAt(MfcDWordArrayCompat& array,
    int index, int count);
MfcDWordArrayCompat& DWordArrayInsertArrayAt(MfcDWordArrayCompat& array,
    int index, const MfcDWordArrayCompat& source);
void DWordArraySerialize(MfcDWordArrayCompat& array, void* archive);
void DWordArrayAssertValid(const MfcDWordArrayCompat& array);
MfcDWordArrayCompat* DeleteDWordArrayScalarDtor(
    MfcDWordArrayCompat* array, unsigned flags);

int UIntArrayGetSize(const MfcUIntArrayCompat& array);
int UIntArrayGetUpperBound(const MfcUIntArrayCompat& array);
void UIntArrayRemoveAll(MfcUIntArrayCompat& array);
unsigned int UIntArrayGetAt(const MfcUIntArrayCompat& array, int index);
void UIntArraySetAt(MfcUIntArrayCompat& array, int index,
    unsigned int value);
unsigned int& UIntArrayElementAt(MfcUIntArrayCompat& array, int index);
unsigned int* UIntArrayGetData(MfcUIntArrayCompat& array);
const unsigned int* UIntArrayGetDataConst(const MfcUIntArrayCompat& array);
unsigned int UIntArraySubscript(const MfcUIntArrayCompat& array, int index);
unsigned int& UIntArraySubscriptRef(MfcUIntArrayCompat& array, int index);
MfcUIntArrayCompat& UIntArraySetSize(MfcUIntArrayCompat& array,
    int new_size, int grow_by);
int UIntArrayAppend(MfcUIntArrayCompat& destination,
    const MfcUIntArrayCompat& source);
MfcUIntArrayCompat& UIntArrayCopy(MfcUIntArrayCompat& destination,
    const MfcUIntArrayCompat& source);
MfcUIntArrayCompat& UIntArrayFreeExtra(MfcUIntArrayCompat& array);
MfcUIntArrayCompat& UIntArraySetAtGrow(MfcUIntArrayCompat& array, int index,
    unsigned int value);
MfcUIntArrayCompat& UIntArrayInsertAt(MfcUIntArrayCompat& array, int index,
    unsigned int value, int count);
MfcUIntArrayCompat& UIntArrayRemoveAt(MfcUIntArrayCompat& array, int index,
    int count);
MfcUIntArrayCompat& UIntArrayInsertArrayAt(MfcUIntArrayCompat& array,
    int index, const MfcUIntArrayCompat& source);
void UIntArrayAssertValid(const MfcUIntArrayCompat& array);
MfcUIntArrayCompat* DeleteUIntArrayScalarDtor(MfcUIntArrayCompat* array,
    unsigned flags);

int PtrArrayGetSize(const MfcPtrArrayCompat& array);
int PtrArrayGetUpperBound(const MfcPtrArrayCompat& array);
void PtrArrayRemoveAll(MfcPtrArrayCompat& array);
void* PtrArrayGetAt(const MfcPtrArrayCompat& array, int index);
void PtrArraySetAt(MfcPtrArrayCompat& array, int index, void* value);
void*& PtrArrayElementAt(MfcPtrArrayCompat& array, int index);
void** PtrArrayGetData(MfcPtrArrayCompat& array);
void* const* PtrArrayGetDataConst(const MfcPtrArrayCompat& array);
void* PtrArraySubscript(const MfcPtrArrayCompat& array, int index);
void*& PtrArraySubscriptRef(MfcPtrArrayCompat& array, int index);
MfcPtrArrayCompat& PtrArraySetSize(MfcPtrArrayCompat& array, int new_size,
    int grow_by);
int PtrArrayAppend(MfcPtrArrayCompat& destination,
    const MfcPtrArrayCompat& source);
MfcPtrArrayCompat& PtrArrayCopy(MfcPtrArrayCompat& destination,
    const MfcPtrArrayCompat& source);
MfcPtrArrayCompat& PtrArrayFreeExtra(MfcPtrArrayCompat& array);
MfcPtrArrayCompat& PtrArraySetAtGrow(MfcPtrArrayCompat& array, int index,
    void* value);
MfcPtrArrayCompat& PtrArrayInsertAt(MfcPtrArrayCompat& array, int index,
    void* value, int count);
MfcPtrArrayCompat& PtrArrayRemoveAt(MfcPtrArrayCompat& array, int index,
    int count);
MfcPtrArrayCompat& PtrArrayInsertArrayAt(MfcPtrArrayCompat& array, int index,
    const MfcPtrArrayCompat& source);
void PtrArrayDump(const MfcPtrArrayCompat& array);
void PtrArrayAssertValid(const MfcPtrArrayCompat& array);
MfcPtrArrayCompat* DeletePtrArrayScalarDtor(MfcPtrArrayCompat* array,
    unsigned flags);

MfcObArrayCompat& ConstructObArray(MfcObArrayCompat& array);
void DestructObArray(MfcObArrayCompat& array);
int ObArrayGetSize(const MfcObArrayCompat& array);
int ObArrayGetUpperBound(const MfcObArrayCompat& array);
void ObArrayRemoveAll(MfcObArrayCompat& array);
void* ObArrayGetAt(const MfcObArrayCompat& array, int index);
void ObArraySetAt(MfcObArrayCompat& array, int index, void* value);
void*& ObArrayElementAt(MfcObArrayCompat& array, int index);
void** ObArrayGetData(MfcObArrayCompat& array);
void* const* ObArrayGetDataConst(const MfcObArrayCompat& array);
void* ObArraySubscript(const MfcObArrayCompat& array, int index);
void*& ObArraySubscriptRef(MfcObArrayCompat& array, int index);
MfcObArrayCompat& ObArraySetSize(MfcObArrayCompat& array, int new_size,
    int grow_by);
int ObArrayAppend(MfcObArrayCompat& destination,
    const MfcObArrayCompat& source);
MfcObArrayCompat& ObArrayCopy(MfcObArrayCompat& destination,
    const MfcObArrayCompat& source);
MfcObArrayCompat& ObArrayFreeExtra(MfcObArrayCompat& array);
MfcObArrayCompat& ObArraySetAtGrow(MfcObArrayCompat& array, int index,
    void* value);
MfcObArrayCompat& ObArrayInsertAt(MfcObArrayCompat& array, int index,
    void* value, int count);
MfcObArrayCompat& ObArrayRemoveAt(MfcObArrayCompat& array, int index,
    int count);
MfcObArrayCompat& ObArrayInsertArrayAt(MfcObArrayCompat& array, int index,
    const MfcObArrayCompat& source);
void ObArraySerialize(MfcObArrayCompat& array, void* archive);
void ObArrayDump(const MfcObArrayCompat& array);
void ObArrayAssertValid(const MfcObArrayCompat& array);
MfcObArrayCompat* DeleteObArrayScalarDtor(MfcObArrayCompat* array,
    unsigned flags);

MfcCStringArrayCompat& ConstructCStringArray(MfcCStringArrayCompat& array);
void DestructCStringArray(MfcCStringArrayCompat& array);
int CStringArrayGetSize(const MfcCStringArrayCompat& array);
int CStringArrayGetUpperBound(const MfcCStringArrayCompat& array);
void CStringArrayRemoveAll(MfcCStringArrayCompat& array);
MfcCStringCompat CStringArrayGetAt(const MfcCStringArrayCompat& array,
    int index);
void CStringArraySetAtText(MfcCStringArrayCompat& array, int index,
    const char* text);
void CStringArraySetAtString(MfcCStringArrayCompat& array, int index,
    const MfcCStringCompat& value);
MfcCStringCompat& CStringArrayElementAt(MfcCStringArrayCompat& array,
    int index);
MfcCStringCompat* CStringArrayGetData(MfcCStringArrayCompat& array);
const MfcCStringCompat* CStringArrayGetDataConst(
    const MfcCStringArrayCompat& array);
MfcCStringCompat CStringArraySubscript(const MfcCStringArrayCompat& array,
    int index);
MfcCStringCompat& CStringArraySubscriptRef(MfcCStringArrayCompat& array,
    int index);
void CStringArrayDestroyElements(MfcCStringCompat* values, int count);
void CStringArrayDestroyElement(MfcCStringCompat& value);
MfcCStringArrayCompat& CStringArraySetSize(MfcCStringArrayCompat& array,
    int new_size, int grow_by);
void CStringArrayConstructElements(MfcCStringCompat* values, int count);
MfcCStringCompat& CStringArrayConstructElement(MfcCStringCompat& value);
int CStringArrayAppend(MfcCStringArrayCompat& destination,
    const MfcCStringArrayCompat& source);
void CStringArrayCopyElements(MfcCStringCompat* destination,
    const MfcCStringCompat* source, int count);
MfcCStringArrayCompat& CStringArrayCopy(MfcCStringArrayCompat& destination,
    const MfcCStringArrayCompat& source);
MfcCStringArrayCompat& CStringArrayFreeExtra(MfcCStringArrayCompat& array);
MfcCStringArrayCompat& CStringArraySetAtGrowText(
    MfcCStringArrayCompat& array, int index, const char* text);
MfcCStringArrayCompat& CStringArraySetAtGrowString(
    MfcCStringArrayCompat& array, int index, const MfcCStringCompat& value);
MfcCStringArrayCompat& CStringArrayInsertEmptyAt(
    MfcCStringArrayCompat& array, int index, int count);
MfcCStringArrayCompat& CStringArrayInsertTextAt(
    MfcCStringArrayCompat& array, int index, const char* text, int count);
MfcCStringArrayCompat& CStringArrayInsertStringAt(
    MfcCStringArrayCompat& array, int index, const MfcCStringCompat& value,
    int count);
MfcCStringArrayCompat& CStringArrayRemoveAt(MfcCStringArrayCompat& array,
    int index, int count);
MfcCStringArrayCompat& CStringArrayInsertArrayAt(
    MfcCStringArrayCompat& array, int index,
    const MfcCStringArrayCompat& source);
void CStringArraySerialize(MfcCStringArrayCompat& array, void* archive);
void CStringArrayDump(const MfcCStringArrayCompat& array);
void CStringArrayAssertValid(const MfcCStringArrayCompat& array);
MfcCStringArrayCompat* DeleteCStringArrayScalarDtor(
    MfcCStringArrayCompat* array, unsigned flags);
MfcCStringCompat* DeleteCStringScalarDtor(MfcCStringCompat* value,
    unsigned flags);

MfcPlexCompat* PlexCreate(MfcPlexCompat*& head, int max_elements,
    int element_size);
void PlexFreeDataChain(MfcPlexCompat*& head);
unsigned MapPtrToPtrHashKey(void* key);
MfcMapPtrToPtrCompat& ConstructMapPtrToPtr(MfcMapPtrToPtrCompat& map,
    int block_size);
MfcMapPtrToPtrCompat& MapPtrToPtrInitHashTable(
    MfcMapPtrToPtrCompat& map, unsigned hash_size, bool alloc_now);
void MapPtrToPtrRemoveAll(MfcMapPtrToPtrCompat& map);
void DestructMapPtrToPtr(MfcMapPtrToPtrCompat& map);
MfcMapPtrToPtrAssocCompat* MapPtrToPtrNewAssoc(MfcMapPtrToPtrCompat& map);
void MapPtrToPtrFreeAssoc(MfcMapPtrToPtrCompat& map,
    MfcMapPtrToPtrAssocCompat* assoc);
MfcMapPtrToPtrAssocCompat* MapPtrToPtrGetAssocAt(
    MfcMapPtrToPtrCompat& map, void* key, unsigned* hash);
void* MapPtrToPtrGetValueAt(MfcMapPtrToPtrCompat& map, void* key);
bool MapPtrToPtrLookup(MfcMapPtrToPtrCompat& map, void* key, void*& value);
void** MapPtrToPtrGetOrCreateValue(MfcMapPtrToPtrCompat& map, void* key);
bool MapPtrToPtrRemoveKey(MfcMapPtrToPtrCompat& map, void* key);
MfcMapPtrToPtrAssocCompat* MapPtrToPtrGetStartPosition(
    MfcMapPtrToPtrCompat& map);
void MapPtrToPtrGetNextAssoc(MfcMapPtrToPtrCompat& map,
    MfcMapPtrToPtrAssocCompat*& position, void*& key, void*& value);
void MapPtrToPtrDump(const MfcMapPtrToPtrCompat& map);
void MapPtrToPtrAssertValid(const MfcMapPtrToPtrCompat& map);
MfcMapPtrToPtrCompat* DeleteMapPtrToPtrScalarDtor(
    MfcMapPtrToPtrCompat* map, unsigned flags);
int MapWordToPtrGetCount(const MfcMapWordToPtrCompat& map);
bool MapWordToPtrIsEmpty(const MfcMapWordToPtrCompat& map);
void MapWordToPtrSetAt(MfcMapWordToPtrCompat& map, unsigned short key,
    void* value);
void* MapWordToPtrGetStartPositionSentinel(
    const MfcMapWordToPtrCompat& map);
int MapWordToPtrGetHashTableSize(const MfcMapWordToPtrCompat& map);
int MapPtrToWordGetCount(const MfcMapPtrToWordCompat& map);
bool MapPtrToWordIsEmpty(const MfcMapPtrToWordCompat& map);
void MapPtrToWordSetAt(MfcMapPtrToWordCompat& map, void* key,
    unsigned short value);
void* MapPtrToWordGetStartPositionSentinel(
    const MfcMapPtrToWordCompat& map);
int MapPtrToWordGetHashTableSize(const MfcMapPtrToWordCompat& map);
int MapPtrToPtrGetCount(const MfcMapPtrToPtrCompat& map);
bool MapPtrToPtrIsEmpty(const MfcMapPtrToPtrCompat& map);
void MapPtrToPtrSetAt(MfcMapPtrToPtrCompat& map, void* key, void* value);
void* MapPtrToPtrGetStartPositionSentinel(
    const MfcMapPtrToPtrCompat& map);
int MapPtrToPtrGetHashTableSize(const MfcMapPtrToPtrCompat& map);
int MapWordToObGetCount(const MfcMapWordToObCompat& map);
bool MapWordToObIsEmpty(const MfcMapWordToObCompat& map);
void MapWordToObSetAt(MfcMapWordToObCompat& map, unsigned short key,
    void* value);
void* MapWordToObGetStartPositionSentinel(
    const MfcMapWordToObCompat& map);
int MapWordToObGetHashTableSize(const MfcMapWordToObCompat& map);
int MapStringToPtrGetCount(const MfcMapStringToPtrCompat& map);
bool MapStringToPtrIsEmpty(const MfcMapStringToPtrCompat& map);
void MapStringToPtrSetAt(MfcMapStringToPtrCompat& map, const char* key,
    void* value);
void* MapStringToPtrGetStartPositionSentinel(
    const MfcMapStringToPtrCompat& map);
int MapStringToPtrGetHashTableSize(const MfcMapStringToPtrCompat& map);
int MapStringToObGetCount(const MfcMapStringToObCompat& map);
bool MapStringToObIsEmpty(const MfcMapStringToObCompat& map);
void MapStringToObSetAt(MfcMapStringToObCompat& map, const char* key,
    void* value);
void* MapStringToObGetStartPositionSentinel(
    const MfcMapStringToObCompat& map);
int MapStringToObGetHashTableSize(const MfcMapStringToObCompat& map);
int MapStringToStringGetCount(const MfcMapStringToStringCompat& map);
bool MapStringToStringIsEmpty(const MfcMapStringToStringCompat& map);
void MapStringToStringSetAtText(MfcMapStringToStringCompat& map,
    const char* key, const char* value);
void* MapStringToStringGetStartPositionSentinel(
    const MfcMapStringToStringCompat& map);
int MapStringToStringGetHashTableSize(
    const MfcMapStringToStringCompat& map);

#ifdef _WIN32
SIZE SizeConstructXY(LONG cx, LONG cy);
SIZE SizeConstructFromSize(SIZE value);
SIZE SizeConstructFromPoint(POINT value);
SIZE SizeConstructFromDWord(DWORD value);
bool SizeEquals(SIZE left, SIZE right);
bool SizeNotEquals(SIZE left, SIZE right);
SIZE& SizeAddAssign(SIZE& value, SIZE delta);
SIZE& SizeSubtractAssign(SIZE& value, SIZE delta);
POINT PointConstructXY(LONG x, LONG y);
POINT PointConstructFromPoint(POINT value);
POINT PointConstructFromSize(SIZE value);
POINT PointConstructFromDWord(DWORD value);
void PointOffsetXY(POINT& point, LONG x, LONG y);
void PointOffsetSize(POINT& point, SIZE size);
bool PointEquals(POINT left, POINT right);
bool PointNotEquals(POINT left, POINT right);
POINT& PointAddAssignSize(POINT& point, SIZE size);
POINT& PointSubtractAssignSize(POINT& point, SIZE size);
POINT& PointAddAssignPoint(POINT& point, POINT delta);
POINT& PointSubtractAssignPoint(POINT& point, POINT delta);
RECT& RectConstructDefault(RECT& rect);
RECT& RectConstructLTRB(RECT& rect, LONG left, LONG top, LONG right,
    LONG bottom);
RECT& RectConstructFromRect(RECT& rect, const RECT& source);
RECT& RectConstructFromRectPtr(RECT& rect, const RECT* source);
RECT& RectConstructFromPointSize(RECT& rect, POINT point, SIZE size);
RECT& RectConstructFromPoints(RECT& rect, POINT top_left, POINT bottom_right);
LONG RectWidth(const RECT& rect);
LONG RectHeight(const RECT& rect);
void NormalizeRect(RECT& rect);
SIZE RectSize(const RECT& rect);
POINT& RectTopLeft(RECT& rect);
const POINT& RectTopLeftConst(const RECT& rect);
POINT& RectBottomRight(RECT& rect);
const POINT& RectBottomRightConst(const RECT& rect);
POINT RectCenterPoint(const RECT& rect);
void RectSwapLeftRight(RECT& rect);
void RectSwapLeftRightStatic(RECT* rect);
RECT* RectAsMutablePtr(RECT& rect);
const RECT* RectAsConstPtr(const RECT& rect);
RECT& RectSetLTRB(RECT& rect, LONG left, LONG top, LONG right, LONG bottom);
RECT& RectSetPoints(RECT& rect, POINT top_left, POINT bottom_right);
bool RectIsEmpty(const RECT& rect);
bool RectIsNull(const RECT& rect);
bool RectPtInXY(const RECT& rect, LONG x, LONG y);
bool RectPtInPoint(const RECT& rect, POINT point);
void RectSetEmpty(RECT& rect);
RECT& RectCopy(RECT& rect, const RECT& source);
bool RectEquals(const RECT& rect, const RECT& other);
bool RectEqualsOperator(const RECT& rect, const RECT& other);
bool RectNotEquals(const RECT& rect, const RECT& other);
void RectInflateXY(RECT& rect, LONG x, LONG y);
void RectInflateSize(RECT& rect, SIZE size);
void RectDeflateXY(RECT& rect, LONG x, LONG y);
void RectDeflateSize(RECT& rect, SIZE size);
void RectOffsetXY(RECT& rect, LONG x, LONG y);
void AfxAdjustRectangle(RECT& rect, POINT point);
void RectOffsetPoint(RECT& rect, POINT point);
void RectOffsetSize(RECT& rect, SIZE size);
bool RectIntersect(RECT& rect, const RECT& left, const RECT& right);
bool RectUnion(RECT& rect, const RECT& left, const RECT& right);
bool RectSubtract(RECT& rect, const RECT& left, const RECT& right);
RECT& RectAssign(RECT& rect, const RECT& source);
RECT& RectOffsetAssignSize(RECT& rect, SIZE size);
RECT& RectOffsetAssignPoint(RECT& rect, POINT point);
RECT& RectOffsetSubtractAssignSize(RECT& rect, SIZE size);
RECT& RectOffsetSubtractAssignPoint(RECT& rect, POINT point);
RECT& RectInflateAssignRect(RECT& rect, const RECT& margins);
RECT& RectDeflateAssignRect(RECT& rect, const RECT& margins);
RECT& RectIntersectAssign(RECT& rect, const RECT& other);
RECT& RectUnionAssign(RECT& rect, const RECT& other);
RECT RectOffsetPlusSize(const RECT& rect, SIZE size);
RECT RectOffsetMinusSize(const RECT& rect, SIZE size);
RECT RectOffsetPlusPoint(const RECT& rect, POINT point);
RECT RectOffsetMinusPoint(const RECT& rect, POINT point);
RECT RectInflatedByRect(const RECT& rect, const RECT& margins);
RECT RectDeflatedByRect(const RECT& rect, const RECT& margins);
RECT RectIntersectionValue(const RECT& left, const RECT& right);
RECT RectUnionValue(const RECT& left, const RECT& right);
MfcArchiveCompat& ArchiveWriteSizeInline(MfcArchiveCompat& archive,
    const SIZE& size);
MfcArchiveCompat& ArchiveWritePointInline(MfcArchiveCompat& archive,
    const POINT& point);
MfcArchiveCompat& ArchiveWriteRectInline(MfcArchiveCompat& archive,
    const RECT& rect);
MfcArchiveCompat& ArchiveReadSizeInline(MfcArchiveCompat& archive, SIZE& size);
MfcArchiveCompat& ArchiveReadPointInline(MfcArchiveCompat& archive,
    POINT& point);
MfcArchiveCompat& ArchiveReadRectInline(MfcArchiveCompat& archive, RECT& rect);

MfcRuntimeClassCompat* GetCWinThreadRuntimeClass();
unsigned __stdcall WinThreadStartThunk(void* context);
unsigned WinThreadStartEpilogue(MfcWinThreadCompat& thread);
MfcWinThreadCompat* AfxGetThreadCompat();
HINSTANCE AfxGetInstanceHandleCompat();
HINSTANCE AfxGetResourceHandleCompat();
void AfxSetResourceHandleCompat(HINSTANCE instance);
const char* AfxGetAppNameCompat();
MfcOccManagerCompat* AfxGetOccManagerCompat();
MfcOccManagerCompat* AfxSetOccManagerCompat(MfcOccManagerCompat* manager);
MfcCWndCompat* WinThreadGetMainWndInline(MfcWinThreadCompat& thread);
MfcRuntimeClassCompat* GetWinAppRuntimeClass();
void DestroyWinThreadCompat(MfcWinThreadCompat& thread);
MfcWinAppCompat& ConstructWinApp(MfcWinAppCompat& app, const char* app_name = nullptr);
void DestroyWinApp(MfcWinAppCompat& app);
int WinAppRun(MfcWinAppCompat& app);
void WinAppSendIdleUpdate(MfcWinAppCompat& app, WPARAM wparam, LPARAM lparam);
bool WinAppOnIdle(MfcWinAppCompat& app, long count);
void WinAppUpdatePrinterSelection(MfcWinAppCompat& app, const char* printer_name);
void WinAppAssertValid(MfcWinAppCompat& app);
void WinAppDump(const MfcWinAppCompat& app);
MfcWinAppCompat* AfxGetAppCompat();
void WinAppDoWaitCursor(MfcWinAppCompat& app, int code);
bool WinAppSaveAllModified(MfcWinAppCompat& app);
void WinAppSetAppId(MfcWinAppCompat& app, const char* app_id);
void WinAppAddDocTemplate(MfcWinAppCompat& app, MfcDocTemplateCompat* templ);
void WinAppRemoveDocTemplate(MfcWinAppCompat& app, MfcDocTemplateCompat* templ);
void WinAppRouteHelpCommand(MfcWinAppCompat& app, void (*fallback)(void*),
    void* context);
bool WinAppPromptFileName(MfcWinAppCompat& app, MfcCStringCompat& path,
    UINT title_id, DWORD flags, bool open_dialog,
    MfcDocTemplateCompat* templ);
bool WinAppOpenRecentFile(MfcWinAppCompat& app, UINT command_id);
bool WinAppWriteProfileInt(MfcWinAppCompat& app, const char* section,
    const char* entry, int value);
bool WinAppWriteProfileString(MfcWinAppCompat& app, const char* section,
    const char* entry, const char* value);
bool WinAppWriteProfileBinary(MfcWinAppCompat& app, const char* section,
    const char* entry, const BYTE* data, UINT bytes);
MfcCWndCompat* AfxGetMainWndCompat();
HWND AfxGetMainWndHandleCompat();
void AfxEnableModelessCompat(bool enable);
int WinAppDoMessageBox(MfcWinAppCompat* app, const char* prompt,
    UINT type, UINT id_prompt);
void AfxOleOnReleaseAllObjects();
bool AfxOleCanExitApp();
void AfxOleLockApp();
void AfxOleUnlockApp();
void AfxOleSetUserCtrl(bool user_control);
bool AfxOleGetUserCtrl();
const char* AfxGetScodeString(SCODE scode);
const char* AfxGetScodeRangeString(SCODE scode);
const char* AfxGetFacilityString(SCODE scode);
DVTARGETDEVICE* _AfxOleCopyTargetDevice(const DVTARGETDEVICE* source);
HRESULT _AfxOleDoTreatAsClass(const char* user_type, REFCLSID old_class,
    REFCLSID new_class);
DVTARGETDEVICE* AfxOleCreateTargetDevice(HGLOBAL devnames_handle,
    HGLOBAL devmode_handle);
void _AfxDeleteMetafilePict(void* metafile_pict);
void _AfxXformSizeInPixelsToHimetric(HDC dc, const SIZE* pixels,
    SIZE* himetric);
void _AfxXformSizeInHimetricToPixels(HDC dc, const SIZE* himetric,
    SIZE* pixels);
int AfxMessageBoxCompat(const char* prompt, UINT type, UINT id_help);
int AfxMessageBoxResource(UINT id_prompt, UINT type, UINT id_help);
HWND AfxGetSafeOwnerCompat(HWND parent, HWND* disabled_owner);
MfcWinThreadCompat* AfxBeginThreadProc(unsigned (__stdcall *thread_proc)(void*),
    void* params, int priority, unsigned stack_size, unsigned create_flags,
    LPSECURITY_ATTRIBUTES security_attributes);
MfcWinThreadCompat* AfxBeginThreadRuntimeClass(MfcRuntimeClassCompat& runtime_class,
    int priority, unsigned stack_size, unsigned create_flags,
    LPSECURITY_ATTRIBUTES security_attributes);
void AfxEndThreadCompat(unsigned exit_code, bool delete_thread,
    MfcWinThreadCompat* current_thread = nullptr);
void AfxWinInitThread(MfcWinThreadCompat& thread);
void AfxTermThread(HINSTANCE instance);
void AfxTermThreadEpilogue();
bool CreateWinThreadHandle(MfcWinThreadCompat& thread, unsigned create_flags,
    unsigned stack_size, LPSECURITY_ATTRIBUTES security_attributes);
int WinThreadInitInstance(MfcWinThreadCompat& thread);
int WinThreadRun(MfcWinThreadCompat& thread);
bool WinThreadIsIdleMessage(MfcWinThreadCompat& thread, const MSG& message);
unsigned WinThreadExitInstance(MfcWinThreadCompat& thread);
bool WinThreadOnIdle(MfcWinThreadCompat& thread, long count);
bool AfxPreTranslateMessageThunk(MSG& msg);
bool AfxInternalPreTranslateMessage(MSG& msg);
bool WinThreadPreTranslateMessage(MfcWinThreadCompat& thread, MSG& msg);
bool WinThreadProcessMessageFilter(MfcWinThreadCompat& thread, int code, MSG& msg);
int WinThreadProcessWndProcException(MfcWinThreadCompat& thread, void* exception,
    MSG* message);
LRESULT CALLBACK MfcKeyboardHookProc(int code, WPARAM wparam, LPARAM lparam);
bool IsHelpKeyMessage(const MSG& msg);
bool WinThreadProcessMessageFilterWithHelp(MfcWinThreadCompat& thread,
    int code, MSG* msg);
bool WinThreadPumpMessage(MfcWinThreadCompat& thread);
void DetachThreadState(void* state);
void WinThreadDump(const MfcWinThreadCompat& thread);
bool IsLeftButtonUpMessage(const MSG& msg);
MfcRuntimeClassCompat* GetCWndRuntimeClass();
void InitializeDragListMessageThunk();
UINT RegisterDragListMessageGlobal();
void InitializeCWndWndTop();
void ConstructCWndWndTop();
void RegisterCWndWndTopCleanup();
void CleanupCWndWndTop();
void InitializeCWndWndBottom();
void ConstructCWndWndBottom();
void RegisterCWndWndBottomCleanup();
void CleanupCWndWndBottom();
void InitializeCWndWndTopMost();
void ConstructCWndWndTopMost();
void RegisterCWndWndTopMostCleanup();
void CleanupCWndWndTopMost();
void InitializeCWndWndNoTopMost();
void ConstructCWndWndNoTopMost();
void RegisterCWndWndNoTopMostCleanup();
void CleanupCWndWndNoTopMost();
MfcCWndCompat& ConstructCWnd(MfcCWndCompat& window);
MfcCWndCompat& ConstructCWndFromHandle(MfcCWndCompat& window, HWND handle);
void DestroyCWndCompat(MfcCWndCompat& window);
bool ModifyWindowLongStyle(HWND window, int index, DWORD remove_bits,
    DWORD add_bits, UINT flags);
void CaptureInitDialogState(MfcCWndCompat& window, RECT& rect, DWORD& style);
void ApplyInitDialogState(MfcCWndCompat& window, const RECT& old_rect,
    DWORD old_style);
void NotifyTopLevelActivation(MfcCWndCompat& window, WPARAM state,
    MfcCWndCompat* active_window);
LRESULT AfxCallWndProc(MfcCWndCompat& window, HWND hwnd, UINT message,
    WPARAM wparam, LPARAM lparam);
LRESULT AfxCallWndProcEpilogue(LRESULT result);
MfcWindowHandleMapCompat* GetWindowHandleMap(bool create);
MfcCWndCompat* CWndFromHandle(HWND handle);
MfcCWndCompat* CWndFromHandlePermanent(HWND handle);
bool AttachCWndHandle(MfcCWndCompat& window, HWND handle);
HWND DetachCWndHandle(MfcCWndCompat& window);
void CWndPreSubclassWindowDefault(MfcCWndCompat& window);
LRESULT CALLBACK AfxWndProc(HWND hwnd, UINT message, WPARAM wparam,
    LPARAM lparam);
WNDPROC GetAfxWndProc();
LRESULT CALLBACK AfxWndProcBase(HWND hwnd, UINT message, WPARAM wparam,
    LPARAM lparam);
LRESULT AfxWndProcBaseEpilogue(LRESULT result);
LRESULT CALLBACK AfxWndProcWithControlSite(HWND hwnd, UINT message,
    WPARAM wparam, LPARAM lparam);
LRESULT CALLBACK AfxCbtFilterHook(int code, WPARAM wparam, LPARAM lparam);
void AfxHookWindowCreate(MfcCWndCompat& window);
bool AfxUnhookWindowCreate();
bool CreateWindowExFromRect(MfcCWndCompat& window, DWORD ex_style,
    const char* class_name, const char* window_name, DWORD style,
    const RECT& rect, HWND parent, HMENU menu, void* param);
bool CreateWindowExCompat(MfcCWndCompat& window, DWORD ex_style,
    const char* class_name, const char* window_name, DWORD style,
    int x, int y, int width, int height, HWND parent, HMENU menu, void* param);
bool EnsureAfxWindowClass(MfcCWndCompat& window);
bool CreateAfxRegisteredWindow(MfcCWndCompat& window, const char* class_name,
    const char* window_name, DWORD style, const RECT& rect, HWND parent,
    HMENU menu, void* param);
void CWndDefaultAndReleaseControlSite(MfcCWndCompat& window);
void CWndOnNcDestroy(MfcCWndCompat& window);
void CWndPostNcDestroyDefault(MfcCWndCompat& window);
void CWndAssertValid(const MfcCWndCompat& window);
void CWndDump(const MfcCWndCompat& window);
bool CWndDestroyWindow(MfcCWndCompat& window);
WNDPROC* CWndGetSuperWndProcAddr(MfcCWndCompat& window);
void CWndOnCancelMode(MfcCWndCompat& window);
UINT CWndOnToolHitTest(MfcCWndCompat& window, POINT point, TOOLINFOA* tool_info);
std::string CWndGetWindowTextCompat(const MfcCWndCompat& window);
std::string CWndGetDlgItemTextCompat(const MfcCWndCompat& window, int control_id);
bool CWndGetWindowPlacementCompat(const MfcCWndCompat& window,
    WINDOWPLACEMENT& placement);
bool CWndSetWindowPlacementCompat(const MfcCWndCompat& window,
    WINDOWPLACEMENT& placement);
void CWndOnDrawItem(MfcCWndCompat& window, int control_id,
    DRAWITEMSTRUCT* draw_item);
BOOL CWndTrackPopupMenuCompat(HMENU menu, UINT flags, int x, int y,
    HWND owner, const RECT* rect);
void CWndOnMeasureItem(MfcCWndCompat& window, int control_id,
    MEASUREITEMSTRUCT* measure_item);
bool AfxRegisterClassCompat(WNDCLASSA& window_class);
bool AfxRegisterClassEpilogue();
const char* AfxRegisterWndClassCompat(UINT class_style, HCURSOR cursor,
    HBRUSH brush, HICON icon);
void CWndOnCtlColorCompat(MfcCWndCompat& window, HDC dc, HWND child);
void CWndWinHelp(MfcCWndCompat& window, ULONG_PTR data, UINT command,
    const char* help_file = nullptr);
const MfcMessageMapEntryCompat* GetCWndMessageMap();
const MfcMessageMapEntryCompat* FindMessageMapEntry(
    const MfcMessageMapEntryCompat* entries, UINT message, UINT code, UINT id);
void ResetMessageMapCache();
bool CWndOnWndMsg(MfcCWndCompat& window, UINT message, WPARAM wparam,
    LPARAM lparam, LRESULT* result);
void TestCmdUISetCheckNoop();
void TestCmdUISetRadioNoop();
void TestCmdUISetTextNoop();
bool CWndOnCommand(MfcCWndCompat& window, WPARAM wparam, HWND control);
bool CWndOnNotify(MfcCWndCompat& window, WPARAM control_id, NMHDR* notify,
    LRESULT* result);
HWND GetWindowOwnerOrParent(HWND window);
MfcCWndCompat* CWndGetTopLevelWindow(MfcCWndCompat& window);
bool CWndIsTopParentActive(MfcCWndCompat& window);
void AfxDoForAllClasses(void (*callback)(const MfcRuntimeClassCompat*, void*),
    void* context);
int AfxHandleSetCursor(MfcCWndCompat* window, UINT hit_test, UINT message);
MfcMenuCompat* AfxFindPopupMenuFromID(MfcMenuCompat* menu, UINT command_id);
MfcCWndCompat* GetTopLevelParent(MfcCWndCompat& window);
MfcCWndCompat* GetTopLevelOwner(MfcCWndCompat& window);
void ActivateTopParent(MfcCWndCompat& window);
MfcFrameWndCompat* GetTopLevelFrame(MfcCWndCompat& window);
int CWndSetScrollPosCompat(MfcCWndCompat& window, int bar, int position,
    BOOL redraw);
void CWndGetScrollRangeCompat(MfcCWndCompat& window, int bar,
    int* min_position, int* max_position);
void EnableScrollBarCtrl(MfcCWndCompat& window, int bar, BOOL enable);
int CWndMessageBox(MfcCWndCompat& window, const char* text,
    const char* caption, UINT type);
bool CWndOnHelpInfoDefault();
bool CWndSetScrollInfoCompat(MfcCWndCompat& window, int bar,
    SCROLLINFO& info, BOOL redraw);
BOOL CWndGetScrollInfoCompat(MfcCWndCompat& window, int bar,
    SCROLLINFO& info, UINT mask);
int CWndGetScrollLimitCompat(MfcCWndCompat& window, int bar);
void CWndScrollWindowCompat(MfcCWndCompat& window, int dx, int dy,
    const RECT* scroll_rect, const RECT* clip_rect);
void CWndRepositionBars(MfcCWndCompat& window, UINT id_first, UINT id_last,
    UINT id_leftover, int repos_query, RECT* rect_param, const RECT* client_rect,
    bool stretch);
void DeferMoveWindow(HDWP* hdwp, HWND window, const RECT& rect);
void CWndCalcWindowRect(MfcCWndCompat& window, RECT& rect, bool include_menu);
bool CWndOnSysCommand(MfcCWndCompat& window, UINT command, LPARAM lparam);
bool AfxPreTranslateMessageFromWindow(HWND stop, MSG& msg);
bool CWndSendChildNotifyLastMsgByHandle(HWND window, LRESULT* result);
bool CWndReflectChildNotify(UINT message, WPARAM wparam, LPARAM lparam,
    LRESULT* result);
void CWndOnParentNotify(MfcCWndCompat& window, UINT event, HWND child);
bool CWndOnEnable(MfcCWndCompat& window, BOOL enabled);
void CWndOnShowWindow(MfcCWndCompat& window);
void CWndOnActivateApp(MfcCWndCompat& window);
void CWndOnSetFocus(MfcCWndCompat& window);
void CWndOnKillFocus(MfcCWndCompat& window, HWND focus);
LRESULT CWndOnHelpCommand(MfcCWndCompat& window);
void CWndOnActivate(MfcCWndCompat& window);
LRESULT CWndSendChildNotifyOrDefault(MfcCWndCompat& window, HWND child);
LRESULT CWndForwardChildNotifyOrDefault(MfcCWndCompat& window,
    MfcCWndCompat* child);
LRESULT CWndOnCtlColor(MfcCWndCompat& window, HDC dc, HWND child, UINT type);
bool ApplyCtlColorBrushColors(HDC dc, HBRUSH brush, COLORREF text_color);
UINT CWndGetDlgCtrlIDDefault();
bool CWndUpdateData(MfcCWndCompat& window, bool save_and_validate);
bool CWndUpdateDataEpilogue(bool result);
void CWndCenterWindow(MfcCWndCompat& window, HWND alternate_owner);
bool CWndOnInitDialogDefault(MfcCWndCompat& window);
bool ExecuteDlgInitResource(MfcCWndCompat& window, const char* resource_name);
bool ExecuteDlgInitStream(MfcCWndCompat& window, const void* resource_data);
void CWndUpdateDialogControls(MfcCWndCompat& window, MfcCWndCompat* target,
    bool disable_if_no_handler);
bool CWndPreTranslateInput(MfcCWndCompat& window, MSG& message);
int CWndRunModalLoop(MfcCWndCompat& window, unsigned flags);
bool CWndContinueModal(const MfcCWndCompat& window);
void CWndEndModalLoop(MfcCWndCompat& window, int result);
bool CWndUnsupportedModalOperation();
bool AfxRegisterIconClass(WNDCLASSA& window_class, const char* class_name,
    UINT class_style, UINT icon_id);
unsigned AfxInitCommonControlsClass(INITCOMMONCONTROLSEX& init,
    unsigned requested);
bool AfxDeferRegisterClass(unsigned flags);
bool CFrameWndDefaultFalse();
bool CFrameWndDefaultTrue();
void InitializeFrameWndRectDefaultThunk();
void InitializeFrameWndRectDefault();
void InitializeMouseWheelMessageThunk();
UINT RegisterMouseWheelMessageCompat();
MfcRuntimeClassCompat* GetFrameWndRuntimeClass();
MfcFrameWndCompat& ConstructFrameWnd(MfcFrameWndCompat& frame);
void DestroyFrameWnd(MfcFrameWndCompat& frame);
MfcFrameWndCompat* DeleteFrameWndScalarDtor(MfcFrameWndCompat* frame,
    unsigned flags);
void FrameWndAddFrameWnd(MfcFrameWndCompat& frame);
void FrameWndRemoveFrameWnd(MfcFrameWndCompat& frame);
bool FrameWndLoadAccelTable(MfcFrameWndCompat& frame, const char* resource_name);
HACCEL FrameWndGetDefaultAccelerator(MfcFrameWndCompat& frame);
bool FrameWndPreTranslateMessage(MfcFrameWndCompat& frame, MSG& message);
void FrameWndPostNcDestroy(MfcFrameWndCompat* frame);
void FrameWndOnPaletteChanged(MfcFrameWndCompat& frame, HWND changed_window);
bool FrameWndOnQueryNewPalette(MfcFrameWndCompat& frame);
void FrameWndCancelMode(MfcFrameWndCompat& frame);
LRESULT OnHelpHitTest(MfcFrameWndCompat& frame, UINT id, LPARAM point);
bool FrameWndOnCommand(MfcFrameWndCompat& frame, WPARAM wparam, LPARAM lparam);
bool FrameWndIsDescendant(HWND ancestor, HWND child);
void FrameWndBeginModalState(MfcFrameWndCompat& frame);
void FrameWndEndModalState(MfcFrameWndCompat& frame);
void FrameWndShowOwnedWindows(MfcFrameWndCompat& frame, bool show);
void FrameWndOnEnable(MfcFrameWndCompat& frame, BOOL enabled);
void FrameWndNotifyFloatingWindows(MfcFrameWndCompat& frame, DWORD flags);
bool FrameWndPreCreateWindow(MfcFrameWndCompat& frame, CREATESTRUCTA& create);
bool FrameWndCreateEx(MfcFrameWndCompat& frame, DWORD ex_style,
    const char* class_name, const char* title, DWORD style,
    const RECT& rect, HWND parent, const char* menu_resource, void* param);
MfcViewCompat* FrameWndCreateView(MfcFrameWndCompat& frame,
    MfcRuntimeClassCompat* view_class, UINT child_id,
    MfcCreateContextCompat* context);
bool FrameWndOnCreateClient(MfcFrameWndCompat& frame, CREATESTRUCTA* create,
    MfcCreateContextCompat* context);
int FrameWndOnCreate(MfcFrameWndCompat& frame, CREATESTRUCTA* create,
    MfcCreateContextCompat* context);
const char* FrameWndLoadFrameIconClass(MfcFrameWndCompat& frame,
    DWORD class_style, UINT resource_id);
bool FrameWndLoadFrame(MfcFrameWndCompat& frame, UINT resource_id,
    DWORD style, HWND parent, MfcCreateContextCompat* context);
void FrameWndOnUpdateFrameMenu(MfcFrameWndCompat& frame, HMENU menu);
void FrameWndInitialUpdateFrame(MfcFrameWndCompat& frame, void* document,
    bool make_visible);
void FrameWndOnClose(MfcFrameWndCompat& frame);
void FrameWndOnDestroy(MfcFrameWndCompat& frame);
void FrameWndRemoveControlBar(MfcFrameWndCompat& frame, void* bar);
bool FrameWndOnCmdMsg(MfcFrameWndCompat& frame, UINT id, int code,
    void* extra, MfcCommandHandlerInfoCompat* handler_info);
void FrameWndOnHScroll(MfcFrameWndCompat& frame, UINT code, UINT position,
    HWND scroll_bar);
void FrameWndOnVScroll(MfcFrameWndCompat& frame, UINT code, UINT position,
    HWND scroll_bar);
void FrameWndOnActivateApp(MfcFrameWndCompat& frame, BOOL active,
    DWORD thread_id);
void FrameWndOnActivate(MfcFrameWndCompat& frame, UINT state,
    HWND other_window, BOOL minimized);
void FrameWndOnActivateFrame(MfcFrameWndCompat& frame, bool active);
void FrameWndOnSysCommand(MfcFrameWndCompat& frame, UINT command,
    LPARAM lparam);
void FrameWndOnDropFiles(MfcFrameWndCompat& frame, HDROP drop);
bool FrameWndOnQueryEndSession(MfcFrameWndCompat& frame);
void FrameWndOnEndSession(MfcFrameWndCompat& frame, BOOL ending);
LRESULT FrameWndOnDDEInitiate(MfcFrameWndCompat& frame, HWND client,
    LPARAM atoms);
LRESULT FrameWndOnDDEExecute(MfcFrameWndCompat& frame, HWND client,
    LPARAM dde_lparam);
LRESULT FrameWndOnDDETerminate(MfcFrameWndCompat& frame, HWND client,
    LPARAM dde_lparam);
MfcViewCompat* FrameWndGetActiveView(MfcFrameWndCompat& frame);
void FrameWndSetActiveView(MfcFrameWndCompat& frame, MfcViewCompat* view,
    bool notify);
void FrameWndOnSetFocus(MfcFrameWndCompat& frame, MfcCWndCompat* old_window);
void* FrameWndGetActiveDocument(MfcFrameWndCompat& frame);
void FrameWndShowControlBar(MfcFrameWndCompat& frame,
    MfcControlBarCompat* bar, bool show, bool delay);
void FrameWndOnWindowPosChanged(MfcFrameWndCompat& frame,
    const WINDOWPOS* position);
void FrameWndOnInitMenuPopup(MfcFrameWndCompat& frame, HMENU menu,
    UINT index, BOOL system_menu);
void FrameWndOnMenuSelect(MfcFrameWndCompat& frame, UINT item_id,
    UINT flags, HMENU menu);
std::string FrameWndLoadMessageString(UINT message_id);
LRESULT FrameWndOnSetMessageString(MfcFrameWndCompat& frame,
    UINT message_id, LPARAM text_lparam);
UINT FrameWndApplyMessageText(MfcFrameWndCompat& frame, UINT message_id,
    const char* explicit_text = nullptr);
UINT FrameWndSetMessageText(MfcFrameWndCompat& frame, UINT message_id,
    const char* explicit_text = nullptr);
std::vector<void*>& FrameWndGetControlBarList(MfcFrameWndCompat& frame);
MfcCWndCompat* FrameWndGetMessageBar(MfcFrameWndCompat& frame);
void FrameWndOnEnterIdle(MfcFrameWndCompat& frame, UINT reason,
    MfcCWndCompat* idle_window);
void FrameWndSetMessageTextRaw(MfcFrameWndCompat& frame, const char* text);
void FrameWndSetMessageTextById(MfcFrameWndCompat& frame, UINT message_id);
void FrameWndDestroyControlBars(MfcFrameWndCompat& frame);
MfcControlBarCompat* FrameWndGetControlBar(MfcFrameWndCompat& frame, UINT id);
void FrameWndOnUpdateControlBarMenu(MfcFrameWndCompat& frame,
    MfcCmdUICompat& cmd_ui);
bool FrameWndOnBarCheck(MfcFrameWndCompat& frame, UINT id);
bool FrameWndOnToolTipText(MfcFrameWndCompat& frame, UINT id, NMHDR* notify,
    LRESULT* result);
void FrameWndOnUpdateKeyIndicator(MfcCmdUICompat& cmd_ui);
void FrameWndOnUpdateContextHelp(MfcFrameWndCompat& frame,
    MfcCmdUICompat& cmd_ui);
void FrameWndOnUpdateFrameTitle(MfcFrameWndCompat& frame, bool add_to_title);
void FrameWndUpdateFrameTitleForDocument(MfcFrameWndCompat& frame,
    const char* document_title);
void FrameWndOnSetPreviewMode(MfcFrameWndCompat& frame, bool preview,
    MfcPreviewStateCompat& state);
void FrameWndDelayUpdateFrameTitle(MfcFrameWndCompat& frame,
    bool add_to_title);
void FrameWndOnIdleUpdateCmdUI(MfcFrameWndCompat& frame);
MfcFrameWndCompat* FrameWndGetParentFrameSelf(MfcFrameWndCompat& frame);
void FrameWndRecalcLayout(MfcFrameWndCompat& frame, bool notify);
bool FrameWndNegotiateBorderSpace(MfcFrameWndCompat& frame, UINT border_cmd,
    RECT* rect);
void FrameWndOnSize(MfcFrameWndCompat& frame, UINT type, int cx, int cy);
bool FrameWndOnEraseBkgnd(MfcFrameWndCompat& frame, HDC dc);
LRESULT FrameWndOnRegisteredMouseWheel(MfcFrameWndCompat& frame, int delta,
    LPARAM point_lparam);
bool CWndEnableToolTips(MfcCWndCompat& window, int flags);
bool CWndFilterToolTipMessage(MfcCWndCompat& window, MSG& message);
void ToolTipFilterRelayMessage(MfcToolTipCtrlCompat& control,
    const MSG& message);
void CWndDragAcceptFiles(MfcCWndCompat& window, BOOL accept);
bool CWndSubclassWindow(MfcCWndCompat& window, HWND handle);
bool CWndSubclassDlgItem(MfcCWndCompat& window, UINT control_id,
    MfcCWndCompat& parent);
HWND CWndUnsubclassWindow(MfcCWndCompat& window);
MfcCWndCompat* DeleteCWndScalarDtor(MfcCWndCompat* window, unsigned flags);
MfcMenuHandleMapCompat* GetMenuHandleMap(bool create);
MfcMenuCompat* CMenuFromHandle(HMENU menu);
MfcMenuCompat* CMenuFromHandlePermanent(HMENU menu);
void CMenuAssertValid(const MfcMenuCompat& menu);
bool AttachMenuHandle(MfcMenuCompat& menu, HMENU handle);
MfcMenuCompat& ConstructMenuCompat(MfcMenuCompat& menu);
void DestroyMenuCompat(MfcMenuCompat& menu);
MfcMenuCompat* DeleteMenuScalarDtor(MfcMenuCompat* menu, unsigned flags);
HMENU MenuGetSafeHandle(const MfcMenuCompat* menu);
HMENU MenuGetSafeHandleInline(const MfcMenuCompat* menu);
bool MenuEquals(const MfcMenuCompat& menu, const MfcMenuCompat* other);
bool MenuEqualsInline(const MfcMenuCompat& menu, const MfcMenuCompat* other);
bool MenuNotEquals(const MfcMenuCompat& menu, const MfcMenuCompat* other);
bool MenuNotEqualsInline(const MfcMenuCompat& menu,
    const MfcMenuCompat* other);
HMENU MenuOperatorHandleInline(const MfcMenuCompat& menu);
bool MenuCreate(MfcMenuCompat& menu);
bool MenuCreateInline(MfcMenuCompat& menu);
bool MenuCreatePopup(MfcMenuCompat& menu);
bool MenuCreatePopupInline(MfcMenuCompat& menu);
bool MenuLoad(MfcMenuCompat& menu, LPCSTR resource_name);
bool MenuLoadInline(MfcMenuCompat& menu, LPCSTR resource_name);
bool MenuLoadResourceId(MfcMenuCompat& menu, UINT resource_id);
bool MenuLoadResourceIdInline(MfcMenuCompat& menu, UINT resource_id);
bool MenuLoadIndirect(MfcMenuCompat& menu, const MENUTEMPLATEA* templ);
bool MenuLoadIndirectInline(MfcMenuCompat& menu, const MENUTEMPLATEA* templ);
BOOL MenuSetContextHelpId(MfcMenuCompat& menu, DWORD context_id);
DWORD MenuGetContextHelpId(const MfcMenuCompat& menu);
DWORD MenuGetContextHelpIdInline(const MfcMenuCompat& menu);
BOOL MenuCheckRadioItem(MfcMenuCompat& menu, UINT first, UINT last,
    UINT check, UINT flags);
BOOL MenuDelete(MfcMenuCompat& menu, UINT position, UINT flags);
BOOL MenuDeleteInline(MfcMenuCompat& menu, UINT position, UINT flags);
BOOL MenuAppend(MfcMenuCompat& menu, UINT flags, UINT_PTR id,
    LPCSTR item);
BOOL MenuAppendInline(MfcMenuCompat& menu, UINT flags, UINT_PTR id,
    LPCSTR item);
BOOL MenuAppendString(MfcMenuCompat& menu, UINT flags, UINT_PTR id,
    const MfcCStringCompat& item);
BOOL MenuAppendBitmap(MfcMenuCompat& menu, UINT flags, UINT_PTR id,
    HBITMAP bitmap);
BOOL MenuAppendBitmapInline(MfcMenuCompat& menu, UINT flags, UINT_PTR id,
    const MfcGdiObjectCompat* bitmap);
UINT MenuCheckItem(MfcMenuCompat& menu, UINT id_check_item, UINT check);
UINT MenuCheckItemInline(MfcMenuCompat& menu, UINT id_check_item,
    UINT check);
UINT MenuEnableItem(MfcMenuCompat& menu, UINT id_enable_item, UINT enable);
UINT MenuEnableItemInline(MfcMenuCompat& menu, UINT id_enable_item,
    UINT enable);
BOOL MenuSetDefaultItem(MfcMenuCompat& menu, UINT item, UINT by_position);
BOOL MenuSetDefaultItemInline(MfcMenuCompat& menu, UINT item,
    UINT by_position);
UINT MenuGetDefaultItem(MfcMenuCompat& menu, UINT gmdi_flags,
    BOOL by_position);
UINT MenuGetDefaultItemInline(MfcMenuCompat& menu, UINT gmdi_flags,
    BOOL by_position);
int MenuGetItemCount(const MfcMenuCompat& menu);
int MenuGetItemCountInline(const MfcMenuCompat& menu);
UINT MenuGetItemID(const MfcMenuCompat& menu, int position);
UINT MenuGetItemIDInline(const MfcMenuCompat& menu, int position);
UINT MenuGetState(const MfcMenuCompat& menu, UINT id, UINT flags);
UINT MenuGetStateInline(const MfcMenuCompat& menu, UINT id, UINT flags);
int MenuGetString(const MfcMenuCompat& menu, UINT id, char* buffer,
    int max_count, UINT flags);
int MenuGetStringInline(const MfcMenuCompat& menu, UINT id, char* buffer,
    int max_count, UINT flags);
std::string MenuGetStringValue(const MfcMenuCompat& menu, UINT id,
    UINT flags);
int MenuGetStringCStringInline(const MfcMenuCompat& menu, UINT id,
    MfcCStringCompat& value, UINT flags);
BOOL MenuGetItemInfo(const MfcMenuCompat& menu, UINT item, BOOL by_position,
    MENUITEMINFOA& info);
BOOL MenuGetItemInfoInline(const MfcMenuCompat& menu, UINT item,
    MENUITEMINFOA& info, BOOL by_position);
MfcMenuCompat* MenuGetSubMenu(const MfcMenuCompat& menu, int position);
MfcMenuCompat* MenuGetSubMenuInline(const MfcMenuCompat& menu, int position);
BOOL MenuInsert(MfcMenuCompat& menu, UINT position, UINT flags,
    UINT_PTR id, LPCSTR item);
BOOL MenuInsertInline(MfcMenuCompat& menu, UINT position, UINT flags,
    UINT_PTR id, LPCSTR item);
BOOL MenuInsertString(MfcMenuCompat& menu, UINT position, UINT flags,
    UINT_PTR id, const MfcCStringCompat& item);
BOOL MenuInsertBitmap(MfcMenuCompat& menu, UINT position, UINT flags,
    UINT_PTR id, HBITMAP bitmap);
BOOL MenuInsertBitmapInline(MfcMenuCompat& menu, UINT position, UINT flags,
    UINT_PTR id, const MfcGdiObjectCompat* bitmap);
BOOL MenuModify(MfcMenuCompat& menu, UINT position, UINT flags,
    UINT_PTR id, LPCSTR item);
BOOL MenuModifyInline(MfcMenuCompat& menu, UINT position, UINT flags,
    UINT_PTR id, LPCSTR item);
BOOL MenuModifyString(MfcMenuCompat& menu, UINT position, UINT flags,
    UINT_PTR id, const MfcCStringCompat& item);
BOOL MenuModifyBitmap(MfcMenuCompat& menu, UINT position, UINT flags,
    UINT_PTR id, HBITMAP bitmap);
BOOL MenuModifyBitmapInline(MfcMenuCompat& menu, UINT position, UINT flags,
    UINT_PTR id, const MfcGdiObjectCompat* bitmap);
BOOL MenuRemove(MfcMenuCompat& menu, UINT position, UINT flags);
BOOL MenuRemoveInline(MfcMenuCompat& menu, UINT position, UINT flags);
BOOL MenuSetItemBitmaps(MfcMenuCompat& menu, UINT position, UINT flags,
    HBITMAP unchecked_bitmap, HBITMAP checked_bitmap);
BOOL MenuSetItemBitmapsInline(MfcMenuCompat& menu, UINT position, UINT flags,
    const MfcGdiObjectCompat* unchecked_bitmap,
    const MfcGdiObjectCompat* checked_bitmap);
void CMenuDrawItemDefault();
void CMenuMeasureItemDefault();
void AfxLockTempMapsCompat();
bool AfxUnlockTempMaps(bool delete_temporary);
MfcHandleMapCompat& ConstructHandleMap(MfcHandleMapCompat& map,
    int handle_offset, MfcRuntimeClassCompat* runtime_class, int handle_count);
void* HandleMapFromHandle(MfcHandleMapCompat& map, void* handle);
void* HandleMapFromHandleEpilogue(void* object, void* handle,
    int handle_offset, int handle_count);
void HandleMapRemoveHandle(MfcHandleMapCompat& map, void* handle);
void DeleteTempMap(MfcHandleMapCompat& map);
void DeleteTempHwndMap();
void DeleteTempImageListMap();
void DeleteTempDcMap();
void DeleteTempGdiObjectMap();
void DeleteTempMenuMap();
void CWndOleControlSiteScroll(MfcCWndCompat& window, int dx, int dy,
    const RECT* scroll_rect, const RECT* clip_rect);
void CWndCheckDlgButton(MfcCWndCompat& window, int control_id, UINT check);
void CWndCheckRadioButton(MfcCWndCompat& window, int first_id, int last_id,
    int check_id);
MfcCWndCompat* CWndGetDlgItem(MfcCWndCompat& window, int control_id);
HWND CWndGetDlgItemHandle(MfcCWndCompat& window, int control_id);
UINT CWndGetDlgItemInt(MfcCWndCompat& window, int control_id, BOOL* translated,
    BOOL signed_value);
UINT CWndGetDlgItemText(MfcCWndCompat& window, int control_id, char* buffer,
    int max_count);
LRESULT CWndSendDlgItemMessage(MfcCWndCompat& window, int control_id,
    UINT message, WPARAM wparam, LPARAM lparam);
void CWndSetDlgItemInt(MfcCWndCompat& window, int control_id, UINT value,
    BOOL signed_value);
void CWndSetDlgItemText(MfcCWndCompat& window, int control_id, const char* text);
UINT CWndIsDlgButtonChecked(MfcCWndCompat& window, int control_id);
int CWndScrollWindowExCompat(MfcCWndCompat& window, int dx, int dy,
    const RECT* scroll_rect, const RECT* clip_rect, HRGN update_region,
    RECT* update_rect, UINT flags);
bool CWndIsDialogMessageCompat(MfcCWndCompat& window, MSG& message);
LONG CWndGetStyle(MfcCWndCompat& window);
LONG CWndGetExStyle(MfcCWndCompat& window);
bool CWndModifyStyle(MfcCWndCompat& window, DWORD remove_bits,
    DWORD add_bits, UINT flags);
bool CWndModifyStyleEx(MfcCWndCompat& window, DWORD remove_bits,
    DWORD add_bits, UINT flags);
void CWndSetWindowText(MfcCWndCompat& window, const char* text);
int CWndGetWindowText(MfcCWndCompat& window, char* buffer, int max_count);
int CWndGetWindowTextLength(MfcCWndCompat& window);
int CWndGetDlgCtrlID(MfcCWndCompat& window);
void CWndSetDlgCtrlID(MfcCWndCompat& window, LONG id);
void CWndMoveWindow(MfcCWndCompat& window, int x, int y, int width,
    int height, BOOL repaint);
void CWndSetWindowPos(MfcCWndCompat& window, HWND insert_after, int x, int y,
    int width, int height, UINT flags);
void CWndShowWindow(MfcCWndCompat& window, int command);
BOOL CWndIsWindowEnabled(MfcCWndCompat& window);
BOOL CWndEnableWindow(MfcCWndCompat& window, BOOL enable);
MfcCWndCompat* CWndSetFocus(MfcCWndCompat& window);
BOOL CWndEqualsInline(const MfcCWndCompat& left,
    const MfcCWndCompat& right);
HWND CWndGetSafeHwndInline(const MfcCWndCompat* window);
BOOL CWndUpdateWindowInline(MfcCWndCompat& window);
void CWndSetRedrawInline(MfcCWndCompat& window, BOOL redraw);
BOOL CWndLockWindowUpdateInline(MfcCWndCompat& window);
BOOL CWndUnlockWindowUpdateInline(MfcCWndCompat& window);
MfcCWndCompat* CWndFindWindowInline(const char* class_name,
    const char* window_name);
MfcCWndCompat* CWndWindowFromPointInline(LONG x, LONG y);
BOOL CWndDrawMenuBarInline(MfcCWndCompat& window);
BOOL CWndIsIconicInline(MfcCWndCompat& window);
BOOL CWndIsZoomedInline(MfcCWndCompat& window);
UINT CWndArrangeIconicWindowsInline(MfcCWndCompat& window);
int CWndSetWindowRgnInline(MfcCWndCompat& window,
    const MfcGdiObjectCompat* region, BOOL redraw);
BOOL CWndBringWindowToTopInline(MfcCWndCompat& window);
BOOL CWndGetWindowRectInline(MfcCWndCompat& window, RECT& rect);
BOOL CWndGetClientRectInline(MfcCWndCompat& window, RECT& rect);
BOOL CWndClientToScreenInline(MfcCWndCompat& window, POINT& point);
BOOL CWndScreenToClientInline(MfcCWndCompat& window, POINT& point);
LRESULT CWndSendMessageInline(MfcCWndCompat& window, UINT message,
    WPARAM wparam, LPARAM lparam);
BOOL CWndPostMessageInline(MfcCWndCompat& window, UINT message,
    WPARAM wparam, LPARAM lparam);
void CWndSetFontInline(MfcCWndCompat& window,
    const MfcGdiObjectCompat* font, BOOL redraw);
MfcGdiObjectCompat* CWndGetFontInline(MfcCWndCompat& window);
MfcMenuCompat* CWndGetMenuInline(MfcCWndCompat& window);
BOOL CWndSetMenuInline(MfcCWndCompat& window, const MfcMenuCompat* menu);
MfcMenuCompat* CWndGetSystemMenuInline(MfcCWndCompat& window, BOOL revert);
BOOL CWndHiliteMenuItemInline(MfcCWndCompat& window,
    const MfcMenuCompat& menu, UINT item, UINT flags);
int CWndGetWindowRgnInline(MfcCWndCompat& window, MfcGdiObjectCompat& region);
int CWndMapWindowPointsInline(MfcCWndCompat& window, MfcCWndCompat* to,
    POINT* points, UINT count);
int CWndMapWindowRectInline(MfcCWndCompat& window, MfcCWndCompat* to,
    RECT& rect);
HDC CWndBeginPaintInline(MfcCWndCompat& window, PAINTSTRUCT& paint);
BOOL CWndEndPaintInline(MfcCWndCompat& window, const PAINTSTRUCT& paint);
HDC CWndGetDCInline(MfcCWndCompat& window);
HDC CWndGetWindowDCInline(MfcCWndCompat& window);
int CWndReleaseDCInline(MfcCWndCompat& window, HDC dc);
int CWndGetUpdateRgnInline(MfcCWndCompat& window, MfcGdiObjectCompat& region,
    BOOL erase);
BOOL CWndGetUpdateRectInline(MfcCWndCompat& window, RECT* rect, BOOL erase);
BOOL CWndInvalidateInline(MfcCWndCompat& window, BOOL erase);
BOOL CWndInvalidateRectInline(MfcCWndCompat& window, const RECT* rect,
    BOOL erase);
BOOL CWndInvalidateRgnInline(MfcCWndCompat& window,
    const MfcGdiObjectCompat* region, BOOL erase);
BOOL CWndValidateRgnInline(MfcCWndCompat& window,
    const MfcGdiObjectCompat* region);
BOOL CWndValidateRectInline(MfcCWndCompat& window, const RECT* rect);
BOOL CWndIsWindowVisibleInline(MfcCWndCompat& window);
BOOL CWndShowOwnedPopupsInline(MfcCWndCompat& window, BOOL show);
void CWndSendMessageToDescendantsInline(MfcCWndCompat& window, UINT message,
    WPARAM wparam, LPARAM lparam);
MfcCWndCompat* CWndGetDescendantWindowInline(MfcCWndCompat& window,
    int control_id, bool only_permanent);
HDC CWndGetDCExInline(MfcCWndCompat& window,
    const MfcGdiObjectCompat* clip_region, DWORD flags);
BOOL CWndRedrawWindowInline(MfcCWndCompat& window, const RECT* update_rect,
    const MfcGdiObjectCompat* update_region, UINT flags);
BOOL CWndEnableScrollBarInline(MfcCWndCompat& window, int bar, UINT arrows);
UINT_PTR CWndSetTimerInline(MfcCWndCompat& window, UINT_PTR id_event,
    UINT elapsed, TIMERPROC proc);
int CWndDlgDirListInline(MfcCWndCompat& window, char* path_spec,
    int list_box_id, int static_path_id, UINT file_type);
int CWndDlgDirListComboBoxInline(MfcCWndCompat& window, char* path_spec,
    int combo_box_id, int static_path_id, UINT file_type);
BOOL CWndDlgDirSelectInline(MfcCWndCompat& window, char* output,
    int output_count, int list_box_id);
BOOL CWndDlgDirSelectComboBoxInline(MfcCWndCompat& window, char* output,
    int output_count, int combo_box_id);
MfcCWndCompat* CWndGetNextDlgGroupItemInline(MfcCWndCompat& window,
    MfcCWndCompat* control, BOOL previous);
MfcCWndCompat* CWndGetNextDlgTabItemInline(MfcCWndCompat& window,
    MfcCWndCompat* control, BOOL previous);
BOOL CWndShowScrollBarInline(MfcCWndCompat& window, int bar, BOOL show);
BOOL CWndKillTimerInline(MfcCWndCompat& window, UINT_PTR id_event);
MfcCWndCompat* CWndSetActiveWindowInline(MfcCWndCompat& window);
MfcCWndCompat* CWndSetCaptureInline(MfcCWndCompat& window);
MfcCWndCompat* CWndChildWindowFromPointInline(MfcCWndCompat& window,
    POINT point);
MfcCWndCompat* CWndChildWindowFromPointExInline(MfcCWndCompat& window,
    POINT point, UINT flags);
MfcCWndCompat* CWndGetNextWindowInline(MfcCWndCompat& window, UINT flags);
MfcCWndCompat* CWndGetWindowInline(MfcCWndCompat& window, UINT flags);
MfcCWndCompat* CWndGetTopWindowInline(MfcCWndCompat& window);
MfcCWndCompat* CWndGetLastActivePopupInline(MfcCWndCompat& window);
MfcCWndCompat* CWndGetParentInline(MfcCWndCompat& window);
BOOL CWndFlashWindowInline(MfcCWndCompat& window, BOOL invert);
BOOL CWndChangeClipboardChainInline(MfcCWndCompat& window,
    MfcCWndCompat* next_window);
MfcCWndCompat* CWndSetClipboardViewerInline(MfcCWndCompat& window);
MfcCWndCompat* CWndGetOpenClipboardWindowInline();
MfcCWndCompat* CWndGetClipboardOwnerInline();
MfcCWndCompat* CWndGetClipboardViewerInline();
BOOL CWndOpenClipboardInline(MfcCWndCompat& window);
BOOL CWndHideCaretInline(MfcCWndCompat& window);
BOOL CWndShowCaretInline(MfcCWndCompat& window);
BOOL CWndSetForegroundWindowInline(MfcCWndCompat& window);
BOOL CWndIsChildInline(MfcCWndCompat& window, const MfcCWndCompat* child);
MfcCWndCompat* CWndSetParentInline(MfcCWndCompat& window,
    MfcCWndCompat* parent);
BOOL CWndSetCaretPosInline(int x, int y);
BOOL CWndSendNotifyMessageInline(MfcCWndCompat& window, UINT message,
    WPARAM wparam, LPARAM lparam);
HICON CWndSetIconInline(MfcCWndCompat& window, HICON icon, BOOL big_icon);
HICON CWndGetIconInline(MfcCWndCompat& window, BOOL big_icon);
void CWndPrintInline(MfcCWndCompat& window, const MfcCDCCompat* dc,
    DWORD flags);
void CWndPrintClientInline(MfcCWndCompat& window, const MfcCDCCompat* dc,
    DWORD flags);
BOOL CWndSetContextHelpIdInline(MfcCWndCompat& window, DWORD context_id);
DWORD CWndGetContextHelpIdInline(MfcCWndCompat& window);
BOOL CWndCreateBitmapCaretInline(MfcCWndCompat& window, HBITMAP bitmap);
BOOL CWndCreateSolidCaretInline(MfcCWndCompat& window, int width, int height);
BOOL CWndCreateGrayCaretInline(MfcCWndCompat& window, int width, int height);
int AfxWin2ControlInline_005e3a59(MfcCWndCompat& window, int position,
    BOOL redraw);
BOOL AfxWin2ControlInline_005e3aae(MfcCWndCompat& window, int* min_position,
    int* max_position);
BOOL AfxWin2ControlInline_005e3b03(MfcCWndCompat& window, int min_position,
    int max_position, BOOL redraw);
BOOL AfxWin2ControlInline_005e3b5c(MfcCWndCompat& window, BOOL show);
BOOL AfxWin2ControlInline_005e3bad(MfcCWndCompat& window, UINT arrows);
void CWndInvokeHelper(MfcCWndCompat& window, LONG dispatch_id, WORD flags,
    unsigned short return_type, void* return_value,
    const unsigned char* param_info, ...);
void CWndGetProperty(MfcCWndCompat& window, LONG dispatch_id,
    unsigned short value_type, void* value);
void CWndSetProperty(MfcCWndCompat& window, LONG dispatch_id,
    unsigned short value_type, ...);
IUnknown* CWndGetControlUnknown(MfcCWndCompat& window);
bool CWndGetAmbientProperty(MfcCWndCompat& window, LONG dispatch_id,
    unsigned short value_type, void* value);
void CWndSetControlSize(MfcCWndCompat& window, int width, int height);
void CWndAttachControlSiteFromParent(MfcCWndCompat* window);
void CWndAttachControlSiteToParent(MfcCWndCompat& window,
    MfcCWndCompat& parent);
void OleControlContainerAttachControlSite(MfcOleControlContainerCompat& container,
    MfcCWndCompat& window);
MfcRuntimeClassCompat* GetViewRuntimeClass();
const MfcMessageMapCompat* GetViewMessageMap();
MfcViewCompat& ConstructView(MfcViewCompat& view);
void DestroyView(MfcViewCompat& view);
MfcViewCompat* DeleteViewScalarDtor(MfcViewCompat* view, unsigned flags);
bool ViewPreCreateWindow(MfcViewCompat& view, CREATESTRUCTA& create);
int ViewOnCreate(MfcViewCompat& view, CREATESTRUCTA& create);
void ViewOnDestroy(MfcViewCompat& view);
void ViewPostNcDestroy(MfcViewCompat* view);
void ViewCalcWindowRect(MfcViewCompat& view, RECT& rect,
    bool adjust_for_client);
bool ViewOnCmdMsg(MfcViewCompat& view, UINT id, int code, void* extra,
    MfcCommandHandlerInfoCompat* handler_info);
void ViewOnPaint(MfcViewCompat& view);
void ViewOnInitialUpdate(MfcViewCompat& view);
void ViewOnUpdate(MfcViewCompat& view, MfcViewCompat* sender, LPARAM hint,
    void* hint_object);
void ViewOnPrint(MfcViewCompat& view, MfcCDCCompat& dc, void* print_info);
void ViewOnDrawDefault(MfcViewCompat& view, MfcCDCCompat& dc);
bool ViewIsSelectedDefault(const MfcViewCompat& view, const MfcObjectCompat* item);
DWORD ViewOnDragEnterDefault(MfcViewCompat& view, void* data_object,
    DWORD key_state, POINT point);
DWORD ViewOnDragOverDefault(MfcViewCompat& view, void* data_object,
    DWORD key_state, POINT point);
bool ViewOnDropDefault(MfcViewCompat& view, void* data_object,
    DWORD drop_effect, POINT point);
DWORD ViewOnDropExDefault(MfcViewCompat& view, void* data_object,
    DWORD drop_default, DWORD drop_list, POINT point);
void ViewOnDragLeaveDefault(MfcViewCompat& view);
bool ViewOnScrollDefault(MfcViewCompat& view, UINT scroll_code, UINT position,
    bool do_scroll);
bool ViewOnScrollByDefault(MfcViewCompat& view, SIZE scroll_size,
    bool do_scroll);
DWORD ViewOnDragScrollDefault(MfcViewCompat& view, DWORD key_state,
    POINT point);
void ViewOnPrepareDC(MfcViewCompat& view, MfcCDCCompat& dc, void* print_info);
void ViewOnActivateView(MfcViewCompat& view, bool active,
    MfcViewCompat* active_view, MfcViewCompat* inactive_view);
void ViewOnActivateFrameDefault(MfcViewCompat& view, UINT state, void* frame);
int ViewOnMouseActivate(MfcViewCompat& view, MfcCWndCompat* top_level,
    UINT hit_test, UINT message);
MfcCWndCompat* ViewGetParentSplitter(const MfcViewCompat& view,
    bool check_nested);
MfcCWndCompat* ViewGetSplitScrollSibling(MfcViewCompat& view, bool vertical);
void ViewOnUpdateSplitCmd(MfcViewCompat& view, MfcCmdUICompat& cmd_ui);
bool ViewSplitCommand(MfcViewCompat& view);
void ViewOnUpdateNextPaneCmd(MfcViewCompat& view, MfcCmdUICompat& cmd_ui);
bool ViewOnNextPaneCmd(MfcViewCompat& view, UINT command);
bool ViewPreparePrintingDefault(MfcViewCompat& view, void* print_info);
void ViewOnBeginPrintingDefault(MfcViewCompat& view, MfcCDCCompat& dc,
    void* print_info);
void ViewOnEndPrintingDefault(MfcViewCompat& view, MfcCDCCompat& dc,
    void* print_info);
void ViewOnEndPrintPreview(MfcViewCompat& view, MfcCDCCompat& dc,
    void* print_info, POINT point, void* preview_view);
void ViewDump(const MfcViewCompat& view);
void ViewAssertValid(const MfcViewCompat& view);
MfcRuntimeClassCompat* GetCtrlViewRuntimeClass();
const MfcMessageMapCompat* GetCtrlViewMessageMap();
MfcCtrlViewCompat& ConstructCtrlView(MfcCtrlViewCompat& view,
    const char* class_name, DWORD default_style);
void DestroyCtrlView(MfcCtrlViewCompat& view);
MfcCtrlViewCompat* DeleteCtrlViewScalarDtor(MfcCtrlViewCompat* view,
    unsigned flags);
bool CtrlViewPreCreateWindow(MfcCtrlViewCompat& view, CREATESTRUCTA& create);
void CtrlViewOnDrawAssert(MfcCtrlViewCompat& view, MfcCDCCompat& dc);
void CtrlViewOnDestroyDefault(MfcCtrlViewCompat& view);
void CtrlViewDump(const MfcCtrlViewCompat& view);
void CtrlViewAssertValid(const MfcCtrlViewCompat& view);
MfcRuntimeClassCompat* GetScrollViewRuntimeClass();
const MfcMessageMapCompat* GetScrollViewMessageMap();
UINT GetMouseWheelScrollLines();
MfcScrollViewCompat& ConstructScrollView(MfcScrollViewCompat& view);
void DestroyScrollView(MfcScrollViewCompat& view);
MfcScrollViewCompat* DeleteScrollViewScalarDtor(MfcScrollViewCompat* view,
    unsigned flags);
void ScrollViewOnPrepareDC(MfcScrollViewCompat& view, MfcCDCCompat& dc,
    void* print_info);
void ScrollViewSetScaleToFitSize(MfcScrollViewCompat& view, SIZE total_log);
void ScrollViewSetScrollSizes(MfcScrollViewCompat& view, int map_mode,
    SIZE total_log, SIZE page_size, SIZE line_size);
POINT ScrollViewGetScrollPosition(MfcScrollViewCompat& view);
void ScrollViewScrollToPosition(MfcScrollViewCompat& view, POINT point);
POINT ScrollViewGetDeviceScrollPosition(MfcScrollViewCompat& view);
void ScrollViewGetDeviceScrollSizes(const MfcScrollViewCompat& view,
    int& map_mode, SIZE& total, SIZE& page, SIZE& line);
void ScrollViewScrollToDevicePosition(MfcScrollViewCompat& view, POINT point);
void ScrollViewFillOutsideRect(MfcScrollViewCompat& view, MfcCDCCompat& dc,
    HBRUSH brush);
void ScrollViewResizeParentToFit(MfcScrollViewCompat& view, bool shrink_only);
void ScrollViewOnSize(MfcScrollViewCompat& view, UINT type, int cx, int cy);
void ScrollViewCenterOnPoint(MfcScrollViewCompat& view, POINT point);
MfcCWndCompat* ScrollViewGetScrollBarCtrl(MfcScrollViewCompat& view,
    bool vertical);
SIZE ScrollViewGetScrollBarSizes(MfcScrollViewCompat& view);
bool ScrollViewGetTrueClientSize(MfcScrollViewCompat& view, SIZE& size,
    SIZE& scrollbars);
void ScrollViewGetScrollBarState(MfcScrollViewCompat& view, SIZE inside,
    SIZE& need_bars, SIZE& ranges, POINT& position, bool inside_client);
void ScrollViewUpdateBars(MfcScrollViewCompat& view);
void ScrollViewCalcWindowRect(MfcScrollViewCompat& view, RECT& rect,
    bool adjust_for_client);
void ScrollViewOnHScroll(MfcScrollViewCompat& view, UINT code, UINT position,
    MfcCWndCompat* scroll_bar);
void ScrollViewOnVScroll(MfcScrollViewCompat& view, UINT code, UINT position,
    MfcCWndCompat* scroll_bar);
bool ScrollViewOnMouseWheel(MfcScrollViewCompat& view, UINT flags,
    short delta, POINT point);
bool ScrollViewDoMouseWheel(MfcScrollViewCompat& view, UINT flags,
    short delta, POINT point);
bool ScrollViewOnScroll(MfcScrollViewCompat& view, UINT scroll_code,
    UINT position, bool do_scroll);
bool ScrollViewOnScrollBy(MfcScrollViewCompat& view, SIZE scroll_size,
    bool do_scroll);
void ScrollViewDump(const MfcScrollViewCompat& view);
void ScrollViewAssertValid(const MfcScrollViewCompat& view);
MfcRuntimeClassCompat* GetSplitterRuntimeClass();
const MfcMessageMapCompat* GetSplitterMessageMap();
MfcSplitterWndCompat& ConstructSplitterWnd(MfcSplitterWndCompat& splitter);
void DestroySplitterWnd(MfcSplitterWndCompat& splitter);
MfcSplitterWndCompat* DeleteSplitterScalarDtor(MfcSplitterWndCompat* splitter,
    unsigned flags);
bool SplitterCreate(MfcSplitterWndCompat& splitter, MfcCWndCompat* parent,
    int max_rows, int max_cols, SIZE min_size,
    MfcCreateContextCompat* context, DWORD style, UINT id);
bool SplitterCreateStatic(MfcSplitterWndCompat& splitter, MfcCWndCompat* parent,
    int rows, int cols, DWORD style, UINT id);
bool SplitterCreateCommon(MfcSplitterWndCompat& splitter, MfcCWndCompat* parent,
    int cx_min, int cy_min, DWORD style, UINT id);
bool SplitterCreateCommonSuccessCleanup();
bool SplitterCreateView(MfcSplitterWndCompat& splitter, int row, int col,
    void* runtime_class, SIZE size, MfcCreateContextCompat* context);
bool SplitterCreateViewFinish(MfcSplitterWndCompat& splitter,
    MfcCWndCompat& pane, int row, int col, SIZE size,
    MfcCreateContextCompat* context);
bool SplitterCreateScrollBarCtrl(MfcSplitterWndCompat& splitter,
    DWORD style, UINT id);
int SplitterIdFromRowCol(const MfcSplitterWndCompat& splitter, int row, int col);
MfcCWndCompat* SplitterGetPane(MfcSplitterWndCompat& splitter, int row, int col);
bool SplitterIsChildPane(const MfcSplitterWndCompat& splitter,
    const MfcCWndCompat& pane, int* row, int* col);
bool SplitterIsChildPaneInline(const MfcSplitterWndCompat& splitter,
    const MfcCWndCompat& pane, int* row, int* col);
void SplitterGetRowInfo(const MfcSplitterWndCompat& splitter, int row,
    int& current, int& minimum);
void SplitterSetRowInfo(MfcSplitterWndCompat& splitter, int row,
    int ideal, int minimum);
void SplitterGetColumnInfo(const MfcSplitterWndCompat& splitter, int col,
    int& current, int& minimum);
void SplitterSetColumnInfo(MfcSplitterWndCompat& splitter, int col,
    int ideal, int minimum);
DWORD SplitterGetScrollStyle(const MfcSplitterWndCompat& splitter);
void SplitterSetScrollStyle(MfcSplitterWndCompat& splitter, DWORD style);
void SplitterDeleteView(MfcSplitterWndCompat& splitter, int row, int col);
void SplitterOnDrawSplitter(MfcSplitterWndCompat& splitter, MfcCDCCompat* dc,
    int split_type, const RECT& rect);
int SplitterCanSplit(const MfcSplitterPaneInfoCompat& info,
    int before_size, int splitter_size);
bool SplitterSplitRow(MfcSplitterWndCompat& splitter, int before_size);
bool SplitterSplitColumn(MfcSplitterWndCompat& splitter, int before_size);
void SplitterDeleteRow(MfcSplitterWndCompat& splitter, int row);
void SplitterDeleteColumn(MfcSplitterWndCompat& splitter, int col);
void SplitterGetInsideRect(const MfcSplitterWndCompat& splitter, RECT& rect);
void SplitterTrackRowSize(MfcSplitterWndCompat& splitter, int y, int row);
void SplitterTrackColumnSize(MfcSplitterWndCompat& splitter, int x, int col);
void SplitterGetHitRect(MfcSplitterWndCompat& splitter, int hit_test,
    RECT& rect);
int SplitterHitTest(MfcSplitterWndCompat& splitter, POINT point);
void SplitterInvertTracker(MfcSplitterWndCompat& splitter, const RECT& rect);
bool SplitterDoKeyboardSplit(MfcSplitterWndCompat& splitter);
void SplitterLayoutRowCol(std::vector<MfcSplitterPaneInfoCompat>& info,
    int count, int total_size, int splitter_gap);
void SplitterDeferClientPos(HDWP* hdwp, MfcCWndCompat& window,
    int x, int y, int cx, int cy, bool scroll_bar);
MfcCWndCompat* SplitterGetSizingParent(MfcSplitterWndCompat& splitter);
void SplitterRecalcLayout(MfcSplitterWndCompat& splitter);
void SplitterDrawAllSplitBars(MfcSplitterWndCompat& splitter,
    MfcCDCCompat& dc, int inside_left, int inside_top);
void SplitterOnPaint(MfcSplitterWndCompat& splitter);
void SplitterSetSplitCursor(int hit_test);
void SplitterOnLButtonDblClk(MfcSplitterWndCompat& splitter, UINT flags,
    POINT point);
void StartTracking(MfcSplitterWndCompat& splitter, int hit_test);
void OnMouseMove(MfcSplitterWndCompat& splitter, UINT flags, POINT point);
void SplitterStopTrackingAccept(MfcSplitterWndCompat& splitter);
void SplitterStopTrackingCancel(MfcSplitterWndCompat& splitter);
void SplitterOnKeyDown(MfcSplitterWndCompat& splitter, UINT key);
bool SplitterOnCommand(MfcSplitterWndCompat& splitter, WPARAM wparam,
    HWND control);
bool SplitterOnNotify(MfcSplitterWndCompat& splitter, WPARAM control_id,
    NMHDR* notify, LRESULT* result);
bool SplitterOnMouseWheel(MfcSplitterWndCompat& splitter, UINT flags,
    short delta, POINT point);
int SplitterGetRowCount(const MfcSplitterWndCompat& splitter);
int SplitterGetColumnCount(const MfcSplitterWndCompat& splitter);
void SplitterOnHScroll(MfcSplitterWndCompat& splitter, UINT code,
    UINT position, MfcCWndCompat* scroll_bar);
void SplitterOnVScroll(MfcSplitterWndCompat& splitter, UINT code,
    UINT position, MfcCWndCompat* scroll_bar);
bool SplitterOnScroll(MfcSplitterWndCompat& splitter, MfcCWndCompat& pane,
    UINT scroll_code, UINT position, bool do_scroll);
bool SplitterOnScrollBy(MfcSplitterWndCompat& splitter, MfcCWndCompat& pane,
    SIZE scroll_size, bool do_scroll);
bool SplitterCanActivateNext(MfcSplitterWndCompat& splitter);
void SplitterActivateNext(MfcSplitterWndCompat& splitter, bool previous);
void SplitterActivatePane(MfcSplitterWndCompat& splitter, int row, int col,
    MfcCWndCompat* pane);
MfcCWndCompat* SplitterGetActivePane(MfcSplitterWndCompat& splitter,
    int* row, int* col);
void SplitterAssertValid(MfcSplitterWndCompat& splitter);
void SplitterDump(const MfcSplitterWndCompat& splitter);
MfcRuntimeClassCompat* GetControlBarRuntimeClass();
MfcControlBarCompat& ConstructControlBar(MfcControlBarCompat& bar);
void DestroyControlBar(MfcControlBarCompat& bar);
MfcControlBarCompat* DeleteControlBarScalarDtor(MfcControlBarCompat* bar,
    unsigned flags);
void ControlBarPostNcDestroy(MfcControlBarCompat* bar);
SIZE ControlBarCalcFixedLayout(MfcControlBarCompat& bar, bool stretch,
    bool horizontal);
SIZE ControlBarCalcDynamicLayout(MfcControlBarCompat& bar, int length,
    DWORD mode);
bool ControlBarDefaultFalse();
void ControlBarStartDelayTimer(UINT timer_id, UINT milliseconds);
void ControlBarOnTimer(MfcControlBarCompat& bar, UINT timer_id);
bool ControlBarSetStatusText(MfcControlBarCompat& bar, int hit);
bool ControlBarPreTranslateMessage(MfcControlBarCompat& bar, MSG& message);
LRESULT ControlBarWindowProc(MfcControlBarCompat& bar, UINT message,
    WPARAM wparam, LPARAM lparam);
int ControlBarOnToolHitTest(MfcControlBarCompat& bar, POINT point,
    TOOLINFOA* tool_info);
void ControlBarOnWindowPosChanging(MfcControlBarCompat& bar,
    WINDOWPOS& window_pos);
int ControlBarOnCreate(MfcControlBarCompat& bar, CREATESTRUCTA& create);
void ControlBarOnDestroy(MfcControlBarCompat& bar);
bool ControlBarDestroyWindow(MfcControlBarCompat& bar);
int ControlBarOnMouseActivate(MfcControlBarCompat& bar, MfcCWndCompat* top,
    UINT hit_test, UINT message);
void ControlBarOnPaint(MfcControlBarCompat& bar);
void ControlBarEraseNonClient(MfcControlBarCompat& bar);
void ControlBarOnLButtonDown(MfcControlBarCompat& bar, UINT flags,
    POINT point);
void ControlBarOnLButtonDblClk(MfcControlBarCompat& bar, UINT flags,
    POINT point);
bool ControlBarDelayShow(MfcControlBarCompat& bar, bool show);
void ControlBarOnShowWindow(MfcControlBarCompat& bar);
DWORD ControlBarRecalcDelayShow(MfcControlBarCompat& bar, HDWP* hdwp);
LRESULT ControlBarOnSizeParent(MfcControlBarCompat& bar, UINT message,
    MfcSizeParentParamsCompat& layout);
void ControlBarSetDelayShow(MfcControlBarCompat& bar, bool show);
bool ControlBarIsVisible(MfcControlBarCompat& bar);
void ControlBarDoPaint(MfcControlBarCompat& bar, MfcCDCCompat& dc);
void ControlBarDrawBorders(MfcControlBarCompat& bar, MfcCDCCompat& dc,
    RECT& rect);
void ControlBarDrawGripper(MfcControlBarCompat& bar, MfcCDCCompat& dc,
    RECT& rect);
void ControlBarCalcInsideRect(MfcControlBarCompat& bar, RECT& rect,
    bool horizontal);
DWORD ControlBarGetDockStyle(const MfcControlBarCompat& bar);
DWORD ControlBarGetBarStyle(const MfcControlBarCompat& bar);
void ControlBarSetBordersLTRB(MfcControlBarCompat& bar, int left, int top,
    int right, int bottom);
void ControlBarAssertValid(MfcControlBarCompat& bar);
MfcDockContextCompat& ConstructDockContext(MfcDockContextCompat& context,
    MfcControlBarCompat& bar);
void DestroyDockContext(MfcDockContextCompat& context);
MfcDockContextCompat* DeleteDockContextScalarDtor(
    MfcDockContextCompat* context, unsigned flags);
void DockContextStartDrag(MfcDockContextCompat& context, POINT point);
void DockContextMove(MfcDockContextCompat& context, POINT point);
void DockContextOnKey(MfcDockContextCompat& context, int key, bool down);
void DockContextEndDrag(MfcDockContextCompat& context);
void DockContextStartResize(MfcDockContextCompat& context, int hit_test,
    POINT point);
void DockContextStretch(MfcDockContextCompat& context, POINT point);
void DockContextEndResize(MfcDockContextCompat& context);
void DockContextToggleDocking(MfcDockContextCompat& context);
void DockContextInitLoop(MfcDockContextCompat& context);
void DockContextCancelLoop(MfcDockContextCompat& context);
void DockContextDrawFocusRect(MfcDockContextCompat& context, bool remove);
void DockContextSetKeyState(MfcDockContextCompat& context, bool& state,
    bool down);
DWORD DockContextCanDock(MfcDockContextCompat& context);
MfcCWndCompat* DockContextGetDockBar(MfcDockContextCompat& context,
    DWORD dock_style);
bool DockContextTrack(MfcDockContextCompat& context);
MfcRuntimeClassCompat* GetDockBarRuntimeClass();
const MfcMessageMapCompat* GetDockBarMessageMap();
MfcDockBarCompat& ConstructDockBar(MfcDockBarCompat& dock_bar,
    bool floating);
void DestroyDockBar(MfcDockBarCompat& dock_bar);
MfcDockBarCompat* DeleteDockBarScalarDtor(MfcDockBarCompat* dock_bar,
    unsigned flags);
bool DockBarCreate(MfcDockBarCompat& dock_bar, MfcCWndCompat* parent,
    DWORD style, UINT id);
bool DockBarDefaultTrue();
int DockBarGetDockedCount(const MfcDockBarCompat& dock_bar);
int DockBarGetVisibleDockedCount(MfcDockBarCompat& dock_bar);
void DockBarDockControlBar(MfcDockBarCompat& dock_bar,
    MfcControlBarCompat& bar, const RECT* rect);
void DockBarDockControlBarAtRect(MfcDockBarCompat& dock_bar,
    MfcControlBarCompat& bar, const RECT* rect);
void DockBarRemovePlaceHolder(MfcDockBarCompat& dock_bar,
    std::uintptr_t bar_or_id);
bool DockBarRemoveControlBar(MfcDockBarCompat& dock_bar,
    MfcControlBarCompat& bar, int start_after, int save_place_holder);
SIZE DockBarCalcFixedLayout(MfcDockBarCompat& dock_bar, bool stretch,
    bool horizontal);
void DockBarCalcInsideRect(MfcDockBarCompat& dock_bar, RECT& rect,
    bool horizontal);
void DockBarOnNcPaint(MfcDockBarCompat& dock_bar);
void DockBarDoPaint(MfcDockBarCompat& dock_bar, MfcCDCCompat& dc);
void DockBarOnNcCalcSize(MfcDockBarCompat& dock_bar);
void DockBarOnPaint(MfcDockBarCompat& dock_bar);
void DockBarOnWindowPosChanging(MfcDockBarCompat& dock_bar,
    WINDOWPOS& window_pos);
int DockBarFindBar(const MfcDockBarCompat& dock_bar,
    std::uintptr_t raw_entry, int skip_index);
void DockBarOnUpdateCmdUI(MfcDockBarCompat& dock_bar,
    MfcCWndCompat* target, bool disable_if_no_handler);
MfcControlBarCompat* DockBarGetDockedControlBar(
    const MfcDockBarCompat& dock_bar, int index);
int DockBarInsertBarAtRect(MfcDockBarCompat& dock_bar,
    MfcControlBarCompat& bar, const RECT& rect);
void DockBarAssertValid(MfcDockBarCompat& dock_bar);
void DockBarDump(MfcDockBarCompat& dock_bar, MfcDumpContext& dump_context);
void ControlBarEnableDocking(MfcControlBarCompat& bar, DWORD dock_style);
MfcRuntimeClassCompat* GetMiniFrameRuntimeClass();
MfcMiniFrameWndCompat& ConstructMiniFrameWnd(MfcMiniFrameWndCompat& frame);
void DestroyMiniFrameWnd(MfcMiniFrameWndCompat& frame);
MfcMiniFrameWndCompat* DeleteMiniFrameScalarDtor(
    MfcMiniFrameWndCompat* frame, unsigned flags);
MfcFrameWndCompat* FrameWndFromChild(MfcCWndCompat* child);
int MiniFrameGetCaptionShowMode(MfcMiniFrameWndCompat& frame);
void CleanupMiniFrameMetrics();
void InitializeMiniFrameMetricsThunk();
void RegisterMiniFrameMetricsCleanup();
void InitializeMiniFrameMetrics();
bool MiniFrameCreate(MfcMiniFrameWndCompat& frame, const char* class_name,
    const char* window_name, DWORD style, const RECT& rect,
    MfcCWndCompat* parent, UINT id);
bool MiniFrameCreateEx(MfcMiniFrameWndCompat& frame, DWORD ex_style,
    const char* class_name, const char* window_name, DWORD style,
    const RECT& rect, MfcCWndCompat* parent, UINT id);
bool MiniFrameOnNcCreate(MfcMiniFrameWndCompat& frame, CREATESTRUCTA& create);
bool MiniFramePreCreateWindow(MfcMiniFrameWndCompat& frame,
    CREATESTRUCTA& create);
bool MiniFrameOnNcActivate(MfcMiniFrameWndCompat& frame, BOOL active);
void MiniFrameCalcInsideRect(MfcMiniFrameWndCompat& frame, RECT& rect);
UINT MiniFrameHitTest(MfcMiniFrameWndCompat& frame, POINT point);
void MiniFrameOnNcLButtonDown(MfcMiniFrameWndCompat& frame, UINT hit_test,
    POINT point);
void MiniFrameOnNcMouseMove(MfcMiniFrameWndCompat& frame, UINT hit_test,
    POINT point);
void MiniFrameOnNcLButtonUp(MfcMiniFrameWndCompat& frame, UINT hit_test,
    POINT point);
void MiniFrameInvertSysMenu(MfcMiniFrameWndCompat& frame);
void MiniFrameDrawBorder(MfcMiniFrameWndCompat& frame, HDC dc,
    const RECT& rect, int cx, int cy);
void MiniFrameOnNcPaint(MfcMiniFrameWndCompat& frame);
void MiniFrameOnSysCommand(MfcMiniFrameWndCompat& frame, UINT command,
    LPARAM lparam);
void MiniFrameCalcWindowRect(MfcMiniFrameWndCompat& frame, RECT& rect,
    bool include_menu);
void MiniFrameAdjustWindowRect(RECT& rect, DWORD style);
void MiniFrameOnGetMinMaxInfo(MfcMiniFrameWndCompat& frame,
    MINMAXINFO& min_max);
LRESULT MiniFrameOnGetText(MfcMiniFrameWndCompat& frame, int max_count,
    char* buffer);
int MiniFrameOnGetTextLength(MfcMiniFrameWndCompat& frame);
void MiniFrameOnSetText(MfcMiniFrameWndCompat& frame, const char* text);
bool MiniFrameOnSetTextEpilogue();
bool MiniFrameModifyStyleFlags(MfcMiniFrameWndCompat& frame, DWORD flags);
HWND MiniFrameOnQueryCenterWnd(MfcMiniFrameWndCompat& frame);
MfcRuntimeClassCompat* GetMiniDockFrameRuntimeClass();
const MfcMessageMapCompat* GetMiniDockFrameMessageMap();
MfcMiniDockFrameWndCompat& ConstructMiniDockFrameWnd(
    MfcMiniDockFrameWndCompat& frame);
void DestroyMiniDockFrameWnd(MfcMiniDockFrameWndCompat& frame);
MfcMiniDockFrameWndCompat* DeleteMiniDockFrameScalarDtor(
    MfcMiniDockFrameWndCompat* frame, unsigned flags);
bool MiniDockFrameCreate(MfcMiniDockFrameWndCompat& frame,
    MfcCWndCompat* parent, DWORD dock_style);
void MiniDockFrameOnSetText(MfcMiniDockFrameWndCompat& frame,
    const char* text);
void MiniDockFrameOnUpdateCmdUI(MfcMiniDockFrameWndCompat& frame,
    bool disable_if_no_handler);
int MiniDockFrameOnMouseActivate(MfcMiniDockFrameWndCompat& frame,
    MfcCWndCompat* top_level, UINT hit_test, UINT message);
void MiniDockFrameOnNcLButtonDown(MfcMiniDockFrameWndCompat& frame,
    UINT hit_test, POINT point);
void MiniDockFrameOnNcLButtonDblClk(MfcMiniDockFrameWndCompat& frame,
    UINT hit_test, POINT point);
MfcMiniDockFrameWndCompat* FrameWndCreateFloatingFrame(
    MfcFrameWndCompat& frame, DWORD dock_style);
void FrameWndEnableDocking(MfcFrameWndCompat& frame, DWORD dock_style);
void FrameWndDockControlBar(MfcFrameWndCompat& frame, MfcControlBarCompat& bar,
    UINT dock_id, const RECT* rect);
void FrameWndDockControlBarToDockBar(MfcFrameWndCompat& frame,
    MfcControlBarCompat& bar, MfcDockBarCompat* dock_bar, const RECT* rect);
void FrameWndDockControlBarAtRect(MfcFrameWndCompat& frame,
    MfcControlBarCompat& bar, MfcDockBarCompat* dock_bar, const RECT* rect);
void FrameWndFloatControlBar(MfcFrameWndCompat& frame, MfcControlBarCompat& bar,
    POINT point, DWORD dock_style);
DWORD FrameWndCanDock(MfcFrameWndCompat& frame, POINT point, DWORD dock_style,
    MfcDockBarCompat** dock_bar);
HBITMAP LoadSysColorBitmap(HMODULE module, HRSRC resource, bool monochrome);
MfcRuntimeClassCompat* GetDialogBarRuntimeClass();
const MfcMessageMapCompat* GetDialogBarMessageMap();
MfcDialogBarCompat& ConstructDialogBar(MfcDialogBarCompat& bar);
void DestroyDialogBar(MfcDialogBarCompat& bar);
MfcDialogBarCompat* DeleteDialogBarScalarDtor(MfcDialogBarCompat* bar,
    unsigned flags);
bool DialogBarCreate(MfcDialogBarCompat& bar, MfcCWndCompat* parent,
    LPCSTR template_name, DWORD style, UINT id);
bool DialogBarCreateById(MfcDialogBarCompat& bar, MfcCWndCompat* parent,
    UINT template_id, DWORD style, UINT id);
SIZE DialogBarCalcFixedLayout(MfcDialogBarCompat& bar, bool stretch,
    bool horizontal);
void DialogBarOnUpdateCmdUI(MfcDialogBarCompat& bar, MfcCWndCompat* target,
    bool disable_if_no_handler);
bool DialogBarOnInitDialog(MfcDialogBarCompat& bar);
bool DialogBarSetOccDialogInfo(MfcDialogBarCompat& bar, void* occ_info);
void AfxFormatStringsFromResource(MfcCStringCompat& output, UINT resource_id,
    const char* const* inserts, int count);
void AfxFormatStrings(MfcCStringCompat& output, const char* format,
    const char* const* inserts, int count);
void AfxFormatString1(MfcCStringCompat& output, UINT resource_id,
    const char* insert);
void AfxFormatString2(MfcCStringCompat& output, UINT resource_id,
    const char* insert1, const char* insert2);
bool ToolBarGetButton(MfcToolBarCompat& toolbar, int index,
    TBBUTTON& button);
bool ToolBarSetButton(MfcToolBarCompat& toolbar, int index,
    const TBBUTTON& button);
int ToolBarCommandToIndex(MfcToolBarCompat& toolbar, int command_id);
int ToolBarGetItemID(MfcToolBarCompat& toolbar, int index);
bool ToolBarGetItemRect(MfcToolBarCompat& toolbar, int index, RECT& rect);
void ToolBarInvalidateButtonLayout(MfcToolBarCompat& toolbar);
UINT ToolBarGetButtonStyle(MfcToolBarCompat& toolbar, int index);
void ToolBarSetButtonStyle(MfcToolBarCompat& toolbar, int index, UINT style);
SIZE ToolBarCalcSize(MfcToolBarCompat& toolbar, const TBBUTTON* buttons,
    int count);
int ToolBarWrapToolBar(MfcToolBarCompat& toolbar, TBBUTTON* buttons,
    int count, int width);
void ToolBarSizeToolBar(MfcToolBarCompat& toolbar, TBBUTTON* buttons,
    int count, int length, bool vertical);
SIZE ToolBarCalcLayout(MfcToolBarCompat& toolbar, DWORD mode, int length);
SIZE ToolBarCalcFixedLayout(MfcToolBarCompat& toolbar, bool stretch,
    bool horizontal);
SIZE ToolBarCalcDynamicLayout(MfcToolBarCompat& toolbar, int length,
    DWORD mode);
void ToolBarGetButtonInfo(MfcToolBarCompat& toolbar, int index,
    int& image, UINT& style, int& command_id);
void ToolBarSetButtonInfo(MfcToolBarCompat& toolbar, int index,
    int image, UINT style, int command_id);
UINT ToolBarOnToolHitTest(MfcToolBarCompat& toolbar, POINT point,
    TOOLINFOA* tool_info);
bool ToolBarSetButtonText(MfcToolBarCompat& toolbar, int index,
    const char* text);
std::string ToolBarFormatButtonText(MfcToolBarCompat& toolbar, int index);
std::string ToolBarGetButtonText(MfcToolBarCompat& toolbar, int index);
MfcRuntimeClassCompat* GetToolBarRuntimeClass();
void ToolBarOnNcDestroy(MfcToolBarCompat& toolbar);
bool ToolBarDefaultTrue();
void ToolBarCalcInsideRect(MfcToolBarCompat& toolbar, RECT& rect,
    bool horizontal);
void ToolBarOnBarStyleChange(MfcToolBarCompat& toolbar, DWORD old_style,
    DWORD new_style);
void ToolBarEraseNonClient(MfcToolBarCompat& toolbar);
void ToolBarOnWindowPosChanging(MfcToolBarCompat& toolbar,
    WINDOWPOS& window_pos);
void ToolBarOnNcCalcSize(MfcToolBarCompat& toolbar);
void ToolBarOnSetButtonSize(MfcToolBarCompat& toolbar, LPARAM size);
void ToolBarOnSetBitmapSize(MfcToolBarCompat& toolbar, LPARAM size);
LRESULT ToolBarHandleSizeMessage(MfcToolBarCompat& toolbar, SIZE& target,
    UINT message, LPARAM lparam);
LRESULT ToolBarDefaultWithStylePatch(MfcToolBarCompat& toolbar, UINT message,
    WPARAM wparam, LPARAM lparam);
void ToolBarReloadBitmap(MfcToolBarCompat& toolbar);
void ToolBarCmdUIEnable(MfcToolBarCompat& toolbar, MfcCmdUICompat& cmd_ui,
    bool enabled);
void ToolBarCmdUISetCheck(MfcToolBarCompat& toolbar, MfcCmdUICompat& cmd_ui,
    int check);
void ToolBarCmdUISetText(MfcToolBarCompat& toolbar, MfcCmdUICompat& cmd_ui,
    const char* text);
void ToolBarOnUpdateCmdUI(MfcToolBarCompat& toolbar,
    MfcCommandTargetCompat* target, bool disable_if_no_handler);
void ToolBarAssertValid(MfcToolBarCompat& toolbar);
void ToolBarDump(MfcToolBarCompat& toolbar);
void ToolBarSetOwnerWindow(MfcToolBarCompat& toolbar, MfcCWndCompat* owner);
void ToolBarSetSizes(MfcToolBarCompat& toolbar, SIZE button_size,
    SIZE image_size);
void ToolBarSetHeight(MfcToolBarCompat& toolbar, int height);
void ToolBarSetBitmapHandle(MfcToolBarCompat& toolbar, HBITMAP bitmap);
bool ToolBarInstallBitmapHandle(MfcToolBarCompat& toolbar, HBITMAP bitmap);
bool ToolBarSetButtons(MfcToolBarCompat& toolbar, const int* command_ids,
    int count);
void* ConstructToolBarLayoutItem(void* item);
void ConstructToolBarLayoutItems(void* first, std::size_t item_size,
    int count, void* (*construct)(void*));
void CWndSendCloseMessage(MfcCWndCompat& window);
MfcRuntimeClassCompat* GetDocTemplateRuntimeClass();
const MfcMessageMapCompat* GetDocTemplateMessageMap();
MfcDocTemplateCompat& ConstructDocTemplate(MfcDocTemplateCompat& templ,
    unsigned resource_id, void* doc_class, void* frame_class, void* view_class);
void DocTemplateLoadTemplate(MfcDocTemplateCompat& templ);
void DocTemplateSetServerInfo(MfcDocTemplateCompat& templ,
    unsigned server_resource, unsigned container_resource, void* frame_class,
    void* view_class);
void DocTemplateSetContainerInfo(MfcDocTemplateCompat& templ,
    unsigned container_resource);
void DestroyDocTemplate(MfcDocTemplateCompat& templ);
MfcDocTemplateCompat* DeleteDocTemplateScalarDtor(MfcDocTemplateCompat* templ,
    unsigned flags);
bool DocTemplateGetDocString(const MfcDocTemplateCompat& templ,
    std::string& out, int index);
void DocTemplateAddDocument(MfcDocTemplateCompat& templ,
    MfcDocumentCompat& document);
void DocTemplateRemoveDocument(MfcDocTemplateCompat& templ,
    MfcDocumentCompat& document);
int DocTemplateMatchDocType(MfcDocTemplateCompat& templ, const char* path,
    MfcDocumentCompat** matched_document);
MfcDocumentCompat* DocTemplateCreateNewDocument(MfcDocTemplateCompat& templ);
MfcDocumentCompat* DocTemplateOpenDocumentFile(MfcDocTemplateCompat& templ,
    const char* path, bool make_visible);
MfcCWndCompat* DocTemplateCreateNewFrame(MfcDocTemplateCompat& templ,
    MfcDocumentCompat* document, MfcCWndCompat* other);
MfcCWndCompat* DocTemplateCreateOleFrame(MfcDocTemplateCompat& templ,
    HWND parent, MfcDocumentCompat* document, bool server);
void DocTemplateInitialUpdateFrame(MfcDocTemplateCompat& templ,
    MfcCWndCompat* frame, MfcDocumentCompat* document, int make_visible);
bool DocTemplateSaveAllModified(MfcDocTemplateCompat& templ);
void DocTemplateCloseAllDocuments(MfcDocTemplateCompat& templ, bool end_session);
void DocTemplateUpdateFrameCounts(MfcDocTemplateCompat& templ);
bool DocTemplateOnCmdMsg(MfcDocTemplateCompat& templ, UINT id, int code,
    void* extra, MfcCommandHandlerInfoCompat* handler_info);
void DocTemplateDump(const MfcDocTemplateCompat& templ);
void DocTemplateAssertValid(const MfcDocTemplateCompat& templ);
const MfcMessageMapCompat* GetDocumentMessageMap();
MfcDocumentCompat& ConstructDocument(MfcDocumentCompat& document);
void DestroyDocument(MfcDocumentCompat& document);
MfcDocumentCompat* DeleteDocumentScalarDtor(MfcDocumentCompat* document,
    unsigned flags);
void DocumentCloseDefault(MfcDocumentCompat& document);
void OnCloseDocument(MfcDocumentCompat& document);
void DocumentDeleteContents(MfcDocumentCompat& document);
void DocumentDeleteAllViews(MfcDocumentCompat& document);
void DocumentDetachViews(MfcDocumentCompat& document);
void DocumentSetTitle(MfcDocumentCompat& document, const char* title);
void DocumentOnChangedViewList(MfcDocumentCompat& document);
void DocumentUpdateFrameCounts(MfcDocumentCompat& document);
bool DocumentCanCloseFrame(MfcDocumentCompat& document, MfcCWndCompat* frame);
void DocumentDefaultNoop();
bool DocumentDefaultFalse();
bool DocumentAlternateDefaultFalse();
void DocumentSetPathName(MfcDocumentCompat& document, const char* path,
    bool add_to_mru);
void DocumentOnFileClose(MfcDocumentCompat& document);
void DocumentOnFileSave(MfcDocumentCompat& document);
void DocumentOnFileSaveAs(MfcDocumentCompat& document);
bool DocumentDoFileSave(MfcDocumentCompat& document);
bool DocumentDoSave(MfcDocumentCompat& document, const char* path,
    bool replace);
bool DocumentDoSaveFailureCleanup();
bool DocumentSaveModified(MfcDocumentCompat& document);
void DocumentReportSaveLoadException(const char* path, void* exception,
    bool saving, unsigned default_message);
std::string DocumentGetTempFileName(const char* path, bool keep_original);
bool DocumentFileOpenWithBackup(MfcFileCompat& file, const char* path,
    unsigned open_flags, MfcFileExceptionCompat* exception);
void DocumentAbortFile(MfcFileCompat& file, const std::string& temp_path);
void DocumentCommitFile(MfcFileCompat& file, std::string& path_name,
    const std::string& temp_path);
MfcFileCompat* DocumentGetFile(MfcDocumentCompat& document, const char* path,
    unsigned open_flags, MfcFileExceptionCompat* exception);
void DocumentReleaseFile(MfcDocumentCompat& document, MfcFileCompat* file,
    bool abort);
bool DocumentOnNewDocument(MfcDocumentCompat& document);
bool DocumentOnOpenDocument(MfcDocumentCompat& document, const char* path);
bool DocumentOpenFailureCleanup();
bool DocumentOpenSuccessCleanup();
bool DocumentOnSaveDocument(MfcDocumentCompat& document, const char* path);
bool DocumentSaveFailureCleanup();
bool DocumentSaveSuccessCleanup();
void DocumentSerializeNoop(MfcDocumentCompat& document, MfcArchiveCompat& archive);
void DocumentAddView(MfcDocumentCompat& document, MfcViewCompat& view);
void DocumentRemoveView(MfcDocumentCompat& document, MfcViewCompat& view);
MfcViewCompat* DocumentGetNextView(MfcDocumentCompat& document,
    std::size_t& position);
void SendInitialUpdate(MfcDocumentCompat& document);
void DocumentUpdateAllViews(MfcDocumentCompat& document, MfcViewCompat* sender,
    LPARAM hint, void* hint_object);
bool DocumentOnCmdMsg(MfcDocumentCompat& document, UINT id, int code,
    void* extra, MfcCommandHandlerInfoCompat* handler_info);
MfcFileStatusCompat& ConstructFileStatusDefault(MfcFileStatusCompat& status);
MfcMirrorFileCompat& ConstructMirrorFile(MfcMirrorFileCompat& file);
MfcMirrorFileCompat* DeleteMirrorFileScalarDtor(MfcMirrorFileCompat* file,
    unsigned flags);
void DestroyMirrorFile(MfcMirrorFileCompat& file);
MfcRuntimeClassCompat* GetDocManagerRuntimeClass();
const MfcMessageMapCompat* GetDocManagerMessageMap();
MfcDocManagerCompat& ConstructDocManager(MfcDocManagerCompat& manager);
void DestroyDocManager(MfcDocManagerCompat& manager);
MfcDocManagerCompat* DeleteDocManagerScalarDtor(MfcDocManagerCompat* manager,
    unsigned flags);
void DocManagerAddDocTemplate(MfcDocManagerCompat& manager,
    MfcDocTemplateCompat& templ);
void DocManagerRemoveDocTemplate(MfcDocManagerCompat& manager,
    MfcDocTemplateCompat& templ);
MfcDocumentCompat* DocManagerOpenDocumentFile(MfcDocManagerCompat* manager,
    const char* path);
MfcDocumentCompat* DocManagerOpenDocumentFile(const char* path);
int DocManagerGetOpenDocumentCount(const MfcDocManagerCompat* manager);
int GetDocumentCount(const MfcDocManagerCompat& manager);
void DocManagerAppendFilterSuffix(MfcDocManagerCompat& manager,
    std::string& filter, MfcFileDialogCompat& dialog,
    MfcDocTemplateCompat& templ, std::string* default_extension);
std::size_t DocManagerGetFirstDocTemplatePosition(
    const MfcDocManagerCompat& manager);
MfcDocTemplateCompat* DocManagerGetNextDocTemplate(
    const MfcDocManagerCompat& manager, std::size_t& position);
bool DocManagerSaveAllModified(MfcDocManagerCompat& manager);
void DocManagerCloseAllDocuments(MfcDocManagerCompat& manager,
    bool end_session);
bool DocManagerDoPromptFileName(MfcDocManagerCompat& manager,
    std::string& path, UINT title_id, DWORD flags, bool open_dialog,
    MfcDocTemplateCompat* templ);
bool DocManagerOnDDECommand(MfcDocManagerCompat& manager,
    const char* command);
void DocManagerOnFileNew(MfcDocManagerCompat& manager);
void DocManagerOnFileOpen(MfcDocManagerCompat& manager);
const MfcMessageMapCompat* GetNewTypeDlgMessageMap();
MfcNewTypeDlgCompat& ConstructNewTypeDlg(MfcNewTypeDlgCompat& dialog,
    const std::vector<MfcDocTemplateCompat*>* templates);
void DestroyNewTypeDlg(MfcNewTypeDlgCompat& dialog);
MfcNewTypeDlgCompat* DeleteNewTypeDlgScalarDtor(
    MfcNewTypeDlgCompat* dialog, unsigned flags);
bool NewTypeDlgOnInitDialog(MfcNewTypeDlgCompat& dialog);
void NewTypeDlgOnOK(MfcNewTypeDlgCompat& dialog);
void WinAppOnFileNew(MfcWinAppCompat& app);
void WinAppOnFileOpen(MfcWinAppCompat& app);
bool WinAppDoPromptFileNameDialog(MfcWinAppCompat& app, std::string& path,
    UINT title_id, DWORD flags, bool open_dialog, MfcDocTemplateCompat* templ);
void* GetThreadStateCurrentWindowSlot();
void* GetThreadStateRoutingFrameSlot();
void UpdateMfcAuxDataSysColors(MfcAuxDataCompat& aux_data);
void UpdateMfcAuxDataSysMetrics(MfcAuxDataCompat& aux_data);
bool CStringLoadString(MfcCStringCompat& text, UINT resource_id);
int AfxLoadStringCompat(UINT resource_id, char* buffer, int max_count);
bool CStringExtractSubString(MfcCStringCompat& text, const char* full_string,
    int substring_index, char separator);
MfcCommandTargetCompat& ConstructCmdTarget(MfcCommandTargetCompat& target);
void DestroyCmdTarget(MfcCommandTargetCompat& target);
void OnFinalRelease(MfcCommandTargetCompat& target);
bool DispatchCmdMsg(MfcCommandTargetCompat& target, UINT id, int code,
    void* handler, void* extra, UINT signature,
    MfcCommandHandlerInfoCompat* handler_info);
bool CmdTargetOnCmdMsg(MfcCommandTargetCompat& target, UINT id, int code,
    void* extra, MfcCommandHandlerInfoCompat* handler_info);
bool CmdTargetIsInvokeAllowed();
bool CmdTargetGetDispatchIIDDefault(void* iid);
unsigned CmdTargetGetTypeInfoCountDefault();
bool CmdTargetGetTypeLibDefault(void* type_lib);
long CmdTargetGetTypeInfoOfGuidDefault();
void AfxBeginWaitCursor();
void AfxEndWaitCursor();
void AfxRestoreWaitCursor();
MfcRuntimeClassCompat* GetCmdTargetRuntimeClass();
const MfcMessageMapCompat* GetCmdTargetMessageMap();
void* GetCmdTargetDispatchMap();
void* GetCmdTargetInterfaceMap();
bool CmdTargetDefaultTrue();
bool CmdTargetDefaultFalse();
bool CmdTargetDispatchDefaultFalse();
bool CmdTargetInterfaceDefaultFalse();
void* GetCmdTargetRuntimeClassThunk();
void* GetCmdTargetMessageMapThunk();
long OnCommandHelp(MfcDialogCompat& dialog, UINT command, long help_id);
long OnCommandHelp(MfcPropertySheetCompat& sheet, UINT command, long help_id);
long OnCommandHelp(MfcFrameWndCompat& frame, UINT command, long help_id);
void* GetThreadStateCurrentWindow();
void* GetThreadStateRoutingFrame();
MfcCmdUICompat& ConstructCmdUI(MfcCmdUICompat& cmd_ui);
void CmdUIEnable(MfcCmdUICompat& cmd_ui, bool enabled);
void CmdUISetCheck(MfcCmdUICompat& cmd_ui, int check);
void CmdUISetRadio(MfcCmdUICompat& cmd_ui, bool enabled);
void CmdUISetText(MfcCmdUICompat& cmd_ui, const char* text);
bool CmdUIDoUpdate(MfcCmdUICompat& cmd_ui, MfcCommandTargetCompat& target,
    bool disable_if_no_handler);
MfcCommandTargetCompat* DeleteCmdTargetScalarDtor(
    MfcCommandTargetCompat* target, unsigned flags);
MfcRuntimeClassCompat* GetDialogRuntimeClass();
MfcDialogCompat& ConstructDialogDefault(MfcDialogCompat& dialog);
void DestroyDialog(MfcDialogCompat& dialog);
bool DialogPreTranslateMessage(MfcDialogCompat& dialog, MSG& message);
bool DialogOnCmdMsg(MfcDialogCompat& dialog, UINT id, int code, void* extra,
    MfcCommandHandlerInfoCompat* handler_info);
bool DialogCreate(MfcDialogCompat& dialog, LPCSTR template_name,
    MfcCWndCompat* parent);
bool DialogCreateIndirectResource(MfcDialogCompat& dialog, HGLOBAL resource,
    MfcCWndCompat* parent);
bool DialogCreateIndirectFromResource(MfcDialogCompat& dialog, HGLOBAL resource,
    MfcCWndCompat* parent, HINSTANCE instance);
bool DialogCreateIndirectCore(MfcDialogCompat& dialog,
    const DLGTEMPLATE* dialog_template, MfcCWndCompat* parent, void* init_param,
    HINSTANCE instance);
bool DialogInitModalIndirectResource(MfcDialogCompat& dialog,
    LPCSTR template_name, MfcCWndCompat* parent);
bool DialogInitModalIndirect(MfcDialogCompat& dialog,
    const DLGTEMPLATE* dialog_template, MfcCWndCompat* parent);
bool DialogCreateIndirectOrModal(MfcDialogCompat& dialog,
    const DLGTEMPLATE* dialog_template, MfcCWndCompat* parent,
    HINSTANCE instance);
bool DialogCreateIndirectCleanup(MfcDialogCompat& dialog, HWND created,
    HGLOBAL global_template, DWORD last_error);
bool DialogSetOccDialogInfo(MfcDialogCompat& dialog, void* occ_info);
MfcDialogCompat& ConstructDialogWithTemplateName(MfcDialogCompat& dialog,
    LPCSTR template_name, MfcCWndCompat* parent);
MfcDialogCompat& ConstructDialogWithTemplateId(MfcDialogCompat& dialog,
    UINT template_id, MfcCWndCompat* parent);
bool DialogInitModalIndirectHandle(MfcDialogCompat& dialog, HGLOBAL resource,
    MfcCWndCompat* parent);
bool DialogInitModalIndirectPointer(MfcDialogCompat& dialog,
    const DLGTEMPLATE* dialog_template, MfcCWndCompat* parent,
    void* dialog_init);
HWND DialogPreModal(MfcDialogCompat& dialog);
void DialogPostModal(MfcDialogCompat& dialog);
INT_PTR DialogDoModal(MfcDialogCompat& dialog);
INT_PTR DialogDoModalCleanup(MfcDialogCompat& dialog, HWND owner,
    HGLOBAL resource, bool resource_locked);
void DialogEndDialog(MfcDialogCompat& dialog, INT_PTR result);
void DialogDefaultNoop();
long DialogHandleInitDialog(MfcDialogCompat& dialog);
#ifdef _WIN32
INT_PTR AfxDlgProcCompat(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
#endif
bool DialogRouteHelpCommand();
void DialogOnSetFontDefault();
bool DialogOnInitDialog(MfcDialogCompat& dialog);
void DialogOnOK(MfcDialogCompat& dialog);
void DialogOnCancel(MfcDialogCompat& dialog);
bool DialogTemplateIsSimpleTopLevel(MfcDialogCompat& dialog);
void DialogOnCtlColorForward(MfcDialogCompat& dialog, HDC dc, HWND child);
void DialogAssertValid(const MfcDialogCompat& dialog);
void DialogDump(const MfcDialogCompat& dialog);
UINT CALLBACK PropertyPageCallback(HWND hwnd, UINT message,
    PROPSHEETPAGEA* page);
bool PropertyPageCallbackSuccess();
MfcRuntimeClassCompat* GetPropertyPageRuntimeClass();
const MfcMessageMapCompat* GetPropertyPageMessageMap();
MfcPropertyPageCompat& ConstructPropertyPageWithTemplateId(
    MfcPropertyPageCompat& page, UINT template_id, UINT caption_id);
MfcPropertyPageCompat& ConstructPropertyPageWithTemplateName(
    MfcPropertyPageCompat& page, LPCSTR template_name, UINT caption_id);
void PropertyPageConstructByTemplateId(MfcPropertyPageCompat& page,
    UINT template_id, UINT caption_id);
void PropertyPageConstructByTemplateName(MfcPropertyPageCompat& page,
    LPCSTR template_name, UINT caption_id);
MfcPropertyPageCompat& ConstructPropertyPageDefault(
    MfcPropertyPageCompat& page);
void PropertyPageCommonConstruct(MfcPropertyPageCompat& page,
    LPCSTR template_name, UINT caption_id);
void DestroyPropertyPage(MfcPropertyPageCompat& page);
void PropertyPageCleanup(MfcPropertyPageCompat& page);
HGLOBAL PropertyPageCloneTemplateWithCurrentFont(const DLGTEMPLATE* source,
    UINT font_flags);
void PropertyPageCreateOccDialogInfo(MfcPropertyPageCompat& page,
    const DLGTEMPLATE* source);
void PropertyPagePrepareSheetPageTemplate(MfcPropertyPageCompat& page,
    PROPSHEETPAGEA& sheet_page, UINT font_flags);
void PropertyPageCancelToClose(MfcPropertyPageCompat& page);
void PropertyPageSetModified(MfcPropertyPageCompat& page, bool modified);
LRESULT PropertyPageQuerySiblings(MfcPropertyPageCompat& page, WPARAM wparam,
    LPARAM lparam);
BOOL PropertyPageOnApply(MfcPropertyPageCompat& page);
void PropertyPageOnReset(MfcPropertyPageCompat& page);
void PropertyPageOnOK(MfcPropertyPageCompat& page);
void PropertyPageOnCancel(MfcPropertyPageCompat& page);
BOOL PropertyPageOnSetActive(MfcPropertyPageCompat& page);
BOOL PropertyPageOnKillActive(MfcPropertyPageCompat& page);
BOOL PropertyPageOnQueryCancel();
LRESULT PropertyPageOnWizardBack();
LRESULT PropertyPageOnWizardNext();
BOOL PropertyPageOnWizardFinish();
LRESULT PropertyPageMapWizardResult(MfcPropertyPageCompat& page,
    LRESULT result);
bool PropertyPageIsButtonEnabled(MfcPropertyPageCompat& page, int button_id);
bool PropertyPageOnNotify(MfcPropertyPageCompat& page, WPARAM control_id,
    NMHDR* notify, LRESULT& result);
bool PropertyPagePreTranslateMessage(MfcPropertyPageCompat& page,
    MSG& message);
LRESULT PropertyPageOnCtlColor(MfcPropertyPageCompat& page, HDC dc,
    HWND child, UINT type);
void PropertyPageAssertValid(const MfcPropertyPageCompat& page);
void PropertyPageDump(const MfcPropertyPageCompat& page);
void PropertyPageEndDialog(MfcPropertyPageCompat& page, int result);
MfcRuntimeClassCompat* GetPropertySheetRuntimeClass();
const MfcMessageMapCompat* GetPropertySheetMessageMap();
PROPSHEETHEADERA& PropertySheetHeader(MfcPropertySheetCompat& sheet);
MfcPropertyPageCompat* PropertySheetGetPageAt(
    MfcPropertySheetCompat& sheet, int index);
void PropertyPageEnableHelpInline(MfcPropertyPageCompat& page);
void PropertySheetOnKickIdle(MfcPropertySheetCompat& sheet, int button_id,
    LPARAM lparam);
MfcPropertySheetCompat& ConstructPropertySheetDefault(
    MfcPropertySheetCompat& sheet);
MfcPropertySheetCompat& ConstructPropertySheetWithCaptionId(
    MfcPropertySheetCompat& sheet, UINT caption_id, MfcCWndCompat* parent,
    UINT selected_page);
MfcPropertySheetCompat& ConstructPropertySheetWithCaptionName(
    MfcPropertySheetCompat& sheet, const char* caption, MfcCWndCompat* parent,
    UINT selected_page);
void PropertySheetConstructByCaptionId(MfcPropertySheetCompat& sheet,
    UINT caption_id, MfcCWndCompat* parent, UINT selected_page);
void PropertySheetConstructByCaptionName(MfcPropertySheetCompat& sheet,
    const char* caption, MfcCWndCompat* parent, UINT selected_page);
void PropertySheetCommonConstruct(MfcPropertySheetCompat& sheet,
    MfcCWndCompat* parent, UINT selected_page);
void PropertySheetEnableStackedTabs(MfcPropertySheetCompat& sheet,
    bool stacked);
void PropertySheetSetTitle(MfcPropertySheetCompat& sheet, const char* title,
    UINT style);
void PropertySheetSetFinishText(MfcPropertySheetCompat& sheet,
    const char* text);
void PropertySheetSetWizardButtons(MfcPropertySheetCompat& sheet,
    DWORD flags);
MfcCWndCompat* PropertySheetGetTabControl(MfcPropertySheetCompat& sheet);
void PropertySheetPressButton(MfcPropertySheetCompat& sheet, int button);
bool PropertySheetUsesWizardFont(MfcPropertySheetCompat& sheet);
void DestroyPropertySheet(MfcPropertySheetCompat& sheet);
bool PropertySheetPreTranslateMessage(MfcPropertySheetCompat& sheet,
    MSG& message);
bool PropertySheetOnCmdMsg(MfcPropertySheetCompat& sheet, UINT id, int code,
    void* extra, MfcCommandHandlerInfoCompat* handler_info);
MfcPropertyPageCompat* PropertySheetGetActivePage(
    MfcPropertySheetCompat& sheet);
bool PropertySheetContinueModal(MfcPropertySheetCompat& sheet);
int PropertySheetDoModal(MfcPropertySheetCompat& sheet);
int CALLBACK PropertySheetCreateCallback(HWND hwnd, UINT message,
    LPARAM lparam);
bool PropertySheetCreateModeless(MfcPropertySheetCompat& sheet,
    MfcCWndCompat* parent, DWORD style, DWORD ex_style);
void PropertySheetBuildPageArray(MfcPropertySheetCompat& sheet);
int PropertySheetGetPageCount(MfcPropertySheetCompat& sheet);
int PropertySheetGetActiveIndex(MfcPropertySheetCompat& sheet);
bool PropertySheetSetActiveIndex(MfcPropertySheetCompat& sheet, int index);
void PropertySheetSetActivePage(MfcPropertySheetCompat& sheet,
    MfcPropertyPageCompat* page);
void PropertySheetAddPage(MfcPropertySheetCompat& sheet,
    MfcPropertyPageCompat* page);
void PropertySheetRemovePage(MfcPropertySheetCompat& sheet,
    MfcPropertyPageCompat* page);
void PropertySheetRemovePageAt(MfcPropertySheetCompat& sheet, int index);
LRESULT PropertySheetOnInitDialog(MfcPropertySheetCompat& sheet);
LRESULT PropertySheetHandleInitDialog(MfcPropertySheetCompat& sheet);
bool PropertySheetOnCommand(MfcPropertySheetCompat& sheet, WPARAM wparam,
    HWND control);
LRESULT PropertySheetOnCtlColor(MfcPropertySheetCompat& sheet, HDC dc,
    HWND child, UINT type);
void PropertySheetAssertValid(const MfcPropertySheetCompat& sheet);
void PropertySheetDump(const MfcPropertySheetCompat& sheet);
MfcRuntimeClassCompat* GetPropertyPageExRuntimeClass();
MfcPropertyPageExCompat& ConstructPropertyPageExWithTemplateId(
    MfcPropertyPageExCompat& page, UINT template_id, UINT caption_id,
    UINT header_title_id, UINT header_subtitle_id);
MfcPropertyPageExCompat& ConstructPropertyPageExWithTemplateName(
    MfcPropertyPageExCompat& page, LPCSTR template_name, UINT caption_id,
    UINT header_title_id, UINT header_subtitle_id);
void PropertyPageExConstructByTemplateId(MfcPropertyPageExCompat& page,
    UINT template_id, UINT caption_id, UINT header_title_id,
    UINT header_subtitle_id);
void PropertyPageExConstructByTemplateName(MfcPropertyPageExCompat& page,
    LPCSTR template_name, UINT caption_id, UINT header_title_id,
    UINT header_subtitle_id);
MfcPropertyPageExCompat& ConstructPropertyPageExDefault(
    MfcPropertyPageExCompat& page);
void PropertyPageExCommonConstruct(MfcPropertyPageExCompat& page,
    LPCSTR template_name, UINT caption_id, UINT header_title_id,
    UINT header_subtitle_id);
void PropertyPageExAssertValid(const MfcPropertyPageExCompat& page);
void PropertyPageExDump(const MfcPropertyPageExCompat& page);
void DestroyPropertyPageEx(MfcPropertyPageExCompat& page);
MfcRuntimeClassCompat* GetPropertySheetExRuntimeClass();
MfcPropertySheetExCompat& ConstructPropertySheetExDefault(
    MfcPropertySheetExCompat& sheet);
MfcPropertySheetExCompat& ConstructPropertySheetExWithCaptionId(
    MfcPropertySheetExCompat& sheet, UINT caption_id, MfcCWndCompat* parent,
    UINT selected_page, HBITMAP watermark, HPALETTE palette, HBITMAP header);
MfcPropertySheetExCompat& ConstructPropertySheetExWithCaptionName(
    MfcPropertySheetExCompat& sheet, const char* caption,
    MfcCWndCompat* parent, UINT selected_page, HBITMAP watermark,
    HPALETTE palette, HBITMAP header);
void PropertySheetExConstructByCaptionId(MfcPropertySheetExCompat& sheet,
    UINT caption_id, MfcCWndCompat* parent, UINT selected_page,
    HBITMAP watermark, HPALETTE palette, HBITMAP header);
void PropertySheetExConstructByCaptionName(MfcPropertySheetExCompat& sheet,
    const char* caption, MfcCWndCompat* parent, UINT selected_page,
    HBITMAP watermark, HPALETTE palette, HBITMAP header);
void PropertySheetExCommonConstruct(MfcPropertySheetExCompat& sheet,
    MfcCWndCompat* parent, UINT selected_page, HBITMAP watermark,
    HPALETTE palette, HBITMAP header);
void PropertySheetExSetWizardMode(MfcPropertySheetExCompat& sheet);
SIZE PropertySheetExGetWatermarkSize(const MfcPropertySheetExCompat& sheet);
void PropertySheetExBuildPageArray(MfcPropertySheetExCompat& sheet);
void DestroyPropertySheetEx(MfcPropertySheetExCompat& sheet);
void PropertySheetExAddPage(MfcPropertySheetExCompat& sheet,
    MfcPropertyPageExCompat* page);
void PropertySheetExAssertValid(const MfcPropertySheetExCompat& sheet);
void PropertySheetExDump(const MfcPropertySheetExCompat& sheet);
MfcPropertyPageCompat* DeletePropertyPageScalarDtor(
    MfcPropertyPageCompat* page, unsigned flags);
MfcPropertySheetCompat* DeletePropertySheetScalarDtor(
    MfcPropertySheetCompat* sheet, unsigned flags);
MfcPropertyPageExCompat* DeletePropertyPageExScalarDtor(
    MfcPropertyPageExCompat* page, unsigned flags);
MfcPropertySheetExCompat* DeletePropertySheetExScalarDtor(
    MfcPropertySheetExCompat* sheet, unsigned flags);
HWND DdxPrepareEditCtrl(MfcDataExchangeCompat& dx, int control_id);
HWND DdxPrepareCtrl(MfcDataExchangeCompat& dx, int control_id);
[[noreturn]] void DdxFail(MfcDataExchangeCompat& dx);
bool DdxParseIntText(const char* text, const char* format, void* value);
void DdxTextWithFormat(MfcDataExchangeCompat& dx, int control_id,
    const char* format, unsigned fail_prompt, void* value);
void DDX_Text(MfcDataExchangeCompat& dx, int control_id,
    MfcCStringCompat& value);
void DdxTextByte(MfcDataExchangeCompat& dx, int control_id, BYTE& value);
void DdxTextBuffer(MfcDataExchangeCompat& dx, int control_id, char* buffer,
    int max_count);
void DdxCheck(MfcDataExchangeCompat& dx, int control_id, int& value);
void DdxRadio(MfcDataExchangeCompat& dx, int first_control_id, int& value);
void DdxListBoxString(MfcDataExchangeCompat& dx, int control_id,
    MfcCStringCompat& value);
void DdxListBoxStringExact(MfcDataExchangeCompat& dx, int control_id,
    MfcCStringCompat& value);
void DdxComboBoxString(MfcDataExchangeCompat& dx, int control_id,
    MfcCStringCompat& value);
void DdxComboBoxStringExact(MfcDataExchangeCompat& dx, int control_id,
    MfcCStringCompat& value);
void DdxListBoxIndex(MfcDataExchangeCompat& dx, int control_id, int& value);
void DdxComboBoxIndex(MfcDataExchangeCompat& dx, int control_id, int& value);
void DdxScroll(MfcDataExchangeCompat& dx, int control_id, int& value);
void DdxSlider(MfcDataExchangeCompat& dx, int control_id, int& value);
void DdvFailMinMax(MfcDataExchangeCompat& dx, int min_value, int max_value,
    const char* format, unsigned prompt_id);
void DdvMinMaxByte(MfcDataExchangeCompat& dx, BYTE value, BYTE min_value,
    BYTE max_value);
void DdvMinMaxShort(MfcDataExchangeCompat& dx, short value, short min_value,
    short max_value);
void DdvMinMaxInt(MfcDataExchangeCompat& dx, int value, int min_value,
    int max_value);
void DdvMinMaxLong(MfcDataExchangeCompat& dx, long value, long min_value,
    long max_value);
void DdvMinMaxUInt(MfcDataExchangeCompat& dx, unsigned value,
    unsigned min_value, unsigned max_value);
void DdvMinMaxULong(MfcDataExchangeCompat& dx, unsigned long value,
    unsigned long min_value, unsigned long max_value);
void DdvSliderMinMax(MfcDataExchangeCompat& dx, unsigned value,
    unsigned min_value, unsigned max_value);
void DdvMaxChars(MfcDataExchangeCompat& dx, const MfcCStringCompat& value,
    unsigned max_chars);
void DdxControl(MfcDataExchangeCompat& dx, int control_id,
    MfcCWndCompat& control);
void DdvFailMaxChars(MfcDataExchangeCompat& dx, unsigned max_chars);
void AfxFailRadio(MfcDataExchangeCompat& dx);
bool CheckDialogTemplate(LPCSTR template_name, bool require_child_style);
MfcDialogCompat* DeleteDialogScalarDtor(MfcDialogCompat* dialog,
    unsigned flags);
void DialogTemplateMapDialogUnits(const char* face_name, unsigned point_size,
    int dialog_cx, int dialog_cy, SIZE& pixels);
void DestroyDialogTemplate(MfcDialogTemplateCompat& dialog_template);
HGLOBAL DetachDialogTemplateHandle(MfcDialogTemplateCompat& dialog_template);
const WORD* DialogTemplateSkipMenuClassTitle(const DLGTEMPLATE* dialog_template);
unsigned DialogTemplateSize(const DLGTEMPLATE* dialog_template);
bool DialogTemplateGetFont(const DLGTEMPLATE* dialog_template,
    MfcCStringCompat& face_name, WORD& point_size);
bool DialogTemplateGetFontFromHandle(MfcDialogTemplateCompat& dialog_template,
    MfcCStringCompat& face_name, WORD& point_size);
bool DialogTemplateSetFont(MfcDialogTemplateCompat& dialog_template,
    const char* face_name, WORD point_size);
bool DialogTemplateSetSystemFont(MfcDialogTemplateCompat& dialog_template,
    WORD point_size);
void DialogTemplateGetSizeInDialogUnits(MfcDialogTemplateCompat& dialog_template,
    SIZE& size);
void DialogTemplateGetSizeInPixels(MfcDialogTemplateCompat& dialog_template,
    SIZE& size);
bool IsDialogTemplateExtended(const DLGTEMPLATE* dialog_template);
unsigned DialogTemplateFontHeaderBytes(bool extended);
const WORD* SkipWideString(const WORD* text);
void AbbreviatePathName(char* path, int max_chars, bool keep_at_least_name);
MfcRecentFileListCompat& ConstructRecentFileList(
    MfcRecentFileListCompat& recent, unsigned start, const char* section_name,
    const char* entry_format, int max_size, int max_display_length);
void DestroyRecentFileList(MfcRecentFileListCompat& recent);
void RecentFileListAdd(MfcRecentFileListCompat& recent, const char* path);
void RecentFileListRemove(MfcRecentFileListCompat& recent, int index);
bool RecentFileListGetDisplayName(MfcRecentFileListCompat& recent,
    std::string& display, int index, const char* directory,
    int directory_length, bool keep_at_least_name);
void RecentFileListUpdateMenu(MfcRecentFileListCompat& recent,
    MfcCmdUICompat& cmd_ui);
void RecentFileListWriteList(MfcRecentFileListCompat& recent);
void RecentFileListReadList(MfcRecentFileListCompat& recent);
MfcRecentFileListCompat* DeleteRecentFileListScalarDtor(
    MfcRecentFileListCompat* recent, unsigned flags);
void* DeleteCStringVectorHelper(MfcCStringCompat* values, unsigned flags);
MfcFileCompat& ConstructFileDefault(MfcFileCompat& file);
MfcFileCompat& ConstructFileFromHandle(MfcFileCompat& file, HANDLE handle);
MfcFileCompat& ConstructFileFromPath(MfcFileCompat& file, const char* path,
    unsigned open_flags);
MfcFileCompat* DuplicateFileCompat(const MfcFileCompat& file);
bool FileOpen(MfcFileCompat& file, const char* path, unsigned open_flags,
    MfcFileExceptionCompat* exception = nullptr);
unsigned FileRead(MfcFileCompat& file, void* buffer, unsigned bytes);
void FileWrite(MfcFileCompat& file, const void* buffer, unsigned bytes);
void FileWriteHuge(MfcFileCompat& file, const void* buffer, unsigned long bytes);
unsigned long FileSeek(MfcFileCompat& file, long offset, unsigned origin);
unsigned long FileSeekToEnd(MfcFileCompat& file);
void FileSeekToBegin(MfcFileCompat& file);
unsigned long FileGetPosition(MfcFileCompat& file);
void FileFlush(MfcFileCompat& file);
void FileClose(MfcFileCompat& file);
void FileDestructor(MfcFileCompat& file);
void FileAbort(MfcFileCompat& file);
void FileLockRange(MfcFileCompat& file, unsigned long position,
    unsigned long count);
void FileUnlockRange(MfcFileCompat& file, unsigned long position,
    unsigned long count);
void FileSetLength(MfcFileCompat& file, unsigned long length);
unsigned long FileGetLength(MfcFileCompat& file);
unsigned FileGetBufferPtrUnsupported(unsigned command);
void FileRename(const char* old_name, const char* new_name);
void FileRemove(const char* path);
long AfxComCreateInstance(REFCLSID class_id, REFIID interface_id,
    void** object, DWORD class_context = CLSCTX_INPROC_SERVER,
    IUnknown* outer = nullptr);
long GetClassObject(REFCLSID class_id, REFIID interface_id, void** object);
std::string FormatGuidString(REFGUID guid);
bool QueryInProcServerFromClsid(const char* clsid_text, std::string& server);
bool ResolveShellLinkTarget(const char* link_path, char* target,
    unsigned target_chars);
bool AfxFullPath(char* full_path, const char* path);
std::string ExtractRootPath(const char* path);
bool FileNameCompare(const char* left, const char* right);
int GetFileTitleCompat(const char* path, char* title, unsigned title_chars);
std::string GetModuleShortFileName(HMODULE module);
void FileAssertValid(const MfcFileCompat& file);
MfcFileCompat* DeleteFileScalarDtor(MfcFileCompat* file, unsigned flags);
void FileStatusDump(const MfcFileStatusCompat& status);
std::string FileGetFileName(MfcFileCompat& file);
std::string FileGetFileTitleString(MfcFileCompat& file);
std::string FileGetFilePath(MfcFileCompat& file);
bool FileGetStatus(MfcFileCompat& file, MfcFileStatusCompat& status);
bool FileGetStatusByPath(const char* path, MfcFileStatusCompat& status);
FILETIME MfcTimeToFileTime(const MfcTimeCompat& time);
void FileSetStatus(const char* path, const MfcFileStatusCompat& status);
bool MemoryFileGetStatus(const MfcFileCompat& file, MfcFileStatusCompat& status);
void CDCAssertValid(const MfcCDCCompat& dc);
std::unordered_map<HDC, MfcCDCCompat*>* GetTempDCHandleMap(bool create);
MfcCDCCompat* CDCFromHandle(HDC handle);
HDC CDCGetSafeHdc(const MfcCDCCompat* dc);
bool CDCAttach(MfcCDCCompat& dc, HDC handle);
void CDCSetAttribDC(MfcCDCCompat& dc, HDC handle);
void CDCSetOutputDC(MfcCDCCompat& dc, HDC handle);
void CDCReleaseAttribDC(MfcCDCCompat& dc);
void CDCReleaseOutputDC(MfcCDCCompat& dc);
MfcCWndCompat* CDCGetWindow(MfcCDCCompat& dc);
bool CDCIsPrinting(const MfcCDCCompat& dc);
bool CDCCreateDC(MfcCDCCompat& dc, LPCSTR driver, LPCSTR device,
    LPCSTR output, const DEVMODEA* init_data);
bool CDCCreateDCInline(MfcCDCCompat& dc, LPCSTR driver, LPCSTR device,
    LPCSTR output, const DEVMODEA* init_data);
bool CDCCreateIC(MfcCDCCompat& dc, LPCSTR driver, LPCSTR device,
    LPCSTR output, const DEVMODEA* init_data);
bool CDCCreateICInline(MfcCDCCompat& dc, LPCSTR driver, LPCSTR device,
    LPCSTR output, const DEVMODEA* init_data);
bool CDCCreateCompatibleDC(MfcCDCCompat& dc, const MfcCDCCompat* source);
int CDCExcludeUpdateRgn(MfcCDCCompat& dc, const MfcCWndCompat& window);
int CDCExcludeUpdateRgnInline(MfcCDCCompat& dc, const MfcCWndCompat& window);
int CDCGetDeviceCaps(const MfcCDCCompat& dc, int index);
int CDCGetDeviceCapsInline(const MfcCDCCompat& dc, int index);
POINT CDCGetBrushOrg(const MfcCDCCompat& dc);
POINT CDCGetBrushOrgInline(const MfcCDCCompat& dc);
POINT CDCSetBrushOrg(MfcCDCCompat& dc, int x, int y);
POINT CDCSetBrushOrgXYInline(MfcCDCCompat& dc, int x, int y);
POINT CDCSetBrushOrgPoint(MfcCDCCompat& dc, POINT point);
POINT CDCSetBrushOrgPointInline(MfcCDCCompat& dc, POINT point);
int CDCEnumObjects(const MfcCDCCompat& dc, int object_type,
    GOBJENUMPROC proc, LPARAM data);
int CDCEnumObjectsInline(const MfcCDCCompat& dc, int object_type,
    GOBJENUMPROC proc, LPARAM data);
DOCINFOA& ConstructDocInfo(DOCINFOA& info);
int CDCStartDocName(MfcCDCCompat& dc, const char* document_name);
bool MetaFileDCCreate(MfcCDCCompat& dc, const char* file_name);
HMETAFILE MetaFileDCClose(MfcCDCCompat& dc);
int CDCSaveDC(MfcCDCCompat& dc);
BOOL CDCRestoreDC(MfcCDCCompat& dc, int saved_dc);
HGDIOBJ CDCSelectObjectRaw(HDC dc, HGDIOBJ object);
HGDIOBJ CDCSelectStockObject(MfcCDCCompat& dc, int stock_object);
HGDIOBJ CDCSelectPen(MfcCDCCompat& dc, HGDIOBJ pen);
HGDIOBJ CDCSelectPenInline(MfcCDCCompat& dc, HGDIOBJ pen);
HGDIOBJ CDCSelectBrush(MfcCDCCompat& dc, HGDIOBJ brush);
HGDIOBJ CDCSelectBrushInline(MfcCDCCompat& dc, HGDIOBJ brush);
HGDIOBJ CDCSelectFont(MfcCDCCompat& dc, HGDIOBJ font);
HGDIOBJ CDCSelectBitmap(MfcCDCCompat& dc, HGDIOBJ bitmap);
HGDIOBJ CDCSelectGdiObject(MfcCDCCompat& dc, HGDIOBJ object);
HGDIOBJ CDCSelectGdiObjectInline(MfcCDCCompat& dc, HGDIOBJ object);
HPALETTE CDCSelectPalette(MfcCDCCompat& dc, HPALETTE palette, BOOL force_background);
COLORREF CDCGetNearestColor(const MfcCDCCompat& dc, COLORREF color);
COLORREF CDCGetNearestColorInline(const MfcCDCCompat& dc, COLORREF color);
UINT CDCRealizePalette(MfcCDCCompat& dc);
UINT CDCRealizePaletteInline(MfcCDCCompat& dc);
BOOL CDCUpdateColors(MfcCDCCompat& dc);
BOOL CDCUpdateColorsInline(MfcCDCCompat& dc);
COLORREF CDCGetBkColor(const MfcCDCCompat& dc);
COLORREF CDCGetBkColorInline(const MfcCDCCompat& dc);
int CDCGetBkMode(const MfcCDCCompat& dc);
int CDCGetBkModeInline(const MfcCDCCompat& dc);
int CDCGetPolyFillMode(const MfcCDCCompat& dc);
int CDCGetPolyFillModeInline(const MfcCDCCompat& dc);
int CDCGetROP2(const MfcCDCCompat& dc);
int CDCGetROP2Inline(const MfcCDCCompat& dc);
int CDCGetStretchBltMode(const MfcCDCCompat& dc);
COLORREF CDCGetTextColor(const MfcCDCCompat& dc);
COLORREF CDCGetTextColorInline(const MfcCDCCompat& dc);
int CDCGetMapMode(const MfcCDCCompat& dc);
int CDCGetMapModeInline(const MfcCDCCompat& dc);
COLORREF CDCSetBkColor(MfcCDCCompat& dc, COLORREF color);
int CDCSetBkMode(MfcCDCCompat& dc, int mode);
int CDCSetPolyFillMode(MfcCDCCompat& dc, int mode);
int CDCSetROP2(MfcCDCCompat& dc, int mode);
int CDCSetStretchBltMode(MfcCDCCompat& dc, int mode);
COLORREF CDCSetTextColor(MfcCDCCompat& dc, COLORREF color);
int CDCSetMapMode(MfcCDCCompat& dc, int mode);
POINT CDCGetViewportOrg(const MfcCDCCompat& dc);
POINT CDCGetViewportOrgInline(const MfcCDCCompat& dc);
SIZE CDCGetViewportExt(const MfcCDCCompat& dc);
SIZE CDCGetViewportExtInline(const MfcCDCCompat& dc);
POINT CDCGetWindowOrg(const MfcCDCCompat& dc);
POINT CDCGetWindowOrgInline(const MfcCDCCompat& dc);
SIZE CDCGetWindowExt(const MfcCDCCompat& dc);
SIZE CDCGetWindowExtInline(const MfcCDCCompat& dc);
POINT CDCSetViewportOrg(MfcCDCCompat& dc, int x, int y);
POINT CDCSetViewportOrgXYInline(MfcCDCCompat& dc, int x, int y);
POINT CDCSetViewportOrgPoint(MfcCDCCompat& dc, POINT point);
POINT CDCOffsetViewportOrg(MfcCDCCompat& dc, int x, int y);
POINT CDCOffsetViewportOrgXYInline(MfcCDCCompat& dc, int x, int y);
POINT CDCOffsetViewportOrgPoint(MfcCDCCompat& dc, POINT point);
SIZE CDCSetViewportExt(MfcCDCCompat& dc, int x, int y);
SIZE CDCScaleViewportExt(MfcCDCCompat& dc, int x_num, int x_den,
    int y_num, int y_den);
POINT CDCSetWindowOrg(MfcCDCCompat& dc, int x, int y);
POINT CDCSetWindowOrgXYInline(MfcCDCCompat& dc, int x, int y);
POINT CDCSetWindowOrgPoint(MfcCDCCompat& dc, POINT point);
POINT CDCOffsetWindowOrg(MfcCDCCompat& dc, int x, int y);
POINT CDCOffsetWindowOrgXYInline(MfcCDCCompat& dc, int x, int y);
POINT CDCOffsetWindowOrgPoint(MfcCDCCompat& dc, POINT point);
SIZE CDCSetWindowExt(MfcCDCCompat& dc, int x, int y);
SIZE CDCScaleWindowExt(MfcCDCCompat& dc, int x_num, int x_den,
    int y_num, int y_den);
BOOL CDCDPtoLP(MfcCDCCompat& dc, POINT* points, int count);
BOOL CDCDPtoLPPointsInline(MfcCDCCompat& dc, POINT* points, int count);
BOOL CDCDPtoLPRect(MfcCDCCompat& dc, RECT& rect);
BOOL CDCDPtoLPRectInline(MfcCDCCompat& dc, RECT& rect);
BOOL CDCLPtoDP(MfcCDCCompat& dc, POINT* points, int count);
BOOL CDCLPtoDPPointsInline(MfcCDCCompat& dc, POINT* points, int count);
BOOL CDCLPtoDPRect(MfcCDCCompat& dc, RECT& rect);
BOOL CDCLPtoDPRectInline(MfcCDCCompat& dc, RECT& rect);
void CDCGetClipBox(MfcCDCCompat& dc, RECT& rect);
int CDCSelectClipRgn(MfcCDCCompat& dc, HRGN region);
int CDCExcludeClipRect(MfcCDCCompat& dc, int left, int top, int right,
    int bottom);
int CDCExcludeClipRectIndirect(MfcCDCCompat& dc, const RECT& rect);
int CDCIntersectClipRect(MfcCDCCompat& dc, int left, int top, int right,
    int bottom);
int CDCIntersectClipRectIndirect(MfcCDCCompat& dc, const RECT& rect);
int CDCOffsetClipRgn(MfcCDCCompat& dc, int x, int y);
int CDCOffsetClipRgnPoint(MfcCDCCompat& dc, POINT point);
BOOL CDCFillRgn(MfcCDCCompat& dc, HRGN region, HBRUSH brush);
BOOL CDCFillRgnInline(MfcCDCCompat& dc, HRGN region, HBRUSH brush);
BOOL CDCFrameRgn(MfcCDCCompat& dc, HRGN region, HBRUSH brush, int width,
    int height);
BOOL CDCFrameRgnInline(MfcCDCCompat& dc, HRGN region, HBRUSH brush, int width,
    int height);
BOOL CDCInvertRgn(MfcCDCCompat& dc, HRGN region);
BOOL CDCInvertRgnInline(MfcCDCCompat& dc, HRGN region);
BOOL CDCPaintRgn(MfcCDCCompat& dc, HRGN region);
BOOL CDCPaintRgnInline(MfcCDCCompat& dc, HRGN region);
BOOL CDCPtVisible(MfcCDCCompat& dc, int x, int y);
BOOL CDCPtVisibleXYInline(MfcCDCCompat& dc, int x, int y);
BOOL CDCPtVisiblePoint(MfcCDCCompat& dc, POINT point);
BOOL CDCPtVisiblePointInline(MfcCDCCompat& dc, POINT point);
BOOL CDCRectVisible(MfcCDCCompat& dc, const RECT& rect);
BOOL CDCRectVisibleInline(MfcCDCCompat& dc, const RECT& rect);
POINT CDCGetCurrentPosition(const MfcCDCCompat& dc);
POINT CDCGetCurrentPositionInline(const MfcCDCCompat& dc);
POINT CDCMoveTo(MfcCDCCompat& dc, int x, int y);
POINT CDCMoveToXYInline(MfcCDCCompat& dc, int x, int y);
POINT CDCMoveToPoint(MfcCDCCompat& dc, POINT point);
void CDCLineTo(MfcCDCCompat& dc, int x, int y);
BOOL CDCLineToXYInline(MfcCDCCompat& dc, int x, int y);
BOOL CDCArc(MfcCDCCompat& dc, int left, int top, int right, int bottom,
    int x_start, int y_start, int x_end, int y_end);
BOOL CDCArcInline(MfcCDCCompat& dc, int left, int top, int right, int bottom,
    int x_start, int y_start, int x_end, int y_end);
BOOL CDCArcRect(MfcCDCCompat& dc, const RECT& rect, int x_start, int y_start,
    int x_end, int y_end);
BOOL CDCArcRectInline(MfcCDCCompat& dc, const RECT& rect, int x_start,
    int y_start, int x_end, int y_end);
BOOL CDCPolyline(MfcCDCCompat& dc, const POINT* points, int count);
BOOL CDCPolylineInline(MfcCDCCompat& dc, const POINT* points, int count);
int CDCFillRect(MfcCDCCompat& dc, const RECT& rect, HBRUSH brush);
int CDCFillRectInline(MfcCDCCompat& dc, const RECT& rect, HBRUSH brush);
int CDCFrameRect(MfcCDCCompat& dc, const RECT& rect, HBRUSH brush);
int CDCFrameRectInline(MfcCDCCompat& dc, const RECT& rect, HBRUSH brush);
BOOL CDCInvertRect(MfcCDCCompat& dc, const RECT& rect);
BOOL CDCInvertRectInline(MfcCDCCompat& dc, const RECT& rect);
BOOL CDCDrawIcon(MfcCDCCompat& dc, int x, int y, HICON icon);
BOOL CDCDrawIconXYInline(MfcCDCCompat& dc, int x, int y, HICON icon);
BOOL CDCDrawIconPoint(MfcCDCCompat& dc, POINT point, HICON icon);
BOOL CDCDrawIconPointInline(MfcCDCCompat& dc, POINT point, HICON icon);
BOOL CDCDrawEdge(MfcCDCCompat& dc, RECT& rect, UINT edge, UINT flags);
BOOL CDCDrawEdgeInline(MfcCDCCompat& dc, RECT& rect, UINT edge, UINT flags);
BOOL CDCDrawFrameControl(MfcCDCCompat& dc, RECT& rect, UINT type, UINT state);
BOOL CDCDrawFrameControlInline(MfcCDCCompat& dc, RECT& rect, UINT type,
    UINT state);
BOOL CDCDrawStateIconBrushInline(MfcCDCCompat& dc, int x, int y, int width,
    int height, HBITMAP bitmap, UINT flags, HBRUSH brush);
BOOL CDCDrawStateIconInline(MfcCDCCompat& dc, int x, int y, int width,
    int height, const MfcGdiObjectCompat* bitmap, UINT flags);
BOOL CDCDrawStateTextBrushInline(MfcCDCCompat& dc, int x, int y, int width,
    int height, HICON icon, UINT flags, HBRUSH brush);
BOOL CDCDrawStateTextInline(MfcCDCCompat& dc, int x, int y, int width,
    int height, HICON icon, UINT flags);
BOOL CDCDrawStateBitmapBrushInline(MfcCDCCompat& dc, int x, int y, int width,
    int height, const char* text, UINT flags, bool prefix_text,
    UINT text_length, HBRUSH brush);
BOOL CDCDrawStateBitmapInline(MfcCDCCompat& dc, int x, int y, int width,
    int height, const char* text, UINT flags, bool prefix_text,
    UINT text_length);
BOOL CDCDrawStateProcBrushInline(MfcCDCCompat& dc, int x, int y, int width,
    int height, DRAWSTATEPROC proc, LPARAM data, UINT flags, HBRUSH brush);
BOOL CDCDrawStateProcInline(MfcCDCCompat& dc, int x, int y, int width,
    int height, DRAWSTATEPROC proc, LPARAM data, UINT flags);
BOOL CDCChord(MfcCDCCompat& dc, int left, int top, int right, int bottom,
    int x_start, int y_start, int x_end, int y_end);
BOOL CDCChordInline(MfcCDCCompat& dc, int left, int top, int right, int bottom,
    int x_start, int y_start, int x_end, int y_end);
BOOL CDCChordRect(MfcCDCCompat& dc, const RECT& rect, int x_start,
    int y_start, int x_end, int y_end);
BOOL CDCChordRectInline(MfcCDCCompat& dc, const RECT& rect, int x_start,
    int y_start, int x_end, int y_end);
BOOL CDCDrawFocusRect(MfcCDCCompat& dc, const RECT& rect);
BOOL CDCDrawFocusRectInline(MfcCDCCompat& dc, const RECT& rect);
BOOL CDCEllipse(MfcCDCCompat& dc, int left, int top, int right, int bottom);
BOOL CDCEllipseInline(MfcCDCCompat& dc, int left, int top, int right,
    int bottom);
BOOL CDCEllipseRect(MfcCDCCompat& dc, const RECT& rect);
BOOL CDCEllipseRectInline(MfcCDCCompat& dc, const RECT& rect);
BOOL CDCPie(MfcCDCCompat& dc, int left, int top, int right, int bottom,
    int x_start, int y_start, int x_end, int y_end);
BOOL CDCPieInline(MfcCDCCompat& dc, int left, int top, int right, int bottom,
    int x_start, int y_start, int x_end, int y_end);
BOOL CDCPieRect(MfcCDCCompat& dc, const RECT& rect, int x_start,
    int y_start, int x_end, int y_end);
BOOL CDCPieRectInline(MfcCDCCompat& dc, const RECT& rect, int x_start,
    int y_start, int x_end, int y_end);
BOOL CDCPolygon(MfcCDCCompat& dc, const POINT* points, int count);
BOOL CDCPolygonInline(MfcCDCCompat& dc, const POINT* points, int count);
BOOL CDCPolyPolygon(MfcCDCCompat& dc, const POINT* points,
    const INT* poly_counts, int count);
BOOL CDCPolyPolygonInline(MfcCDCCompat& dc, const POINT* points,
    const INT* poly_counts, int count);
BOOL CDCRectangle(MfcCDCCompat& dc, int left, int top, int right,
    int bottom);
BOOL CDCRectangleInline(MfcCDCCompat& dc, int left, int top, int right,
    int bottom);
BOOL CDCRectangleRect(MfcCDCCompat& dc, const RECT& rect);
BOOL CDCRectangleRectInline(MfcCDCCompat& dc, const RECT& rect);
BOOL CDCRoundRect(MfcCDCCompat& dc, int left, int top, int right, int bottom,
    int width, int height);
BOOL CDCRoundRectInline(MfcCDCCompat& dc, int left, int top, int right,
    int bottom, int width, int height);
BOOL CDCRoundRectRect(MfcCDCCompat& dc, const RECT& rect, POINT point);
BOOL CDCRoundRectRectInline(MfcCDCCompat& dc, const RECT& rect, POINT point);
BOOL CDCPatBlt(MfcCDCCompat& dc, int x, int y, int width, int height,
    DWORD raster_op);
BOOL CDCPatBltInline(MfcCDCCompat& dc, int x, int y, int width, int height,
    DWORD raster_op);
BOOL CDCBitBlt(MfcCDCCompat& dc, int x, int y, int width, int height,
    const MfcCDCCompat& source, int src_x, int src_y, DWORD raster_op);
BOOL CDCBitBltInline(MfcCDCCompat& dc, int x, int y, int width, int height,
    const MfcCDCCompat& source, int src_x, int src_y, DWORD raster_op);
BOOL CDCStretchBlt(MfcCDCCompat& dc, int x, int y, int width, int height,
    const MfcCDCCompat& source, int src_x, int src_y, int src_width,
    int src_height, DWORD raster_op);
BOOL CDCStretchBltInline(MfcCDCCompat& dc, int x, int y, int width,
    int height, const MfcCDCCompat& source, int src_x, int src_y,
    int src_width, int src_height, DWORD raster_op);
COLORREF CDCGetPixel(MfcCDCCompat& dc, int x, int y);
COLORREF CDCGetPixelXYInline(MfcCDCCompat& dc, int x, int y);
COLORREF CDCGetPixelPoint(MfcCDCCompat& dc, POINT point);
COLORREF CDCGetPixelPointInline(MfcCDCCompat& dc, POINT point);
COLORREF CDCSetPixel(MfcCDCCompat& dc, int x, int y, COLORREF color);
COLORREF CDCSetPixelXYInline(MfcCDCCompat& dc, int x, int y, COLORREF color);
COLORREF CDCSetPixelPoint(MfcCDCCompat& dc, POINT point, COLORREF color);
COLORREF CDCSetPixelPointInline(MfcCDCCompat& dc, POINT point,
    COLORREF color);
BOOL CDCFloodFill(MfcCDCCompat& dc, int x, int y, COLORREF color);
BOOL CDCFloodFillInline(MfcCDCCompat& dc, int x, int y, COLORREF color);
BOOL CDCExtFloodFill(MfcCDCCompat& dc, int x, int y, COLORREF color,
    UINT fill_type);
BOOL CDCExtFloodFillInline(MfcCDCCompat& dc, int x, int y, COLORREF color,
    UINT fill_type);
BOOL CDCTextOutChars(MfcCDCCompat& dc, int x, int y, const char* text,
    int count);
BOOL CDCTextOutCharsInline(MfcCDCCompat& dc, int x, int y,
    const char* text, int count);
BOOL CDCExtTextOutChars(MfcCDCCompat& dc, int x, int y, UINT options,
    const RECT* rect, const char* text, UINT count, const INT* dx);
BOOL CDCExtTextOutCharsInline(MfcCDCCompat& dc, int x, int y, UINT options,
    const RECT* rect, const char* text, UINT count, const INT* dx);
SIZE CDCTabbedTextOutChars(MfcCDCCompat& dc, int x, int y,
    const char* text, int count, int tab_count, const INT* tab_positions,
    int tab_origin);
SIZE CDCTabbedTextOutCharsInline(MfcCDCCompat& dc, int x, int y,
    const char* text, int count, int tab_count, const INT* tab_positions,
    int tab_origin);
int CDCDrawTextChars(MfcCDCCompat& dc, const char* text, int count,
    RECT& rect, UINT format);
int CDCDrawTextCharsInline(MfcCDCCompat& dc, const char* text, int count,
    RECT& rect, UINT format);
SIZE CDCGetTextExtentChars(MfcCDCCompat& dc, const char* text, int count);
SIZE CDCGetTextExtentCharsInline(MfcCDCCompat& dc, const char* text,
    int count);
SIZE CDCGetOutputTextExtentChars(MfcCDCCompat& dc, const char* text,
    int count);
SIZE CDCGetOutputTextExtentCharsInline(MfcCDCCompat& dc, const char* text,
    int count);
SIZE CDCGetTabbedTextExtentChars(MfcCDCCompat& dc, const char* text,
    int count, int tab_count, const INT* tab_positions);
SIZE CDCGetTabbedTextExtentCharsInline(MfcCDCCompat& dc, const char* text,
    int count, int tab_count, const INT* tab_positions);
SIZE CDCGetTabbedTextExtentString(MfcCDCCompat& dc,
    const MfcCStringCompat& text, int tab_count, const INT* tab_positions);
SIZE CDCGetTabbedTextExtentCStringInline(MfcCDCCompat& dc,
    const MfcCStringCompat& text, int tab_count, const INT* tab_positions);
SIZE CDCGetOutputTabbedTextExtentChars(MfcCDCCompat& dc, const char* text,
    int count, int tab_count, const INT* tab_positions);
SIZE CDCGetOutputTabbedTextExtentCharsInline(MfcCDCCompat& dc,
    const char* text, int count, int tab_count, const INT* tab_positions);
SIZE CDCGetOutputTabbedTextExtentString(MfcCDCCompat& dc,
    const MfcCStringCompat& text, int tab_count, const INT* tab_positions);
SIZE CDCGetOutputTabbedTextExtentCStringInline(MfcCDCCompat& dc,
    const MfcCStringCompat& text, int tab_count, const INT* tab_positions);
BOOL CDCGrayString(MfcCDCCompat& dc, HBRUSH brush, GRAYSTRINGPROC proc,
    LPARAM data, int count, int x, int y, int width, int height);
BOOL CDCGrayStringInline(MfcCDCCompat& dc, HBRUSH brush, GRAYSTRINGPROC proc,
    LPARAM data, int count, int x, int y, int width, int height);
UINT CDCGetTextAlign(const MfcCDCCompat& dc);
UINT CDCGetTextAlignInline(const MfcCDCCompat& dc);
int CDCGetTextFace(const MfcCDCCompat& dc, int count, char* face_name);
int CDCGetTextFaceInline(const MfcCDCCompat& dc, int count, char* face_name);
std::string CDCGetTextFaceString(const MfcCDCCompat& dc);
std::string CDCGetTextFaceCStringInline(const MfcCDCCompat& dc);
BOOL CDCGetTextMetrics(const MfcCDCCompat& dc, TEXTMETRICA& metrics);
BOOL CDCGetTextMetricsInline(const MfcCDCCompat& dc, TEXTMETRICA& metrics);
BOOL CDCGetOutputTextMetrics(const MfcCDCCompat& dc, TEXTMETRICA& metrics);
BOOL CDCGetOutputTextMetricsInline(const MfcCDCCompat& dc,
    TEXTMETRICA& metrics);
int CDCGetTextCharacterExtra(const MfcCDCCompat& dc);
int CDCGetTextCharacterExtraInline(const MfcCDCCompat& dc);
BOOL CDCGetCharWidth(const MfcCDCCompat& dc, UINT first, UINT last,
    int* widths);
BOOL CDCGetCharWidthInline(const MfcCDCCompat& dc, UINT first, UINT last,
    int* widths);
BOOL CDCGetOutputCharWidth(const MfcCDCCompat& dc, UINT first, UINT last,
    int* widths);
BOOL CDCGetOutputCharWidthInline(const MfcCDCCompat& dc, UINT first,
    UINT last, int* widths);
SIZE CDCGetAspectRatioFilter(const MfcCDCCompat& dc);
SIZE CDCGetAspectRatioFilterInline(const MfcCDCCompat& dc);
BOOL CDCScrollDC(MfcCDCCompat& dc, int dx, int dy, const RECT* scroll,
    const RECT* clip, HRGN update_region, RECT* update_rect);
BOOL CDCScrollDCInline(MfcCDCCompat& dc, int dx, int dy, const RECT* scroll,
    const RECT* clip, const MfcGdiObjectCompat* update_region,
    RECT* update_rect);
int CDCEscape(MfcCDCCompat& dc, int escape, int input_size,
    const char* input, void* output);
int CDCEscapeInline(MfcCDCCompat& dc, int escape, int input_size,
    const char* input, void* output);
UINT CDCSetBoundsRect(MfcCDCCompat& dc, const RECT* bounds, UINT flags);
UINT CDCSetBoundsRectInline(MfcCDCCompat& dc, const RECT* bounds, UINT flags);
UINT CDCGetBoundsRect(const MfcCDCCompat& dc, RECT& bounds, UINT flags);
UINT CDCGetBoundsRectInline(const MfcCDCCompat& dc, RECT& bounds, UINT flags);
bool CDCResetDC(MfcCDCCompat& dc, const DEVMODEA* devmode);
bool CDCResetDCInline(MfcCDCCompat& dc, const DEVMODEA* devmode);
UINT CDCGetOutlineTextMetrics(const MfcCDCCompat& dc, UINT bytes,
    OUTLINETEXTMETRICA* metrics);
UINT CDCGetOutlineTextMetricsInline(const MfcCDCCompat& dc, UINT bytes,
    OUTLINETEXTMETRICA* metrics);
BOOL CDCGetCharABCWidths(const MfcCDCCompat& dc, UINT first, UINT last,
    ABC* widths);
BOOL CDCGetCharABCWidthsInline(const MfcCDCCompat& dc, UINT first,
    UINT last, ABC* widths);
DWORD CDCGetFontData(const MfcCDCCompat& dc, DWORD table, DWORD offset,
    void* buffer, DWORD bytes);
DWORD CDCGetFontDataInline(const MfcCDCCompat& dc, DWORD table,
    DWORD offset, void* buffer, DWORD bytes);
DWORD CDCGetKerningPairs(const MfcCDCCompat& dc, DWORD pairs,
    KERNINGPAIR* output);
DWORD CDCGetKerningPairsInline(const MfcCDCCompat& dc, DWORD pairs,
    KERNINGPAIR* output);
DWORD CDCGetGlyphOutline(const MfcCDCCompat& dc, UINT character, UINT format,
    GLYPHMETRICS* metrics, DWORD bytes, void* buffer, const MAT2* matrix);
DWORD CDCGetGlyphOutlineInline(const MfcCDCCompat& dc, UINT character,
    UINT format, GLYPHMETRICS* metrics, DWORD bytes, void* buffer,
    const MAT2* matrix);
int CDCStartDoc(MfcCDCCompat& dc, const DOCINFOA& info);
int CDCStartDocInline(MfcCDCCompat& dc, const DOCINFOA& info);
int CDCStartPage(MfcCDCCompat& dc);
int CDCStartPageInline(MfcCDCCompat& dc);
int CDCEndPage(MfcCDCCompat& dc);
int CDCEndPageInline(MfcCDCCompat& dc);
int CDCSetAbortProcCompat(MfcCDCCompat& dc, ABORTPROC proc);
int CDCSetAbortProcInline(MfcCDCCompat& dc, ABORTPROC proc);
int CDCAbortDoc(MfcCDCCompat& dc);
int CDCAbortDocInline(MfcCDCCompat& dc);
int CDCEndDoc(MfcCDCCompat& dc);
int CDCEndDocInline(MfcCDCCompat& dc);
BOOL CDCMaskBlt(MfcCDCCompat& dc, int x, int y, int width, int height,
    const MfcCDCCompat& source, int src_x, int src_y, HBITMAP mask,
    int mask_x, int mask_y, DWORD rop);
BOOL CDCMaskBltInline(MfcCDCCompat& dc, int x, int y, int width,
    int height, const MfcCDCCompat& source, int src_x, int src_y,
    HBITMAP mask, int mask_x, int mask_y, DWORD rop);
BOOL CDCPlgBlt(MfcCDCCompat& dc, const POINT* points,
    const MfcCDCCompat& source, int src_x, int src_y, int width, int height,
    HBITMAP mask, int mask_x, int mask_y);
BOOL CDCPlgBltInline(MfcCDCCompat& dc, const POINT* points,
    const MfcCDCCompat& source, int src_x, int src_y, int width, int height,
    HBITMAP mask, int mask_x, int mask_y);
BOOL CDCSetPixelV(MfcCDCCompat& dc, int x, int y, COLORREF color);
BOOL CDCSetPixelVXYInline(MfcCDCCompat& dc, int x, int y, COLORREF color);
BOOL CDCSetPixelVPoint(MfcCDCCompat& dc, POINT point, COLORREF color);
BOOL CDCSetPixelVPointInline(MfcCDCCompat& dc, POINT point, COLORREF color);
BOOL CDCAngleArc(MfcCDCCompat& dc, int x, int y, DWORD radius,
    FLOAT start_angle, FLOAT sweep_angle);
BOOL CDCAngleArcInline(MfcCDCCompat& dc, int x, int y, DWORD radius,
    FLOAT start_angle, FLOAT sweep_angle);
BOOL CDCArcToRect(MfcCDCCompat& dc, const RECT& rect, int x_start,
    int y_start, int x_end, int y_end);
BOOL CDCArcToRectInline(MfcCDCCompat& dc, const RECT& rect, int x_start,
    int y_start, int x_end, int y_end);
int CDCGetArcDirection(const MfcCDCCompat& dc);
int CDCGetArcDirectionInline(const MfcCDCCompat& dc);
BOOL CDCPolyPolyline(MfcCDCCompat& dc, const POINT* points,
    const DWORD* poly_counts, DWORD count);
BOOL CDCPolyPolylineInline(MfcCDCCompat& dc, const POINT* points,
    const DWORD* poly_counts, DWORD count);
BOOL CDCGetColorAdjustmentCompat(const MfcCDCCompat& dc,
    COLORADJUSTMENT& adjustment);
BOOL CDCGetColorAdjustmentInline(const MfcCDCCompat& dc,
    COLORADJUSTMENT& adjustment);
HGDIOBJ CDCGetCurrentObjectCompat(const MfcCDCCompat& dc, UINT object_type);
MfcGdiObjectCompat* CDCGetCurrentBrush(const MfcCDCCompat& dc);
MfcGdiObjectCompat* CDCGetCurrentBrushInline(const MfcCDCCompat& dc);
MfcGdiObjectCompat* CDCGetCurrentPen(const MfcCDCCompat& dc);
MfcGdiObjectCompat* CDCGetCurrentPenInline(const MfcCDCCompat& dc);
MfcGdiObjectCompat* CDCGetCurrentBitmap(const MfcCDCCompat& dc);
MfcGdiObjectCompat* CDCGetCurrentBitmapInline(const MfcCDCCompat& dc);
MfcGdiObjectCompat* CDCGetCurrentPalette(const MfcCDCCompat& dc);
MfcGdiObjectCompat* CDCGetCurrentPaletteInline(const MfcCDCCompat& dc);
MfcGdiObjectCompat* CDCGetCurrentFont(const MfcCDCCompat& dc);
MfcGdiObjectCompat* CDCGetCurrentFontInline(const MfcCDCCompat& dc);
BOOL CDCPolyBezier(MfcCDCCompat& dc, const POINT* points, DWORD count);
BOOL CDCPolyBezierInline(MfcCDCCompat& dc, const POINT* points, DWORD count);
int CDCDrawEscape(MfcCDCCompat& dc, int escape, int input_size,
    const char* input);
int CDCDrawEscapeInline(MfcCDCCompat& dc, int escape, int input_size,
    const char* input);
int CDCExtEscape(MfcCDCCompat& dc, int escape, int input_size,
    const char* input, int output_size, char* output);
int CDCExtEscapeInline(MfcCDCCompat& dc, int escape, int input_size,
    const char* input, int output_size, char* output);
BOOL CDCGetCharABCWidthsFloat(const MfcCDCCompat& dc, UINT first, UINT last,
    ABCFLOAT* widths);
BOOL CDCGetCharABCWidthsFloatInline(const MfcCDCCompat& dc, UINT first,
    UINT last, ABCFLOAT* widths);
BOOL CDCGetCharWidthFloat(const MfcCDCCompat& dc, UINT first, UINT last,
    FLOAT* widths);
BOOL CDCGetCharWidthFloatInline(const MfcCDCCompat& dc, UINT first,
    UINT last, FLOAT* widths);
BOOL CDCAbortPath(MfcCDCCompat& dc);
BOOL CDCAbortPathInline(MfcCDCCompat& dc);
BOOL CDCBeginPath(MfcCDCCompat& dc);
BOOL CDCBeginPathInline(MfcCDCCompat& dc);
BOOL CDCCloseFigure(MfcCDCCompat& dc);
BOOL CDCCloseFigureInline(MfcCDCCompat& dc);
BOOL CDCEndPath(MfcCDCCompat& dc);
BOOL CDCEndPathInline(MfcCDCCompat& dc);
BOOL CDCFillPath(MfcCDCCompat& dc);
BOOL CDCFillPathInline(MfcCDCCompat& dc);
BOOL CDCFlattenPath(MfcCDCCompat& dc);
BOOL CDCFlattenPathInline(MfcCDCCompat& dc);
FLOAT CDCGetMiterLimit(MfcCDCCompat& dc);
FLOAT CDCGetMiterLimitInline(MfcCDCCompat& dc);
int CDCGetPath(MfcCDCCompat& dc, POINT* points, BYTE* types, int count);
int CDCGetPathInline(MfcCDCCompat& dc, POINT* points, BYTE* types,
    int count);
BOOL CDCSetMiterLimit(MfcCDCCompat& dc, FLOAT limit);
BOOL CDCSetMiterLimitInline(MfcCDCCompat& dc, FLOAT limit);
BOOL CDCStrokeAndFillPath(MfcCDCCompat& dc);
BOOL CDCStrokeAndFillPathInline(MfcCDCCompat& dc);
BOOL CDCStrokePath(MfcCDCCompat& dc);
BOOL CDCStrokePathInline(MfcCDCCompat& dc);
BOOL CDCWidenPath(MfcCDCCompat& dc);
BOOL CDCWidenPathInline(MfcCDCCompat& dc);
BOOL CDCGdiComment(MfcCDCCompat& dc, UINT bytes, const BYTE* data);
BOOL CDCGdiCommentInline(MfcCDCCompat& dc, UINT bytes, const BYTE* data);
BOOL CDCPlayEnhMetaFile(MfcCDCCompat& dc, HENHMETAFILE metafile,
    const RECT& bounds);
BOOL CDCPlayEnhMetaFileInline(MfcCDCCompat& dc, HENHMETAFILE metafile,
    const RECT& bounds);
SIZE CDCGetTextExtentString(MfcCDCCompat& dc, const MfcCStringCompat& text);
SIZE CDCGetTextExtentCStringInline(MfcCDCCompat& dc,
    const MfcCStringCompat& text);
SIZE CDCGetOutputTextExtentString(MfcCDCCompat& dc,
    const MfcCStringCompat& text);
SIZE CDCGetOutputTextExtentCStringInline(MfcCDCCompat& dc,
    const MfcCStringCompat& text);
BOOL CDCTextOutString(MfcCDCCompat& dc, int x, int y,
    const MfcCStringCompat& text);
BOOL CDCTextOutCStringInline(MfcCDCCompat& dc, int x, int y,
    const MfcCStringCompat& text);
BOOL CDCExtTextOutString(MfcCDCCompat& dc, int x, int y, UINT options,
    const RECT* rect, const MfcCStringCompat& text, const INT* dx);
BOOL CDCExtTextOutCStringInline(MfcCDCCompat& dc, int x, int y,
    UINT options, const RECT* rect, const MfcCStringCompat& text,
    const INT* dx);
SIZE CDCTabbedTextOutString(MfcCDCCompat& dc, int x, int y,
    const MfcCStringCompat& text, int tab_count, const INT* tab_positions,
    int tab_origin);
SIZE CDCTabbedTextOutCStringInline(MfcCDCCompat& dc, int x, int y,
    const MfcCStringCompat& text, int tab_count, const INT* tab_positions,
    int tab_origin);
int CDCDrawTextString(MfcCDCCompat& dc, const MfcCStringCompat& text,
    RECT& rect, UINT format);
int CDCDrawTextCStringInline(MfcCDCCompat& dc, const MfcCStringCompat& text,
    RECT& rect, UINT format);
UINT CDCSetTextAlign(MfcCDCCompat& dc, UINT flags);
BOOL CDCSetTextJustification(MfcCDCCompat& dc, int break_extra,
    int break_count);
int CDCSetTextCharacterExtra(MfcCDCCompat& dc, int extra);
DWORD CDCSetMapperFlags(MfcCDCCompat& dc, DWORD flags);
DWORD CDCGetLayoutCompat(MfcCDCCompat& dc);
DWORD CDCSetLayoutCompat(MfcCDCCompat& dc, DWORD layout);
void WindowScreenToClientRect(HWND window, RECT& rect);
void WindowClientToScreenRect(HWND window, RECT& rect);
BOOL CDCArcTo(MfcCDCCompat& dc, int left, int top, int right, int bottom,
    int x_start, int y_start, int x_end, int y_end);
int CDCSetArcDirection(MfcCDCCompat& dc, int direction);
BOOL CDCPolyDraw(MfcCDCCompat& dc, const POINT* points, const BYTE* types,
    int count);
BOOL CDCPolylineTo(MfcCDCCompat& dc, const POINT* points, DWORD count);
BOOL CDCSetColorAdjustmentCompat(MfcCDCCompat& dc,
    const COLORADJUSTMENT& adjustment);
BOOL CDCPolyBezierTo(MfcCDCCompat& dc, const POINT* points, DWORD count);
bool CDCSelectClipPathCompat(MfcCDCCompat& dc, int mode);
int CDCExtSelectClipRgn(MfcCDCCompat& dc, HRGN region, int mode);
int CALLBACK CDCMetaFileEnumProc(HDC dc, HANDLETABLE* table,
    METARECORD* record, int object_count, LPARAM data);
void CDCDeviceToHIMETRIC(MfcCDCCompat& dc, SIZE& size);
void CDCHIMETRICToDevice(MfcCDCCompat& dc, SIZE& size);
void DeleteHalftoneBrush();
void RegisterHalftoneBrushCleanupThunk();
void RegisterHalftoneBrushCleanup();
void CDCDPtoHIMETRIC(MfcCDCCompat* dc, SIZE& size);
void CDCHIMETRICtoDP(MfcCDCCompat* dc, SIZE& size);
void CDCLPtoHIMETRIC(MfcCDCCompat& dc, SIZE& size);
void CDCHIMETRICtoLP(MfcCDCCompat& dc, SIZE& size);
HBRUSH GetHalftoneBrush();
void CDCDrawDragRect(MfcCDCCompat& dc, const RECT& rect, SIZE size,
    const RECT* last_rect, SIZE last_size, HBRUSH brush,
    HBRUSH last_brush);
void CDCFillSolidRect(MfcCDCCompat& dc, const RECT& rect, COLORREF color);
void CDCFillSolidRectXY(MfcCDCCompat& dc, int x, int y, int width,
    int height, COLORREF color);
void Draw3dRect(MfcCDCCompat& dc, int x, int y, int width, int height,
    COLORREF top_left, COLORREF bottom_right);
bool FontCreatePointFont(MfcGdiObjectCompat& font, int point_size,
    const char* face_name, MfcCDCCompat* dc);
bool FontCreatePointFontIndirect(MfcGdiObjectCompat& font, LOGFONTA log_font,
    MfcCDCCompat* dc);
void DeleteRectTrackerStatics();
void RegisterRectTrackerStaticsThunk();
void RegisterRectTrackerStatics();
MfcRectTrackerCompat& ConstructRectTrackerDefault(
    MfcRectTrackerCompat& tracker);
MfcRectTrackerCompat& ConstructRectTracker(MfcRectTrackerCompat& tracker,
    const RECT& rect, UINT style);
void RectTrackerConstructCommon(MfcRectTrackerCompat& tracker);
void DestroyRectTracker(MfcRectTrackerCompat& tracker);
MfcRectTrackerCompat* DeleteRectTrackerScalarDtor(
    MfcRectTrackerCompat* tracker, unsigned flags);
void RectTrackerDraw(MfcRectTrackerCompat& tracker, MfcCDCCompat& dc);
bool RectTrackerSetCursor(MfcRectTrackerCompat& tracker,
    MfcCWndCompat& window, UINT hit_test);
int RectTrackerHitTest(MfcRectTrackerCompat& tracker, POINT point);
int RectTrackerNormalizeHit(MfcRectTrackerCompat& tracker, int hit);
void RectTrackerDrawTrackerRect(MfcRectTrackerCompat& tracker,
    const RECT& rect, MfcCWndCompat* clip_window, MfcCDCCompat& dc,
    MfcCWndCompat* window);
void RectTrackerAdjustRect(MfcRectTrackerCompat& tracker, int handle);
void RectTrackerGetTrueRect(MfcRectTrackerCompat& tracker, RECT& rect);
void RectTrackerOnChangedRect(MfcRectTrackerCompat& tracker);
void RectTrackerGetHandleRect(MfcRectTrackerCompat& tracker, int handle,
    RECT& rect);
int RectTrackerGetHandleSize(MfcRectTrackerCompat& tracker,
    const RECT* rect = nullptr);
int RectTrackerHitTestHandles(MfcRectTrackerCompat& tracker, POINT point);
bool RectTrackerTrackHandle(MfcRectTrackerCompat& tracker, int handle,
    MfcCWndCompat& window, POINT point, MfcCWndCompat* clip_window);
void RectTrackerGetModifyPointers(MfcRectTrackerCompat& tracker, int handle,
    LONG** x_ptr, LONG** y_ptr, LONG* fixed_x, LONG* fixed_y);
UINT RectTrackerGetHandleMask(MfcRectTrackerCompat& tracker);
void ClientDCAssertValid(const MfcWindowDCCompat& dc);
MfcWindowDCCompat& ConstructClientDC(MfcWindowDCCompat& dc, HWND window);
void DestroyClientDC(MfcWindowDCCompat& dc);
void WindowDCAssertValid(const MfcWindowDCCompat& dc);
MfcWindowDCCompat& ConstructWindowDC(MfcWindowDCCompat& dc, HWND window);
void DestroyWindowDC(MfcWindowDCCompat& dc);
void PaintDCAssertValid(const MfcWindowDCCompat& dc);
MfcWindowDCCompat& ConstructPaintDC(MfcWindowDCCompat& dc, HWND window);
void DestroyPaintDC(MfcWindowDCCompat& dc);
void DestroyCDC(MfcCDCCompat& dc);
void CDCDestructor(MfcCDCCompat& dc);
MfcCDCCompat* DeleteCDCScalarDtor(MfcCDCCompat* dc, unsigned flags);
MfcWindowDCCompat* DeleteClientDCScalarDtor(MfcWindowDCCompat* dc,
    unsigned flags);
MfcWindowDCCompat* DeleteWindowDCScalarDtor(MfcWindowDCCompat* dc,
    unsigned flags);
MfcWindowDCCompat* DeletePaintDCScalarDtor(MfcWindowDCCompat* dc,
    unsigned flags);
[[noreturn]] void ThrowMfcMemoryExceptionAlias();
void CGdiObjectAssertValid(const MfcGdiObjectCompat& object);
MfcHandleMapCompat* GetTempGdiObjectHandleMap(bool create);
MfcGdiObjectCompat* GdiObjectFromHandle(HGDIOBJ handle);
bool GdiObjectAttach(MfcGdiObjectCompat& object, HGDIOBJ handle);
HGDIOBJ GdiObjectDetach(MfcGdiObjectCompat& object);
BOOL GdiObjectDeleteObject(MfcGdiObjectCompat& object);
MfcGdiObjectCompat& ConstructGdiObjectCompat(MfcGdiObjectCompat& object);
void DestroyGdiObjectCompat(MfcGdiObjectCompat& object);
MfcGdiObjectCompat& ConstructPen(MfcGdiObjectCompat& pen);
void DestroyPen(MfcGdiObjectCompat& pen);
MfcGdiObjectCompat& ConstructBrush(MfcGdiObjectCompat& brush);
void DestroyBrush(MfcGdiObjectCompat& brush);
MfcGdiObjectCompat& ConstructFont(MfcGdiObjectCompat& font);
void DestroyFont(MfcGdiObjectCompat& font);
MfcGdiObjectCompat& ConstructBitmap(MfcGdiObjectCompat& bitmap);
void DestroyBitmap(MfcGdiObjectCompat& bitmap);
MfcGdiObjectCompat& ConstructPalette(MfcGdiObjectCompat& palette);
void DestroyPalette(MfcGdiObjectCompat& palette);
MfcGdiObjectCompat* DeletePenScalarDtor(MfcGdiObjectCompat* pen,
    unsigned flags);
MfcGdiObjectCompat* DeleteBrushScalarDtor(MfcGdiObjectCompat* brush,
    unsigned flags);
MfcGdiObjectCompat* DeleteFontScalarDtor(MfcGdiObjectCompat* font,
    unsigned flags);
MfcGdiObjectCompat* DeleteBitmapScalarDtor(MfcGdiObjectCompat* bitmap,
    unsigned flags);
MfcGdiObjectCompat* DeletePaletteScalarDtor(MfcGdiObjectCompat* palette,
    unsigned flags);
int GdiObjectGetObjectBytes(MfcGdiObjectCompat& object, int bytes,
    void* buffer);
BOOL GdiObjectUnrealize(MfcGdiObjectCompat& object);
DWORD GdiObjectGetObjectTypeCompat(const MfcGdiObjectCompat& object);
HGDIOBJ GdiObjectGetSafeHandle(const MfcGdiObjectCompat* object);
MfcGdiObjectCompat* GdiObjectFromHandleCompat(HGDIOBJ handle);
bool PenCreate(MfcGdiObjectCompat& pen, int style, int width,
    COLORREF color);
bool PenCreateIndirect(MfcGdiObjectCompat& pen, const LOGPEN& log_pen);
bool PenCreateExt(MfcGdiObjectCompat& pen, DWORD style, DWORD width,
    const LOGBRUSH& brush, DWORD style_count, const DWORD* style_bits);
int PenGetExtLogPen(MfcGdiObjectCompat& pen, EXTLOGPEN& log_pen);
int PenGetLogPen(MfcGdiObjectCompat& pen, LOGPEN& log_pen);
HPEN PenGetSafeHandle(const MfcGdiObjectCompat* pen);
MfcGdiObjectCompat* PenFromHandle(HPEN pen);
void DumpPenObject(const MfcGdiObjectCompat& pen);
bool BrushCreateSolid(MfcGdiObjectCompat& brush, COLORREF color);
bool BrushCreateHatch(MfcGdiObjectCompat& brush, int hatch, COLORREF color);
bool BrushCreateIndirect(MfcGdiObjectCompat& brush, const LOGBRUSH& log_brush);
bool BrushCreatePattern(MfcGdiObjectCompat& brush,
    const MfcGdiObjectCompat& bitmap);
bool BrushCreateDIBPattern(MfcGdiObjectCompat& brush, const void* packed_dib,
    UINT usage);
bool BrushCreateSysColor(MfcGdiObjectCompat& brush, int color_index);
bool BrushCreateDIBPatternFromGlobal(MfcGdiObjectCompat& brush,
    HGLOBAL global_dib, UINT usage);
int BrushGetLogBrush(MfcGdiObjectCompat& brush, LOGBRUSH& log_brush);
HBRUSH BrushGetSafeHandle(const MfcGdiObjectCompat* brush);
MfcGdiObjectCompat* BrushFromHandle(HBRUSH brush);
void DumpBrushObject(const MfcGdiObjectCompat& brush);
bool FontCreateIndirect(MfcGdiObjectCompat& font, const LOGFONTA& log_font);
bool FontCreate(MfcGdiObjectCompat& font, int height, int width,
    int escapement, int orientation, int weight, BYTE italic,
    BYTE underline, BYTE strike_out, BYTE char_set, BYTE output_precision,
    BYTE clip_precision, BYTE quality, BYTE pitch_and_family,
    const char* face_name);
int FontGetLogFont(MfcGdiObjectCompat& font, LOGFONTA& log_font);
HFONT FontGetSafeHandle(const MfcGdiObjectCompat* font);
MfcGdiObjectCompat* FontFromHandle(HFONT font);
void DumpFontObject(const MfcGdiObjectCompat& font);
bool BitmapCreate(MfcGdiObjectCompat& bitmap, int width, int height,
    UINT planes, UINT bit_count, const void* bits);
bool BitmapCreateIndirect(MfcGdiObjectCompat& bitmap, const BITMAP& info);
LONG BitmapSetBits(MfcGdiObjectCompat& bitmap, DWORD bytes, const void* bits);
LONG BitmapGetBits(MfcGdiObjectCompat& bitmap, LONG bytes, void* bits);
bool BitmapLoadResource(MfcGdiObjectCompat& bitmap, const char* name);
bool BitmapLoadResourceId(MfcGdiObjectCompat& bitmap, UINT id);
bool BitmapLoadMappedResource(MfcGdiObjectCompat& bitmap, INT_PTR id,
    UINT flags, LPCOLORMAP color_map, int color_count);
bool BitmapLoadOEM(MfcGdiObjectCompat& bitmap, UINT id);
bool BitmapCreateCompatible(MfcGdiObjectCompat& bitmap, HDC dc, int width,
    int height);
bool BitmapCreateDiscardable(MfcGdiObjectCompat& bitmap, HDC dc, int width,
    int height);
SIZE BitmapSetDimension(MfcGdiObjectCompat& bitmap, int width, int height);
SIZE BitmapGetDimension(MfcGdiObjectCompat& bitmap);
int BitmapGetObject(MfcGdiObjectCompat& bitmap, BITMAP& info);
HBITMAP BitmapGetSafeHandle(const MfcGdiObjectCompat* bitmap);
MfcGdiObjectCompat* BitmapFromHandle(HBITMAP bitmap);
void DumpBitmapObject(const MfcGdiObjectCompat& bitmap);
bool PaletteCreate(MfcGdiObjectCompat& palette, const LOGPALETTE& log_palette);
bool PaletteCreateHalftone(MfcGdiObjectCompat& palette, HDC dc);
UINT PaletteGetEntries(MfcGdiObjectCompat& palette, UINT start, UINT count,
    PALETTEENTRY* entries);
UINT PaletteSetEntries(MfcGdiObjectCompat& palette, UINT start, UINT count,
    const PALETTEENTRY* entries);
void PaletteAnimate(MfcGdiObjectCompat& palette, UINT start, UINT count,
    const PALETTEENTRY* entries);
UINT PaletteGetNearestIndex(MfcGdiObjectCompat& palette, COLORREF color);
BOOL PaletteResize(MfcGdiObjectCompat& palette, UINT count);
UINT PaletteGetEntryCount(MfcGdiObjectCompat& palette);
HPALETTE PaletteGetSafeHandle(const MfcGdiObjectCompat* palette);
MfcGdiObjectCompat* PaletteFromHandle(HPALETTE palette);
MfcGdiObjectCompat& ConstructRgn(MfcGdiObjectCompat& region);
void DestroyRgn(MfcGdiObjectCompat& region);
MfcGdiObjectCompat* DeleteRgnScalarDtor(MfcGdiObjectCompat* region,
    unsigned flags);
bool RgnCreateRect(MfcGdiObjectCompat& region, int left, int top,
    int right, int bottom);
bool RgnCreateRectIndirect(MfcGdiObjectCompat& region, const RECT& rect);
bool RgnCreateElliptic(MfcGdiObjectCompat& region, int left, int top,
    int right, int bottom);
bool RgnCreateEllipticIndirect(MfcGdiObjectCompat& region, const RECT& rect);
bool RgnCreatePolygon(MfcGdiObjectCompat& region, const POINT* points,
    int count, int mode);
bool RgnCreatePolyPolygon(MfcGdiObjectCompat& region, const POINT* points,
    const INT* counts, int polygon_count, int mode);
bool RgnCreateRoundRect(MfcGdiObjectCompat& region, int left, int top,
    int right, int bottom, int ellipse_width, int ellipse_height);
bool RgnCreateFromPath(MfcGdiObjectCompat& region, HDC dc);
bool RgnCreateExt(MfcGdiObjectCompat& region, const XFORM* transform,
    DWORD bytes, const RGNDATA* data);
DWORD RgnGetRegionData(MfcGdiObjectCompat& region, DWORD bytes,
    RGNDATA* data);
BOOL RgnSetRect(MfcGdiObjectCompat& region, int left, int top, int right,
    int bottom);
BOOL RgnSetRectIndirect(MfcGdiObjectCompat& region, const RECT& rect);
int RgnCombine(MfcGdiObjectCompat& region, const MfcGdiObjectCompat* left,
    const MfcGdiObjectCompat* right, int mode);
int RgnCopy(MfcGdiObjectCompat& region, const MfcGdiObjectCompat& source);
BOOL RgnEqual(MfcGdiObjectCompat& region, const MfcGdiObjectCompat& other);
int RgnOffsetXY(MfcGdiObjectCompat& region, int x, int y);
int RgnOffsetPoint(MfcGdiObjectCompat& region, POINT point);
int RgnGetBox(MfcGdiObjectCompat& region, RECT& rect);
BOOL RgnPtInXY(MfcGdiObjectCompat& region, int x, int y);
BOOL RgnPtInPoint(MfcGdiObjectCompat& region, POINT point);
BOOL RgnRectIn(MfcGdiObjectCompat& region, const RECT& rect);
HRGN RgnGetSafeHandle(const MfcGdiObjectCompat* region);
MfcGdiObjectCompat* RgnFromHandle(HRGN region);

const char* GetCommonDialogRuntimeClassName();
UINT_PTR CALLBACK MfcCommonDialogHookProc(HWND dialog, UINT message, WPARAM wparam,
    LPARAM lparam);
void DialogOnInitDoneUpdateData(HWND dialog);
void DialogOnHelpCommandDefault(HWND dialog);
void DialogDefaultMessageHandler(HWND dialog);
MfcFileDialogCompat& ConstructMfcFileDialog(MfcFileDialogCompat& dialog,
    bool open_dialog, const char* default_extension, const char* initial_file,
    DWORD flags, const char* filter, HWND owner);
int DoModalMfcFileDialog(MfcFileDialogCompat& dialog);
std::string GetMfcFileDialogPathName(const MfcFileDialogCompat& dialog);
std::string GetMfcFileDialogFileTitle(const MfcFileDialogCompat& dialog);
std::string GetMfcFileDialogFileName(const MfcFileDialogCompat& dialog);
std::string GetMfcFileDialogFileExt(const MfcFileDialogCompat& dialog);
std::string GetNextMfcFileDialogPathName(MfcFileDialogCompat& dialog,
    const char*& position);
void SetMfcFileDialogTemplate(MfcFileDialogCompat& dialog, unsigned old_template,
    unsigned new_template);
void OnFileDialogFolderChange(MfcFileDialogCompat& dialog);
void SetFileDialogControlText(MfcFileDialogCompat& dialog, int control_id,
    const char* text);
void HideFileDialogControl(MfcFileDialogCompat& dialog, int control_id);
void SetFileDialogDefaultExtension(MfcFileDialogCompat& dialog, const char* extension);
unsigned OnFileDialogShareViolation(MfcFileDialogCompat& dialog, const char* path);
unsigned OnFileDialogFileNameOK(MfcFileDialogCompat& dialog);
void OnFileDialogFolderChangeNotify(MfcFileDialogCompat& dialog);
void OnFileDialogHelp(MfcFileDialogCompat& dialog);
void OnFileDialogTypeChange(MfcFileDialogCompat& dialog);
void OnFileDialogInitDone(MfcFileDialogCompat& dialog);
void OnFileDialogListSelectionChanged(MfcFileDialogCompat& dialog);
bool RouteFileDialogNotify(MfcFileDialogCompat& dialog, const OFNOTIFYA& notify,
    LRESULT& result);
void DumpMfcFileDialog(const MfcFileDialogCompat& dialog);
void FileDialogSetTemplateIds(MfcFileDialogCompat& dialog,
    unsigned old_template, unsigned new_template);
DWORD PrintDialogExGetResultAction(const MfcPrintDialogExCompat& dialog);
bool PrintDialogPrintRange(const MfcPrintDialogCompat& dialog);
bool PrintDialogPrintSelection(const MfcPrintDialogCompat& dialog);
bool PrintDialogPrintAll(const MfcPrintDialogCompat& dialog);
bool PrintDialogPrintCollate(const MfcPrintDialogCompat& dialog);
unsigned PrintDialogGetFromPage(const MfcPrintDialogCompat& dialog);
unsigned PrintDialogGetToPage(const MfcPrintDialogCompat& dialog);
HDC PrintDialogGetPrinterDC(const MfcPrintDialogCompat& dialog);
void PrintInfoSetMinPage(MfcPrintInfoCompat& info, UINT page);
void PrintInfoSetMaxPage(MfcPrintInfoCompat& info, UINT page);
UINT PrintInfoGetMinPage(const MfcPrintInfoCompat& info);
UINT PrintInfoGetMaxPage(const MfcPrintInfoCompat& info);
UINT PrintInfoGetFromPage(const MfcPrintInfoCompat& info);
UINT PrintInfoGetToPage(const MfcPrintInfoCompat& info);
MfcCStringCompat FontDialogGetFaceName(const MfcFontDialogCompat& dialog);
MfcCStringCompat FontDialogGetStyleName(const MfcFontDialogCompat& dialog);
int FontDialogGetSize(const MfcFontDialogCompat& dialog);
int FontDialogGetWeight(const MfcFontDialogCompat& dialog);
bool FontDialogIsItalic(const MfcFontDialogCompat& dialog);
bool FontDialogIsStrikeOut(const MfcFontDialogCompat& dialog);
bool FontDialogIsBold(const MfcFontDialogCompat& dialog);
bool FontDialogIsUnderline(const MfcFontDialogCompat& dialog);
COLORREF FontDialogGetColor(const MfcFontDialogCompat& dialog);
const LOGFONTA* FontDialogGetCurrentFont(const MfcFontDialogCompat& dialog);
bool FindReplaceIsTerminating(const MfcFindReplaceDialogCompat& dialog);
MfcCStringCompat FindReplaceGetReplaceString(
    const MfcFindReplaceDialogCompat& dialog);
MfcCStringCompat FindReplaceGetFindString(
    const MfcFindReplaceDialogCompat& dialog);
bool FindReplaceSearchDown(const MfcFindReplaceDialogCompat& dialog);
bool FindReplaceFindNext(const MfcFindReplaceDialogCompat& dialog);
bool FindReplaceMatchCase(const MfcFindReplaceDialogCompat& dialog);
bool FindReplaceMatchWholeWord(const MfcFindReplaceDialogCompat& dialog);
bool FindReplaceReplaceCurrent(const MfcFindReplaceDialogCompat& dialog);
bool FindReplaceReplaceAll(const MfcFindReplaceDialogCompat& dialog);

void DeleteMiniDockFrameWindow(void* window, unsigned flags);
MfcBitmapButtonCompat& ConstructMfcBitmapButton(
    MfcBitmapButtonCompat& button);
void DestroyMfcBitmapButton(MfcBitmapButtonCompat& button);
void CBitmapButtonDestructor(MfcBitmapButtonCompat& button);
MfcBitmapButtonCompat* DeleteMfcBitmapButtonScalarDtor(
    MfcBitmapButtonCompat* button, unsigned flags);
bool LoadMfcBitmapButtonBitmaps(MfcBitmapButtonCompat& button, int normal_id,
    int selected_id = 0, int focus_id = 0, int disabled_id = 0);
bool BitmapButtonLoadBitmapsInline(MfcBitmapButtonCompat& button,
    int normal_id, int selected_id = 0, int focus_id = 0,
    int disabled_id = 0);
void SizeMfcBitmapButtonToContent(MfcBitmapButtonCompat& button);
bool AutoLoadMfcBitmapButton(MfcBitmapButtonCompat& button, HWND owner,
    int base_resource_id);
void DrawMfcBitmapButton(MfcBitmapButtonCompat& button, const DRAWITEMSTRUCT& item);

const char* GetColorDialogRuntimeClassName();
COLORREF* GetSavedCustomColorsCompat();
MfcColorDialogCompat& ConstructMfcColorDialog(MfcColorDialogCompat& dialog,
    COLORREF initial_color, DWORD flags, HWND owner);
int DoModalMfcColorDialog(MfcColorDialogCompat& dialog);
unsigned OnColorDialogColorOK(MfcColorDialogCompat& dialog);
void SetColorDialogCurrentColor(MfcColorDialogCompat& dialog, COLORREF color);
void ColorDialogDefaultMessageHandler(HWND dialog);
void DumpMfcColorDialog(const MfcColorDialogCompat& dialog);
void DeleteMfcNoTrackObjectBase(void* object, unsigned flags);
void DestroyMfcNoTrackObjectBase(void* object);
void DeleteMfcDockFrameWindow(void* object, unsigned flags);
void DestroyMfcDockFrameWindow(void* object);

bool CreateStaticControl(MfcStaticCompat& control, const char* caption,
    DWORD style, const RECT& bounds, HWND parent, UINT id);
void DestroyStaticControl(MfcStaticCompat& control);
bool CreateButtonControl(MfcButtonCompat& control, const char* caption,
    DWORD style, const RECT& bounds, HWND parent, UINT id);
void DestroyButtonControl(MfcButtonCompat& control);
int CWndGetCheckedRadioButton(MfcCWndCompat& parent, int first_id, int last_id);
void ButtonDrawItemDefault(DRAWITEMSTRUCT* draw_item);
bool ButtonOnChildNotify(MfcButtonCompat& control, UINT message,
    WPARAM wparam, LPARAM lparam, LRESULT* result);

bool CreateListBoxControl(MfcListBoxCompat& control, DWORD style,
    const RECT& bounds, HWND parent, UINT id);
void DestroyListBoxControl(MfcListBoxCompat& control);
void ListBoxDrawItemDefault(DRAWITEMSTRUCT* draw_item);
void ListBoxMeasureItemDefault(MEASUREITEMSTRUCT* measure_item);
int ListBoxCompareItemDefault(COMPAREITEMSTRUCT* compare_item);
void ListBoxDeleteItemDefault(DELETEITEMSTRUCT* delete_item);
int ListBoxVKeyToItemDefault(MfcListBoxCompat& control, UINT key, UINT index);
int ListBoxCharToItemDefault(MfcListBoxCompat& control, UINT ch, UINT index);
bool ListBoxOnChildNotify(MfcListBoxCompat& control, UINT message,
    WPARAM wparam, LPARAM lparam, LRESULT* result);
std::string ListBoxGetText(MfcListBoxCompat& control, int index);
void ListBoxGetTextCString(MfcListBoxCompat& control, int index,
    MfcCStringCompat& text);
int ListBoxItemFromPoint(MfcListBoxCompat& control, POINT point, bool& outside);

MfcCheckListStateCompat& ConstructAfxCheckListState(
    MfcCheckListStateCompat& state);
void DestroyAfxCheckListState(MfcCheckListStateCompat& state);
MfcCheckListStateCompat* DeleteAfxCheckListStateScalarDtor(
    MfcCheckListStateCompat* state, unsigned flags);
MfcCheckDataCompat& ConstructAfxCheckData(MfcCheckDataCompat& data);
const char* GetCheckListBoxRuntimeClassName();
bool CreateCheckListBox(MfcCheckListBoxCompat& control, DWORD style,
    const RECT& bounds, HWND parent, UINT id);
void DestroyCheckListBox(MfcCheckListBoxCompat& control);
void CheckListBoxSetCheckStyle(MfcCheckListBoxCompat& control, int style);
void CheckListBoxSetCheck(MfcCheckListBoxCompat& control, int item, int check);
int CheckListBoxGetCheck(MfcCheckListBoxCompat& control, int item);
void CheckListBoxEnable(MfcCheckListBoxCompat& control, int item, bool enabled);
bool CheckListBoxIsEnabled(MfcCheckListBoxCompat& control, int item);
RECT CheckListBoxOnGetCheckPosition(MfcCheckListBoxCompat& control,
    RECT item_rect, RECT check_rect);
void CheckListBoxDrawItem(MfcCheckListBoxCompat& control,
    DRAWITEMSTRUCT& draw_item);
void CheckListBoxDrawItemText(MfcCheckListBoxCompat& control,
    DRAWITEMSTRUCT& draw_item, const RECT& text_rect, COLORREF text_color,
    COLORREF background);
void CheckListBoxMeasureItem(MfcCheckListBoxCompat& control,
    MEASUREITEMSTRUCT& measure_item);
int CheckListBoxCompareItem(MfcCheckListBoxCompat& control,
    COMPAREITEMSTRUCT& compare_item);
void CheckListBoxDeleteItem(MfcCheckListBoxCompat& control,
    DELETEITEMSTRUCT& delete_item);
bool CheckListBoxOnChildNotify(MfcCheckListBoxCompat& control, UINT message,
    WPARAM wparam, LPARAM lparam, LRESULT* result);
void CheckListBoxPreSubclassWindow(MfcCheckListBoxCompat& control);
int CheckListBoxCalcMinimumItemHeight(MfcCheckListBoxCompat& control);
void CheckListBoxInvalidateCheck(MfcCheckListBoxCompat& control, int item);
void CheckListBoxInvalidateItem(MfcCheckListBoxCompat& control, int item);
int CheckListBoxCheckFromPoint(MfcCheckListBoxCompat& control, POINT point,
    bool& check_area);
void CheckListBoxSetSelectionCheck(MfcCheckListBoxCompat& control, int check);
void CheckListBoxOnLButtonDown(MfcCheckListBoxCompat& control, UINT flags,
    POINT point);
void CheckListBoxOnLButtonDblClk(MfcCheckListBoxCompat& control, UINT flags,
    POINT point);
void CheckListBoxOnKeyDown(MfcCheckListBoxCompat& control, UINT key,
    UINT repeat, UINT flags);
int CheckListBoxOnCreate(MfcCheckListBoxCompat& control);
LRESULT CheckListBoxOnSetFont(MfcCheckListBoxCompat& control, WPARAM font,
    LPARAM redraw);
int CheckListBoxAddString(MfcCheckListBoxCompat& control, const char* text,
    LPARAM item_data = 0);
int CheckListBoxFindString(MfcCheckListBoxCompat& control, int start_after,
    LPARAM item_data);
int CheckListBoxFindStringExact(MfcCheckListBoxCompat& control, int start_after,
    LPARAM item_data);
LRESULT CheckListBoxOnLBGetItemData(MfcCheckListBoxCompat& control,
    int item, LPARAM fallback);
LRESULT CheckListBoxGetTextRaw(MfcCheckListBoxCompat& control, int item,
    char* text);
int CheckListBoxInsertString(MfcCheckListBoxCompat& control, int index,
    const char* text, LPARAM item_data = 0);
int CheckListBoxSelectString(MfcCheckListBoxCompat& control, int start_after,
    LPARAM item_data);
LRESULT CheckListBoxOnLBSetItemData(MfcCheckListBoxCompat& control,
    int item, LPARAM item_data);
void CheckListBoxSetItemHeight(MfcCheckListBoxCompat& control, int item,
    UINT height);

bool CreateComboBoxControl(MfcComboBoxCompat& control, DWORD style,
    const RECT& bounds, HWND parent, UINT id);
void DestroyComboBoxControl(MfcComboBoxCompat& control);
void ComboBoxDrawItemDefault(DRAWITEMSTRUCT* draw_item);
void ComboBoxMeasureItemDefault(MEASUREITEMSTRUCT* measure_item);
int ComboBoxCompareItemDefault(COMPAREITEMSTRUCT* compare_item);
void ComboBoxDeleteItemDefault(DELETEITEMSTRUCT* delete_item);
bool ComboBoxOnChildNotify(MfcComboBoxCompat& control, UINT message,
    WPARAM wparam, LPARAM lparam, LRESULT* result);
std::string ComboBoxGetLBText(MfcComboBoxCompat& control, int index);
void ComboBoxGetLBTextCString(MfcComboBoxCompat& control, int index,
    MfcCStringCompat& text);

bool CreateEditControl(MfcEditCompat& control, DWORD style, const RECT& bounds,
    HWND parent, UINT id);
void DestroyEditControl(MfcEditCompat& control);
bool CreateScrollBarControl(MfcScrollBarCompat& control, DWORD style,
    const RECT& bounds, HWND parent, UINT id);
void DestroyScrollBarControl(MfcScrollBarCompat& control);

UINT DragListBoxRegisteredMessage();
void PreSubclassDragListBox(MfcDragListBoxCompat& box);
int BeginDragListBox(MfcDragListBoxCompat& box, POINT point);
void CancelDragListBoxDrag(MfcDragListBoxCompat& box);
unsigned DragListBoxDragging(MfcDragListBoxCompat& box, POINT point);
void DropDragListBoxItem(MfcDragListBoxCompat& box, int source_index, POINT point);
void DrawSingle(MfcDragListBoxCompat& box, int item);
void DrawDragListBoxInsert(MfcDragListBoxCompat& box, int item);
LRESULT RouteDragListBoxChildNotify(MfcDragListBoxCompat& box, UINT message,
    WPARAM wparam, LPARAM lparam, LRESULT* result);

const char* GetToolbarCtrlRuntimeClassName();
bool CreateToolbarCtrl(MfcToolbarCtrlCompat& control, DWORD style,
    const RECT& bounds, HWND parent, UINT id);
void DestroyToolbarCtrl(MfcToolbarCtrlCompat& control);
void FillInToolInfo(TOOLINFOA& tool_info, const MfcCWndCompat* window,
    UINT_PTR tool_id);
LRESULT ToolbarAddBitmapResource(MfcToolbarCtrlCompat& control, int count,
    UINT bitmap_id);
LRESULT ToolbarAddBitmapHandle(MfcToolbarCtrlCompat& control, int count,
    HBITMAP bitmap);
LRESULT ToolbarAddButtons(MfcToolbarCtrlCompat& control, int count,
    const TBBUTTON* buttons);
LRESULT ToolbarInsertButton(MfcToolbarCtrlCompat& control, int index,
    const TBBUTTON& button);
void ToolbarSetBitmapSize(MfcToolbarCtrlCompat& control, int width, int height);
void ToolbarSetButtonSize(MfcToolbarCtrlCompat& control, int width, int height);
void ToolbarReplaceBitmap(MfcToolbarCtrlCompat& control, const TBREPLACEBITMAP& bitmap);
void ToolbarSetButtonStructSize(MfcToolbarCtrlCompat& control);
void ToolbarSetOwner(MfcToolbarCtrlCompat& control, HWND owner);

bool CreateStatusBarCtrl(MfcStatusBarCtrlCompat& control, DWORD style,
    const RECT& bounds, HWND parent, UINT id);
void DestroyStatusBarCtrl(MfcStatusBarCtrlCompat& control);
unsigned StatusBarGetTextRaw(MfcStatusBarCtrlCompat& control, int pane,
    char* buffer, int buffer_chars, unsigned* text_type);
std::string StatusBarGetText(MfcStatusBarCtrlCompat& control, int pane,
    unsigned* text_type = nullptr);
unsigned StatusBarGetTextLength(MfcStatusBarCtrlCompat& control, int pane,
    unsigned* text_type = nullptr);
std::string StatusBarGetTipText(MfcStatusBarCtrlCompat& control, int pane);
bool StatusBarGetBorders(MfcStatusBarCtrlCompat& control, int& horizontal,
    int& vertical, int& border);
void StatusBarDefaultDebugAssert();
bool StatusBarOnChildNotify(MfcStatusBarCtrlCompat& control, UINT message,
    WPARAM wparam, LPARAM lparam, LRESULT* result);

const char* GetListCtrlRuntimeClassName();
bool CreateListCtrl(MfcListCtrlCompat& control, DWORD style, const RECT& bounds,
    HWND parent, UINT id);
void DestroyListCtrl(MfcListCtrlCompat& control);
void ListCtrlSetItemText(MfcListCtrlCompat& control, int item, int subitem,
    const char* text);
void ListCtrlSetItemTextAlt(MfcListCtrlCompat& control, int item, int subitem,
    const char* text);
void ListCtrlSetItemState(MfcListCtrlCompat& control, int item, UINT state,
    UINT mask);
bool ListCtrlGetItemPosition(MfcListCtrlCompat& control, int item, POINT& point);
void ListCtrlSetItemPosition(MfcListCtrlCompat& control, int item, int x, int y);
bool ListCtrlGetSubItemRect(MfcListCtrlCompat& control, int item, int subitem,
    int code, RECT& rect);
int ListCtrlInsertColumn(MfcListCtrlCompat& control, int column,
    const char* heading, int format, int width, int subitem);
LRESULT ListCtrlInsertItemFull(MfcListCtrlCompat& control, UINT mask, int item,
    int subitem, const char* text, int image, UINT state, UINT state_mask,
    LPARAM data);
bool ListCtrlGetColumnOrderArray(MfcListCtrlCompat& control, int count,
    int* order);
LRESULT ListCtrlSetItemFull(MfcListCtrlCompat& control, int item, int subitem,
    UINT mask, const char* text, int image, UINT state, UINT state_mask,
    LPARAM data);
std::string ListCtrlGetItemText(MfcListCtrlCompat& control, int item,
    int subitem);
void ListCtrlSetColumn(MfcListCtrlCompat& control, int column, int format,
    int width, const char* heading);
void ListCtrlSetColumnFull(MfcListCtrlCompat& control, int column, int format,
    int width, const char* heading);
int ListCtrlGetColumnWidth(MfcListCtrlCompat& control, int column);
void ListCtrlDebugAssert();
bool ListCtrlOnChildNotify(MfcListCtrlCompat& control, UINT message,
    WPARAM wparam, LPARAM lparam, LRESULT* result);
MfcImageListCompat ListCtrlSetImageList(MfcListCtrlCompat& control,
    HIMAGELIST image_list, int image_list_type);
void ListCtrlOnNcDestroy(MfcListCtrlCompat& control);

const char* GetTreeCtrlRuntimeClassName();
bool CreateTreeCtrl(MfcTreeCtrlCompat& control, DWORD style, const RECT& bounds,
    HWND parent, UINT id);
void DestroyTreeCtrl(MfcTreeCtrlCompat& control);
void TreeCtrlSetItemText(MfcTreeCtrlCompat& control, HTREEITEM item,
    const char* text);
std::string TreeCtrlGetItemText(MfcTreeCtrlCompat& control, HTREEITEM item);
bool TreeCtrlGetItemImage(MfcTreeCtrlCompat& control, HTREEITEM item,
    int& image, int& selected_image);
UINT TreeCtrlGetItemState(MfcTreeCtrlCompat& control, HTREEITEM item,
    UINT mask);
UINT TreeCtrlGetItemStateAlt(MfcTreeCtrlCompat& control, HTREEITEM item,
    UINT mask);
LPARAM TreeCtrlGetItemData(MfcTreeCtrlCompat& control, HTREEITEM item);
void TreeCtrlSetItemFull(MfcTreeCtrlCompat& control, HTREEITEM item,
    UINT mask, const char* text, int image, int selected_image, UINT state,
    UINT state_mask, LPARAM data);
HTREEITEM TreeCtrlInsertItemFull(MfcTreeCtrlCompat& control, HTREEITEM parent,
    HTREEITEM insert_after, UINT mask, const char* text, int image,
    int selected_image, UINT state, UINT state_mask, LPARAM data);
bool TreeCtrlSortChildrenCB(MfcTreeCtrlCompat& control, HTREEITEM parent,
    PFNTVCOMPARE compare, LPARAM data, bool recurse);
bool TreeCtrlGetCheck(MfcTreeCtrlCompat& control, HTREEITEM item);
void TreeCtrlSetCheck(MfcTreeCtrlCompat& control, HTREEITEM item, bool checked);
MfcImageListCompat TreeCtrlSetImageList(MfcTreeCtrlCompat& control,
    HIMAGELIST image_list, int image_list_type);
void TreeCtrlOnDestroy(MfcTreeCtrlCompat& control);

bool CreateSpinButtonCtrl(MfcSpinButtonCtrlCompat& control, DWORD style,
    const RECT& bounds, HWND parent, UINT id);
void DestroySpinButtonCtrl(MfcSpinButtonCtrlCompat& control);
void SpinButtonGetRange(MfcSpinButtonCtrlCompat& control, int& lower,
    int& upper);

bool CreateSliderCtrl(MfcSliderCtrlCompat& control, DWORD style,
    const RECT& bounds, HWND parent, UINT id);
void DestroySliderCtrl(MfcSliderCtrlCompat& control);
void SliderGetRange(MfcSliderCtrlCompat& control, int& lower, int& upper);
void SliderSetRange(MfcSliderCtrlCompat& control, int lower, int upper,
    bool redraw);
void SliderGetSelection(MfcSliderCtrlCompat& control, LRESULT& start,
    LRESULT& end);
void SliderSetSelection(MfcSliderCtrlCompat& control, LPARAM start, LPARAM end);

bool CreateProgressCtrl(MfcProgressCtrlCompat& control, DWORD style,
    const RECT& bounds, HWND parent, UINT id);
void DestroyProgressCtrl(MfcProgressCtrlCompat& control);
bool CreateHeaderCtrl(MfcHeaderCtrlCompat& control, DWORD style,
    const RECT& bounds, HWND parent, UINT id);
void DestroyHeaderCtrl(MfcHeaderCtrlCompat& control);
void HeaderDebugAssert();
bool HeaderOnChildNotify(MfcHeaderCtrlCompat& control, UINT message,
    WPARAM wparam, LPARAM lparam, LRESULT* result);
bool CreateHotKeyCtrl(MfcHotKeyCtrlCompat& control, DWORD style,
    const RECT& bounds, HWND parent, UINT id);
void DestroyHotKeyCtrl(MfcHotKeyCtrlCompat& control);
void HotKeyGetHotKey(MfcHotKeyCtrlCompat& control, WORD& virtual_key,
    WORD& modifiers);

const char* GetTabCtrlRuntimeClassName();
bool CreateTabCtrl(MfcTabCtrlCompat& control, DWORD style, const RECT& bounds,
    HWND parent, UINT id);
void DestroyTabCtrl(MfcTabCtrlCompat& control);
void TabDebugAssert();
bool TabOnChildNotify(MfcTabCtrlCompat& control, UINT message, WPARAM wparam,
    LPARAM lparam, LRESULT* result);
int TabGetItemImage(MfcTabCtrlCompat& control, int item, const char* text);
void TabSetItemImage(MfcTabCtrlCompat& control, int item, const char* text,
    int image);
void TabInsertItemFull(MfcTabCtrlCompat& control, int item, UINT mask,
    const char* text, int image, LPARAM data);
void TabSetItemFull(MfcTabCtrlCompat& control, int item, UINT mask,
    const char* text, int image, LPARAM data, UINT state, UINT state_mask);

bool CreateAnimateCtrl(MfcAnimateCtrlCompat& control, DWORD style,
    const RECT& bounds, HWND parent, UINT id);
void DestroyAnimateCtrl(MfcAnimateCtrlCompat& control);
void DestroyRichEditCtrl(MfcRichEditCtrlCompat& control);

void* EnsureTempImageListHandleMap(bool create);
MfcImageListCompat* LookupPermanentImageList(HIMAGELIST handle);
MfcImageListCompat* LookupTemporaryImageList(HIMAGELIST handle);
void DeleteTempImageListHandleMapEntries();
bool CreateImageListCompat(MfcImageListCompat& image_list, int width,
    int height, UINT flags, int initial_count, int grow_count);
bool LoadImageListResourceId(MfcImageListCompat& image_list, UINT resource_id,
    int width, int grow_count, COLORREF mask);
bool LoadImageListResourceName(MfcImageListCompat& image_list,
    const char* resource_name, int width, int grow_count, COLORREF mask);
bool AttachImageListHandle(MfcImageListCompat& image_list, HIMAGELIST handle);
bool ReadImageListFromArchive(MfcImageListCompat& image_list, IStream* stream);
#endif

} // namespace ranker
