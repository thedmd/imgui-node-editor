//------------------------------------------------------------------------------
// VERSION 0.9.1
//
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
#ifndef GImGuiNodeEditor
ax::NodeEditor::Detail::EditorContext* GImGuiNodeEditor = nullptr;
#endif


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
    GImGuiNodeEditor = reinterpret_cast<ax::NodeEditor::Detail::EditorContext*>(ctx);
}

ax::NodeEditor::EditorContext* ax::NodeEditor::GetCurrentEditor()
{
    return reinterpret_cast<ax::NodeEditor::EditorContext*>(GImGuiNodeEditor);
}

ax::NodeEditor::Style& ax::NodeEditor::GetStyle()
{
    return GImGuiNodeEditor->GetStyle();
}

const char* ax::NodeEditor::GetStyleColorName(StyleColor colorIndex)
{
    return GImGuiNodeEditor->GetStyle().GetColorName(colorIndex);
}

void ax::NodeEditor::PushStyleColor(StyleColor colorIndex, const ImVec4& color)
{
    GImGuiNodeEditor->GetStyle().PushColor(colorIndex, color);
}

void ax::NodeEditor::PopStyleColor(int count)
{
    GImGuiNodeEditor->GetStyle().PopColor(count);
}

void ax::NodeEditor::PushStyleVar(StyleVar varIndex, float value)
{
    GImGuiNodeEditor->GetStyle().PushVar(varIndex, value);
}

void ax::NodeEditor::PushStyleVar(StyleVar varIndex, const ImVec2& value)
{
    GImGuiNodeEditor->GetStyle().PushVar(varIndex, value);
}

void ax::NodeEditor::PushStyleVar(StyleVar varIndex, const ImVec4& value)
{
    GImGuiNodeEditor->GetStyle().PushVar(varIndex, value);
}

void ax::NodeEditor::PopStyleVar(int count)
{
    GImGuiNodeEditor->GetStyle().PopVar(count);
}

void ax::NodeEditor::Begin(const char* id, const ImVec2& size)
{
    GImGuiNodeEditor->Begin(id, size);
}

void ax::NodeEditor::End()
{
    GImGuiNodeEditor->End();
}

void ax::NodeEditor::BeginNode(NodeId id)
{
    GImGuiNodeEditor->GetNodeBuilder().Begin(id);
}

void ax::NodeEditor::BeginPin(PinId id, PinKind kind)
{
    GImGuiNodeEditor->GetNodeBuilder().BeginPin(id, kind);
}

void ax::NodeEditor::PinRect(const ImVec2& a, const ImVec2& b)
{
    GImGuiNodeEditor->GetNodeBuilder().PinRect(a, b);
}

void ax::NodeEditor::PinPivotRect(const ImVec2& a, const ImVec2& b)
{
    GImGuiNodeEditor->GetNodeBuilder().PinPivotRect(a, b);
}

void ax::NodeEditor::PinPivotSize(const ImVec2& size)
{
    GImGuiNodeEditor->GetNodeBuilder().PinPivotSize(size);
}

void ax::NodeEditor::PinPivotScale(const ImVec2& scale)
{
    GImGuiNodeEditor->GetNodeBuilder().PinPivotScale(scale);
}

void ax::NodeEditor::PinPivotAlignment(const ImVec2& alignment)
{
    GImGuiNodeEditor->GetNodeBuilder().PinPivotAlignment(alignment);
}

void ax::NodeEditor::EndPin()
{
    GImGuiNodeEditor->GetNodeBuilder().EndPin();
}

void ax::NodeEditor::Group(const ImVec2& size)
{
    GImGuiNodeEditor->GetNodeBuilder().Group(size);
}

void ax::NodeEditor::EndNode()
{
    GImGuiNodeEditor->GetNodeBuilder().End();
}

bool ax::NodeEditor::BeginGroupHint(NodeId nodeId)
{
    return GImGuiNodeEditor->GetHintBuilder().Begin(nodeId);
}

ImVec2 ax::NodeEditor::GetGroupMin()
{
    return GImGuiNodeEditor->GetHintBuilder().GetGroupMin();
}

ImVec2 ax::NodeEditor::GetGroupMax()
{
    return GImGuiNodeEditor->GetHintBuilder().GetGroupMax();
}

