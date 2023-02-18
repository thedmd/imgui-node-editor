if (TARGET imgui_backends)
    return()
endif()

set(_imgui_backends_SourceDir ${IMGUI_NODE_EDITOR_ROOT_DIR}/backends)


find_package(imgui REQUIRED)
find_package(stb_image REQUIRED)

# find_package(OpenGL)

set(_Backends_Sources ${_imgui_backends_SourceDir}/imgui_extra_keys.h)
add_library(imgui_backends STATIC ${_Backends_Sources})
target_include_directories(imgui_backends PUBLIC ${_imgui_backends_SourceDir})
target_link_libraries(imgui_backends PUBLIC imgui stb_image)

if (WIN32)
    list(APPEND _Backends_Sources
        ${IMGUI_NODE_EDITOR_ROOT_DIR}/backends/imgui_impl_dx11.cpp
        ${IMGUI_NODE_EDITOR_ROOT_DIR}/backends/imgui_impl_dx11.h
        ${IMGUI_NODE_EDITOR_ROOT_DIR}/backends/imgui_impl_win32.cpp
        ${IMGUI_NODE_EDITOR_ROOT_DIR}/backends/imgui_impl_win32.h
    )

    set(_DXSDK_Dir  ${IMGUI_NODE_EDITOR_ROOT_DIR}/external/DXSDK)
    set(_DXSDK_Arch x86)
    if (${CMAKE_SIZEOF_VOID_P} EQUAL 8)
        set(_DXSDK_Arch x64)
    endif()

    add_library(dxerr STATIC ${_DXSDK_Dir}/src/dxerr.cpp)
    target_include_directories(dxerr PUBLIC "${_DXSDK_Dir}/include")
    set_property(TARGET dxerr PROPERTY FOLDER "external")

    add_library(d3dx11 UNKNOWN IMPORTED)
    set_target_properties(d3dx11 PROPERTIES
        IMPORTED_LOCATION               "${_DXSDK_Dir}/lib/${_DXSDK_Arch}/d3dx11.lib"
        IMPORTED_LOCATION_DEBUG         "${_DXSDK_Dir}/lib/${_DXSDK_Arch}/d3dx11d.lib"
        INTERFACE_INCLUDE_DIRECTORIES   "${_DXSDK_Dir}/include"
        INTERFACE_LINK_LIBRARIES        "$<$<CONFIG:Debug>:dxerr>"
    )

    target_link_libraries(imgui_backends PRIVATE d3d11.lib d3dcompiler.lib d3dx11)
else()
    find_package(OpenGL REQUIRED)
    find_package(glfw3 3 REQUIRED)

    if (APPLE)
        target_link_libraries(imgui_backends PRIVATE
            "-framework CoreFoundation"
            "-framework Cocoa"
            "-framework IOKit"
            "-framework CoreVideo"
        )
    endif()
endif()

if (OpenGL_FOUND)
    set(HAVE_OPENGL YES)

    find_package(gl3w REQUIRED)
    # Explicitly select embedded GL3W loader
    target_compile_definitions(imgui_backends PRIVATE IMGUI_IMPL_OPENGL_LOADER_GL3W)

    target_include_directories(imgui_backends PRIVATE ${OPENGL_INCLUDE_DIR})
    target_link_libraries(imgui_backends PRIVATE ${OPENGL_gl_LIBRARY} gl3w)
    list(APPEND _Backends_Sources
        ${IMGUI_NODE_EDITOR_ROOT_DIR}/backends/imgui_impl_opengl3.cpp
        ${IMGUI_NODE_EDITOR_ROOT_DIR}/backends/imgui_impl_opengl3.h
    )
endif()

if (glfw3_FOUND)
    set(HAVE_GLFW3 YES)

    list(APPEND _Backends_Sources
        imgui_impl_glfw.cpp
        imgui_impl_glfw.h
    )
    target_link_libraries(imgui_backends PRIVATE
        glfw
    )
endif()

target_sources(imgui_backends PRIVATE ${_Backends_Sources})

include(${CMAKE_ROOT}/Modules/FindPackageHandleStandardArgs.cmake)

find_package_handle_standard_args(
    imgui_backends
    REQUIRED_VARS
        _imgui_backends_SourceDir
)
