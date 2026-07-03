#pragma once

#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace ranker {

struct CrtMemState {
    void* first_block = nullptr;
    long block_count[5]{};
    std::size_t block_bytes[5]{};
    std::size_t high_water_bytes = 0;
    std::size_t total_allocated_bytes = 0;
};

struct CrtFileStatus {
    unsigned device = 0;
    unsigned inode = 0;
    unsigned mode = 0;
    unsigned link_count = 0;
    unsigned user_id = 0;
    unsigned group_id = 0;
    unsigned special_device = 0;
    unsigned size = 0;
    std::time_t access_time = 0;
    std::time_t modify_time = 0;
    std::time_t create_time = 0;
};

struct LocaleTimeData {
    std::array<std::string, 7> abbreviated_day_names{};
    std::array<std::string, 7> day_names{};
    std::array<std::string, 12> abbreviated_month_names{};
    std::array<std::string, 12> month_names{};
    std::string am_name;
    std::string pm_name;
    std::string short_date_format;
    std::string long_date_format;
    std::string time_format;
};

struct LocaleNumericData {
    std::string decimal_point = ".";
    std::string thousands_separator;
    std::string grouping;
};

struct LocaleMonetaryData {
    std::string currency_symbol;
    std::string international_currency_symbol;
    std::string monetary_decimal_point;
    std::string monetary_thousands_separator;
    std::string monetary_grouping;
    std::string positive_sign;
    std::string negative_sign;
    int fractional_digits = 2;
    int international_fractional_digits = 2;
    int positive_format = 0;
    int negative_format = 0;
};

struct CrtResolvedLocale {
    std::string language;
    std::string country;
    unsigned locale_id = 0;
    unsigned sort_locale_id = 0;
    unsigned code_page = 0;
};

struct CrtThreadData {
    unsigned thread_id = 0;
    unsigned handle_marker = 0xffffffffu;
};

struct CrtFpuExceptionState {
    unsigned control_word = 0;
    unsigned status_word = 0;
    unsigned exception_flags = 0;
    double argument1 = 0.0;
    double argument2 = 0.0;
    double result = 0.0;
};

struct CrtLocaleNameParts {
    std::string language;
    std::string country;
    std::string code_page;
};

using CrtSignalHandler = void (*)(int signal);

struct CrtExceptionSignalAction {
    unsigned exception_code = 0;
    int signal = 0;
    CrtSignalHandler handler = nullptr;
};

struct CrtLongDouble80 {
    std::uint32_t mantissa_low = 0;
    std::uint32_t mantissa_high = 0;
    std::uint16_t sign_exponent = 0;
};

struct CrtParsedFloat {
    CrtLongDouble80 value;
    const char* end = nullptr;
    unsigned flags = 0;
};

struct CrtDecimalConversion {
    int sign = 0;
    int decimal_point = 0;
    int status = 0;
    std::string digits;
};

struct CrtMantissa96 {
    std::array<std::uint32_t, 3> words{};
};

using CrtWideOutputWriter = int (*)(wchar_t character, void* context);

#ifdef _WIN32
struct CrtFindDataA {
    unsigned attrib = 0;
    std::time_t time_create = 0;
    std::time_t time_access = 0;
    std::time_t time_write = 0;
    unsigned long size = 0;
    char name[MAX_PATH]{};
};
#endif

void DestroyCrtLocaleObject(void* object);
void DeleteCrtLocaleObject(void* object, bool free_storage);

char* CrtStrCopy(char* destination, const char* source);
char* CrtStrCat(char* destination, const char* source);
char* CrtStrStr(char* text, const char* needle);
const char* CrtStrStr(const char* text, const char* needle);
int CrtSprintf(char* destination, const char* format, ...);
int CrtSnprintf(char* destination, int destination_chars, const char* format, ...);
int CrtFprintf(FILE* stream, const char* format, ...);
FILE* CrtFsopen(const char* path, const char* mode, int share_flags);
FILE* CrtFopenShare(const char* path, const char* mode, int share_flags);
FILE* CrtFopen(const char* path, const char* mode);
int CrtFclose(FILE* stream);
std::size_t CrtFread(void* buffer, std::size_t element_size,
    std::size_t element_count, FILE* stream);
std::size_t CrtFreadUnlocked(void* buffer, std::size_t element_size,
    std::size_t element_count, FILE* stream);
std::size_t CrtFwrite(const void* buffer, std::size_t element_size,
    std::size_t element_count, FILE* stream);
std::size_t CrtFwriteUnlocked(const void* buffer, std::size_t element_size,
    std::size_t element_count, FILE* stream);
int CrtStreamFileDescriptor(FILE* stream);
long CrtFileDescriptorLength(int file_descriptor);
long CrtFtell(FILE* stream);
long CrtFtellUnlocked(FILE* stream);
int CrtFseek(FILE* stream, long offset, int origin);
int CrtFseekUnlocked(FILE* stream, long offset, int origin);

#ifdef _WIN32
HANDLE CrtFindFirstFile(const char* pattern, CrtFindDataA& out);
int CrtFindNextFile(HANDLE handle, CrtFindDataA& out);
int CrtFindClose(HANDLE handle);
std::time_t CrtFileTimeToUnixTime(const FILETIME& file_time);
#endif

