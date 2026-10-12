if (TARGET glfw)
    return()
endif()

include(FetchContent)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        7b6aead9fb88b3623e3b3725ebb42670cbe4c579 # 3.4
    FIND_PACKAGE_ARGS 3.4 NAMES glfw3
)

set(GLFW_BUILD_EXAMPLES OFF CACHE INTERNAL "Build the GLFW example programs")
set(GLFW_BUILD_TESTS OFF CACHE INTERNAL "Build the GLFW test programs")
set(GLFW_BUILD_DOCS OFF CACHE INTERNAL "Build the GLFW documentation")
set(GLFW_INSTALL OFF CACHE INTERNAL "Generate installation target")

FetchContent_MakeAvailable(glfw)

if (TARGET update_mappings)
    set_target_properties(
        glfw
        update_mappings
        PROPERTIES
            FOLDER "external"
    )
endif()

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(
    glfw
    DEFAULT_MSG
)