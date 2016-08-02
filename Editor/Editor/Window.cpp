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

enum class PinKind
{
    Input,
    Output
};

struct Node;

struct Pin
{
    int         ID;
    Node*       Node;
    std::string Name;
    PinType     Type;
    PinKind     Kind;

    Pin(int id, const char* name, PinType type):
        ID(id), Node(nullptr), Name(name), Type(type), Kind(PinKind::Input)
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

    ImColor Color;

    Link(int id, int startPinId, int endPinId):
        ID(id), StartPinID(startPinId), EndPinID(endPinId), Color(255, 255, 255)
    {
    }
};

static std::vector<Node>    s_Nodes;
static std::vector<Link>    s_Links;

static int s_NextId = 1;
static int GetNextId()
{
    return s_NextId++;
}

static Pin* FindPin(int id)
{
    if (id <= 0)
        return nullptr;

    for (auto& node : s_Nodes)
    {
        for (auto& pin : node.Inputs)
            if (pin.ID == id)
                return &pin;

        for (auto& pin : node.Outputs)
            if (pin.ID == id)
                return &pin;
    }

    return nullptr;
}

static bool IsPinLinked(int id)
{
    if (id <= 0)
        return false;

    for (auto& link : s_Links)
        if (link.StartPinID == id || link.EndPinID == id)
            return true;

    return false;
}


NodeWindow::NodeWindow(void):
    m_Editor(nullptr)
{
    SetTitle("Node editor");

    m_Editor = ed::CreateEditor();

    s_Nodes.emplace_back(GetNextId(), "InputAction Fire", ImColor(255, 128, 128));
    s_Nodes.back().Outputs.emplace_back(GetNextId(), "Pressed", PinType::Flow);
    s_Nodes.back().Outputs.emplace_back(GetNextId(), "Released", PinType::Flow);

    s_Nodes.emplace_back(GetNextId(), "Branch");
    s_Nodes.back().Inputs.emplace_back(GetNextId(), "", PinType::Flow);
    s_Nodes.back().Inputs.emplace_back(GetNextId(), "Condition", PinType::Bool);
    s_Nodes.back().Outputs.emplace_back(GetNextId(), "True", PinType::Flow);
    s_Nodes.back().Outputs.emplace_back(GetNextId(), "False", PinType::Flow);

    s_Nodes.emplace_back(GetNextId(), "Do N");
    s_Nodes.back().Inputs.emplace_back(GetNextId(), "Enter", PinType::Flow);
    s_Nodes.back().Inputs.emplace_back(GetNextId(), "N", PinType::Int);
    s_Nodes.back().Inputs.emplace_back(GetNextId(), "Reset", PinType::Flow);
    s_Nodes.back().Outputs.emplace_back(GetNextId(), "Exit", PinType::Flow);
    s_Nodes.back().Outputs.emplace_back(GetNextId(), "Counter", PinType::Int);

    s_Nodes.emplace_back(GetNextId(), "OutputAction");
    s_Nodes.back().Inputs.emplace_back(GetNextId(), "Sample", PinType::Float);
    s_Nodes.back().Outputs.emplace_back(GetNextId(), "Condition", PinType::Bool);

    s_Nodes.emplace_back(GetNextId(), "Set Timer", ImColor(128, 195, 248));
    s_Nodes.back().Inputs.emplace_back(GetNextId(), "", PinType::Flow);
    s_Nodes.back().Inputs.emplace_back(GetNextId(), "Object", PinType::Object);
    s_Nodes.back().Inputs.emplace_back(GetNextId(), "Function Name", PinType::Function);
    s_Nodes.back().Inputs.emplace_back(GetNextId(), "Time", PinType::Float);
    s_Nodes.back().Inputs.emplace_back(GetNextId(), "Looping", PinType::Bool);
    s_Nodes.back().Outputs.emplace_back(GetNextId(), "", PinType::Flow);


//     s_Nodes.emplace_back(GetNextId(), "Single Line Trace by Channel", point(600, 30));
//     s_Nodes.back().Inputs.emplace_back(GetNextId(), "", PinType::Flow);
//     s_Nodes.back().Inputs.emplace_back(GetNextId(), "Start", PinType::Flow);
//     s_Nodes.back().Inputs.emplace_back(GetNextId(), "End", PinType::Int);
//     s_Nodes.back().Inputs.emplace_back(GetNextId(), "Trace Channel", PinType::Float);
//     s_Nodes.back().Inputs.emplace_back(GetNextId(), "Trace Complex", PinType::Bool);
//     s_Nodes.back().Inputs.emplace_back(GetNextId(), "Actors to Ignore", PinType::Int);
//     s_Nodes.back().Inputs.emplace_back(GetNextId(), "Draw Debug Type", PinType::Bool);
//     s_Nodes.back().Inputs.emplace_back(GetNextId(), "Ignore Self", PinType::Bool);
//     s_Nodes.back().Outputs.emplace_back(GetNextId(), "", PinType::Flow);
//     s_Nodes.back().Outputs.emplace_back(GetNextId(), "Out Hit", PinType::Float);
//     s_Nodes.back().Outputs.emplace_back(GetNextId(), "Return Value", PinType::Bool);

    for (auto& node : s_Nodes)
    {
        for (auto& input : node.Inputs)
        {
            input.Node = &node;
            input.Kind = PinKind::Input;
        }

        for (auto& output : node.Outputs)
        {
            output.Node = &node;
            output.Kind = PinKind::Output;
        }
    }

//     s_Links.emplace_back(GetNextId(), s_Nodes[0].Outputs[0].ID, s_Nodes[1].Inputs[0].ID);
//     s_Links.emplace_back(GetNextId(), s_Nodes[1].Outputs[0].ID, s_Nodes[2].Inputs[0].ID);
//     s_Links.emplace_back(GetNextId(), s_Nodes[1].Outputs[1].ID, s_Nodes[2].Inputs[2].ID);
//     s_Links.emplace_back(GetNextId(), s_Nodes[2].Outputs[1].ID, s_Nodes[3].Inputs[0].ID);
}