void CrtStackProbe(std::size_t bytes);
int CrtAtoi(const char* text);
long long CrtAtoi64(const char* text);
long CrtStrToLong(const char* text, char** end, int radix);
unsigned long CrtStrToUnsignedLong(const char* text, char** end, int radix);
unsigned long CrtStrToLongCore(const char* text, char** end, int radix,
    bool unsigned_result);
unsigned CrtRotateLeft32Thunk(unsigned value, unsigned count);
unsigned CrtRotateLeft32(unsigned value, unsigned count);
void CrtSplitPath(const char* path, char* drive, char* directory,
    char* filename, char* extension);
void* CrtMemMoveBytes(void* destination, const void* source, std::size_t size);
int CrtRemovePath(const char* path);
void CrtRemovePathThunk(const char* path);
void CrtAssertFailed(const char* expression, const char* file, int line);
int CrtMakeDirectory(const char* path);
int CrtFileStatusByDescriptor(int file_descriptor, CrtFileStatus& status);
void CrtRuntimeErrorExitProcess(int message_id);
int* CrtErrnoPointer();
unsigned long* CrtDosErrnoPointer();
int CrtChangeDirectory(const char* path);
char* CrtStrChr(char* text, int character);
const char* CrtStrChr(const char* text, int character);
int UppercaseAsciiFromLowercase(int character);
int CrtToUpper(int character);
int CrtToUpperLocale(int character);
int CrtVsnprintf(char* destination, int destination_chars,
    const char* format, va_list args);
int CrtVsprintf(char* destination, const char* format, va_list args);
int CrtOutputFormatCore(char* destination, std::size_t destination_chars,
    const char* format, va_list args);
int CrtFlushBufferedCharacter(FILE* stream, int character);
int CrtOutputPutChar(std::string& out, int character);
void CrtOutputRepeatChar(std::string& out, int character, int count);
void CrtOutputWriteString(std::string& out, const char* text, int count);
std::uint32_t CrtReadVaArg32(const unsigned char*& cursor);
std::uint64_t CrtReadVaArg64(const unsigned char*& cursor);
std::uint16_t CrtReadVaArgWideChar(const unsigned char*& cursor);
void CrtDebugBreak();
int CrtSetReportMode(int report_type, unsigned mode);
void* CrtSetReportFile(int report_type, void* file);
using CrtDumpClientCallback = void (*)(const void* object, std::size_t bytes);
using CrtReportHookCallback = int (*)(int report_type, const char* message,
    int* result);
CrtDumpClientCallback CrtSetDumpClient(CrtDumpClientCallback callback);
CrtReportHookCallback CrtSetReportHook(CrtReportHookCallback callback);
int CrtDbgReport(int report_type, const char* file, int line,
    const char* module, const char* format, ...);
bool CrtDbgReportDialog(int report_type, const char* file, const char* line,
    const char* module, const char* message);
void DestroyTypeInfoObject(void* object);
void* DeleteTypeInfoObject(void* object, bool free_storage);
const char* TypeInfoRawName(const void* object);
void* InitializeTypeInfoObject(void* object);
void* ReturnTypeInfoObject(void* object);
int CrtSwprintf(wchar_t* destination, const wchar_t* format, ...);
#ifdef _WIN32
using CrtThreadProcedure = unsigned (__stdcall *)(void* context);
HANDLE CrtBeginThreadEx(LPSECURITY_ATTRIBUTES security_attributes,
    std::size_t stack_size, CrtThreadProcedure procedure, void* context,
    DWORD creation_flags, LPDWORD thread_id);
DWORD WINAPI CrtThreadStartThunk(void* context);
#endif
std::size_t CrtWideStringLength(const wchar_t* text);
std::time_t CrtMktime(std::tm* value);
std::time_t CrtMkGmTime(std::tm* value);
std::time_t CrtMakeTimeCore(std::tm* value, bool local_time);
const LocaleTimeData& DefaultLocaleTimeData();
std::string BuildLocaleDayNamesList();
std::string BuildLocaleMonthNamesList();
LocaleTimeData CloneLocaleTimeData(const LocaleTimeData& source);
std::size_t CrtStrftime(char* destination, std::size_t destination_chars,
    const char* format, const std::tm* value);
std::size_t CrtStrftimeWithLocale(char* destination, std::size_t destination_chars,
    const char* format, const std::tm* value, const LocaleTimeData* locale);
void FormatStrftimeToken(char token, const std::tm& value, std::string& out,
    const LocaleTimeData& locale, bool alternate = false);
void StoreStrftimePaddedNumber(int value, unsigned width, std::string& out,
    bool suppress_padding);
void FormatLocaleDateTimePattern(const char* pattern, const std::tm& value,
    std::string& out, const LocaleTimeData& locale);

std::tm* CrtLocalTime(const std::time_t* value);
std::time_t CrtTime(std::time_t* out);

void RunCrtExitTerminators();
void CrtExit(int code);
void CrtCeExit();
void CrtCExit();
void CrtDoExit(int code, bool quick_exit, bool cleanup_only);

void* CrtDebugHeapAlloc(std::size_t size);
void* CrtMallocRetry(std::size_t size);
void* CrtReallocOrExpand(void* memory, std::size_t new_size, bool allow_move);
void* CrtRealloc(void* memory, std::size_t new_size);
void* CrtExpand(void* memory, std::size_t new_size);
void CrtFree(void* memory);
void CrtDebugHeapFree(void* memory);
std::size_t CrtDebugMemorySize(void* memory);
std::size_t CrtMemorySize(void* memory);

