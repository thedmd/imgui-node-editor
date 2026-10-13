include(FetchContent)

if (NOT TARGET stb)
    FetchContent_Declare(
        stb
        GIT_REPOSITORY      https://github.com/nothings/stb.git
        GIT_TAG             2c980bb59875b0d32144a71867fbdebb2f77cd20
    )

    FetchContent_MakeAvailable(stb)

    add_library(stb INTERFACE)

    set_target_properties(
        stb
        PROPERTIES
            FOLDER "external"
    )
endif()

FetchContent_GetProperties(stb)

foreach(component ${stb_FIND_COMPONENTS})

    if (TARGET stb_${component})
        set(stb_${component}_FOUND TRUE)
        continue()
    endif()

    set(HEADER "${stb_SOURCE_DIR}/stb_${component}.h")
    if (NOT EXISTS "${HEADER}")
        set(stb_${component}_FOUND FALSE)
        continue()
    endif()
    set(SOURCE "${stb_BINARY_DIR}/stb_${component}_generated.c")

    string(TOUPPER ${component} componentUppercase)

    file(GENERATE
        OUTPUT "${SOURCE}"
        CONTENT "#define STB_${componentUppercase}_IMPLEMENTATION\n#include \"${HEADER}\""
    )

    source_group("" FILES "${HEADER}" "${SOURCE}")

    add_library(stb_${component} STATIC "${SOURCE}" "${HEADER}")

    target_include_directories(stb_${component} PUBLIC
        ${stb_SOURCE_DIR}
    )

    add_library(stb::${component} ALIAS stb_${component})

    set_target_properties(
        stb_${component}
        PROPERTIES
            FOLDER "external"
    )

    set(stb_${component}_FOUND TRUE)

endforeach()

if (TARGET stb)
    set(stb_LIBRARY stb)
endif()

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(
    stb
    REQUIRED_VARS stb_LIBRARY
    HANDLE_COMPONENTS
)

