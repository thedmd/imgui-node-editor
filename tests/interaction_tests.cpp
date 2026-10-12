//------------------------------------------------------------------------------
// Mouse and keyboard interaction.
//------------------------------------------------------------------------------
# include "harness.h"
# include <cmath>
# include <cstring>
# include <vector>

TEST_CASE(ShortcutsWhileTyping, "interaction.shortcuts_ignored_while_typing", "INT-2", test::TestFlags_None)
{
    test::Storage storage;
    test::Session session(&storage);
    session.HostContext.Clipboard = "abc";

    char   text[64]      = "";
    bool   textActive    = false;
    ImVec2 textOnScreen;
    int    actions       = 0;
    session.DefaultContent = [&](int frame)
    {
        if (frame == 0)
            ed::SetNodePosition(1, ImVec2(100.0f, 100.0f));
        if (frame == 1)
            ed::SelectNode(1);
        ed::BeginNode(1);
        ImGui::Text("Node");
        ImGui::SetNextItemWidth(150.0f);
        ImGui::InputText("##text", text, sizeof(text));
        textActive   = ImGui::IsItemActive();
        textOnScreen = ed::CanvasToScreen((ImGui::GetItemRectMin() + ImGui::GetItemRectMax()) * 0.5f);
        ed::EndNode();

        if (ed::BeginShortcut())
        {
            if (textActive && (ed::AcceptCut() || ed::AcceptCopy() || ed::AcceptPaste() || ed::AcceptDuplicate() || ed::AcceptCreateNode()))
                ++actions;
            ed::EndShortcut();
        }
    };

    session.Run(3);
    session.Click(textOnScreen);
    const bool selected = session.Use([] { return ed::IsNodeSelected(1); });
    test::Check(textActive && selected, "text field inside the selected node is active: %d, node selected: %d", textActive, selected);

    session.PressKey(ImGuiKey_V, true);
    ImGui::GetIO().AddInputCharacter(' ');
    session.PressKey(ImGuiKey_Space);
    session.PressKey(ImGuiKey_D, true);
    test::Check(strcmp(text, "abc ") == 0, "text field received the paste and the space: \"%s\"", text);

    session.PressKey(ImGuiKey_C, true);
    session.PressKey(ImGuiKey_X, true);
    test::Check(session.HostContext.Clipboard == "abc " && text[0] == 0, "text field copied and cut its text: \"%s\", left \"%s\"", session.HostContext.Clipboard.c_str(), text);

    test::Check(actions == 0, "Ctrl+X/C/V/D and Space typed into the text field reach no editor shortcut: %d did", actions);
}

TEST_CASE(GroupResizeFromPressPosition, "interaction.group_resize_uses_press_position", "INT-3", test::TestFlags_None)
{
    test::Storage storage;
    test::Session session(&storage);

    ImVec2 position, size, screenMin, screenMax;
    session.DefaultContent = [&](int frame)
    {
        if (frame == 0)
            ed::SetNodePosition(1, ImVec2(100.0f, 100.0f));
        ed::BeginNode(1);
        ImGui::Text("Group");
        ed::Group(ImVec2(200.0f, 150.0f));
        ed::EndNode();
        position  = ed::GetNodePosition(1);
        size      = ed::GetNodeSize(1);
        screenMin = ed::CanvasToScreen(position);
        screenMax = ed::CanvasToScreen(position + size);
    };

    session.Run(3);
    const ImVec2 positionBefore = position;
    const float  widthBefore    = size.x;

    // Press 2 px inside the right edge, then move fast: the first frame past the
    // drag threshold already lies outside the edge band.
    const ImVec2 press(screenMax.x - 2.0f, (screenMin.y + screenMax.y) * 0.5f);
    test::MouseMove(press);
    session.Run(2);
    test::MouseButton(0, true);
    session.Run(1);
    for (float dx : { 12.0f, 20.0f, 30.0f, 40.0f })
    {
        test::MouseMove(press + ImVec2(dx, 0.0f));
        session.Run(1);
    }
    test::MouseButton(0, false);
    session.Run(3);

    test::Check(size.x >= widthBefore + 30.0f, "group widened by the drag: %g -> %g", widthBefore, size.x);
    test::Check(position.x == positionBefore.x && position.y == positionBefore.y, "group not moved: (%g, %g) -> (%g, %g)", positionBefore.x, positionBefore.y, position.x, position.y);
}

