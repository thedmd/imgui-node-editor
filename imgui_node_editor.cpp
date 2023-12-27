//------------------------------------------------------------------------------
// VERSION 0.9.1
//
// LICENSE
//   This software is dual-licensed to the public domain and under the following
//   license: you are granted a perpetual, irrevocable license to copy, modify,
//   publish, and distribute this file as you see fit.
//
// CREDITS
//   Written by Michal Cichon
//------------------------------------------------------------------------------
# include "imgui_node_editor_internal.h"
# include <cstdio> // snprintf
# include <string>
# include <fstream>
# include <bitset>
# include <climits>
# include <algorithm>
# include <sstream>
# include <streambuf>
# include <type_traits>

// https://stackoverflow.com/a/8597498
# define DECLARE_HAS_NESTED(Name, Member)                                          \
                                                                                   \
    template<class T>                                                              \
    struct has_nested_ ## Name                                                     \
    {                                                                              \
        typedef char yes;                                                          \
        typedef yes(&no)[2];                                                       \
                                                                                   \
        template<class U> static yes test(decltype(U::Member)*);                   \
        template<class U> static no  test(...);                                    \
                                                                                   \
        static bool const value = sizeof(test<T>(0)) == sizeof(yes);               \
    };


namespace ax {
namespace NodeEditor {
namespace Detail {

# if !defined(IMGUI_VERSION_NUM) || (IMGUI_VERSION_NUM < 18822)
# define DECLARE_KEY_TESTER(Key)                                                                    \
    DECLARE_HAS_NESTED(Key, Key)                                                                    \
    struct KeyTester_ ## Key                                                                        \
    {                                                                                               \
        template <typename T>                                                                       \
        static int Get(typename std::enable_if<has_nested_ ## Key<ImGuiKey_>::value, T>::type*)     \
        {                                                                                           \
            return ImGui::GetKeyIndex(T::Key);                                                      \
        }                                                                                           \
                                                                                                    \
        template <typename T>                                                                       \
        static int Get(typename std::enable_if<!has_nested_ ## Key<ImGuiKey_>::value, T>::type*)    \
        {                                                                                           \
            return -1;                                                                              \
        }                                                                                           \
    }

DECLARE_KEY_TESTER(ImGuiKey_F);
DECLARE_KEY_TESTER(ImGuiKey_D);

static inline int GetKeyIndexForF()
{
    return KeyTester_ImGuiKey_F::Get<ImGuiKey_>(nullptr);
}

static inline int GetKeyIndexForD()
{
    return KeyTester_ImGuiKey_D::Get<ImGuiKey_>(nullptr);
}
# else
static inline ImGuiKey GetKeyIndexForF()
{
    return ImGuiKey_F;
}

static inline ImGuiKey GetKeyIndexForD()
{
    return ImGuiKey_D;
}
# endif

} // namespace Detail
} // namespace NodeEditor
} // namespace ax


//------------------------------------------------------------------------------
namespace ed = ax::NodeEditor::Detail;


//------------------------------------------------------------------------------
static const int c_BackgroundChannelCount = 1;
static const int c_LinkChannelCount       = 4;
static const int c_UserLayersCount        = 5;

static const int c_UserLayerChannelStart  = 0;
static const int c_BackgroundChannelStart = c_UserLayerChannelStart  + c_UserLayersCount;
static const int c_LinkStartChannel       = c_BackgroundChannelStart + c_BackgroundChannelCount;
static const int c_NodeStartChannel       = c_LinkStartChannel       + c_LinkChannelCount;

static const int c_BackgroundChannel_SelectionRect = c_BackgroundChannelStart + 0;

static const int c_UserChannel_Content         = c_UserLayerChannelStart + 1;
static const int c_UserChannel_Grid            = c_UserLayerChannelStart + 2;
static const int c_UserChannel_HintsBackground = c_UserLayerChannelStart + 3;
static const int c_UserChannel_Hints           = c_UserLayerChannelStart + 4;

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

static const float c_GroupSelectThickness       = 6.0f;  // canvas pixels
static const float c_LinkSelectThickness        = 5.0f;  // canvas pixels
static const float c_NavigationZoomMargin       = 0.1f;  // percentage of visible bounds
static const float c_MouseZoomDuration          = 0.15f; // seconds
static const float c_SelectionFadeOutDuration   = 0.15f; // seconds

static const auto  c_MaxMoveOverEdgeSpeed       = 10.0f;
static const auto  c_MaxMoveOverEdgeDistance    = 300.0f;

#if IMGUI_VERSION_NUM > 18101
static const auto  c_AllRoundCornersFlags = ImDrawFlags_RoundCornersAll;
#else
static const auto  c_AllRoundCornersFlags = 15;
#endif


//------------------------------------------------------------------------------
# if defined(_DEBUG) && defined(_WIN32)
extern "C" __declspec(dllimport) void __stdcall OutputDebugStringA(const char* string);

static void LogV(const char* fmt, va_list args)
{
    const int buffer_size = 1024;
    static char buffer[1024];

    vsnprintf(buffer, buffer_size - 1, fmt, args);
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
static void ImDrawListSplitter_Grow(ImDrawList* draw_list, ImDrawListSplitter* splitter, int channels_count)
{
    IM_ASSERT(splitter != nullptr);
    IM_ASSERT(splitter->_Count <= channels_count);

    if (splitter->_Count == 1)
    {
        splitter->Split(draw_list, channels_count);
        return;
    }

    int old_channels_count = splitter->_Channels.Size;
    if (old_channels_count < channels_count)
    {
        splitter->_Channels.reserve(channels_count);
        splitter->_Channels.resize(channels_count);
    }
    int old_used_channels_count = splitter->_Count;
    splitter->_Count = channels_count;

    for (int i = old_used_channels_count; i < channels_count; i++)
    {
        if (i >= old_channels_count)
        {
            IM_PLACEMENT_NEW(&splitter->_Channels[i]) ImDrawChannel();
        }
        else
        {
            splitter->_Channels[i]._CmdBuffer.resize(0);
            splitter->_Channels[i]._IdxBuffer.resize(0);
        }
    }
}

static void ImDrawList_ChannelsGrow(ImDrawList* draw_list, int channels_count)
{
    ImDrawListSplitter_Grow(draw_list, &draw_list->_Splitter, channels_count);
}

static void ImDrawListSplitter_SwapChannels(ImDrawListSplitter* splitter, int left, int right)
{
    IM_ASSERT(left < splitter->_Count && right < splitter->_Count);
    if (left == right)
        return;

    auto currentChannel = splitter->_Current;

    auto* leftCmdBuffer  = &splitter->_Channels[left]._CmdBuffer;
    auto* leftIdxBuffer  = &splitter->_Channels[left]._IdxBuffer;
    auto* rightCmdBuffer = &splitter->_Channels[right]._CmdBuffer;
    auto* rightIdxBuffer = &splitter->_Channels[right]._IdxBuffer;

    leftCmdBuffer->swap(*rightCmdBuffer);
    leftIdxBuffer->swap(*rightIdxBuffer);

    if (currentChannel == left)
        splitter->_Current = right;
    else if (currentChannel == right)
        splitter->_Current = left;
}

static void ImDrawList_SwapChannels(ImDrawList* drawList, int left, int right)
{
    ImDrawListSplitter_SwapChannels(&drawList->_Splitter, left, right);
}

static void ImDrawList_SwapSplitter(ImDrawList* drawList, ImDrawListSplitter& splitter)
{
    auto& currentSplitter = drawList->_Splitter;

    std::swap(currentSplitter._Current, splitter._Current);
    std::swap(currentSplitter._Count, splitter._Count);
    currentSplitter._Channels.swap(splitter._Channels);
}

//static void ImDrawList_TransformChannel_Inner(ImVector<ImDrawVert>& vtxBuffer, const ImVector<ImDrawIdx>& idxBuffer, const ImVector<ImDrawCmd>& cmdBuffer, const ImVec2& preOffset, const ImVec2& scale, const ImVec2& postOffset)
//{
//    auto idxRead = idxBuffer.Data;
//
//    int indexOffset = 0;
//    for (auto& cmd : cmdBuffer)
//    {
//        auto idxCount = cmd.ElemCount;
//
//        if (idxCount == 0) continue;
//
//        auto minIndex = idxRead[indexOffset];
//        auto maxIndex = idxRead[indexOffset];
//
//        for (auto i = 1u; i < idxCount; ++i)
//        {
//            auto idx = idxRead[indexOffset + i];
//            minIndex = std::min(minIndex, idx);
//            maxIndex = ImMax(maxIndex, idx);
//        }
//
//        for (auto vtx = vtxBuffer.Data + minIndex, vtxEnd = vtxBuffer.Data + maxIndex + 1; vtx < vtxEnd; ++vtx)
//        {
//            vtx->pos.x = (vtx->pos.x + preOffset.x) * scale.x + postOffset.x;
//            vtx->pos.y = (vtx->pos.y + preOffset.y) * scale.y + postOffset.y;
//        }
//
//        indexOffset += idxCount;
//    }
//}

//static void ImDrawList_TransformChannels(ImDrawList* drawList, int begin, int end, const ImVec2& preOffset, const ImVec2& scale, const ImVec2& postOffset)
//{
//    int lastCurrentChannel = drawList->_ChannelsCurrent;
//    if (lastCurrentChannel != 0)
//        drawList->ChannelsSetCurrent(0);
//
//    auto& vtxBuffer = drawList->VtxBuffer;
//
//    if (begin == 0 && begin != end)
//    {
//        ImDrawList_TransformChannel_Inner(vtxBuffer, drawList->IdxBuffer, drawList->CmdBuffer, preOffset, scale, postOffset);
//        ++begin;
//    }
//
//    for (int channelIndex = begin; channelIndex < end; ++channelIndex)
//    {
//        auto& channel = drawList->_Channels[channelIndex];
//        ImDrawList_TransformChannel_Inner(vtxBuffer, channel.IdxBuffer, channel.CmdBuffer, preOffset, scale, postOffset);
//    }
//
//    if (lastCurrentChannel != 0)
//        drawList->ChannelsSetCurrent(lastCurrentChannel);
//}

//static void ImDrawList_ClampClipRects_Inner(ImVector<ImDrawCmd>& cmdBuffer, const ImVec4& clipRect, const ImVec2& offset)
//{
//    for (auto& cmd : cmdBuffer)
//    {
//        cmd.ClipRect.x = ImMax(cmd.ClipRect.x + offset.x, clipRect.x);
//        cmd.ClipRect.y = ImMax(cmd.ClipRect.y + offset.y, clipRect.y);
//        cmd.ClipRect.z = std::min(cmd.ClipRect.z + offset.x, clipRect.z);
//        cmd.ClipRect.w = std::min(cmd.ClipRect.w + offset.y, clipRect.w);
//    }
//}

//static void ImDrawList_TranslateAndClampClipRects(ImDrawList* drawList, int begin, int end, const ImVec2& offset)
//{
//    int lastCurrentChannel = drawList->_ChannelsCurrent;
//    if (lastCurrentChannel != 0)
//        drawList->ChannelsSetCurrent(0);
//
//    auto clipRect = drawList->_ClipRectStack.back();
//
//    if (begin == 0 && begin != end)
//    {
//        ImDrawList_ClampClipRects_Inner(drawList->CmdBuffer, clipRect, offset);
//        ++begin;
//    }
//
//    for (int channelIndex = begin; channelIndex < end; ++channelIndex)
//    {
//        auto& channel = drawList->_Channels[channelIndex];
//        ImDrawList_ClampClipRects_Inner(channel.CmdBuffer, clipRect, offset);
//    }
//
//    if (lastCurrentChannel != 0)
//        drawList->ChannelsSetCurrent(lastCurrentChannel);
//}

static void ImDrawList_PathBezierOffset(ImDrawList* drawList, float offset, const ImVec2& p0, const ImVec2& p1, const ImVec2& p2, const ImVec2& p3)
{
    using namespace ed;

    auto acceptPoint = [drawList, offset](const ImCubicBezierSubdivideSample& r)
    {
        drawList->PathLineTo(r.Point + ImNormalized(ImVec2(-r.Tangent.y, r.Tangent.x)) * offset);
    };

    ImCubicBezierSubdivide(acceptPoint, p0, p1, p2, p3);
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

static void ImDrawList_AddBezierWithArrows(ImDrawList* drawList, const ImCubicBezierPoints& curve, float thickness,
    float startArrowSize, float startArrowWidth, float endArrowSize, float endArrowWidth,
    bool fill, ImU32 color, float strokeThickness, const ImVec2* startDirHint = nullptr, const ImVec2* endDirHint = nullptr)
{
    using namespace ax;

    if ((color >> 24) == 0)
        return;

    const auto half_thickness = thickness * 0.5f;

    if (fill)
    {
        drawList->AddBezierCubic(curve.P0, curve.P1, curve.P2, curve.P3, color, thickness);

        if (startArrowSize > 0.0f)
        {
            const auto start_dir  = ImNormalized(startDirHint ? *startDirHint : ImCubicBezierTangent(curve.P0, curve.P1, curve.P2, curve.P3, 0.0f));
            const auto start_n    = ImVec2(-start_dir.y, start_dir.x);
            const auto half_width = startArrowWidth * 0.5f;
            const auto tip        = curve.P0 - start_dir * startArrowSize;

            drawList->PathLineTo(curve.P0 - start_n * ImMax(half_width, half_thickness));
            drawList->PathLineTo(curve.P0 + start_n * ImMax(half_width, half_thickness));
            drawList->PathLineTo(tip);
            drawList->PathFillConvex(color);
        }

        if (endArrowSize > 0.0f)
        {
            const auto    end_dir = ImNormalized(endDirHint ? -*endDirHint : ImCubicBezierTangent(curve.P0, curve.P1, curve.P2, curve.P3, 1.0f));
            const auto    end_n   = ImVec2(  -end_dir.y,   end_dir.x);
            const auto half_width = endArrowWidth * 0.5f;
            const auto tip        = curve.P3 + end_dir * endArrowSize;

            drawList->PathLineTo(curve.P3 + end_n * ImMax(half_width, half_thickness));
            drawList->PathLineTo(curve.P3 - end_n * ImMax(half_width, half_thickness));
            drawList->PathLineTo(tip);
            drawList->PathFillConvex(color);
        }
    }
    else
    {
        if (startArrowSize > 0.0f)
        {
            const auto start_dir  = ImNormalized(ImCubicBezierTangent(curve.P0, curve.P1, curve.P2, curve.P3, 0.0f));
            const auto start_n    = ImVec2(-start_dir.y, start_dir.x);
            const auto half_width = startArrowWidth * 0.5f;
            const auto tip        = curve.P0 - start_dir * startArrowSize;

            if (half_width > half_thickness)
                drawList->PathLineTo(curve.P0 - start_n * half_width);
            drawList->PathLineTo(tip);
            if (half_width > half_thickness)
                drawList->PathLineTo(curve.P0 + start_n * half_width);
        }

        ImDrawList_PathBezierOffset(drawList, half_thickness, curve.P0, curve.P1, curve.P2, curve.P3);

        if (endArrowSize > 0.0f)
        {
            const auto    end_dir = ImNormalized(ImCubicBezierTangent(curve.P0, curve.P1, curve.P2, curve.P3, 1.0f));
            const auto    end_n   = ImVec2(  -end_dir.y,   end_dir.x);
            const auto half_width = endArrowWidth * 0.5f;
            const auto tip        = curve.P3 + end_dir * endArrowSize;

            if (half_width > half_thickness)
                drawList->PathLineTo(curve.P3 + end_n * half_width);
            drawList->PathLineTo(tip);
            if (half_width > half_thickness)
                drawList->PathLineTo(curve.P3 - end_n * half_width);
        }

        ImDrawList_PathBezierOffset(drawList, half_thickness, curve.P3, curve.P2, curve.P1, curve.P0);

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

        drawList->AddRectFilled(m_Bounds.Min, m_Bounds.Max,
            m_Color, m_Rounding, m_Corners);

        if (m_BorderWidth > 0.0f)
        {
            FringeScaleScope fringe(1.0f);
            drawList->AddRect(m_Bounds.Min, m_Bounds.Max,
                m_BorderColor, m_Rounding, m_Corners, m_BorderWidth);
        }

        if (!Editor->IsSelected(m_Node))
            m_Node->Draw(drawList, flags);
    }
}

ImVec2 ed::Pin::GetClosestPoint(const ImVec2& p) const
{
    auto pivot  = m_Pivot;
    auto extent = m_Radius + m_ArrowSize;

    if (m_SnapLinkToDir && extent > 0.0f)
    {
        pivot.Min += m_Dir * extent;
        pivot.Max += m_Dir * extent;

        extent = 0;
    }

    return ImRect_ClosestPoint(pivot, p, true, extent);
}

ImLine ed::Pin::GetClosestLine(const Pin* pin) const
{
    auto pivotA  =      m_Pivot;
    auto pivotB  = pin->m_Pivot;
    auto extentA =      m_Radius +      m_ArrowSize;
    auto extentB = pin->m_Radius + pin->m_ArrowSize;

    if (m_SnapLinkToDir && extentA > 0.0f)
    {
        pivotA.Min += m_Dir * extentA;
        pivotA.Max += m_Dir * extentA;

        extentA = 0;
    }

    if (pin->m_SnapLinkToDir && extentB > 0.0f)
    {
        pivotB.Min += pin->m_Dir * extentB;
        pivotB.Max += pin->m_Dir * extentB;

        extentB = 0;
    }

    return ImRect_ClosestLine(pivotA, pivotB, extentA, extentB);
}




//------------------------------------------------------------------------------
//
// Node
//
//------------------------------------------------------------------------------
bool ed::Node::AcceptDrag()
{
    m_DragStart = m_Bounds.Min;
    return true;
}

void ed::Node::UpdateDrag(const ImVec2& offset)
{
    auto size = m_Bounds.GetSize();
    m_Bounds.Min = ImFloor(m_DragStart + offset);
    m_Bounds.Max = m_Bounds.Min + size;
}

bool ed::Node::EndDrag()
{
    return m_Bounds.Min != m_DragStart;
}

void ed::Node::Draw(ImDrawList* drawList, DrawFlags flags)
{
    if (flags == Detail::Object::None)
    {
        drawList->ChannelsSetCurrent(m_Channel + c_NodeBackgroundChannel);

        drawList->AddRectFilled(
            m_Bounds.Min,
            m_Bounds.Max,
            m_Color, m_Rounding);

        if (IsGroup(this))
        {
            drawList->AddRectFilled(
                m_GroupBounds.Min,
                m_GroupBounds.Max,
                m_GroupColor, m_GroupRounding);

            if (m_GroupBorderWidth > 0.0f)
            {
                FringeScaleScope fringe(1.0f);

                drawList->AddRect(
                    m_GroupBounds.Min,
                    m_GroupBounds.Max,
                    m_GroupBorderColor, m_GroupRounding, c_AllRoundCornersFlags, m_GroupBorderWidth);
            }
        }

# if 0
        // #debug: highlight group regions
        auto drawRect = [drawList](const ImRect& rect, ImU32 color)
        {
            if (ImRect_IsEmpty(rect)) return;
            drawList->AddRectFilled(rect.Min, rect.Max, color);
        };

        drawRect(GetRegionBounds(NodeRegion::Top), IM_COL32(255, 0, 0, 64));
        drawRect(GetRegionBounds(NodeRegion::Bottom), IM_COL32(255, 0, 0, 64));
        drawRect(GetRegionBounds(NodeRegion::Left), IM_COL32(0, 255, 0, 64));
        drawRect(GetRegionBounds(NodeRegion::Right), IM_COL32(0, 255, 0, 64));
        drawRect(GetRegionBounds(NodeRegion::TopLeft), IM_COL32(255, 0, 255, 64));
        drawRect(GetRegionBounds(NodeRegion::TopRight), IM_COL32(255, 0, 255, 64));
        drawRect(GetRegionBounds(NodeRegion::BottomLeft), IM_COL32(255, 0, 255, 64));
        drawRect(GetRegionBounds(NodeRegion::BottomRight), IM_COL32(255, 0, 255, 64));
        drawRect(GetRegionBounds(NodeRegion::Center), IM_COL32(0, 0, 255, 64));
        drawRect(GetRegionBounds(NodeRegion::Header), IM_COL32(0, 255, 255, 64));
# endif

        DrawBorder(drawList, m_BorderColor, m_BorderWidth);
    }
    else if (flags & Selected)
    {
        const auto  borderColor = Editor->GetColor(StyleColor_SelNodeBorder);
        const auto& editorStyle = Editor->GetStyle();

        drawList->ChannelsSetCurrent(m_Channel + c_NodeBaseChannel);

        DrawBorder(drawList, borderColor, editorStyle.SelectedNodeBorderWidth, editorStyle.SelectedNodeBorderOffset);
    }
    else if (!IsGroup(this) && (flags & Hovered))
    {
        const auto  borderColor = Editor->GetColor(StyleColor_HovNodeBorder);
        const auto& editorStyle = Editor->GetStyle();

        drawList->ChannelsSetCurrent(m_Channel + c_NodeBaseChannel);

        DrawBorder(drawList, borderColor, editorStyle.HoveredNodeBorderWidth, editorStyle.HoverNodeBorderOffset);
    }
}

void ed::Node::DrawBorder(ImDrawList* drawList, ImU32 color, float thickness, float offset)
{
    if (thickness > 0.0f)
    {
        const ImVec2 extraOffset = ImVec2(offset, offset);

        drawList->AddRect(m_Bounds.Min - extraOffset, m_Bounds.Max + extraOffset,
            color, ImMax(0.0f, m_Rounding + offset), c_AllRoundCornersFlags, thickness);
    }
}

void ed::Node::GetGroupedNodes(std::vector<Node*>& result, bool append)
{
    if (!append)
        result.resize(0);

    if (!IsGroup(this))
        return;

    const auto firstNodeIndex = result.size();
    Editor->FindNodesInRect(m_GroupBounds, result, true, false);

    for (auto index = firstNodeIndex; index < result.size(); ++index)
        result[index]->GetGroupedNodes(result, true);
}

ImRect ed::Node::GetRegionBounds(NodeRegion region) const
{
    if (m_Type == NodeType::Node)
    {
        if (region == NodeRegion::Header)
            return m_Bounds;
    }
    else if (m_Type == NodeType::Group)
    {
        const float activeAreaMinimumSize = ImMax(ImMax(
            Editor->GetView().InvScale * c_GroupSelectThickness,
            m_GroupBorderWidth), c_GroupSelectThickness);
        const float minimumSize = activeAreaMinimumSize * 5;

        auto bounds = m_Bounds;
        if (bounds.GetWidth() < minimumSize)
            bounds.Expand(ImVec2(minimumSize - bounds.GetWidth(), 0.0f));
        if (bounds.GetHeight() < minimumSize)
            bounds.Expand(ImVec2(0.0f, minimumSize - bounds.GetHeight()));

        if (region == NodeRegion::Top)
        {
            bounds.Max.y = bounds.Min.y + activeAreaMinimumSize;
            bounds.Min.x += activeAreaMinimumSize;
            bounds.Max.x -= activeAreaMinimumSize;
            return bounds;
        }
        else if (region == NodeRegion::Bottom)
        {
            bounds.Min.y = bounds.Max.y - activeAreaMinimumSize;
            bounds.Min.x += activeAreaMinimumSize;
            bounds.Max.x -= activeAreaMinimumSize;
            return bounds;
        }
        else if (region == NodeRegion::Left)
        {
            bounds.Max.x = bounds.Min.x + activeAreaMinimumSize;
            bounds.Min.y += activeAreaMinimumSize;
            bounds.Max.y -= activeAreaMinimumSize;
            return bounds;
        }
        else if (region == NodeRegion::Right)
        {
            bounds.Min.x = bounds.Max.x - activeAreaMinimumSize;
            bounds.Min.y += activeAreaMinimumSize;
            bounds.Max.y -= activeAreaMinimumSize;
            return bounds;
        }
        else if (region == NodeRegion::TopLeft)
        {
            bounds.Max.x = bounds.Min.x + activeAreaMinimumSize * 2;
            bounds.Max.y = bounds.Min.y + activeAreaMinimumSize * 2;
            return bounds;
        }
        else if (region == NodeRegion::TopRight)
        {
            bounds.Min.x = bounds.Max.x - activeAreaMinimumSize * 2;
            bounds.Max.y = bounds.Min.y + activeAreaMinimumSize * 2;
            return bounds;
        }
        else if (region == NodeRegion::BottomRight)
        {
            bounds.Min.x = bounds.Max.x - activeAreaMinimumSize * 2;
            bounds.Min.y = bounds.Max.y - activeAreaMinimumSize * 2;
            return bounds;
        }
        else if (region == NodeRegion::BottomLeft)
        {
            bounds.Max.x = bounds.Min.x + activeAreaMinimumSize * 2;
            bounds.Min.y = bounds.Max.y - activeAreaMinimumSize * 2;
            return bounds;
        }
        else if (region == NodeRegion::Header)
        {
            bounds.Min.x += activeAreaMinimumSize;
            bounds.Max.x -= activeAreaMinimumSize;
            bounds.Min.y += activeAreaMinimumSize;
            bounds.Max.y  = ImMax(bounds.Min.y + activeAreaMinimumSize, m_GroupBounds.Min.y);
            return bounds;
        }
        else if (region == NodeRegion::Center)
        {
            bounds.Max.x -= activeAreaMinimumSize;
            bounds.Min.y  = ImMax(bounds.Min.y + activeAreaMinimumSize, m_GroupBounds.Min.y);
            bounds.Min.x += activeAreaMinimumSize;
            bounds.Max.y -= activeAreaMinimumSize;
            return bounds;
        }
    }

    return ImRect();
}

ed::NodeRegion ed::Node::GetRegion(const ImVec2& point) const
{
    if (m_Type == NodeType::Node)
    {
        if (m_Bounds.Contains(point))
            return NodeRegion::Header;
        else
            return NodeRegion::None;
    }
    else if (m_Type == NodeType::Group)
    {
        static const NodeRegion c_Regions[] =
        {
            // Corners first, they may overlap other regions.
            NodeRegion::TopLeft,
            NodeRegion::TopRight,
            NodeRegion::BottomLeft,
            NodeRegion::BottomRight,
            NodeRegion::Header,
            NodeRegion::Top,
            NodeRegion::Bottom,
            NodeRegion::Left,
            NodeRegion::Right,
            NodeRegion::Center
        };

        for (auto region : c_Regions)
        {
            auto bounds = GetRegionBounds(region);
            if (bounds.Contains(point))
                return region;
        }
    }

    return NodeRegion::None;
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
    else if (flags & Highlighted)
    {
        drawList->ChannelsSetCurrent(c_LinkChannel_Selection);

        Draw(drawList, m_HighlightColor, 3.5f);
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
        true, color, 1.0f,
        m_StartPin && m_StartPin->m_SnapLinkToDir ? &m_StartPin->m_Dir : nullptr,
        m_EndPin   &&   m_EndPin->m_SnapLinkToDir ?   &m_EndPin->m_Dir : nullptr);
}

void ed::Link::UpdateEndpoints()
{
    const auto line = m_StartPin->GetClosestLine(m_EndPin);
    m_Start = line.A;
    m_End   = line.B;
}

ImCubicBezierPoints ed::Link::GetCurve() const
{
    auto easeLinkStrength = [](const ImVec2& a, const ImVec2& b, float strength)
    {
        const auto distanceX    = b.x - a.x;
        const auto distanceY    = b.y - a.y;
        const auto distance     = ImSqrt(distanceX * distanceX + distanceY * distanceY);
        const auto halfDistance = distance * 0.5f;

        if (halfDistance < strength)
            strength = strength * ImSin(IM_PI * 0.5f * halfDistance / strength);

        return strength;
    };

    const auto startStrength = easeLinkStrength(m_Start, m_End, m_StartPin->m_Strength);
    const auto   endStrength = easeLinkStrength(m_Start, m_End,   m_EndPin->m_Strength);
    const auto           cp0 = m_Start + m_StartPin->m_Dir * startStrength;
    const auto           cp1 =   m_End +   m_EndPin->m_Dir *   endStrength;

    ImCubicBezierPoints result;
    result.P0 = m_Start;
    result.P1 = cp0;
    result.P2 = cp1;
    result.P3 = m_End;

    return result;
}

bool ed::Link::TestHit(const ImVec2& point, float extraThickness) const
{
    if (!m_IsLive)
        return false;

    auto bounds = GetBounds();
    if (extraThickness > 0.0f)
        bounds.Expand(extraThickness);

    if (!bounds.Contains(point))
        return false;

    const auto bezier = GetCurve();
    const auto result = ImProjectOnCubicBezier(point, bezier.P0, bezier.P1, bezier.P2, bezier.P3, 50);

    return result.Distance <= m_Thickness + extraThickness;
}

bool ed::Link::TestHit(const ImRect& rect, bool allowIntersect) const
{
    if (!m_IsLive)
        return false;

    const auto bounds = GetBounds();

    if (rect.Contains(bounds))
        return true;

    if (!allowIntersect || !rect.Overlaps(bounds))
        return false;

    const auto bezier = GetCurve();

    const auto p0 = rect.GetTL();
    const auto p1 = rect.GetTR();
    const auto p2 = rect.GetBR();
    const auto p3 = rect.GetBL();

    if (ImCubicBezierLineIntersect(bezier.P0, bezier.P1, bezier.P2, bezier.P3, p0, p1).Count > 0)
        return true;
    if (ImCubicBezierLineIntersect(bezier.P0, bezier.P1, bezier.P2, bezier.P3, p1, p2).Count > 0)
        return true;
    if (ImCubicBezierLineIntersect(bezier.P0, bezier.P1, bezier.P2, bezier.P3, p2, p3).Count > 0)
        return true;
    if (ImCubicBezierLineIntersect(bezier.P0, bezier.P1, bezier.P2, bezier.P3, p3, p0).Count > 0)
        return true;

    return false;
}

ImRect ed::Link::GetBounds() const
{
    if (m_IsLive)
    {
        const auto curve = GetCurve();
        auto bounds = ImCubicBezierBoundingRect(curve.P0, curve.P1, curve.P2, curve.P3);

        if (bounds.GetWidth() == 0.0f)
        {
            bounds.Min.x -= 0.5f;
            bounds.Max.x += 0.5f;
        }

        if (bounds.GetHeight() == 0.0f)
        {
            bounds.Min.y -= 0.5f;
            bounds.Max.y += 0.5f;
        }

        if (m_StartPin->m_ArrowSize)
        {
            const auto start_dir = ImNormalized(ImCubicBezierTangent(curve.P0, curve.P1, curve.P2, curve.P3, 0.0f));
            const auto p0 = curve.P0;
            const auto p1 = curve.P0 - start_dir * m_StartPin->m_ArrowSize;
            const auto min = ImMin(p0, p1);
            const auto max = ImMax(p0, p1);
            auto arrowBounds = ImRect(min, ImMax(max, min + ImVec2(1, 1)));
            bounds.Add(arrowBounds);
        }

        if (m_EndPin->m_ArrowSize)
        {
            const auto end_dir = ImNormalized(ImCubicBezierTangent(curve.P0, curve.P1, curve.P2, curve.P3, 1.0f));
            const auto p0 = curve.P3;
            const auto p1 = curve.P3 + end_dir * m_EndPin->m_ArrowSize;
            const auto min = ImMin(p0, p1);
            const auto max = ImMax(p0, p1);
            auto arrowBounds = ImRect(min, ImMax(max, min + ImVec2(1, 1)));
            bounds.Add(arrowBounds);
        }

        return bounds;
    }
    else
        return ImRect();
}




//------------------------------------------------------------------------------
//
// Editor Context
//
//------------------------------------------------------------------------------
ed::EditorContext::EditorContext(const ax::NodeEditor::Config* config)
    : m_Config(config)
    , m_EditorActiveId(0)
    , m_IsFirstFrame(true)
    , m_IsFocused(false)
    , m_IsHovered(false)
    , m_IsHoveredWithoutOverlapp(false)
    , m_ShortcutsEnabled(true)
    , m_Style()
    , m_Nodes()
    , m_Pins()
    , m_Links()
    , m_SelectionId(1)
    , m_LastActiveLink(nullptr)
    , m_Canvas()
    , m_IsCanvasVisible(false)
    , m_NodeBuilder(this)
    , m_HintBuilder(this)
    , m_CurrentAction(nullptr)
    , m_NavigateAction(this, m_Canvas)
    , m_SizeAction(this)
    , m_DragAction(this)
    , m_SelectAction(this)
    , m_ContextMenuAction(this)
    , m_ShortcutAction(this)
    , m_CreateItemAction(this)
    , m_DeleteItemsAction(this)
    , m_AnimationControllers{ &m_FlowAnimationController }
    , m_FlowAnimationController(this)
    , m_HoveredNode(0)
    , m_HoveredPin(0)
    , m_HoveredLink(0)
    , m_DoubleClickedNode(0)
    , m_DoubleClickedPin(0)
    , m_DoubleClickedLink(0)
    , m_BackgroundClickButtonIndex(-1)
    , m_BackgroundDoubleClickButtonIndex(-1)
    , m_IsInitialized(false)
    , m_Settings()
    , m_DrawList(nullptr)
    , m_ExternalChannel(0)
{
}

ed::EditorContext::~EditorContext()
{
    if (m_IsInitialized)
        SaveSettings();

    for (auto link  : m_Links)  delete link.m_Object;
    for (auto pin   : m_Pins)   delete pin.m_Object;
    for (auto node  : m_Nodes)  delete node.m_Object;

    m_Splitter.ClearFreeMemory();
}

void ed::EditorContext::Begin(const char* id, const ImVec2& size)
{
    m_EditorActiveId = ImGui::GetID(id);
    ImGui::PushID(id);

    auto availableContentSize = ImGui::GetContentRegionAvail();
    ImVec2 canvasSize = ImFloor(size);
    if (canvasSize.x <= 0.0f)
        canvasSize.x = ImMax(4.0f, availableContentSize.x);
    if (canvasSize.y <= 0.0f)
        canvasSize.y = ImMax(4.0f, availableContentSize.y);

    if (!m_IsInitialized)
    {
        // Cycle canvas, so it has a chance to initialize its size before settings are loaded
        if (m_Canvas.Begin(id, canvasSize))
            m_Canvas.End();

        LoadSettings();
        m_IsInitialized = true;
    }

    //ImGui::LogToClipboard();
    //Log("---- begin ----");

    static auto resetAndCollect = [](auto& objects)
    {
        objects.erase(std::remove_if(objects.begin(), objects.end(), [](auto objectWrapper)
        {
            if (objectWrapper->m_DeleteOnNewFrame)
            {
                delete objectWrapper.m_Object;
                return true;
            }
            else
            {
                objectWrapper->Reset();
                return false;
            }
        }), objects.end());
    };

    resetAndCollect(m_Nodes);
    resetAndCollect(m_Pins);
    resetAndCollect(m_Links);

    m_DrawList = ImGui::GetWindowDrawList();

    ImDrawList_SwapSplitter(m_DrawList, m_Splitter);
    m_ExternalChannel = m_DrawList->_Splitter._Current;

    if (m_CurrentAction && m_CurrentAction->IsDragging() && m_NavigateAction.MoveOverEdge(canvasSize))
    {
        auto& io = ImGui::GetIO();
        auto offset = m_NavigateAction.GetMoveScreenOffset();
        for (int i = 0; i < 5; ++i)
            io.MouseClickedPos[i] = io.MouseClickedPos[i] - offset;
    }
    else
        m_NavigateAction.StopMoveOverEdge();

    auto previousSize        = m_Canvas.Rect().GetSize();
    auto previousVisibleRect = m_Canvas.ViewRect();
    m_IsCanvasVisible = m_Canvas.Begin(id, canvasSize);

    //ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    //ImGui::BeginChild(id, size, false,
    //    ImGuiWindowFlags_NoMove |
    //    ImGuiWindowFlags_NoScrollbar |
    //    ImGuiWindowFlags_NoScrollWithMouse);

    m_IsFocused = ImGui::IsWindowFocused();

    //
    m_NavigateAction.SetWindow(m_Canvas.ViewRect().Min, m_Canvas.ViewRect().GetSize());

    // Handle canvas size change. Scale to Y axis, center on X.
    if (!ImRect_IsEmpty(previousVisibleRect) && previousSize != canvasSize)
    {
        m_NavigateAction.FinishNavigation();

        auto centerX            = (previousVisibleRect.Max.x + previousVisibleRect.Min.x) * 0.5f;
        auto centerY            = (previousVisibleRect.Max.y + previousVisibleRect.Min.y) * 0.5f;
        auto currentVisibleRect = m_Canvas.ViewRect();
        auto currentAspectRatio = currentVisibleRect.GetHeight() ? (currentVisibleRect.GetWidth() / currentVisibleRect.GetHeight()) : 0.0f;
        auto width              = previousVisibleRect.GetWidth();
        auto height             = previousVisibleRect.GetHeight();

        if (m_Config.CanvasSizeMode == ax::NodeEditor::CanvasSizeMode::FitVerticalView)
        {
            height  = previousVisibleRect.GetHeight();
            width   = height * currentAspectRatio;
        }
        else if (m_Config.CanvasSizeMode == ax::NodeEditor::CanvasSizeMode::FitHorizontalView)
        {
            width   = previousVisibleRect.GetWidth();
            height  = width / currentAspectRatio;
        }
        else if (m_Config.CanvasSizeMode == ax::NodeEditor::CanvasSizeMode::CenterOnly)
        {
            width  = currentVisibleRect.GetWidth();
            height = currentVisibleRect.GetHeight();
        }

        previousVisibleRect.Min.x = centerX - 0.5f * width;
        previousVisibleRect.Max.x = centerX + 0.5f * width;
        previousVisibleRect.Min.y = centerY - 0.5f * height;
        previousVisibleRect.Max.y = centerY + 0.5f * height;

        m_NavigateAction.NavigateTo(previousVisibleRect, Detail::NavigateAction::ZoomMode::Exact, 0.0f);
    }

    m_Canvas.SetView(m_NavigateAction.GetView());

    // #debug #clip
    //ImGui::Text("CLIP = { x=%g y=%g w=%g h=%g r=%g b=%g }",
    //    clipMin.x, clipMin.y, clipMax.x - clipMin.x, clipMax.y - clipMin.y, clipMax.x, clipMax.y);

    // Reserve channels for background and links
    ImDrawList_ChannelsGrow(m_DrawList, c_NodeStartChannel);

    if (HasSelectionChanged())
        ++m_SelectionId;

    m_LastSelectedObjects = m_SelectedObjects;
}

void ed::EditorContext::End()
{
    //auto& io          = ImGui::GetIO();
    auto  control     = BuildControl(m_CurrentAction && m_CurrentAction->IsDragging()); // NavigateAction.IsMovingOverEdge()
    //auto& editorStyle = GetStyle();

    m_HoveredNode             = control.HotNode && m_CurrentAction == nullptr ? control.HotNode->m_ID : 0;
    m_HoveredPin              = control.HotPin  && m_CurrentAction == nullptr ? control.HotPin->m_ID  : 0;
    m_HoveredLink             = control.HotLink && m_CurrentAction == nullptr ? control.HotLink->m_ID : 0;
    m_DoubleClickedNode       = control.DoubleClickedNode ? control.DoubleClickedNode->m_ID : 0;
    m_DoubleClickedPin        = control.DoubleClickedPin  ? control.DoubleClickedPin->m_ID  : 0;
    m_DoubleClickedLink       = control.DoubleClickedLink ? control.DoubleClickedLink->m_ID : 0;
    m_BackgroundClickButtonIndex       = control.BackgroundClickButtonIndex;
    m_BackgroundDoubleClickButtonIndex = control.BackgroundDoubleClickButtonIndex;

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
            node->Draw(m_DrawList);

    // Draw links
    for (auto link : m_Links)
        if (link->m_IsLive && link->IsVisible())
            link->Draw(m_DrawList);

    // Highlight selected objects
    {
        auto selectedObjects = &m_SelectedObjects;
        if (auto selectAction = m_CurrentAction ? m_CurrentAction->AsSelect() : nullptr)
            selectedObjects = &selectAction->m_CandidateObjects;

        for (auto selectedObject : *selectedObjects)
        {
            if (selectedObject->IsVisible())
                selectedObject->Draw(m_DrawList, Object::Selected);
        }

        // Highlight adjacent links
        static auto isLinkHighlightedForPin = [](const Pin& pin)
        {
            return pin.m_Node->m_HighlightConnectedLinks && pin.m_Node->m_IsSelected;
        };

        for (auto& link : m_Links)
        {
            if (!link->m_IsLive || !link->IsVisible())
                continue;

            auto isLinkHighlighted = isLinkHighlightedForPin(*link->m_StartPin) || isLinkHighlightedForPin(*link->m_EndPin);
            if (!isLinkHighlighted)
                continue;

            link->Draw(m_DrawList, Object::Highlighted);
        }
    }

    if (!isSelecting)
    {
        auto hoveredObject = control.HotObject;
        if (auto dragAction = m_CurrentAction ? m_CurrentAction->AsDrag() : nullptr)
            hoveredObject = dragAction->m_DraggedObject;
        if (auto sizeAction = m_CurrentAction ? m_CurrentAction->AsSize() : nullptr)
            hoveredObject = sizeAction->m_SizedNode;

        if (hoveredObject && !IsSelected(hoveredObject) && hoveredObject->IsVisible())
            hoveredObject->Draw(m_DrawList, Object::Hovered);
    }

    // Draw animations
    for (auto controller : m_AnimationControllers)
        controller->Draw(m_DrawList);

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
            else if (/*!possibleAction &&*/ result == EditorAction::Possible)
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
        else if (accept(m_CreateItemAction))
            m_CurrentAction = &m_CreateItemAction;
        else if (accept(m_DeleteItemsAction))
            m_CurrentAction = &m_DeleteItemsAction;
        else if (accept(m_SelectAction))
            m_CurrentAction = &m_SelectAction;

        if (possibleAction)
            ImGui::SetMouseCursor(possibleAction->GetCursor());

        if (m_CurrentAction && possibleAction)
            possibleAction->Reject();
    }

    if (m_CurrentAction)
        ImGui::SetMouseCursor(m_CurrentAction->GetCursor());

    // Draw selection rectangle
    m_SelectAction.Draw(m_DrawList);

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
            const auto& lhsSize = lhs == m_SizeAction.m_SizedNode ? m_SizeAction.GetStartGroupBounds().GetSize() : lhs->m_GroupBounds.GetSize();
            const auto& rhsSize = rhs == m_SizeAction.m_SizedNode ? m_SizeAction.GetStartGroupBounds().GetSize() : rhs->m_GroupBounds.GetSize();

            const auto lhsArea = lhsSize.x * lhsSize.y;
            const auto rhsArea = rhsSize.x * rhsSize.y;

            return lhsArea > rhsArea;
        });
    }

