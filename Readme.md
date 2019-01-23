# Node Editor in ImGui

[![Build status](https://ci.appveyor.com/api/projects/status/lm0io3m8mv7avacp?svg=true)](https://ci.appveyor.com/project/thedmd/imgui-node-editor)


## About

This is an implementaion of node editor with ImGui-like API.

Project purpose is to serve as a basis for more complex solutions like blueprint editors.

![Preview](Screenshots/node_editor_overview.gif)

Project is a prototype. What that means in practise:
 * API will break. Current API does not help user do the right things and require some head banging on the wall to understand it. Can be done much more better.
 * Code is hacked to test ideas, not to pursue production quality
 * Relies on modified version of ImGui. There is a goal to use vanila version of ImGui and provide hacked-in code as an extension.
 * Keeping dependencies minimal is not a priority. In the long run C++14 without non ImGui dependencies should be a minimum. Currently you will find parts of C++17 and use of picojson.
 * If you have issues with editor, please let me know. I'm not going to promise immediate fix but I for sure will take them into account while reworking code.


## Code

Editor code is in `NodeEditor` directory alone. Project can be build with examples with help of CMake 3.8. macOS and Linux require GLFW3 to be installed on your system.
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

Source code target to be comatible with C++11. Due to last minute pulls it require C++17. This requirement will soon be removed. Sorry for inconvenience.

**Note**: Editor code has pending refactor. Since almost a year has passed without time to do it, please don't tell me code style is horrible. It was and still is a prototype.

### Public API

Public API of node editor is located in [NodeEditor.h](NodeEditor/Include/NodeEditor.h). For an examples of usage, please see [Examples](Examples).

Minimal example of simple node can be found in [Simple.cpp](Examples/00-Simple/Simple.cpp).
If you do not see any node, press 'F' in editor to focus on content.
```cpp
# include "Application.h"
# include "NodeEditor.h"

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


### Dependencies

Code is using own copy of [ImGui v1.67 WIP with modifications](https://github.com/thedmd/imgui/tree/combined):
 * New: [Stack Based Layout](https://github.com/ocornut/imgui/pull/846) implementation
 * New: Add ImMatrix for stacked transformation. This is pulled from derived project.
 * Changed: Ability to scale polygon AA fringe. This was needed to achieve nice looking zoomed contend.

As a backend modified `imgui_impl_dx11` is used. Changes:
 * Add: Add ability to create custom RGBA textures.
 * Fixed: Return values of WndProc are now correct. (already fixed in main branch)
 * Changed: Font atlas texture is gamma corrected and clamped
 * Changed: ImGui cursors are mapped to native cursors. It was easy to get this wrong, and I did in first try. WinAPI is a funny thing.

