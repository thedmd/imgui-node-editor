
set(_Application_Sources
    Include/Application.h
)

if (WIN32)
    list(APPEND _Application_Sources
        Source/DX11/Entry.cpp
        Source/DX11/imgui_impl_dx11.cpp
        Source/DX11/imgui_impl_dx11.h
        Source/DX11/imgui_impl_win32.cpp
        Source/DX11/imgui_impl_win32.h
    )
else()
    find_package(glfw3 3 REQUIRED)
    find_package(OpenGL REQUIRED)

    list(APPEND _Application_Sources
        Source/GLFW/Entry.cpp
        Source/GLFW/imgui_impl_glfw_gl3.cpp
        Source/GLFW/imgui_impl_glfw_gl3.h
    )
endif()



source_group("" FILES ${_Application_Sources})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${_Application_Sources})

add_library(Application STATIC ${_Application_Sources})

target_include_directories(Application PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Include)

find_package(imgui REQUIRED)
find_package(stb_image REQUIRED)
find_package(ScopeGuard REQUIRED)
target_link_libraries(Application PUBLIC imgui ScopeGuard)
target_link_libraries(Application PRIVATE stb_image)

if (WIN32)
    set(_DXSDK_Dir  ${CMAKE_SOURCE_DIR}/ThirdParty/DXSDK)
    set(_DXSDK_Arch x86)
    if (${CMAKE_SIZEOF_VOID_P} EQUAL 8)
        set(_DXSDK_Arch x64)
    endif()

    add_library(dxerr STATIC ${_DXSDK_Dir}/src/dxerr.cpp)
    target_include_directories(dxerr PUBLIC "${_DXSDK_Dir}/include")
    set_property(TARGET dxerr PROPERTY FOLDER "ThirdParty")

    add_library(d3dx11 UNKNOWN IMPORTED)
    set_target_properties(d3dx11 PROPERTIES
        IMPORTED_LOCATION               "${_DXSDK_Dir}/lib/${_DXSDK_Arch}/d3dx11.lib"
        IMPORTED_LOCATION_DEBUG         "${_DXSDK_Dir}/lib/${_DXSDK_Arch}/d3dx11d.lib"
        INTERFACE_INCLUDE_DIRECTORIES   "${_DXSDK_Dir}/include"
        INTERFACE_LINK_LIBRARIES        "$<$<CONFIG:Debug>:dxerr>"
    )

    target_link_libraries(Application PRIVATE d3d11.lib d3dcompiler.lib d3dx11)
else()
    target_include_directories(Application PRIVATE ${OPENGL_INCLUDE_DIR})

    find_package(gl3w REQUIRED)
    target_link_libraries(Application PRIVATE ${OPENGL_gl_LIBRARY} gl3w glfw)

    if (APPLE)
        target_link_libraries(Application PRIVATE
            "-framework CoreFoundation"
            "-framework Cocoa"
            "-framework OpenGL"
            "-framework IOKit"
            "-framework CoreVideo"
        )
    endif()
endif()

set_property(TARGET Application PROPERTY FOLDER "Examples/Common")
