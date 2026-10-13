//------------------------------------------------------------------------------
// Group nodes and the nodes they contain.
//------------------------------------------------------------------------------
# include "harness.h"
# include <vector>

namespace {

void SubmitGroup(uintptr_t id, ImVec2 position, ImVec2 size, int frame)
{
    if (frame == 0)
        ed::SetNodePosition(id, position);
    ed::BeginNode(id);
    ImGui::Text("Group %d", (int)id);
    ed::Group(size);
    ed::EndNode();
}

void SubmitNode(uintptr_t id, ImVec2 position, int frame)
{
    if (frame == 0)
        ed::SetNodePosition(id, position);
    ed::BeginNode(id);
    ImGui::Text("Node %d", (int)id);
    ed::EndNode();
}

// G1 contains G2, which contains N. 'order' lists the nodes in submission order.
void NestedGroups(int frame, const std::vector<int>& order)
{
    for (int id : order)
    {
        if (id == 1)
            SubmitGroup(1, ImVec2(100.0f, 100.0f), ImVec2(500.0f, 400.0f), frame);
        else if (id == 2)
            SubmitGroup(2, ImVec2(150.0f, 150.0f), ImVec2(300.0f, 250.0f), frame);
        else
            SubmitNode(3, ImVec2(200.0f, 200.0f), frame);
    }
}

void CheckCenteringMovesNestedContent(const std::vector<int>& order)
{
    test::Storage storage;
    test::Session session(&storage);
    session.DefaultContent = [&](int frame) { NestedGroups(frame, order); };

    session.Run(3);
    ImVec2 before[3];
    session.Use([&] { for (int i = 0; i < 3; ++i) before[i] = ed::GetNodePosition(i + 1); });
    session.Use([] { ed::CenterNodeOnScreen(1); });
    session.Run(3);
    ImVec2 offset[3];
    session.Use([&] { for (int i = 0; i < 3; ++i) offset[i] = ed::GetNodePosition(i + 1) - before[i]; });

    test::Check(offset[0].x != 0.0f || offset[0].y != 0.0f, "group 1 moved by (%g, %g)", offset[0].x, offset[0].y);
    test::Check(offset[1].x == offset[0].x && offset[1].y == offset[0].y && offset[2].x == offset[0].x && offset[2].y == offset[0].y,
        "group 2 and node 3 moved with it: (%g, %g) and (%g, %g)", offset[1].x, offset[1].y, offset[2].x, offset[2].y);
}

} // namespace

TEST_CASE(CenterGroupContentAfter, "groups.centering_moves_content_submitted_after_the_group", "CORE-8", test::TestFlags_None)
{
    CheckCenteringMovesNestedContent({ 1, 2, 3 });
}

TEST_CASE(CenterGroupContentBefore, "groups.centering_moves_content_submitted_before_the_group", "CORE-8", test::TestFlags_None)
{
    CheckCenteringMovesNestedContent({ 3, 2, 1 });
}

TEST_CASE(GroupInsideItself, "groups.group_inside_its_own_group_area", "INT-11", test::TestFlags_None)
{
    // Without padding and other content, the node's bounds equal its group area.
    test::Storage storage;
    test::Session session(&storage);
    session.DefaultContent = [&](int frame)
    {
        ed::PushStyleVar(ed::StyleVar_NodePadding, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        if (frame == 0)
            ed::SetNodePosition(1, ImVec2(100.0f, 100.0f));
        ed::BeginNode(1);
        ed::Group(ImVec2(300.0f, 200.0f));
        ed::EndNode();
        ed::PopStyleVar();
        SubmitNode(2, ImVec2(150.0f, 150.0f), frame);
    };

    session.Run(3);
    session.Use([] { ed::SelectNode(1); ed::CenterNodeOnScreen(1); });
    session.Run(3);

    // Copying a selected group collects its content too.
    session.Click(ImVec2(900.0f, 600.0f));
    session.Use([] { ed::SelectNode(1); });
    session.PressKey(ImGuiKey_C, true);

    const ImVec2 position = session.Use([] { return ed::GetNodePosition(2); });
    test::Check(position.x != 150.0f || position.y != 150.0f, "node 2 moved with the group: (%g, %g)", position.x, position.y);
}

TEST_CASE(SmallGroupRegions, "groups.small_group_regions_reach_the_minimum_size", "CORE-17", test::TestFlags_None)
{
    // A 26 x 26 group grows its regions to the 30 x 30 minimum: 2 px beyond each side.
    test::Storage storage;
    test::Session session(&storage);
    ImVec2 min, max;
    session.DefaultContent = [&](int frame)
    {
        if (frame == 0)
            ed::SetNodePosition(1, ImVec2(100.0f, 100.0f));
        ed::BeginNode(1);
        ed::Group(ImVec2(10.0f, 10.0f));
        ed::EndNode();
        min = ed::CanvasToScreen(ed::GetNodePosition(1));
        max = ed::CanvasToScreen(ed::GetNodePosition(1) + ed::GetNodeSize(1));
    };
    session.Run(3);

    auto hoveredAt = [&](float x)
    {
        test::MouseMove(ImVec2(x, (min.y + max.y) * 0.5f));
        session.Run(2);
        return session.Use([] { return ed::GetHoveredNode(); });
    };
    const auto inside  = hoveredAt(max.x + 1.0f);
    const auto outside = hoveredAt(max.x + 3.0f);

    test::Check(max.x - min.x == 26.0f, "the group is 26 wide: %g", max.x - min.x);
    test::Check(inside == ed::NodeId(1) && !outside, "its right edge reaches 2 px out: hovered 1 px out %d, 3 px out %d", (int)inside.Get(), (int)outside.Get());
}
