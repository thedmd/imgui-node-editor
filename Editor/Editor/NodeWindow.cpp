#include "NodeWindow.h"
#include <string>
#include <vector>
#include <algorithm>
#include <utility>
#include "Types.h"
#include "Types.inl"
#include "Backend/imgui_impl_dx11.h"
#include "NodeEditorInternal.h"

namespace ed = ax::NodeEditor;

using namespace ax;
using ax::point;
using ax::pointf;
using ax::size;
using ax::rect;
using ed::roundi;
using ed::to_size;
using ed::to_point;
using ed::to_imvec;
using ed::operator+;
using ed::operator-;


using ax::NodeEditor::IconType;

void DrawIcon(ImDrawList* drawList, rect rect, IconType type, bool filled, ImU32 color, ImU32 innerColor = 0)
{
    ax::NodeEditor::Draw::Icon(drawList, rect, type, filled, color, innerColor);
}

void DrawIcon(rect rect, IconType type, bool filled, ImU32 color, ImU32 innerColor = 0)
{
    DrawIcon(ImGui::GetWindowDrawList(), rect, type, filled, color, innerColor);
}



const int   PortIconSize = 16;
const float JoinBezierStrength = 50;

enum class PortType
{
    Flow,

    Bool,
    Int,
    Float
};

enum class PortLayout
{
    Left,
    Right
};

struct Port
{
    int ID;
    int NodeID;
    std::string Name;
    PortType Type;

    rect FrameRect;
    rect IconRect;
    rect LabelRect;

    Port(int id, int nodeId, const char* name, PortType type):
        ID(id), NodeID(nodeId), Name(name), Type(type)
    {
    }

    void Layout(PortLayout layout)
    {
        auto& style = ImGui::GetStyle();

        const auto hasLabel = !Name.empty();
        const auto innerSpacingX = roundi(style.ItemInnerSpacing.x);

        // Measure side of every component
        IconRect.size      = size(PortIconSize, PortIconSize);
        LabelRect.size     = to_size(ImGui::CalcTextSize(Name.c_str()));

        // Align items to left or right
        if (layout == PortLayout::Left)
        {
            IconRect.x = 0;
            if (hasLabel)
                LabelRect.x = IconRect.right() + innerSpacingX;
            else
                LabelRect.x = 0;
        }
        else
        {
            LabelRect.x = 0;
            IconRect.x = 0;
            if (hasLabel)
                IconRect.x = LabelRect.right() + innerSpacingX;
            else
                IconRect.x = 0;
        }

        // Align text vertically
        if (IconRect.h >= LabelRect.h)
        {
            // Icon is bigger, put label in middle
            IconRect.y  = 0;
            LabelRect.y = (IconRect.h - LabelRect.h) / 2;
        }
        else if (IconRect.h < LabelRect.h)
        {
            const auto lineHeight = roundi(ImGui::GetTextLineHeight());

            // Label is higher than icon, make sure we start rendering
            // label at same position as in case when icon is bigger.
            if (IconRect.h >= lineHeight)
            {
                IconRect.y  = 0;
                LabelRect.y = (IconRect.h - lineHeight) / 2;
            }
            else
            {
                IconRect.y  = (lineHeight - IconRect.h) / 2;
                LabelRect.y = 0;
            }
        }

        // Calculate whole widget size
        FrameRect = ax::rect::make_union(IconRect, LabelRect);
    }

    void MoveLayout(const point& offset)
    {
        FrameRect.location += offset;
        IconRect.location  += offset;
        LabelRect.location += offset;
    }
};

struct Node
{
    int ID;
    std::string Name;
    point Position;
    std::vector<Port> Inputs;
    std::vector<Port> Outputs;

    rect FrameRect;
    rect HeaderRect;
    rect LabelRect;
    rect ClientRect;
    rect InputsRect;
    rect OutputsRect;

    Node(int id, const char* name, const point& position):
        ID(id), Name(name), Position(position)
    {
    }

