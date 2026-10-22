project(application)

set(_Application_Sources
    include/application.h
    include/splitter.h
    source/application.cpp
    source/entry_point.cpp
    source/config.h.in
    source/setup.h
    source/platform.h
    source/platform_win32.cpp
    source/platform_glfw.cpp
    source/renderer.h
    source/renderer_dx11.cpp
    source/renderer_ogl3.cpp
    source/splitter.cpp
)

add_library(application STATIC)
set_example_warnings(application)

target_include_directories(application PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

if (WIN32)
    set(IMGUI_NODE_EDITOR_EXAMPLES_BACKEND  "win32" CACHE STRING "Platform backend of the examples: win32 or glfw")
    set(IMGUI_NODE_EDITOR_EXAMPLES_RENDERER "dx11"  CACHE STRING "Renderer of the examples: dx11, or opengl3 with the glfw backend")
    set_property(CACHE IMGUI_NODE_EDITOR_EXAMPLES_BACKEND  PROPERTY STRINGS win32 glfw)
    set_property(CACHE IMGUI_NODE_EDITOR_EXAMPLES_RENDERER PROPERTY STRINGS dx11 opengl3)
    target_compile_definitions(application PRIVATE UNICODE _UNICODE)
else()
    set(IMGUI_NODE_EDITOR_EXAMPLES_BACKEND  glfw)
    set(IMGUI_NODE_EDITOR_EXAMPLES_RENDERER opengl3)
endif()

if (IMGUI_NODE_EDITOR_EXAMPLES_BACKEND STREQUAL "win32")
    target_compile_definitions(application PRIVATE BACKEND_CONFIG=IMGUI_WIN32)
elseif (IMGUI_NODE_EDITOR_EXAMPLES_BACKEND STREQUAL "glfw")
    find_package(glfw REQUIRED)
    set(HAVE_GLFW3 YES)
    target_compile_definitions(application PRIVATE BACKEND_CONFIG=IMGUI_GLFW)
else()
    message(FATAL_ERROR "IMGUI_NODE_EDITOR_EXAMPLES_BACKEND must be win32 or glfw, not ${IMGUI_NODE_EDITOR_EXAMPLES_BACKEND}")
endif()

if (IMGUI_NODE_EDITOR_EXAMPLES_RENDERER STREQUAL "dx11")
    target_compile_definitions(application PRIVATE RENDERER_CONFIG=IMGUI_DX11)
elseif (IMGUI_NODE_EDITOR_EXAMPLES_RENDERER STREQUAL "opengl3")
    # The renderer uses the context GLFW creates with the window.
    if (NOT IMGUI_NODE_EDITOR_EXAMPLES_BACKEND STREQUAL "glfw")
        message(FATAL_ERROR "The opengl3 renderer of the examples needs the glfw backend")
    endif()
    target_compile_definitions(application PRIVATE RENDERER_CONFIG=IMGUI_OGL3)
else()
    message(FATAL_ERROR "IMGUI_NODE_EDITOR_EXAMPLES_RENDERER must be dx11 or opengl3, not ${IMGUI_NODE_EDITOR_EXAMPLES_RENDERER}")
endif()

find_package(imgui REQUIRED COMPONENTS ${IMGUI_NODE_EDITOR_EXAMPLES_BACKEND} ${IMGUI_NODE_EDITOR_EXAMPLES_RENDERER})
find_package(stb REQUIRED COMPONENTS image)
target_link_libraries(application PUBLIC imgui)
target_link_libraries(application PRIVATE imgui::${IMGUI_NODE_EDITOR_EXAMPLES_BACKEND} imgui::${IMGUI_NODE_EDITOR_EXAMPLES_RENDERER} stb::image)
if (WIN32)
    target_link_libraries(application PRIVATE dwmapi)
endif()

if (OpenGL_FOUND)
    set(HAVE_OPENGL YES)
endif()

configure_file(
    source/config.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/source/config.h
)

target_include_directories(application PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/source)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${_Application_Sources})

target_sources(application PRIVATE ${_Application_Sources})

set_property(TARGET application PROPERTY FOLDER "lib")