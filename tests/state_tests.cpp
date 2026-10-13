//------------------------------------------------------------------------------
// Typed node, view and selection state.
//------------------------------------------------------------------------------
# include "harness.h"
# include <cfloat>
# include <cmath>
# include <limits>
# include <string>
# include <vector>

namespace {

ed::Config UnnamedConfig()
{
    ed::Config config;
    config.SettingsName = nullptr;
    return config;
}

// Node 1 at (100, 100) with output pin 11, node 2 at (400, 100) with input pin 21,
// link 10 between them, and group 3 of 300 x 200 at (100, 400). Groups have no pins.
struct Scene
{
    bool Place  = true;
    bool Node2  = true;
    bool Group3 = true;

    void Submit(int frame)
    {
        if (frame == 0 && Place)
        {
            ed::SetNodePosition(1, ImVec2(100.0f, 100.0f));
            ed::SetNodePosition(2, ImVec2(400.0f, 100.0f));
            ed::SetNodePosition(3, ImVec2(100.0f, 400.0f));
        }

        ed::BeginNode(1);
        ImGui::TextUnformatted("Node 1");
        ed::BeginPin(11, ed::PinKind::Output);
        ImGui::TextUnformatted("out >");
        ed::EndPin();
        ed::EndNode();

        if (Node2)
        {
            ed::BeginNode(2);
            ImGui::TextUnformatted("Node 2");
            ed::BeginPin(21, ed::PinKind::Input);
            ImGui::TextUnformatted("> in");
            ed::EndPin();
            ed::EndNode();

            ed::Link(10, 11, 21);
        }

        if (Group3)
        {
            ed::BeginNode(3);
            ImGui::TextUnformatted("Group 3");
            ed::Group(ImVec2(300.0f, 200.0f));
            ed::EndNode();
        }
    }
};

std::vector<ed::NodeState> AllStates(test::Session& session)
{
    return session.Use([]
    {
        std::vector<ed::NodeState> states(static_cast<size_t>(ed::GetNodeStates(nullptr, 0)));
        states.resize(static_cast<size_t>(ed::GetNodeStates(states.data(), static_cast<int>(states.size()))));
        return states;
    });
}

ed::NodeState StateOf(test::Session& session, uintptr_t id)
{
    ed::NodeState state;
    session.Use([&] { return ed::GetNodeState(id, &state); });
    return state;
}

ed::SaveReasonFlags TakeChangeReason(test::Session& session)
{
    return session.Use([]
    {
        const auto reason = ed::GetSettingsChangeReason();
        ed::ClearWantSaveSettings();
        return reason;
    });
}

} // namespace

TEST_CASE(NodeStateRoundTrip, "state.node_state_round_trip", "BPN-11", test::TestFlags_None)
{
    test::Session session(UnnamedConfig());
    Scene scene;
    session.DefaultContent = [&](int frame) { scene.Submit(frame); };
    session.Run(3);

    const auto states = AllStates(session);
    test::Check(states.size() == 3 && states[0].Id == ed::NodeId(1) && states[1].Id == ed::NodeId(2) && states[2].Id == ed::NodeId(3), "three nodes, by ID: %d", static_cast<int>(states.size()));
    test::Check(states.size() == 3 && states[0].Position.x == 100.0f && states[0].Position.y == 100.0f && states[0].GroupSize.x == 0.0f && states[0].GroupSize.y == 0.0f,
        "node 1 at (100, 100), no group size");
    test::Check(states.size() == 3 && states[2].Position.x == 100.0f && states[2].Position.y == 400.0f && states[2].GroupSize.x == 300.0f && states[2].GroupSize.y == 200.0f,
        "group 3 at (100, 400), 300 x 200");

    TakeChangeReason(session);
    ed::NodeState moved;
    moved.Id        = 3;
    moved.Position  = ImVec2(500.0f, 250.0f);
    moved.GroupSize = ImVec2(320.0f, 240.0f);
    moved.ZPosition = 3.0f;
    const bool changed = session.Use([&] { return ed::SetNodeState(moved); });
    const bool again   = session.Use([&] { return ed::SetNodeState(moved); });
    session.Run(2);

    const auto after  = StateOf(session, 3);
    const auto reason = TakeChangeReason(session);
    test::Check(changed && !again, "SetNodeState() reports the change once: %d, then %d", changed, again);
    test::Check(after.Position.x == 500.0f && after.Position.y == 250.0f && after.GroupSize.x == 320.0f && after.GroupSize.y == 240.0f && after.ZPosition == 3.0f,
        "group 3 keeps the new state through the frames: (%g, %g), %g x %g, z %g", after.Position.x, after.Position.y, after.GroupSize.x, after.GroupSize.y, after.ZPosition);
    test::Check((reason & (ed::SaveReasonFlags::Position | ed::SaveReasonFlags::Size)) == (ed::SaveReasonFlags::Position | ed::SaveReasonFlags::Size),
        "the settings record a position and a size change: 0x%02X", static_cast<unsigned>(reason));
}

