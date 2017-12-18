//------------------------------------------------------------------------------
// LICENSE
//   This software is dual-licensed to the public domain and under the following
//   license: you are granted a perpetual, irrevocable license to copy, modify,
//   publish, and distribute this file as you see fit.
//
// CREDITS
//   Written by Michal Cichon
//------------------------------------------------------------------------------
# include "Editor.h"
# include <cstdio> // snprintf
# include <string>
# include <fstream>
# include <bitset>
# include <climits>


//------------------------------------------------------------------------------
namespace ed = ax::NodeEditor::Detail;


//------------------------------------------------------------------------------
static const int c_BackgroundChannelCount = 1;
static const int c_LinkChannelCount       = 4;
static const int c_UserLayersCount        = 4;

static const int c_UserLayerChannelStart  = 0;
static const int c_BackgroundChannelStart = c_UserLayerChannelStart  + c_UserLayersCount;
static const int c_LinkStartChannel       = c_BackgroundChannelStart + c_BackgroundChannelCount;
static const int c_NodeStartChannel       = c_LinkStartChannel       + c_LinkChannelCount;

static const int c_BackgroundChannel_SelectionRect = c_BackgroundChannelStart + 0;

static const int c_UserChannel_Content         = c_UserLayerChannelStart + 0;
static const int c_UserChannel_Grid            = c_UserLayerChannelStart + 1;
static const int c_UserChannel_HintsBackground = c_UserLayerChannelStart + 2;
static const int c_UserChannel_Hints           = c_UserLayerChannelStart + 3;

static const int c_LinkChannel_Selection  = c_LinkStartChannel + 0;
static const int c_LinkChannel_Links      = c_LinkStartChannel + 1;
static const int c_LinkChannel_Flow       = c_LinkStartChannel + 2;
static const int c_LinkChannel_NewLink    = c_LinkStartChannel + 3;

static const int c_ChannelsPerNode           = 5;
static const int c_NodeBaseChannel           = 0;
static const int c_NodeBackgroundChannel     = 1;
static const int c_NodeUserBackgroundChannel = 2;
static const int c_NodePinChannel            = 3;
static const int c_NodeContentChannel        = 4;

static const float c_GroupSelectThickness       = 3.0f;  // canvas pixels
static const float c_LinkSelectThickness        = 5.0f;  // canvas pixels
static const float c_NavigationZoomMargin       = 0.1f;  // percentage of visible bounds
static const float c_MouseZoomDuration          = 0.15f; // seconds
static const float c_SelectionFadeOutDuration   = 0.15f; // seconds
static const auto  c_ScrollButtonIndex          = 1;


//------------------------------------------------------------------------------
# if defined(_DEBUG) && defined(_WIN32)
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
# endif

void ed::Log(const char* fmt, ...)
{
# if defined(_DEBUG) && defined(_WIN32)
    va_list args;
    va_start(args, fmt);
    LogV(fmt, args);
    va_end(args);
# endif
}


//------------------------------------------------------------------------------
static bool IsGroup(const ed::Node* node)
{
    if (node && node->m_Type == ed::NodeType::Group)
        return true;
    else
        return false;
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

static void ImDrawList_TransformChannel_Inner(ImVector<ImDrawVert>& vtxBuffer, const ImVector<ImDrawIdx>& idxBuffer, const ImVector<ImDrawCmd>& cmdBuffer, const ImVec2& preOffset, const ImVec2& scale, const ImVec2& postOffset)
{
    auto idxRead = idxBuffer.Data;

    int indexOffset = 0;
    for (auto& cmd : cmdBuffer)
    {
        auto idxCount = cmd.ElemCount;

        if (idxCount == 0) continue;

        auto minIndex = idxRead[indexOffset];
        auto maxIndex = idxRead[indexOffset];

        for (auto i = 1u; i < idxCount; ++i)
        {
            auto idx = idxRead[indexOffset + i];
            minIndex = std::min(minIndex, idx);
            maxIndex = std::max(maxIndex, idx);
        }

        for (auto vtx = vtxBuffer.Data + minIndex, vtxEnd = vtxBuffer.Data + maxIndex + 1; vtx < vtxEnd; ++vtx)
        {
            vtx->pos.x = (vtx->pos.x + preOffset.x) * scale.x + postOffset.x;
            vtx->pos.y = (vtx->pos.y + preOffset.y) * scale.y + postOffset.y;
        }

        indexOffset += idxCount;
    }
}

static void ImDrawList_TransformChannels(ImDrawList* drawList, int begin, int end, const ImVec2& preOffset, const ImVec2& scale, const ImVec2& postOffset)
{
    int lastCurrentChannel = drawList->_ChannelsCurrent;
    if (lastCurrentChannel != 0)
        drawList->ChannelsSetCurrent(0);

    auto& vtxBuffer = drawList->VtxBuffer;

    if (begin == 0 && begin != end)
    {
        ImDrawList_TransformChannel_Inner(vtxBuffer, drawList->IdxBuffer, drawList->CmdBuffer, preOffset, scale, postOffset);
        ++begin;
    }

    for (int channelIndex = begin; channelIndex < end; ++channelIndex)
    {
        auto& channel = drawList->_Channels[channelIndex];
        ImDrawList_TransformChannel_Inner(vtxBuffer, channel.IdxBuffer, channel.CmdBuffer, preOffset, scale, postOffset);
    }

    if (lastCurrentChannel != 0)
        drawList->ChannelsSetCurrent(lastCurrentChannel);
}

static void ImDrawList_ClampClipRects_Inner(ImVector<ImDrawCmd>& cmdBuffer, const ImVec4& clipRect, const ImVec2& offset)
{
    for (auto& cmd : cmdBuffer)
    {
        cmd.ClipRect.x = std::max(cmd.ClipRect.x + offset.x, clipRect.x);
        cmd.ClipRect.y = std::max(cmd.ClipRect.y + offset.y, clipRect.y);
        cmd.ClipRect.z = std::min(cmd.ClipRect.z + offset.x, clipRect.z);
        cmd.ClipRect.w = std::min(cmd.ClipRect.w + offset.y, clipRect.w);
    }
}

static void ImDrawList_TranslateAndClampClipRects(ImDrawList* drawList, int begin, int end, const ImVec2& offset)
{
    int lastCurrentChannel = drawList->_ChannelsCurrent;
    if (lastCurrentChannel != 0)
        drawList->ChannelsSetCurrent(0);

    auto clipRect = drawList->_ClipRectStack.back();

    if (begin == 0 && begin != end)
    {
        ImDrawList_ClampClipRects_Inner(drawList->CmdBuffer, clipRect, offset);
        ++begin;
    }

    for (int channelIndex = begin; channelIndex < end; ++channelIndex)
    {
        auto& channel = drawList->_Channels[channelIndex];
        ImDrawList_ClampClipRects_Inner(channel.CmdBuffer, clipRect, offset);
    }

    if (lastCurrentChannel != 0)
        drawList->ChannelsSetCurrent(lastCurrentChannel);
}

static void ImDrawList_PathBezierOffset(ImDrawList* drawList, float offset, const ImVec2& p0, const ImVec2& p1, const ImVec2& p2, const ImVec2& p3)
{
    using namespace ax;

    auto acceptPoint = [drawList, offset](const bezier_subdivide_result_t& r)
    {
        drawList->PathLineTo(to_imvec(r.point + ax::pointf(-r.tangent.y, r.tangent.x).normalized() * offset));
    };

    cubic_bezier_subdivide(acceptPoint, to_pointf(p0), to_pointf(p1), to_pointf(p2), to_pointf(p3));
}

/*
static void ImDrawList_PolyFillScanFlood(ImDrawList *draw, std::vector<ImVec2>* poly, ImColor color, int gap = 1, float strokeWidth = 1.0f)
{
    std::vector<ImVec2> scanHits;
    ImVec2 min, max; // polygon min/max points
    auto io = ImGui::GetIO();
    float y;
    bool isMinMaxDone = false;
    unsigned int polysize = poly->size();

    // find the orthagonal bounding box
    // probably can put this as a predefined
    if (!isMinMaxDone)
    {
        min.x = min.y = FLT_MAX;
        max.x = max.y = FLT_MIN;
        for (auto p : *poly)
        {
            if (p.x < min.x) min.x = p.x;
            if (p.y < min.y) min.y = p.y;
            if (p.x > max.x) max.x = p.x;
            if (p.y > max.y) max.y = p.y;
        }
        isMinMaxDone = true;
    }

    // Bounds check
    if ((max.x < 0) || (min.x > io.DisplaySize.x) || (max.y < 0) || (min.y > io.DisplaySize.y)) return;

    // Vertically clip
    if (min.y < 0) min.y = 0;
    if (max.y > io.DisplaySize.y) max.y = io.DisplaySize.y;

    // so we know we start on the outside of the object we step out by 1.
    min.x -= 1;
    max.x += 1;

    // Initialise our starting conditions
    y = min.y;

    // Go through each scan line iteratively, jumping by 'gap' pixels each time
    while (y < max.y)
    {
        scanHits.clear();

        {
            int jump = 1;
            ImVec2 fp = poly->at(0);

            for (size_t i = 0; i < polysize - 1; i++)
            {
                ImVec2 pa = poly->at(i);
                ImVec2 pb = poly->at(i + 1);

                // jump double/dud points
                if (pa.x == pb.x && pa.y == pb.y) continue;

                // if we encounter our hull/poly start point, then we've now created the
                // closed
                // hull, jump the next segment and reset the first-point
                if ((!jump) && (fp.x == pb.x) && (fp.y == pb.y))
                {
                    if (i < polysize - 2)
                    {
                        fp = poly->at(i + 2);
                        jump = 1;
                        i++;
                    }
                }
                else
                {
                    jump = 0;
                }

                // test to see if this segment makes the scan-cut.
                if ((pa.y > pb.y && y < pa.y && y > pb.y) || (pa.y < pb.y && y > pa.y && y < pb.y))
                {
                    ImVec2 intersect;

                    intersect.y = y;
                    if (pa.x == pb.x)
                    {
                        intersect.x = pa.x;
                    }
                    else
                    {
                        intersect.x = (pb.x - pa.x) / (pb.y - pa.y) * (y - pa.y) + pa.x;
                    }
                    scanHits.push_back(intersect);
                }
            }

            // Sort the scan hits by X, so we have a proper left->right ordering
            sort(scanHits.begin(), scanHits.end(), [](ImVec2 const &a, ImVec2 const &b) { return a.x < b.x; });

            // generate the line segments.
            {
                int i = 0;
                int l = scanHits.size() - 1; // we need pairs of points, this prevents segfault.
                for (i = 0; i < l; i += 2)
                {
                    draw->AddLine(scanHits[i], scanHits[i + 1], color, strokeWidth);
                }
            }
        }
        y += gap;
    } // for each scan line
    scanHits.clear();
}
*/

static void ImDrawList_AddBezierWithArrows(ImDrawList* drawList, const ax::cubic_bezier_t& curve, float thickness,
    float startArrowSize, float startArrowWidth, float endArrowSize, float endArrowWidth,
    bool fill, ImU32 color, float strokeThickness)
{
    using namespace ax;

    if ((color >> 24) == 0)
        return;

    const auto half_thickness = thickness * 0.5f;

    if (fill)
    {
        drawList->AddBezierCurve(to_imvec(curve.p0), to_imvec(curve.p1), to_imvec(curve.p2), to_imvec(curve.p3), color, thickness);

        if (startArrowSize > 0.0f)
        {
            const auto start_dir  = curve.tangent(0.0f).normalized();
            const auto start_n    = pointf(-start_dir.y, start_dir.x);
            const auto half_width = startArrowWidth * 0.5f;
            const auto tip        = curve.p0 - start_dir * startArrowSize;

            drawList->PathLineTo(to_imvec(curve.p0 - start_n * std::max(half_width, half_thickness)));
            drawList->PathLineTo(to_imvec(curve.p0 + start_n * std::max(half_width, half_thickness)));
            drawList->PathLineTo(to_imvec(tip));
            drawList->PathFill(color);
        }

        if (endArrowSize > 0.0f)
        {
            const auto    end_dir = curve.tangent(1.0f).normalized();
            const auto    end_n   = pointf(  -end_dir.y,   end_dir.x);
            const auto half_width = endArrowWidth * 0.5f;
            const auto tip        = curve.p3 + end_dir * endArrowSize;

            drawList->PathLineTo(to_imvec(curve.p3 + end_n * std::max(half_width, half_thickness)));
            drawList->PathLineTo(to_imvec(curve.p3 - end_n * std::max(half_width, half_thickness)));
            drawList->PathLineTo(to_imvec(tip));
            drawList->PathFill(color);
        }
    }
    else
    {
        if (startArrowSize > 0.0f)
        {
            const auto start_dir  = curve.tangent(0.0f).normalized();
            const auto start_n    = pointf(-start_dir.y, start_dir.x);
            const auto half_width = startArrowWidth * 0.5f;
            const auto tip        = curve.p0 - start_dir * startArrowSize;

            if (half_width > half_thickness)
                drawList->PathLineTo(to_imvec(curve.p0 - start_n * half_width));
            drawList->PathLineTo(to_imvec(tip));
            if (half_width > half_thickness)
                drawList->PathLineTo(to_imvec(curve.p0 + start_n * half_width));
        }

        ImDrawList_PathBezierOffset(drawList, half_thickness, to_imvec(curve.p0), to_imvec(curve.p1), to_imvec(curve.p2), to_imvec(curve.p3));

        if (endArrowSize > 0.0f)
        {
            const auto    end_dir = curve.tangent(1.0f).normalized();
            const auto    end_n   = pointf(  -end_dir.y,   end_dir.x);
            const auto half_width = endArrowWidth * 0.5f;
            const auto tip        = curve.p3 + end_dir * endArrowSize;

            if (half_width > half_thickness)
                drawList->PathLineTo(to_imvec(curve.p3 + end_n * half_width));
            drawList->PathLineTo(to_imvec(tip));
            if (half_width > half_thickness)
                drawList->PathLineTo(to_imvec(curve.p3 - end_n * half_width));
        }

        ImDrawList_PathBezierOffset(drawList, half_thickness, to_imvec(curve.p3), to_imvec(curve.p2), to_imvec(curve.p1), to_imvec(curve.p0));

        drawList->PathStroke(color, true, strokeThickness);
    }
}




//------------------------------------------------------------------------------
//
// Pin
//
//------------------------------------------------------------------------------
void ed::Pin::Draw(ImDrawList* drawList, DrawFlags flags)
{
    if (flags & Hovered)
    {
        drawList->ChannelsSetCurrent(m_Node->m_Channel + c_NodePinChannel);

        drawList->AddRectFilled(to_imvec(m_Bounds.top_left()), to_imvec(m_Bounds.bottom_right()),
            m_Color, m_Rounding, m_Corners);

        if (m_BorderWidth > 0.0f)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_AntiAliasFringeScale, 1.0f);
            drawList->AddRect(to_imvec(m_Bounds.top_left()), to_imvec(m_Bounds.bottom_right()),
                m_BorderColor, m_Rounding, m_Corners, m_BorderWidth);
            ImGui::PopStyleVar();
        }

        if (!Editor->IsSelected(m_Node))
            m_Node->Draw(drawList, flags);
    }
}

ImVec2 ed::Pin::GetClosestPoint(const ImVec2& p) const
{
    return to_imvec(m_Pivot.get_closest_point(to_pointf(p), true, m_Radius + m_ArrowSize));
}

ax::line_f ed::Pin::GetClosestLine(const Pin* pin) const
{
    return m_Pivot.get_closest_line(pin->m_Pivot, m_Radius + m_ArrowSize, pin->m_Radius + pin->m_ArrowSize);
}




//------------------------------------------------------------------------------
//
// Node
//
//------------------------------------------------------------------------------
bool ed::Node::AcceptDrag()
{
    m_DragStart = m_Bounds.location;
    return true;
}

void ed::Node::UpdateDrag(const ax::point& offset)
{
    m_Bounds.location = m_DragStart + offset;
}

bool ed::Node::EndDrag()
{
    return m_Bounds.location != m_DragStart;
}

void ed::Node::Draw(ImDrawList* drawList, DrawFlags flags)
{
    if (flags == Detail::Object::None)
    {
        drawList->ChannelsSetCurrent(m_Channel + c_NodeBackgroundChannel);

        drawList->AddRectFilled(
            to_imvec(m_Bounds.top_left()),
            to_imvec(m_Bounds.bottom_right()),
            m_Color, m_Rounding);

        if (IsGroup(this))
        {
            drawList->AddRectFilled(
                to_imvec(m_GroupBounds.top_left()),
                to_imvec(m_GroupBounds.bottom_right()),
                m_GroupColor, m_GroupRounding);

            if (m_GroupBorderWidth > 0.0f)
            {
                ImGui::PushStyleVar(ImGuiStyleVar_AntiAliasFringeScale, 1.0f);

                drawList->AddRect(
                    to_imvec(m_GroupBounds.top_left()),
                    to_imvec(m_GroupBounds.bottom_right()),
                    m_GroupBorderColor, m_GroupRounding, 15, m_GroupBorderWidth);

                ImGui::PopStyleVar();
            }
        }

        DrawBorder(drawList, m_BorderColor, m_BorderWidth);
    }
    else if (flags & Selected)
    {
        const auto  borderColor = Editor->GetColor(StyleColor_SelNodeBorder);
        const auto& editorStyle = Editor->GetStyle();

        drawList->ChannelsSetCurrent(m_Channel + c_NodeBaseChannel);

        DrawBorder(drawList, borderColor, editorStyle.SelectedNodeBorderWidth);
    }
    else if (!IsGroup(this) && (flags & Hovered))
    {
        const auto  borderColor = Editor->GetColor(StyleColor_HovNodeBorder);
        const auto& editorStyle = Editor->GetStyle();

        drawList->ChannelsSetCurrent(m_Channel + c_NodeBaseChannel);

        DrawBorder(drawList, borderColor, editorStyle.HoveredNodeBorderWidth);
    }
}

void ed::Node::DrawBorder(ImDrawList* drawList, ImU32 color, float thickness)
{
    if (thickness > 0.0f)
    {
        drawList->AddRect(to_imvec(m_Bounds.top_left()), to_imvec(m_Bounds.bottom_right()),
            color, m_Rounding, 15, thickness);
    }
}

void ed::Node::GetGroupedNodes(std::vector<Node*>& result, bool append)
{
    if (!append)
        result.resize(0);

    if (!IsGroup(this))
        return;

    const auto firstNodeIndex = result.size();
    Editor->FindNodesInRect(static_cast<ax::rectf>(m_GroupBounds), result, true, false);

    for (auto index = firstNodeIndex; index < result.size(); ++index)
        result[index]->GetGroupedNodes(result, true);
}




//------------------------------------------------------------------------------
//
// Link
//
//------------------------------------------------------------------------------
void ed::Link::Draw(ImDrawList* drawList, DrawFlags flags)
{
    if (flags == None)
    {
        drawList->ChannelsSetCurrent(c_LinkChannel_Links);

        Draw(drawList, m_Color, 0.0f);
    }
    else if (flags & Selected)
    {
        const auto borderColor = Editor->GetColor(StyleColor_SelLinkBorder);

        drawList->ChannelsSetCurrent(c_LinkChannel_Selection);

        Draw(drawList, borderColor, 4.5f);
    }
    else if (flags & Hovered)
    {
        const auto borderColor = Editor->GetColor(StyleColor_HovLinkBorder);

        drawList->ChannelsSetCurrent(c_LinkChannel_Selection);

        Draw(drawList, borderColor, 2.0f);
    }
}

