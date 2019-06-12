project(imgui)

set(_imgui_Sources
    imgui.cpp
    imgui_demo.cpp
    imgui_draw.cpp
    imgui_widgets.cpp
    imconfig.h
    imgui.h
    imgui_internal.h
    imstb_rectpack.h
    imstb_textedit.h
    imstb_truetype.h
)

source_group("" FILES ${_imgui_Sources})

add_library(${PROJECT_NAME} STATIC ${_imgui_Sources})

target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_definitions(${PROJECT_NAME} PUBLIC "ImDrawIdx=unsigned int")

set_property(TARGET ${PROJECT_NAME} PROPERTY FOLDER "ThirdParty")
