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
static const int c_LinkChannelCount       = 3;
static const int c_UserLayersCount        = 1;

static const int c_UserLayerChannelStart  = 0;
static const int c_BackgroundChannelStart = c_UserLayerChannelStart  + c_UserLayersCount;
static const int c_LinkStartChannel       = c_BackgroundChannelStart + c_BackgroundChannelCount;
static const int c_NodeStartChannel       = c_LinkStartChannel       + c_LinkChannelCount;

static const int c_ChannelsPerNode        = 3;
static const int c_NodeBaseChannel        = 0;
static const int c_NodeBackgroundChannel  = 1;
static const int c_NodeContentChannel     = 2;

static const float c_NodeFrameRounding    = 12.0f;
static const float c_LinkStrength         = 100.0f;
static const float c_LinkSelectThickness  = 5.0f;


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




//------------------------------------------------------------------------------
//
// Editor Context
//
//------------------------------------------------------------------------------
ed::Context::Context():
    Nodes(),
    Pins(),
    Links(),
    SelectedObject(nullptr),
    LastActiveLink(nullptr),
    CurrentPin(nullptr),
    CurrentNode(nullptr),
    MousePosBackup(0, 0),
    MouseClickPosBackup(),
    Canvas(),
    IsSuspended(false),
    NodeBuildStage(NodeStage::Invalid),
    CurrentAction(nullptr),
    ScrollAction(this),
    DragAction(this),
    SelectAction(this),
    CreateItemAction(this),
    DeleteItemsAction(this),
    IsInitialized(false),
    HeaderTextureID(nullptr),
    Settings()
{
}

ed::Context::~Context()
{
    if (IsInitialized)
    {
        if (HeaderTextureID)
        {
            ImGui_DestroyTexture(HeaderTextureID);
            HeaderTextureID = nullptr;
        }

        SaveSettings();
    }

    for (auto link : Links) delete link;
    for (auto pin  : Pins)  delete pin;
    for (auto node : Nodes) delete node;
}

void ed::Context::Begin(const char* id)
{
    if (!IsInitialized)
    {
        LoadSettings();

        HeaderTextureID = ImGui_LoadTexture("../Data/BlueprintBackground.png");

        IsInitialized = true;
    }

    //ImGui::LogToClipboard();
    //Log("---- begin ----");

    for (auto node : Nodes) node->IsLive = false;
    for (auto pin  : Pins)   pin->IsLive = false;
    for (auto link : Links) link->IsLive = false;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_ChildWindowBg, ImColor(60, 60, 70, 0));
    ImGui::BeginChild(id, ImVec2(0, 0), false,
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse);

    ScrollAction.SetWindow(ImGui::GetWindowPos(), ImGui::GetWindowSize());

    Canvas = ScrollAction.GetCanvas();

    auto& io = ImGui::GetIO();
    MousePosBackup = io.MousePos;
    for (int i = 0; i < 5; ++i)
        MouseClickPosBackup[i] = io.MouseClickedPos[i];

    // debug
    //drawList->PushClipRectFullScreen();
    //drawList->AddRect(Canvas.FromClient(ImVec2(0, 0)), Canvas.FromClient(ImVec2(0, 0)) + Canvas.ClientSize, IM_COL32(255, 0, 255, 255), 0, 15, 4.0f);
    //drawList->PopClipRect();

    auto clipMin = Canvas.FromScreen(Canvas.WindowScreenPos);
    auto clipMax = Canvas.FromScreen(Canvas.WindowScreenPos) + Canvas.CanvasSize;

    //ImGui::Text("CLIP = { x=%g y=%g w=%g h=%g r=%g b=%g }",
    //    clipMin.x, clipMin.y, clipMax.x - clipMin.x, clipMax.y - clipMin.y, clipMax.x, clipMax.y);

    ImGui::PushClipRect(clipMin, clipMax, false);

    CaptureMouse();

    // Reserve channels for background and links
    auto drawList = ImGui::GetWindowDrawList();
    ImDrawList_ChannelsGrow(drawList, c_NodeStartChannel);
}

