//------------------------------------------------------------------------------
// The editor in host windows that clip or skip its canvas.
//------------------------------------------------------------------------------
# include "harness.h"
# include <cfloat>
# include <cstring>

TEST_CASE(ClippedEditor, "canvas.clipped_editor_is_not_drawn_untransformed", "CNV-2", test::TestFlags_KnownFailure)
{
    test::Storage storage;
    test::Session session(&storage);
    session.HostPos    = ImVec2(0.0f, 0.0f);
    session.HostSize   = ImVec2(400.0f, 300.0f);
    session.HostFlags  = ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar;
    session.EditorSize = ImVec2(380.0f, 250.0f);

    // The editor sits below the visible area of the scrolling host window.
    int    verticesBefore = 0;
    float  editorTop      = 0.0f;
    ImVec2 cursorAfter;
    ed::NodeId hovered;
    session.BeforeEditor = [&]
    {
        ImGui::TextUnformatted("Scroll down to see the node editor");
        ImGui::Dummy(ImVec2(10.0f, 800.0f));
        verticesBefore = session.DrawList->VtxBuffer.Size;
        editorTop      = ImGui::GetCursorScreenPos().y;
    };
    ImVec2 editorMin, editorMax;
    session.AfterEditor = [&]
    {
        cursorAfter = ImGui::GetCursorScreenPos();
        hovered     = session.Use([] { return ed::GetHoveredNode(); });
        editorMin   = ImVec2(FLT_MAX, FLT_MAX);
        editorMax   = ImVec2(-FLT_MAX, -FLT_MAX);
        for (int i = verticesBefore; i < session.DrawList->VtxBuffer.Size; ++i)
        {
            editorMin = ImMin(editorMin, session.DrawList->VtxBuffer[i].pos);
            editorMax = ImMax(editorMax, session.DrawList->VtxBuffer[i].pos);
        }
    };

    test::MouseMove(ImVec2(140.0f, 120.0f));
    for (int i = 0; i < 6; ++i)
        session.Frame([](int frame)
        {
            if (frame == 0)
                ed::SetNodePosition(1, ImVec2(100.0f, 100.0f));
            ed::BeginNode(1);
            ImGui::TextUnformatted("Node 1");
            ed::EndNode();
        });

    test::Check(editorMin.y >= editorTop, "editor draws nothing above its widget: geometry starts at y=%.0f, widget at y=%.0f", editorMin.y, editorTop);
    test::Check(!hovered, "node isn't hovered through the visible part of the host window: hovered node %d", (int)hovered.Get());
    test::Check(cursorAfter.y >= editorTop + session.EditorSize.y, "layout continues below the editor: cursor at y=%.0f, editor ends at y=%.0f", cursorAfter.y, editorTop + session.EditorSize.y);
}

TEST_CASE(CollapsedHost, "canvas.collapsed_host_keeps_group_size", "CORE-2", test::TestFlags_KnownFailure)
{
    test::Storage storage;
    test::Session session(&storage);
    session.HostPos   = ImVec2(0.0f, 0.0f);
    session.HostSize  = ImVec2(1000.0f, 700.0f);
    session.HostFlags = ImGuiWindowFlags_NoSavedSettings;

    ImVec2 size;
    auto scene = [&size](int frame)
    {
        if (frame == 0)
            ed::SetNodePosition(1, ImVec2(100.0f, 100.0f));
        ed::BeginNode(1);
        ImGui::TextUnformatted("Group");
        ed::Group(ImVec2(400.0f, 250.0f));
        ed::EndNode();
        size = ed::GetNodeSize(1);
    };

    for (int i = 0; i < 5; ++i)
        session.Frame(scene);
    const ImVec2 before = size;
    const std::string savedBefore = storage.Global;

    // The host window is collapsed for two frames; the application keeps submitting the editor.
    session.HostCollapsed = true;
    for (int i = 0; i < 2; ++i)
        session.Frame(scene);
    session.HostCollapsed = false;
    for (int i = 0; i < 5; ++i)
        session.Frame(scene);

    test::Check(size.x == before.x && size.y == before.y, "group size after the host was collapsed: (%g, %g), before (%g, %g)", size.x, size.y, before.x, before.y);
    test::Check(strstr(storage.Global.c_str(), "\"group_size\":{\"x\":400") != nullptr, "saved group size unchanged: %s", storage.Global.c_str());
}
