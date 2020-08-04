if (TARGET imgui_node_editor)
    return()
endif()

#set(_imgui_node_editor_SourceDir ${IMGUI_NODE_EDITOR_ROOT_DIR})
#set(_imgui_node_editor_BinaryDir ${CMAKE_BINARY_DIR}/NodeEditor)

#add_subdirectory(${_imgui_node_editor_SourceDir} ${_imgui_node_editor_BinaryDir})

find_package(imgui REQUIRED)

set(_imgui_node_editor_Sources
    ${IMGUI_NODE_EDITOR_ROOT_DIR}/crude_json.cpp
    ${IMGUI_NODE_EDITOR_ROOT_DIR}/crude_json.h
    ${IMGUI_NODE_EDITOR_ROOT_DIR}/imgui_bezier_math.h
    ${IMGUI_NODE_EDITOR_ROOT_DIR}/imgui_bezier_math.inl
    ${IMGUI_NODE_EDITOR_ROOT_DIR}/imgui_canvas.cpp
    ${IMGUI_NODE_EDITOR_ROOT_DIR}/imgui_canvas.cpp
    ${IMGUI_NODE_EDITOR_ROOT_DIR}/imgui_canvas.h
    ${IMGUI_NODE_EDITOR_ROOT_DIR}/imgui_canvas.h
    ${IMGUI_NODE_EDITOR_ROOT_DIR}/imgui_extra_math.h
    ${IMGUI_NODE_EDITOR_ROOT_DIR}/imgui_extra_math.inl
    ${IMGUI_NODE_EDITOR_ROOT_DIR}/imgui_node_editor_api.cpp
    ${IMGUI_NODE_EDITOR_ROOT_DIR}/imgui_node_editor_internal.h
    ${IMGUI_NODE_EDITOR_ROOT_DIR}/imgui_node_editor_internal.inl
    ${IMGUI_NODE_EDITOR_ROOT_DIR}/imgui_node_editor.cpp
    ${IMGUI_NODE_EDITOR_ROOT_DIR}/imgui_node_editor.h
    ${IMGUI_NODE_EDITOR_ROOT_DIR}/misc/imgui_node_editor.natvis
)

add_library(imgui_node_editor STATIC
    ${_imgui_node_editor_Sources}
)

target_include_directories(imgui_node_editor PUBLIC
    ${IMGUI_NODE_EDITOR_ROOT_DIR}
)

target_link_libraries(imgui_node_editor PUBLIC imgui)

source_group(TREE ${IMGUI_NODE_EDITOR_ROOT_DIR} FILES ${_imgui_node_editor_Sources})

include(${CMAKE_ROOT}/Modules/FindPackageHandleStandardArgs.cmake)

find_package_handle_standard_args(
    imgui_node_editor
    REQUIRED_VARS
        IMGUI_NODE_EDITOR_ROOT_DIR
)