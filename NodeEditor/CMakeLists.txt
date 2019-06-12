project(imgui_node_editor)

find_package(imgui REQUIRED)

add_library(imgui_canvas STATIC
    Source/imgui_canvas.cpp
    Source/imgui_canvas.h
)
target_include_directories(imgui_canvas PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Source)
target_link_libraries(imgui_canvas PUBLIC  imgui)
source_group("" FILES Source/imgui_canvas.cpp Source/imgui_canvas.h)
set_property(TARGET imgui_canvas PROPERTY FOLDER "NodeEditor")

set(_imgui_node_editor_Sources
    Include/imgui_node_editor.h
    Source/imgui_bezier_math.h
    Source/imgui_bezier_math.inl
    Source/imgui_canvas.cpp
    Source/imgui_canvas.h
    Source/imgui_extra_math.h
    Source/imgui_extra_math.inl
    Source/imgui_node_editor_api.cpp
    Source/imgui_node_editor_internal.h
    Source/imgui_node_editor_internal.inl
    Source/imgui_node_editor.cpp
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${_imgui_node_editor_Sources})

add_library(${PROJECT_NAME} STATIC ${_imgui_node_editor_Sources})

set_property(TARGET ${PROJECT_NAME} PROPERTY FOLDER "NodeEditor")

find_package(picojson REQUIRED)
target_link_libraries(${PROJECT_NAME} PUBLIC  imgui)
target_link_libraries(${PROJECT_NAME} PRIVATE picojson imgui_canvas)

target_include_directories(${PROJECT_NAME} PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/Include)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Source)