    // Apply Z order
    std::stable_sort(m_Nodes.begin(), m_Nodes.end(), [](const auto& lhs, const auto& rhs)
    {
        return lhs->m_ZPosition < rhs->m_ZPosition;
    });

# if 1
    // Every node has few channels assigned. Grow channel list
    // to hold twice as much of channels and place them in
    // node drawing order.
    {
        // Copy group nodes
        auto liveNodeCount = static_cast<int>(std::count_if(m_Nodes.begin(), m_Nodes.end(), [](Node* node) { return node->m_IsLive; }));

        // Reserve two additional channels for sorted list of channels
        auto nodeChannelCount = m_DrawList->_Splitter._Count;
        ImDrawList_ChannelsGrow(m_DrawList, m_DrawList->_Splitter._Count + c_ChannelsPerNode * liveNodeCount + c_LinkChannelCount);

        int targetChannel = nodeChannelCount;

        auto copyNode = [this, &targetChannel](Node* node)
        {
            if (!node->m_IsLive)
                return;

            for (int i = 0; i < c_ChannelsPerNode; ++i)
                ImDrawList_SwapChannels(m_DrawList, node->m_Channel + i, targetChannel + i);

            node->m_Channel = targetChannel;
            targetChannel += c_ChannelsPerNode;
        };

        auto groupsItEnd = std::find_if(m_Nodes.begin(), m_Nodes.end(), [](Node* node) { return !IsGroup(node); });

        // Copy group nodes
        std::for_each(m_Nodes.begin(), groupsItEnd, copyNode);

        // Copy links
        for (int i = 0; i < c_LinkChannelCount; ++i, ++targetChannel)
            ImDrawList_SwapChannels(m_DrawList, c_LinkStartChannel + i, targetChannel);

        // Copy normal nodes
        std::for_each(groupsItEnd, m_Nodes.end(), copyNode);
    }
# endif

