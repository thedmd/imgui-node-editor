#include "Editor.h"
#include "Backend/imgui_impl_dx11.h"
#include "Drawing.h"
#include <cstdlib> // _itoa
#include <string>
#include <fstream>


//------------------------------------------------------------------------------
namespace ed = ax::Editor::Detail;


//------------------------------------------------------------------------------
static const int c_BackgroundChannelCount = 1;
static const int c_LinkChannelCount       = 2;

static const int c_BackgroundChannelStart = 0;
static const int c_LinkStartChannel       = c_BackgroundChannelStart + c_BackgroundChannelCount;
static const int c_NodeStartChannel       = c_LinkStartChannel       + c_LinkChannelCount;

static const int c_ChannelsPerNode        = 3;
static const int c_NodeBaseChannel        = 0;
static const int c_NodeBackgroundChannel  = 1;
static const int c_NodeContentChannel     = 2;

static const float c_NodeFrameRounding    = 12.0f;
static const float c_LinkStrength         = 100.0f;
static const float c_LinkSelectThickness  = 5.0f;


//------------------------------------------------------------------------------
extern "C" __declspec(dllimport) void __stdcall OutputDebugStringA(const char* string);

static void LogV(const char* fmt, va_list args)
{
    const int buffer_size = 1024;
    static char buffer[1024];

    int w = vsnprintf(buffer, buffer_size - 1, fmt, args);
    buffer[buffer_size - 1] = 0;

    ImGui::LogText("\nNode Editor: %s", buffer);

    OutputDebugStringA("NodeEditor: ");
    OutputDebugStringA(buffer);
    OutputDebugStringA("\n");
}

void ed::Log(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    LogV(fmt, args);
    va_end(args);
}


//------------------------------------------------------------------------------
static void ImDrawList_ChannelsGrow(ImDrawList* draw_list, int channels_count)
{
    IM_ASSERT(draw_list->_ChannelsCount <= channels_count);
    int old_channels_count = draw_list->_Channels.Size;
    if (old_channels_count < channels_count)
        draw_list->_Channels.resize(channels_count);
    int old_used_channels_count = draw_list->_ChannelsCount;
    draw_list->_ChannelsCount = channels_count;

    if (old_channels_count == 0)
        memset(&draw_list->_Channels[0], 0, sizeof(ImDrawChannel));
    for (int i = old_used_channels_count; i < channels_count; i++)
    {
        if (i >= old_channels_count)
        {
            new (&draw_list->_Channels[i]) ImDrawChannel();
        }
        else
        {
            draw_list->_Channels[i].CmdBuffer.resize(0);
            draw_list->_Channels[i].IdxBuffer.resize(0);
        }
        if (draw_list->_Channels[i].CmdBuffer.Size == 0)
        {
            ImDrawCmd draw_cmd;
            draw_cmd.ClipRect  = draw_list->_ClipRectStack.back();
            draw_cmd.TextureId = draw_list->_TextureIdStack.back();
            draw_list->_Channels[i].CmdBuffer.push_back(draw_cmd);
        }
    }
}

static void ImDrawList_SwapChannels(ImDrawList* drawList, int left, int right)
{
    IM_ASSERT(left < drawList->_ChannelsCount && right < drawList->_ChannelsCount);
    if (left == right)
        return;

    auto currentChannel = drawList->_ChannelsCurrent;

    auto* leftCmdBuffer  = &drawList->_Channels[left].CmdBuffer;
    auto* leftIdxBuffer  = &drawList->_Channels[left].IdxBuffer;
    auto* rightCmdBuffer = &drawList->_Channels[right].CmdBuffer;
    auto* rightIdxBuffer = &drawList->_Channels[right].IdxBuffer;

    leftCmdBuffer->swap(*rightCmdBuffer);
    leftIdxBuffer->swap(*rightIdxBuffer);

    if (currentChannel == left)
        drawList->_ChannelsCurrent = right;
    else if (currentChannel == right)
        drawList->_ChannelsCurrent = left;
}

static void ImDrawList_OffsetChannels(ImDrawList* drawList, const ImVec2& offset, int begin, int end)
{
    int lastCurrentChannel = drawList->_ChannelsCurrent;
    if (lastCurrentChannel != 0)
        drawList->ChannelsSetCurrent(0);

    auto& vtxBuffer = drawList->VtxBuffer;
    for (int channelIndex = begin; channelIndex < end; ++channelIndex)
    {
        auto& channel = drawList->_Channels[channelIndex];
        auto  idxRead = channel.IdxBuffer.Data;

        int indexOffset = 0;
        for (auto& cmd : channel.CmdBuffer)
        {
            if (cmd.ElemCount == 0) continue;

            auto idxRange = std::minmax_element(idxRead, idxRead + cmd.ElemCount);
            auto vtxBegin = vtxBuffer.Data + *idxRange.first;
            auto vtxEnd   = vtxBuffer.Data + *idxRange.second + 1;

            for (auto vtx = vtxBegin; vtx < vtxEnd; ++vtx)
            {
                vtx->pos.x += offset.x;
                vtx->pos.y += offset.y;
            }

            idxRead += cmd.ElemCount;
        }
    }

    if (lastCurrentChannel != 0)
        drawList->ChannelsSetCurrent(lastCurrentChannel);
}



