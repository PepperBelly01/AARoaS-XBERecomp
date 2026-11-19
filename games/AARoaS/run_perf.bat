@echo off
rem Starts the built game with the per second performance log on (XBE_PERF: where each frame's time goes, "perf:"
rem lines in run_log.txt) and records how it exited
set XBE_PERF=1
cd /d "%~dp0out\build\release"
echo RUN_STARTED %date% %time% > "%~dp0run_log.txt"
"%~dp0out\build\release\AARoaS.exe" >> "%~dp0run_log.txt" 2>&1
echo RUN_EXIT %errorlevel% %date% %time% >> "%~dp0run_log.txt"
