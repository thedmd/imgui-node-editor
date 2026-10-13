//------------------------------------------------------------------------------
// LICENSE
//   This software is dual-licensed to the public domain and under the following
//   license: you are granted a perpetual, irrevocable license to copy, modify,
//   publish, and distribute this file as you see fit.
//
// CREDITS
//   Written by Michal Cichon
//------------------------------------------------------------------------------
# include "imgui_node_editor_internal.h"
# include <algorithm>


//------------------------------------------------------------------------------
static ax::NodeEditor::Detail::EditorContext* s_Editor = nullptr;

static ax::NodeEditor::Detail::EditorContext* CurrentEditor()
{
    IM_ASSERT(s_Editor && "No current editor. Did you call SetCurrentEditor()?");
    return s_Editor;
}


//------------------------------------------------------------------------------
template <typename C, typename I, typename F>
static int BuildIdList(C& container, I* list, int listSize, F&& accept)
{
    if (list != nullptr)
    {
        int count = 0;
        for (auto object : container)
        {
            if (listSize <= 0)
                break;

            if (accept(object))
            {
                list[count] = I(object->ID().AsPointer());
                ++count;
                --listSize;}
        }

        return count;
    }
    else
        return static_cast<int>(std::count_if(container.begin(), container.end(), accept));
}


//------------------------------------------------------------------------------
ax::NodeEditor::EditorContext* ax::NodeEditor::CreateEditor(const Config* config)
{
    return reinterpret_cast<ax::NodeEditor::EditorContext*>(new ax::NodeEditor::Detail::EditorContext(config));
}

void ax::NodeEditor::DestroyEditor(EditorContext* ctx)
{
    auto lastContext = GetCurrentEditor();

    // Set context we're about to destroy as current, to give callback valid context
    if (lastContext != ctx)
        SetCurrentEditor(ctx);

    auto editor = reinterpret_cast<ax::NodeEditor::Detail::EditorContext*>(ctx);

    delete editor;

    if (lastContext != ctx)
        SetCurrentEditor(lastContext);
    else
        SetCurrentEditor(nullptr);
}

const ax::NodeEditor::Config& ax::NodeEditor::GetConfig(EditorContext* ctx)
{
    if (ctx == nullptr)
        ctx = GetCurrentEditor();

    if (ctx)
    {
        auto editor = reinterpret_cast<ax::NodeEditor::Detail::EditorContext*>(ctx);

        return editor->GetConfig();
    }
    else
    {
        static Config s_EmptyConfig;
        return s_EmptyConfig;
    }
}

void ax::NodeEditor::SetCurrentEditor(EditorContext* ctx)
{
    s_Editor = reinterpret_cast<ax::NodeEditor::Detail::EditorContext*>(ctx);
}

ax::NodeEditor::EditorContext* ax::NodeEditor::GetCurrentEditor()
{
    return reinterpret_cast<ax::NodeEditor::EditorContext*>(s_Editor);
}

ax::NodeEditor::Style& ax::NodeEditor::GetStyle()
{
    return CurrentEditor()->GetStyle();
}

const char* ax::NodeEditor::GetStyleColorName(StyleColor colorIndex)
{
    return CurrentEditor()->GetStyle().GetColorName(colorIndex);
}

void ax::NodeEditor::PushStyleColor(StyleColor colorIndex, const ImVec4& color)
{
    CurrentEditor()->GetStyle().PushColor(colorIndex, color);
}

void ax::NodeEditor::PopStyleColor(int count)
{
    CurrentEditor()->GetStyle().PopColor(count);
}

void ax::NodeEditor::PushStyleVar(StyleVar varIndex, float value)
{
    CurrentEditor()->GetStyle().PushVar(varIndex, value);
}

void ax::NodeEditor::PushStyleVar(StyleVar varIndex, const ImVec2& value)
{
    CurrentEditor()->GetStyle().PushVar(varIndex, value);
}

void ax::NodeEditor::PushStyleVar(StyleVar varIndex, const ImVec4& value)
{
    CurrentEditor()->GetStyle().PushVar(varIndex, value);
}

void ax::NodeEditor::PopStyleVar(int count)
{
    CurrentEditor()->GetStyle().PopVar(count);
}

void ax::NodeEditor::Begin(const char* id, const ImVec2& size)
{
    CurrentEditor()->Begin(id, size);
}

void ax::NodeEditor::End()
{
    CurrentEditor()->End();
}

void ax::NodeEditor::BeginNode(NodeId id)
{
    CurrentEditor()->GetNodeBuilder().Begin(id);
}

