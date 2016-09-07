#include "Editor.h"
#include "EditorApi.h"


//------------------------------------------------------------------------------
static ax::Editor::Detail::Context* s_Editor = nullptr;


//------------------------------------------------------------------------------
ax::Editor::Context* ax::Editor::CreateEditor(const Config* config)
{
    return reinterpret_cast<ax::Editor::Context*>(new ax::Editor::Detail::Context(config));
}

void ax::Editor::DestroyEditor(Context* ctx)
{
    if (GetCurrentEditor() == ctx)
        SetCurrentEditor(nullptr);

    auto editor = reinterpret_cast<ax::Editor::Detail::Context*>(ctx);

    delete editor;
}

void ax::Editor::SetCurrentEditor(Context* ctx)
{
    s_Editor = reinterpret_cast<ax::Editor::Detail::Context*>(ctx);
}

ax::Editor::Context* ax::Editor::GetCurrentEditor()
{
    return reinterpret_cast<ax::Editor::Context*>(s_Editor);
}

ax::Editor::Style& ax::Editor::GetStyle()
{
    return s_Editor->GetStyle();
}

const char* ax::Editor::GetStyleColorName(StyleColor colorIndex)
{
    return s_Editor->GetStyle().GetColorName(colorIndex);
}

void ax::Editor::PushStyleColor(StyleColor colorIndex, const ImVec4& color)
{
    s_Editor->GetStyle().PushColor(colorIndex, color);
}

void ax::Editor::PopStyleColor(int count)
{
    s_Editor->GetStyle().PopColor(count);
}

void ax::Editor::PushStyleVar(StyleVar varIndex, float value)
{
    s_Editor->GetStyle().PushVar(varIndex, value);
}

void ax::Editor::PushStyleVar(StyleVar varIndex, const ImVec2& value)
{
    s_Editor->GetStyle().PushVar(varIndex, value);
}

void ax::Editor::PushStyleVar(StyleVar varIndex, const ImVec4& value)
{
    s_Editor->GetStyle().PushVar(varIndex, value);
}

void ax::Editor::PopStyleVar(int count)
{
    s_Editor->GetStyle().PopVar(count);
}

void ax::Editor::Begin(const char* id, const ImVec2& size)
{
    s_Editor->Begin(id, size);
}

void ax::Editor::End()
{
    s_Editor->End();
}

void ax::Editor::BeginNode(int id)
{
    s_Editor->GetNodeBuilder().Begin(id);
}

void ax::Editor::BeginPin(int id, PinKind kind)
{
    s_Editor->GetNodeBuilder().BeginPin(id, kind);
}

void ax::Editor::PinRect(const ImVec2& a, const ImVec2& b)
{
    s_Editor->GetNodeBuilder().PinRect(a, b);
}

void ax::Editor::PinPivotRect(const ImVec2& a, const ImVec2& b)
{
    s_Editor->GetNodeBuilder().PinPivotRect(a, b);
}

void ax::Editor::PinPivotSize(const ImVec2& size)
{
    s_Editor->GetNodeBuilder().PinPivotSize(size);
}

void ax::Editor::PinPivotScale(const ImVec2& scale)
{
    s_Editor->GetNodeBuilder().PinPivotScale(scale);
}

void ax::Editor::PinPivotAlignment(const ImVec2& alignment)
{
    s_Editor->GetNodeBuilder().PinPivotAlignment(alignment);
}

void ax::Editor::EndPin()
{
    s_Editor->GetNodeBuilder().EndPin();
}

void ax::Editor::Group(const ImVec2& size)
{
    s_Editor->GetNodeBuilder().Group(size);
}

void ax::Editor::EndNode()
{
    s_Editor->GetNodeBuilder().End();
}

bool ax::Editor::BeginGroupHint(int nodeId)
{
    return s_Editor->GetHintBuilder().Begin(nodeId);
}

ImVec2 ax::Editor::GetGroupMin()
{
    return s_Editor->GetHintBuilder().GetGroupMin();
}