void ed::Link::Draw(ImDrawList* drawList, ImU32 color, float extraThickness) const
{
    if (!m_IsLive)
        return;

    const auto curve = GetCurve();

    ImDrawList_AddBezierWithArrows(drawList, curve, m_Thickness + extraThickness,
        m_StartPin && m_StartPin->m_ArrowSize  > 0.0f ? m_StartPin->m_ArrowSize  + extraThickness : 0.0f,
        m_StartPin && m_StartPin->m_ArrowWidth > 0.0f ? m_StartPin->m_ArrowWidth + extraThickness : 0.0f,
          m_EndPin &&   m_EndPin->m_ArrowSize  > 0.0f ?   m_EndPin->m_ArrowSize  + extraThickness : 0.0f,
          m_EndPin &&   m_EndPin->m_ArrowWidth > 0.0f ?   m_EndPin->m_ArrowWidth + extraThickness : 0.0f,
        true, color, 1.0f);
}

void ed::Link::UpdateEndpoints()
{
    const auto line = m_StartPin->GetClosestLine(m_EndPin);
    m_Start = to_imvec(line.a);
    m_End   = to_imvec(line.b);
}

ax::cubic_bezier_t ed::Link::GetCurve() const
{
    auto easeLinkStrength = [](const ImVec2& a, const ImVec2& b, float strength)
    {
        const auto distanceX    = b.x - a.x;
        const auto distanceY    = b.y - a.y;
        const auto distance     = sqrtf(distanceX * distanceX + distanceY * distanceY);
        const auto halfDistance = distance * 0.5f;

        if (halfDistance < strength)
            strength = strength * sinf(ax::AX_PI * 0.5f * halfDistance / strength);

        return strength;
    };

    const auto startStrength = easeLinkStrength(m_Start, m_End, m_StartPin->m_Strength);
    const auto   endStrength = easeLinkStrength(m_Start, m_End,   m_EndPin->m_Strength);
    const auto           cp0 = m_Start + m_StartPin->m_Dir * startStrength;
    const auto           cp1 =   m_End +   m_EndPin->m_Dir *   endStrength;

    return ax::cubic_bezier_t { to_pointf(m_Start), to_pointf(cp0), to_pointf(cp1), to_pointf(m_End) };
}

bool ed::Link::TestHit(const ImVec2& point, float extraThickness) const
{
    if (!m_IsLive)
        return false;

    auto bounds = GetBounds();
    if (extraThickness > 0.0f)
        bounds.expand(extraThickness);

    if (!bounds.contains(to_pointf(point)))
        return false;

    const auto bezier = GetCurve();
    const auto result = cubic_bezier_project_point(to_pointf(point), bezier.p0, bezier.p1, bezier.p2, bezier.p3, 50);

    return result.distance <= m_Thickness + extraThickness;
}

bool ed::Link::TestHit(const ax::rectf& rect, bool allowIntersect) const
{
    if (!m_IsLive)
        return false;

    const auto bounds = GetBounds();

    if (rect.contains(bounds))
        return true;

    if (!allowIntersect || !rect.intersects(bounds))
        return false;

    const auto bezier = GetCurve();

    const auto p0 = rect.top_left();
    const auto p1 = rect.top_right();
    const auto p2 = rect.bottom_right();
    const auto p3 = rect.bottom_left();

    pointf points[3];
    if (cubic_bezier_line_intersect(bezier.p0, bezier.p1, bezier.p2, bezier.p3, p0, p1, points) > 0)
        return true;
    if (cubic_bezier_line_intersect(bezier.p0, bezier.p1, bezier.p2, bezier.p3, p1, p2, points) > 0)
        return true;
    if (cubic_bezier_line_intersect(bezier.p0, bezier.p1, bezier.p2, bezier.p3, p2, p3, points) > 0)
        return true;
    if (cubic_bezier_line_intersect(bezier.p0, bezier.p1, bezier.p2, bezier.p3, p3, p0, points) > 0)
        return true;

    return false;
}

ax::rectf ed::Link::GetBounds() const
{
    if (m_IsLive)
    {
        const auto curve = GetCurve();
        auto bounds = cubic_bezier_bounding_rect(curve.p0, curve.p1, curve.p2, curve.p3);

        if (bounds.w == 0.0f)
        {
            bounds.x -= 0.5f;
            bounds.w  = 1.0f;
        }

        if (bounds.h == 0.0f)
        {
            bounds.y -= 0.5f;
            bounds.h = 1.0f;
        }

        if (m_StartPin->m_ArrowSize)
        {
            const auto start_dir = curve.tangent(0.0f).normalized();
            const auto p0 = curve.p0;
            const auto p1 = curve.p0 - start_dir * m_StartPin->m_ArrowSize;
            const auto min = p0.cwise_min(p1);
            const auto max = p0.cwise_max(p1);
            auto arrowBounds = rectf(min, max);
            arrowBounds.w = std::max(arrowBounds.w, 1.0f);
            arrowBounds.h = std::max(arrowBounds.h, 1.0f);
            bounds = make_union(bounds, arrowBounds);
        }

        if (m_EndPin->m_ArrowSize)
        {
            const auto end_dir = curve.tangent(0.0f).normalized();
            const auto p0 = curve.p3;
            const auto p1 = curve.p3 + end_dir * m_EndPin->m_ArrowSize;
            const auto min = p0.cwise_min(p1);
            const auto max = p0.cwise_max(p1);
            auto arrowBounds = rectf(min, max);
            arrowBounds.w = std::max(arrowBounds.w, 1.0f);
            arrowBounds.h = std::max(arrowBounds.h, 1.0f);
            bounds = make_union(bounds, arrowBounds);
        }

        return bounds;
    }
    else
        return ax::rectf();
}




//------------------------------------------------------------------------------
//
// Editor Context
//
//------------------------------------------------------------------------------
ed::EditorContext::EditorContext(const ax::NodeEditor::Config* config):
    m_IsFirstFrame(true),
    m_IsWindowActive(false),
    m_ShortcutsEnabled(true),
    m_Style(),
    m_Nodes(),
    m_Pins(),
    m_Links(),
    m_SelectionId(1),
    m_LastActiveLink(nullptr),
    m_MousePosBackup(0, 0),
    m_MousePosPrevBackup(0, 0),
    m_MouseClickPosBackup(),
    m_Canvas(),
    m_SuspendCount(0),
    m_NodeBuilder(this),
    m_HintBuilder(this),
    m_CurrentAction(nullptr),
    m_NavigateAction(this),
    m_SizeAction(this),
    m_DragAction(this),
    m_SelectAction(this),
    m_ContextMenuAction(this),
    m_ShortcutAction(this),
    m_CreateItemAction(this),
    m_DeleteItemsAction(this),
    m_AnimationControllers{ &m_FlowAnimationController },
    m_FlowAnimationController(this),
    m_DoubleClickedNode(0),
    m_DoubleClickedPin(0),
    m_DoubleClickedLink(0),
    m_BackgroundClicked(false),
    m_BackgroundDoubleClicked(false),
    m_IsInitialized(false),
    m_Settings(),
    m_Config(config)
{
}

ed::EditorContext::~EditorContext()
{
    if (m_IsInitialized)
        SaveSettings();

    for (auto link  : m_Links)  delete link.m_Object;
    for (auto pin   : m_Pins)   delete pin.m_Object;
    for (auto node  : m_Nodes)  delete node.m_Object;
}

void ed::EditorContext::Begin(const char* id, const ImVec2& size)
{
    if (!m_IsInitialized)
    {
        LoadSettings();
        m_IsInitialized = true;
    }

    //ImGui::LogToClipboard();
    //Log("---- begin ----");

    for (auto node  : m_Nodes)   node->Reset();
    for (auto pin   : m_Pins)     pin->Reset();
    for (auto link  : m_Links)   link->Reset();

    ImGui::PushStyleColor(ImGuiCol_ChildWindowBg, ImColor(0, 0, 0, 0));
    ImGui::BeginChild(id, size, false,
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse);

    ImGui::CaptureKeyboardFromApp();

    m_IsWindowActive = ImGui::IsWindowFocused();

    //
    if (m_CurrentAction && m_CurrentAction->IsDragging() && m_NavigateAction.MoveOverEdge())
    {
        auto& io = ImGui::GetIO();
        auto offset = m_NavigateAction.GetMoveOffset();
        for (int i = 0; i < 5; ++i)
            io.MouseClickedPos[i] = io.MouseClickedPos[i] - offset;
    }
    else
        m_NavigateAction.StopMoveOverEdge();

    // Setup canvas
    m_NavigateAction.SetWindow(ImGui::GetWindowPos(), ImGui::GetWindowSize());

    m_Canvas = m_NavigateAction.GetCanvas();

    ImGui::PushStyleVar(ImGuiStyleVar_AntiAliasFringeScale, std::min(std::max(m_Canvas.InvZoom.x, m_Canvas.InvZoom.y), 1.0f));

    // Save mouse positions
    auto& io = ImGui::GetIO();
    m_MousePosBackup = io.MousePos;
    m_MousePosPrevBackup = io.MousePosPrev;
    for (int i = 0; i < 5; ++i)
        m_MouseClickPosBackup[i] = io.MouseClickedPos[i];

    // #debug
    //auto drawList2 = ImGui::GetWindowDrawList();
    //drawList2->PushClipRectFullScreen();
    //drawList2->AddRect(Canvas.FromClient(ImVec2(0, 0)), Canvas.FromClient(ImVec2(0, 0)) + Canvas.ClientSize, IM_COL32(255, 0, 255, 255), 0, 15, 4.0f);
    //drawList2->PopClipRect();

    auto clipMin = m_Canvas.FromScreen(m_Canvas.WindowScreenPos);
    auto clipMax = m_Canvas.FromScreen(m_Canvas.WindowScreenPos) + m_Canvas.ClientSize;

    // #debug #clip
    //ImGui::Text("CLIP = { x=%g y=%g w=%g h=%g r=%g b=%g }",
    //    clipMin.x, clipMin.y, clipMax.x - clipMin.x, clipMax.y - clipMin.y, clipMax.x, clipMax.y);

    ImGui::PushClipRect(clipMin, clipMax, false);

    CaptureMouse();

    // Reserve channels for background and links
    auto drawList = ImGui::GetWindowDrawList();
    ImDrawList_ChannelsGrow(drawList, c_NodeStartChannel);

    if (HasSelectionChanged())
        ++m_SelectionId;

    m_LastSelectedObjects = m_SelectedObjects;
}

void ed::EditorContext::End()
{
    //auto& io          = ImGui::GetIO();
    auto  control     = BuildControl(m_CurrentAction && m_CurrentAction->IsDragging()); // NavigateAction.IsMovingOverEdge()
    auto  drawList    = ImGui::GetWindowDrawList();
    //auto& editorStyle = GetStyle();

    m_DoubleClickedNode       = control.DoubleClickedNode ? control.DoubleClickedNode->m_ID : 0;
    m_DoubleClickedPin        = control.DoubleClickedPin  ? control.DoubleClickedPin->m_ID  : 0;
    m_DoubleClickedLink       = control.DoubleClickedLink ? control.DoubleClickedLink->m_ID : 0;
    m_BackgroundClicked       = control.BackgroundClicked;
    m_BackgroundDoubleClicked = control.BackgroundDoubleClicked;

    //if (DoubleClickedNode) LOG_TRACE(0, "DOUBLE CLICK NODE: %d", DoubleClickedNode);
    //if (DoubleClickedPin)  LOG_TRACE(0, "DOUBLE CLICK PIN:  %d", DoubleClickedPin);
    //if (DoubleClickedLink) LOG_TRACE(0, "DOUBLE CLICK LINK: %d", DoubleClickedLink);
    //if (BackgroundDoubleClicked) LOG_TRACE(0, "DOUBLE CLICK BACKGROUND", DoubleClickedLink);

    const bool isSelecting = m_CurrentAction && m_CurrentAction->AsSelect() != nullptr;
    const bool isDragging  = m_CurrentAction && m_CurrentAction->AsDrag()   != nullptr;
    //const bool isSizing    = CurrentAction && CurrentAction->AsSize()   != nullptr;

    // Draw nodes
    for (auto node : m_Nodes)
        if (node->m_IsLive && node->IsVisible())
            node->Draw(drawList);

    // Draw links
    for (auto link : m_Links)
        if (link->m_IsLive && link->IsVisible())
            link->Draw(drawList);

    // Highlight selected objects
    {
        auto selectedObjects = &m_SelectedObjects;
        if (auto selectAction = m_CurrentAction ? m_CurrentAction->AsSelect() : nullptr)
            selectedObjects = &selectAction->m_CandidateObjects;

        for (auto selectedObject : *selectedObjects)
            if (selectedObject->IsVisible())
                selectedObject->Draw(drawList, Object::Selected);
    }

    if (!isSelecting)
    {
        auto hoveredObject = control.HotObject;
        if (auto dragAction = m_CurrentAction ? m_CurrentAction->AsDrag() : nullptr)
            hoveredObject = dragAction->m_DraggedObject;
        if (auto sizeAction = m_CurrentAction ? m_CurrentAction->AsSize() : nullptr)
            hoveredObject = sizeAction->m_SizedNode;

        if (hoveredObject && !IsSelected(hoveredObject) && hoveredObject->IsVisible())
            hoveredObject->Draw(drawList, Object::Hovered);
    }

    // Draw animations
    for (auto controller : m_AnimationControllers)
        controller->Draw(drawList);

    if (m_CurrentAction && !m_CurrentAction->Process(control))
        m_CurrentAction = nullptr;

    if (m_NavigateAction.m_IsActive)
        m_NavigateAction.Process(control);
    else
        m_NavigateAction.Accept(control);

    if (nullptr == m_CurrentAction)
    {
        EditorAction* possibleAction   = nullptr;

        auto accept = [&possibleAction, &control](EditorAction& action)
        {
            auto result = action.Accept(control);

            if (result == EditorAction::True)
                return true;
            else if (!possibleAction && result == EditorAction::Possible)
                possibleAction = &action;
            else if (result == EditorAction::Possible)
                action.Reject();

            return false;
        };

        if (accept(m_ContextMenuAction))
            m_CurrentAction = &m_ContextMenuAction;
        else if (accept(m_ShortcutAction))
            m_CurrentAction = &m_ShortcutAction;
        else if (accept(m_SizeAction))
            m_CurrentAction = &m_SizeAction;
        else if (accept(m_DragAction))
            m_CurrentAction = &m_DragAction;
        else if (accept(m_SelectAction))
            m_CurrentAction = &m_SelectAction;
        else if (accept(m_CreateItemAction))
            m_CurrentAction = &m_CreateItemAction;
        else if (accept(m_DeleteItemsAction))
            m_CurrentAction = &m_DeleteItemsAction;

        if (possibleAction)
            ImGui::SetMouseCursor(possibleAction->GetCursor());

        if (m_CurrentAction && possibleAction)
            possibleAction->Reject();
    }

    if (m_CurrentAction)
        ImGui::SetMouseCursor(m_CurrentAction->GetCursor());

    // Draw selection rectangle
    m_SelectAction.Draw(drawList);

    bool sortGroups = false;
    if (control.ActiveNode)
    {
        if (!IsGroup(control.ActiveNode))
        {
            // Bring active node to front
            auto activeNodeIt = std::find(m_Nodes.begin(), m_Nodes.end(), control.ActiveNode);
            std::rotate(activeNodeIt, activeNodeIt + 1, m_Nodes.end());
        }
        else if (!isDragging && m_CurrentAction && m_CurrentAction->AsDrag())
        {
            // Bring content of dragged group to front
            std::vector<Node*> nodes;
            control.ActiveNode->GetGroupedNodes(nodes);

            std::stable_partition(m_Nodes.begin(), m_Nodes.end(), [&nodes](Node* node)
            {
                return std::find(nodes.begin(), nodes.end(), node) == nodes.end();
            });

            sortGroups = true;
        }
    }

    // Sort nodes if bounds of node changed
    if (sortGroups || ((m_Settings.m_DirtyReason & (SaveReasonFlags::Position | SaveReasonFlags::Size)) != SaveReasonFlags::None))
    {
        // Bring all groups before regular nodes
        auto groupsItEnd = std::stable_partition(m_Nodes.begin(), m_Nodes.end(), IsGroup);

        // Sort groups by area
        std::sort(m_Nodes.begin(), groupsItEnd, [this](Node* lhs, Node* rhs)
        {
            const auto& lhsSize = lhs == m_SizeAction.m_SizedNode ? m_SizeAction.GetStartGroupBounds().size : lhs->m_GroupBounds.size;
            const auto& rhsSize = rhs == m_SizeAction.m_SizedNode ? m_SizeAction.GetStartGroupBounds().size : rhs->m_GroupBounds.size;

            const auto lhsArea = lhsSize.w * lhsSize.h;
            const auto rhsArea = rhsSize.w * rhsSize.h;

            return lhsArea > rhsArea;
        });
    }

    // Every node has few channels assigned. Grow channel list
    // to hold twice as much of channels and place them in
    // node drawing order.
    {
        // Copy group nodes
        auto liveNodeCount = std::count_if(m_Nodes.begin(), m_Nodes.end(), [](Node* node) { return node->m_IsLive; });

        // Reserve two additional channels for sorted list of channels
        auto nodeChannelCount = drawList->_ChannelsCount;
        ImDrawList_ChannelsGrow(drawList, drawList->_ChannelsCount + c_ChannelsPerNode * liveNodeCount + c_LinkChannelCount);

        int targetChannel = nodeChannelCount;

        auto copyNode = [&targetChannel, drawList](Node* node)
        {
            if (!node->m_IsLive)
                return;

            for (int i = 0; i < c_ChannelsPerNode; ++i)
                ImDrawList_SwapChannels(drawList, node->m_Channel + i, targetChannel + i);

            node->m_Channel = targetChannel;
            targetChannel += c_ChannelsPerNode;
        };

        auto groupsItEnd = std::find_if(m_Nodes.begin(), m_Nodes.end(), [](Node* node) { return !IsGroup(node); });

        // Copy group nodes
        std::for_each(m_Nodes.begin(), groupsItEnd, copyNode);

        // Copy links
        for (int i = 0; i < c_LinkChannelCount; ++i, ++targetChannel)
            ImDrawList_SwapChannels(drawList, c_LinkStartChannel + i, targetChannel);

        // Copy normal nodes
        std::for_each(groupsItEnd, m_Nodes.end(), copyNode);
    }

    ImGui::PopClipRect();

    // Draw grid
    {
        auto& style = ImGui::GetStyle();

        drawList->ChannelsSetCurrent(c_UserChannel_Grid);

        ImGui::PushClipRect(m_Canvas.WindowScreenPos + ImVec2(1, 1), m_Canvas.WindowScreenPos + m_Canvas.WindowScreenSize - ImVec2(1, 1), false);

        ImVec2 offset    = m_Canvas.ClientOrigin;
        ImU32 GRID_COLOR = GetColor(StyleColor_Grid);
        float GRID_SX    = 32.0f * m_Canvas.Zoom.x;
        float GRID_SY    = 32.0f * m_Canvas.Zoom.y;
        //ImVec2 win_pos   = Canvas.WindowScreenPos;
        //ImVec2 canvas_sz = Canvas.WindowScreenSize;

        drawList->AddRectFilled(m_Canvas.WindowScreenPos, m_Canvas.WindowScreenPos + m_Canvas.WindowScreenSize, GetColor(StyleColor_Bg), style.WindowRounding);

        for (float x = fmodf(offset.x, GRID_SX); x < m_Canvas.WindowScreenSize.x; x += GRID_SX)
            drawList->AddLine(ImVec2(x, 0.0f) + m_Canvas.WindowScreenPos, ImVec2(x, m_Canvas.WindowScreenSize.y) + m_Canvas.WindowScreenPos, GRID_COLOR);
        for (float y = fmodf(offset.y, GRID_SY); y < m_Canvas.WindowScreenSize.y; y += GRID_SY)
            drawList->AddLine(ImVec2(0.0f, y) + m_Canvas.WindowScreenPos, ImVec2(m_Canvas.WindowScreenSize.x, y) + m_Canvas.WindowScreenPos, GRID_COLOR);

        ImGui::PopClipRect();
    }

    {
        auto userChannel = drawList->_ChannelsCount;
        auto channelsToCopy = 1; //c_UserLayersCount;
        ImDrawList_ChannelsGrow(drawList, userChannel + channelsToCopy);
        for (int i = 0; i < channelsToCopy; ++i)
            ImDrawList_SwapChannels(drawList, userChannel + i, c_UserLayerChannelStart + i);
    }

    {
        auto preOffset  = ImVec2(0, 0);
        auto postOffset = m_Canvas.WindowScreenPos + m_Canvas.ClientOrigin;
        auto scale      = m_Canvas.Zoom;

        ImDrawList_TransformChannels(drawList,                        0,                            1, preOffset, scale, postOffset);
        ImDrawList_TransformChannels(drawList, c_BackgroundChannelStart, drawList->_ChannelsCount - 1, preOffset, scale, postOffset);

        auto clipTranslation = m_Canvas.WindowScreenPos - m_Canvas.FromScreen(m_Canvas.WindowScreenPos);
        ImGui::PushClipRect(m_Canvas.WindowScreenPos + ImVec2(1, 1), m_Canvas.WindowScreenPos + m_Canvas.WindowScreenSize - ImVec2(1, 1), false);
        ImDrawList_TranslateAndClampClipRects(drawList,                        0,                            1, clipTranslation);
        ImDrawList_TranslateAndClampClipRects(drawList, c_BackgroundChannelStart, drawList->_ChannelsCount - 1, clipTranslation);
        ImGui::PopClipRect();

        // #debug: Static grid in local space
        //for (float x = 0; x < Canvas.WindowScreenSize.x; x += 100)
        //    drawList->AddLine(ImVec2(x, 0.0f) + Canvas.WindowScreenPos, ImVec2(x, Canvas.WindowScreenSize.y) + Canvas.WindowScreenPos, IM_COL32(255, 0, 0, 128));
        //for (float y = 0; y < Canvas.WindowScreenSize.y; y += 100)
        //    drawList->AddLine(ImVec2(0.0f, y) + Canvas.WindowScreenPos, ImVec2(Canvas.WindowScreenSize.x, y) + Canvas.WindowScreenPos, IM_COL32(255, 0, 0, 128));
    }

    // Move hint channels to top
    {
        auto channelCount = drawList->_ChannelsCount;
        //auto channelsToCopy = 1; //c_UserLayersCount;
        ImDrawList_ChannelsGrow(drawList, channelCount + 2);
        ImDrawList_SwapChannels(drawList, c_UserChannel_HintsBackground, channelCount + 0);
        ImDrawList_SwapChannels(drawList, c_UserChannel_Hints,           channelCount + 1);
    }

    UpdateAnimations();

    drawList->ChannelsMerge();

    // Draw border
    {
        auto& style = ImGui::GetStyle();
        auto borderShadoColor = style.Colors[ImGuiCol_BorderShadow];
        auto borderColor      = style.Colors[ImGuiCol_Border];
        drawList->AddRect(m_Canvas.WindowScreenPos + ImVec2(1, 1), m_Canvas.WindowScreenPos + m_Canvas.WindowScreenSize + ImVec2(1, 1), ImColor(borderShadoColor), style.WindowRounding);
        drawList->AddRect(m_Canvas.WindowScreenPos,                m_Canvas.WindowScreenPos + m_Canvas.WindowScreenSize,                ImColor(borderColor),      style.WindowRounding);
    }

    // ShowMetrics(control);

    // fringe scale
    ImGui::PopStyleVar();

    ImGui::EndChild();
    ImGui::PopStyleColor();

    ReleaseMouse();

    if (!m_CurrentAction && m_IsFirstFrame && !m_Settings.m_Selection.empty())
    {
        ClearSelection();
        for (int id : m_Settings.m_Selection)
            if (auto object = FindObject(id))
                SelectObject(object);
    }

    if (HasSelectionChanged())
        MakeDirty(SaveReasonFlags::Selection);

    if (m_Settings.m_IsDirty && !m_CurrentAction)
        SaveSettings();

    m_IsFirstFrame = false;
}