namespace {

struct ResizeResult
{
    ImVec2 PositionBefore, PositionAfter;
    float  WidthBefore = 0.0f, WidthAfter = 0.0f;
};

// Presses 'button' 2 px inside the right edge of a 200 x 150 group, moves it by 'dx'
// in the first dragging frame, and releases it. Alt keeps the edge off the grid.
ResizeResult ResizeRightEdge(const ed::Config& config, int button, float dx)
{
    test::Session session(config);
    ImVec2 position, size, screenMin, screenMax;
    session.DefaultContent = [&](int frame)
    {
        if (frame == 0)
            ed::SetNodePosition(1, ImVec2(100.0f, 100.0f));
        ed::BeginNode(1);
        ImGui::Text("Group");
        ed::Group(ImVec2(200.0f, 150.0f));
        ed::EndNode();
        position  = ed::GetNodePosition(1);
        size      = ed::GetNodeSize(1);
        screenMin = ed::CanvasToScreen(position);
        screenMax = ed::CanvasToScreen(position + size);
    };
    session.Run(3);

    ResizeResult result;
    result.PositionBefore = position;
    result.WidthBefore    = size.x;

    const ImVec2 press(screenMax.x - 2.0f, (screenMin.y + screenMax.y) * 0.5f);
    ImGui::GetIO().AddKeyEvent(ImGuiMod_Alt, true);
    test::MouseMove(press);
    session.Run(2);
    test::MouseButton(button, true);
    session.Run(1);
    test::MouseMove(press + ImVec2(dx, 0.0f));
    session.Run(3);
    test::MouseButton(button, false);
    session.Run(2);
    ImGui::GetIO().AddKeyEvent(ImGuiMod_Alt, false);
    session.Run(1);

    result.PositionAfter = position;
    result.WidthAfter    = size.x;
    return result;
}

void CheckResized(const ResizeResult& result, float dx, const char* what)
{
    test::Check(result.WidthAfter == result.WidthBefore + dx && result.PositionAfter.x == result.PositionBefore.x && result.PositionAfter.y == result.PositionBefore.y,
        "%s by %g px resizes: width %g -> %g, position (%g, %g) -> (%g, %g)", what, dx, result.WidthBefore, result.WidthAfter,
        result.PositionBefore.x, result.PositionBefore.y, result.PositionAfter.x, result.PositionAfter.y);
}

} // namespace

TEST_CASE(GroupResizeFastFirstMove, "interaction.group_resize_after_fast_first_move", "INT-3", test::TestFlags_None)
{
    for (float dx : { 3.0f, 14.0f, -18.0f })
    {
        test::Storage storage;
        CheckResized(ResizeRightEdge(test::MakeConfig(&storage), 0, dx), dx, "a first move from the right edge");
    }
}

TEST_CASE(GroupResizeDragButton, "interaction.group_resize_follows_drag_button", "INT-4", test::TestFlags_None)
{
    test::Storage storage;
    auto config = test::MakeConfig(&storage);
    config.DragButtonIndex        = 1;
    config.NavigateButtonIndex    = 2;
    config.ContextMenuButtonIndex = 2;
    CheckResized(ResizeRightEdge(config, 1, 40.0f), 40.0f, "a right-button drag of the right edge");
}

namespace {

ed::Config ZoomConfig(bool smooth, std::initializer_list<float> levels = {})
{
    ed::Config config;
    config.SettingsName     = nullptr;
    config.EnableSmoothZoom = smooth;
    config.SmoothZoomPower  = 1.3f;
    for (auto level : levels)
        config.CustomZoomLevels.push_back(level);
    return config;
}

const char* ZoomMode(bool smooth)
{
    return smooth ? "smooth" : "stepped";
}

void SmallNode(int)
{
    ed::BeginNode(1);
    ImGui::TextUnformatted("Node");
    ed::EndNode();
}

// The editor's zoom, 1 at 100%.
float Zoom(test::Session& session)
{
    return 1.0f / session.Use([] { return ed::GetCurrentZoom(); });
}

// One wheel event of 'delta' over the editor, and time to settle.
void Wheel(test::Session& session, float delta)
{
    test::MouseWheel(delta);
    session.Run(30);
}

} // namespace

TEST_CASE(FractionalWheelZooms, "interaction.fractional_wheel_zooms", "INT-8", test::TestFlags_None)
{
    // Precision touchpads and macOS trackpads send fractional wheel deltas.
    for (bool smooth : { false, true })
    {
        test::Session session(ZoomConfig(smooth));
        session.DefaultContent = SmallNode;
        test::MouseMove(ImVec2(500.0f, 300.0f));
        session.Run(10);
        const float before = Zoom(session);
        for (int i = 0; i < 20; ++i)
        {
            test::MouseWheel(0.25f);
            session.Run(1);
        }
        session.Run(40);
        const float after = Zoom(session);

        test::Check(after > before * 1.05f, "%s: 20 wheel deltas of +0.25 zoom in: %g -> %g", ZoomMode(smooth), before, after);
    }
}