TEST_CASE(NodesNotSubmittedKeepState, "state.nodes_not_submitted_keep_their_state", "BPN-5", test::TestFlags_None)
{
    // Node 7 is only loaded; nodes 2 and 3 stop being submitted.
    test::Session session(UnnamedConfig());
    session.Use([] { ed::LoadSettingsFromMemory(R"({"nodes":{"node:7":{"location":{"x":50,"y":60}}}})"); });
    Scene scene;
    session.DefaultContent = [&](int frame) { scene.Submit(frame); };
    session.Run(3);
    scene.Node2 = scene.Group3 = false;
    session.Run(2);

    const auto states = AllStates(session);
    std::vector<uintptr_t> ids;
    for (auto& state : states)
        ids.push_back(state.Id.Get());
    test::Check(ids == std::vector<uintptr_t>({ 1, 2, 3, 7 }), "nodes 1, 2, 3 and 7, by ID: %d nodes", static_cast<int>(ids.size()));
    test::Check(states.size() == 4 && states[1].Position.x == 400.0f && states[2].GroupSize.x == 300.0f && states[3].Position.x == 50.0f && states[3].Position.y == 60.0f,
        "hidden and loaded nodes have their state");

    ed::NodeState state;
    const bool known   = session.Use([&] { return ed::GetNodeState(7, &state); });
    const bool unknown = session.Use([&] { return ed::GetNodeState(99, &state); });
    const int  first   = session.Use([&] { ed::NodeState two[2]; return ed::GetNodeStates(two, 2); });
    test::Check(known && !unknown, "GetNodeState() knows node 7 and not node 99");
    test::Check(first == 2, "GetNodeStates() writes no more than its capacity: %d", first);
}

TEST_CASE(SetNodeStateValidates, "state.set_node_state_validates_input", "BPN-9", test::TestFlags_None)
{
    test::Session session(UnnamedConfig());
    Scene scene;
    session.DefaultContent = [&](int frame) { scene.Submit(frame); };
    session.Run(3);

    const auto nan = std::numeric_limits<float>::quiet_NaN();
    auto state = StateOf(session, 1);
    auto set = [&](ed::NodeState s) { return session.Use([&] { return ed::SetNodeState(s); }); };

    auto bad = state;
    bad.Position.x = nan;
    const bool rejectedPosition = !set(bad);
    bad = state;
    bad.ZPosition = std::numeric_limits<float>::infinity();
    const bool rejectedZ = !set(bad);
    bad = state;
    bad.GroupSize = ImVec2(-10.0f, 10.0f);
    const bool rejectedSize = !set(bad);
    bad = state;
    bad.Id = 0;
    const bool rejectedZero = !set(bad);

    const auto unchanged = StateOf(session, 1);
    test::Check(rejectedPosition && rejectedZ && rejectedSize, "positions, z positions and group sizes that aren't valid are refused");
    test::Check(rejectedZero, "node ID 0 is refused");
    test::Check(unchanged.Position.x == 100.0f && unchanged.Position.y == 100.0f && unchanged.ZPosition == 0.0f, "node 1 is unchanged: (%g, %g), z %g",
        unchanged.Position.x, unchanged.Position.y, unchanged.ZPosition);

    auto far = state;
    far.Position = ImVec2(1.0e9f, -1.0e9f);
    set(far);
    const auto clamped = StateOf(session, 1);
    const auto size    = session.Use([] { return ed::GetNodeSize(1); });
    test::Check(clamped.Position.x == 250000.0f - size.x && clamped.Position.y == -250000.0f, "a position far away stops at the canvas border: (%g, %g)", clamped.Position.x, clamped.Position.y);
}

