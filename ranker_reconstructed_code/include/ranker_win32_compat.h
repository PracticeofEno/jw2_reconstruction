#pragma once

#include "ranker_types.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <cstddef>

namespace ranker {

#ifdef _WIN32
struct User32MonitorApiState {
    bool initialized = false;
    bool available = false;
};

LPSTR ConvertWideToAnsiDefaultCodePage(LPSTR destination, LPCWSTR source,
    int destination_chars);
LPSTR ConvertWideToAnsiCodePage(LPSTR destination, LPCWSTR source,
    int destination_chars, UINT code_page);
int MeasureWideStringLength(LPCWSTR source);
LPWSTR ConvertAnsiToWideDefaultCodePage(LPWSTR destination, LPCSTR source,
    int destination_chars);
LPWSTR ConvertAnsiToWideCodePage(LPWSTR destination, LPCSTR source,
    int destination_chars, UINT code_page);
DEVMODEW* ConvertDevModeAToW(DEVMODEW* destination, const DEVMODEA* source,
    UINT code_page = CP_ACP);
DEVMODEA* ConvertDevModeWToA(DEVMODEA* destination, const DEVMODEW* source,
    UINT code_page = CP_ACP);

User32MonitorApiState& user32_monitor_api_state();
bool LoadUser32MonitorApiThunks();
int CompatGetSystemMetrics(int index);
int xGetSystemMetrics(int index);
HMONITOR CompatMonitorFromPoint(POINT point, DWORD flags);
HMONITOR CompatMonitorFromRect(const RECT* rect, DWORD flags);
HMONITOR CompatMonitorFromWindow(HWND window, DWORD flags);
BOOL CompatGetMonitorInfoA(HMONITOR monitor, LPMONITORINFO info);
BOOL CompatEnumDisplayMonitors(HDC dc, LPCRECT clip_rect,
    MONITORENUMPROC callback, LPARAM data);
BOOL xEnumDisplayMonitors(HDC dc, LPCRECT clip_rect, MONITORENUMPROC callback,
    LPARAM data);

const char* ReturnCStringBufferPointer(const char* value);
char* GetCStringDataBufferFromHeader(void* header);
const char* FindCStringOneOf(const char* text, const char* chars);
char* LowercaseCStringInPlace(char* text);
char* ReverseMbcsStringInPlace(char* text);
const char* AdvanceMbcsStringPointer(const char* text);
std::size_t SpanMbcsStringIncluding(const char* text, const char* chars);
std::size_t SpanMbcsStringExcluding(const char* text, const char* chars);
int CompareMbcsStringN(const char* lhs, const char* rhs, std::size_t count);
int CompareMbcsCaseInsensitive(const char* lhs, const char* rhs);
char* CopyMbcsStringNBytes(char* destination, const char* source, std::size_t count);
char* FindMbcsCharacter(char* text, unsigned character);
const char* FindMbcsCharacter(const char* text, unsigned character);
int CompareMbcsString(const char* lhs, const char* rhs);
char* FindLastMbcsCharacter(char* text, unsigned character);
const char* FindLastMbcsCharacter(const char* text, unsigned character);
char* TokenizeMbcsString(char* text, const char* delimiters);
char* LowercaseMbcsStringInPlace(char* text);
char* FindMbcsSubstring(char* text, const char* needle);
const char* FindMbcsSubstring(const char* text, const char* needle);
char* FindMbcsStringOneOf(char* text, const char* characters);
const char* FindMbcsStringOneOf(const char* text, const char* characters);
char* UppercaseMbcsStringInPlace(char* text);
char* ReverseMbcsStringInPlaceCore(char* text);
int GetMbcsCharacterLength(const char* text);
const char* PreviousMbcsStringPointer(const char* start, const char* current);
const char* CrtMbsInc(const char* text);
bool CrtIsDigit(unsigned character);
bool CrtIsSpace(unsigned character);
unsigned LocaleIdFromMbcsCodePage(unsigned code_page);
int SetMbcsCodePage(unsigned code_page);
void RebuildMbcsCaseMapTables();
unsigned GetActiveMbcsCodePage();
void InitializeMbcsCodePageOnce();
#endif

} // namespace ranker
