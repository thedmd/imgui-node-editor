// Canvas widget - view over infinite virtual space.
//
// Canvas allows you to draw your widgets anywhere over infinite space and provide
// view over it with support for panning and scaling.
//
// Between Canvas::Begin() and Canvas::End() ImGui works in the canvas' local space:
//   - ImGui::GetCursorScreenPos() starts at the local origin, which the view places
//     on the screen.
//   - Mouse input is brought to local space, so widgets work as usual.
//   - Everything drawn with ImDrawList is in local space.
//
// The view decides which part of local space is shown. SetView() takes the offset of
// the local origin from the canvas' top left corner, and a scale; View(), ViewOrigin()
// and ViewScale() return them. CenterView() moves the view to a point or rectangle,
// and CalcCenterView() returns that view without setting it.
//
// The canvas takes the 'size' given to Begin(), and a component of zero or less takes
// the space left on that axis. Rect() returns the canvas' rectangle, and ViewRect()
// the visible part of local space, which CalcViewRect() returns for any view. What is
// drawn outside of it is clipped as usual in ImGui.
//
// FromLocal() and ToLocal() transform points between local space and the space the
// canvas is in; FromLocalV() and ToLocalV() transform vectors.
//
// Canvases can be nested, since a canvas is a regular widget of the space it begins in.
//
// Suspend() goes back to the space the canvas is in, to draw or begin windows there,
// and Resume() returns to local space.
//
// Note:
//     Suspend() and Resume() are valid only between Begin() and End(). The view can be
//     set and read at any time, and uses the canvas' rectangle from its last Begin().
//
// Limitations:
//   - Windows begun inside the canvas, such as popups, tooltips, combo boxes and
//     child windows, are placed at canvas coordinates as if they were screen ones,
//     and are not transformed. Begin them between Suspend() and Resume().
//   - Text is rasterized for the view's scale, so it stays sharp when zoomed in, only
//     with Dear ImGui 1.92 or later and a renderer backend that sets
//     ImGuiBackendFlags_RendererHasTextures; see SetRasterizeTextForScale(). Otherwise
//     glyphs are rasterized at the font's size and scaled with the view. Text after a
//     window that began and ended inside the canvas, or drawn with a font other than
//     the current one, is rasterized at the density ImGui last set for it.
//   - Before Dear ImGui 1.93, circles and rounded corners get the same number of
//     segments at any scale.
//
// VERSION 0.1
//
// LICENSE
//   This software is dual-licensed to the public domain and under the following
//   license: you are granted a perpetual, irrevocable license to copy, modify,
//   publish, and distribute this file as you see fit.
//
// CREDITS
//   Written by Michal Cichon
# ifndef IMGUI_EX_CANVAS_H
# define IMGUI_EX_CANVAS_H
# pragma once

# include <imgui.h>
# if !defined(IMGUI_VERSION_NUM)
#     error IMGUI_VERSION_NUM is not defined, please update Dear ImGui copy to at least 1.89 (November 2022)
# elif IMGUI_VERSION_NUM < 18900
#     error Please update Dear ImGui copy to at least 1.89 (November 2022)
# endif
# include <imgui_internal.h> // ImRect, ImFloor

#ifndef IMGUIEX_CANVAS_API
#define IMGUIEX_CANVAS_API
#endif

namespace ImGuiEx {

struct CanvasView
{
    CanvasView() = default;
    CanvasView(const ImVec2& origin, float scale)
        : m_Origin(origin)
    {
        SetScale(scale);
    }

    void Set(const ImVec2& origin, float scale)
    {
        *this = CanvasView(origin, scale);
    }

    const ImVec2& Origin()   const { return m_Origin; }
    float         Scale()    const { return m_Scale; }
    float         InvScale() const { return m_InvScale; }

    void SetOrigin(const ImVec2& origin) { m_Origin = origin; }

