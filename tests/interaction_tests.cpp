//------------------------------------------------------------------------------
// Mouse and keyboard interaction.
//------------------------------------------------------------------------------
# include "harness.h"
# include <cmath>
# include <cstring>

TEST_CASE(ShortcutsWhileTyping, "interaction.shortcuts_ignored_while_typing", "INT-2", test::TestFlags_KnownFailure)
{
    test::Storage storage;
    test::Session session(&storage);
    session.HostContext.Clipboard = "abc";

    char   text[64]      = "";
    bool   textActive    = false;
    ImVec2 textOnScreen;
    bool   paste         = false;
    bool   createNode    = false;
    session.DefaultContent = [&](int frame)
    {
        if (frame == 0)
            ed::SetNodePosition(1, ImVec2(100.0f, 100.0f));
        ed::BeginNode(1);
        ImGui::Text("Node");
        ImGui::SetNextItemWidth(150.0f);
        ImGui::InputText("##text", text, sizeof(text));
        textActive   = ImGui::IsItemActive();
        textOnScreen = ed::CanvasToScreen((ImGui::GetItemRectMin() + ImGui::GetItemRectMax()) * 0.5f);
        ed::EndNode();

        if (ed::BeginShortcut())
        {
            if (textActive && ed::AcceptPaste())
                paste = true;
            if (textActive && ed::AcceptCreateNode())
                createNode = true;
            ed::EndShortcut();
        }
    };

    session.Run(3);
    session.Click(textOnScreen);
    test::Check(textActive, "text field inside the node is active");

    session.PressKey(ImGuiKey_V, true);
    ImGui::GetIO().AddInputCharacter(' ');
    session.PressKey(ImGuiKey_Space);

    test::Check(strcmp(text, "abc ") == 0, "text field received the paste and the space: \"%s\"", text);
    test::Check(!paste, "Ctrl+V into the text field doesn't reach AcceptPaste()");
    test::Check(!createNode, "Space typed into the text field doesn't reach AcceptCreateNode()");
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
