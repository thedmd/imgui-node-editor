# API reference

The comments in [imgui_node_editor.h](../imgui_node_editor.h) describe each function. This page collects the rules that span several functions: the order of calls in a frame, IDs, the create and delete protocols, input, persistence, edit reports, and the coordinate space.

The examples use the alias `namespace ed = ax::NodeEditor;`.

## Frames

An editor draws inside an ImGui window, between `ed::Begin()` and `ed::End()`:

```cpp
ed::SetCurrentEditor(editor);
ed::Begin("My Editor");

// 1. Nodes, with their pins
ed::BeginNode(nodeId);
    ImGui::TextUnformatted("Node");
    ed::BeginPin(pinId, ed::PinKind::Output);
        ImGui::TextUnformatted("Out ->");
    ed::EndPin();
ed::EndNode();

// 2. Links
ed::Link(linkId, outputPinId, inputPinId);

// 3. Interactions: create, delete, shortcuts, context menus
if (ed::BeginCreate()) { /* see below */ ed::EndCreate(); }
if (ed::BeginDelete()) { /* see below */ ed::EndDelete(); }

ed::End();
ed::SetCurrentEditor(nullptr);
```

- `ed::Begin()` belongs in a visible host window. While the host skips its items (collapsed, in a hidden dock tab, or a child window out of view), the editor measures nothing and changes no state, so the frame is harmless.
- Every object is submitted every frame. A node that isn't submitted isn't drawn, but the editor keeps its position and selection (see [IDs](#ids)).
- Queries see the frame as far as it has been submitted:

| Function | Answers for |
| --- | --- |
| `HasAnyLinks()`, `BreakLinks()`, `GetNodeCount()`, `GetOrderedNodeIds()` | The objects submitted so far in this frame. Call them after the frame's links. |
| `PinHadAnyLinks()` | The previous frame |
| `HasSelectionChanged()` | The last frame, also when asked between `Begin()` and `End()` |
| `GetNodeBackgroundDrawList()` | A node after its `EndNode()` in this frame; `nullptr` otherwise |
| `IsFocused()` | The host window's focus at `Begin()` |
| `GetCurrentZoom()` | The zoom factor: 2 at 200%, as in `Config::CustomZoomLevels` |

- `ForgetNode()` and `ClearState()` work outside `Begin()`/`End()`, and assert inside.

### `Begin*()` and `End*()`

As with ImGui's own `BeginXXX()`/`EndXXX()` pairs, call `EndCreate()`, `EndDelete()`, `EndShortcut()` and `EndEdits()` only when the matching `BeginCreate()`, `BeginDelete()`, `BeginShortcut()` or `BeginEdits()` returned true. An `End*()` after a `Begin*()` that returned false asserts. Code that also builds against 0.9.x, which wanted the `End*()` calls unconditionally, can tell the versions apart with `IMGUI_NODE_EDITOR_VERSION_NUM >= 1000`.

## IDs

- `NodeId`, `PinId` and `LinkId` are separate kinds: node 1, pin 1 and link 1 are three different objects.
- 0 isn't a valid ID of any kind. `BeginNode(0)`, `BeginPin(0)` and `Link(0, ...)` assert and create nothing, and the calls nested in such a node or pin are ignored.
- A pin ID names one pin. Submitting it twice in a frame asserts.
- IDs are pointer-sized, so pointers to the application's objects work as IDs: `ed::NodeId(node)`, and back with `nodeId.AsPointer<Node>()`.
- Each node pushes an ImGui ID scope of its own, so widgets in different nodes may share labels. Inside a node, ImGui's usual rules apply.
- **Reuse:** a node, pin or link deleted through the editor is freed by the next `ed::Begin()`. An ID submitted after that names a new object. A node the application stops submitting stays known, with its saved position and selection, until `ForgetNode()` forgets it, its pins, the links to them and its saved state, or `ClearState()` forgets everything, as for a new document.

## Creating links and nodes

The user drags from a pin. The application answers on every frame of the drag, and creates the item once the user releases the mouse:

```cpp
if (ed::BeginCreate())
{
    ed::PinId startPinId, endPinId;
    if (ed::QueryNewLink(&startPinId, &endPinId))
    {
        // startPinId is the pin the drag started from, endPinId the one under
        // the mouse; either can be an input or an output.
        if (!CanConnect(startPinId, endPinId))
            ed::RejectNewItem(ImVec4(1, 0, 0, 1));
        else if (ed::AcceptNewItem())
            AddLink(startPinId, endPinId); // order them as output -> input here
    }

    ed::PinId pinId;
    if (ed::QueryNewNode(&pinId))
    {
        if (ed::AcceptNewItem())
            OpenCreateNodePopup(pinId);
    }

    ed::EndCreate();
}
```

- While the user drags, `AcceptNewItem()` returns false and shows the item as accepted; `RejectNewItem()` shows it as rejected. `AcceptNewItem()` returns true once, in the frame after the release over an accepted item.
- A drag released over the pin it started from offers nothing.
- `QueryNewNode()` reports a drag over empty space, with the pin it started from. `AcceptNewItem()` returns true when the user releases it there.

## Deleting

The user deletes the selection with the Delete key, or a link with Alt+click. `ed::DeleteNode()` and `ed::DeleteLink()` start the same interaction from code. Every deletion goes through the application:

```cpp
if (ed::BeginDelete())
{
    ed::NodeId nodeId;
    while (ed::QueryDeletedNode(&nodeId))
    {
        if (ed::AcceptDeletedItem())
            RemoveNode(nodeId);
    }

    ed::LinkId linkId;
    ed::PinId startPinId, endPinId;
    while (ed::QueryDeletedLink(&linkId, &startPinId, &endPinId))
    {
        if (ed::AcceptDeletedItem())
            RemoveLink(linkId);
    }

    ed::EndDelete();
}
```

- `RejectDeletedItem()` keeps an item.
- The links attached to a node accepted with `AcceptDeletedItem(true)`, the default, are deleted with it. `QueryDeletedLink()` reports each of them, with the pins it connected:
  - in the same interaction, when links are queried after nodes, as above;
  - otherwise, in one more delete interaction in the next frame.

  They are already deleted. `AcceptDeletedItem()` returns true, so the application removes them from its model, and `RejectDeletedItem()` asserts. The application's links therefore match the editor's whatever order its loop queries in.

## Shortcuts

| Keys | Effect |
| --- | --- |
| Delete | Deletes the selection, through `BeginDelete()` |
| Alt+click on a link | Deletes the link, through `BeginDelete()` |
| Ctrl+X, Ctrl+C, Ctrl+V, Ctrl+D | `AcceptCut()`, `AcceptCopy()`, `AcceptPaste()` and `AcceptDuplicate()`, through `BeginShortcut()` |
| Space | `AcceptCreateNode()`, through `BeginShortcut()` |
| F | Navigates to the selection, the object under the mouse, or the content |
| Shift+F | The same, zoomed to leave a margin |

- The editor doesn't cut, copy or paste anything itself. `GetActionContextNodes()` and `GetActionContextLinks()` give the objects a shortcut applies to.
- The keys go through ImGui's shortcut routing. They don't fire while another ImGui item is active, for example a text field inside a node, so code that turned shortcuts off while typing (`ed::EnableShortcuts(!io.WantTextInput)`) isn't needed any more.
- `ed::EnableShortcuts(false)` turns off all of the above, Alt+click included.

## Mouse and keyboard

- `Config::DragButtonIndex`, `SelectButtonIndex`, `NavigateButtonIndex` and `ContextMenuButtonIndex` assign the mouse buttons. Only the select button changes the selection: the context menu button leaves it alone, and `ShowNodeContextMenu()` and its siblings report the object it was clicked on.
- Ctrl+click adds an object to the selection or removes it from it.
- On macOS, Cmd takes Ctrl's place in the keyboard shortcuts and in Ctrl+click, as `io.ConfigMacOSXBehaviors` decides.

## Persistence

The editor keeps node positions and group sizes, node z positions, the selection, and the view. `Config::SettingsContent` picks which of them (`Nodes`, `Selection`, `View`); leave `Nodes` out when the application keeps node positions in its own documents. The editor writes no files. Settings go to `imgui.ini`, or to the application's own storage. Node layout can also live in the application's documents, as typed state.

### In `imgui.ini`

```cpp
ImGui::CreateContext();
ed::InstallSettingsHandler(); // once per ImGui context, before ImGui reads .ini data

ed::Config config;
config.SettingsName = "Blueprint"; // section [NodeEditor][Blueprint]; the default is "NodeEditor"
ed::EditorContext* editor = ed::CreateEditor(&config);
```

- Install the handler before ImGui reads `.ini` data: before the first `ImGui::NewFrame()` when `io.IniFilename` is set, otherwise before the application's `ImGui::LoadIniSettingsFromMemory()`. Installing it later, or twice, asserts.
- Each editor has a section named by `Config::SettingsName`, which `CreateEditor()` copies. Two live editors can't share a name.
- The handler keeps every section of the run, so an editor created later, or created again, starts from the newest state of its name, and sections of editors that don't exist are written back unchanged.
- Changes mark ImGui's settings dirty, and ImGui saves them with its windows after `io.IniSavingRate`. Only settled state is saved, never a drag in progress.
- ImGui rewrites `imgui.ini` in place. For an atomic save, use ImGui's manual mode: set `io.IniFilename = nullptr`, load the data with `ImGui::LoadIniSettingsFromMemory()`, and when `io.WantSaveIniSettings` is set, write `ImGui::SaveIniSettingsToMemory()` to a temporary file, rename it over the old one, and clear the flag.

### In the application's own storage

```cpp
ed::Config config;
config.SettingsName = nullptr; // stays out of imgui.ini
ed::EditorContext* editor = ed::CreateEditor(&config);

ed::SetCurrentEditor(editor);
ed::LoadSettingsFromMemory(document.EditorState.c_str()); // before the first Begin()

// Each frame, after ed::End():
if (ed::WantSaveSettings())
{
    document.EditorState = ed::SaveSettingsToMemory();
    ed::ClearWantSaveSettings();
}
```

- `WantSaveSettings()` turns true after a change, once `io.IniSavingRate` has passed. `GetSettingsChangeReason()` tells what changed.
- `LoadSettingsFromMemory()` before the first `Begin()` takes precedence over `imgui.ini`. Later, it replaces the state the editor holds. It also reads the files 0.9's `Config::SettingsFile` wrote.

### In the application's documents

Applications that keep node layout in their own documents read and write it as typed state:

```cpp
// Keeping it
std::vector<ed::NodeState> nodes(ed::GetNodeStates(nullptr, 0));
nodes.resize(ed::GetNodeStates(nodes.data(), static_cast<int>(nodes.size())));
ed::ViewState view = ed::GetViewState();

// Restoring it
ed::SetNodeStates(nodes.data(), static_cast<int>(nodes.size()));
ed::SetViewState(view);
ed::SetSelection(selectedNodes, selectedNodeCount, nullptr, 0);
```

- `GetNodeStates()` returns every node the editor knows, by ID: nodes submitted in the last frame, nodes no longer submitted, and nodes only loaded from settings. The selection is read with `GetSelectedNodes()` and `GetSelectedLinks()`.
- The Set functions return whether anything changed, and mark the settings changed. They refuse values that aren't finite, keep nodes inside the canvas border and the zoom within the zoom levels, and report no edits.
- `SetNodeState()` also takes the state of a node the editor doesn't know: the node starts with it when the application submits it. That restores a document before the editor's first frame, and a deleted node brought back by undo. A node being deleted is refused until the next frame frees it.
- `SetNodeState()` on a node the user is dragging or resizing ends that gesture, and puts the other nodes it moved back.
- `SetSelection()` replaces the selection. It skips objects the editor doesn't know and objects being deleted, and selects links only when no nodes are given. Before the editor's first frame, it also takes nodes not created yet.
- With `SettingsContent` left without `Nodes`, node layout stays out of the editor's own settings, so the two don't compete.

Documents that keep a node's layout as text use `FormatNodeState()` and `ParseNodeState()`:

```cpp
// Keeping it
node.Layout = ed::FormatNodeState(state);

// Restoring it
ed::NodeState state;
state.Id = node.Id;
if (ed::ParseNodeState(node.Layout.c_str(), node.Layout.size(), &state))
    ed::SetNodeState(state);
```

- The text is `{"group_size":{"x":300,"y":200},"location":{"x":100,"y":400},"z":2}`, with `group_size` only for a group and `z` only when it isn't 0. That is what 0.9's `Config::SaveNodeSettings` gave, so text it wrote loads too.
- The text holds no ID: `ParseNodeState()` leaves `state.Id` as it is, and changes nothing when the text doesn't parse.

## Edits

The editor reports each edit the user completes, with the state before and after it, for an application's undo history:

```cpp
// Inside Begin()/End(), or after End()
if (ed::BeginEdits())
{
    ed::Edit edit;
    while (ed::QueryEdit(&edit))
    {
        if (edit.Type == ed::EditType::Move || edit.Type == ed::EditType::Resize)
            history.PushNodes(edit.NodesBefore, edit.NodesAfter, edit.NodeCount);
        else if (edit.Type == ed::EditType::Select)
            history.PushSelection(edit.SelectionBefore, edit.SelectionAfter);
    }
    ed::EndEdits();
}

// Undoing a step the history copied
ed::SetNodeStates(step.NodesBefore.data(), static_cast<int>(step.NodesBefore.size()));
```

- **What:**
  - `Move` when a drag ends, with every node it moved, in the same order before and after;
  - `Resize` when the resize of a group ends;
  - `Select` for each change of the selection, with node and link IDs: a click, Ctrl+click, a click on the background, a selection rectangle once it's released;
  - `Navigate`, only with `Config::ReportNavigationEdits`, once the view settles after a pan, a zoom or F, with the visible canvas rectangle.
- **Only the user's:** changes the application makes aren't reported: the Set functions, `SetNodePosition()`, `SelectNode()`, `NavigateToContent()` and the like. Applying an undo step, also from inside the loop above, records no new one. A Set function on a node the user is dragging ends the drag without an edit; a drag that has already ended is reported first.
- New and deleted objects go through the [create](#creating-links-and-nodes) and [delete](#deleting) protocols. Deleting a selected object deselects it without a `Select` edit.
- **When:** the edits completed in a frame are reported from its `End()` until the next `End()`, each once: `EndEdits()` consumes them, and the next `End()` drops those nobody read. Their data stays valid until `EndEdits()`.

## Coordinates

- `ScreenToCanvas()` and `CanvasToScreen()` convert between ImGui's screen coordinates and the canvas, whose units are screen pixels at zoom 1.
- The canvas ends 250000 units from the origin in each direction, inside the -256000 below which ImGui treats mouse positions as invalid. Nodes stay inside that border: `SetNodePosition()`, dragging, resizing, growing content and loaded settings all keep them in. The view can still pan past it.

### Drawing inside the canvas

The editor draws through a canvas, which transforms ImGui's coordinates and draw lists. That has limits:

- Windows begun inside the editor, such as popups, tooltips, combo boxes and child windows, are placed at canvas coordinates as if they were screen ones, and aren't transformed. Begin them between `ed::Suspend()` and `ed::Resume()`, which work inside nodes too:

  ```cpp
  ed::Suspend();
  if (ed::ShowNodeContextMenu(&contextNodeId))
      ImGui::OpenPopup("Node Context Menu");
  if (ImGui::BeginPopup("Node Context Menu"))
  {
      // ...
      ImGui::EndPopup();
  }
  ed::Resume();
  ```

- Text is rasterized for the zoom, so it stays sharp, with Dear ImGui 1.92 or later and a renderer backend that sets `ImGuiBackendFlags_RendererHasTextures`. The zoom is taken in quarter octave steps from 0.5 up, and glyphs are rasterized at most 8 times their size. Otherwise glyphs are rasterized at the font's size and scaled with the view, so text looks blurry when zoomed in. Text after a window that began and ended inside the editor without `ed::Suspend()`, or drawn with `ImDrawList::AddText()` and a font other than the current one, is rasterized at the density ImGui last set for it.
- Before Dear ImGui 1.93, circles and rounded corners get the same number of segments at any zoom.