using CrtAllocHookCallback = int (*)(int alloc_type, void* user_data,
    std::size_t size, int block_type, long request_number,
    const char* file_name, int line_number);
CrtAllocHookCallback CrtSetAllocHook(CrtAllocHookCallback callback);
long CrtSetBreakAlloc(long allocation);
void CrtSetDbgBlockType(void* memory, int block_type);
bool CrtCheckBytes(const void* memory, unsigned char expected, std::size_t size);
bool CrtCheckMemory();
int CrtSetDebugFlag(int flag);
using CrtClientObjectCallback = void (*)(void* memory, void* context);
void CrtDoForAllClientObjects(CrtClientObjectCallback callback, void* context);
bool CrtIsValidHeapPointer(void* memory);
void CrtMemCheckpoint(CrtMemState& state);
bool CrtMemDifference(CrtMemState& diff, const CrtMemState& old_state,
    const CrtMemState& new_state, bool include_crt_blocks);
void CrtDumpAllObjectsSince(const CrtMemState* state);
void CrtDumpBlockData(const void* memory, std::size_t size);
bool CrtDumpMemoryLeaks();
void CrtMemDumpStatistics(const CrtMemState& state);
void* CrtMemMove(void* destination, const void* source, std::size_t size);

void InitializeCrtFloatingPoint();
void InitializeCrtFloatingPointTrapTable();
int SetCrtMathErrorMode(int mode);
double CrtSin(double value);
double CrtCos(double value);
double CrtAtan(double value);
double CrtFabs(double value);
double CrtFloor(double value);
double CrtCeil(double value);
double CrtModf(double value, double* integer_part);
char* CrtGcvt(double value, int digits, char* destination);
void CrtPurecall();
bool LegacyFdivBugProbeFallback();
char* InsertLocaleDecimalPointBeforeExponent(char* text);
int FormatScientificFloat(double value, char* buffer, std::size_t buffer_chars,
    int precision, bool uppercase);
char* BuildScientificFloatString(char* buffer, std::size_t buffer_chars,
    const char* digits, int exponent, bool negative, bool uppercase);
int FormatFixedFloat(double value, char* buffer, std::size_t buffer_chars,
    int precision);
char* BuildFixedFloatString(char* buffer, std::size_t buffer_chars,
    const char* digits, int decimal_position, bool negative);
int FormatGeneralFloat(double value, char* buffer, std::size_t buffer_chars,
    int precision, bool uppercase);
int MapFpuStatusToMathError(unsigned status_word);
unsigned ClassifyDoubleExponentBits(double value);
double StartTwoArgErrorHandling(double left, double right, unsigned control_word);
double HandleOneArgMathDomainError(const char* name, double argument,
    double fallback, unsigned control_word);
double HandleTwoArgMathDomainError(const char* name, double left, double right,
    double fallback, unsigned control_word);
double HandleOneArgFpuException(int exception_flags, const char* name,
    double argument, double fallback, unsigned control_word);
double HandleTwoArgFpuException(int exception_flags, const char* name,
    double left, double right, double fallback, unsigned control_word);
void BuildFpuExceptionRecord(CrtFpuExceptionState& state,
    unsigned exception_flags, unsigned control_word, unsigned status_word,
    double argument1, double argument2, double result);
bool ApplyFpuExceptionMask(int exception_flags, double& value,
    unsigned control_word);
int MathErrorTypeFromExceptionFlags(unsigned flags);
double SetDoubleBiasedExponent(double value, int exponent);
int GetDoubleUnbiasedExponent(double value);
double ScaleDoubleByPowerOfTwo(double value, int exponent_delta);
double SetDoubleRawExponent(double value, unsigned raw_exponent);
int ClassifyDoubleSpecial(double value);
double NormalizeDoubleMantissa(double value, int* exponent);
unsigned ReadFpuStatusWord();
unsigned ClearFpuStatusWord();
unsigned ReadFpuControlWord();
void RaiseFpuStatusFlags(unsigned flags);
void CrtAbortWithThreadCallback();
void CrtAbortAfterThreadCallback();
void CrtTerminateAfterUnhandledException();
void AbortThunk();
void RestoreAbortExceptionFrame();
int CompatMapString(unsigned locale_id, unsigned flags, const char* source,
    int source_chars, char* destination, int destination_chars,
    unsigned code_page, bool fail_invalid_chars);
int CrtBoundedStringLength(const char* text, int max_chars);
int CrtFillStreamBuffer(FILE* stream);
int CrtReadFileDescriptorLocked(int file_descriptor, void* buffer,
    unsigned bytes);
int CrtReadFileDescriptorNoLock(int file_descriptor, void* buffer,
    unsigned bytes);
int AllocateCrtFileDescriptor();
int ClearCrtFileDescriptorHandle(int file_descriptor);
intptr_t CrtGetOsFileHandle(int file_descriptor);
int CrtOpenOsFileHandle(void* os_handle, unsigned flags);
void LockCrtFileDescriptor(int file_descriptor);
int CrtSeekFileDescriptorLocked(int file_descriptor, long offset, int origin);
long CrtSeekFileDescriptorNoLock(int file_descriptor, long offset, int origin);
void InitializeCrtIoTable();
int CrtWriteFileDescriptorLocked(int file_descriptor, const void* buffer,
    unsigned bytes);