bool ed::EditorContext::DoLink(int id, int startPinId, int endPinId, ImU32 color, float thickness)
{
    //auto& editorStyle = GetStyle();

    auto startPin = FindPin(startPinId);
    auto endPin   = FindPin(endPinId);

    if (!startPin || !startPin->m_IsLive || !endPin || !endPin->m_IsLive)
        return false;

    startPin->m_HasConnection = true;
      endPin->m_HasConnection = true;

    auto link           = GetLink(id);
    link->m_StartPin      = startPin;
    link->m_EndPin        = endPin;
    link->m_Color         = color;
    link->m_Thickness     = thickness;
    link->m_IsLive        = true;

    link->UpdateEndpoints();

    return true;
}

void ed::EditorContext::SetNodePosition(int nodeId, const ImVec2& position)
{
    auto node = FindNode(nodeId);
    if (!node)
    {
        node = CreateNode(nodeId);
        node->m_IsLive = false;
    }

    auto newPosition = to_point(position);
    if (node->m_Bounds.location != newPosition)
    {
        node->m_Bounds.location = to_point(position);
        MakeDirty(NodeEditor::SaveReasonFlags::Position, node);
    }
}

ImVec2 ed::EditorContext::GetNodePosition(int nodeId)
{
    auto node = FindNode(nodeId);
    if (!node)
        return ImVec2(FLT_MAX, FLT_MAX);

    return to_imvec(node->m_Bounds.location);
}

ImVec2 ed::EditorContext::GetNodeSize(int nodeId)
{
    auto node = FindNode(nodeId);
    if (!node)
        return ImVec2(0, 0);

    return to_imvec(node->m_Bounds.size);
}

void ed::EditorContext::MarkNodeToRestoreState(Node* node)
{
    node->m_RestoreState = true;
}

void ed::EditorContext::RestoreNodeState(Node* node)
{
    auto settings = m_Settings.FindNode(node->m_ID);
    if (!settings)
        return;

    // Load state from config (if possible)
    if (!NodeSettings::Parse(m_Config.LoadNode(node->m_ID), *settings))
        return;

    auto diff = to_point(settings->m_Location) - node->m_Bounds.location;

    node->m_Bounds.location      += diff;
    node->m_Bounds.size           = to_size(settings->m_Size);
    node->m_GroupBounds.location += diff;
    node->m_GroupBounds.size      = to_size(settings->m_GroupSize);
}

void ed::EditorContext::ClearSelection()
{
    m_SelectedObjects.clear();
}

void ed::EditorContext::SelectObject(Object* object)
{
    m_SelectedObjects.push_back(object);
}

void ed::EditorContext::DeselectObject(Object* object)
{
    auto objectIt = std::find(m_SelectedObjects.begin(), m_SelectedObjects.end(), object);
    if (objectIt != m_SelectedObjects.end())
        m_SelectedObjects.erase(objectIt);
}

void ed::EditorContext::SetSelectedObject(Object* object)
{
    ClearSelection();
    SelectObject(object);
}

void ed::EditorContext::ToggleObjectSelection(Object* object)
{
    if (IsSelected(object))
        DeselectObject(object);
    else
        SelectObject(object);
}

bool ed::EditorContext::IsSelected(Object* object)
{
    return std::find(m_SelectedObjects.begin(), m_SelectedObjects.end(), object) != m_SelectedObjects.end();
}

const ed::vector<ed::Object*>& ed::EditorContext::GetSelectedObjects()
{
    return m_SelectedObjects;
}

bool ed::EditorContext::IsAnyNodeSelected()
{
    for (auto object : m_SelectedObjects)
        if (object->AsNode())
            return true;

    return false;
}

bool ed::EditorContext::IsAnyLinkSelected()
{
    for (auto object : m_SelectedObjects)
        if (object->AsLink())
            return true;

    return false;
}

bool ed::EditorContext::HasSelectionChanged()
{
    return m_LastSelectedObjects != m_SelectedObjects;
}

ed::Node* ed::EditorContext::FindNodeAt(const ImVec2& p)
{
    for (auto node : m_Nodes)
        if (node->TestHit(p))
            return node;

    return nullptr;
}

void ed::EditorContext::FindNodesInRect(const ax::rectf& r, vector<Node*>& result, bool append, bool includeIntersecting)
{
    if (!append)
        result.resize(0);

    if (r.is_empty())
        return;

    for (auto node : m_Nodes)
        if (node->TestHit(r, includeIntersecting))
            result.push_back(node);
}

void ed::EditorContext::FindLinksInRect(const ax::rectf& r, vector<Link*>& result, bool append)
{
    if (!append)
        result.resize(0);

    if (r.is_empty())
        return;

    for (auto link : m_Links)
        if (link->TestHit(r))
            result.push_back(link);
}

void ed::EditorContext::FindLinksForNode(int nodeId, vector<Link*>& result, bool add)
{
    if (!add)
        result.clear();

    for (auto link : m_Links)
    {
        if (!link->m_IsLive)
            continue;

        if (link->m_StartPin->m_Node->m_ID == nodeId || link->m_EndPin->m_Node->m_ID == nodeId)
            result.push_back(link);
    }
}

bool ed::EditorContext::PinHadAnyLinks(int pinId)
{
    auto pin = FindPin(pinId);
    if (!pin || !pin->m_IsLive)
        return false;

    return pin->m_HasConnection || pin->m_HadConnection;
}

void ed::EditorContext::NotifyLinkDeleted(Link* link)
{
    if (m_LastActiveLink == link)
        m_LastActiveLink = nullptr;
}

void ed::EditorContext::Suspend()
{
    //assert(!IsSuspended);
    if (0 == m_SuspendCount++)
        ReleaseMouse();
}

void ed::EditorContext::Resume()
{
    assert(m_SuspendCount > 0);

    if (0 == --m_SuspendCount)
        CaptureMouse();
}

bool ed::EditorContext::IsSuspended()
{
    return m_SuspendCount > 0;
}

bool ed::EditorContext::IsActive()
{
    return m_IsWindowActive;
}

ed::Pin* ed::EditorContext::CreatePin(int id, PinKind kind)
{
    assert(nullptr == FindObject(id));
    auto pin = new Pin(this, id, kind);
    m_Pins.push_back({id, pin});
    std::sort(m_Pins.begin(), m_Pins.end());
    return pin;
}

ed::Node* ed::EditorContext::CreateNode(int id)
{
    assert(nullptr == FindObject(id));
    auto node = new Node(this, id);
    m_Nodes.push_back({id, node});
    //std::sort(Nodes.begin(), Nodes.end());

    auto settings = m_Settings.FindNode(id);
    if (!settings)
        settings = m_Settings.AddNode(id);

    if (!settings->m_WasUsed)
    {
        settings->m_WasUsed = true;
        RestoreNodeState(node);
    }

    node->m_Bounds.location  = to_point(settings->m_Location);

    if (settings->m_GroupSize.x > 0 || settings->m_GroupSize.y > 0)
    {
        node->m_Type             = NodeType::Group;
        node->m_GroupBounds.size = to_size(settings->m_GroupSize);
    }

    node->m_IsLive = false;

    return node;
}

ed::Link* ed::EditorContext::CreateLink(int id)
{
    assert(nullptr == FindObject(id));
    auto link = new Link(this, id);
    m_Links.push_back({id, link});
    std::sort(m_Links.begin(), m_Links.end());

    return link;
}

ed::Object* ed::EditorContext::FindObject(int id)
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
static inline auto FindItemInLinear(C& container, int id)
{
# if defined(_DEBUG)
    auto start = container.data();
    auto end   = container.data() + container.size();
    for (auto it = start; it < end; ++it)
        if ((*it).m_ID == id)
            return it->m_Object;
# else
    for (auto item : container)
        if (item.m_ID == id)
            return item.m_Object;
# endif

   return static_cast<decltype(container[0].m_Object)>(nullptr);
}

template <typename C>
static inline auto FindItemIn(C& container, int id)
{
//# if defined(_DEBUG)
//    auto start = container.data();
//    auto end   = container.data() + container.size();
//    for (auto it = start; it < end; ++it)
//        if ((*it)->ID == id)
//            return *it;
//# else
//    for (auto item : container)
//        if (item->ID == id)
//            return item;
//# endif
    auto key = typename C::value_type{ id, nullptr };
    auto first = container.cbegin();
    auto last  = container.cend();
    auto it    = std::lower_bound(first, last, key);
    if (it != last && (key.m_ID == it->m_ID))
        return it->m_Object;
    else
        return static_cast<decltype(it->m_Object)>(nullptr);
}

ed::Node* ed::EditorContext::FindNode(int id)
{
    return FindItemInLinear(m_Nodes, id);
}

ed::Pin* ed::EditorContext::FindPin(int id)
{
    return FindItemIn(m_Pins, id);
}

ed::Link* ed::EditorContext::FindLink(int id)
{
    return FindItemIn(m_Links, id);
}

ed::Node* ed::EditorContext::GetNode(int id)
{
    auto node = FindNode(id);
    if (!node)
        node = CreateNode(id);
    return node;
}

ed::Pin* ed::EditorContext::GetPin(int id, PinKind kind)
{
    if (auto pin = FindPin(id))
    {
        pin->m_Kind = kind;
        return pin;
    }
    else
        return CreatePin(id, kind);
}

ed::Link* ed::EditorContext::GetLink(int id)
{
    if (auto link = FindLink(id))
        return link;
    else
        return CreateLink(id);
}

void ed::EditorContext::LoadSettings()
{
    ed::Settings::Parse(m_Config.Load(), m_Settings);

    m_NavigateAction.m_Scroll = m_Settings.m_ViewScroll;
    m_NavigateAction.m_Zoom   = m_Settings.m_ViewZoom;
}

void ed::EditorContext::SaveSettings()
{
    m_Config.BeginSave();

    for (auto& node : m_Nodes)
    {
        auto settings = m_Settings.FindNode(node->m_ID);
        settings->m_Location = to_imvec(node->m_Bounds.location);
        settings->m_Size     = to_imvec(node->m_Bounds.size);
        if (IsGroup(node))
            settings->m_GroupSize = to_imvec(node->m_GroupBounds.size);

        if (!node->m_RestoreState && settings->m_IsDirty && m_Config.SaveNodeSettings)
        {
            if (m_Config.SaveNode(node->m_ID, json::value(settings->Serialize()).serialize(), settings->m_DirtyReason))
                settings->ClearDirty();
        }
    }

    m_Settings.m_Selection.resize(0);
    for (auto& object : m_SelectedObjects)
        m_Settings.m_Selection.push_back(object->m_ID);

    m_Settings.m_ViewScroll = m_NavigateAction.m_Scroll;
    m_Settings.m_ViewZoom   = m_NavigateAction.m_Zoom;

    if (m_Config.Save(m_Settings.Serialize(), m_Settings.m_DirtyReason))
        m_Settings.ClearDirty();

    m_Config.EndSave();
}

void ed::EditorContext::MakeDirty(SaveReasonFlags reason)
{
    m_Settings.MakeDirty(reason);
}

void ed::EditorContext::MakeDirty(SaveReasonFlags reason, Node* node)
{
    m_Settings.MakeDirty(reason, node);
}

ed::Link* ed::EditorContext::FindLinkAt(const ax::point& p)
{
    for (auto& link : m_Links)
        if (link->TestHit(to_imvec(p), c_LinkSelectThickness))
            return link;

    return nullptr;
}

ImU32 ed::EditorContext::GetColor(StyleColor colorIndex) const
{
    return ImColor(m_Style.Colors[colorIndex]);
}

ImU32 ed::EditorContext::GetColor(StyleColor colorIndex, float alpha) const
{
    auto color = m_Style.Colors[colorIndex];
    return ImColor(color.x, color.y, color.z, color.w * alpha);
}

void ed::EditorContext::RegisterAnimation(Animation* animation)
{
    m_LiveAnimations.push_back(animation);
}

void ed::EditorContext::UnregisterAnimation(Animation* animation)
{
    auto it = std::find(m_LiveAnimations.begin(), m_LiveAnimations.end(), animation);
    if (it != m_LiveAnimations.end())
        m_LiveAnimations.erase(it);
}

void ed::EditorContext::UpdateAnimations()
{
    m_LastLiveAnimations = m_LiveAnimations;

    for (auto animation : m_LastLiveAnimations)
    {
        const bool isLive = (std::find(m_LiveAnimations.begin(), m_LiveAnimations.end(), animation) != m_LiveAnimations.end());

        if (isLive)
            animation->Update();
    }
}

void ed::EditorContext::Flow(Link* link)
{
    m_FlowAnimationController.Flow(link);
}

void ed::EditorContext::SetUserContext(bool globalSpace)
{
    const auto mousePos = ImGui::GetMousePos();

    // Move drawing cursor to mouse location and prepare layer for
    // content added by user.
    if (globalSpace)
        ImGui::SetCursorScreenPos(m_Canvas.ToScreen(mousePos));
    else
        ImGui::SetCursorScreenPos(ImVec2(floorf(mousePos.x), floorf(mousePos.y)));

    auto drawList = ImGui::GetWindowDrawList();
    drawList->ChannelsSetCurrent(c_UserChannel_Content);

    // #debug
    //drawList->AddCircleFilled(ImGui::GetMousePos(), 4, IM_COL32(0, 255, 0, 255));
}

void ed::EditorContext::EnableShortcuts(bool enable)
{
    m_ShortcutsEnabled = enable;
}

bool ed::EditorContext::AreShortcutsEnabled()
{
    return m_ShortcutsEnabled;
}

