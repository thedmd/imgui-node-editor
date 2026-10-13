//------------------------------------------------------------------------------
// Nodes an application drops from its model without the delete action.
//------------------------------------------------------------------------------
# include "harness.h"
# include <cfloat>
# include <string>

namespace {

void SubmitNode(uintptr_t id, bool place)
{
    if (place)
        ed::SetNodePosition(id, ImVec2(id == 1 ? 100.0f : 400.0f, 100.0f));
    ed::BeginNode(id);
    ImGui::TextUnformatted("Node");
    ed::BeginPin(id + 1, ed::PinKind::Input);
    ImGui::TextUnformatted("> in");
    ed::EndPin();
    ImGui::SameLine();
    ed::BeginPin(id + 2, ed::PinKind::Output);
    ImGui::TextUnformatted("out >");
    ed::EndPin();
    ed::EndNode();
}

// Nodes 1 and 11 with an input and an output pin each (node ID + 1 and + 2), and
// link 100 from the output of node 1 to the input of node 11, placed in frame 0.
struct TwoNodes
{
    bool Node1   = true;
    bool Node11  = true;
    bool Link100 = true;

    void Submit(int frame)
    {
        if (Node1)
            SubmitNode(1, frame == 0);
        if (Node11)
            SubmitNode(11, frame == 0);
        if (Link100)
            ed::Link(100, 3, 12);
    }
};

std::string SavedSettings(test::Session& session)
{
    return session.Use([] { return std::string(ed::SaveSettingsToMemory()); });
}

} // namespace

TEST_CASE(ForgetNodeReusedId, "forget.reused_node_id_starts_fresh", "CORE-7", test::TestFlags_None)
{
    // The application drops node 1 and its link, with its own state for them, and
    // later reuses the ID. The editor kept the node selected, saved and placed.
    test::Storage storage;
    test::Session session(&storage);
    TwoNodes scene;
    session.DefaultContent = [&](int frame)
    {
        scene.Submit(frame);
        if (frame == 1)
        {
            ed::SelectNode(1);
            ed::SelectLink(100, true);
        }
    };
    session.Run(3);

    scene.Node1 = scene.Link100 = false;
    storage.Nodes.erase(1);
    const auto forgotten = session.Use([] { return ed::ForgetNode(1); });
    const auto settings  = SavedSettings(session);
    const auto selected  = session.Use([] { return ed::GetSelectedObjectCount(); });
    const auto position  = session.Use([] { return ed::GetNodePosition(1); });
    test::Check(forgotten, "ForgetNode(1) knew the node");
    test::Check(settings.find(R"("node:1")") == std::string::npos && settings.find(R"("link:100")") == std::string::npos,
        "saved settings leave out node 1 and link 100: %s", settings.c_str());
    test::Check(selected == 0 && position.x == FLT_MAX, "node 1 and link 100 aren't selected (%d objects), and node 1 is unknown", selected);

    session.Run(2);
    scene.Node1 = true;
    session.Run(2);
    const auto reused     = session.Use([] { return ed::GetNodePosition(1); });
    const auto isSelected = session.Use([] { return ed::IsNodeSelected(1); });
    test::Check(reused.x == 0.0f && reused.y == 0.0f && !isSelected, "the reused ID starts at the origin, unselected: (%g, %g), %s",
        reused.x, reused.y, isSelected ? "selected" : "not selected");
    test::Check(!session.Use([] { return ed::ForgetNode(5); }), "ForgetNode(5) doesn't know a node never seen");
}

TEST_CASE(ForgetNodeFromSettings, "forget.node_known_only_from_settings", "CORE-7", test::TestFlags_None)
{
    // Node 5 has saved state, and the application drops it before submitting it.
    const char* saved = R"({"nodes":{"node:5":{"location":{"x":300,"y":200}}}})";
    ImVec2 position[2];
    for (int forget = 0; forget < 2; ++forget)
    {
        test::Storage storage;
        storage.Global = saved;
        test::Session session(&storage);
        session.Run(2);
        if (forget)
            test::Check(session.Use([] { return ed::ForgetNode(5); }), "ForgetNode(5) knew its saved state");
        session.DefaultContent = [](int) { SubmitNode(5, false); };
        session.Run(2);
        position[forget] = session.Use([] { return ed::GetNodePosition(5); });
    }
    test::Check(position[0].x == 300.0f && position[0].y == 200.0f, "kept, node 5 starts where it was saved: (%g, %g)", position[0].x, position[0].y);
    test::Check(position[1].x == 0.0f && position[1].y == 0.0f, "forgotten, it starts at the origin: (%g, %g)", position[1].x, position[1].y);
}

TEST_CASE(ForgetDraggedNode, "forget.node_while_it_is_dragged", "CORE-7", test::TestFlags_NeedsSanitizer)
{
    // The drag action holds the node, and the selection holds it too, across frames.
    test::Storage storage;
    test::Session session(&storage);
    TwoNodes scene;
    session.DefaultContent = [&](int frame) { scene.Submit(frame); };
    session.Run(3);

    const auto grab = session.Use([] { return ed::CanvasToScreen(ed::GetNodePosition(1) + ImVec2(5.0f, 5.0f)); });
    test::MouseMove(grab);
    session.Run(1);
    test::MouseButton(0, true);
    for (int step = 1; step <= 10; ++step)
    {
        if (step == 6)
        {
            const auto dragged = session.Use([] { return ed::GetNodePosition(1); });
            test::Check(dragged.x > 100.0f, "node 1 is being dragged: at (%g, %g)", dragged.x, dragged.y);
            scene.Node1 = scene.Link100 = false;
            session.Use([] { ed::ForgetNode(1); });
        }
        test::MouseMove(grab + ImVec2(step * 10.0f, 0.0f));
        session.Run(1);
    }
    test::MouseButton(0, false);
    session.Run(3);

    test::Check(session.Use([] { return ed::GetNodePosition(1).x == FLT_MAX && ed::GetSelectedObjectCount() == 0; }), "node 1 stays forgotten after the drag");
}