int CrtWriteFileDescriptorNoLock(int file_descriptor, const void* buffer,
    unsigned bytes);
std::string CrtSetLocaleCategory(int category, const char* locale_name);
bool CrtApplyLocaleCategory(int category, const char* locale_name);
std::string BuildCompositeLocaleString();
std::string ResolveLocaleName(const char* locale_name,
    CrtLocaleNameParts* parts = nullptr);
int CrtLocaleNoOp();
bool ParseLocaleNameParts(const char* locale_name, CrtLocaleNameParts& parts);
int CxxFrameHandler(void* exception_record, void* registration,
    void* context, void* dispatcher_context, void* function_info,
    int catch_depth, void* nested_registration, bool unwind_target);
void DispatchCxxException(void* exception_record, void* registration,
    void* context, void* dispatcher_context, void* function_info,
    bool destruct_exception_object, int catch_depth, void* nested_registration);
void TranslateAndDispatchSehException(void* exception_record, void* registration,
    void* context, void* dispatcher_context, void* function_info,
    int current_state, int catch_depth, void* nested_registration);
bool CxxCatchTypeMatches(const char* handler_type, const char* thrown_type,
    unsigned handler_attributes, unsigned thrown_attributes);
void UnwindCxxFrameToState(void* registration, void* dispatcher_context,
    void* function_info, int target_state);
int CallCxxCatchHandler(void* exception_record, void* registration,
    void* context, void* function_info, void* handler, int catch_depth,
    unsigned nlg_code);
void RestoreCatchContextAndDestruct(void* exception_record, bool destruct_object);
int FinishCatchHandlerCall();
bool IsPureCxxRethrow(const void* exception_record);
void CopyExceptionObjectToCatch(void* destination, const void* source,
    std::size_t size);
char* CrtFindMbcsStringOneOf(char* text, const char* characters);
int AsciiUpperToLower(int character);
int CrtToLower(int character);
int CrtToLowerLocale(int character);
long long CrtFtell64(FILE* stream);
long long CrtFtell64Unlocked(FILE* stream);
CrtSignalHandler CrtSignal(int signal, CrtSignalHandler handler);
bool CrtConsoleCtrlSignalHandler(unsigned control_type);
int CrtRaiseSignal(int signal);
CrtSignalHandler* FindSignalAction(int signal);
int* CrtFpecodePointer();
void** CrtExceptionPointerSlot();
int CrtMessageBox(const char* text, const char* title, unsigned flags);
int CrtSetStreamBuffering(FILE* stream, char* buffer, unsigned mode,
    unsigned size);
int CrtOpenFileDescriptor(const char* path, unsigned open_flags,
    unsigned share_flags, unsigned permission);
CrtExceptionSignalAction* FindExceptionSignalAction(unsigned exception_code,
    CrtExceptionSignalAction* actions, std::size_t action_count);
const char* GetCommandLineArgumentsStart(const char* command_line = nullptr);
std::vector<std::string> InitializeEnvironmentVector(
    const char* environment_block = nullptr);
std::vector<std::string> InitializeArgumentVector(
    const char* command_line = nullptr);
void ParseCommandLineArguments(const char* command_line,
    std::vector<std::string>& arguments);
std::string CloneAnsiEnvironmentBlock();
void CrtPrintRuntimeMessageBanner(int message_id);
const char* CrtGetRuntimeErrorMessage(int message_id);
unsigned CrtToLowerMbcs(unsigned character);
#ifdef _WIN32
LONG WINAPI CrtUnhandledExceptionFilterThunk(
    EXCEPTION_POINTERS* exception_pointers);
#else
int CrtUnhandledExceptionFilterThunk(void* exception_pointers);
#endif
void InstallCrtUnhandledExceptionFilter();
void RestoreCrtUnhandledExceptionFilter();
int CrtWideOutputFormatCore(CrtWideOutputWriter writer, void* context,
    const wchar_t* format, va_list args);
int CrtWriteWideOutputChar(wchar_t character, CrtWideOutputWriter writer,
    void* context, int& count);
int CrtWriteWideOutputSpan(const wchar_t* text, int count,
    CrtWideOutputWriter writer, void* context, int& written_count);
std::uint32_t CrtReadOutputArgument32(const unsigned char*& cursor);
std::uint64_t CrtReadOutputArgument64(const unsigned char*& cursor);
int CrtCompareStringCompat(unsigned locale_id, unsigned flags, const char* left,
    int left_chars, const char* right, int right_chars, unsigned code_page);
int CrtAnsiStringLengthBounded(const char* text, int max_chars);
char* CrtFindCharInSet(char* text, const char* characters);
const char* CrtFindCharInSet(const char* text, const char* characters);
bool CrtGetStringTypeCompat(unsigned info_type, const char* text, int chars,
    unsigned short* types, unsigned code_page, unsigned locale_id,
    bool fail_invalid);
