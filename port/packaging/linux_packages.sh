#!/bin/sh
# linux_packages.sh <build dir> <deps prefix> <out dir>: the Linux packages, from the same binaries: the game
# with the libraries distributions may lack (SDL3, OpenAL Soft), which it finds next to itself. glibc, OpenGL,
# X11, Wayland and sound come from the system's 32-bit libraries.
# - gta2_re-<version>-linux-i686.tar.gz, the portable build: the launcher gta2, bin/ and linux/README.md.
# - gta2-re_<version>_i386.deb, in /usr/lib/gta2_re, started with gta2_re; its dependencies are the 32-bit
#   libraries, which apt installs where i386 is enabled (dpkg --add-architecture i386).
set -eu
B=$1; P=$2; OUT=$3
HERE=$(cd "$(dirname "$0")" && pwd)
VERSION=${GTA2_VERSION:-dev}
T=$(mktemp -d)
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)

S=$T/bin
mkdir -p "$S"
llvm-strip --strip-all -R .comment -o "$S/gta2" "$B/gta2"
llvm-strip --strip-unneeded -R .comment -o "$S/libSDL3.so.0" "$P/lib/libSDL3.so.0"
llvm-strip --strip-unneeded -R .comment -o "$S/libopenal.so.1" "$B/libopenal.so.1"
chmod 755 "$S/gta2"
chmod 644 "$S"/*.so.*
# The newest glibc symbol version they need: the oldest glibc they start with.
llvm-objdump -T "$S"/* | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -n 1

NAME=gta2_re-$VERSION-linux-i686
D=$T/$NAME
mkdir -p "$D/licenses"
cp -a "$S" "$D/bin"
cp "$HERE/linux/gta2" "$D/gta2"
chmod 755 "$D/gta2"
cp "$HERE/linux/README.md" "$D/README.md"
cp "$HERE/../README.md" "$D/README-port.md"
cp "$P"/licenses/* "$D/licenses/"
tar -C "$T" --owner=0 --group=0 --numeric-owner -czf "$OUT/$NAME.tar.gz" "$NAME"
echo "$OUT/$NAME.tar.gz"

# A tag v1.2 gives 1.2-1; a commit, 0~<date>.<commit>-1.
case $VERSION in
v[0-9]*) DV=${VERSION#v}-1 ;;
*) DV=0~$(date -u +%Y%m%d).$VERSION-1 ;;
esac
R=$T/deb
L=$R/usr/lib/gta2_re
DOC=$R/usr/share/doc/gta2-re
ICONS=$R/usr/share/icons/hicolor
mkdir -p "$R/DEBIAN" "$R/usr/lib" "$R/usr/games" "$R/usr/share/applications" "$R/usr/share/metainfo" "$R/usr/share/man/man6" \
         "$R/usr/share/lintian/overrides" "$ICONS/32x32/apps" "$ICONS/256x256/apps" "$DOC"
cp -a "$S" "$L"
ln -s ../lib/gta2_re/gta2 "$R/usr/games/gta2_re"
cp "$HERE/linux/io.github.gta2re.gta2.desktop" "$R/usr/share/applications/"
cp "$HERE/linux/io.github.gta2re.gta2.metainfo.xml" "$R/usr/share/metainfo/"
cp "$HERE/../resources/gta2.png" "$ICONS/32x32/apps/io.github.gta2re.gta2.png"
cp "$HERE/../resources/gta2_256.png" "$ICONS/256x256/apps/io.github.gta2re.gta2.png"
gzip -9n < "$HERE/linux/gta2_re.6" > "$R/usr/share/man/man6/gta2_re.6.gz"
cp "$HERE/../README.md" "$DOC/README.md"
cp "$HERE/linux/lintian-overrides" "$R/usr/share/lintian/overrides/gta2-re"
cat > "$T/changelog" << EOF
gta2-re ($DV) unstable; urgency=medium

  * Built from gta2_re $VERSION by port/packaging/linux_packages.sh.

 -- gta2_re contributors <gta2re@users.noreply.github.com>  $(date -u -R)
EOF
gzip -9n < "$T/changelog" > "$DOC/changelog.Debian.gz"

# Debian's copyright format, with the licences of the libraries in the package or linked into the game.
lic() { sed -e 's/[[:space:]]*$//' -e 's/^$/./' -e 's/^/ /' "$1"; }
{
    cat << 'EOF'
Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/
Upstream-Name: gta2_re
Source: https://github.com/CriminalRETeam/gta2_re
Disclaimer: GTA2 and its data are not included: the game needs the player's own copy. This project is not
 affiliated with or endorsed by Rockstar Games or Take-Two Interactive.

Files: *
Copyright: gta2_re contributors
License: none
 No licence is given for gta2_re, a decompilation of Grand Theft Auto 2 (1999).

Files: usr/lib/gta2_re/libSDL3.so.0
Copyright: Sam Lantinga and the SDL contributors
License: Zlib
EOF
    lic "$P/licenses/SDL3.txt"
    cat << 'EOF'

Files: usr/lib/gta2_re/libopenal.so.1
Copyright: Chris Robinson and the OpenAL Soft contributors
License: LGPL-2+
 On Debian systems, the complete text of the GNU Library General Public License version 2 can be found in
 /usr/share/common-licenses/LGPL-2.

Files: usr/lib/gta2_re/gta2
Copyright: libarchive, unshield, zlib and xz (liblzma) contributors, linked into the game
License: BSD-2-Clause and MIT and Zlib and 0BSD
 libarchive:
 .
EOF
    lic "$P/licenses/libarchive.txt"
    printf ' .\n unshield:\n .\n'
    lic "$P/licenses/unshield.txt"
    printf ' .\n zlib:\n .\n'
    lic "$P/licenses/zlib.txt"
    printf ' .\n xz:\n .\n'
    lic "$P/licenses/xz.txt"
    cat << 'EOF'

Files: usr/lib/gta2_re/gta2 usr/lib/gta2_re/libopenal.so.1
Copyright: Free Software Foundation, Inc.
License: GPL-3+ with GCC-exception-3.1
 The GNU C++ library, linked into these files, is under the GNU General Public License version 3 or later
 with the GCC Runtime Library Exception 3.1, which lets them carry any licence.
 On Debian systems, the complete text of the GNU General Public License version 3 can be found in
 /usr/share/common-licenses/GPL-3.
EOF
} > "$DOC/copyright"

cat > "$R/DEBIAN/control" << EOF
Package: gta2-re
Version: $DV
Architecture: i386
Maintainer: gta2_re contributors <gta2re@users.noreply.github.com>
Installed-Size: $(du -sk --apparent-size "$R/usr" | cut -f1)
Depends: libc6 (>= 2.29), libgcc-s1, libgl1, libegl1, libgles2, libx11-6, libxext6, libxi6, libxcursor1, libxrandr2, libxkbcommon0, libwayland-client0, libwayland-cursor0, libwayland-egl1, libpulse0
Recommends: libdecor-0-0, libxss1, libxfixes3, libdbus-1-3, libudev1, libasound2t64 | libasound2, libpipewire-0.3-0t64 | libpipewire-0.3-0
Section: games
Priority: optional
Homepage: https://github.com/CriminalRETeam/gta2_re
Description: native port of the GTA2 decompilation
 gta2_re is a decompilation of Grand Theft Auto 2 (1999), rebuilt from the
 original game's code, with SDL3, OpenGL ES and OpenAL Soft.
 .
 The game's files are not included: it needs the player's own copy of GTA2
 for Windows (the original CD, or the version Rockstar Games gave away in
 2004). On the first start, choose a GTA2 folder, its installer or a disc
 image: the game's data is copied into the per-user folder.
EOF
(cd "$R" && find usr -type f -exec md5sum {} + | sort -k 2) > "$R/DEBIAN/md5sums"
DEB=$OUT/gta2-re_${DV}_i386.deb
dpkg-deb --root-owner-group -Zxz --build "$R" "$DEB" > /dev/null
echo "$DEB"
rm -rf "$T"
