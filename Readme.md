# Node Editor in ImGui

[![Appveyor status](https://ci.appveyor.com/api/projects/status/lm0io3m8mv7avacp/branch/master?svg=true)](https://ci.appveyor.com/project/thedmd/imgui-node-editor/branch/master)
[![Travis status](https://travis-ci.org/thedmd/imgui-node-editor.svg?branch=master)](https://travis-ci.org/thedmd/imgui-node-editor)


## About

This is an implementaion of node editor with ImGui-like API.

Project purpose is to serve as a basis for more complex solutions like blueprint editors.

![Preview](Screenshots/node_editor_overview.gif)

Project is a prototype. What that means in practise:
 * API will break 
 * Project is hacked around fresh ideas, it does not pursue production quality, but should be decent enough to use without hassle
 * Relies on modified version of ImGui 1.72 (WIP) with:
    - https://github.com/thedmd/imgui/tree/feature/draw-list-fringe-scale
    - https://github.com/thedmd/imgui/tree/feature/extra-keys
    - https://github.com/thedmd/imgui/tree/feature/layout (used in blueprints sample only)
 * Keeping zero dependencies is not a priority, current one:
    - C++14
    - std::map, std::vector
    - picojson
 * Please report issues or questions if something isn't clear
 
## Status

Project is used to implement blueprint editor in Spark CE engine. It proven itself so furher work can be pursued. Some goals:
 * Remove dependency from picojson
 * Remove dependency from std::map and std::vector (ImVector<> should be sufficient)
 * Investigate if C++14 is low bar enough
 * Use vanila copy of ImGui

## Code

Editor code is in `NodeEditor` directory alone and can be build with CMake:
```
Windows:
    cmake -H. -BBuild -G "Visual Studio 15 2017 Win64"

macOS:
    cmake -H. -BBuild -G "Xcode"

Linux:
    cmake -H. -BBuild -G "Unix Makefiles"

Build:
    cmake --build Build --config Release
```
You will find examples in `Build\Bin` directory.

Source code target to be comatible with C++14.

### Example

Main node editor header is located in [imgui_node_editor.h](NodeEditor/Include/imgui_node_editor.h).

Minimal example of one node can be found in [Simple.cpp](Examples/00-Simple/Simple.cpp).
Press 'F' in editor to focus on editor content if you see only grid.
```cpp
# include "Application.h"
# include <imgui_node_editor.h>

namespace ed = ax::NodeEditor;

static ed::EditorContext* g_Context = nullptr;

void Application_Initialize()
{
    g_Context = ed::CreateEditor();
}

void Application_Finalize()
{
    ed::DestroyEditor(g_Context);
}

void Application_Frame()
{
    ed::SetCurrentEditor(g_Context);

    ed::Begin("My Editor");

    int uniqueId = 1;

    // Start drawing nodes.
    ed::BeginNode(uniqueId++);
        ImGui::Text("Node A");
        ed::BeginPin(uniqueId++, ed::PinKind::Target);
            ImGui::Text("-> In");
        ed::EndPin();
        ImGui::SameLine();
        ed::BeginPin(uniqueId++, ed::PinKind::Source);
            ImGui::Text("Out ->");
        ed::EndPin();
    ed::EndNode();

    ed::End();
}
```

Result:

![00-Simple.png](Screenshots/00-Simple.png)

For more details please visit [Examples](Examples) folder.
