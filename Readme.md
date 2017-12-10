# Node Editor in ImGui

## About

This is an implementaion of node editor with ImGui-like API.

Project purpose is to serve as a basis for more complex solutions like blueprint editors.

![Preview](Screenshots/node_editor_overview.gif)

## Code

Editor was developed in Visual Studio and updated to Visual Studio 2017, solution file is provided.

**Node**: Editor code has pending refactor. Since almost a year has passed without time to do it, please don't tell me code style is horrible. It was and still is a prototype.

### Public API

Public API of node editor is located in [NodeEditor.h](Editor\Editor\NodeEditor.h). For an examples of usage, please see [application.cpp](Editor\Application\application.cpp).

Minimal example of simple node can be found in [application_simple.cpp](Editor\Application\application_simple.cpp).
If you do not see any node, press 'F' in editor to focus on content.
```cpp
# include "application.h"
# include "Editor/NodeEditor.h"

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

![application_simple.png](Screenshots/application_simple.png)


### Dependencies

Code is using own copy of ImGui v1.50 WIP with modifications:
 * New: [Stack Based Layout](https://github.com/ocornut/imgui/pull/846) implementation
 * New: Added option to disable creation of global context. That simplifies management of multi-context environment by removing issues with static initialization. Required in work derived from this project.
 * New: Add ImMatrix for stacked transformation. This is pulled from derived project.
 * Changed: Made ImGuiStyle shareable between contexts. Required in work derived from this project.
 * Changed: Don't clamp negative mouse coordinates to 0. Clamping caused problems with dragging nodes and canvas scrolling. Also was inconsistent behaviod since right bottom edges of DisplaySide did not clamp.
 * Changed: Ability to scale polygon AA fringe. This was needed to achieve nice looking zoomed contend.

As a backend modified `imgui_impl_dx11` is used. Changes:
 * Add: Add ability to create custom RGBA textures.
 * Fixed: Return values of WndProc are now correct. (already fixed in main branch)
 * Changed: Font atlas texture is gamma corrected and clamped
 * Changed: ImGui cursors are mapped to native cursors. It was easy to get this wrong, and I did in first try. WinAPI is a funny thing.