void ed::Context::End()
{
    auto& io       = ImGui::GetIO();
    auto  control  = ComputeControl();
    auto  drawList = ImGui::GetWindowDrawList();

    bool isSelecting = CurrentAction && CurrentAction->AsSelect() != nullptr;

    // Draw links
    drawList->ChannelsSetCurrent(c_LinkStartChannel + 1);
    for (auto link : Links)
    {
        if (!link->IsLive)
            continue;

        const auto startPoint = to_imvec(link->StartPin->DragPoint);
        const auto endPoint   = to_imvec(link->EndPin->DragPoint);

        const auto bounds = ax::Drawing::GetLinkBounds(startPoint, endPoint, c_LinkStrength);

        if (ImGui::IsRectVisible(to_imvec(bounds.top_left()), to_imvec(bounds.bottom_right())))
        {
            float extraThickness = !isSelecting && !IsSelected(link) && (control.HotLink == link || control.ActiveLink == link) ? 2.0f : 0.0f;

            ax::Drawing::DrawLink(drawList, startPoint, endPoint,
                link->Color, link->Thickness + extraThickness, c_LinkStrength);
        }
    }

    // Highlight hovered node
    {
        auto selectedObjects = &SelectedObjects;
        if (auto selectAction = CurrentAction ? CurrentAction->AsSelect() : nullptr)
            selectedObjects = &selectAction->CandidateObjects;

        for (auto selectedObject : *selectedObjects)
        {
            if (auto selectedNode = selectedObject->AsNode())
            {
                const auto rectMin = to_imvec(selectedNode->Bounds.top_left());
                const auto rectMax = to_imvec(selectedNode->Bounds.bottom_right());

                if (ImGui::IsRectVisible(rectMin, rectMax))
                {
                    drawList->ChannelsSetCurrent(selectedNode->Channel + c_NodeBaseChannel);

                    drawList->AddRect(rectMin, rectMax,
                        ImColor(255, 176, 50, 255), c_NodeFrameRounding, 15, 3.5f);
                }
            }
            else if (auto selectedLink = selectedObject->AsLink())
            {
                const auto rectMin = to_imvec(selectedLink->StartPin->DragPoint);
                const auto rectMax = to_imvec(selectedLink->EndPin->DragPoint);

                if (ImGui::IsRectVisible(rectMin, rectMax))
                {
                    drawList->ChannelsSetCurrent(c_LinkStartChannel + 0);

                    ax::Drawing::DrawLink(drawList, rectMin, rectMax,
                        ImColor(255, 176, 50, 255), selectedLink->Thickness + 4.5f, c_LinkStrength);
                }
            }
        }
    }

    if (!isSelecting)
    {
        // Highlight selected node
        auto hotNode = control.HotNode;
        if (CurrentAction && CurrentAction->AsDrag())
            hotNode = CurrentAction->AsDrag()->DraggedNode;
        if (hotNode && !IsSelected(hotNode))
        {
            const auto rectMin = to_imvec(hotNode->Bounds.top_left());
            const auto rectMax = to_imvec(hotNode->Bounds.bottom_right());

            drawList->ChannelsSetCurrent(hotNode->Channel + c_NodeBaseChannel);

            drawList->AddRect(rectMin, rectMax,
                ImColor(50, 176, 255, 255), c_NodeFrameRounding, 15, 3.5f);
        }

        // Highlight hovered pin
        if (auto hotPin = control.HotPin)
        {
            const auto rectMin = to_imvec(hotPin->Bounds.top_left());
            const auto rectMax = to_imvec(hotPin->Bounds.bottom_right());

            drawList->ChannelsSetCurrent(hotPin->Node->Channel + c_NodeBackgroundChannel);

            drawList->AddRectFilled(rectMin, rectMax,
                ImColor(60, 180, 255, 100), 4.0f);
        }

        // Highlight hovered link
        if (control.HotLink && !IsSelected(control.HotLink))
        {
            drawList->ChannelsSetCurrent(c_LinkStartChannel + 0);

            ax::Drawing::DrawLink(drawList,
                to_imvec(control.HotLink->StartPin->DragPoint),
                to_imvec(control.HotLink->EndPin->DragPoint),
                ImColor(50, 176, 255, 255), control.HotLink->Thickness + 4.5f, c_LinkStrength);
        }
    }

    if (CurrentAction && !CurrentAction->Process(control))
        CurrentAction = nullptr;

    if (nullptr == CurrentAction)
    {
        if (ScrollAction.Accept(control))
            CurrentAction = &ScrollAction;
        else if (DragAction.Accept(control))
            CurrentAction = &DragAction;
        else if (SelectAction.Accept(control))
            CurrentAction = &SelectAction;
        else if (CreateItemAction.Accept(control))
            CurrentAction = &CreateItemAction;
        else if (DeleteItemsAction.Accept(control))
            CurrentAction = &DeleteItemsAction;
    }

    if (SelectAction.IsActive)
    {
        auto fillColor    = SelectAction.SelectLinkMode ? ImColor(5, 130, 255, 64) : ImColor(5, 130, 255, 64);
        auto outlineColor = SelectAction.SelectLinkMode ? ImColor(5, 130, 255, 128) : ImColor(5, 130, 255, 128);

        drawList->ChannelsSetCurrent(c_BackgroundChannelStart + 1);

        auto from = SelectAction.StartPoint;
        auto to   = ImGui::GetMousePos();

        auto min  = ImVec2(std::min(from.x, to.x), std::min(from.y, to.y));
        auto max  = ImVec2(std::max(from.x, to.x), std::max(from.y, to.y));

        drawList->AddRectFilled(min, max, fillColor);
        drawList->AddRect(min, max, outlineColor);
    }

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
        drawList->ChannelsSetCurrent(c_BackgroundChannelStart + 0);

        ImGui::PushClipRect(Canvas.WindowScreenPos + ImVec2(1, 1), Canvas.WindowScreenPos + Canvas.WindowScreenSize - ImVec2(1, 1), false);

        ImVec2 offset    = Canvas.ClientOrigin;
        ImU32 GRID_COLOR = ImColor(120, 120, 120, 40);
        float GRID_SX    = 32.0f * Canvas.Zoom.x;
        float GRID_SY    = 32.0f * Canvas.Zoom.y;
        ImVec2 win_pos   = Canvas.WindowScreenPos;
        ImVec2 canvas_sz = Canvas.WindowScreenSize;

        drawList->AddRectFilled(Canvas.WindowScreenPos, Canvas.WindowScreenPos + Canvas.WindowScreenSize, ImColor(60, 60, 70, 200));

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
        auto preOffset  = ImVec2(0, 0);//ImVec2(Canvas.WindowScreenPos.x, Canvas.WindowScreenPos.y);
        auto postOffset = Canvas.WindowScreenPos + Canvas.ClientOrigin;
        auto scale      = Canvas.Zoom;

        ImDrawList_TransformChannels(drawList,                            0, c_BackgroundChannelStart, preOffset, scale, postOffset);
        ImDrawList_TransformChannels(drawList, c_BackgroundChannelStart + 1, drawList->_ChannelsCount, preOffset, scale, postOffset);

        ImGui::PushClipRect(Canvas.WindowScreenPos + ImVec2(1, 1), Canvas.WindowScreenPos + Canvas.WindowScreenSize - ImVec2(1, 1), false);
        ImDrawList_TranslateAndClampClipRects(drawList,                            0, c_BackgroundChannelStart, -Canvas.FromScreen(Canvas.WindowScreenPos));
        ImDrawList_TranslateAndClampClipRects(drawList, c_BackgroundChannelStart + 1, drawList->_ChannelsCount, -Canvas.FromScreen(Canvas.WindowScreenPos));
        ImGui::PopClipRect();

        for (float x = 0; x < Canvas.WindowScreenSize.x; x += 100)
            drawList->AddLine(ImVec2(x, 0.0f) + Canvas.WindowScreenPos, ImVec2(x, Canvas.WindowScreenSize.y) + Canvas.WindowScreenPos, IM_COL32(255, 0, 0, 128));
        for (float y = 0; y < Canvas.WindowScreenSize.y; y += 100)
            drawList->AddLine(ImVec2(0.0f, y) + Canvas.WindowScreenPos, ImVec2(Canvas.WindowScreenSize.x, y) + Canvas.WindowScreenPos, IM_COL32(255, 0, 0, 128));
    }

    drawList->ChannelsMerge();

    // Draw border
    {
        auto& style = ImGui::GetStyle();
        auto borderShadoColor = style.Colors[ImGuiCol_BorderShadow];
        auto borderColor      = style.Colors[ImGuiCol_Border];
        drawList->AddRect(Canvas.WindowScreenPos + ImVec2(1, 1), Canvas.WindowScreenPos + Canvas.WindowScreenSize + ImVec2(1, 1), ImColor(borderShadoColor), style.WindowRounding);
        drawList->AddRect(Canvas.WindowScreenPos,                Canvas.WindowScreenPos + Canvas.WindowScreenSize,                ImColor(borderColor),      style.WindowRounding);
    }

    ShowMetrics(control);

    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    ReleaseMouse();

    if (Settings.Dirty)
    {
        Settings.Dirty = false;
        SaveSettings();
    }
}

