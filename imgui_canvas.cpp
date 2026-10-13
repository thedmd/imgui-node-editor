# ifndef IMGUI_DEFINE_MATH_OPERATORS
#     define IMGUI_DEFINE_MATH_OPERATORS
# endif
# include "imgui_canvas.h"
# include <cmath>
# include <type_traits>

// https://stackoverflow.com/a/36079786
# define DECLARE_HAS_MEMBER(TraitName, MemberName)                                   \
                                                                                     \
    template <typename T>                                                            \
    class TraitName                                                                  \
    {                                                                                \
        using check_type = ::std::remove_const_t<T>;                                 \
        struct no_type {char x[2];};                                                 \
        using  yes_type = char;                                                      \
                                                                                     \
        struct  base { void MemberName() {}};                                        \
        struct mixin : public base, public check_type {};                            \
                                                                                     \
        template <void (base::*)()> struct aux {};                                   \
                                                                                     \
        template <typename U> static no_type  test(aux<&U::MemberName>*);            \
        template <typename U> static yes_type test(...);                             \
                                                                                     \
        public:                                                                      \
                                                                                     \
        static constexpr bool value = (sizeof(yes_type) == sizeof(test<mixin>(0)));  \
    }

// Special sentinel value. This needs to be unique, so allow it to be overridden in the user's ImGui config
# ifndef ImDrawCallback_ImCanvas
#     define ImDrawCallback_ImCanvas        (ImDrawCallback)(-2)
# endif

namespace ImCanvasDetails {

DECLARE_HAS_MEMBER(HasFringeScale, _FringeScale);

struct FringeScaleRef
{
    // Overload is present when ImDrawList does have _FringeScale member variable.
    template <typename T>
    static float& Get(typename std::enable_if<HasFringeScale<T>::value, T>::type* drawList)
    {
        return drawList->_FringeScale;
    }

    // Overload is present when ImDrawList does not have _FringeScale member variable.
    template <typename T>
    static float& Get(typename std::enable_if<!HasFringeScale<T>::value, T>::type*)
    {
        static float placeholder = 1.0f;
        return placeholder;
    }
};

DECLARE_HAS_MEMBER(HasVtxCurrentOffset, _VtxCurrentOffset);

struct VtxCurrentOffsetRef
{
    // Overload is present when ImDrawList does have _FringeScale member variable.
    template <typename T>
    static unsigned int& Get(typename std::enable_if<HasVtxCurrentOffset<T>::value, T>::type* drawList)
    {
        return drawList->_VtxCurrentOffset;
    }

    // Overload is present when ImDrawList does not have _FringeScale member variable.
    template <typename T>
    static unsigned int& Get(typename std::enable_if<!HasVtxCurrentOffset<T>::value, T>::type* drawList)
    {
        return drawList->_CmdHeader.VtxOffset;
    }
};

} // namespace ImCanvasDetails

# undef DECLARE_HAS_MEMBER

# if IMGUI_VERSION_NUM < 19292
// Returns a reference to _FringeScale extension to ImDrawList
//
// If ImDrawList does not have _FringeScale a placeholder is returned.
static inline float& ImFringeScaleRef(ImDrawList* drawList)
{
    using namespace ImCanvasDetails;
    return FringeScaleRef::Get<ImDrawList>(drawList);
}
# endif

static inline unsigned int& ImVtxOffsetRef(ImDrawList* drawList)
{
    using namespace ImCanvasDetails;
    return VtxCurrentOffsetRef::Get<ImDrawList>(drawList);
}

static inline ImVec2 ImSelectPositive(const ImVec2& lhs, const ImVec2& rhs) { return ImVec2(lhs.x > 0.0f ? lhs.x : rhs.x, lhs.y > 0.0f ? lhs.y : rhs.y); }

bool ImGuiEx::Canvas::Begin(const char* id, const ImVec2& size)
{
    return Begin(ImGui::GetID(id), size);
}

