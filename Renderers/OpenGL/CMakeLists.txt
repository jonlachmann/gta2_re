# The renderer DLLs for the 32-bit Windows game (glvideo.dll, glrender.dll, and the reference proxy capvideo.dll), with a
# MinGW-w64 toolchain, and the native tests of the portable core on any other host (README "Build"):
#   cmake -S Renderers/OpenGL -B build_gl -DCMAKE_TOOLCHAIN_FILE=$PWD/Port/cmake/mingw-gcc.cmake && cmake --build build_gl
#   cmake -S Renderers/OpenGL -B build_gl_test && cmake --build build_gl_test && ctest --test-dir build_gl_test
# The port's Windows build (Port/CMakeLists.txt) builds the DLLs too, with its compiler.
cmake_minimum_required(VERSION 3.21)
project(gta2gl CXX)
include(${CMAKE_CURRENT_LIST_DIR}/sources.cmake)
set(GL ${CMAKE_CURRENT_LIST_DIR})

if(WIN32)
  # --kill-at: undecorated stdcall export names, as the game resolves them with GetProcAddress
  add_library(glvideo SHARED ${GL}/dll/glvideo.cpp ${GL}/dll/dinput_hook.cpp ${GL}/dev/input_script.cpp
              ${GL}/platform/wgl_context.cpp ${GL_VIDEO_SRC})
  target_link_libraries(glvideo PRIVATE opengl32 gdi32 dinput dxguid)
  add_library(glrender SHARED ${GL}/dll/glrender.cpp ${GL_RENDER_SRC})
  # Development tool: a proxy of DMA's Dmavideo.dll that captures reference frames and logs the D3D calls
  add_library(capvideo SHARED ${GL}/tools/capvideo.cpp ${GL}/tools/d3dlog.cpp ${GL}/dll/dinput_hook.cpp
              ${GL}/dev/input_script.cpp ${GL}/dev/capture.cpp ${GL}/dev/cmdlog.cpp ${GL}/dev/frame_list.cpp)
  target_link_libraries(capvideo PRIVATE ddraw dxguid uuid)
  foreach(t glvideo glrender capvideo)
    set_target_properties(${t} PROPERTIES PREFIX "" CXX_STANDARD 11)
    target_compile_definitions(${t} PRIVATE GTA2GL_BUILD_DLL)
    target_link_options(${t} PRIVATE -static -static-libgcc -static-libstdc++ -Wl,--kill-at)
  endforeach()
  return()
endif()

# Native tests: the gbh layer (texture updates), the ini options, and the GL core and the SDL platform compiled
enable_testing()
set(CMAKE_CXX_STANDARD 11)
add_executable(gbh_test ${GL}/tests/gbh_test.cpp ${GL_RENDER_SRC})
add_executable(options_test ${GL}/tests/options_test.cpp ${GL}/core/options.cpp)
foreach(t gbh_test options_test)
  add_test(NAME ${t} COMMAND ${t} WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
endforeach()
add_library(gl_video_core OBJECT ${GL_VIDEO_SRC}) # a compile check without a GL context
if(APPLE)
  target_compile_definitions(gl_video_core PRIVATE GL_SILENCE_DEPRECATION)
endif()
# sdl_demo: a native SDL3 window that draws through the GL core, no game needed (F12 toggles the enhancements)
find_package(SDL3 CONFIG)
find_package(OpenGL)
if(SDL3_FOUND AND OpenGL_FOUND)
  add_executable(sdl_demo ${GL}/tests/sdl_demo.cpp ${GL}/platform/sdl_context.cpp ${GL_VIDEO_SRC})
  target_link_libraries(sdl_demo PRIVATE SDL3::SDL3 OpenGL::GL)
  if(APPLE)
    target_compile_definitions(sdl_demo PRIVATE GL_SILENCE_DEPRECATION)
  endif()
else()
  message(STATUS "SDL3 or OpenGL not found: no sdl_demo")
endif()
