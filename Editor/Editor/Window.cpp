#include "Window.h"
#include <string>
#include <vector>
#include <algorithm>
#include <utility>
#include "Types.h"
#include "Types.inl"
#include "Backend/imgui_impl_dx11.h"
#include "ImGuiInterop.h"
#include "EditorApi.h"
#include "Widgets.h"

namespace ed = ax::Editor;

using namespace ax;
using namespace ax::ImGuiInterop;

using ax::Widgets::IconType;


const int   PortIconSize = 16;
const float JoinBezierStrength = 50;

enum class PinType
{
    Flow,
    Bool,
    Int,
    Float,
    Object,
    Function,
};

enum class PinLayout
{
    Left,
    Right
};

struct Node;

struct Pin
{
    int         ID;
    Node*       Node;
    std::string Name;
    PinType     Type;

    Pin(int id, const char* name, PinType type):
        ID(id), Node(nullptr), Name(name), Type(type)
    {
    }
};

struct Node
{
    int ID;
    std::string Name;
    std::vector<Pin> Inputs;
    std::vector<Pin> Outputs;
    ImColor Color;

    Node(int id, const char* name, ImColor color = ImColor(255, 255, 255)):
        ID(id), Name(name), Color(color)
    {
    }
};

struct Link
{
    int ID;

    int StartPinID;
    int EndPinID;

    Link(int id, int startPinId, int endPinId):
        ID(id), StartPinID(startPinId), EndPinID(endPinId)
    {
    }
};

static std::vector<Node>    s_Nodes;
static std::vector<Link>    s_Links;

static ImTextureID s_Background = nullptr;

NodeWindow::NodeWindow(void):
    m_Editor(nullptr)
{
    SetTitle("Node editor");

    s_Background = ImGui_LoadTexture("../Data/BlueprintBackground.png");

    m_Editor = ed::CreateEditor();

    int nextId = 1;
    auto genId = [&nextId]() { return nextId++; };

    s_Nodes.emplace_back(genId(), "InputAction Fire", ImColor(255, 128, 128));
    s_Nodes.back().Outputs.emplace_back(genId(), "Pressed", PinType::Flow);
    s_Nodes.back().Outputs.emplace_back(genId(), "Released", PinType::Flow);

    s_Nodes.emplace_back(genId(), "Branch");
    s_Nodes.back().Inputs.emplace_back(genId(), "", PinType::Flow);
    s_Nodes.back().Inputs.emplace_back(genId(), "Condition", PinType::Bool);
    s_Nodes.back().Outputs.emplace_back(genId(), "True", PinType::Flow);
    s_Nodes.back().Outputs.emplace_back(genId(), "False", PinType::Flow);

    s_Nodes.emplace_back(genId(), "Do N");
    s_Nodes.back().Inputs.emplace_back(genId(), "Enter", PinType::Flow);
    s_Nodes.back().Inputs.emplace_back(genId(), "N", PinType::Int);
    s_Nodes.back().Inputs.emplace_back(genId(), "Reset", PinType::Flow);
    s_Nodes.back().Outputs.emplace_back(genId(), "Exit", PinType::Flow);
    s_Nodes.back().Outputs.emplace_back(genId(), "Counter", PinType::Int);

    s_Nodes.emplace_back(genId(), "OutputAction");
    s_Nodes.back().Inputs.emplace_back(genId(), "Sample", PinType::Float);

    s_Nodes.emplace_back(genId(), "Set Timer", ImColor(128, 195, 248));
    s_Nodes.back().Inputs.emplace_back(genId(), "", PinType::Flow);
    s_Nodes.back().Inputs.emplace_back(genId(), "Object", PinType::Object);
    s_Nodes.back().Inputs.emplace_back(genId(), "Function Name", PinType::Function);
    s_Nodes.back().Inputs.emplace_back(genId(), "Time", PinType::Float);
    s_Nodes.back().Inputs.emplace_back(genId(), "Looping", PinType::Bool);
    s_Nodes.back().Outputs.emplace_back(genId(), "", PinType::Flow);


//     s_Nodes.emplace_back(genId(), "Single Line Trace by Channel", point(600, 30));
//     s_Nodes.back().Inputs.emplace_back(genId(), "", PinType::Flow);
//     s_Nodes.back().Inputs.emplace_back(genId(), "Start", PinType::Flow);
//     s_Nodes.back().Inputs.emplace_back(genId(), "End", PinType::Int);
//     s_Nodes.back().Inputs.emplace_back(genId(), "Trace Channel", PinType::Float);
//     s_Nodes.back().Inputs.emplace_back(genId(), "Trace Complex", PinType::Bool);
//     s_Nodes.back().Inputs.emplace_back(genId(), "Actors to Ignore", PinType::Int);
//     s_Nodes.back().Inputs.emplace_back(genId(), "Draw Debug Type", PinType::Bool);
//     s_Nodes.back().Inputs.emplace_back(genId(), "Ignore Self", PinType::Bool);
//     s_Nodes.back().Outputs.emplace_back(genId(), "", PinType::Flow);
//     s_Nodes.back().Outputs.emplace_back(genId(), "Out Hit", PinType::Float);
//     s_Nodes.back().Outputs.emplace_back(genId(), "Return Value", PinType::Bool);

    for (auto& node : s_Nodes)
    {
        for (auto& input : node.Inputs)
            input.Node = &node;

        for (auto& output : node.Outputs)
            output.Node = &node;
    }

//     s_Links.emplace_back(genId(), s_Nodes[0].Outputs[0].ID, s_Nodes[1].Inputs[0].ID);
//     s_Links.emplace_back(genId(), s_Nodes[1].Outputs[0].ID, s_Nodes[2].Inputs[0].ID);
//     s_Links.emplace_back(genId(), s_Nodes[1].Outputs[1].ID, s_Nodes[2].Inputs[2].ID);
//     s_Links.emplace_back(genId(), s_Nodes[2].Outputs[1].ID, s_Nodes[3].Inputs[0].ID);
}