    void Layout()
    {
        auto& style = ImGui::GetStyle();

        const auto hasLabel      = !Name.empty();
        const auto hasInputs     = !Inputs.empty();
        const auto hasOutputs    = !Outputs.empty();
        const auto innerSpacingX = roundi(style.ItemInnerSpacing.x);
        const auto innerSpacingY = roundi(style.ItemInnerSpacing.y);
        const auto framePaddingX = roundi(style.FramePadding.x);
        const auto framePaddingY = roundi(style.FramePadding.y);

        int maxInputLayoutWidth    = 0;
        int totalInputPortsHeight  = 0;
        int maxOutputLayoutWidth   = 0;
        int totalOutputPortsHeight = 0;

        for (auto& input : Inputs)
        {
            input.Layout(PortLayout::Left);
            maxInputLayoutWidth = std::max(maxInputLayoutWidth, input.FrameRect.w);
            totalInputPortsHeight += input.FrameRect.h;
        }

        for (auto& output : Outputs)
        {
            output.Layout(PortLayout::Right);
            maxOutputLayoutWidth = std::max(maxOutputLayoutWidth, output.FrameRect.w);
            totalOutputPortsHeight += output.FrameRect.h;
        }

        const auto portAreaWidth = maxInputLayoutWidth + maxOutputLayoutWidth + ((hasInputs && hasOutputs) ? innerSpacingX * 4: 0);

        // Measure label size
        LabelRect.size = to_size(ImGui::CalcTextSize(Name.c_str()));

        // Measure header size with paddings
        HeaderRect.location = point(0, 0);
        HeaderRect.w = std::max(LabelRect.w, portAreaWidth) + framePaddingX * 2;
        HeaderRect.h = LabelRect.h + framePaddingY * 2;

        // Center label on header horizontally
        //LabelRect.x  = (HeaderRect.w - LabelRect.w) / 2;
        LabelRect.x  = framePaddingX;
        LabelRect.y  = framePaddingY;

        // Calculate port areas, inputs are aligned to left, outputs are aligned to right
        InputsRect.x = framePaddingX;
        InputsRect.y = HeaderRect.bottom() + innerSpacingY;
        InputsRect.w = maxInputLayoutWidth;
        InputsRect.h = hasInputs ? (totalInputPortsHeight + (Inputs.size() - 1) * innerSpacingY) : 0;

        OutputsRect.w = maxOutputLayoutWidth;
        OutputsRect.h = hasOutputs ? (totalOutputPortsHeight + (Outputs.size() - 1) * innerSpacingY) : 0;
        OutputsRect.x = HeaderRect.w - framePaddingX - OutputsRect.w;
        OutputsRect.y = HeaderRect.bottom() + innerSpacingY;

        // Client area contain inputs and outputs without padding
        ClientRect = rect::make_union(InputsRect, OutputsRect);

        // Widget frame contain header and client area with padding
        FrameRect  = rect::make_union(HeaderRect, ClientRect);
        FrameRect.h += framePaddingY;

        // Move ports to positions inside node
        point cursor = InputsRect.location;
        for (auto& input : Inputs)
        {
            input.MoveLayout(cursor);
            cursor.y += input.FrameRect.h + innerSpacingY;
        }

        cursor = OutputsRect.location;
        for (auto& output : Outputs)
        {
            cursor.x = OutputsRect.right() - output.FrameRect.w;
            output.MoveLayout(cursor);
            cursor.y += output.FrameRect.h + innerSpacingY;
        }
    }

    void MoveLayout(const point& offset)
    {
        FrameRect.location += offset;
        HeaderRect.location += offset;
        LabelRect.location += offset;
        ClientRect.location += offset;
        InputsRect.location += offset;
        OutputsRect.location += offset;

        for (auto& input : Inputs)
            input.MoveLayout(offset);

        for (auto& output : Outputs)
            output.MoveLayout(offset);
    }

};

struct NodeJoin
{
    int ID;

    int StartPortID;
    int EndPortID;

    NodeJoin(int id, int startPortId, int endPortId):
        ID(id), StartPortID(startPortId), EndPortID(endPortId)
    {
    }
};

static std::vector<Node>        s_Nodes;
static std::vector<NodeJoin>    s_Joins;

static ImTextureID s_Background = nullptr;

