#include "ranker_mfc_dialog_template_internal.h"

#include <cstring>

namespace ranker {

#ifdef _WIN32
namespace {

#pragma pack(push, 1)
struct DialogTemplateExHeader {
    WORD version = 0;
    WORD signature = 0;
    DWORD help_id = 0;
    DWORD extended_style = 0;
    DWORD style = 0;
    WORD item_count = 0;
    short x = 0;
    short y = 0;
    short cx = 0;
    short cy = 0;
};
#pragma pack(pop)

const BYTE* align_dialog_ptr(const BYTE* value) {
    const auto raw = reinterpret_cast<std::uintptr_t>(value);
    return reinterpret_cast<const BYTE*>((raw + 3U) & ~std::uintptr_t{3U});
}

BYTE* align_dialog_ptr(BYTE* value) {
    const auto raw = reinterpret_cast<std::uintptr_t>(value);
    return reinterpret_cast<BYTE*>((raw + 3U) & ~std::uintptr_t{3U});
}

std::size_t dialog_wide_length(const WORD* text) {
    if (text == nullptr) {
        return 0;
    }
    const WORD* cursor = text;
    while (*cursor != 0) {
        ++cursor;
    }
    return static_cast<std::size_t>(cursor - text);
}

std::string dialog_wide_to_ansi(const WORD* text) {
    if (text == nullptr) {
        return {};
    }
    const auto* wide = reinterpret_cast<const WCHAR*>(text);
    const int needed = WideCharToMultiByte(CP_ACP, 0, wide, -1, nullptr, 0,
        nullptr, nullptr);
    if (needed <= 1) {
        return {};
    }
    std::string result(static_cast<std::size_t>(needed - 1), '\0');
    WideCharToMultiByte(CP_ACP, 0, wide, -1, result.data(), needed,
        nullptr, nullptr);
    return result;
}

void ansi_to_dialog_wide(const char* text, std::vector<WORD>& out) {
    if (text == nullptr) {
        text = "";
    }
    const int needed = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
    out.assign(static_cast<std::size_t>(needed > 0 ? needed : 1), 0);
    if (needed > 0) {
        MultiByteToWideChar(CP_ACP, 0, text, -1,
            reinterpret_cast<WCHAR*>(out.data()), needed);
    }
}

DWORD& dialog_template_style_ref(DLGTEMPLATE* dialog_template) {
    if (IsDialogTemplateExtended(dialog_template)) {
        return reinterpret_cast<DialogTemplateExHeader*>(dialog_template)->style;
    }
    return dialog_template->style;
}

DWORD dialog_template_style(const DLGTEMPLATE* dialog_template) {
    if (IsDialogTemplateExtended(dialog_template)) {
        return reinterpret_cast<const DialogTemplateExHeader*>(
            dialog_template)->style;
    }
    return dialog_template->style;
}

WORD dialog_template_item_count(const DLGTEMPLATE* dialog_template) {
    if (IsDialogTemplateExtended(dialog_template)) {
        return reinterpret_cast<const DialogTemplateExHeader*>(
            dialog_template)->item_count;
    }
    return dialog_template->cdit;
}

SIZE dialog_template_dialog_units(const DLGTEMPLATE* dialog_template) {
    SIZE size{};
    if (IsDialogTemplateExtended(dialog_template)) {
        const auto* ex = reinterpret_cast<const DialogTemplateExHeader*>(
            dialog_template);
        size.cx = ex->cx;
        size.cy = ex->cy;
    } else {
        size.cx = dialog_template->cx;
        size.cy = dialog_template->cy;
    }
    return size;
}

} // namespace

DWORD MfcDialogTemplateStyle(const DLGTEMPLATE* dialog_template) {
    return dialog_template_style(dialog_template);
}

SIZE MfcDialogTemplateDialogUnits(const DLGTEMPLATE* dialog_template) {
    return dialog_template_dialog_units(dialog_template);
}

bool CheckDialogTemplate(LPCSTR template_name, bool require_child_style) {
    if (template_name == nullptr) {
        return false;
    }
    HINSTANCE instance = GetModuleHandleA(nullptr);
    HRSRC info = FindResourceA(instance, template_name, RT_DIALOG);
    if (info == nullptr) {
        if (IS_INTRESOURCE(template_name)) {
            AfxTraceOutput("ERROR: Cannot find dialog template 0x%04X.\n",
                LOWORD(reinterpret_cast<ULONG_PTR>(template_name)));
        } else {
            AfxTraceOutput("ERROR: Cannot find dialog template '%s'.\n",
                template_name);
        }
        return false;
    }
    if (!require_child_style) {
        return true;
    }
    HGLOBAL resource = LoadResource(instance, info);
    const auto* dialog_template =
        resource == nullptr ? nullptr :
            static_cast<const DLGTEMPLATE*>(LockResource(resource));
    bool ok = true;
    if (dialog_template != nullptr) {
        const DWORD style = dialog_template->style;
        ok = (style & WS_VISIBLE) == 0 && (style & WS_CHILD) != 0;
    }
    if (resource != nullptr) {
        UnlockResource(resource);
        FreeResource(resource);
    }
    return ok;
}

MfcDialogCompat* DeleteDialogScalarDtor(MfcDialogCompat* dialog,
    unsigned flags) {
    if (dialog == nullptr) {
        return nullptr;
    }
    DestroyDialog(*dialog);
    if ((flags & 1U) != 0) {
        MfcDebugDeleteClientBlock(dialog);
    }
    return dialog;
}

void DialogTemplateMapDialogUnits(const char* face_name, unsigned point_size,
    int dialog_cx, int dialog_cy, SIZE& pixels) {
    HDC dc = GetDC(nullptr);
    int base_x = LOWORD(GetDialogBaseUnits());
    int base_y = HIWORD(GetDialogBaseUnits());
    if (dc != nullptr && face_name != nullptr && *face_name != '\0' &&
        point_size != 0) {
        LOGFONTA font_info{};
        font_info.lfHeight = -MulDiv(static_cast<int>(point_size),
            GetDeviceCaps(dc, LOGPIXELSY), 72);
        font_info.lfWeight = FW_NORMAL;
        font_info.lfCharSet = DEFAULT_CHARSET;
        lstrcpynA(font_info.lfFaceName, face_name, LF_FACESIZE);
        HFONT font = CreateFontIndirectA(&font_info);
        if (font != nullptr) {
            HGDIOBJ old_font = SelectObject(dc, font);
            TEXTMETRICA metrics{};
            SIZE extent{};
            if (GetTextMetricsA(dc, &metrics) &&
                GetTextExtentPoint32A(dc,
                    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz",
                    52, &extent)) {
                base_y = metrics.tmHeight + metrics.tmExternalLeading;
                base_x = (extent.cx + 26) / 52;
            }
            SelectObject(dc, old_font);
            DeleteObject(font);
        }
    }
    if (dc != nullptr) {
        ReleaseDC(nullptr, dc);
    }
    pixels.cx = MulDiv(dialog_cx, base_x, 4);
    pixels.cy = MulDiv(dialog_cy, base_y, 8);
}

void DestroyDialogTemplate(MfcDialogTemplateCompat& dialog_template) {
    if (dialog_template.handle != nullptr) {
        GlobalFree(dialog_template.handle);
    }
    dialog_template.handle = nullptr;
    dialog_template.size = 0;
    dialog_template.no_font = true;
}

MfcDialogTemplateCompat& CDialogTemplate(
    MfcDialogTemplateCompat& dialog_template,
    const DLGTEMPLATE* source) {
    DestroyDialogTemplate(dialog_template);
    if (source == nullptr) {
        return dialog_template;
    }

    const unsigned size = DialogTemplateSize(source);
    HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, size);
    if (handle == nullptr) {
        return dialog_template;
    }
    void* destination = GlobalLock(handle);
    if (destination == nullptr) {
        GlobalFree(handle);
        return dialog_template;
    }
    std::memcpy(destination, source, size);
    GlobalUnlock(handle);
    dialog_template.handle = handle;
    dialog_template.size = size;
    dialog_template.no_font =
        (dialog_template_style(source) & DS_SETFONT) == 0;
    return dialog_template;
}

