#include "Drawing.h"
#include "Types.h"
#include "ImGuiInterop.h"
#include "Application/imgui_impl_dx11.h"
#include <cmath>

void ax::Drawing::DrawIcon(ImDrawList* drawList, const ImVec2& a, const ImVec2& b, IconType type, bool filled, ImU32 color, ImU32 innerColor)
{
    using namespace ImGuiInterop;

          auto rect           = ax::rect(to_point(a), to_point(b));
    const auto outline_scale  = rect.w / 24.0f;
    const auto extra_segments = roundi(2 * outline_scale); // for full circle

    if (type == IconType::Flow)
    {
        const auto origin_scale = rect.w / 24.0f;

        const auto offset_x  = 1.0f * origin_scale;
        const auto offset_y  = 0.0f * origin_scale;
        const auto margin     = (filled ? 2.0f : 2.0f) * origin_scale;
        const auto rounding   = 2.0f * origin_scale;
        const auto tip_round  = 0.7f; // percentage of triangle edge (for tip)
        const auto edge_round = 0.7f; // percentage of triangle edge (for corner)
        const auto canvas = rectf(
            rect.x + margin + offset_x,
            rect.y + margin + offset_y,
            rect.w - margin * 2.0f,
            rect.h - margin * 2.0f);

        const auto left   = canvas.x + canvas.w            * 0.5f * 0.3f;
        const auto right  = canvas.x + canvas.w - canvas.w * 0.5f * 0.3f;
        const auto top    = canvas.y + canvas.h            * 0.5f * 0.2f;
        const auto bottom = canvas.y + canvas.h - canvas.h * 0.5f * 0.2f;
        const auto center_y = (top + bottom) * 0.5f;
        const auto angle = AX_PI * 0.5f * 0.5f * 0.5f;

        const auto tip_top    = ImVec2(canvas.x + canvas.w * 0.5f, top);
        const auto tip_right  = ImVec2(right, center_y);
        const auto tip_bottom = ImVec2(canvas.x + canvas.w * 0.5f, bottom);

        drawList->PathLineTo(ImVec2(left, top) + ImVec2(0, rounding));
        drawList->PathBezierCurveTo(
            ImVec2(left, top),
            ImVec2(left, top),
            ImVec2(left, top) + ImVec2(rounding, 0));
        drawList->PathLineTo(tip_top);
        drawList->PathLineTo(tip_top + (tip_right - tip_top) * tip_round);
        drawList->PathBezierCurveTo(
            tip_right,
            tip_right,
            tip_bottom + (tip_right - tip_bottom) * tip_round);
        drawList->PathLineTo(tip_bottom);
        drawList->PathLineTo(ImVec2(left, bottom) + ImVec2(rounding, 0));
        drawList->PathBezierCurveTo(ImVec2(left, bottom), ImVec2(left, bottom), ImVec2(left, bottom) - ImVec2(0, rounding));

        if (!filled)
        {
            if (innerColor & 0xFF000000)
                drawList->AddConvexPolyFilled(drawList->_Path.Data, drawList->_Path.Size, innerColor, true);

            drawList->PathStroke(color, true, 2.0f * outline_scale);
        }
        else
            drawList->PathFill(color);
    }
    else
    {
        auto triangleStart = rect.center_x() + 0.32f * rect.w;

        rect.x -= roundi(rect.w * 0.25f * 0.25f);

        if (type == IconType::Circle)
        {
            const auto c = to_imvec(rect.center());

            if (!filled)
            {
                const auto r = 0.5f * rect.w / 2.0f - 0.5f;

                if (innerColor & 0xFF000000)
                    drawList->AddCircleFilled(c, r, innerColor, 12 + extra_segments);
                drawList->AddCircle(c, r, color, 12 + extra_segments, 2.0f * outline_scale);
            }
            else
                drawList->AddCircleFilled(c, 0.5f * rect.w / 2.0f, color, 12 + extra_segments);
        }

        if (type == IconType::Square)
        {
            if (filled)
            {
                const auto r  = 0.5f * rect.w / 2.0f;
                const auto p0 = to_imvec(rect.center()) - ImVec2(r, r);
                const auto p1 = to_imvec(rect.center()) + ImVec2(r, r);

                drawList->AddRectFilled(p0, p1, color, 0, 15 + extra_segments);
            }
            else
            {
                const auto r = 0.5f * rect.w / 2.0f - 0.5f;
                const auto p0 = to_imvec(rect.center()) - ImVec2(r, r);
                const auto p1 = to_imvec(rect.center()) + ImVec2(r, r);

                if (innerColor & 0xFF000000)
                    drawList->AddRectFilled(p0, p1, innerColor, 0, 15 + extra_segments);

                drawList->AddRect(p0, p1, color, 0, 15 + extra_segments, 2.0f * outline_scale);
            }
        }

        if (type == IconType::Grid)
        {
            const auto r = 0.5f * rect.w / 2.0f;
            const auto w = ceilf(r / 3.0f);

            const auto baseTl = ImVec2(floorf(rect.center_x() - w * 2.5f), floorf(rect.center_y() - w * 2.5f));
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

            triangleStart = br.x + w + 1.0f / 24.0f * rect.w;
        }

        if (type == IconType::RoundSquare)
        {
            if (filled)
            {
                const auto r  = 0.5f * rect.w / 2.0f;
                const auto cr = r * 0.5f;
                const auto p0 = to_imvec(rect.center()) - ImVec2(r, r);
                const auto p1 = to_imvec(rect.center()) + ImVec2(r, r);

                drawList->AddRectFilled(p0, p1, color, cr, 15);
            }
            else
            {
                const auto r = 0.5f * rect.w / 2.0f - 0.5f;
                const auto cr = r * 0.5f;
                const auto p0 = to_imvec(rect.center()) - ImVec2(r, r);
                const auto p1 = to_imvec(rect.center()) + ImVec2(r, r);

                if (innerColor & 0xFF000000)
                    drawList->AddRectFilled(p0, p1, innerColor, cr, 15);

                drawList->AddRect(p0, p1, color, cr, 15, 2.0f * outline_scale);
            }
        }
        else if (type == IconType::Diamond)
        {
            if (filled)
            {
                const auto r = 0.607f * rect.w / 2.0f;
                const auto c = rect.center();

                drawList->PathLineTo(to_imvec(c) + ImVec2( 0, -r));
                drawList->PathLineTo(to_imvec(c) + ImVec2( r,  0));
                drawList->PathLineTo(to_imvec(c) + ImVec2( 0,  r));
                drawList->PathLineTo(to_imvec(c) + ImVec2(-r,  0));
                drawList->PathFill(color);
            }
            else
            {
                const auto r = 0.607f * rect.w / 2.0f - 0.5f;
                const auto c = rect.center();

                drawList->PathLineTo(to_imvec(c) + ImVec2( 0, -r));
                drawList->PathLineTo(to_imvec(c) + ImVec2( r,  0));
                drawList->PathLineTo(to_imvec(c) + ImVec2( 0,  r));
                drawList->PathLineTo(to_imvec(c) + ImVec2(-r,  0));

                if (innerColor & 0xFF000000)
                    drawList->AddConvexPolyFilled(drawList->_Path.Data, drawList->_Path.Size, innerColor, true);

                drawList->PathStroke(color, true, 2.0f * outline_scale);
            }
        }
        else
        {
            const auto triangleTip = triangleStart + rect.w * (0.45f - 0.32f);

            drawList->AddTriangleFilled(
                ImVec2(ceilf(triangleTip), rect.top() + rect.h * 0.5f),
                ImVec2(triangleStart, rect.center_y() - 0.15f * rect.h),
                ImVec2(triangleStart, rect.center_y() + 0.15f * rect.h),
                color);
        }
    }
}