TEST_CASE(RestoreBeforeFirstFrame, "state.restore_before_the_first_frame", "BPN-2", test::TestFlags_None)
{
    // A document opens: its layout is set before the editor has created any node.
    ed::NodeState states[3];
    states[0].Id = 1; states[0].Position = ImVec2(150.0f, 50.0f);
    states[1].Id = 2; states[1].Position = ImVec2(600.0f, 80.0f); states[1].ZPosition = 2.0f;
    states[2].Id = 3; states[2].Position = ImVec2(200.0f, 500.0f); states[2].GroupSize = ImVec2(350.0f, 250.0f);

    test::Session session(UnnamedConfig());
    const bool changed = session.Use([&] { return ed::SetNodeStates(states, 3); });
    Scene scene;
    scene.Place = false;
    session.DefaultContent = [&](int frame) { scene.Submit(frame); };
    session.Run(3);

    const auto one   = StateOf(session, 1);
    const auto two   = StateOf(session, 2);
    const auto three = StateOf(session, 3);
    const auto saved = session.Use([] { return std::string(ed::SaveSettingsToMemory()); });
    test::Check(changed, "SetNodeStates() takes state for nodes not created yet");
    test::Check(one.Position.x == 150.0f && one.Position.y == 50.0f, "node 1 starts where the document had it: (%g, %g)", one.Position.x, one.Position.y);
    test::Check(two.Position.x == 600.0f && two.Position.y == 80.0f && two.ZPosition == 2.0f, "node 2 too, with its z position: (%g, %g), z %g", two.Position.x, two.Position.y, two.ZPosition);
    test::Check(three.Position.x == 200.0f && three.Position.y == 500.0f && three.GroupSize.x == 350.0f && three.GroupSize.y == 250.0f,
        "group 3 too, with its size: (%g, %g), %g x %g", three.Position.x, three.Position.y, three.GroupSize.x, three.GroupSize.y);
    test::Check(saved.find(R"("node:1":{"location":{"x":150,"y":50}})") != std::string::npos, "the first save keeps the restored layout: %s", saved.c_str());
}

TEST_CASE(UndoDeletion, "state.undoing_a_deletion_restores_the_node", "EXB-6", test::TestFlags_None)
{
    test::Session session(UnnamedConfig());
    Scene scene;
    session.DefaultContent = [&](int frame) { scene.Submit(frame); };
    session.Run(3);
    const auto before = StateOf(session, 2);

    // The user deletes node 2, and the application records the step.
    session.Use([] { ed::DeleteNode(2); });
    bool accepted = false;
    for (int i = 0; i < 3 && !accepted; ++i)
    {
        session.Frame([&](int frame)
        {
            scene.Submit(frame);
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
        });
    }
    scene.Node2 = false;
    session.Run(2);
    const bool gone = !session.Use([] { ed::NodeState state; return ed::GetNodeState(2, &state); });

    // Undo: the application restores the node's state, then submits it again.
    const bool restored = session.Use([&] { return ed::SetNodeState(before); });
    scene.Node2 = true;
    session.Run(2);
    const auto after = StateOf(session, 2);
    test::Check(accepted && gone, "node 2 was deleted");
    test::Check(restored, "SetNodeState() takes the state of the deleted node");
    test::Check(after.Position.x == before.Position.x && after.Position.y == before.Position.y, "node 2 comes back where it was: (%g, %g), was (%g, %g)",
        after.Position.x, after.Position.y, before.Position.x, before.Position.y);
}

