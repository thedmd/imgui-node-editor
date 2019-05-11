
if (TARGET imgui)
    return()
endif()

set(_imgui_SourceDir ${CMAKE_SOURCE_DIR}/ThirdParty/imgui)
set(_imgui_BinaryDir ${CMAKE_BINARY_DIR}/ThirdParty/imgui)

add_subdirectory(${_imgui_SourceDir} ${_imgui_BinaryDir})

include(${CMAKE_ROOT}/Modules/FindPackageHandleStandardArgs.cmake)

find_package_handle_standard_args(
    imgui
    REQUIRED_VARS
        _imgui_SourceDir
)