void ax::NodeEditor::BeginPin(PinId id, PinKind kind)
{
    CurrentEditor()->GetNodeBuilder().BeginPin(id, kind);
}

void ax::NodeEditor::PinRect(const ImVec2& a, const ImVec2& b)
{
    CurrentEditor()->GetNodeBuilder().PinRect(a, b);
}

void ax::NodeEditor::PinPivotRect(const ImVec2& a, const ImVec2& b)
{
    CurrentEditor()->GetNodeBuilder().PinPivotRect(a, b);
}

void ax::NodeEditor::PinPivotSize(const ImVec2& size)
{
    CurrentEditor()->GetNodeBuilder().PinPivotSize(size);
}

void ax::NodeEditor::PinPivotScale(const ImVec2& scale)
{
    CurrentEditor()->GetNodeBuilder().PinPivotScale(scale);
}

void ax::NodeEditor::PinPivotAlignment(const ImVec2& alignment)
{
    CurrentEditor()->GetNodeBuilder().PinPivotAlignment(alignment);
}

void ax::NodeEditor::EndPin()
{
    CurrentEditor()->GetNodeBuilder().EndPin();
}

void ax::NodeEditor::Group(const ImVec2& size)
{
    CurrentEditor()->GetNodeBuilder().Group(size);
}

void ax::NodeEditor::EndNode()
{
    CurrentEditor()->GetNodeBuilder().End();
}

bool ax::NodeEditor::BeginGroupHint(NodeId nodeId)
{
    return CurrentEditor()->GetHintBuilder().Begin(nodeId);
}

ImVec2 ax::NodeEditor::GetGroupMin()
{
    return CurrentEditor()->GetHintBuilder().GetGroupMin();
}

ImVec2 ax::NodeEditor::GetGroupMax()
{
    return CurrentEditor()->GetHintBuilder().GetGroupMax();
}

ImDrawList* ax::NodeEditor::GetHintForegroundDrawList()
{
    return CurrentEditor()->GetHintBuilder().GetForegroundDrawList();
}

ImDrawList* ax::NodeEditor::GetHintBackgroundDrawList()
{
    return CurrentEditor()->GetHintBuilder().GetBackgroundDrawList();
}

void ax::NodeEditor::EndGroupHint()
{
    CurrentEditor()->GetHintBuilder().End();
}

ImDrawList* ax::NodeEditor::GetNodeBackgroundDrawList(NodeId nodeId)
{
    if (auto node = CurrentEditor()->FindNode(nodeId))
        return CurrentEditor()->GetNodeBuilder().GetUserBackgroundDrawList(node);
    else
        return nullptr;
}

bool ax::NodeEditor::Link(LinkId id, PinId startPinId, PinId endPinId, const ImVec4& color/* = ImVec4(1, 1, 1, 1)*/, float thickness/* = 1.0f*/)
{
    return CurrentEditor()->DoLink(id, startPinId, endPinId, ImColor(color), thickness);
}

void ax::NodeEditor::Flow(LinkId linkId, FlowDirection direction)
{
    if (auto link = CurrentEditor()->FindLink(linkId))
        CurrentEditor()->Flow(link, direction);
}

bool ax::NodeEditor::BeginCreate(const ImVec4& color, float thickness)
{
    auto& context = CurrentEditor()->GetItemCreator();

    if (context.Begin())
    {
        context.SetStyle(ImColor(color), thickness);
        return true;
    }
    else
        return false;
}

bool ax::NodeEditor::QueryNewLink(PinId* startId, PinId* endId)
{
    using Result = ax::NodeEditor::Detail::CreateItemAction::Result;

    auto& context = CurrentEditor()->GetItemCreator();

    return context.QueryLink(startId, endId) == Result::True;
}

bool ax::NodeEditor::QueryNewLink(PinId* startId, PinId* endId, const ImVec4& color, float thickness)
{
    using Result = ax::NodeEditor::Detail::CreateItemAction::Result;

    auto& context = CurrentEditor()->GetItemCreator();

    auto result = context.QueryLink(startId, endId);
    if (result != Result::Indeterminate)
        context.SetStyle(ImColor(color), thickness);

    return result == Result::True;
}

bool ax::NodeEditor::QueryNewNode(PinId* pinId)
{
    using Result = ax::NodeEditor::Detail::CreateItemAction::Result;

    auto& context = CurrentEditor()->GetItemCreator();

    return context.QueryNode(pinId) == Result::True;
}

