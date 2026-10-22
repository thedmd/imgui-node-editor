include(FetchContent)

if (NOT TARGET nfd)
    set(NFD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(NFD_INSTALL     OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        nfd
        GIT_REPOSITORY      https://github.com/btzy/nativefiledialog-extended.git
        GIT_TAG             86d5f2005fe1c00747348a12070fec493ea2407e # v1.2.1
    )

    FetchContent_MakeAvailable(nfd)

    set_target_properties(
        nfd
        PROPERTIES
            FOLDER "external"
    )
endif()

FetchContent_GetProperties(nfd)

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(
    nativefiledialog-extended
    REQUIRED_VARS nfd_SOURCE_DIR
)