TEST_CASE(SetNodeStateDuringDrag, "state.set_node_state_during_a_drag_cancels_it", "BPN-11", test::TestFlags_None)
{
    // Nodes 1 and 2 are dragged together, and node 1 gets a state of its own.
    test::Session session(UnnamedConfig());
    Scene scene;
    session.DefaultContent = [&](int frame) { scene.Submit(frame); };
    session.Run(3);
    session.Use([] { ed::SelectNode(1); ed::SelectNode(2, true); });
    session.Run(1);

    const auto grab = session.Use([] { return ed::CanvasToScreen(ed::GetNodePosition(1) + ImVec2(5.0f, 5.0f)); });
    test::MouseMove(grab);
    session.Run(1);
    test::MouseButton(0, true);
    for (int step = 1; step <= 5; ++step)
    {
        test::MouseMove(grab + ImVec2(step * 10.0f, step * 10.0f));
        session.Run(1);
    }
    const auto dragged      = StateOf(session, 1);
    const auto draggedAlong = StateOf(session, 2);

    auto state = dragged;
    state.Position = ImVec2(700.0f, 400.0f);
    const bool changed = session.Use([&] { return ed::SetNodeState(state); });
    for (int step = 6; step <= 10; ++step)
    {
        test::MouseMove(grab + ImVec2(step * 10.0f, step * 10.0f));
        session.Run(1);
    }
    test::MouseButton(0, false);
    session.Run(3);

    const auto after  = StateOf(session, 1);
    const auto other  = StateOf(session, 2);
    test::Check(dragged.Position.x > 100.0f && draggedAlong.Position.x > 400.0f, "nodes 1 and 2 were being dragged: at (%g, %g) and (%g, %g)",
        dragged.Position.x, dragged.Position.y, draggedAlong.Position.x, draggedAlong.Position.y);
    test::Check(changed, "SetNodeState() changed node 1");
    test::Check(after.Position.x == 700.0f && after.Position.y == 400.0f, "node 1 stays where it was set, and the drag goes on no more: (%g, %g)", after.Position.x, after.Position.y);
    test::Check(other.Position.x == 400.0f && other.Position.y == 100.0f, "node 2 goes back to where the drag found it: (%g, %g)", other.Position.x, other.Position.y);
}

TEST_CASE(ViewStateRoundTrip, "state.view_state_round_trip", "BPN-9", test::TestFlags_None)
{
    // Before the first frame, the view is restored with the settings. The default
    // view shows the origin, far from this rectangle.
    test::Session session(UnnamedConfig());
    ed::ViewState requested;
    requested.VisibleMin = ImVec2(5200.0f, 3100.0f);
    requested.VisibleMax = ImVec2(5700.0f, 3400.0f);
    const bool early = session.Use([&] { return ed::SetViewState(requested); });
    Scene scene;
    session.DefaultContent = [&](int frame) { scene.Submit(frame); };
    session.Run(3);

    auto contains = [](const ed::ViewState& view, const ed::ViewState& rect)
    {
        return view.VisibleMin.x <= rect.VisibleMin.x + 0.5f && view.VisibleMin.y <= rect.VisibleMin.y + 0.5f
            && view.VisibleMax.x >= rect.VisibleMax.x - 0.5f && view.VisibleMax.y >= rect.VisibleMax.y - 0.5f;
    };
    auto view = session.Use([] { return ed::GetViewState(); });
    test::Check(early && contains(view, requested), "the view set before the first frame shows the rectangle: (%g, %g)-(%g, %g)",
        view.VisibleMin.x, view.VisibleMin.y, view.VisibleMax.x, view.VisibleMax.y);

    ed::ViewState moved;
    moved.VisibleMin = ImVec2(-1000.0f, -600.0f);
    moved.VisibleMax = ImVec2(1000.0f, 600.0f);
    const bool changed = session.Use([&] { return ed::SetViewState(moved); });
    const bool again   = session.Use([&] { return ed::SetViewState(moved); });
    session.Run(2);
    view = session.Use([] { return ed::GetViewState(); });
    test::Check(changed && !again, "SetViewState() reports the change once: %d, then %d", changed, again);
    test::Check(contains(view, moved), "the view shows the new rectangle: (%g, %g)-(%g, %g)", view.VisibleMin.x, view.VisibleMin.y, view.VisibleMax.x, view.VisibleMax.y);

    ed::ViewState tiny;
    tiny.VisibleMin = ImVec2(10.0f, 10.0f);
    tiny.VisibleMax = ImVec2(11.0f, 11.0f);
    session.Use([&] { ed::SetViewState(tiny); });
    session.Run(2);
    const auto zoom = session.Use([] { return ed::GetCurrentZoom(); });
    view = session.Use([] { return ed::GetViewState(); });
    test::Check(zoom == 8.0f, "the zoom stops at the last zoom level: %g", zoom);
    test::Check(std::fabs((view.VisibleMin.x + view.VisibleMax.x) * 0.5f - 10.5f) < 1.0f && std::fabs((view.VisibleMin.y + view.VisibleMax.y) * 0.5f - 10.5f) < 1.0f,
        "around the requested center: (%g, %g)-(%g, %g)", view.VisibleMin.x, view.VisibleMin.y, view.VisibleMax.x, view.VisibleMax.y);

    ed::ViewState empty;
    empty.VisibleMin = ImVec2(10.0f, 10.0f);
    empty.VisibleMax = ImVec2(10.0f, 20.0f);
    test::Check(!session.Use([&] { return ed::SetViewState(empty); }), "an empty rectangle is refused");
}

