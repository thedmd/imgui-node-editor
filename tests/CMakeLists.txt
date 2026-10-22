cmake_minimum_required(VERSION 3.24)

project(imgui-node-editor-tests CXX)

get_filename_component(IMGUI_NODE_EDITOR_ROOT_DIR ${CMAKE_CURRENT_LIST_DIR}/.. ABSOLUTE)

# 14 is the minimum; CI also builds with 17, where crude_json converts numbers with std::from_chars().
if (NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 14)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED YES)

set(IMGUI_DIR "" CACHE PATH "Dear ImGui sources to test against. When empty, the pinned revision the examples use is fetched.")
# Like the examples, blueprints2-example builds only against the pinned revision.
if (IMGUI_DIR)
    set(_IsPinnedImGui OFF)
else()
    set(_IsPinnedImGui ON)
endif()
option(IMGUI_NODE_EDITOR_TESTS_BLUEPRINTS2 "Also test blueprints2-example: its runtime, and its documents" ${_IsPinnedImGui})
set(IMGUI_NODE_EDITOR_TESTS_DEFINITIONS "" CACHE STRING "Definitions for Dear ImGui and the editor, for example IMGUI_DISABLE_OBSOLETE_FUNCTIONS")
option(IMGUI_NODE_EDITOR_TESTS_SANITIZE "Build with AddressSanitizer and run the tests that need it" OFF)
option(IMGUI_NODE_EDITOR_WARNINGS_AS_ERRORS "Treat warnings in the node editor and blueprints2-example runtime sources as errors" OFF)

if (NOT IMGUI_DIR)
    include(FetchContent)
    FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/thedmd/imgui.git
        GIT_TAG        b085dd8feac1595915d85243b5515af34a10a394 # tag layout-external-v2.1: Dear ImGui 1.93.0 WIP with Stack Layout v2.1
    )
    FetchContent_MakeAvailable(imgui)
    set(IMGUI_DIR ${imgui_SOURCE_DIR})
endif()

if (IMGUI_NODE_EDITOR_TESTS_SANITIZE)
    if (MSVC)
        add_compile_options(/fsanitize=address)
        string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    else()
        add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
    endif()
endif()

# Every translation unit counts failed IM_ASSERTs instead of aborting.
set(_TestConfig ${CMAKE_CURRENT_SOURCE_DIR}/test_config.h)
if (MSVC)
    set(_ForceInclude "/FI${_TestConfig}")
else()
    set(_ForceInclude -include "${_TestConfig}")
endif()

file(GLOB _ImGuiSources
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_stacklayout.cpp
)
add_library(imgui_for_tests STATIC ${_ImGuiSources})
target_include_directories(imgui_for_tests SYSTEM PUBLIC ${IMGUI_DIR})
target_compile_options(imgui_for_tests PUBLIC ${_ForceInclude})
target_compile_definitions(imgui_for_tests PUBLIC ${IMGUI_NODE_EDITOR_TESTS_DEFINITIONS})

add_library(imgui_node_editor_for_tests STATIC
    ${IMGUI_NODE_EDITOR_ROOT_DIR}/crude_json.cpp
    ${IMGUI_NODE_EDITOR_ROOT_DIR}/imgui_canvas.cpp
    ${IMGUI_NODE_EDITOR_ROOT_DIR}/imgui_node_editor.cpp
    ${IMGUI_NODE_EDITOR_ROOT_DIR}/imgui_node_editor_api.cpp
)
target_include_directories(imgui_node_editor_for_tests PUBLIC ${IMGUI_NODE_EDITOR_ROOT_DIR})
target_link_libraries(imgui_node_editor_for_tests PUBLIC imgui_for_tests)

function(treat_warnings_as_errors _Target)
    if (IMGUI_NODE_EDITOR_WARNINGS_AS_ERRORS)
        if (MSVC)
            target_compile_options(${_Target} PRIVATE /W4 /WX)
        else()
            target_compile_options(${_Target} PRIVATE -Wall -Wextra -Werror)
        endif()
    endif()
endfunction()

treat_warnings_as_errors(imgui_node_editor_for_tests)

set(_TestSources
    api_tests.cpp
    canvas_tests.cpp
    canvas_widget_tests.cpp
    crude_json_renamed.cpp
    crude_json_tests.cpp
    deletion_tests.cpp
    draw_order_tests.cpp
    edit_tests.cpp
    flow_tests.cpp
    forget_tests.cpp
    geometry_tests.cpp
    group_tests.cpp
    interaction_tests.cpp
    lifetime_tests.cpp
    link_tests.cpp
    math_tests.cpp
    node_state_tests.cpp
    operator_tests.cpp
    protocol_tests.cpp
    settings_handler_tests.cpp
    settings_tests.cpp
    smoke_tests.cpp
    state_tests.cpp
    suspend_tests.cpp
    view_tests.cpp
)