ImDrawList* ax::NodeEditor::GetHintForegroundDrawList()
{
    return GImGuiNodeEditor->GetHintBuilder().GetForegroundDrawList();
}

ImDrawList* ax::NodeEditor::GetHintBackgroundDrawList()
{
    return GImGuiNodeEditor->GetHintBuilder().GetBackgroundDrawList();
}

void ax::NodeEditor::EndGroupHint()
{
    GImGuiNodeEditor->GetHintBuilder().End();
}

ImDrawList* ax::NodeEditor::GetNodeBackgroundDrawList(NodeId nodeId)
{
    if (auto node = GImGuiNodeEditor->FindNode(nodeId))
        return GImGuiNodeEditor->GetNodeBuilder().GetUserBackgroundDrawList(node);
    else
        return nullptr;
}

bool ax::NodeEditor::Link(LinkId id, PinId startPinId, PinId endPinId, const ImVec4& color/* = ImVec4(1, 1, 1, 1)*/, float thickness/* = 1.0f*/)
{
    return GImGuiNodeEditor->DoLink(id, startPinId, endPinId, ImColor(color), thickness);
}

void ax::NodeEditor::Flow(LinkId linkId, FlowDirection direction)
{
    if (auto link = GImGuiNodeEditor->FindLink(linkId))
        GImGuiNodeEditor->Flow(link, direction);
}

bool ax::NodeEditor::BeginCreate(const ImVec4& color, float thickness)
{
    auto& context = GImGuiNodeEditor->GetItemCreator();

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

    auto& context = GImGuiNodeEditor->GetItemCreator();

    return context.QueryLink(startId, endId) == Result::True;
}

bool ax::NodeEditor::QueryNewLink(PinId* startId, PinId* endId, const ImVec4& color, float thickness)
{
    using Result = ax::NodeEditor::Detail::CreateItemAction::Result;

    auto& context = GImGuiNodeEditor->GetItemCreator();

    auto result = context.QueryLink(startId, endId);
    if (result != Result::Indeterminate)
        context.SetStyle(ImColor(color), thickness);

    return result == Result::True;
}

bool ax::NodeEditor::QueryNewNode(PinId* pinId)
{
    using Result = ax::NodeEditor::Detail::CreateItemAction::Result;

    auto& context = GImGuiNodeEditor->GetItemCreator();

    return context.QueryNode(pinId) == Result::True;
}

bool ax::NodeEditor::QueryNewNode(PinId* pinId, const ImVec4& color, float thickness)
{
    using Result = ax::NodeEditor::Detail::CreateItemAction::Result;

    auto& context = GImGuiNodeEditor->GetItemCreator();

    auto result = context.QueryNode(pinId);
    if (result != Result::Indeterminate)
        context.SetStyle(ImColor(color), thickness);

    return result == Result::True;
}

bool ax::NodeEditor::AcceptNewItem()
{
    using Result = ax::NodeEditor::Detail::CreateItemAction::Result;

    auto& context = GImGuiNodeEditor->GetItemCreator();

    return context.AcceptItem() == Result::True;
}

bool ax::NodeEditor::AcceptNewItem(const ImVec4& color, float thickness)
{
    using Result = ax::NodeEditor::Detail::CreateItemAction::Result;

    auto& context = GImGuiNodeEditor->GetItemCreator();

    auto result = context.AcceptItem();
    if (result != Result::Indeterminate)
        context.SetStyle(ImColor(color), thickness);

    return result == Result::True;
}

void ax::NodeEditor::RejectNewItem()
{
    auto& context = GImGuiNodeEditor->GetItemCreator();

    context.RejectItem();
}

void ax::NodeEditor::RejectNewItem(const ImVec4& color, float thickness)
{
    using Result = ax::NodeEditor::Detail::CreateItemAction::Result;

    auto& context = GImGuiNodeEditor->GetItemCreator();

    if (context.RejectItem() != Result::Indeterminate)
        context.SetStyle(ImColor(color), thickness);
}

void ax::NodeEditor::EndCreate()
{
    auto& context = GImGuiNodeEditor->GetItemCreator();

    context.End();
}

