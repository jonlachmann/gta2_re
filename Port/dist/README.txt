GTA2, the cross-platform port with a network game (github.com/jonlachmann/gta2_re, the netcode branch)

1. Game data: this zip has no game files. Copy the "data" folder of a GTA2 install here, next to the
   game, so that you get   data/   (with data/audio, data/frontend, *.gmp, *.sty, ...).

     gta2_port (gta2_port.exe)   the game (client and server)
     gta2.sh / gta2.bat          start the game
     gta2-server.sh / .bat       start a server (no window)
     gta2.ini                    settings (keys, window size, network address and name)
     player/                     save slots and high scores (keep it, the game needs the folder)
     data/                       <- the game data goes here

2. Play:   ./gta2.sh   (Windows: gta2.bat)
   Main menu -> NETWORK GAME -> set ADDRESS (server IP, port 27920 by default, or host:port)
   and NAME -> JOIN GAME. Esc in game asks to leave (Y / N).
   Or straight from the command line:   ./gta2.sh --connect 192.168.1.10:27920 --name you

3. Host:   ./gta2-server.sh --map mp1-6p      (UDP port 27920; --port to change it)
   Maps: downtown-6p, industrial-6p, mp1-6p, mp2-6p, mp5-6p, res-6p ...
   Other options: --players 1-6, --timelimit (minutes), --fraglimit, --police (stars), --maps a,b,c

Per platform:
   Linux:    64-bit x86; SDL3 comes along (libSDL3.so.0 next to the game). Needs glibc 2.39 or newer
             (Ubuntu 24.04, Debian 13, Fedora 40, or newer).
   macOS:    Apple silicon; SDL3 comes along. The download is unsigned: once, in this folder,
               xattr -dr com.apple.quarantine .
   Windows:  32-bit; SDL3.dll comes along. The server shows no console output.
