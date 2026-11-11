@echo off
rem Starts the built game with the Direct3D 12 debug layer: its errors and warnings go into the log (slower)
set "XBE_D3D12_DEBUG=1"
call "%~dp0run_game.bat"
