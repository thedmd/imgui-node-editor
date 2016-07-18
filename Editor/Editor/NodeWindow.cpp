#include "NodeWindow.h"
#include <string>
#include <vector>
#include <algorithm>
#include <utility>
#include "Types.h"
#include "Types.inl"

static inline ImVec2 operator+(const ImVec2& lhs, const ImVec2& rhs) { return ImVec2(lhs.x+rhs.x, lhs.y+rhs.y); }
static inline ImVec2 operator-(const ImVec2& lhs, const ImVec2& rhs) { return ImVec2(lhs.x-rhs.x, lhs.y-rhs.y); }

inline int roundi(float value) { return static_cast<int>(value); }
inline Point to_point(const ImVec2& value) { return Point(roundi(value.x), roundi(value.y)); }
inline Size  to_size (const ImVec2& value) { return Size (roundi(value.x), roundi(value.y)); }
inline ImVec2 to_imvec(const Point& value) { return ImVec2(static_cast<float>(value.x), static_cast<float>(value.y)); }
inline ImVec2 to_imvec(const Size& value)  { return ImVec2(static_cast<float>(value.w), static_cast<float>(value.h)); }


const int PortIconSize = 24;
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

    Rect FrameRect;
    Rect IconRect;
    Rect LabelRect;

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
        IconRect.size      = Size(PortIconSize, PortIconSize);
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
        FrameRect = Rect::make_union(IconRect, LabelRect);
    }

    void MoveLayout(const Point& offset)
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
    Point Position;
    std::vector<Port> Inputs;
    std::vector<Port> Outputs;

    Rect FrameRect;
    Rect HeaderRect;
    Rect LabelRect;
    Rect ClientRect;
    Rect InputsRect;
    Rect OutputsRect;

    Node(int id, const char* name, const Point& position):
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
        HeaderRect.location = Point(0, 0);
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
        ClientRect = Rect::make_union(InputsRect, OutputsRect);

        // Widget frame contain header and client area with padding
        FrameRect  = Rect::make_union(HeaderRect, ClientRect);
        FrameRect.h += framePaddingY;

        // Move ports to positions inside node
        Point cursor = InputsRect.location;
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

    void MoveLayout(const Point& offset)
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


NodeWindow::NodeWindow(void)
{
    SetTitle("Node editor");

    ImGui::GetIO().IniFilename = "NodeEditor.ini";

    int nextId = 1;
    auto genId = [&nextId]() { return nextId++; };

    s_Nodes.emplace_back(genId(), "InputAction Fire", Point(150, 200));
    s_Nodes.back().Outputs.emplace_back(genId(), s_Nodes.back().ID, "Pressed", PortType::Flow);
    s_Nodes.back().Outputs.emplace_back(genId(), s_Nodes.back().ID, "Released", PortType::Flow);

    s_Nodes.emplace_back(genId(), "Branch", Point(300, 20));
    s_Nodes.back().Inputs.emplace_back(genId(), s_Nodes.back().ID, "", PortType::Flow);
    s_Nodes.back().Inputs.emplace_back(genId(), s_Nodes.back().ID, "Condition", PortType::Bool);
    s_Nodes.back().Outputs.emplace_back(genId(), s_Nodes.back().ID, "True", PortType::Flow);
    s_Nodes.back().Outputs.emplace_back(genId(), s_Nodes.back().ID, "False", PortType::Flow);

    s_Nodes.emplace_back(genId(), "Do N", Point(600, 30));
    s_Nodes.back().Inputs.emplace_back(genId(), s_Nodes.back().ID, "Enter", PortType::Flow);
    s_Nodes.back().Inputs.emplace_back(genId(), s_Nodes.back().ID, "N", PortType::Int);
    s_Nodes.back().Inputs.emplace_back(genId(), s_Nodes.back().ID, "Reset", PortType::Flow);
    s_Nodes.back().Outputs.emplace_back(genId(), s_Nodes.back().ID, "Exit", PortType::Flow);
    s_Nodes.back().Outputs.emplace_back(genId(), s_Nodes.back().ID, "Counter", PortType::Int);

    s_Nodes.emplace_back(genId(), "OutputAction", Point(400, 200));
    s_Nodes.back().Inputs.emplace_back(genId(), s_Nodes.back().ID, "Sample", PortType::Float);

    s_Joins.emplace_back(genId(), s_Nodes[0].Outputs[0].ID, s_Nodes[1].Inputs[1].ID);
    s_Joins.emplace_back(genId(), s_Nodes[2].Outputs[0].ID, s_Nodes[3].Inputs[0].ID);
}