//------------------------------------------------------------------------------
ed::Context::Context():
    Nodes(),
    Pins(),
    Links(),
    //HotObject(nullptr),
    //ActiveObject(nullptr),
    SelectedObject(nullptr),
    LastActiveLink(nullptr),
    CurrentPin(nullptr),
    CurrentNode(nullptr),
    DragOffset(),
    DraggedNode(nullptr),
    DraggedPin(nullptr),
    Scrolling(0, 0),
    NodeBuildStage(NodeStage::Invalid),
    LinkCreateStage(LinkCreateStage::None),
    IsInitialized(false),
    HeaderTextureID(nullptr),
    Settings()
{
}

ed::Context::~Context()
{
    if (IsInitialized)
    {
        if (HeaderTextureID)
        {
            ImGui_DestroyTexture(HeaderTextureID);
            HeaderTextureID = nullptr;
        }

        SaveSettings();
    }

    for (auto link : Links) delete link;
    for (auto pin  : Pins)  delete pin;
    for (auto node : Nodes) delete node;
}

void ed::Context::Begin(const char* id)
{
    if (!IsInitialized)
    {
        LoadSettings();

        HeaderTextureID = ImGui_LoadTexture("../Data/BlueprintBackground.png");

        IsInitialized = true;
    }

    //ImGui::LogToClipboard();
    //Log("---- begin ----");

    for (auto node : Nodes) node->IsLive = false;
    for (auto pin  : Pins)   pin->IsLive = false;
    for (auto link : Links) link->IsLive = false;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_ChildWindowBg, ImColor(60, 60, 70, 0));
    ImGui::BeginChild(id, ImVec2(0, 0), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove);

    // Reserve channels for background and links
    auto drawList = ImGui::GetWindowDrawList();
    ImDrawList_ChannelsGrow(drawList, c_NodeStartChannel);
}