TEST_CASE(WheelFromOutsideZoomLevels, "interaction.wheel_moves_zoom_toward_levels", "INT-7", test::TestFlags_None)
{
    for (bool smooth : { false, true })
    {
        // One small node, fitted to the view, is zoomed in far past the largest level.
        {
            test::Session session(ZoomConfig(smooth));
            session.DefaultContent = SmallNode;
            test::MouseMove(ImVec2(500.0f, 300.0f));
            session.Run(3);
            session.Use([] { ed::NavigateToContent(0.0f); });
            session.Run(3);
            const float fitted = Zoom(session);
            Wheel(session, 1.0f);
            const float in = Zoom(session);
            Wheel(session, -1.0f);
            const float out = Zoom(session);

            test::Check(fitted > 8.0f, "%s: a fitted small node is zoomed past the levels: %g", ZoomMode(smooth), fitted);
            test::Check(in >= fitted && out < in, "%s: from there, the wheel zooms in its own direction: %g, in %g, out %g", ZoomMode(smooth), fitted, in, out);
        }

        // Content 20000 units across, fitted to the view, is below the smallest level.
        {
            test::Session session(ZoomConfig(smooth));
            session.DefaultContent = [](int frame)
            {
                if (frame == 0)
                    ed::SetNodePosition(2, ImVec2(20000.0f, 20000.0f));
                SmallNode(frame);
                ed::BeginNode(2);
                ImGui::TextUnformatted("Node");
                ed::EndNode();
            };
            test::MouseMove(ImVec2(500.0f, 300.0f));
            session.Run(3);
            session.Use([] { ed::NavigateToContent(0.0f); });
            session.Run(3);
            const float fitted = Zoom(session);
            Wheel(session, -1.0f);
            const float out = Zoom(session);
            Wheel(session, 1.0f);
            const float in = Zoom(session);

            test::Check(fitted < 0.1f, "%s: fitted large content is zoomed out past the levels: %g", ZoomMode(smooth), fitted);
            test::Check(out <= fitted && in > out, "%s: from there, the wheel zooms in its own direction: %g, out %g, in %g", ZoomMode(smooth), fitted, out, in);
        }
    }
}

TEST_CASE(ZoomLevelTables, "interaction.zoom_levels_step_in_order", "INT-13", test::TestFlags_None)
{
    struct Case
    {
        const char*         Name;
        ed::Config          Config;
        float               Start;
        std::vector<float>  Expected; // after a wheel in, then three wheel outs
    };
    const Case cases[] =
    {
        { "duplicate levels {0.5, 1, 1, 2}", ZoomConfig(false, { 0.5f, 1.0f, 1.0f, 2.0f }), 1.0f, { 2.0f, 1.0f, 0.5f, 0.5f } },
        { "descending levels {2, 1, 0.5}",   ZoomConfig(false, { 2.0f, 1.0f, 0.5f }),       1.0f, { 2.0f, 1.0f, 0.5f, 0.5f } },
        { "default levels from 0.9",         ZoomConfig(false),                              0.9f, { 1.0f, 0.75f, 0.5f, 0.33f } },
    };

    for (auto& testCase : cases)
    {
        test::Session session(testCase.Config);
        char settings[128];
        snprintf(settings, sizeof(settings), R"({"view":{"scroll":{"x":0,"y":0},"zoom":%g}})", testCase.Start);
        session.Use([&] { ed::LoadSettingsFromMemory(settings); });
        session.DefaultContent = SmallNode;
        test::MouseMove(ImVec2(500.0f, 300.0f));
        session.Run(3);

        std::vector<float> zooms;
        for (float delta : { 1.0f, -1.0f, -1.0f, -1.0f })
        {
            Wheel(session, delta);
            zooms.push_back(Zoom(session));
        }

        bool matches = true;
        for (size_t i = 0; i < zooms.size(); ++i)
            matches = matches && ImFabs(zooms[i] - testCase.Expected[i]) < 0.001f;
        test::Check(matches, "%s: wheel in, then out three times: %g, %g, %g, %g", testCase.Name, zooms[0], zooms[1], zooms[2], zooms[3]);
    }
}

TEST_CASE(InvalidSmoothZoomPower, "interaction.invalid_smooth_zoom_power_asserts", "INT-13", test::TestFlags_None)
{
    test::ExpectAsserts(1);
    auto config = ZoomConfig(true);
    config.SmoothZoomPower = 1.0f;
    test::Session session(config);
    session.DefaultContent = SmallNode;
    test::MouseMove(ImVec2(500.0f, 300.0f));
    session.Run(3);
    Wheel(session, 1.0f);

    test::Check(Zoom(session) > 1.0f, "the wheel still zooms, with the default power: %g", Zoom(session));
}

namespace {

// Node 1 at (100, 100), with the shortcuts the editor accepts counted.
struct ShortcutScene
{
    int    Copies     = 0;
    int    Duplicates = 0;
    ImVec2 NodeOnScreen;