bool ax::NodeEditor::BeginDelete()
{
    auto& context = GImGuiNodeEditor->GetItemDeleter();

    return context.Begin();
}

bool ax::NodeEditor::QueryDeletedLink(LinkId* linkId, PinId* startId, PinId* endId)
{
    auto& context = GImGuiNodeEditor->GetItemDeleter();

    return context.QueryLink(linkId, startId, endId);
}

bool ax::NodeEditor::QueryDeletedNode(NodeId* nodeId)
{
    auto& context = GImGuiNodeEditor->GetItemDeleter();

    return context.QueryNode(nodeId);
}

bool ax::NodeEditor::AcceptDeletedItem(bool deleteDependencies)
{
    auto& context = GImGuiNodeEditor->GetItemDeleter();

    return context.AcceptItem(deleteDependencies);
}

void ax::NodeEditor::RejectDeletedItem()
{
    auto& context = GImGuiNodeEditor->GetItemDeleter();

    context.RejectItem();
}

void ax::NodeEditor::EndDelete()
{
    auto& context = GImGuiNodeEditor->GetItemDeleter();

    context.End();
}

void ax::NodeEditor::SetNodePosition(NodeId nodeId, const ImVec2& position)
{
    GImGuiNodeEditor->SetNodePosition(nodeId, position);
}

void ax::NodeEditor::SetGroupSize(NodeId nodeId, const ImVec2& size)
{
    GImGuiNodeEditor->SetGroupSize(nodeId, size);
}

ImVec2 ax::NodeEditor::GetNodePosition(NodeId nodeId)
{
    return GImGuiNodeEditor->GetNodePosition(nodeId);
}

ImVec2 ax::NodeEditor::GetNodeSize(NodeId nodeId)
{
    return GImGuiNodeEditor->GetNodeSize(nodeId);
}

void ax::NodeEditor::CenterNodeOnScreen(NodeId nodeId)
{
    if (auto node = GImGuiNodeEditor->FindNode(nodeId))
        node->CenterOnScreenInNextFrame();
}

void ax::NodeEditor::SetNodeZPosition(NodeId nodeId, float z)
{
    GImGuiNodeEditor->SetNodeZPosition(nodeId, z);
}

float ax::NodeEditor::GetNodeZPosition(NodeId nodeId)
{
    return GImGuiNodeEditor->GetNodeZPosition(nodeId);
}

void ax::NodeEditor::RestoreNodeState(NodeId nodeId)
{
    if (auto node = GImGuiNodeEditor->FindNode(nodeId))
        GImGuiNodeEditor->MarkNodeToRestoreState(node);
}

void ax::NodeEditor::Suspend()
{
    GImGuiNodeEditor->Suspend();
}

void ax::NodeEditor::Resume()
{
    GImGuiNodeEditor->Resume();
}

bool ax::NodeEditor::IsSuspended()
{
    return GImGuiNodeEditor->IsSuspended();
}

bool ax::NodeEditor::IsActive()
{
    return GImGuiNodeEditor->IsFocused();
}

bool ax::NodeEditor::HasSelectionChanged()
{
    return GImGuiNodeEditor->HasSelectionChanged();
}

int ax::NodeEditor::GetSelectedObjectCount()
{
    return (int)GImGuiNodeEditor->GetSelectedObjects().size();
}

int ax::NodeEditor::GetSelectedNodes(NodeId* nodes, int size)
{
    return BuildIdList(GImGuiNodeEditor->GetSelectedObjects(), nodes, size, [](auto object)
    {
        return object->AsNode() != nullptr;
    });
}

int ax::NodeEditor::GetSelectedLinks(LinkId* links, int size)
{
    return BuildIdList(GImGuiNodeEditor->GetSelectedObjects(), links, size, [](auto object)
    {
        return object->AsLink() != nullptr;
    });
}

bool ax::NodeEditor::IsNodeSelected(NodeId nodeId)
{
    if (auto node = GImGuiNodeEditor->FindNode(nodeId))
        return GImGuiNodeEditor->IsSelected(node);
    else
        return false;
}

bool ax::NodeEditor::IsLinkSelected(LinkId linkId)
{
    if (auto link = GImGuiNodeEditor->FindLink(linkId))
        return GImGuiNodeEditor->IsSelected(link);
    else
        return false;
}

