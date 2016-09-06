#include "Editor.h"
#include "Application/imgui_impl_dx11.h"
#include "Drawing.h"
#include "ImGuiInterop.h"
#include <cstdlib> // _itoa
#include <string>
#include <fstream>
#include <bitset>


//------------------------------------------------------------------------------
namespace ed = ax::Editor::Detail;


//------------------------------------------------------------------------------
static const int c_BackgroundChannelCount = 2;
static const int c_LinkChannelCount       = 4;
static const int c_UserLayersCount        = 1;

static const int c_UserLayerChannelStart  = 0;
static const int c_BackgroundChannelStart = c_UserLayerChannelStart  + c_UserLayersCount;
static const int c_LinkStartChannel       = c_BackgroundChannelStart + c_BackgroundChannelCount;
static const int c_NodeStartChannel       = c_LinkStartChannel       + c_LinkChannelCount;

static const int c_BackgroundChannel_Grid          = c_BackgroundChannelStart + 0;
static const int c_BackgroundChannel_SelectionRect = c_BackgroundChannelStart + 1;

static const int c_LinkChannel_Selection  = c_LinkStartChannel + 0;
static const int c_LinkChannel_Links      = c_LinkStartChannel + 1;
static const int c_LinkChannel_Flow       = c_LinkStartChannel + 2;
static const int c_LinkChannel_NewLink    = c_LinkStartChannel + 3;

static const int c_ChannelsPerNode        = 3;
static const int c_NodeBaseChannel        = 0;
static const int c_NodeBackgroundChannel  = 1;
static const int c_NodeContentChannel     = 2;

static const float c_LinkSelectThickness        = 5.0f;  // canvas pixels
static const float c_NavigationZoomMargin       = 0.1f;  // percentage of visible bounds
static const float c_MouseZoomDuration          = 0.15f; // seconds
static const float c_SelectionFadeOutDuration   = 0.15f; // seconds


//------------------------------------------------------------------------------
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

