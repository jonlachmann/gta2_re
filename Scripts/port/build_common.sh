# The parts of the port builds (build.sh, build_linux.sh, build_mac.sh) that are the same for every target, sourced by
# them from the repository root after they set their flags. Also sourced for ENET_DEFS (net_tests.sh) and
# mingw_includes (build_gl_dlls_clang.sh, clang_check.sh).

# The game: gta2_lib_src without the ImGui overlay and DirectPlay (NetPlay/NetUi: Port/net/engine/netplay.cpp instead),
# plus StdAfx.cpp, Port/ (main.cpp replaces test.cpp), the headless tools and the netcode. Non-Windows builds add
# Source/3rdParty/bink.cpp.
GAME_SRC=($(sed -n '/set *(gta2_lib_src/,/)/p' CMakeLists.txt | grep -o 'Source/[^ ]*\.cpp' | grep -v 'ImGuiDebug.cpp\|NetPlay.cpp\|NetUi.cpp')
          StdAfx.cpp Port/*.cpp Port/headless/state_log.cpp Port/headless/fast_clock.cpp Port/headless/sim_bench.cpp
          $(find Port/net -name "*.cpp" -not -path "*/tests/*" | sort))
# The OpenGL renderer (PR #837), linked in: the game finds its gbh_* functions and Port/sdl_video.cpp's Vid_* ones in
# the program itself.
REN=Renderers/OpenGL
RENSRC=($REN/core/gl_funcs.cpp $REN/core/gl_renderer.cpp $REN/core/gl_enhanced.cpp $REN/core/vid_system.cpp $REN/core/capture.cpp
        $REN/core/options.cpp $REN/core/gbh_api.cpp $REN/core/cmdlog.cpp $REN/platform/sdl_context.cpp)
ENET_SRC=(Port/net/enet/*.c) # ENet, vendored, plain C
# What ENet's configure would find on Linux and macOS
ENET_DEFS="-DHAS_SOCKLEN_T=1 -DHAS_POLL=1 -DHAS_FCNTL=1 -DHAS_MSGHDR_FLAGS=1 -DHAS_GETADDRINFO=1 -DHAS_GETNAMEINFO=1 -DHAS_INET_PTON=1 -DHAS_INET_NTOP=1"

# The MinGW-w64 g++'s C++ and C header directories, in the order libstdc++'s #include_next needs, for clang
mingw_includes() {
  echo | i686-w64-mingw32-g++ -xc++ -E -v - 2>&1 | sed -n '/#include <...> search starts here:/,/End of search list./p' | grep '^ ' |
    grep -Ev '/lib/gcc/[^/]+/[^/]+/include(-fixed)?$' | sed 's/^ */-isystem /'
}

# compile <object dir> <sources...>: the objects into OBJS, each compiled when it is missing or older than its source
# (FORCE=1: all; JOBS, default 8, at a time). Port/net's sources also depend on its headers: they are rebuilt when the
# newest one is newer (anything else needs FORCE=1 after a header change). Flags: RENFLAGS for Renderers/, ENETCC (the
# whole command) for *.c, CXXFLAGS for the rest (bink.cpp with -DBUILD_BINK). Exits when an object is missing.
compile() {
  local dir=$1 f o cc fl nethdr
  shift
  nethdr=$(ls -t $(find Port/net -name "*.hpp" -not -path "*/enet/*") | head -1)
  OBJS=()
  for f in "$@"; do
    cc=clang++
    case $f in
      Renderers/*) fl=$RENFLAGS;;
      Source/3rdParty/bink.cpp) fl="$CXXFLAGS -DBUILD_BINK";;
      *.c) cc=$ENETCC; fl="";;
      *) fl=$CXXFLAGS;;
    esac
    o=$dir/$(echo "$f" | tr '/' '_').o
    OBJS+=("$o")
    if [ ! -f "$o" ] || [ "$f" -nt "$o" ] || [ -n "$FORCE" ] || { [[ $f == Port/net/* ]] && [ "$nethdr" -nt "$o" ]; }; then
      ( $cc $fl -c "$f" -o "$o" 2> "$o.log" || { echo "FAILED: $f"; grep error "$o.log" | head -5; } ) &
    fi
    while [ "$(jobs -r | wc -l)" -ge "${JOBS:-8}" ]; do sleep 0.1; done
  done
  wait
  for o in "${OBJS[@]}"; do [ -f "$o" ] || { echo "compile failed"; exit 1; }; done
}
