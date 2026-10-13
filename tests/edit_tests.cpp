//------------------------------------------------------------------------------
// Edits the user completes, as BeginEdits() and QueryEdit() report them.
//------------------------------------------------------------------------------
# include "harness.h"
# include <algorithm>
# include <vector>

namespace {

ed::Config UnnamedConfig(bool reportNavigation = false)
{
    ed::Config config;
    config.SettingsName          = nullptr;
    config.ReportNavigationEdits = reportNavigation;
    return config;
}

// Node 1 at (100, 100) with output pin 11, node 2 at (400, 100) with input pin 21,
// link 10 between them, and group 3 of 300 x 200 at (100, 400).
struct Scene
{
    bool   Node2 = true;
    ImVec2 LinkMiddle; // on screen

    void Submit(int frame)
    {
        if (frame == 0)
        {
            ed::SetNodePosition(1, ImVec2(100.0f, 100.0f));
            ed::SetNodePosition(2, ImVec2(400.0f, 100.0f));
            ed::SetNodePosition(3, ImVec2(100.0f, 400.0f));
        }

        ImVec2 pins[2];
        ed::BeginNode(1);
        ImGui::TextUnformatted("Node 1");
        ed::BeginPin(11, ed::PinKind::Output);
        ImGui::TextUnformatted("out >");
        ed::EndPin();
        pins[0] = (ImGui::GetItemRectMin() + ImGui::GetItemRectMax()) * 0.5f;
        ed::EndNode();

        if (Node2)
        {
            ed::BeginNode(2);
            ImGui::TextUnformatted("Node 2");
            ed::BeginPin(21, ed::PinKind::Input);
            ImGui::TextUnformatted("> in");
            ed::EndPin();
            pins[1] = (ImGui::GetItemRectMin() + ImGui::GetItemRectMax()) * 0.5f;
            ed::EndNode();

            ed::Link(10, 11, 21);
            LinkMiddle = ed::CanvasToScreen((pins[0] + pins[1]) * 0.5f);
        }

        ed::BeginNode(3);
        ImGui::TextUnformatted("Group 3");
        ed::Group(ImVec2(300.0f, 200.0f));
        ed::EndNode();
    }
};

// Copies of the edits the application read.
struct EditLog
{
    struct Entry
    {
        ed::EditType               Type = ed::EditType::Move;
        std::vector<ed::NodeState> Before;
        std::vector<ed::NodeState> After;
        std::vector<uintptr_t>     NodesBefore;
        std::vector<uintptr_t>     NodesAfter;
        std::vector<uintptr_t>     LinksBefore;
        std::vector<uintptr_t>     LinksAfter;
        ed::ViewState              ViewBefore;
        ed::ViewState              ViewAfter;
    };

    std::vector<Entry> Entries;

    void Read()
    {
        if (!ed::BeginEdits())
            return;

        ed::Edit edit;
        while (ed::QueryEdit(&edit))
        {
            Entry entry;
            entry.Type = edit.Type;
            entry.Before.assign(edit.NodesBefore, edit.NodesBefore + edit.NodeCount);
            entry.After.assign(edit.NodesAfter, edit.NodesAfter + edit.NodeCount);
            for (int i = 0; i < edit.SelectionBefore.NodeCount; ++i)
                entry.NodesBefore.push_back(edit.SelectionBefore.Nodes[i].Get());
            for (int i = 0; i < edit.SelectionAfter.NodeCount; ++i)
                entry.NodesAfter.push_back(edit.SelectionAfter.Nodes[i].Get());
            for (int i = 0; i < edit.SelectionBefore.LinkCount; ++i)
                entry.LinksBefore.push_back(edit.SelectionBefore.Links[i].Get());
            for (int i = 0; i < edit.SelectionAfter.LinkCount; ++i)
                entry.LinksAfter.push_back(edit.SelectionAfter.Links[i].Get());
            entry.ViewBefore = edit.ViewBefore;
            entry.ViewAfter  = edit.ViewAfter;
            Entries.push_back(entry);
        }

        ed::EndEdits();
    }