ed::Control ed::EditorContext::BuildControl(bool allowOffscreen)
{
    if (!allowOffscreen && !ImGui::IsWindowHovered())
        return Control(nullptr, nullptr, nullptr, nullptr, false, false, false, false);

    const auto mousePos = to_point(ImGui::GetMousePos());

    // Expand clip rectangle to always contain cursor
    auto editorRect = rect(to_point(m_Canvas.FromClient(ImVec2(0, 0))), to_size(m_Canvas.ClientSize));
    auto isMouseOffscreen = allowOffscreen && !editorRect.contains(mousePos);
    if (isMouseOffscreen)
    {
        // Extend clip rect to capture off-screen mouse cursor
        editorRect = make_union(editorRect, static_cast<point>(to_pointf(ImGui::GetMousePos()).cwise_floor()));
        editorRect = make_union(editorRect, static_cast<point>(to_pointf(ImGui::GetMousePos()).cwise_ceil()));

        const auto clipMin = to_imvec(editorRect.top_left());
        const auto clipMax = to_imvec(editorRect.bottom_right());

        ImGui::PushClipRect(clipMin, clipMax, false);
    }

    Object* hotObject           = nullptr;
    Object* activeObject        = nullptr;
    Object* clickedObject       = nullptr;
    Object* doubleClickedObject = nullptr;

    // Emits invisible button and returns true if it is clicked.
    auto emitInteractiveArea = [this](int id, const rect& rect)
    {
        char idString[33] = { 0 }; // itoa can output 33 bytes maximum
        snprintf(idString, 32, "%d", id);
        ImGui::SetCursorScreenPos(to_imvec(rect.location));

        // debug
        //if (id < 0)
        //    return ImGui::Button(idString, to_imvec(rect.size));

        auto result = ImGui::InvisibleButton(idString, to_imvec(rect.size));

        // #debug
        //ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(0, 255, 0, 64));

        return result;
    };

    // Check input interactions over area.
    auto checkInteractionsInArea = [&emitInteractiveArea, &hotObject, &activeObject, &clickedObject, &doubleClickedObject](int id, const rect& rect, Object* object)
    {
        if (emitInteractiveArea(id, rect))
            clickedObject = object;
        if (!doubleClickedObject && ImGui::IsMouseDoubleClicked(0) && ImGui::IsItemHoveredRect())
            doubleClickedObject = object;

        if (!hotObject && ImGui::IsItemHoveredRect())
            hotObject = object;

        if (ImGui::IsItemActive())
            activeObject = object;
    };

    // Process live nodes and pins.
    for (auto nodeIt = m_Nodes.rbegin(), nodeItEnd = m_Nodes.rend(); nodeIt != nodeItEnd; ++nodeIt)
    {
        auto node = *nodeIt;

        if (!node->m_IsLive) continue;

        // Check for interactions with live pins in node before
        // processing node itself. Pins does not overlap each other
        // and all are within node bounds.
        for (auto pin = node->m_LastPin; pin; pin = pin->m_PreviousPin)
        {
            if (!pin->m_IsLive) continue;

            checkInteractionsInArea(pin->m_ID, pin->m_Bounds, pin);
        }

        // Check for interactions with node.
        if (node->m_Type == NodeType::Group)
        {
            // Node with a hole
            ImGui::PushID(node->m_ID);

            const auto top    = node->m_GroupBounds.top()  - node->m_Bounds.top();
            const auto left   = node->m_GroupBounds.left() - node->m_Bounds.left();
            const auto bottom = node->m_Bounds.bottom()    - node->m_GroupBounds.bottom();
            const auto right  = node->m_Bounds.right()     - node->m_GroupBounds.right();

            checkInteractionsInArea(1, rect(node->m_Bounds.left(),  node->m_Bounds.top(),             node->m_Bounds.w, top),    node);
            checkInteractionsInArea(2, rect(node->m_Bounds.left(),  node->m_Bounds.bottom() - bottom, node->m_Bounds.w, bottom), node);
            checkInteractionsInArea(3, rect(node->m_Bounds.left(),  node->m_Bounds.top() + top,       left, node->m_Bounds.h - top - bottom), node);
            checkInteractionsInArea(4, rect(node->m_Bounds.right() - right, node->m_Bounds.top() + top, right, node->m_Bounds.h - top - bottom), node);

            ImGui::PopID();
        }
        else
            checkInteractionsInArea(node->m_ID, node->m_Bounds, node);
    }

    // Links are not regular widgets and must be done manually since
    // ImGui does not support interactive elements with custom hit maps.
    //
    // Links can steal input from background.

    // Links are just over background. So if anything else
    // is hovered we can skip them.
    if (nullptr == hotObject)
        hotObject = FindLinkAt(mousePos);

    // Check for interaction with background.
    auto backgroundClicked       = emitInteractiveArea(0, editorRect);
    auto backgroundDoubleClicked = !doubleClickedObject && ImGui::IsItemHoveredRect() ? ImGui::IsMouseDoubleClicked(0) : false;
    auto isBackgroundActive      = ImGui::IsItemActive();
    auto isBackgroundHot         = !hotObject;
    auto isDragging              = ImGui::IsMouseDragging(0, 1) || ImGui::IsMouseDragging(1, 1) || ImGui::IsMouseDragging(2, 1);

    if (backgroundDoubleClicked)
        backgroundClicked = false;

    if (isMouseOffscreen)
        ImGui::PopClipRect();

    // Process link input using background interactions.
    auto hotLink = hotObject ? hotObject->AsLink() : nullptr;

    // ImGui take care of tracking active items. With link
    // we must do this ourself.
    if (!isDragging && isBackgroundActive && hotLink && !m_LastActiveLink)
        m_LastActiveLink = hotLink;
    if (isBackgroundActive && m_LastActiveLink)
    {
        activeObject       = m_LastActiveLink;
        isBackgroundActive = false;
    }
    else if (!isBackgroundActive && m_LastActiveLink)
        m_LastActiveLink = nullptr;

    // Steal click from backgrounds if link is hovered.
    if (!isDragging && backgroundClicked && hotLink)
    {
        clickedObject     = hotLink;
        backgroundClicked = false;
    }

    // Steal double-click from backgrounds if link is hovered.
    if (!isDragging && backgroundDoubleClicked && hotLink)
    {
        doubleClickedObject = hotLink;
        backgroundDoubleClicked = false;
    }

    return Control(hotObject, activeObject, clickedObject, doubleClickedObject,
        isBackgroundHot, isBackgroundActive, backgroundClicked, backgroundDoubleClicked);
}

void ed::EditorContext::ShowMetrics(const Control& control)
{
    auto& io = ImGui::GetIO();

    ImGui::SetCursorScreenPos(m_Canvas.WindowScreenPos);
    auto getObjectName = [](Object* object)
    {
        if (!object) return "";
        else if (object->AsNode())  return "Node";
        else if (object->AsPin())   return "Pin";
        else if (object->AsLink())  return "Link";
        else return "";
    };

    auto getHotObjectName = [&control, &getObjectName]()
    {
        if (control.HotObject)
            return getObjectName(control.HotObject);
        else if (control.BackgroundHot)
            return "Background";
        else
            return "<unknown>";
    };

    auto getActiveObjectName = [&control, &getObjectName]()
    {
        if (control.ActiveObject)
            return getObjectName(control.ActiveObject);
        else if (control.BackgroundActive)
            return "Background";
        else
            return "<unknown>";
    };

    auto liveNodeCount  = (int)std::count_if(m_Nodes.begin(),  m_Nodes.end(),  [](Node*  node)  { return  node->m_IsLive; });
    auto livePinCount   = (int)std::count_if(m_Pins.begin(),   m_Pins.end(),   [](Pin*   pin)   { return   pin->m_IsLive; });
    auto liveLinkCount  = (int)std::count_if(m_Links.begin(),  m_Links.end(),  [](Link*  link)  { return  link->m_IsLive; });

    ImGui::SetCursorPos(ImVec2(10, 10));
    ImGui::BeginGroup();
    ImGui::Text("Is Editor Active: %s", ImGui::IsWindowHovered() ? "true" : "false");
    ImGui::Text("Window Position: { x=%g y=%g }", m_Canvas.WindowScreenPos.x, m_Canvas.WindowScreenPos.y);
    ImGui::Text("Window Size: { w=%g h=%g }", m_Canvas.WindowScreenSize.x, m_Canvas.WindowScreenSize.y);
    ImGui::Text("Canvas Size: { w=%g h=%g }", m_Canvas.ClientSize.x, m_Canvas.ClientSize.y);
    ImGui::Text("Mouse: { x=%.0f y=%.0f } global: { x=%g y=%g }", io.MousePos.x, io.MousePos.y, m_MousePosBackup.x, m_MousePosBackup.y);
    ImGui::Text("Live Nodes: %d", liveNodeCount);
    ImGui::Text("Live Pins: %d", livePinCount);
    ImGui::Text("Live Links: %d", liveLinkCount);
    ImGui::Text("Hot Object: %s (%d)", getHotObjectName(), control.HotObject ? control.HotObject->m_ID : 0);
    if (auto node = control.HotObject ? control.HotObject->AsNode() : nullptr)
    {
        ImGui::SameLine();
        ImGui::Text("{ x=%d y=%d w=%d h=%d }", node->m_Bounds.x, node->m_Bounds.y, node->m_Bounds.w, node->m_Bounds.h);
    }
    ImGui::Text("Active Object: %s (%d)", getActiveObjectName(), control.ActiveObject ? control.ActiveObject->m_ID : 0);
    if (auto node = control.ActiveObject ? control.ActiveObject->AsNode() : nullptr)
    {
        ImGui::SameLine();
        ImGui::Text("{ x=%d y=%d w=%d h=%d }", node->m_Bounds.x, node->m_Bounds.y, node->m_Bounds.w, node->m_Bounds.h);
    }
    ImGui::Text("Action: %s", m_CurrentAction ? m_CurrentAction->GetName() : "<none>");
    ImGui::Text("Action Is Dragging: %s", m_CurrentAction && m_CurrentAction->IsDragging() ? "Yes" : "No");
    m_NavigateAction.ShowMetrics();
    m_SizeAction.ShowMetrics();
    m_DragAction.ShowMetrics();
    m_SelectAction.ShowMetrics();
    m_ContextMenuAction.ShowMetrics();
    m_CreateItemAction.ShowMetrics();
    m_DeleteItemsAction.ShowMetrics();
    ImGui::EndGroup();
}

void ed::EditorContext::CaptureMouse()
{
    auto& io = ImGui::GetIO();

    io.MousePos = m_Canvas.FromScreen(m_MousePosBackup);
    io.MousePosPrev = m_Canvas.FromScreen(m_MousePosPrevBackup);

    for (int i = 0; i < 5; ++i)
        io.MouseClickedPos[i] = m_Canvas.FromScreen(m_MouseClickPosBackup[i]);
}

void ed::EditorContext::ReleaseMouse()
{
    auto& io = ImGui::GetIO();

    io.MousePos = m_MousePosBackup;
    io.MousePosPrev = m_MousePosPrevBackup;

    for (int i = 0; i < 5; ++i)
        io.MouseClickedPos[i] = m_MouseClickPosBackup[i];
}




//------------------------------------------------------------------------------
//
// Node Settings
//
//------------------------------------------------------------------------------
void ed::NodeSettings::ClearDirty()
{
    m_IsDirty     = false;
    m_DirtyReason = SaveReasonFlags::None;
}

void ed::NodeSettings::MakeDirty(SaveReasonFlags reason)
{
    m_IsDirty     = true;
    m_DirtyReason = m_DirtyReason | reason;
}

ed::json::object ed::NodeSettings::Serialize()
{
    auto serializeVector = [](ImVec2 p) -> json::object
    {
        json::object result;
        result["x"] = json::value(static_cast<double>(p.x));
        result["y"] = json::value(static_cast<double>(p.y));
        return result;
    };

    json::object nodeData;
    nodeData["location"] = json::value(serializeVector(m_Location));
    nodeData["size"]     = json::value(serializeVector(m_Size));

    if (m_GroupSize.x > 0 || m_GroupSize.y > 0)
        nodeData["group_size"] = json::value(serializeVector(m_GroupSize));

    return nodeData;
}

bool ed::NodeSettings::Parse(const char* data, const char* dataEnd, NodeSettings& settings)
{
    json::value settingsValue;
    auto error = json::parse(settingsValue, data, dataEnd);
    if (!error.empty() || settingsValue.is<json::null>())
        return false;

    return Parse(settingsValue, settings);

}

bool ed::NodeSettings::Parse(const json::value& data, NodeSettings& result)
{
    if (!data.is<json::object>())
        return false;

    auto tryParseVector = [](const json::value& v, ImVec2& result) -> bool
    {
        if (v.is<json::object>())
        {
            auto xValue = v.get("x");
            auto yValue = v.get("y");

            if (xValue.is<double>() && yValue.is<double>())
            {
                result.x = static_cast<float>(xValue.get<double>());
                result.y = static_cast<float>(yValue.get<double>());

                return true;
            }
        }

        return false;
    };

    if (!tryParseVector(data.get("location"), result.m_Location))
        return false;

    if (data.contains("group_size") && !tryParseVector(data.get("group_size"), result.m_GroupSize))
        return false;

    return true;
}




//------------------------------------------------------------------------------
//
// Settings
//
//------------------------------------------------------------------------------
ed::NodeSettings* ed::Settings::AddNode(int id)
{
    m_Nodes.push_back(NodeSettings(id));
    return &m_Nodes.back();
}

ed::NodeSettings* ed::Settings::FindNode(int id)
{
    for (auto& settings : m_Nodes)
        if (settings.m_ID == id)
            return &settings;

    return nullptr;
}

void ed::Settings::ClearDirty(Node* node)
{
    if (node)
    {
        auto settings = FindNode(node->m_ID);
        assert(settings);
        settings->ClearDirty();
    }
    else
    {
        m_IsDirty     = false;
        m_DirtyReason = SaveReasonFlags::None;

        for (auto& node : m_Nodes)
            node.ClearDirty();
    }
}

void ed::Settings::MakeDirty(SaveReasonFlags reason, Node* node)
{
    m_IsDirty     = true;
    m_DirtyReason = m_DirtyReason | reason;

    if (node)
    {
        auto settings = FindNode(node->m_ID);
        assert(settings);

        settings->MakeDirty(reason);
    }
}

std::string ed::Settings::Serialize()
{
    auto serializeVector = [](ImVec2 p) -> json::object
    {
        json::object result;
        result["x"] = json::value(static_cast<double>(p.x));
        result["y"] = json::value(static_cast<double>(p.y));
        return result;
    };

    json::object nodes;
    for (auto& node : m_Nodes)
    {
        if (node.m_WasUsed)
            nodes[std::to_string(node.m_ID)] = json::value(node.Serialize());
    }

    json::array selection;
    for (auto& id : m_Selection)
        selection.push_back(json::value(static_cast<double>(id)));

    json::object view;
    view["scroll"] = json::value(serializeVector(m_ViewScroll));
    view["zoom"]   = json::value((double)m_ViewZoom);

    json::object settings;
    settings["nodes"]     = json::value(std::move(nodes));
    settings["view"]      = json::value(std::move(view));
    settings["selection"] = json::value(std::move(selection));

    json::value settingsValue(std::move(settings));

    return settingsValue.serialize(false);
}

bool ed::Settings::Parse(const char* data, const char* dataEnd, Settings& settings)
{
    Settings result;

    json::value settingsValue;
    auto error = json::parse(settingsValue, data, dataEnd);
    if (!error.empty() || settingsValue.is<json::null>())
        return false;

    if (!settingsValue.is<json::object>())
        return false;

    auto tryParseVector = [](const json::value& v, ImVec2& result) -> bool
    {
        if (v.is<json::object>())
        {
            auto xValue = v.get("x");
            auto yValue = v.get("y");

            if (xValue.is<double>() && yValue.is<double>())
            {
                result.x = static_cast<float>(xValue.get<double>());
                result.y = static_cast<float>(yValue.get<double>());

                return true;
            }
        }

        return false;
    };

    //auto& settingsObject = settingsValue.get<json::object>();

    auto& nodesValue = settingsValue.get("nodes");
    if (nodesValue.is<json::object>())
    {
        for (auto& node : nodesValue.get<json::object>())
        {
            auto id = static_cast<int>(strtoll(node.first.c_str(), nullptr, 10));

            auto settings = result.FindNode(id);
            if (!settings)
                settings = result.AddNode(id);

            NodeSettings::Parse(node.second, *settings);
        }
    }

    auto& selectionValue = settingsValue.get("selection");
    if (selectionValue.is<json::array>())
    {
        const auto selectionArray = selectionValue.get<json::array>();

        result.m_Selection.reserve(selectionArray.size());
        result.m_Selection.resize(0);
        for (auto& selection : selectionArray)
        {
            if (selection.is<double>())
                result.m_Selection.push_back(static_cast<int>(selection.get<double>()));
        }
    }

    auto& viewValue = settingsValue.get("view");
    if (viewValue.is<json::object>())
    {
        auto& viewScrollValue = viewValue.get("scroll");
        auto& viewZoomValue = viewValue.get("zoom");

        if (!tryParseVector(viewScrollValue, result.m_ViewScroll))
            result.m_ViewScroll = ImVec2(0, 0);

        result.m_ViewZoom = viewZoomValue.is<double>() ? static_cast<float>(viewZoomValue.get<double>()) : 1.0f;
    }

    settings = std::move(result);

    return true;
}




//------------------------------------------------------------------------------
//
// Canvas
//
//------------------------------------------------------------------------------
ed::Canvas::Canvas():
    WindowScreenPos(0, 0),
    WindowScreenSize(0, 0),
    ClientOrigin(0, 0),
    ClientSize(0, 0),
    Zoom(1, 1),
    InvZoom(1, 1)
{
}

ed::Canvas::Canvas(ImVec2 position, ImVec2 size, ImVec2 zoom, ImVec2 origin):
    WindowScreenPos(position),
    WindowScreenSize(size),
    ClientOrigin(origin),
    ClientSize(size),
    Zoom(zoom),
    InvZoom(1, 1)
{
    InvZoom.x = Zoom.x ? 1.0f / Zoom.x : 1.0f;
    InvZoom.y = Zoom.y ? 1.0f / Zoom.y : 1.0f;

    if (InvZoom.x > 1.0f)
        ClientSize.x *= InvZoom.x;
    if (InvZoom.y > 1.0f)
        ClientSize.y *= InvZoom.y;
}

ax::rectf ed::Canvas::GetVisibleBounds() const
{
    return ax::rectf(
        to_pointf(FromScreen(WindowScreenPos)),
        to_pointf(FromScreen(WindowScreenPos + WindowScreenSize)));
}

ImVec2 ed::Canvas::FromScreen(ImVec2 point) const
{
    return ImVec2(
        (point.x - WindowScreenPos.x - ClientOrigin.x) * InvZoom.x,
        (point.y - WindowScreenPos.y - ClientOrigin.y) * InvZoom.y);
}

ImVec2 ed::Canvas::ToScreen(ImVec2 point) const
{
    return ImVec2(
        (point.x * Zoom.x + ClientOrigin.x + WindowScreenPos.x),
        (point.y * Zoom.y + ClientOrigin.y + WindowScreenPos.y));
}

ImVec2 ed::Canvas::FromClient(ImVec2 point) const
{
    return ImVec2(
        (point.x - ClientOrigin.x) * InvZoom.x,
        (point.y - ClientOrigin.y) * InvZoom.y);
}

ImVec2 ed::Canvas::ToClient(ImVec2 point) const
{
    return ImVec2(
        (point.x * Zoom.x + ClientOrigin.x),
        (point.y * Zoom.y + ClientOrigin.y));
}




//------------------------------------------------------------------------------
//
// Animation
//
//------------------------------------------------------------------------------
ed::Animation::Animation(EditorContext* editor):
    Editor(editor),
    m_State(Stopped),
    m_Time(0.0f),
    m_Duration(0.0f)
{
}

ed::Animation::~Animation()
{
    Stop();
}

void ed::Animation::Play(float duration)
{
    if (IsPlaying())
        Stop();

    m_State = Playing;
    if (duration < 0)
        duration = 0.0f;

    m_Time     = 0.0f;
    m_Duration = duration;

    OnPlay();

    Editor->RegisterAnimation(this);

    if (duration == 0.0f)
        Stop();
}

void ed::Animation::Stop()
{
    if (!IsPlaying())
        return;

    m_State = Stopped;

    Editor->UnregisterAnimation(this);

    OnStop();
}

void ed::Animation::Finish()
{
    if (!IsPlaying())
        return;

    OnFinish();

    Stop();
}

void ed::Animation::Update()
{
    if (!IsPlaying())
        return;

    m_Time += std::max(0.0f, ImGui::GetIO().DeltaTime);
    if (m_Time < m_Duration)
    {
        const float progress = GetProgress();
        OnUpdate(progress);
    }
    else
    {
        OnFinish();
        Stop();
    }
}




//------------------------------------------------------------------------------
//
// Navigate Animation
//
//------------------------------------------------------------------------------
ed::NavigateAnimation::NavigateAnimation(EditorContext* editor, NavigateAction& scrollAction):
    Animation(editor),
    Action(scrollAction)
{
}

void ed::NavigateAnimation::NavigateTo(const ImVec2& target, float targetZoom, float duration)
{
    Stop();

    m_Start      = Action.m_Scroll;
    m_StartZoom  = Action.m_Zoom;
    m_Target     = target;
    m_TargetZoom = targetZoom;

    Play(duration);
}

void ed::NavigateAnimation::OnUpdate(float progress)
{
    Action.m_Scroll = easing::ease_out_quad(m_Start, m_Target - m_Start, progress);
    Action.m_Zoom   = easing::ease_out_quad(m_StartZoom, m_TargetZoom - m_StartZoom, progress);
}

void ed::NavigateAnimation::OnStop()
{
    Editor->MakeDirty(SaveReasonFlags::Navigation);
}

void ed::NavigateAnimation::OnFinish()
{
    Action.m_Scroll = m_Target;
    Action.m_Zoom   = m_TargetZoom;

    Editor->MakeDirty(SaveReasonFlags::Navigation);
}




//------------------------------------------------------------------------------
//
// Flow Animation
//
//------------------------------------------------------------------------------
ed::FlowAnimation::FlowAnimation(FlowAnimationController* controller):
    Animation(controller->Editor),
    Controller(controller),
    m_Link(nullptr),
    m_Offset(0.0f),
    m_PathLength(0.0f)
{
}