void ed::Context::End()
{
    auto& io       = ImGui::GetIO();
    auto  control  = ComputeControl();
    auto  drawList = ImGui::GetWindowDrawList();

    if (control.BackgroundClicked)
    {
        ClearSelection();
    }
    else if (control.ClickedNode && (control.ClickedNode != DraggedNode))
    {
        if (IsAnyLinkSelected())
            ClearSelection();

        if (io.KeyCtrl)
        {
            if (IsSelected(control.ClickedNode))
                RemoveSelectedObject(control.ClickedNode);
            else
                AddSelectedObject(control.ClickedNode);
        }
        else
            SetSelectedObject(control.ClickedNode);
    }
    else if (control.ClickedLink)
    {
        if (IsAnyNodeSelected())
            ClearSelection();

        if (io.KeyCtrl)
        {
            if (IsSelected(control.ClickedLink))
                RemoveSelectedObject(control.ClickedLink);
            else
                AddSelectedObject(control.ClickedLink);
        }
        else
            SetSelectedObject(control.ClickedLink);
    }

    //auto selectedNode = SelectedObject ? SelectedObject->AsNode() : nullptr;

    // Highlight hovered node
    for (auto selectedObject : SelectedObjects)
    {
        if (auto selectedNode = selectedObject->AsNode())
        {
            drawList->ChannelsSetCurrent(selectedNode->Channel + c_NodeBaseChannel);

            drawList->AddRect(
                to_imvec(selectedNode->Bounds.top_left()) - Scrolling,
                to_imvec(selectedNode->Bounds.bottom_right()) - Scrolling,
                ImColor(255, 176, 50, 255), c_NodeFrameRounding, 15, 3.5f);
        }
        else if (auto selectedLink = selectedObject->AsLink())
        {
            drawList->ChannelsSetCurrent(c_LinkStartChannel + 0);

            ax::Drawing::DrawLink(drawList,
                to_imvec(selectedLink->StartPin->DragPoint),
                to_imvec(selectedLink->EndPin->DragPoint),
                ImColor(255, 176, 50, 255), selectedLink->Thickness + 4.5f, c_LinkStrength);
        }
    }

    // Highlight selected node
    auto hotNode = control.HotNode;
    if (hotNode && !IsSelected(hotNode))
    {
        drawList->ChannelsSetCurrent(hotNode->Channel + c_NodeBaseChannel);

        drawList->AddRect(
            to_imvec(hotNode->Bounds.top_left()) - Scrolling,
            to_imvec(hotNode->Bounds.bottom_right()) - Scrolling,
            ImColor(50, 176, 255, 255), c_NodeFrameRounding, 15, 3.5f);
    }

    // Highlight hovered pin
    if (auto hotPin = control.HotPin)
    {
        drawList->ChannelsSetCurrent(hotPin->Node->Channel + c_NodeBackgroundChannel);

        drawList->AddRectFilled(
            to_imvec(hotPin->Bounds.top_left()),
            to_imvec(hotPin->Bounds.bottom_right()),
            ImColor(60, 180, 255, 100), 4.0f);
    }

//     for (auto pin : Pins)
//     {
//         drawList->ChannelsSetCurrent(pin->Node->Channel + c_NodeBackgroundChannel);
// 
//         drawList->AddRectFilled(
//             to_imvec(pin->Bounds.top_left()),
//             to_imvec(pin->Bounds.bottom_right()),
//             ImColor(60, 180, 255, 100), 4.0f);
//     }

    // Draw links
    drawList->ChannelsSetCurrent(c_LinkStartChannel + 1);
    for (auto link : Links)
    {
        if (!link->IsLive)
            continue;

        float extraThickness = !IsSelected(link) && (control.HotLink == link || control.ActiveLink == link) ? 2.0f : 0.0f;

        ax::Drawing::DrawLink(drawList,
            to_imvec(link->StartPin->DragPoint),
            to_imvec(link->EndPin->DragPoint),
            link->Color, link->Thickness + extraThickness, c_LinkStrength);
    }

    // Highlight hovered link
    if (control.HotLink && !IsSelected(control.HotLink))
    {
        drawList->ChannelsSetCurrent(c_LinkStartChannel + 0);

        ax::Drawing::DrawLink(drawList,
            to_imvec(control.HotLink->StartPin->DragPoint),
            to_imvec(control.HotLink->EndPin->DragPoint),
            ImColor(50, 176, 255, 255), control.HotLink->Thickness + 4.5f, c_LinkStrength);
    }

    // Handle node dragging, does not steal selection
    if (!DraggedNode && control.ActiveNode && ImGui::IsMouseDragging(0))
    {
        DraggedNode = control.ActiveNode;
        control.ActiveNode->DragStart = control.ActiveNode->Bounds.location;

        if (IsSelected(control.ActiveNode))
        {
            for (auto selectedObject : SelectedObjects)
                if (auto selectedNode = selectedObject->AsNode())
                    selectedNode->DragStart = selectedNode->Bounds.location;
        }
        else
            control.ActiveNode->DragStart = control.ActiveNode->Bounds.location;
    }

    if (DraggedNode && control.ActiveNode == DraggedNode)
    {
        DragOffset = ImGui::GetMouseDragDelta(0, 0.0f);

        if (IsSelected(control.ActiveNode))
        {
            for (auto selectedObject : SelectedObjects)
                if (auto selectedNode = selectedObject->AsNode())
                    selectedNode->Bounds.location = selectedNode->DragStart + to_point(DragOffset);
        }
        else
            control.ActiveNode->Bounds.location = control.ActiveNode->DragStart + to_point(DragOffset);
    }
    else if (DraggedNode && !control.ActiveNode)
    {
        DraggedNode = nullptr;
    }


    // Link creation
    if (!DraggedPin && control.ActivePin && ImGui::IsMouseDragging(0))
    {
        DraggedPin = control.ActivePin;
        LinkCreateStage  = LinkCreateStage::Possible;
        LinkStart  = DraggedPin->Type == PinType::Output ? DraggedPin : nullptr;
        LinkEnd    = DraggedPin->Type == PinType::Input  ? DraggedPin : nullptr;

        ClearSelection();
    }

    if (DraggedPin && control.ActivePin == DraggedPin && (LinkCreateStage == LinkCreateStage::Edit || LinkCreateStage == LinkCreateStage::Accept || LinkCreateStage == LinkCreateStage::Reject))
    {
        auto& startPin = DraggedPin->Type == PinType::Output ? LinkStart : LinkEnd;
        auto& endPin   = DraggedPin->Type == PinType::Output ? LinkEnd   : LinkStart;

        ImVec2 startPoint = to_imvec(DraggedPin->DragPoint);
        ImVec2 endPoint   = ImGui::GetMousePos();
        if (LinkCreateStage == LinkCreateStage::Accept)
            endPoint = to_imvec(endPin->DragPoint);

        if (DraggedPin->Type == PinType::Input)
            std::swap(startPoint, endPoint);

        ax::Drawing::DrawLink(drawList, startPoint, endPoint, LinkColor, LinkThickness, c_LinkStrength);

        if (control.HotPin && control.HotPin != DraggedPin)
            endPin = control.HotPin;
        else
            endPin = nullptr;
    }
    else if (DraggedPin && !control.ActivePin)
    {
        DraggedPin = nullptr;

        if (LinkCreateStage == LinkCreateStage::Accept)
            LinkCreateStage = LinkCreateStage::Created;
        else
            LinkCreateStage = LinkCreateStage::None;
    }

    // Link deletion
    const bool isDeletePressed = ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Delete));
    if ((IsAnyLinkSelected() && isDeletePressed) || (control.ClickedLink && io.KeyAlt))
    {
        if (IsAnyLinkSelected())
        {
            for (auto selectedObject : SelectedObjects)
                DeletedLinks.push_back(selectedObject->AsLink());
        }

        if (control.ClickedObject && !IsSelected(control.ClickedObject))
            DeletedLinks.push_back(control.ClickedLink);
    }
    else
        DeletedLinks.clear();

    // Bring active node to front
    std::stable_sort(Nodes.begin(), Nodes.end(), [&control](Node* lhs, Node* rhs)
    {
        return (rhs == control.ActiveNode);
    });

    // Every node has few channels assigned. Grow channel list
    // to hold twice as much of channels and place them in
    // node drawing order.
    {
        // Reserve two additional channels for sorted list of channels
        auto nodeChannelCount = drawList->_ChannelsCount;
        ImDrawList_ChannelsGrow(drawList, (drawList->_ChannelsCount - 1) * 2 + 1);

        int targetChannel = nodeChannelCount;
        for (auto node : Nodes)
        {
            for (int i = 0; i < c_ChannelsPerNode; ++i)
                ImDrawList_SwapChannels(drawList, node->Channel + i, targetChannel + i);
            node->Channel = targetChannel;
            targetChannel += c_ChannelsPerNode;
        }
    }

    // Draw grid
    {
        drawList->ChannelsSetCurrent(c_BackgroundChannelStart + 0);

        ImVec2 offset      = ImVec2(0, 0) - Scrolling;
        //ImVec2 offset    = ImVec2(0, 0);
        ImU32 GRID_COLOR = ImColor(120, 120, 120, 40);
        float GRID_SZ    = 32.0f;
        ImVec2 win_pos   = ImGui::GetWindowPos();
        ImVec2 canvas_sz = ImGui::GetWindowSize();

        drawList->AddRectFilled(win_pos, win_pos + canvas_sz, ImColor(60, 60, 70, 200));

        for (float x = fmodf(offset.x, GRID_SZ); x < canvas_sz.x; x += GRID_SZ)
            drawList->AddLine(ImVec2(x, 0.0f) + win_pos, ImVec2(x, canvas_sz.y) + win_pos, GRID_COLOR);
        for (float y = fmodf(offset.y, GRID_SZ); y < canvas_sz.y; y += GRID_SZ)
            drawList->AddLine(ImVec2(0.0f, y) + win_pos, ImVec2(canvas_sz.x, y) + win_pos, GRID_COLOR);
    }

    if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemActive() && ImGui::IsMouseDragging(2, 0.0f))
        Scrolling = Scrolling - ImGui::GetIO().MouseDelta;

    drawList->ChannelsMerge();

    ImGui::SetCursorScreenPos(ImGui::GetWindowPos());
    auto getObjectName = [](Object* object)
    {
        if (!object) return "";
        else if (object->AsNode()) return "Node";
        else if (object->AsPin())  return "Pin";
        else if (object->AsLink()) return "Link";
        else return "";
    };

    ImGui::Text("Hot Object: %s (%d)", getObjectName(control.HotObject), control.HotObject ? control.HotObject->ID : 0);
    ImGui::Text("Active Object: %s (%d)", getObjectName(control.ActiveObject), control.ActiveObject ? control.ActiveObject->ID : 0);
    //ImGui::Text("Clicked Object: %s (%d)", getObjectName(control.ClickedObject), control.ClickedObject ? control.ClickedObject->ID : 0);

    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    if (Settings.Dirty)
    {
        Settings.Dirty = false;
        SaveSettings();
    }
}

