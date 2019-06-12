add_example(01-Blueprints
    Blueprints.cpp
)

find_package(blueprint-utilities REQUIRED)

target_link_libraries(01-Blueprints PRIVATE blueprint-utilities)
