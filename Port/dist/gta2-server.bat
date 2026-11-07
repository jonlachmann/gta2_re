@echo off
rem A GTA2 network game server. Defaults: port 27920, map downtown-6p, 6 players.
rem e.g. gta2-server.bat --map mp1-6p --police 6 --timelimit 10
cd /d "%~dp0"
gta2_port.exe --server %*
