//------------------------------------------------------------------------------
// The canvas widget on its own: view, suspension and the state it remaps.
//------------------------------------------------------------------------------
# include "harness.h"
# include "imgui_canvas.h"
# include <cfloat>
# include <cmath>
# include <limits>

namespace {

const ImU32 c_Red   = IM_COL32(255,   0, 0, 255);
const ImU32 c_Green = IM_COL32(  0, 255, 0, 255);

// Bounds of the vertices of the color.
ImRect VertexBounds(const ImDrawList* drawList, ImU32 color)
{
    ImRect bounds(FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX);
    for (auto& vertex : drawList->VtxBuffer)
        if (vertex.col == color)
            bounds.Add(vertex.pos);
    return bounds;
}

// A window at the origin without padding, so a canvas in it starts at (0, 0).
void BeginHostWindow()
{
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(600.0f, 400.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("Host", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar();
}

// Clip rectangles of the commands that draw vertices of the color.
ImVector<ImVec4> ClipRectsOf(const ImDrawList* drawList, ImU32 color)
{
    ImVector<ImVec4> result;
    for (auto& command : drawList->CmdBuffer)
    {
        for (unsigned int i = 0; i < command.ElemCount; ++i)
        {
            const auto index = drawList->IdxBuffer[static_cast<int>(command.IdxOffset + i)];
            if (drawList->VtxBuffer[static_cast<int>(command.VtxOffset + index)].col == color)
            {
                result.push_back(command.ClipRect);
                break;
            }
        }
    }
    return result;
}

} // namespace

TEST_CASE(SetViewWhileSuspended, "canvas_widget.set_view_while_suspended_stays_in_screen_space", "CNV-4", test::TestFlags_None)
{
    // At scale 3 a rectangle at local (36, 36) lands at (108, 108). The view then
    // changes while suspended, as a "center view" button in an overlay does.
    test::Host host;
    ImGui::GetIO().AddMousePosEvent(150.0f, 150.0f);
    host.NewFrame();
    BeginHostWindow();
    ImGuiEx::Canvas canvas;
    canvas.SetView(ImVec2(0.0f, 0.0f), 3.0f);
    canvas.Begin("canvas", ImVec2(300.0f, 300.0f));
    auto drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(ImVec2(36.0f, 36.0f), ImVec2(40.0f, 40.0f), c_Red);

    canvas.Suspend();
    const auto mouse = ImGui::GetIO().MousePos;
    const auto clip  = drawList->_ClipRectStack.back();
    canvas.SetView(ImVec2(-30.0f, -30.0f), 3.0f);
    const auto mouseAfter = ImGui::GetIO().MousePos;
    const auto clipAfter  = drawList->_ClipRectStack.back();
    drawList->AddRectFilled(ImVec2(200.0f, 200.0f), ImVec2(210.0f, 210.0f), c_Green);
    canvas.Resume();

    canvas.End();
    ImGui::End();
    host.Render();

    const auto local  = VertexBounds(drawList, c_Red);
    const auto screen = VertexBounds(drawList, c_Green);
    test::Check(local.Min.x == 108.0f && local.Min.y == 108.0f, "geometry drawn before Suspend() is transformed once: at (%g, %g)", local.Min.x, local.Min.y);
    test::Check(screen.Min.x == 200.0f && screen.Min.y == 200.0f, "geometry drawn while suspended stays in screen space: at (%g, %g)", screen.Min.x, screen.Min.y);
    test::Check(mouseAfter.x == mouse.x && mouseAfter.y == mouse.y, "the mouse stays in screen space: (%g, %g), was (%g, %g)", mouseAfter.x, mouseAfter.y, mouse.x, mouse.y);
    test::Check(clipAfter.x == clip.x && clipAfter.y == clip.y && clipAfter.z == clip.z && clipAfter.w == clip.w, "the host's clip rectangle stays current");
}

TEST_CASE(ViewRectOutsideBeginEnd, "canvas_widget.view_rect_follows_set_view", "CNV-4", test::TestFlags_None)
{
    // A 300 by 300 canvas, then the view at origin (-100, -50) and scale 2 between frames.
    test::Host host;
    host.NewFrame();
    BeginHostWindow();
    ImGuiEx::Canvas canvas;
    canvas.Begin("canvas", ImVec2(300.0f, 300.0f));
    canvas.End();
    ImGui::End();
    host.Render();

    canvas.SetView(ImVec2(-100.0f, -50.0f), 2.0f);
    const auto rect = canvas.ViewRect();
    test::Check(rect.Min.x == 50.0f && rect.Min.y == 25.0f && rect.Max.x == 200.0f && rect.Max.y == 175.0f,
        "the view rectangle is (50, 25) - (200, 175): (%g, %g) - (%g, %g)", rect.Min.x, rect.Min.y, rect.Max.x, rect.Max.y);
}

TEST_CASE(ClipRectAcrossSuspend, "canvas_widget.clip_rect_across_suspend_is_set_aside", "CNV-5", test::TestFlags_None)
{
    // At scale 2, a clip rectangle pushed inside the canvas stays pushed across
    // Suspend(); a rectangle drawn while suspended is in screen space.
    test::Host host;
    host.NewFrame();
    BeginHostWindow();
    ImGuiEx::Canvas canvas;
    canvas.SetView(ImVec2(0.0f, 0.0f), 2.0f);
    canvas.Begin("canvas", ImVec2(300.0f, 300.0f));
    auto drawList = ImGui::GetWindowDrawList();
    ImGui::PushClipRect(ImVec2(10.0f, 10.0f), ImVec2(50.0f, 50.0f), true);
    const auto pushed = drawList->_ClipRectStack.back();

    canvas.Suspend();
    drawList->AddRectFilled(ImVec2(200.0f, 200.0f), ImVec2(210.0f, 210.0f), c_Green);
    canvas.Resume();
    const auto resumed = drawList->_ClipRectStack.back();

    ImGui::PopClipRect();
    canvas.End();
    ImGui::End();
    host.Render();

    const auto geometry = test::Capture(drawList);
    int drawn = 0, outside = 0;
    for (size_t i = 0; i < geometry.Colors.size(); ++i)
    {
        if (geometry.Colors[i] != c_Green)
            continue;
        ++drawn;
        const auto& p    = geometry.Positions[i];
        const auto& clip = geometry.ClipRects[i];
        if (p.x < clip.x || p.y < clip.y || p.x > clip.z || p.y > clip.w)
            ++outside;
    }

    test::Check(drawn > 0 && outside == 0, "drawing while suspended is clipped in screen space: %d of %d vertices outside their clip rectangle", outside, drawn);
    test::Check(resumed.x == pushed.x && resumed.y == pushed.y && resumed.z == pushed.z && resumed.w == pushed.w,
        "the pushed clip rectangle is current again after Resume(): (%g, %g) - (%g, %g)", resumed.x, resumed.y, resumed.z, resumed.w);
}

TEST_CASE(ClipRectLeftAtEnd, "canvas_widget.clip_rect_left_pushed_at_end_asserts", "CNV-5", test::TestFlags_None)
{
    test::ExpectAsserts(1);
    test::Host host;
    host.NewFrame();
    BeginHostWindow();
    ImGuiEx::Canvas canvas;
    canvas.Begin("canvas", ImVec2(300.0f, 300.0f));
    ImGui::PushClipRect(ImVec2(10.0f, 10.0f), ImVec2(50.0f, 50.0f), true);
    canvas.End();
    ImGui::End();
    host.Render();
}

TEST_CASE(SuspendInChildWindow, "canvas_widget.suspend_in_another_window_asserts", "CNV-5", test::TestFlags_None)
{
    // A child window begun inside the canvas is current when the canvas is suspended.
    test::ExpectAsserts(1);
    test::Host host;
    host.NewFrame();
    BeginHostWindow();
    ImGuiEx::Canvas canvas;
    canvas.Begin("canvas", ImVec2(300.0f, 300.0f));
    ImGui::BeginChild("child", ImVec2(100.0f, 100.0f));
    const auto position = ImGui::GetCurrentWindow()->Pos;
    canvas.Suspend();
    const auto suspendedPosition = ImGui::GetCurrentWindow()->Pos;
    canvas.Resume();
    ImGui::EndChild();
    canvas.End();
    ImGui::End();
    host.Render();

    test::Check(suspendedPosition.x == position.x && suspendedPosition.y == position.y,
        "the child window stays in place: at (%g, %g), was (%g, %g)", suspendedPosition.x, suspendedPosition.y, position.x, position.y);
}

TEST_CASE(SuspendOutsideBeginEnd, "canvas_widget.suspend_outside_begin_end_asserts", "CNV-5", test::TestFlags_None)
{
    // Suspend() and Resume() on the frame after the canvas, before its Begin().
    test::ExpectAsserts(2);
    test::Host host;
    ImGuiEx::Canvas canvas;
    host.NewFrame();
    BeginHostWindow();
    canvas.SetView(ImVec2(0.0f, 0.0f), 2.0f);
    canvas.Begin("canvas", ImVec2(300.0f, 300.0f));
    canvas.End();
    ImGui::End();
    host.Render();

    ImGui::GetIO().AddMousePosEvent(150.0f, 150.0f);
    host.NewFrame();
    BeginHostWindow();
    const auto mouse = ImGui::GetIO().MousePos;
    canvas.Suspend();
    canvas.Resume();
    const auto mouseAfter = ImGui::GetIO().MousePos;
    ImGui::End();
    host.Render();

    test::Check(mouseAfter.x == mouse.x && mouseAfter.y == mouse.y, "the mouse stays where it is: (%g, %g), was (%g, %g)", mouseAfter.x, mouseAfter.y, mouse.x, mouse.y);
}

TEST_CASE(ViewScale, "canvas_widget.view_keeps_a_positive_scale", "CNV-5", test::TestFlags_None)
{
    // Scales of 0, -1 and infinity are each refused with an assert.
    test::ExpectAsserts(3);
    ImGuiEx::CanvasView view(ImVec2(10.0f, 20.0f), 0.0f);
    const bool zeroRefused = view.Scale() == 1.0f && view.InvScale() == 1.0f;
    view.SetScale(-1.0f);
    view.SetScale(std::numeric_limits<float>::infinity());
    const bool othersRefused = view.Scale() == 1.0f && view.InvScale() == 1.0f;
    view.SetScale(4.0f);

    test::Check(zeroRefused && othersRefused, "the view keeps scale 1 when given 0, -1 or infinity");
    test::Check(view.Scale() == 4.0f && view.InvScale() == 0.25f, "the inverse scale follows the scale: %g for %g", view.InvScale(), view.Scale());
}

TEST_CASE(ViewportWorkPos, "canvas_widget.viewport_work_area_is_in_local_space", "CNV-7", test::TestFlags_None)
{
# if defined(IMGUI_HAS_VIEWPORT)
    // A main menu bar moves the viewport's work area down; the canvas sits at
    // (100, 80) at scale 2.
    test::Host host;
    ImGuiEx::Canvas canvas;
    ImVec2 position, size, workPosition, expected;
    for (int frame = 0; frame < 2; ++frame)
    {
        host.NewFrame();
        if (ImGui::BeginMainMenuBar())
            ImGui::EndMainMenuBar();
        BeginHostWindow();
        ImGui::SetCursorScreenPos(ImVec2(100.0f, 80.0f));
        canvas.SetView(ImVec2(0.0f, 0.0f), 2.0f);
        const auto screenWorkPosition = ImGui::GetMainViewport()->WorkPos;
        canvas.Begin("canvas", ImVec2(300.0f, 300.0f));
        auto viewport = ImGui::GetWindowViewport();
        position     = viewport->Pos;
        size         = viewport->Size;
        workPosition = viewport->WorkPos;
        expected     = canvas.ToLocal(screenWorkPosition);
        canvas.End();
        ImGui::End();
        host.Render();
    }

    test::Check(workPosition.x == expected.x && workPosition.y == expected.y,
        "the work area starts at its local position (%g, %g): (%g, %g)", expected.x, expected.y, workPosition.x, workPosition.y);
    test::Check(workPosition.x >= position.x && workPosition.y >= position.y && workPosition.x <= position.x + size.x && workPosition.y <= position.y + size.y,
        "the work area starts inside the viewport (%g, %g) - (%g, %g)", position.x, position.y, position.x + size.x, position.y + size.y);
# else
    test::Skip("needs a Dear ImGui branch with viewports");
# endif
}

TEST_CASE(MouseDelta, "canvas_widget.mouse_delta_is_in_local_space", "CNV-8", test::TestFlags_None)
{
    // The mouse moves by (20, 10) between frames; the canvas is at scale 2.
    test::Host host;
    ImGuiEx::Canvas canvas;
    ImVec2 inside, after;
    for (int frame = 0; frame < 2; ++frame)
    {
        ImGui::GetIO().AddMousePosEvent(frame == 0 ? 150.0f : 170.0f, frame == 0 ? 150.0f : 160.0f);
        host.NewFrame();
        BeginHostWindow();
        canvas.SetView(ImVec2(0.0f, 0.0f), 2.0f);
        canvas.Begin("canvas", ImVec2(300.0f, 300.0f));
        inside = ImGui::GetIO().MouseDelta;
        canvas.End();
        after = ImGui::GetIO().MouseDelta;
        ImGui::End();
        host.Render();
    }

    test::Check(inside.x == 10.0f && inside.y == 5.0f, "inside the canvas the mouse moved by (10, 5): (%g, %g)", inside.x, inside.y);
    test::Check(after.x == 20.0f && after.y == 10.0f, "after it, by (20, 10) again: (%g, %g)", after.x, after.y);
}

TEST_CASE(ImePosition, "canvas_widget.ime_position_is_in_screen_space", "CNV-8", test::TestFlags_None)
{
    // A focused InputText() at local (20, 30) in a canvas at (100, 80) and scale 2.
    test::Host host;
    ImGuiEx::Canvas canvas;
    char text[32] = "";
    ImRect item;
    for (int frame = 0; frame < 3; ++frame)
    {
        host.NewFrame();
        BeginHostWindow();
        ImGui::SetCursorScreenPos(ImVec2(100.0f, 80.0f));
        canvas.SetView(ImVec2(0.0f, 0.0f), 2.0f);
        canvas.Begin("canvas", ImVec2(300.0f, 300.0f));
        ImGui::SetCursorScreenPos(ImVec2(20.0f, 30.0f));
        if (frame == 0)
            ImGui::SetKeyboardFocusHere();
        ImGui::InputText("##text", text, sizeof(text));
        item = ImRect(canvas.FromLocal(ImGui::GetItemRectMin()), canvas.FromLocal(ImGui::GetItemRectMax()));
        canvas.End();
        ImGui::End();
        host.Render();
    }

    const auto& ime = GImGui->PlatformImeData;
    test::Check(ime.WantVisible, "the input wants the IME");
    test::Check(ime.InputPos.x >= item.Min.x - 2.0f && ime.InputPos.y >= item.Min.y - 2.0f && ime.InputPos.x <= item.Max.x && ime.InputPos.y <= item.Max.y,
        "the IME is placed at the input on screen, (%g, %g) - (%g, %g): at (%g, %g)", item.Min.x, item.Min.y, item.Max.x, item.Max.y, ime.InputPos.x, ime.InputPos.y);
    test::Check(ime.InputLineHeight == ImGui::GetFontSize() * 2.0f, "the IME line is as tall as on screen: %g", ime.InputLineHeight);
}

TEST_CASE(PixelSnappedOrigin, "canvas_widget.view_origin_is_on_whole_pixels", "CNV-9", test::TestFlags_None)
{
    // At scale 1 with the origin at (-150.5, -90.25), as zooming in and back out can
    // leave it, a rectangle on whole canvas units.
    test::Host host;
    host.NewFrame();
    BeginHostWindow();
    ImGuiEx::Canvas canvas;
    canvas.SetView(ImVec2(-150.5f, -90.25f), 1.0f);
    canvas.Begin("canvas", ImVec2(300.0f, 300.0f));
    auto drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(ImVec2(160.0f, 100.0f), ImVec2(200.0f, 140.0f), c_Red);
    const auto mouseMapped = canvas.ToLocal(canvas.FromLocal(ImVec2(170.0f, 110.0f)));
    canvas.End();
    ImGui::End();
    host.Render();

    const auto rect = VertexBounds(drawList, c_Red);
    test::Check(rect.Min.x == std::floor(rect.Min.x) && rect.Min.y == std::floor(rect.Min.y) && rect.Max.x == std::floor(rect.Max.x) && rect.Max.y == std::floor(rect.Max.y),
        "the rectangle lands on whole pixels: (%g, %g) - (%g, %g)", rect.Min.x, rect.Min.y, rect.Max.x, rect.Max.y);
    test::Check(mouseMapped.x == 170.0f && mouseMapped.y == 110.0f, "mapping to the screen and back is exact: (%g, %g)", mouseMapped.x, mouseMapped.y);
}

TEST_CASE(PixelSnappedOriginAtFractionalDensity, "canvas_widget.view_origin_is_on_whole_pixels_at_fractional_density", nullptr, test::TestFlags_None)
{
# if IMGUI_VERSION_NUM >= 19292
    // As above, with a framebuffer at 1.5 times the display's density, where every other whole
    // unit is half a pixel off: a rectangle on whole pixels of local space, without anti-aliasing.
    test::Host host;
    ImGui::GetIO().DisplayFramebufferScale = ImVec2(1.5f, 1.5f);
    host.NewFrame();
    BeginHostWindow();
    ImGuiEx::Canvas canvas;
    canvas.SetView(ImVec2(-150.5f, -90.25f), 1.0f);
    canvas.Begin("canvas", ImVec2(300.0f, 300.0f));
    auto drawList = ImGui::GetWindowDrawList();
    drawList->PrimReserve(6, 4);
    drawList->PrimRect(ImVec2(160.0f, 100.0f), ImVec2(200.0f, 140.0f), c_Red);
    canvas.End();
    ImGui::End();
    host.Render();

    auto onWholePixel = [](float value) { return std::fabs(value * 1.5f - std::round(value * 1.5f)) < 0.001f; };
    const auto rect = VertexBounds(drawList, c_Red);
    test::Check(onWholePixel(rect.Min.x) && onWholePixel(rect.Min.y) && onWholePixel(rect.Max.x) && onWholePixel(rect.Max.y),
        "the rectangle lands on whole pixels: (%g, %g) - (%g, %g) in pixels", rect.Min.x * 1.5f, rect.Min.y * 1.5f, rect.Max.x * 1.5f, rect.Max.y * 1.5f);
# else
    test::Skip("needs Dear ImGui 1.93, whose draw lists know their pixel density");
# endif
}

TEST_CASE(SuspendWithAnotherSplitter, "canvas_widget.suspend_with_another_splitter_asserts", "CNV-1", test::TestFlags_None)
{
    // A splitter of the application's own has its second channel current when the
    // canvas is suspended; its channel numbers match the window's.
    test::ExpectAsserts(1);
    test::Host host;
    host.NewFrame();
    BeginHostWindow();
    ImGuiEx::Canvas canvas;
    canvas.Begin("canvas", ImVec2(300.0f, 300.0f));
    auto drawList = ImGui::GetWindowDrawList();
    ImDrawListSplitter splitter;
    splitter.Split(drawList, 2);
    splitter.SetCurrentChannel(drawList, 1);
    bool asserted = false;
    try
    {
        test::ThrowOnAssert throwOnAssert;
        canvas.Suspend();
    }
    catch (const test::AssertFailure&)
    {
        asserted = true;
    }
    splitter.SetCurrentChannel(drawList, 0);
    splitter.Merge(drawList);
    canvas.End();
    ImGui::End();
    host.Render();

    test::Check(asserted, "Suspend() asserts");
    test::Check(host.StrayCallbacks == 0, "no canvas callback is left for the renderer: %d", host.StrayCallbacks);
}

TEST_CASE(SentinelAfterMisuse, "canvas_widget.misused_canvas_leaves_no_sentinel", "CNV-1", test::TestFlags_None)
{
    // As above, but the canvas goes on past the assert and resumes in the other
    // splitter's channel before it is merged.
    test::ExpectAsserts(1);
    test::Host host;
    host.NewFrame();
    BeginHostWindow();
    ImGuiEx::Canvas canvas;
    canvas.Begin("canvas", ImVec2(300.0f, 300.0f));
    auto drawList = ImGui::GetWindowDrawList();
    ImDrawListSplitter splitter;
    splitter.Split(drawList, 2);
    splitter.SetCurrentChannel(drawList, 1);
    canvas.Suspend();
    canvas.Resume();
    splitter.SetCurrentChannel(drawList, 0);
    splitter.Merge(drawList);
    canvas.End();
    ImGui::End();
    host.Render();

    test::Check(host.StrayCallbacks == 0, "no canvas callback is left for the renderer: %d", host.StrayCallbacks);
}

TEST_CASE(LocalLayoutFrame, "canvas_widget.layout_starts_at_the_local_origin", "CNV-6", test::TestFlags_None)
{
    // A 200 by 150 canvas at scale 1 in a padded window at (300, 100).
    test::Host host;
    host.NewFrame();
    ImGui::SetNextWindowPos(ImVec2(300.0f, 100.0f));
    ImGui::SetNextWindowSize(ImVec2(600.0f, 400.0f));
    ImGui::Begin("Host", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings);
    ImGuiEx::Canvas canvas;
    canvas.Begin("canvas", ImVec2(200.0f, 150.0f));
    ImGui::TextUnformatted("First");
    ImGui::TextUnformatted("Second");
    const auto second = ImGui::GetItemRectMin();
    ImGui::SameLine(50.0f);
    ImGui::TextUnformatted("Third");
    const auto third  = ImGui::GetItemRectMin();
    const auto cursor = ImGui::GetCursorScreenPos();
    const auto avail  = ImGui::GetContentRegionAvail();
    canvas.End();
    const auto position = ImGui::GetWindowPos();
    ImGui::End();
    host.Render();

    test::Check(second.x == 0.0f, "a new line starts at local x = 0: %g", second.x);
    test::Check(third.x == 50.0f, "SameLine(50) starts at local x = 50: %g", third.x);
    test::Check(avail.x == 200.0f - cursor.x && avail.y == 150.0f - cursor.y,
        "the content region ends where the canvas does: (%g, %g) available at (%g, %g)", avail.x, avail.y, cursor.x, cursor.y);
    test::Check(position.x == 300.0f && position.y == 100.0f, "the window is back at (300, 100) after the canvas: (%g, %g)", position.x, position.y);
}

TEST_CASE(IdealSizeKeepsLocalContentOut, "canvas_widget.local_content_leaves_the_window_ideal_size_alone", "CNV-11", test::TestFlags_None)
{
    // An auto-resizing window around a 200 by 100 canvas, with a table at local (2000, 1500),
    // as a node may hold. Tables, unlike most items, extend the window's ideal size.
    test::Host host;
    ImVec2 size;
    for (int frame = 0; frame < 4; ++frame)
    {
        host.NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("Auto", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::PopStyleVar();
        ImGuiEx::Canvas canvas;
        canvas.Begin("canvas", ImVec2(200.0f, 100.0f));
        ImGui::SetCursorScreenPos(ImVec2(2000.0f, 1500.0f));
        if (ImGui::BeginTable("table", 1, ImGuiTableFlags_SizingFixedFit))
        {
            ImGui::TableNextColumn();
            ImGui::TextUnformatted("Cell");
            ImGui::EndTable();
        }
        canvas.End();
        size = ImGui::GetWindowSize();
        ImGui::End();
        host.Render();
    }

    test::Check(size.x <= 200.0f + 2.0f && size.y <= 100.0f + 2.0f, "the window fits the canvas: %g x %g", size.x, size.y);
}

TEST_CASE(ClipRectsMapBackExactly, "canvas_widget.clip_rectangles_map_back_to_the_screen_exactly", "CNV-11", test::TestFlags_None)
{
    // Canvases at fractional positions, scales and origins. Their clip rectangle goes to local
    // space and back, where a value just inside the original makes a truncating scissor lose a
    // pixel. Red is drawn under the canvas' clip, green under one pushed inside it that reaches
    // past the canvas' bottom right.
    int configurations = 0, inexact = 0, inexactInner = 0, outside = 0;
    for (float x : { 10.3f, 33.7f, 101.1f })
        for (float scale : { 0.3f, 0.7f, 1.0f, 1.3f, 2.1f, 3.7f })
            for (float origin : { 0.0f, -123.4f, 7777.7f })
            {
                test::Host host;
                host.NewFrame();
                BeginHostWindow();
                ImGui::SetCursorScreenPos(ImVec2(x, x * 0.5f));
                ImRect expected = ImGui::GetCurrentWindow()->ClipRect;
                ImGuiEx::Canvas canvas;
                canvas.SetView(ImVec2(origin, origin * 0.5f), scale);
                canvas.Begin("canvas", ImVec2(250.0f, 150.0f));
                expected.ClipWithFull(canvas.Rect());
                auto drawList = ImGui::GetWindowDrawList();
                const auto view = canvas.ViewRect();
                drawList->AddRectFilled(view.Min, view.Max, c_Red);
                drawList->PushClipRect(view.Min + ImVec2(7.0f, 5.0f), view.Max + ImVec2(50.0f, 50.0f), true);
                drawList->AddRectFilled(view.Min, view.Max, c_Green);
                drawList->PopClipRect();
                canvas.End();
                ImGui::End();
                host.Render();

                ++configurations;
                for (auto& clip : ClipRectsOf(drawList, c_Red))
                    inexact += clip.x != expected.Min.x || clip.y != expected.Min.y || clip.z != expected.Max.x || clip.w != expected.Max.y;
                for (auto& clip : ClipRectsOf(drawList, c_Green))
                {
                    inexactInner += clip.z != expected.Max.x || clip.w != expected.Max.y;
                    outside      += clip.x < expected.Min.x || clip.y < expected.Min.y;
                }
            }

    test::Check(inexact == 0, "the canvas' clip rectangle comes back as it was: %d of %d differ", inexact, configurations);
    test::Check(inexactInner == 0 && outside == 0, "and a clip inside it keeps the edges it shares: %d differ, %d reach outside", inexactInner, outside);
}

TEST_CASE(CanvasItemId, "canvas_widget.canvas_is_an_item_of_its_id", "CNV-11", test::TestFlags_None)
{
    test::Host host;
    host.NewFrame();
    BeginHostWindow();
    const auto id = ImGui::GetID("canvas");
    ImGuiEx::Canvas canvas;
    canvas.Begin("canvas", ImVec2(300.0f, 200.0f));
    canvas.End();
    const auto itemId   = ImGui::GetItemID();
    const auto itemRect = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    ImGui::End();
    host.Render();

    test::Check(itemId == id, "after End(), the last item is the canvas: ID %08X, the canvas' %08X", itemId, id);
    test::Check(itemRect.Min.x == canvas.Rect().Min.x && itemRect.Max.y == canvas.Rect().Max.y, "with its rectangle");
}

# if IMGUI_VERSION_NUM >= 19200
namespace {

// Density the current font is rasterized at.
float CurrentTextDensity()
{
    return ImGui::GetFontBaked()->RasterizerDensity;
}

// Density of text inside a canvas at 'viewScale', with a framebuffer at 'framebufferScale'.
float TextDensityInCanvas(float framebufferScale, float viewScale, bool rasterizeForScale = true)
{
    test::Host host;
    ImGui::GetIO().DisplayFramebufferScale = ImVec2(framebufferScale, framebufferScale);
    host.NewFrame();
    BeginHostWindow();
    ImGuiEx::Canvas canvas;
    canvas.SetRasterizeTextForScale(rasterizeForScale);
    canvas.SetView(ImVec2(0.0f, 0.0f), viewScale);
    canvas.Begin("canvas", ImVec2(300.0f, 300.0f));
    ImGui::TextUnformatted("Text");
    const float density = CurrentTextDensity();
    canvas.End();
    ImGui::End();
    host.Render();
    return density;
}

} // namespace
# endif

TEST_CASE(TextDensity, "canvas_widget.text_is_rasterized_for_the_view_scale", nullptr, test::TestFlags_None)
{
# if IMGUI_VERSION_NUM >= 19200
    // A framebuffer at twice the display's density, and a canvas at scale 3, which a
    // quarter octave step takes to 2^1.5.
    test::Host host;
    ImGui::GetIO().DisplayFramebufferScale = ImVec2(2.0f, 2.0f);
    host.NewFrame();
    BeginHostWindow();
    const float before = CurrentTextDensity();
    ImGuiEx::Canvas canvas;
    canvas.SetView(ImVec2(0.0f, 0.0f), 3.0f);
    canvas.Begin("canvas", ImVec2(300.0f, 300.0f));
    ImGui::TextUnformatted("Text");
    const float inside = CurrentTextDensity();
    canvas.Suspend();
    const float suspended = CurrentTextDensity();
    canvas.Resume();
    const float resumed = CurrentTextDensity();
    canvas.End();
    const float after = CurrentTextDensity();
    ImGui::End();
    host.Render();

    const float expected = 2.0f * powf(2.0f, 1.5f);
    test::Check(before == 2.0f, "text before the canvas is rasterized at the framebuffer's density: %g", before);
    test::Check(inside == expected && resumed == expected, "text inside it at %g: %g, and %g after Resume()", expected, inside, resumed);
    test::Check(suspended == 2.0f && after == 2.0f, "suspended, and after the canvas, at 2 again: %g and %g", suspended, after);
# else
    test::Skip("needs Dear ImGui 1.92, which rasterizes glyphs on demand");
# endif
}

TEST_CASE(TextDensitySteps, "canvas_widget.text_density_takes_quarter_octave_steps_within_bounds", nullptr, test::TestFlags_None)
{
# if IMGUI_VERSION_NUM >= 19200
    struct Case { float FramebufferScale, ViewScale, Expected; };
    const Case cases[] =
    {
        { 1.0f,  1.0f, 1.0f               }, // as without a canvas
        { 1.0f,  1.5f, powf(2.0f, 0.5f)   }, // the nearest quarter octave
        { 1.0f,  0.3f, powf(2.0f, -1.75f) }, // under half too
        { 1.0f,  0.1f, 0.25f              }, // no less than a quarter
        { 1.0f, 20.0f, 8.0f               }, // no more than 8 times the font's size
        { 2.0f,  8.0f, 8.0f               }, // whatever the framebuffer's density
        { 2.0f,  0.1f, 0.5f               }, // a quarter of it
    };
    for (auto& c : cases)
    {
        const float density = TextDensityInCanvas(c.FramebufferScale, c.ViewScale);
        test::Check(density == c.Expected, "framebuffer at %g, view at %g: text at %g, %g expected", c.FramebufferScale, c.ViewScale, density, c.Expected);
    }
# else
    test::Skip("needs Dear ImGui 1.92, which rasterizes glyphs on demand");
# endif
}

TEST_CASE(TextDensityOff, "canvas_widget.text_density_stays_when_asked", nullptr, test::TestFlags_None)
{
# if IMGUI_VERSION_NUM >= 19200
    const float density = TextDensityInCanvas(1.0f, 3.0f, false);
    test::Check(density == 1.0f, "with SetRasterizeTextForScale(false), text at scale 3 is rasterized at 1: %g", density);
# else
    test::Skip("needs Dear ImGui 1.92, which rasterizes glyphs on demand");
# endif
}

TEST_CASE(TextDensityStaticAtlas, "canvas_widget.text_density_stays_with_a_prebuilt_atlas", nullptr, test::TestFlags_None)
{
# if IMGUI_VERSION_NUM >= 19200 && !defined(IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
    // A renderer without ImGuiBackendFlags_RendererHasTextures uses an atlas built up
    // front, which can't take glyphs of another density.
    test::Host host;
    auto& io = ImGui::GetIO();
    io.BackendFlags &= ~ImGuiBackendFlags_RendererHasTextures;
    unsigned char* pixels = nullptr;
    int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    io.Fonts->SetTexID((ImTextureID)(intptr_t)1);
    host.NewFrame();
    BeginHostWindow();
    ImGuiEx::Canvas canvas;
    canvas.SetView(ImVec2(0.0f, 0.0f), 3.0f);
    canvas.Begin("canvas", ImVec2(300.0f, 300.0f));
    ImGui::TextUnformatted("Text");
    const float density = CurrentTextDensity();
    canvas.End();
    ImGui::End();
    host.Render();

    test::Check(density == 1.0f, "text at scale 3 is rasterized at 1: %g", density);
# elif IMGUI_VERSION_NUM >= 19200
    test::Skip("a renderer without dynamic textures needs functions this Dear ImGui build leaves out");
# else
    test::Skip("needs Dear ImGui 1.92, which rasterizes glyphs on demand");
# endif
}