bool ax::NodeEditor::QueryNewNode(PinId* pinId, const ImVec4& color, float thickness)
{
    using Result = ax::NodeEditor::Detail::CreateItemAction::Result;

    auto& context = CurrentEditor()->GetItemCreator();

    auto result = context.QueryNode(pinId);
    if (result != Result::Indeterminate)
        context.SetStyle(ImColor(color), thickness);

    return result == Result::True;
}

bool ax::NodeEditor::AcceptNewItem()
{
    using Result = ax::NodeEditor::Detail::CreateItemAction::Result;

    auto& context = CurrentEditor()->GetItemCreator();

    return context.AcceptItem() == Result::True;
}

bool ax::NodeEditor::AcceptNewItem(const ImVec4& color, float thickness)
{
    using Result = ax::NodeEditor::Detail::CreateItemAction::Result;

    auto& context = CurrentEditor()->GetItemCreator();

    auto result = context.AcceptItem();
    if (result != Result::Indeterminate)
        context.SetStyle(ImColor(color), thickness);

    return result == Result::True;
}

void ax::NodeEditor::RejectNewItem()
{
    auto& context = CurrentEditor()->GetItemCreator();

    context.RejectItem();
}

void ax::NodeEditor::RejectNewItem(const ImVec4& color, float thickness)
{
    using Result = ax::NodeEditor::Detail::CreateItemAction::Result;

    auto& context = CurrentEditor()->GetItemCreator();

    if (context.RejectItem() != Result::Indeterminate)
        context.SetStyle(ImColor(color), thickness);
}

void ax::NodeEditor::EndCreate()
{
    auto& context = CurrentEditor()->GetItemCreator();

    context.End();
}

bool ax::NodeEditor::BeginDelete()
{
    auto& context = CurrentEditor()->GetItemDeleter();

    return context.Begin();
}

bool ax::NodeEditor::QueryDeletedLink(LinkId* linkId, PinId* startId, PinId* endId)
{
    auto& context = CurrentEditor()->GetItemDeleter();

    return context.QueryLink(linkId, startId, endId);
}

bool ax::NodeEditor::QueryDeletedNode(NodeId* nodeId)
{
    auto& context = CurrentEditor()->GetItemDeleter();

    return context.QueryNode(nodeId);
}

bool ax::NodeEditor::AcceptDeletedItem(bool deleteDependencies)
{
    auto& context = CurrentEditor()->GetItemDeleter();

    return context.AcceptItem(deleteDependencies);
}

void ax::NodeEditor::RejectDeletedItem()
{
    auto& context = CurrentEditor()->GetItemDeleter();

    context.RejectItem();
}

void ax::NodeEditor::EndDelete()
{
    auto& context = CurrentEditor()->GetItemDeleter();

    context.End();
}

void ax::NodeEditor::SetNodePosition(NodeId nodeId, const ImVec2& position)
{
    CurrentEditor()->SetNodePosition(nodeId, position);
}

void ax::NodeEditor::SetGroupSize(NodeId nodeId, const ImVec2& size)
{
    CurrentEditor()->SetGroupSize(nodeId, size);
}

ImVec2 ax::NodeEditor::GetNodePosition(NodeId nodeId)
{
    return CurrentEditor()->GetNodePosition(nodeId);
}

ImVec2 ax::NodeEditor::GetNodeSize(NodeId nodeId)
{
    return CurrentEditor()->GetNodeSize(nodeId);
}

void ax::NodeEditor::CenterNodeOnScreen(NodeId nodeId)
{
    if (auto node = CurrentEditor()->FindNode(nodeId))
        node->CenterOnScreenInNextFrame();
}

void ax::NodeEditor::SetNodeZPosition(NodeId nodeId, float z)
{
    CurrentEditor()->SetNodeZPosition(nodeId, z);
}

float ax::NodeEditor::GetNodeZPosition(NodeId nodeId)
{
    return CurrentEditor()->GetNodeZPosition(nodeId);
}

void ax::NodeEditor::RestoreNodeState(NodeId nodeId)
{
    if (auto node = CurrentEditor()->FindNode(nodeId))
        CurrentEditor()->MarkNodeToRestoreState(node);
}

void ax::NodeEditor::Suspend()
{
    CurrentEditor()->Suspend();
}

void ax::NodeEditor::Resume()
{
    CurrentEditor()->Resume();
}

bool ax::NodeEditor::IsSuspended()
{
    return CurrentEditor()->IsSuspended();
}

bool ax::NodeEditor::IsActive()
{
    return CurrentEditor()->IsFocused();
}

bool ax::NodeEditor::HasSelectionChanged()
{
    return CurrentEditor()->HasSelectionChanged();
}

