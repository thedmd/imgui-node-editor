project(imgui VERSION 1.76)

set(_imgui_Sources
    imgui.cpp
    imgui.natvis
    imgui_demo.cpp
    imgui_draw.cpp
    imgui_widgets.cpp
    imgui_tables.cpp
    LICENSE.txt
)

set(_imgui_headers
imconfig.h
imgui.h
imgui_internal.h
imstb_rectpack.h
imstb_textedit.h
imstb_truetype.h
)

add_library(${PROJECT_NAME} STATIC ${_imgui_Sources} ${_imgui_headers})

target_include_directories(${PROJECT_NAME} INTERFACE 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/${PROJECT_NAME}/>
)

#target_compile_definitions(${PROJECT_NAME} PUBLIC "ImDrawIdx=unsigned int")
target_compile_definitions(${PROJECT_NAME} PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${_imgui_Sources})

set_property(TARGET ${PROJECT_NAME} PROPERTY FOLDER "external")

install(
    FILES
        ${_imgui_headers}
    DESTINATION
        include/${PROJECT_NAME}    
)