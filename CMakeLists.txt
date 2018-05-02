cmake_minimum_required(VERSION 3.8)
project(ImGuiNodeEditor)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(CMAKE_CXX_STANDARD            17)
set(CMAKE_CXX_STANDARD_REQUIRED   YES)

if (DEFINED MSVC_VERSION)
    if (MSVC_VERSION GREATER_EQUAL 1500)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /std:c++latest")
    endif()
    if (MSVC_VERSION GREATER_EQUAL 1910)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /permissive-")
    endif()
endif()


add_subdirectory(ThirdParty/ImGui)
add_subdirectory(ThirdParty/picojson)
add_subdirectory(ThirdParty/stb_image)

add_subdirectory(NodeEditor)

add_subdirectory(Examples)
