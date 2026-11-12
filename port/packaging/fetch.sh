#!/bin/sh
# fetch.sh <dir> [name...]: downloads the pinned sources of versions.sh (all, or the named ones) and checks them.
set -eu
DL=$1; shift
. "$(dirname "$0")/versions.sh"
# macOS has a BSD sha256sum without --status; shasum is on every Mac.
[ "$(uname)" = Darwin ] && sha256sum() { shasum -a 256 "$@"; }
command -v sha256sum > /dev/null || sha256sum() { shasum -a 256 "$@"; }
mkdir -p "$DL"
echo "$SOURCES" | while read -r name url sum; do
    [ -n "$name" ] || continue
    if [ $# -gt 0 ]; then
        case " $* " in *" $name "*) ;; *) continue ;; esac
    fi
    if ! echo "$sum  $DL/$name" | sha256sum -c --status 2>/dev/null; then
        curl -sSLf --retry 3 -o "$DL/$name" "$url"
        echo "$sum  $DL/$name" | sha256sum -c
    fi
done
