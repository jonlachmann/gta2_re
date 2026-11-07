#!/bin/sh
# A GTA2 network game server, no window. Defaults: port 27920, map downtown-6p, 6 players.
# e.g. ./gta2-server.sh --map mp1-6p --police 6 --timelimit 10
cd "$(dirname "$0")" && exec ./gta2_port --server "$@"
