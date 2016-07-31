#include "Editor.h"
#include "Backend/imgui_impl_dx11.h"
#include "imgui/imgui_internal.h"
#include "Drawing.h"
#include <string>
#include <fstream>


//------------------------------------------------------------------------------
namespace ed = ax::Editor;


//------------------------------------------------------------------------------
static const int c_ChannelsPerNode       = 3;
static const int c_NodeBaseChannel       = 0;
static const int c_NodeBackgroundChannel = 1;
static const int c_NodeContentChannel    = 2;


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


//------------------------------------------------------------------------------
ed::Context::Context():
    Nodes(),
    Pins(),
    HotObject(nullptr),
    ActiveObject(nullptr),
    SelectedObject(nullptr),
    CurrentPin(nullptr),
    CurrentNode(nullptr),
    NodeBuildStage(NodeStage::Invalid),
    DragOffset(),
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

    for (auto pin : Pins)
        delete pin;

    for (auto node : Nodes)
        delete node;
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

    SetHotObject(nullptr);

    for (auto node : Nodes)
        node->IsLive = false;

    for (auto pin : Pins)
        pin->IsLive = false;

    ImGui::BeginChild(id, ImVec2(0, 0), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove);
}

void ed::Context::End()
{
    auto drawList = ImGui::GetWindowDrawList();

    drawList->ChannelsSetCurrent(0);

    Node* hotNode = nullptr;
    Node* activeNode = nullptr;
    Node* clickedNode = nullptr;
    for (auto nodeIt = Nodes.rbegin(), nodeItEnd = Nodes.rend(); nodeIt != nodeItEnd; ++nodeIt)
    {
        auto& node = *nodeIt;

        auto nodeId = "node_area_" + std::to_string(node->ID);
        ImGui::SetCursorScreenPos(to_imvec(node->Bounds.location));
        if (ImGui::InvisibleButton(nodeId.c_str(), to_imvec(node->Bounds.size)))
            clickedNode = node;

        if (!hotNode && ImGui::IsItemHoveredRect())
            hotNode = node;

        if (ImGui::IsItemActive())
            activeNode = node;
    }

    SetHotObject(hotNode);

    SetActiveObject(activeNode);

    if (clickedNode)
        SetSelectedObject(clickedNode);

    auto selectedNode = SelectedObject ? SelectedObject->AsNode() : nullptr;


    std::stable_sort(Nodes.begin(), Nodes.end(), [activeNode](Node* lhs, Node* rhs)
    {
        return (rhs == activeNode);
    });

    // Every node has few channels assigned. Grow channel list
    // to hold twice as much of channels and place them in
    // node drawing order.
    {
        // Reserve two additional channels for sorted list of channels
        auto nodeChannelCount = drawList->_ChannelsCount;
        ImDrawList_ChannelsGrow(drawList, (drawList->_ChannelsCount - 1) * c_ChannelsPerNode + 1);

        int targetChannel = nodeChannelCount;
        for (auto node : Nodes)
        {
            for (int i = 0; i < c_ChannelsPerNode; ++i)
                ImDrawList_SwapChannels(drawList, node->Channel + i, targetChannel + i);
            node->Channel = targetChannel;
            targetChannel += c_ChannelsPerNode;
        }
    }

    ImGui::SetCursorScreenPos(ImVec2(500, 200));
    ImGui::BeginGroup();
    ImGui::Text("ActiveObject:   %p", ActiveObject);
    ImGui::Text("HotObject:      %p", HotObject);
    ImGui::Text("SelectedObject: %p", SelectedObject);
    ImGui::Text("CurrentPin:     %p", CurrentPin);
    ImGui::Text("CurrentNode:    %p", CurrentNode);

    for (auto node : Nodes)
        ImGui::Text("   Node:    %d  %d  %p  ", node->ID, node->Channel, node);

    ImGui::EndGroup();

    //if (clickedNode == ActiveObject)
    //{
    //    DragOffset = ImGui::GetMouseDragDelta(0, 0.0f);
    //}
    //else if (ActiveObject == SelectedObject)
    //    SelectedObject->AsNode()->Bounds.location += to_point(DragOffset);

    //if (SelectedObject == CurrentNode)
    //{
    //    drawList->AddRect(
    //        to_imvec(NodeRect.top_left()),
    //        to_imvec(NodeRect.bottom_right()),
    //        ImColor(255, 176, 50, 255), 12.0f, 15, 3.5f);
    //}
    if (selectedNode)
    {
        drawList->ChannelsSetCurrent(selectedNode->Channel + c_NodeBaseChannel);

        drawList->AddRect(
            to_imvec(selectedNode->Bounds.top_left()),
            to_imvec(selectedNode->Bounds.bottom_right()),
            ImColor(255, 176, 50, 255), 12.0f, 15, 3.5f);
    }

    if (hotNode && hotNode != selectedNode)
    {
        drawList->ChannelsSetCurrent(hotNode->Channel + c_NodeBaseChannel);

        drawList->AddRect(
            to_imvec(hotNode->Bounds.top_left()),
            to_imvec(hotNode->Bounds.bottom_right()),
            ImColor(50, 176, 255, 255), 12.0f, 15, 3.5f);
    }

    //if (!PinRect.is_empty())
    //{
    //    drawList->AddRectFilled(
    //        to_imvec(PinRect.top_left()),
    //        to_imvec(PinRect.bottom_right()),
    //        ImColor(60, 180, 255, 100), 4.0f);

    //    //drawList->AddCircleFilled(
    //    //    ImVec2(PinRect.right() + PinRect.h * 0.25f, (float)PinRect.center_y()),
    //    //    PinRect.h * 0.25f, ImColor(255, 255, 255, 200));
    //}









    drawList->ChannelsMerge();

    ImGui::EndChild();

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
    if (ActiveObject == node)
        ImGui::SetCursorScreenPos(to_imvec(node->Bounds.location) + DragOffset); // drag, find a better way
    else
        ImGui::SetCursorScreenPos(to_imvec(node->Bounds.location));

    node->IsLive = true;
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

    if (CurrentNode->Bounds != NodeRect)
    {
        MarkSettingsDirty();

        CurrentNode->Bounds = NodeRect;
    }

    if (ImGui::IsItemHoveredRect())
    {
        auto hotPin = HotObject ? HotObject->AsPin() : nullptr;
        if (!hotPin || (hotPin && (hotPin->Node != CurrentNode)))
            SetHotObject(CurrentNode);
    }

    auto drawList = ImGui::GetWindowDrawList();

    auto drawListForegroundEnd = drawList->CmdBuffer.size();

    drawList->ChannelsSetCurrent(CurrentNode->Channel + c_NodeBackgroundChannel);

    const float nodeRounding = 12.0f;

    HeaderColor.Value.w = 0.75f;
    ax::Drawing::DrawHeader(drawList, HeaderTextureID,
        to_imvec(HeaderRect.top_left()),
        to_imvec(HeaderRect.bottom_right()),
        HeaderColor, nodeRounding, 4.0f);

    auto headerSeparatorRect      = ax::rect(HeaderRect.bottom_left(),  ContentRect.top_right());
    auto footerSeparatorRect      = ax::rect(ContentRect.bottom_left(), NodeRect.bottom_right());
    auto contentWithSeparatorRect = ax::rect::make_union(headerSeparatorRect, footerSeparatorRect);

    drawList->AddRectFilled(
        to_imvec(contentWithSeparatorRect.top_left()),
        to_imvec(contentWithSeparatorRect.bottom_right()),
        ImColor(32, 32, 32, 200), 12.0f, 4 | 8);

    drawList->AddLine(
        to_imvec(headerSeparatorRect.top_left() + point(1, -1)),
        to_imvec(headerSeparatorRect.top_right() + point(-1, -1)),
        ImColor(255, 255, 255, 96 / 3), 1.0f);

    drawList->AddRect(
        to_imvec(NodeRect.top_left()),
        to_imvec(NodeRect.bottom_right()),
        ImColor(255, 255, 255, 96), 12.0f, 15, 1.5f);

    SetCurrentNode(nullptr);

    SetNodeStage(NodeStage::Invalid);
}

