if (TARGET stb_image)
    return()
endif()

set(_stb_image_SourceDir ${IMGUI_NODE_EDITOR_ROOT_DIR}/external/stb_image)
set(_stb_image_BinaryDir ${CMAKE_BINARY_DIR}/external/stb_image)

add_subdirectory(${_stb_image_SourceDir} ${_stb_image_BinaryDir})

include(${CMAKE_ROOT}/Modules/FindPackageHandleStandardArgs.cmake)

find_package_handle_standard_args(
    stb_image
    REQUIRED_VARS
        _stb_image_SourceDir
)

