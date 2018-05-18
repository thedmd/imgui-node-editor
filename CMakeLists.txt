cmake_minimum_required(VERSION 3.8)
project(ImGuiNodeEditor)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(CMAKE_CXX_STANDARD            17)
set(CMAKE_CXX_STANDARD_REQUIRED   YES)

if (DEFINED MSVC_VERSION AND (NOT (MSVC_VERSION LESS 1400)))
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /std:c++17")# /permissive-")
endif()

set(_imgui_Sources
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/imgui/imgui.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/imgui/imgui_draw.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/imgui/imgui_demo.cpp
)

add_library(ImGui STATIC ${_imgui_Sources})
target_include_directories(ImGui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/imgui)

add_subdirectory(ThirdParty/picojson)
add_subdirectory(ThirdParty/stb_image)
add_subdirectory(NodeEditor)
add_subdirectory(Examples)