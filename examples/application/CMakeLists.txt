project(application)

set(_Application_Sources
    include/application.h
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
)

add_library(application STATIC)

target_include_directories(application PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(application PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/source)

find_package(imgui REQUIRED)
find_package(ScopeGuard REQUIRED)
find_package(imgui_backends REQUIRED)

target_link_libraries(application PUBLIC imgui imgui_backends)
target_link_libraries(application PRIVATE ScopeGuard)

configure_file(
    source/config.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/source/config.h
)

target_compile_definitions(application PRIVATE
    #BACKEND_CONFIG=IMGUI_GLFW
    #RENDERER_CONFIG=IMGUI_OGL3
)

target_include_directories(application PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/source)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${_Application_Sources})

target_sources(application PRIVATE ${_Application_Sources})

set_property(TARGET application PROPERTY FOLDER "examples")