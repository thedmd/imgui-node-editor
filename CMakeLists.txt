cmake_minimum_required(VERSION 3.24)
Project(imgui_node_editor)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/misc/cmake-modules)

set(IMGUI_NODE_EDITOR_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR})
add_subdirectory(${IMGUI_NODE_EDITOR_ROOT_DIR}/examples/application)

find_package(imgui REQUIRED)

set(${PROJECT_NAME}_Sources
    ${CMAKE_CURRENT_SOURCE_DIR}/crude_json.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui_bezier_math.inl
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui_canvas.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui_extra_math.inl
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui_node_editor_api.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui_node_editor_internal.inl
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui_node_editor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/misc/imgui_node_editor.natvis
)

set(${PROJECT_NAME}_Headers
    ${CMAKE_CURRENT_SOURCE_DIR}/crude_json.h
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui_bezier_math.h
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui_canvas.h
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui_extra_math.h
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui_node_editor_internal.h
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui_node_editor.h
)

add_library(${PROJECT_NAME} STATIC ${${PROJECT_NAME}_Sources} ${${PROJECT_NAME}_Headers})

target_include_directories(${PROJECT_NAME} PUBLIC 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_PREFIX}/include/${PROJECT_NAME}/>
)

target_link_libraries(${PROJECT_NAME} PUBLIC imgui)
target_link_directories(${PROJECT_NAME} PUBLIC $<INSTALL_INTERFACE:lib>)

install(
    TARGETS         ${PROJECT_NAME} imgui application stb_image ScopeGuard
    EXPORT          Targets
)

if(WIN32)
install(
    FILES       external/DXSDK/lib/x64/d3dx11.lib
    DESTINATION lib
)
endif()

install(
    FILES       ${${PROJECT_NAME}_Headers}
    DESTINATION include/${PROJECT_NAME}
)

install(
    EXPORT      Targets
    FILE        ${PROJECT_NAME}Config.cmake
    NAMESPACE   ${PROJECT_NAME}::
    DESTINATION ${CMAKE_INSTALL_PREFIX}
)