NodeWindow::~NodeWindow()
{
    if (m_Editor)
    {
        ed::DestroyEditor(m_Editor);
        m_Editor = nullptr;
    }

    if (s_Background)
    {
        ImGui_DestroyTexture(s_Background);
        s_Background = nullptr;
    }
}

void Dummy();

static void DrawItemRect(ImColor color, float expand = 0.0f)
{
    ImGui::GetWindowDrawList()->AddRect(
        ImGui::GetItemRectMin() - ImVec2(expand, expand),
        ImGui::GetItemRectMax() + ImVec2(expand, expand),
        color);
};

static void FillItemRect(ImColor color, float expand = 0.0f)
{
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImGui::GetItemRectMin() - ImVec2(expand, expand),
        ImGui::GetItemRectMax() + ImVec2(expand, expand),
        color);
};

void NodeWindow::OnGui()
{
    auto& io = ImGui::GetIO();

    ImGui::Text("FPS: %.2f", io.Framerate);
    ImGui::Separator();

    static float iconWidth = 24;

    auto iconSize = ImVec2(iconWidth, iconWidth);

    static float scale = 1.77f;
    static int s_ActiveNodeId = -1;

    ImGui::DragFloat("Scale", &scale, 0.01f, 0.1f, 8.0f);
    auto iconSize2 = size(roundi(PortIconSize * scale), roundi(PortIconSize * scale));
    ImGui::Text("size: %d", iconSize2.w);

    static auto portIconSize = PortIconSize;
    portIconSize = iconSize2.w;


    ed::SetCurrentEditor(m_Editor);


    auto& style = ImGui::GetStyle();

    auto drawPinIcon = [iconSize2](const Pin& pin)
    {
        IconType iconType;
        ImColor  color;
        switch (pin.Type)
        {
            case PinType::Flow:     iconType = IconType::Flow;   color = ImColor(255, 255, 255); break;
            case PinType::Bool:     iconType = IconType::Circle; color = ImColor(139,   0,   0); break;
            case PinType::Int:      iconType = IconType::Circle; color = ImColor( 68, 201, 156); break;
            case PinType::Float:    iconType = IconType::Circle; color = ImColor(147, 226,  74); break;
            case PinType::Object:   iconType = IconType::Circle; color = ImColor( 51, 150, 215); break;
            case PinType::Function: iconType = IconType::Circle; color = ImColor(218,   0, 183); break;
            default:
                return;
        }

        ax::Widgets::Icon(to_imvec(iconSize2), iconType, false, color, ImColor(32, 32, 32));
    };


    ed::Begin("Node editor");
    {
        auto cursorTopLeft = ImGui::GetCursorScreenPos();

        //std::stable_sort(s_Nodes.begin(), s_Nodes.end(), [](const Node& lhs, const Node& rhs)
        //{
        //    if (rhs.ID == s_ActiveNodeId)
        //        return true;
        //    else
        //        return false;
        //});

        for (auto& node : s_Nodes)
        {
            ed::BeginNode(node.ID);
                ed::BeginHeader(node.Color);
                    ImGui::Spring(0);
                    ImGui::TextUnformatted(node.Name.c_str());
                    ImGui::Spring(1);
                    ImGui::Dummy(ImVec2(0, 28));
                    ImGui::Spring(0);
                ed::EndHeader();

                for (auto& input : node.Inputs)
                {
                    ed::BeginInput(input.ID);
                    drawPinIcon(input);
                    ImGui::Spring(0);
                    if (!input.Name.empty())
                    {
                        ImGui::TextUnformatted(input.Name.c_str());
                        ImGui::Spring(0);
                    }
                    ed::EndInput();
                }

                for (auto& output : node.Outputs)
                {
                    ed::BeginOutput(output.ID);
                    if (!output.Name.empty())
                    {
                        ImGui::Spring(0);
                        ImGui::TextUnformatted(output.Name.c_str());
                    }
                    ImGui::Spring(0);
                    drawPinIcon(output);
                    ed::EndOutput();

                    //ed::Icon("##icon", iconSizeIm, IconType::Flow, false);
                }

            ed::EndNode();
        }

        ImGui::SetCursorScreenPos(cursorTopLeft);
        //ImGui::Text("IsAnyItemActive: %d", ImGui::IsAnyItemActive() ? 1 : 0);

        //ImGui::SetCursorScreenPos(cursorTopLeft + ImVec2(400, 400));


        //ImVec2 iconOrigin(300, 200);
        //DrawIcon(ax::rect(to_point(iconOrigin),                                                     iconSize2), IconType::Flow,        false, ImColor(255, 255, 255), ImColor(255, 0, 0));
        //DrawIcon(ax::rect(to_point(iconOrigin) + point(roundi(     0 * scale), roundi(28 * scale)), iconSize2), IconType::Flow,        true,  ImColor(255, 255, 255), ImColor(255, 0, 0));
        //DrawIcon(ax::rect(to_point(iconOrigin) + point(roundi(    28 * scale), roundi( 0 * scale)), iconSize2), IconType::Circle,      false, ImColor(  0, 255, 255), ImColor(255, 0, 0));
        //DrawIcon(ax::rect(to_point(iconOrigin) + point(roundi(    28 * scale), roundi(28 * scale)), iconSize2), IconType::Circle,      true,  ImColor(  0, 255, 255), ImColor(255, 0, 0));
        //DrawIcon(ax::rect(to_point(iconOrigin) + point(roundi(2 * 28 * scale), roundi( 0 * scale)), iconSize2), IconType::Square,      false, ImColor(128, 255, 128), ImColor(255, 0, 0));
        //DrawIcon(ax::rect(to_point(iconOrigin) + point(roundi(2 * 28 * scale), roundi(28 * scale)), iconSize2), IconType::Square,      true,  ImColor(128, 255, 128), ImColor(255, 0, 0));
        //DrawIcon(ax::rect(to_point(iconOrigin) + point(roundi(3 * 28 * scale), roundi( 0 * scale)), iconSize2), IconType::Grid,        false, ImColor(128, 255, 128), ImColor(255, 0, 0));
        //DrawIcon(ax::rect(to_point(iconOrigin) + point(roundi(3 * 28 * scale), roundi(28 * scale)), iconSize2), IconType::Grid,        true,  ImColor(128, 255, 128), ImColor(255, 0, 0));
        //DrawIcon(ax::rect(to_point(iconOrigin) + point(roundi(4 * 28 * scale), roundi( 0 * scale)), iconSize2), IconType::RoundSquare, false, ImColor(255, 128, 128), ImColor(255, 0, 0));
        //DrawIcon(ax::rect(to_point(iconOrigin) + point(roundi(4 * 28 * scale), roundi(28 * scale)), iconSize2), IconType::RoundSquare, true,  ImColor(255, 128, 128), ImColor(255, 0, 0));
        //DrawIcon(ax::rect(to_point(iconOrigin) + point(roundi(5 * 28 * scale), roundi( 0 * scale)), iconSize2), IconType::Diamond,     false, ImColor(255, 255, 128), ImColor(255, 0, 0));
        //DrawIcon(ax::rect(to_point(iconOrigin) + point(roundi(5 * 28 * scale), roundi(28 * scale)), iconSize2), IconType::Diamond,     true,  ImColor(255, 255, 128), ImColor(255, 0, 0));

        //int tw = ImGui_GetTextureWidth(s_Background);
        //int th = ImGui_GetTextureHeight(s_Background);
        //ImGui::Image(s_Background, ImVec2((float)tw, (float)th));

        auto drawHeader = [&](ImVec2 topLeft, ImVec2 rightBottom, ImColor color = ImColor(1,1,1))
        {
//             drawList->AddDrawCmd();
//             drawList->PathRect(topLeft, rightBottom, 12.0f, 1 | 2);
//             drawList->PathFill(ImColor(color));
// 
//             auto size = rightBottom - topLeft;
// 
//             float zoom = 4.0f;
//             ax::matrix transform;
//             transform.scale(1.0f / zoom, 1.0f / zoom);
//             transform.scale(size.x / tw, size.y / th);
//             transform.scale(1.0f / (size.x - 1.0f), 1.0f / (size.y - 1.0f));
//             transform.translate(-topLeft.x - 0.5f, -topLeft.y - 0.5f);
// 
//             ImGui_PushGizmo(drawList, s_Background, (float*)&transform);
//             drawList->AddDrawCmd();
// 
            //drawList->PathRect(topLeft, rightBottom, 12.0f, 1 | 2);
            //drawList->PathStroke(ImColor(128, 128, 128), true, 1.5f);
        };

        auto get_item_bounds = [] { return rect(to_point(ImGui::GetItemRectMin()), to_point(ImGui::GetItemRectMax())); };

        ax::rect selectedRect = ax::rect();
        auto checkFocus = [&selectedRect, &get_item_bounds]() { if (ImGui::IsItemHoveredRect()) selectedRect = get_item_bounds(); };

        ImGui::SetCursorScreenPos(ImVec2(400, 300));

        /*
        auto nodeTopLeft = ImGui::GetCursorScreenPos();

        drawList->ChannelsSplit(2);

        drawList->ChannelsSetCurrent(1);
        ImGui::BeginVertical("SampleNode");
            ImGui::BeginHorizontal("header");
                ImGui::Spring(0);
                ImGui::Text("Do Once");
                ImGui::Spring(1);
                ImGui::Dummy(ImVec2(0, 28));
            ImGui::EndHorizontal();
            auto headerRect = get_item_bounds();

            ImGui::Spring(0);
            drawList->ChannelsSetCurrent(1);
            ImGui::BeginHorizontal("content");
                ImGui::Spring(1);
                ImGui::BeginVertical("inputs", ImVec2(0,0), 0.0f);
                    ImGui::BeginHorizontal(0);
                        DrawIcon(to_size(iconSizeIm), ax::Editor::IconType::Flow, true, ImColor(255, 255, 255));
                    ImGui::EndHorizontal();
                    checkFocus();
                    ImGui::Spring(0, 0);
                    ImGui::BeginHorizontal(1);
                        DrawIcon(to_size(iconSizeIm), ax::Editor::IconType::Flow, true, ImColor(255, 255, 255));
                        ImGui::Spring(0);
                        ImGui::TextUnformatted("Reset");
                        ImGui::Spring(0);
                    ImGui::EndHorizontal();
                    checkFocus();
                    ImGui::Spring(0, 0);
                    ImGui::BeginHorizontal(2);
                        DrawIcon(to_size(iconSizeIm), ax::Editor::IconType::Circle, false, ImColor(128, 0, 0));
                        ImGui::Spring(0);
                        ImGui::TextUnformatted("Start Closed");
                        ImGui::Spring(0);
                        ImGui::Spring(0);
                        static bool x = false;
                        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0,0));
                        ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(0, 0));
                        ImGui::Checkbox("", &x);
                        ImGui::PopStyleVar(2);
                        ImGui::Spring(0);
                    ImGui::EndHorizontal();
                    checkFocus();
                    ImGui::Spring(1, 0);
                ImGui::EndVertical();
                ImGui::Spring(1);
                ImGui::BeginVertical("outputs", ImVec2(0, 0), 1.0f);
                    ImGui::BeginHorizontal(0);
                        ImGui::Spring(0);
                        ImGui::TextUnformatted("Completed");
                        ImGui::Spring(0);
                        DrawIcon(to_size(iconSizeIm), ax::Editor::IconType::Flow, false, ImColor(255, 255, 255), ImColor(0, 0, 0));
                    ImGui::EndHorizontal();
                    checkFocus();
                    ImGui::Spring(1,0);
                ImGui::EndVertical();
                ImGui::Spring(1);
            ImGui::EndHorizontal();
            auto contentRect = get_item_bounds();

            ImGui::Spring(0);

        ImGui::EndVertical();
        auto nodeRect = get_item_bounds();

        drawList->ChannelsSetCurrent(0);
        drawHeader(
            to_imvec(headerRect.top_left()),
            to_imvec(headerRect.bottom_right()),
            ImColor(255, 255, 255, 128));

        auto headerSeparatorRect      = ax::rect(headerRect.bottom_left(),  contentRect.top_right());
        auto footerSeparatorRect      = ax::rect(contentRect.bottom_left(), nodeRect.bottom_right());
        auto contentWithSeparatorRect = ax::rect::make_union(headerSeparatorRect, footerSeparatorRect);

        drawList->AddRectFilled(
            to_imvec(contentWithSeparatorRect.top_left()),
            to_imvec(contentWithSeparatorRect.bottom_right()),
            ImColor(32, 32, 32, 200), 12.0f, 4 | 8);

        drawList->AddLine(
            to_imvec(headerSeparatorRect.top_left()  + point( 1, -1)),
            to_imvec(headerSeparatorRect.top_right() + point(-1, -1)),
            ImColor(255, 255, 255, 96 / 3), 1.0f);

        drawList->AddRect(
            to_imvec(nodeRect.top_left()),
            to_imvec(nodeRect.bottom_right()),
            ImColor(255, 255, 255, 96), 12.0f, 15, 1.5f);

        if (!selectedRect.is_empty())
        {
            drawList->AddRectFilled(
                to_imvec(selectedRect.top_left()),
                to_imvec(selectedRect.bottom_right()),
                ImColor(60, 180, 255, 100), 4.0f);

            drawList->AddCircleFilled(
                ImVec2(selectedRect.right() + selectedRect.h * 0.25f, (float)selectedRect.center_y()),
                selectedRect.h * 0.25f, ImColor(255, 255, 255, 200));
        }

        drawList->ChannelsMerge();
        */
    }
    ed::End();

    //    //ImGui::ShowMetricsWindow();
//
//    //static bool show = true;
//    //ImGui::ShowTestWindow(&show);
}

