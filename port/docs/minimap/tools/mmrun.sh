#!/bin/sh
# mmrun.sh <game dir> <name> <W> <H> <lighting 0 noon|1 dusk> <frames csv> <exit frame>
# Headless capture of the Linux port in <game dir> (a copy: its registry file is edited), frames as PNG in <game dir>/cap_<name>.
G=$1; N=$2; W=$3; H=$4; L=$5; F=$6; X=$7
R=$G/gta2_registry.ini
hx() { printf %08x "$1"; }
sed -i "s/^lighting = dword:.*/lighting = dword:0000000$L/; s/^full_width = .*/full_width = dword:$(hx $W)/; s/^full_height = .*/full_height = dword:$(hx $H)/; s/^port_fullscreen_width = .*/port_fullscreen_width = dword:$(hx $W)/; s/^port_fullscreen_height = .*/port_fullscreen_height = dword:$(hx $H)/; s/^port_window_width = .*/port_window_width = dword:$(hx $W)/; s/^port_window_height = .*/port_window_height = dword:$(hx $H)/; s/^window_width = .*/window_width = dword:$(hx $W)/; s/^window_height = .*/window_height = dword:$(hx $H)/" "$R"
cd "$G" && rm -rf "cap_$N" && mkdir -p "cap_$N"
env SDL_AUDIO_DRIVER=disk SDL_AUDIO_DISK_OUTPUT_FILE="$G/audio.raw" \
    GTA2_INPUT="${INPUT:-100:Return,160:Return,300:Right:12,330:Up:120,455:Left:12,470:Up:100,575:Right:12}" \
    GTA2_EXIT_AT="$X" GTA2GL_CAPTURE="cap_$N,$F" \
    nice -n 19 timeout -k 5 90 gamescope --backend headless -W "$W" -H "$H" -w "$W" -h "$H" -- ./gta2_linux > "$G/run_$N.log" 2>&1
rm -f "$G/audio.raw"
for f in "cap_$N"/*.bmp; do magick "$f" "${f%.bmp}.png" && rm "$f"; done
ls "cap_$N"