void ed::Context::BeginNode(int id)
{
    assert(nullptr == CurrentNode);

    auto node = FindNode(id);
    if (!node)
        node = CreateNode(id);

    SetCurrentNode(node);

    // Position node on screen
    ImGui::SetCursorScreenPos(to_imvec(node->Bounds.location));

    node->IsLive  = true;
    node->LastPin = nullptr;

    auto drawList = ImGui::GetWindowDrawList();
    node->Channel = drawList->_ChannelsCount;
    ImDrawList_ChannelsGrow(drawList, drawList->_ChannelsCount + c_ChannelsPerNode);
    drawList->ChannelsSetCurrent(node->Channel + c_NodeContentChannel);

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 1.0f);

    SetNodeStage(NodeStage::Begin);
}

void ed::Context::EndNode()
{
    assert(nullptr != CurrentNode);

    SetNodeStage(NodeStage::End);

    if (CurrentNode->Bounds.size != NodeRect.size)
    {
        MarkSettingsDirty();

        CurrentNode->Bounds.size = NodeRect.size;
    }

    // Draw background
    if (ImGui::IsItemVisible())
    {
        auto alpha = static_cast<int>(255 * ImGui::GetStyle().Alpha);

        auto drawList = ImGui::GetWindowDrawList();

        auto drawListForegroundEnd = drawList->CmdBuffer.size();

        drawList->ChannelsSetCurrent(CurrentNode->Channel + c_NodeBackgroundChannel);

        drawList->AddRectFilled(
            to_imvec(NodeRect.top_left()),
            to_imvec(NodeRect.bottom_right()),
            ImColor(32, 32, 32, (200 * alpha) / 255), c_NodeFrameRounding);

        auto headerColor = IM_COL32(0, 0, 0, alpha) | (HeaderColor & IM_COL32(255, 255, 255, 0));
        ax::Drawing::DrawHeader(drawList, HeaderTextureID,
            to_imvec(HeaderRect.top_left()),
            to_imvec(HeaderRect.bottom_right()),
            headerColor, c_NodeFrameRounding, 4.0f);

        auto headerSeparatorRect      = ax::rect(HeaderRect.bottom_left(),  ContentRect.top_right());
        auto footerSeparatorRect      = ax::rect(ContentRect.bottom_left(), NodeRect.bottom_right());
        auto contentWithSeparatorRect = ax::rect::make_union(headerSeparatorRect, footerSeparatorRect);

        //drawList->AddRectFilled(
        //    to_imvec(contentWithSeparatorRect.top_left()),
        //    to_imvec(contentWithSeparatorRect.bottom_right()),
        //    ImColor(32, 32, 32, 200), c_NodeFrameRounding, 4 | 8);

        drawList->AddLine(
            to_imvec(headerSeparatorRect.top_left() + point(1, -1)),
            to_imvec(headerSeparatorRect.top_right() + point(-1, -1)),
            ImColor(255, 255, 255, 96 * alpha / (3 * 255)), 1.0f);

        drawList->AddRect(
            to_imvec(NodeRect.top_left()),
            to_imvec(NodeRect.bottom_right()),
            ImColor(255, 255, 255, 96 * alpha / 255), c_NodeFrameRounding, 15, 1.5f);
    }

    ImGui::PopStyleVar();

    SetCurrentNode(nullptr);

    SetNodeStage(NodeStage::Invalid);
}