    int Count(ed::EditType type) const
    {
        return static_cast<int>(std::count_if(Entries.begin(), Entries.end(), [type](const Entry& entry) { return entry.Type == type; }));
    }
};

std::vector<uintptr_t> Sorted(std::vector<uintptr_t> ids)
{
    std::sort(ids.begin(), ids.end());
    return ids;
}

bool SamePosition(const ed::NodeState& state, ImVec2 position)
{
    return state.Position.x == position.x && state.Position.y == position.y;
}

bool SameView(const ed::ViewState& a, const ed::ViewState& b)
{
    return a.VisibleMin.x == b.VisibleMin.x && a.VisibleMin.y == b.VisibleMin.y && a.VisibleMax.x == b.VisibleMax.x && a.VisibleMax.y == b.VisibleMax.y;
}

ImVec2 NodeCenter(test::Session& session, uintptr_t id)
{
    return session.Use([id] { return ed::CanvasToScreen(ed::GetNodePosition(id) + ed::GetNodeSize(id) * 0.5f); });
}

ImVec2 NodeGrab(test::Session& session, uintptr_t id)
{
    return session.Use([id] { return ed::CanvasToScreen(ed::GetNodePosition(id) + ImVec2(5.0f, 5.0f)); });
}

// Reads the edits, and counts the moves.
int ReadMoves()
{
    int count = 0;
    if (ed::BeginEdits())
    {
        ed::Edit edit;
        while (ed::QueryEdit(&edit))
            count += edit.Type == ed::EditType::Move ? 1 : 0;
        ed::EndEdits();
    }
    return count;
}

// Drags node 1 to the right, and releases it.
void DragNode1(test::Session& session)
{
    const auto grab = NodeGrab(session, 1);
    test::MouseMove(grab);
    session.Run(2);
    test::MouseButton(0, true);
    session.Run(1);
    for (int step = 1; step <= 4; ++step)
    {
        test::MouseMove(grab + ImVec2(step * 10.0f, 0.0f));
        session.Run(1);
    }
    test::MouseButton(0, false);
}

// Accepts the deletion of node 2 and its link, if the editor offers it.
bool AcceptDeletion()
{
    bool accepted = false;
    if (ed::BeginDelete())
    {
        ed::NodeId nodeId;
        while (ed::QueryDeletedNode(&nodeId))
            accepted |= ed::AcceptDeletedItem() && nodeId == ed::NodeId(2);
        ed::LinkId linkId;
        while (ed::QueryDeletedLink(&linkId))
            ed::AcceptDeletedItem();
        ed::EndDelete();
    }
    return accepted;
}

} // namespace

TEST_CASE(DragReportsMove, "edit.drag_reports_one_move", "BPN-10", test::TestFlags_None)
{
    // Nodes 1 and 2 are dragged together.
    test::Session session(UnnamedConfig());
    Scene scene;
    EditLog log;
    session.DefaultContent = [&](int frame) { scene.Submit(frame); log.Read(); };
    session.Run(3);
    const ed::NodeId nodes[] = { 1, 2 };
    session.Use([&] { ed::SetSelection(nodes, 2, nullptr, 0); });
    session.Run(1);

    const auto grab = NodeGrab(session, 1);
    session.Drag(grab, grab + ImVec2(64.0f, 32.0f));
    const auto end1 = session.Use([] { return ed::GetNodePosition(1); });
    const auto end2 = session.Use([] { return ed::GetNodePosition(2); });

    const auto move = log.Entries.empty() ? EditLog::Entry() : log.Entries[0];
    test::Check(log.Entries.size() == 1 && move.Type == ed::EditType::Move, "one edit, a move: %d edits", static_cast<int>(log.Entries.size()));
    test::Check(move.After.size() == 2 && move.Before.size() == 2 && move.After[0].Id == move.Before[0].Id && move.After[1].Id == move.Before[1].Id
        && Sorted({ move.After[0].Id.Get(), move.After[1].Id.Get() }) == std::vector<uintptr_t>({ 1, 2 }), "of nodes 1 and 2, in the same order before and after");
    if (move.After.size() == 2 && move.Before.size() == 2)
    {
        const size_t one = move.After[0].Id == ed::NodeId(1) ? 0 : 1;
        test::Check(SamePosition(move.Before[one], ImVec2(100.0f, 100.0f)) && SamePosition(move.Before[1 - one], ImVec2(400.0f, 100.0f)), "from where the drag found them");
        test::Check(SamePosition(move.After[one], end1) && SamePosition(move.After[1 - one], end2) && end1.x > 100.0f, "to where it left them: (%g, %g) and (%g, %g)",
            end1.x, end1.y, end2.x, end2.y);
    }
}

