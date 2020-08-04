project(application)

set(_Application_Sources
    include/application.h
)

if (WIN32)
    list(APPEND _Application_Sources
        source/dx11/entry.cpp
        source/dx11/imgui_impl_dx11.cpp
        source/dx11/imgui_impl_dx11.h
        source/dx11/imgui_impl_win32.cpp
        source/dx11/imgui_impl_win32.h
    )
else()
    find_package(glfw3 3 REQUIRED)
    find_package(OpenGL REQUIRED)

    list(APPEND _Application_Sources
        source/glfw/entry.cpp
        source/glfw/imgui_impl_glfw_gl3.cpp
        source/glfw/imgui_impl_glfw_gl3.h
    )
endif()


source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${_Application_Sources})

add_library(application STATIC ${_Application_Sources})

target_include_directories(application PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

find_package(imgui REQUIRED)
find_package(stb_image REQUIRED)
find_package(ScopeGuard REQUIRED)
target_link_libraries(application PUBLIC imgui)
target_link_libraries(application PRIVATE stb_image ScopeGuard)

if (WIN32)
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

    target_link_libraries(application PRIVATE d3d11.lib d3dcompiler.lib d3dx11)
else()
    target_include_directories(application PRIVATE ${OPENGL_INCLUDE_DIR})

    find_package(gl3w REQUIRED)
    target_link_libraries(application PRIVATE ${OPENGL_gl_LIBRARY} gl3w glfw)

    if (APPLE)
        target_link_libraries(application PRIVATE
            "-framework CoreFoundation"
            "-framework Cocoa"
            "-framework OpenGL"
            "-framework IOKit"
            "-framework CoreVideo"
        )
    endif()
endif()

set_property(TARGET application PROPERTY FOLDER "examples")
