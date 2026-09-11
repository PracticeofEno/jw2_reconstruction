@echo off
setlocal
"%~dp0.venv-ai\Scripts\python.exe" -B "%~dp0ranker_reconstructed_code\tools\ai\ranker_commander_watch.py" --work-dir "%~dp0debug_artifacts\commander\combat_support_20260911_121416\watch_selection" %*
if errorlevel 1 (
    echo.
    echo Replay could not be opened. See the message above.
    pause
    exit /b 1
)
endlocal
