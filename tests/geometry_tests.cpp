//------------------------------------------------------------------------------
// Node geometry.
//------------------------------------------------------------------------------
# include "harness.h"
# include <cstdio>

TEST_CASE(NodeSizeIndependentOfPosition, "geometry.node_size_independent_of_position", "CORE-17", test::TestFlags_KnownFailure)
{
    // Content with a fractional size, placed on both sides of 0.
    const float positions[] = { -1000.25f, -300.5f, -300.0f, -0.5f, 0.0f, 0.5f, 300.0f, 300.5f, 1000.25f };

    ImVec2 reference;
    bool   same  = true;
    bool   first = true;
    for (float x : positions)
    {
        test::Storage storage;
        test::Session session(&storage);
        for (int i = 0; i < 3; ++i)
            session.Frame([x](int frame)
            {
                if (frame == 0)
                    ed::SetNodePosition(1, ImVec2(x, x));
                ed::BeginNode(1);
                ImGui::Dummy(ImVec2(100.5f, 20.25f));
                ed::EndNode();
            });

        const auto size = session.Use([] { return ed::GetNodeSize(1); });
        printf("    position (%g, %g): size (%g, %g)\n", x, x, size.x, size.y);
        if (first)
            reference = size;
        else if (size.x != reference.x || size.y != reference.y)
            same = false;
        first = false;
    }

    test::Check(same, "node size is the same at every position");
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