    // Scale must be positive and finite; any other is ignored.
    void SetScale(float scale)
    {
        IM_ASSERT(scale > 0.0f && scale <= FLT_MAX && "Canvas view scale must be positive and finite.");
        if (!(scale > 0.0f && scale <= FLT_MAX))
            return;

        m_Scale    = scale;
        m_InvScale = 1.0f / scale;
    }

private:
    ImVec2 m_Origin;
    float  m_Scale    = 1.0f;
    float  m_InvScale = 1.0f;
};

// Canvas widget represent view over infinite plane.
//
// It acts like a child window without scroll bars with
// ability to zoom to specific part of canvas plane.
//
// Widgets are clipped according to current view exactly
// same way ImGui do. To avoid `missing widgets` artifacts first
// setup visible region with SetView() then draw content.
//
// Everything drawn with ImDrawList between calls to Begin()/End()
// will be drawn on canvas plane. This behavior can be suspended
// by calling Suspend() and resumed by calling Resume().
//
// Warning:
//     Please do not interleave canvas with use of channel splitter.
//     Keep channel splitter contained inside canvas or always
//     call canvas functions from same channel.
struct Canvas
{
    // Begins drawing content of canvas plane.
    //
    // Returns true, and drawing must be ended with call to End(). A canvas
    // out of view clips its content away; check IsVisible() to skip it.
    //
    // If any size component is equal to zero or less canvas will
    // automatically expand to all available area on that axis.
    // So (0, 300) will take horizontal space and have height
    // of 300 points. (0, 0) will take all remaining space of
    // the window.
    //
    // You can query size of the canvas while it is being drawn
    // by calling Rect().
    IMGUIEX_CANVAS_API bool Begin(const char* id, const ImVec2& size);
    IMGUIEX_CANVAS_API bool Begin(ImGuiID id, const ImVec2& size);

    // Ends interaction with canvas plane.
    //
    // Must be called only when Begin() returned true.
    IMGUIEX_CANVAS_API void End();

    // Returns true if any part of the canvas is visible.
    //
    // Note:
    //     Valid after call to Begin().
    bool IsVisible() const { return m_IsVisible; }

    // Sets visible region of canvas plane.
    //
    // Origin is an offset of infinite plane origin from top left
    // corner of the canvas.
    //
    // Scale greater than 1 make canvas content be bigger, less than 1 smaller.
    IMGUIEX_CANVAS_API void SetView(const ImVec2& origin, float scale);
    IMGUIEX_CANVAS_API void SetView(const CanvasView& view);

    // Centers view over specific point on canvas plane.
    //
    // View will be centered on specific point by changing origin
    // but not scale.
    IMGUIEX_CANVAS_API void CenterView(const ImVec2& canvasPoint);

    // Calculates view over specific point on canvas plane.
    IMGUIEX_CANVAS_API CanvasView CalcCenterView(const ImVec2& canvasPoint) const;

    // Centers view over specific rectangle on canvas plane.
    //
    // Whole rectangle will fit in canvas view. This will affect both
    // origin and scale.
    IMGUIEX_CANVAS_API void CenterView(const ImRect& canvasRect);

    // Calculates view over specific rectangle on canvas plane.
    IMGUIEX_CANVAS_API CanvasView CalcCenterView(const ImRect& canvasRect) const;

    // Suspends canvas by returning to normal ImGui transformation space.
    // While suspended UI will not be drawn on canvas plane.
    //
    // Calls to Suspend()/Resume() are symmetrical. Each call to Suspend()
    // must be matched with call to Resume().
    IMGUIEX_CANVAS_API void Suspend();
    IMGUIEX_CANVAS_API void Resume();

    // Transforms point from canvas plane to ImGui.
    IMGUIEX_CANVAS_API ImVec2 FromLocal(const ImVec2& point) const;
    IMGUIEX_CANVAS_API ImVec2 FromLocal(const ImVec2& point, const CanvasView& view) const;

    // Transforms vector from canvas plane to ImGui.
    IMGUIEX_CANVAS_API ImVec2 FromLocalV(const ImVec2& vector) const;
    IMGUIEX_CANVAS_API ImVec2 FromLocalV(const ImVec2& vector, const CanvasView& view) const;

    // Transforms point from ImGui to canvas plane.
    IMGUIEX_CANVAS_API ImVec2 ToLocal(const ImVec2& point) const;
    IMGUIEX_CANVAS_API ImVec2 ToLocal(const ImVec2& point, const CanvasView& view) const;

    // Transforms vector from ImGui to canvas plane.
    IMGUIEX_CANVAS_API ImVec2 ToLocalV(const ImVec2& vector) const;
    IMGUIEX_CANVAS_API ImVec2 ToLocalV(const ImVec2& vector, const CanvasView& view) const;

    // Returns widget bounds.
    //
    // Note:
    //     Rect is valid after call to Begin().
    const ImRect& Rect() const { return m_WidgetRect; }