void ed::Context::BeginNode(int id)
{
    assert(nullptr == CurrentNode);

    auto node = FindNode(id);
    if (!node)
        node = CreateNode(id);

    SetCurrentNode(node);

    // Position node on screen
    ImGui::SetCursorScreenPos(to_imvec(node->Bounds.location) - Scrolling);

    node->IsLive  = true;
    node->LastPin = nullptr;

    auto drawList = ImGui::GetWindowDrawList();
    node->Channel = drawList->_ChannelsCount;
    ImDrawList_ChannelsGrow(drawList, drawList->_ChannelsCount + c_ChannelsPerNode);
    drawList->ChannelsSetCurrent(node->Channel + c_NodeContentChannel);

    SetNodeStage(NodeStage::Begin);
}

void ed::Context::EndNode()
{
    assert(nullptr != CurrentNode);

    SetNodeStage(NodeStage::End);

    if (CurrentNode->Bounds.size != NodeRect.size)
    {
        MarkSettingsDirty();

        CurrentNode->Bounds.size = NodeRect.size;
    }

    // Draw background
    {
        auto drawList = ImGui::GetWindowDrawList();

        auto drawListForegroundEnd = drawList->CmdBuffer.size();

        drawList->ChannelsSetCurrent(CurrentNode->Channel + c_NodeBackgroundChannel);

        auto headerColor = 0xFF000000 | (HeaderColor & 0x00FFFFFF);
        ax::Drawing::DrawHeader(drawList, HeaderTextureID,
            to_imvec(HeaderRect.top_left()),
            to_imvec(HeaderRect.bottom_right()),
            headerColor, c_NodeFrameRounding, 4.0f);

        auto headerSeparatorRect      = ax::rect(HeaderRect.bottom_left(),  ContentRect.top_right());
        auto footerSeparatorRect      = ax::rect(ContentRect.bottom_left(), NodeRect.bottom_right());
        auto contentWithSeparatorRect = ax::rect::make_union(headerSeparatorRect, footerSeparatorRect);

        drawList->AddRectFilled(
            to_imvec(contentWithSeparatorRect.top_left()),
            to_imvec(contentWithSeparatorRect.bottom_right()),
            ImColor(32, 32, 32, 200), c_NodeFrameRounding, 4 | 8);

        drawList->AddLine(
            to_imvec(headerSeparatorRect.top_left() + point(1, -1)),
            to_imvec(headerSeparatorRect.top_right() + point(-1, -1)),
            ImColor(255, 255, 255, 96 / 3), 1.0f);

        drawList->AddRect(
            to_imvec(NodeRect.top_left()),
            to_imvec(NodeRect.bottom_right()),
            ImColor(255, 255, 255, 96), c_NodeFrameRounding, 15, 1.5f);
    }

    SetCurrentNode(nullptr);

    SetNodeStage(NodeStage::Invalid);
}

