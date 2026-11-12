#!/bin/sh
# flatpak.sh <work dir>: reG2-x86_64.flatpak in <work dir>/out, run from the repository's root in
# ghcr.io/flathub-infra/flatpak-github-actions:freedesktop-26.08 (bwrap needs the docker options of
# port/README.md, Packages). <work dir>/state holds flatpak-builder's downloads and build cache.
set -eu
W=$1
HERE=$(cd "$(dirname "$0")" && pwd)
ID=io.github.parigi_n.reG2
M=$HERE/flatpak/$ID.yml

# The manifest's archives are the ones pinned in versions.sh.
. "$HERE/versions.sh"
awk '$1 == "url:" {url = $2} $1 == "sha256:" {print url, $2}' "$M" | while read -r url sum; do
    if ! echo "$SOURCES" | grep -q " $url $sum\$"; then
        echo "flatpak.sh: $url $sum is not pinned in versions.sh" >&2
        exit 1
    fi
done

flatpak-builder --disable-rofiles-fuse --force-clean --install-deps-from=flathub --state-dir="$W/state" \
    --repo="$W/repo" "$W/app" "$M"
mkdir -p "$W/out"
NAME=$W/out/reG2-x86_64.flatpak
flatpak build-bundle --runtime-repo=https://dl.flathub.org/repo/flathub.flatpakrepo "$W/repo" "$NAME" "$ID"
rm -rf "$W/app"
echo "$NAME"