add_executable(node_editor_tests
    harness.cpp
    harness.h
    main.cpp
    test_config.h
    ${_TestSources}
)
target_link_libraries(node_editor_tests PRIVATE imgui_node_editor_for_tests)
if (IMGUI_NODE_EDITOR_TESTS_SANITIZE)
    target_compile_definitions(node_editor_tests PRIVATE IMGUI_NODE_EDITOR_TESTS_SANITIZE)
endif()

# blueprints2-example's runtime, and the example without its window and file dialogs. C++20.
if (IMGUI_NODE_EDITOR_TESTS_BLUEPRINTS2)
    set(_Blueprints2Dir ${IMGUI_NODE_EDITOR_ROOT_DIR}/examples/blueprints2-example)
    set(_RuntimeDir ${_Blueprints2Dir}/runtime)
    add_library(blueprints2_runtime STATIC
        ${_RuntimeDir}/crude_blueprint.cpp
        ${_RuntimeDir}/crude_blueprint.h
        ${_RuntimeDir}/crude_blueprint_library.cpp
        ${_RuntimeDir}/crude_blueprint_library.h
    )
    target_include_directories(blueprints2_runtime PUBLIC ${_RuntimeDir})
    target_compile_features(blueprints2_runtime PUBLIC cxx_std_20)
    target_link_libraries(blueprints2_runtime PUBLIC imgui_node_editor_for_tests) # crude_json
    treat_warnings_as_errors(blueprints2_runtime)

    add_library(blueprints2_for_tests STATIC
        ${_Blueprints2Dir}/blueprint_editor_document.cpp
        ${_Blueprints2Dir}/blueprint_editor_icons.cpp
        ${_Blueprints2Dir}/blueprint_editor_utilities.cpp
        ${_Blueprints2Dir}/crude_layout.cpp
        ${_Blueprints2Dir}/crude_logger.cpp
        ${_Blueprints2Dir}/imgui_extras.cpp
    )
    target_include_directories(blueprints2_for_tests PUBLIC ${_Blueprints2Dir})
    target_link_libraries(blueprints2_for_tests PUBLIC blueprints2_runtime)

    # The names of the icons the example's nodes show, without the font, which the tests don't draw.
    list(APPEND CMAKE_MODULE_PATH ${IMGUI_NODE_EDITOR_ROOT_DIR}/examples/cmake)
    find_package(FontAwesome REQUIRED)
    target_link_libraries(blueprints2_for_tests PUBLIC FontAwesome)

    set(_RuntimeTestSources
        blueprints2_context_tests.cpp
        blueprints2_core_tests.cpp
        blueprints2_file_tests.cpp
        blueprints2_function_tests.cpp
        blueprints2_graph_tests.cpp
        blueprints2_library_tests.cpp
        blueprints2_tests.cpp
        blueprints2_variable_tests.cpp
    )
    add_executable(blueprints2_tests
        blueprints2_fixture.h
        harness.cpp
        harness.h
        main.cpp
        test_config.h
        ${_RuntimeTestSources}
    )
    target_link_libraries(blueprints2_tests PRIVATE blueprints2_for_tests)
    if (IMGUI_NODE_EDITOR_TESTS_SANITIZE)
        target_compile_definitions(blueprints2_tests PRIVATE IMGUI_NODE_EDITOR_TESTS_SANITIZE)
    endif()
endif()

# One CTest test per TEST_CASE. Tests of open findings are expected to fail, and
# tests that detect memory errors run only in sanitized builds.
enable_testing()
function(add_test_cases _Executable)
    foreach (_Source ${ARGN})
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_Source})
        file(STRINGS ${_Source} _Lines REGEX "^TEST_CASE\\(")
        foreach (_Line ${_Lines})
            if (NOT _Line MATCHES "^TEST_CASE\\([A-Za-z0-9_]+, \"([^\"]+)\", ([^,]+), ([^)]+)\\)")
                message(FATAL_ERROR "${_Source}: TEST_CASE must stay on one line with literal arguments: ${_Line}")
            endif()
            set(_Name  "${CMAKE_MATCH_1}")
            set(_Flags "${CMAKE_MATCH_3}")
            if (_Flags MATCHES "NeedsSanitizer" AND NOT IMGUI_NODE_EDITOR_TESTS_SANITIZE)
                continue()
            endif()
            add_test(NAME ${_Name} COMMAND ${_Executable} ${_Name})
            set_tests_properties(${_Name} PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
            if (_Flags MATCHES "KnownFailure")
                set_tests_properties(${_Name} PROPERTIES WILL_FAIL TRUE)
            endif()
        endforeach()
    endforeach()
endfunction()

add_test_cases(node_editor_tests ${_TestSources})
if (IMGUI_NODE_EDITOR_TESTS_BLUEPRINTS2)
    add_test_cases(blueprints2_tests ${_RuntimeTestSources})
endif()