TEST_CASE(SetSelectionValidates, "state.set_selection_validates_input", "BPN-6", test::TestFlags_None)
{
    test::Session session(UnnamedConfig());
    Scene scene;
    session.DefaultContent = [&](int frame) { scene.Submit(frame); };
    session.Run(3);
    TakeChangeReason(session);

    const ed::NodeId nodes[] = { 1, 2, 99, 1 };
    const ed::LinkId links[] = { 10 };
    const bool changed = session.Use([&] { return ed::SetSelection(nodes, 4, links, 1); });
    const bool again   = session.Use([&] { return ed::SetSelection(nodes, 4, links, 1); });
    const bool nodesSelected = session.Use([] { return ed::IsNodeSelected(1) && ed::IsNodeSelected(2) && !ed::IsLinkSelected(10) && ed::GetSelectedObjectCount() == 2; });
    session.Run(1);
    const auto reason = TakeChangeReason(session);
    test::Check(changed && !again, "SetSelection() reports the change once: %d, then %d", changed, again);
    test::Check(nodesSelected, "nodes 1 and 2 are selected, node 99 and link 10 aren't");
    test::Check((reason & ed::SaveReasonFlags::Selection) == ed::SaveReasonFlags::Selection, "the settings record a selection change: 0x%02X", static_cast<unsigned>(reason));

    session.Use([&] { ed::SetSelection(nullptr, 0, links, 1); });
    test::Check(session.Use([] { return ed::IsLinkSelected(10) && ed::GetSelectedObjectCount() == 1; }), "link 10 alone is selected");

    // Node 2 is deleted, and selected again before the next frame frees it.
    session.Use([] { ed::DeleteNode(2); });
    bool accepted = false;
    for (int i = 0; i < 3 && !accepted; ++i)
    {
        session.Frame([&](int frame)
        {
            scene.Submit(frame);
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
        });
    }
    scene.Node2 = false;
    const ed::NodeId deleted[] = { 2 };
    session.Use([&] { ed::SetSelection(deleted, 1, nullptr, 0); });
    const auto count = session.Use([] { return ed::GetSelectedObjectCount(); });
    session.Run(2);
    test::Check(accepted, "node 2's deletion was accepted");
    test::Check(count == 0, "a node being deleted isn't selected: %d objects", count);
}

TEST_CASE(SetSelectionBeforeFirstFrame, "state.set_selection_before_the_first_frame", "BPN-2", test::TestFlags_None)
{
    // A document restores its selection before the editor has created its nodes.
    test::Session session(UnnamedConfig());
    const ed::NodeId nodes[] = { 2 };
    const bool changed = session.Use([&] { return ed::SetSelection(nodes, 1, nullptr, 0); });
    Scene scene;
    session.DefaultContent = [&](int frame) { scene.Submit(frame); };
    session.Run(3);
    test::Check(changed, "SetSelection() takes the selection before the first frame");
    test::Check(session.Use([] { return ed::IsNodeSelected(2) && ed::GetSelectedObjectCount() == 1; }), "node 2 is selected once it exists");
}