TEST_CASE(ResizeReportsResize, "edit.resize_reports_one_resize", "BPN-10", test::TestFlags_None)
{
    test::Session session(UnnamedConfig());
    Scene scene;
    EditLog log;
    session.DefaultContent = [&](int frame) { scene.Submit(frame); log.Read(); };
    session.Run(3);

    // 2 px inside the right edge.
    const auto edge = session.Use([] { return ed::CanvasToScreen(ed::GetNodePosition(3) + ed::GetNodeSize(3) * ImVec2(1.0f, 0.5f) - ImVec2(2.0f, 0.0f)); });
    session.Drag(edge, edge + ImVec2(64.0f, 0.0f));
    ed::NodeState after;
    session.Use([&] { return ed::GetNodeState(3, &after); });

    const auto resize = log.Entries.empty() ? EditLog::Entry() : log.Entries[0];
    test::Check(log.Entries.size() == 1 && resize.Type == ed::EditType::Resize, "one edit, a resize: %d edits", static_cast<int>(log.Entries.size()));
    test::Check(resize.Before.size() == 1 && resize.Before[0].Id == ed::NodeId(3) && SamePosition(resize.Before[0], ImVec2(100.0f, 400.0f))
        && resize.Before[0].GroupSize.x == 300.0f && resize.Before[0].GroupSize.y == 200.0f, "of group 3, from 300 x 200 at (100, 400)");
    test::Check(resize.After.size() == 1 && SamePosition(resize.After[0], after.Position) && resize.After[0].GroupSize.x == after.GroupSize.x
        && resize.After[0].GroupSize.y == after.GroupSize.y && after.GroupSize.x > 300.0f, "to %g x %g", after.GroupSize.x, after.GroupSize.y);
}

TEST_CASE(SelectionReportsSelect, "edit.selection_changes_report_select", "BPN-10", test::TestFlags_None)
{
    test::Session session(UnnamedConfig());
    Scene scene;
    EditLog log;
    session.DefaultContent = [&](int frame) { scene.Submit(frame); log.Read(); };
    session.Run(3);

    const auto background = session.Use([] { return ed::CanvasToScreen(ImVec2(700.0f, 300.0f)); });
    session.Click(background);
    const auto empty = log.Entries.size();

    session.Click(NodeCenter(session, 1));
    test::Ctrl(true);
    session.Click(NodeCenter(session, 2));
    test::Ctrl(false);
    session.Run(1);
    session.Click(scene.LinkMiddle);
    session.Click(background);
    const auto from = session.Use([] { return ed::CanvasToScreen(ImVec2(60.0f, 60.0f)); });
    const auto to   = session.Use([] { return ed::CanvasToScreen(ed::GetNodePosition(2) + ed::GetNodeSize(2) + ImVec2(20.0f, 20.0f)); });
    session.Drag(from, to);

    using Ids = std::vector<uintptr_t>;
    struct Expected { const char* Gesture; Ids NodesBefore, LinksBefore, NodesAfter, LinksAfter; };
    const Expected expected[] =
    {
        { "click",            {},       {},     { 1 },    {}     },
        { "Ctrl+click",       { 1 },    {},     { 1, 2 }, {}     },
        { "link click",       { 1, 2 }, {},     {},       { 10 } },
        { "background click", {},       { 10 }, {},       {}     },
        { "rectangle",        {},       {},     { 1, 2 }, {}     },
    };
    const int count = static_cast<int>(sizeof(expected) / sizeof(*expected));

    test::Check(empty == 0, "a background click with nothing selected reports nothing: %d edits", static_cast<int>(empty));
    test::Check(static_cast<int>(log.Entries.size()) == count && log.Count(ed::EditType::Select) == count, "%d selection changes: %d edits, %d selections",
        count, static_cast<int>(log.Entries.size()), log.Count(ed::EditType::Select));
    for (int i = 0; i < count && i < static_cast<int>(log.Entries.size()); ++i)
    {
        const auto& entry = log.Entries[i];
        test::Check(Sorted(entry.NodesBefore) == expected[i].NodesBefore && entry.LinksBefore == expected[i].LinksBefore
            && Sorted(entry.NodesAfter) == expected[i].NodesAfter && entry.LinksAfter == expected[i].LinksAfter,
            "%s: %d nodes and %d links, then %d nodes and %d links", expected[i].Gesture,
            static_cast<int>(entry.NodesBefore.size()), static_cast<int>(entry.LinksBefore.size()), static_cast<int>(entry.NodesAfter.size()), static_cast<int>(entry.LinksAfter.size()));
    }
}