    void Submit(int frame)
    {
        if (frame == 0)
            ed::SetNodePosition(1, ImVec2(100.0f, 100.0f));
        ed::BeginNode(1);
        ImGui::Text("Node");
        ed::EndNode();
        NodeOnScreen = ed::CanvasToScreen(ed::GetNodePosition(1) + ed::GetNodeSize(1) * 0.5f);

        if (ed::BeginShortcut())
        {
            if (ed::AcceptCopy())
                ++Copies;
            if (ed::AcceptDuplicate())
                ++Duplicates;
            ed::EndShortcut();
        }
    }
};

// Where the canvas origin is on screen, and the zoom.
ImVec4 View(test::Session& session)
{
    return session.Use([]
    {
        const auto origin = ed::CanvasToScreen(ImVec2(0.0f, 0.0f));
        return ImVec4(origin.x, origin.y, ed::GetCurrentZoom(), 0.0f);
    });
}

bool SameView(const ImVec4& lhs, const ImVec4& rhs)
{
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
}

} // namespace

TEST_CASE(HeldShortcutsTriggerOnce, "interaction.held_shortcuts_trigger_once", "INT-14", test::TestFlags_None)
{
    test::Storage storage;
    test::Session session(&storage);
    ShortcutScene scene;
    session.DefaultContent = [&](int frame) { scene.Submit(frame); };
    session.Run(3);
    session.Click(scene.NodeOnScreen);

    test::Ctrl(true);
    session.Run(1);
    test::Key(ImGuiKey_D, true);
    session.Run(60);
    test::Key(ImGuiKey_D, false);
    test::Ctrl(false);
    session.Run(2);
    test::Check(scene.Duplicates == 1, "Ctrl+D held for one second duplicates once: %d times", scene.Duplicates);

    // The first press centers the selected node; repeats would alternate with the content.
    const auto before = View(session);
    test::Key(ImGuiKey_F, true);
    session.Run(30);
    const auto settled = View(session);
    int changes = 0;
    for (int i = 0; i < 30; ++i)
    {
        session.Frame();
        if (!SameView(View(session), settled))
            ++changes;
    }
    test::Key(ImGuiKey_F, false);
    session.Run(2);
    test::Check(!SameView(settled, before), "F navigated: origin (%g, %g) -> (%g, %g)", before.x, before.y, settled.x, settled.y);
    test::Check(changes == 0, "F held for one second navigates once: the view changed in %d of the last 30 frames", changes);
}

TEST_CASE(CtrlFDoesntNavigate, "interaction.ctrl_f_doesnt_navigate", "INT-18", test::TestFlags_None)
{
    test::Storage storage;
    test::Session session(&storage);
    ShortcutScene scene;
    session.DefaultContent = [&](int frame) { scene.Submit(frame); };
    session.Run(3);
    session.Click(scene.NodeOnScreen);

    const auto before = View(session);
    session.PressKey(ImGuiKey_F, true);
    session.Run(30);
    const auto after = View(session);
    test::Check(SameView(after, before), "Ctrl+F leaves the view: origin (%g, %g) -> (%g, %g)", before.x, before.y, after.x, after.y);
}

TEST_CASE(ShortcutsInFocusedEditorOnly, "interaction.shortcuts_act_in_focused_editor_only", "INT-2", test::TestFlags_None)
{
    test::Storage storage;
    test::Session session(&storage);
    session.HostSize = ImVec2(700.0f, 640.0f);

    ed::Config config;
    config.SettingsName = nullptr;
    auto other = ed::CreateEditor(&config);

    ShortcutScene scenes[2];
    session.DefaultContent = [&](int frame) { scenes[0].Submit(frame); };
    session.AfterHost = [&]
    {
        ImGui::SetNextWindowPos(ImVec2(800.0f, 40.0f));
        ImGui::SetNextWindowSize(ImVec2(400.0f, 400.0f));
        ImGui::Begin("Other", nullptr, session.HostFlags);
        ed::SetCurrentEditor(other);
        ed::Begin("Other Editor");
        scenes[1].Submit(session.FrameIndex);
        ed::End();
        ed::SetCurrentEditor(nullptr);
        ImGui::End();
    };
    session.Run(3);

    session.Click(scenes[1].NodeOnScreen);
    session.PressKey(ImGuiKey_C, true);
    const int copiesInOther[2] = { scenes[0].Copies, scenes[1].Copies };
    session.Click(scenes[0].NodeOnScreen);
    session.PressKey(ImGuiKey_C, true);

    session.AfterHost = nullptr;
    ed::DestroyEditor(other);

    test::Check(copiesInOther[0] == 0 && copiesInOther[1] == 1, "Ctrl+C with the other window focused copies only there: %d, %d", copiesInOther[0], copiesInOther[1]);
    test::Check(scenes[0].Copies == 1 && scenes[1].Copies == 1, "and then only in the host window: %d, %d", scenes[0].Copies, scenes[1].Copies);
}

