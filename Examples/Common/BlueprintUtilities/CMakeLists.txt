project(blueprint-utilities)

set(_Sources
    Include/ax/Builders.h
    Include/ax/Drawing.h
    Include/ax/Widgets.h
    Include/ax/Math2D.h
    Include/ax/Math2D.inl
    Source/Interop.h
    Source/ax/Builders.cpp
    Source/ax/Drawing.cpp
    Source/ax/Widgets.cpp
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${_Sources})

add_library(${PROJECT_NAME} STATIC ${_Sources})

find_package(imgui_node_editor REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE imgui_node_editor)

target_include_directories(${PROJECT_NAME} PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/Include)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Source)

set_property(TARGET ${PROJECT_NAME} PROPERTY FOLDER "Examples/Common")