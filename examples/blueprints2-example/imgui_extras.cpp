# include "imgui_extras.h"
# define IMGUI_DEFINE_MATH_OPERATORS
# include <imgui_internal.h>

void ImEx::DrawIcon(ImDrawList* drawList, const ImVec2& a, const ImVec2& b, IconType type, bool filled, ImU32 color, ImU32 innerColor)
{
          auto rect           = ImRect(a, b);
          auto rect_x         = rect.Min.x;
          auto rect_y         = rect.Min.y;
          auto rect_w         = rect.Max.x - rect.Min.x;
          auto rect_h         = rect.Max.y - rect.Min.y;
          auto rect_center_x  = (rect.Min.x + rect.Max.x) * 0.5f;
          auto rect_center_y  = (rect.Min.y + rect.Max.y) * 0.5f;
          auto rect_center    = ImVec2(rect_center_x, rect_center_y);
    const auto outline_scale  = ImMin(rect_w, rect_h) / 24.0f;
    const auto extra_segments = static_cast<int>(2 * outline_scale); // for full circle

    if (type == IconType::Flow || type == IconType::FlowDown)
    {
        auto tr = [type, rect_center_x, rect_center_y](const ImVec2& point) -> ImVec2
        {
            ImVec2 result = point;
            if (type == IconType::FlowDown)
            {
                result = point - ImVec2(rect_center_x, rect_center_y);
                result = ImVec2(result.y, result.x);
                result = result + ImVec2(rect_center_x, rect_center_y);
            }
            return result;
        };

        const auto origin_scale = rect_w / 24.0f;

        const auto offset_x  = 1.0f * origin_scale;
        const auto offset_y  = 0.0f * origin_scale;
        const auto margin     = (filled ? 2.0f : 2.0f) * origin_scale;
        const auto rounding   = 0.1f * origin_scale;
        const auto tip_round  = 0.7f; // percentage of triangle edge (for tip)
        //const auto edge_round = 0.7f; // percentage of triangle edge (for corner)
        const auto canvas = ImRect(
            tr(ImVec2(rect.Min.x + margin + offset_x, rect.Min.y + margin + offset_y)),
            tr(ImVec2(rect.Max.x - margin + offset_x, rect.Max.y - margin + offset_y)));
        const auto canvas_x = canvas.Min.x;
        const auto canvas_y = canvas.Min.y;
        const auto canvas_w = canvas.Max.x - canvas.Min.x;
        const auto canvas_h = canvas.Max.y - canvas.Min.y;

        const auto left   = canvas_x + canvas_w            * 0.5f * 0.3f;
        const auto right  = canvas_x + canvas_w - canvas_w * 0.5f * 0.3f;
        const auto top    = canvas_y + canvas_h            * 0.5f * 0.2f;
        const auto bottom = canvas_y + canvas_h - canvas_h * 0.5f * 0.2f;
        const auto center_x = (left + right) * 0.5f;
        const auto center_y = (top + bottom) * 0.5f;
        //const auto angle = AX_PI * 0.5f * 0.5f * 0.5f;

        const auto tip_top    = ImVec2(canvas_x + canvas_w * 0.5f, top);
        const auto tip_right  = ImVec2(right, center_y);
        const auto tip_bottom = ImVec2(canvas_x + canvas_w * 0.5f, bottom);

        drawList->PathLineTo(tr(ImVec2(left, top) + ImVec2(0, rounding)));
        drawList->PathBezierCurveTo(
            tr(ImVec2(left, top)),
            tr(ImVec2(left, top)),
            tr(ImVec2(left, top) + ImVec2(rounding, 0)));
        drawList->PathLineTo(tr(tip_top));
        drawList->PathLineTo(tr(tip_top + (tip_right - tip_top) * tip_round));
        drawList->PathBezierCurveTo(
            tr(tip_right),
            tr(tip_right),
            tr(tip_bottom + (tip_right - tip_bottom) * tip_round));
        drawList->PathLineTo(tr(tip_bottom));
        drawList->PathLineTo(tr(ImVec2(left, bottom) + ImVec2(rounding, 0)));
        drawList->PathBezierCurveTo(
            tr(ImVec2(left, bottom)),
            tr(ImVec2(left, bottom)),
            tr(ImVec2(left, bottom) - ImVec2(0, rounding)));

        if (type == IconType::FlowDown)
        {
            // reverse order of vertices in path, so PathFillConvex will emit
            // proper AA fringe
            auto first = drawList->_Path.Data;
            auto last = drawList->_Path.Data + drawList->_Path.Size;

            while ((first != last) && (first != --last))
            {
                ImSwap(*first, *last);
                ++first;
            }
        }

        if (!filled)
        {
            if (innerColor & 0xFF000000)
                drawList->AddConvexPolyFilled(drawList->_Path.Data, drawList->_Path.Size, innerColor);

            drawList->PathStroke(color, true, 2.0f * outline_scale);
        }
        else
            drawList->PathFillConvex(color);
    }
    else
    {
        auto triangleStart = rect_center_x + 0.32f * rect_w;

        auto rect_offset = -static_cast<int>(rect_w * 0.25f * 0.25f);

        rect.Min.x    += rect_offset;
        rect.Max.x    += rect_offset;
        rect_x        += rect_offset;
        rect_center_x += rect_offset * 0.5f;
        rect_center.x += rect_offset * 0.5f;

        if (type == IconType::Circle)
        {
            const auto c = rect_center;

            if (!filled)
            {
                const auto r = 0.5f * rect_w / 2.0f - 0.5f;

                if (innerColor & 0xFF000000)
                    drawList->AddCircleFilled(c, r, innerColor, 12 + extra_segments);
                drawList->AddCircle(c, r, color, 12 + extra_segments, 2.0f * outline_scale);
            }
            else
                drawList->AddCircleFilled(c, 0.5f * rect_w / 2.0f, color, 12 + extra_segments);
        }

        if (type == IconType::Square)
        {
            if (filled)
            {
                const auto r  = 0.5f * rect_w / 2.0f;
                const auto p0 = rect_center - ImVec2(r, r);
                const auto p1 = rect_center + ImVec2(r, r);

                drawList->AddRectFilled(p0, p1, color, 0, 15 + extra_segments);
            }
            else
            {
                const auto r = 0.5f * rect_w / 2.0f - 0.5f;
                const auto p0 = rect_center - ImVec2(r, r);
                const auto p1 = rect_center + ImVec2(r, r);

                if (innerColor & 0xFF000000)
                    drawList->AddRectFilled(p0, p1, innerColor, 0, 15 + extra_segments);

                drawList->AddRect(p0, p1, color, 0, 15 + extra_segments, 2.0f * outline_scale);
            }
        }

        if (type == IconType::Grid)
        {
            const auto r = 0.5f * rect_w / 2.0f;
            const auto w = ceilf(r / 3.0f);

            const auto baseTl = ImVec2(floorf(rect_center_x - w * 2.5f), floorf(rect_center_y - w * 2.5f));
            const auto baseBr = ImVec2(floorf(baseTl.x + w), floorf(baseTl.y + w));

            auto tl = baseTl;
            auto br = baseBr;
            for (int i = 0; i < 3; ++i)
            {
                tl.x = baseTl.x;
                br.x = baseBr.x;
                drawList->AddRectFilled(tl, br, color);
                tl.x += w * 2;
                br.x += w * 2;
                if (i != 1 || filled)
                    drawList->AddRectFilled(tl, br, color);
                tl.x += w * 2;
                br.x += w * 2;
                drawList->AddRectFilled(tl, br, color);

                tl.y += w * 2;
                br.y += w * 2;
            }

            triangleStart = br.x + w + 1.0f / 24.0f * rect_w;
        }

        if (type == IconType::RoundSquare)
        {
            if (filled)
            {
                const auto r  = 0.5f * rect_w / 2.0f;
                const auto cr = r * 0.5f;
                const auto p0 = rect_center - ImVec2(r, r);
                const auto p1 = rect_center + ImVec2(r, r);

                drawList->AddRectFilled(p0, p1, color, cr, 15);
            }
            else
            {
                const auto r = 0.5f * rect_w / 2.0f - 0.5f;
                const auto cr = r * 0.5f;
                const auto p0 = rect_center - ImVec2(r, r);
                const auto p1 = rect_center + ImVec2(r, r);

                if (innerColor & 0xFF000000)
                    drawList->AddRectFilled(p0, p1, innerColor, cr, 15);

                drawList->AddRect(p0, p1, color, cr, 15, 2.0f * outline_scale);
            }
        }
        else if (type == IconType::Diamond)
        {
            if (filled)
            {
                const auto r = 0.607f * rect_w / 2.0f;
                const auto c = rect_center;

                drawList->PathLineTo(c + ImVec2( 0, -r));
                drawList->PathLineTo(c + ImVec2( r,  0));
                drawList->PathLineTo(c + ImVec2( 0,  r));
                drawList->PathLineTo(c + ImVec2(-r,  0));
                drawList->PathFillConvex(color);
            }
            else
            {
                const auto r = 0.607f * rect_w / 2.0f - 0.5f;
                const auto c = rect_center;

                drawList->PathLineTo(c + ImVec2( 0, -r));
                drawList->PathLineTo(c + ImVec2( r,  0));
                drawList->PathLineTo(c + ImVec2( 0,  r));
                drawList->PathLineTo(c + ImVec2(-r,  0));

                if (innerColor & 0xFF000000)
                    drawList->AddConvexPolyFilled(drawList->_Path.Data, drawList->_Path.Size, innerColor);

                drawList->PathStroke(color, true, 2.0f * outline_scale);
            }
        }
        else
        {
            const auto triangleTip = triangleStart + rect_w * (0.45f - 0.32f);

            drawList->AddTriangleFilled(
                ImVec2(ceilf(triangleTip), rect_y + rect_h * 0.5f),
                ImVec2(triangleStart, rect_center_y + 0.15f * rect_h),
                ImVec2(triangleStart, rect_center_y - 0.15f * rect_h),
                color);
        }
    }
}