void ed::Context::BeginHeader(ImU32 color)
{
    assert(nullptr != CurrentNode);

    HeaderColor = color;
    SetNodeStage(NodeStage::Header);
}

void ed::Context::EndHeader()
{
    assert(nullptr != CurrentNode);

    SetNodeStage(NodeStage::Content);
}

void ed::Context::BeginInput(int id)
{
    assert(nullptr != CurrentNode);

    if (NodeBuildStage == NodeStage::Begin)
        SetNodeStage(NodeStage::Content);

    SetNodeStage(NodeStage::Input);

    BeginPin(id, PinType::Input);

    ImGui::Spring(0);
    ImGui::BeginHorizontal(id);
}

void ed::Context::EndInput()
{
    ImGui::EndHorizontal();

    EndPin();

    ImGui::Spring(0, 0);
}

void ed::Context::BeginOutput(int id)
{
    assert(nullptr != CurrentNode);

    if (NodeBuildStage == NodeStage::Begin)
        SetNodeStage(NodeStage::Content);

    if (NodeBuildStage == NodeStage::Begin)
        SetNodeStage(NodeStage::Input);

    SetNodeStage(NodeStage::Output);

    BeginPin(id, PinType::Output);

    ImGui::Spring(0);
    ImGui::BeginHorizontal(id);
}

void ed::Context::EndOutput()
{
    ImGui::EndHorizontal();

    EndPin();

    ImGui::Spring(0, 0);
}

bool ed::Context::CreateLink(int* startId, int* endId, ImU32 color, float thickness)
{
    if (LinkCreateStage == LinkCreateStage::None)
        return false;

    *startId = LinkStart ? LinkStart->ID : 0;
    *endId   = LinkEnd   ? LinkEnd->ID   : 0;

    if (LinkCreateStage != LinkCreateStage::Created)
    {
        LinkCreateStage = LinkCreateStage::Edit;
        LinkColor       = color;
        LinkThickness   = thickness;
    }

    return true;
}

void ed::Context::RejectLink(ImU32 color, float thickness)
{
    assert(LinkCreateStage == LinkCreateStage::Edit || LinkCreateStage == LinkCreateStage::Accept || LinkCreateStage == LinkCreateStage::Reject);
    LinkCreateStage = LinkCreateStage::Reject;
    LinkColor       = color;
    LinkThickness   = thickness;
}

bool ed::Context::AcceptLink(ImU32 color, float thickness)
{
    assert(LinkCreateStage == LinkCreateStage::Edit || LinkCreateStage == LinkCreateStage::Accept || LinkCreateStage == LinkCreateStage::Reject || LinkCreateStage == LinkCreateStage::Created);
    assert(LinkEnd);

    LinkColor     = color;
    LinkThickness = thickness;

    if (LinkCreateStage == LinkCreateStage::Created)
    {
        LinkCreateStage = LinkCreateStage::None;
        return true;
    }
    else
    {
        LinkCreateStage = LinkCreateStage::Accept;
        return false;
    }
}

bool ed::Context::DestroyLink()
{
    return !DeletedLinks.empty();
}

