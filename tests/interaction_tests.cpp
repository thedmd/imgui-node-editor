//------------------------------------------------------------------------------
// Mouse and keyboard interaction.
//------------------------------------------------------------------------------
# include "harness.h"
# include <cmath>
# include <cstring>

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

TEST_CASE(GroupResizeFromPressPosition, "interaction.group_resize_uses_press_position", "INT-3", test::TestFlags_KnownFailure)
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

TEST_CASE(FractionalWheelZooms, "interaction.fractional_wheel_zooms", "INT-8", test::TestFlags_KnownFailure)
{
    // Precision touchpads and macOS trackpads send fractional wheel deltas.
    test::Storage storage;
    auto config = test::MakeConfig(&storage);
    config.EnableSmoothZoom = false;
    test::Session session(config);

    float width = 0.0f;
    session.DefaultContent = [&](int)
    {
        ed::BeginNode(1);
        ImGui::TextUnformatted("Node");
        ImGui::Dummy(ImVec2(100.0f, 20.0f));
        ed::EndNode();
        width = ed::CanvasToScreen(ed::GetNodeSize(1)).x - ed::CanvasToScreen(ImVec2(0.0f, 0.0f)).x;
    };

    test::MouseMove(ImVec2(500.0f, 300.0f));
    session.Run(10);
    const float before = width;
    for (int i = 0; i < 20; ++i)
    {
        test::MouseWheel(0.25f);
        session.Run(1);
    }
    session.Run(40);

    test::Check(width > before * 1.05f, "20 wheel deltas of +0.25 zoom in: node width %g -> %g", before, width);
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