HGLOBAL DetachDialogTemplateHandle(MfcDialogTemplateCompat& dialog_template) {
    HGLOBAL handle = dialog_template.handle;
    dialog_template.handle = nullptr;
    dialog_template.size = 0;
    dialog_template.no_font = true;
    return handle;
}

HGLOBAL Detach(MfcDialogTemplateCompat& dialog_template) {
    return DetachDialogTemplateHandle(dialog_template);
}

const WORD* DialogTemplateSkipMenuClassTitle(
    const DLGTEMPLATE* dialog_template) {
    if (dialog_template == nullptr) {
        return nullptr;
    }
    const BYTE* cursor = reinterpret_cast<const BYTE*>(dialog_template) +
        (IsDialogTemplateExtended(dialog_template)
            ? sizeof(DialogTemplateExHeader) : sizeof(DLGTEMPLATE));
    auto* words = reinterpret_cast<const WORD*>(cursor);
    if (*words == 0xffff) {
        words += 2;
    } else {
        words = SkipWideString(words);
    }
    if (*words == 0xffff) {
        words += 2;
    } else {
        words = SkipWideString(words);
    }
    return SkipWideString(words);
}

unsigned DialogTemplateSize(const DLGTEMPLATE* dialog_template) {
    if (dialog_template == nullptr) {
        return 0;
    }
    const bool extended = IsDialogTemplateExtended(dialog_template);
    const BYTE* base = reinterpret_cast<const BYTE*>(dialog_template);
    const WORD* font = DialogTemplateSkipMenuClassTitle(dialog_template);
    const BYTE* cursor = reinterpret_cast<const BYTE*>(font);
    if ((dialog_template_style(dialog_template) & DS_SETFONT) != 0) {
        const unsigned header_bytes = DialogTemplateFontHeaderBytes(extended);
        const WORD* face =
            reinterpret_cast<const WORD*>(reinterpret_cast<const BYTE*>(font) +
                header_bytes);
        cursor = reinterpret_cast<const BYTE*>(face + dialog_wide_length(face) + 1);
    }

    WORD count = dialog_template_item_count(dialog_template);
    while (count-- != 0) {
        cursor = align_dialog_ptr(cursor);
        cursor += extended ? 0x18 : 0x12;
        const WORD* words = reinterpret_cast<const WORD*>(cursor);
        if (*words == 0xffff) {
            words += 2;
        } else {
            words = SkipWideString(words);
        }
        if (*words == 0xffff) {
            words += 2;
        } else {
            words = SkipWideString(words);
        }
        const WORD extra_bytes = *words++;
        cursor = reinterpret_cast<const BYTE*>(words) + extra_bytes;
    }
    return static_cast<unsigned>(cursor - base);
}