int ed::Context::GetDestroyedLinkId()
{
    if (DeletedLinks.empty())
        return 0;

    auto link = DeletedLinks.back();
    DeletedLinks.pop_back();

    RemoveSelectedObject(link);
    if (link == LastActiveLink)
        LastActiveLink = nullptr;

    return link->ID;
}

bool ed::Context::DoLink(int id, int startPinId, int endPinId, ImU32 color, float thickness)
{
    auto link     = GetLink(id);
    auto startPin = FindPin(startPinId);
    auto endPin   = FindPin(endPinId);

    link->StartPin = startPin;
    link->EndPin   = endPin;

    link->Color     = color;
    link->Thickness = thickness;

    link->IsLive =
        (startPin && startPin->IsLive) &&
        (endPin   && endPin->IsLive);

    return link->IsLive;
}

ed::Pin* ed::Context::CreatePin(int id, PinType type)
{
    assert(nullptr == FindObject(id));
    auto pin = new Pin(id, type);
    Pins.push_back(pin);
    return pin;
}

ed::Node* ed::Context::CreateNode(int id)
{
    assert(nullptr == FindObject(id));
    auto node = new Node(id);
    Nodes.push_back(node);

    auto settings = FindNodeSettings(id);
    if (!settings)
    {
        settings = AddNodeSettings(id);
        node->Bounds.location = to_point(ImGui::GetCursorScreenPos());
    }
    else
        node->Bounds.location = settings->Location;

    return node;
}

ed::Link* ed::Context::CreateLink(int id)
{
    assert(nullptr == FindObject(id));
    auto link = new Link(id);
    Links.push_back(link);

    return link;
}

void ed::Context::DestroyObject(Node* node)
{
}

ed::Object* ed::Context::FindObject(int id)
{
    if (auto object = FindNode(id))
        return object;
    if (auto object = FindPin(id))
        return object;
    if (auto object = FindLink(id))
        return object;
    return nullptr;
}

template <typename C>
static inline auto FindItemIn(C& container, int id)
{
    for (auto item : container)
        if (item->ID == id)
            return item;

    return (typename C::value_type)nullptr;
}

ed::Node* ed::Context::FindNode(int id)
{
    return FindItemIn(Nodes, id);
}

ed::Pin* ed::Context::FindPin(int id)
{
    return FindItemIn(Pins, id);
}

ed::Link* ed::Context::FindLink(int id)
{
    return FindItemIn(Links, id);
}

void ed::Context::ClearSelection()
{
    SelectedObjects.clear();
    SelectedObject = nullptr;
}

void ed::Context::AddSelectedObject(Object* object)
{
    SelectedObjects.push_back(object);
    SelectedObject = SelectedObjects.front();
}

void ed::Context::RemoveSelectedObject(Object* object)
{
    auto objectIt = std::find(SelectedObjects.begin(), SelectedObjects.end(), object);
    if (objectIt != SelectedObjects.end())
    {
        SelectedObjects.erase(objectIt);
        if (!SelectedObjects.empty())
            SelectedObject = SelectedObjects.front();
    }
}

void ed::Context::SetSelectedObject(Object* object)
{
    ClearSelection();
    AddSelectedObject(object);
}

bool ed::Context::IsSelected(Object* object)
{
    return std::find(SelectedObjects.begin(), SelectedObjects.end(), object) != SelectedObjects.end();
}

bool ed::Context::IsAnyNodeSelected()
{
    for (auto object : SelectedObjects)
        if (object->AsNode())
            return true;

    return false;
}

bool ed::Context::IsAnyLinkSelected()
{
    for (auto object : SelectedObjects)
        if (object->AsLink())
            return true;

    return false;
}

void ed::Context::SetCurrentNode(Node* node)
{
    CurrentNode = node;
}

void ed::Context::SetCurrentPin(Pin* pin)
{
    CurrentPin = pin;

    if (pin)
        SetCurrentNode(pin->Node);
}

bool ed::Context::SetNodeStage(NodeStage stage)
{
    if (stage == NodeBuildStage)
        return false;

    auto oldStage = NodeBuildStage;
    NodeBuildStage = stage;

    ImVec2 cursor;
    switch (oldStage)
    {
        case NodeStage::Begin:
            break;

        case NodeStage::Header:
            ImGui::EndHorizontal();
            HeaderRect = ImGui_GetItemRect();

            // spacing between header and content
            ImGui::Spring(0);

            break;

        case NodeStage::Content:
            break;

        case NodeStage::Input:
            ImGui::Spring(1);
            ImGui::EndVertical();
            break;

        case NodeStage::Output:
            ImGui::Spring(1);
            ImGui::EndVertical();
            break;

        case NodeStage::End:
            break;
    }

    switch (stage)
    {
        case NodeStage::Begin:
            ImGui::BeginVertical(CurrentNode->ID);
            break;

        case NodeStage::Header:
            ImGui::BeginHorizontal("header");
            break;

        case NodeStage::Content:
            ImGui::BeginHorizontal("content");
            ImGui::Spring(0);
            break;

        case NodeStage::Input:
            ImGui::BeginVertical("inputs", ImVec2(0,0), 0.0f);
            break;

        case NodeStage::Output:
            ImGui::Spring(1);
            ImGui::BeginVertical("outputs", ImVec2(0, 0), 1.0f);
            break;

        case NodeStage::End:
            if (oldStage == NodeStage::Input)
                ImGui::Spring(1);
            ImGui::Spring(0);
            ImGui::EndHorizontal();
            ContentRect = ImGui_GetItemRect();

            ImGui::Spring(0);
            ImGui::EndVertical();
            NodeRect = ImGui_GetItemRect();
            break;
    }

    return true;
}

