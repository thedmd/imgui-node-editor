#include "NodeEditorImpl.h"
#include <cstdlib> // _itoa
#include <string>
#include <fstream>
#include <bitset>


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
    if (node && node->Type == ed::NodeType::Group)
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
    using namespace ax::ImGuiInterop;

    auto acceptPoint = [drawList, offset](const bezier_subdivide_result_t& r)
    {
        drawList->PathLineTo(to_imvec(r.point + ax::pointf(-r.tangent.y, r.tangent.x).normalized() * offset));
    };

    cubic_bezier_subdivide(acceptPoint, to_pointf(p0), to_pointf(p1), to_pointf(p2), to_pointf(p3));
}

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

static void ImDrawList_AddBezierWithArrows(ImDrawList* drawList, const ax::cubic_bezier_t& curve, float thickness,
    float startArrowSize, float startArrowWidth, float endArrowSize, float endArrowWidth,
    bool fill, ImU32 color, float strokeThickness)
{
    using namespace ax;
    using namespace ax::ImGuiInterop;

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
        drawList->ChannelsSetCurrent(Node->Channel + c_NodePinChannel);

        drawList->AddRectFilled(to_imvec(Bounds.top_left()), to_imvec(Bounds.bottom_right()),
            Color, Rounding, Corners);

        if (BorderWidth > 0.0f)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_AntiAliasFringeScale, 1.0f);
            drawList->AddRect(to_imvec(Bounds.top_left()), to_imvec(Bounds.bottom_right()),
                BorderColor, Rounding, Corners, BorderWidth);
            ImGui::PopStyleVar();
        }

        Node->Draw(drawList, flags);
    }
}

ImVec2 ed::Pin::GetClosestPoint(const ImVec2& p) const
{
    return to_imvec(Pivot.get_closest_point(to_pointf(p), true, Radius + ArrowSize));
}

ax::line_f ed::Pin::GetClosestLine(const Pin* pin) const
{
    return Pivot.get_closest_line(pin->Pivot, Radius + ArrowSize, pin->Radius + pin->ArrowSize);
}




//------------------------------------------------------------------------------
//
// Node
//
//------------------------------------------------------------------------------
bool ed::Node::AcceptDrag()
{
    DragStart = Bounds.location;
    return true;
}

void ed::Node::UpdateDrag(const ax::point& offset)
{
    Bounds.location = DragStart + offset;
}

bool ed::Node::EndDrag()
{
    return Bounds.location != DragStart;
}

void ed::Node::Draw(ImDrawList* drawList, DrawFlags flags)
{
    if (flags == Detail::Object::None)
    {
        drawList->ChannelsSetCurrent(Channel + c_NodeBackgroundChannel);

        drawList->AddRectFilled(
            to_imvec(Bounds.top_left()),
            to_imvec(Bounds.bottom_right()),
            Color, Rounding);

        if (IsGroup(this))
        {
            drawList->AddRectFilled(
                to_imvec(GroupBounds.top_left()),
                to_imvec(GroupBounds.bottom_right()),
                GroupColor, GroupRounding);

            if (GroupBorderWidth > 0.0f)
            {
                ImGui::PushStyleVar(ImGuiStyleVar_AntiAliasFringeScale, 1.0f);

                drawList->AddRect(
                    to_imvec(GroupBounds.top_left()),
                    to_imvec(GroupBounds.bottom_right()),
                    GroupBorderColor, GroupRounding, 15, GroupBorderWidth);

                ImGui::PopStyleVar();
            }
        }

        DrawBorder(drawList, BorderColor, BorderWidth);
    }
    else if (flags & Selected)
    {
        const auto  borderColor = Editor->GetColor(StyleColor_SelNodeBorder);
        const auto& editorStyle = Editor->GetStyle();

        drawList->ChannelsSetCurrent(Channel + c_NodeBaseChannel);

        DrawBorder(drawList, borderColor, editorStyle.SelectedNodeBorderWidth);
    }
    else if (!IsGroup(this) && (flags & Hovered))
    {
        const auto  borderColor = Editor->GetColor(StyleColor_HovNodeBorder);
        const auto& editorStyle = Editor->GetStyle();

        drawList->ChannelsSetCurrent(Channel + c_NodeBaseChannel);

        DrawBorder(drawList, borderColor, editorStyle.HoveredNodeBorderWidth);
    }
}

void ed::Node::DrawBorder(ImDrawList* drawList, ImU32 color, float thickness)
{
    if (thickness > 0.0f)
    {
        drawList->AddRect(to_imvec(Bounds.top_left()), to_imvec(Bounds.bottom_right()),
            color, Rounding, 15, thickness);
    }
}

void ed::Node::GetGroupedNodes(std::vector<Node*>& result, bool append)
{
    if (!append)
        result.resize(0);

    if (!IsGroup(this))
        return;

    const auto firstNodeIndex = result.size();
    Editor->FindNodesInRect(static_cast<ax::rectf>(GroupBounds), result, true, false);

    for (auto index = firstNodeIndex, lastNodeIndex = result.size(); index < result.size(); ++index)
        result[index]->GetGroupedNodes(result, true);
}




