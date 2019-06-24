macro(add_example name)
    project(${name})

    set(_Example_Sources
        ${ARGN}
    )

    source_group("" FILES ${_Example_Sources})
    source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${_Example_Sources})

    set(_Example_Type)
    if (WIN32)
        set(_Example_Type WIN32)

        set(ApplicationIcon ${CMAKE_SOURCE_DIR}/Examples/Common/Support/Icon.ico)
        file(TO_NATIVE_PATH "${ApplicationIcon}" ApplicationIcon)
        string(REPLACE "\\" "\\\\" ApplicationIcon "${ApplicationIcon}")
        configure_file(
            ${CMAKE_SOURCE_DIR}/Examples/Common/Support/Resource.rc.in
            ${CMAKE_CURRENT_BINARY_DIR}/Resource.rc
        )
        set(_Example_Resources
            ${CMAKE_CURRENT_BINARY_DIR}/Resource.rc
        )
    elseif (APPLE)
        set(_Example_Type MACOSX_BUNDLE)

        file(GLOB _Example_Resources "${CMAKE_SOURCE_DIR}/Data/*")
        set_source_files_properties(${_Example_Resources} PROPERTIES
            MACOSX_PACKAGE_LOCATION "Resources/Data"
        )
        set(_Example_Icon "${CMAKE_SOURCE_DIR}/Examples/Common/Support/Icon.icns")
        list(APPEND _Example_Resources ${_Example_Icon})
        set_source_files_properties(${_Example_Icon} PROPERTIES
            MACOSX_PACKAGE_LOCATION "Resources"
        )
    endif()

    add_executable(${name} ${_Example_Type} ${_Example_Sources} ${_Example_Resources})

    find_package(imgui REQUIRED)
    find_package(imgui_node_editor REQUIRED)
    target_link_libraries(${name} PRIVATE imgui imgui_node_editor Application)

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
        MACOSX_BUNDLE_INFO_PLIST                "${CMAKE_SOURCE_DIR}/Examples/Common/Support/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME               "${PACKAGE_NAME}"
        MACOSX_BUNDLE_GUI_IDENTIFIER            "com.sandbox.collisions"
        MACOSX_BUNDLE_LONG_VERSION_STRING       "${PACKAGE_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING      "${PACKAGE_VERSION_MAJOR}.${PACKAGE_VERSION_MINOR}"
        MACOSX_BUNDLE_ICON_FILE                 Icon.icns
    )

    add_custom_command(
        TARGET ${name}
        PRE_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ARGS ${_ExampleBinDir}
        COMMAND ${CMAKE_COMMAND} -E copy_directory ARGS ${CMAKE_SOURCE_DIR}/Data ${_ExampleBinDir}/Data
    )

endmacro()

add_subdirectory(Common/Application)

add_subdirectory(Canvas)
add_subdirectory(Simple)
add_subdirectory(Blueprints)