void ImEx::Icon(const ImVec2& size, IconType type, bool filled, const ImVec4& color/* = ImVec4(1, 1, 1, 1)*/, const ImVec4& innerColor/* = ImVec4(0, 0, 0, 0)*/)
{
    if (ImGui::IsRectVisible(size))
    {
        auto cursorPos = ImGui::GetCursorScreenPos();
        auto drawList  = ImGui::GetWindowDrawList();
        DrawIcon(drawList, cursorPos, cursorPos + size, type, filled, ImColor(color), ImColor(innerColor));
    }

    ImGui::Dummy(size);
}

void ImEx::Debug_DrawItemRect(const ImVec4& col)
{
    auto drawList = ImGui::GetWindowDrawList();
    auto itemMin = ImGui::GetItemRectMin();
    auto itemMax = ImGui::GetItemRectMax();
    drawList->AddRect(itemMin, itemMax, ImColor(col));
}




ImEx::ScopedItemWidth::ScopedItemWidth(float width)
{
    ImGui::PushItemWidth(width);
}

ImEx::ScopedItemWidth::~ScopedItemWidth()
{
    Release();
}

void ImEx::ScopedItemWidth::Release()
{
    if (m_IsDone)
        return;

    ImGui::PopItemWidth();

    m_IsDone = true;
}




