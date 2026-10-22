include(FetchContent)

if (NOT TARGET imgui)
    find_package(Git REQUIRED)

    # The patch goes to the fetched sources: a directory in FETCHCONTENT_SOURCE_DIR_IMGUI is used as it is.
    # git apply takes it with LF line endings, which a checkout with core.autocrlf doesn't keep.
    file(READ "${CMAKE_CURRENT_LIST_DIR}/patches/imgui-glyphs-on-whole-pixels.patch" _ImGuiPatch)
    string(REPLACE "\r\n" "\n" _ImGuiPatch "${_ImGuiPatch}")
    string(SHA256 _ImGuiPatchHash "${_ImGuiPatch}")
    file(WRITE "${CMAKE_BINARY_DIR}/imgui-glyphs-on-whole-pixels.patch" "${_ImGuiPatch}")

    FetchContent_Declare(
        imgui
        GIT_REPOSITORY      https://github.com/thedmd/imgui.git
        GIT_TAG             9172446182e4519cfc1827453b32ece876c8f05f # combined-docking: Dear ImGui 1.93.0 docking with Stack Layout v2.2
        # With the hash in the command, a changed patch is applied again.
        PATCH_COMMAND       ${CMAKE_COMMAND} "-DGIT_EXECUTABLE=${GIT_EXECUTABLE}" "-DPATCH=${CMAKE_BINARY_DIR}/imgui-glyphs-on-whole-pixels.patch" -DPATCH_SHA256=${_ImGuiPatchHash} -P "${CMAKE_CURRENT_LIST_DIR}/ApplyPatch.cmake"
    )

    FetchContent_MakeAvailable(imgui)

    file(GLOB SOURCES CONFIGURE_DEPENDS "${imgui_SOURCE_DIR}/*.cpp" "${imgui_SOURCE_DIR}/*.h")

    source_group(TREE ${imgui_SOURCE_DIR} FILES ${SOURCES})

    set(imgui_natvis_path "${imgui_SOURCE_DIR}/misc/debuggers/imgui.natvis")

    add_library(imgui STATIC ${SOURCES} ${imgui_natvis_path})

    target_include_directories(imgui PUBLIC
        ${imgui_SOURCE_DIR}
    )

    # They change the layout of ImGui's types, so everything built with ImGui sees them.
    set(IMGUI_NODE_EDITOR_EXAMPLES_DEFINITIONS "" CACHE STRING "Definitions for Dear ImGui and the examples, for example IMGUI_DISABLE_OBSOLETE_FUNCTIONS")
    target_compile_definitions(imgui PUBLIC ${IMGUI_NODE_EDITOR_EXAMPLES_DEFINITIONS})

    set_target_properties(
        imgui
        PROPERTIES
            FOLDER "external"
    )
endif()

FetchContent_GetProperties(imgui)

foreach(component ${imgui_FIND_COMPONENTS})

    if (TARGET imgui_${component})
        set(imgui_${component}_FOUND TRUE)
        continue()
    endif()

    file(GLOB COMPONENT_SOURCES CONFIGURE_DEPENDS "${imgui_SOURCE_DIR}/backends/imgui_impl_${component}*")
    if (NOT COMPONENT_SOURCES)
        set(imgui_${component}_FOUND FALSE)
        continue()
    endif()

    source_group(TREE ${imgui_SOURCE_DIR}/backends FILES ${COMPONENT_SOURCES})

    add_library(imgui_${component} STATIC ${COMPONENT_SOURCES})

    target_include_directories(imgui_${component} PUBLIC
        ${imgui_SOURCE_DIR}/backends
    )

    target_link_libraries(imgui_${component} PUBLIC imgui)

    if (component MATCHES "dx11")
        target_link_libraries(imgui_${component} PUBLIC d3d11)
    elseif (component MATCHES "opengl*")
        find_package(OpenGL REQUIRED COMPONENTS OpenGL)
        target_link_libraries(imgui_${component} PUBLIC OpenGL::GL)
    elseif(component STREQUAL "glfw")
        find_package(glfw REQUIRED)
        target_link_libraries(imgui_${component} PUBLIC glfw)
    endif()

    add_library(imgui::${component} ALIAS imgui_${component})

    set_target_properties(
        imgui_${component}
        PROPERTIES
            FOLDER "external"
    )

    set(imgui_${component}_FOUND TRUE)

endforeach()

if (TARGET imgui)
    set(imgui_LIBRARY imgui)
endif()

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(
    imgui
    REQUIRED_VARS imgui_LIBRARY
    HANDLE_COMPONENTS
)

