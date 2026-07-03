#pragma once

#include "ranker_types.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <oleauto.h>
#endif

#include <string>

namespace ranker {

#ifdef _WIN32
enum class OleDateStatus : u32 {
    Valid = 0,
    Invalid = 1,
    Null = 2,
};

struct OleCurrencyCompat {
    CY value{};
    OleDateStatus status = OleDateStatus::Valid;
};

struct OleDateTimeCompat {
    DATE value = 0.0;
    OleDateStatus status = OleDateStatus::Valid;
};

struct OleDateTimeSpanCompat {
    double days = 0.0;
    OleDateStatus status = OleDateStatus::Valid;
};

OleCurrencyCompat ConstructOleCurrencyFromVariant(const VARIANTARG& variant);
int CompareOleCurrency(const OleCurrencyCompat& lhs, const OleCurrencyCompat& rhs);
OleCurrencyCompat AddOleCurrency(const OleCurrencyCompat& lhs,
    const OleCurrencyCompat& rhs);
OleCurrencyCompat SubtractOleCurrency(const OleCurrencyCompat& lhs,
    const OleCurrencyCompat& rhs);
OleCurrencyCompat NegateOleCurrency(const OleCurrencyCompat& value);
OleCurrencyCompat MultiplyOleCurrencyByLong(const OleCurrencyCompat& value, LONG factor);
OleCurrencyCompat DivideOleCurrencyByLong(const OleCurrencyCompat& value, LONG divisor);
OleCurrencyCompat SetOleCurrencyParts(LONG units, LONG fractional_10000);
OleCurrencyCompat SetCurrency(LONG units, LONG fractional_10000);
bool ParseOleCurrencyString(const char* text, ULONG flags, LCID lcid,
    OleCurrencyCompat& out);
bool FormatOleCurrencyString(const OleCurrencyCompat& value, ULONG flags, LCID lcid,
    std::string& out);
bool ExchangeOleCurrencyText(HWND control, bool save_and_validate,
    OleCurrencyCompat& value, ULONG flags = 0, LCID lcid = LOCALE_USER_DEFAULT);

bool EncodeOleDateTime(WORD year, WORD month, WORD day, WORD hour, WORD minute,
    WORD second, OleDateTimeCompat& out);
OleDateTimeCompat ConstructOleDateTimeFromFields(WORD year, WORD month,
    WORD day, WORD hour, WORD minute, WORD second);
bool DecodeOleDateTime(const OleDateTimeCompat& value, SYSTEMTIME& out);
OleDateTimeCompat ConstructOleDateTimeFromVariant(const VARIANTARG& variant);
OleDateTimeCompat ConstructOleDateTimeFromFileTime(const FILETIME& file_time);
bool ParseOleDateTimeString(const char* text, ULONG flags, LCID lcid,
    OleDateTimeCompat& out);
bool FormatOleDateTimeString(const OleDateTimeCompat& value, ULONG flags, LCID lcid,
    std::string& out);
bool FormatOleDateTimeWithFormat(const OleDateTimeCompat& value,
    const char* format, std::string& out);
bool FormatOleDateTimeWithResource(const OleDateTimeCompat& value,
    UINT resource_id, std::string& out);
bool ExchangeOleDateTimeText(HWND control, bool save_and_validate,
    OleDateTimeCompat& value, ULONG flags = 0, LCID lcid = LOCALE_USER_DEFAULT);
int CompareOleDateTime(const OleDateTimeCompat& lhs, const OleDateTimeCompat& rhs);
OleDateTimeCompat AddOleDateTimeSpan(const OleDateTimeCompat& value,
    const OleDateTimeSpanCompat& span);
OleDateTimeSpanCompat SubtractOleDateTime(const OleDateTimeCompat& lhs,
    const OleDateTimeCompat& rhs);

OleDateTimeSpanCompat SetOleDateTimeSpan(int days, int hours, int minutes,
    int seconds);
OleDateTimeSpanCompat AddOleDateTimeSpans(const OleDateTimeSpanCompat& lhs,
    const OleDateTimeSpanCompat& rhs);
OleDateTimeSpanCompat SubtractOleDateTimeSpans(const OleDateTimeSpanCompat& lhs,
    const OleDateTimeSpanCompat& rhs);
bool FormatOleDateTimeSpan(const OleDateTimeSpanCompat& span, const char* format,
    std::string& out);
int GetOleDateTimeSpanHours(const OleDateTimeSpanCompat& span);
int GetOleDateTimeSpanMinutes(const OleDateTimeSpanCompat& span);
int GetOleDateTimeSpanSeconds(const OleDateTimeSpanCompat& span);
bool FormatOleDateTimeSpanWithResource(const OleDateTimeSpanCompat& span,
    UINT resource_id, std::string& out);
void DumpOleDateTimeSpan(const OleDateTimeSpanCompat& span, std::string& out);
#endif

} // namespace ranker
