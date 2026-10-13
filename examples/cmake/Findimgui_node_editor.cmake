get_filename_component(_imgui_node_editor_RootDir "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

if (NOT TARGET imgui_node_editor)
    find_package(imgui REQUIRED)

    set(_imgui_node_editor_Sources
        ${_imgui_node_editor_RootDir}/crude_json.cpp
        ${_imgui_node_editor_RootDir}/crude_json.h
        ${_imgui_node_editor_RootDir}/imgui_bezier_math.h
        ${_imgui_node_editor_RootDir}/imgui_bezier_math.inl
        ${_imgui_node_editor_RootDir}/imgui_canvas.cpp
        ${_imgui_node_editor_RootDir}/imgui_canvas.h
        ${_imgui_node_editor_RootDir}/imgui_extra_math.h
        ${_imgui_node_editor_RootDir}/imgui_extra_math.inl
        ${_imgui_node_editor_RootDir}/imgui_node_editor.cpp
        ${_imgui_node_editor_RootDir}/imgui_node_editor.h
        ${_imgui_node_editor_RootDir}/imgui_node_editor_api.cpp
        ${_imgui_node_editor_RootDir}/imgui_node_editor_internal.h
        ${_imgui_node_editor_RootDir}/imgui_node_editor_internal.inl
        ${_imgui_node_editor_RootDir}/misc/crude_json.natvis
        ${_imgui_node_editor_RootDir}/misc/imgui_node_editor.natvis
    )

    add_library(imgui_node_editor STATIC ${_imgui_node_editor_Sources})

    target_include_directories(imgui_node_editor PUBLIC
        ${_imgui_node_editor_RootDir}
    )

    target_link_libraries(imgui_node_editor PUBLIC imgui)

    source_group(TREE ${_imgui_node_editor_RootDir} FILES ${_imgui_node_editor_Sources})
endif()

file(STRINGS "${_imgui_node_editor_RootDir}/imgui_node_editor.h" _imgui_node_editor_Version REGEX "^# *define IMGUI_NODE_EDITOR_VERSION ")
string(REGEX MATCH "[0-9]+[.][0-9]+[.][0-9]+" imgui_node_editor_VERSION "${_imgui_node_editor_Version}")

if (TARGET imgui_node_editor)
    set(imgui_node_editor_LIBRARY imgui_node_editor)
endif()

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(
    imgui_node_editor
    REQUIRED_VARS imgui_node_editor_LIBRARY
    VERSION_VAR imgui_node_editor_VERSION
)