bool ImGuiEx::Canvas::Begin(ImGuiID id, const ImVec2& size)
{
    IM_ASSERT(m_InBeginEnd == false);

    m_WidgetPosition = ImGui::GetCursorScreenPos();
    m_WidgetSize = ImSelectPositive(size, ImGui::GetContentRegionAvail());
    m_WidgetRect = ImRect(m_WidgetPosition, m_WidgetPosition + m_WidgetSize);
    m_Window = ImGui::GetCurrentWindow();
    m_DrawList = m_Window->DrawList;

    UpdateViewTransformPosition();

    // A clipped canvas still enters local space: its content is clipped away there,
    // instead of being drawn untransformed, and End() reserves its layout space.
    m_IsVisible = !ImGui::IsClippedEx(m_WidgetRect, id);

    // Save current channel, so we can assert when user
    // call canvas API with different one.
    m_ExpectedChannel = m_DrawList->_Splitter._Current;

    // #debug: Canvas content.
    //m_DrawList->AddRectFilled(m_StartPos, m_StartPos + m_CurrentSize, IM_COL32(0, 0, 0, 64));
    //m_DrawList->AddRect(m_WidgetRect.Min, m_WidgetRect.Max, IM_COL32(255, 0, 255, 64));

    ImGui::SetCursorScreenPos(ImVec2(0.0f, 0.0f));

    SaveInputState();
    SaveViewportState();

    // Record cursor max to prevent scrollbars from appearing, and ideal max, which
    // tables and tab bars inside extend, to keep the window's auto-fit size.
    m_WindowCursorMaxBackup = m_Window->DC.CursorMaxPos;
    m_WindowIdealMaxBackup  = m_Window->DC.IdealMaxPos;

    // Local space lays lines out from x = 0, in the visible part of the canvas.
    m_LocalIndent            = 0.0f;
    m_LocalColumnsOffset     = 0.0f;
    m_LocalGroupOffset       = 0.0f;
    m_LocalWorkRect          = CalcViewRect(m_View);
    m_LocalContentRegionRect = m_LocalWorkRect;

    EnterLocalSpace();

    // Emit dummy widget matching bounds of the canvas.
    ImGui::SetCursorScreenPos(m_ViewRect.Min);
    ImGui::Dummy(m_ViewRect.GetSize());

    ImGui::SetCursorScreenPos(ImVec2(0.0f, 0.0f));

    m_InBeginEnd = true;

    return true;
}

void ImGuiEx::Canvas::End()
{
    // If you're here, Begin() wasn't called.
    IM_ASSERT(m_InBeginEnd == true);

    // If you're here, please make sure you do not interleave
    // channel splitter with canvas.
    // Always call canvas function with using same channel.
    IM_ASSERT(m_DrawList->_Splitter._Current == m_ExpectedChannel);

    //auto& io = ImGui::GetIO();

    // Check: Unmatched calls to Suspend() / Resume(). Please check your code.
    IM_ASSERT(m_SuspendCounter == 0);

    // Check: Clip rectangle pushed inside the canvas wasn't popped.
    IM_ASSERT(m_DrawList->_ClipRectStack.Size == m_ClipRectStackSize);

    LeaveLocalSpace();
    m_SuspendedClipRects.resize(0);

    // Misuse that asserted already can leave a sentinel behind, which a renderer
    // would call.
    for (int i = m_DrawList->CmdBuffer.Size - 1; i >= 0; --i)
        if (m_DrawList->CmdBuffer[i].UserCallback == ImDrawCallback_ImCanvas && m_DrawList->CmdBuffer[i].UserCallbackData == this)
            m_DrawList->CmdBuffer.erase(m_DrawList->CmdBuffer.Data + i);

    m_Window->DC.CursorMaxPos = m_WindowCursorMaxBackup;
    m_Window->DC.IdealMaxPos  = m_WindowIdealMaxBackup;

    // Emit dummy widget matching bounds of the canvas.
    ImGui::SetCursorScreenPos(m_WidgetPosition);
    ImGui::Dummy(m_WidgetSize);

    // #debug: Rect around canvas. Content should be inside these bounds.
    //m_DrawList->AddRect(m_WidgetPosition - ImVec2(1.0f, 1.0f), m_WidgetPosition + m_WidgetSize + ImVec2(1.0f, 1.0f), IM_COL32(196, 0, 0, 255));

    m_InBeginEnd = false;
}