void ax::NodeEditor::ClearSelection()
{
    GImGuiNodeEditor->ClearSelection();
}

void ax::NodeEditor::SelectNode(NodeId nodeId, bool append)
{
    if (auto node = GImGuiNodeEditor->FindNode(nodeId))
    {
        if (append)
            GImGuiNodeEditor->SelectObject(node);
        else
            GImGuiNodeEditor->SetSelectedObject(node);
    }
}

void ax::NodeEditor::SelectLink(LinkId linkId, bool append)
{
    if (auto link = GImGuiNodeEditor->FindLink(linkId))
    {
        if (append)
            GImGuiNodeEditor->SelectObject(link);
        else
            GImGuiNodeEditor->SetSelectedObject(link);
    }
}

void ax::NodeEditor::DeselectNode(NodeId nodeId)
{
    if (auto node = GImGuiNodeEditor->FindNode(nodeId))
        GImGuiNodeEditor->DeselectObject(node);
}

void ax::NodeEditor::DeselectLink(LinkId linkId)
{
    if (auto link = GImGuiNodeEditor->FindLink(linkId))
        GImGuiNodeEditor->DeselectObject(link);
}

bool ax::NodeEditor::DeleteNode(NodeId nodeId)
{
    if (auto node = GImGuiNodeEditor->FindNode(nodeId))
        return GImGuiNodeEditor->GetItemDeleter().Add(node);
    else
        return false;
}

bool ax::NodeEditor::DeleteLink(LinkId linkId)
{
    if (auto link = GImGuiNodeEditor->FindLink(linkId))
        return GImGuiNodeEditor->GetItemDeleter().Add(link);
    else
        return false;
}

bool ax::NodeEditor::HasAnyLinks(NodeId nodeId)
{
    return GImGuiNodeEditor->HasAnyLinks(nodeId);
}

bool ax::NodeEditor::HasAnyLinks(PinId pinId)
{
    return GImGuiNodeEditor->HasAnyLinks(pinId);
}

int ax::NodeEditor::BreakLinks(NodeId nodeId)
{
    return GImGuiNodeEditor->BreakLinks(nodeId);
}

int ax::NodeEditor::BreakLinks(PinId pinId)
{
    return GImGuiNodeEditor->BreakLinks(pinId);
}

void ax::NodeEditor::NavigateToContent(float duration)
{
    GImGuiNodeEditor->NavigateTo(GImGuiNodeEditor->GetContentBounds(), true, duration);
}

void ax::NodeEditor::NavigateToSelection(bool zoomIn, float duration)
{
    GImGuiNodeEditor->NavigateTo(GImGuiNodeEditor->GetSelectionBounds(), zoomIn, duration);
}

bool ax::NodeEditor::ShowNodeContextMenu(NodeId* nodeId)
{
    return GImGuiNodeEditor->GetContextMenu().ShowNodeContextMenu(nodeId);
}

bool ax::NodeEditor::ShowPinContextMenu(PinId* pinId)
{
    return GImGuiNodeEditor->GetContextMenu().ShowPinContextMenu(pinId);
}

bool ax::NodeEditor::ShowLinkContextMenu(LinkId* linkId)
{
    return GImGuiNodeEditor->GetContextMenu().ShowLinkContextMenu(linkId);
}

bool ax::NodeEditor::ShowBackgroundContextMenu()
{
    return GImGuiNodeEditor->GetContextMenu().ShowBackgroundContextMenu();
}

void ax::NodeEditor::EnableShortcuts(bool enable)
{
    GImGuiNodeEditor->EnableShortcuts(enable);
}

bool ax::NodeEditor::AreShortcutsEnabled()
{
    return GImGuiNodeEditor->AreShortcutsEnabled();
}

bool ax::NodeEditor::BeginShortcut()
{
    return GImGuiNodeEditor->GetShortcut().Begin();
}

bool ax::NodeEditor::AcceptCut()
{
    return GImGuiNodeEditor->GetShortcut().AcceptCut();
}

bool ax::NodeEditor::AcceptCopy()
{
    return GImGuiNodeEditor->GetShortcut().AcceptCopy();
}

bool ax::NodeEditor::AcceptPaste()
{
    return GImGuiNodeEditor->GetShortcut().AcceptPaste();
}

