# Node Editor in ImGui

[![build](https://github.com/thedmd/imgui-node-editor/actions/workflows/build.yml/badge.svg)](https://github.com/thedmd/imgui-node-editor/actions/workflows/build.yml)

## About

An implementation of node editor with ImGui-like API.

Project purpose is to serve as a basis for more complex solutions like blueprint editors.

![node_editor_overview](https://user-images.githubusercontent.com/1197433/89328475-c01bc680-d68d-11ea-88bf-8c4155480927.gif)

Node Editor is build around an idea "draw your content, we do the rest", which mean interactions are handled by editor, content rendering is handled by user. Editor will take care of:
 * placing your node in the word
 * dragging nodes
 * zoom and scrolling
 * selection
 * various interaction that can be queried by API (creation, deletion, selection changes, etc.)

Here are some highlights:
 * Node movement and selection is handled internally
 * Node and pin contents are fully customizable
 * Fully styled, default theme is modeled after UE4 blueprints
    - Flexible enough to produce such nodes:

        ![image](https://user-images.githubusercontent.com/1197433/60381408-c3895b00-9a54-11e9-8312-d9fc9af63347.png)
        ![image](https://user-images.githubusercontent.com/1197433/60381400-a3599c00-9a54-11e9-9c51-a88f25f7db07.png)
        ![image](https://user-images.githubusercontent.com/1197433/60381589-7d81c680-9a57-11e9-87b1-9f73ec33bea4.png)
    - Customizable links based on Bézier curves:

        ![image](https://user-images.githubusercontent.com/1197433/60381475-ac973880-9a55-11e9-9ad9-5862975cd2b8.png)
        ![image](https://user-images.githubusercontent.com/1197433/60381467-9db08600-9a55-11e9-9868-2ae849f67de9.png)
        ![image](https://user-images.githubusercontent.com/1197433/60381488-cd5f8e00-9a55-11e9-8346-1f4c8d6bea22.png)
 * Automatic highlights for nodes, pins and links:

    ![image](https://user-images.githubusercontent.com/1197433/60381536-9e95e780-9a56-11e9-80bb-dad0d3d9557a.png)
 * Smooth navigation and selection
 * Node state can be saved in user context, so layout will not break
 * Selection rectangles and group dragging
 * Context menu support
 * Basic shortcuts support (cut/copy/paste/delete)
 * ImGui style API

Editor is used to implement blueprint editor in Spark CE engine, it proved itself there by allowing to do everything we needed. Therefore it is now slowly moving into stable state from beeing a prototype.

Note: Project recently was restructured to mimic ImGui layout.

Please report issues or questions if something isn't clear.

## Dependencies

 * Dear ImGui 1.89 (November 2022) or newer, tested up to 1.93.0 WIP
 * C++14

### Dependencies for examples:

 * [thedmd/imgui feature/layout-external](https://github.com/thedmd/imgui/tree/feature/layout-external) (Dear ImGui 1.93.0 WIP with Stack Layout, used in blueprints sample)
 * stb, and GLFW on macOS and Linux

CMake fetches all of them at revisions pinned in [examples/cmake](../examples/cmake). An installed GLFW package is used when found.

### Optional extension you can pull into your local copy of ImGui node editor can take advantage of:
 * ~~https://github.com/thedmd/imgui/tree/feature/draw-list-fringe-scale (for sharp rendering, while zooming)~~ It is part of ImGui since 1.80 release
 * https://github.com/thedmd/imgui/tree/feature/extra-keys (for extra shortcuts)

## Building / Installing

Node Editor sources are located in root project directory. To use it, simply copy&paste sources into your project. Exactly like you can do with ImGui.

### Examples
[Examples](../examples) can be build with CMake:
```
Windows:
    cmake -S examples -B build -G "Visual Studio 17 2022"

macOS:
    cmake -S examples -B build -G "Xcode"

Linux:
    cmake -S examples -B build -G "Unix Makefiles"

Build:
    cmake --build build --config Release
```
Executables will be located in `build\bin` directory. CMake 3.24 or newer is required.

To build offline or against a local checkout of a dependency, point FetchContent at it, for example `-DFETCHCONTENT_SOURCE_DIR_IMGUI=<path>`. `FETCHCONTENT_SOURCE_DIR_STB` and `FETCHCONTENT_SOURCE_DIR_GLFW` work the same way.

### Tests
The headless [test suite](../tests) runs with CTest:
```
cmake -S tests -B build-tests
cmake --build build-tests --config Debug
ctest --test-dir build-tests -C Debug
```
`-DIMGUI_DIR=<path>` tests against another copy of Dear ImGui. `-DIMGUI_NODE_EDITOR_TESTS_SANITIZE=ON` builds with AddressSanitizer and adds the tests that detect memory errors. Tests of open issues are expected to fail until the issue is fixed.

`python tests/imgui_walk.py --imgui-repo <clone of Dear ImGui> --work <directory>` runs the suite against every Dear ImGui release from 1.89 on, and accepts `-strict` and docking variants of revisions.

### Quick Start

Main node editor header is located in [imgui_node_editor.h](../imgui_node_editor.h).

Minimal example of one node can be found in [simple-example.cpp](../examples/simple-example/simple-example.cpp).
Press 'F' in editor to focus on editor content if you see only grid.
```cpp
# include <imgui.h>
# include <imgui_node_editor.h>
# include <application.h>

namespace ed = ax::NodeEditor;

struct Example:
    public Application
{
    using Application::Application;

    void OnStart() override
    {
        ed::InstallSettingsHandler();
        m_Context = ed::CreateEditor();
    }

    void OnStop() override
    {
        ed::DestroyEditor(m_Context);
    }

    void OnFrame(float deltaTime) override
    {
        auto& io = ImGui::GetIO();

        ImGui::Text("FPS: %.2f (%.2gms)", io.Framerate, io.Framerate ? 1000.0f / io.Framerate : 0.0f);

        ImGui::Separator();

        ed::SetCurrentEditor(m_Context);
        ed::Begin("My Editor", ImVec2(0.0, 0.0f));
        int uniqueId = 1;
        // Start drawing nodes.
        ed::BeginNode(uniqueId++);
            ImGui::Text("Node A");
            ed::BeginPin(uniqueId++, ed::PinKind::Input);
                ImGui::Text("-> In");
            ed::EndPin();
            ImGui::SameLine();
            ed::BeginPin(uniqueId++, ed::PinKind::Output);
                ImGui::Text("Out ->");
            ed::EndPin();
        ed::EndNode();
        ed::End();
        ed::SetCurrentEditor(nullptr);

        //ImGui::ShowMetricsWindow();
    }

    ed::EditorContext* m_Context = nullptr;
};

int Main(int argc, char** argv)
{
    Example exampe("Simple", argc, argv);

    if (exampe.Create())
        return exampe.Run();

    return 0;
}
```

Result:

![00-Simple](https://user-images.githubusercontent.com/1197433/89328516-cca01f00-d68d-11ea-9959-2da159851101.png)

`ed::InstallSettingsHandler()` keeps node positions, the selection and the view in `imgui.ini`, next to ImGui's windows. Call it before ImGui reads `.ini` data. Applications that keep this state elsewhere set `Config::SettingsName` to `nullptr`, and use `ed::LoadSettingsFromMemory()` and `ed::SaveSettingsToMemory()` once `ed::WantSaveSettings()` is true.

For more details please visit [examples](../examples) folder.

### Blueprints Example

![Preview2](https://user-images.githubusercontent.com/1197433/60053458-2f2b9b00-96d8-11e9-92f9-08aff63b2023.png)

### Here is Node Editor at work in Spark CE
![image](https://user-images.githubusercontent.com/1197433/60381756-174a7300-9a5a-11e9-9a04-00f10565e05e.png)
![image](https://user-images.githubusercontent.com/1197433/60381760-2f21f700-9a5a-11e9-9053-c0547a9cc40a.png)
