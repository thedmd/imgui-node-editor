add_example_executable(blueprints2-example
    blueprints2-example.cpp
    crude_blueprint.cpp
    crude_blueprint.h
    crude_blueprint_library.cpp
    crude_blueprint_library.h
    crude_blueprint_utilities.cpp
    crude_blueprint_utilities.h
    crude_layout.cpp
    crude_layout.h
    imgui_extras.cpp
    imgui_extras.h
    external/nonstd/include/nonstd/span.hpp
    external/nonstd/include/nonstd/string_view.hpp
    external/nonstd/include/nonstd/variant.hpp
    external/tinyfiledialogs/include/tinyfiledialogs.h
    external/tinyfiledialogs/src/tinyfiledialogs.c
    misc/crude_blueprint.natvis
    misc/nonstd.natvis
)

target_include_directories(blueprints2-example PRIVATE
    external/nonstd/include
    external/tinyfiledialogs/include
)

if (MSVC)
    target_compile_definitions(blueprints2-example PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