TEST_CASE(MacCommandShortcuts, "interaction.macos_command_shortcuts", "INT-19", test::TestFlags_None)
{
# if IMGUI_VERSION_NUM < 18920
    test::Skip("before 1.89.2 ImGui picks the macOS shortcut modifier at compile time");
# else
    test::Storage storage;
    test::Session session(&storage);
    session.HostContext.Context->IO.ConfigMacOSXBehaviors = true;
    ShortcutScene scene;
    session.DefaultContent = [&](int frame) { scene.Submit(frame); };
    session.Run(3);
    session.Click(scene.NodeOnScreen);

    ImGui::GetIO().AddKeyEvent(ImGuiMod_Super, true);
    session.Run(1);
    session.PressKey(ImGuiKey_C);
    ImGui::GetIO().AddKeyEvent(ImGuiMod_Super, false);
    session.Run(2);

    test::Check(scene.Copies == 1, "Cmd+C copies with io.ConfigMacOSXBehaviors: %d copies", scene.Copies);
# endif
}

TEST_CASE(DragAnchorSurvivesZoom, "interaction.dragged_node_stays_under_mouse_when_zooming", "INT-5", test::TestFlags_None)
{
    test::Storage storage;
    test::Session session(&storage);
    ShortcutScene scene;
    session.DefaultContent = [&](int frame) { scene.Submit(frame); };
    session.Run(3);

    // Where on the node the mouse holds it, in canvas units.
    auto grabOffset = [&]
    {
        return session.Use([] { return ed::ScreenToCanvas(ImGui::GetIO().MousePos) - ed::GetNodePosition(1); });
    };

    // Alt keeps the node off the grid.
    const ImVec2 press = scene.NodeOnScreen;
    ImGui::GetIO().AddKeyEvent(ImGuiMod_Alt, true);
    test::MouseMove(press);
    session.Run(2);
    test::MouseButton(0, true);
    session.Run(1);
    for (int i = 1; i <= 10; ++i)
    {
        test::MouseMove(press + ImVec2(30.0f * i, 0.0f));
        session.Run(1);
    }
    session.Run(2);
    const auto before = grabOffset();

    test::MouseWheel(1.0f);
    session.Run(30);
    const auto after = grabOffset();
    const float zoom = 1.0f / session.Use([] { return ed::GetCurrentZoom(); });

    test::MouseButton(0, false);
    session.Run(2);
    ImGui::GetIO().AddKeyEvent(ImGuiMod_Alt, false);
    session.Run(1);

    test::Check(zoom > 1.0f, "zoomed in during the drag: zoom %g", zoom);
    test::Check(ImFabs(after.x - before.x) < 0.5f && ImFabs(after.y - before.y) < 0.5f, "the node stays where the mouse holds it: offset (%g, %g) -> (%g, %g)", before.x, before.y, after.x, after.y);
}

namespace {

// Node 1, and the canvas point under 'Mouse' in each frame's view, measured
// between ed::Begin() and ed::End(), where the editor reads the mouse.
struct ViewProbe
{
    ShortcutScene Scene;
    ImVec2        Mouse = ImVec2(500.0f, 300.0f);
    ImVec2        UnderMouse;

    void Submit(int frame)
    {
        Scene.Submit(frame);
        UnderMouse = ed::ScreenToCanvas(Mouse);
    }
};

float Distance(const ImVec2& a, const ImVec2& b)
{
    return ImSqrt(ImLengthSqr(a - b));
}

} // namespace

TEST_CASE(PanStopsZoomAnimation, "interaction.pan_after_wheel_doesnt_drift", "INT-6", test::TestFlags_None)
{
    test::Storage storage;
    test::Session session(&storage);
    ViewProbe probe;
    session.DefaultContent = [&](int frame) { probe.Submit(frame); };
    test::MouseMove(probe.Mouse);
    session.Run(5);

    // A right-drag starts while the zoom animation of a wheel notch is playing.
    test::MouseWheel(1.0f);
    session.Run(1);
    test::MouseButton(1, true);
    session.Run(1);
    probe.Mouse = probe.Mouse + ImVec2(40.0f, 0.0f);
    test::MouseMove(probe.Mouse);
    session.Run(2);

    // The mouse holds still: so does the view.
    const auto held = probe.UnderMouse;
    float drift = 0.0f;
    for (int i = 0; i < 15; ++i)
    {
        session.Run(1);
        drift = ImMax(drift, Distance(probe.UnderMouse, held));
    }
    test::MouseButton(1, false);
    session.Run(2);

    test::Check(drift < 0.5f, "the point under a still mouse doesn't move once the pan started: %g canvas units", drift);
}

