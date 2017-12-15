function(add_example name)
    project(${name})

    set(_Example_Sources
        ${ARGN}
    )

    source_group("" FILES ${_Example_Sources})
    source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${_Example_Sources})

    add_executable(${name} WIN32 ${_Example_Sources})

    target_link_libraries(${name} PRIVATE ImGui NodeEditor Shared Application)

    set(_ExampleBinDir ${CMAKE_BINARY_DIR}/Bin)

    set_target_properties(${name} PROPERTIES
        FOLDER "Examples"
        RUNTIME_OUTPUT_DIRECTORY                "${_ExampleBinDir}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG          "${_ExampleBinDir}"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${_ExampleBinDir}"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL     "${_ExampleBinDir}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE        "${_ExampleBinDir}"
        DEBUG_POSTFIX                           _d
        RELWITHDEBINGO_POSTFIX                  _rd
        MINSIZEREL_POSTFIX                      _r
        VS_DEBUGGER_WORKING_DIRECTORY           ${_ExampleBinDir}
    )

    add_custom_command(
        TARGET ${name}
        PRE_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ARGS ${_ExampleBinDir}
        COMMAND ${CMAKE_COMMAND} -E copy_directory ARGS ${CMAKE_SOURCE_DIR}/Data ${_ExampleBinDir}/Data
    )

endfunction()

add_subdirectory(Common/Shared)
add_subdirectory(Common/Application)

add_subdirectory(00-Simple)
add_subdirectory(01-Blueprints)