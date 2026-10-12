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