void ed::FlowAnimation::Flow(ed::Link* link, float markerDistance, float speed, float duration)
{
    Stop();

    if (m_Link != link)
    {
        m_Offset = 0.0f;
        ClearPath();
    }

    if (m_MarkerDistance != markerDistance)
        ClearPath();

    m_MarkerDistance = markerDistance;
    m_Speed          = speed;
    m_Link           = link;

    Play(duration);
}

void ed::FlowAnimation::Draw(ImDrawList* drawList)
{
    if (!IsPlaying() || !IsLinkValid() || !m_Link->IsVisible())
        return;

    if (!IsPathValid())
        UpdatePath();

    m_Offset = fmodf(m_Offset, m_MarkerDistance);

    const auto progress    = GetProgress();

    const auto flowAlpha = 1.0f - progress * progress;
    const auto flowColor = Editor->GetColor(StyleColor_Flow, flowAlpha);
    //const auto flowPath  = Link->GetCurve();

    m_Link->Draw(drawList, flowColor, 2.0f);

    if (IsPathValid())
    {
        //Offset = 0;

        const auto markerAlpha  = powf(1.0f - progress, 0.35f);
        const auto markerRadius = 4.0f * (1.0f - progress) + 2.0f;
        const auto markerColor  = Editor->GetColor(StyleColor_FlowMarker, markerAlpha);

        for (float d = m_Offset; d < m_PathLength; d += m_MarkerDistance)
            drawList->AddCircleFilled(SamplePath(d), markerRadius, markerColor);
    }
}

bool ed::FlowAnimation::IsLinkValid() const
{
    return m_Link && m_Link->m_IsLive;
}

bool ed::FlowAnimation::IsPathValid() const
{
    return m_Path.size() > 1 && m_PathLength > 0.0f && m_Link->m_Start == m_LastStart && m_Link->m_End == m_LastEnd;
}

void ed::FlowAnimation::UpdatePath()
{
    if (!IsLinkValid())
    {
        ClearPath();
        return;
    }

    const auto curve  = m_Link->GetCurve();

    m_LastStart  = m_Link->m_Start;
    m_LastEnd    = m_Link->m_End;
    m_PathLength = cubic_bezier_length(curve.p0, curve.p1, curve.p2, curve.p3);

    auto collectPointsCallback = [this](bezier_fixed_step_result_t& result)
    {
        m_Path.push_back(CurvePoint{ result.length, to_imvec(result.point) });
    };

    const auto step = std::max(m_MarkerDistance * 0.5f, 15.0f);

    m_Path.resize(0);
    cubic_bezier_fixed_step(collectPointsCallback, curve.p0, curve.p1, curve.p2, curve.p3, step, false, 0.5f, 0.001f);
}

void ed::FlowAnimation::ClearPath()
{
    vector<CurvePoint>().swap(m_Path);
    m_PathLength = 0.0f;
}

ImVec2 ed::FlowAnimation::SamplePath(float distance)
{
    //distance = std::max(0.0f, std::min(distance, PathLength));

    auto endPointIt = std::find_if(m_Path.begin(), m_Path.end(), [distance](const CurvePoint& p) { return distance < p.Distance; });
    if (endPointIt == m_Path.end())
        endPointIt = m_Path.end() - 1;
    else if (endPointIt == m_Path.begin())
        endPointIt = m_Path.begin() + 1;

    const auto& start = endPointIt[-1];
    const auto& end   = *endPointIt;
    const auto  t     = (distance - start.Distance) / (end.Distance - start.Distance);

    return start.Point + (end.Point - start.Point) * t;
}

void ed::FlowAnimation::OnUpdate(float progress)
{
    m_Offset += m_Speed * ImGui::GetIO().DeltaTime;
}

void ed::FlowAnimation::OnStop()
{
    Controller->Release(this);
}




//------------------------------------------------------------------------------
//
// Flow Animation Controller
//
//------------------------------------------------------------------------------
ed::FlowAnimationController::FlowAnimationController(EditorContext* editor):
    AnimationController(editor)
{
}

ed::FlowAnimationController::~FlowAnimationController()
{
    for (auto animation : m_Animations)
        delete animation;
}

void ed::FlowAnimationController::Flow(Link* link)
{
    if (!link || !link->m_IsLive)
        return;

    auto& editorStyle = GetStyle();

    auto animation = GetOrCreate(link);

    animation->Flow(link, editorStyle.FlowMarkerDistance, editorStyle.FlowSpeed, editorStyle.FlowDuration);
}

void ed::FlowAnimationController::Draw(ImDrawList* drawList)
{
    if (m_Animations.empty())
        return;

    drawList->ChannelsSetCurrent(c_LinkChannel_Flow);

    for (auto animation : m_Animations)
        animation->Draw(drawList);
}

ed::FlowAnimation* ed::FlowAnimationController::GetOrCreate(Link* link)
{
    // Return live animation which match target link
    {
        auto animationIt = std::find_if(m_Animations.begin(), m_Animations.end(), [link](FlowAnimation* animation) { return animation->m_Link == link; });
        if (animationIt != m_Animations.end())
            return *animationIt;
    }

    // There are no live animations for target link, try to reuse inactive old one
    if (!m_FreePool.empty())
    {
        auto animation = m_FreePool.back();
        m_FreePool.pop_back();
        return animation;
    }

    // Cache miss, allocate new one
    auto animation = new FlowAnimation(this);
    m_Animations.push_back(animation);

    return animation;
}

void ed::FlowAnimationController::Release(FlowAnimation* animation)
{
}



//------------------------------------------------------------------------------
//
// Navigate Action
//
//------------------------------------------------------------------------------
const float ed::NavigateAction::s_ZoomLevels[] =
{
    0.1f, 0.15f, 0.20f, 0.25f, 0.33f, 0.5f, 0.75f, 1.0f, 1.25f, 1.50f, 2.0f, 2.5f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f
};

const int ed::NavigateAction::s_ZoomLevelCount = sizeof(s_ZoomLevels) / sizeof(*s_ZoomLevels);

ed::NavigateAction::NavigateAction(EditorContext* editor):
    EditorAction(editor),
    m_IsActive(false),
    m_Zoom(1),
    m_Scroll(0, 0),
    m_ScrollStart(0, 0),
    m_ScrollDelta(0, 0),
    m_WindowScreenPos(0, 0),
    m_WindowScreenSize(0, 0),
    m_Animation(editor, *this),
    m_Reason(NavigationReason::Unknown),
    m_LastSelectionId(0),
    m_LastObject(nullptr),
    m_MovingOverEdge(false),
    m_MoveOffset(0, 0)
{
}

ed::EditorAction::AcceptResult ed::NavigateAction::Accept(const Control& control)
{
    assert(!m_IsActive);

    if (m_IsActive)
        return False;

    if (ImGui::IsWindowHovered() /*&& !ImGui::IsAnyItemActive()*/ && ImGui::IsMouseDragging(c_ScrollButtonIndex, 0.0f))
    {
        m_IsActive    = true;
        m_ScrollStart = m_Scroll;
        m_ScrollDelta = ImGui::GetMouseDragDelta(c_ScrollButtonIndex);
        m_Scroll      = m_ScrollStart - m_ScrollDelta * m_Zoom;
    }

    auto& io = ImGui::GetIO();

    if (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_F)) && Editor->AreShortcutsEnabled())
    {
        const auto allowZoomIn = io.KeyShift;

        auto findHotObjectToZoom = [this, &control, &io]() -> Object*
        {
            if (control.HotObject)
            {
                if (auto pin = control.HotObject->AsPin())
                    return pin->m_Node;
                else
                    return control.HotObject;
            }
            else if (control.BackgroundHot)
            {
                auto node = Editor->FindNodeAt(io.MousePos);
                if (IsGroup(node))
                    return node;
            }

            return nullptr;
        };

        bool navigateToContent = false;
        if (!Editor->GetSelectedObjects().empty())
        {
            if (m_Reason != NavigationReason::Selection || m_LastSelectionId != Editor->GetSelectionId() || allowZoomIn)
            {
                m_LastSelectionId = Editor->GetSelectionId();
                NavigateTo(Editor->GetSelectionBounds(), allowZoomIn, -1.0f, NavigationReason::Selection);
            }
            else
                navigateToContent = true;
        }
        else if(auto hotObject = findHotObjectToZoom())
        {
            if (m_Reason != NavigationReason::Object || m_LastObject != hotObject || allowZoomIn)
            {
                m_LastObject = hotObject;
                auto bounds = hotObject->GetBounds();
                NavigateTo(bounds, allowZoomIn, -1.0f, NavigationReason::Object);
            }
            else
                navigateToContent = true;
        }
        else
            navigateToContent = true;

        if (navigateToContent)
            NavigateTo(Editor->GetContentBounds(), true, -1.0f, NavigationReason::Content);
    }

    // // #debug
    // if (auto drawList = ImGui::GetWindowDrawList())
    //     drawList->AddCircleFilled(io.MousePos, 4.0f, IM_COL32(255, 0, 255, 255));

    if (HandleZoom(control))
        return True;

    return m_IsActive ? True : False;
}

bool ed::NavigateAction::Process(const Control& control)
{
    if (!m_IsActive)
        return false;

    if (ImGui::IsMouseDragging(c_ScrollButtonIndex, 0.0f))
    {
        m_ScrollDelta = ImGui::GetMouseDragDelta(c_ScrollButtonIndex);
        m_Scroll      = m_ScrollStart - m_ScrollDelta * m_Zoom;

//         if (IsActive && Animation.IsPlaying())
//             Animation.Target = Animation.Target - ScrollDelta * Animation.TargetZoom;
    }
    else
    {
        if (m_Scroll != m_ScrollStart)
            Editor->MakeDirty(SaveReasonFlags::Navigation);

        m_IsActive = false;
    }

    // #TODO: Handle zoom while scrolling
    // HandleZoom(control);

    return m_IsActive;
}

bool ed::NavigateAction::HandleZoom(const Control& control)
{
    const auto currentAction  = Editor->GetCurrentAction();
    const auto allowOffscreen = currentAction && currentAction->IsDragging();

    auto& io = ImGui::GetIO();

    if (!io.MouseWheel || (!allowOffscreen && !ImGui::IsWindowHovered()))// && !ImGui::IsAnyItemActive())
        return false;

    auto savedScroll = m_Scroll;
    auto savedZoom   = m_Zoom;

    m_Animation.Finish();

    auto mousePos = io.MousePos;
    auto steps    = (int)io.MouseWheel;
    auto newZoom  = MatchZoom(steps, s_ZoomLevels[steps < 0 ? 0 : s_ZoomLevelCount - 1]);

    auto oldCanvas = GetCanvas(false);
    m_Zoom = newZoom;
    auto newCanvas = GetCanvas(false);

    auto screenPos = oldCanvas.ToScreen(mousePos);
    auto canvasPos = newCanvas.FromScreen(screenPos);

    auto offset       = (canvasPos - mousePos) * m_Zoom;
    auto targetScroll = m_Scroll - offset;

    if (m_Scroll != savedScroll || m_Zoom != savedZoom)
    {
        m_Scroll = savedScroll;
        m_Zoom = savedZoom;

        Editor->MakeDirty(SaveReasonFlags::Navigation);
    }

    NavigateTo(targetScroll, newZoom, c_MouseZoomDuration, NavigationReason::MouseZoom);

    return true;
}

void ed::NavigateAction::ShowMetrics()
{
    EditorAction::ShowMetrics();

    ImGui::Text("%s:", GetName());
    ImGui::Text("    Active: %s", m_IsActive ? "yes" : "no");
    ImGui::Text("    Scroll: { x=%g y=%g }", m_Scroll.x, m_Scroll.y);
    ImGui::Text("    Zoom: %g", m_Zoom);
}

void ed::NavigateAction::NavigateTo(const ax::rectf& bounds, bool zoomIn, float duration, NavigationReason reason)
{
    if (bounds.is_empty())
        return;

    auto canvas = GetCanvas(false);

    const auto selectionBounds     = bounds;
    const auto visibleBounds       = canvas.GetVisibleBounds();

    const auto visibleBoundsMargin = c_NavigationZoomMargin;
    const auto targetVisibleSize   = static_cast<pointf>(visibleBounds.size) - static_cast<pointf>(visibleBounds.size) * visibleBoundsMargin;
    const auto sourceSize          = static_cast<pointf>(selectionBounds.size);
    const auto ratio               = sourceSize.cwise_safe_quotient(targetVisibleSize);
    const auto maxRatio            = std::max(ratio.x, ratio.y);
    const auto zoomChange          = maxRatio ? 1.0f / (zoomIn ? maxRatio : std::max(maxRatio, 1.0f)) : 1.0f;

    auto action = *this;
    action.m_Zoom *= zoomChange;

    const auto targetBounds    = action.GetCanvas(false).GetVisibleBounds();
    const auto selectionCenter = to_imvec(selectionBounds.center());
    const auto targetCenter    = to_imvec(targetBounds.center());
    const auto offset          = selectionCenter - targetCenter;

    action.m_Scroll = action.m_Scroll + offset * action.m_Zoom;

    const auto scrollOffset = action.m_Scroll - m_Scroll;
    const auto zoomOffset   = action.m_Zoom   - m_Zoom;

    const auto epsilon = 1e-4f;

    if (fabsf(scrollOffset.x) < epsilon && fabsf(scrollOffset.y) < epsilon && fabsf(zoomOffset) < epsilon)
        NavigateTo(action.m_Scroll, action.m_Zoom, 0, reason);
    else
        NavigateTo(action.m_Scroll, action.m_Zoom, GetStyle().ScrollDuration, reason);
}

void ed::NavigateAction::NavigateTo(const ImVec2& target, float targetZoom, float duration, NavigationReason reason)
{
    m_Reason = reason;

    m_Animation.NavigateTo(target, targetZoom, duration);
}

void ed::NavigateAction::StopNavigation()
{
    m_Animation.Stop();
}

void ed::NavigateAction::FinishNavigation()
{
    m_Animation.Finish();
}

bool ed::NavigateAction::MoveOverEdge()
{
    // Don't interrupt non-edge animations
    if (m_Animation.IsPlaying())
        return false;

          auto& io            = ImGui::GetIO();
    const auto screenRect     = ax::rectf(to_pointf(m_WindowScreenPos), to_sizef(m_WindowScreenSize));
    const auto screenMousePos = to_pointf(io.MousePos);

    // Mouse is over screen, do nothing
    if (screenRect.contains(screenMousePos))
        return false;

    const auto screenPointOnEdge = screenRect.get_closest_point(screenMousePos, true);
    const auto direction         = screenPointOnEdge - screenMousePos;
    const auto offset            = to_imvec(-direction) * io.DeltaTime * 10.0f;

    m_Scroll = m_Scroll + offset;

    m_MoveOffset     = offset;
    m_MovingOverEdge = true;

    return true;
}

void ed::NavigateAction::StopMoveOverEdge()
{
    if (m_MovingOverEdge)
    {
        Editor->MakeDirty(SaveReasonFlags::Navigation);

        m_MoveOffset     = ImVec2(0, 0);
        m_MovingOverEdge = false;
    }
}

void ed::NavigateAction::SetWindow(ImVec2 position, ImVec2 size)
{
    m_WindowScreenPos  = position;
    m_WindowScreenSize = size;
}

ed::Canvas ed::NavigateAction::GetCanvas(bool alignToPixels)
{
    ImVec2 origin = -m_Scroll;
    if (alignToPixels)
    {
        origin.x = floorf(origin.x);
        origin.y = floorf(origin.y);
    }

    return Canvas(m_WindowScreenPos, m_WindowScreenSize, ImVec2(m_Zoom, m_Zoom), origin);
}

float ed::NavigateAction::MatchZoom(int steps, float fallbackZoom)
{
    auto currentZoomIndex = MatchZoomIndex(steps);
    if (currentZoomIndex < 0)
        return fallbackZoom;

    auto currentZoom = s_ZoomLevels[currentZoomIndex];
    if (fabsf(currentZoom - m_Zoom) > 0.001f)
        return currentZoom;

    auto newIndex = currentZoomIndex + steps;
    if (newIndex >= 0 && newIndex < s_ZoomLevelCount)
        return s_ZoomLevels[newIndex];
    else
        return fallbackZoom;
}

int ed::NavigateAction::MatchZoomIndex(int direction)
{
    int   bestIndex    = -1;
    float bestDistance = 0.0f;

    for (int i = 0; i < s_ZoomLevelCount; ++i)
    {
        auto distance = fabsf(s_ZoomLevels[i] - m_Zoom);
        if (distance < bestDistance || bestIndex < 0)
        {
            bestDistance = distance;
            bestIndex    = i;
        }
    }

    if (bestDistance > 0.001f)
    {
        if (direction > 0)
        {
            ++bestIndex;

            if (bestIndex >= s_ZoomLevelCount)
                bestIndex = s_ZoomLevelCount - 1;
        }
        else if (direction < 0)
        {
            --bestIndex;

            if (bestIndex < 0)
                bestIndex = 0;
        }
    }

    return bestIndex;
}




//------------------------------------------------------------------------------
//
// Size Action
//
//------------------------------------------------------------------------------
ed::SizeAction::SizeAction(EditorContext* editor):
    EditorAction(editor),
    m_IsActive(false),
    m_Clean(false),
    m_SizedNode(nullptr),
    m_Pivot(rect_region::center),
    m_Cursor(ImGuiMouseCursor_Arrow)
{
}

ed::EditorAction::AcceptResult ed::SizeAction::Accept(const Control& control)
{
    assert(!m_IsActive);

    if (m_IsActive)
        return False;

    if (control.ActiveNode && IsGroup(control.ActiveNode) && ImGui::IsMouseDragging(0, 0))
    {
        //const auto mousePos     = to_point(ImGui::GetMousePos());
        //const auto closestPoint = control.ActiveNode->Bounds.get_closest_point_hollow(mousePos, static_cast<int>(control.ActiveNode->Rounding));

        auto pivot = GetRegion(control.ActiveNode);
        if (pivot != rect_region::center)
        {
            m_StartBounds      = control.ActiveNode->m_Bounds;
            m_StartGroupBounds = control.ActiveNode->m_GroupBounds;
            m_LastSize         = control.ActiveNode->m_Bounds.size;
            m_LastDragOffset   = point(0, 0);
            m_Stable           = true;
            m_Pivot            = pivot;
            m_Cursor           = ChooseCursor(m_Pivot);
            m_SizedNode        = control.ActiveNode;
            m_IsActive         = true;
        }
    }
    else if (control.HotNode && IsGroup(control.HotNode))
    {
        m_Cursor = ChooseCursor(GetRegion(control.HotNode));
        return Possible;
    }

    return m_IsActive ? True : False;
}

