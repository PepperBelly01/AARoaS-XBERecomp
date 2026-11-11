@echo off
rem Runs build.bat without prompts and writes all output to build_log.txt
cd /d "%~dp0"
set CI=1
title AARoaS build - running, output goes to build_log.txt
echo Building... all output goes to build_log.txt. This window closes when done.
echo BUILD_STARTED %date% %time% > build_log.txt
call "%~dp0build.bat" >> build_log.txt 2>&1
echo BUILD_EXIT %errorlevel% %date% %time% >> build_log.txt
