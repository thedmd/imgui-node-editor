# include <imgui.h>
# define IMGUI_DEFINE_MATH_OPERATORS
# include <imgui_internal.h>
# include <imgui_canvas.h>
# include <application.h>

static void DrawScale(const ImVec2& from, const ImVec2& to, float majorUnit, float minorUnit, float labelAlignment, float sign = 1.0f)
{
    auto drawList  = ImGui::GetWindowDrawList();
    auto direction = (to - from) * ImInvLength(to - from, 0.0f);
    auto normal    = ImVec2(-direction.y, direction.x);
    auto distance  = sqrtf(ImLengthSqr(to - from));

    if (ImDot(direction, direction) < FLT_EPSILON)
        return;

    auto minorSize = 5.0f;
    auto majorSize = 10.0f;
    auto labelDistance = 8.0f;

    drawList->AddLine(from, to, IM_COL32(255, 255, 255, 255));

    auto p = from;
    for (auto d = 0.0f; d <= distance; d += minorUnit, p += direction * minorUnit)
        drawList->AddLine(p - normal * minorSize, p + normal * minorSize, IM_COL32(255, 255, 255, 255));

    for (auto d = 0.0f; d <= distance + majorUnit; d += majorUnit)
    {
        p = from + direction * d;

        drawList->AddLine(p - normal * majorSize, p + normal * majorSize, IM_COL32(255, 255, 255, 255));

        if (d == 0.0f)
            continue;

        char label[16];
        snprintf(label, 15, "%g", d * sign);
        auto labelSize = ImGui::CalcTextSize(label);

        auto labelPosition    = p + ImVec2(fabsf(normal.x), fabsf(normal.y)) * labelDistance;
        auto labelAlignedSize = ImDot(labelSize, direction);
        labelPosition += direction * (-labelAlignedSize + labelAlignment * labelAlignedSize * 2.0f);
        labelPosition = ImFloor(labelPosition + ImVec2(0.5f, 0.5f));

        drawList->AddText(labelPosition, IM_COL32(255, 255, 255, 255), label);
    }
}

static bool Splitter(bool split_vertically, float thickness, float* size1, float* size2, float min_size1, float min_size2, float splitter_long_axis_size = -1.0f)
{
    using namespace ImGui;
    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = g.CurrentWindow;
    ImGuiID id = window->GetID("##Splitter");
    ImRect bb;
    bb.Min = window->DC.CursorPos + (split_vertically ? ImVec2(*size1, 0.0f) : ImVec2(0.0f, *size1));
    bb.Max = bb.Min + CalcItemSize(split_vertically ? ImVec2(thickness, splitter_long_axis_size) : ImVec2(splitter_long_axis_size, thickness), 0.0f, 0.0f);
    return SplitterBehavior(bb, id, split_vertically ? ImGuiAxis_X : ImGuiAxis_Y, size1, size2, min_size1, min_size2, 0.0f);
}

const char* Application_GetName()
{
    return "Canvas";
}

void Application_Initialize()
{
}

void Application_Finalize()
{
}