    // ImGui::PopClipRect();

    // Draw grid
# if 1 // #FIXME
    {
        //auto& style = ImGui::GetStyle();

        m_DrawList->ChannelsSetCurrent(c_UserChannel_Grid);

        ImVec2 offset    = m_Canvas.ViewOrigin() * (1.0f / m_Canvas.ViewScale());
        ImU32 GRID_COLOR = GetColor(StyleColor_Grid, ImClamp(m_Canvas.ViewScale() * m_Canvas.ViewScale(), 0.0f, 1.0f));
        float GRID_SX    = 32.0f;// * m_Canvas.ViewScale();
        float GRID_SY    = 32.0f;// * m_Canvas.ViewScale();
        ImVec2 VIEW_POS  = m_Canvas.ViewRect().Min;
        ImVec2 VIEW_SIZE = m_Canvas.ViewRect().GetSize();

        m_DrawList->AddRectFilled(VIEW_POS, VIEW_POS + VIEW_SIZE, GetColor(StyleColor_Bg));

        for (float x = fmodf(offset.x, GRID_SX); x < VIEW_SIZE.x; x += GRID_SX)
            m_DrawList->AddLine(ImVec2(x, 0.0f) + VIEW_POS, ImVec2(x, VIEW_SIZE.y) + VIEW_POS, GRID_COLOR);
        for (float y = fmodf(offset.y, GRID_SY); y < VIEW_SIZE.y; y += GRID_SY)
            m_DrawList->AddLine(ImVec2(0.0f, y) + VIEW_POS, ImVec2(VIEW_SIZE.x, y) + VIEW_POS, GRID_COLOR);
    }
# endif

# if 0
    {
        auto userChannel = drawList->_Splitter._Count;
        auto channelsToCopy = c_UserLayersCount;
        ImDrawList_ChannelsGrow(drawList, userChannel + channelsToCopy);
        for (int i = 0; i < channelsToCopy; ++i)
            ImDrawList_SwapChannels(drawList, userChannel + i, c_UserLayerChannelStart + i);
    }
# endif

# if 0
    {
        auto preOffset  = ImVec2(0, 0);
        auto postOffset = m_OldCanvas.WindowScreenPos + m_OldCanvas.ClientOrigin;
        auto scale      = m_OldCanvas.Zoom;

        ImDrawList_TransformChannels(drawList,                        0,                            1, preOffset, scale, postOffset);
        ImDrawList_TransformChannels(drawList, c_BackgroundChannelStart, drawList->_ChannelsCount - 1, preOffset, scale, postOffset);

        auto clipTranslation = m_OldCanvas.WindowScreenPos - m_OldCanvas.FromScreen(m_OldCanvas.WindowScreenPos);
        ImGui::PushClipRect(m_OldCanvas.WindowScreenPos + ImVec2(1, 1), m_OldCanvas.WindowScreenPos + m_OldCanvas.WindowScreenSize - ImVec2(1, 1), false);
        ImDrawList_TranslateAndClampClipRects(drawList,                        0,                            1, clipTranslation);
        ImDrawList_TranslateAndClampClipRects(drawList, c_BackgroundChannelStart, drawList->_ChannelsCount - 1, clipTranslation);
        ImGui::PopClipRect();

        // #debug: Static grid in local space
        //for (float x = 0; x < Canvas.WindowScreenSize.x; x += 100)
        //    drawList->AddLine(ImVec2(x, 0.0f) + Canvas.WindowScreenPos, ImVec2(x, Canvas.WindowScreenSize.y) + Canvas.WindowScreenPos, IM_COL32(255, 0, 0, 128));
        //for (float y = 0; y < Canvas.WindowScreenSize.y; y += 100)
        //    drawList->AddLine(ImVec2(0.0f, y) + Canvas.WindowScreenPos, ImVec2(Canvas.WindowScreenSize.x, y) + Canvas.WindowScreenPos, IM_COL32(255, 0, 0, 128));
    }
# endif

# if 1
    // Move user and hint channels to top
    {
        // Clip plane is transformed to global space.
        // These channels already have clip planes in global space, so
        // we move them to clip plane. Batch transformation in canvas
        // will bring them back to global space.
        auto preTransformClipRect = [this](int channelIndex)
        {
            ImDrawChannel& channel = m_DrawList->_Splitter._Channels[channelIndex];
            for (ImDrawCmd& cmd : channel._CmdBuffer)
            {
                auto a = ToCanvas(ImVec2(cmd.ClipRect.x, cmd.ClipRect.y));
                auto b = ToCanvas(ImVec2(cmd.ClipRect.z, cmd.ClipRect.w));
                cmd.ClipRect = ImVec4(a.x, a.y, b.x, b.y);
            }
        };

        m_DrawList->ChannelsSetCurrent(0);

        auto channelCount = m_DrawList->_Splitter._Count;
        ImDrawList_ChannelsGrow(m_DrawList, channelCount + 3);
        ImDrawList_SwapChannels(m_DrawList, c_UserChannel_HintsBackground, channelCount + 0);
        ImDrawList_SwapChannels(m_DrawList, c_UserChannel_Hints,           channelCount + 1);
        ImDrawList_SwapChannels(m_DrawList, c_UserChannel_Content,         channelCount + 2);

        preTransformClipRect(channelCount + 0);
        preTransformClipRect(channelCount + 1);
        preTransformClipRect(channelCount + 2);
    }
# endif

    UpdateAnimations();

    m_DrawList->ChannelsMerge();

    // #debug
    // drawList->AddRectFilled(ImVec2(-10.0f, -10.0f), ImVec2(10.0f, 10.0f), IM_COL32(255, 0, 255, 255));

    // ImGui::EndChild();
    // ImGui::PopStyleColor();
    if (m_IsCanvasVisible)
        m_Canvas.End();

    ImDrawList_SwapSplitter(m_DrawList, m_Splitter);

