cmake_minimum_required (VERSION 3.8)
project(imgui_node_editor)

add_library(${PROJECT_NAME} STATIC 
crude_json.cpp
imgui_canvas.cpp
imgui_node_editor.cpp
imgui_node_editor_api.cpp
 "crude_json.cpp" "crude_json.h" "examples/application/include/application.h" "examples/application/source/dx11/entry.cpp" "examples/application/source/dx11/imgui_impl_dx11.cpp" "examples/application/source/dx11/imgui_impl_dx11.h" "examples/application/source/dx11/imgui_impl_win32.cpp" "examples/application/source/dx11/imgui_impl_win32.h" "examples/application/source/glfw/entry.cpp" "examples/application/source/glfw/imgui_impl_glfw_gl3.cpp" "examples/application/source/glfw/imgui_impl_glfw_gl3.h" "examples/basic-interaction-example/basic-interaction-example.cpp" "examples/blueprints-example/blueprints-example.cpp" "examples/blueprints-example/utilities/builders.cpp" "examples/blueprints-example/utilities/builders.h" "examples/blueprints-example/utilities/drawing.cpp" "examples/blueprints-example/utilities/drawing.h" "examples/blueprints-example/utilities/widgets.cpp" "examples/blueprints-example/utilities/widgets.h" "examples/canvas-example/canvas-example.cpp" "examples/simple-example/simple-example.cpp" "external/DXSDK/include/D3DX11.h" "external/DXSDK/include/D3DX11async.h" "external/DXSDK/include/D3DX11core.h" "external/DXSDK/include/D3DX11tex.h" "external/DXSDK/include/dxerr.h" "external/DXSDK/src/dxerr.cpp" "external/gl3w/Include/GL/gl3w.h" "external/gl3w/Include/GL/glcorearb.h" "external/gl3w/Source/gl3w.c" "external/imgui/imconfig.h" "external/imgui/imgui.cpp" "external/imgui/imgui.h" "external/imgui/imgui_demo.cpp" "external/imgui/imgui_draw.cpp" "external/imgui/imgui_internal.h" "external/imgui/imgui_widgets.cpp" "external/imgui/imstb_rectpack.h" "external/imgui/imstb_textedit.h" "external/imgui/imstb_truetype.h" "external/ScopeGuard/ScopeGuard.h" "external/stb_image/stb_image.h" "imgui_bezier_math.h" "imgui_canvas.cpp" "imgui_canvas.h" "imgui_extra_math.h" "imgui_node_editor.cpp" "imgui_node_editor.h" "imgui_node_editor_api.cpp" "imgui_node_editor_internal.h")

target_link_libraries(${PROJECT_NAME} imgui)

target_include_directories(${PROJECT_NAME}
    INTERFACE
    ./
    )