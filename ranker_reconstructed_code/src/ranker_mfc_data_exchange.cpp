#include "ranker_mfc_runtime.h"

#include <algorithm>
#include <cstdlib>

namespace ranker {

#ifdef _WIN32

HWND DdxPrepareCtrl(MfcDataExchangeCompat& dx, int control_id) {
    if (control_id == 0 || control_id == -1 || dx.dialog == nullptr) {
        AfxTraceOutput("Error: invalid data exchange control ID %d.\n",
            control_id);
        ThrowMfcResourceException();
    }
    HWND control = CWndGetDlgItemHandle(*dx.dialog, control_id);
    if (control == nullptr) {
        AfxTraceOutput("Error: no data exchange control with ID %d.\n",
            control_id);
        ThrowMfcResourceException();
    }
    dx.last_control = control;
    dx.edit_last_control = false;
    return control;
}

HWND DdxPrepareEditCtrl(MfcDataExchangeCompat& dx, int control_id) {
    HWND control = DdxPrepareCtrl(dx, control_id);
    dx.edit_last_control = true;
    return control;
}

[[noreturn]] void DdxFail(MfcDataExchangeCompat& dx) {
    if (!dx.save_and_validate) {
        AfxTraceOutput("Warning: CDataExchange::Fail called during init.\n");
    } else if (dx.last_control == nullptr) {
        AfxTraceOutput("Error: validation failed with no control prepared.\n");
    } else {
        SetFocus(dx.last_control);
        if (dx.edit_last_control) {
            SendMessageA(dx.last_control, EM_SETSEL, 0, -1);
        }
    }
    ThrowMfcResourceException();
}

bool DdxParseIntText(const char* text, const char* format, void* value) {
    if (text == nullptr || format == nullptr || value == nullptr ||
        *format != '%') {
        return false;
    }
    bool short_value = false;
    const char* spec = format + 1;
    if (*spec == 'l') {
        ++spec;
    } else if (*spec == 's') {
        short_value = true;
        ++spec;
    }
    if ((*spec != 'd' && *spec != 'u') || spec[1] != '\0') {
        return false;
    }
    while (*text == ' ' || *text == '\t') {
        ++text;
    }
    char* end = nullptr;
    long parsed = 0;
    if (*spec == 'd') {
        parsed = std::strtol(text, &end, 10);
    } else {
        if (*text == '-') {
            return false;
        }
        parsed = static_cast<long>(std::strtoul(text, &end, 10));
    }
    if (end == text) {
        return false;
    }
    while (*end == ' ' || *end == '\t') {
        ++end;
    }
    if (*end != '\0') {
        return false;
    }
    if (short_value) {
        const short narrowed = static_cast<short>(parsed);
        if (narrowed != parsed) {
            return false;
        }
        *static_cast<short*>(value) = narrowed;
    } else {
        *static_cast<int*>(value) = static_cast<int>(parsed);
    }
    return true;
}

void DdxTextWithFormat(MfcDataExchangeCompat& dx, int control_id,
    const char* format, unsigned fail_prompt, void* value) {
    HWND control = DdxPrepareEditCtrl(dx, control_id);
    if (!dx.save_and_validate) {
        char text[64]{};
        if (format == nullptr) {
            format = "%d";
        }
        wsprintfA(text, format, value == nullptr ? 0 : *static_cast<int*>(value));
        SetWindowTextIfChanged(control, text);
        return;
    }
    char text[64]{};
    GetWindowTextA(control, text, static_cast<int>(sizeof(text)));
    if (!DdxParseIntText(text, format, value)) {
        AfxMessageBoxResource(fail_prompt, 0, 0xffffffffU);
        DdxFail(dx);
    }
}

void DDX_Text(MfcDataExchangeCompat& dx, int control_id,
    MfcCStringCompat& value) {
    HWND control = DdxPrepareEditCtrl(dx, control_id);
    if (!dx.save_and_validate) {
        SetWindowTextIfChanged(control, CStringGetStringPtr(value));
        return;
    }

    const int length = GetWindowTextLengthA(control);
    char* buffer = CStringGetBufferSetLength(value, length);
    GetWindowTextA(control, buffer, length + 1);
    CStringReleaseBuffer(value, -1);
}

void DdxTextByte(MfcDataExchangeCompat& dx, int control_id, BYTE& value) {
    int temp = value;
    if (!dx.save_and_validate) {
        DdxTextWithFormat(dx, control_id, "%u", 0xf116, &temp);
        return;
    }
    DdxTextWithFormat(dx, control_id, "%u", 0xf116, &temp);
    if (temp < 0 || temp > 0xff) {
        AfxMessageBoxResource(0xf116, 0, 0xffffffffU);
        DdxFail(dx);
    }
    value = static_cast<BYTE>(temp);
}

void DdxTextBuffer(MfcDataExchangeCompat& dx, int control_id, char* buffer,
    int max_count) {
    if (buffer == nullptr || max_count <= 0) {
        return;
    }
    HWND control = DdxPrepareEditCtrl(dx, control_id);
    if (!dx.save_and_validate) {
        SetWindowTextIfChanged(control, buffer);
        return;
    }
    const int length = GetWindowTextLengthA(control);
    const int copied = GetWindowTextA(control, buffer, max_count);
    if (copied < length) {
        AfxTraceOutput("Text in control ID %d is too long; truncated.\n",
            control_id);
    }
}

void DdxCheck(MfcDataExchangeCompat& dx, int control_id, int& value) {
    HWND control = DdxPrepareCtrl(dx, control_id);
    if (!dx.save_and_validate) {
        if (value < 0 || value > 2) {
            AfxTraceOutput("Warning: dialog data checkbox value out of range: %d\n",
                value);
            value = 0;
        }
        SendMessageA(control, BM_SETCHECK, static_cast<WPARAM>(value), 0);
    } else {
        value = static_cast<int>(SendMessageA(control, BM_GETCHECK, 0, 0));
    }
}

void DdxRadio(MfcDataExchangeCompat& dx, int first_control_id, int& value) {
    HWND control = DdxPrepareCtrl(dx, first_control_id);
    if (dx.save_and_validate) {
        value = -1;
    }
    int index = 0;
    while (control != nullptr) {
        const LONG style = GetWindowLongA(control, GWL_STYLE);
        const LRESULT type = SendMessageA(control, WM_GETDLGCODE, 0, 0);
        if ((type & DLGC_RADIOBUTTON) != 0) {
            if (!dx.save_and_validate) {
                SendMessageA(control, BM_SETCHECK,
                    static_cast<WPARAM>(index == value), 0);
            } else if (SendMessageA(control, BM_GETCHECK, 0, 0) != 0) {
                value = index;
            }
            ++index;
        } else {
            AfxTraceOutput("Warning: skipping non-radio button in DDX_Radio.\n");
        }
        control = GetWindow(control, GW_HWNDNEXT);
        if (control == nullptr ||
            (GetWindowLongA(control, GWL_STYLE) & WS_GROUP) != 0) {
            break;
        }
        (void)style;
    }
}

void AfxFailRadio(MfcDataExchangeCompat& dx) {
    AfxMessageBoxResource(0xf115, MB_ICONEXCLAMATION, 0xf115);
    DdxFail(dx);
}

void DdxListBoxString(MfcDataExchangeCompat& dx, int control_id,
    MfcCStringCompat& value) {
    HWND control = DdxPrepareCtrl(dx, control_id);
    if (!dx.save_and_validate) {
        if (SendMessageA(control, LB_SELECTSTRING, static_cast<WPARAM>(-1),
                reinterpret_cast<LPARAM>(value.text.c_str())) == LB_ERR) {
            AfxTraceOutput("Warning: no listbox item selected for '%s'.\n",
                value.text.c_str());
        }
        return;
    }
    const LRESULT selection = SendMessageA(control, LB_GETCURSEL, 0, 0);
    if (selection == LB_ERR) {
        value.text.clear();
        return;
    }
    const LRESULT length = SendMessageA(control, LB_GETTEXTLEN,
        static_cast<WPARAM>(selection), 0);
    char* buffer = CStringGetBufferSetLength(value, static_cast<int>(length));
    SendMessageA(control, LB_GETTEXT, static_cast<WPARAM>(selection),
        reinterpret_cast<LPARAM>(buffer));
    CStringReleaseBuffer(value, -1);
}

void DdxListBoxStringExact(MfcDataExchangeCompat& dx, int control_id,
    MfcCStringCompat& value) {
    HWND control = DdxPrepareCtrl(dx, control_id);
    if (!dx.save_and_validate) {
        LRESULT index = SendMessageA(control, LB_FINDSTRINGEXACT,
            static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(value.text.c_str()));
        if (index < 0) {
            AfxTraceOutput("Warning: no exact listbox item selected for '%s'.\n",
                value.text.c_str());
        } else {
            SendMessageA(control, LB_SETCURSEL, static_cast<WPARAM>(index), 0);
        }
        return;
    }
    DdxListBoxString(dx, control_id, value);
}

void DdxComboBoxString(MfcDataExchangeCompat& dx, int control_id,
    MfcCStringCompat& value) {
    HWND control = DdxPrepareCtrl(dx, control_id);
    if (!dx.save_and_validate) {
        if (SendMessageA(control, CB_SELECTSTRING, static_cast<WPARAM>(-1),
                reinterpret_cast<LPARAM>(value.text.c_str())) == CB_ERR) {
            SetWindowTextIfChanged(control, value.text.c_str());
        }
        return;
    }
    const int length = std::max(255, GetWindowTextLengthA(control));
    char* buffer = CStringGetBuffer(value, length);
    GetWindowTextA(control, buffer, length + 1);
    CStringReleaseBuffer(value, -1);
}

void DdxComboBoxStringExact(MfcDataExchangeCompat& dx, int control_id,
    MfcCStringCompat& value) {
    HWND control = DdxPrepareCtrl(dx, control_id);
    if (!dx.save_and_validate) {
        LRESULT index = SendMessageA(control, CB_FINDSTRINGEXACT,
            static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(value.text.c_str()));
        if (index < 0) {
            SetWindowTextIfChanged(control, value.text.c_str());
        } else {
            SendMessageA(control, CB_SETCURSEL, static_cast<WPARAM>(index), 0);
        }
        return;
    }
    DdxComboBoxString(dx, control_id, value);
}

void DdxListBoxIndex(MfcDataExchangeCompat& dx, int control_id, int& value) {
    HWND control = DdxPrepareCtrl(dx, control_id);
    if (!dx.save_and_validate) {
        SendMessageA(control, LB_SETCURSEL, static_cast<WPARAM>(value), 0);
    } else {
        value = static_cast<int>(SendMessageA(control, LB_GETCURSEL, 0, 0));
    }
}

void DdxComboBoxIndex(MfcDataExchangeCompat& dx, int control_id, int& value) {
    HWND control = DdxPrepareCtrl(dx, control_id);
    if (!dx.save_and_validate) {
        SendMessageA(control, CB_SETCURSEL, static_cast<WPARAM>(value), 0);
    } else {
        value = static_cast<int>(SendMessageA(control, CB_GETCURSEL, 0, 0));
    }
}

void DdxScroll(MfcDataExchangeCompat& dx, int control_id, int& value) {
    HWND control = DdxPrepareCtrl(dx, control_id);
    if (!dx.save_and_validate) {
        SetScrollPos(control, SB_CTL, value, TRUE);
    } else {
        value = GetScrollPos(control, SB_CTL);
    }
}

void DdxSlider(MfcDataExchangeCompat& dx, int control_id, int& value) {
    HWND control = DdxPrepareCtrl(dx, control_id);
    if (!dx.save_and_validate) {
        SendMessageA(control, TBM_SETPOS, TRUE, value);
    } else {
        value = static_cast<int>(SendMessageA(control, TBM_GETPOS, 0, 0));
    }
}

void DdvFailMinMax(MfcDataExchangeCompat& dx, int min_value, int max_value,
    const char* format, unsigned prompt_id) {
    if (!dx.save_and_validate) {
        AfxTraceOutput("Warning: initial dialog data is out of range.\n");
        return;
    }
    char min_text[32]{};
    char max_text[32]{};
    wsprintfA(min_text, format == nullptr ? "%d" : format, min_value);
    wsprintfA(max_text, format == nullptr ? "%d" : format, max_value);
    MfcCStringCompat message;
    AfxFormatString2(message, prompt_id, min_text, max_text);
    AfxMessageBoxCompat(message.text.c_str(), MB_ICONEXCLAMATION, prompt_id);
    DdxFail(dx);
}

void DdvMinMaxByte(MfcDataExchangeCompat& dx, BYTE value, BYTE min_value,
    BYTE max_value) {
    if (value < min_value || value > max_value) {
        DdvFailMinMax(dx, min_value, max_value, "%u", 0xf112);
    }
}

void DdvMinMaxShort(MfcDataExchangeCompat& dx, short value, short min_value,
    short max_value) {
    if (value < min_value || value > max_value) {
        DdvFailMinMax(dx, min_value, max_value, "%d", 0xf112);
    }
}

void DdvMinMaxInt(MfcDataExchangeCompat& dx, int value, int min_value,
    int max_value) {
    if (value < min_value || value > max_value) {
        DdvFailMinMax(dx, min_value, max_value, "%d", 0xf112);
    }
}

void DdvMinMaxLong(MfcDataExchangeCompat& dx, long value, long min_value,
    long max_value) {
    if (value < min_value || value > max_value) {
        DdvFailMinMax(dx, static_cast<int>(min_value),
            static_cast<int>(max_value), "%ld", 0xf112);
    }
}

void DdvMinMaxUInt(MfcDataExchangeCompat& dx, unsigned value,
    unsigned min_value, unsigned max_value) {
    if (value < min_value || value > max_value) {
        DdvFailMinMax(dx, static_cast<int>(min_value),
            static_cast<int>(max_value), "%u", 0xf112);
    }
}

void DdvMinMaxULong(MfcDataExchangeCompat& dx, unsigned long value,
    unsigned long min_value, unsigned long max_value) {
    if (value < min_value || value > max_value) {
        DdvFailMinMax(dx, static_cast<int>(min_value),
            static_cast<int>(max_value), "%lu", 0xf112);
    }
}

void DdvSliderMinMax(MfcDataExchangeCompat& dx, unsigned value,
    unsigned min_value, unsigned max_value) {
    if (min_value > max_value) {
        return;
    }
    if (!dx.save_and_validate &&
        (value < min_value || value > max_value)) {
        LONG id = dx.last_control == nullptr ? 0 :
            GetWindowLongA(dx.last_control, GWL_ID);
        AfxTraceOutput("Warning: initial dialog data is out of range for control ID %ld.\n",
            id);
    }
    if (dx.last_control != nullptr) {
        SendMessageA(dx.last_control, TBM_SETRANGEMIN, FALSE, min_value);
        SendMessageA(dx.last_control, TBM_SETRANGEMAX, TRUE, max_value);
    }
}

void DdvFailMaxChars(MfcDataExchangeCompat& dx, unsigned max_chars) {
    char limit[32]{};
    wsprintfA(limit, "%u", max_chars);
    MfcCStringCompat message;
    AfxFormatString1(message, 0xf114, limit);
    AfxMessageBoxCompat(message.text.c_str(), MB_ICONEXCLAMATION, 0xf114);
    DdxFail(dx);
}

void DdvMaxChars(MfcDataExchangeCompat& dx, const MfcCStringCompat& value,
    unsigned max_chars) {
    if (max_chars < 1) {
        return;
    }
    if (dx.save_and_validate && value.text.size() > max_chars) {
        DdvFailMaxChars(dx, max_chars);
    }
    if (dx.last_control != nullptr && dx.edit_last_control) {
        SendMessageA(dx.last_control, EM_LIMITTEXT, max_chars, 0);
    }
}

void DdxControl(MfcDataExchangeCompat& dx, int control_id,
    MfcCWndCompat& control) {
    if (control.window != nullptr) {
        return;
    }
    if (dx.save_and_validate) {
        AfxTraceOutput("Warning: DDX_Control called during save/validate.\n");
        return;
    }
    HWND handle = DdxPrepareCtrl(dx, control_id);
    if (!CWndSubclassWindow(control, handle)) {
        ThrowMfcResourceException();
    }
    if (dx.dialog != nullptr && control.window != nullptr) {
        CWndAttachControlSiteToParent(control, *dx.dialog);
    }
}

#endif

} // namespace ranker