void Dummy();

static int s_ActiveNodeId = 0;
static Point s_DragOffset;

void NodeWindow::OnGui()
{
    //Dummy();

    auto& style = ImGui::GetStyle();

    ImDrawList* drawList1 = ImGui::GetWindowDrawList();

    ImGui::BeginChild("scrolling_region", ImVec2(0, 0), true, /*ImGuiWindowFlags_NoScrollbar | */ImGuiWindowFlags_NoMove);

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

    drawList->ChannelsSetCurrent(2);
    for (auto& node : s_Nodes)
    {
        node.Layout();

        auto nodeTopLeft = to_point(cursorTopLeft) + node.Position;
        node.MoveLayout(nodeTopLeft);

        drawList->ChannelsSetCurrent(1);
        ImGui::PushID(node.ID);
        ImGui::SetCursorScreenPos(to_imvec(nodeTopLeft));
        ImGui::InvisibleButton("node", to_imvec(node.FrameRect.size));
        ImGui::PopID();

        if (ImGui::IsItemActive())
        {
            s_ActiveNodeId = node.ID;
            s_DragOffset = to_point(ImGui::GetMouseDragDelta(0, 0.0f));

            node.MoveLayout(s_DragOffset);
        }
        else if (s_ActiveNodeId == node.ID)
        {
            node.Position += s_DragOffset;
            node.MoveLayout(s_DragOffset);
            s_ActiveNodeId = 0;
        }

        drawList->ChannelsSetCurrent(2);
        for (auto& input : node.Inputs)
        {
            ImGui::PushID(input.ID);
            ImGui::SetCursorScreenPos(to_imvec(input.IconRect.location));
            ImGui::SetItemAllowOverlap();
            ImGui::Button("X", to_imvec(input.IconRect.size));
            ImGui::PopID();
        }

        for (auto& output : node.Outputs)
        {
            ImGui::PushID(output.ID);
            ImGui::SetCursorScreenPos(to_imvec(output.IconRect.location));
            ImGui::SetItemAllowOverlap();
            ImGui::Button("X", to_imvec(output.IconRect.size));
            ImGui::PopID();
        }

        drawList->ChannelsSetCurrent(1);
        auto drawRect = [drawList, &nodeTopLeft](const Rect& rect, ImU32 color)
        {
            if (rect.is_empty())
                return;

            auto tl = to_imvec(rect.top_left());
            auto br = to_imvec(rect.bottom_right());

            drawList->AddRect(tl, br, color);
        };

        auto fillRect = [drawList, &nodeTopLeft](const Rect& rect, ImU32 color)
        {
            if (rect.is_empty())
                return;

            auto tl = to_imvec(rect.top_left());
            auto br = to_imvec(rect.bottom_right());

            drawList->AddRectFilled(tl, br, color);
        };

        auto drawText = [drawList, &nodeTopLeft](const Rect& rect, const std::string& text)
        {
            if (text.empty())
                return;

            drawList->AddText(to_imvec(rect.location), ImColor(255, 255, 255), text.c_str());
        };

        fillRect(node.FrameRect, ImColor(0, 32, 64));
        fillRect(node.HeaderRect, ImColor(0, 32, 0));
        drawRect(node.FrameRect, ImColor(255, 0, 255));
        drawRect(node.HeaderRect, ImColor(255, 0, 0));
        /* *///drawRect(node.LabelRect, ImColor(255, 255, 0));
        /* *///drawRect(node.InputsRect, ImColor(0, 0, 255));
        /* *///drawRect(node.OutputsRect, ImColor(0, 0, 255));
        drawText(node.LabelRect, node.Name);

        for (auto& input : node.Inputs)
        {
            /* *///drawRect(input.FrameRect, ImColor(255, 0, 0));
            /* *///drawRect(input.LabelRect, ImColor(255, 255, 0));
            drawRect(input.IconRect, ImColor(0, 255, 255));
            drawText(input.LabelRect, input.Name);
        }

        for (auto& output : node.Outputs)
        {
            /* *///drawRect(output.FrameRect, ImColor(255, 0, 0));
            /* *///drawRect(output.LabelRect, ImColor(255, 255, 0));
            drawRect(output.IconRect, ImColor(0, 255, 255));
            drawText(output.LabelRect, output.Name);
        }

        //    const float rounding = 6;

        //    auto cursor = ImGui::GetCursorScreenPos();
        //    ImRect headerRect(cursor, cursor + ImVec2(headerWidth, ImGui::GetTextLineHeightWithSpacing()));

        //    drawList->PathLineTo(headerRect.GetBL());
        //    drawList->PathLineTo(headerRect.GetTL() + ImVec2(0, rounding));
        //    drawList->PathArcTo(headerRect.GetTL() + ImVec2(rounding, rounding), rounding, IM_PI, IM_PI + IM_PI / 2);
        //    drawList->PathLineTo(headerRect.GetTR() - ImVec2(rounding, 0));
        //    drawList->PathArcTo(headerRect.GetTR() + ImVec2(-rounding, rounding), rounding, IM_PI + IM_PI / 2, 2 * IM_PI);
        //    drawList->PathLineTo(headerRect.GetBR());
        //    drawList->PathFill(ImColor(255, 0, 255));
        //}
    }

    drawList->ChannelsSetCurrent(0);
    for (auto& join : s_Joins)
    {
        auto findPort = [](int id) -> const Port*
        {
            for (auto& node : s_Nodes)
            {
                for (auto& input : node.Inputs)
                    if (input.ID == id)
                        return &input;

                for (auto& output : node.Outputs)
                    if (output.ID == id)
                        return &output;
            }

            return nullptr;
        };

        auto start = findPort(join.StartPortID);
        auto end   = findPort(join.EndPortID);

        auto startPoint   = to_imvec(Point(start->IconRect.right(), start->IconRect.center_y()));
        auto endPoint     = to_imvec(Point(end->IconRect.left(), end->IconRect.center_y()));
        auto startControl = startPoint + ImVec2(JoinBezierStrength, 0);
        auto endControl   = endPoint - ImVec2(JoinBezierStrength, 0);

        drawList->AddBezierCurve(startPoint, startControl, endControl, endPoint, ImColor(255, 255, 0), 2.0f);
    }

    drawList->ChannelsSetCurrent(0);
    drawList->ChannelsMerge();


    //ImGui::SetCursorScreenPos(cursorTopLeft + ImVec2(400, 400));
    ImVec2 iconOrigin(500, 400);


    auto drawIcon = [&drawList](Rect rect, bool filled)
    {
        drawList->AddRectFilled(to_imvec(rect.top_left()), to_imvec(rect.bottom_right()), ImColor(255, 255, 255));

        if (filled)
            drawList->AddCircleFilled(to_imvec(rect.center()), 0.5f * rect.w / 2.0f, ImColor(0, 0, 0));
        else
            drawList->AddCircle(to_imvec(rect.center()), 0.5f * rect.w / 2.0f, ImColor(0, 0, 0));


    };

    drawIcon(Rect(to_point(iconOrigin), to_size(ImVec2(PortIconSize, PortIconSize))), false);
    drawIcon(Rect(to_point(iconOrigin) + Point(0, 32), to_size(ImVec2(PortIconSize, PortIconSize))), true);

    ImGui::SetCursorScreenPos(cursorTopLeft);
    ImGui::Text("IsAnyItemActive: %d", ImGui::IsAnyItemActive() ? 1 : 0);

    ImGui::EndChild();

    //ImGui::ShowMetricsWindow();
}