//------------------------------------------------------------------------------
//
// Link
//
//------------------------------------------------------------------------------
void ed::Link::Draw(ImDrawList* drawList, DrawFlags flags)
{
    using namespace ax::ImGuiInterop;

    if (flags == None)
    {
        drawList->ChannelsSetCurrent(c_LinkChannel_Links);

        Draw(drawList, Color, 0.0f);
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
    using namespace ax::ImGuiInterop;

    if (!IsLive)
        return;

    const auto curve = GetCurve();

    ImDrawList_AddBezierWithArrows(drawList, curve, Thickness + extraThickness,
        StartPin && StartPin->ArrowSize  > 0.0f ? StartPin->ArrowSize  + extraThickness : 0.0f,
        StartPin && StartPin->ArrowWidth > 0.0f ? StartPin->ArrowWidth + extraThickness : 0.0f,
          EndPin &&   EndPin->ArrowSize  > 0.0f ?   EndPin->ArrowSize  + extraThickness : 0.0f,
          EndPin &&   EndPin->ArrowWidth > 0.0f ?   EndPin->ArrowWidth + extraThickness : 0.0f,
        true, color, 1.0f);
}

void ed::Link::UpdateEndpoints()
{
    const auto line = StartPin->GetClosestLine(EndPin);
    Start = to_imvec(line.a);
    End   = to_imvec(line.b);
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

    const auto startStrength = easeLinkStrength(Start, End, StartPin->Strength);
    const auto   endStrength = easeLinkStrength(Start, End,   EndPin->Strength);
    const auto           cp0 = Start + StartPin->Dir * startStrength;
    const auto           cp1 =   End +   EndPin->Dir *   endStrength;

    return ax::cubic_bezier_t { to_pointf(Start), to_pointf(cp0), to_pointf(cp1), to_pointf(End) };
}

bool ed::Link::TestHit(const ImVec2& point, float extraThickness) const
{
    if (!IsLive)
        return false;

    auto bounds = GetBounds();
    if (extraThickness > 0.0f)
        bounds.expand(extraThickness);

    if (!bounds.contains(to_pointf(point)))
        return false;

    const auto bezier = GetCurve();
    const auto result = cubic_bezier_project_point(to_pointf(point), bezier.p0, bezier.p1, bezier.p2, bezier.p3, 50);

    return result.distance <= Thickness + extraThickness;
}

bool ed::Link::TestHit(const ax::rectf& rect, bool allowIntersect) const
{
    if (!IsLive)
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
    if (IsLive)
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

        if (StartPin->ArrowSize)
        {
            const auto start_dir = curve.tangent(0.0f).normalized();
            const auto p0 = curve.p0;
            const auto p1 = curve.p0 - start_dir * StartPin->ArrowSize;
            const auto min = p0.cwise_min(p1);
            const auto max = p0.cwise_max(p1);
            auto arrowBounds = rectf(min, max);
            arrowBounds.w = std::max(arrowBounds.w, 1.0f);
            arrowBounds.h = std::max(arrowBounds.h, 1.0f);
            bounds = make_union(bounds, arrowBounds);
        }

        if (EndPin->ArrowSize)
        {
            const auto end_dir = curve.tangent(0.0f).normalized();
            const auto p0 = curve.p3;
            const auto p1 = curve.p3 + end_dir * EndPin->ArrowSize;
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
    IsFirstFrame(true),
    Style(),
    Nodes(),
    Pins(),
    Links(),
    SelectionId(1),
    LastActiveLink(nullptr),
    MousePosBackup(0, 0),
    MouseClickPosBackup(),
    Canvas(),
    SuspendCount(0),
    NodeBuilder(this),
    HintBuilder(this),
    CurrentAction(nullptr),
    NavigateAction(this),
    SizeAction(this),
    DragAction(this),
    SelectAction(this),
    ContextMenuAction(this),
    CreateItemAction(this),
    DeleteItemsAction(this),
    AnimationControllers{ &FlowAnimationController },
    FlowAnimationController(this),
    IsInitialized(false),
    Settings(),
    Config(config)
{
}

ed::EditorContext::~EditorContext()
{
    if (IsInitialized)
        SaveSettings();

    for (auto link  : Links)  delete link;
    for (auto pin   : Pins)   delete pin;
    for (auto node  : Nodes)  delete node;
}

void ed::EditorContext::Begin(const char* id, const ImVec2& size)
{
    if (!IsInitialized)
    {
        LoadSettings();
        IsInitialized = true;
    }

    //ImGui::LogToClipboard();
    //Log("---- begin ----");

    for (auto node  : Nodes)   node->IsLive = false;
    for (auto pin   : Pins)     pin->IsLive = false;
    for (auto link  : Links)   link->IsLive = false;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_ChildWindowBg, ImColor(0, 0, 0, 0));
    ImGui::BeginChild(id, size, false,
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse);

    //
    if (CurrentAction && CurrentAction->IsDragging() && NavigateAction.MoveOverEdge())
    {
        auto& io = ImGui::GetIO();
        auto offset = NavigateAction.GetMoveOffset();
        for (int i = 0; i < 5; ++i)
            io.MouseClickedPos[i] = io.MouseClickedPos[i] - offset;
    }
    else
        NavigateAction.StopMoveOverEdge();

    // Setup canvas
    NavigateAction.SetWindow(ImGui::GetWindowPos(), ImGui::GetWindowSize());

    Canvas = NavigateAction.GetCanvas();

    ImGui::PushStyleVar(ImGuiStyleVar_AntiAliasFringeScale, std::min(std::max(Canvas.InvZoom.x, Canvas.InvZoom.y), 1.0f));

    // Save mouse positions
    auto& io = ImGui::GetIO();
    MousePosBackup = io.MousePos;
    for (int i = 0; i < 5; ++i)
        MouseClickPosBackup[i] = io.MouseClickedPos[i];

    // #debug
    //auto drawList2 = ImGui::GetWindowDrawList();
    //drawList2->PushClipRectFullScreen();
    //drawList2->AddRect(Canvas.FromClient(ImVec2(0, 0)), Canvas.FromClient(ImVec2(0, 0)) + Canvas.ClientSize, IM_COL32(255, 0, 255, 255), 0, 15, 4.0f);
    //drawList2->PopClipRect();

    auto clipMin = Canvas.FromScreen(Canvas.WindowScreenPos);
    auto clipMax = Canvas.FromScreen(Canvas.WindowScreenPos) + Canvas.ClientSize;

    // #debug #clip
    //ImGui::Text("CLIP = { x=%g y=%g w=%g h=%g r=%g b=%g }",
    //    clipMin.x, clipMin.y, clipMax.x - clipMin.x, clipMax.y - clipMin.y, clipMax.x, clipMax.y);

    ImGui::PushClipRect(clipMin, clipMax, false);

    CaptureMouse();

    // Reserve channels for background and links
    auto drawList = ImGui::GetWindowDrawList();
    ImDrawList_ChannelsGrow(drawList, c_NodeStartChannel);

    if (HasSelectionChanged())
        ++SelectionId;

    LastSelectedObjects = SelectedObjects;
}

void ed::EditorContext::End()
{
    auto& io          = ImGui::GetIO();
    auto  control     = BuildControl(CurrentAction && CurrentAction->IsDragging()  /*NavigateAction.IsMovingOverEdge()*/);
    auto  drawList    = ImGui::GetWindowDrawList();
    auto& editorStyle = GetStyle();

    const bool isSelecting = CurrentAction && CurrentAction->AsSelect() != nullptr;
    const bool isDragging  = CurrentAction && CurrentAction->AsDrag()   != nullptr;
    const bool isSizing    = CurrentAction && CurrentAction->AsSize()   != nullptr;

    // Draw nodes
    for (auto node : Nodes)
        if (node->IsLive && node->IsVisible())
            node->Draw(drawList);

    // Draw links
    for (auto link : Links)
        if (link->IsLive && link->IsVisible())
            link->Draw(drawList);

    // Highlight selected objects
    {
        auto selectedObjects = &SelectedObjects;
        if (auto selectAction = CurrentAction ? CurrentAction->AsSelect() : nullptr)
            selectedObjects = &selectAction->CandidateObjects;

        for (auto selectedObject : *selectedObjects)
            if (selectedObject->IsVisible())
                selectedObject->Draw(drawList, Object::Selected);
    }

    if (!isSelecting)
    {
        auto hoveredObject = isDragging || isSizing ? control.ActiveObject : control.HotObject;
        if (hoveredObject && !IsSelected(hoveredObject) && hoveredObject->IsVisible())
            hoveredObject->Draw(drawList, Object::Hovered);
    }

    // Draw animations
    for (auto controller : AnimationControllers)
        controller->Draw(drawList);

    if (CurrentAction && !CurrentAction->Process(control))
        CurrentAction = nullptr;

    if (NavigateAction.IsActive)
        NavigateAction.Process(control);
    else
        NavigateAction.Accept(control);

    if (nullptr == CurrentAction)
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

        if (accept(ContextMenuAction))
            CurrentAction = &ContextMenuAction;
        else if (accept(SizeAction))
            CurrentAction = &SizeAction;
        else if (accept(DragAction))
            CurrentAction = &DragAction;
        else if (accept(SelectAction))
            CurrentAction = &SelectAction;
        else if (accept(CreateItemAction))
            CurrentAction = &CreateItemAction;
        else if (accept(DeleteItemsAction))
            CurrentAction = &DeleteItemsAction;

        if (possibleAction)
            ImGui::SetMouseCursor(possibleAction->GetCursor());

        if (CurrentAction && possibleAction)
            possibleAction->Reject();
    }

    if (CurrentAction)
        ImGui::SetMouseCursor(CurrentAction->GetCursor());

    // Draw selection rectangle
    SelectAction.Draw(drawList);

    bool sortGroups = false;
    if (control.ActiveNode)
    {
        if (!IsGroup(control.ActiveNode))
        {
            // Bring active node to front
            auto activeNodeIt = std::find(Nodes.begin(), Nodes.end(), control.ActiveNode);
            std::rotate(activeNodeIt, activeNodeIt + 1, Nodes.end());
        }
        else if (!isDragging && CurrentAction && CurrentAction->AsDrag())
        {
            // Bring content of dragged group to front
            std::vector<Node*> nodes;
            control.ActiveNode->GetGroupedNodes(nodes);

            std::stable_partition(Nodes.begin(), Nodes.end(), [&nodes](Node* node)
            {
                return std::find(nodes.begin(), nodes.end(), node) == nodes.end();
            });

            sortGroups = true;
        }
    }

    // Sort nodes if bounds of node changed
    if (sortGroups || ((Settings.DirtyReason & (SaveReasonFlags::Position | SaveReasonFlags::Size)) != SaveReasonFlags::None))
    {
        // Bring all groups before regular nodes
        auto groupsItEnd = std::stable_partition(Nodes.begin(), Nodes.end(), IsGroup);

        // Sort groups by area
        std::sort(Nodes.begin(), groupsItEnd, [this](Node* lhs, Node* rhs)
        {
            const auto& lhsSize = lhs == SizeAction.SizedNode ? SizeAction.GetStartGroupBounds().size : lhs->GroupBounds.size;
            const auto& rhsSize = rhs == SizeAction.SizedNode ? SizeAction.GetStartGroupBounds().size : rhs->GroupBounds.size;

            const auto lhsArea = lhsSize.w * lhsSize.h;
            const auto rhsArea = rhsSize.w * rhsSize.h;

            return lhsArea > rhsArea;
        });
    }

    // Every node has few channels assigned. Grow channel list
    // to hold twice as much of channels and place them in
    // node drawing order.
    {
        // Reserve two additional channels for sorted list of channels
        auto nodeChannelCount = drawList->_ChannelsCount;
        ImDrawList_ChannelsGrow(drawList, (drawList->_ChannelsCount - 1) * 2 + 1 + c_LinkChannelCount);

        int targetChannel = nodeChannelCount;

        auto copyNode = [&targetChannel, drawList](Node* node)
        {
            if (!node->IsLive)
                return;

            for (int i = 0; i < c_ChannelsPerNode; ++i)
                ImDrawList_SwapChannels(drawList, node->Channel + i, targetChannel + i);

            node->Channel = targetChannel;
            targetChannel += c_ChannelsPerNode;
        };

        auto groupsItEnd = std::find_if(Nodes.begin(), Nodes.end(), [](Node* node) { return !IsGroup(node); });

        // Copy group nodes
        std::for_each(Nodes.begin(), groupsItEnd, copyNode);

        // Copy links
        for (int i = 0; i < c_LinkChannelCount; ++i, ++targetChannel)
            ImDrawList_SwapChannels(drawList, c_LinkStartChannel + i, targetChannel);

        // Copy normal nodes
        std::for_each(groupsItEnd, Nodes.end(), copyNode);
    }

    ImGui::PopClipRect();

    // Draw grid
    {
        drawList->ChannelsSetCurrent(c_UserChannel_Grid);

        ImGui::PushClipRect(Canvas.WindowScreenPos + ImVec2(1, 1), Canvas.WindowScreenPos + Canvas.WindowScreenSize - ImVec2(1, 1), false);

        ImVec2 offset    = Canvas.ClientOrigin;
        ImU32 GRID_COLOR = GetColor(StyleColor_Grid);
        float GRID_SX    = 32.0f * Canvas.Zoom.x;
        float GRID_SY    = 32.0f * Canvas.Zoom.y;
        ImVec2 win_pos   = Canvas.WindowScreenPos;
        ImVec2 canvas_sz = Canvas.WindowScreenSize;

        drawList->AddRectFilled(Canvas.WindowScreenPos, Canvas.WindowScreenPos + Canvas.WindowScreenSize, GetColor(StyleColor_Bg));

        for (float x = fmodf(offset.x, GRID_SX); x < Canvas.WindowScreenSize.x; x += GRID_SX)
            drawList->AddLine(ImVec2(x, 0.0f) + Canvas.WindowScreenPos, ImVec2(x, Canvas.WindowScreenSize.y) + Canvas.WindowScreenPos, GRID_COLOR);
        for (float y = fmodf(offset.y, GRID_SY); y < Canvas.WindowScreenSize.y; y += GRID_SY)
            drawList->AddLine(ImVec2(0.0f, y) + Canvas.WindowScreenPos, ImVec2(Canvas.WindowScreenSize.x, y) + Canvas.WindowScreenPos, GRID_COLOR);

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
        auto postOffset = Canvas.WindowScreenPos + Canvas.ClientOrigin;
        auto scale      = Canvas.Zoom;

        ImDrawList_TransformChannels(drawList,                        0,                            1, preOffset, scale, postOffset);
        ImDrawList_TransformChannels(drawList, c_BackgroundChannelStart, drawList->_ChannelsCount - 1, preOffset, scale, postOffset);

        auto clipTranslation = Canvas.WindowScreenPos - Canvas.FromScreen(Canvas.WindowScreenPos);
        ImGui::PushClipRect(Canvas.WindowScreenPos + ImVec2(1, 1), Canvas.WindowScreenPos + Canvas.WindowScreenSize - ImVec2(1, 1), false);
        ImDrawList_TranslateAndClampClipRects(drawList,                        0,                            1, clipTranslation);
        ImDrawList_TranslateAndClampClipRects(drawList, c_BackgroundChannelStart, drawList->_ChannelsCount - 1, clipTranslation);
        ImGui::PopClipRect();

        // #debug: Static grid in local space
        //for (float x = 0; x < Canvas.WindowScreenSize.x; x += 100)
        //    drawList->AddLine(ImVec2(x, 0.0f) + Canvas.WindowScreenPos, ImVec2(x, Canvas.WindowScreenSize.y) + Canvas.WindowScreenPos, IM_COL32(255, 0, 0, 128));
        //for (float y = 0; y < Canvas.WindowScreenSize.y; y += 100)
        //    drawList->AddLine(ImVec2(0.0f, y) + Canvas.WindowScreenPos, ImVec2(Canvas.WindowScreenSize.x, y) + Canvas.WindowScreenPos, IM_COL32(255, 0, 0, 128));
    }

    UpdateAnimations();

    drawList->ChannelsMerge();

    // Draw border
    {
        auto& style = ImGui::GetStyle();
        auto borderShadoColor = style.Colors[ImGuiCol_BorderShadow];
        auto borderColor      = style.Colors[ImGuiCol_Border];
        drawList->AddRect(Canvas.WindowScreenPos + ImVec2(1, 1), Canvas.WindowScreenPos + Canvas.WindowScreenSize + ImVec2(1, 1), ImColor(borderShadoColor), style.WindowRounding);
        drawList->AddRect(Canvas.WindowScreenPos,                Canvas.WindowScreenPos + Canvas.WindowScreenSize,                ImColor(borderColor),      style.WindowRounding);
    }

    //ShowMetrics(control);

    // fringe scale
    ImGui::PopStyleVar();

    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    ReleaseMouse();

    if (!CurrentAction && IsFirstFrame && !Settings.Selection.empty())
    {
        ClearSelection();
        for (int id : Settings.Selection)
            if (auto object = FindObject(id))
                SelectObject(object);
    }

    if (HasSelectionChanged())
        MakeDirty(SaveReasonFlags::Selection);

    if (Settings.IsDirty && !CurrentAction)
        SaveSettings();

    IsFirstFrame = false;
}

bool ed::EditorContext::DoLink(int id, int startPinId, int endPinId, ImU32 color, float thickness)
{
    auto& editorStyle = GetStyle();

    auto startPin = FindPin(startPinId);
    auto endPin   = FindPin(endPinId);

    if (!startPin || !startPin->IsLive || !endPin || !endPin->IsLive)
        return false;

    auto link           = GetLink(id);
    link->StartPin      = startPin;
    link->EndPin        = endPin;
    link->Color         = color;
    link->Thickness     = thickness;
    link->IsLive        = true;

    link->UpdateEndpoints();

    return true;
}

void ed::EditorContext::SetNodePosition(int nodeId, const ImVec2& position)
{
    auto node = FindNode(nodeId);
    if (!node)
    {
        node = CreateNode(nodeId);
        node->IsLive = false;
    }

    auto newPosition = to_point(position);
    if (node->Bounds.location != newPosition)
    {
        node->Bounds.location = to_point(position);
        MakeDirty(NodeEditor::SaveReasonFlags::Position, node);
    }
}

ImVec2 ed::EditorContext::GetNodePosition(int nodeId)
{
    auto node = FindNode(nodeId);
    if (!node)
        return ImVec2(FLT_MAX, FLT_MAX);

    return to_imvec(node->Bounds.location);
}

ImVec2 ed::EditorContext::GetNodeSize(int nodeId)
{
    auto node = FindNode(nodeId);
    if (!node)
        return ImVec2(0, 0);

    return to_imvec(node->Bounds.size);
}

void ed::EditorContext::MarkNodeToRestoreState(Node* node)
{
    node->RestoreState = true;
}

void ed::EditorContext::RestoreNodeState(Node* node)
{
    auto settings = Settings.FindNode(node->ID);
    if (!settings)
        return;

    // Load state from config (if possible)
    if (!NodeSettings::Parse(Config.LoadNode(node->ID), *settings))
        return;

    node->Bounds.location  = to_point(settings->Location);
    node->GroupBounds.size = to_size(settings->GroupSize);
}

void ed::EditorContext::ClearSelection()
{
    SelectedObjects.clear();
}

void ed::EditorContext::SelectObject(Object* object)
{
    SelectedObjects.push_back(object);
}

void ed::EditorContext::DeselectObject(Object* object)
{
    auto objectIt = std::find(SelectedObjects.begin(), SelectedObjects.end(), object);
    if (objectIt != SelectedObjects.end())
        SelectedObjects.erase(objectIt);
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
    return std::find(SelectedObjects.begin(), SelectedObjects.end(), object) != SelectedObjects.end();
}

const ed::vector<ed::Object*>& ed::EditorContext::GetSelectedObjects()
{
    return SelectedObjects;
}

bool ed::EditorContext::IsAnyNodeSelected()
{
    for (auto object : SelectedObjects)
        if (object->AsNode())
            return true;

    return false;
}

bool ed::EditorContext::IsAnyLinkSelected()
{
    for (auto object : SelectedObjects)
        if (object->AsLink())
            return true;

    return false;
}

bool ed::EditorContext::HasSelectionChanged()
{
    return LastSelectedObjects != SelectedObjects;
}

ed::Node* ed::EditorContext::FindNodeAt(const ImVec2& p)
{
    for (auto node : Nodes)
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

    for (auto node : Nodes)
        if (node->TestHit(r, includeIntersecting))
            result.push_back(node);
}

void ed::EditorContext::FindLinksInRect(const ax::rectf& r, vector<Link*>& result, bool append)
{
    using namespace ImGuiInterop;

    if (!append)
        result.resize(0);

    if (r.is_empty())
        return;

    for (auto link : Links)
        if (link->TestHit(r))
            result.push_back(link);
}

void ed::EditorContext::FindLinksForNode(int nodeId, vector<Link*>& result, bool add)
{
    if (!add)
        result.clear();

    for (auto link : Links)
    {
        if (!link->IsLive)
            continue;

        if (link->StartPin->Node->ID == nodeId || link->EndPin->Node->ID == nodeId)
            result.push_back(link);
    }
}

void ed::EditorContext::NotifyLinkDeleted(Link* link)
{
    if (LastActiveLink == link)
        LastActiveLink = nullptr;
}

void ed::EditorContext::Suspend()
{
    //assert(!IsSuspended);
    if (0 == SuspendCount++)
        ReleaseMouse();
}

void ed::EditorContext::Resume()
{
    assert(SuspendCount > 0);

    if (0 == --SuspendCount)
        CaptureMouse();
}

bool ed::EditorContext::IsSuspended()
{
    return SuspendCount > 0;
}

ed::Pin* ed::EditorContext::CreatePin(int id, PinKind kind)
{
    assert(nullptr == FindObject(id));
    auto pin = new Pin(this, id, kind);
    Pins.push_back(pin);
    return pin;
}

ed::Node* ed::EditorContext::CreateNode(int id)
{
    assert(nullptr == FindObject(id));
    auto node = new Node(this, id);
    Nodes.push_back(node);

    auto settings = Settings.FindNode(id);
    if (!settings)
        settings = Settings.AddNode(id);

    if (!settings->WasUsed)
    {
        settings->WasUsed = true;
        RestoreNodeState(node);
    }

    node->Bounds.location  = to_point(settings->Location);

    if (settings->GroupSize.x > 0 || settings->GroupSize.y > 0)
    {
        node->Type             = NodeType::Group;
        node->GroupBounds.size = to_size(settings->GroupSize);
    }

    node->IsLive = false;

    return node;
}

ed::Link* ed::EditorContext::CreateLink(int id)
{
    assert(nullptr == FindObject(id));
    auto link = new Link(this, id);
    Links.push_back(link);

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
static inline auto FindItemIn(C& container, int id)
{
    for (auto item : container)
        if (item->ID == id)
            return item;

    return (typename C::value_type)nullptr;
}

ed::Node* ed::EditorContext::FindNode(int id)
{
    return FindItemIn(Nodes, id);
}

ed::Pin* ed::EditorContext::FindPin(int id)
{
    return FindItemIn(Pins, id);
}

ed::Link* ed::EditorContext::FindLink(int id)
{
    return FindItemIn(Links, id);
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
        pin->Kind = kind;
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
    if (!ed::Settings::Parse(Config.Load(), Settings))
        Settings = ed::Settings();

    NavigateAction.Scroll = Settings.ViewScroll;
    NavigateAction.Zoom   = Settings.ViewZoom;
}

void ed::EditorContext::SaveSettings()
{
    for (auto& node : Nodes)
    {
        auto settings = Settings.FindNode(node->ID);
        settings->Location = to_imvec(node->Bounds.location);
        settings->Size     = to_imvec(node->Bounds.size);
        if (IsGroup(node))
            settings->GroupSize = to_imvec(node->GroupBounds.size);

        if (!node->RestoreState && settings->IsDirty && Config.SaveNodeSettings)
        {
            if (Config.SaveNode(node->ID, json::value(settings->Serialize()).serialize(), settings->DirtyReason))
                settings->ClearDirty();
        }
    }

    Settings.Selection.resize(0);
    for (auto& object : SelectedObjects)
        Settings.Selection.push_back(object->ID);

    Settings.ViewScroll = NavigateAction.Scroll;
    Settings.ViewZoom   = NavigateAction.Zoom;

    if (Config.Save(Settings.Serialize(), Settings.DirtyReason))
        Settings.ClearDirty();
}

void ed::EditorContext::MakeDirty(SaveReasonFlags reason)
{
    Settings.MakeDirty(reason);
}

void ed::EditorContext::MakeDirty(SaveReasonFlags reason, Node* node)
{
    Settings.MakeDirty(reason, node);
}

ed::Link* ed::EditorContext::FindLinkAt(const ax::point& p)
{
    for (auto& link : Links)
        if (link->TestHit(to_imvec(p), c_LinkSelectThickness))
            return link;

    return nullptr;
}

ImU32 ed::EditorContext::GetColor(StyleColor colorIndex) const
{
    return ImColor(Style.Colors[colorIndex]);
}

ImU32 ed::EditorContext::GetColor(StyleColor colorIndex, float alpha) const
{
    auto color = Style.Colors[colorIndex];
    return ImColor(color.x, color.y, color.z, color.w * alpha);
}

void ed::EditorContext::RegisterAnimation(Animation* animation)
{
    LiveAnimations.push_back(animation);
}

void ed::EditorContext::UnregisterAnimation(Animation* animation)
{
    auto it = std::find(LiveAnimations.begin(), LiveAnimations.end(), animation);
    if (it != LiveAnimations.end())
        LiveAnimations.erase(it);
}

void ed::EditorContext::UpdateAnimations()
{
    LastLiveAnimations = LiveAnimations;

    for (auto animation : LastLiveAnimations)
    {
        const bool isLive = (std::find(LiveAnimations.begin(), LiveAnimations.end(), animation) != LiveAnimations.end());

        if (isLive)
            animation->Update();
    }
}

void ed::EditorContext::Flow(Link* link)
{
    FlowAnimationController.Flow(link);
}

void ed::EditorContext::SetUserContext(bool globalSpace)
{
    const auto mousePos = ImGui::GetMousePos();

    // Move drawing cursor to mouse location and prepare layer for
    // content added by user.
    if (globalSpace)
        ImGui::SetCursorScreenPos(Canvas.ToScreen(mousePos));
    else
        ImGui::SetCursorScreenPos(ImVec2(floorf(mousePos.x), floorf(mousePos.y)));

    auto drawList = ImGui::GetWindowDrawList();
    drawList->ChannelsSetCurrent(c_UserChannel_Content);

    // #debug
    //drawList->AddCircleFilled(ImGui::GetMousePos(), 4, IM_COL32(0, 255, 0, 255));
}

ed::Control ed::EditorContext::BuildControl(bool allowOffscreen)
{
    if (!allowOffscreen && !ImGui::IsWindowHovered())
        return Control(nullptr, nullptr, nullptr, false, false, false);

    const auto mousePos = to_point(ImGui::GetMousePos());

    // Expand clip rectangle to always contain cursor
    auto editorRect = rect(to_point(Canvas.FromClient(ImVec2(0, 0))), to_size(Canvas.ClientSize));
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

    Object* hotObject     = nullptr;
    Object* activeObject  = nullptr;
    Object* clickedObject = nullptr;

    // Emits invisible button and returns true if it is clicked.
    auto emitInteractiveArea = [this](int id, const rect& rect)
    {
        char idString[33]; // itoa can output 33 bytes maximum
        _itoa(id, idString, 16);
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
    auto checkInteractionsInArea = [&emitInteractiveArea, &hotObject, &activeObject, &clickedObject](int id, const rect& rect, Object* object)
    {
        if (emitInteractiveArea(id, rect))
            clickedObject = object;

        if (!hotObject && ImGui::IsItemHoveredRect())
            hotObject = object;

        if (ImGui::IsItemActive())
            activeObject = object;
    };

    // Process live nodes and pins.
    for (auto nodeIt = Nodes.rbegin(), nodeItEnd = Nodes.rend(); nodeIt != nodeItEnd; ++nodeIt)
    {
        auto node = *nodeIt;

        if (!node->IsLive) continue;

        // Check for interactions with live pins in node before
        // processing node itself. Pins does not overlap each other
        // and all are within node bounds.
        for (auto pin = node->LastPin; pin; pin = pin->PreviousPin)
        {
            if (!pin->IsLive) continue;

            checkInteractionsInArea(pin->ID, pin->Bounds, pin);
        }

        // Check for interactions with node.
        if (node->Type == NodeType::Group)
        {
            // Node with a hole
            ImGui::PushID(node->ID);

            const auto top    = node->GroupBounds.top()  - node->Bounds.top();
            const auto left   = node->GroupBounds.left() - node->Bounds.left();
            const auto bottom = node->Bounds.bottom()    - node->GroupBounds.bottom();
            const auto right  = node->Bounds.right()     - node->GroupBounds.right();

            checkInteractionsInArea(1, rect(node->Bounds.left(),  node->Bounds.top(),             node->Bounds.w, top),    node);
            checkInteractionsInArea(2, rect(node->Bounds.left(),  node->Bounds.bottom() - bottom, node->Bounds.w, bottom), node);
            checkInteractionsInArea(3, rect(node->Bounds.left(),  node->Bounds.top() + top,       left, node->Bounds.h - top - bottom), node);
            checkInteractionsInArea(4, rect(node->Bounds.right() - right, node->Bounds.top() + top, right, node->Bounds.h - top - bottom), node);

            ImGui::PopID();
        }
        else
            checkInteractionsInArea(node->ID, node->Bounds, node);
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
    auto backgroundClicked  = emitInteractiveArea(0, editorRect);
    auto isBackgroundActive = ImGui::IsItemActive();
    auto isBackgroundHot    = !hotObject;
    auto isDragging         = ImGui::IsMouseDragging(0, 1) || ImGui::IsMouseDragging(1, 1) || ImGui::IsMouseDragging(2, 1);

    if (isMouseOffscreen)
        ImGui::PopClipRect();

    // Process link input using background interactions.
    auto hotLink = hotObject ? hotObject->AsLink() : nullptr;

    // ImGui take care of tracking active items. With link
    // we must do this ourself.
    if (!isDragging && isBackgroundActive && hotLink && !LastActiveLink)
        LastActiveLink = hotLink;
    if (isBackgroundActive && LastActiveLink)
    {
        activeObject       = LastActiveLink;
        isBackgroundActive = false;
    }
    else if (!isBackgroundActive && LastActiveLink)
        LastActiveLink = nullptr;

    // Steal click from backgrounds if link is hovered.
    if (!isDragging && backgroundClicked && hotLink)
    {
        clickedObject     = hotLink;
        backgroundClicked = false;
    }

    return Control(hotObject, activeObject, clickedObject,
        isBackgroundHot, isBackgroundActive, backgroundClicked);
}

void ed::EditorContext::ShowMetrics(const Control& control)
{
    auto& io = ImGui::GetIO();

    ImGui::SetCursorScreenPos(Canvas.WindowScreenPos);
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
            return "<???>";
    };

    auto getActiveObjectName = [&control, &getObjectName]()
    {
        if (control.ActiveObject)
            return getObjectName(control.ActiveObject);
        else if (control.BackgroundActive)
            return "Background";
        else
            return "<???>";
    };

    auto liveNodeCount  = (int)std::count_if(Nodes.begin(),  Nodes.end(),  [](Node*  node)  { return  node->IsLive; });
    auto livePinCount   = (int)std::count_if(Pins.begin(),   Pins.end(),   [](Pin*   pin)   { return   pin->IsLive; });
    auto liveLinkCount  = (int)std::count_if(Links.begin(),  Links.end(),  [](Link*  link)  { return  link->IsLive; });

    ImGui::SetCursorPos(ImVec2(10, 10));
    ImGui::BeginGroup();
    ImGui::Text("Is Editor Active: %s", ImGui::IsWindowHovered() ? "true" : "false");
    ImGui::Text("Window Position: { x=%g y=%g }", Canvas.WindowScreenPos.x, Canvas.WindowScreenPos.y);
    ImGui::Text("Window Size: { w=%g h=%g }", Canvas.WindowScreenSize.x, Canvas.WindowScreenSize.y);
    ImGui::Text("Canvas Size: { w=%g h=%g }", Canvas.ClientSize.x, Canvas.ClientSize.y);
    ImGui::Text("Mouse: { x=%.0f y=%.0f } global: { x=%g y=%g }", io.MousePos.x, io.MousePos.y, MousePosBackup.x, MousePosBackup.y);
    ImGui::Text("Live Nodes: %d", liveNodeCount);
    ImGui::Text("Live Pins: %d", livePinCount);
    ImGui::Text("Live Links: %d", liveLinkCount);
    ImGui::Text("Hot Object: %s (%d)", getHotObjectName(), control.HotObject ? control.HotObject->ID : 0);
    if (auto node = control.HotObject ? control.HotObject->AsNode() : nullptr)
    {
        ImGui::SameLine();
        ImGui::Text("{ x=%d y=%d w=%d h=%d }", node->Bounds.x, node->Bounds.y, node->Bounds.w, node->Bounds.h);
    }
    ImGui::Text("Active Object: %s (%d)", getActiveObjectName(), control.ActiveObject ? control.ActiveObject->ID : 0);
    if (auto node = control.ActiveObject ? control.ActiveObject->AsNode() : nullptr)
    {
        ImGui::SameLine();
        ImGui::Text("{ x=%d y=%d w=%d h=%d }", node->Bounds.x, node->Bounds.y, node->Bounds.w, node->Bounds.h);
    }
    ImGui::Text("Action: %s", CurrentAction ? CurrentAction->GetName() : "<none>");
    ImGui::Text("Action Is Dragging: %s", CurrentAction && CurrentAction->IsDragging() ? "Yes" : "No");
    NavigateAction.ShowMetrics();
    SizeAction.ShowMetrics();
    DragAction.ShowMetrics();
    SelectAction.ShowMetrics();
    ContextMenuAction.ShowMetrics();
    CreateItemAction.ShowMetrics();
    DeleteItemsAction.ShowMetrics();
    ImGui::EndGroup();
}

void ed::EditorContext::CaptureMouse()
{
    auto& io = ImGui::GetIO();

    io.MousePos = Canvas.FromScreen(MousePosBackup);

    for (int i = 0; i < 5; ++i)
        io.MouseClickedPos[i] = Canvas.FromScreen(MouseClickPosBackup[i]);
}

void ed::EditorContext::ReleaseMouse()
{
    auto& io = ImGui::GetIO();

    io.MousePos = MousePosBackup;

    for (int i = 0; i < 5; ++i)
        io.MouseClickedPos[i] = MouseClickPosBackup[i];
}




//------------------------------------------------------------------------------
//
// Node Settings
//
//------------------------------------------------------------------------------
void ed::NodeSettings::ClearDirty()
{
    IsDirty     = false;
    DirtyReason = SaveReasonFlags::None;
}

void ed::NodeSettings::MakeDirty(SaveReasonFlags reason)
{
    IsDirty     = true;
    DirtyReason = DirtyReason | reason;
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
    nodeData["location"] = json::value(serializeVector(Location));
    nodeData["size"]     = json::value(serializeVector(Size));

    if (GroupSize.x > 0 || GroupSize.y > 0)
        nodeData["group_size"] = json::value(serializeVector(GroupSize));

    return std::move(nodeData);
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

    if (!tryParseVector(data.get("location"), result.Location))
        return false;

    if (data.contains("group_size") && !tryParseVector(data.get("group_size"), result.GroupSize))
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
    Nodes.push_back(NodeSettings(id));
    return &Nodes.back();
}

ed::NodeSettings* ed::Settings::FindNode(int id)
{
    for (auto& settings : Nodes)
        if (settings.ID == id)
            return &settings;

    return nullptr;
}

void ed::Settings::ClearDirty(Node* node)
{
    if (node)
    {
        auto settings = FindNode(node->ID);
        assert(settings);
        settings->ClearDirty();
    }
    else
    {
        IsDirty     = false;
        DirtyReason = SaveReasonFlags::None;

        for (auto& node : Nodes)
            node.ClearDirty();
    }
}

void ed::Settings::MakeDirty(SaveReasonFlags reason, Node* node)
{
    IsDirty     = true;
    DirtyReason = DirtyReason | reason;

    if (node)
    {
        auto settings = FindNode(node->ID);
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
    for (auto& node : Nodes)
    {
        if (node.WasUsed)
            nodes[std::to_string(node.ID)] = json::value(node.Serialize());
    }

    json::array selection;
    for (auto& id : Selection)
        selection.push_back(json::value(static_cast<double>(id)));

    json::object view;
    view["scroll"] = json::value(serializeVector(ViewScroll));
    view["zoom"]   = json::value((double)ViewZoom);

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

    auto& settingsObject = settingsValue.get<json::object>();

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

        result.Selection.reserve(selectionArray.size());
        result.Selection.resize(0);
        for (auto& selection : selectionArray)
        {
            if (selection.is<double>())
                result.Selection.push_back(static_cast<int>(selection.get<double>()));
        }
    }

    auto& viewValue = settingsValue.get("view");
    if (viewValue.is<json::object>())
    {
        auto& viewScrollValue = viewValue.get("scroll");
        auto& viewZoomValue = viewValue.get("zoom");

        if (!tryParseVector(viewValue.get("scroll"), result.ViewScroll))
            result.ViewScroll = ImVec2(0, 0);

        result.ViewZoom = viewZoomValue.is<double>() ? static_cast<float>(viewZoomValue.get<double>()) : 1.0f;
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
    ClientSize(size),
    ClientOrigin(origin),
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

ax::rectf ed::Canvas::GetVisibleBounds()
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
    State(Stopped),
    Time(0.0f),
    Duration(0.0f)
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

    State = Playing;
    if (duration < 0)
        duration = 0.0f;

    Time     = 0.0f;
    Duration = duration;

    OnPlay();

    Editor->RegisterAnimation(this);

    if (duration == 0.0f)
        Stop();
}

void ed::Animation::Stop()
{
    if (!IsPlaying())
        return;

    State = Stopped;

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

    Time += std::max(0.0f, ImGui::GetIO().DeltaTime);
    if (Time < Duration)
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

    Start      = Action.Scroll;
    StartZoom  = Action.Zoom;
    Target     = target;
    TargetZoom = targetZoom;

    Play(duration);
}

void ed::NavigateAnimation::OnUpdate(float progress)
{
    Action.Scroll = easing::ease_out_quad(Start, Target - Start, progress);
    Action.Zoom   = easing::ease_out_quad(StartZoom, TargetZoom - StartZoom, progress);
}

void ed::NavigateAnimation::OnStop()
{
    Editor->MakeDirty(SaveReasonFlags::Navigation);
}

void ed::NavigateAnimation::OnFinish()
{
    Action.Scroll = Target;
    Action.Zoom   = TargetZoom;

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
    Link(nullptr),
    Offset(0.0f),
    PathLength(0.0f)
{
}

void ed::FlowAnimation::Flow(ed::Link* link, float markerDistance, float speed, float duration)
{
    Stop();

    if (Link != link)
    {
        Offset = 0.0f;
        ClearPath();
    }

    if (MarkerDistance != markerDistance)
        ClearPath();

    MarkerDistance = markerDistance;
    Speed          = speed;
    Link           = link;

    Play(duration);
}

void ed::FlowAnimation::Draw(ImDrawList* drawList)
{
    if (!IsPlaying() || !IsLinkValid() || !Link->IsVisible())
        return;

    if (!IsPathValid())
        UpdatePath();

    Offset = fmodf(Offset, MarkerDistance);

    const auto progress    = GetProgress();

    const auto flowAlpha = 1.0f - progress * progress;
    const auto flowColor = Editor->GetColor(StyleColor_Flow, flowAlpha);
    const auto flowPath  = Link->GetCurve();

    Link->Draw(drawList, flowColor, 2.0f);

    if (IsPathValid())
    {
        //Offset = 0;

        const auto markerAlpha  = powf(1.0f - progress, 0.35f);
        const auto markerRadius = 4.0f * (1.0f - progress) + 2.0f;
        const auto markerColor  = Editor->GetColor(StyleColor_FlowMarker, markerAlpha);

        for (float d = Offset; d < PathLength; d += MarkerDistance)
            drawList->AddCircleFilled(SamplePath(d), markerRadius, markerColor);
    }
}

bool ed::FlowAnimation::IsLinkValid() const
{
    return Link && Link->IsLive;
}

bool ed::FlowAnimation::IsPathValid() const
{
    return Path.size() > 1 && PathLength > 0.0f && Link->Start == LastStart && Link->End == LastEnd;
}

void ed::FlowAnimation::UpdatePath()
{
    if (!IsLinkValid())
    {
        ClearPath();
        return;
    }

    const auto curve  = Link->GetCurve();

    LastStart  = Link->Start;
    LastEnd    = Link->End;
    PathLength = cubic_bezier_length(curve.p0, curve.p1, curve.p2, curve.p3);

    auto collectPointsCallback = [this](bezier_fixed_step_result_t& result)
    {
        Path.push_back(CurvePoint{ result.length, to_imvec(result.point) });
    };

    const auto step = std::max(MarkerDistance * 0.5f, 15.0f);

    Path.resize(0);
    cubic_bezier_fixed_step(collectPointsCallback, curve.p0, curve.p1, curve.p2, curve.p3, step, false, 0.5f, 0.001f);
}

void ed::FlowAnimation::ClearPath()
{
    vector<CurvePoint>().swap(Path);
    PathLength = 0.0f;
}

ImVec2 ed::FlowAnimation::SamplePath(float distance)
{
    //distance = std::max(0.0f, std::min(distance, PathLength));

    auto endPointIt = std::find_if(Path.begin(), Path.end(), [distance](const CurvePoint& p) { return distance < p.Distance; });
    if (endPointIt == Path.end())
        endPointIt = Path.end() - 1;
    else if (endPointIt == Path.begin())
        endPointIt = Path.begin() + 1;

    const auto& start = endPointIt[-1];
    const auto& end   = *endPointIt;
    const auto  t     = (distance - start.Distance) / (end.Distance - start.Distance);

    return start.Point + (end.Point - start.Point) * t;
}

void ed::FlowAnimation::OnUpdate(float progress)
{
    Offset += Speed * ImGui::GetIO().DeltaTime;
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
    for (auto animation : Animations)
        delete animation;
}

void ed::FlowAnimationController::Flow(Link* link)
{
    if (!link || !link->IsLive)
        return;

    auto& editorStyle = GetStyle();

    auto animation = GetOrCreate(link);

    animation->Flow(link, editorStyle.FlowMarkerDistance, editorStyle.FlowSpeed, editorStyle.FlowDuration);
}

void ed::FlowAnimationController::Draw(ImDrawList* drawList)
{
    if (Animations.empty())
        return;

    drawList->ChannelsSetCurrent(c_LinkChannel_Flow);

    for (auto animation : Animations)
        animation->Draw(drawList);
}

ed::FlowAnimation* ed::FlowAnimationController::GetOrCreate(Link* link)
{
    // Return live animation which match target link
    {
        auto animationIt = std::find_if(Animations.begin(), Animations.end(), [link](FlowAnimation* animation) { return animation->Link == link; });
        if (animationIt != Animations.end())
            return *animationIt;
    }

    // There are no live animations for target link, try to reuse inactive old one
    if (!FreePool.empty())
    {
        auto animation = FreePool.back();
        FreePool.pop_back();
        return animation;
    }

    // Cache miss, allocate new one
    auto animation = new FlowAnimation(this);
    Animations.push_back(animation);

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
    /*0.1f, 0.15f, */0.20f, 0.25f, 0.33f, 0.5f, 0.75f, 1.0f, 1.25f, 1.50f, 2.0f, 2.5f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f
};

const int ed::NavigateAction::s_ZoomLevelCount = sizeof(s_ZoomLevels) / sizeof(*s_ZoomLevels);

ed::NavigateAction::NavigateAction(EditorContext* editor):
    EditorAction(editor),
    IsActive(false),
    Zoom(1),
    Scroll(0, 0),
    ScrollStart(0, 0),
    ScrollDelta(0, 0),
    Animation(editor, *this),
    WindowScreenPos(0, 0),
    WindowScreenSize(0, 0),
    Reason(NavigationReason::Unknown),
    LastSelectionId(0),
    LastObject(nullptr),
    MovingOverEdge(false),
    MoveOffset(0, 0)
{
}

ed::EditorAction::AcceptResult ed::NavigateAction::Accept(const Control& control)
{
    assert(!IsActive);

    if (IsActive)
        return False;

    if (ImGui::IsWindowHovered() /*&& !ImGui::IsAnyItemActive()*/ && ImGui::IsMouseDragging(c_ScrollButtonIndex, 0.0f))
    {
        IsActive    = true;
        ScrollStart = Scroll;
        ScrollDelta = ImGui::GetMouseDragDelta(c_ScrollButtonIndex);
        Scroll      = ScrollStart - ScrollDelta * Zoom;
    }

    auto& io = ImGui::GetIO();

    if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_F)))
    {
        const auto allowZoomIn = io.KeyShift;

        auto findHotObjectToZoom = [this, &control, &io]() -> Object*
        {
            if (control.HotObject)
            {
                if (auto pin = control.HotObject->AsPin())
                    return pin->Node;
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
            if (Reason != NavigationReason::Selection || LastSelectionId != Editor->GetSelectionId() || allowZoomIn)
            {
                LastSelectionId = Editor->GetSelectionId();
                NavigateTo(Editor->GetSelectionBounds(), allowZoomIn, -1.0f, NavigationReason::Selection);
            }
            else
                navigateToContent = true;
        }
        else if(auto hotObject = findHotObjectToZoom())
        {
            if (Reason != NavigationReason::Object || LastObject != hotObject || allowZoomIn)
            {
                LastObject = hotObject;
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

    return IsActive ? True : False;
}

bool ed::NavigateAction::Process(const Control& control)
{
    if (!IsActive)
        return false;

    if (ImGui::IsMouseDragging(c_ScrollButtonIndex, 0.0f))
    {
        ScrollDelta = ImGui::GetMouseDragDelta(c_ScrollButtonIndex);
        Scroll      = ScrollStart - ScrollDelta * Zoom;

//         if (IsActive && Animation.IsPlaying())
//             Animation.Target = Animation.Target - ScrollDelta * Animation.TargetZoom;
    }
    else
    {
        if (Scroll != ScrollStart)
            Editor->MakeDirty(SaveReasonFlags::Navigation);

        IsActive = false;
    }

    // #TODO: Handle zoom while scrolling
    // HandleZoom(control);

    return IsActive;
}

bool ed::NavigateAction::HandleZoom(const Control& control)
{
    const auto currentAction  = Editor->GetCurrentAction();
    const auto allowOffscreen = currentAction && currentAction->IsDragging();

    auto& io = ImGui::GetIO();

    if (!io.MouseWheel || (!allowOffscreen && !ImGui::IsWindowHovered()))// && !ImGui::IsAnyItemActive())
        return false;

    auto savedScroll = Scroll;
    auto savedZoom   = Zoom;

    Animation.Finish();

    auto mousePos = io.MousePos;
    auto steps    = (int)io.MouseWheel;
    auto newZoom  = MatchZoom(steps, s_ZoomLevels[steps < 0 ? 0 : s_ZoomLevelCount - 1]);

    auto oldCanvas = GetCanvas(false);
    Zoom = newZoom;
    auto newCanvas = GetCanvas(false);

    auto screenPos = oldCanvas.ToScreen(mousePos);
    auto canvasPos = newCanvas.FromScreen(screenPos);

    auto offset       = (canvasPos - mousePos) * Zoom;
    auto targetScroll = Scroll - offset;

    if (Scroll != savedScroll || Zoom != savedZoom)
    {
        Scroll = savedScroll;
        Zoom = savedZoom;

        Editor->MakeDirty(SaveReasonFlags::Navigation);
    }

    NavigateTo(targetScroll, newZoom, c_MouseZoomDuration, NavigationReason::MouseZoom);

    return true;
}

void ed::NavigateAction::ShowMetrics()
{
    EditorAction::ShowMetrics();

    ImGui::Text("%s:", GetName());
    ImGui::Text("    Active: %s", IsActive ? "yes" : "no");
    ImGui::Text("    Scroll: { x=%g y=%g }", Scroll.x, Scroll.y);
    ImGui::Text("    Zoom: %g", Zoom);
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
    action.Zoom *= zoomChange;

    const auto targetBounds    = action.GetCanvas(false).GetVisibleBounds();
    const auto selectionCenter = to_imvec(selectionBounds.center());
    const auto targetCenter    = to_imvec(targetBounds.center());
    const auto offset          = selectionCenter - targetCenter;

    action.Scroll = action.Scroll + offset * action.Zoom;

    const auto scrollOffset = action.Scroll - Scroll;
    const auto zoomOffset   = action.Zoom   - Zoom;

    const auto epsilon = 1e-4f;

    if (fabsf(scrollOffset.x) < epsilon && fabsf(scrollOffset.y) < epsilon && fabsf(zoomOffset) < epsilon)
        NavigateTo(action.Scroll, action.Zoom, 0, reason);
    else
        NavigateTo(action.Scroll, action.Zoom, GetStyle().ScrollDuration, reason);
}

void ed::NavigateAction::NavigateTo(const ImVec2& target, float targetZoom, float duration, NavigationReason reason)
{
    Reason = reason;

    Animation.NavigateTo(target, targetZoom, duration);
}

void ed::NavigateAction::StopNavigation()
{
    Animation.Stop();
}

void ed::NavigateAction::FinishNavigation()
{
    Animation.Finish();
}

bool ed::NavigateAction::MoveOverEdge()
{
    // Don't interrupt non-edge animations
    if (Animation.IsPlaying())
        return false;

          auto& io            = ImGui::GetIO();
    const auto screenRect     = ax::rectf(to_pointf(WindowScreenPos), to_sizef(WindowScreenSize));
    const auto screenMousePos = to_pointf(io.MousePos);

    // Mouse is over screen, do nothing
    if (screenRect.contains(screenMousePos))
        return false;

    const auto screenPointOnEdge = screenRect.get_closest_point(screenMousePos, true);
    const auto direction         = screenPointOnEdge - screenMousePos;
    const auto offset            = to_imvec(-direction) * io.DeltaTime * 10.0f;

    Scroll = Scroll + offset;

    MoveOffset     = offset;
    MovingOverEdge = true;

    return true;
}

void ed::NavigateAction::StopMoveOverEdge()
{
    if (MovingOverEdge)
    {
        Editor->MakeDirty(SaveReasonFlags::Navigation);

        MoveOffset     = ImVec2(0, 0);
        MovingOverEdge = false;
    }
}

void ed::NavigateAction::SetWindow(ImVec2 position, ImVec2 size)
{
    WindowScreenPos  = position;
    WindowScreenSize = size;
}

ed::Canvas ed::NavigateAction::GetCanvas(bool alignToPixels)
{
    ImVec2 origin = -Scroll;
    if (alignToPixels)
    {
        origin.x = floorf(origin.x);
        origin.y = floorf(origin.y);
    }

    return Canvas(WindowScreenPos, WindowScreenSize, ImVec2(Zoom, Zoom), origin);
}

float ed::NavigateAction::MatchZoom(int steps, float fallbackZoom)
{
    auto currentZoomIndex = MatchZoomIndex(steps);
    if (currentZoomIndex < 0)
        return fallbackZoom;

    auto currentZoom = s_ZoomLevels[currentZoomIndex];
    if (fabsf(currentZoom - Zoom) > 0.001f)
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
        auto distance = fabsf(s_ZoomLevels[i] - Zoom);
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
    IsActive(false),
    SizedNode(nullptr),
    Pivot(rect_region::center),
    Cursor(ImGuiMouseCursor_Arrow)
{
}

ed::EditorAction::AcceptResult ed::SizeAction::Accept(const Control& control)
{
    assert(!IsActive);

    if (IsActive)
        return False;

    if (control.ActiveNode && IsGroup(control.ActiveNode) && ImGui::IsMouseDragging(0, 0))
    {
        const auto mousePos     = to_point(ImGui::GetMousePos());
        const auto closestPoint = control.ActiveNode->Bounds.get_closest_point_hollow(mousePos, static_cast<int>(control.ActiveNode->Rounding));

        auto pivot = GetRegion(control.ActiveNode);
        if (pivot != rect_region::center)
        {
            StartBounds      = control.ActiveNode->Bounds;
            StartGroupBounds = control.ActiveNode->GroupBounds;
            LastSize         = control.ActiveNode->Bounds.size;
            LastDragOffset   = point(0, 0);
            Stable           = true;
            Pivot            = pivot;
            Cursor           = ChooseCursor(Pivot);
            SizedNode        = control.ActiveNode;
            IsActive         = true;
        }
    }
    else if (control.HotNode && IsGroup(control.HotNode))
    {
        Cursor = ChooseCursor(GetRegion(control.HotNode));
        return Possible;
    }

    return IsActive ? True : False;
}

bool ed::SizeAction::Process(const Control& control)
{
    if (!IsActive)
        return false;

    if (control.ActiveNode == SizedNode || !Stable)
    {
        const auto dragOffset = (control.ActiveNode == SizedNode) ? to_point(ImGui::GetMouseDragDelta(0, 0.0f)) : LastDragOffset;
        LastDragOffset = dragOffset;

        // Calculate new rect size
        auto p0 = StartBounds.top_left();
        auto p1 = StartBounds.bottom_right();

        if ((Pivot & rect_region::top) == rect_region::top)
            p0.y = std::min(p0.y + dragOffset.y, p1.y);
        if ((Pivot & rect_region::bottom) == rect_region::bottom)
            p1.y = std::max(p1.y + dragOffset.y, p0.y);
        if ((Pivot & rect_region::left) == rect_region::left)
            p0.x = std::min(p0.x + dragOffset.x, p1.x);
        if ((Pivot & rect_region::right) == rect_region::right)
            p1.x = std::max(p1.x + dragOffset.x, p0.x);

        auto newBounds = ax::rect(p0, p1);
        auto newSize   = newBounds.size;

        Stable = true;

        // Apply dynamic minimum size constrains. It will always be one frame off
        if (SizedNode->Bounds.w > LastSize.w)
        {
            if ((Pivot & rect_region::left) == rect_region::left)
            {
                  LastSize.w = newBounds.w;
                 newBounds.w = SizedNode->Bounds.w;
                 newBounds.x = StartBounds.right() - newBounds.w;
            }
            else
            {
                  LastSize.w = newBounds.w;
                 newBounds.w = SizedNode->Bounds.w;
            }
        }
        else
        {
            if (LastSize.w != newBounds.w)
                Stable &= false;

            LastSize.w = newBounds.w;
        }

        if (SizedNode->Bounds.h > LastSize.h)
        {
            if ((Pivot & rect_region::top) == rect_region::top)
            {
                 LastSize.h = newBounds.h;
                newBounds.h = SizedNode->Bounds.h;
                newBounds.y = StartBounds.bottom() - newBounds.h;
            }
            else
            {
                 LastSize.h = newBounds.h;
                newBounds.h = SizedNode->Bounds.h;
            }
        }
        else
        {
            if (LastSize.h != newBounds.h)
                Stable &= false;

            LastSize.h = newBounds.h;
        }

        SizedNode->Bounds      = newBounds;
        SizedNode->GroupBounds = newBounds.expanded(
            StartBounds.left()        - StartGroupBounds.left(),
            StartBounds.top()         - StartGroupBounds.top(),
            StartGroupBounds.right()  - StartBounds.right(),
            StartGroupBounds.bottom() - StartBounds.bottom());
    }
    else if (!control.ActiveNode)
    {
        if (SizedNode->Bounds.location != StartBounds.location || SizedNode->GroupBounds.location != StartGroupBounds.location)
            Editor->MakeDirty(SaveReasonFlags::Position, SizedNode);

        if (SizedNode->Bounds.size != StartBounds.size || SizedNode->GroupBounds.size != StartGroupBounds.size)
            Editor->MakeDirty(SaveReasonFlags::Size, SizedNode);

        SizedNode = nullptr;
        IsActive = false;
        return true;
    }

    return IsActive;
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
    ImGui::Text("    Active: %s", IsActive ? "yes" : "no");
    ImGui::Text("    Node: %s (%d)", getObjectName(SizedNode), SizedNode ? SizedNode->ID : 0);
    if (SizedNode && IsActive)
    {
        ImGui::Text("    Bounds: { x=%d y=%d w=%d h=%d }", SizedNode->Bounds.x, SizedNode->Bounds.y, SizedNode->Bounds.w, SizedNode->Bounds.h);
        ImGui::Text("    Group Bounds: { x=%d y=%d w=%d h=%d }", SizedNode->GroupBounds.x, SizedNode->GroupBounds.y, SizedNode->GroupBounds.w, SizedNode->GroupBounds.h);
        ImGui::Text("    Start Bounds: { x=%d y=%d w=%d h=%d }", StartBounds.x, StartBounds.y, StartBounds.w, StartBounds.h);
        ImGui::Text("    Start Group Bounds: { x=%d y=%d w=%d h=%d }", StartGroupBounds.x, StartGroupBounds.y, StartGroupBounds.w, StartGroupBounds.h);
        ImGui::Text("    Last Size: { w=%d h=%d }", LastSize.w, LastSize.h);
        ImGui::Text("    Stable: %s", Stable ? "Yes" : "No");
    }
}

ax::rect_region ed::SizeAction::GetRegion(Node* node)
{
    if (!node || !IsGroup(node))
        return rect_region::center;

    rect_region region = rect_region::center;
    const auto mousePos = to_point(ImGui::GetMousePos());
    const auto closestPoint = node->Bounds.get_closest_point_hollow(to_point(ImGui::GetMousePos()), static_cast<int>(node->Rounding), &region);

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
    IsActive(false),
    DraggedObject(nullptr)
{
}

ed::EditorAction::AcceptResult ed::DragAction::Accept(const Control& control)
{
    assert(!IsActive);

    if (IsActive)
        return False;

    if (control.ActiveObject && ImGui::IsMouseDragging(0))
    {
        if (!control.ActiveObject->AcceptDrag())
            return False;

        DraggedObject = control.ActiveObject;

        Objects.resize(0);
        Objects.push_back(DraggedObject);

        if (Editor->IsSelected(DraggedObject))
        {
            for (auto selectedObject : Editor->GetSelectedObjects())
                if (auto selectedNode = selectedObject->AsNode())
                    if (selectedNode != DraggedObject && selectedNode->AcceptDrag())
                        Objects.push_back(selectedNode);
        }

        auto& io = ImGui::GetIO();
        if (!io.KeyShift)
        {
            std::vector<Node*> groupedNodes;
            for (auto object : Objects)
                if (auto node = object->AsNode())
                    node->GetGroupedNodes(groupedNodes, true);

            auto isAlreadyPicked = [this](Node* node)
            {
                return std::find(Objects.begin(), Objects.end(), node) != Objects.end();
            };

            for (auto candidate : groupedNodes)
                if (!isAlreadyPicked(candidate) && candidate->AcceptDrag())
                    Objects.push_back(candidate);
        }

        IsActive = true;
    }

    return IsActive ? True : False;
}

bool ed::DragAction::Process(const Control& control)
{
    if (!IsActive)
        return false;

    if (control.ActiveObject == DraggedObject)
    {
        auto dragOffset = to_point(ImGui::GetMouseDragDelta(0, 0.0f));

        for (auto object : Objects)
            object->UpdateDrag(dragOffset);
    }
    else if (!control.ActiveObject)
    {
        for (auto object : Objects)
        {
            if (object->EndDrag())
                Editor->MakeDirty(SaveReasonFlags::Position, object->AsNode());
        }

        Objects.resize(0);

        DraggedObject = nullptr;
        IsActive = false;
        return true;
    }

    return IsActive;
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
    ImGui::Text("    Active: %s", IsActive ? "yes" : "no");
    ImGui::Text("    Node: %s (%d)", getObjectName(DraggedObject), DraggedObject ? DraggedObject->ID : 0);
}




//------------------------------------------------------------------------------
//
// Select Action
//
//------------------------------------------------------------------------------
ed::SelectAction::SelectAction(EditorContext* editor):
    EditorAction(editor),
    IsActive(false),
    SelectGroups(false),
    SelectLinkMode(false),
    StartPoint(),
    Animation(editor)
{
}

ed::EditorAction::AcceptResult ed::SelectAction::Accept(const Control& control)
{
    assert(!IsActive);

    if (IsActive)
        return False;

    auto& io = ImGui::GetIO();
    SelectGroups   = io.KeyShift;
    SelectLinkMode = io.KeyAlt;

    SelectedObjectsAtStart.clear();

    if (control.BackgroundActive && ImGui::IsMouseDragging(0, 1))
    {
        IsActive = true;
        StartPoint = ImGui::GetMousePos();
        EndPoint   = StartPoint;

        // Links and nodes cannot be selected together
        if ((SelectLinkMode && Editor->IsAnyNodeSelected()) ||
           (!SelectLinkMode && Editor->IsAnyLinkSelected()))
        {
            Editor->ClearSelection();
        }

        if (io.KeyCtrl)
            SelectedObjectsAtStart = Editor->GetSelectedObjects();
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

    if (IsActive)
        Animation.Stop();

    return IsActive ? True : False;
}

bool ed::SelectAction::Process(const Control& control)
{
    if (!IsActive)
        return false;

    using namespace ax::ImGuiInterop;

    if (ImGui::IsMouseDragging(0, 0))
    {
        EndPoint = ImGui::GetMousePos();

        auto topLeft     = ImVec2(std::min(StartPoint.x, EndPoint.x), std::min(StartPoint.y, EndPoint.y));
        auto bottomRight = ImVec2(std::max(StartPoint.x, EndPoint.x), std::max(StartPoint.y, EndPoint.y));
        auto rect        = ax::rectf(to_pointf(topLeft), to_pointf(bottomRight));
        if (rect.w <= 0)
            rect.w = 1;
        if (rect.h <= 0)
            rect.h = 1;

        vector<Node*> nodes;
        vector<Link*> links;

        if (SelectLinkMode)
        {
            Editor->FindLinksInRect(rect, links);
            CandidateObjects.assign(links.begin(), links.end());
        }
        else
        {
            Editor->FindNodesInRect(rect, nodes);
            CandidateObjects.assign(nodes.begin(), nodes.end());

            if (SelectGroups)
            {
                auto endIt = std::remove_if(CandidateObjects.begin(), CandidateObjects.end(), [](Object* object) { return !IsGroup(object->AsNode()); });
                CandidateObjects.erase(endIt, CandidateObjects.end());
            }
            else
            {
                auto endIt = std::remove_if(CandidateObjects.begin(), CandidateObjects.end(), [](Object* object) { return IsGroup(object->AsNode()); });
                CandidateObjects.erase(endIt, CandidateObjects.end());
            }
        }

        CandidateObjects.insert(CandidateObjects.end(), SelectedObjectsAtStart.begin(), SelectedObjectsAtStart.end());
        std::sort(CandidateObjects.begin(), CandidateObjects.end());
        CandidateObjects.erase(std::unique(CandidateObjects.begin(), CandidateObjects.end()), CandidateObjects.end());
    }
    else
    {
        Editor->ClearSelection();
        for (auto object : CandidateObjects)
            Editor->SelectObject(object);

        CandidateObjects.clear();

        IsActive = false;

        Animation.Play(c_SelectionFadeOutDuration);

        return true;
    }

    return IsActive;
}

void ed::SelectAction::ShowMetrics()
{
    EditorAction::ShowMetrics();

    ImGui::Text("%s:", GetName());
    ImGui::Text("    Active: %s", IsActive ? "yes" : "no");
}

void ed::SelectAction::Draw(ImDrawList* drawList)
{
    if (!IsActive && !Animation.IsPlaying())
        return;

    const auto alpha = Animation.IsPlaying() ? easing::ease_out_quad(1.0f, -1.0f, Animation.GetProgress()) : 1.0f;

    const auto fillColor    = Editor->GetColor(SelectLinkMode ? StyleColor_LinkSelRect       : StyleColor_NodeSelRect, alpha);
    const auto outlineColor = Editor->GetColor(SelectLinkMode ? StyleColor_LinkSelRectBorder : StyleColor_NodeSelRectBorder, alpha);

    drawList->ChannelsSetCurrent(c_BackgroundChannel_SelectionRect);

    auto min  = ImVec2(std::min(StartPoint.x, EndPoint.x), std::min(StartPoint.y, EndPoint.y));
    auto max  = ImVec2(std::max(StartPoint.x, EndPoint.x), std::max(StartPoint.y, EndPoint.y));

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
    CandidateMenu(Menu::None),
    CurrentMenu(Menu::None),
    ContextId(0)
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
                contextId = hotObejct->ID;
        }
        else if (control.BackgroundHot)
            candidateMenu = Background;

        if (isPressed)
        {
            CandidateMenu = candidateMenu;
            ContextId     = contextId;
            return Possible;
        }
        else if (isReleased && CandidateMenu == candidateMenu && ContextId == contextId)
        {
            CurrentMenu   = CandidateMenu;
            CandidateMenu = ContextMenuAction::None;
            return True;
        }
        else
        {
            CandidateMenu = None;
            CurrentMenu   = None;
            ContextId     = 0;
            return False;
        }
    }

    return False;
}

bool ed::ContextMenuAction::Process(const Control& control)
{
    CandidateMenu = None;
    CurrentMenu   = None;
    ContextId     = 0;
    return false;
}

void ed::ContextMenuAction::Reject()
{
    CandidateMenu = None;
    CurrentMenu   = None;
    ContextId     = 0;
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
    ImGui::Text("    Menu: %s", getMenuName(CurrentMenu));
}

bool ed::ContextMenuAction::ShowNodeContextMenu(int* nodeId)
{
    if (CurrentMenu != Node)
        return false;

    *nodeId = ContextId;
    Editor->SetUserContext();
    return true;
}

bool ed::ContextMenuAction::ShowPinContextMenu(int* pinId)
{
    if (CurrentMenu != Pin)
        return false;

    *pinId = ContextId;
    Editor->SetUserContext();
    return true;
}

bool ed::ContextMenuAction::ShowLinkContextMenu(int* linkId)
{
    if (CurrentMenu != Link)
        return false;

    *linkId = ContextId;
    Editor->SetUserContext();
    return true;
}

bool ed::ContextMenuAction::ShowBackgroundContextMenu()
{
    if (CurrentMenu != Background)
        return false;

    Editor->SetUserContext();
    return true;
}




//------------------------------------------------------------------------------
//
// Create Item Action
//
//------------------------------------------------------------------------------
ed::CreateItemAction::CreateItemAction(EditorContext* editor):
    EditorAction(editor),
    InActive(false),
    NextStage(None),
    CurrentStage(None),
    ItemType(NoItem),
    UserAction(Unknown),
    LinkColor(IM_COL32_WHITE),
    LinkThickness(1.0f),
    LinkStart(nullptr),
    LinkEnd(nullptr),

    IsActive(false),
    DraggedPin(nullptr),

    IsInGlobalSpace(false)
{
}

ed::EditorAction::AcceptResult ed::CreateItemAction::Accept(const Control& control)
{
    assert(!IsActive);

    if (IsActive)
        return EditorAction::False;

    if (control.ActivePin && ImGui::IsMouseDragging(0))
    {
        DraggedPin = control.ActivePin;
        DragStart(DraggedPin);

        Editor->ClearSelection();
    }
    else
        return EditorAction::False;

    IsActive = true;

    return EditorAction::True;
}

bool ed::CreateItemAction::Process(const Control& control)
{
    assert(IsActive);

    if (!IsActive)
        return false;

    if (DraggedPin && control.ActivePin == DraggedPin && (CurrentStage == Possible))
    {
        const auto draggingFromSource = (DraggedPin->Kind == PinKind::Source);

        ed::Pin cursorPin(Editor, 0, draggingFromSource ? PinKind::Target : PinKind::Source);
        cursorPin.Pivot    = ax::rectf(to_pointf(ImGui::GetMousePos()), sizef(0, 0));
        cursorPin.Dir      = -DraggedPin->Dir;
        cursorPin.Strength =  DraggedPin->Strength;

        ed::Link candidate(Editor, 0);
        candidate.Color    = LinkColor;
        candidate.StartPin = draggingFromSource ? DraggedPin : &cursorPin;
        candidate.EndPin   = draggingFromSource ? &cursorPin : DraggedPin;

        ed::Pin*& freePin  = draggingFromSource ? candidate.EndPin : candidate.StartPin;

        if (control.HotPin)
        {
            DropPin(control.HotPin);

            if (UserAction == UserAccept)
                freePin = control.HotPin;
        }
        else if (control.BackgroundHot)
            DropNode();
        else
            DropNothing();

        auto drawList = ImGui::GetWindowDrawList();
        drawList->ChannelsSetCurrent(c_LinkChannel_NewLink);

        candidate.UpdateEndpoints();
        candidate.Draw(drawList, LinkColor, LinkThickness);
    }
    else if (CurrentStage == Possible)
    {
        if (!ImGui::IsWindowHovered())
        {
            DraggedPin = nullptr;
            DropNothing();
        }

        DragEnd();
        IsActive = false;
    }

    return IsActive;
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
    ImGui::Text("    Stage: %s", getStageName(CurrentStage));
    ImGui::Text("    User Action: %s", getActionName(UserAction));
    ImGui::Text("    Item Type: %s", getItemName(ItemType));
}

void ed::CreateItemAction::SetStyle(ImU32 color, float thickness)
{
    LinkColor     = color;
    LinkThickness = thickness;
}

bool ed::CreateItemAction::Begin()
{
    IM_ASSERT(false == InActive);

    InActive        = true;
    CurrentStage    = NextStage;
    UserAction      = Unknown;
    LinkColor       = IM_COL32_WHITE;
    LinkThickness   = 1.0f;

    if (CurrentStage == None)
        return false;

    return true;
}

void ed::CreateItemAction::End()
{
    IM_ASSERT(InActive);

    if (IsInGlobalSpace)
    {
        ImGui::PopClipRect();
        IsInGlobalSpace = false;
    }

    InActive = false;
}

void ed::CreateItemAction::DragStart(Pin* startPin)
{
    IM_ASSERT(!InActive);

    NextStage = Possible;
    LinkStart = startPin;
    LinkEnd   = nullptr;
}

void ed::CreateItemAction::DragEnd()
{
    IM_ASSERT(!InActive);

    if (CurrentStage == Possible && UserAction == UserAccept)
    {
        NextStage = Create;
    }
    else
    {
        NextStage = None;
        ItemType  = NoItem;
        LinkStart = nullptr;
        LinkEnd   = nullptr;
    }
}

void ed::CreateItemAction::DropPin(Pin* endPin)
{
    IM_ASSERT(!InActive);

    ItemType = Link;
    LinkEnd  = endPin;
}

void ed::CreateItemAction::DropNode()
{
    IM_ASSERT(!InActive);

    ItemType = Node;
    LinkEnd  = nullptr;
}

void ed::CreateItemAction::DropNothing()
{
    IM_ASSERT(!InActive);

    ItemType = NoItem;
    LinkEnd  = nullptr;
}

ed::CreateItemAction::Result ed::CreateItemAction::RejectItem()
{
    IM_ASSERT(InActive);

    if (!InActive || CurrentStage == None || ItemType == NoItem)
        return Indeterminate;

    UserAction = UserReject;

    return True;
}

ed::CreateItemAction::Result ed::CreateItemAction::AcceptItem()
{
    IM_ASSERT(InActive);

    if (!InActive || CurrentStage == None || ItemType == NoItem)
        return Indeterminate;

    UserAction = UserAccept;

    if (CurrentStage == Create)
    {
        NextStage = None;
        ItemType  = NoItem;
        LinkStart = nullptr;
        LinkEnd   = nullptr;
        return True;
    }
    else
        return False;
}

ed::CreateItemAction::Result ed::CreateItemAction::QueryLink(int* startId, int* endId)
{
    IM_ASSERT(InActive);

    if (!InActive || CurrentStage == None || ItemType != Link)
        return Indeterminate;

    int linkStartId = LinkStart->ID;
    int linkEndId   = LinkEnd->ID;

    *startId = linkStartId;
    *endId   = linkEndId;

    Editor->SetUserContext(true);

    if (!IsInGlobalSpace)
    {
        auto& canvas = Editor->GetCanvas();
        ImGui::PushClipRect(canvas.WindowScreenPos + ImVec2(1, 1), canvas.WindowScreenPos + canvas.WindowScreenSize - ImVec2(1, 1), false);
        IsInGlobalSpace = true;
    }

    return True;
}

ed::CreateItemAction::Result ed::CreateItemAction::QueryNode(int* pinId)
{
    IM_ASSERT(InActive);

    if (!InActive || CurrentStage == None || ItemType != Node)
        return Indeterminate;

    *pinId = LinkStart ? LinkStart->ID : 0;

    Editor->SetUserContext(true);

    if (!IsInGlobalSpace)
    {
        auto& canvas = Editor->GetCanvas();
        ImGui::PushClipRect(canvas.WindowScreenPos + ImVec2(1, 1), canvas.WindowScreenPos + canvas.WindowScreenSize - ImVec2(1, 1), false);
        IsInGlobalSpace = true;
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
    IsActive(false),
    InInteraction(false),
    CurrentItemType(Unknown),
    UserAction(Undetermined)
{
}

ed::EditorAction::AcceptResult ed::DeleteItemsAction::Accept(const Control& control)
{
    assert(!IsActive);

    if (IsActive)
        return False;

    auto addDeadLinks = [this]()
    {
        vector<ed::Link*> links;
        for (auto object : CandidateObjects)
        {
            auto node = object->AsNode();
            if (!node)
                continue;

            Editor->FindLinksForNode(node->ID, links, true);
        }
        if (!links.empty())
        {
            std::sort(links.begin(), links.end());
            links.erase(std::unique(links.begin(), links.end()), links.end());
            CandidateObjects.insert(CandidateObjects.end(), links.begin(), links.end());
        }
    };

    auto& io = ImGui::GetIO();
    if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Delete)))
    {
        auto& selection = Editor->GetSelectedObjects();
        if (!selection.empty())
        {
            CandidateObjects = selection;
            addDeadLinks();
            IsActive = true;
            return True;
        }
    }
    else if (control.ClickedLink && io.KeyAlt)
    {
        CandidateObjects.clear();
        CandidateObjects.push_back(control.ClickedLink);
        IsActive = true;
        return True;
    }

    else if (!ManuallyDeletedObjects.empty())
    {
        CandidateObjects = ManuallyDeletedObjects;
        ManuallyDeletedObjects.clear();
        addDeadLinks();
        IsActive = true;
        return True;
    }

    return IsActive ? True : False;
}

bool ed::DeleteItemsAction::Process(const Control& control)
{
    if (!IsActive)
        return false;

    if (CandidateObjects.empty())
    {
        IsActive = false;
        return true;
    }

    return IsActive;
}

void ed::DeleteItemsAction::ShowMetrics()
{
    EditorAction::ShowMetrics();

    auto getObjectName = [](Object* object)
    {
        if (!object) return "";
        else if (object->AsNode()) return "Node";
        else if (object->AsPin())  return "Pin";
        else if (object->AsLink()) return "Link";
        else return "";
    };

    ImGui::Text("%s:", GetName());
    ImGui::Text("    Active: %s", IsActive ? "yes" : "no");
    //ImGui::Text("    Node: %s (%d)", getObjectName(DeleteItemsgedNode), DeleteItemsgedNode ? DeleteItemsgedNode->ID : 0);
}

bool ed::DeleteItemsAction::Add(Object* object)
{
    if (Editor->GetCurrentAction() != nullptr)
        return false;

    ManuallyDeletedObjects.push_back(object);

    return true;
}

bool ed::DeleteItemsAction::Begin()
{
    if (!IsActive)
        return false;

    assert(!InInteraction);
    InInteraction = true;

    CurrentItemType = Unknown;
    UserAction      = Undetermined;

    return IsActive;
}

void ed::DeleteItemsAction::End()
{
    if (!IsActive)
        return;

    assert(InInteraction);
    InInteraction = false;
}

bool ed::DeleteItemsAction::QueryLink(int* linkId)
{
    return QueryItem(linkId, Link);
}

bool ed::DeleteItemsAction::QueryNode(int* nodeId)
{
    return QueryItem(nodeId, Node);
}

bool ed::DeleteItemsAction::QueryItem(int* itemId, IteratorType itemType)
{
    if (!InInteraction)
        return false;

    if (CurrentItemType != itemType)
    {
        CurrentItemType    = itemType;
        CandidateItemIndex = 0;
    }
    else if (UserAction == Undetermined)
    {
        RejectItem();
    }

    UserAction = Undetermined;

    auto itemCount = (int)CandidateObjects.size();
    while (CandidateItemIndex < itemCount)
    {
        auto item = CandidateObjects[CandidateItemIndex];
        if (itemType == Node)
        {
            if (auto node = item->AsNode())
            {
                *itemId = node->ID;
                return true;
            }
        }
        else if (itemType == Link)
        {
            if (auto link = item->AsLink())
            {
                *itemId = link->ID;
                return true;
            }
        }

        ++CandidateItemIndex;
    }

    if (CandidateItemIndex == itemCount)
        CurrentItemType = Unknown;

    return false;
}

bool ed::DeleteItemsAction::AcceptItem()
{
    if (!InInteraction)
        return false;

    UserAction = Accepted;

    RemoveItem();

    return true;
}

void ed::DeleteItemsAction::RejectItem()
{
    if (!InInteraction)
        return;

    UserAction = Rejected;

    RemoveItem();
}

void ed::DeleteItemsAction::RemoveItem()
{
    auto item = CandidateObjects[CandidateItemIndex];
    CandidateObjects.erase(CandidateObjects.begin() + CandidateItemIndex);

    Editor->DeselectObject(item);

    if (CurrentItemType == Link)
        Editor->NotifyLinkDeleted(item->AsLink());
}




//------------------------------------------------------------------------------
//
// Node Builder
//
//------------------------------------------------------------------------------
ed::NodeBuilder::NodeBuilder(EditorContext* editor):
    Editor(editor),
    CurrentNode(nullptr),
    CurrentPin(nullptr)
{
}

void ed::NodeBuilder::Begin(int nodeId)
{
    assert(nullptr == CurrentNode);

    CurrentNode = Editor->GetNode(nodeId);

    if (CurrentNode->RestoreState)
    {
        Editor->RestoreNodeState(CurrentNode);
        CurrentNode->RestoreState = false;
    }

    // Position node on screen
    ImGui::SetCursorScreenPos(to_imvec(CurrentNode->Bounds.location));

    auto& editorStyle = Editor->GetStyle();

    const auto alpha = ImGui::GetStyle().Alpha;

    CurrentNode->IsLive           = true;
    CurrentNode->LastPin          = nullptr;
    CurrentNode->Color            = Editor->GetColor(StyleColor_NodeBg, alpha);
    CurrentNode->BorderColor      = Editor->GetColor(StyleColor_NodeBorder, alpha);
    CurrentNode->BorderWidth      = editorStyle.NodeBorderWidth;
    CurrentNode->Rounding         = editorStyle.NodeRounding;
    CurrentNode->GroupColor       = Editor->GetColor(StyleColor_GroupBg, alpha);
    CurrentNode->GroupBorderColor = Editor->GetColor(StyleColor_GroupBorder, alpha);
    CurrentNode->GroupBorderWidth = editorStyle.GroupBorderWidth;
    CurrentNode->GroupRounding    = editorStyle.GroupRounding;

    IsGroup = false;

    // Grow channel list and select user channel
    if (auto drawList = ImGui::GetWindowDrawList())
    {
        CurrentNode->Channel = drawList->_ChannelsCount;
        ImDrawList_ChannelsGrow(drawList, drawList->_ChannelsCount + c_ChannelsPerNode);
        drawList->ChannelsSetCurrent(CurrentNode->Channel + c_NodeContentChannel);
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
    assert(nullptr != CurrentNode);

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

    NodeRect = ImGui_GetItemRect();

    if (CurrentNode->Bounds.size != NodeRect.size)
    {
        CurrentNode->Bounds.size = NodeRect.size;
        Editor->MakeDirty(SaveReasonFlags::Size, CurrentNode);
    }

    if (IsGroup)
    {
        // Groups cannot have pins. Discard them.
        for (auto pin = CurrentNode->LastPin; pin; pin = pin->PreviousPin)
            pin->IsLive = false;

        CurrentNode->Type        = NodeType::Group;
        CurrentNode->GroupBounds = GroupBounds;
        CurrentNode->LastPin     = nullptr;
    }
    else
        CurrentNode->Type        = NodeType::Node;

    CurrentNode = nullptr;
}

void ed::NodeBuilder::BeginPin(int pinId, PinKind kind)
{
    assert(nullptr != CurrentNode);
    assert(nullptr == CurrentPin);
    assert(false   == IsGroup);

    auto& editorStyle = Editor->GetStyle();

    CurrentPin = Editor->GetPin(pinId, kind);
    CurrentPin->Node = CurrentNode;

    CurrentPin->IsLive      = true;
    CurrentPin->Color       = Editor->GetColor(StyleColor_PinRect);
    CurrentPin->BorderColor = Editor->GetColor(StyleColor_PinRectBorder);
    CurrentPin->BorderWidth = editorStyle.PinBorderWidth;
    CurrentPin->Rounding    = editorStyle.PinRounding;
    CurrentPin->Corners     = static_cast<int>(editorStyle.PinCorners);
    CurrentPin->Radius      = editorStyle.PinRadius;
    CurrentPin->ArrowSize   = editorStyle.PinArrowSize;
    CurrentPin->ArrowWidth  = editorStyle.PinArrowWidth;
    CurrentPin->Dir         = kind == PinKind::Source ? editorStyle.SourceDirection : editorStyle.TargetDirection;
    CurrentPin->Strength    = editorStyle.LinkStrength;

    CurrentPin->PreviousPin = CurrentNode->LastPin;
    CurrentNode->LastPin    = CurrentPin;

    PivotAlignment          = editorStyle.PivotAlignment;
    PivotSize               = editorStyle.PivotSize;
    PivotScale              = editorStyle.PivotScale;
    ResolvePinRect          = true;
    ResolvePivot            = true;

    ImGui::BeginGroup();
}

void ed::NodeBuilder::EndPin()
{
    assert(nullptr != CurrentPin);

    ImGui::EndGroup();

    if (ResolvePinRect)
        CurrentPin->Bounds = ImGui_GetItemRect();

    if (ResolvePivot)
    {
        auto& pinRect = CurrentPin->Bounds;

        if (PivotSize.x < 0)
            PivotSize.x = static_cast<float>(pinRect.size.w);
        if (PivotSize.y < 0)
            PivotSize.y = static_cast<float>(pinRect.size.h);

        CurrentPin->Pivot.location = static_cast<pointf>(to_pointf(pinRect.top_left()) + static_cast<pointf>(pinRect.size).cwise_product(to_pointf(PivotAlignment)));
        CurrentPin->Pivot.size     = static_cast<sizef>((to_pointf(PivotSize).cwise_product(to_pointf(PivotScale))));
    }

    CurrentPin = nullptr;
}

void ed::NodeBuilder::PinRect(const ImVec2& a, const ImVec2& b)
{
    assert(nullptr != CurrentPin);

    CurrentPin->Bounds = rect(to_point(a), to_size(b - a));
    ResolvePinRect     = false;
}

void ed::NodeBuilder::PinPivotRect(const ImVec2& a, const ImVec2& b)
{
    assert(nullptr != CurrentPin);

    CurrentPin->Pivot = rectf(to_pointf(a), to_sizef(b - a));
    ResolvePivot      = false;
}

void ed::NodeBuilder::PinPivotSize(const ImVec2& size)
{
    assert(nullptr != CurrentPin);

    PivotSize    = size;
    ResolvePivot = true;
}

void ed::NodeBuilder::PinPivotScale(const ImVec2& scale)
{
    assert(nullptr != CurrentPin);

    PivotScale   = scale;
    ResolvePivot = true;
}

void ed::NodeBuilder::PinPivotAlignment(const ImVec2& alignment)
{
    assert(nullptr != CurrentPin);

    PivotAlignment = alignment;
    ResolvePivot   = true;
}

void ed::NodeBuilder::Group(const ImVec2& size)
{
    assert(nullptr != CurrentNode);
    assert(nullptr == CurrentPin);
    assert(false   == IsGroup);

    IsGroup = true;

    if (::IsGroup(CurrentNode))
        ImGui::Dummy(to_imvec(CurrentNode->GroupBounds.size));
    else
        ImGui::Dummy(size);

    GroupBounds = ImGui_GetItemRect();
}

ImDrawList* ed::NodeBuilder::GetUserBackgroundDrawList() const
{
    return GetUserBackgroundDrawList(CurrentNode);
}

ImDrawList* ed::NodeBuilder::GetUserBackgroundDrawList(Node* node) const
{
    if (node && node->IsLive)
    {
        auto drawList = ImGui::GetWindowDrawList();
        drawList->ChannelsSetCurrent(node->Channel + c_NodeUserBackgroundChannel);
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
    IsActive(false),
    CurrentNode(nullptr)
{
}

bool ed::HintBuilder::Begin(int nodeId)
{
    assert(nullptr == CurrentNode);

    auto& canvas = Editor->GetCanvas();

    const float c_min_zoom = 0.75f;
    const float c_max_zoom = 0.50f;

    if (canvas.Zoom.y > 0.75f)
        return false;

    auto node = Editor->FindNode(nodeId);
    if (!IsGroup(node))
        return false;

    CurrentNode = node;

    Editor->Suspend();

    const auto alpha = std::max(0.0f, std::min(1.0f, (canvas.Zoom.y - c_min_zoom) / (c_max_zoom - c_min_zoom)));

    ImGui::GetWindowDrawList()->ChannelsSetCurrent(c_UserChannel_HintsBackground);
    ImGui::PushClipRect(canvas.WindowScreenPos + ImVec2(1, 1), canvas.WindowScreenPos + canvas.WindowScreenSize - ImVec2(1, 1), false);

    ImGui::GetWindowDrawList()->ChannelsSetCurrent(c_UserChannel_Hints);
    ImGui::PushClipRect(canvas.WindowScreenPos + ImVec2(1, 1), canvas.WindowScreenPos + canvas.WindowScreenSize - ImVec2(1, 1), false);

    ImGui::PushStyleVar(ImGuiStyleVar_AntiAliasFringeScale, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);

    IsActive = true;

    return true;
}

void ed::HintBuilder::End()
{
    if (!IsActive)
        return;

    ImGui::PopClipRect();
    ImGui::PopClipRect();
    ImGui::PopStyleVar(2);

    Editor->Resume();

    IsActive    = false;
    CurrentNode = nullptr;
}

ImVec2 ed::HintBuilder::GetGroupMin()
{
    assert(nullptr != CurrentNode);

    return Editor->GetCanvas().ToScreen(to_imvec(CurrentNode->Bounds.top_left()));
}

ImVec2 ed::HintBuilder::GetGroupMax()
{
    assert(nullptr != CurrentNode);

    return Editor->GetCanvas().ToScreen(to_imvec(CurrentNode->Bounds.bottom_right()));
}

ImDrawList* ed::HintBuilder::GetForegroundDrawList()
{
    assert(nullptr != CurrentNode);

    auto drawList = ImGui::GetWindowDrawList();

    drawList->ChannelsSetCurrent(c_UserChannel_Hints);

    return drawList;
}

ImDrawList* ed::HintBuilder::GetBackgroundDrawList()
{
    assert(nullptr != CurrentNode);

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
    ColorStack.push_back(modifier);
    Colors[colorIndex] = color;
}

void ed::Style::PopColor(int count)
{
    while (count > 0)
    {
        auto& modifier = ColorStack.back();
        Colors[modifier.Index] = modifier.Value;
        ColorStack.pop_back();
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
    VarStack.push_back(modifier);
}

void ed::Style::PushVar(StyleVar varIndex, const ImVec2& value)
{
    auto* var = GetVarVec2Addr(varIndex);
    assert(var != nullptr);
    VarModifier modifier;
    modifier.Index = varIndex;
    modifier.Value = ImVec4(var->x, var->y, 0, 0);
    *var = value;
    VarStack.push_back(modifier);
}

void ed::Style::PushVar(StyleVar varIndex, const ImVec4& value)
{
    auto* var = GetVarVec4Addr(varIndex);
    assert(var != nullptr);
    VarModifier modifier;
    modifier.Index = varIndex;
    modifier.Value = *var;
    *var = value;
    VarStack.push_back(modifier);
}

void ed::Style::PopVar(int count)
{
    while (count > 0)
    {
        auto& modifier = VarStack.back();
        if (auto v = GetVarFloatAddr(modifier.Index))
            *v = modifier.Value.x;
        else if (auto v = GetVarVec2Addr(modifier.Index))
            *v = ImVec2(modifier.Value.x, modifier.Value.y);
        else if (auto v = GetVarVec4Addr(modifier.Index))
            *v = modifier.Value;
        VarStack.pop_back();
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
    }

    return nullptr;
}

ImVec2* ed::Style::GetVarVec2Addr(StyleVar idx)
{
    switch (idx)
    {
        case StyleVar_SourceDirection: return &SourceDirection;
        case StyleVar_TargetDirection: return &TargetDirection;
        case StyleVar_PivotAlignment:  return &PivotAlignment;
        case StyleVar_PivotSize:       return &PivotSize;
        case StyleVar_PivotScale:      return &PivotScale;
    }

    return nullptr;
}

ImVec4* ed::Style::GetVarVec4Addr(StyleVar idx)
{
    switch (idx)
    {
        case StyleVar_NodePadding: return &NodePadding;
    }

    return nullptr;
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