ed::Pin* ed::Context::GetPin(int id, PinType type)
{
    if (auto pin = FindPin(id))
    {
        pin->Type = type;
        return pin;
    }
    else
        return CreatePin(id, type);
}

void ed::Context::BeginPin(int id, PinType type)
{
    auto pin = GetPin(id, type);
    pin->Node = CurrentNode;
    SetCurrentPin(pin);

    pin->PreviousPin     = CurrentNode->LastPin;
    CurrentNode->LastPin = pin;
    pin->IsLive = true;
}

void ed::Context::EndPin()
{
    auto pinRect = ImGui_GetItemRect();

    CurrentPin->Bounds = pinRect;

    if (CurrentPin->Type == PinType::Input)
        CurrentPin->DragPoint = point(pinRect.left(), pinRect.center_y());
    else
        CurrentPin->DragPoint = point(pinRect.right(), pinRect.center_y());

    SetCurrentPin(nullptr);
}

ed::Link* ed::Context::GetLink(int id)
{
    if (auto link = FindLink(id))
        return link;
    else
        return CreateLink(id);
}

ed::NodeSettings* ed::Context::FindNodeSettings(int id)
{
    for (auto& nodeSettings : Settings.Nodes)
        if (nodeSettings.ID == id)
            return &nodeSettings;

    return nullptr;
}

ed::NodeSettings* ed::Context::AddNodeSettings(int id)
{
    Settings.Nodes.push_back(NodeSettings(id));
    return &Settings.Nodes.back();
}

void ed::Context::LoadSettings()
{
    namespace json = picojson;

    if (Settings.Path.empty())
        return;

    std::ifstream settingsFile(Settings.Path);
    if (!settingsFile)
        return;

    json::value settingsValue;
    auto error = json::parse(settingsValue, settingsFile);
    if (!error.empty() || settingsValue.is<json::null>())
        return;

    if (!settingsValue.is<json::object>())
        return;

    auto& settingsObject = settingsValue.get<json::object>();
    auto& nodesValue     = settingsValue.get("nodes");

    if (nodesValue.is<json::object>())
    {
        for (auto& node : nodesValue.get<json::object>())
        {
            auto id       = static_cast<int>(strtoll(node.first.c_str(), nullptr, 10));
            auto settings = FindNodeSettings(id);
            if (!settings)
                settings = AddNodeSettings(id);

            auto& nodeData = node.second;

            auto locationValue = nodeData.get("location");
            if (locationValue.is<json::object>())
            {
                auto xValue = locationValue.get("x");
                auto yValue = locationValue.get("y");

                if (xValue.is<double>() && yValue.is<double>())
                {
                    settings->Location.x = static_cast<int>(xValue.get<double>());
                    settings->Location.y = static_cast<int>(yValue.get<double>());
                }
            }
        }
    }
}

void ed::Context::SaveSettings()
{
    namespace json = picojson;

    if (Settings.Path.empty())
        return;

    std::ofstream settingsFile(Settings.Path);
    if (!settingsFile)
        return;

    // update settings data
    for (auto& node : Nodes)
    {
        auto settings = FindNodeSettings(node->ID);
        settings->Location = node->Bounds.location;
    }

    json::object nodes;
    for (auto& node : Settings.Nodes)
    {
        json::object locationData;
        locationData["x"] = json::value(static_cast<double>(node.Location.x));
        locationData["y"] = json::value(static_cast<double>(node.Location.y));

        json::object nodeData;
        nodeData["location"] = json::value(std::move(locationData));

        nodes[std::to_string(node.ID)] = json::value(std::move(nodeData));
    }

    json::object settings;
    settings["nodes"] = json::value(std::move(nodes));

    json::value settingsValue(std::move(settings));

    settingsFile << settingsValue.serialize(true);
}

void ed::Context::MarkSettingsDirty()
{
    Settings.Dirty = true;
}