void Dummy()
{
    // Dummy
    struct Node
    {
        int     ID;
        char    Name[32];
        ImVec2  Pos, Size;
        float   Value;
        ImVec4  Color;
        int     InputsCount, OutputsCount;

        Node(int id, const char* name, const ImVec2& pos, float value, const ImVec4& color, int inputs_count, int outputs_count) { ID = id; strncpy(Name, name, 31); Name[31] = 0; Pos = pos; Value = value; Color = color; InputsCount = inputs_count; OutputsCount = outputs_count; }

        ImVec2 GetInputSlotPos(int slot_no) const   { return ImVec2(Pos.x, Pos.y + Size.y * ((float)slot_no+1) / ((float)InputsCount+1)); }
        ImVec2 GetOutputSlotPos(int slot_no) const  { return ImVec2(Pos.x + Size.x, Pos.y + Size.y * ((float)slot_no+1) / ((float)OutputsCount+1)); }
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
        node->Size = ImGui::GetItemRectSize() + NODE_WINDOW_PADDING + NODE_WINDOW_PADDING;
        ImVec2 node_rect_max = node_rect_min + node->Size;

        // Display node box
        draw_list->ChannelsSetCurrent(0); // Background
        ImGui::SetCursorScreenPos(node_rect_min);
        ImGui::InvisibleButton("node", node->Size);
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