void ed::Context::BeginHeader(ImU32 color)
{
    assert(nullptr != CurrentNode);

    HeaderColor = color;
    SetNodeStage(NodeStage::Header);
}

void ed::Context::EndHeader()
{
    assert(nullptr != CurrentNode);

    SetNodeStage(NodeStage::Content);
}

void ed::Context::BeginInput(int id)
{
    assert(nullptr != CurrentNode);

    if (NodeBuildStage == NodeStage::Begin)
        SetNodeStage(NodeStage::Content);

    SetNodeStage(NodeStage::Input);

    BeginPin(id, PinType::Input);

    ImGui::Spring(0);
    ImGui::BeginHorizontal(id);
}

void ed::Context::EndInput()
{
    ImGui::EndHorizontal();

    EndPin();

    ImGui::Spring(0, 0);
}

void ed::Context::BeginOutput(int id)
{
    assert(nullptr != CurrentNode);

    if (NodeBuildStage == NodeStage::Begin)
        SetNodeStage(NodeStage::Content);

    if (NodeBuildStage == NodeStage::Begin)
        SetNodeStage(NodeStage::Input);

    SetNodeStage(NodeStage::Output);

    BeginPin(id, PinType::Output);

    ImGui::Spring(0);
    ImGui::BeginHorizontal(id);
}