ImVec2 ax::Editor::GetGroupMax()
{
    return s_Editor->GetHintBuilder().GetGroupMax();
}

ImDrawList* ax::Editor::GetHintForegroundDrawList()
{
    return s_Editor->GetHintBuilder().GetForegroundDrawList();
}

ImDrawList* ax::Editor::GetHintBackgroundDrawList()
{
    return s_Editor->GetHintBuilder().GetBackgroundDrawList();
}

void ax::Editor::EndGroupHint()
{
    s_Editor->GetHintBuilder().End();
}

ImDrawList* ax::Editor::GetNodeBackgroundDrawList(int nodeId)
{
    if (auto node = s_Editor->FindNode(nodeId))
        return s_Editor->GetNodeBuilder().GetUserBackgroundDrawList(node);
    else
        return nullptr;
}

bool ax::Editor::Link(int id, int startPinId, int endPinId, const ImVec4& color/* = ImVec4(1, 1, 1, 1)*/, float thickness/* = 1.0f*/)
{
    return s_Editor->DoLink(id, startPinId, endPinId, ImColor(color), thickness);
}

void ax::Editor::Flow(int linkId)
{
    if (auto link = s_Editor->FindLink(linkId))
        s_Editor->Flow(link);
}

bool ax::Editor::BeginCreate(const ImVec4& color, float thickness)
{
    auto& context = s_Editor->GetItemCreator();

    if (context.Begin())
    {
        context.SetStyle(ImColor(color), thickness);
        return true;
    }
    else
        return false;
}

bool ax::Editor::QueryNewLink(int* startId, int* endId)
{
    using Result = ax::Editor::Detail::CreateItemAction::Result;

    auto& context = s_Editor->GetItemCreator();

    return context.QueryLink(startId, endId) == Result::True;
}

bool ax::Editor::QueryNewLink(int* startId, int* endId, const ImVec4& color, float thickness)
{
    using Result = ax::Editor::Detail::CreateItemAction::Result;

    auto& context = s_Editor->GetItemCreator();

    auto result = context.QueryLink(startId, endId);
    if (result != Result::Indeterminate)
        context.SetStyle(ImColor(color), thickness);

    return result == Result::True;
}

bool ax::Editor::QueryNewNode(int* pinId)
{
    using Result = ax::Editor::Detail::CreateItemAction::Result;

    auto& context = s_Editor->GetItemCreator();

    return context.QueryNode(pinId) == Result::True;
}

bool ax::Editor::QueryNewNode(int* pinId, const ImVec4& color, float thickness)
{
    using Result = ax::Editor::Detail::CreateItemAction::Result;

    auto& context = s_Editor->GetItemCreator();

    auto result = context.QueryNode(pinId);
    if (result != Result::Indeterminate)
        context.SetStyle(ImColor(color), thickness);

    return result == Result::True;
}

bool ax::Editor::AcceptNewItem()
{
    using Result = ax::Editor::Detail::CreateItemAction::Result;

    auto& context = s_Editor->GetItemCreator();

    return context.AcceptItem() == Result::True;
}

bool ax::Editor::AcceptNewItem(const ImVec4& color, float thickness)
{
    using Result = ax::Editor::Detail::CreateItemAction::Result;

    auto& context = s_Editor->GetItemCreator();

    auto result = context.AcceptItem();
    if (result != Result::Indeterminate)
        context.SetStyle(ImColor(color), thickness);

    return result == Result::True;
}

void ax::Editor::RejectNewItem()
{
    auto& context = s_Editor->GetItemCreator();

    context.RejectItem();
}

void ax::Editor::RejectNewItem(const ImVec4& color, float thickness)
{
    using Result = ax::Editor::Detail::CreateItemAction::Result;

    auto& context = s_Editor->GetItemCreator();

    if (context.RejectItem() != Result::Indeterminate)
        context.SetStyle(ImColor(color), thickness);
}

void ax::Editor::EndCreate()
{
    auto& context = s_Editor->GetItemCreator();

    context.End();
}

bool ax::Editor::BeginDelete()
{
    auto& context = s_Editor->GetItemDeleter();

    return context.Begin();
}