void ax::Drawing::DrawHeader(ImDrawList* drawList, ImTextureID textureId, const ImVec2& a, const ImVec2& b, ImU32 color, float rounding, float zoom/* = 1.0f*/)
{
    using namespace ImGuiInterop;

    const auto size = b - a;
    if (size.x == 0 || size.y == 0)
        return;

    if (textureId)
        drawList->AddDrawCmd();

    drawList->PathRect(a, b, rounding, 1 | 2);
    drawList->PathFill(ImColor(color));

    if (textureId)
    {
        auto textureWidth  = ImGui_GetTextureWidth(textureId);
        auto textureHeight = ImGui_GetTextureHeight(textureId);

        ax::matrix transform;
        transform.scale(1.0f / zoom, 1.0f / zoom);
        transform.scale(size.x / textureWidth, size.y / textureHeight);
        transform.scale(1.0f / (size.x - 1.0f), 1.0f / (size.y - 1.0f));
        transform.translate(-a.x - 0.5f, -a.y - 0.5f);

        ImGui_PushGizmo(drawList, textureId, (float*)&transform);
        drawList->AddDrawCmd();
    }
}

static float EaseLinkStrength(const ImVec2& a, const ImVec2& b, float strength)
{
    const auto distanceX    = b.x - a.x;
    const auto distanceY    = b.y - a.y;
    const auto distance     = sqrtf(distanceX * distanceX + distanceY * distanceY);
    const auto halfDistance = distance * 0.5f;

    if (halfDistance < strength)
        strength = strength * sinf(ax::AX_PI * 0.5f * halfDistance / strength);

    return strength;
}