double CrtRoundToNearest(double value);
void CrtFloatConversionThunk(void* destination, const void* source, int mode);
double CrtLogb(double value);
double CrtNextAfter(double value, double target);
bool CrtIsFiniteDouble(double value);
bool CrtIsNanDouble(double value);
int CrtFpClassifyDouble(double value);
std::size_t CrtStringSpanIncluding(const char* text, const char* characters);
std::size_t CrtStringSpanExcluding(const char* text, const char* characters);
CrtDecimalConversion ConvertDoubleToDecimalString(double value, int precision);
CrtLongDouble80 ConvertDoubleToLongDoubleBits(double value);
bool CrtGetStreamBuffer(FILE* stream, std::vector<char>& storage);
int CrtWideCharToMultibyteLocked(char* destination, wchar_t character,
    unsigned code_page = 0, int destination_chars = 8,
    bool fail_invalid = true);
int CrtWideCharToMultibyteNoLock(char* destination, wchar_t character,
    unsigned code_page = 0, int destination_chars = 8,
    bool fail_invalid = true);
int CrtCloseAllStreams();
int CrtFlushFileDescriptor(int file_descriptor);
char* CrtGetenvLocked(const char* name);
unsigned ReadFpuControlWordMapped();
unsigned ReadSseControlWordMapped();
unsigned CrtControlFpMasked(unsigned new_value, unsigned mask);
void CrtSetDefaultFpuPrecision();
unsigned MapX87ControlWordToCrt(unsigned control_word);
unsigned MapCrtControlWordToX87(unsigned control_word);
unsigned MapSseControlWordToCrt(unsigned control_word);
bool MantissaHasNoBitsAfter(const CrtMantissa96& mantissa, int bit_index);
bool RoundMantissaToBit(CrtMantissa96& mantissa, int bit_index);
int PackExtendedToFloatOrDouble(const CrtLongDouble80& value, void* output,
    int precision_bits, bool output_double);
double ConvertExtendedToDouble(const CrtLongDouble80& value);
float ConvertExtendedToFloat(const CrtLongDouble80& value);
bool RoundExtendedPrecision(CrtLongDouble80& value, int bit_index);
double ParseDecimalStringToDouble(const char* text);
long double ParseDecimalStringToLongDouble(const char* text);
float ParseDecimalStringToFloat(const char* text);
void RoundDecimalDigits(std::string& digits, int precision, int& decimal_point);
void HandleMatherrAndFpuException(int operation, int error_type,
    double argument1, double argument2, double result);
bool InitializeLocaleTimeDataFromWin32(LocaleTimeData& data,
    unsigned locale_id = 0);
LocaleNumericData InitializeLocaleNumericData(unsigned locale_id = 0);
bool InitializeLocaleMonetaryData(LocaleMonetaryData& data,
    unsigned locale_id = 0);
bool LoadLocaleMonetaryData(LocaleMonetaryData& data, unsigned locale_id);
void FreeLocaleMonetaryData(LocaleMonetaryData& data);
bool InitializeLocaleCtypeTables(unsigned code_page,
    std::vector<unsigned short>& ctype_table,
    std::vector<unsigned short>& case_map_table);
int CrtLocaleInitializationNoOp();
bool ResolveLocaleTriple(const char* locale_name, CrtResolvedLocale* out);
bool MapLocaleAlias(const char* input, std::string& out);
bool ResolveLanguageCountryLocale(const char* language, const char* country,
    CrtResolvedLocale& out);
bool EnumLanguageCountryLocaleProc(const char* locale_id_text,
    const char* language, const char* country, CrtResolvedLocale& out);
bool ResolveLanguageOnlyLocale(const char* language, CrtResolvedLocale& out);
bool EnumLanguageOnlyLocaleProc(const char* locale_id_text,
    const char* language, CrtResolvedLocale& out);
bool ResolveCountryOnlyLocale(const char* country, CrtResolvedLocale& out);
bool EnumCountryOnlyLocaleProc(const char* locale_id_text,
    const char* country, CrtResolvedLocale& out);
void UseUserDefaultLocale(CrtResolvedLocale& out);
unsigned ResolveLocaleCodePage(const char* code_page_text, unsigned locale_id);
bool IsNonCountryLocale(unsigned locale_id);
bool IsPrimaryLanguageLocale(unsigned locale_id, bool strict_primary);
bool IsWindowsNtPlatform();
unsigned ParseHexLocaleId(const char* text);
bool ValidateReadPointer(const void* memory, std::size_t bytes);
bool ValidateWritePointer(void* memory, std::size_t bytes);
long long CrtSeekFileDescriptor64Locked(int file_descriptor, long long offset,
    int origin);
long long CrtSeekFileDescriptor64NoLock(int file_descriptor, long long offset,
    int origin);
int CrtChangeFileSizeLocked(int file_descriptor, long long size);
int CrtChangeFileSizeNoLock(int file_descriptor, long long size);
bool CrtIsMbbAlpha(unsigned character);
bool CrtIsMbbAlnum(unsigned character);
bool CrtIsMbbPunct(unsigned character);
bool CrtIsMbbGraph(unsigned character);
bool CrtIsMbbPrint(unsigned character);
bool CrtIsMbbKalnum(unsigned character);
bool CrtIsMbbKprint(unsigned character);
bool CrtIsMbbPunctOrKana(unsigned character);
bool CrtIsMbcsLeadByte(unsigned character);
bool CrtIsMbcsTrailByte(unsigned character);
bool CrtTestMbcsByteType(unsigned character, unsigned ctype_mask,
    unsigned mbcs_mask);
