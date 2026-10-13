//------------------------------------------------------------------------------
// Node geometry.
//------------------------------------------------------------------------------
# include "harness.h"
# include <cstdio>

namespace {

// Node 1 placed at (x, x) with content of 'contentSize', measured on the third frame.
ImVec2 NodeSizeAt(float x, ImVec2 contentSize)
{
    test::Storage storage;
    test::Session session(&storage);
    for (int i = 0; i < 3; ++i)
        session.Frame([x, contentSize](int frame)
        {
            if (frame == 0)
                ed::SetNodePosition(1, ImVec2(x, x));
            ed::BeginNode(1);
            ImGui::Dummy(contentSize);
            ed::EndNode();
        });
    return session.Use([] { return ed::GetNodeSize(1); });
}

// Whether the node measures the same at positions on both sides of 0.
bool IsNodeSizeIndependentOfPosition(ImVec2 contentSize)
{
    const float positions[] = { -1000.25f, -300.5f, -300.0f, -0.5f, 0.0f, 0.5f, 300.0f, 300.5f, 1000.25f };

    const auto reference = NodeSizeAt(positions[0], contentSize);
    bool same = true;
    for (float x : positions)
    {
        const auto size = NodeSizeAt(x, contentSize);
        printf("    position (%g, %g): size (%g, %g)\n", x, x, size.x, size.y);
        same = same && size.x == reference.x && size.y == reference.y;
    }
    return same;
}

} // namespace

TEST_CASE(NodeSizeIndependentOfPosition, "geometry.node_size_independent_of_position", "CORE-17", test::TestFlags_KnownFailure)
{
    test::Check(IsNodeSizeIndependentOfPosition(ImVec2(100.5f, 20.0f)), "node size is the same at every position");
}

namespace {

// Node 1 at (400, 300) holds a button. Node 2, submitted after it at (100, 100),
// has no items of its own, apart from an empty pin when 'withPin' is set.
struct ItemlessScene
{
    bool WithPin = false;

    void Submit(int frame)
    {
        if (frame == 0)
        {
            ed::SetNodePosition(1, ImVec2(400.0f, 300.0f));
            ed::SetNodePosition(2, ImVec2(100.0f, 100.0f));
        }
        ed::BeginNode(1);
        ImGui::Button("Button", ImVec2(120.0f, 40.0f));
        ed::EndNode();

        ed::BeginNode(2);
        if (WithPin)
        {
            ed::BeginPin(21, ed::PinKind::Input);
            ed::EndPin();
            ImGui::TextUnformatted("Node");
        }
        ed::EndNode();
    }
};

} // namespace

TEST_CASE(ItemlessNode, "geometry.itemless_node_keeps_its_size", "API-10", test::TestFlags_None)
{
    test::Storage storage;
    test::Session session(&storage);
    ItemlessScene scene;
    session.DefaultContent = [&](int frame) { scene.Submit(frame); };
    session.Run(3);

    const auto size    = session.Use([] { return ed::GetNodeSize(2); });
    const auto padding = ed::Style().NodePadding;
    test::Check(size.x == padding.x + padding.z && size.y == padding.y + padding.w, "a node without items, after a node at its bottom right, measures its padding: (%g, %g)", size.x, size.y);
}

TEST_CASE(ItemlessPin, "geometry.itemless_pin_has_no_area", "API-10", test::TestFlags_None)
{
    test::Storage storage;
    test::Session session(&storage);
    ItemlessScene scene;
    scene.WithPin = true;
    session.DefaultContent = [&](int frame) { scene.Submit(frame); };
    session.Run(3);

    // Between the two nodes, where a pin reaching node 1's button would be.
    test::MouseMove(session.Use([] { return ed::CanvasToScreen(ImVec2(300.0f, 250.0f)); }));
    session.Run(2);

    const auto hovered = session.Use([] { return ed::GetHoveredPin(); });
    test::Check(!hovered, "the empty pin of node 2 doesn't reach toward node 1: pin %d hovered between the nodes", (int)hovered.Get());
}

TEST_CASE(CornerOfCanvas, "geometry.node_in_the_canvas_corner_is_usable", "CNV-8", test::TestFlags_None)
{
    // Node 1 and group 10 in the top left corner of the canvas, where mouse positions
    // are still valid for ImGui.
    const float extent = 250000.0f;
    char state[128];
    snprintf(state, sizeof(state), R"({"view":{"scroll":{"x":%g,"y":%g},"zoom":1}})", -extent - 100.0f, -extent - 100.0f);
    test::Storage storage;
    storage.Global = state;
    test::Session session(&storage);
    session.DefaultContent = [extent](int frame)
    {
        if (frame == 0)
        {
            ed::SetNodePosition(1, ImVec2(-extent, -extent));
            ed::SetNodePosition(10, ImVec2(-extent + 300.0f, -extent));
        }
        ed::BeginNode(1);
        ImGui::Text("Corner");
        ImGui::Dummy(ImVec2(80.0f, 20.0f));
        ed::EndNode();
        ed::BeginNode(10);
        ImGui::Text("Group");
        ed::Group(ImVec2(200.0f, 100.0f));
        ed::EndNode();
    };
    session.Run(3);

    const auto inside = session.Use([&] { return ed::CanvasToScreen(ImVec2(-extent + 10.0f, -extent + 8.0f)); });
    test::MouseMove(inside);
    session.Run(2);
    const bool hovered = session.Use([] { return ed::GetHoveredNode() == ed::NodeId(1); });

    session.Click(inside);
    session.Run(2);
    const bool selected = session.Use([] { return ed::IsNodeSelected(1); });

    session.Drag(inside, inside + ImVec2(64.0f, 48.0f));
    session.Run(2);
    const auto moved = session.Use([] { return ed::GetNodePosition(1); });

    const auto groupSize = session.Use([] { return ed::GetNodeSize(10); });
    const auto edge      = session.Use([&] { return ed::CanvasToScreen(ImVec2(-extent + 300.0f + groupSize.x - 2.0f, -extent + groupSize.y * 0.5f)); });
    session.Drag(edge, edge + ImVec2(64.0f, 0.0f));
    session.Run(2);
    const auto resized = session.Use([] { return ed::GetNodeSize(10); });

    test::Check(hovered, "the node in the corner is hovered");
    test::Check(selected, "a click selects it");
    test::Check(moved.x == -extent + 64.0f && moved.y == -extent + 48.0f, "a drag moves it: to (%g, %g)", moved.x, moved.y);
    test::Check(resized.x > groupSize.x, "a drag on its edge resizes the group next to it: %g wide, was %g", resized.x, groupSize.x);
}