void ImGuiEx::Canvas::SetView(const ImVec2& origin, float scale)
{
    SetView(CanvasView(origin, scale));
}

void ImGuiEx::Canvas::SetView(const CanvasView& view)
{
    // A suspended canvas is in screen space already, and enters local space with the
    // new view on Resume().
    const auto inLocalSpace = m_InBeginEnd && m_SuspendCounter == 0;
    if (inLocalSpace)
        LeaveLocalSpace();

    if (m_View.Origin().x != view.Origin().x || m_View.Origin().y != view.Origin().y)
    {
        m_View.SetOrigin(view.Origin());

        UpdateViewTransformPosition();
    }

    if (m_View.Scale() != view.Scale())
        m_View.SetScale(view.Scale());

    m_ViewRect = CalcViewRect(m_View);

    if (inLocalSpace)
        EnterLocalSpace();
}

void ImGuiEx::Canvas::CenterView(const ImVec2& canvasPoint)
{
    auto view = CalcCenterView(canvasPoint);
    SetView(view);
}

ImGuiEx::CanvasView ImGuiEx::Canvas::CalcCenterView(const ImVec2& canvasPoint) const
{
    auto localCenter = ToLocal(m_WidgetPosition + m_WidgetSize * 0.5f);
    auto localOffset = canvasPoint - localCenter;
    auto offset      = FromLocalV(localOffset);

    return CanvasView{ m_View.Origin() - offset, m_View.Scale() };
}

void ImGuiEx::Canvas::CenterView(const ImRect& canvasRect)
{
    auto view = CalcCenterView(canvasRect);

    SetView(view);
}

ImGuiEx::CanvasView ImGuiEx::Canvas::CalcCenterView(const ImRect& canvasRect) const
{
    auto canvasRectSize = canvasRect.GetSize();

    if (canvasRectSize.x <= 0.0f || canvasRectSize.y <= 0.0f)
        return View();

    auto widgetAspectRatio     = m_WidgetSize.y   > 0.0f ? m_WidgetSize.x   / m_WidgetSize.y   : 0.0f;
    auto canvasRectAspectRatio = canvasRectSize.y > 0.0f ? canvasRectSize.x / canvasRectSize.y : 0.0f;

    if (widgetAspectRatio <= 0.0f || canvasRectAspectRatio <= 0.0f)
        return View();

    auto newOrigin = m_View.Origin();
    auto newScale  = m_View.Scale();
    if (canvasRectAspectRatio > widgetAspectRatio)
    {
        // width span across view
        newScale = m_WidgetSize.x / canvasRectSize.x;
        newOrigin = canvasRect.Min * -newScale;
        newOrigin.y += (m_WidgetSize.y - canvasRectSize.y * newScale) * 0.5f;
    }
    else
    {
        // height span across view
        newScale = m_WidgetSize.y / canvasRectSize.y;
        newOrigin = canvasRect.Min * -newScale;
        newOrigin.x += (m_WidgetSize.x - canvasRectSize.x * newScale) * 0.5f;
    }

    return CanvasView{ newOrigin, newScale };
}

void ImGuiEx::Canvas::Suspend()
{
    // If you're here, Begin() wasn't called, or End() already was.
    IM_ASSERT(m_InBeginEnd);
    if (!m_InBeginEnd)
        return;

    // If you're here, please make sure you do not interleave
    // channel splitter with canvas.
    // Always call canvas function with using same channel.
    IM_ASSERT(m_DrawList->_Splitter._Current == m_ExpectedChannel);

    if (m_SuspendCounter == 0)
        LeaveLocalSpace();

    ++m_SuspendCounter;
}

void ImGuiEx::Canvas::Resume()
{
    // If you're here, Begin() wasn't called, or End() already was.
    IM_ASSERT(m_InBeginEnd);
    if (!m_InBeginEnd)
        return;

    // If you're here, please make sure you do not interleave
    // channel splitter with canvas.
    // Always call canvas function with using same channel.
    IM_ASSERT(m_DrawList->_Splitter._Current == m_ExpectedChannel);

    // Check: Number of calls to Resume() do not match calls to Suspend(). Please check your code.
    IM_ASSERT(m_SuspendCounter > 0);
    if (--m_SuspendCounter == 0)
        EnterLocalSpace();
}

