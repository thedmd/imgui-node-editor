add_example_executable(blueprints2-example
    blueprints2-example.cpp
    crude_blueprint.cpp
    crude_blueprint.h
    external/include/nonstd/span.hpp
    external/include/nonstd/string_view.hpp
    external/include/nonstd/variant.hpp
)

target_include_directories(blueprints2-example PRIVATE external/include)