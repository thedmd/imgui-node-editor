if (TARGET gl3w)
    return()
endif()

set(_gl3w_SourceDir ${IMGUI_NODE_EDITOR_ROOT_DIR}/external/gl3w)
set(_gl3w_BinaryDir ${CMAKE_BINARY_DIR}/external/gl3w)

add_subdirectory(${_gl3w_SourceDir} ${_gl3w_BinaryDir})

include(${CMAKE_ROOT}/Modules/FindPackageHandleStandardArgs.cmake)

find_package_handle_standard_args(
    gl3w
    REQUIRED_VARS
        _gl3w_SourceDir
)