int CrtMbtowcLocked(wchar_t* destination, const char* source,
    std::size_t bytes);
int CrtMbtowcNoLock(wchar_t* destination, const char* source,
    std::size_t bytes);
wint_t CrtFputwcLocked(wchar_t character, FILE* stream);
wint_t CrtFputwcNoLock(wchar_t character, FILE* stream);
void CrtFputwcThunk(wchar_t character, FILE* stream);
double CrtScaleDoubleByPowerOfTwo(double value, int exponent);
CrtDecimalConversion ConvertExtendedToDecimalString(
    const CrtLongDouble80& value, int precision, bool fixed_digits);
bool RebuildEnvironmentFromWide(const std::vector<std::wstring>& wide_environment,
    std::vector<std::string>& ansi_environment);
CrtLongDouble80 AccumulateDecimalDigitsToExtended(const char* digits, int count);
CrtParsedFloat ParseFloatingPointStringToExtended(const char* text,
    bool allow_exponent_sign = true);
unsigned ParseAndRoundFloatingPointString(const char* text, int precision,
    CrtLongDouble80& out);
std::string LoadLocaleStringValue(unsigned locale_id, unsigned locale_type,
    bool string_value);
LocaleMonetaryData* GetLocaleMonetaryDataPointer();
bool GetStringTypeWideCompat(unsigned info_type, const wchar_t* text, int chars,
    unsigned short* types, unsigned code_page, unsigned locale_id);
int CompareLocaleStringsCaseInsensitivePrefix(const char* left,
    const char* right, int count);
int CrtSetFileTextModeLocked(int file_descriptor, int mode);
int CrtSetFileTextModeNoLock(int file_descriptor, int mode);
wint_t CrtFlushWideStreamBuffer(wchar_t character, FILE* stream);
void MultiplyExtendedTemporary(CrtLongDouble80& value,
    const CrtLongDouble80& multiplier);
void ScaleExtendedByPowerOfTen(CrtLongDouble80& value, int exponent,
    bool initialize);
int CrtSetEnvironmentEntry(const char* entry, bool update_process);
int GetLocaleInfoWideCompat(unsigned locale_id, unsigned locale_type,
    wchar_t* destination, int destination_chars, unsigned code_page = 0);
int GetLocaleInfoAnsiCompat(unsigned locale_id, unsigned locale_type,
    char* destination, int destination_chars, unsigned code_page = 0);
int CrtStreamFileDescriptor(FILE* stream);

void InitializeCrtFileTable();
void ShutdownCrtStdio();
void LockCrtStream(FILE* stream);
void LockCrtStreamByIndex(int stream_index, FILE* stream);
void UnlockCrtStream(FILE* stream);
void UnlockCrtStreamByIndex(int stream_index, FILE* stream);
int CrtCloseFileDescriptor(int file_descriptor);
void CrtFreeStreamBuffer(FILE* stream);
int CrtFlushStream(FILE* stream);
int CrtFlushStreamNoLock(FILE* stream);
int CrtFlushStreamBuffer(FILE* stream);
int CrtFlushAllStreamsOnExit();
int CrtFlushAllStreams(bool flush_all);
bool CrtInstallTemporaryStreamBuffer(FILE* stream);
void CrtRemoveTemporaryStreamBuffer(bool installed, FILE* stream);
bool InitializeCrtThreadData();
void ShutdownCrtThreadData();
void InitializeCrtThreadDataBlock(CrtThreadData& data);
CrtThreadData& CrtGetThreadData();
void FreeCrtThreadData(CrtThreadData* data);
unsigned CrtCurrentThreadId();
void* CrtCurrentThreadHandle();
FILE* CrtOpenFileStream(const char* path, const char* mode, int share_flags,
    FILE* stream);
FILE* CrtAllocateStreamSlot();
void InitializeCrtTimeZoneOnce();
void RefreshCrtTimeZone();
void RebuildCrtTimeZoneFromEnvironment();
bool CrtIsDaylightSavingsTime(const std::tm& value);
bool CrtIsDaylightSavingsTimeLocked(const std::tm& value);
void ComputeDaylightTransitionDay(bool daylight_transition, bool week_based,
    int year, int month, int week, int day_of_week, int day_of_month,
    int hour, int minute, int second, int millisecond);
std::time_t CrtEncodeLocalTimeFields(int year, int month, int day, int hour,
    int minute, int second, int daylight_savings);
