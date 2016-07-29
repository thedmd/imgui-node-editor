#include "Editor.h"
#include "Backend/imgui_impl_dx11.h"
#include "Drawing.h"
#include <string>
#include <fstream>


//------------------------------------------------------------------------------
namespace ed = ax::Editor;


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
ed::Context::Context():
    Nodes(),
    CurrentNode(nullptr),
    CurrentNodeStage(NodeStage::Invalid),
    ActiveNode(nullptr),
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

    for (auto node : Nodes)
        delete node;

    Nodes.clear();
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

    ImGui::BeginChild(id, ImVec2(0, 0), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove);
}

void ed::Context::End()
{
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

    ImGui::PushID(id);

    SetCurrentNode(node);

    // Position node on screen
    if (ActiveNode == node)
        ImGui::SetCursorScreenPos(to_imvec(node->Bounds.location) + DragOffset); // drag, find a better way
    else
        ImGui::SetCursorScreenPos(to_imvec(node->Bounds.location));

    auto drawList = ImGui::GetWindowDrawList();
    drawList->ChannelsSplit(2);
    drawList->ChannelsSetCurrent(1);

    SetNodeStage(NodeStage::Begin);
}

void ed::Context::EndNode()
{
    assert(nullptr != CurrentNode);

    SetNodeStage(NodeStage::End);

    if (CurrentNode->Bounds != NodeRect)
    {
        MarkSettingsDirty();

        // TODO: update layout
        //ImGui::Text((std::string("Changax::Editor: ") + std::to_string(CurrentNode->ID)).c_str());
    }

    ImGui::PopID();

    auto drawList = ImGui::GetWindowDrawList();
    drawList->ChannelsSetCurrent(0);

    const float nodeRounding = 12.0f;

    ax::Drawing::DrawHeader(drawList, HeaderTextureID,
        to_imvec(HeaderRect.top_left()),
        to_imvec(HeaderRect.bottom_right()),
        ImColor(255, 255, 255, 128), nodeRounding, 4.0f);

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

//     if (!SelectedRect.is_empty())
//     {
//         drawList->AddRectFilled(
//             to_imvec(selectedRect.top_left()),
//             to_imvec(selectedRect.bottom_right()),
//             ImColor(60, 180, 255, 100), 4.0f);
// 
//         drawList->AddCircleFilled(
//             ImVec2(selectedRect.right() + selectedRect.h * 0.25f, (float)selectedRect.center_y()),
//             selectedRect.h * 0.25f, ImColor(255, 255, 255, 200));
//     }

    drawList->ChannelsMerge();


    ImGui::SetCursorScreenPos(to_imvec(NodeRect.location));
    ImGui::InvisibleButton(std::to_string(CurrentNode->ID).c_str(), to_imvec(NodeRect.size));

    //if (!CurrentNodeIsNew)
    {
        if (ImGui::IsItemActive())
        {
            SetActiveNode(CurrentNode);
            DragOffset = ImGui::GetMouseDragDelta(0, 0.0f);
        }
        else if (ActiveNode == CurrentNode)
        {
            ActiveNode->Bounds.location += to_point(DragOffset);
            SetActiveNode(nullptr);
        }
    }

    SetCurrentNode(nullptr);

    SetNodeStage(NodeStage::Invalid);
}

void ed::Context::BeginHeader()
{
    assert(nullptr != CurrentNode);

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

    if (CurrentNodeStage == NodeStage::Begin)
        SetNodeStage(NodeStage::Content);

    SetNodeStage(NodeStage::Input);

    ImGui::BeginHorizontal(id);
}

void ed::Context::EndInput()
{
    ImGui::EndHorizontal();
    ImGui::Spring(0);
}

void ed::Context::BeginOutput(int id)
{
    assert(nullptr != CurrentNode);

    if (CurrentNodeStage == NodeStage::Begin)
        SetNodeStage(NodeStage::Content);

    if (CurrentNodeStage == NodeStage::Begin)
        SetNodeStage(NodeStage::Input);

    SetNodeStage(NodeStage::Output);

    ImGui::BeginHorizontal(id);
}

void ed::Context::EndOutput()
{
    ImGui::EndHorizontal();
    ImGui::Spring(0);
}

ed::Node* ed::Context::FindNode(int id)
{
    for (auto node : Nodes)
        if (node->ID == id)
            return node;

    return nullptr;
}

ed::Node* ed::Context::CreateNode(int id)
{
    assert(nullptr == FindNode(id));
    Nodes.push_back(new Node(id));
    auto node = Nodes.back();

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

void ed::Context::DestroyNode(Node* node)
{
    if (!node) return;
    auto& nodes = Nodes;
    auto nodeIt = std::find(nodes.begin(), nodes.end(), node);
    assert(nodeIt != nodes.end());
    nodes.erase(nodeIt);
    delete node;
}

void ed::Context::SetCurrentNode(Node* node, bool isNew/* = false*/)
{
    CurrentNode = node;
}

void ed::Context::SetActiveNode(Node* node)
{
    ActiveNode = node;
}

bool ed::Context::SetNodeStage(NodeStage stage)
{
    if (stage == CurrentNodeStage)
        return false;

    auto oldStage = CurrentNodeStage;
    CurrentNodeStage = stage;

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