void ed::Context::EndOutput()
{
    ImGui::EndHorizontal();

    EndPin();

    ImGui::Spring(0, 0);
}

bool ed::Context::DoLink(int id, int startPinId, int endPinId, ImU32 color, float thickness)
{
    auto link     = GetLink(id);
    auto startPin = FindPin(startPinId);
    auto endPin   = FindPin(endPinId);

    link->StartPin = startPin;
    link->EndPin   = endPin;

    link->Color     = color;
    link->Thickness = thickness;

    link->IsLive =
        (startPin && startPin->IsLive) &&
        (endPin   && endPin->IsLive);

    return link->IsLive;
}

void ed::Context::SetNodePosition(int nodeId, const ImVec2& position)
{
    auto node = FindNode(nodeId);
    if (!node)
    {
        node = CreateNode(nodeId);
        node->IsLive = false;
    }

    node->Bounds.location = to_point(position);
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
    SelectedObject = nullptr;
}

void ed::Context::SelectObject(Object* object)
{
    SelectedObjects.push_back(object);
    SelectedObject = SelectedObjects.front();
}

void ed::Context::DeselectObject(Object* object)
{
    auto objectIt = std::find(SelectedObjects.begin(), SelectedObjects.end(), object);
    if (objectIt != SelectedObjects.end())
    {
        SelectedObjects.erase(objectIt);
        if (!SelectedObjects.empty())
            SelectedObject = SelectedObjects.front();
        else
            SelectedObject = nullptr;
    }
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

void ed::Context::FindNodesInRect(ax::rect r, vector<Node*>& result)
{
    result.clear();

    if (r.is_empty())
        return;

    for (auto node : Nodes)
    {
        if (!node->IsLive)
            continue;

        //auto drawList = ImGui::GetWindowDrawList();
        //drawList->AddRectFilled(
        //    to_imvec(node->Bounds.top_left()),
        //    to_imvec(node->Bounds.bottom_right()),
        //    IM_COL32(255, 0, 0, 64));

        if (!node->Bounds.is_empty() && r.intersects(node->Bounds))
            result.push_back(node);
    }
}

void ed::Context::FindLinksInRect(ax::rect r, vector<Link*>& result)
{
    using namespace ImGuiInterop;

    result.clear();

    if (r.is_empty())
        return;

    //r.location += to_point(Canvas.ClientToCanvas(ImVec2(0, 0)));

    for (auto link : Links)
    {
        if (!link->IsLive)
            continue;

        auto a      = link->StartPin->DragPoint;
        auto b      = link->EndPin->DragPoint;
        auto bounds = Drawing::GetLinkBounds(to_imvec(a), to_imvec(b), c_LinkStrength);

        //auto drawList = ImGui::GetWindowDrawList();
        //drawList->AddRectFilled(
        //    to_imvec(bounds.top_left()),
        //    to_imvec(bounds.bottom_right()),
        //    IM_COL32(255, 0, 0, 64));

        if (r.contains(bounds))
        {
            result.push_back(link);
        }
        else if (r.intersects(bounds) && Drawing::CollideLinkWithRect(r, to_imvec(a), to_imvec(b), c_LinkStrength))
        {
            result.push_back(link);
        }
    }
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

ed::Pin* ed::Context::CreatePin(int id, PinType type)
{
    assert(nullptr == FindObject(id));
    auto pin = new Pin(id, type);
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
    {
        settings = AddNodeSettings(id);
        node->Bounds.location = to_point(ImGui::GetCursorScreenPos());
    }
    else
        node->Bounds.location = settings->Location;

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

void ed::Context::SetCurrentNode(Node* node)
{
    CurrentNode = node;
}

void ed::Context::SetCurrentPin(Pin* pin)
{
    CurrentPin = pin;

    if (pin)
        SetCurrentNode(pin->Node);
}

bool ed::Context::SetNodeStage(NodeStage stage)
{
    if (stage == NodeBuildStage)
        return false;

    auto oldStage = NodeBuildStage;
    NodeBuildStage = stage;

    ImVec2 cursor;
    switch (oldStage)
    {
        case NodeStage::Begin:
            break;

        case NodeStage::Header:
            ImGui::EndHorizontal();
            HeaderRect = ImGui_GetItemRect();

            // spacing between header and content
            ImGui::Spring(0);

            break;

        case NodeStage::Content:
            break;

        case NodeStage::Input:
            ImGui::Spring(1);
            ImGui::EndVertical();
            break;

        case NodeStage::Output:
            ImGui::Spring(1);
            ImGui::EndVertical();
            break;

        case NodeStage::End:
            break;
    }

    switch (stage)
    {
        case NodeStage::Begin:
            ImGui::BeginVertical(CurrentNode->ID);
            break;

        case NodeStage::Header:
            ImGui::BeginHorizontal("header");
            break;

        case NodeStage::Content:
            ImGui::BeginHorizontal("content");
            ImGui::Spring(0);
            break;

        case NodeStage::Input:
            ImGui::BeginVertical("inputs", ImVec2(0,0), 0.0f);
            break;

        case NodeStage::Output:
            ImGui::Spring(1);
            ImGui::BeginVertical("outputs", ImVec2(0, 0), 1.0f);
            break;

        case NodeStage::End:
            if (oldStage == NodeStage::Input)
                ImGui::Spring(1);
            ImGui::Spring(0);
            ImGui::EndHorizontal();
            ContentRect = ImGui_GetItemRect();

            ImGui::Spring(0);
            ImGui::EndVertical();
            NodeRect = ImGui_GetItemRect();
            break;
    }

    return true;
}

ed::Pin* ed::Context::GetPin(int id, PinType type)
{
    if (auto pin = FindPin(id))
    {
        pin->Type = type;
        return pin;
    }
    else
        return CreatePin(id, type);
}

void ed::Context::BeginPin(int id, PinType type)
{
    auto pin = GetPin(id, type);
    pin->Node = CurrentNode;
    SetCurrentPin(pin);

    pin->PreviousPin     = CurrentNode->LastPin;
    CurrentNode->LastPin = pin;
    pin->IsLive = true;
}

void ed::Context::EndPin()
{
    auto pinRect = ImGui_GetItemRect();

    CurrentPin->Bounds = pinRect;

    if (CurrentPin->Type == PinType::Input)
        CurrentPin->DragPoint = point(pinRect.left(), pinRect.center_y());
    else
        CurrentPin->DragPoint = point(pinRect.right(), pinRect.center_y());

    SetCurrentPin(nullptr);
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
    return &Settings.Nodes.back();
}

void ed::Context::LoadSettings()
{
    namespace json = picojson;

    if (Settings.Path.empty())
        return;

    std::ifstream settingsFile(Settings.Path);
    if (!settingsFile)
        return;

    json::value settingsValue;
    auto error = json::parse(settingsValue, settingsFile);
    if (!error.empty() || settingsValue.is<json::null>())
        return;

    if (!settingsValue.is<json::object>())
        return;

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
            if (locationValue.is<json::object>())
            {
                auto xValue = locationValue.get("x");
                auto yValue = locationValue.get("y");

                if (xValue.is<double>() && yValue.is<double>())
                {
                    settings->Location.x = static_cast<int>(xValue.get<double>());
                    settings->Location.y = static_cast<int>(yValue.get<double>());
                }
            }
        }
    }
}

void ed::Context::SaveSettings()
{
    namespace json = picojson;

    if (Settings.Path.empty())
        return;

    std::ofstream settingsFile(Settings.Path);
    if (!settingsFile)
        return;

    // update settings data
    for (auto& node : Nodes)
    {
        auto settings = FindNodeSettings(node->ID);
        settings->Location = node->Bounds.location;
    }

    json::object nodes;
    for (auto& node : Settings.Nodes)
    {
        json::object locationData;
        locationData["x"] = json::value(static_cast<double>(node.Location.x));
        locationData["y"] = json::value(static_cast<double>(node.Location.y));

        json::object nodeData;
        nodeData["location"] = json::value(std::move(locationData));

        nodes[std::to_string(node.ID)] = json::value(std::move(nodeData));
    }

    json::object settings;
    settings["nodes"] = json::value(std::move(nodes));

    json::value settingsValue(std::move(settings));

    settingsFile << settingsValue.serialize(true);
}

void ed::Context::MarkSettingsDirty()
{
    Settings.Dirty = true;
}

ed::Link* ed::Context::FindLinkAt(const ax::point& p)
{
    for (auto& link : Links)
    {
        if (!link->IsLive) continue;

        // Live links are guarantee to have complete set of pins.
        const auto startPoint = link->StartPin->DragPoint;
        const auto endPoint   = link->EndPin->DragPoint;

        // Build bounding rectangle of link.
        const auto linkBounds = ax::Drawing::GetLinkBounds(to_imvec(startPoint), to_imvec(endPoint), c_LinkStrength);

        // Calculate thickness of interactive area around curve.
        // Making it slightly larger help user to click it.
        const auto thickness = roundi(link->Thickness + c_LinkSelectThickness);

        // Expand bounding rectangle corners by curve thickness.
        const auto interactiveBounds = linkBounds.expanded(thickness);

        // Ignore link if mouse is not hovering bounding rect.
        if (!interactiveBounds.contains(p))
            continue;

        // Calculate distance from mouse position to link curve.
        const auto distance = ax::Drawing::LinkDistance(to_imvec(p),
            to_imvec(startPoint), to_imvec(endPoint), c_LinkStrength);

        // If point is close enough link was found.
        if (distance < thickness)
            return link;
    }

    return nullptr;
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

    const auto editorRect = rect(to_point(Canvas.FromClient(ImVec2(0, 0))), to_size(Canvas.CanvasSize));

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

    ImGui::SetCursorScreenPos(ImGui::GetWindowPos());
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
    ImGui::Text("Canvas Size: { w=%g h=%g }", Canvas.CanvasSize.x, Canvas.CanvasSize.y);
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
    CanvasSize(0, 0),
    Zoom(1, 1),
    InvZoom(1, 1)
{
}

ed::Canvas::Canvas(ImVec2 position, ImVec2 size, ImVec2 zoom, ImVec2 origin):
    WindowScreenPos(position),
    WindowScreenSize(size),
    CanvasSize(size),
    ClientOrigin(origin),
    Zoom(zoom),
    InvZoom(1, 1)
{
    InvZoom.x = Zoom.x ? 1.0f / Zoom.x : 1.0f;
    InvZoom.y = Zoom.y ? 1.0f / Zoom.y : 1.0f;

    if (InvZoom.x > 1.0f)
        CanvasSize.x *= InvZoom.x;
    if (InvZoom.y > 1.0f)
        CanvasSize.y *= InvZoom.y;
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
// Scroll Action
//
//------------------------------------------------------------------------------
const float ed::ScrollAction::s_ZoomLevels[] =
{
    /*0.1f, 0.15f, */0.25f, 0.33f, 0.5f, 0.75f, 1.0f, 1.25f, 1.50f, 2.0f, 2.5f, 3.0f, 4.0f/*, 5.0f*/
};

const int ed::ScrollAction::s_ZoomLevelCount = sizeof(s_ZoomLevels) / sizeof(*s_ZoomLevels);

ed::ScrollAction::ScrollAction(Context* editor):
    EditorAction(editor),
    IsActive(false),
    WindowScreenPos(0, 0),
    WindowScreenSize(0, 0),
    Zoom(1),
    Scroll(0, 0),
    ScrollStart(0, 0)
{
    Zoom = 0.75f;
}

bool ed::ScrollAction::Accept(const Control& control)
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

    auto pos = (ImVec2(712, 418));

    if (auto drawList = ImGui::GetWindowDrawList())
    {
        // #debug
        drawList->AddCircleFilled(io.MousePos, 4.0f, IM_COL32(255, 0, 255, 255));

        //drawList->AddLine(ImVec2(0, pos.y), ImVec2(Canvas.ClientSize.x, pos.y), IM_COL32(255, 0, 0, 255));
        //drawList->AddLine(ImVec2(pos.x, 0), ImVec2(pos.x, Canvas.ClientSize.y), IM_COL32(255, 0, 0, 255));
    }

    if (io.MouseWheel && 0)
    {
        auto mousePos     = pos;//io.MousePos;
        auto steps        = (int)io.MouseWheel;
        auto newZoom      = MatchZoom(steps, s_ZoomLevels[steps < 0 ? 0 : s_ZoomLevelCount - 1]);

        //Scroll.x = (Scroll.x + mousePos.x) * newZoom / Zoom - mousePos.x;
        //Scroll.y = (Scroll.y + mousePos.y) * newZoom / Zoom - mousePos.y;
        Zoom     = newZoom;

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

void ed::ScrollAction::SetWindow(ImVec2 position, ImVec2 size)
{
    WindowScreenPos  = position;
    WindowScreenSize = size;
}

ed::Canvas ed::ScrollAction::GetCanvas()
{
    return Canvas(WindowScreenPos, WindowScreenSize, ImVec2(Zoom, Zoom), ImVec2(-Scroll.x, -Scroll.y));
}

float ed::ScrollAction::MatchZoom(int steps, float fallbackZoom)
{
    auto currentZoomIndex = MatchZoomIndex();
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

int ed::ScrollAction::MatchZoomIndex()
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
    DraggedNode(nullptr)
{
}

bool ed::DragAction::Accept(const Control& control)
{
    assert(!IsActive);

    if (IsActive)
        return false;

    if (control.ActiveNode && ImGui::IsMouseDragging(0))
    {
        DraggedNode = control.ActiveNode;
        control.ActiveNode->DragStart = control.ActiveNode->Bounds.location;

        if (Editor->IsSelected(control.ActiveNode))
        {
            for (auto selectedObject : Editor->GetSelectedObjects())
                if (auto selectedNode = selectedObject->AsNode())
                    selectedNode->DragStart = selectedNode->Bounds.location;
        }
        else
            control.ActiveNode->DragStart = control.ActiveNode->Bounds.location;

        IsActive = true;
    }

    return IsActive;
}

bool ed::DragAction::Process(const Control& control)
{
    if (!IsActive)
        return false;

    if (control.ActiveNode == DraggedNode)
    {
        auto dragOffset = ImGui::GetMouseDragDelta(0, 0.0f);

        if (Editor->IsSelected(control.ActiveNode))
        {
            for (auto selectedObject : Editor->GetSelectedObjects())
                if (auto selectedNode = selectedObject->AsNode())
                    selectedNode->Bounds.location = selectedNode->DragStart + to_point(dragOffset);
        }
        else
            control.ActiveNode->Bounds.location = control.ActiveNode->DragStart + to_point(dragOffset);
    }
    else if (!control.ActiveNode)
    {
        DraggedNode = nullptr;
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
    ImGui::Text("    Node: %s (%d)", getObjectName(DraggedNode), DraggedNode ? DraggedNode->ID : 0);
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
    StartPoint()
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
        auto rect        = ax::rect(to_point(topLeft), to_point(bottomRight));
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
    }
    else if (control.BackgroundHot && ImGui::IsMouseClicked(1))
    {
        DragStart(nullptr);
        DropNode();
    }
    else
        return false;

    Editor->ClearSelection();

    IsActive = true;

    return true;
}

bool ed::CreateItemAction::Process(const Control& control)
{
    assert(IsActive);

    if (!IsActive)
        return false;

    if (DraggedPin)
    {
        if (control.ActivePin == DraggedPin && (CurrentStage == Possible))
        {
            ImVec2 startPoint = to_imvec(DraggedPin->DragPoint);
            ImVec2 endPoint = ImGui::GetMousePos();

            if (control.HotPin)
            {
                DropPin(control.HotPin);

                if (UserAction == UserAccept)
                    endPoint = to_imvec(control.HotPin->DragPoint);
            }
            else if (control.BackgroundHot)
                DropNode();
            else
                DropNothing();

            auto  drawList = ImGui::GetWindowDrawList();

            if (DraggedPin->Type == PinType::Input)
                std::swap(startPoint, endPoint);

            drawList->ChannelsSetCurrent(c_LinkStartChannel + 2);

            ax::Drawing::DrawLink(drawList, startPoint, endPoint, LinkColor, LinkThickness, c_LinkStrength);
        }
        else if (!control.ActivePin)
        {
            DraggedPin = nullptr;
            DragEnd();
            IsActive = false;
        }
    }
    else
    {
        if (CurrentStage == Possible)
        {
            DragEnd();
            IsActive = false;
        }
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

    SetUserContext();

    return True;
}

ed::CreateItemAction::Result ed::CreateItemAction::QueryNode(int* pinId)
{
    IM_ASSERT(InActive);

    if (!InActive || CurrentStage == None || ItemType != Node)
        return Indeterminate;

    *pinId = LinkStart ? LinkStart->ID : 0;

    SetUserContext();

    return True;
}

void ed::CreateItemAction::SetUserContext()
{
    // Move drawing cursor to mouse location and prepare layer for
    // content added by user.
    ImGui::SetCursorScreenPos(ImGui::GetMousePos());

    auto drawList = ImGui::GetWindowDrawList();
    drawList->ChannelsSetCurrent(c_UserLayerChannelStart);

    // #debug
    drawList->AddCircleFilled(ImGui::GetMousePos(), 4, IM_COL32(0, 255, 0, 255));
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