void ed::Context::BeginHeader(const ImColor& color)
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

void ed::Context::DestroyObject(Node* node)
{
}

ed::Object* ed::Context::FindObject(int id)
{
    if (auto object = FindNode(id))
        return object;
    if (auto object = FindPin(id))
        return object;
    return nullptr;
}

ed::Node* ed::Context::FindNode(int id)
{
    for (auto node : Nodes)
        if (node->ID == id)
            return node;

    return nullptr;
}

ed::Pin* ed::Context::FindPin(int id)
{
    for (auto pin : Pins)
        if (pin->ID == id)
            return pin;

    return nullptr;
}

void ed::Context::SetHotObject(Object* object)
{
    HotObject = object;
}

void ed::Context::SetActiveObject(Object* object)
{
    ActiveObject = object;
}

void ed::Context::SetSelectedObject(Object* object)
{
    SelectedObject = object;
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
            HeaderRect = get_item_bounds();

            // spacing between header and content
            ImGui::Spring(0);

            break;

        case NodeStage::Content:
            break;

        case NodeStage::Input:
            //ImGui::Spring(0);
            ImGui::Spring(1);
            ImGui::EndVertical();
            //ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImColor(0, 255, 0, 32));
            break;

        case NodeStage::Output:
            //ImGui::Spring(0);
            ImGui::Spring(1);
            ImGui::EndVertical();
            //ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImColor(0, 255, 0, 32));
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
            ContentRect = get_item_bounds();

            //ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImColor(0, 0, 255, 32));
            //ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImColor(0, 0, 255, 64));

            ImGui::Spring(0);
            ImGui::EndVertical();
            NodeRect = get_item_bounds();
            //ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin() - ImVec2(2,2), ImGui::GetItemRectMax() + ImVec2(2, 2), ImColor(255, 255, 255));
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
    auto pin = GetPin(id, PinType::Input);
    pin->Node = CurrentNode;
    SetCurrentPin(pin);

    pin->PreviousPin     = CurrentNode->LastPin;
    CurrentNode->LastPin = pin;
    pin->IsLive = true;
}

void ed::Context::EndPin()
{
    auto pinRect = get_item_bounds();

    if (ImGui::IsItemHoveredRect())
        SetHotObject(CurrentPin);

    CurrentPin->Bounds = pinRect;

    SetCurrentPin(nullptr);
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