TEST_CASE(ApplicationChangesNotReported, "edit.application_changes_are_not_reported", "BPN-10", test::TestFlags_None)
{
    // Edits are read from the first frame on, with navigation.
    test::Session session(UnnamedConfig(true));
    Scene scene;
    EditLog log;
    session.DefaultContent = [&](int frame) { scene.Submit(frame); log.Read(); };
    session.Run(3);

    session.Use([]
    {
        ed::NodeState state;
        ed::GetNodeState(1, &state);
        state.Position = ImVec2(150.0f, 150.0f);
        ed::SetNodeState(state);
        ed::SetNodePosition(2, ImVec2(500.0f, 120.0f));
        ed::SetGroupSize(3, ImVec2(320.0f, 220.0f));
        const ed::NodeId nodes[] = { 1, 2 };
        ed::SetSelection(nodes, 2, nullptr, 0);
        ed::SelectNode(3, true);
        ed::ClearSelection();
        ed::SelectLink(10);
        ed::NavigateToContent(0.0f);
    });
    session.Run(30);

    session.Use([]
    {
        ed::ViewState view;
        view.VisibleMin = ImVec2(-500.0f, -300.0f);
        view.VisibleMax = ImVec2(500.0f, 300.0f);
        ed::SetViewState(view, 0.2f);
    });
    session.Run(30);
    session.Use([] { ed::CenterNodeOnScreen(1); });
    session.Run(60);

    session.HostSize = ImVec2(800.0f, 500.0f);
    session.Run(3);

    test::Check(log.Entries.empty(), "nothing reported: %d edits", static_cast<int>(log.Entries.size()));
}

TEST_CASE(DeletionReportsNoSelect, "edit.deletion_reports_no_selection_change", "BPN-10", test::TestFlags_None)
{
    // The user selects node 2 and deletes it, which the delete protocol reports.
    test::Session session(UnnamedConfig());
    Scene scene;
    EditLog log;
    bool accepted = false;
    session.DefaultContent = [&](int frame) { scene.Submit(frame); accepted |= AcceptDeletion(); log.Read(); };
    session.Run(3);

    session.Click(NodeCenter(session, 2));
    const auto selected = log.Entries.size();
    session.PressKey(ImGuiKey_Delete);
    scene.Node2 = false;
    session.Run(3);

    test::Check(selected == 1 && accepted, "node 2 was selected and deleted: %d edits, accepted %d", static_cast<int>(selected), accepted);
    test::Check(log.Entries.size() == 1, "the deletion deselects node 2 without a selection change: %d edits", static_cast<int>(log.Entries.size()));
}

TEST_CASE(NavigationReportedWhenAsked, "edit.navigation_is_reported_when_asked", "EXB-12", test::TestFlags_None)
{
    for (int report = 0; report < 2; ++report)
    {
        test::Session session(UnnamedConfig(report != 0));
        Scene scene;
        EditLog log;
        session.DefaultContent = [&](int frame) { scene.Submit(frame); log.Read(); };
        session.Run(3);

        // Zoom in with the wheel, and let the view settle.
        test::MouseMove(NodeCenter(session, 2));
        session.Run(2);
        const auto beforeZoom = session.Use([] { return ed::GetViewState(); });
        test::MouseWheel(1.0f);
        session.Run(60);
        const auto afterZoom = session.Use([] { return ed::GetViewState(); });
        const auto zooms = log.Entries;

        // Pan with the navigate button.
        const auto from = session.Use([] { return ed::CanvasToScreen(ImVec2(700.0f, 300.0f)); });
        session.Drag(from, from + ImVec2(80.0f, 40.0f), 1);
        const auto afterPan = session.Use([] { return ed::GetViewState(); });
        const auto pans = std::vector<EditLog::Entry>(log.Entries.begin() + static_cast<std::ptrdiff_t>(zooms.size()), log.Entries.end());

        if (report)
        {
            test::Check(zooms.size() == 1 && zooms[0].Type == ed::EditType::Navigate, "one edit for the zoom: %d edits", static_cast<int>(zooms.size()));
            test::Check(zooms.size() == 1 && SameView(zooms[0].ViewBefore, beforeZoom) && SameView(zooms[0].ViewAfter, afterZoom)
                && afterZoom.VisibleMax.x - afterZoom.VisibleMin.x < beforeZoom.VisibleMax.x - beforeZoom.VisibleMin.x,
                "from the view before the zoom to where the view settled");
            test::Check(pans.size() == 1 && pans[0].Type == ed::EditType::Navigate && SameView(pans[0].ViewBefore, afterZoom) && SameView(pans[0].ViewAfter, afterPan)
                && afterPan.VisibleMin.x < afterZoom.VisibleMin.x, "one edit for the pan, from the zoomed view: %d edits", static_cast<int>(pans.size()));
        }
        else
            test::Check(zooms.empty() && pans.empty(), "without Config::ReportNavigationEdits, navigation isn't reported: %d, %d",
                static_cast<int>(zooms.size()), static_cast<int>(pans.size()));
    }
}