void InitializeCrtLockTable();
void ShutdownCrtLockTable();
void LockCrtRuntime(int lock_index);
void UnlockCrtRuntime(int lock_index);
void CrtFatalAppExit(const char* message);
using CrtNewHandler = int (*)(std::size_t size);
CrtNewHandler CrtGetNewHandler();
void UnlockHeapAfterSbhAlloc();
void* FinishSbhAllocOrFallback(std::size_t size);
void UnlockHeapAfterLookasideAlloc();
void* FinishLookasideAllocOrHeap(std::size_t size);
void* HeapAllocRoundedFallback(std::size_t size);
int CrtHeapAddNoOp();
void* CrtHeapReallocInPlace(void* memory, std::size_t size);
void UnlockHeapAfterSbhRealloc();
void* FinishSbhReallocFallback(void* memory, std::size_t size);
void UnlockHeapAfterLookasideRealloc();
void* FinishLookasideReallocFallback(void* memory, std::size_t size);
void* CrtHeapRealloc(void* memory, std::size_t size);
void UnlockHeapAfterSbhReallocRetry();
void* RetrySbhReallocAfterNewHandler(void* memory, std::size_t size);
void UnlockHeapAfterLookasideReallocRetry();
void* RetryLookasideReallocAfterNewHandler(void* memory, std::size_t size);
void RestoreHeapExceptionFrame();
void CrtHeapFree(void* memory);
void UnlockHeapAfterSbhFree();
void FinishSbhFreeFallback(void* memory);
void UnlockHeapAfterLookasideFree();
void FinishLookasideFreeFallback(void* memory);
void UnlockHeapAfterSbhCheck();
void UnlockHeapAfterLookasideCheck();
int FinishHeapValidate();
std::size_t CrtGetSmallBlockThreshold();
bool CrtSetSmallBlockThreshold(std::size_t threshold);
void* SbhFindRegionForPointer(void* memory);
bool SbhValidatePointerInRegion(void* region, void* memory);
void SbhFreeBlock(void* region, void* memory);
void* SbhAllocateBlock(std::size_t size);
void* SbhCreateRegion();
int SbhCommitRegionPage(void* region);
bool SbhResizeBlock(void* region, void* memory, std::size_t size);
void SbhReleaseDeferredPage();
int SbhValidateHeap();
std::size_t LookasideGetThreshold();
bool LookasideSetThreshold(std::size_t threshold);
void* LookasideCreateRegion();
void LookasideDestroyRegion(void* region);
void LookasideReleaseFreePages(int page_count);
void* LookasideFindBlock(void* memory, void** region, unsigned* page);
void LookasideFreeBlock(void* region, unsigned page, void* block);
void* LookasideAllocateBlock(unsigned units);
void* LookasideAllocateFromPage(void* page, unsigned free_units, unsigned units);
bool LookasideResizeBlockInPlace(void* region, void* page, void* block,
    unsigned units);
int LookasideValidateHeap();
unsigned GetProcessSubsystemVersion();
int SelectCrtHeapMode();
bool InitializeCrtHeap(bool growable);
void ShutdownCrtHeap();
unsigned CrtIsCharType(int character, unsigned mask);

using CrtOnExitFunction = void (*)();
CrtOnExitFunction CrtRegisterOnExitFunction(CrtOnExitFunction function);
int CrtAtexit(CrtOnExitFunction function);
void InitializeCrtOnExitTable();
void CrtSrand(unsigned seed);
int _rand();
char* StrUpperAscii(char* text);
int StrCaseCompareAscii(const char* lhs, const char* rhs);
char* StrTokDelimiterSet(char* value, const char* delimiters);
void __initterm(CrtOnExitFunction* first, CrtOnExitFunction* last);
void __exit(int code);
void* _malloc(std::size_t size);
void* _calloc(std::size_t count, std::size_t size);
long __ftol(double value);
long long __allshr(long long value, unsigned shift);
long long __allmul(long long lhs, long long rhs);
unsigned long long __allshl(unsigned long long value, unsigned shift);
int _strncmp(const char* lhs, const char* rhs, std::size_t count);
char* _strrchr(char* text, int character);
const char* _strrchr(const char* text, int character);
long _labs(long value);
char* __ultoa(unsigned long value, char* buffer, int radix);
char* __ui64toa(unsigned long long value, char* buffer, int radix);
int _fgetpos(FILE* stream, fpos_t* position);
void __amsg_exit(int message_id);
CrtNewHandler _set_new_handler(CrtNewHandler handler);
int __callnewh(std::size_t size);
int __heapset(unsigned fill);
void __setdefaultprecision();
int __ms_p5_mp_test_fdiv();
int __positive(const double* value);
void __fassign(int negative, char* destination, const char* digits);
int __cfltcvt(const double* value, char* buffer, std::size_t buffer_chars,
    int format, int precision, int uppercase);
void __shift(char* text, int right);
double __fload_withFB(double value, int flags);
double __math_exit();
double __startOneArgErrorHandling(double value = 0.0);
void __unlock_fhandle(int file_descriptor);
char* __strcats(char* destination, int count, ...);
void __FF_MSGBANNER();
const char* __GET_RTERRMSG(int message_id);
void write_multi_char(int character, int count, FILE* stream,
    void* context = nullptr);
char* __strrev(char* text);
double __copysign(double value, double sign);
double __chgsign(double value);
int __isatty(int file_descriptor);
unsigned __controlfp(unsigned new_value, unsigned mask);
void __CopyMan(CrtMantissa96& destination, const CrtMantissa96& source);
void __FillZeroMan(CrtMantissa96& value);
int __IsZeroMan(const CrtMantissa96& value);
void __fptrap();
int __matherr(void* exception_record);
int _ValidateExecute(void* callback);
int __ismbbkana(unsigned character);
unsigned ___addl(unsigned left, unsigned right, unsigned* out);

using EhObjectCallback = void (*)(void* object);
using EhCatchCopyCallback = void (*)(void* destination, const void* source);
using EhContinuationCallback = void (*)();
void EhVectorConstructorIterator(void* first, std::size_t element_size,
    int element_count, EhObjectCallback constructor, EhObjectCallback destructor);