bool DialogTemplateGetFont(const DLGTEMPLATE* dialog_template,
    MfcCStringCompat& face_name, WORD& point_size) {
    if (dialog_template == nullptr) {
        return false;
    }
    if ((dialog_template_style(dialog_template) & DS_SETFONT) == 0) {
        return false;
    }

    const WORD* font = DialogTemplateSkipMenuClassTitle(dialog_template);
    point_size = *font;
    const unsigned header_bytes =
        DialogTemplateFontHeaderBytes(IsDialogTemplateExtended(dialog_template));
    const WORD* face =
        reinterpret_cast<const WORD*>(reinterpret_cast<const BYTE*>(font) +
            header_bytes);
    face_name.text = dialog_wide_to_ansi(face);
    return true;
}

bool HasFont(const DLGTEMPLATE* dialog_template) {
    MfcCStringCompat face_name;
    WORD point_size = 0;
    return DialogTemplateGetFont(dialog_template, face_name, point_size);
}

bool DialogTemplateGetFontFromHandle(MfcDialogTemplateCompat& dialog_template,
    MfcCStringCompat& face_name, WORD& point_size) {
    if (dialog_template.handle == nullptr) {
        return false;
    }
    auto* locked = static_cast<const DLGTEMPLATE*>(
        GlobalLock(dialog_template.handle));
    if (locked == nullptr) {
        return false;
    }
    const bool result = DialogTemplateGetFont(locked, face_name, point_size);
    GlobalUnlock(dialog_template.handle);
    return result;
}