bool ed::SizeAction::Process(const Control& control)
{
    if (m_Clean)
    {
        m_Clean = false;

        if (m_SizedNode->m_Bounds.location != m_StartBounds.location || m_SizedNode->m_GroupBounds.location != m_StartGroupBounds.location)
            Editor->MakeDirty(SaveReasonFlags::Position | SaveReasonFlags::User, m_SizedNode);

        if (m_SizedNode->m_Bounds.size != m_StartBounds.size || m_SizedNode->m_GroupBounds.size != m_StartGroupBounds.size)
            Editor->MakeDirty(SaveReasonFlags::Size | SaveReasonFlags::User, m_SizedNode);

        m_SizedNode = nullptr;
    }

    if (!m_IsActive)
        return false;

    if (control.ActiveNode == m_SizedNode || !m_Stable)
    {
        const auto dragOffset = (control.ActiveNode == m_SizedNode) ? to_point(ImGui::GetMouseDragDelta(0, 0.0f)) : m_LastDragOffset;
        m_LastDragOffset = dragOffset;

        // Calculate new rect size
        auto p0 = m_StartBounds.top_left();
        auto p1 = m_StartBounds.bottom_right();

        if ((m_Pivot & rect_region::top) == rect_region::top)
            p0.y = std::min(p0.y + dragOffset.y, p1.y);
        if ((m_Pivot & rect_region::bottom) == rect_region::bottom)
            p1.y = std::max(p1.y + dragOffset.y, p0.y);
        if ((m_Pivot & rect_region::left) == rect_region::left)
            p0.x = std::min(p0.x + dragOffset.x, p1.x);
        if ((m_Pivot & rect_region::right) == rect_region::right)
            p1.x = std::max(p1.x + dragOffset.x, p0.x);

        auto newBounds = ax::rect(p0, p1);

        m_Stable = true;

        // Apply dynamic minimum size constrains. It will always be one frame off
        if (m_SizedNode->m_Bounds.w > m_LastSize.w)
        {
            if ((m_Pivot & rect_region::left) == rect_region::left)
            {
                  m_LastSize.w = newBounds.w;
                 newBounds.w = m_SizedNode->m_Bounds.w;
                 newBounds.x = m_StartBounds.right() - newBounds.w;
            }
            else
            {
                  m_LastSize.w = newBounds.w;
                 newBounds.w = m_SizedNode->m_Bounds.w;
            }
        }
        else
        {
            if (m_LastSize.w != newBounds.w)
                m_Stable &= false;

            m_LastSize.w = newBounds.w;
        }

        if (m_SizedNode->m_Bounds.h > m_LastSize.h)
        {
            if ((m_Pivot & rect_region::top) == rect_region::top)
            {
                 m_LastSize.h = newBounds.h;
                newBounds.h = m_SizedNode->m_Bounds.h;
                newBounds.y = m_StartBounds.bottom() - newBounds.h;
            }
            else
            {
                 m_LastSize.h = newBounds.h;
                newBounds.h = m_SizedNode->m_Bounds.h;
            }
        }
        else
        {
            if (m_LastSize.h != newBounds.h)
                m_Stable &= false;

            m_LastSize.h = newBounds.h;
        }

        m_SizedNode->m_Bounds      = newBounds;
        m_SizedNode->m_GroupBounds = newBounds.expanded(
            m_StartBounds.left()        - m_StartGroupBounds.left(),
            m_StartBounds.top()         - m_StartGroupBounds.top(),
            m_StartGroupBounds.right()  - m_StartBounds.right(),
            m_StartGroupBounds.bottom() - m_StartBounds.bottom());
    }
    else if (!control.ActiveNode)
    {
        m_Clean = true;
        m_IsActive = false;
        return true;
    }

    return m_IsActive;
}

void ed::SizeAction::ShowMetrics()
{
    EditorAction::ShowMetrics();

    auto getObjectName = [](Object* object)
    {
        if (!object) return "";
        else if (object->AsNode())  return "Node";
        else if (object->AsPin())   return "Pin";
        else if (object->AsLink())  return "Link";
        else return "";
    };

    ImGui::Text("%s:", GetName());
    ImGui::Text("    Active: %s", m_IsActive ? "yes" : "no");
    ImGui::Text("    Node: %s (%d)", getObjectName(m_SizedNode), m_SizedNode ? m_SizedNode->m_ID : 0);
    if (m_SizedNode && m_IsActive)
    {
        ImGui::Text("    Bounds: { x=%d y=%d w=%d h=%d }", m_SizedNode->m_Bounds.x, m_SizedNode->m_Bounds.y, m_SizedNode->m_Bounds.w, m_SizedNode->m_Bounds.h);
        ImGui::Text("    Group Bounds: { x=%d y=%d w=%d h=%d }", m_SizedNode->m_GroupBounds.x, m_SizedNode->m_GroupBounds.y, m_SizedNode->m_GroupBounds.w, m_SizedNode->m_GroupBounds.h);
        ImGui::Text("    Start Bounds: { x=%d y=%d w=%d h=%d }", m_StartBounds.x, m_StartBounds.y, m_StartBounds.w, m_StartBounds.h);
        ImGui::Text("    Start Group Bounds: { x=%d y=%d w=%d h=%d }", m_StartGroupBounds.x, m_StartGroupBounds.y, m_StartGroupBounds.w, m_StartGroupBounds.h);
        ImGui::Text("    Last Size: { w=%d h=%d }", m_LastSize.w, m_LastSize.h);
        ImGui::Text("    Stable: %s", m_Stable ? "Yes" : "No");
    }
}

ax::rect_region ed::SizeAction::GetRegion(Node* node)
{
    if (!node || !IsGroup(node))
        return rect_region::center;

    rect_region region = rect_region::center;
    const auto mousePos = to_point(ImGui::GetMousePos());
    const auto closestPoint = node->m_Bounds.get_closest_point_hollow(to_point(ImGui::GetMousePos()), static_cast<int>(node->m_Rounding), &region);

    auto distance = (mousePos - closestPoint).length_sq();
    if (distance > (c_GroupSelectThickness * c_GroupSelectThickness))
        return rect_region::center;

    return region;
}

ImGuiMouseCursor ed::SizeAction::ChooseCursor(ax::rect_region region)
{
    switch (region)
    {
        default:
        case rect_region::center:
            return ImGuiMouseCursor_Arrow;

        case rect_region::top:
        case rect_region::bottom:
            return ImGuiMouseCursor_ResizeNS;

        case rect_region::left:
        case rect_region::right:
            return ImGuiMouseCursor_ResizeEW;

        case rect_region::top_left:
        case rect_region::bottom_right:
            return ImGuiMouseCursor_ResizeNWSE;

        case rect_region::top_right:
        case rect_region::bottom_left:
            return ImGuiMouseCursor_ResizeNESW;
    }
}




//------------------------------------------------------------------------------
//
// Drag Action
//
//------------------------------------------------------------------------------
ed::DragAction::DragAction(EditorContext* editor):
    EditorAction(editor),
    m_IsActive(false),
    m_Clear(false),
    m_DraggedObject(nullptr)
{
}

ed::EditorAction::AcceptResult ed::DragAction::Accept(const Control& control)
{
    assert(!m_IsActive);

    if (m_IsActive)
        return False;

    if (control.ActiveObject && ImGui::IsMouseDragging(0))
    {
        if (!control.ActiveObject->AcceptDrag())
            return False;

        m_DraggedObject = control.ActiveObject;

        m_Objects.resize(0);
        m_Objects.push_back(m_DraggedObject);

        if (Editor->IsSelected(m_DraggedObject))
        {
            for (auto selectedObject : Editor->GetSelectedObjects())
                if (auto selectedNode = selectedObject->AsNode())
                    if (selectedNode != m_DraggedObject && selectedNode->AcceptDrag())
                        m_Objects.push_back(selectedNode);
        }

        auto& io = ImGui::GetIO();
        if (!io.KeyShift)
        {
            std::vector<Node*> groupedNodes;
            for (auto object : m_Objects)
                if (auto node = object->AsNode())
                    node->GetGroupedNodes(groupedNodes, true);

            auto isAlreadyPicked = [this](Node* node)
            {
                return std::find(m_Objects.begin(), m_Objects.end(), node) != m_Objects.end();
            };

            for (auto candidate : groupedNodes)
                if (!isAlreadyPicked(candidate) && candidate->AcceptDrag())
                    m_Objects.push_back(candidate);
        }

        m_IsActive = true;
    }

    return m_IsActive ? True : False;
}

bool ed::DragAction::Process(const Control& control)
{
    if (m_Clear)
    {
        m_Clear = false;

        for (auto object : m_Objects)
        {
            if (object->EndDrag())
                Editor->MakeDirty(SaveReasonFlags::Position | SaveReasonFlags::User, object->AsNode());
        }

        m_Objects.resize(0);

        m_DraggedObject = nullptr;
    }

    if (!m_IsActive)
        return false;

    if (control.ActiveObject == m_DraggedObject)
    {
        auto dragOffset = to_point(ImGui::GetMouseDragDelta(0, 0.0f));

        auto draggedOrigin  = (ax::point)m_DraggedObject->DragStartLocation();
        auto alignPivot     = point(0, 0);

        // TODO: Move this experimental alignment to closes pivot out of internals to node API
        if (auto draggedNode = m_DraggedObject->AsNode())
        {
            int x = INT_MAX;
            int y = INT_MAX;

            auto testPivot = [this, &x, &y, &draggedOrigin, &dragOffset, &alignPivot](point pivot)
            {
                auto initial   = draggedOrigin + dragOffset + pivot;
                auto candidate = Editor->AlignPointToGrid(initial) - draggedOrigin - pivot;

                if (abs(candidate.x) < abs(std::min(x, INT_MAX)))
                {
                    x = candidate.x;
                    alignPivot.x = pivot.x;
                }

                if (abs(candidate.y) < abs(std::min(y, INT_MAX)))
                {
                    y = candidate.y;
                    alignPivot.y = pivot.y;
                }
            };

            for (auto pin = draggedNode->m_LastPin; pin; pin = pin->m_PreviousPin)
            {
                auto pivot = (point)pin->m_Pivot.center() - draggedNode->m_Bounds.top_left();
                testPivot(pivot);
            }

            //testPivot(point(0, 0));
        }

        auto alignedOffset  = Editor->AlignPointToGrid(draggedOrigin + dragOffset + alignPivot) - draggedOrigin - alignPivot;

        if (!ImGui::GetIO().KeyAlt)
            dragOffset = alignedOffset;

        for (auto object : m_Objects)
            object->UpdateDrag(dragOffset);
    }
    else if (!control.ActiveObject)
    {
        m_Clear = true;

        m_IsActive = false;
        return true;
    }

    return m_IsActive;
}

void ed::DragAction::ShowMetrics()
{
    EditorAction::ShowMetrics();

    auto getObjectName = [](Object* object)
    {
        if (!object) return "";
        else if (object->AsNode())  return "Node";
        else if (object->AsPin())   return "Pin";
        else if (object->AsLink())  return "Link";
        else return "";
    };

    ImGui::Text("%s:", GetName());
    ImGui::Text("    Active: %s", m_IsActive ? "yes" : "no");
    ImGui::Text("    Node: %s (%d)", getObjectName(m_DraggedObject), m_DraggedObject ? m_DraggedObject->m_ID : 0);
}




//------------------------------------------------------------------------------
//
// Select Action
//
//------------------------------------------------------------------------------
ed::SelectAction::SelectAction(EditorContext* editor):
    EditorAction(editor),
    m_IsActive(false),
    m_SelectGroups(false),
    m_SelectLinkMode(false),
    m_CommitSelection(false),
    m_StartPoint(),
    m_Animation(editor)
{
}

ed::EditorAction::AcceptResult ed::SelectAction::Accept(const Control& control)
{
    assert(!m_IsActive);

    if (m_IsActive)
        return False;

    auto& io = ImGui::GetIO();
    m_SelectGroups   = io.KeyShift;
    m_SelectLinkMode = io.KeyAlt;

    m_SelectedObjectsAtStart.clear();

    if (control.BackgroundActive && ImGui::IsMouseDragging(0, 1))
    {
        m_IsActive = true;
        m_StartPoint = ImGui::GetMousePos();
        m_EndPoint   = m_StartPoint;

        // Links and nodes cannot be selected together
        if ((m_SelectLinkMode && Editor->IsAnyNodeSelected()) ||
           (!m_SelectLinkMode && Editor->IsAnyLinkSelected()))
        {
            Editor->ClearSelection();
        }

        if (io.KeyCtrl)
            m_SelectedObjectsAtStart = Editor->GetSelectedObjects();
    }
    else if (control.BackgroundClicked)
    {
        Editor->ClearSelection();
    }
    else
    {
        Object* clickedObject = control.ClickedNode ? static_cast<Object*>(control.ClickedNode) : static_cast<Object*>(control.ClickedLink);

        if (clickedObject)
        {
            // Links and nodes cannot be selected together
            if ((clickedObject->AsLink() && Editor->IsAnyNodeSelected()) ||
                (clickedObject->AsNode() && Editor->IsAnyLinkSelected()))
            {
                Editor->ClearSelection();
            }

            if (io.KeyCtrl)
                Editor->ToggleObjectSelection(clickedObject);
            else
                Editor->SetSelectedObject(clickedObject);
        }
    }

    if (m_IsActive)
        m_Animation.Stop();

    return m_IsActive ? True : False;
}

bool ed::SelectAction::Process(const Control& control)
{
    if (m_CommitSelection)
    {
        Editor->ClearSelection();
        for (auto object : m_CandidateObjects)
            Editor->SelectObject(object);

        m_CandidateObjects.clear();

        m_CommitSelection = false;
    }

    if (!m_IsActive)
        return false;

    if (ImGui::IsMouseDragging(0, 0))
    {
        m_EndPoint = ImGui::GetMousePos();

        auto topLeft     = ImVec2(std::min(m_StartPoint.x, m_EndPoint.x), std::min(m_StartPoint.y, m_EndPoint.y));
        auto bottomRight = ImVec2(std::max(m_StartPoint.x, m_EndPoint.x), std::max(m_StartPoint.y, m_EndPoint.y));
        auto rect        = ax::rectf(to_pointf(topLeft), to_pointf(bottomRight));
        if (rect.w <= 0)
            rect.w = 1;
        if (rect.h <= 0)
            rect.h = 1;

        vector<Node*> nodes;
        vector<Link*> links;

        if (m_SelectLinkMode)
        {
            Editor->FindLinksInRect(rect, links);
            m_CandidateObjects.assign(links.begin(), links.end());
        }
        else
        {
            Editor->FindNodesInRect(rect, nodes);
            m_CandidateObjects.assign(nodes.begin(), nodes.end());

            if (m_SelectGroups)
            {
                auto endIt = std::remove_if(m_CandidateObjects.begin(), m_CandidateObjects.end(), [](Object* object) { return !IsGroup(object->AsNode()); });
                m_CandidateObjects.erase(endIt, m_CandidateObjects.end());
            }
            else
            {
                auto endIt = std::remove_if(m_CandidateObjects.begin(), m_CandidateObjects.end(), [](Object* object) { return IsGroup(object->AsNode()); });
                m_CandidateObjects.erase(endIt, m_CandidateObjects.end());
            }
        }

        m_CandidateObjects.insert(m_CandidateObjects.end(), m_SelectedObjectsAtStart.begin(), m_SelectedObjectsAtStart.end());
        std::sort(m_CandidateObjects.begin(), m_CandidateObjects.end());
        m_CandidateObjects.erase(std::unique(m_CandidateObjects.begin(), m_CandidateObjects.end()), m_CandidateObjects.end());
    }
    else
    {
        m_IsActive = false;

        m_Animation.Play(c_SelectionFadeOutDuration);

        m_CommitSelection = true;

        return true;
    }

    return m_IsActive;
}

void ed::SelectAction::ShowMetrics()
{
    EditorAction::ShowMetrics();

    ImGui::Text("%s:", GetName());
    ImGui::Text("    Active: %s", m_IsActive ? "yes" : "no");
}

void ed::SelectAction::Draw(ImDrawList* drawList)
{
    if (!m_IsActive && !m_Animation.IsPlaying())
        return;

    const auto alpha = m_Animation.IsPlaying() ? easing::ease_out_quad(1.0f, -1.0f, m_Animation.GetProgress()) : 1.0f;

    const auto fillColor    = Editor->GetColor(m_SelectLinkMode ? StyleColor_LinkSelRect       : StyleColor_NodeSelRect, alpha);
    const auto outlineColor = Editor->GetColor(m_SelectLinkMode ? StyleColor_LinkSelRectBorder : StyleColor_NodeSelRectBorder, alpha);

    drawList->ChannelsSetCurrent(c_BackgroundChannel_SelectionRect);

    auto min  = ImVec2(std::min(m_StartPoint.x, m_EndPoint.x), std::min(m_StartPoint.y, m_EndPoint.y));
    auto max  = ImVec2(std::max(m_StartPoint.x, m_EndPoint.x), std::max(m_StartPoint.y, m_EndPoint.y));

    drawList->AddRectFilled(min, max, fillColor);
    ImGui::PushStyleVar(ImGuiStyleVar_AntiAliasFringeScale, 1.0f);
    drawList->AddRect(min, max, outlineColor);
    ImGui::PopStyleVar();
}




//------------------------------------------------------------------------------
//
// Context Menu Action
//
//------------------------------------------------------------------------------
ed::ContextMenuAction::ContextMenuAction(EditorContext* editor):
    EditorAction(editor),
    m_CandidateMenu(Menu::None),
    m_CurrentMenu(Menu::None),
    m_ContextId(0)
{
}

ed::EditorAction::AcceptResult ed::ContextMenuAction::Accept(const Control& control)
{
    const auto isPressed  = ImGui::IsMouseClicked(1);
    const auto isReleased = ImGui::IsMouseReleased(1);
    const auto isDragging = ImGui::IsMouseDragging(1);

    if (isPressed || isReleased || isDragging)
    {
        Menu candidateMenu = ContextMenuAction::None;
        int  contextId     = 0;

        if (auto hotObejct = control.HotObject)
        {
            if (hotObejct->AsNode())
                candidateMenu = Node;
            else if (hotObejct->AsPin())
                candidateMenu = Pin;
            else if (hotObejct->AsLink())
                candidateMenu = Link;

            if (candidateMenu != None)
                contextId = hotObejct->m_ID;
        }
        else if (control.BackgroundHot)
            candidateMenu = Background;

        if (isPressed)
        {
            m_CandidateMenu = candidateMenu;
            m_ContextId     = contextId;
            return Possible;
        }
        else if (isReleased && m_CandidateMenu == candidateMenu && m_ContextId == contextId)
        {
            m_CurrentMenu   = m_CandidateMenu;
            m_CandidateMenu = ContextMenuAction::None;
            return True;
        }
        else
        {
            m_CandidateMenu = None;
            m_CurrentMenu   = None;
            m_ContextId     = 0;
            return False;
        }
    }

    return False;
}

bool ed::ContextMenuAction::Process(const Control& control)
{
    m_CandidateMenu = None;
    m_CurrentMenu   = None;
    m_ContextId     = 0;
    return false;
}

void ed::ContextMenuAction::Reject()
{
    m_CandidateMenu = None;
    m_CurrentMenu   = None;
    m_ContextId     = 0;
}

void ed::ContextMenuAction::ShowMetrics()
{
    EditorAction::ShowMetrics();

    auto getMenuName = [](Menu menu)
    {
        switch (menu)
        {
            default:
            case None:        return "None";
            case Node:        return "Node";
            case Pin:         return "Pin";
            case Link:        return "Link";
            case Background:  return "Background";
        }
    };


    ImGui::Text("%s:", GetName());
    ImGui::Text("    Menu: %s", getMenuName(m_CurrentMenu));
}

bool ed::ContextMenuAction::ShowNodeContextMenu(int* nodeId)
{
    if (m_CurrentMenu != Node)
        return false;

    *nodeId = m_ContextId;
    Editor->SetUserContext();
    return true;
}

bool ed::ContextMenuAction::ShowPinContextMenu(int* pinId)
{
    if (m_CurrentMenu != Pin)
        return false;

    *pinId = m_ContextId;
    Editor->SetUserContext();
    return true;
}

bool ed::ContextMenuAction::ShowLinkContextMenu(int* linkId)
{
    if (m_CurrentMenu != Link)
        return false;

    *linkId = m_ContextId;
    Editor->SetUserContext();
    return true;
}

bool ed::ContextMenuAction::ShowBackgroundContextMenu()
{
    if (m_CurrentMenu != Background)
        return false;

    Editor->SetUserContext();
    return true;
}




//------------------------------------------------------------------------------
//
// Cut/Copy/Paste Action
//
//------------------------------------------------------------------------------
ed::ShortcutAction::ShortcutAction(EditorContext* editor):
    EditorAction(editor),
    m_IsActive(false),
    m_InAction(false),
    m_CurrentAction(Action::None),
    m_Context()
{
}