    // Draw border
    {
        auto& style = ImGui::GetStyle();
        auto borderShadoColor = style.Colors[ImGuiCol_BorderShadow];
        auto borderColor = style.Colors[ImGuiCol_Border];
        m_DrawList->AddRect(m_Canvas.Rect().Min + ImVec2(1, 1), m_Canvas.Rect().Max - ImVec2(1, 1), ImColor(borderShadoColor));
        m_DrawList->AddRect(m_Canvas.Rect().Min, m_Canvas.Rect().Max, ImColor(borderColor));
    }

    // #metrics
    // ShowMetrics(control);

    ImGui::PopID();

    if (!m_CurrentAction && m_IsFirstFrame && !m_Settings.m_Selection.empty())
    {
        ClearSelection();
        for (auto id : m_Settings.m_Selection)
            if (auto object = FindObject(id))
                SelectObject(object);
    }

    if (HasSelectionChanged())
        MakeDirty(SaveReasonFlags::Selection);

    if (m_Settings.m_IsDirty && !m_CurrentAction)
        SaveSettings();

    m_DrawList = nullptr;
    m_IsFirstFrame = false;
}

bool ed::EditorContext::DoLink(LinkId id, PinId startPinId, PinId endPinId, ImU32 color, float thickness)
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
    link->m_HighlightColor= GetColor(StyleColor_HighlightLinkBorder);
    link->m_Thickness     = thickness;
    link->m_IsLive        = true;

    link->UpdateEndpoints();

    return true;
}

void ed::EditorContext::SetNodePosition(NodeId nodeId, const ImVec2& position)
{
    auto node = FindNode(nodeId);
    if (!node)
    {
        node = CreateNode(nodeId);
        node->m_IsLive = false;
    }

    if (node->m_Bounds.Min != position)
    {
        node->m_Bounds.Translate(position - node->m_Bounds.Min);
        node->m_Bounds.Floor();
        MakeDirty(NodeEditor::SaveReasonFlags::Position, node);
    }
}

void ed::EditorContext::SetGroupSize(NodeId nodeId, const ImVec2& size)
{
    auto node = FindNode(nodeId);
    if (!node)
    {
        node = CreateNode(nodeId);
        node->m_IsLive = false;
    }

    node->m_Type = NodeType::Group;

    if (node->m_GroupBounds.GetSize() != size)
    {
        node->m_GroupBounds.Min = node->m_Bounds.Min;
        node->m_GroupBounds.Max = node->m_Bounds.Min + size;
        node->m_GroupBounds.Floor();
        MakeDirty(NodeEditor::SaveReasonFlags::Size, node);
    }
}

ImVec2 ed::EditorContext::GetNodePosition(NodeId nodeId)
{
    auto node = FindNode(nodeId);
    if (!node)
        return ImVec2(FLT_MAX, FLT_MAX);

    return node->m_Bounds.Min;
}

ImVec2 ed::EditorContext::GetNodeSize(NodeId nodeId)
{
    auto node = FindNode(nodeId);
    if (!node)
        return ImVec2(0, 0);

    return node->m_Bounds.GetSize();
}

void ed::EditorContext::SetNodeZPosition(NodeId nodeId, float z)
{
    auto node = FindNode(nodeId);
    if (!node)
    {
        node = CreateNode(nodeId);
        node->m_IsLive = false;
    }

    node->m_ZPosition = z;
}

float ed::EditorContext::GetNodeZPosition(NodeId nodeId)
{
    auto node = FindNode(nodeId);
    if (!node)
        return 0.0f;

    return node->m_ZPosition;
}

void ed::EditorContext::MarkNodeToRestoreState(Node* node)
{
    node->m_RestoreState = true;
}

void ed::EditorContext::UpdateNodeState(Node* node)
{
    bool tryLoadState = node->m_RestoreState;

    node->m_RestoreState = false;

    auto settings = m_Settings.FindNode(node->m_ID);
    if (!settings)
        return;

    if (!tryLoadState && settings->m_WasUsed)
        return;

    if (!settings->m_WasUsed)
    {
        MakeDirty(SaveReasonFlags::AddNode, node);
        settings->m_WasUsed = true;
    }

    // Load state from config (if possible)
    if (tryLoadState)
    {
        NodeSettings newSettings = *settings;
        if (NodeSettings::Parse(m_Config.LoadNode(node->m_ID), newSettings))
            *settings = newSettings;
    }

    node->m_Bounds.Min      = settings->m_Location;
    node->m_Bounds.Max      = node->m_Bounds.Min + settings->m_Size;
    node->m_Bounds.Floor();
    node->m_GroupBounds.Min = settings->m_Location;
    node->m_GroupBounds.Max = node->m_GroupBounds.Min + settings->m_GroupSize;
    node->m_GroupBounds.Floor();
}

void ed::EditorContext::RemoveSettings(Object* object)
{
    if (auto node = object->AsNode())
    {
        m_Settings.RemoveNode(node->m_ID);
        MakeDirty(SaveReasonFlags::RemoveNode, node);
    }
}

void ed::EditorContext::ClearSelection()
{
    for (auto& object : m_SelectedObjects)
        object->m_IsSelected = false;

    m_SelectedObjects.clear();
}

void ed::EditorContext::SelectObject(Object* object)
{
    m_SelectedObjects.push_back(object);
    object->m_IsSelected = true;
}