int ax::NodeEditor::GetSelectedObjectCount()
{
    return (int)CurrentEditor()->GetSelectedObjects().size();
}

int ax::NodeEditor::GetSelectedNodes(NodeId* nodes, int size)
{
    return BuildIdList(CurrentEditor()->GetSelectedObjects(), nodes, size, [](auto object)
    {
        return object->AsNode() != nullptr;
    });
}

int ax::NodeEditor::GetSelectedLinks(LinkId* links, int size)
{
    return BuildIdList(CurrentEditor()->GetSelectedObjects(), links, size, [](auto object)
    {
        return object->AsLink() != nullptr;
    });
}

bool ax::NodeEditor::IsNodeSelected(NodeId nodeId)
{
    if (auto node = CurrentEditor()->FindNode(nodeId))
        return CurrentEditor()->IsSelected(node);
    else
        return false;
}

bool ax::NodeEditor::IsLinkSelected(LinkId linkId)
{
    if (auto link = CurrentEditor()->FindLink(linkId))
        return CurrentEditor()->IsSelected(link);
    else
        return false;
}

void ax::NodeEditor::ClearSelection()
{
    CurrentEditor()->ClearSelection();
}

void ax::NodeEditor::SelectNode(NodeId nodeId, bool append)
{
    if (auto node = CurrentEditor()->FindNode(nodeId))
    {
        if (append)
            CurrentEditor()->SelectObject(node);
        else
            CurrentEditor()->SetSelectedObject(node);
    }
}

void ax::NodeEditor::SelectLink(LinkId linkId, bool append)
{
    if (auto link = CurrentEditor()->FindLink(linkId))
    {
        if (append)
            CurrentEditor()->SelectObject(link);
        else
            CurrentEditor()->SetSelectedObject(link);
    }
}

void ax::NodeEditor::DeselectNode(NodeId nodeId)
{
    if (auto node = CurrentEditor()->FindNode(nodeId))
        CurrentEditor()->DeselectObject(node);
}

void ax::NodeEditor::DeselectLink(LinkId linkId)
{
    if (auto link = CurrentEditor()->FindLink(linkId))
        CurrentEditor()->DeselectObject(link);
}

bool ax::NodeEditor::DeleteNode(NodeId nodeId)
{
    if (auto node = CurrentEditor()->FindNode(nodeId))
        return CurrentEditor()->GetItemDeleter().Add(node);
    else
        return false;
}

bool ax::NodeEditor::DeleteLink(LinkId linkId)
{
    if (auto link = CurrentEditor()->FindLink(linkId))
        return CurrentEditor()->GetItemDeleter().Add(link);
    else
        return false;
}

bool ax::NodeEditor::HasAnyLinks(NodeId nodeId)
{
    return CurrentEditor()->HasAnyLinks(nodeId);
}

bool ax::NodeEditor::HasAnyLinks(PinId pinId)
{
    return CurrentEditor()->HasAnyLinks(pinId);
}

int ax::NodeEditor::BreakLinks(NodeId nodeId)
{
    return CurrentEditor()->BreakLinks(nodeId);
}

int ax::NodeEditor::BreakLinks(PinId pinId)
{
    return CurrentEditor()->BreakLinks(pinId);
}

void ax::NodeEditor::NavigateToContent(float duration)
{
    CurrentEditor()->NavigateTo(CurrentEditor()->GetContentBounds(), true, duration);
}

void ax::NodeEditor::NavigateToSelection(bool zoomIn, float duration)
{
    CurrentEditor()->NavigateTo(CurrentEditor()->GetSelectionBounds(), zoomIn, duration);
}

bool ax::NodeEditor::ShowNodeContextMenu(NodeId* nodeId)
{
    return CurrentEditor()->GetContextMenu().ShowNodeContextMenu(nodeId);
}

bool ax::NodeEditor::ShowPinContextMenu(PinId* pinId)
{
    return CurrentEditor()->GetContextMenu().ShowPinContextMenu(pinId);
}

bool ax::NodeEditor::ShowLinkContextMenu(LinkId* linkId)
{
    return CurrentEditor()->GetContextMenu().ShowLinkContextMenu(linkId);
}

bool ax::NodeEditor::ShowBackgroundContextMenu()
{
    return CurrentEditor()->GetContextMenu().ShowBackgroundContextMenu();
}

void ax::NodeEditor::EnableShortcuts(bool enable)
{
    CurrentEditor()->EnableShortcuts(enable);
}

bool ax::NodeEditor::AreShortcutsEnabled()
{
    return CurrentEditor()->AreShortcutsEnabled();
}