ImEx::ScopedDisableItem::ScopedDisableItem(bool disable, float disabledAlpha)
    : m_Disable(disable)
{
    if (!m_Disable)
        return;

    auto wasDisabled = (ImGui::GetCurrentWindow()->DC.ItemFlags & ImGuiItemFlags_Disabled) == ImGuiItemFlags_Disabled;

    ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);

    auto& stale = ImGui::GetStyle();
    m_LastAlpha = stale.Alpha;

    // Don't override alpha if we're already in disabled context.
    if (!wasDisabled)
        stale.Alpha = disabledAlpha;
}

ImEx::ScopedDisableItem::~ScopedDisableItem()
{
    Release();
}

void ImEx::ScopedDisableItem::Release()
{
    if (!m_Disable)
        return;

    auto& stale = ImGui::GetStyle();
    stale.Alpha = m_LastAlpha;

    ImGui::PopItemFlag();

    m_Disable = false;
}




ImEx::ScopedSuspendLayout::ScopedSuspendLayout()
{
    m_Window = ImGui::GetCurrentWindow();
    m_CursorPos = m_Window->DC.CursorPos;
    m_CursorPosPrevLine = m_Window->DC.CursorPosPrevLine;
    m_CursorMaxPos = m_Window->DC.CursorMaxPos;
    m_CurrLineSize = m_Window->DC.CurrLineSize;
    m_PrevLineSize = m_Window->DC.PrevLineSize;
    m_CurrLineTextBaseOffset = m_Window->DC.CurrLineTextBaseOffset;
    m_PrevLineTextBaseOffset = m_Window->DC.PrevLineTextBaseOffset;
}

ImEx::ScopedSuspendLayout::~ScopedSuspendLayout()
{
    Release();
}

void ImEx::ScopedSuspendLayout::Release()
{
    if (m_Window == nullptr)
        return;

    m_Window->DC.CursorPos = m_CursorPos;
    m_Window->DC.CursorPosPrevLine = m_CursorPosPrevLine;
    m_Window->DC.CursorMaxPos = m_CursorMaxPos;
    m_Window->DC.CurrLineSize = m_CurrLineSize;
    m_Window->DC.PrevLineSize = m_PrevLineSize;
    m_Window->DC.CurrLineTextBaseOffset = m_CurrLineTextBaseOffset;
    m_Window->DC.PrevLineTextBaseOffset = m_PrevLineTextBaseOffset;

    m_Window = nullptr;
}
