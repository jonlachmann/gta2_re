#!/bin/sh
# GTA2: the game, with "Network game" on its main menu. Extra arguments go to the game, e.g. --connect host:27920 --name you
cd "$(dirname "$0")" && exec ./gta2_port "$@"