void EhVectorDestructorIterator(void* first, std::size_t element_size,
    int element_count, EhObjectCallback destructor);
void EhVectorDestructorRange(void* first, std::size_t element_size,
    int element_count, EhObjectCallback destructor);
void FinishEhVectorConstructorOnException(void* first,
    std::size_t element_size, int constructed_count, EhObjectCallback destructor);
void FinishEhVectorConstructor();
void FinishEhVectorDestructorOnException(void* first,
    std::size_t element_size, int element_count, EhObjectCallback destructor);
void FinishEhVectorDestructor();
void CxxContinueAfterCatchUnwind(EhContinuationCallback continuation);
void _CallMemberFunction0(void* object, EhObjectCallback callback);
void CallCatchCopyFunction(void* destination, const void* source,
    EhCatchCopyCallback callback);
void CallCatchCopyFunctionIndirect(void* destination, const void* source,
    EhCatchCopyCallback* callback);
void CxxUnwindFrameAndRestore(void* registration, void* exception_record);
int CallCxxCatchBlockWithFrame(EhContinuationCallback handler,
    void* exception_record, void* registration, void* frame, int catch_depth);
int CxxCatchFrameHandler(void* exception_record, void* frame,
    void* dispatcher_context);
int CallCxxExceptionTranslator(void* exception_record, void* registration,
    void* context, void* dispatcher_context, void* function_info,
    int catch_depth, void* nested_registration);
int CxxExceptionTranslatorFilter(void* exception_record, void* frame,
    void* dispatcher_context);
void* FindCxxTryBlockRange(void* function_info, int try_level,
    int current_state, unsigned* first, unsigned* last);
void __global_unwind2(void* registration);
int __abnormal_termination();
void __NLG_Notify1(std::uintptr_t code);
void NotifyLocalUnwind(std::uintptr_t code = 0);

std::size_t _strlen(const char* text);
void* _memset(void* destination, int value, std::size_t size);
char* _asctime(const std::tm* value);
char* _strncpy(char* destination, const char* source, std::size_t count);
int AsciiToInt(const char* text);
int _strcmp(const char* lhs, const char* rhs);
int _memcmp(const void* lhs, const void* rhs, std::size_t count);
char* _strncat(char* destination, const char* source, std::size_t count);
std::tm* _gmtime(const std::time_t* value);
void* __calloc_dbg(std::size_t count, std::size_t size);
void* __malloc_dbg(std::size_t size, int block_type = 1,
    const char* file_name = nullptr, int line_number = 0);
void* __realloc_dbg(void* memory, std::size_t size);
void __free_dbg(void* memory);
bool __CrtIsMemoryBlock(const void* memory, std::size_t size,
    long* request_number = nullptr, const char** file_name = nullptr,
    int* line_number = nullptr);
int __CrtIsValidPointer(const void* memory, std::size_t bytes,
    int write_access);
void __local_unwind2(void* frame, int target_state);
int __futime(int file_descriptor, const std::time_t* access_time,
    const std::time_t* modify_time);
void __dosmaperr(unsigned long error);
int HandleCrtRenamePath(const char* old_path, const char* new_path);
unsigned getSystemCP();
int setSBCS();
char* xtoa(unsigned long value, char* buffer, unsigned radix, bool negative = false);
char* x64toa(unsigned long long value, char* buffer, unsigned radix,
    bool negative = false);
char* __strdup(const char* text);
char* __itoa(int value, char* buffer, int radix);
char* __ltoa(long value, char* buffer, int radix);
char* __i64toa(long long value, char* buffer, int radix);
int __close_lk(int file_descriptor);
void* __malloc_base(std::size_t size);
void* __nh_malloc_base(std::size_t size);
void* __heap_alloc_base(std::size_t size);
int __heapchk();
int ___sbh_heap_init();
void __cropzeros(char* text);
int __umatherr(void* exception_record);
int __set_osfhnd(int file_descriptor, intptr_t os_handle);
void __ioterm();
std::string ___lc_lctostr(unsigned locale_id, unsigned locale_type);
void CatchIt();
void ___DestructExceptionObject(void* object);
int __XcptFilter(unsigned code, void* exception_pointers);
unsigned long long __aulldiv(unsigned long long dividend,
    unsigned long long divisor);
unsigned long long __aullrem(unsigned long long dividend,
    unsigned long long divisor);
char* __getenv_lk(const char* name);
void __IncMan(CrtMantissa96& mantissa);
void __ShrMan(CrtMantissa96& mantissa);
int ___init_time();
void ___free_lc_time();
std::string fix_grouping(const char* grouping);
int crtGetLocaleInfoA(unsigned locale_id, unsigned locale_type, char* buffer,
    int buffer_chars);
std::size_t _GetPrimaryLen(const char* text);
void ___add_12(CrtMantissa96& lhs, const CrtMantissa96& rhs);
void ___shl_12(CrtMantissa96& value, unsigned count);
void ___shr_12(CrtMantissa96& value, unsigned count);
char* findenv(const char* name);
std::vector<std::string> copy_environ();
int __mbscoll(const unsigned char* left, const unsigned char* right);
int __mbsicoll(const unsigned char* left, const unsigned char* right);
int __mbsnbicoll(const unsigned char* left, const unsigned char* right,
    std::size_t count);
char* __get_fname(const char* path);

} // namespace ranker
