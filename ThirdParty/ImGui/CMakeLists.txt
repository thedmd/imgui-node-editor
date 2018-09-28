
set(_ImGui_Sources
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

source_group("" FILES ${_ImGui_Sources})

add_library(ImGui STATIC ${_ImGui_Sources})

target_include_directories(ImGui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

set_property(TARGET ImGui PROPERTY FOLDER "ThirdParty")