TEST_CASE(ClickReportsNoNavigation, "edit.navigate_button_click_reports_no_navigation", "BPN-10", test::TestFlags_None)
{
    // A click of the navigate button leaves the view where it was. Then the
    // application loads a view with only a scroll and a zoom, which moves the view
    // without navigating to it.
    test::Session session(UnnamedConfig(true));
    Scene scene;
    EditLog log;
    session.DefaultContent = [&](int frame) { scene.Submit(frame); log.Read(); };
    session.Run(3);

    session.Click(session.Use([] { return ed::CanvasToScreen(ImVec2(700.0f, 300.0f)); }), 1);
    const auto before = session.Use([] { return ed::GetViewState(); });
    session.Use([] { ed::LoadSettingsFromMemory(R"({"view":{"scroll":{"x":-120,"y":-40},"zoom":1}})"); });
    session.Run(3);
    const auto after = session.Use([] { return ed::GetViewState(); });

    test::Check(!SameView(before, after), "the loaded view moved it: (%g, %g), was (%g, %g)", after.VisibleMin.x, after.VisibleMin.y, before.VisibleMin.x, before.VisibleMin.y);
    test::Check(log.Entries.empty(), "that isn't reported as navigation: %d edits", static_cast<int>(log.Entries.size()));
}

TEST_CASE(SetDuringDragNotReported, "edit.set_during_a_drag_reports_no_move", "BPN-11", test::TestFlags_None)
{
    test::Session session(UnnamedConfig());
    Scene scene;
    EditLog log;
    session.DefaultContent = [&](int frame) { scene.Submit(frame); log.Read(); };
    session.Run(3);

    const auto grab = NodeGrab(session, 1);
    test::MouseMove(grab);
    session.Run(2);
    test::MouseButton(0, true);
    session.Run(1);
    for (int step = 1; step <= 6; ++step)
    {
        test::MouseMove(grab + ImVec2(step * 10.0f, step * 10.0f));
        session.Run(1);
    }
    const auto dragged = session.Use([] { return ed::GetNodePosition(1); });
    session.Use([] { ed::NodeState state; ed::GetNodeState(1, &state); state.Position = ImVec2(300.0f, 300.0f); ed::SetNodeState(state); });
    test::MouseMove(grab + ImVec2(80.0f, 80.0f));
    session.Run(1);
    test::MouseButton(0, false);
    session.Run(3);

    test::Check(dragged.x > 100.0f, "node 1 was being dragged: at (%g, %g)", dragged.x, dragged.y);
    test::Check(log.Entries.empty(), "the drag SetNodeState() stopped reports nothing: %d edits", static_cast<int>(log.Entries.size()));
}

TEST_CASE(SetAfterDragReportsMove, "edit.set_after_a_drag_reports_its_move", "BPN-10", test::TestFlags_None)
{
    // The mouse button is up, and the application sets node 1 before the editor
    // completes the drag.
    test::Session session(UnnamedConfig());
    Scene scene;
    EditLog log;
    session.DefaultContent = [&](int frame) { scene.Submit(frame); log.Read(); };
    session.Run(3);

    DragNode1(session);
    session.Run(1);
    const auto dropped = session.Use([] { return ed::GetNodePosition(1); });
    session.Use([] { ed::NodeState state; ed::GetNodeState(1, &state); state.Position = ImVec2(300.0f, 300.0f); ed::SetNodeState(state); });
    session.Run(3);
    const auto after = session.Use([] { return ed::GetNodePosition(1); });

    test::Check(log.Entries.size() == 1 && log.Entries[0].Type == ed::EditType::Move && log.Entries[0].After.size() == 1
        && SamePosition(log.Entries[0].After[0], dropped) && dropped.x > 100.0f, "the drag is reported, to where the user dropped node 1: (%g, %g)", dropped.x, dropped.y);
    test::Check(after.x == 300.0f && after.y == 300.0f, "node 1 is where the application set it: (%g, %g)", after.x, after.y);
}

