add_example_executable(blueprints2-example
    blueprint_editor_document.cpp
    blueprint_editor_document.h
    blueprint_editor_icons.cpp
    blueprint_editor_icons.h
    blueprint_editor_utilities.cpp
    blueprint_editor_utilities.h
    blueprints2-example.cpp
    crude_layout.cpp
    crude_layout.h
    crude_logger.cpp
    crude_logger.h
    imgui_extras.cpp
    imgui_extras.h
    runtime/crude_blueprint.cpp
    runtime/crude_blueprint.h
    runtime/crude_blueprint_library.cpp
    runtime/crude_blueprint_library.h
    external/tinyfiledialogs/include/tinyfiledialogs.h
    external/tinyfiledialogs/src/tinyfiledialogs.c
)

# Its runtime takes C++20; the node editor keeps to C++14.
target_compile_features(blueprints2-example PRIVATE cxx_std_20)
target_include_directories(blueprints2-example PRIVATE runtime)

# Vendored headers, built with their own warnings.
target_include_directories(blueprints2-example SYSTEM PRIVATE
    external/tinyfiledialogs/include
)

if (MSVC)
    target_compile_definitions(blueprints2-example PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
