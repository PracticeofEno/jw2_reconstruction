#include "ranker_types.h"
#include "ranker_imports.h"
#include "ranker_winmain.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR command_line, int show_cmd) {
    ranker::log_import_surface();
    return ranker::run_reconstructed_winmain(instance, command_line, show_cmd);
}
#else
int main() {
    ranker::log_import_surface();
    return ranker::run_reconstructed_winmain();
}
#endif
