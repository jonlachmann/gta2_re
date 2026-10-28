# The renderer's source lists (README "Layout"), for its own CMakeLists.txt (the DLLs and the tests) and the port's
# (Port/CMakeLists.txt links the renderer into the game).
set(GL_ROOT ${CMAKE_CURRENT_LIST_DIR})
# The video side (Vid_*): the GL renderer, the frame flow and its development aids
set(GL_VIDEO_SRC core/gl_funcs.cpp core/gl_renderer.cpp core/gl_enhanced.cpp core/options.cpp core/vid_system.cpp
    dev/frame_tools.cpp dev/capture.cpp dev/frame_list.cpp)
# The render side (gbh_*), with its side of the command log
set(GL_RENDER_SRC core/gbh_api.cpp core/gbh_textures.cpp core/gbh_lighting.cpp dev/gbh_log.cpp dev/cmdlog.cpp
    dev/frame_list.cpp)
list(TRANSFORM GL_VIDEO_SRC PREPEND ${GL_ROOT}/)
list(TRANSFORM GL_RENDER_SRC PREPEND ${GL_ROOT}/)