TEST_CASE(UndoWhileReading, "edit.undo_while_reading_edits", "BPN-10", test::TestFlags_None)
{
    // The application undoes each move as it reads it.
    test::Session session(UnnamedConfig());
    Scene scene;
    int moves = 0, others = 0;
    bool undone = false;
    session.DefaultContent = [&](int frame)
    {
        scene.Submit(frame);
        if (ed::BeginEdits())
        {
            ed::Edit edit;
            while (ed::QueryEdit(&edit))
            {
                if (edit.Type == ed::EditType::Move)
                {
                    ++moves;
                    undone |= ed::SetNodeStates(edit.NodesBefore, edit.NodeCount);
                }
                else
                    ++others;
            }
            ed::EndEdits();
        }
    };
    session.Run(3);

    const auto grab = NodeGrab(session, 1);
    session.Drag(grab, grab + ImVec2(64.0f, 32.0f));
    session.Run(3);
    const auto after = session.Use([] { return ed::GetNodePosition(1); });

    test::Check(moves == 1 && others == 0 && undone, "one move, undone: %d moves, %d other edits", moves, others);
    test::Check(after.x == 100.0f && after.y == 100.0f, "node 1 is back where it was: (%g, %g)", after.x, after.y);
}

TEST_CASE(EditsLastUntilNextEnd, "edit.edits_last_until_the_next_end", "BPN-10", test::TestFlags_None)
{
    // Node 1 is dragged twice. After each frame that follows the release, the
    // application reads the moves twice, except after frame 'skip'.
    test::Session session(UnnamedConfig());
    Scene scene;
    session.DefaultContent = [&](int frame) { scene.Submit(frame); };
    session.Run(3);

    auto drag = [&session](int skip)
    {
        DragNode1(session);

        std::vector<int> moves;
        for (int frame = 0; frame < 5; ++frame)
        {
            session.Run(1);
            moves.push_back(frame == skip ? 0 : session.Use([] { return ReadMoves() + ReadMoves(); }));
        }
        return moves;
    };

    const auto read   = drag(-1);
    const auto first  = static_cast<int>(std::find(read.begin(), read.end(), 1) - read.begin());
    const auto missed = drag(first);
    test::Check(std::count(read.begin(), read.end(), 1) == 1 && std::count(read.begin(), read.end(), 0) == 4, "read between frames, the move is there once");
    test::Check(std::count(missed.begin(), missed.end(), 0) == 5, "not read then, it's gone after the next frame");
}

TEST_CASE(ClearStateDropsEdits, "edit.clear_state_drops_unread_edits", "BPN-10", test::TestFlags_None)
{
    // Node 1 is dragged twice. The second time, the application starts a new
    // document before it reads the edits.
    test::Session session(UnnamedConfig(true));
    Scene scene;
    session.DefaultContent = [&](int frame) { scene.Submit(frame); };
    session.Run(3);

    DragNode1(session);
    session.Run(2);
    const int read = session.Use([] { return ReadMoves(); });

    DragNode1(session);
    session.Run(2);
    const int cleared = session.Use([] { ed::ClearState(); return ReadMoves(); });
    session.Run(2);
    const int later = session.Use([] { return ReadMoves(); });

    // Away from the view a new document starts with, a zoom is still animating
    // when the application starts another one.
    EditLog log;
    session.DefaultContent = [&](int frame) { scene.Submit(frame); log.Read(); };
    session.Use([]
    {
        ed::ViewState view;
        view.VisibleMin = ImVec2(2000.0f, 1000.0f);
        view.VisibleMax = ImVec2(3000.0f, 1600.0f);
        ed::SetViewState(view);
    });
    session.Run(2);
    test::MouseMove(session.Use([] { const auto view = ed::GetViewState(); return ed::CanvasToScreen((view.VisibleMin + view.VisibleMax) * 0.5f); }));
    session.Run(2);
    test::MouseWheel(1.0f);
    session.Run(1);
    session.Use([] { ed::ClearState(); });
    session.Run(60);

    test::Check(read == 1, "the first move is read: %d", read);
    test::Check(cleared == 0 && later == 0, "the second isn't, after ClearState(): %d, then %d", cleared, later);
    test::Check(log.Entries.empty(), "nor the zoom: %d edits", static_cast<int>(log.Entries.size()));
}

TEST_CASE(EndEditsAfterFailedBegin, "edit.end_edits_after_failed_begin_asserts", "BPN-10", test::TestFlags_None)
{
    test::ExpectAsserts(1);
    test::Session session(UnnamedConfig());
    session.Frame([](int)
    {
        if (!ed::BeginEdits())
            ed::EndEdits();
    });
}