ed::EditorAction::AcceptResult ed::ShortcutAction::Accept(const Control& control)
{
    if (!Editor->IsActive() || !Editor->AreShortcutsEnabled())
        return False;

    Action candidateAction = None;

    auto& io = ImGui::GetIO();
    if (io.KeyCtrl && !io.KeyShift && !io.KeyAlt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_X)))
        candidateAction = Cut;
    if (io.KeyCtrl && !io.KeyShift && !io.KeyAlt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_C)))
        candidateAction = Copy;
    if (io.KeyCtrl && !io.KeyShift && !io.KeyAlt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_V)))
        candidateAction = Paste;
    if (io.KeyCtrl && !io.KeyShift && !io.KeyAlt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_D)))
        candidateAction = Duplicate;
    if (!io.KeyCtrl && !io.KeyShift && !io.KeyAlt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Space)))
        candidateAction = CreateNode;

    if (candidateAction != None)
    {
        if (candidateAction != Paste && candidateAction != CreateNode)
        {
            auto& selection = Editor->GetSelectedObjects();
            if (!selection.empty())
            {
                // #TODO: Find a way to simplify logic.

                m_Context.assign(selection.begin(), selection.end());

                // Expand groups
                vector<Node*> extra;
                for (auto object : m_Context)
                {
                    auto node = object->AsNode();
                    if (IsGroup(node))
                        node->GetGroupedNodes(extra, true);
                }

                // Apply groups and remove duplicates
                if (!extra.empty())
                {
                    m_Context.insert(m_Context.end(), extra.begin(), extra.end());
                    std::sort(m_Context.begin(), m_Context.end());
                    m_Context.erase(std::unique(m_Context.begin(), m_Context.end()), m_Context.end());
                }
            }
            else if (control.HotObject && control.HotObject->IsSelectable() && !IsGroup(control.HotObject->AsNode()))
            {
                m_Context.push_back(control.HotObject);
            }

            if (m_Context.empty())
                return False;

            // Does copying only links make sense?
            //const auto hasOnlyLinks = std::all_of(Context.begin(), Context.end(), [](Object* object) { return object->AsLink() != nullptr; });
            //if (hasOnlyLinks)
            //    return False;

            // If no links are selected, pick all links between nodes within context
            const auto hasAnyLinks = std::any_of(m_Context.begin(), m_Context.end(), [](Object* object) { return object->AsLink() != nullptr; });
            if (!hasAnyLinks && m_Context.size() > 1) // one node cannot make connection to anything
            {
                // Collect nodes in sorted vector viable for binary search
                std::vector<ObjectWrapper<Node>> nodes;

                nodes.reserve(m_Context.size());
                std::for_each(m_Context.begin(), m_Context.end(), [&nodes](Object* object) { if (auto node = object->AsNode()) nodes.push_back({node->m_ID, node}); });

                std::sort(nodes.begin(), nodes.end());

                auto isNodeInContext = [&nodes](int nodeId)
                {
                    return std::binary_search(nodes.begin(), nodes.end(), ObjectWrapper<Node>{nodeId, nullptr});
                };

                // Collect links connected to nodes and drop those reaching out of context
                std::vector<Link*> links;

                for (auto node : nodes)
                    Editor->FindLinksForNode(node.m_ID, links, true);

                // Remove duplicates
                std::sort(links.begin(), links.end());
                links.erase(std::unique(links.begin(), links.end()), links.end());

                // Drop out of context links
                links.erase(std::remove_if(links.begin(), links.end(), [&isNodeInContext](Link* link)
                {
                    return !isNodeInContext(link->m_StartPin->m_Node->m_ID) || !isNodeInContext(link->m_EndPin->m_Node->m_ID);
                }), links.end());

                // Append links and remove duplicates
                m_Context.insert(m_Context.end(), links.begin(), links.end());
            }
        }
        else
            m_Context.resize(0);

        m_IsActive      = true;
        m_CurrentAction = candidateAction;

        return True;
    }

    return False;
}

bool ed::ShortcutAction::Process(const Control& control)
{
    m_IsActive        = false;
    m_CurrentAction   = None;
    m_Context.resize(0);
    return false;
}

void ed::ShortcutAction::Reject()
{
    m_IsActive        = false;
    m_CurrentAction   = None;
    m_Context.resize(0);
}

void ed::ShortcutAction::ShowMetrics()
{
    EditorAction::ShowMetrics();

    auto getActionName = [](Action action)
    {
        switch (action)
        {
            default:
            case None:       return "None";
            case Cut:        return "Cut";
            case Copy:       return "Copy";
            case Paste:      return "Paste";
            case Duplicate:  return "Duplicate";
            case CreateNode: return "CreateNode";
        }
    };

    ImGui::Text("%s:", GetName());
    ImGui::Text("    Action: %s", getActionName(m_CurrentAction));
}

bool ed::ShortcutAction::Begin()
{
    if (m_IsActive)
        m_InAction = true;
    return m_IsActive;
}

void ed::ShortcutAction::End()
{
    if (m_IsActive)
        m_InAction = false;
}

bool ed::ShortcutAction::AcceptCut()
{
    assert(m_InAction);
    return m_CurrentAction == Cut;
}

bool ed::ShortcutAction::AcceptCopy()
{
    assert(m_InAction);
    return m_CurrentAction == Copy;
}

bool ed::ShortcutAction::AcceptPaste()
{
    assert(m_InAction);
    return m_CurrentAction == Paste;
}

bool ed::ShortcutAction::AcceptDuplicate()
{
    assert(m_InAction);
    return m_CurrentAction == Duplicate;
}

bool ed::ShortcutAction::AcceptCreateNode()
{
    assert(m_InAction);
    return m_CurrentAction == CreateNode;
}




//------------------------------------------------------------------------------
//
// Create Item Action
//
//------------------------------------------------------------------------------
ed::CreateItemAction::CreateItemAction(EditorContext* editor):
    EditorAction(editor),
    m_InActive(false),
    m_NextStage(None),
    m_CurrentStage(None),
    m_ItemType(NoItem),
    m_UserAction(Unknown),
    m_LinkColor(IM_COL32_WHITE),
    m_LinkThickness(1.0f),
    m_LinkStart(nullptr),
    m_LinkEnd(nullptr),

    m_IsActive(false),
    m_DraggedPin(nullptr),

    m_IsInGlobalSpace(false)
{
}

ed::EditorAction::AcceptResult ed::CreateItemAction::Accept(const Control& control)
{
    assert(!m_IsActive);

    if (m_IsActive)
        return EditorAction::False;

    if (control.ActivePin && ImGui::IsMouseDragging(0))
    {
        m_DraggedPin = control.ActivePin;
        DragStart(m_DraggedPin);

        Editor->ClearSelection();
    }
    else
        return EditorAction::False;

    m_IsActive = true;

    return EditorAction::True;
}

bool ed::CreateItemAction::Process(const Control& control)
{
    assert(m_IsActive);

    if (!m_IsActive)
        return false;

    if (m_DraggedPin && control.ActivePin == m_DraggedPin && (m_CurrentStage == Possible))
    {
        const auto draggingFromSource = (m_DraggedPin->m_Kind == PinKind::Source);

        ed::Pin cursorPin(Editor, 0, draggingFromSource ? PinKind::Target : PinKind::Source);
        cursorPin.m_Pivot    = ax::rectf(to_pointf(ImGui::GetMousePos()), sizef(0, 0));
        cursorPin.m_Dir      = -m_DraggedPin->m_Dir;
        cursorPin.m_Strength =  m_DraggedPin->m_Strength;

        ed::Link candidate(Editor, 0);
        candidate.m_Color    = m_LinkColor;
        candidate.m_StartPin = draggingFromSource ? m_DraggedPin : &cursorPin;
        candidate.m_EndPin   = draggingFromSource ? &cursorPin : m_DraggedPin;

        ed::Pin*& freePin  = draggingFromSource ? candidate.m_EndPin : candidate.m_StartPin;

        if (control.HotPin)
        {
            DropPin(control.HotPin);

            if (m_UserAction == UserAccept)
                freePin = control.HotPin;
        }
        else if (control.BackgroundHot)
            DropNode();
        else
            DropNothing();

        auto drawList = ImGui::GetWindowDrawList();
        drawList->ChannelsSetCurrent(c_LinkChannel_NewLink);

        candidate.UpdateEndpoints();
        candidate.Draw(drawList, m_LinkColor, m_LinkThickness);
    }
    else if (m_CurrentStage == Possible || !control.ActivePin)
    {
        if (!ImGui::IsWindowHovered())
        {
            m_DraggedPin = nullptr;
            DropNothing();
        }

        DragEnd();
        m_IsActive = false;
    }

    return m_IsActive;
}

void ed::CreateItemAction::ShowMetrics()
{
    EditorAction::ShowMetrics();

    auto getStageName = [](Stage stage)
    {
        switch (stage)
        {
            case None:     return "None";
            case Possible: return "Possible";
            case Create:   return "Create";
            default:       return "<unknown>";
        }
    };

    auto getActionName = [](Action action)
    {
        switch (action)
        {
            default:
            case Unknown:     return "Unknown";
            case UserReject:  return "Reject";
            case UserAccept:  return "Accept";
        }
    };

    auto getItemName = [](Type item)
    {
        switch (item)
        {
            default:
            case NoItem: return "None";
            case Node:   return "Node";
            case Link:   return "Link";
        }
    };

    ImGui::Text("%s:", GetName());
    ImGui::Text("    Stage: %s", getStageName(m_CurrentStage));
    ImGui::Text("    User Action: %s", getActionName(m_UserAction));
    ImGui::Text("    Item Type: %s", getItemName(m_ItemType));
}

void ed::CreateItemAction::SetStyle(ImU32 color, float thickness)
{
    m_LinkColor     = color;
    m_LinkThickness = thickness;
}

bool ed::CreateItemAction::Begin()
{
    IM_ASSERT(false == m_InActive);

    m_InActive        = true;
    m_CurrentStage    = m_NextStage;
    m_UserAction      = Unknown;
    m_LinkColor       = IM_COL32_WHITE;
    m_LinkThickness   = 1.0f;

    if (m_CurrentStage == None)
        return false;

    return true;
}

void ed::CreateItemAction::End()
{
    IM_ASSERT(m_InActive);

    if (m_IsInGlobalSpace)
    {
        ImGui::PopClipRect();
        m_IsInGlobalSpace = false;
    }

    m_InActive = false;
}

void ed::CreateItemAction::DragStart(Pin* startPin)
{
    IM_ASSERT(!m_InActive);

    m_NextStage = Possible;
    m_LinkStart = startPin;
    m_LinkEnd   = nullptr;
}

void ed::CreateItemAction::DragEnd()
{
    IM_ASSERT(!m_InActive);

    if (m_CurrentStage == Possible && m_UserAction == UserAccept)
    {
        m_NextStage = Create;
    }
    else
    {
        m_NextStage = None;
        m_ItemType  = NoItem;
        m_LinkStart = nullptr;
        m_LinkEnd   = nullptr;
    }
}

void ed::CreateItemAction::DropPin(Pin* endPin)
{
    IM_ASSERT(!m_InActive);

    m_ItemType = Link;
    m_LinkEnd  = endPin;
}

void ed::CreateItemAction::DropNode()
{
    IM_ASSERT(!m_InActive);

    m_ItemType = Node;
    m_LinkEnd  = nullptr;
}

void ed::CreateItemAction::DropNothing()
{
    IM_ASSERT(!m_InActive);

    m_ItemType = NoItem;
    m_LinkEnd  = nullptr;
}

ed::CreateItemAction::Result ed::CreateItemAction::RejectItem()
{
    IM_ASSERT(m_InActive);

    if (!m_InActive || m_CurrentStage == None || m_ItemType == NoItem)
        return Indeterminate;

    m_UserAction = UserReject;

    return True;
}

ed::CreateItemAction::Result ed::CreateItemAction::AcceptItem()
{
    IM_ASSERT(m_InActive);

    if (!m_InActive || m_CurrentStage == None || m_ItemType == NoItem)
        return Indeterminate;

    m_UserAction = UserAccept;

    if (m_CurrentStage == Create)
    {
        m_NextStage = None;
        m_ItemType  = NoItem;
        m_LinkStart = nullptr;
        m_LinkEnd   = nullptr;
        return True;
    }
    else
        return False;
}

ed::CreateItemAction::Result ed::CreateItemAction::QueryLink(int* startId, int* endId)
{
    IM_ASSERT(m_InActive);

    if (!m_InActive || m_CurrentStage == None || m_ItemType != Link)
        return Indeterminate;

    int linkStartId = m_LinkStart->m_ID;
    int linkEndId   = m_LinkEnd->m_ID;

    *startId = linkStartId;
    *endId   = linkEndId;

    Editor->SetUserContext(true);

    if (!m_IsInGlobalSpace)
    {
        auto& canvas = Editor->GetCanvas();
        ImGui::PushClipRect(canvas.WindowScreenPos + ImVec2(1, 1), canvas.WindowScreenPos + canvas.WindowScreenSize - ImVec2(1, 1), false);
        m_IsInGlobalSpace = true;
    }

    return True;
}

ed::CreateItemAction::Result ed::CreateItemAction::QueryNode(int* pinId)
{
    IM_ASSERT(m_InActive);

    if (!m_InActive || m_CurrentStage == None || m_ItemType != Node)
        return Indeterminate;

    *pinId = m_LinkStart ? m_LinkStart->m_ID : 0;

    Editor->SetUserContext(true);

    if (!m_IsInGlobalSpace)
    {
        auto& canvas = Editor->GetCanvas();
        ImGui::PushClipRect(canvas.WindowScreenPos + ImVec2(1, 1), canvas.WindowScreenPos + canvas.WindowScreenSize - ImVec2(1, 1), false);
        m_IsInGlobalSpace = true;
    }

    return True;
}




//------------------------------------------------------------------------------
//
// Delete Items Action
//
//------------------------------------------------------------------------------
ed::DeleteItemsAction::DeleteItemsAction(EditorContext* editor):
    EditorAction(editor),
    m_IsActive(false),
    m_InInteraction(false),
    m_CurrentItemType(Unknown),
    m_UserAction(Undetermined)
{
}

ed::EditorAction::AcceptResult ed::DeleteItemsAction::Accept(const Control& control)
{
    assert(!m_IsActive);

    if (m_IsActive)
        return False;

    auto addDeadLinks = [this]()
    {
        vector<ed::Link*> links;
        for (auto object : m_CandidateObjects)
        {
            auto node = object->AsNode();
            if (!node)
                continue;

            Editor->FindLinksForNode(node->m_ID, links, true);
        }
        if (!links.empty())
        {
            std::sort(links.begin(), links.end());
            links.erase(std::unique(links.begin(), links.end()), links.end());
            m_CandidateObjects.insert(m_CandidateObjects.end(), links.begin(), links.end());
        }
    };

    auto& io = ImGui::GetIO();
    if (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Delete)) && Editor->AreShortcutsEnabled())
    {
        auto& selection = Editor->GetSelectedObjects();
        if (!selection.empty())
        {
            m_CandidateObjects = selection;
            addDeadLinks();
            m_IsActive = true;
            return True;
        }
    }
    else if (control.ClickedLink && io.KeyAlt)
    {
        m_CandidateObjects.clear();
        m_CandidateObjects.push_back(control.ClickedLink);
        m_IsActive = true;
        return True;
    }

    else if (!m_ManuallyDeletedObjects.empty())
    {
        m_CandidateObjects = m_ManuallyDeletedObjects;
        m_ManuallyDeletedObjects.clear();
        addDeadLinks();
        m_IsActive = true;
        return True;
    }

    return m_IsActive ? True : False;
}

bool ed::DeleteItemsAction::Process(const Control& control)
{
    if (!m_IsActive)
        return false;

    if (m_CandidateObjects.empty())
    {
        m_IsActive = false;
        return true;
    }

    return m_IsActive;
}

void ed::DeleteItemsAction::ShowMetrics()
{
    EditorAction::ShowMetrics();

    //auto getObjectName = [](Object* object)
    //{
    //    if (!object) return "";
    //    else if (object->AsNode()) return "Node";
    //    else if (object->AsPin())  return "Pin";
    //    else if (object->AsLink()) return "Link";
    //    else return "";
    //};

    ImGui::Text("%s:", GetName());
    ImGui::Text("    Active: %s", m_IsActive ? "yes" : "no");
    //ImGui::Text("    Node: %s (%d)", getObjectName(DeleteItemsgedNode), DeleteItemsgedNode ? DeleteItemsgedNode->ID : 0);
}

bool ed::DeleteItemsAction::Add(Object* object)
{
    if (Editor->GetCurrentAction() != nullptr)
        return false;

    m_ManuallyDeletedObjects.push_back(object);

    return true;
}

bool ed::DeleteItemsAction::Begin()
{
    if (!m_IsActive)
        return false;

    assert(!m_InInteraction);
    m_InInteraction = true;

    m_CurrentItemType = Unknown;
    m_UserAction      = Undetermined;

    return m_IsActive;
}

void ed::DeleteItemsAction::End()
{
    if (!m_IsActive)
        return;

    assert(m_InInteraction);
    m_InInteraction = false;
}

bool ed::DeleteItemsAction::QueryLink(int* linkId, int* startId, int* endId)
{
    if (!QueryItem(linkId, Link))
        return false;

    if (startId || endId)
    {
        auto link = Editor->FindLink(*linkId);
        if (startId)
            *startId = link->m_StartPin->m_ID;
        if (endId)
            *endId = link->m_EndPin->m_ID;
    }

    return true;
}

bool ed::DeleteItemsAction::QueryNode(int* nodeId)
{
    return QueryItem(nodeId, Node);
}

bool ed::DeleteItemsAction::QueryItem(int* itemId, IteratorType itemType)
{
    if (!m_InInteraction)
        return false;

    if (m_CurrentItemType != itemType)
    {
        m_CurrentItemType    = itemType;
        m_CandidateItemIndex = 0;
    }
    else if (m_UserAction == Undetermined)
    {
        RejectItem();
    }

    m_UserAction = Undetermined;

    auto itemCount = (int)m_CandidateObjects.size();
    while (m_CandidateItemIndex < itemCount)
    {
        auto item = m_CandidateObjects[m_CandidateItemIndex];
        if (itemType == Node)
        {
            if (auto node = item->AsNode())
            {
                *itemId = node->m_ID;
                return true;
            }
        }
        else if (itemType == Link)
        {
            if (auto link = item->AsLink())
            {
                *itemId = link->m_ID;
                return true;
            }
        }

        ++m_CandidateItemIndex;
    }

    if (m_CandidateItemIndex == itemCount)
        m_CurrentItemType = Unknown;

    return false;
}

bool ed::DeleteItemsAction::AcceptItem()
{
    if (!m_InInteraction)
        return false;

    m_UserAction = Accepted;

    RemoveItem();

    return true;
}

void ed::DeleteItemsAction::RejectItem()
{
    if (!m_InInteraction)
        return;

    m_UserAction = Rejected;

    RemoveItem();
}

void ed::DeleteItemsAction::RemoveItem()
{
    auto item = m_CandidateObjects[m_CandidateItemIndex];
    m_CandidateObjects.erase(m_CandidateObjects.begin() + m_CandidateItemIndex);

    Editor->DeselectObject(item);

    if (m_CurrentItemType == Link)
        Editor->NotifyLinkDeleted(item->AsLink());
}




//------------------------------------------------------------------------------
//
// Node Builder
//
//------------------------------------------------------------------------------
ed::NodeBuilder::NodeBuilder(EditorContext* editor):
    Editor(editor),
    m_CurrentNode(nullptr),
    m_CurrentPin(nullptr)
{
}