void Application_Frame()
{
    auto& io = ImGui::GetIO();

    ImGui::Text("FPS: %.2f (%.2gms)", io.Framerate, io.Framerate ? 1000.0f / io.Framerate : 0.0f);

    ImGui::Separator();

    auto availableRegion = ImGui::GetContentRegionAvail();

    static float s_SplitterSize     = 6.0f;
    static float s_SplitterArea     = 0.0f;
    static float s_LeftPaneSize     = 0.0f;
    static float s_RightPaneSize    = 0.0f;

    if (s_SplitterArea != availableRegion.x)
    {
        if (s_SplitterArea == 0.0f)
        {
            s_SplitterArea     = availableRegion.x;
            s_LeftPaneSize     = ImFloor(availableRegion.x * 0.25f);
            s_RightPaneSize    = availableRegion.x - s_LeftPaneSize - s_SplitterSize;
        }
        else
        {
            auto ratio = availableRegion.x / s_SplitterArea;
            s_SplitterArea     = availableRegion.x;
            s_LeftPaneSize     = s_LeftPaneSize * ratio;
            s_RightPaneSize    = availableRegion.x - s_LeftPaneSize - s_SplitterSize;
        }
    }

    static ImGuiEx::Canvas canvas;
    static ImVec2 drawStartPoint;
    static bool isDragging = false;
    static ImRect panelRect;

    Splitter(true, s_SplitterSize, &s_LeftPaneSize, &s_RightPaneSize, 100.0f, 100.0f);

    auto canvasRect = canvas.Rect();
    auto viewRect = canvas.ViewRect();
    auto viewOrigin = canvas.ViewOrigin();
    auto viewScale = canvas.ViewScale();

    ImGui::BeginChild("##top", ImVec2(s_LeftPaneSize, -1), false, ImGuiWindowFlags_NoScrollWithMouse);

    ImGui::TextUnformatted("Rect:");
    ImGui::BeginColumns("rect", 2, ImGuiColumnsFlags_NoBorder);
    ImGui::SetColumnWidth(0, ImGui::CalcTextSize("\t\tL: 0000.00\t").x);
    ImGui::Text("\tL: %.2f", canvasRect.Min.x);       ImGui::NextColumn();
    ImGui::Text("\tT: %.2f", canvasRect.Min.y);       ImGui::NextColumn();
    ImGui::Text("\tR: %.2f", canvasRect.Max.x);       ImGui::NextColumn();
    ImGui::Text("\tB: %.2f", canvasRect.Max.y);       ImGui::NextColumn();
    ImGui::Text("\tW: %.2f", canvasRect.GetWidth());  ImGui::NextColumn();
    ImGui::Text("\tH: %.2f", canvasRect.GetHeight()); ImGui::NextColumn();
    ImGui::EndColumns();

    ImGui::TextUnformatted("View Rect:");
    ImGui::BeginColumns("viewrect", 2, ImGuiColumnsFlags_NoBorder);
    ImGui::SetColumnWidth(0, ImGui::CalcTextSize("\t\tL: 0000.00\t").x);
    ImGui::Text("\tL: %.2f", viewRect.Min.x);       ImGui::NextColumn();
    ImGui::Text("\tT: %.2f", viewRect.Min.y);       ImGui::NextColumn();
    ImGui::Text("\tR: %.2f", viewRect.Max.x);       ImGui::NextColumn();
    ImGui::Text("\tB: %.2f", viewRect.Max.y);       ImGui::NextColumn();
    ImGui::Text("\tW: %.2f", viewRect.GetWidth());  ImGui::NextColumn();
    ImGui::Text("\tH: %.2f", viewRect.GetHeight()); ImGui::NextColumn();
    ImGui::EndColumns();

    ImGui::TextUnformatted("Origin:");
    ImGui::Indent();
    auto originChanged = false;
    ImGui::PushItemWidth(-ImGui::GetStyle().IndentSpacing);
    originChanged |= ImGui::DragFloat("##originx", &viewOrigin.x, 1.0f);
    originChanged |= ImGui::DragFloat("##originy", &viewOrigin.y, 1.0f);
    if (originChanged) canvas.SetView(viewOrigin, viewScale);
    ImGui::PopItemWidth();
    ImGui::Unindent();

    ImGui::TextUnformatted("Scale:");
    ImGui::Indent();
    ImGui::PushItemWidth(-ImGui::GetStyle().IndentSpacing);
    if (ImGui::DragFloat("##scale", &viewScale, 0.01f, 0.01f, 15.0f))
        canvas.SetView(viewOrigin, viewScale);
    ImGui::PopItemWidth();
    ImGui::Unindent();

    ImGui::Separator();

    if (ImGui::Button("Center over Panel", ImVec2(s_LeftPaneSize, 0)))
        canvas.CenterView(panelRect.GetCenter());

    if (ImGui::Button("Center and zoom to Panel", ImVec2(s_LeftPaneSize, 0)))
        canvas.CenterView(panelRect);

    ImGui::TextUnformatted("Panel Rect:");
    ImGui::BeginColumns("panelrect", 2, ImGuiColumnsFlags_NoBorder);
    ImGui::SetColumnWidth(0, ImGui::CalcTextSize("\t\tL: 0000.00\t").x);
    ImGui::Text("\tL: %.2f", panelRect.Min.x);       ImGui::NextColumn();
    ImGui::Text("\tT: %.2f", panelRect.Min.y);       ImGui::NextColumn();
    ImGui::Text("\tR: %.2f", panelRect.Max.x);       ImGui::NextColumn();
    ImGui::Text("\tB: %.2f", panelRect.Max.y);       ImGui::NextColumn();
    ImGui::Text("\tW: %.2f", panelRect.GetWidth());  ImGui::NextColumn();
    ImGui::Text("\tH: %.2f", panelRect.GetHeight()); ImGui::NextColumn();
    ImGui::EndColumns();

    ImGui::EndChild();

    ImGui::SameLine(0.0f, s_SplitterSize);


    if (canvas.Begin("##mycanvas", ImVec2(s_RightPaneSize, 0.0f)))
    {
        //auto drawList = ImGui::GetWindowDrawList();

        if ((isDragging || ImGui::IsItemHovered()) && ImGui::IsMouseDragging(1, 0.0f))
        {
            if (!isDragging)
            {
                isDragging = true;
                drawStartPoint = viewOrigin;
            }

            canvas.SetView(drawStartPoint + ImGui::GetMouseDragDelta(1, 0.0f) * viewScale, viewScale);
        }
        else if (isDragging)
            isDragging = false;

        viewRect = canvas.ViewRect();

        if (viewRect.Max.x > 0.0f)
            DrawScale(ImVec2(0.0f, 0.0f), ImVec2(viewRect.Max.x, 0.0f), 100.0f, 10.0f, 0.6f);
        if (viewRect.Min.x < 0.0f)
            DrawScale(ImVec2(0.0f, 0.0f), ImVec2(viewRect.Min.x, 0.0f), 100.0f, 10.0f, 0.6f, -1.0f);
        if (viewRect.Max.y > 0.0f)
            DrawScale(ImVec2(0.0f, 0.0f), ImVec2(0.0f, viewRect.Max.y), 100.0f, 10.0f, 0.6f);
        if (viewRect.Min.y < 0.0f)
            DrawScale(ImVec2(0.0f, 0.0f), ImVec2(0.0f, viewRect.Min.y), 100.0f, 10.0f, 0.6f, -1.0f);

        ImGui::Text("Hovered:     %d", ImGui::IsItemHovered() ? 1 : 0);

        ImGui::TextUnformatted("Hello World!");

        ImGui::Bullet();

        ImGui::Button("Panel", ImVec2(s_RightPaneSize * 0.75f, availableRegion.y * 0.5f) * 0.5f);
        panelRect.Min = ImGui::GetItemRectMin();
        panelRect.Max = ImGui::GetItemRectMax();

        canvas.End();
    }






    //ed::SetCurrentEditor(g_Context);
    //ed::Begin("My Editor", ImVec2(0.0, 0.0f));
    //int uniqueId = 1;
    //// Start drawing nodes.
    //ed::BeginNode(uniqueId++);
    //    ImGui::Text("Node A");
    //    ed::BeginPin(uniqueId++, ed::PinKind::Input);
    //        ImGui::Text("-> In");
    //    ed::EndPin();
    //    ImGui::SameLine();
    //    ed::BeginPin(uniqueId++, ed::PinKind::Output);
    //        ImGui::Text("Out ->");
    //    ed::EndPin();
    //ed::EndNode();
    //ed::End();
    //ed::SetCurrentEditor(nullptr);

	//ImGui::ShowMetricsWindow();
}

