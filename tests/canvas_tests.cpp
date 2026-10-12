//------------------------------------------------------------------------------
// The editor in host windows that clip or skip its canvas.
//------------------------------------------------------------------------------
# include "harness.h"
# include <cfloat>
# include <cstring>
# include <functional>

TEST_CASE(ClippedEditor, "canvas.clipped_editor_is_not_drawn_untransformed", "CNV-2", test::TestFlags_None)
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

    bool suspended = false;
    test::MouseMove(ImVec2(140.0f, 120.0f));
    for (int i = 0; i < 6; ++i)
        session.Frame([&](int frame)
        {
            if (frame == 0)
                ed::SetNodePosition(1, ImVec2(100.0f, 100.0f));
            ed::BeginNode(1);
            ImGui::TextUnformatted("Node 1");
            ed::EndNode();
            ed::Suspend();
            suspended = ed::IsSuspended();
            ed::Resume();
        });

    test::Check(editorMin.y >= editorTop, "editor draws nothing above its widget: geometry starts at y=%.0f, widget at y=%.0f", editorMin.y, editorTop);
    test::Check(!hovered, "node isn't hovered through the visible part of the host window: hovered node %d", (int)hovered.Get());
    test::Check(cursorAfter.y >= editorTop + session.EditorSize.y, "layout continues below the editor: cursor at y=%.0f, editor ends at y=%.0f", cursorAfter.y, editorTop + session.EditorSize.y);
    test::Check(suspended, "IsSuspended() is true after Suspend()");
}

namespace {

// Shows a 400 x 250 group, hides the editor with 'setHidden' for a few frames
// while the application keeps submitting it, and checks the group afterwards.
void CheckGroupSurvivesHiding(test::Session& session, test::Storage& storage, const std::function<void(bool hidden)>& setHidden)
{
    ImVec2 size;
    float  zoom = 0.0f;
    auto scene = [&](int frame)
    {
        if (frame == 0)
            ed::SetNodePosition(1, ImVec2(100.0f, 100.0f));
        ed::BeginNode(1);
        ImGui::TextUnformatted("Group");
        ed::Group(ImVec2(400.0f, 250.0f));
        ed::EndNode();
        size = ed::GetNodeSize(1);
        zoom = ed::GetCurrentZoom();
    };

    for (int i = 0; i < 5; ++i)
        session.Frame(scene);
    const ImVec2 sizeBefore = size;
    const float  zoomBefore = zoom;

    setHidden(true);
    for (int i = 0; i < 3; ++i)
        session.Frame(scene);
    const float zoomHidden = zoom;
    setHidden(false);
    for (int i = 0; i < 5; ++i)
        session.Frame(scene);

    test::Check(zoomHidden == zoomBefore && zoom == zoomBefore, "view zoom kept while hidden: %g, before %g, after %g", zoomHidden, zoomBefore, zoom);
    test::Check(size.x == sizeBefore.x && size.y == sizeBefore.y, "group size after the editor was hidden: (%g, %g), before (%g, %g)", size.x, size.y, sizeBefore.x, sizeBefore.y);
    test::Check(strstr(storage.Global.c_str(), "\"group_size\":{\"x\":400") != nullptr, "saved group size unchanged: %s", storage.Global.c_str());
}

} // namespace

TEST_CASE(CollapsedHost, "canvas.collapsed_host_keeps_group_size", "CORE-2", test::TestFlags_None)
{
    test::Storage storage;
    test::Session session(&storage);
    session.HostPos   = ImVec2(0.0f, 0.0f);
    session.HostSize  = ImVec2(1000.0f, 700.0f);
    session.HostFlags = ImGuiWindowFlags_NoSavedSettings;

    CheckGroupSurvivesHiding(session, storage, [&](bool hidden) { session.HostCollapsed = hidden; });
}

TEST_CASE(ScrolledOutChild, "canvas.child_scrolled_out_of_view_keeps_group_size", "CORE-2", test::TestFlags_None)
{
    // The editor sits in a child window, which content above it pushes out of view.
    test::Storage storage;
    test::Session session(&storage);
    session.HostPos  = ImVec2(0.0f, 0.0f);
    session.HostSize = ImVec2(1000.0f, 700.0f);

    bool pushedOut = false;
    session.BeforeEditor = [&]
    {
        if (pushedOut)
            ImGui::Dummy(ImVec2(10.0f, 2000.0f));
        ImGui::BeginChild("Child", ImVec2(900.0f, 600.0f));
    };
    session.AfterEditor = []
    {
        ImGui::EndChild();
    };

    CheckGroupSurvivesHiding(session, storage, [&](bool hidden) { pushedOut = hidden; });
}

TEST_CASE(HiddenDockTab, "canvas.hidden_dock_tab_keeps_group_size", "CORE-2", test::TestFlags_None)
{
# if defined(IMGUI_HAS_DOCK)
    // The host window is docked with another one, whose tab is selected for a while.
    test::Storage storage;
    test::Session session(&storage);
    session.PlaceHost = false;
    ImGui::SetCurrentContext(session.HostContext.Context);
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    // Docked in the second frame, once both windows exist.
    bool showOther = false;
    session.BeforeHost = [&]
    {
        if (session.FrameIndex != 1)
            return;
        const ImGuiID node = ImGui::DockBuilderAddNode();
        ImGui::DockBuilderSetNodePos(node, ImVec2(0.0f, 0.0f));
        ImGui::DockBuilderSetNodeSize(node, ImVec2(1000.0f, 700.0f));
        ImGui::DockBuilderDockWindow("Host", node);
        ImGui::DockBuilderDockWindow("Other", node);
        ImGui::DockBuilderFinish(node);
    };
    session.AfterHost = [&]
    {
        ImGui::Begin("Other");
        ImGui::TextUnformatted("Other tab");
        ImGui::End();
        ImGui::SetWindowFocus(showOther ? "Other" : "Host");
    };

    CheckGroupSurvivesHiding(session, storage, [&](bool hidden) { showOther = hidden; });
# else
    test::Skip("needs a docking build of Dear ImGui");
# endif
}
