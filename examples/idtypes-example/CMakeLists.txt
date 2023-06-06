add_example_executable(idtypes-example NOLINK
    SOURCES idtypes-example.cpp
)

set(CMAKE_CXX_STANDARD            14)
set(CMAKE_CXX_STANDARD_REQUIRED   YES)

add_library(imgui_node_editor_customids STATIC
    ${IMGUI_NODE_EDITOR_SOURCES}
)

target_include_directories(imgui_node_editor_customids PUBLIC
    ${IMGUI_NODE_EDITOR_ROOT_DIR}
)

target_link_libraries(imgui_node_editor_customids PUBLIC imgui)

target_compile_definitions(imgui_node_editor_customids PUBLIC IMGUI_NODE_EDITOR_USER_CONFIG="${CMAKE_CURRENT_SOURCE_DIR}/edconfig.h")
target_link_libraries(idtypes-example PRIVATE imgui_node_editor_customids)