bool DialogTemplateSetFont(MfcDialogTemplateCompat& dialog_template,
    const char* face_name, WORD point_size) {
    if (dialog_template.handle == nullptr || face_name == nullptr) {
        return false;
    }
    auto* locked = static_cast<DLGTEMPLATE*>(GlobalLock(dialog_template.handle));
    if (locked == nullptr) {
        return false;
    }

    const bool extended = IsDialogTemplateExtended(locked);
    const BYTE* base = reinterpret_cast<const BYTE*>(locked);
    const unsigned old_size = dialog_template.size != 0
        ? dialog_template.size : DialogTemplateSize(locked);
    const bool had_font = (dialog_template_style(locked) & DS_SETFONT) != 0;
    const WORD* old_font = DialogTemplateSkipMenuClassTitle(locked);
    const std::size_t font_offset =
        reinterpret_cast<const BYTE*>(old_font) - base;
    const unsigned header_bytes = DialogTemplateFontHeaderBytes(extended);
    const BYTE* old_items = align_dialog_ptr(base + font_offset);
    if (had_font) {
        const WORD* old_face =
            reinterpret_cast<const WORD*>(base + font_offset + header_bytes);
        old_items = align_dialog_ptr(
            reinterpret_cast<const BYTE*>(old_face + dialog_wide_length(old_face) + 1));
    }
    const std::size_t old_items_offset = old_items - base;

    std::vector<WORD> wide_face;
    ansi_to_dialog_wide(face_name, wide_face);
    const std::size_t new_font_bytes =
        header_bytes + wide_face.size() * sizeof(WORD);
    const std::size_t new_items_offset =
        (font_offset + new_font_bytes + 3U) & ~std::size_t{3U};
    const std::ptrdiff_t delta =
        static_cast<std::ptrdiff_t>(new_items_offset) -
        static_cast<std::ptrdiff_t>(old_items_offset);
    const unsigned new_size = static_cast<unsigned>(
        static_cast<std::ptrdiff_t>(old_size) + delta);
    GlobalUnlock(dialog_template.handle);

    HGLOBAL resized = GlobalReAlloc(dialog_template.handle, new_size + 0x40,
        GMEM_MOVEABLE | GMEM_ZEROINIT);
    if (resized == nullptr) {
        return false;
    }
    dialog_template.handle = resized;
    locked = static_cast<DLGTEMPLATE*>(GlobalLock(dialog_template.handle));
    if (locked == nullptr) {
        return false;
    }

    BYTE* mutable_base = reinterpret_cast<BYTE*>(locked);
    BYTE* old_item_data = mutable_base + old_items_offset;
    BYTE* new_item_data = mutable_base + new_items_offset;
    if (old_items_offset <= old_size) {
        std::memmove(new_item_data, old_item_data, old_size - old_items_offset);
    }
    dialog_template_style_ref(locked) |= DS_SETFONT;
    BYTE* font = mutable_base + font_offset;
    *reinterpret_cast<WORD*>(font) = point_size;
    if (extended) {
        *reinterpret_cast<WORD*>(font + 2) = FW_NORMAL;
        font[4] = 0;
        font[5] = DEFAULT_CHARSET;
    }
    std::memcpy(font + header_bytes, wide_face.data(),
        wide_face.size() * sizeof(WORD));
    if (new_item_data > font + new_font_bytes) {
        std::memset(font + new_font_bytes, 0,
            static_cast<std::size_t>(new_item_data - (font + new_font_bytes)));
    }
    GlobalUnlock(dialog_template.handle);
    dialog_template.size = new_size;
    dialog_template.no_font = false;
    return true;
}