void Dummy()
{
    // Dummy
    struct Node
    {
        int     ID;
        char    Name[32];
        ImVec2  Pos, size;
        float   Value;
        ImVec4  Color;
        int     InputsCount, OutputsCount;

        Node(int id, const char* name, const ImVec2& pos, float value, const ImVec4& color, int inputs_count, int outputs_count) { ID = id; strncpy(Name, name, 31); Name[31] = 0; Pos = pos; Value = value; Color = color; InputsCount = inputs_count; OutputsCount = outputs_count; }

        ImVec2 GetInputSlotPos(int slot_no) const   { return ImVec2(Pos.x, Pos.y + size.y * ((float)slot_no+1) / ((float)InputsCount+1)); }
        ImVec2 GetOutputSlotPos(int slot_no) const  { return ImVec2(Pos.x + size.x, Pos.y + size.y * ((float)slot_no+1) / ((float)OutputsCount+1)); }
    };
    struct NodeLink
    {
        int     InputIdx, InputSlot, OutputIdx, OutputSlot;

        NodeLink(int input_idx, int input_slot, int output_idx, int output_slot) { InputIdx = input_idx; InputSlot = input_slot; OutputIdx = output_idx; OutputSlot = output_slot; }
    };

    static std::vector<Node> nodes;
    static std::vector<NodeLink> links;
    static bool inited = false;
    static ImVec2 scrolling = ImVec2(0.0f, 0.0f);
    static bool show_grid = true;
    static int node_selected = -1;
    if (!inited)
    {
        nodes.push_back(Node(0, "MainTex",  ImVec2(40,50), 0.5f, ImColor(255,100,100), 1, 1));
        nodes.push_back(Node(1, "BumpMap",  ImVec2(40,150), 0.42f, ImColor(200,100,200), 1, 1));
        nodes.push_back(Node(2, "Combine", ImVec2(270,80), 1.0f, ImColor(0,200,100), 2, 2));
        links.push_back(NodeLink(0, 0, 2, 0));
        links.push_back(NodeLink(1, 0, 2, 1));
        inited = true;
    }

    // Draw a list of nodes on the left side
    bool open_context_menu = false;
    int node_hovered_in_list = -1;
    int node_hovered_in_scene = -1;
    ImGui::BeginChild("node_list", ImVec2(100,0));
    ImGui::Text("Nodes");
    ImGui::Separator();
    for (int node_idx = 0; node_idx < (int)nodes.size(); node_idx++)
    {
        Node* node = &nodes[node_idx];
        ImGui::PushID(node->ID);
        if (ImGui::Selectable(node->Name, node->ID == node_selected))
            node_selected = node->ID;
        if (ImGui::IsItemHovered())
        {
            node_hovered_in_list = node->ID;
            open_context_menu |= ImGui::IsMouseClicked(1);
        }
        ImGui::PopID();
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginGroup();

    const float NODE_SLOT_RADIUS = 4.0f;
    const ImVec2 NODE_WINDOW_PADDING(8.0f, 8.0f);

    // Create our child canvas
    ImGui::Text("Hold middle mouse button to scroll (%.2f,%.2f)", scrolling.x, scrolling.y);
    ImGui::SameLine(ImGui::GetWindowWidth()-180);
    ImGui::Checkbox("Show grid", &show_grid);
    auto mp = ImGui::GetMousePos();
    ImGui::SameLine(ImGui::GetWindowWidth() - 480);
    ImGui::Text("X: %f Y: %f", mp.x, mp.y);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(1,1));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0,0));
    ImGui::PushStyleColor(ImGuiCol_ChildWindowBg, ImColor(60,60,70,200));
    ImGui::BeginChild("scrolling_region", ImVec2(0,0), true, ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoMove);
    ImGui::PushItemWidth(120.0f);

    ImVec2 offset = ImGui::GetCursorScreenPos() - scrolling;
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->ChannelsSplit(2);

    // Display grid
    if (show_grid)
    {
        ImU32 GRID_COLOR = ImColor(120,120,120,40);
        float GRID_SZ = 32.0f;
        ImVec2 win_pos = ImGui::GetCursorScreenPos();
        ImVec2 canvas_sz = ImGui::GetWindowSize();
        for (float x = fmodf(offset.x,GRID_SZ); x < canvas_sz.x; x += GRID_SZ)
            draw_list->AddLine(ImVec2(x,0.0f)+win_pos, ImVec2(x,canvas_sz.y)+win_pos, GRID_COLOR);
        for (float y = fmodf(offset.y,GRID_SZ); y < canvas_sz.y; y += GRID_SZ)
            draw_list->AddLine(ImVec2(0.0f,y)+win_pos, ImVec2(canvas_sz.x,y)+win_pos, GRID_COLOR);
    }

    // Display links
    draw_list->ChannelsSetCurrent(0); // Background
    for (int link_idx = 0; link_idx < (int)links.size(); link_idx++)
    {
        NodeLink* link = &links[link_idx];
        Node* node_inp = &nodes[link->InputIdx];
        Node* node_out = &nodes[link->OutputIdx];
        ImVec2 p1 = offset + node_inp->GetOutputSlotPos(link->InputSlot);
        ImVec2 p2 = offset + node_out->GetInputSlotPos(link->OutputSlot);
        draw_list->AddBezierCurve(p1, p1+ImVec2(+50,0), p2+ImVec2(-50,0), p2, ImColor(200,200,100), 3.0f);
    }

    // Display nodes
    for (int node_idx = 0; node_idx < (int)nodes.size(); node_idx++)
    {
        Node* node = &nodes[node_idx];
        ImGui::PushID(node->ID);
        ImVec2 node_rect_min = offset + node->Pos;

        // Display node contents first
        draw_list->ChannelsSetCurrent(1); // Foreground
        bool old_any_active = ImGui::IsAnyItemActive();
        ImGui::SetCursorScreenPos(node_rect_min + NODE_WINDOW_PADDING);
        ImGui::BeginGroup(); // Lock horizontal position
        ImGui::Text("%s", node->Name);
        ImGui::SliderFloat("##value", &node->Value, 0.0f, 1.0f, "Alpha %.2f");
        ImGui::ColorEdit3("##color", &node->Color.x);
        ImGui::EndGroup();

        // Save the size of what we have emitted and whether any of the widgets are being used
        bool node_widgets_active = (!old_any_active && ImGui::IsAnyItemActive());
        node->size = ImGui::GetItemRectSize() + NODE_WINDOW_PADDING + NODE_WINDOW_PADDING;
        ImVec2 node_rect_max = node_rect_min + node->size;

        // Display node box
        draw_list->ChannelsSetCurrent(0); // Background
        ImGui::SetCursorScreenPos(node_rect_min);
        ImGui::InvisibleButton("node", node->size);
        if (ImGui::IsItemHovered())
        {
            node_hovered_in_scene = node->ID;
            open_context_menu |= ImGui::IsMouseClicked(1);
        }
        bool node_moving_active = ImGui::IsItemActive();
        if (node_widgets_active || node_moving_active)
            node_selected = node->ID;
        if (node_moving_active && ImGui::IsMouseDragging(0))
            node->Pos = node->Pos + ImGui::GetIO().MouseDelta;

        ImU32 node_bg_color = (node_hovered_in_list == node->ID || node_hovered_in_scene == node->ID || (node_hovered_in_list == -1 && node_selected == node->ID)) ? ImColor(75,75,75) : ImColor(60,60,60);
        draw_list->AddRectFilled(node_rect_min, node_rect_max, node_bg_color, 3.0f);
        draw_list->AddRect(node_rect_min, node_rect_max, ImColor(100,100,100), 3.0f);
        for (int slot_idx = 0; slot_idx < node->InputsCount; slot_idx++)
            draw_list->AddCircleFilled(offset + node->GetInputSlotPos(slot_idx), NODE_SLOT_RADIUS, ImColor(150,150,150,150));
        for (int slot_idx = 0; slot_idx < node->OutputsCount; slot_idx++)
            draw_list->AddCircleFilled(offset + node->GetOutputSlotPos(slot_idx), NODE_SLOT_RADIUS, ImColor(150,150,150,150));

        ImGui::PopID();
    }
    draw_list->ChannelsMerge();

    // Open context menu
    if (!ImGui::IsAnyItemHovered() && ImGui::IsMouseHoveringWindow() && ImGui::IsMouseClicked(1))
    {
        node_selected = node_hovered_in_list = node_hovered_in_scene = -1;
        open_context_menu = true;
    }
    if (open_context_menu)
    {
        ImGui::OpenPopup("context_menu");
        if (node_hovered_in_list != -1)
            node_selected = node_hovered_in_list;
        if (node_hovered_in_scene != -1)
            node_selected = node_hovered_in_scene;
    }

    // Draw context menu
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8,8));
    if (ImGui::BeginPopup("context_menu"))
    {
        Node* node = node_selected != -1 ? &nodes[node_selected] : NULL;
        ImVec2 scene_pos = ImGui::GetMousePosOnOpeningCurrentPopup() - offset;
        if (node)
        {
            ImGui::Text("Node '%s'", node->Name);
            ImGui::Separator();
            if (ImGui::MenuItem("Rename..", NULL, false, false)) {}
            if (ImGui::MenuItem("Delete", NULL, false, false)) {}
            if (ImGui::MenuItem("Copy", NULL, false, false)) {}
        }
        else
        {
            if (ImGui::MenuItem("Add")) { nodes.push_back(Node((int)nodes.size(), "New node", scene_pos, 0.5f, ImColor(100,100,200), 2, 2)); }
            if (ImGui::MenuItem("Paste", NULL, false, false)) {}
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();

    // Scrolling
    if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemActive() && ImGui::IsMouseDragging(2, 0.0f))
        scrolling = scrolling - ImGui::GetIO().MouseDelta;

    ImGui::PopItemWidth();
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    ImGui::EndGroup();
}
