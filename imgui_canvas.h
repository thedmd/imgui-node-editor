// Canvas widget - view over infinite virtual space.
//
// Canvas allows you to draw your widgets anywhere over infinite space and provide
// view over it with support for panning and scaling.
//
// When you enter a canvas ImGui is moved to virtual space which mean:
//   - ImGui::GetCursorScreenPos() return (0, 0) and which correspond to top left corner
//     of the canvas on the screen (this can be changed using CanvasView()).
//   - Mouse input is brought to canvas space, so widgets works as usual.
//   - Everything you draw with ImDrawList will be in virtual space.
//
// By default origin point is on top left corner of canvas widget. It can be
// changed with call to CanvasView() where you can specify what part of space
// should be viewed by setting viewport origin point and scale. Current state
// can be queried with CanvasViewOrigin() and CanvasViewScale().
//
// Viewport size is controlled by 'size' parameter in BeginCanvas(). You can query
// it using CanvasContentMin/Max/Size functions. They are useful if you to not specify
// canvas size in which case all free space is used.
//
// Bounds of visible region of infinite space can be queried using CanvasViewMin/Max/Size
// functions. Everything that is drawn outside of this region will be clipped
// as usual in ImGui.
//
// While drawing inside canvas you can translate position from world (usual ImGui space)
// to virtual space and back using CanvasFromWorld()/CanvasToWorld().
//
// Canvas can be nested in each other (they are regular widgets after all). There
// is a way to transform position between current and parent canvas with
// CanvasFromParent()/CanvasToParent().
//
// Sometimes in more elaborate scenarios you want to move out canvas virtual space,
// do something and came back. You can do that with SuspendCanvas() and ResumeCanvas().
//
// Note:
//     It is not valid to call canvas API outside of BeginCanvas() / EndCanvas() scope.
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
# ifndef __IMGUI_EX_CANVAS_H__
# define __IMGUI_EX_CANVAS_H__
# pragma once

# include <imgui.h>
# include <imgui_internal.h> // ImRect, ImFloor

#ifndef IMGUIEX_CANVAS_API
#define IMGUIEX_CANVAS_API
#endif

namespace ImGuiEx {

struct CanvasView
{
    ImVec2 Origin;
    float  Scale  = 1.0f;
    float  InvScale = 1.0f;

    CanvasView() = default;
    CanvasView(const ImVec2& origin, float scale)
        : Origin(origin)
        , Scale(scale)
        , InvScale(scale ? 1.0f / scale : 0.0f)
    {
    }

    void Set(const ImVec2& origin, float scale)
    {
        *this = CanvasView(origin, scale);
    }
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
// Everything drawn with ImDrawList betwen calls to Begin()/End()
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
    // When false is returned that mean canvas is not visible to the
    // user can drawing should be skipped and End() not called.
    // When true is returned drawing must be ended with call to End().
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
    // Must be called only when Begin() retuned true.
    IMGUIEX_CANVAS_API void End();

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
    // Calls to Suspend()/Resume() are symetrical. Each call to Suspend()
    // must be matched with call to Resume().
    IMGUIEX_CANVAS_API void Suspend();
    IMGUIEX_CANVAS_API void Resume();

    // Transforms point from canvas plane to ImGui.
    IMGUIEX_CANVAS_API ImVec2 FromLocal(const ImVec2& point) const;
    IMGUIEX_CANVAS_API ImVec2 FromLocal(const ImVec2& point, const CanvasView& view) const;

    // Transforms vector from canvas plant to ImGui.
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
    const ImVec2& ViewOrigin()  const { return m_View.Origin; }

    // Returns scale of the view.
    float ViewScale() const { return m_View.Scale; }

    // Returns true if canvas is suspended.
    //
    // See: Suspend()/Resume()
    bool IsSuspended() const { return m_SuspendCounter > 0; }

private:
# define IMGUI_EX_CANVAS_DEFERED() 0

# if IMGUI_EX_CANVAS_DEFERED()
    struct Range
    {
        int BeginVertexIndex = 0;
        int EndVertexIndex   = 0;
        int BeginComandIndex = 0;
        int EndCommandIndex  = 0;
    };
# endif

    void UpdateViewTransformPosition();

    void SaveInputState();
    void RestoreInputState();

    void SaveViewportState();
    void RestoreViewportState();

    void EnterLocalSpace();
    void LeaveLocalSpace();

    bool m_InBeginEnd = false;

    ImVec2 m_WidgetPosition;
    ImVec2 m_WidgetSize;
    ImRect m_WidgetRect;

    ImDrawList* m_DrawList = nullptr;
    int m_ExpectedChannel = 0;

# if IMGUI_EX_CANVAS_DEFERED()
    ImVector<Range> m_Ranges;
    Range* m_CurrentRange = nullptr;
# endif

    int m_DrawListCommadBufferSize = 0;
    int m_DrawListStartVertexIndex = 0;

    CanvasView  m_View;
    ImRect      m_ViewRect;

    ImVec2 m_ViewTransformPosition;

    int m_SuspendCounter = 0;

    float m_LastFringeScale = 1.0f;

    ImVec2 m_MousePosBackup;
    ImVec2 m_MousePosPrevBackup;
    ImVec2 m_MouseClickedPosBackup[IM_ARRAYSIZE(ImGuiIO::MouseClickedPos)];
    ImVec2 m_WindowCursorMaxBackup;

# if defined(IMGUI_HAS_VIEWPORT)
    ImVec2 m_WindowPosBackup;
    ImVec2 m_ViewportPosBackup;
    ImVec2 m_ViewportSizeBackup;
# if IMGUI_VERSION_NUM > 18002
    ImVec2 m_ViewportWorkPosBackup;
    ImVec2 m_ViewportWorkSizeBackup;
# else
    ImVec2 m_ViewportWorkOffsetMinBackup;
    ImVec2 m_ViewportWorkOffsetMaxBackup;
# endif
# endif
};

} // namespace ImGuiEx

# endif // __IMGUI_EX_CANVAS_H__