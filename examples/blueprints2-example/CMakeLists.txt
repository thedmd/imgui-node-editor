add_example_executable(blueprints2-example
    blueprints2-example.cpp
    crude_blueprint.cpp
    crude_blueprint.h
    crude_layout.cpp
    crude_layout.h
    imgui_extras.cpp
    imgui_extras.h
    imgui_blueprint_utilities.cpp
    imgui_blueprint_utilities.h
    external/include/nonstd/span.hpp
    external/include/nonstd/string_view.hpp
    external/include/nonstd/variant.hpp
    misc/crude_blueprint.natvis
    misc/nonstd.natvis
)

target_include_directories(blueprints2-example PRIVATE external/include)

if (MSVC)
    target_compile_definitions(blueprints2-example PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()