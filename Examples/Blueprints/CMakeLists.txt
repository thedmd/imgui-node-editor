add_example(Blueprints
    Blueprints.cpp
)

find_package(blueprint-utilities REQUIRED)

target_link_libraries(Blueprints PRIVATE blueprint-utilities)
