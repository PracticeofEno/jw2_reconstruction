#include "ranker_mfc_runtime.h"

#include "ranker_crt_runtime.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

namespace ranker {

MfcTimeCompat& ConstructMfcTimeFromFields(MfcTimeCompat& out, int year,
    int month, int day, int hour, int minute, int second,
    int daylight_savings) {
    std::tm fields{};
    fields.tm_sec = second;
    fields.tm_min = minute;
    fields.tm_hour = hour;
    fields.tm_mday = day;
    fields.tm_mon = month - 1;
    fields.tm_year = year - 1900;
    fields.tm_isdst = daylight_savings;
    out.value = CrtMktime(&fields);
    return out;
}

MfcTimeCompat& ConstructMfcTimeFromDosDateTime(MfcTimeCompat& out,
    unsigned dos_date, unsigned dos_time, int daylight_savings) {
    const int second = static_cast<int>((dos_time & 0x1f) * 2);
    const int minute = static_cast<int>((dos_time >> 5) & 0x3f);
    const int hour = static_cast<int>((dos_time >> 11) & 0x1f);
    const int day = static_cast<int>(dos_date & 0x1f);
    const int month = static_cast<int>((dos_date >> 5) & 0x0f);
    const int year = static_cast<int>((dos_date >> 9) + 1980);
    return ConstructMfcTimeFromFields(out, year, month, day, hour, minute,
        second, daylight_savings);
}

#ifdef _WIN32
MfcTimeCompat& ConstructMfcTimeFromSystemTime(MfcTimeCompat& out,
    const SYSTEMTIME& value, int daylight_savings) {
    if (value.wYear < 1900) {
        out.value = 0;
        return out;
    }
    return ConstructMfcTimeFromFields(out, value.wYear, value.wMonth,
        value.wDay, value.wHour, value.wMinute, value.wSecond,
        daylight_savings);
}

MfcTimeCompat& ConstructMfcTimeFromFileTime(MfcTimeCompat& out,
    const FILETIME& value, int daylight_savings) {
    FILETIME local_file_time{};
    SYSTEMTIME system_time{};
    if (FileTimeToLocalFileTime(&value, &local_file_time) == FALSE ||
        FileTimeToSystemTime(&local_file_time, &system_time) == FALSE) {
        out.value = 0;
        return out;
    }
    return ConstructMfcTimeFromSystemTime(out, system_time, daylight_savings);
}

bool GetCurrentMfcSystemTime(SYSTEMTIME& out) {
    GetLocalTime(&out);
    return true;
}
#endif

MfcTimeCompat& SetMfcTimeToCurrentTime(MfcTimeCompat& out) {
    out.value = CrtTime(nullptr);
    return out;
}

std::tm* GetMfcTimeGmtTm(const MfcTimeCompat& value, std::tm* out) {
    std::tm* source = std::gmtime(&value.value);
    if (source == nullptr) {
        return nullptr;
    }
    if (out == nullptr) {
        return source;
    }
    *out = *source;
    return out;
}

void DumpMfcTime(const MfcTimeCompat& value) {
    char* text = std::ctime(&value.value);
    if (text == nullptr || value.value == 0) {
        AfxTraceOutput("CTime(invalid = %lld)\n",
            static_cast<long long>(value.value));
        return;
    }
    char trimmed[32]{};
    std::strncpy(trimmed, text, sizeof(trimmed) - 1);
    if (std::strlen(trimmed) > 24) {
        trimmed[24] = '\0';
    }
    AfxTraceOutput("CTime(%s)\n", trimmed);
}

void SerializeMfcTime(std::string& archive, const MfcTimeCompat& value) {
    archive.assign(reinterpret_cast<const char*>(&value.value), sizeof(value.value));
}

bool DeserializeMfcTime(const std::string& archive, MfcTimeCompat& out) {
    if (archive.size() < sizeof(out.value)) {
        return false;
    }
    std::memcpy(&out.value, archive.data(), sizeof(out.value));
    return true;
}

void DumpMfcTimeSpan(const MfcTimeSpanCompat& span) {
    AfxTraceOutput("CTimeSpan(%lld days, %02lld hours, %02lld minutes and %02lld seconds)",
        span.seconds / 86400,
        (span.seconds / 3600) % 24,
        (span.seconds / 60) % 60,
        span.seconds % 60);
}

void SerializeMfcTimeSpan(std::string& archive, const MfcTimeSpanCompat& span) {
    archive.assign(reinterpret_cast<const char*>(&span.seconds), sizeof(span.seconds));
}

bool DeserializeMfcTimeSpan(const std::string& archive, MfcTimeSpanCompat& out) {
    if (archive.size() < sizeof(out.seconds)) {
        return false;
    }
    std::memcpy(&out.seconds, archive.data(), sizeof(out.seconds));
    return true;
}

namespace {

std::tm local_tm_from_mfc_time(const MfcTimeCompat& value) {
    std::tm result{};
    std::tm* source = std::localtime(&value.value);
    if (source != nullptr) {
        result = *source;
    }
    return result;
}

} // namespace

long long MfcTimeSpanFromSecondsValue(long long seconds) {
    return seconds;
}

MfcTimeSpanCompat& ConstructMfcTimeSpanFromSeconds(
    MfcTimeSpanCompat& out, long long seconds) {
    out.seconds = seconds;
    return out;
}

MfcTimeSpanCompat& ConstructMfcTimeSpanFromParts(MfcTimeSpanCompat& out,
    int days, int hours, int minutes, int seconds) {
    out.seconds = static_cast<long long>(seconds) +
        (static_cast<long long>(minutes) +
            (static_cast<long long>(hours) +
                static_cast<long long>(days) * 24) * 60) * 60;
    return out;
}

MfcTimeSpanCompat& ConstructMfcTimeSpanCopy(MfcTimeSpanCompat& out,
    const MfcTimeSpanCompat& source) {
    out.seconds = source.seconds;
    return out;
}

MfcTimeSpanCompat& AssignMfcTimeSpan(MfcTimeSpanCompat& out,
    const MfcTimeSpanCompat& source) {
    out.seconds = source.seconds;
    return out;
}

long long MfcTimeSpanGetDays(const MfcTimeSpanCompat& span) {
    return span.seconds / 86400;
}

long long MfcTimeSpanGetTotalHours(const MfcTimeSpanCompat& span) {
    return span.seconds / 3600;
}

long long MfcTimeSpanGetHours(const MfcTimeSpanCompat& span) {
    return MfcTimeSpanGetTotalHours(span) - MfcTimeSpanGetDays(span) * 24;
}

long long MfcTimeSpanGetTotalMinutes(const MfcTimeSpanCompat& span) {
    return span.seconds / 60;
}

long long MfcTimeSpanGetMinutes(const MfcTimeSpanCompat& span) {
    return MfcTimeSpanGetTotalMinutes(span) -
        MfcTimeSpanGetTotalHours(span) * 60;
}

long long MfcTimeSpanGetTotalSeconds(const MfcTimeSpanCompat& span) {
    return span.seconds;
}

long long MfcTimeSpanGetSeconds(const MfcTimeSpanCompat& span) {
    return MfcTimeSpanGetTotalSeconds(span) -
        MfcTimeSpanGetTotalMinutes(span) * 60;
}

MfcTimeSpanCompat SubtractMfcTimeSpans(const MfcTimeSpanCompat& left,
    const MfcTimeSpanCompat& right) {
    MfcTimeSpanCompat out;
    out.seconds = left.seconds - right.seconds;
    return out;
}

MfcTimeSpanCompat AddMfcTimeSpans(const MfcTimeSpanCompat& left,
    const MfcTimeSpanCompat& right) {
    MfcTimeSpanCompat out;
    out.seconds = left.seconds + right.seconds;
    return out;
}

MfcTimeSpanCompat& MfcTimeSpanAddAssign(MfcTimeSpanCompat& left,
    const MfcTimeSpanCompat& right) {
    left.seconds += right.seconds;
    return left;
}

MfcTimeSpanCompat& MfcTimeSpanSubtractAssign(MfcTimeSpanCompat& left,
    const MfcTimeSpanCompat& right) {
    left.seconds -= right.seconds;
    return left;
}

bool MfcTimeSpanEquals(const MfcTimeSpanCompat& left,
    const MfcTimeSpanCompat& right) {
    return left.seconds == right.seconds;
}

bool MfcTimeSpanNotEquals(const MfcTimeSpanCompat& left,
    const MfcTimeSpanCompat& right) {
    return !MfcTimeSpanEquals(left, right);
}

bool MfcTimeSpanLessThan(const MfcTimeSpanCompat& left,
    const MfcTimeSpanCompat& right) {
    return left.seconds < right.seconds;
}

bool MfcTimeSpanGreaterThan(const MfcTimeSpanCompat& left,
    const MfcTimeSpanCompat& right) {
    return right.seconds < left.seconds;
}

bool MfcTimeSpanLessEqual(const MfcTimeSpanCompat& left,
    const MfcTimeSpanCompat& right) {
    return left.seconds <= right.seconds;
}

bool MfcTimeSpanGreaterEqual(const MfcTimeSpanCompat& left,
    const MfcTimeSpanCompat& right) {
    return right.seconds <= left.seconds;
}

std::time_t MfcTimeFromTimeValue(std::time_t value) {
    return value;
}

MfcTimeCompat& ConstructMfcTimeFromTimeT(MfcTimeCompat& out,
    std::time_t value) {
    out.value = value;
    return out;
}

MfcTimeCompat& ConstructMfcTimeCopy(MfcTimeCompat& out,
    const MfcTimeCompat& source) {
    out.value = source.value;
    return out;
}

MfcTimeCompat& AssignMfcTime(MfcTimeCompat& out,
    const MfcTimeCompat& source) {
    out.value = source.value;
    return out;
}

MfcTimeCompat& AssignMfcTimeFromTimeT(MfcTimeCompat& out,
    std::time_t value) {
    out.value = value;
    return out;
}

std::time_t MfcTimeGetTime(const MfcTimeCompat& time) {
    return time.value;
}

int MfcTimeGetYearLocal(const MfcTimeCompat& time) {
    return local_tm_from_mfc_time(time).tm_year + 1900;
}

int MfcTimeGetMonthLocal(const MfcTimeCompat& time) {
    return local_tm_from_mfc_time(time).tm_mon + 1;
}

int MfcTimeGetDayLocal(const MfcTimeCompat& time) {
    return local_tm_from_mfc_time(time).tm_mday;
}

int MfcTimeGetHourLocal(const MfcTimeCompat& time) {
    return local_tm_from_mfc_time(time).tm_hour;
}

int MfcTimeGetMinuteLocal(const MfcTimeCompat& time) {
    return local_tm_from_mfc_time(time).tm_min;
}

int MfcTimeGetSecondLocal(const MfcTimeCompat& time) {
    return local_tm_from_mfc_time(time).tm_sec;
}

int MfcTimeGetDayOfWeekLocal(const MfcTimeCompat& time) {
    return local_tm_from_mfc_time(time).tm_wday + 1;
}

MfcTimeSpanCompat SubtractMfcTimes(const MfcTimeCompat& left,
    const MfcTimeCompat& right) {
    MfcTimeSpanCompat out;
    out.seconds = static_cast<long long>(left.value) -
        static_cast<long long>(right.value);
    return out;
}

MfcTimeCompat SubtractMfcTimeSpanFromTime(const MfcTimeCompat& time,
    const MfcTimeSpanCompat& span) {
    MfcTimeCompat out;
    out.value = static_cast<std::time_t>(
        static_cast<long long>(time.value) - span.seconds);
    return out;
}

MfcTimeCompat AddMfcTimeSpanToTime(const MfcTimeCompat& time,
    const MfcTimeSpanCompat& span) {
    MfcTimeCompat out;
    out.value = static_cast<std::time_t>(
        static_cast<long long>(time.value) + span.seconds);
    return out;
}

MfcTimeCompat& MfcTimeAddAssignSpan(MfcTimeCompat& time,
    const MfcTimeSpanCompat& span) {
    time.value = AddMfcTimeSpanToTime(time, span).value;
    return time;
}

MfcTimeCompat& MfcTimeSubtractAssignSpan(MfcTimeCompat& time,
    const MfcTimeSpanCompat& span) {
    time.value = SubtractMfcTimeSpanFromTime(time, span).value;
    return time;
}

bool MfcTimeEquals(const MfcTimeCompat& left, const MfcTimeCompat& right) {
    return left.value == right.value;
}

bool MfcTimeNotEquals(const MfcTimeCompat& left, const MfcTimeCompat& right) {
    return !MfcTimeEquals(left, right);
}

bool MfcTimeLessThan(const MfcTimeCompat& left, const MfcTimeCompat& right) {
    return left.value < right.value;
}

bool MfcTimeGreaterThan(const MfcTimeCompat& left, const MfcTimeCompat& right) {
    return right.value < left.value;
}

bool MfcTimeLessEqual(const MfcTimeCompat& left, const MfcTimeCompat& right) {
    return left.value <= right.value;
}

bool MfcTimeGreaterEqual(const MfcTimeCompat& left, const MfcTimeCompat& right) {
    return right.value <= left.value;
}

std::string FormatMfcTimeSpan(const MfcTimeSpanCompat& span, const char* format) {
    if (format == nullptr) {
        return {};
    }
    char buffer[128]{};
    char* out = buffer;
    const char* cursor = format;
    while (*cursor != '\0' && out < buffer + sizeof(buffer) - 1) {
        if (*cursor != '%') {
            *out++ = *cursor++;
            continue;
        }
        ++cursor;
        int written = 0;
        switch (*cursor++) {
        case '%':
            *out++ = '%';
            break;
        case 'D':
            written = std::snprintf(out, static_cast<std::size_t>(buffer + sizeof(buffer) - out),
                "%lld", span.seconds / 86400);
            out += written > 0 ? written : 0;
            break;
        case 'H':
            written = std::snprintf(out, static_cast<std::size_t>(buffer + sizeof(buffer) - out),
                "%02lld", (span.seconds / 3600) % 24);
            out += written > 0 ? written : 0;
            break;
        case 'M':
            written = std::snprintf(out, static_cast<std::size_t>(buffer + sizeof(buffer) - out),
                "%02lld", (span.seconds / 60) % 60);
            out += written > 0 ? written : 0;
            break;
        case 'S':
            written = std::snprintf(out, static_cast<std::size_t>(buffer + sizeof(buffer) - out),
                "%02lld", span.seconds % 60);
            out += written > 0 ? written : 0;
            break;
        default:
            break;
        }
    }
    *out = '\0';
    return buffer;
}

std::string FormatMfcTimeSpanWithResource(const MfcTimeSpanCompat& span,
    const char* format) {
    return FormatMfcTimeSpan(span, format);
}

std::string FormatMfcTimeLocal(const MfcTimeCompat& value, const char* format) {
    char buffer[128]{};
    std::tm* local = CrtLocalTime(&value.value);
    if (local != nullptr && CrtStrftime(buffer, sizeof(buffer), format, local) != 0) {
        return buffer;
    }
    return {};
}

std::string FormatMfcTimeGmt(const MfcTimeCompat& value, const char* format) {
    char buffer[128]{};
    std::tm* gmt = std::gmtime(&value.value);
    if (gmt != nullptr && CrtStrftime(buffer, sizeof(buffer), format, gmt) != 0) {
        return buffer;
    }
    return {};
}

std::string FormatMfcTimeLocalWithResource(const MfcTimeCompat& value,
    const char* format) {
    return FormatMfcTimeLocal(value, format);
}

std::string FormatMfcTimeGmtWithResource(const MfcTimeCompat& value,
    const char* format) {
    return FormatMfcTimeGmt(value, format);
}

} // namespace ranker
