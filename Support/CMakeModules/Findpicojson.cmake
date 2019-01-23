
if (TARGET picojson)
    return()
endif()

set(_picojson_SourceDir ${CMAKE_SOURCE_DIR}/ThirdParty/picojson)
set(_picojson_BinaryDir ${CMAKE_BINARY_DIR}/ThirdParty/picojson)

add_subdirectory(${_picojson_SourceDir} ${_picojson_BinaryDir})

include(${CMAKE_ROOT}/Modules/FindPackageHandleStandardArgs.cmake)

find_package_handle_standard_args(
    picojson
    REQUIRED_VARS
        _picojson_SourceDir
)