void ed::Log(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    LogV(fmt, args);
    va_end(args);
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

    std::bitset<65536> indexMap;

    int minIndex    = 65536;
    int maxIndex    = 0;
    int indexOffset = 0;
    for (auto& cmd : cmdBuffer)
    {
        int idxCount = cmd.ElemCount;

        if (idxCount == 0) continue;

        for (int i = 0; i < idxCount; ++i)
        {
            int idx = idxRead[indexOffset + i];
            indexMap.set(idx);
            if (minIndex > idx) minIndex = idx;
            if (maxIndex < idx) maxIndex = idx;
        }

        indexOffset += idxCount;
    }

    ++maxIndex;
    for (int idx = minIndex; idx < maxIndex; ++idx)
    {
        if (!indexMap.test(idx))
            continue;

        auto& vtx = vtxBuffer.Data[idx];

        vtx.pos.x = (vtx.pos.x + preOffset.x) * scale.x + postOffset.x;
        vtx.pos.y = (vtx.pos.y + preOffset.y) * scale.y + postOffset.y;
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
void ed::Pin::Draw(ImDrawList* drawList)
{
    drawList->AddRectFilled(to_imvec(Bounds.top_left()), to_imvec(Bounds.bottom_right()),
        Color, Rounding, Corners);

    if (BorderWidth > 0.0f)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_AntiAliasFringeScale, 1.0f);
        drawList->AddRect(to_imvec(Bounds.top_left()), to_imvec(Bounds.bottom_right()),
            BorderColor, Rounding, Corners, BorderWidth);
        ImGui::PopStyleVar();
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

void ed::Node::Draw(ImDrawList* drawList)
{
    drawList->AddRectFilled(
        to_imvec(Bounds.top_left()),
        to_imvec(Bounds.bottom_right()),
        Color, Rounding);

    DrawBorder(drawList, BorderColor, BorderWidth);
}

void ed::Node::DrawBorder(ImDrawList* drawList, ImU32 color, float thickness)
{
    if (thickness > 0.0f)
    {
        drawList->AddRect(to_imvec(Bounds.top_left()), to_imvec(Bounds.bottom_right()),
            color, Rounding, 15, thickness);
    }
}




//------------------------------------------------------------------------------
//
// Link
//
//------------------------------------------------------------------------------
void ed::Link::Draw(ImDrawList* drawList, float extraThickness) const
{
    Draw(drawList, Color, extraThickness);
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

bool ed::Link::TestHit(const ax::rectf& rect) const
{
    if (!IsLive)
        return false;

    const auto bounds = GetBounds();

    if (rect.contains(bounds))
        return true;

    if (!rect.intersects(bounds))
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
            bounds = make_union(bounds, rectf(min, max));
        }

        if (EndPin->ArrowSize)
        {
            const auto end_dir = curve.tangent(0.0f).normalized();
            const auto p0 = curve.p3;
            const auto p1 = curve.p3 + end_dir * EndPin->ArrowSize;
            const auto min = p0.cwise_min(p1);
            const auto max = p0.cwise_max(p1);
            bounds = make_union(bounds, rectf(min, max));
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
ed::Context::Context(const ax::Editor::Config* config):
    Nodes(),
    Pins(),
    Links(),
    SelectionId(1),
    LastActiveLink(nullptr),
    MousePosBackup(0, 0),
    MouseClickPosBackup(),
    Canvas(),
    IsSuspended(false),
    NodeBuilder(this),
    CurrentAction(nullptr),
    ScrollAction(this),
    DragAction(this),
    SelectAction(this),
    ContextMenuAction(this),
    CreateItemAction(this),
    DeleteItemsAction(this),
    AnimationControllers{ &FlowAnimationController },
    FlowAnimationController(this),
    IsInitialized(false),
    Settings(),
    Config(config ? *config : ax::Editor::Config())
{
}

ed::Context::~Context()
{
    if (IsInitialized)
        SaveSettings();

    for (auto link : Links) delete link;
    for (auto pin  : Pins)  delete pin;
    for (auto node : Nodes) delete node;
}

void ed::Context::Begin(const char* id, const ImVec2& size)
{
    if (!IsInitialized)
    {
        LoadSettings();
        IsInitialized = true;
    }

    //ImGui::LogToClipboard();
    //Log("---- begin ----");

    for (auto node : Nodes) node->IsLive = false;
    for (auto pin  : Pins)   pin->IsLive = false;
    for (auto link : Links) link->IsLive = false;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_ChildWindowBg, ImColor(0, 0, 0, 0));
    ImGui::BeginChild(id, size, false,
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse);

    ScrollAction.SetWindow(ImGui::GetWindowPos(), ImGui::GetWindowSize());

    Canvas = ScrollAction.GetCanvas();

    ImGui::PushStyleVar(ImGuiStyleVar_AntiAliasFringeScale, std::min(std::max(Canvas.InvZoom.x, Canvas.InvZoom.y), 1.0f));

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

void ed::Context::End()
{
    auto& io          = ImGui::GetIO();
    auto  control     = ComputeControl();
    auto  drawList    = ImGui::GetWindowDrawList();
    auto& editorStyle = GetStyle();

    bool isSelecting = CurrentAction && CurrentAction->AsSelect() != nullptr;

    // Draw links
    drawList->ChannelsSetCurrent(c_LinkChannel_Links);
    for (auto link : Links)
    {
        if (!link->IsLive || !link->IsVisible())
            continue;

        float extraThickness = !isSelecting && !IsSelected(link) && (control.HotLink == link || control.ActiveLink == link) ? 2.0f : 0.0f;

        link->Draw(drawList, extraThickness);
    }

    // Highlight selected objects
    {
        auto selectedObjects = &SelectedObjects;
        if (auto selectAction = CurrentAction ? CurrentAction->AsSelect() : nullptr)
            selectedObjects = &selectAction->CandidateObjects;

        if (!selectedObjects->empty())
        {
            const auto nodeBorderColor = GetColor(StyleColor_SelNodeBorder);
            const auto linkBorderColor = GetColor(StyleColor_SelLinkBorder);

            for (auto selectedObject : *selectedObjects)
            {
                if (auto selectedNode = selectedObject->AsNode())
                {
                    if (selectedNode->IsVisible())
                    {
                        drawList->ChannelsSetCurrent(selectedNode->Channel + c_NodeBaseChannel);

                        selectedNode->DrawBorder(drawList, nodeBorderColor, editorStyle.SelectedNodeBorderWidth);
                    }
                }
                else if (auto selectedLink = selectedObject->AsLink())
                {
                    if (selectedLink->IsVisible())
                    {
                        drawList->ChannelsSetCurrent(c_LinkChannel_Selection);

                        selectedLink->Draw(drawList, linkBorderColor, 4.5f);
                    }
                }
            }
        }
    }

    if (!isSelecting)
    {
        // #TODO: Simplify this

        bool allowPinHighlight = true;

        // Highlight selected node
        auto hotNode = control.HotNode;
        if (CurrentAction && CurrentAction->AsDrag() && CurrentAction->AsDrag()->DraggedObject && CurrentAction->AsDrag()->DraggedObject->AsNode())
        {
            hotNode = CurrentAction->AsDrag()->DraggedObject->AsNode();
            if (hotNode != nullptr)
                allowPinHighlight = false; // Don't highlight pins when dragging nodes
        }

        // Highlight node or link but never both at once
        if (hotNode && !IsSelected(hotNode))
        {
            const auto rectMin = to_imvec(hotNode->Bounds.top_left());
            const auto rectMax = to_imvec(hotNode->Bounds.bottom_right());

            drawList->ChannelsSetCurrent(hotNode->Channel + c_NodeBaseChannel);

            hotNode->DrawBorder(drawList, GetColor(StyleColor_HovNodeBorder), editorStyle.HoveredNodeBorderWidth);
        }
        else
        {
            // Highlight hovered link
            if (control.HotLink && !IsSelected(control.HotLink))
            {
                drawList->ChannelsSetCurrent(c_LinkChannel_Selection);

                control.HotLink->Draw(drawList, GetColor(StyleColor_HovLinkBorder), 4.5f);

                allowPinHighlight = false;
            }
        }

        // Highlight hovered pin
        if (allowPinHighlight)
        {
            if (auto hotPin = control.HotPin)
            {
                const auto rectMin = to_imvec(hotPin->Bounds.top_left());
                const auto rectMax = to_imvec(hotPin->Bounds.bottom_right());

                drawList->ChannelsSetCurrent(hotPin->Node->Channel + c_NodeBackgroundChannel);

                hotPin->Draw(drawList);
            }
        }
    }

    for (auto controller : AnimationControllers)
        controller->Draw(drawList);

    if (CurrentAction && !CurrentAction->Process(control))
        CurrentAction = nullptr;

    if (CurrentAction || io.MouseDown[0] || io.MouseDown[1] || io.MouseDown[2])
        ScrollAction.StopNavigation();

    if (nullptr == CurrentAction)
    {
        if (ScrollAction.Accept(control))
            CurrentAction = &ScrollAction;
        else if (DragAction.Accept(control))
            CurrentAction = &DragAction;
        else if (SelectAction.Accept(control))
            CurrentAction = &SelectAction;
        else if (ContextMenuAction.Accept(control))
            CurrentAction = &ContextMenuAction;
        else if (CreateItemAction.Accept(control))
            CurrentAction = &CreateItemAction;
        else if (DeleteItemsAction.Accept(control))
            CurrentAction = &DeleteItemsAction;
    }

    SelectAction.Draw(drawList);

    // Bring active node to front
    std::stable_sort(Nodes.begin(), Nodes.end(), [&control](Node* lhs, Node* rhs)
    {
        if (lhs->IsLive && rhs->IsLive)
            return (rhs == control.ActiveNode);
        else
            return lhs->IsLive;
    });

    // Every node has few channels assigned. Grow channel list
    // to hold twice as much of channels and place them in
    // node drawing order.
    {
        // Reserve two additional channels for sorted list of channels
        auto nodeChannelCount = drawList->_ChannelsCount;
        ImDrawList_ChannelsGrow(drawList, (drawList->_ChannelsCount - 1) * 2 + 1);

        int targetChannel = nodeChannelCount;
        for (auto node : Nodes)
        {
            if (!node->IsLive)
                continue;

            for (int i = 0; i < c_ChannelsPerNode; ++i)
                ImDrawList_SwapChannels(drawList, node->Channel + i, targetChannel + i);
            node->Channel = targetChannel;
            targetChannel += c_ChannelsPerNode;
        }
    }

    ImGui::PopClipRect();

    // Draw grid
    {
        drawList->ChannelsSetCurrent(c_BackgroundChannel_Grid);

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

    auto userChannel = drawList->_ChannelsCount;
    ImDrawList_ChannelsGrow(drawList, userChannel + c_UserLayersCount);
    for (int i = 0; i < c_UserLayersCount; ++i)
        ImDrawList_SwapChannels(drawList, userChannel + i, c_UserLayerChannelStart + i);

    {
        auto preOffset  = ImVec2(0, 0);
        auto postOffset = Canvas.WindowScreenPos + Canvas.ClientOrigin;
        auto scale      = Canvas.Zoom;

        ImDrawList_TransformChannels(drawList,                                 0, c_BackgroundChannel_Grid, preOffset, scale, postOffset);
        ImDrawList_TransformChannels(drawList, c_BackgroundChannel_SelectionRect, drawList->_ChannelsCount, preOffset, scale, postOffset);

        auto clipTranslation = Canvas.WindowScreenPos - Canvas.FromScreen(Canvas.WindowScreenPos);
        ImGui::PushClipRect(Canvas.WindowScreenPos + ImVec2(1, 1), Canvas.WindowScreenPos + Canvas.WindowScreenSize - ImVec2(1, 1), false);
        ImDrawList_TranslateAndClampClipRects(drawList,                                 0, c_BackgroundChannel_Grid, clipTranslation);
        ImDrawList_TranslateAndClampClipRects(drawList, c_BackgroundChannel_SelectionRect, drawList->_ChannelsCount, clipTranslation);
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

    // ShowMetrics(control);

    // fringe scale
    ImGui::PopStyleVar();

    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    ReleaseMouse();

    if (Settings.Dirty)
    {
        Settings.Dirty = false;
        SaveSettings();
        Settings.Reason = SaveReasonFlags::None;
    }
}

bool ed::Context::DoLink(int id, int startPinId, int endPinId, ImU32 color, float thickness)
{
    auto& editorStyle = GetStyle();

    auto link     = GetLink(id);
    auto startPin = FindPin(startPinId);
    auto endPin   = FindPin(endPinId);

    if (!startPin || !startPin->IsLive || !endPin || !endPin->IsLive)
        return false;

    link->StartPin      = startPin;
    link->EndPin        = endPin;
    link->Color         = color;
    link->Thickness     = thickness;
    link->IsLive        = true;

    link->UpdateEndpoints();

    return true;
}

void ed::Context::SetNodePosition(int nodeId, const ImVec2& position)
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
        MarkSettingsDirty(Editor::SaveReasonFlags::NodePosition);
    }
}

ImVec2 ed::Context::GetNodePosition(int nodeId)
{
    auto node = FindNode(nodeId);
    if (!node)
        return ImVec2(FLT_MAX, FLT_MAX);

    return to_imvec(node->Bounds.location);
}

void ed::Context::ClearSelection()
{
    SelectedObjects.clear();
}

void ed::Context::SelectObject(Object* object)
{
    SelectedObjects.push_back(object);
}

void ed::Context::DeselectObject(Object* object)
{
    auto objectIt = std::find(SelectedObjects.begin(), SelectedObjects.end(), object);
    if (objectIt != SelectedObjects.end())
        SelectedObjects.erase(objectIt);
}

void ed::Context::SetSelectedObject(Object* object)
{
    ClearSelection();
    SelectObject(object);
}

void ed::Context::ToggleObjectSelection(Object* object)
{
    if (IsSelected(object))
        DeselectObject(object);
    else
        SelectObject(object);
}

bool ed::Context::IsSelected(Object* object)
{
    return std::find(SelectedObjects.begin(), SelectedObjects.end(), object) != SelectedObjects.end();
}

const ed::vector<ed::Object*>& ed::Context::GetSelectedObjects()
{
    return SelectedObjects;
}

bool ed::Context::IsAnyNodeSelected()
{
    for (auto object : SelectedObjects)
        if (object->AsNode())
            return true;

    return false;
}

bool ed::Context::IsAnyLinkSelected()
{
    for (auto object : SelectedObjects)
        if (object->AsLink())
            return true;

    return false;
}

bool ed::Context::HasSelectionChanged()
{
    return LastSelectedObjects != SelectedObjects;
}

void ed::Context::FindNodesInRect(const ax::rectf& r, vector<Node*>& result)
{
    result.clear();

    if (r.is_empty())
        return;

    for (auto node : Nodes)
        if (node->TestHit(r))
            result.push_back(node);
}

void ed::Context::FindLinksInRect(const ax::rectf& r, vector<Link*>& result)
{
    using namespace ImGuiInterop;

    result.clear();

    if (r.is_empty())
        return;

    for (auto link : Links)
        if (link->TestHit(r))
            result.push_back(link);
}

void ed::Context::FindLinksForNode(int nodeId, vector<Link*>& result, bool add)
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

void ed::Context::NotifyLinkDeleted(Link* link)
{
    if (LastActiveLink == link)
        LastActiveLink = nullptr;
}

void ed::Context::Suspend()
{
    assert(!IsSuspended);

    IsSuspended = true;

    ReleaseMouse();
}

void ed::Context::Resume()
{
    assert(IsSuspended);

    IsSuspended = false;

    CaptureMouse();
}

ed::Pin* ed::Context::CreatePin(int id, PinKind kind)
{
    assert(nullptr == FindObject(id));
    auto pin = new Pin(id, kind);
    Pins.push_back(pin);
    return pin;
}

ed::Node* ed::Context::CreateNode(int id)
{
    assert(nullptr == FindObject(id));
    auto node = new Node(id);
    Nodes.push_back(node);

    auto settings = FindNodeSettings(id);
    if (!settings)
        settings = AddNodeSettings(id);
    else
        node->Bounds.location = to_point(settings->Location);

    settings->WasUsed = true;

    node->IsLive = false;

    return node;
}

ed::Link* ed::Context::CreateLink(int id)
{
    assert(nullptr == FindObject(id));
    auto link = new Link(id);
    Links.push_back(link);

    return link;
}

void ed::Context::DestroyObject(Node* node)
{
}

ed::Object* ed::Context::FindObject(int id)
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

ed::Node* ed::Context::FindNode(int id)
{
    return FindItemIn(Nodes, id);
}

ed::Pin* ed::Context::FindPin(int id)
{
    return FindItemIn(Pins, id);
}

ed::Link* ed::Context::FindLink(int id)
{
    return FindItemIn(Links, id);
}

ed::Node* ed::Context::GetNode(int id)
{
    auto node = FindNode(id);
    if (!node)
        node = CreateNode(id);
    return node;
}

ed::Pin* ed::Context::GetPin(int id, PinKind kind)
{
    if (auto pin = FindPin(id))
    {
        pin->Kind = kind;
        return pin;
    }
    else
        return CreatePin(id, kind);
}

ed::Link* ed::Context::GetLink(int id)
{
    if (auto link = FindLink(id))
        return link;
    else
        return CreateLink(id);
}

ed::NodeSettings* ed::Context::FindNodeSettings(int id)
{
    for (auto& nodeSettings : Settings.Nodes)
        if (nodeSettings.ID == id)
            return &nodeSettings;

    return nullptr;
}

ed::NodeSettings* ed::Context::AddNodeSettings(int id)
{
    Settings.Nodes.push_back(NodeSettings(id));
    auto result = &Settings.Nodes.back();
    return result;
}

void ed::Context::LoadSettings()
{
    namespace json = picojson;

    std::vector<char> savedData;

    if (Config.LoadSettings)
    {
        auto dataSize = Config.LoadSettings(nullptr, Config.UserPointer);
        if (dataSize > 0)
        {
            savedData.resize(dataSize);
            Config.LoadSettings(savedData.data(), Config.UserPointer);
        }
    }
    else if (Config.SettingsFile)
    {
        std::ifstream settingsFile(Config.SettingsFile);
        if (settingsFile)
            std::copy(std::istream_iterator<char>(settingsFile), std::istream_iterator<char>(), std::back_inserter(savedData));
    }

    if (savedData.empty())
        return;

    json::value settingsValue;
    auto error = json::parse(settingsValue, savedData.begin(), savedData.end());
    if (!error.empty() || settingsValue.is<json::null>())
        return;

    if (!settingsValue.is<json::object>())
        return;

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
    auto& nodesValue     = settingsValue.get("nodes");

    if (nodesValue.is<json::object>())
    {
        for (auto& node : nodesValue.get<json::object>())
        {
            auto id       = static_cast<int>(strtoll(node.first.c_str(), nullptr, 10));
            auto settings = FindNodeSettings(id);
            if (!settings)
                settings = AddNodeSettings(id);

            auto& nodeData = node.second;

            auto locationValue = nodeData.get("location");
            if (!tryParseVector(nodeData.get("location"), settings->Location))
                settings->Location = ImVec2(0, 0);
        }
    }

    auto& viewValue = settingsValue.get("view");
    if (viewValue.is<json::object>())
    {
        auto& viewScrollValue = viewValue.get("scroll");
        auto& viewZoomValue   = viewValue.get("zoom");

        if (!tryParseVector(viewValue.get("scroll"), Settings.ViewScroll))
            Settings.ViewScroll = ImVec2(0, 0);

        Settings.ViewZoom = viewZoomValue.is<double>() ? static_cast<float>(viewZoomValue.get<double>()) : 1.0f;
    }

    ScrollAction.Scroll = Settings.ViewScroll;
    ScrollAction.Zoom   = Settings.ViewZoom;
}

void ed::Context::SaveSettings()
{
    namespace json = picojson;

    auto serializeVector = [](ImVec2 p) -> json::object
    {
        json::object result;
        result["x"] = json::value(static_cast<double>(p.x));
        result["y"] = json::value(static_cast<double>(p.y));
        return result;
    };

    // update settings data
    for (auto& node : Nodes)
    {
        auto settings = FindNodeSettings(node->ID);
        settings->Location = to_imvec(node->Bounds.location);
    }

    Settings.ViewScroll = ScrollAction.Scroll;
    Settings.ViewZoom   = ScrollAction.Zoom;

    // serializes
    json::object nodes;
    for (auto& node : Settings.Nodes)
    {
        if (!node.WasUsed)
            continue;

        json::object nodeData;
        nodeData["location"] = json::value(serializeVector(node.Location));

        nodes[std::to_string(node.ID)] = json::value(std::move(nodeData));
    }

    json::object view;
    view["scroll"] = json::value(serializeVector(Settings.ViewScroll));
    view["zoom"]   = json::value((double)Settings.ViewZoom);

    json::object settings;
    settings["nodes"] = json::value(std::move(nodes));
    settings["view"]  = json::value(std::move(view));

    json::value settingsValue(std::move(settings));

    auto data = settingsValue.serialize(false);
    if (Config.SaveSettings)
        Config.SaveSettings(data.c_str(), Config.UserPointer, Settings.Reason);
    else if (Config.SettingsFile)
    {
        std::ofstream settingsFile(Config.SettingsFile);
        if (settingsFile)
            settingsFile << data;
    }
}

void ed::Context::MarkSettingsDirty(SaveReasonFlags reason)
{
    Settings.Dirty  = true;
    Settings.Reason = Settings.Reason | reason;
}

ed::Link* ed::Context::FindLinkAt(const ax::point& p)
{
    for (auto& link : Links)
        if (link->TestHit(to_imvec(p), c_LinkSelectThickness))
            return link;

    return nullptr;
}

ImU32 ed::Context::GetColor(StyleColor colorIndex) const
{
    return ImColor(Style.Colors[colorIndex]);
}

ImU32 ed::Context::GetColor(StyleColor colorIndex, float alpha) const
{
    auto color = Style.Colors[colorIndex];
    return ImColor(color.x, color.y, color.z, color.w * alpha);
}

void ed::Context::RegisterAnimation(Animation* animation)
{
    LiveAnimations.push_back(animation);
}

void ed::Context::UnregisterAnimation(Animation* animation)
{
    auto it = std::find(LiveAnimations.begin(), LiveAnimations.end(), animation);
    if (it != LiveAnimations.end())
        LiveAnimations.erase(it);
}

void ed::Context::UpdateAnimations()
{
    LastLiveAnimations = LiveAnimations;

    for (auto animation : LastLiveAnimations)
    {
        const bool isLive = (std::find(LiveAnimations.begin(), LiveAnimations.end(), animation) != LiveAnimations.end());

        if (isLive)
            animation->Update();
    }
}

void ed::Context::Flow(Link* link)
{
    FlowAnimationController.Flow(link);
}

void ed::Context::SetUserContext()
{
    const auto mousePos = ImGui::GetMousePos();

    // Move drawing cursor to mouse location and prepare layer for
    // content added by user.
    ImGui::SetCursorScreenPos(ImVec2(floorf(mousePos.x), floorf(mousePos.y)));

    auto drawList = ImGui::GetWindowDrawList();
    drawList->ChannelsSetCurrent(c_UserLayerChannelStart);

    // #debug
    //drawList->AddCircleFilled(ImGui::GetMousePos(), 4, IM_COL32(0, 255, 0, 255));
}

ed::Control ed::Context::ComputeControl()
{
    if (!ImGui::IsWindowHovered())
        return ed::Control(nullptr, nullptr, nullptr, false, false, false);

    const auto mousePos = to_point(ImGui::GetMousePos());

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
        //if (id == 0)
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

    const auto editorRect = rect(to_point(Canvas.FromClient(ImVec2(0, 0))), to_size(Canvas.ClientSize));

    // Check for interaction with background.
    auto backgroundClicked  = emitInteractiveArea(0, editorRect);
    auto isBackgroundActive = ImGui::IsItemActive();
    auto isBackgroundHot    = !hotObject && ImGui::IsItemHoveredRect();
    auto isDragging         = ImGui::IsMouseDragging(0, 1) || ImGui::IsMouseDragging(1, 1) || ImGui::IsMouseDragging(2, 1);

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

void ed::Context::ShowMetrics(const Control& control)
{
    auto& io = ImGui::GetIO();

    ImGui::SetCursorScreenPos(Canvas.WindowScreenPos);
    auto getObjectName = [](Object* object)
    {
        if (!object) return "";
        else if (object->AsNode()) return "Node";
        else if (object->AsPin())  return "Pin";
        else if (object->AsLink()) return "Link";
        else return "";
    };

    auto liveNodeCount = (int)std::count_if(Nodes.begin(), Nodes.end(), [](Node* node) { return node->IsLive; });
    auto livePinCount  = (int)std::count_if(Pins.begin(),  Pins.end(),  [](Pin*  pin)  { return  pin->IsLive; });
    auto liveLinkCount = (int)std::count_if(Links.begin(), Links.end(), [](Link* link) { return link->IsLive; });

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
    ImGui::Text("Hot Object: %s (%d)", getObjectName(control.HotObject), control.HotObject ? control.HotObject->ID : 0);
    if (auto node = control.HotObject ? control.HotObject->AsNode() : nullptr)
    {
        ImGui::SameLine();
        ImGui::Text("{ x=%d y=%d w=%d h=%d }", node->Bounds.x, node->Bounds.y, node->Bounds.w, node->Bounds.h);
    }
    ImGui::Text("Active Object: %s (%d)", getObjectName(control.ActiveObject), control.ActiveObject ? control.ActiveObject->ID : 0);
    if (auto node = control.ActiveObject ? control.ActiveObject->AsNode() : nullptr)
    {
        ImGui::SameLine();
        ImGui::Text("{ x=%d y=%d w=%d h=%d }", node->Bounds.x, node->Bounds.y, node->Bounds.w, node->Bounds.h);
    }
    ImGui::Text("Action: %s", CurrentAction ? CurrentAction->GetName() : "<none>");
    //ImGui::Text("Clicked Object: %s (%d)", getObjectName(control.ClickedObject), control.ClickedObject ? control.ClickedObject->ID : 0);
    ScrollAction.ShowMetrics();
    DragAction.ShowMetrics();
    SelectAction.ShowMetrics();
    ContextMenuAction.ShowMetrics();
    CreateItemAction.ShowMetrics();
    DeleteItemsAction.ShowMetrics();
    ImGui::EndGroup();
}

void ed::Context::CaptureMouse()
{
    auto& io = ImGui::GetIO();

    io.MousePos = Canvas.FromScreen(MousePosBackup);

    for (int i = 0; i < 5; ++i)
        io.MouseClickedPos[i] = Canvas.FromScreen(MouseClickPosBackup[i]);
}

void ed::Context::ReleaseMouse()
{
    auto& io = ImGui::GetIO();

    io.MousePos = MousePosBackup;

    for (int i = 0; i < 5; ++i)
        io.MouseClickedPos[i] = MouseClickPosBackup[i];
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

ImVec2 ed::Canvas::FromScreen(ImVec2 point)
{
    return ImVec2(
        (point.x - WindowScreenPos.x - ClientOrigin.x) * InvZoom.x,
        (point.y - WindowScreenPos.y - ClientOrigin.y) * InvZoom.y);
}

ImVec2 ed::Canvas::ToScreen(ImVec2 point)
{
    return ImVec2(
        (point.x * Zoom.x + ClientOrigin.x + WindowScreenPos.x),
        (point.y * Zoom.y + ClientOrigin.y + WindowScreenPos.y));
}

ImVec2 ed::Canvas::FromClient(ImVec2 point)
{
    return ImVec2(
        (point.x - ClientOrigin.x) * InvZoom.x,
        (point.y - ClientOrigin.y) * InvZoom.y);
}

ImVec2 ed::Canvas::ToClient(ImVec2 point)
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
ed::Animation::Animation(Context* editor):
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
// Scroll Animation
//
//------------------------------------------------------------------------------
ed::ScrollAnimation::ScrollAnimation(Context* editor, ScrollAction& scrollAction):
    Animation(editor),
    Action(scrollAction)
{
}

void ed::ScrollAnimation::NavigateTo(const ImVec2& target, float targetZoom, float duration)
{
    Stop();

    Start      = Action.Scroll;
    StartZoom  = Action.Zoom;
    Target     = target;
    TargetZoom = targetZoom;

    Play(duration);
}

void ed::ScrollAnimation::OnUpdate(float progress)
{
    Action.Scroll = easing::ease_out_quad(Start,     Target     - Start,     progress);
    Action.Zoom   = easing::ease_out_quad(StartZoom, TargetZoom - StartZoom, progress);
}

void ed::ScrollAnimation::OnStop()
{
    Editor->MarkSettingsDirty(SaveReasonFlags::Navigation);
}

void ed::ScrollAnimation::OnFinish()
{
    Action.Scroll = Target;
    Action.Zoom   = TargetZoom;

    Editor->MarkSettingsDirty(SaveReasonFlags::Navigation);
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
    if (!IsPlaying() || !IsLinkValid())
        return;

    if (!IsPathValid())
        UpdatePath();

    Offset = fmodf(Offset, MarkerDistance);

    const auto progress    = GetProgress();

    const auto flowAlpha = 1.0f - progress * progress;
    const auto flowColor = Editor->GetColor(StyleColor_Flow, flowAlpha);
    const auto flowPath  = Link->GetCurve();
    drawList->AddBezierCurve(to_imvec(flowPath.p0), to_imvec(flowPath.p1), to_imvec(flowPath.p2), to_imvec(flowPath.p3), flowColor, 4.0f);

    if (IsPathValid())
    {
        const auto markerAlpha  = powf(1.0f - progress, 0.5f);
        const auto markerRadius = 4.0f * (1.0f - progress) + 2.0f;
        const auto markerColor  = Editor->GetColor(StyleColor_FlowMarker, markerAlpha);

        for (float d = 0.0f; d < PathLength; d += MarkerDistance)
        {
            auto point = SamplePath(d + Offset);

            drawList->AddCircleFilled(point, markerRadius, markerColor);
        }
    }
}

bool ed::FlowAnimation::IsLinkValid() const
{
    return Link && Link->IsLive;
}

bool ed::FlowAnimation::IsPathValid() const
{
    return !Path.empty() && PathLength > 0.0f && Link->Start == LastStart && Link->End == LastEnd;
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
    cubic_bezier_fixed_step(collectPointsCallback, curve.p0, curve.p1, curve.p2, curve.p3, step, true, 0.5f, 0.001f);
}

void ed::FlowAnimation::ClearPath()
{
    vector<CurvePoint>().swap(Path);
    PathLength = 0.0f;
}

ImVec2 ed::FlowAnimation::SamplePath(float distance)
{
    distance = std::max(0.0f, std::min(distance, PathLength));

    auto endPointIt = std::find_if(Path.begin(), Path.end(), [distance](const CurvePoint& p) { return distance < p.Distance; });
    if (endPointIt == Path.end() || endPointIt == Path.begin())
        return ImVec2(0, 0);

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
ed::FlowAnimationController::FlowAnimationController(Context* editor):
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
// Scroll Action
//
//------------------------------------------------------------------------------
const float ed::ScrollAction::s_ZoomLevels[] =
{
    /*0.1f, 0.15f, */0.20f, 0.25f, 0.33f, 0.5f, 0.75f, 1.0f, 1.25f, 1.50f, 2.0f, 2.5f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f
};

const int ed::ScrollAction::s_ZoomLevelCount = sizeof(s_ZoomLevels) / sizeof(*s_ZoomLevels);

ed::ScrollAction::ScrollAction(Context* editor):
    EditorAction(editor),
    IsActive(false),
    Zoom(1),
    Scroll(0, 0),
    Animation(editor, *this),
    WindowScreenPos(0, 0),
    WindowScreenSize(0, 0),
    ScrollStart(0, 0),
    Reason(NavigationReason::Unknown),
    LastSelectionId(0),
    LastObject(nullptr)
{
}

bool ax::Editor::Detail::ScrollAction::Accept(const Control& control)
{
    assert(!IsActive);

    if (IsActive)
        return false;

    if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemActive() && ImGui::IsMouseDragging(2, 0.0f))
    {
        IsActive    = true;
        ScrollStart = Scroll;
        Scroll      = ScrollStart - ImGui::GetMouseDragDelta(2) * Zoom;
    }

    auto& io = ImGui::GetIO();

    if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_F)))
    {
        const auto  allowZoomIn = io.KeyShift;

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
        else if(control.HotObject)
        {
            auto object = control.HotObject->AsPin() ? control.HotObject->AsPin()->Node : control.HotObject;

            if (Reason != NavigationReason::Object || LastObject != object || allowZoomIn)
            {
                LastObject = object;
                auto bounds = (control.HotObject->AsPin() ? control.HotObject->AsPin()->Node : control.HotObject)->GetBounds();
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

    if (io.MouseWheel && ImGui::IsWindowHovered() && !ImGui::IsAnyItemActive())
    {
        auto savedScroll = Scroll;
        auto savedZoom   = Zoom;

        Animation.Finish();

        auto mousePos     = io.MousePos;
        auto steps        = (int)io.MouseWheel;
        auto newZoom      = MatchZoom(steps, s_ZoomLevels[steps < 0 ? 0 : s_ZoomLevelCount - 1]);

        auto oldCanvas    = GetCanvas(false);
        Zoom = newZoom;
        auto newCanvas    = GetCanvas(false);

        auto screenPos    = oldCanvas.ToScreen(mousePos);
        auto canvasPos    = newCanvas.FromScreen(screenPos);

        auto offset       = (canvasPos - mousePos) * Zoom;
        auto targetScroll = Scroll - offset;

        Scroll = savedScroll;
        Zoom   = savedZoom;

        NavigateTo(targetScroll, newZoom, c_MouseZoomDuration, NavigationReason::MouseZoom);

        return true;
    }

    return IsActive;
}

bool ed::ScrollAction::Process(const Control& control)
{
    if (!IsActive)
        return false;

    if (ImGui::IsMouseDragging(2, 0.0f))
    {
        Scroll = ScrollStart - ImGui::GetMouseDragDelta(2) * Zoom;
    }
    else
    {
        if (Scroll != ScrollStart)
            Editor->MarkSettingsDirty(SaveReasonFlags::Navigation);

        IsActive = false;
    }

    return IsActive;
}

void ed::ScrollAction::ShowMetrics()
{
    EditorAction::ShowMetrics();

    ImGui::Text("%s:", GetName());
    ImGui::Text("    Active: %s", IsActive ? "yes" : "no");
    ImGui::Text("    Scroll: { x=%g y=%g }", Scroll.x, Scroll.y);
    ImGui::Text("    Zoom: %g", Zoom);
}

void ed::ScrollAction::NavigateTo(const ax::rectf& bounds, bool zoomIn, float duration, NavigationReason reason)
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

void ed::ScrollAction::NavigateTo(const ImVec2& target, float targetZoom, float duration, NavigationReason reason)
{
    Reason = reason;

    Animation.NavigateTo(target, targetZoom, duration);
}

void ed::ScrollAction::StopNavigation()
{
    Animation.Stop();
}

void ed::ScrollAction::FinishNavigation()
{
    Animation.Finish();
}

void ed::ScrollAction::SetWindow(ImVec2 position, ImVec2 size)
{
    WindowScreenPos  = position;
    WindowScreenSize = size;
}

ed::Canvas ed::ScrollAction::GetCanvas(bool alignToPixels)
{
    ImVec2 origin = -Scroll;
    if (alignToPixels)
    {
        origin.x = floorf(origin.x);
        origin.y = floorf(origin.y);
    }

    return Canvas(WindowScreenPos, WindowScreenSize, ImVec2(Zoom, Zoom), origin);
}

float ed::ScrollAction::MatchZoom(int steps, float fallbackZoom)
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

int ed::ScrollAction::MatchZoomIndex(int direction)
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
// Drag Action
//
//------------------------------------------------------------------------------
ed::DragAction::DragAction(Context* editor):
    EditorAction(editor),
    IsActive(false),
    DraggedObject(nullptr)
{
}

bool ed::DragAction::Accept(const Control& control)
{
    assert(!IsActive);

    if (IsActive)
        return false;

    if (control.ActiveNode && ImGui::IsMouseDragging(0))
    {
        if (!control.ActiveNode->AcceptDrag())
            return false;

        DraggedObject = control.ActiveNode;

        Objects.resize(0);
        Objects.push_back(DraggedObject);

        if (Editor->IsSelected(DraggedObject))
        {
            for (auto selectedObject : Editor->GetSelectedObjects())
                if (auto selectedNode = selectedObject->AsNode())
                    if (selectedNode != DraggedObject && selectedNode->AcceptDrag())
                        Objects.push_back(selectedNode);
        }

        IsActive = true;
    }

    return IsActive;
}

bool ed::DragAction::Process(const Control& control)
{
    if (!IsActive)
        return false;

    if (control.ActiveNode == DraggedObject)
    {
        auto dragOffset = to_point(ImGui::GetMouseDragDelta(0, 0.0f));

        for (auto object : Objects)
            object->UpdateDrag(dragOffset);
    }
    else if (!control.ActiveNode)
    {
        bool anyModified = false;
        for (auto object : Objects)
            anyModified |= object->EndDrag();

        if (anyModified)
            Editor->MarkSettingsDirty(Editor::SaveReasonFlags::NodePosition);

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
        else if (object->AsNode()) return "Node";
        else if (object->AsPin())  return "Pin";
        else if (object->AsLink()) return "Link";
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
ed::SelectAction::SelectAction(Context* editor):
    EditorAction(editor),
    IsActive(false),
    SelectLinkMode(false),
    StartPoint(),
    Animation(editor)
{
}

bool ed::SelectAction::Accept(const Control& control)
{
    assert(!IsActive);

    if (IsActive)
        return false;

    auto& io = ImGui::GetIO();
    SelectLinkMode = io.KeyAlt;

    SelectedObjectsAtStart.clear();

    if (control.BackgroundActive && ImGui::IsMouseDragging(0, 0))
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

    return IsActive;
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
ed::ContextMenuAction::ContextMenuAction(Context* editor):
    EditorAction(editor),
    CurrentMenu(Menu::None),
    ContextId(0)
{
}

bool ed::ContextMenuAction::Accept(const Control& control)
{
    if (!ImGui::IsMouseClicked(1))
        return false;

    if (auto hotObejct = control.HotObject)
    {
        if (hotObejct->AsNode())
            CurrentMenu = Node;
        else if (hotObejct->AsPin())
            CurrentMenu = Pin;
        else if (hotObejct->AsLink())
            CurrentMenu = Link;

        if (CurrentMenu != None)
        {
            ContextId = hotObejct->ID;
            return true;
        }
    }
    else if (control.BackgroundHot)
    {
        CurrentMenu = Background;
        return true;
    }

    return false;
}

bool ed::ContextMenuAction::Process(const Control& control)
{
    CurrentMenu = None;
    ContextId   = 0;
    return false;
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
ed::CreateItemAction::CreateItemAction(Context* editor):
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
    DraggedPin(nullptr)
{
}

bool ed::CreateItemAction::Accept(const Control& control)
{
    assert(!IsActive);

    if (IsActive)
        return false;

    if (control.ActivePin && ImGui::IsMouseDragging(0))
    {
        DraggedPin = control.ActivePin;
        DragStart(DraggedPin);

        Editor->ClearSelection();
    }
    else
        return false;

    IsActive = true;

    return true;
}

bool ed::CreateItemAction::Process(const Control& control)
{
    assert(IsActive);

    if (!IsActive)
        return false;

    if (DraggedPin && control.ActivePin == DraggedPin && (CurrentStage == Possible))
    {
        const auto draggingFromSource = (DraggedPin->Kind == PinKind::Source);

        ed::Pin cursorPin(0, draggingFromSource ? PinKind::Target : PinKind::Source);
        cursorPin.Pivot    = ax::rectf(to_pointf(ImGui::GetMousePos()), sizef(0, 0));
        cursorPin.Dir      = -DraggedPin->Dir;
        cursorPin.Strength =  DraggedPin->Strength;

        ed::Link candidate(0);
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
        candidate.Draw(drawList, LinkThickness);
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

    Editor->SetUserContext();

    return True;
}

ed::CreateItemAction::Result ed::CreateItemAction::QueryNode(int* pinId)
{
    IM_ASSERT(InActive);

    if (!InActive || CurrentStage == None || ItemType != Node)
        return Indeterminate;

    *pinId = LinkStart ? LinkStart->ID : 0;

    Editor->SetUserContext();

    return True;
}




//------------------------------------------------------------------------------
//
// Delete Items Action
//
//------------------------------------------------------------------------------
ed::DeleteItemsAction::DeleteItemsAction(Context* editor):
    EditorAction(editor),
    IsActive(false),
    InInteraction(false),
    CurrentItemType(Unknown),
    UserAction(Undetermined)
{
}

bool ed::DeleteItemsAction::Accept(const Control& control)
{
    assert(!IsActive);

    if (IsActive)
        return false;

    auto& io = ImGui::GetIO();
    if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Delete)))
    {
        auto& selection = Editor->GetSelectedObjects();
        if (!selection.empty())
        {
            CandidateObjects = selection;

            vector<ed::Link*> links;
            for (auto object : selection)
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

            IsActive = true;
            return true;
        }
    }
    else if (control.ClickedLink && io.KeyAlt)
    {
        CandidateObjects.clear();
        CandidateObjects.push_back(control.ClickedLink);
        IsActive = true;
        return true;
    }

    return IsActive;
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
ed::NodeBuilder::NodeBuilder(Context* editor):
    Editor(editor),
    CurrentNode(nullptr),
    CurrentPin(nullptr)
{
}

void ed::NodeBuilder::Begin(int nodeId)
{
    assert(nullptr == CurrentNode);

    CurrentNode = Editor->GetNode(nodeId);

    // Position node on screen
    ImGui::SetCursorScreenPos(to_imvec(CurrentNode->Bounds.location));

    auto& editorStyle = Editor->GetStyle();

    const auto alpha = ImGui::GetStyle().Alpha;

    CurrentNode->IsLive      = true;
    CurrentNode->LastPin     = nullptr;
    CurrentNode->Color       = Editor->GetColor(StyleColor_NodeBg, alpha);
    CurrentNode->BorderColor = Editor->GetColor(StyleColor_NodeBorder, alpha);
    CurrentNode->BorderWidth = editorStyle.NodeBorderWidth;
    CurrentNode->Rounding    = editorStyle.NodeRounding;

    // Grow channel list and select user channel
    if (auto drawList = ImGui::GetWindowDrawList())
    {
        CurrentNode->Channel = drawList->_ChannelsCount;
        ImDrawList_ChannelsGrow(drawList, drawList->_ChannelsCount + c_ChannelsPerNode);
        drawList->ChannelsSetCurrent(CurrentNode->Channel + c_NodeContentChannel);
    }

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 1.0f);

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
        Editor->MarkSettingsDirty(SaveReasonFlags::NodeSize);
    }

    if (CurrentNode->IsVisible())
        CurrentNode->Draw(GetBackgroundDrawList());

    ImGui::PopStyleVar();

    CurrentNode = nullptr;
}

void ed::NodeBuilder::BeginPin(int pinId, PinKind kind)
{
    assert(nullptr == CurrentPin);

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

ImDrawList* ed::NodeBuilder::GetBackgroundDrawList() const
{
    return GetBackgroundDrawList(CurrentNode);
}

ImDrawList* ed::NodeBuilder::GetBackgroundDrawList(Node* node) const
{
    if (node && node->IsLive)
    {
        auto drawList = ImGui::GetWindowDrawList();
        drawList->ChannelsSetCurrent(node->Channel + c_NodeBackgroundChannel);
        return drawList;
    }
    else
        return nullptr;
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
