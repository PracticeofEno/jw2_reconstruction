#pragma once

namespace ranker {

// Startup diagnostics intentionally remain available throughout the frontend
// and gameplay wiring so early failures can be recorded before UI is ready.
const char* startup_log_path();
void append_startup_log(const char* format, ...);

void install_startup_exception_logger_once();

// Locate the shipped data beside the executable or a nearby RankerOCPV_Win
// directory and make it the process working directory.
bool ensure_ranker_data_working_directory();

bool CpuSupportsMmx();
void WriteStartupTimestampLog(const char* path = nullptr);
bool VerifySetupVersionData();
bool VerifySetupOrFindJw208Archive();
bool background_test_mode_enabled();

} // namespace ranker