NodeWindow::NodeWindow(void):
    m_Editor(nullptr)
{
    SetTitle("Node editor");

    s_Background = ImGui_LoadTexture("../Data/BlueprintBackground.png");

    m_Editor = ed::CreateEditor();


    //ImGui::GetIO().IniFilename = "NodeEditor.ini";

    int nextId = 1;
    auto genId = [&nextId]() { return nextId++; };

    s_Nodes.emplace_back(genId(), "InputAction Fire", point(50, 200));
    s_Nodes.back().Outputs.emplace_back(genId(), s_Nodes.back().ID, "Pressed", PortType::Flow);
    s_Nodes.back().Outputs.emplace_back(genId(), s_Nodes.back().ID, "Released", PortType::Flow);

    s_Nodes.emplace_back(genId(), "Branch", point(300, 20));
    s_Nodes.back().Inputs.emplace_back(genId(), s_Nodes.back().ID, "", PortType::Flow);
    s_Nodes.back().Inputs.emplace_back(genId(), s_Nodes.back().ID, "Condition", PortType::Bool);
    s_Nodes.back().Outputs.emplace_back(genId(), s_Nodes.back().ID, "True", PortType::Flow);
    s_Nodes.back().Outputs.emplace_back(genId(), s_Nodes.back().ID, "False", PortType::Flow);

    s_Nodes.emplace_back(genId(), "Do N", point(600, 30));
    s_Nodes.back().Inputs.emplace_back(genId(), s_Nodes.back().ID, "Enter", PortType::Flow);
    s_Nodes.back().Inputs.emplace_back(genId(), s_Nodes.back().ID, "N", PortType::Int);
    s_Nodes.back().Inputs.emplace_back(genId(), s_Nodes.back().ID, "Reset", PortType::Flow);
    s_Nodes.back().Outputs.emplace_back(genId(), s_Nodes.back().ID, "Exit", PortType::Flow);
    s_Nodes.back().Outputs.emplace_back(genId(), s_Nodes.back().ID, "Counter", PortType::Int);

    s_Nodes.emplace_back(genId(), "OutputAction", point(1000, 200));
    s_Nodes.back().Inputs.emplace_back(genId(), s_Nodes.back().ID, "Sample", PortType::Float);

    s_Nodes.emplace_back(genId(), "Single Line Trace by Channel", point(600, 30));
    s_Nodes.back().Inputs.emplace_back(genId(), s_Nodes.back().ID, "", PortType::Flow);
    s_Nodes.back().Inputs.emplace_back(genId(), s_Nodes.back().ID, "Start", PortType::Flow);
    s_Nodes.back().Inputs.emplace_back(genId(), s_Nodes.back().ID, "End", PortType::Int);
    s_Nodes.back().Inputs.emplace_back(genId(), s_Nodes.back().ID, "Trace Channel", PortType::Float);
    s_Nodes.back().Inputs.emplace_back(genId(), s_Nodes.back().ID, "Trace Complex", PortType::Bool);
    s_Nodes.back().Inputs.emplace_back(genId(), s_Nodes.back().ID, "Actors to Ignore", PortType::Int);
    s_Nodes.back().Inputs.emplace_back(genId(), s_Nodes.back().ID, "Draw Debug Type", PortType::Bool);
    s_Nodes.back().Inputs.emplace_back(genId(), s_Nodes.back().ID, "Ignore Self", PortType::Bool);
    s_Nodes.back().Outputs.emplace_back(genId(), s_Nodes.back().ID, "", PortType::Flow);
    s_Nodes.back().Outputs.emplace_back(genId(), s_Nodes.back().ID, "Out Hit", PortType::Float);
    s_Nodes.back().Outputs.emplace_back(genId(), s_Nodes.back().ID, "Return Value", PortType::Bool);

    s_Joins.emplace_back(genId(), s_Nodes[0].Outputs[0].ID, s_Nodes[1].Inputs[0].ID);
    s_Joins.emplace_back(genId(), s_Nodes[1].Outputs[0].ID, s_Nodes[2].Inputs[0].ID);
    s_Joins.emplace_back(genId(), s_Nodes[1].Outputs[1].ID, s_Nodes[2].Inputs[2].ID);
    s_Joins.emplace_back(genId(), s_Nodes[2].Outputs[1].ID, s_Nodes[3].Inputs[0].ID);
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
    //ImGui::DragFloat("Icon Size", &iconWidth, 1.0f, 1.0f, 128.0f);

    //ImGui::SetCursorPos(ImGui::GetCursorPos() + ImVec2(100, 100));

    auto iconSize = ImVec2(iconWidth, iconWidth);
    //ImGui::BeginVertical("node");
    //    ImGui::BeginHorizontal("header");
    //        ImGui::Spring(0.0f);
    //        ImGui::TextUnformatted("Do N");
    //        ImGui::Spring(1.0f);
    //        ImGui::Button("X", ImVec2(24, 24));
    //        ImGui::Spring(0.0f);
    //    ImGui::EndHorizontal();
    //    ImGui::BeginHorizontal("content");
    //        //ImGui::BeginVertical("input");
    //            ImGui::Button("X", iconSize);
    //        //    ImGui::Spring(0.0f);
    //        //    ImGui::TextUnformatted("Input");
    //        //ImGui::EndVertical();
    //        //FillItemRect(ImColor(0, 255, 0, 64));
    //        ImGui::Spring(1.0f);
    //        //ImGui::BeginVertical("output");
    //        //    ImGui::TextUnformatted("Output");
    //        //    ImGui::Spring(0.0f, 80.0f);
    //            ImGui::Button("X", iconSize);
    //            ImGui::BeginVertical("content2");
    //            ImGui::TextUnformatted("Output");
    //            ImGui::TextUnformatted("Output");
    //            ImGui::TextUnformatted("Output");
    //            ImGui::EndVertical();
    //        //ImGui::EndVertical();
    //        //FillItemRect(ImColor(0, 255, 0, 64));
    //    ImGui::EndHorizontal();
    //ImGui::EndVertical();
    //DrawItemRect(ImColor(255, 220, 255), 2);

    static float scale = 1.77f;
    static int s_ActiveNodeId = -1;

    ImGui::DragFloat("Scale", &scale, 0.01f, 0.1f, 8.0f);
    auto iconSize2 = size(roundi(PortIconSize * scale), roundi(PortIconSize * scale));
    ImGui::Text("size: %d", iconSize2.w);

    static auto portIconSize = PortIconSize;
    portIconSize = iconSize2.w;


    ed::SetCurrentEditor(m_Editor);


    auto& style = ImGui::GetStyle();

    ed::Begin("Node editor");
    //ImGui::BeginChild("scrolling_region", ImVec2(0, 0), true, /*ImGuiWindowFlags_NoScrollbar | */ImGuiWindowFlags_NoMove);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->ChannelsSplit(3);

    auto cursorTopLeft = ImGui::GetCursorScreenPos();

    std::stable_sort(s_Nodes.begin(), s_Nodes.end(), [](const Node& lhs, const Node& rhs)
    {
        if (rhs.ID == s_ActiveNodeId)
            return true;
        else
            return false;
    });

    auto iconSizeIm = ImVec2(static_cast<float>(portIconSize), static_cast<float>(portIconSize));

    auto drawIcon = [&drawList](size s, PortType type, bool connected)
    {
        auto rect = ax::rect(to_point(ImGui::GetCursorScreenPos()), s);
        if (type == PortType::Flow)
            DrawIcon(drawList, rect, IconType::Flow, connected, ImColor(255, 255, 255));
        else if (type == PortType::Bool)
            DrawIcon(drawList, rect, IconType::Circle, connected, ImColor(180, 0, 0));
        else if (type == PortType::Int)
            DrawIcon(drawList, rect, IconType::Circle, connected, ImColor(70, 225, 172));
        else if (type == PortType::Float)
            DrawIcon(drawList, rect, IconType::Circle, connected, ImColor(160, 245, 85));
        else
        {
            ImGui::Button("X", to_imvec(s));
            return;
        }
        ImGui::Dummy(to_imvec(s));
    };

    ImVec2 lastNodePos;

    drawList->ChannelsSetCurrent(2);
    for (auto& node : s_Nodes)
    {
        ed::BeginNode(node.ID);

        lastNodePos = ImGui::GetCursorScreenPos();

        ed::BeginHeader();
            ImGui::Spring(0);
            ImGui::Text(node.Name.c_str());
            ImGui::Spring(1);

//             ImGui::SuspendLayout();
//             ImGui::BeginGroup();
//             ImGui::Button("Button");
//             ImGui::TextUnformatted("Label!");
//             ImGui::EndGroup();
//             ImGui::ResumeLayout();

            ImGui::Spring(0);
            ImGui::Button("X", ImVec2(24, 24));
            ImGui::Spring(0);
            ImGui::Dummy(ImVec2(0, 36));
        ed::EndHeader();

        for (auto& input : node.Inputs)
        {
            ed::BeginInput(input.ID);
            ImGui::PushStyleVar(ImGuiStyleVar_LayoutAlign, 0.5f);
            ImGui::BeginHorizontal(input.ID);
            ImGui::Spring(0,2);
            drawIcon(to_size(iconSizeIm), input.Type, false);
            ImGui::Spring(0);
            ImGui::Text(input.Name.c_str());
            if (input.Type == PortType::Bool)
            {
                static bool xxx = false;
                ImGui::Spring(0);
                ImGui::Checkbox("", &xxx);
            }
            else if (input.Type == PortType::Float)
            {
                static int xxx = 0;
                ImGui::Spring(0);
                ImGui::PushItemWidth(100.0f);
                ImGui::SuspendLayout();
                ImGui::Combo("", &xxx, "Visibility" "\0" "Position" "\0" "Scale");
                ImGui::ResumeLayout();
                ImGui::PopItemWidth();
            }
            ImGui::EndHorizontal();
            ImGui::PopStyleVar();
            ed::EndInput();
        }

        for (auto& output : node.Outputs)
        {
            ed::BeginOutput(output.ID);
            ImGui::PushStyleVar(ImGuiStyleVar_LayoutAlign, 0.5f);
            ImGui::BeginHorizontal(output.ID);
            ImGui::Text(output.Name.c_str());
            ImGui::Spring(0);
            drawIcon(to_size(iconSizeIm), output.Type, false);
            ImGui::Spring(0, 2);
            ImGui::EndHorizontal();
            ImGui::PopStyleVar();
            ed::EndOutput();

            //ed::Icon("##icon", iconSizeIm, IconType::Flow, false);
        }

        //ImGui::Button("X", ImVec2(100 * scale, 40));

        ed::EndNode();
    }


        //node.Layout();

        //auto nodeTopLeft = to_point(cursorTopLeft) + node.Position;
        //node.MoveLayout(nodeTopLeft);

        //drawList->ChannelsSetCurrent(1);
        //ImGui::PushID(node.ID);
        //ImGui::SetCursorScreenPos(to_imvec(nodeTopLeft));
        //ImGui::InvisibleButton("node", to_imvec(node.FrameRect.size));
        //ImGui::PopID();

        //if (ImGui::IsItemActive())
        //{
        //    s_ActiveNodeId = node.ID;
        //    s_DragOffset = to_point(ImGui::GetMouseDragDelta(0, 0.0f));

        //    node.MoveLayout(s_DragOffset);
        //}
        //else if (s_ActiveNodeId == node.ID)
        //{
        //    node.Position += s_DragOffset;
        //    node.MoveLayout(s_DragOffset);
        //    s_ActiveNodeId = 0;
        //}

        //drawList->ChannelsSetCurrent(2);
//        //for (auto& input : node.Inputs)
//        //{
//        //    ImGui::PushID(input.ID);
//        //    ImGui::SetCursorScreenPos(to_imvec(input.IconRect.location));
//        //    ImGui::SetItemAllowOverlap();
//        //    ImGui::InvisibleButton("X", to_imvec(input.IconRect.size));
//        //    ImGui::PopID();
//        //}
//
//        //for (auto& output : node.Outputs)
//        //{
//        //    ImGui::PushID(output.ID);
//        //    ImGui::SetCursorScreenPos(to_imvec(output.IconRect.location));
//        //    ImGui::SetItemAllowOverlap();
//        //    ImGui::InvisibleButton("X", to_imvec(output.IconRect.size));
//        //    ImGui::PopID();
//        //}
//
//        /*
//        drawList->ChannelsSetCurrent(1);
//        auto drawRect = [drawList, &nodeTopLeft](const rect& rect, ImU32 color)
//        {
//            if (rect.is_empty())
//                return;
//
//            auto tl = to_imvec(rect.top_left());
//            auto br = to_imvec(rect.bottom_right());
//
//            drawList->AddRect(tl, br, color);
//        };
//
//        auto fillRect = [drawList, &nodeTopLeft](const rect& rect, ImU32 color)
//        {
//            if (rect.is_empty())
//                return;
//
//            auto tl = to_imvec(rect.top_left());
//            auto br = to_imvec(rect.bottom_right());
//
//            drawList->AddRectFilled(tl, br, color);
//        };
//
//        auto drawText = [drawList, &nodeTopLeft](const rect& rect, const std::string& text)
//        {
//            if (text.empty())
//                return;
//
//            drawList->AddText(to_imvec(rect.location), ImColor(255, 255, 255), text.c_str());
//        };
//
//        auto drawIcon = [&drawList, &drawRect](rect rect, PortType type, bool connected)
//        {
//            if (type == PortType::Flow)
//                DrawIcon(drawList, rect, IconType::Flow, connected, ImColor(255, 255, 255));
//            else if (type == PortType::Bool)
//                DrawIcon(drawList, rect, IconType::Circle, connected, ImColor(180, 0, 0));
//            else if (type == PortType::Int)
//                DrawIcon(drawList, rect, IconType::Circle, connected, ImColor(70, 225, 172));
//            else if (type == PortType::Float)
//                DrawIcon(drawList, rect, IconType::Circle, connected, ImColor(160, 245, 85));
//            else
//                drawRect(rect, ImColor(0, 255, 255));
//        };
//
//        fillRect(node.FrameRect, ImColor(0, 32, 64));
//        fillRect(node.HeaderRect, ImColor(0, 32, 0));
//        drawRect(node.FrameRect, ImColor(255, 0, 255));
//        drawRect(node.HeaderRect, ImColor(255, 0, 0));
//        /////drawRect(node.LabelRect, ImColor(255, 255, 0));
//        /////drawRect(node.InputsRect, ImColor(0, 0, 255));
//        /////drawRect(node.OutputsRect, ImColor(0, 0, 255));
//        drawText(node.LabelRect, node.Name);
//
//
//
//        for (auto& input : node.Inputs)
//        {
//            /////drawRect(input.FrameRect, ImColor(255, 0, 0));
//            /////drawRect(input.LabelRect, ImColor(255, 255, 0));
//            drawIcon(input.IconRect, input.Type, isPortConnected(input.ID));
//            drawText(input.LabelRect, input.Name);
//        }
//
//        for (auto& output : node.Outputs)
//        {
//            /////drawRect(output.FrameRect, ImColor(255, 0, 0));
//            /////drawRect(output.LabelRect, ImColor(255, 255, 0));
//            drawIcon(output.IconRect, output.Type, isPortConnected(output.ID));
//            drawText(output.LabelRect, output.Name);
//        }
//
//        //    const float rounding = 6;
//
//        //    auto cursor = ImGui::GetCursorScreenPos();
//        //    ImRect headerRect(cursor, cursor + ImVec2(headerWidth, ImGui::GetTextLineHeightWithSpacing()));
//
//        //    drawList->PathLineTo(headerRect.GetBL());
//        //    drawList->PathLineTo(headerRect.GetTL() + ImVec2(0, rounding));
//        //    drawList->PathArcTo(headerRect.GetTL() + ImVec2(rounding, rounding), rounding, IM_PI, IM_PI + IM_PI / 2);
//        //    drawList->PathLineTo(headerRect.GetTR() - ImVec2(rounding, 0));
//        //    drawList->PathArcTo(headerRect.GetTR() + ImVec2(-rounding, rounding), rounding, IM_PI + IM_PI / 2, 2 * IM_PI);
//        //    drawList->PathLineTo(headerRect.GetBR());
//        //    drawList->PathFill(ImColor(255, 0, 255));
//        //}
//        */
//    }
//
//    for (auto& join : s_Joins)
//        ed::Link(join.ID, join.StartPortID, join.EndPortID, ImColor(255, 255, 0));
//
//    /*
//    drawList->ChannelsSetCurrent(0);
//    for (auto& join : s_Joins)
//    {
//        auto findPort = [](int id) -> const Port*
//        {
//            for (auto& node : s_Nodes)
//            {
//                for (auto& input : node.Inputs)
//                    if (input.ID == id)
//                        return &input;
//
//                for (auto& output : node.Outputs)
//                    if (output.ID == id)
//                        return &output;
//            }
//
//            return nullptr;
//        };
//
//        auto start = findPort(join.StartPortID);
//        auto end   = findPort(join.EndPortID);
//
//        auto startpoint   = to_imvec(point(start->IconRect.right(), start->IconRect.center_y()));
//        auto endpoint     = to_imvec(point(end->IconRect.left(), end->IconRect.center_y()));
//        auto startControl = startpoint + ImVec2(JoinBezierStrength, 0);
//        auto endControl   = endpoint - ImVec2(JoinBezierStrength, 0);
//
//        drawList->AddBezierCurve(startpoint, startControl, endControl, endpoint, ImColor(255, 255, 0), 2.0f);
//    }
//    */

    drawList->ChannelsSetCurrent(0);
    drawList->ChannelsMerge();

    ImGui::SetCursorScreenPos(cursorTopLeft);
    //ImGui::Text("IsAnyItemActive: %d", ImGui::IsAnyItemActive() ? 1 : 0);

    //ImGui::SetCursorScreenPos(cursorTopLeft + ImVec2(400, 400));


    ImVec2 iconOrigin(300, 200);
    DrawIcon(ax::rect(to_point(iconOrigin),                                                     iconSize2), IconType::Flow,        false, ImColor(255, 255, 255), ImColor(255, 0, 0));
    DrawIcon(ax::rect(to_point(iconOrigin) + point(roundi(     0 * scale), roundi(28 * scale)), iconSize2), IconType::Flow,        true,  ImColor(255, 255, 255), ImColor(255, 0, 0));
    DrawIcon(ax::rect(to_point(iconOrigin) + point(roundi(    28 * scale), roundi( 0 * scale)), iconSize2), IconType::Circle,      false, ImColor(  0, 255, 255), ImColor(255, 0, 0));
    DrawIcon(ax::rect(to_point(iconOrigin) + point(roundi(    28 * scale), roundi(28 * scale)), iconSize2), IconType::Circle,      true,  ImColor(  0, 255, 255), ImColor(255, 0, 0));
    DrawIcon(ax::rect(to_point(iconOrigin) + point(roundi(2 * 28 * scale), roundi( 0 * scale)), iconSize2), IconType::Square,      false, ImColor(128, 255, 128), ImColor(255, 0, 0));
    DrawIcon(ax::rect(to_point(iconOrigin) + point(roundi(2 * 28 * scale), roundi(28 * scale)), iconSize2), IconType::Square,      true,  ImColor(128, 255, 128), ImColor(255, 0, 0));
    DrawIcon(ax::rect(to_point(iconOrigin) + point(roundi(3 * 28 * scale), roundi( 0 * scale)), iconSize2), IconType::Grid,        false, ImColor(128, 255, 128), ImColor(255, 0, 0));
    DrawIcon(ax::rect(to_point(iconOrigin) + point(roundi(3 * 28 * scale), roundi(28 * scale)), iconSize2), IconType::Grid,        true,  ImColor(128, 255, 128), ImColor(255, 0, 0));
    DrawIcon(ax::rect(to_point(iconOrigin) + point(roundi(4 * 28 * scale), roundi( 0 * scale)), iconSize2), IconType::RoundSquare, false, ImColor(255, 128, 128), ImColor(255, 0, 0));
    DrawIcon(ax::rect(to_point(iconOrigin) + point(roundi(4 * 28 * scale), roundi(28 * scale)), iconSize2), IconType::RoundSquare, true,  ImColor(255, 128, 128), ImColor(255, 0, 0));
    DrawIcon(ax::rect(to_point(iconOrigin) + point(roundi(5 * 28 * scale), roundi( 0 * scale)), iconSize2), IconType::Diamond,     false, ImColor(255, 255, 128), ImColor(255, 0, 0));
    DrawIcon(ax::rect(to_point(iconOrigin) + point(roundi(5 * 28 * scale), roundi(28 * scale)), iconSize2), IconType::Diamond,     true,  ImColor(255, 255, 128), ImColor(255, 0, 0));

    int tw = ImGui_GetTextureWidth(s_Background);
    int th = ImGui_GetTextureHeight(s_Background);
    ImGui::Image(s_Background, ImVec2((float)tw, (float)th));

    auto drawHeader = [&](ImVec2 size)
    {
        auto origin = ImGui::GetCursorScreenPos();

        drawList->AddDrawCmd();
        drawList->PathRect(origin, origin + size, 12.0f, 1 | 2);
        drawList->PathFill(ImColor(255, 0, 255));

        float zoom = 4.0f;
        ax::matrix transform;
        transform.scale(1.0f / zoom, 1.0f / zoom);
        transform.scale(size.x / tw, size.y / th);
        transform.scale(1.0f / (size.x - 1.0f), 1.0f / (size.y - 1.0f));
        transform.translate(-origin.x - 0.5f, -origin.y - 0.5f);

        ImGui_PushGizmo(drawList, s_Background, (float*)&transform);
        drawList->AddDrawCmd();

        drawList->PathRect(origin, origin + size, 12.0f, 1 | 2);
        drawList->PathStroke(ImColor(255, 0, 255), true, 2.0f);
    };

    ImGui::SetCursorScreenPos(ImVec2(400, 300));
    drawHeader(ImVec2(250, 30));
    ImGui::SetCursorScreenPos(lastNodePos - ImVec2(0, 105));//to_imvec(s_Nodes.back().Position));
    drawHeader(ImVec2(250, 100));
    //drawHeader(ImVec2(512, 256));

    ImGui::SetCursorScreenPos(ImVec2(900, 500));
    drawHeader(ImVec2(250, 30));
//
//
//    ImGui::Separator();
//
//    static bool groups = false;
//    ImGui::Checkbox("Groups", &groups);
//
//    ImGui::BeginGroup();
//    ImGui::Text("Do N");
//    ImGui::SameLine();
//    //ed::Spring();
//    //ImGui::SameLine();
//    ImGui::Button("Button \\o/");
//    ImGui::EndGroup();
//
//    ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImColor(255, 0, 0));
//
//    //ImGui::BeginGroup();
//    //ImGui::BeginGroup();
//    //ImGui::BeginGroup();
//    //ImGui::Text("Long Long Long");
//    //ImGui::SameLine();
//    //ImGui::BeginGroup();
//    //ImGui::Dummy(ImVec2(0,0));
//    //ImGui::EndGroup();
//    //ImGui::SameLine();
//    //ImGui::BeginGroup();
//    //ImGui::Text("In Same Line");
//    //ImGui::EndGroup();
//    //if (groups) ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImColor(0, 255, 0));
//    //else        ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImColor(255, 0, 0));
//    //ImGui::EndGroup();
//    //if (groups) ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImColor(0, 255, 0));
//    //else        ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImColor(255, 0, 0));
//    //ImGui::EndGroup();
//    //if (groups) ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImColor(0, 255, 0));
//    //else        ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImColor(255, 0, 0));
//
//    ////ImGui::SameLine();
//
//    //ImGui::BeginGroup();
//    //ImGui::Button("I'm in the next line");
//    //ImGui::EndGroup();
//    //if (groups) ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImColor(0, 255, 0));
//    //else        ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImColor(255, 0, 0));
//    //ImGui::EndGroup();
//    //if (groups) ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImColor(0, 255, 0));
//    //else        ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImColor(255, 0, 0));
//
////     ImGui::SameLine();
////
////     ImGui::BeginGroup();
////     ImGui::Button("Z");
////     ImGui::EndGroup();
////     if (groups) ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImColor(0, 255, 0));
////     else        ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImColor(255, 0, 0));
//
//
    ed::End();
//
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
