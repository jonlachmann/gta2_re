#!/bin/sh
# The game is i686: it runs through the loader, glibc, libraries and Mesa of /app/lib32, so it needs no
# Compat.i386 extension. A GL32 extension, when installed, comes first: NVIDIA's driver, or a newer Mesa.
L=/app/lib32
GL=/app/lib/i386-linux-gnu/GL
if ! "$L/ld-linux.so.2" --version > /dev/null 2>&1; then
    echo "gta2: this system cannot run 32-bit programs (a kernel without IA32 emulation)" >&2
    exit 1
fi
path=
nvidia=
for d in "$GL"/nvidia-*/lib "$GL"/*/lib; do
    case ":$path" in *":$d:"*) continue ;; esac
    [ -d "$d" ] && path=$path$d:
    [ -e "$d/libGLX_nvidia.so.0" ] && nvidia=1
done
EGL=$GL/glvnd/egl_vendor.d
[ -e "$EGL/50_mesa.json" ] || EGL=$EGL:$L/mesa/egl_vendor.d
export __EGL_VENDOR_LIBRARY_DIRS="${__EGL_VENDOR_LIBRARY_DIRS:-$EGL}"
export __EGL_EXTERNAL_PLATFORM_CONFIG_DIRS="$GL/egl/egl_external_platform.d"
# Without NVIDIA's GLX, an NVIDIA X server's GLX gets Mesa's (llvmpipe).
[ -n "$nvidia" ] || export __GLX_VENDOR_LIBRARY_NAME="${__GLX_VENDOR_LIBRARY_NAME:-mesa}"
export GBM_BACKENDS_PATH="$GL/lib/gbm:$L/mesa/gbm"
export LIBDECOR_PLUGIN_DIR="$L/libdecor/plugins-1"
# --library-path rather than LD_LIBRARY_PATH: the programs the game starts keep the runtime's.
exec "$L/ld-linux.so.2" --inhibit-cache --library-path "$path$L/mesa:$L" "$L/gta2_re/gta2" "$@"