bool ax::NodeEditor::AcceptDuplicate()
{
    return GImGuiNodeEditor->GetShortcut().AcceptDuplicate();
}

bool ax::NodeEditor::AcceptCreateNode()
{
    return GImGuiNodeEditor->GetShortcut().AcceptCreateNode();
}

int ax::NodeEditor::GetActionContextSize()
{
    return static_cast<int>(GImGuiNodeEditor->GetShortcut().m_Context.size());
}

int ax::NodeEditor::GetActionContextNodes(NodeId* nodes, int size)
{
    return BuildIdList(GImGuiNodeEditor->GetSelectedObjects(), nodes, size, [](auto object)
    {
        return object->AsNode() != nullptr;
    });
}

int ax::NodeEditor::GetActionContextLinks(LinkId* links, int size)
{
    return BuildIdList(GImGuiNodeEditor->GetSelectedObjects(), links, size, [](auto object)
    {
        return object->AsLink() != nullptr;
    });
}

void ax::NodeEditor::EndShortcut()
{
    return GImGuiNodeEditor->GetShortcut().End();
}

float ax::NodeEditor::GetCurrentZoom()
{
    return GImGuiNodeEditor->GetView().InvScale;
}

ax::NodeEditor::NodeId ax::NodeEditor::GetHoveredNode()
{
    return GImGuiNodeEditor->GetHoveredNode();
}

ax::NodeEditor::PinId ax::NodeEditor::GetHoveredPin()
{
    return GImGuiNodeEditor->GetHoveredPin();
}

ax::NodeEditor::LinkId ax::NodeEditor::GetHoveredLink()
{
    return GImGuiNodeEditor->GetHoveredLink();
}

ax::NodeEditor::NodeId ax::NodeEditor::GetDoubleClickedNode()
{
    return GImGuiNodeEditor->GetDoubleClickedNode();
}

ax::NodeEditor::PinId ax::NodeEditor::GetDoubleClickedPin()
{
    return GImGuiNodeEditor->GetDoubleClickedPin();
}

ax::NodeEditor::LinkId ax::NodeEditor::GetDoubleClickedLink()
{
    return GImGuiNodeEditor->GetDoubleClickedLink();
}

bool ax::NodeEditor::IsBackgroundClicked()
{
    return GImGuiNodeEditor->IsBackgroundClicked();
}

bool ax::NodeEditor::IsBackgroundDoubleClicked()
{
    return GImGuiNodeEditor->IsBackgroundDoubleClicked();
}

ImGuiMouseButton ax::NodeEditor::GetBackgroundClickButtonIndex()
{
    return GImGuiNodeEditor->GetBackgroundClickButtonIndex();
}

ImGuiMouseButton ax::NodeEditor::GetBackgroundDoubleClickButtonIndex()
{
    return GImGuiNodeEditor->GetBackgroundDoubleClickButtonIndex();
}

bool ax::NodeEditor::GetLinkPins(LinkId linkId, PinId* startPinId, PinId* endPinId)
{
    auto link = GImGuiNodeEditor->FindLink(linkId);
    if (!link)
        return false;

    if (startPinId)
        *startPinId = link->m_StartPin->m_ID;
    if (endPinId)
        *endPinId = link->m_EndPin->m_ID;

    return true;
}

bool ax::NodeEditor::PinHadAnyLinks(PinId pinId)
{
    return GImGuiNodeEditor->PinHadAnyLinks(pinId);
}

ImVec2 ax::NodeEditor::GetScreenSize()
{
    return GImGuiNodeEditor->GetRect().GetSize();
}

ImVec2 ax::NodeEditor::ScreenToCanvas(const ImVec2& pos)
{
    return GImGuiNodeEditor->ToCanvas(pos);
}

ImVec2 ax::NodeEditor::CanvasToScreen(const ImVec2& pos)
{
    return GImGuiNodeEditor->ToScreen(pos);
}

int ax::NodeEditor::GetNodeCount()
{
    return GImGuiNodeEditor->CountLiveNodes();
}

int ax::NodeEditor::GetOrderedNodeIds(NodeId* nodes, int size)
{
    return GImGuiNodeEditor->GetNodeIds(nodes, size);
}