TEST_CASE(ClearStateNewDocument, "forget.clear_state_starts_a_new_document", "CORE-7", test::TestFlags_None)
{
    // A document with a zoomed and scrolled view, and a selection, is replaced by
    // one that reuses its IDs.
    test::Storage storage;
    storage.Global = R"({"view":{"scroll":{"x":-50,"y":-30},"zoom":2}})";
    test::Session session(&storage);
    TwoNodes scene;
    session.DefaultContent = [&](int frame)
    {
        scene.Submit(frame);
        if (frame == 1)
        {
            ed::SelectNode(1);
            ed::SelectLink(100, true);
        }
    };
    session.Run(3);
    const auto zoomBefore = session.Use([] { return ed::GetCurrentZoom(); });

    storage.Nodes.clear();
    session.Use([] { ed::ClearState(); });
    const auto settings = SavedSettings(session);
    test::Check(settings.find("node:") == std::string::npos && settings.find("link:") == std::string::npos && settings.find(R"("zoom":1)") != std::string::npos,
        "saved settings hold no nodes, no selection and the default view: %s", settings.c_str());
    test::Check(session.Use([] { return ed::GetNodePosition(1).x == FLT_MAX && ed::GetNodePosition(11).x == FLT_MAX && ed::GetSelectedObjectCount() == 0; }),
        "the nodes and the selection are gone");

    session.Run(3);
    const auto reused   = session.Use([] { return ed::GetNodePosition(11); });
    const auto zoom     = session.Use([] { return ed::GetCurrentZoom(); });
    const auto selected = session.Use([] { return ed::GetSelectedObjectCount(); });
    test::Check(zoomBefore == 2.0f && zoom == 1.0f, "the view zoom goes from %g to %g", zoomBefore, zoom);
    test::Check(reused.x == 0.0f && reused.y == 0.0f && selected == 0, "a reused ID starts at the origin, unselected: (%g, %g), %d selected", reused.x, reused.y, selected);
}

TEST_CASE(ClearStateBoundsGrowth, "forget.clear_state_keeps_settings_bounded", "PERF-3", test::TestFlags_None)
{
    // Documents of 10 nodes, each with fresh IDs, as in the review's growth benchmark.
    size_t first[2] = {}, last[2] = {};
    for (int clear = 0; clear < 2; ++clear)
    {
        test::Storage storage;
        test::Session session(&storage);
        int document = 0;
        session.DefaultContent = [&](int)
        {
            for (int i = 0; i < 10; ++i)
                SubmitNode(1 + (document * 10 + i) * 4, false);
        };
        for (document = 0; document < 30; ++document)
        {
            if (clear && document > 0)
                session.Use([] { ed::ClearState(); });
            session.Run(3);
            if (document == 0)
                first[clear] = storage.Global.size();
            last[clear] = storage.Global.size();
        }
    }
    test::Check(last[0] > first[0] * 10, "without ClearState(), settings grow with every document: %d to %d bytes", (int)first[0], (int)last[0]);
    test::Check(last[1] < first[1] * 2, "with it, they stay one document's: %d to %d bytes", (int)first[1], (int)last[1]);
}

TEST_CASE(ClearStateInFrame, "forget.clear_state_between_begin_and_end_asserts", "CORE-7", test::TestFlags_None)
{
    // Without the application's own state for node 1, only the kept node is still placed.
    test::ExpectAsserts(1);
    test::Storage storage;
    test::Session session(&storage);
    TwoNodes scene;
    session.DefaultContent = [&](int frame)
    {
        scene.Submit(frame);
        if (frame == 2)
        {
            storage.Nodes.clear();
            ed::ClearState();
        }
    };
    session.Run(4);

    const auto position = session.Use([] { return ed::GetNodePosition(1); });
    test::Check(position.x == 100.0f, "ClearState() is ignored there: node 1 kept at (%g, %g)", position.x, position.y);
}

TEST_CASE(ForgetNodeInFrame, "forget.node_between_begin_and_end_asserts", "CORE-7", test::TestFlags_None)
{
    // Freeing a node that may be submitted in the same frame isn't supported.
    test::ExpectAsserts(1);
    test::Storage storage;
    test::Session session(&storage);
    TwoNodes scene;
    bool forgotten = true;
    session.DefaultContent = [&](int frame)
    {
        scene.Submit(frame);
        if (frame == 2)
            forgotten = ed::ForgetNode(1);
    };
    session.Run(4);

    const auto position = session.Use([] { return ed::GetNodePosition(1); });
    test::Check(!forgotten && position.x == 100.0f, "ForgetNode() is ignored there: node 1 kept at (%g, %g)", position.x, position.y);
}