bool ax::NodeEditor::BeginShortcut()
{
    return CurrentEditor()->GetShortcut().Begin();
}

bool ax::NodeEditor::AcceptCut()
{
    return CurrentEditor()->GetShortcut().AcceptCut();
}

bool ax::NodeEditor::AcceptCopy()
{
    return CurrentEditor()->GetShortcut().AcceptCopy();
}

bool ax::NodeEditor::AcceptPaste()
{
    return CurrentEditor()->GetShortcut().AcceptPaste();
}

bool ax::NodeEditor::AcceptDuplicate()
{
    return CurrentEditor()->GetShortcut().AcceptDuplicate();
}

bool ax::NodeEditor::AcceptCreateNode()
{
    return CurrentEditor()->GetShortcut().AcceptCreateNode();
}

int ax::NodeEditor::GetActionContextSize()
{
    return static_cast<int>(CurrentEditor()->GetShortcut().m_Context.size());
}

int ax::NodeEditor::GetActionContextNodes(NodeId* nodes, int size)
{
    return BuildIdList(CurrentEditor()->GetShortcut().m_Context, nodes, size, [](auto object)
    {
        return object->AsNode() != nullptr;
    });
}

int ax::NodeEditor::GetActionContextLinks(LinkId* links, int size)
{
    return BuildIdList(CurrentEditor()->GetShortcut().m_Context, links, size, [](auto object)
    {
        return object->AsLink() != nullptr;
    });
}

void ax::NodeEditor::EndShortcut()
{
    return CurrentEditor()->GetShortcut().End();
}

float ax::NodeEditor::GetCurrentZoom()
{
    return CurrentEditor()->GetView().InvScale();
}

ax::NodeEditor::NodeId ax::NodeEditor::GetHoveredNode()
{
    return CurrentEditor()->GetHoveredNode();
}

ax::NodeEditor::PinId ax::NodeEditor::GetHoveredPin()
{
    return CurrentEditor()->GetHoveredPin();
}

ax::NodeEditor::LinkId ax::NodeEditor::GetHoveredLink()
{
    return CurrentEditor()->GetHoveredLink();
}

ax::NodeEditor::NodeId ax::NodeEditor::GetDoubleClickedNode()
{
    return CurrentEditor()->GetDoubleClickedNode();
}

ax::NodeEditor::PinId ax::NodeEditor::GetDoubleClickedPin()
{
    return CurrentEditor()->GetDoubleClickedPin();
}

ax::NodeEditor::LinkId ax::NodeEditor::GetDoubleClickedLink()
{
    return CurrentEditor()->GetDoubleClickedLink();
}

bool ax::NodeEditor::IsBackgroundClicked()
{
    return CurrentEditor()->IsBackgroundClicked();
}

bool ax::NodeEditor::IsBackgroundDoubleClicked()
{
    return CurrentEditor()->IsBackgroundDoubleClicked();
}

ImGuiMouseButton ax::NodeEditor::GetBackgroundClickButtonIndex()
{
    return CurrentEditor()->GetBackgroundClickButtonIndex();
}

ImGuiMouseButton ax::NodeEditor::GetBackgroundDoubleClickButtonIndex()
{
    return CurrentEditor()->GetBackgroundDoubleClickButtonIndex();
}

bool ax::NodeEditor::GetLinkPins(LinkId linkId, PinId* startPinId, PinId* endPinId)
{
    auto link = CurrentEditor()->FindLink(linkId);
    if (!link || !link->m_StartPin || !link->m_EndPin)
        return false;

    if (startPinId)
        *startPinId = link->m_StartPin->m_ID;
    if (endPinId)
        *endPinId = link->m_EndPin->m_ID;

    return true;
}

bool ax::NodeEditor::PinHadAnyLinks(PinId pinId)
{
    return CurrentEditor()->PinHadAnyLinks(pinId);
}

ImVec2 ax::NodeEditor::GetScreenSize()
{
    return CurrentEditor()->GetRect().GetSize();
}

ImVec2 ax::NodeEditor::ScreenToCanvas(const ImVec2& pos)
{
    return CurrentEditor()->ToCanvas(pos);
}

ImVec2 ax::NodeEditor::CanvasToScreen(const ImVec2& pos)
{
    return CurrentEditor()->ToScreen(pos);
}

int ax::NodeEditor::GetNodeCount()
{
    return CurrentEditor()->CountLiveNodes();
}

int ax::NodeEditor::GetOrderedNodeIds(NodeId* nodes, int size)
{
    return CurrentEditor()->GetNodeIds(nodes, size);
}