bool ax::Editor::QueryDeletedLink(int* linkId)
{
    auto& context = s_Editor->GetItemDeleter();

    return context.QueryLink(linkId);
}

bool ax::Editor::QueryDeletedNode(int* nodeId)
{
    auto& context = s_Editor->GetItemDeleter();

    return context.QueryNode(nodeId);
}

bool ax::Editor::AcceptDeletedItem()
{
    auto& context = s_Editor->GetItemDeleter();

    return context.AcceptItem();
}

void ax::Editor::RejectDeletedItem()
{
    auto& context = s_Editor->GetItemDeleter();

    context.RejectItem();
}

void ax::Editor::EndDelete()
{
    auto& context = s_Editor->GetItemDeleter();

    context.End();
}

void ax::Editor::SetNodePosition(int nodeId, const ImVec2& position)
{
    s_Editor->SetNodePosition(nodeId, position);
}

ImVec2 ax::Editor::GetNodePosition(int nodeId)
{
    return s_Editor->GetNodePosition(nodeId);
}

void ax::Editor::Suspend()
{
    s_Editor->Suspend();
}

void ax::Editor::Resume()
{
    s_Editor->Resume();
}

bool ax::Editor::HasSelectionChanged()
{
    return s_Editor->HasSelectionChanged();
}

int ax::Editor::GetSelectedObjectCount()
{
    return (int)s_Editor->GetSelectedObjects().size();
}

int ax::Editor::GetSelectedNodes(int* nodes, int size)
{
    int count = 0;
    for (auto object : s_Editor->GetSelectedObjects())
    {
        if (size <= 0)
            break;

        if (auto node = object->AsNode())
        {
            nodes[count] = node->ID;
            ++count;
            --size;
        }
    }

    return count;
}

int ax::Editor::GetSelectedLinks(int* links, int size)
{
    int count = 0;
    for (auto object : s_Editor->GetSelectedObjects())
    {
        if (size <= 0)
            break;

        if (auto link = object->AsLink())
        {
            links[count] = link->ID;
            ++count;
            --size;
        }
    }

    return count;
}

void ax::Editor::ClearSelection()
{
    s_Editor->ClearSelection();
}

void ax::Editor::SelectNode(int nodeId, bool append)
{
    if (auto node = s_Editor->FindNode(nodeId))
    {
        if (append)
            s_Editor->SelectObject(node);
        else
            s_Editor->SetSelectedObject(node);
    }
}

void ax::Editor::SelectLink(int linkId, bool append)
{
    if (auto link = s_Editor->FindLink(linkId))
    {
        if (append)
            s_Editor->SelectObject(link);
        else
            s_Editor->SetSelectedObject(link);
    }
}

void ax::Editor::DeselectNode(int nodeId)
{
    if (auto node = s_Editor->FindNode(nodeId))
        s_Editor->DeselectObject(node);
}

void ax::Editor::DeselectLink(int linkId)
{
    if (auto link = s_Editor->FindLink(linkId))
        s_Editor->DeselectObject(link);
}

void ax::Editor::NavigateToContent(float duration)
{
    s_Editor->NavigateTo(s_Editor->GetContentBounds(), true, duration);
}

void ax::Editor::NavigateToSelection(bool zoomIn, float duration)
{
    s_Editor->NavigateTo(s_Editor->GetSelectionBounds(), zoomIn, duration);
}

bool ax::Editor::ShowNodeContextMenu(int* nodeId)
{
    return s_Editor->GetContextMenu().ShowNodeContextMenu(nodeId);
}

bool ax::Editor::ShowPinContextMenu(int* pinId)
{
    return s_Editor->GetContextMenu().ShowPinContextMenu(pinId);
}

bool ax::Editor::ShowLinkContextMenu(int* linkId)
{
    return s_Editor->GetContextMenu().ShowLinkContextMenu(linkId);
}

bool ax::Editor::ShowBackgroundContextMenu()
{
    return s_Editor->GetContextMenu().ShowBackgroundContextMenu();
}