bool DialogTemplateSetSystemFont(MfcDialogTemplateCompat& dialog_template,
    WORD point_size) {
    char face_name[LF_FACESIZE] = "System";
    WORD size = point_size != 0 ? point_size : 10;
    LOGFONTA log_font{};
    HGDIOBJ stock_font = GetStockObject(DEFAULT_GUI_FONT);
    if (stock_font == nullptr) {
        stock_font = GetStockObject(SYSTEM_FONT);
    }
    if (stock_font != nullptr &&
        GetObjectA(stock_font, sizeof(log_font), &log_font) != 0) {
        lstrcpynA(face_name, log_font.lfFaceName, LF_FACESIZE);
        if (point_size == 0 && log_font.lfHeight != 0) {
            HDC dc = GetDC(nullptr);
            if (dc != nullptr) {
                const int height = log_font.lfHeight < 0
                    ? -log_font.lfHeight : log_font.lfHeight;
                size = static_cast<WORD>(MulDiv(height, 72,
                    GetDeviceCaps(dc, LOGPIXELSY)));
                ReleaseDC(nullptr, dc);
            }
        }
    }
    return DialogTemplateSetFont(dialog_template, face_name, size);
}

void DialogTemplateGetSizeInDialogUnits(
    MfcDialogTemplateCompat& dialog_template, SIZE& size) {
    size = SIZE{};
    if (dialog_template.handle == nullptr) {
        return;
    }
    auto* locked = static_cast<const DLGTEMPLATE*>(
        GlobalLock(dialog_template.handle));
    if (locked == nullptr) {
        return;
    }
    size = dialog_template_dialog_units(locked);
    GlobalUnlock(dialog_template.handle);
}

void DialogTemplateGetSizeInPixels(MfcDialogTemplateCompat& dialog_template,
    SIZE& size) {
    DialogTemplateGetSizeInDialogUnits(dialog_template, size);
    if (dialog_template.no_font) {
        const DWORD units = GetDialogBaseUnits();
        size.cx = MulDiv(size.cx, LOWORD(units), 4);
        size.cy = MulDiv(size.cy, HIWORD(units), 8);
        return;
    }

    MfcCStringCompat face_name;
    WORD point_size = 0;
    if (DialogTemplateGetFontFromHandle(dialog_template, face_name,
            point_size)) {
        DialogTemplateMapDialogUnits(face_name.text.c_str(), point_size,
            size.cx, size.cy, size);
        return;
    }
    const DWORD units = GetDialogBaseUnits();
    size.cx = MulDiv(size.cx, LOWORD(units), 4);
    size.cy = MulDiv(size.cy, HIWORD(units), 8);
}

bool IsDialogTemplateExtended(const DLGTEMPLATE* dialog_template) {
    if (dialog_template == nullptr) {
        return false;
    }
    const auto* words = reinterpret_cast<const WORD*>(dialog_template);
    return words[1] == 0xffff;
}

unsigned DialogTemplateFontHeaderBytes(bool extended) {
    return extended ? 6U : 2U;
}

const WORD* SkipWideString(const WORD* text) {
    if (text == nullptr) {
        return nullptr;
    }
    while (*text++ != 0) {
    }
    return text;
}

#endif

} // namespace ranker