ImVec2 ImGuiEx::Canvas::FromLocal(const ImVec2& point) const
{
    return point * m_View.Scale() + m_ViewTransformPosition;
}

ImVec2 ImGuiEx::Canvas::FromLocal(const ImVec2& point, const CanvasView& view) const
{
    return point * view.Scale() + CalcViewTransformPosition(view);
}

ImVec2 ImGuiEx::Canvas::FromLocalV(const ImVec2& vector) const
{
    return vector * m_View.Scale();
}

ImVec2 ImGuiEx::Canvas::FromLocalV(const ImVec2& vector, const CanvasView& view) const
{
    return vector * view.Scale();
}

ImVec2 ImGuiEx::Canvas::ToLocal(const ImVec2& point) const
{
    return (point - m_ViewTransformPosition) * m_View.InvScale();
}

ImVec2 ImGuiEx::Canvas::ToLocal(const ImVec2& point, const CanvasView& view) const
{
    return (point - CalcViewTransformPosition(view)) * view.InvScale();
}

ImVec2 ImGuiEx::Canvas::ToLocalV(const ImVec2& vector) const
{
    return vector * m_View.InvScale();
}

ImVec2 ImGuiEx::Canvas::ToLocalV(const ImVec2& vector, const CanvasView& view) const
{
    return vector * view.InvScale();
}

ImRect ImGuiEx::Canvas::CalcViewRect(const CanvasView& view) const
{
    ImRect result;
    const auto transformPosition = CalcViewTransformPosition(view);
    result.Min = (m_WidgetPosition - transformPosition) * view.InvScale();
    result.Max = (m_WidgetPosition + m_WidgetSize - transformPosition) * view.InvScale();
    return result;
}

ImVec2 ImGuiEx::Canvas::CalcViewTransformPosition(const CanvasView& view) const
{
    // On whole pixels, so content on whole canvas units is too at scale 1.
    const auto position = view.Origin() + m_WidgetPosition;
    return ImVec2(std::floor(position.x), std::floor(position.y));
}

void ImGuiEx::Canvas::UpdateViewTransformPosition()
{
    m_ViewTransformPosition = CalcViewTransformPosition(m_View);
}

void ImGuiEx::Canvas::SaveInputState()
{
    auto& io = ImGui::GetIO();
    m_MousePosBackup = io.MousePos;
    m_MousePosPrevBackup = io.MousePosPrev;
    m_MouseDeltaBackup = io.MouseDelta;
    for (auto i = 0; i < IM_ARRAYSIZE(m_MouseClickedPosBackup); ++i)
        m_MouseClickedPosBackup[i] = io.MouseClickedPos[i];
}

void ImGuiEx::Canvas::RestoreInputState()
{
    auto& io = ImGui::GetIO();
    io.MousePos = m_MousePosBackup;
    io.MousePosPrev = m_MousePosPrevBackup;
    io.MouseDelta = m_MouseDeltaBackup;
    for (auto i = 0; i < IM_ARRAYSIZE(m_MouseClickedPosBackup); ++i)
        io.MouseClickedPos[i] = m_MouseClickedPosBackup[i];
}

void ImGuiEx::Canvas::SaveViewportState()
{
# if defined(IMGUI_HAS_VIEWPORT)
    auto viewport = m_Window->Viewport;

    m_ViewportPosBackup = viewport->Pos;
    m_ViewportSizeBackup = viewport->Size;
    m_ViewportWorkPosBackup = viewport->WorkPos;
    m_ViewportWorkSizeBackup = viewport->WorkSize;
# endif
}

void ImGuiEx::Canvas::RestoreViewportState()
{
# if defined(IMGUI_HAS_VIEWPORT)
    auto viewport = m_Window->Viewport;

    viewport->Pos = m_ViewportPosBackup;
    viewport->Size = m_ViewportSizeBackup;
    viewport->WorkPos = m_ViewportWorkPosBackup;
    viewport->WorkSize = m_ViewportWorkSizeBackup;
# endif
}