ed::Link* ed::Context::FindLinkAt(const ax::point& p)
{
    for (auto& link : Links)
    {
        if (!link->IsLive) continue;

        // Live links are guarantee to have complete set of pins.
        auto startPoint = link->StartPin->DragPoint;
        auto endPoint   = link->EndPin->DragPoint;

        // Build bounding rectangle of link.
        auto topLeft = startPoint;
        auto bottomRight = endPoint;

        // Links are drawn as Bezier quadratic curves with
        // start point tangent always pointing to right, end tangent
        // pointing to left. If curve is drawn from right to left
        // there is small overshot outside of bounding box based on
        // tangent strength.
        if (topLeft.x > bottomRight.x)
        {
            std::swap(topLeft.x, bottomRight.x);
            topLeft.x = roundi(topLeft.x - c_LinkStrength * 0.25f);
            bottomRight.x = roundi(bottomRight.x + c_LinkStrength * 0.25f);
        }

        if (topLeft.y > bottomRight.y)
            std::swap(topLeft.y, bottomRight.y);

        // Calculate thickness of interactive area around curve.
        // Making it slightly larger help user to click it.
        const auto thickness = roundi(link->Thickness + c_LinkSelectThickness);

        // Expand bounding rectangle corners by curve thickness.
        topLeft -= point(thickness, thickness);
        bottomRight += point(thickness, thickness);

        // Ignore link if mouse is not hovering bounding rect.
        const auto curveBoundingRect = rect(topLeft, bottomRight);
        if (!curveBoundingRect.contains(p))
            continue;

        // Calculate distance from mouse position to link curve.
        const auto distance = ax::Drawing::LinkDistance(to_imvec(p),
            to_imvec(startPoint), to_imvec(endPoint), c_LinkStrength);

        // If point is close enough link was found.
        if (distance < thickness)
            return link;
    }

    return nullptr;
}

ed::Control ed::Context::ComputeControl()
{
    const auto mousePos = to_point(ImGui::GetMousePos());

    Object* hotObject     = nullptr;
    Object* activeObject  = nullptr;
    Object* clickedObject = nullptr;

    // Emits invisible button and returns true if it is clicked.
    auto emitInteractiveArea = [this](int id, const rect& rect, bool scroll)
    {
        char idString[33]; // itoa can output 33 bytes maximum
        _itoa(id, idString, 16);
        ImGui::SetCursorScreenPos(to_imvec(rect.location) - (scroll ? Scrolling : ImVec2(0, 0)));
        return ImGui::InvisibleButton(idString, to_imvec(rect.size));
    };

    // Check input interactions over area.
    auto checkInteractionsInArea = [&emitInteractiveArea, &hotObject, &activeObject, &clickedObject](int id, const rect& rect, Object* object)
    {
        if (emitInteractiveArea(id, rect, !!object->AsNode()))
            clickedObject = object;

        if (!hotObject && ImGui::IsItemHoveredRect())
            hotObject = object;

        if (ImGui::IsItemActive())
            activeObject = object;
    };

    // Process live nodes and pins.
    for (auto nodeIt = Nodes.rbegin(), nodeItEnd = Nodes.rend(); nodeIt != nodeItEnd; ++nodeIt)
    {
        auto node = *nodeIt;

        if (!node->IsLive) continue;

        // Check for interactions with live pins in node before
        // processing node itself. Pins does not overlap each other
        // and all are within node bounds.
        for (auto pin = node->LastPin; pin; pin = pin->PreviousPin)
        {
            if (!pin->IsLive) continue;

            checkInteractionsInArea(pin->ID, pin->Bounds, pin);
        }

        // Check for interactions with node.
        checkInteractionsInArea(node->ID, node->Bounds, node);
    }

    // Links are not regular widgets and must be done manually since
    // ImGui does not support interactive elements with custom hit maps.
    //
    // Links can steal input from background.

    // Links are just over background. So if anything else
    // is hovered we can skip them.
    if (nullptr == hotObject)
        hotObject = FindLinkAt(mousePos);

    const auto editorRect = rect(to_point(ImGui::GetWindowPos()), to_size(ImGui::GetWindowSize()));

    // Check for interaction with background.
    auto backgroundClicked  = emitInteractiveArea(0, editorRect, false);
    auto isBackgroundActive = ImGui::IsItemActive();
    auto isBackgroundHot    = !hotObject && ImGui::IsItemHoveredRect();

    // Process link input using background interactions.
    auto hotLink = hotObject ? hotObject->AsLink() : nullptr;

    // ImGui take care of tracking active items. With link
    // we must do this ourself.
    if (isBackgroundActive && hotLink && !LastActiveLink)
        LastActiveLink = hotLink;
    if (isBackgroundActive && LastActiveLink)
    {
        activeObject       = LastActiveLink;
        isBackgroundActive = false;
    }
    else if (!isBackgroundActive && LastActiveLink)
        LastActiveLink = nullptr;

    // Steal click from backgrounds if link is hovered.
    if (backgroundClicked && hotLink)
    {
        clickedObject     = hotLink;
        backgroundClicked = false;
    }

    return Control(hotObject, activeObject, clickedObject,
        isBackgroundHot, isBackgroundActive, backgroundClicked);
}