void ed::EditorContext::DeselectObject(Object* object)
{
    auto objectIt = std::find(m_SelectedObjects.begin(), m_SelectedObjects.end(), object);
    if (objectIt == m_SelectedObjects.end())
        return;

    object->m_IsSelected = false;
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
    return object && object->m_IsSelected;
    // return std::find(m_SelectedObjects.begin(), m_SelectedObjects.end(), object) != m_SelectedObjects.end();
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

void ed::EditorContext::FindNodesInRect(const ImRect& r, vector<Node*>& result, bool append, bool includeIntersecting)
{
    if (!append)
        result.resize(0);

    if (ImRect_IsEmpty(r))
        return;

    for (auto node : m_Nodes)
        if (node->TestHit(r, includeIntersecting))
            result.push_back(node);
}

void ed::EditorContext::FindLinksInRect(const ImRect& r, vector<Link*>& result, bool append)
{
    if (!append)
        result.resize(0);

    if (ImRect_IsEmpty(r))
        return;

    for (auto link : m_Links)
        if (link->TestHit(r))
            result.push_back(link);
}

bool ed::EditorContext::HasAnyLinks(NodeId nodeId) const
{
    for (auto link : m_Links)
    {
        if (!link->m_IsLive)
            continue;

        if (link->m_StartPin->m_Node->m_ID == nodeId || link->m_EndPin->m_Node->m_ID == nodeId)
            return true;
    }

    return false;
}

bool ed::EditorContext::HasAnyLinks(PinId pinId) const
{
    for (auto link : m_Links)
    {
        if (!link->m_IsLive)
            continue;

        if (link->m_StartPin->m_ID == pinId || link->m_EndPin->m_ID == pinId)
            return true;
    }

    return false;
}

int ed::EditorContext::BreakLinks(NodeId nodeId)
{
    int result = 0;
    for (auto link : m_Links)
    {
        if (!link->m_IsLive)
            continue;

        if (link->m_StartPin->m_Node->m_ID == nodeId || link->m_EndPin->m_Node->m_ID == nodeId)
        {
            if (GetItemDeleter().Add(link))
                ++result;
        }
    }
    return result;
}

int ed::EditorContext::BreakLinks(PinId pinId)
{
    int result = 0;
    for (auto link : m_Links)
    {
        if (!link->m_IsLive)
            continue;

        if (link->m_StartPin->m_ID == pinId || link->m_EndPin->m_ID == pinId)
        {
            if (GetItemDeleter().Add(link))
                ++result;
        }
    }
    return result;
}

void ed::EditorContext::FindLinksForNode(NodeId nodeId, vector<Link*>& result, bool add)
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

bool ed::EditorContext::PinHadAnyLinks(PinId pinId)
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

void ed::EditorContext::Suspend(SuspendFlags flags)
{
    IM_ASSERT(m_DrawList != nullptr && "Suspend was called outiside of Begin/End.");
    auto lastChannel = m_DrawList->_Splitter._Current;
    m_DrawList->ChannelsSetCurrent(m_ExternalChannel);
    if (m_IsCanvasVisible)
        m_Canvas.Suspend();
    m_DrawList->ChannelsSetCurrent(lastChannel);
    if ((flags & SuspendFlags::KeepSplitter) != SuspendFlags::KeepSplitter)
        ImDrawList_SwapSplitter(m_DrawList, m_Splitter);
}

void ed::EditorContext::Resume(SuspendFlags flags)
{
    IM_ASSERT(m_DrawList != nullptr && "Reasume was called outiside of Begin/End.");
    if ((flags & SuspendFlags::KeepSplitter) != SuspendFlags::KeepSplitter)
        ImDrawList_SwapSplitter(m_DrawList, m_Splitter);
    auto lastChannel = m_DrawList->_Splitter._Current;
    m_DrawList->ChannelsSetCurrent(m_ExternalChannel);
    if (m_IsCanvasVisible)
        m_Canvas.Resume();
    m_DrawList->ChannelsSetCurrent(lastChannel);
}

bool ed::EditorContext::IsSuspended()
{
    return m_Canvas.IsSuspended();
}

bool ed::EditorContext::IsFocused()
{
    return m_IsFocused;
}

bool ed::EditorContext::IsHovered() const
{
    return m_IsHovered;
}

bool ed::EditorContext::IsHoveredWithoutOverlapp() const
{
    return m_IsHoveredWithoutOverlapp;
}

bool ed::EditorContext::CanAcceptUserInput() const
{
    return m_IsFocused && m_IsHovered;
}

int ed::EditorContext::CountLiveNodes() const
{
    return (int)std::count_if(m_Nodes.begin(),  m_Nodes.end(),  [](const Node* node)  { return node->m_IsLive; });
}

int ed::EditorContext::CountLivePins() const
{
    return (int)std::count_if(m_Pins.begin(),   m_Pins.end(),   [](const Pin*  pin)   { return pin->m_IsLive; });
}

int ed::EditorContext::CountLiveLinks() const
{
    return (int)std::count_if(m_Links.begin(),  m_Links.end(),  [](const Link* link)  { return link->m_IsLive; });
}

ed::Pin* ed::EditorContext::CreatePin(PinId id, PinKind kind)
{
    IM_ASSERT(nullptr == FindObject(id));
    auto pin = new Pin(this, id, kind);
    m_Pins.push_back({id, pin});
    std::sort(m_Pins.begin(), m_Pins.end());
    return pin;
}

ed::Node* ed::EditorContext::CreateNode(NodeId id)
{
    IM_ASSERT(nullptr == FindObject(id));
    auto node = new Node(this, id);
    m_Nodes.push_back({id, node});
    //std::sort(Nodes.begin(), Nodes.end());

    auto settings = m_Settings.FindNode(id);
    if (!settings)
        settings = m_Settings.AddNode(id);

    UpdateNodeState(node);

    if (settings->m_GroupSize.x > 0 || settings->m_GroupSize.y > 0)
        node->m_Type = NodeType::Group;

    node->m_IsLive = false;

    return node;
}

ed::Link* ed::EditorContext::CreateLink(LinkId id)
{
    IM_ASSERT(nullptr == FindObject(id));
    auto link = new Link(this, id);
    m_Links.push_back({id, link});
    std::sort(m_Links.begin(), m_Links.end());

    return link;
}

template <typename C, typename Id>
static inline auto FindItemInLinear(C& container, Id id)
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

template <typename C, typename Id>
static inline auto FindItemIn(C& container, Id id)
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

ed::Node* ed::EditorContext::FindNode(NodeId id)
{
    return FindItemInLinear(m_Nodes, id);
}

ed::Pin* ed::EditorContext::FindPin(PinId id)
{
    return FindItemIn(m_Pins, id);
}

ed::Link* ed::EditorContext::FindLink(LinkId id)
{
    return FindItemIn(m_Links, id);
}

ed::Object* ed::EditorContext::FindObject(ObjectId id)
{
    if (id.IsNodeId())
        return FindNode(id.AsNodeId());
    else if (id.IsLinkId())
        return FindLink(id.AsLinkId());
    else if (id.IsPinId())
        return FindPin(id.AsPinId());
    else
        return nullptr;
}

ed::Node* ed::EditorContext::GetNode(NodeId id)
{
    auto node = FindNode(id);
    if (!node)
        node = CreateNode(id);
    return node;
}

ed::Pin* ed::EditorContext::GetPin(PinId id, PinKind kind)
{
    if (auto pin = FindPin(id))
    {
        pin->m_Kind = kind;
        return pin;
    }
    else
        return CreatePin(id, kind);
}

ed::Link* ed::EditorContext::GetLink(LinkId id)
{
    if (auto link = FindLink(id))
        return link;
    else
        return CreateLink(id);
}

void ed::EditorContext::LoadSettings()
{
    ed::Settings::Parse(m_Config.Load(), m_Settings);

    if (ImRect_IsEmpty(m_Settings.m_VisibleRect))
    {
        m_NavigateAction.m_Scroll = m_Settings.m_ViewScroll;
        m_NavigateAction.m_Zoom   = m_Settings.m_ViewZoom;
    }
    else
    {
        m_NavigateAction.NavigateTo(m_Settings.m_VisibleRect, NavigateAction::ZoomMode::Exact, 0.0f);
    }
}

void ed::EditorContext::SaveSettings()
{
    m_Config.BeginSave();

    for (auto& node : m_Nodes)
    {
        auto settings = m_Settings.FindNode(node->m_ID);
        settings->m_Location = node->m_Bounds.Min;
        settings->m_Size     = node->m_Bounds.GetSize();
        if (IsGroup(node))
            settings->m_GroupSize = node->m_GroupBounds.GetSize();

        if (!node->m_RestoreState && settings->m_IsDirty && m_Config.SaveNodeSettings)
        {
            if (m_Config.SaveNode(node->m_ID, settings->Serialize().dump(), settings->m_DirtyReason))
                settings->ClearDirty();
        }
    }

    m_Settings.m_Selection.resize(0);
    for (auto& object : m_SelectedObjects)
        m_Settings.m_Selection.push_back(object->ID());

    m_Settings.m_ViewScroll  = m_NavigateAction.m_Scroll;
    m_Settings.m_ViewZoom    = m_NavigateAction.m_Zoom;
    m_Settings.m_VisibleRect = m_NavigateAction.m_VisibleRect;

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

ed::Link* ed::EditorContext::FindLinkAt(const ImVec2& p)
{
    for (auto& link : m_Links)
        if (link->TestHit(p, c_LinkSelectThickness))
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

int ed::EditorContext::GetNodeIds(NodeId* nodes, int size) const
{
    if (size <= 0)
        return 0;

    int result = 0;
    for (auto node : m_Nodes)
    {
        if (!node->m_IsLive)
            continue;

        *nodes++ = node->m_ID;
        ++result;
        if (--size == 0)
            break;
    }

    return result;
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

void ed::EditorContext::Flow(Link* link, FlowDirection direction)
{
    m_FlowAnimationController.Flow(link, direction);
}

void ed::EditorContext::SetUserContext(bool globalSpace)
{
    const auto mousePos = ImGui::GetMousePos();

    // Move drawing cursor to mouse location and prepare layer for
    // content added by user.
    if (globalSpace)
        ImGui::SetCursorScreenPos(m_Canvas.FromLocal(mousePos));
    else
        ImGui::SetCursorScreenPos(m_Canvas.FromLocal(mousePos));
        //ImGui::SetCursorScreenPos(ImFloor(mousePos));
        //ImGui::SetCursorScreenPos(ImVec2(floorf(mousePos.x), floorf(mousePos.y)));

    if (!IsSuspended())
    {
        m_DrawList->ChannelsSetCurrent(c_UserChannel_Content);
    }

    // #debug
    // drawList->AddCircleFilled(ImGui::GetMousePos(), 4, IM_COL32(0, 255, 0, 255));
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
    m_IsHovered = false;
    m_IsHoveredWithoutOverlapp = false;

    const auto windowHovered = ImGui::IsWindowHovered();
    const auto widgetHovered = ImGui::IsMouseHoveringRect(m_Canvas.ViewRect().Min, m_Canvas.ViewRect().Max, true);

    if (!allowOffscreen && !windowHovered && !widgetHovered)
        return Control();

    const auto mousePos = ImGui::GetMousePos();

    // Expand clip rectangle to always contain cursor
    auto editorRect = m_Canvas.ViewRect();
    auto isMouseOffscreen = allowOffscreen && !editorRect.Contains(mousePos);
    if (isMouseOffscreen)
    {
        // Extend clip rect to capture off-screen mouse cursor
        editorRect.Add(ImFloor(mousePos));
        editorRect.Add(ImVec2(ImCeil(mousePos.x), ImCeil(mousePos.y)));

        ImGui::PushClipRect(editorRect.Min, editorRect.Max, false);
    }

    ImGuiID activeId            = 0;
    Object* hotObject           = nullptr;
    Object* activeObject        = nullptr;
    Object* clickedObject       = nullptr;
    Object* doubleClickedObject = nullptr;

    ImGuiButtonFlags extraFlags = ImGuiButtonFlags_None;
    extraFlags |= ImGuiButtonFlags_MouseButtonLeft;
    extraFlags |= ImGuiButtonFlags_MouseButtonRight;
    extraFlags |= ImGuiButtonFlags_MouseButtonMiddle;

    static auto invisibleButtonEx = [](const char* str_id, const ImVec2& size_arg, ImGuiButtonFlags extraFlags) -> int
    {
        using namespace ImGui;

        ImGuiWindow* window = GetCurrentWindow();
        if (window->SkipItems)
            return -1;

        if (size_arg.x == 0.0f || size_arg.y == 0.0f)
            return false;

        const ImGuiID id = window->GetID(str_id);
        ImVec2 size = CalcItemSize(size_arg, 0.0f, 0.0f);
        const ImRect bb(window->DC.CursorPos, window->DC.CursorPos + size);
        ItemSize(size);
        if (!ItemAdd(bb, id))
            return -1;

        auto buttonIndex = ImGui::GetCurrentContext()->ActiveIdMouseButton;

        bool hovered, held;
        bool pressed = ButtonBehavior(bb, id, &hovered, &held, extraFlags);

        return pressed ? buttonIndex : -1;
    };

    // Emits invisible button and returns true if it is clicked.
    auto emitInteractiveAreaEx = [&activeId](ObjectId id, const ImRect& rect, ImGuiButtonFlags extraFlags) -> int
    {
        char idString[33] = { 0 }; // itoa can output 33 bytes maximum
        snprintf(idString, 32, "%p", id.AsPointer());
        ImGui::SetCursorScreenPos(rect.Min);

        // debug
        //if (id < 0) return ImGui::Button(idString, to_imvec(rect.size));

        auto buttonIndex = invisibleButtonEx(idString, rect.GetSize(), extraFlags);

        // #debug
        //ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(0, 255, 0, 64));

        if (ImGui::IsItemActive())
            activeId = ImGui::GetActiveID();

        return buttonIndex;
    };

    auto emitInteractiveArea = [&emitInteractiveAreaEx, extraFlags](ObjectId id, const ImRect& rect)
    {
        return emitInteractiveAreaEx(id, rect, extraFlags);
    };

    // Check input interactions over area.
    auto checkInteractionsInArea = [this, &emitInteractiveArea, &hotObject, &activeObject, &clickedObject, &doubleClickedObject](ObjectId id, const ImRect& rect, Object* object)
    {
        if (emitInteractiveArea(id, rect) >= 0)
            clickedObject = object;
        if (!doubleClickedObject && ImGui::IsMouseDoubleClicked(m_Config.DragButtonIndex) && ImGui::IsItemHovered())
            doubleClickedObject = object;

        if (!hotObject && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
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
            ImGui::PushID(node->m_ID.AsPointer());

            static const NodeRegion c_Regions[] =
            {
                NodeRegion::TopLeft,
                NodeRegion::TopRight,
                NodeRegion::BottomLeft,
                NodeRegion::BottomRight,
                NodeRegion::Top,
                NodeRegion::Bottom,
                NodeRegion::Left,
                NodeRegion::Right,
                NodeRegion::Header,
            };

            for (auto region : c_Regions)
            {
                auto bounds = node->GetRegionBounds(region);
                if (ImRect_IsEmpty(bounds))
                    continue;
                checkInteractionsInArea(NodeId(static_cast<int>(region)), bounds, node);
            }

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

    ImGuiButtonFlags backgroundExtraFlags = ImGuiButtonFlags_None;
    if (m_Config.DragButtonIndex == 0 || m_Config.SelectButtonIndex == 0 || m_Config.NavigateButtonIndex == 0)
        backgroundExtraFlags |= ImGuiButtonFlags_MouseButtonLeft;
    if (m_Config.DragButtonIndex == 1 || m_Config.SelectButtonIndex == 1 || m_Config.NavigateButtonIndex == 1)
        backgroundExtraFlags |= ImGuiButtonFlags_MouseButtonRight;
    if (m_Config.DragButtonIndex == 2 || m_Config.SelectButtonIndex == 2 || m_Config.NavigateButtonIndex == 2)
        backgroundExtraFlags |= ImGuiButtonFlags_MouseButtonMiddle;

    auto isMouseDoubleClickOverBackground = [doubleClickedObject, backgroundExtraFlags]() -> int
    {
        if (doubleClickedObject)
            return -1;

        if (!ImGui::IsItemHovered())
            return -1;

        if ((backgroundExtraFlags & ImGuiButtonFlags_MouseButtonLeft) && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            return ImGuiButtonFlags_MouseButtonLeft;
        if ((backgroundExtraFlags & ImGuiButtonFlags_MouseButtonRight) && ImGui::IsMouseDoubleClicked(ImGuiButtonFlags_MouseButtonRight))
            return ImGuiButtonFlags_MouseButtonRight;
        if ((backgroundExtraFlags & ImGuiButtonFlags_MouseButtonMiddle) && ImGui::IsMouseDoubleClicked(ImGuiButtonFlags_MouseButtonMiddle))
            return ImGuiButtonFlags_MouseButtonMiddle;

        return -1;
    };

    // Check for interaction with background.
    auto backgroundClickButonIndex        = emitInteractiveAreaEx(NodeId(0), editorRect, backgroundExtraFlags);
    auto backgroundDoubleClickButtonIndex = isMouseDoubleClickOverBackground();
    auto isBackgroundActive               = ImGui::IsItemActive();
    auto isBackgroundHot                  = !hotObject;
    auto isDragging                       = ImGui::IsMouseDragging(0, 1) || ImGui::IsMouseDragging(1, 1) || ImGui::IsMouseDragging(2, 1);

    if (backgroundDoubleClickButtonIndex >= 0)
        backgroundClickButonIndex = -1;

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
    if (!isDragging && backgroundClickButonIndex >= 0 && hotLink)
    {
        clickedObject             = hotLink;
        backgroundClickButonIndex = -1;
    }

    // Steal double-click from backgrounds if link is hovered.
    if (!isDragging && backgroundDoubleClickButtonIndex >= 0 && hotLink)
    {
        doubleClickedObject              = hotLink;
        backgroundDoubleClickButtonIndex = -1;
    }

    if (activeId)
        m_EditorActiveId = activeId;

    if (ImGui::IsAnyItemActive() && ImGui::GetActiveID() != m_EditorActiveId)
        return Control();

    m_IsHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_RectOnly);
    m_IsHoveredWithoutOverlapp = ImGui::IsItemHovered();
    if (!allowOffscreen && !m_IsHovered)
        return Control();

# if IMGUI_VERSION_NUM >= 18836
    if (m_IsHoveredWithoutOverlapp)
        ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
# elif IMGUI_VERSION_NUM >= 17909
    if (m_IsHoveredWithoutOverlapp)
        ImGui::SetItemUsingMouseWheel();
# endif

    return Control(hotObject, activeObject, clickedObject, doubleClickedObject,
        isBackgroundHot, isBackgroundActive, backgroundClickButonIndex, backgroundDoubleClickButtonIndex);
}

void ed::EditorContext::ShowMetrics(const Control& control)
{
    auto& io = ImGui::GetIO();

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

    auto liveNodeCount  = CountLiveNodes();
    auto livePinCount   = CountLivePins();
    auto liveLinkCount  = CountLiveLinks();

    auto canvasRect     = m_Canvas.Rect();
    auto viewRect       = m_Canvas.ViewRect();
    auto localMousePos  = m_Canvas.ToLocal(io.MousePos);
    auto globalMousePos = io.MousePos;

    ImGui::SetCursorScreenPos(canvasRect.Min + ImVec2(5, 5));
    ImGui::BeginGroup();
    ImGui::Text("Is Focused: %s", m_IsFocused ? "true" : "false");
    ImGui::Text("Is Hovered: %s", m_IsHovered ? "true" : "false");
    ImGui::Text("Is Hovered (without overlapp): %s", m_IsHoveredWithoutOverlapp ? "true" : "false");
    ImGui::Text("Accept Input: %s", CanAcceptUserInput() ? "true" : "false");
    ImGui::Text("View Position: { x=%g y=%g }", viewRect.Min.x, viewRect.Min.y);
    ImGui::Text("View Size: { w=%g h=%g }", viewRect.GetWidth(), viewRect.GetHeight());
    ImGui::Text("Canvas Size: { w=%g h=%g }", canvasRect.GetWidth(), canvasRect.GetHeight());
    ImGui::Text("Mouse: { x=%.0f y=%.0f } global: { x=%g y=%g }", localMousePos.x, localMousePos.y, globalMousePos.x, globalMousePos.y);
    ImGui::Text("Live Nodes: %d", liveNodeCount);
    ImGui::Text("Live Pins: %d", livePinCount);
    ImGui::Text("Live Links: %d", liveLinkCount);
    ImGui::Text("Hot Object: %s (%p)", getHotObjectName(), control.HotObject ? control.HotObject->ID().AsPointer() : nullptr);
    if (auto node = control.HotObject ? control.HotObject->AsNode() : nullptr)
    {
        ImGui::SameLine();
        ImGui::Text("{ x=%g y=%g w=%g h=%g }", node->m_Bounds.Min.x, node->m_Bounds.Min.y, node->m_Bounds.GetWidth(), node->m_Bounds.GetHeight());
    }
    ImGui::Text("Active Object: %s (%p)", getActiveObjectName(), control.ActiveObject ? control.ActiveObject->ID().AsPointer() : nullptr);
    if (auto node = control.ActiveObject ? control.ActiveObject->AsNode() : nullptr)
    {
        ImGui::SameLine();
        ImGui::Text("{ x=%g y=%g w=%g h=%g }", node->m_Bounds.Min.x, node->m_Bounds.Min.y, node->m_Bounds.GetWidth(), node->m_Bounds.GetHeight());
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

ed::json::value ed::NodeSettings::Serialize()
{
    json::value result;
    result["location"]["x"] = m_Location.x;
    result["location"]["y"] = m_Location.y;

    if (m_GroupSize.x > 0 || m_GroupSize.y > 0)
    {
        result["group_size"]["x"] = m_GroupSize.x;
        result["group_size"]["y"] = m_GroupSize.y;
    }

    return result;
}

bool ed::NodeSettings::Parse(const std::string& string, NodeSettings& settings)
{
    auto settingsValue = json::value::parse(string);
    if (settingsValue.is_discarded())
        return false;

    return Parse(settingsValue, settings);
}

bool ed::NodeSettings::Parse(const json::value& data, NodeSettings& result)
{
    if (!data.is_object())
        return false;

    auto tryParseVector = [](const json::value& v, ImVec2& result) -> bool
    {
        if (v.is_object())
        {
            auto xValue = v["x"];
            auto yValue = v["y"];

            if (xValue.is_number() && yValue.is_number())
            {
                result.x = static_cast<float>(xValue.get<double>());
                result.y = static_cast<float>(yValue.get<double>());

                return true;
            }
        }

        return false;
    };

    if (!tryParseVector(data["location"], result.m_Location))
        return false;

    if (data.contains("group_size") && !tryParseVector(data["group_size"], result.m_GroupSize))
        return false;

    return true;
}




//------------------------------------------------------------------------------
//
// Settings
//
//------------------------------------------------------------------------------
ed::NodeSettings* ed::Settings::AddNode(NodeId id)
{
    m_Nodes.push_back(NodeSettings(id));
    return &m_Nodes.back();
}

ed::NodeSettings* ed::Settings::FindNode(NodeId id)
{
    for (auto& settings : m_Nodes)
        if (settings.m_ID == id)
            return &settings;

    return nullptr;
}

void ed::Settings::RemoveNode(NodeId id)
{
    auto node = FindNode(id);
    if (!node)
        return;

    *node = NodeSettings(id);
}

void ed::Settings::ClearDirty(Node* node)
{
    if (node)
    {
        auto settings = FindNode(node->m_ID);
        IM_ASSERT(settings);
        settings->ClearDirty();
    }
    else
    {
        m_IsDirty     = false;
        m_DirtyReason = SaveReasonFlags::None;

        for (auto& knownNode : m_Nodes)
            knownNode.ClearDirty();
    }
}

void ed::Settings::MakeDirty(SaveReasonFlags reason, Node* node)
{
    m_IsDirty     = true;
    m_DirtyReason = m_DirtyReason | reason;

    if (node)
    {
        auto settings = FindNode(node->m_ID);
        IM_ASSERT(settings);

        settings->MakeDirty(reason);
    }
}

std::string ed::Settings::Serialize()
{
    json::value result;

    auto serializeObjectId = [](ObjectId id)
    {
        auto value = std::to_string(reinterpret_cast<uintptr_t>(id.AsPointer()));
        switch (id.Type())
        {
            default:
            case NodeEditor::Detail::ObjectType::None: return value;
            case NodeEditor::Detail::ObjectType::Node: return "node:" + value;
            case NodeEditor::Detail::ObjectType::Link: return "link:" + value;
            case NodeEditor::Detail::ObjectType::Pin:  return "pin:"  + value;
        }
    };

    auto& nodes = result["nodes"];
    for (auto& node : m_Nodes)
    {
        if (node.m_WasUsed)
            nodes[serializeObjectId(node.m_ID)] = node.Serialize();
    }

    auto& selection = result["selection"];
    for (auto& id : m_Selection)
        selection.push_back(serializeObjectId(id));

    auto& view = result["view"];
    view["scroll"]["x"] = m_ViewScroll.x;
    view["scroll"]["y"] = m_ViewScroll.y;
    view["zoom"]   = m_ViewZoom;
    view["visible_rect"]["min"]["x"] = m_VisibleRect.Min.x;
    view["visible_rect"]["min"]["y"] = m_VisibleRect.Min.y;
    view["visible_rect"]["max"]["x"] = m_VisibleRect.Max.x;
    view["visible_rect"]["max"]["y"] = m_VisibleRect.Max.y;

    return result.dump();
}

bool ed::Settings::Parse(const std::string& string, Settings& settings)
{
    Settings result = settings;

    auto settingsValue = json::value::parse(string);
    if (settingsValue.is_discarded())
        return false;

    if (!settingsValue.is_object())
        return false;

    auto tryParseVector = [](const json::value& v, ImVec2& result) -> bool
    {
        if (v.is_object() && v.contains("x") && v.contains("y"))
        {
            auto xValue = v["x"];
            auto yValue = v["y"];

            if (xValue.is_number() && yValue.is_number())
            {
                result.x = static_cast<float>(xValue.get<double>());
                result.y = static_cast<float>(yValue.get<double>());

                return true;
            }
        }

        return false;
    };

    auto deserializeObjectId = [](const std::string& str)
    {
        auto separator = str.find_first_of(':');
        auto idStart   = str.c_str() + ((separator != std::string::npos) ? separator + 1 : 0);
        auto id        = reinterpret_cast<void*>(strtoull(idStart, nullptr, 10));
        if (str.compare(0, separator, "node") == 0)
            return ObjectId(NodeId(id));
        else if (str.compare(0, separator, "link") == 0)
            return ObjectId(LinkId(id));
        else if (str.compare(0, separator, "pin") == 0)
            return ObjectId(PinId(id));
        else
            // fallback to old format
            return ObjectId(NodeId(id)); //return ObjectId();
    };

    //auto& settingsObject = settingsValue.get<json::object>();

    auto& nodesValue = settingsValue["nodes"];
    if (nodesValue.is_object())
    {
        for (auto& node : nodesValue.get<json::object>())
        {
            auto id = deserializeObjectId(node.first.c_str()).AsNodeId();

            auto nodeSettings = result.FindNode(id);
            if (!nodeSettings)
                nodeSettings = result.AddNode(id);

            NodeSettings::Parse(node.second, *nodeSettings);
        }
    }

    auto& selectionValue = settingsValue["selection"];
    if (selectionValue.is_array())
    {
        const auto selectionArray = selectionValue.get<json::array>();

        result.m_Selection.reserve(selectionArray.size());
        result.m_Selection.resize(0);
        for (auto& selection : selectionArray)
        {
            if (selection.is_string())
                result.m_Selection.push_back(deserializeObjectId(selection.get<json::string>()));
        }
    }

    auto& viewValue = settingsValue["view"];
    if (viewValue.is_object())
    {
        auto& viewScrollValue = viewValue["scroll"];
        auto& viewZoomValue   = viewValue["zoom"];

        if (!tryParseVector(viewScrollValue, result.m_ViewScroll))
            result.m_ViewScroll = ImVec2(0, 0);

        result.m_ViewZoom = viewZoomValue.is_number() ? static_cast<float>(viewZoomValue.get<double>()) : 1.0f;

        if (!viewValue.contains("visible_rect") || !tryParseVector(viewValue["visible_rect"]["min"], result.m_VisibleRect.Min) || !tryParseVector(viewValue["visible_rect"]["max"], result.m_VisibleRect.Max))
            result.m_VisibleRect = {};
    }

    settings = std::move(result);

    return true;
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
        Finish();
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

    m_Time += ImMax(0.0f, ImGui::GetIO().DeltaTime);
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

void ed::NavigateAnimation::NavigateTo(const ImRect& target, float duration)
{
    Stop();

    m_Start      = Action.GetViewRect();
    m_Target     = target;

    // Skip tiny animations
    auto minoffset = m_Target.Min - m_Start.Min;
    auto maxOffset = m_Target.Max - m_Start.Max;
    auto epsilon   = 1e-4f;
    if (ImFabs(minoffset.x) < epsilon && ImFabs(minoffset.y) < epsilon &&
        ImFabs(maxOffset.x) < epsilon && ImFabs(maxOffset.y) < epsilon)
    {
        duration = 0;
    }

    Play(duration);
}

void ed::NavigateAnimation::OnUpdate(float progress)
{
    ImRect current;
    current.Min = ImEasing::EaseOutQuad(m_Start.Min, m_Target.Min - m_Start.Min, progress);
    current.Max = ImEasing::EaseOutQuad(m_Start.Max, m_Target.Max - m_Start.Max, progress);
    Action.SetViewRect(current);
}

void ed::NavigateAnimation::OnStop()
{
    Editor->MakeDirty(SaveReasonFlags::Navigation);
}

void ed::NavigateAnimation::OnFinish()
{
    Action.SetViewRect(m_Target);

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
    if (m_Offset < 0)
        m_Offset += m_MarkerDistance;

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
    m_PathLength = ImCubicBezierLength(curve.P0, curve.P1, curve.P2, curve.P3);

    auto collectPointsCallback = [this](ImCubicBezierFixedStepSample& result)
    {
        m_Path.push_back(CurvePoint{ result.Length, result.Point });
    };

    const auto step = ImMax(m_MarkerDistance * 0.5f, 15.0f);

    m_Path.resize(0);
    ImCubicBezierFixedStep(collectPointsCallback, curve, step, false, 0.5f, 0.001f);
}

void ed::FlowAnimation::ClearPath()
{
    vector<CurvePoint>().swap(m_Path);
    m_PathLength = 0.0f;
}

ImVec2 ed::FlowAnimation::SamplePath(float distance) const
{
    //distance = ImMax(0.0f, std::min(distance, PathLength));

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
    IM_UNUSED(progress);

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

void ed::FlowAnimationController::Flow(Link* link, FlowDirection direction)
{
    if (!link || !link->m_IsLive)
        return;

    auto& editorStyle = GetStyle();

    auto animation = GetOrCreate(link);

    float speedDirection = 1.0f;
    if (direction == FlowDirection::Backward)
        speedDirection = -1.0f;

    animation->Flow(link, editorStyle.FlowMarkerDistance, editorStyle.FlowSpeed * speedDirection, editorStyle.FlowDuration);
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
    IM_UNUSED(animation);
}



//------------------------------------------------------------------------------
//
// Navigate Action
//
//------------------------------------------------------------------------------
const float ed::NavigateAction::s_DefaultZoomLevels[] =
{
    0.1f, 0.15f, 0.20f, 0.25f, 0.33f, 0.5f, 0.75f, 1.0f, 1.25f, 1.50f, 2.0f, 2.5f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f
};

const int ed::NavigateAction::s_DefaultZoomLevelCount = sizeof(s_DefaultZoomLevels) / sizeof(*s_DefaultZoomLevels);

ed::NavigateAction::NavigateAction(EditorContext* editor, ImGuiEx::Canvas& canvas):
    EditorAction(editor),
    m_IsActive(false),
    m_Zoom(1),
    m_VisibleRect(),
    m_Scroll(0, 0),
    m_ScrollStart(0, 0),
    m_ScrollDelta(0, 0),
    m_Canvas(canvas),
    m_WindowScreenPos(0, 0),
    m_WindowScreenSize(0, 0),
    m_Animation(editor, *this),
    m_Reason(NavigationReason::Unknown),
    m_LastSelectionId(0),
    m_LastObject(nullptr),
    m_MovingOverEdge(false),
    m_MoveScreenOffset(0, 0),
    m_ZoomLevels(editor->GetConfig().CustomZoomLevels.Size > 0 ? editor->GetConfig().CustomZoomLevels.Data : s_DefaultZoomLevels),
    m_ZoomLevelCount(editor->GetConfig().CustomZoomLevels.Size > 0 ? editor->GetConfig().CustomZoomLevels.Size : s_DefaultZoomLevelCount)
{
}

ed::EditorAction::AcceptResult ed::NavigateAction::Accept(const Control& control)
{
    IM_ASSERT(!m_IsActive);

    if (m_IsActive)
        return False;

    if (Editor->CanAcceptUserInput() /*&& !ImGui::IsAnyItemActive()*/ && ImGui::IsMouseDragging(Editor->GetConfig().NavigateButtonIndex, 0.0f))
    {
        m_IsActive    = true;
        m_ScrollStart = m_Scroll;
        m_ScrollDelta = ImGui::GetMouseDragDelta(Editor->GetConfig().NavigateButtonIndex);
        m_Scroll      = m_ScrollStart - m_ScrollDelta * m_Zoom;
    }

    auto& io = ImGui::GetIO();

    if (Editor->CanAcceptUserInput() && ImGui::IsKeyPressed(GetKeyIndexForF()) && Editor->AreShortcutsEnabled())
    {
        const auto zoomMode = io.KeyShift ? NavigateAction::ZoomMode::WithMargin : NavigateAction::ZoomMode::None;

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
            if (m_Reason != NavigationReason::Selection || m_LastSelectionId != Editor->GetSelectionId() || (zoomMode != NavigateAction::ZoomMode::None))
            {
                m_LastSelectionId = Editor->GetSelectionId();
                NavigateTo(Editor->GetSelectionBounds(), zoomMode, -1.0f, NavigationReason::Selection);
            }
            else
                navigateToContent = true;
        }
        else if(auto hotObject = findHotObjectToZoom())
        {
            if (m_Reason != NavigationReason::Object || m_LastObject != hotObject || (zoomMode != NavigateAction::ZoomMode::None))
            {
                m_LastObject = hotObject;
                auto bounds = hotObject->GetBounds();
                NavigateTo(bounds, zoomMode, -1.0f, NavigationReason::Object);
            }
            else
                navigateToContent = true;
        }
        else
            navigateToContent = true;

        if (navigateToContent)
            NavigateTo(Editor->GetContentBounds(), NavigateAction::ZoomMode::WithMargin, -1.0f, NavigationReason::Content);
    }

    auto visibleRect = GetViewRect();
    if (m_VisibleRect.Min != visibleRect.Min || m_VisibleRect.Max != visibleRect.Max)
    {
        m_VisibleRect = visibleRect;
        Editor->MakeDirty(SaveReasonFlags::Navigation);
    }

    // // #debug
    // if (m_DrawList)
    //     m_DrawList->AddCircleFilled(io.MousePos, 4.0f, IM_COL32(255, 0, 255, 255));

    if (HandleZoom(control))
        return True;

    return m_IsActive ? True : False;
}

bool ed::NavigateAction::Process(const Control& control)
{
    IM_UNUSED(control);

    if (!m_IsActive)
        return false;

    if (ImGui::IsMouseDragging(Editor->GetConfig().NavigateButtonIndex, 0.0f))
    {
        m_ScrollDelta = ImGui::GetMouseDragDelta(Editor->GetConfig().NavigateButtonIndex);
        m_Scroll      = m_ScrollStart - m_ScrollDelta * m_Zoom;
        m_VisibleRect = GetViewRect();
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
    IM_UNUSED(control);

    const auto currentAction  = Editor->GetCurrentAction();
    const auto allowOffscreen = currentAction && currentAction->IsDragging();

    auto& io = ImGui::GetIO();

    if (!io.MouseWheel || (!allowOffscreen && !Editor->IsHoveredWithoutOverlapp()))// && !ImGui::IsAnyItemActive())
        return false;

    auto savedScroll = m_Scroll;
    auto savedZoom   = m_Zoom;

    m_Animation.Finish();

    auto mousePos = io.MousePos;
    auto newZoom  = GetNextZoom(io.MouseWheel);

    auto oldView   = GetView();
    m_Zoom = newZoom;
    auto newView   = GetView();

    auto screenPos = m_Canvas.FromLocal(mousePos, oldView);
    auto canvasPos = m_Canvas.ToLocal(screenPos, newView);

    auto offset       = (canvasPos - mousePos) * m_Zoom;
    auto targetScroll = m_Scroll - offset;

    auto visibleRect = GetViewRect();

    if (m_Scroll != savedScroll || m_Zoom != savedZoom || m_VisibleRect.Min != visibleRect.Min || m_VisibleRect.Max != visibleRect.Max)
    {
        m_Scroll      = savedScroll;
        m_Zoom        = savedZoom;
        m_VisibleRect = visibleRect;

        Editor->MakeDirty(SaveReasonFlags::Navigation);
    }

    auto targetRect = m_Canvas.CalcViewRect(ImGuiEx::CanvasView(-targetScroll, newZoom));

    NavigateTo(targetRect, c_MouseZoomDuration, NavigationReason::MouseZoom);

    return true;
}

void ed::NavigateAction::ShowMetrics()
{
    EditorAction::ShowMetrics();

    ImGui::Text("%s:", GetName());
    ImGui::Text("    Active: %s", m_IsActive ? "yes" : "no");
    ImGui::Text("    Scroll: { x=%g y=%g }", m_Scroll.x, m_Scroll.y);
    ImGui::Text("    Zoom: %g", m_Zoom);
    ImGui::Text("    Visible Rect: { l=%g t=%g, r=%g b=%g w=%g h=%g }",
        m_VisibleRect.Min.x, m_VisibleRect.Min.y,
        m_VisibleRect.Max.x, m_VisibleRect.Max.y,
        m_VisibleRect.Max.x - m_VisibleRect.Min.x,
        m_VisibleRect.Max.y - m_VisibleRect.Min.y);
}

void ed::NavigateAction::NavigateTo(const ImRect& bounds, ZoomMode zoomMode, float duration, NavigationReason reason)
{
    if (ImRect_IsEmpty(bounds))
        return;

    if (duration < 0.0f)
        duration = GetStyle().ScrollDuration;

    if (zoomMode == ZoomMode::None)
    {
        auto viewRect       = m_Canvas.ViewRect();
        auto viewRectCenter = viewRect.GetCenter();
        auto targetCenter   = bounds.GetCenter();

        viewRect.Translate(targetCenter - viewRectCenter);

        NavigateTo(viewRect, duration, reason);
    }
    else
    {
        // Grow rect by 5% to leave some reasonable margin
        // from the edges of the canvas.
        auto rect   = bounds;

        if (zoomMode == ZoomMode::WithMargin)
        {
            auto extend = ImMax(rect.GetWidth(), rect.GetHeight());
            rect.Expand(extend * c_NavigationZoomMargin * 0.5f);
        }

        NavigateTo(rect, duration, reason);
    }
}

void ed::NavigateAction::NavigateTo(const ImRect& target, float duration, NavigationReason reason)
{
    m_Reason = reason;

    m_Animation.NavigateTo(target, duration);
}

void ed::NavigateAction::StopNavigation()
{
    m_Animation.Stop();
}

void ed::NavigateAction::FinishNavigation()
{
    m_Animation.Finish();
}

bool ed::NavigateAction::MoveOverEdge(const ImVec2& canvasSize)
{
    // Don't interrupt non-edge animations
    if (m_Animation.IsPlaying())
        return false;

    auto& io = ImGui::GetIO();

    const auto screenMousePos   = io.MousePos;
    const auto screenRect       = ImRect(ImGui::GetCursorScreenPos(), ImGui::GetCursorScreenPos() + canvasSize);

    // Mouse is over screen, do nothing
    if (screenRect.Contains(screenMousePos))
        return false;

    // Several backend move mouse position to -FLT_MAX to indicate
    // uninitialized/unknown state. To prevent all sorts
    // of math problems, we just ignore such state.
    if (screenMousePos.x <= -FLT_MAX || screenMousePos.y <= -FLT_MAX)
        return false;

    const auto minDistance       = ImVec2(-c_MaxMoveOverEdgeDistance, -c_MaxMoveOverEdgeDistance);
    const auto maxDistance       = ImVec2( c_MaxMoveOverEdgeDistance,  c_MaxMoveOverEdgeDistance);

    const auto screenPointOnEdge  = ImRect_ClosestPoint(screenRect, screenMousePos, true);
    const auto offset             = ImMin(ImMax(screenPointOnEdge - screenMousePos, minDistance), maxDistance);
    const auto relativeOffset     = -offset * io.DeltaTime * c_MaxMoveOverEdgeSpeed;

    m_Scroll = m_Scroll + relativeOffset;

    m_MoveScreenOffset  = relativeOffset;
    m_MovingOverEdge    = true;

    return true;
}

void ed::NavigateAction::StopMoveOverEdge()
{
    if (m_MovingOverEdge)
    {
        Editor->MakeDirty(SaveReasonFlags::Navigation);

        m_MoveScreenOffset  = ImVec2(0, 0);
        m_MovingOverEdge    = false;
    }
}

void ed::NavigateAction::SetWindow(ImVec2 position, ImVec2 size)
{
    m_WindowScreenPos  = position;
    m_WindowScreenSize = size;
}

ImGuiEx::CanvasView ed::NavigateAction::GetView() const
{
    return ImGuiEx::CanvasView(-m_Scroll, m_Zoom);
}

ImVec2 ed::NavigateAction::GetViewOrigin() const
{
    return -m_Scroll;
}

float ed::NavigateAction::GetViewScale() const
{
    return m_Zoom;
}

void ed::NavigateAction::SetViewRect(const ImRect& rect)
{
    auto view = m_Canvas.CalcCenterView(rect);
    m_Scroll = -view.Origin;
    m_Zoom   =  view.Scale;
}

ImRect ed::NavigateAction::GetViewRect() const
{
    return m_Canvas.CalcViewRect(GetView());
}

float ed::NavigateAction::GetNextZoom(float steps)
{
    if (this->Editor->GetConfig().EnableSmoothZoom)
    {
        return MatchSmoothZoom(steps);
    }
    else
    {
        auto fixedSteps = (int)steps;
        return MatchZoom(fixedSteps, m_ZoomLevels[fixedSteps < 0 ? 0 : m_ZoomLevelCount - 1]);
    }
}

float ed::NavigateAction::MatchSmoothZoom(float steps)
{
    const auto power = Editor->GetConfig().SmoothZoomPower;

    const auto newZoom = m_Zoom * powf(power, steps);
    if (newZoom < m_ZoomLevels[0])
        return m_ZoomLevels[0];
    else if (newZoom > m_ZoomLevels[m_ZoomLevelCount - 1])
        return m_ZoomLevels[m_ZoomLevelCount - 1];
    else
        return newZoom;
}

float ed::NavigateAction::MatchZoom(int steps, float fallbackZoom)
{
    auto currentZoomIndex = MatchZoomIndex(steps);
    if (currentZoomIndex < 0)
        return fallbackZoom;

    auto currentZoom = m_ZoomLevels[currentZoomIndex];
    if (fabsf(currentZoom - m_Zoom) > 0.001f)
        return currentZoom;

    auto newIndex = currentZoomIndex + steps;
    if (newIndex >= 0 && newIndex < m_ZoomLevelCount)
        return m_ZoomLevels[newIndex];
    else
        return fallbackZoom;
}

int ed::NavigateAction::MatchZoomIndex(int direction)
{
    int   bestIndex    = -1;
    float bestDistance = 0.0f;

    for (int i = 0; i < m_ZoomLevelCount; ++i)
    {
        auto distance = fabsf(m_ZoomLevels[i] - m_Zoom);
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

            if (bestIndex >= m_ZoomLevelCount)
                bestIndex = m_ZoomLevelCount - 1;
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
    m_Pivot(NodeRegion::None),
    m_Cursor(ImGuiMouseCursor_Arrow)
{
}

ed::EditorAction::AcceptResult ed::SizeAction::Accept(const Control& control)
{
    IM_ASSERT(!m_IsActive);

    if (m_IsActive)
        return False;

    if (control.ActiveNode && IsGroup(control.ActiveNode) && ImGui::IsMouseDragging(Editor->GetConfig().DragButtonIndex, 1))
    {
        //const auto mousePos     = to_point(ImGui::GetMousePos());
        //const auto closestPoint = control.ActiveNode->Bounds.get_closest_point_hollow(mousePos, static_cast<int>(control.ActiveNode->Rounding));

        auto pivot = GetRegion(control.ActiveNode);
        if (pivot != NodeRegion::Header && pivot != NodeRegion::Center)
        {
            m_StartBounds      = control.ActiveNode->m_Bounds;
            m_StartGroupBounds = control.ActiveNode->m_GroupBounds;
            m_LastSize         = control.ActiveNode->m_Bounds.GetSize();
            m_MinimumSize      = ImVec2(0, 0);
            m_LastDragOffset   = ImVec2(0, 0);
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

        if (m_SizedNode->m_Bounds.Min != m_StartBounds.Min || m_SizedNode->m_GroupBounds.Min != m_StartGroupBounds.Min)
            Editor->MakeDirty(SaveReasonFlags::Position | SaveReasonFlags::User, m_SizedNode);

        if (m_SizedNode->m_Bounds.GetSize() != m_StartBounds.GetSize() || m_SizedNode->m_GroupBounds.GetSize() != m_StartGroupBounds.GetSize())
            Editor->MakeDirty(SaveReasonFlags::Size | SaveReasonFlags::User, m_SizedNode);

        m_SizedNode = nullptr;
    }

    if (!m_IsActive)
        return false;

    if (control.ActiveNode == m_SizedNode)
    {
        const auto dragOffset = (control.ActiveNode == m_SizedNode) ? ImGui::GetMouseDragDelta(0, 0.0f) : m_LastDragOffset;
        m_LastDragOffset = dragOffset;

        if (m_MinimumSize.x == 0.0f && m_LastSize.x != m_SizedNode->m_Bounds.GetWidth())
            m_MinimumSize.x = m_SizedNode->m_Bounds.GetWidth();
        if (m_MinimumSize.y == 0.0f && m_LastSize.y != m_SizedNode->m_Bounds.GetHeight())
            m_MinimumSize.y = m_SizedNode->m_Bounds.GetHeight();

        auto minimumSize = ImMax(m_MinimumSize, m_StartBounds.GetSize() - m_StartGroupBounds.GetSize());


        auto newBounds = m_StartBounds;

        if ((m_Pivot & NodeRegion::Top) == NodeRegion::Top)
            newBounds.Min.y = ImMin(newBounds.Max.y - minimumSize.y, Editor->AlignPointToGrid(newBounds.Min.y + dragOffset.y));
        if ((m_Pivot & NodeRegion::Bottom) == NodeRegion::Bottom)
            newBounds.Max.y = ImMax(newBounds.Min.y + minimumSize.y, Editor->AlignPointToGrid(newBounds.Max.y + dragOffset.y));
        if ((m_Pivot & NodeRegion::Left) == NodeRegion::Left)
            newBounds.Min.x = ImMin(newBounds.Max.x - minimumSize.x, Editor->AlignPointToGrid(newBounds.Min.x + dragOffset.x));
        if ((m_Pivot & NodeRegion::Right) == NodeRegion::Right)
            newBounds.Max.x = ImMax(newBounds.Min.x + minimumSize.x, Editor->AlignPointToGrid(newBounds.Max.x + dragOffset.x));

        newBounds.Floor();

        m_LastSize = newBounds.GetSize();

        m_SizedNode->m_Bounds      = newBounds;
        m_SizedNode->m_GroupBounds = newBounds;
        m_SizedNode->m_GroupBounds.Min.x -= m_StartBounds.Min.x - m_StartGroupBounds.Min.x;
        m_SizedNode->m_GroupBounds.Min.y -= m_StartBounds.Min.y - m_StartGroupBounds.Min.y;
        m_SizedNode->m_GroupBounds.Max.x -= m_StartBounds.Max.x - m_StartGroupBounds.Max.x;
        m_SizedNode->m_GroupBounds.Max.y -= m_StartBounds.Max.y - m_StartGroupBounds.Max.y;
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
    ImGui::Text("    Node: %s (%p)", getObjectName(m_SizedNode), m_SizedNode ? m_SizedNode->m_ID.AsPointer() : nullptr);
    if (m_SizedNode && m_IsActive)
    {
        ImGui::Text("    Bounds: { x=%g y=%g w=%g h=%g }", m_SizedNode->m_Bounds.Min.x, m_SizedNode->m_Bounds.Min.y, m_SizedNode->m_Bounds.GetWidth(), m_SizedNode->m_Bounds.GetHeight());
        ImGui::Text("    Group Bounds: { x=%g y=%g w=%g h=%g }", m_SizedNode->m_GroupBounds.Min.x, m_SizedNode->m_GroupBounds.Min.y, m_SizedNode->m_GroupBounds.GetWidth(), m_SizedNode->m_GroupBounds.GetHeight());
        ImGui::Text("    Start Bounds: { x=%g y=%g w=%g h=%g }", m_StartBounds.Min.x, m_StartBounds.Min.y, m_StartBounds.GetWidth(), m_StartBounds.GetHeight());
        ImGui::Text("    Start Group Bounds: { x=%g y=%g w=%g h=%g }", m_StartGroupBounds.Min.x, m_StartGroupBounds.Min.y, m_StartGroupBounds.GetWidth(), m_StartGroupBounds.GetHeight());
        ImGui::Text("    Minimum Size: { w=%g h=%g }", m_MinimumSize.x, m_MinimumSize.y);
        ImGui::Text("    Last Size: { w=%g h=%g }", m_LastSize.x, m_LastSize.y);
    }
}

ed::NodeRegion ed::SizeAction::GetRegion(Node* node)
{
    return node->GetRegion(ImGui::GetMousePos());
}

ImGuiMouseCursor ed::SizeAction::ChooseCursor(NodeRegion region)
{
    switch (region)
    {
        default:
        case NodeRegion::Center:
            return ImGuiMouseCursor_Arrow;

        case NodeRegion::Top:
        case NodeRegion::Bottom:
            return ImGuiMouseCursor_ResizeNS;

        case NodeRegion::Left:
        case NodeRegion::Right:
            return ImGuiMouseCursor_ResizeEW;

        case NodeRegion::TopLeft:
        case NodeRegion::BottomRight:
            return ImGuiMouseCursor_ResizeNWSE;

        case NodeRegion::TopRight:
        case NodeRegion::BottomLeft:
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
    IM_ASSERT(!m_IsActive);

    if (m_IsActive)
        return False;

    if (Editor->CanAcceptUserInput() && control.ActiveObject && ImGui::IsMouseDragging(Editor->GetConfig().DragButtonIndex, 1))
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
    else if (control.HotNode && IsGroup(control.HotNode) && control.HotNode->GetRegion(ImGui::GetMousePos()) == NodeRegion::Header)
    {
        return Possible;
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
        auto dragOffset = ImGui::GetMouseDragDelta(Editor->GetConfig().DragButtonIndex, 0.0f);

        auto draggedOrigin  = m_DraggedObject->DragStartLocation();
        auto alignPivot     = ImVec2(0, 0);

        // TODO: Move this experimental alignment to closes pivot out of internals to node API
        if (auto draggedNode = m_DraggedObject->AsNode())
        {
            float x = FLT_MAX;
            float y = FLT_MAX;

            auto testPivot = [this, &x, &y, &draggedOrigin, &dragOffset, &alignPivot](const ImVec2& pivot)
            {
                auto initial   = draggedOrigin + dragOffset + pivot;
                auto candidate = Editor->AlignPointToGrid(initial) - draggedOrigin - pivot;

                if (ImFabs(candidate.x) < ImFabs(ImMin(x, FLT_MAX)))
                {
                    x = candidate.x;
                    alignPivot.x = pivot.x;
                }

                if (ImFabs(candidate.y) < ImFabs(ImMin(y, FLT_MAX)))
                {
                    y = candidate.y;
                    alignPivot.y = pivot.y;
                }
            };

            for (auto pin = draggedNode->m_LastPin; pin; pin = pin->m_PreviousPin)
            {
                auto pivot = pin->m_Pivot.GetCenter() - draggedNode->m_Bounds.Min;
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
    ImGui::Text("    Node: %s (%p)", getObjectName(m_DraggedObject), m_DraggedObject ? m_DraggedObject->ID().AsPointer() : nullptr);
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
    IM_ASSERT(!m_IsActive);

    if (m_IsActive)
        return False;

    auto& io = ImGui::GetIO();
    m_SelectGroups   = io.KeyShift;
    m_SelectLinkMode = io.KeyAlt;

    m_SelectedObjectsAtStart.clear();

    if (Editor->CanAcceptUserInput() && control.BackgroundHot && ImGui::IsMouseDragging(Editor->GetConfig().SelectButtonIndex, 1))
    {
        m_IsActive = true;
        m_StartPoint = ImGui_GetMouseClickPos(Editor->GetConfig().SelectButtonIndex);
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
    else if (control.BackgroundClickButtonIndex == Editor->GetConfig().SelectButtonIndex)
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
    IM_UNUSED(control);

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

    if (ImGui::IsMouseDragging(Editor->GetConfig().SelectButtonIndex, 0))
    {
        m_EndPoint = ImGui::GetMousePos();

        auto topLeft     = ImVec2(std::min(m_StartPoint.x, m_EndPoint.x), std::min(m_StartPoint.y, m_EndPoint.y));
        auto bottomRight = ImVec2(ImMax(m_StartPoint.x, m_EndPoint.x), ImMax(m_StartPoint.y, m_EndPoint.y));
        auto rect        = ImRect(topLeft, bottomRight);
        if (rect.GetWidth() <= 0)
            rect.Max.x = rect.Min.x + 1;
        if (rect.GetHeight() <= 0)
            rect.Max.y = rect.Min.y + 1;

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

    const auto alpha = m_Animation.IsPlaying() ? ImEasing::EaseOutQuad(1.0f, -1.0f, m_Animation.GetProgress()) : 1.0f;

    const auto fillColor    = Editor->GetColor(m_SelectLinkMode ? StyleColor_LinkSelRect       : StyleColor_NodeSelRect, alpha);
    const auto outlineColor = Editor->GetColor(m_SelectLinkMode ? StyleColor_LinkSelRectBorder : StyleColor_NodeSelRectBorder, alpha);

    drawList->ChannelsSetCurrent(c_BackgroundChannel_SelectionRect);

    auto min  = ImVec2(std::min(m_StartPoint.x, m_EndPoint.x), std::min(m_StartPoint.y, m_EndPoint.y));
    auto max  = ImVec2(ImMax(m_StartPoint.x, m_EndPoint.x), ImMax(m_StartPoint.y, m_EndPoint.y));

    drawList->AddRectFilled(min, max, fillColor);
    drawList->AddRect(min, max, outlineColor);
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
    m_ContextId()
{
}

ed::EditorAction::AcceptResult ed::ContextMenuAction::Accept(const Control& control)
{
    const auto isPressed  = ImGui::IsMouseClicked(Editor->GetConfig().ContextMenuButtonIndex);
    const auto isReleased = ImGui::IsMouseReleased(Editor->GetConfig().ContextMenuButtonIndex);
    const auto isDragging = ImGui::IsMouseDragging(Editor->GetConfig().ContextMenuButtonIndex, 1);

    if (isPressed || isReleased || isDragging)
    {
        Menu candidateMenu = ContextMenuAction::None;
        ObjectId contextId;

        if (auto hotObejct = control.HotObject)
        {
            if (hotObejct->AsNode())
                candidateMenu = Node;
            else if (hotObejct->AsPin())
                candidateMenu = Pin;
            else if (hotObejct->AsLink())
                candidateMenu = Link;

            if (candidateMenu != None)
                contextId = hotObejct->ID();
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
            m_ContextId     = ObjectId();
            return False;
        }
    }

    return False;
}

bool ed::ContextMenuAction::Process(const Control& control)
{
    IM_UNUSED(control);

    m_CandidateMenu = None;
    m_CurrentMenu   = None;
    m_ContextId     = ObjectId();
    return false;
}

void ed::ContextMenuAction::Reject()
{
    m_CandidateMenu = None;
    m_CurrentMenu   = None;
    m_ContextId     = ObjectId();
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

bool ed::ContextMenuAction::ShowNodeContextMenu(NodeId* nodeId)
{
    if (m_CurrentMenu != Node)
        return false;

    *nodeId = m_ContextId.AsNodeId();
    Editor->SetUserContext();
    return true;
}

bool ed::ContextMenuAction::ShowPinContextMenu(PinId* pinId)
{
    if (m_CurrentMenu != Pin)
        return false;

    *pinId = m_ContextId.AsPinId();
    Editor->SetUserContext();
    return true;
}

bool ed::ContextMenuAction::ShowLinkContextMenu(LinkId* linkId)
{
    if (m_CurrentMenu != Link)
        return false;

    *linkId = m_ContextId.AsLinkId();
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
    if (!Editor->IsFocused() || !Editor->AreShortcutsEnabled())
        return False;

    Action candidateAction = None;

    auto& io = ImGui::GetIO();
    if (io.KeyCtrl && !io.KeyShift && !io.KeyAlt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_X)))
        candidateAction = Cut;
    if (io.KeyCtrl && !io.KeyShift && !io.KeyAlt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_C)))
        candidateAction = Copy;
    if (io.KeyCtrl && !io.KeyShift && !io.KeyAlt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_V)))
        candidateAction = Paste;
    if (io.KeyCtrl && !io.KeyShift && !io.KeyAlt && ImGui::IsKeyPressed(GetKeyIndexForD()))
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

                auto isNodeInContext = [&nodes](NodeId nodeId)
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
    IM_UNUSED(control);

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
    IM_ASSERT(m_InAction);
    return m_CurrentAction == Cut;
}

bool ed::ShortcutAction::AcceptCopy()
{
    IM_ASSERT(m_InAction);
    return m_CurrentAction == Copy;
}

bool ed::ShortcutAction::AcceptPaste()
{
    IM_ASSERT(m_InAction);
    return m_CurrentAction == Paste;
}

bool ed::ShortcutAction::AcceptDuplicate()
{
    IM_ASSERT(m_InAction);
    return m_CurrentAction == Duplicate;
}

bool ed::ShortcutAction::AcceptCreateNode()
{
    IM_ASSERT(m_InAction);
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
    IM_ASSERT(!m_IsActive);

    if (m_IsActive)
        return EditorAction::False;

    if (control.ActivePin && ImGui::IsMouseDragging(Editor->GetConfig().DragButtonIndex, 1))
    {
        m_DraggedPin = control.ActivePin;
        DragStart(m_DraggedPin);

        Editor->ClearSelection();
    }
    else if (control.HotPin)
    {
        return EditorAction::Possible;
    }
    else
        return EditorAction::False;

    m_IsActive = true;

    return EditorAction::True;
}

bool ed::CreateItemAction::Process(const Control& control)
{
    IM_ASSERT(m_IsActive);

    if (!m_IsActive)
        return false;

    if (m_DraggedPin && control.ActivePin == m_DraggedPin && (m_CurrentStage == Possible))
    {
        const auto draggingFromSource = (m_DraggedPin->m_Kind == PinKind::Output);

        ed::Pin cursorPin(Editor, 0, draggingFromSource ? PinKind::Input : PinKind::Output);
        cursorPin.m_Pivot    = ImRect(ImGui::GetMousePos(), ImGui::GetMousePos());
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

        auto drawList = Editor->GetDrawList();
        drawList->ChannelsSetCurrent(c_LinkChannel_NewLink);

        candidate.UpdateEndpoints();
        candidate.Draw(drawList, m_LinkColor, m_LinkThickness);
    }
    else if (m_CurrentStage == Possible || !control.ActivePin)
    {
        if (!Editor->CanAcceptUserInput())
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

    m_LastChannel = Editor->GetDrawList()->_Splitter._Current;

    return true;
}

void ed::CreateItemAction::End()
{
    IM_ASSERT(m_InActive);

    if (m_IsInGlobalSpace)
    {
        ImGui::PopClipRect();
        Editor->Resume(SuspendFlags::KeepSplitter);

        auto currentChannel = Editor->GetDrawList()->_Splitter._Current;
        if (currentChannel != m_LastChannel)
            Editor->GetDrawList()->ChannelsSetCurrent(m_LastChannel);

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

ed::CreateItemAction::Result ed::CreateItemAction::QueryLink(PinId* startId, PinId* endId)
{
    IM_ASSERT(m_InActive);

    if (!m_InActive || m_CurrentStage == None || m_ItemType != Link)
        return Indeterminate;

    auto linkStartId = m_LinkStart->m_ID;
    auto linkEndId   = m_LinkEnd->m_ID;

    *startId = linkStartId;
    *endId   = linkEndId;

    Editor->SetUserContext(true);

    if (!m_IsInGlobalSpace)
    {
        Editor->Suspend(SuspendFlags::KeepSplitter);

        auto rect = Editor->GetRect();
        ImGui::PushClipRect(rect.Min + ImVec2(1, 1), rect.Max - ImVec2(1, 1), false);
        m_IsInGlobalSpace = true;
    }

    return True;
}

ed::CreateItemAction::Result ed::CreateItemAction::QueryNode(PinId* pinId)
{
    IM_ASSERT(m_InActive);

    if (!m_InActive || m_CurrentStage == None || m_ItemType != Node)
        return Indeterminate;

    *pinId = m_LinkStart ? m_LinkStart->m_ID : 0;

    Editor->SetUserContext(true);

    if (!m_IsInGlobalSpace)
    {
        Editor->Suspend(SuspendFlags::KeepSplitter);

        auto rect = Editor->GetRect();
        ImGui::PushClipRect(rect.Min + ImVec2(1, 1), rect.Max - ImVec2(1, 1), false);
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

void ed::DeleteItemsAction::DeleteDeadLinks(NodeId nodeId)
{
    vector<ed::Link*> links;
    Editor->FindLinksForNode(nodeId, links, true);
    for (auto link : links)
    {
        link->m_DeleteOnNewFrame = true;

        auto it = std::find(m_CandidateObjects.begin(), m_CandidateObjects.end(), link);
        if (it != m_CandidateObjects.end())
            continue;

        m_CandidateObjects.push_back(link);
    }
}

void ed::DeleteItemsAction::DeleteDeadPins(NodeId nodeId)
{
    auto node = Editor->FindNode(nodeId);
    if (!node)
        return;

    for (auto pin = node->m_LastPin; pin; pin = pin->m_PreviousPin)
        pin->m_DeleteOnNewFrame = true;
}

ed::EditorAction::AcceptResult ed::DeleteItemsAction::Accept(const Control& control)
{
    IM_ASSERT(!m_IsActive);

    if (m_IsActive)
        return False;

    auto& io = ImGui::GetIO();
    if (Editor->CanAcceptUserInput() && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Delete)) && Editor->AreShortcutsEnabled())
    {
        auto& selection = Editor->GetSelectedObjects();
        if (!selection.empty())
        {
            m_CandidateObjects = selection;
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
        m_IsActive = true;
        return True;
    }

    return m_IsActive ? True : False;
}

bool ed::DeleteItemsAction::Process(const Control& control)
{
    IM_UNUSED(control);

    if (!m_IsActive)
        return false;

    m_IsActive = false;
    return true;
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

    IM_ASSERT(!m_InInteraction);
    m_InInteraction = true;

    m_CurrentItemType = Unknown;
    m_UserAction      = Undetermined;

    return m_IsActive;
}

void ed::DeleteItemsAction::End()
{
    if (!m_IsActive)
        return;

    IM_ASSERT(m_InInteraction);
    m_InInteraction = false;
}

bool ed::DeleteItemsAction::QueryLink(LinkId* linkId, PinId* startId, PinId* endId)
{
    ObjectId objectId;
    if (!QueryItem(&objectId, Link))
        return false;

    if (auto id = objectId.AsLinkId())
        *linkId = id;
    else
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

bool ed::DeleteItemsAction::QueryNode(NodeId* nodeId)
{
    ObjectId objectId;
    if (!QueryItem(&objectId, Node))
        return false;

    if (auto id = objectId.AsNodeId())
        *nodeId = id;
    else
        return false;

    return true;
}

bool ed::DeleteItemsAction::QueryItem(ObjectId* itemId, IteratorType itemType)
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

bool ed::DeleteItemsAction::AcceptItem(bool deleteDependencies)
{
    if (!m_InInteraction)
        return false;

    m_UserAction = Accepted;

    RemoveItem(deleteDependencies);

    return true;
}

void ed::DeleteItemsAction::RejectItem()
{
    if (!m_InInteraction)
        return;

    m_UserAction = Rejected;

    DropCurrentItem();
}

void ed::DeleteItemsAction::RemoveItem(bool deleteDependencies)
{
    auto item = DropCurrentItem();

    Editor->DeselectObject(item);

    Editor->RemoveSettings(item);

    item->m_DeleteOnNewFrame = true;

    if (deleteDependencies && m_CurrentItemType == Node)
    {
        auto node = item->ID().AsNodeId();
        DeleteDeadLinks(node);
        DeleteDeadPins(node);
    }

    if (m_CurrentItemType == Link)
        Editor->NotifyLinkDeleted(item->AsLink());
}

ed::Object* ed::DeleteItemsAction::DropCurrentItem()
{
    auto item = m_CandidateObjects[m_CandidateItemIndex];
    m_CandidateObjects.erase(m_CandidateObjects.begin() + m_CandidateItemIndex);

    return item;
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

ed::NodeBuilder::~NodeBuilder()
{
    m_Splitter.ClearFreeMemory();
    m_PinSplitter.ClearFreeMemory();
}

void ed::NodeBuilder::Begin(NodeId nodeId)
{
    IM_ASSERT(nullptr == m_CurrentNode);

    m_CurrentNode = Editor->GetNode(nodeId);

    Editor->UpdateNodeState(m_CurrentNode);

    if (m_CurrentNode->m_CenterOnScreen)
    {
        auto bounds = Editor->GetViewRect();
        auto offset = bounds.GetCenter() - m_CurrentNode->m_Bounds.GetCenter();

        if (ImLengthSqr(offset) > 0)
        {
            if (::IsGroup(m_CurrentNode))
            {
                std::vector<Node*> groupedNodes;
                m_CurrentNode->GetGroupedNodes(groupedNodes);
                groupedNodes.push_back(m_CurrentNode);

                for (auto node : groupedNodes)
                {
                    node->m_Bounds.Translate(ImFloor(offset));
                    node->m_GroupBounds.Translate(ImFloor(offset));
                    Editor->MakeDirty(SaveReasonFlags::Position | SaveReasonFlags::User, node);
                }
            }
            else
            {
                m_CurrentNode->m_Bounds.Translate(ImFloor(offset));
                m_CurrentNode->m_GroupBounds.Translate(ImFloor(offset));
                Editor->MakeDirty(SaveReasonFlags::Position | SaveReasonFlags::User, m_CurrentNode);
            }
        }

        m_CurrentNode->m_CenterOnScreen = false;
    }

    // Position node on screen
    ImGui::SetCursorScreenPos(m_CurrentNode->m_Bounds.Min);

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
    m_CurrentNode->m_HighlightConnectedLinks = editorStyle.HighlightConnectedLinks != 0.0f;

    m_IsGroup = false;

    // Grow channel list and select user channel
    if (auto drawList = Editor->GetDrawList())
    {
        m_CurrentNode->m_Channel = drawList->_Splitter._Count;
        ImDrawList_ChannelsGrow(drawList, drawList->_Splitter._Count + c_ChannelsPerNode);
        drawList->ChannelsSetCurrent(m_CurrentNode->m_Channel + c_NodeContentChannel);

        m_Splitter.Clear();
        ImDrawList_SwapSplitter(drawList, m_Splitter);
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
    IM_ASSERT(nullptr != m_CurrentNode);

    if (auto drawList = Editor->GetDrawList())
    {
        IM_ASSERT(drawList->_Splitter._Count == 1); // Did you forgot to call drawList->ChannelsMerge()?
        ImDrawList_SwapSplitter(drawList, m_Splitter);
    }

    // Apply frame padding. This must be done in this convoluted way if outer group
    // size must contain inner group padding.
    auto& editorStyle = Editor->GetStyle();
    if (editorStyle.NodePadding.x != 0 || editorStyle.NodePadding.y != 0 || editorStyle.NodePadding.z != 0 || editorStyle.NodePadding.w != 0)
    {
        ImGui::EndGroup();
        ImGui::SameLine(0, editorStyle.NodePadding.z);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
        ImGui::Dummy(ImVec2(0, 0)); // bump cursor at the end of the line and move to next one
        ImGui::Dummy(ImVec2(0, editorStyle.NodePadding.w)); // apply padding
        ImGui::PopStyleVar();
    }

    // End outer group.
    ImGui::EndGroup();

    m_NodeRect = ImGui_GetItemRect();
    m_NodeRect.Floor();

    if (m_CurrentNode->m_Bounds.GetSize() != m_NodeRect.GetSize())
    {
        m_CurrentNode->m_Bounds.Max = m_CurrentNode->m_Bounds.Min + m_NodeRect.GetSize();
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

void ed::NodeBuilder::BeginPin(PinId pinId, PinKind kind)
{
    IM_ASSERT(nullptr != m_CurrentNode);
    IM_ASSERT(nullptr == m_CurrentPin);
    IM_ASSERT(false   == m_IsGroup);

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
    m_CurrentPin->m_Dir         = kind == PinKind::Output ? editorStyle.SourceDirection : editorStyle.TargetDirection;
    m_CurrentPin->m_Strength    = editorStyle.LinkStrength;
    m_CurrentPin->m_SnapLinkToDir = editorStyle.SnapLinkToPinDir != 0.0f;

    m_CurrentPin->m_PreviousPin = m_CurrentNode->m_LastPin;
    m_CurrentNode->m_LastPin    = m_CurrentPin;

    m_PivotAlignment          = editorStyle.PivotAlignment;
    m_PivotSize               = editorStyle.PivotSize;
    m_PivotScale              = editorStyle.PivotScale;
    m_ResolvePinRect          = true;
    m_ResolvePivot            = true;

    if (auto drawList = Editor->GetDrawList())
    {
        m_PinSplitter.Clear();
        ImDrawList_SwapSplitter(drawList, m_PinSplitter);
    }

    ImGui::BeginGroup();
}

void ed::NodeBuilder::EndPin()
{
    IM_ASSERT(nullptr != m_CurrentPin);

    if (auto drawList = Editor->GetDrawList())
    {
        IM_ASSERT(drawList->_Splitter._Count == 1); // Did you forgot to call drawList->ChannelsMerge()?
        ImDrawList_SwapSplitter(drawList, m_PinSplitter);
    }

    ImGui::EndGroup();

    if (m_ResolvePinRect)
        m_CurrentPin->m_Bounds = ImGui_GetItemRect();

    if (m_ResolvePivot)
    {
        auto& pinRect = m_CurrentPin->m_Bounds;

        if (m_PivotSize.x < 0)
            m_PivotSize.x = pinRect.GetWidth();
        if (m_PivotSize.y < 0)
            m_PivotSize.y = pinRect.GetHeight();

        m_CurrentPin->m_Pivot.Min = pinRect.Min + ImMul(pinRect.GetSize(), m_PivotAlignment);
        m_CurrentPin->m_Pivot.Max = m_CurrentPin->m_Pivot.Min + ImMul(m_PivotSize, m_PivotScale);
    }

    // #debug: Draw pin bounds
    //Editor->GetDrawList()->AddRect(m_CurrentPin->m_Bounds.Min, m_CurrentPin->m_Bounds.Max, IM_COL32(255, 255, 0, 255));

    // #debug: Draw pin pivot rectangle
    //Editor->GetDrawList()->AddRect(m_CurrentPin->m_Pivot.Min, m_CurrentPin->m_Pivot.Max, IM_COL32(255, 0, 255, 255));

    m_CurrentPin = nullptr;
}

void ed::NodeBuilder::PinRect(const ImVec2& a, const ImVec2& b)
{
    IM_ASSERT(nullptr != m_CurrentPin);

    m_CurrentPin->m_Bounds = ImRect(a, b);
    m_CurrentPin->m_Bounds.Floor();
    m_ResolvePinRect     = false;
}

void ed::NodeBuilder::PinPivotRect(const ImVec2& a, const ImVec2& b)
{
    IM_ASSERT(nullptr != m_CurrentPin);

    m_CurrentPin->m_Pivot = ImRect(a, b);
    m_ResolvePivot      = false;
}

void ed::NodeBuilder::PinPivotSize(const ImVec2& size)
{
    IM_ASSERT(nullptr != m_CurrentPin);

    m_PivotSize    = size;
    m_ResolvePivot = true;
}

void ed::NodeBuilder::PinPivotScale(const ImVec2& scale)
{
    IM_ASSERT(nullptr != m_CurrentPin);

    m_PivotScale   = scale;
    m_ResolvePivot = true;
}

void ed::NodeBuilder::PinPivotAlignment(const ImVec2& alignment)
{
    IM_ASSERT(nullptr != m_CurrentPin);

    m_PivotAlignment = alignment;
    m_ResolvePivot   = true;
}

void ed::NodeBuilder::Group(const ImVec2& size)
{
    IM_ASSERT(nullptr != m_CurrentNode);
    IM_ASSERT(nullptr == m_CurrentPin);
    IM_ASSERT(false   == m_IsGroup);

    m_IsGroup = true;

    if (IsGroup(m_CurrentNode))
        ImGui::Dummy(m_CurrentNode->m_GroupBounds.GetSize());
    else
        ImGui::Dummy(size);

    m_GroupBounds = ImGui_GetItemRect();
    m_GroupBounds.Floor();
}

ImDrawList* ed::NodeBuilder::GetUserBackgroundDrawList() const
{
    return GetUserBackgroundDrawList(m_CurrentNode);
}

ImDrawList* ed::NodeBuilder::GetUserBackgroundDrawList(Node* node) const
{
    if (node && node->m_IsLive)
    {
        auto drawList = Editor->GetDrawList();
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

bool ed::HintBuilder::Begin(NodeId nodeId)
{
    IM_ASSERT(nullptr == m_CurrentNode);

    auto& view = Editor->GetView();
    auto& rect = Editor->GetRect();

    const float c_min_zoom = 0.75f;
    const float c_max_zoom = 0.50f;

    if (view.Scale > 0.75f)
        return false;

    auto node = Editor->FindNode(nodeId);
    if (!IsGroup(node))
        return false;

    m_CurrentNode = node;

    m_LastChannel = Editor->GetDrawList()->_Splitter._Current;

    Editor->Suspend(SuspendFlags::KeepSplitter);

    const auto alpha = ImMax(0.0f, std::min(1.0f, (view.Scale - c_min_zoom) / (c_max_zoom - c_min_zoom)));

    Editor->GetDrawList()->ChannelsSetCurrent(c_UserChannel_HintsBackground);
    ImGui::PushClipRect(rect.Min + ImVec2(1, 1), rect.Max - ImVec2(1, 1), false);

    Editor->GetDrawList()->ChannelsSetCurrent(c_UserChannel_Hints);
    ImGui::PushClipRect(rect.Min + ImVec2(1, 1), rect.Max - ImVec2(1, 1), false);

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);

    m_IsActive = true;

    return true;
}

void ed::HintBuilder::End()
{
    if (!m_IsActive)
        return;

    ImGui::PopStyleVar();

    Editor->GetDrawList()->ChannelsSetCurrent(c_UserChannel_Hints);
    ImGui::PopClipRect();

    Editor->GetDrawList()->ChannelsSetCurrent(c_UserChannel_HintsBackground);
    ImGui::PopClipRect();

    Editor->GetDrawList()->ChannelsSetCurrent(m_LastChannel);

    Editor->Resume(SuspendFlags::KeepSplitter);

    m_IsActive    = false;
    m_CurrentNode = nullptr;
}

ImVec2 ed::HintBuilder::GetGroupMin()
{
    IM_ASSERT(nullptr != m_CurrentNode);

    return Editor->ToScreen(m_CurrentNode->m_Bounds.Min);
}

ImVec2 ed::HintBuilder::GetGroupMax()
{
    IM_ASSERT(nullptr != m_CurrentNode);

    return Editor->ToScreen(m_CurrentNode->m_Bounds.Max);
}

ImDrawList* ed::HintBuilder::GetForegroundDrawList()
{
    IM_ASSERT(nullptr != m_CurrentNode);

    auto drawList = Editor->GetDrawList();

    drawList->ChannelsSetCurrent(c_UserChannel_Hints);

    return drawList;
}

ImDrawList* ed::HintBuilder::GetBackgroundDrawList()
{
    IM_ASSERT(nullptr != m_CurrentNode);

    auto drawList = Editor->GetDrawList();

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
    IM_ASSERT(var != nullptr);
    VarModifier modifier;
    modifier.Index = varIndex;
    modifier.Value = ImVec4(*var, 0, 0, 0);
    *var = value;
    m_VarStack.push_back(modifier);
}

void ed::Style::PushVar(StyleVar varIndex, const ImVec2& value)
{
    auto* var = GetVarVec2Addr(varIndex);
    IM_ASSERT(var != nullptr);
    VarModifier modifier;
    modifier.Index = varIndex;
    modifier.Value = ImVec4(var->x, var->y, 0, 0);
    *var = value;
    m_VarStack.push_back(modifier);
}

void ed::Style::PushVar(StyleVar varIndex, const ImVec4& value)
{
    auto* var = GetVarVec4Addr(varIndex);
    IM_ASSERT(var != nullptr);
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
        if (auto floatValue = GetVarFloatAddr(modifier.Index))
            *floatValue = modifier.Value.x;
        else if (auto vec2Value = GetVarVec2Addr(modifier.Index))
            *vec2Value = ImVec2(modifier.Value.x, modifier.Value.y);
        else if (auto vec4Value = GetVarVec4Addr(modifier.Index))
            *vec4Value = modifier.Value;
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
        case StyleColor_HighlightLinkBorder: return "HighlightLinkBorder";
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

    IM_ASSERT(0);
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
        case StyleVar_HighlightConnectedLinks:  return &HighlightConnectedLinks;
        case StyleVar_SnapLinkToPinDir:         return &SnapLinkToPinDir;
        case StyleVar_HoveredNodeBorderOffset:  return &HoverNodeBorderOffset;
        case StyleVar_SelectedNodeBorderOffset: return &SelectedNodeBorderOffset;
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
            data.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        }
    }

    return data;
}

std::string ed::Config::LoadNode(NodeId nodeId)
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

bool ed::Config::SaveNode(NodeId nodeId, const std::string& data, SaveReasonFlags flags)
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