TEST_CASE(ZoomAnchorDuringAnimation, "interaction.second_wheel_notch_zooms_at_the_mouse", "INT-12", test::TestFlags_None)
{
    test::Storage storage;
    test::Session session(&storage);
    ViewProbe probe;
    session.DefaultContent = [&](int frame) { probe.Submit(frame); };
    probe.Mouse = ImVec2(400.0f, 300.0f);
    test::MouseMove(probe.Mouse);
    session.Run(5);

    test::MouseWheel(1.0f);
    session.Run(2);
    probe.Mouse = ImVec2(600.0f, 350.0f);
    test::MouseMove(probe.Mouse);
    session.Run(1);

    // ImGui handles a wheel event queued after a mouse move one frame later, so
    // this notch comes 3 frames after the first one, with the animation running.
    test::MouseWheel(1.0f);
    session.Run(1);
    const auto anchor = probe.UnderMouse;
    session.Run(30);

    const float error = Distance(probe.UnderMouse, anchor);
    test::Check(error < 0.5f, "the second notch zooms around the point under the mouse: off by %g canvas units", error);
}

namespace {

// Node 2 holds pin 'pinId' in front of 'pinContent'; node 1, submitted after it and
// drawn on top, is dragged by its title. Reports whether node 1 moved and a link was offered.
struct DragOverPinResult
{
    bool Moved   = false;
    bool Offered = false;
};

DragOverPinResult DragNodeAbovePin(uintptr_t pinId, bool emptyPin)
{
    test::Storage storage;
    test::Session session(&storage);
    ImVec2 titleOnScreen;
    bool   offered = false;
    session.DefaultContent = [&](int frame)
    {
        if (frame == 0)
        {
            ed::SetNodePosition(2, ImVec2(400.0f, 100.0f));
            ed::SetNodePosition(1, ImVec2(100.0f, 100.0f));
        }
        ed::BeginNode(2);
        ImGui::TextUnformatted("Node 2");
        ed::BeginPin(pinId, ed::PinKind::Input);
        if (!emptyPin)
            ImGui::TextUnformatted("In");
        ed::EndPin();
        ed::EndNode();

        ed::BeginNode(1);
        ImGui::TextUnformatted("Node 1");
        titleOnScreen = ed::CanvasToScreen((ImGui::GetItemRectMin() + ImGui::GetItemRectMax()) * 0.5f);
        ed::EndNode();

        if (ed::BeginCreate())
        {
            ed::PinId start, end;
            if (ed::QueryNewLink(&start, &end) || ed::QueryNewNode(&start))
                offered = true;
            ed::EndCreate();
        }
    };
    session.Run(3);

    const auto before = session.Use([] { return ed::GetNodePosition(1); });
    session.Drag(titleOnScreen, titleOnScreen + ImVec2(64.0f, 32.0f));
    const auto after = session.Use([] { return ed::GetNodePosition(1); });

    DragOverPinResult result;
    result.Moved   = after.x != before.x || after.y != before.y;
    result.Offered = offered;
    return result;
}

} // namespace

TEST_CASE(NodeAndPinWithSameId, "interaction.node_and_pin_with_same_id", "CORE-3", test::TestFlags_None)
{
    const auto result = DragNodeAbovePin(1, false);
    test::Check(result.Moved && !result.Offered, "dragging node 1 above pin 1 of node 2 moves the node (%d), without a link from the pin (%d)", result.Moved, result.Offered);
}

TEST_CASE(EmptyPinDoesntClick, "interaction.empty_pin_isnt_clicked", "CORE-4", test::TestFlags_None)
{
    const auto result = DragNodeAbovePin(10, true);
    test::Check(result.Moved && !result.Offered, "dragging a node above an empty pin moves the node (%d), without a link from the pin (%d)", result.Moved, result.Offered);
}