void ed::NodeBuilder::Begin(int nodeId)
{
    assert(nullptr == m_CurrentNode);

    m_CurrentNode = Editor->GetNode(nodeId);

    if (m_CurrentNode->m_RestoreState)
    {
        Editor->RestoreNodeState(m_CurrentNode);
        m_CurrentNode->m_RestoreState = false;
    }

    if (m_CurrentNode->m_CenterOnScreen)
    {
        auto bounds = Editor->GetCanvas().GetVisibleBounds();
        auto offset = static_cast<ax::point>(bounds.centerf() -  m_CurrentNode->m_Bounds.centerf());

        if (offset.length_sq() > 0)
        {
            if (::IsGroup(m_CurrentNode))
            {
                std::vector<Node*> groupedNodes;
                m_CurrentNode->GetGroupedNodes(groupedNodes);
                groupedNodes.push_back(m_CurrentNode);

                for (auto node : groupedNodes)
                {
                    node->m_Bounds.location += offset;
                    node->m_GroupBounds.location += offset;
                    Editor->MakeDirty(SaveReasonFlags::Position | SaveReasonFlags::User, node);
                }
            }
            else
            {
                m_CurrentNode->m_Bounds.location += offset;
                m_CurrentNode->m_GroupBounds.location += offset;
                Editor->MakeDirty(SaveReasonFlags::Position | SaveReasonFlags::User, m_CurrentNode);
            }
        }

        m_CurrentNode->m_CenterOnScreen = false;
    }

    // Position node on screen
    ImGui::SetCursorScreenPos(to_imvec(m_CurrentNode->m_Bounds.location));

    auto& editorStyle = Editor->GetStyle();

    const auto alpha = ImGui::GetStyle().Alpha;

    m_CurrentNode->m_IsLive           = true;
    m_CurrentNode->m_LastPin          = nullptr;
    m_CurrentNode->m_Color            = Editor->GetColor(StyleColor_NodeBg, alpha);
    m_CurrentNode->m_BorderColor      = Editor->GetColor(StyleColor_NodeBorder, alpha);
    m_CurrentNode->m_BorderWidth      = editorStyle.NodeBorderWidth;
    m_CurrentNode->m_Rounding         = editorStyle.NodeRounding;
    m_CurrentNode->m_GroupColor       = Editor->GetColor(StyleColor_GroupBg, alpha);
    m_CurrentNode->m_GroupBorderColor = Editor->GetColor(StyleColor_GroupBorder, alpha);
    m_CurrentNode->m_GroupBorderWidth = editorStyle.GroupBorderWidth;
    m_CurrentNode->m_GroupRounding    = editorStyle.GroupRounding;

    m_IsGroup = false;

    // Grow channel list and select user channel
    if (auto drawList = ImGui::GetWindowDrawList())
    {
        m_CurrentNode->m_Channel = drawList->_ChannelsCount;
        ImDrawList_ChannelsGrow(drawList, drawList->_ChannelsCount + c_ChannelsPerNode);
        drawList->ChannelsSetCurrent(m_CurrentNode->m_Channel + c_NodeContentChannel);
    }

    // Begin outer group
    ImGui::BeginGroup();

    // Apply frame padding. Begin inner group if necessary.
    if (editorStyle.NodePadding.x != 0 || editorStyle.NodePadding.y != 0 || editorStyle.NodePadding.z != 0 || editorStyle.NodePadding.w != 0)
    {
        ImGui::SetCursorPos(ImGui::GetCursorPos() + ImVec2(editorStyle.NodePadding.x, editorStyle.NodePadding.y));
        ImGui::BeginGroup();
    }
}

void ed::NodeBuilder::End()
{
    assert(nullptr != m_CurrentNode);

    // Apply frame padding. This must be done in this convoluted way if outer group
    // size must contain inner group padding.
    auto& editorStyle = Editor->GetStyle();
    if (editorStyle.NodePadding.x != 0 || editorStyle.NodePadding.y != 0 || editorStyle.NodePadding.z != 0 || editorStyle.NodePadding.w != 0)
    {
        ImGui::EndGroup();
        ImGui::SameLine(0, editorStyle.NodePadding.z);
        ImGui::Dummy(ImVec2(0, 0));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + editorStyle.NodePadding.w);
    }

    // End outer group.
    ImGui::EndGroup();

    m_NodeRect = ImGui_GetItemRect();

    if (m_CurrentNode->m_Bounds.size != m_NodeRect.size)
    {
        m_CurrentNode->m_Bounds.size = m_NodeRect.size;
        Editor->MakeDirty(SaveReasonFlags::Size, m_CurrentNode);
    }

    if (m_IsGroup)
    {
        // Groups cannot have pins. Discard them.
        for (auto pin = m_CurrentNode->m_LastPin; pin; pin = pin->m_PreviousPin)
            pin->Reset();

        m_CurrentNode->m_Type        = NodeType::Group;
        m_CurrentNode->m_GroupBounds = m_GroupBounds;
        m_CurrentNode->m_LastPin     = nullptr;
    }
    else
        m_CurrentNode->m_Type        = NodeType::Node;

    m_CurrentNode = nullptr;
}

void ed::NodeBuilder::BeginPin(int pinId, PinKind kind)
{
    assert(nullptr != m_CurrentNode);
    assert(nullptr == m_CurrentPin);
    assert(false   == m_IsGroup);

    auto& editorStyle = Editor->GetStyle();

    m_CurrentPin = Editor->GetPin(pinId, kind);
    m_CurrentPin->m_Node = m_CurrentNode;

    m_CurrentPin->m_IsLive      = true;
    m_CurrentPin->m_Color       = Editor->GetColor(StyleColor_PinRect);
    m_CurrentPin->m_BorderColor = Editor->GetColor(StyleColor_PinRectBorder);
    m_CurrentPin->m_BorderWidth = editorStyle.PinBorderWidth;
    m_CurrentPin->m_Rounding    = editorStyle.PinRounding;
    m_CurrentPin->m_Corners     = static_cast<int>(editorStyle.PinCorners);
    m_CurrentPin->m_Radius      = editorStyle.PinRadius;
    m_CurrentPin->m_ArrowSize   = editorStyle.PinArrowSize;
    m_CurrentPin->m_ArrowWidth  = editorStyle.PinArrowWidth;
    m_CurrentPin->m_Dir         = kind == PinKind::Source ? editorStyle.SourceDirection : editorStyle.TargetDirection;
    m_CurrentPin->m_Strength    = editorStyle.LinkStrength;

    m_CurrentPin->m_PreviousPin = m_CurrentNode->m_LastPin;
    m_CurrentNode->m_LastPin    = m_CurrentPin;

    m_PivotAlignment          = editorStyle.PivotAlignment;
    m_PivotSize               = editorStyle.PivotSize;
    m_PivotScale              = editorStyle.PivotScale;
    m_ResolvePinRect          = true;
    m_ResolvePivot            = true;

    ImGui::BeginGroup();
}

void ed::NodeBuilder::EndPin()
{
    assert(nullptr != m_CurrentPin);

    ImGui::EndGroup();

    if (m_ResolvePinRect)
        m_CurrentPin->m_Bounds = ImGui_GetItemRect();

    if (m_ResolvePivot)
    {
        auto& pinRect = m_CurrentPin->m_Bounds;

        if (m_PivotSize.x < 0)
            m_PivotSize.x = static_cast<float>(pinRect.size.w);
        if (m_PivotSize.y < 0)
            m_PivotSize.y = static_cast<float>(pinRect.size.h);

        m_CurrentPin->m_Pivot.location = static_cast<pointf>(to_pointf(pinRect.top_left()) + static_cast<pointf>(pinRect.size).cwise_product(to_pointf(m_PivotAlignment)));
        m_CurrentPin->m_Pivot.size     = static_cast<sizef>((to_pointf(m_PivotSize).cwise_product(to_pointf(m_PivotScale))));
    }

    m_CurrentPin = nullptr;
}

void ed::NodeBuilder::PinRect(const ImVec2& a, const ImVec2& b)
{
    assert(nullptr != m_CurrentPin);

    m_CurrentPin->m_Bounds = rect(to_point(a), to_size(b - a));
    m_ResolvePinRect     = false;
}

void ed::NodeBuilder::PinPivotRect(const ImVec2& a, const ImVec2& b)
{
    assert(nullptr != m_CurrentPin);

    m_CurrentPin->m_Pivot = rectf(to_pointf(a), to_sizef(b - a));
    m_ResolvePivot      = false;
}

void ed::NodeBuilder::PinPivotSize(const ImVec2& size)
{
    assert(nullptr != m_CurrentPin);

    m_PivotSize    = size;
    m_ResolvePivot = true;
}

void ed::NodeBuilder::PinPivotScale(const ImVec2& scale)
{
    assert(nullptr != m_CurrentPin);

    m_PivotScale   = scale;
    m_ResolvePivot = true;
}

void ed::NodeBuilder::PinPivotAlignment(const ImVec2& alignment)
{
    assert(nullptr != m_CurrentPin);

    m_PivotAlignment = alignment;
    m_ResolvePivot   = true;
}

void ed::NodeBuilder::Group(const ImVec2& size)
{
    assert(nullptr != m_CurrentNode);
    assert(nullptr == m_CurrentPin);
    assert(false   == m_IsGroup);

    m_IsGroup = true;

    if (IsGroup(m_CurrentNode))
        ImGui::Dummy(to_imvec(m_CurrentNode->m_GroupBounds.size));
    else
        ImGui::Dummy(size);

    m_GroupBounds = ImGui_GetItemRect();
}

ImDrawList* ed::NodeBuilder::GetUserBackgroundDrawList() const
{
    return GetUserBackgroundDrawList(m_CurrentNode);
}

ImDrawList* ed::NodeBuilder::GetUserBackgroundDrawList(Node* node) const
{
    if (node && node->m_IsLive)
    {
        auto drawList = ImGui::GetWindowDrawList();
        drawList->ChannelsSetCurrent(node->m_Channel + c_NodeUserBackgroundChannel);
        return drawList;
    }
    else
        return nullptr;
}




//------------------------------------------------------------------------------
//
// Node Builder
//
//------------------------------------------------------------------------------
ed::HintBuilder::HintBuilder(EditorContext* editor):
    Editor(editor),
    m_IsActive(false),
    m_CurrentNode(nullptr)
{
}

bool ed::HintBuilder::Begin(int nodeId)
{
    assert(nullptr == m_CurrentNode);

    auto& canvas = Editor->GetCanvas();

    const float c_min_zoom = 0.75f;
    const float c_max_zoom = 0.50f;

    if (canvas.Zoom.y > 0.75f)
        return false;

    auto node = Editor->FindNode(nodeId);
    if (!IsGroup(node))
        return false;

    m_CurrentNode = node;

    Editor->Suspend();

    const auto alpha = std::max(0.0f, std::min(1.0f, (canvas.Zoom.y - c_min_zoom) / (c_max_zoom - c_min_zoom)));

    ImGui::GetWindowDrawList()->ChannelsSetCurrent(c_UserChannel_HintsBackground);
    ImGui::PushClipRect(canvas.WindowScreenPos + ImVec2(1, 1), canvas.WindowScreenPos + canvas.WindowScreenSize - ImVec2(1, 1), false);

    ImGui::GetWindowDrawList()->ChannelsSetCurrent(c_UserChannel_Hints);
    ImGui::PushClipRect(canvas.WindowScreenPos + ImVec2(1, 1), canvas.WindowScreenPos + canvas.WindowScreenSize - ImVec2(1, 1), false);

    ImGui::PushStyleVar(ImGuiStyleVar_AntiAliasFringeScale, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);

    m_IsActive = true;

    return true;
}

void ed::HintBuilder::End()
{
    if (!m_IsActive)
        return;

    ImGui::PopClipRect();
    ImGui::PopClipRect();
    ImGui::PopStyleVar(2);

    Editor->Resume();

    m_IsActive    = false;
    m_CurrentNode = nullptr;
}

ImVec2 ed::HintBuilder::GetGroupMin()
{
    assert(nullptr != m_CurrentNode);

    return Editor->GetCanvas().ToScreen(to_imvec(m_CurrentNode->m_Bounds.top_left()));
}

ImVec2 ed::HintBuilder::GetGroupMax()
{
    assert(nullptr != m_CurrentNode);

    return Editor->GetCanvas().ToScreen(to_imvec(m_CurrentNode->m_Bounds.bottom_right()));
}

ImDrawList* ed::HintBuilder::GetForegroundDrawList()
{
    assert(nullptr != m_CurrentNode);

    auto drawList = ImGui::GetWindowDrawList();

    drawList->ChannelsSetCurrent(c_UserChannel_Hints);

    return drawList;
}

ImDrawList* ed::HintBuilder::GetBackgroundDrawList()
{
    assert(nullptr != m_CurrentNode);

    auto drawList = ImGui::GetWindowDrawList();

    drawList->ChannelsSetCurrent(c_UserChannel_HintsBackground);

    return drawList;
}




//------------------------------------------------------------------------------
//
// Style
//
//------------------------------------------------------------------------------
void ed::Style::PushColor(StyleColor colorIndex, const ImVec4& color)
{
    ColorModifier modifier;
    modifier.Index = colorIndex;
    modifier.Value = Colors[colorIndex];
    m_ColorStack.push_back(modifier);
    Colors[colorIndex] = color;
}

void ed::Style::PopColor(int count)
{
    while (count > 0)
    {
        auto& modifier = m_ColorStack.back();
        Colors[modifier.Index] = modifier.Value;
        m_ColorStack.pop_back();
        --count;
    }
}

void ed::Style::PushVar(StyleVar varIndex, float value)
{
    auto* var = GetVarFloatAddr(varIndex);
    assert(var != nullptr);
    VarModifier modifier;
    modifier.Index = varIndex;
    modifier.Value = ImVec4(*var, 0, 0, 0);
    *var = value;
    m_VarStack.push_back(modifier);
}

void ed::Style::PushVar(StyleVar varIndex, const ImVec2& value)
{
    auto* var = GetVarVec2Addr(varIndex);
    assert(var != nullptr);
    VarModifier modifier;
    modifier.Index = varIndex;
    modifier.Value = ImVec4(var->x, var->y, 0, 0);
    *var = value;
    m_VarStack.push_back(modifier);
}

void ed::Style::PushVar(StyleVar varIndex, const ImVec4& value)
{
    auto* var = GetVarVec4Addr(varIndex);
    assert(var != nullptr);
    VarModifier modifier;
    modifier.Index = varIndex;
    modifier.Value = *var;
    *var = value;
    m_VarStack.push_back(modifier);
}

void ed::Style::PopVar(int count)
{
    while (count > 0)
    {
        auto& modifier = m_VarStack.back();
        if (auto v = GetVarFloatAddr(modifier.Index))
            *v = modifier.Value.x;
        else if (auto v = GetVarVec2Addr(modifier.Index))
            *v = ImVec2(modifier.Value.x, modifier.Value.y);
        else if (auto v = GetVarVec4Addr(modifier.Index))
            *v = modifier.Value;
        m_VarStack.pop_back();
        --count;
    }
}

const char* ed::Style::GetColorName(StyleColor colorIndex) const
{
    switch (colorIndex)
    {
        case StyleColor_Bg: return "Bg";
        case StyleColor_Grid: return "Grid";
        case StyleColor_NodeBg: return "NodeBg";
        case StyleColor_NodeBorder: return "NodeBorder";
        case StyleColor_HovNodeBorder: return "HoveredNodeBorder";
        case StyleColor_SelNodeBorder: return "SelNodeBorder";
        case StyleColor_NodeSelRect: return "NodeSelRect";
        case StyleColor_NodeSelRectBorder: return "NodeSelRectBorder";
        case StyleColor_HovLinkBorder: return "HoveredLinkBorder";
        case StyleColor_SelLinkBorder: return "SelLinkBorder";
        case StyleColor_LinkSelRect: return "LinkSelRect";
        case StyleColor_LinkSelRectBorder: return "LinkSelRectBorder";
        case StyleColor_PinRect: return "PinRect";
        case StyleColor_PinRectBorder: return "PinRectBorder";
        case StyleColor_Flow: return "Flow";
        case StyleColor_FlowMarker: return "FlowMarker";
        case StyleColor_GroupBg: return "GroupBg";
        case StyleColor_GroupBorder: return "GroupBorder";
        case StyleColor_Count: break;
    }

    assert(0);
    return "Unknown";
}

float* ed::Style::GetVarFloatAddr(StyleVar idx)
{
    switch (idx)
    {
        case StyleVar_NodeRounding:             return &NodeRounding;
        case StyleVar_NodeBorderWidth:          return &NodeBorderWidth;
        case StyleVar_HoveredNodeBorderWidth:   return &HoveredNodeBorderWidth;
        case StyleVar_SelectedNodeBorderWidth:  return &SelectedNodeBorderWidth;
        case StyleVar_PinRounding:              return &PinRounding;
        case StyleVar_PinBorderWidth:           return &PinBorderWidth;
        case StyleVar_LinkStrength:             return &LinkStrength;
        case StyleVar_ScrollDuration:           return &ScrollDuration;
        case StyleVar_FlowMarkerDistance:       return &FlowMarkerDistance;
        case StyleVar_FlowSpeed:                return &FlowSpeed;
        case StyleVar_FlowDuration:             return &FlowDuration;
        case StyleVar_PinCorners:               return &PinCorners;
        case StyleVar_PinRadius:                return &PinRadius;
        case StyleVar_PinArrowSize:             return &PinArrowSize;
        case StyleVar_PinArrowWidth:            return &PinArrowWidth;
        case StyleVar_GroupRounding:            return &GroupRounding;
        case StyleVar_GroupBorderWidth:         return &GroupBorderWidth;
        default:                                return nullptr;
    }
}

ImVec2* ed::Style::GetVarVec2Addr(StyleVar idx)
{
    switch (idx)
    {
        case StyleVar_SourceDirection:  return &SourceDirection;
        case StyleVar_TargetDirection:  return &TargetDirection;
        case StyleVar_PivotAlignment:   return &PivotAlignment;
        case StyleVar_PivotSize:        return &PivotSize;
        case StyleVar_PivotScale:       return &PivotScale;
        default:                        return nullptr;
    }
}

ImVec4* ed::Style::GetVarVec4Addr(StyleVar idx)
{
    switch (idx)
    {
        case StyleVar_NodePadding:  return &NodePadding;
        default:                    return nullptr;
    }
}




//------------------------------------------------------------------------------
//
// Config
//
//------------------------------------------------------------------------------
ed::Config::Config(const ax::NodeEditor::Config* config)
{
    if (config)
        *static_cast<ax::NodeEditor::Config*>(this) = *config;
}

std::string ed::Config::Load()
{
    std::string data;

    if (LoadSettings)
    {
        const auto size = LoadSettings(nullptr, UserPointer);
        if (size > 0)
        {
            data.resize(size);
            LoadSettings(const_cast<char*>(data.data()), UserPointer);
        }
    }
    else if (SettingsFile)
    {
        std::ifstream file(SettingsFile);
        if (file)
        {
            file.seekg(0, std::ios_base::end);
            auto size = static_cast<size_t>(file.tellg());
            file.seekg(0, std::ios_base::beg);

            data.reserve(size);

            std::copy(std::istream_iterator<char>(file), std::istream_iterator<char>(), std::back_inserter(data));
        }
    }

    return data;
}

std::string ed::Config::LoadNode(int nodeId)
{
    std::string data;

    if (LoadNodeSettings)
    {
        const auto size = LoadNodeSettings(nodeId, nullptr, UserPointer);
        if (size > 0)
        {
            data.resize(size);
            LoadNodeSettings(nodeId, const_cast<char*>(data.data()), UserPointer);
        }
    }

    return data;
}

void ed::Config::BeginSave()
{
    if (BeginSaveSession)
        BeginSaveSession(UserPointer);
}

bool ed::Config::Save(const std::string& data, SaveReasonFlags flags)
{
    if (SaveSettings)
    {
        return SaveSettings(data.c_str(), data.size(), flags, UserPointer);
    }
    else if (SettingsFile)
    {
        std::ofstream settingsFile(SettingsFile);
        if (settingsFile)
            settingsFile << data;

        return !!settingsFile;
    }

    return false;
}

bool ed::Config::SaveNode(int nodeId, const std::string& data, SaveReasonFlags flags)
{
    if (SaveNodeSettings)
        return SaveNodeSettings(nodeId, data.c_str(), data.size(), flags, UserPointer);

    return false;
}

void ed::Config::EndSave()
{
    if (EndSaveSession)
        EndSaveSession(UserPointer);
}