void ImGuiEx::Canvas::EnterLocalSpace()
{
    // Prepare ImDrawList for drawing in local coordinate system:
    //   - determine visible part of the canvas
    //   - start unique draw command
    //   - add clip rect matching canvas size
    //   - record current command index
    //   - record current vertex write index

    // Determine visible part of the canvas. Make it before
    // adding new command, to avoid round rip where command
    // is removed in PopClipRect() and added again next PushClipRect().
    ImGui::PushClipRect(m_WidgetPosition, m_WidgetPosition + m_WidgetSize, true);
    auto clipped_clip_rect = m_DrawList->_ClipRectStack.back();
    ImGui::PopClipRect();

    // Nothing of a canvas out of view can be seen or hovered.
    if (!m_IsVisible)
    {
        clipped_clip_rect.z = clipped_clip_rect.x;
        clipped_clip_rect.w = clipped_clip_rect.y;
    }

    m_DrawListCommandBufferSize = ImMax(m_DrawList->CmdBuffer.Size, 0);
    m_DrawListStartVertexIndex  = m_DrawList->_VtxCurrentIdx + ImVtxOffsetRef(m_DrawList);

    // Mark where local space begins with a draw command of its own, which also keeps
    // canvas commands from sharing one with what was drawn before. AddCallback() adds
    // a fresh command after it, so removing it in LeaveLocalSpace() leaves the buffer
    // as it would be without it.
    m_DrawList->AddCallback(ImDrawCallback_ImCanvas, this);

    m_DrawListFirstCommandIndex = ImMax(m_DrawList->CmdBuffer.Size - 1, 0);

    // One layout frame on every ImGui branch, with the window at the local origin.
    // The window's own layout state comes back on leaving local space, and the
    // local one on entering it again.
    m_WindowPosBackup               = m_Window->Pos;
    m_WindowIndentBackup            = m_Window->DC.Indent.x;
    m_WindowColumnsOffsetBackup     = m_Window->DC.ColumnsOffset.x;
    m_WindowGroupOffsetBackup       = m_Window->DC.GroupOffset.x;
    m_WindowWorkRectBackup          = m_Window->WorkRect;
    m_WindowContentRegionRectBackup = m_Window->ContentRegionRect;
    m_Window->Pos                   = ImVec2(0.0f, 0.0f);
    m_Window->DC.Indent.x           = m_LocalIndent;
    m_Window->DC.ColumnsOffset.x    = m_LocalColumnsOffset;
    m_Window->DC.GroupOffset.x      = m_LocalGroupOffset;
    m_Window->WorkRect              = m_LocalWorkRect;
    m_Window->ContentRegionRect     = m_LocalContentRegionRect;

# if defined(IMGUI_HAS_VIEWPORT)
    auto viewport_min = m_ViewportPosBackup;
    auto viewport_max = m_ViewportPosBackup + m_ViewportSizeBackup;

    viewport_min.x = (viewport_min.x - m_ViewTransformPosition.x) * m_View.InvScale();
    viewport_min.y = (viewport_min.y - m_ViewTransformPosition.y) * m_View.InvScale();
    viewport_max.x = (viewport_max.x - m_ViewTransformPosition.x) * m_View.InvScale();
    viewport_max.y = (viewport_max.y - m_ViewTransformPosition.y) * m_View.InvScale();

    auto viewport = m_Window->Viewport;
    viewport->Pos  = viewport_min;
    viewport->Size = viewport_max - viewport_min;

    viewport->WorkPos  = (m_ViewportWorkPosBackup - m_ViewTransformPosition) * m_View.InvScale();
    viewport->WorkSize = m_ViewportWorkSizeBackup * m_View.InvScale();
# endif

    // Clip rectangle in parent canvas space and move it to local space.
    m_ScreenClipRect = clipped_clip_rect;
    clipped_clip_rect.x = (clipped_clip_rect.x - m_ViewTransformPosition.x) * m_View.InvScale();
    clipped_clip_rect.y = (clipped_clip_rect.y - m_ViewTransformPosition.y) * m_View.InvScale();
    clipped_clip_rect.z = (clipped_clip_rect.z - m_ViewTransformPosition.x) * m_View.InvScale();
    clipped_clip_rect.w = (clipped_clip_rect.w - m_ViewTransformPosition.y) * m_View.InvScale();
    m_LocalClipRect = clipped_clip_rect;
    m_DrawList->PushClipRect(ImVec2(clipped_clip_rect.x, clipped_clip_rect.y), ImVec2(clipped_clip_rect.z, clipped_clip_rect.w), false);
    m_ClipRectStackSize = m_DrawList->_ClipRectStack.Size;
    for (auto& clipRect : m_SuspendedClipRects)
        m_DrawList->PushClipRect(ImVec2(clipRect.x, clipRect.y), ImVec2(clipRect.z, clipRect.w), false);
    m_SuspendedClipRects.resize(0);
    m_Window->ClipRect = ImRect(m_DrawList->_ClipRectStack.back());

    // Transform mouse position to local space.
    auto& io = ImGui::GetIO();
    io.MousePos     = (m_MousePosBackup - m_ViewTransformPosition) * m_View.InvScale();
    io.MousePosPrev = (m_MousePosPrevBackup - m_ViewTransformPosition) * m_View.InvScale();
    io.MouseDelta   = m_MouseDeltaBackup * m_View.InvScale();
    for (auto i = 0; i < IM_ARRAYSIZE(m_MouseClickedPosBackup); ++i)
        io.MouseClickedPos[i] = (m_MouseClickedPosBackup[i] - m_ViewTransformPosition) * m_View.InvScale();

    // An IME position set in local space is mapped to screen space on leaving it.
    auto& imeData = GImGui->PlatformImeData;
    m_ImeInputPosBackup = imeData.InputPos;
    imeData.InputPos = ImVec2(-FLT_MAX, -FLT_MAX);

    m_ViewRect = CalcViewRect(m_View);;

# if IMGUI_VERSION_NUM >= 19292
    m_LastPixelDensity = m_DrawList->_InvFringeScale;
    m_DrawList->_SetPixelDensity(m_LastPixelDensity * m_View.Scale());
# else
    auto& fringeScale = ImFringeScaleRef(m_DrawList);
    m_LastFringeScale = fringeScale;
    fringeScale *= m_View.InvScale();

    m_LastCurveTessellationTol = m_DrawList->_Data->CurveTessellationTol;
    m_DrawList->_Data->CurveTessellationTol *= m_View.InvScale() * m_View.InvScale(); // a squared distance
# endif
}