TEST_CASE(EmptyNodeDoesntClick, "interaction.empty_node_isnt_clicked", "CORE-4", test::TestFlags_None)
{
    test::Storage storage;
    test::Session session(&storage);
    session.DefaultContent = [&](int frame)
    {
        if (frame == 0)
            ed::SetNodePosition(3, ImVec2(100.0f, 100.0f));
        ed::PushStyleVar(ed::StyleVar_NodePadding, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ed::BeginNode(3);
        ed::EndNode();
        ed::PopStyleVar();
    };
    test::MouseMove(ImVec2(900.0f, 600.0f));
    session.Run(10);

    const bool selected = session.Use([] { return ed::IsNodeSelected(3); });
    test::Check(!selected, "without any click, an empty node isn't selected");
}

TEST_CASE(BackgroundDoubleClickButtons, "interaction.background_double_click_reports_button_index", "CORE-6", test::TestFlags_None)
{
    test::Storage storage;
    auto config = test::MakeConfig(&storage);
    config.SelectButtonIndex   = 1;
    config.NavigateButtonIndex = 2;
    test::Session session(config);
    session.DefaultContent = SmallNode;
    test::MouseMove(ImVec2(900.0f, 600.0f));
    session.Run(3);

    int reported[3] = { -2, -2, -2 };
    for (int button = 0; button < 3; ++button)
    {
        for (int click = 0; click < 2; ++click)
        {
            test::MouseButton(button, true);
            session.Run(1);
            if (click == 1)
                reported[button] = session.Use([] { return ed::IsBackgroundDoubleClicked() ? (int)ed::GetBackgroundDoubleClickButtonIndex() : -1; });
            test::MouseButton(button, false);
            session.Run(1);
        }
        session.Run(30);
    }

    test::Check(reported[0] == 0 && reported[1] == 1 && reported[2] == 2, "left, right and middle double-clicks report %d, %d and %d", reported[0], reported[1], reported[2]);
}

TEST_CASE(MiddleClickDoesntSelect, "interaction.only_the_select_button_selects", "INT-16", test::TestFlags_None)
{
    test::Storage storage;
    test::Session session(&storage);
    ImVec2 nodeOnScreen[2];
    session.DefaultContent = [&](int frame)
    {
        for (int i = 0; i < 2; ++i)
        {
            const auto id = ed::NodeId(i + 1);
            if (frame == 0)
                ed::SetNodePosition(id, ImVec2(100.0f + 200.0f * i, 100.0f));
            ed::BeginNode(id);
            ImGui::TextUnformatted("Node");
            ed::EndNode();
            nodeOnScreen[i] = ed::CanvasToScreen(ed::GetNodePosition(id) + ed::GetNodeSize(id) * 0.5f);
        }
    };
    session.Run(3);

    session.Click(nodeOnScreen[0]);
    session.Click(nodeOnScreen[1], 2);
    const bool first  = session.Use([] { return ed::IsNodeSelected(1); });
    const bool second = session.Use([] { return ed::IsNodeSelected(2); });

    test::Check(first && !second, "a middle-click on node 2 keeps node 1 selected: %d, %d", first, second);
}

TEST_CASE(ActionContextLists, "interaction.action_context_lists_match_its_size", "INT-10", test::TestFlags_None)
{
    // Group 3 holds node 1, linked to node 2 by link 100.
    struct Context { int Size = -1, Nodes = -1, Links = -1; };
    Context copied;
    ImVec2  nodeOnScreen;
    test::Storage storage;
    test::Session session(&storage);
    session.DefaultContent = [&](int frame)
    {
        if (frame == 0)
        {
            ed::SetNodePosition(3, ImVec2(50.0f, 50.0f));
            ed::SetNodePosition(1, ImVec2(100.0f, 120.0f));
            ed::SetNodePosition(2, ImVec2(300.0f, 120.0f));
        }
        ed::BeginNode(3);
        ImGui::TextUnformatted("Group");
        ed::Group(ImVec2(450.0f, 200.0f));
        ed::EndNode();

        ed::BeginNode(1);
        ImGui::TextUnformatted("Node 1");
        ed::BeginPin(11, ed::PinKind::Output);
        ImGui::TextUnformatted("Out");
        ed::EndPin();
        ed::EndNode();
        nodeOnScreen = ed::CanvasToScreen(ed::GetNodePosition(1) + ImVec2(10.0f, 8.0f));

        ed::BeginNode(2);
        ImGui::TextUnformatted("Node 2");
        ed::BeginPin(21, ed::PinKind::Input);
        ImGui::TextUnformatted("In");
        ed::EndPin();
        ed::EndNode();

        ed::Link(100, 11, 21);

        if (ed::BeginShortcut())
        {
            if (ed::AcceptCopy())
                copied = { ed::GetActionContextSize(), ed::GetActionContextNodes(nullptr, 0), ed::GetActionContextLinks(nullptr, 0) };
            ed::EndShortcut();
        }
    };
    session.Run(3);
    session.Click(ImVec2(900.0f, 600.0f));

    auto copy = [&]
    {
        copied = Context();
        session.PressKey(ImGuiKey_C, true);
        return copied;
    };

    test::MouseMove(nodeOnScreen);
    session.Run(2);
    const auto hovered = copy();
    session.Use([] { ed::SelectNode(1); ed::SelectNode(2, true); });
    const auto selected = copy();
    session.Use([] { ed::SelectNode(3); });
    const auto group = copy();

    test::Check(hovered.Size == 1 && hovered.Nodes == 1 && hovered.Links == 0, "nothing selected, node 1 hovered: size %d, %d nodes, %d links", hovered.Size, hovered.Nodes, hovered.Links);
    test::Check(selected.Size == 3 && selected.Nodes == 2 && selected.Links == 1, "nodes 1 and 2 selected: size %d, %d nodes, %d links", selected.Size, selected.Nodes, selected.Links);
    test::Check(group.Size == 4 && group.Nodes == 3 && group.Links == 1, "group 3 selected: size %d, %d nodes, %d links", group.Size, group.Nodes, group.Links);
}

TEST_CASE(ContextMenuClickOnNothing, "interaction.context_menu_click_on_nothing_blocks_nothing", "INT-15", test::TestFlags_None)
{
    test::Storage storage;
    test::Session session(&storage);
    session.HostContext.Context->IO.ConfigInputTrickleEventQueue = false; // a release and a key press in one frame
    ShortcutScene scene;
    session.DefaultContent = [&](int frame) { scene.Submit(frame); };
    session.Run(3);
    session.Click(scene.NodeOnScreen);

    // Right-click outside the host window, which keeps its focus, and press Ctrl+C
    // in the frame of the release.
    test::MouseMove(ImVec2(1200.0f, 700.0f));
    session.Run(2);
    test::MouseButton(1, true);
    session.Run(2);
    test::Ctrl(true);
    session.Run(2);
    test::MouseButton(1, false);
    test::Key(ImGuiKey_C, true);
    session.Run(1);
    test::Key(ImGuiKey_C, false);
    test::Ctrl(false);
    session.Run(2);

    test::Check(scene.Copies == 1, "Ctrl+C in the frame a right-click on nothing ends copies: %d copies", scene.Copies);
}

TEST_CASE(FocusInnermostGroup, "interaction.f_centers_innermost_group_under_mouse", "INT-18", test::TestFlags_None)
{
    // Group 1 holds group 2. F over the inside of group 2 centers group 2.
    test::Storage storage;
    test::Session session(&storage);
    ImVec2 innerCenter, outerCenter, insideInner;
    session.DefaultContent = [&](int frame)
    {
        if (frame == 0)
        {
            ed::SetNodePosition(1, ImVec2(0.0f, 0.0f));
            ed::SetNodePosition(2, ImVec2(300.0f, 250.0f));
        }
        ed::BeginNode(1);
        ImGui::TextUnformatted("Outer");
        ed::Group(ImVec2(900.0f, 700.0f));
        ed::EndNode();
        ed::BeginNode(2);
        ImGui::TextUnformatted("Inner");
        ed::Group(ImVec2(200.0f, 150.0f));
        ed::EndNode();
        outerCenter = ed::GetNodePosition(1) + ed::GetNodeSize(1) * 0.5f;
        innerCenter = ed::GetNodePosition(2) + ed::GetNodeSize(2) * 0.5f;
        insideInner = ed::CanvasToScreen(innerCenter + ImVec2(0.0f, 20.0f));
    };
    session.Run(3);
    const auto screenCenter = session.Use([] { return ed::CanvasToScreen(ImVec2(0.0f, 0.0f)) + ed::GetScreenSize() * 0.5f; });

    session.Click(insideInner);
    session.PressKey(ImGuiKey_F);
    session.Run(30);

    const auto inner = session.Use([&] { return ed::CanvasToScreen(innerCenter); });
    const auto outer = session.Use([&] { return ed::CanvasToScreen(outerCenter); });
    test::Check(ImFabs(inner.x - screenCenter.x) < 1.0f && ImFabs(inner.y - screenCenter.y) < 1.0f,
        "F centers the inner group at (%g, %g): it is at (%g, %g), the outer group at (%g, %g)", screenCenter.x, screenCenter.y, inner.x, inner.y, outer.x, outer.y);
}

TEST_CASE(MacCommandClick, "interaction.macos_command_click_adds_to_selection", "INT-19", test::TestFlags_None)
{
    test::Storage storage;
    test::Session session(&storage);
    session.HostContext.Context->IO.ConfigMacOSXBehaviors = true;
    ImVec2 nodeOnScreen[2];
    session.DefaultContent = [&](int frame)
    {
        for (int i = 0; i < 2; ++i)
        {
            const auto id = ed::NodeId(i + 1);
            if (frame == 0)
                ed::SetNodePosition(id, ImVec2(100.0f + 200.0f * i, 100.0f));
            ed::BeginNode(id);
            ImGui::TextUnformatted("Node");
            ed::EndNode();
            nodeOnScreen[i] = ed::CanvasToScreen(ed::GetNodePosition(id) + ed::GetNodeSize(id) * 0.5f);
        }
    };
    session.Run(3);

    session.Click(nodeOnScreen[0]);
    ImGui::GetIO().AddKeyEvent(ImGuiMod_Super, true);
    session.Run(1);
    session.Click(nodeOnScreen[1]);
    ImGui::GetIO().AddKeyEvent(ImGuiMod_Super, false);
    session.Run(1);

    const bool first  = session.Use([] { return ed::IsNodeSelected(1); });
    const bool second = session.Use([] { return ed::IsNodeSelected(2); });
    test::Check(first && second, "Cmd+click on node 2 adds it to the selection: %d, %d", first, second);
}
