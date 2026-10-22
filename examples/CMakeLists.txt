cmake_minimum_required(VERSION 3.24)

project(imgui-node-editor-examples)

# Define IMGUI_NODE_EDITOR_ROOT_DIR pointing to project root directory
get_filename_component(IMGUI_NODE_EDITOR_ROOT_DIR ${CMAKE_CURRENT_LIST_DIR}/.. ABSOLUTE CACHE)

# Enable solution folders in Visual Studio and Folders in Xcode
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Point CMake where to look for module files.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Node editor use C++14
set(CMAKE_CXX_STANDARD            14)
set(CMAKE_CXX_STANDARD_REQUIRED   YES)
set(CMAKE_CXX_EXTENSIONS          OFF)

# Copies each file to bin/data, keeping its path relative to 'root'. Every file is
# copied by one command owned by 'target', so parallel builds don't race on it.
function(add_example_data_target target root)
    set(_outputs)
    foreach(_source ${ARGN})
        file(RELATIVE_PATH _relative "${root}" "${_source}")
        set(_output "${CMAKE_BINARY_DIR}/bin/data/${_relative}")
        get_filename_component(_output_dir "${_output}" DIRECTORY)
        add_custom_command(
            OUTPUT "${_output}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_output_dir}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_source}" "${_output}"
            DEPENDS "${_source}"
            VERBATIM
        )
        list(APPEND _outputs "${_output}")
    endforeach()
    add_custom_target(${target} DEPENDS ${_outputs})
    set_target_properties(${target} PROPERTIES FOLDER "examples")
endfunction()

# MSVC keeps its default /W3.
function(set_example_warnings target)
    if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()
endfunction()

# Macro that will configure an example application
macro(add_example_executable name)
    project(${name})

    set(_Example_Sources
        ${ARGN}
    )

    #source_group("" FILES ${_Example_Sources})
    source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${_Example_Sources})

    file(GLOB _Example_CommonResources CONFIGURE_DEPENDS "${IMGUI_NODE_EDITOR_ROOT_DIR}/examples/data/*")
    file(GLOB _Example_Resources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/data/*")
    list(FILTER _Example_CommonResources EXCLUDE REGEX "\\.DS_Store$")
    list(FILTER _Example_Resources EXCLUDE REGEX "\\.DS_Store$")
    set(_Example_Data ${_Example_Resources})
    #message(FATAL_ERROR "_Example_Resources = ${_Example_Resources}")

    set(_Example_Type)
    if (WIN32)
        set(_Example_Type WIN32)

        set(ApplicationIcon ${IMGUI_NODE_EDITOR_ROOT_DIR}/examples/application/support/Icon.ico)
        file(TO_NATIVE_PATH "${ApplicationIcon}" ApplicationIcon)
        string(REPLACE "\\" "\\\\" ApplicationIcon "${ApplicationIcon}")
        configure_file(
            ${IMGUI_NODE_EDITOR_ROOT_DIR}/examples/application/support/Resource.rc.in
            ${CMAKE_CURRENT_BINARY_DIR}/Resource.rc
        )
        set(_Example_Manifest ${IMGUI_NODE_EDITOR_ROOT_DIR}/examples/application/support/Application.manifest)
        source_group(TREE "${IMGUI_NODE_EDITOR_ROOT_DIR}/examples" FILES ${_Example_CommonResources})
        source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${_Example_Resources})
        list(APPEND _Example_Resources
            ${CMAKE_CURRENT_BINARY_DIR}/Resource.rc
            ${_Example_Manifest}
            ${_Example_CommonResources}
        )
        source_group("resources" FILES ${CMAKE_CURRENT_BINARY_DIR}/Resource.rc ${_Example_Manifest})
    elseif (APPLE)
        set(_Example_Type MACOSX_BUNDLE)

        set_source_files_properties(${_Example_Resources} ${_Example_CommonResources} PROPERTIES
            MACOSX_PACKAGE_LOCATION "Resources/data"
        )
        set(_Example_Icon "${IMGUI_NODE_EDITOR_ROOT_DIR}/examples/application/support/Icon.icns")
        list(APPEND _Example_Resources ${_Example_Icon})
        set_source_files_properties(${_Example_Icon} PROPERTIES
            MACOSX_PACKAGE_LOCATION "Resources"
        )
    endif()

    add_executable(${name} ${_Example_Type} ${_Example_Sources} ${_Example_Resources} ${_Example_CommonResources})
    set_example_warnings(${name})

    target_link_libraries(${name} PRIVATE imgui imgui_node_editor application)

    set(_ExampleBinDir ${CMAKE_BINARY_DIR}/bin)

    set_target_properties(${name} PROPERTIES
        FOLDER "examples"
        RUNTIME_OUTPUT_DIRECTORY                "${_ExampleBinDir}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG          "${_ExampleBinDir}"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${_ExampleBinDir}"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL     "${_ExampleBinDir}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE        "${_ExampleBinDir}"
        DEBUG_POSTFIX                           _d
        RELWITHDEBINFO_POSTFIX                  _rd
        MINSIZEREL_POSTFIX                      _r
        VS_DEBUGGER_WORKING_DIRECTORY           ${_ExampleBinDir}
        MACOSX_BUNDLE_INFO_PLIST                "${IMGUI_NODE_EDITOR_ROOT_DIR}/examples/application/support/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME               "${name}"
        MACOSX_BUNDLE_GUI_IDENTIFIER            "io.github.thedmd.imgui-node-editor.${name}"
        MACOSX_BUNDLE_BUNDLE_VERSION            "${imgui_node_editor_VERSION}"
        MACOSX_BUNDLE_LONG_VERSION_STRING       "${imgui_node_editor_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING      "${imgui_node_editor_VERSION}"
        MACOSX_BUNDLE_ICON_FILE                 Icon.icns
    )

    add_dependencies(${name} examples-data)
    if (_Example_Data)
        add_example_data_target(${name}-data "${CMAKE_CURRENT_SOURCE_DIR}/data" ${_Example_Data})
        add_dependencies(${name} ${name}-data)
    endif()

endmacro()

file(GLOB _Examples_CommonData CONFIGURE_DEPENDS "${IMGUI_NODE_EDITOR_ROOT_DIR}/examples/data/*")
list(FILTER _Examples_CommonData EXCLUDE REGEX "\\.DS_Store$")
add_example_data_target(examples-data "${IMGUI_NODE_EDITOR_ROOT_DIR}/examples/data" ${_Examples_CommonData})

find_package(imgui REQUIRED)
find_package(imgui_node_editor REQUIRED)

add_subdirectory(application)

add_subdirectory(canvas-example)
add_subdirectory(simple-example)
add_subdirectory(widgets-example)
add_subdirectory(basic-interaction-example)
add_subdirectory(blueprints-example)
add_subdirectory(blueprints2-example)

set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT blueprints-example)