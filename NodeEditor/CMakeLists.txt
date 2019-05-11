project(imgui_node_editor)

set(_imgui_node_editor_Sources
    Include/ax/Math2D.h
    Include/ax/Math2D.inl
    Include/ax/Builders.h
    Include/ax/Drawing.h
    Include/ax/Widgets.h
    Include/NodeEditor.h
    Source/Interop.h
    Source/NodeEditor.cpp
    Source/Editor.h
    Source/Editor.cpp
    Source/ax/Builders.cpp
    Source/ax/Drawing.cpp
    Source/ax/Widgets.cpp
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${_imgui_node_editor_Sources})

add_library(${PROJECT_NAME} STATIC ${_imgui_node_editor_Sources})

set_property(TARGET ${PROJECT_NAME} PROPERTY FOLDER "NodeEditor")

find_package(imgui REQUIRED)
find_package(picojson REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE imgui picojson)

target_include_directories(${PROJECT_NAME} PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/Include)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Source)