void ImGuiEx::Canvas::LeaveLocalSpace()
{
    // If you're here, a window begun inside the canvas wasn't ended before
    // Suspend() or End().
    IM_ASSERT(ImGui::GetCurrentWindow() == m_Window && "Canvas suspended or ended in another window than it began in.");
    IM_ASSERT(m_DrawList->_ClipRectStack.Size >= m_ClipRectStackSize && "Canvas clip rectangle was popped inside the canvas.");

    IM_ASSERT(m_DrawList->_Splitter._Current == m_ExpectedChannel);

    // The sentinel is where local space began in the command buffer it was entered with.
    int sentinelIndex = -1;
    for (int i = ImMax(m_DrawListCommandBufferSize - 1, 0); i < m_DrawList->CmdBuffer.Size && sentinelIndex < 0; ++i)
        if (m_DrawList->CmdBuffer[i].UserCallback == ImDrawCallback_ImCanvas && m_DrawList->CmdBuffer[i].UserCallbackData == this)
            sentinelIndex = i;

    // If you're here, another draw list splitter or channel is current than the one
    // the canvas began or resumed with.
    IM_ASSERT(sentinelIndex >= 0 && "Canvas suspended or ended in another draw list splitter or channel.");

    // Move vertices to screen space.
    auto vertex    = m_DrawList->VtxBuffer.Data + m_DrawListStartVertexIndex;
    auto vertexEnd = m_DrawList->VtxBuffer.Data + m_DrawList->_VtxCurrentIdx + ImVtxOffsetRef(m_DrawList);

    // If canvas view is not scaled take a faster path.
    if (m_View.Scale() != 1.0f)
    {
        while (vertex < vertexEnd)
        {
            vertex->pos.x = vertex->pos.x * m_View.Scale() + m_ViewTransformPosition.x;
            vertex->pos.y = vertex->pos.y * m_View.Scale() + m_ViewTransformPosition.y;
            ++vertex;
        }
    }
    else
    {
        while (vertex < vertexEnd)
        {
            vertex->pos.x = vertex->pos.x + m_ViewTransformPosition.x;
            vertex->pos.y = vertex->pos.y + m_ViewTransformPosition.y;
            ++vertex;
        }
    }

    // Move clip rectangles to screen space. An edge of the canvas' own clip rectangle, or of
    // one clipped by it, gets back the value it came from: the round trip through local space
    // can land just inside it, which a truncating scissor turns into a pixel lost.
    auto toScreen = [](float value, float local, float screen, float scale, float offset)
    {
        return value == local ? screen : value * scale + offset;
    };
    for (int i = m_DrawListFirstCommandIndex; i < m_DrawList->CmdBuffer.size(); ++i)
    {
        auto& clipRect = m_DrawList->CmdBuffer[i].ClipRect;
        clipRect.x = toScreen(clipRect.x, m_LocalClipRect.x, m_ScreenClipRect.x, m_View.Scale(), m_ViewTransformPosition.x);
        clipRect.y = toScreen(clipRect.y, m_LocalClipRect.y, m_ScreenClipRect.y, m_View.Scale(), m_ViewTransformPosition.y);
        clipRect.z = toScreen(clipRect.z, m_LocalClipRect.z, m_ScreenClipRect.z, m_View.Scale(), m_ViewTransformPosition.x);
        clipRect.w = toScreen(clipRect.w, m_LocalClipRect.w, m_ScreenClipRect.w, m_View.Scale(), m_ViewTransformPosition.y);
    }

    if (sentinelIndex >= 0)
        m_DrawList->CmdBuffer.erase(m_DrawList->CmdBuffer.Data + sentinelIndex);

# if IMGUI_VERSION_NUM >= 19292
    m_DrawList->_SetPixelDensity(m_LastPixelDensity);
# else
    auto& fringeScale = ImFringeScaleRef(m_DrawList);
    fringeScale = m_LastFringeScale;

    m_DrawList->_Data->CurveTessellationTol = m_LastCurveTessellationTol;
# endif

    // Clip rectangles pushed inside the canvas are in local space. Set them aside,
    // with the canvas' own, until Resume().
    m_SuspendedClipRects.resize(0);
    for (int i = m_ClipRectStackSize; i < m_DrawList->_ClipRectStack.Size; ++i)
        m_SuspendedClipRects.push_back(m_DrawList->_ClipRectStack[i]);
    while (m_DrawList->_ClipRectStack.Size >= m_ClipRectStackSize && m_DrawList->_ClipRectStack.Size > 0)
        m_DrawList->PopClipRect();
    m_Window->ClipRect = ImRect(m_DrawList->_ClipRectStack.back());

    auto& imeData = GImGui->PlatformImeData;
    if (imeData.InputPos.x == -FLT_MAX && imeData.InputPos.y == -FLT_MAX)
    {
        imeData.InputPos = m_ImeInputPosBackup;
    }
    else
    {
        imeData.InputPos        = FromLocal(imeData.InputPos);
        imeData.InputLineHeight = imeData.InputLineHeight * m_View.Scale();
    }

    m_LocalIndent                = m_Window->DC.Indent.x;
    m_LocalColumnsOffset         = m_Window->DC.ColumnsOffset.x;
    m_LocalGroupOffset           = m_Window->DC.GroupOffset.x;
    m_LocalWorkRect              = m_Window->WorkRect;
    m_LocalContentRegionRect     = m_Window->ContentRegionRect;
    m_Window->Pos                = m_WindowPosBackup;
    m_Window->DC.Indent.x        = m_WindowIndentBackup;
    m_Window->DC.ColumnsOffset.x = m_WindowColumnsOffsetBackup;
    m_Window->DC.GroupOffset.x   = m_WindowGroupOffsetBackup;
    m_Window->WorkRect           = m_WindowWorkRectBackup;
    m_Window->ContentRegionRect  = m_WindowContentRegionRectBackup;

    RestoreInputState();
    RestoreViewportState();
}
