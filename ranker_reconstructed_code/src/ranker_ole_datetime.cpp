#include "ranker_ole_datetime.h"

#ifdef _WIN32

#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

namespace ranker {
namespace {

constexpr long long kCurrencyScale = 10000;
constexpr double kOleHalfSecondDays = 1.0 / (2.0 * 24.0 * 60.0 * 60.0);
constexpr double kOleHoursPerDay = 24.0;
constexpr double kOleMinutesPerHour = 60.0;

std::wstring ansi_to_wide(const char* text) {
    if (text == nullptr) {
        return {};
    }
    const int chars = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
    if (chars <= 0) {
        return {};
    }
    std::wstring wide(static_cast<std::size_t>(chars), L'\0');
    MultiByteToWideChar(CP_ACP, 0, text, -1, wide.data(), chars);
    if (!wide.empty() && wide.back() == L'\0') {
        wide.pop_back();
    }
    return wide;
}

std::string wide_to_ansi(const wchar_t* text) {
    if (text == nullptr) {
        return {};
    }
    const int bytes = WideCharToMultiByte(CP_ACP, 0, text, -1, nullptr, 0,
        nullptr, nullptr);
    if (bytes <= 0) {
        return {};
    }
    std::string ansi(static_cast<std::size_t>(bytes), '\0');
    WideCharToMultiByte(CP_ACP, 0, text, -1, ansi.data(), bytes, nullptr, nullptr);
    if (!ansi.empty() && ansi.back() == '\0') {
        ansi.pop_back();
    }
    return ansi;
}

OleCurrencyCompat invalid_currency() {
    OleCurrencyCompat value{};
    value.status = OleDateStatus::Invalid;
    return value;
}

OleDateTimeCompat invalid_datetime() {
    OleDateTimeCompat value{};
    value.status = OleDateStatus::Invalid;
    return value;
}

OleDateTimeSpanCompat invalid_span() {
    OleDateTimeSpanCompat value{};
    value.status = OleDateStatus::Invalid;
    return value;
}

bool is_valid(OleDateStatus status) {
    return status == OleDateStatus::Valid;
}

int truncate_ole_float(double value) {
    return static_cast<int>(value);
}

int span_remainder_component(double scaled_span, double scale,
    int component_modulus) {
    double whole = 0.0;
    const double fraction = std::modf(scaled_span, &whole);
    int component = truncate_ole_float((fraction + kOleHalfSecondDays) * scale);
    if (component >= component_modulus) {
        component -= component_modulus;
    }
    return component;
}

std::string read_window_text(HWND control) {
    const int length = GetWindowTextLengthA(control);
    if (length <= 0) {
        return {};
    }
    std::vector<char> buffer(static_cast<std::size_t>(length) + 1);
    GetWindowTextA(control, buffer.data(), length + 1);
    return buffer.data();
}

OleCurrencyCompat currency_from_i64(long long scaled) {
    OleCurrencyCompat result{};
    result.value.int64 = scaled;
    result.status = OleDateStatus::Valid;
    return result;
}

} // namespace

OleCurrencyCompat ConstructOleCurrencyFromVariant(const VARIANTARG& variant) {
    VARIANTARG converted;
    VariantInit(&converted);
    HRESULT hr = VariantChangeType(&converted, const_cast<VARIANTARG*>(&variant), 0, VT_CY);
    if (FAILED(hr)) {
        return invalid_currency();
    }
    OleCurrencyCompat result{};
    result.value = V_CY(&converted);
    result.status = OleDateStatus::Valid;
    VariantClear(&converted);
    return result;
}

int CompareOleCurrency(const OleCurrencyCompat& lhs, const OleCurrencyCompat& rhs) {
    if (!is_valid(lhs.status) || !is_valid(rhs.status)) {
        return 0;
    }
    if (lhs.value.int64 < rhs.value.int64) {
        return -1;
    }
    if (lhs.value.int64 > rhs.value.int64) {
        return 1;
    }
    return 0;
}

OleCurrencyCompat AddOleCurrency(const OleCurrencyCompat& lhs,
    const OleCurrencyCompat& rhs) {
    if (!is_valid(lhs.status) || !is_valid(rhs.status)) {
        return invalid_currency();
    }
    const long long result = lhs.value.int64 + rhs.value.int64;
    if (((lhs.value.int64 ^ rhs.value.int64) >= 0) &&
        ((lhs.value.int64 ^ result) < 0)) {
        return invalid_currency();
    }
    return currency_from_i64(result);
}

OleCurrencyCompat SubtractOleCurrency(const OleCurrencyCompat& lhs,
    const OleCurrencyCompat& rhs) {
    if (!is_valid(lhs.status) || !is_valid(rhs.status)) {
        return invalid_currency();
    }
    const long long result = lhs.value.int64 - rhs.value.int64;
    if (((lhs.value.int64 ^ rhs.value.int64) < 0) &&
        ((lhs.value.int64 ^ result) < 0)) {
        return invalid_currency();
    }
    return currency_from_i64(result);
}

OleCurrencyCompat NegateOleCurrency(const OleCurrencyCompat& value) {
    if (!is_valid(value.status)) {
        return value;
    }
    if (value.value.int64 == std::numeric_limits<long long>::min()) {
        return invalid_currency();
    }
    return currency_from_i64(-value.value.int64);
}

OleCurrencyCompat MultiplyOleCurrencyByLong(const OleCurrencyCompat& value, LONG factor) {
    if (!is_valid(value.status)) {
        return value;
    }
    const long double product = static_cast<long double>(value.value.int64) *
        static_cast<long double>(factor);
    if (product < static_cast<long double>(std::numeric_limits<long long>::min()) ||
        product > static_cast<long double>(std::numeric_limits<long long>::max())) {
        return invalid_currency();
    }
    return currency_from_i64(static_cast<long long>(product));
}

OleCurrencyCompat DivideOleCurrencyByLong(const OleCurrencyCompat& value, LONG divisor) {
    if (!is_valid(value.status) || divisor == 0) {
        return invalid_currency();
    }
    return currency_from_i64(value.value.int64 / divisor);
}

OleCurrencyCompat SetOleCurrencyParts(LONG units, LONG fractional_10000) {
    const long long abs_units = units < 0 ? -static_cast<long long>(units) : units;
    const long long abs_fraction =
        fractional_10000 < 0 ? -static_cast<long long>(fractional_10000) : fractional_10000;
    long long scaled = abs_units * kCurrencyScale + abs_fraction;
    if (units < 0 || fractional_10000 < 0) {
        scaled = -scaled;
    }
    return currency_from_i64(scaled);
}

OleCurrencyCompat SetCurrency(LONG units, LONG fractional_10000) {
    return SetOleCurrencyParts(units, fractional_10000);
}

bool ParseOleCurrencyString(const char* text, ULONG flags, LCID lcid,
    OleCurrencyCompat& out) {
    const std::wstring wide = ansi_to_wide(text);
    CY value{};
    HRESULT hr = VarCyFromStr(wide.c_str(), lcid, flags, &value);
    if (FAILED(hr)) {
        out = invalid_currency();
        return false;
    }
    out.value = value;
    out.status = OleDateStatus::Valid;
    return true;
}

bool FormatOleCurrencyString(const OleCurrencyCompat& value, ULONG flags, LCID lcid,
    std::string& out) {
    out.clear();
    if (!is_valid(value.status)) {
        return false;
    }
    BSTR bstr = nullptr;
    HRESULT hr = VarBstrFromCy(value.value, lcid, flags, &bstr);
    if (FAILED(hr)) {
        return false;
    }
    out = wide_to_ansi(bstr);
    SysFreeString(bstr);
    return true;
}

bool ExchangeOleCurrencyText(HWND control, bool save_and_validate,
    OleCurrencyCompat& value, ULONG flags, LCID lcid) {
    if (control == nullptr) {
        return false;
    }
    if (!save_and_validate) {
        std::string text;
        if (!FormatOleCurrencyString(value, flags, lcid, text)) {
            return false;
        }
        return SetWindowTextA(control, text.c_str()) != FALSE;
    }

    OleCurrencyCompat parsed{};
    if (!ParseOleCurrencyString(read_window_text(control).c_str(), flags, lcid, parsed)) {
        return false;
    }
    value = parsed;
    return true;
}

bool EncodeOleDateTime(WORD year, WORD month, WORD day, WORD hour, WORD minute,
    WORD second, OleDateTimeCompat& out) {
    SYSTEMTIME st{};
    st.wYear = year;
    st.wMonth = month;
    st.wDay = day;
    st.wHour = hour;
    st.wMinute = minute;
    st.wSecond = second;
    DATE date = 0.0;
    if (SystemTimeToVariantTime(&st, &date) == FALSE) {
        out = invalid_datetime();
        return false;
    }
    out.value = date;
    out.status = OleDateStatus::Valid;
    return true;
}

OleDateTimeCompat ConstructOleDateTimeFromFields(WORD year, WORD month,
    WORD day, WORD hour, WORD minute, WORD second) {
    OleDateTimeCompat out{};
    if (!EncodeOleDateTime(year, month, day, hour, minute, second, out)) {
        return invalid_datetime();
    }
    return out;
}

bool DecodeOleDateTime(const OleDateTimeCompat& value, SYSTEMTIME& out) {
    if (!is_valid(value.status)) {
        return false;
    }
    return VariantTimeToSystemTime(value.value, &out) != FALSE;
}

OleDateTimeCompat ConstructOleDateTimeFromVariant(const VARIANTARG& variant) {
    VARIANTARG converted;
    VariantInit(&converted);
    HRESULT hr = VariantChangeType(&converted, const_cast<VARIANTARG*>(&variant), 0, VT_DATE);
    if (FAILED(hr)) {
        return invalid_datetime();
    }
    OleDateTimeCompat result{};
    result.value = V_DATE(&converted);
    result.status = OleDateStatus::Valid;
    VariantClear(&converted);
    return result;
}

OleDateTimeCompat ConstructOleDateTimeFromFileTime(const FILETIME& file_time) {
    FILETIME local{};
    SYSTEMTIME st{};
    if (FileTimeToLocalFileTime(&file_time, &local) == FALSE ||
        FileTimeToSystemTime(&local, &st) == FALSE) {
        return invalid_datetime();
    }
    OleDateTimeCompat out{};
    EncodeOleDateTime(st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
        st.wSecond, out);
    return out;
}

bool ParseOleDateTimeString(const char* text, ULONG flags, LCID lcid,
    OleDateTimeCompat& out) {
    const std::wstring wide = ansi_to_wide(text);
    DATE value = 0.0;
    HRESULT hr = VarDateFromStr(wide.c_str(), lcid, flags, &value);
    if (FAILED(hr)) {
        out = invalid_datetime();
        return false;
    }
    out.value = value;
    out.status = OleDateStatus::Valid;
    return true;
}

bool FormatOleDateTimeString(const OleDateTimeCompat& value, ULONG flags, LCID lcid,
    std::string& out) {
    out.clear();
    if (!is_valid(value.status)) {
        return false;
    }
    BSTR bstr = nullptr;
    HRESULT hr = VarBstrFromDate(value.value, lcid, flags, &bstr);
    if (FAILED(hr)) {
        return false;
    }
    out = wide_to_ansi(bstr);
    SysFreeString(bstr);
    return true;
}

bool FormatOleDateTimeWithFormat(const OleDateTimeCompat& value,
    const char* format, std::string& out) {
    out.clear();
    SYSTEMTIME st{};
    if (format == nullptr || !DecodeOleDateTime(value, st)) {
        return false;
    }

    char buffer[128]{};
    std::snprintf(buffer, sizeof(buffer), format, st.wYear, st.wMonth, st.wDay,
        st.wHour, st.wMinute, st.wSecond);
    out = buffer;
    return true;
}

bool FormatOleDateTimeWithResource(const OleDateTimeCompat& value,
    UINT, std::string& out) {
    return FormatOleDateTimeWithFormat(value, "%04u-%02u-%02u %02u:%02u:%02u",
        out);
}

bool ExchangeOleDateTimeText(HWND control, bool save_and_validate,
    OleDateTimeCompat& value, ULONG flags, LCID lcid) {
    if (control == nullptr) {
        return false;
    }
    if (!save_and_validate) {
        std::string text;
        if (!FormatOleDateTimeString(value, flags, lcid, text)) {
            return false;
        }
        return SetWindowTextA(control, text.c_str()) != FALSE;
    }

    OleDateTimeCompat parsed{};
    if (!ParseOleDateTimeString(read_window_text(control).c_str(), flags, lcid, parsed)) {
        return false;
    }
    value = parsed;
    return true;
}

int CompareOleDateTime(const OleDateTimeCompat& lhs, const OleDateTimeCompat& rhs) {
    if (!is_valid(lhs.status) || !is_valid(rhs.status)) {
        return 0;
    }
    if (lhs.value < rhs.value) {
        return -1;
    }
    if (lhs.value > rhs.value) {
        return 1;
    }
    return 0;
}

OleDateTimeCompat AddOleDateTimeSpan(const OleDateTimeCompat& value,
    const OleDateTimeSpanCompat& span) {
    if (!is_valid(value.status) || !is_valid(span.status)) {
        return invalid_datetime();
    }
    OleDateTimeCompat out{};
    out.value = value.value + span.days;
    out.status = OleDateStatus::Valid;
    return out;
}

OleDateTimeSpanCompat SubtractOleDateTime(const OleDateTimeCompat& lhs,
    const OleDateTimeCompat& rhs) {
    if (!is_valid(lhs.status) || !is_valid(rhs.status)) {
        return invalid_span();
    }
    OleDateTimeSpanCompat out{};
    out.days = lhs.value - rhs.value;
    out.status = OleDateStatus::Valid;
    return out;
}

OleDateTimeSpanCompat SetOleDateTimeSpan(int days, int hours, int minutes,
    int seconds) {
    OleDateTimeSpanCompat out{};
    out.days = static_cast<double>(days) +
        static_cast<double>(hours) / 24.0 +
        static_cast<double>(minutes) / (24.0 * 60.0) +
        static_cast<double>(seconds) / (24.0 * 60.0 * 60.0);
    out.status = OleDateStatus::Valid;
    return out;
}

OleDateTimeSpanCompat AddOleDateTimeSpans(const OleDateTimeSpanCompat& lhs,
    const OleDateTimeSpanCompat& rhs) {
    if (!is_valid(lhs.status) || !is_valid(rhs.status)) {
        return invalid_span();
    }
    OleDateTimeSpanCompat out{};
    out.days = lhs.days + rhs.days;
    out.status = OleDateStatus::Valid;
    return out;
}

OleDateTimeSpanCompat SubtractOleDateTimeSpans(const OleDateTimeSpanCompat& lhs,
    const OleDateTimeSpanCompat& rhs) {
    if (!is_valid(lhs.status) || !is_valid(rhs.status)) {
        return invalid_span();
    }
    OleDateTimeSpanCompat out{};
    out.days = lhs.days - rhs.days;
    out.status = OleDateStatus::Valid;
    return out;
}

bool FormatOleDateTimeSpan(const OleDateTimeSpanCompat& span, const char* format,
    std::string& out) {
    out.clear();
    if (!is_valid(span.status) || format == nullptr) {
        return false;
    }
    const int total_seconds = static_cast<int>(std::floor(std::abs(span.days) * 86400.0));
    const int days = total_seconds / 86400;
    const int hours = (total_seconds / 3600) % 24;
    const int minutes = (total_seconds / 60) % 60;
    const int seconds = total_seconds % 60;

    char buffer[128]{};
    wsprintfA(buffer, format, days, hours, minutes, seconds);
    out = buffer;
    return true;
}

int GetOleDateTimeSpanHours(const OleDateTimeSpanCompat& span) {
    if (!is_valid(span.status)) {
        return 0;
    }
    return span_remainder_component(span.days, kOleHoursPerDay, 24);
}

int GetOleDateTimeSpanMinutes(const OleDateTimeSpanCompat& span) {
    if (!is_valid(span.status)) {
        return 0;
    }
    return span_remainder_component(span.days * kOleHoursPerDay,
        kOleMinutesPerHour, 60);
}

int GetOleDateTimeSpanSeconds(const OleDateTimeSpanCompat& span) {
    if (!is_valid(span.status)) {
        return 0;
    }
    return span_remainder_component(
        span.days * kOleHoursPerDay * kOleMinutesPerHour,
        kOleMinutesPerHour, 60);
}

bool FormatOleDateTimeSpanWithResource(const OleDateTimeSpanCompat& span,
    UINT, std::string& out) {
    return FormatOleDateTimeSpan(span, "%d days %02d:%02d:%02d", out);
}

void DumpOleDateTimeSpan(const OleDateTimeSpanCompat& span, std::string& out) {
    char buffer[128]{};
    std::snprintf(buffer, sizeof(buffer), "COleDateTimeSpan(days=%f,status=%u)",
        span.days, static_cast<unsigned>(span.status));
    out = buffer;
}

} // namespace ranker

#endif
