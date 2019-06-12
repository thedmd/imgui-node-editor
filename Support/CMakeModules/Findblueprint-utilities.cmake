
if (TARGET blueprint-utilities)
    return()
endif()

set(_blueprint-utilities_SourceDir ${CMAKE_SOURCE_DIR}/Examples/Common/BlueprintUtilities)
set(_blueprint-utilities_BinaryDir ${CMAKE_BINARY_DIR}/Examples/Common/BlueprintUtilities)

add_subdirectory(${_blueprint-utilities_SourceDir} ${_blueprint-utilities_BinaryDir})

include(${CMAKE_ROOT}/Modules/FindPackageHandleStandardArgs.cmake)

find_package_handle_standard_args(
    blueprint-utilities
    REQUIRED_VARS
        _blueprint-utilities_SourceDir
)

