cmake_minimum_required (VERSION 3.8)
project(imgui_node_editor)

add_library(${PROJECT_NAME} STATIC 
crude_json.cpp
imgui_canvas.cpp
imgui_node_editor.cpp
imgui_node_editor_api.cpp)

target_link_libraries(${PROJECT_NAME} imgui)

target_include_directories(${PROJECT_NAME}
    INTERFACE
    ./
    )