@echo off
rem Starts the built game with frame capture on: F3 saves the next frame's draws under out\build\release\capture
set "XBE_CAPTURE_DIR=capture"
call "%~dp0run_game.bat"
