@echo off
rem GTA2: the game, with "Network game" on its main menu. Extra arguments go to the game, e.g. --connect host:27920 --name you
cd /d "%~dp0"
start "" gta2_port.exe %*