    // Returns visible region on canvas plane (in canvas plane coordinates).
    const ImRect& ViewRect() const { return m_ViewRect; }

    // Calculates visible region for view.
    IMGUIEX_CANVAS_API ImRect CalcViewRect(const CanvasView& view) const;

    // Returns current view.
    const CanvasView& View() const { return m_View; }

    // Returns origin of the view.
    //
    // Origin is an offset of infinite plane origin from top left
    // corner of the canvas.
    const ImVec2& ViewOrigin()  const { return m_View.Origin(); }

    // Returns scale of the view.
    float ViewScale() const { return m_View.Scale(); }

    // Returns true if canvas is suspended.
    //
    // See: Suspend()/Resume()
    bool IsSuspended() const { return m_SuspendCounter > 0; }

    // Rasterizes text in local space for the view's scale, so it stays sharp when
    // zoomed in. The scale is taken in quarter octave steps from 0.25 up, and glyphs
    // are rasterized at most 8 times their size, or at the framebuffer's density when
    // that is higher. On by default; see Limitations.
    void SetRasterizeTextForScale(bool rasterize) { m_RasterizeTextForScale = rasterize; }
    bool RasterizeTextForScale() const { return m_RasterizeTextForScale; }

private:
    ImVec2 CalcViewTransformPosition(const CanvasView& view) const;
    void UpdateViewTransformPosition();

    void SaveInputState();
    void RestoreInputState();

    void SaveViewportState();
    void RestoreViewportState();

    void EnterLocalSpace();
    void LeaveLocalSpace();

    bool m_InBeginEnd = false;
    bool m_IsVisible = false;

    ImGuiID m_ID = 0;

    ImVec2 m_WidgetPosition;
    ImVec2 m_WidgetSize;
    ImRect m_WidgetRect;

    ImGuiWindow* m_Window = nullptr;
    ImDrawList* m_DrawList = nullptr;
    int m_ExpectedChannel = 0;
    int m_ClipRectStackSize = 0;
    ImVector<ImVec4> m_SuspendedClipRects;
    ImVec4 m_ScreenClipRect;
    ImVec4 m_LocalClipRect;

    int m_DrawListFirstCommandIndex = 0;
    int m_DrawListCommandBufferSize = 0;
    int m_DrawListStartVertexIndex = 0;

    CanvasView  m_View;
    ImRect      m_ViewRect;

    ImVec2 m_ViewTransformPosition;
    float  m_PixelDensity = 1.0f; // of the space the canvas began in

    int m_SuspendCounter = 0;

    bool m_RasterizeTextForScale = true;
# if IMGUI_VERSION_NUM >= 19200
    float m_LastTextDensity = 0.0f;
# endif

# if IMGUI_VERSION_NUM >= 19292
    float m_LastPixelDensity = 1.0f;
# else
    float m_LastFringeScale = 1.0f;
    float m_LastCurveTessellationTol = 1.25f;
# endif

    ImVec2 m_MousePosBackup;
    ImVec2 m_MousePosPrevBackup;
    ImVec2 m_MouseDeltaBackup;
    ImVec2 m_ImeInputPosBackup;
    ImVec2 m_MouseClickedPosBackup[IM_ARRAYSIZE(ImGuiIO::MouseClickedPos)];
    ImVec2 m_WindowCursorMaxBackup;
    ImVec2 m_WindowIdealMaxBackup;
    ImVec2 m_WindowPosBackup;
    float  m_WindowIndentBackup = 0.0f;
    float  m_WindowColumnsOffsetBackup = 0.0f;
    float  m_WindowGroupOffsetBackup = 0.0f;
    ImRect m_WindowWorkRectBackup;
    ImRect m_WindowContentRegionRectBackup;
    float  m_LocalIndent = 0.0f;
    float  m_LocalColumnsOffset = 0.0f;
    float  m_LocalGroupOffset = 0.0f;
    ImRect m_LocalWorkRect;
    ImRect m_LocalContentRegionRect;

# if defined(IMGUI_HAS_VIEWPORT)
    ImVec2 m_ViewportPosBackup;
    ImVec2 m_ViewportSizeBackup;
    ImVec2 m_ViewportWorkPosBackup;
    ImVec2 m_ViewportWorkSizeBackup;
# endif
};

} // namespace ImGuiEx

# endif // IMGUI_EX_CANVAS_H