void ax::Drawing::DrawLink(ImDrawList* drawList, const ImVec2& a, const ImVec2& b, ImU32 color, float thickness/* = 1.0f*/, float strength/* = 1.0f*/)
{
    using namespace ImGuiInterop;

    if (strength != 0.0f)
    {
        strength = EaseLinkStrength(a, b, strength);

        ImVec2 cp0 = ImVec2(a.x + strength, a.y);
        ImVec2 cp1 = ImVec2(b.x - strength, b.y);

        //drawList->AddCircleFilled(cp0, 4.0f, 0xFFFF00FF);
        //drawList->AddCircleFilled(cp1, 4.0f, 0xFFFF00FF);

        drawList->AddBezierCurve(a, cp0, cp1, b, color, thickness);
    }
    else
        drawList->AddLine(a, b, color, thickness);
}

float ax::Drawing::LinkDistance(const ImVec2& p, const ImVec2& a, const ImVec2& b, float strength/* = 1.0f*/)
{
    using namespace ImGuiInterop;

    strength = EaseLinkStrength(a, b, strength);

    ImVec2 cp0 = ImVec2(a.x + strength, a.y);
    ImVec2 cp1 = ImVec2(b.x - strength, b.y);

    auto result = bezier_project_point(to_pointf(p),
        to_pointf(a), to_pointf(cp0), to_pointf(cp1), to_pointf(b), 50);

    return result.distance;
}

ax::rectf ax::Drawing::GetLinkBounds(const ImVec2& a, const ImVec2& b, float strength/* = 1.0f*/)
{
    using namespace ImGuiInterop;

    if (strength != 0.0f)
    {
        strength = EaseLinkStrength(a, b, strength);

        ImVec2 cp0 = ImVec2(a.x + strength, a.y);
        ImVec2 cp1 = ImVec2(b.x - strength, b.y);

        return bezier_bounding_rect(to_pointf(a), to_pointf(cp0), to_pointf(cp1), to_pointf(b));
    }
    else
    {
        return rectf(to_pointf(a).cwise_min(to_pointf(b)), to_pointf(a).cwise_max(to_pointf(b)));
    }

    // Build bounding rectangle of link.
    //auto topLeft     = to_pointf(a);
    //auto bottomRight = to_pointf(b);

    //// Links are drawn as Bezier quadratic curves with
    //// start point tangent always pointing to right, end tangent
    //// pointing to left. If curve is drawn from right to left
    //// there is small overshot outside of bounding box based on
    //// tangent strength.
    //if (topLeft.x > bottomRight.x)
    //{
    //    std::swap(topLeft.x, bottomRight.x);
    //    topLeft.x     = topLeft.x - strength * 0.25f;
    //    bottomRight.x = bottomRight.x + strength * 0.25f;
    //}

    //if (topLeft.y > bottomRight.y)
    //    std::swap(topLeft.y, bottomRight.y);

    //return rectf(topLeft, bottomRight);
}

bool ax::Drawing::CollideLinkWithRect(const ax::rect& r, const ImVec2& a, const ImVec2& b, float strength/* = 1.0f*/)
{
    using namespace ImGuiInterop;

    strength = EaseLinkStrength(a, b, strength);

    ImVec2 cp0 = ImVec2(a.x + strength, a.y);
    ImVec2 cp1 = ImVec2(b.x - strength, b.y);

    auto p0 = r.top_left();
    auto p1 = r.top_right();
    auto p2 = r.bottom_right();
    auto p3 = r.bottom_left();

    pointf points[3];
    if (bezier_line_intersect(to_pointf(a), to_pointf(cp0), to_pointf(cp1), to_pointf(b), to_pointf(p0), to_pointf(p1), points) > 0)
        return true;
    if (bezier_line_intersect(to_pointf(a), to_pointf(cp0), to_pointf(cp1), to_pointf(b), to_pointf(p1), to_pointf(p2), points) > 0)
        return true;
    if (bezier_line_intersect(to_pointf(a), to_pointf(cp0), to_pointf(cp1), to_pointf(b), to_pointf(p2), to_pointf(p3), points) > 0)
        return true;
    if (bezier_line_intersect(to_pointf(a), to_pointf(cp0), to_pointf(cp1), to_pointf(b), to_pointf(p3), to_pointf(p0), points) > 0)
        return true;

    return false;
}