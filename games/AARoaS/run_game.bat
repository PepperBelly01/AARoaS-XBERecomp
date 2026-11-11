@echo off
rem Starts the built game and records how it exited
cd /d "%~dp0out\build\release"
echo RUN_STARTED %date% %time% > "%~dp0run_log.txt"
"%~dp0out\build\release\AARoaS.exe" >> "%~dp0run_log.txt" 2>&1
echo RUN_EXIT %errorlevel% %date% %time% >> "%~dp0run_log.txt"
