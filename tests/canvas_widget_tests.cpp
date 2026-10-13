//------------------------------------------------------------------------------
// The canvas widget on its own: view, suspension and the state it remaps.
//------------------------------------------------------------------------------
# include "harness.h"
# include "imgui_canvas.h"
# include <cfloat>

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
