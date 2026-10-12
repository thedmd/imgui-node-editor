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

TEST_CASE(NodeSizeIndependentOfPosition, "geometry.node_size_independent_of_position", "CORE-17", test::TestFlags_None)
{
    test::Check(IsNodeSizeIndependentOfPosition(ImVec2(100.5f, 20.0f)), "node size is the same at every position");
}

// ImGui advances lines to whole pixels by truncation, which rounds toward zero, so
// a line of fractional height ends 1 px lower below 0 than above it.
TEST_CASE(NodeHeightWithFractionalLines, "geometry.node_height_with_fractional_lines", "CORE-17", test::TestFlags_KnownFailure)
{
    test::Check(IsNodeSizeIndependentOfPosition(ImVec2(100.5f, 20.25f)), "node size is the same at every position");
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

TEST_CASE(SizeWhileDraggedAcrossZero, "geometry.size_kept_while_dragged_across_zero", "CORE-17", test::TestFlags_None)
{
    // Node 1 has content of a fractional width, group 2 a fractional group width.
    // The view shows (0, 0) near the middle of the editor, so the drag stays inside.
    test::Storage storage;
    storage.Global = R"({"view":{"scroll":{"x":-400,"y":-300},"zoom":1}})";
    test::Session session(&storage);
    ImVec2 titleOnScreen, sizes[2];
    session.DefaultContent = [&](int frame)
    {
        if (frame == 0)
        {
            ed::SetNodePosition(1, ImVec2(50.0f, 50.0f));
            ed::SetNodePosition(2, ImVec2(150.0f, 50.0f));
        }
        ed::BeginNode(1);
        ImGui::TextUnformatted("Node");
        titleOnScreen = ed::CanvasToScreen((ImGui::GetItemRectMin() + ImGui::GetItemRectMax()) * 0.5f);
        ImGui::Dummy(ImVec2(100.5f, 20.0f));
        ed::EndNode();
        ed::BeginNode(2);
        ImGui::TextUnformatted("Group");
        ed::Group(ImVec2(150.5f, 80.0f));
        ed::EndNode();
        sizes[0] = ed::GetNodeSize(1);
        sizes[1] = ed::GetNodeSize(2);
    };
    session.Run(3);
    session.Use([] { ed::SelectNode(1); ed::SelectNode(2, true); });
    session.Run(1);
    const ImVec2 before[2] = { sizes[0], sizes[1] };

    // Both selected nodes move by (-400, -300), to lie entirely below 0. Alt keeps them off the grid.
    int changed = 0;
    ImGui::GetIO().AddKeyEvent(ImGuiMod_Alt, true);
    const auto press = titleOnScreen;
    test::MouseMove(press);
    session.Run(2);
    test::MouseButton(0, true);
    session.Run(2);
    for (int i = 1; i <= 50; ++i)
    {
        test::MouseMove(press + ImVec2(-8.0f * i, -6.0f * i));
        session.Run(1);
        for (int n = 0; n < 2; ++n)
            if (sizes[n].x != before[n].x || sizes[n].y != before[n].y)
                ++changed;
    }
    test::MouseButton(0, false);
    ImGui::GetIO().AddKeyEvent(ImGuiMod_Alt, false);
    session.Run(2);
    const auto position = session.Use([] { return ed::GetNodePosition(2); });

    test::Check(position.x + sizes[1].x < 0.0f && position.y + sizes[1].y < 0.0f, "group 2 dragged entirely below 0: at (%g, %g)", position.x, position.y);
    test::Check(changed == 0, "node and group sizes kept while dragged across 0: %d changes, now (%g, %g) and (%g, %g), were (%g, %g) and (%g, %g)",
        changed, sizes[0].x, sizes[0].y, sizes[1].x, sizes[1].y, before[0].x, before[0].y, before[1].x, before[1].y);
}