NodeWindow::~NodeWindow()
{
    if (m_Editor)
    {
        ed::DestroyEditor(m_Editor);
        m_Editor = nullptr;
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
    //ImGui::Spacing();

    auto iconSize2 = size(24, 24);

    ed::SetCurrentEditor(m_Editor);

    auto& style = ImGui::GetStyle();

    auto getIconColor = [](PinType type)
    {
        switch (type)
        {
            default:
            case PinType::Flow:     return ImColor(255, 255, 255);
            case PinType::Bool:     return ImColor(139,   0,   0);
            case PinType::Int:      return ImColor( 68, 201, 156);
            case PinType::Float:    return ImColor(147, 226,  74);
            case PinType::Object:   return ImColor( 51, 150, 215);
            case PinType::Function: return ImColor(218,   0, 183);
        }
    };

    auto drawPinIcon = [iconSize2, &getIconColor](const Pin& pin, bool connected)
    {
        IconType iconType;
        ImColor  color = getIconColor(pin.Type);
        switch (pin.Type)
        {
            case PinType::Flow:     iconType = IconType::Flow;   break;
            case PinType::Bool:     iconType = IconType::Circle; break;
            case PinType::Int:      iconType = IconType::Circle; break;
            case PinType::Float:    iconType = IconType::Circle; break;
            case PinType::Object:   iconType = IconType::Circle; break;
            case PinType::Function: iconType = IconType::Circle; break;
            default:
                return;
        }

        ax::Widgets::Icon(to_imvec(iconSize2), iconType, connected, color, ImColor(32, 32, 32));
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

        int orderIndex = 0;
        for (auto& node : s_Nodes)
        {
            ++orderIndex;

            ed::BeginNode(node.ID);
                ed::BeginHeader(node.Color);
                    ImGui::Spring(0);
                    ImGui::TextUnformatted(node.Name.c_str());
                    ImGui::Spring(1);
                    ImGui::Text("%d", orderIndex);
                    ImGui::Spring(0,0);
                    ImGui::Dummy(ImVec2(0, 28));
                    ImGui::Spring(0);
                ed::EndHeader();

                for (auto& input : node.Inputs)
                {
                    ed::BeginInput(input.ID);
                    drawPinIcon(input, IsPinLinked(input.ID));
                    ImGui::Spring(0);
                    if (!input.Name.empty())
                    {
                        ImGui::TextUnformatted(input.Name.c_str());
                        ImGui::Spring(0);
                    }
                    if (input.Type == PinType::Bool)
                    {
                        ImGui::Button("Hello");
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
                    drawPinIcon(output, IsPinLinked(output.ID));
                    ed::EndOutput();
                }

            ed::EndNode();
        }

        for (auto& link : s_Links)
            ed::Link(link.ID, link.StartPinID, link.EndPinID, link.Color, 2.0f);

        int startPinId = 0, endPinId = 0;
        if (ed::CreateLink(&startPinId, &endPinId, ImColor(255, 255, 255), 2.0f))
        {
            auto startPin = FindPin(startPinId);
            auto endPin   = FindPin(endPinId);

            if (startPin && endPin)
            {
                if (endPin->Kind == startPin->Kind)
                    ed::RejectLink(ImColor(255, 0, 0), 2.0f);
                else if (endPin->Node == startPin->Node)
                    ed::RejectLink(ImColor(255, 0, 0), 1.0f);
                else if (endPin->Type != startPin->Type)
                    ed::RejectLink(ImColor(255, 128, 128), 1.0f);
                else if (ed::AcceptLink(ImColor(128, 255, 128), 4.0f))
                {
                    s_Links.emplace_back(Link(GetNextId(), startPinId, endPinId));
                    s_Links.back().Color = getIconColor(startPin->Type);
                }
            }
        }

        if (ed::DestroyLink())
        {
            while (int linkId = ed::GetDestroyedLinkId())
            {
                auto id = std::find_if(s_Links.begin(), s_Links.end(), [linkId](auto& link) { return link.ID == linkId; });
                if (id != s_Links.end())
                    s_Links.erase(id);
            }
        }

        ImGui::SetCursorScreenPos(cursorTopLeft);


        //auto drawList = ImGui::GetWindowDrawList();

        //static float time = 0.0f;

        //time += io.DeltaTime;

        //auto p0 = pointf(300, 300);
        //auto p1 = pointf(400, 200);
        //auto p2 = pointf(300 + 400 * (0.5f + 0.5f * cosf(time)), 600);
        //auto p3 = pointf(900, 400);

        //drawList->AddLine(to_imvec(p0), to_imvec(p1), IM_COL32(255, 0, 0, 255));
        //drawList->AddLine(to_imvec(p1), to_imvec(p2), IM_COL32(255, 0, 0, 255));
        //drawList->AddLine(to_imvec(p2), to_imvec(p3), IM_COL32(255, 0, 0, 255));

        //drawList->AddBezierCurve(to_imvec(p0), to_imvec(p1), to_imvec(p2), to_imvec(p3), IM_COL32(255, 255, 255, 255), 2.0f);

        //for (int i = 0; i < 5000; ++i)
        //{
        //    float x = rand() / (float)RAND_MAX;
        //    float y = rand() / (float)RAND_MAX;

        //    x = x * 800 + 200;
        //    y = y * 500 + 150;

        //    ImU32 color = IM_COL32(255, 255, 0, 255);

        //    auto s = pointf(x, y);

        //    auto t = bezier_project_point(s, p0, p1, p2, p3, 50);

        //    //auto p = bezier(p0, p1, p2, p3, t);

        //    auto distance = t.distance;
        //    if (distance < 50.0f)
        //        color = IM_COL32(255, 0, 255, 255);

        //    //s = p;

        //    drawList->AddRect(ImVec2(s.x - 2.0f, s.y - 2.0f), ImVec2(s.x + 2.0f, s.y + 2.0f), color);
        //}
    }
    ed::End();

    //ImGui::ShowMetricsWindow();

    //static bool show = true;
    //ImGui::ShowTestWindow(&show);
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
