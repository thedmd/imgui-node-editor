//------------------------------------------------------------------------------
// Public API misuse.
//------------------------------------------------------------------------------
# include "harness.h"

TEST_CASE(DestroyCurrentEditor, "api.destroying_current_editor_clears_it", "API-8", test::TestFlags_None)
{
    test::Host host;
    test::Storage storage;
    const auto config = test::MakeConfig(&storage);
    auto first  = ed::CreateEditor(&config);
    auto second = ed::CreateEditor(&config);

    ed::SetCurrentEditor(first);
    ed::DestroyEditor(second);
    test::Check(ed::GetCurrentEditor() == first, "destroying another editor keeps the current one");

    ed::DestroyEditor(first);
    test::Check(ed::GetCurrentEditor() == nullptr, "destroying the current editor clears it");
    test::Check(ed::GetConfig().UserPointer == nullptr, "GetConfig() without a current editor returns an empty config");
}

TEST_CASE(SelectTwiceDeselectOnce, "api.node_selected_twice_deselected_once", "CORE-14", test::TestFlags_None)
{
    // The selection border is drawn magenta, which nothing else uses.
    const ImU32 selectionColor = IM_COL32(255, 0, 255, 0);
    test::Storage storage;
    test::Session session(&storage);
    session.DefaultContent = [&](int frame)
    {
        if (frame == 0)
        {
            ed::GetStyle().Colors[ed::StyleColor_SelNodeBorder] = ImColor(selectionColor | IM_COL32_A_MASK);
            ed::SetNodePosition(1, ImVec2(100.0f, 100.0f));
        }
        ed::BeginNode(1);
        ImGui::TextUnformatted("Node");
        ed::EndNode();
    };

    session.Run(3);
    session.Use([]
    {
        ed::SelectNode(1, true);
        ed::SelectNode(1, true);
        ed::DeselectNode(1);
    });
    session.Run(2);

    int selectionVertices = 0;
    for (auto color : test::Capture(session.DrawList).Colors)
        if ((color & ~IM_COL32_A_MASK) == selectionColor)
            ++selectionVertices;

    const bool selected = session.Use([] { return ed::IsNodeSelected(1); });
    const int  count    = session.Use([] { return ed::GetSelectedObjectCount(); });
    test::Check(!selected && count == 0, "node 1 not selected: IsNodeSelected() %d, %d selected objects", selected ? 1 : 0, count);
    test::Check(selectionVertices == 0, "node 1 not drawn as selected: %d selection border vertices", selectionVertices);
}

TEST_CASE(BackgroundDrawListOutsideFrame, "api.node_background_draw_list_between_frames", "API-13", test::TestFlags_None)
{
    test::Storage storage;
    test::Session session(&storage);
    ImDrawList* inFrame = nullptr;
    session.DefaultContent = [&](int frame)
    {
        if (frame == 0)
            ed::SetNodePosition(1, ImVec2(100.0f, 100.0f));
        ed::BeginNode(1);
        ImGui::TextUnformatted("Node");
        ed::EndNode();
        inFrame = ed::GetNodeBackgroundDrawList(1);
    };

    session.Run(3);
    ImDrawList* betweenFrames = session.Use([] { return ed::GetNodeBackgroundDrawList(1); });
    test::Check(inFrame != nullptr, "available after EndNode()");
    test::Check(betweenFrames == nullptr, "nullptr between frames");
}

TEST_CASE(PinSubmittedTwice, "api.pin_submitted_twice_in_a_node", "API-4", test::TestFlags_None)
{
    // One pin ID used for an input and an output of the same node.
    test::ExpectAsserts(4);
    test::Storage storage;
    test::Session session(&storage);
    ImVec2 nodeCenter;
    session.DefaultContent = [&](int frame)
    {
        if (frame == 0)
            ed::SetNodePosition(1, ImVec2(100.0f, 100.0f));
        ed::BeginNode(1);
        ImGui::TextUnformatted("Node");
        ed::BeginPin(2, ed::PinKind::Input);
        ImGui::TextUnformatted("> in");
        ed::EndPin();
        ed::BeginPin(2, ed::PinKind::Output);
        ImGui::TextUnformatted("out >");
        ed::EndPin();
        ed::EndNode();
        nodeCenter = ed::CanvasToScreen(ed::GetNodePosition(1) + ed::GetNodeSize(1) * 0.5f);
    };

    // The editor walks the node's pins while the mouse is over it.
    session.Run(2);
    test::MouseMove(nodeCenter);
    session.Run(2);
    test::Check(true, "ed::End() returns with the mouse over the node");
}

TEST_CASE(NodePaddingScopedToBegin, "api.node_padding_scoped_to_begin_or_end_node", "API-5", test::TestFlags_None)
{
    // ImGui's idiom of scoping a style var to the one call it affects.
    test::Storage storage;
    test::Session session(&storage);
    ImVec2 sizes[2];
    session.DefaultContent = [&](int frame)
    {
        if (frame == 0)
        {
            ed::SetNodePosition(1, ImVec2(100.0f, 100.0f));
            ed::SetNodePosition(2, ImVec2(400.0f, 100.0f));
        }

        ed::PushStyleVar(ed::StyleVar_NodePadding, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ed::BeginNode(1);
        ed::PopStyleVar();
        ImGui::TextUnformatted("Node");
        ed::EndNode();
        sizes[0] = ed::GetNodeSize(1);

        ed::BeginNode(2);
        ImGui::TextUnformatted("Node");
        ed::PushStyleVar(ed::StyleVar_NodePadding, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ed::EndNode();
        ed::PopStyleVar();
        sizes[1] = ed::GetNodeSize(2);
    };

    session.Run(3);
    const ImVec2 textSize = ImGui::CalcTextSize("Node");
    test::Check(sizes[0].x == ImFloor(textSize.x) && sizes[0].y == ImFloor(textSize.y), "node 1 has no padding: %g x %g", sizes[0].x, sizes[0].y);
    test::Check(sizes[1].x > textSize.x && sizes[1].y > textSize.y, "node 2 has the default padding: %g x %g", sizes[1].x, sizes[1].y);
}

TEST_CASE(DrawListsInsideNode, "api.node_draw_lists_requested_inside_a_node", "API-6", test::TestFlags_None)
{
    // Three frames, each asking for a node background draw list and a group hint inside a node.
    test::ExpectAsserts(6);
    test::Storage storage;
    test::Session session(&storage);
    ImDrawList* inside = nullptr;
    ImDrawList* after  = nullptr;
    bool        hint   = true;
    session.DefaultContent = [&](int frame)
    {
        if (frame == 0)
            ed::SetNodePosition(1, ImVec2(100.0f, 100.0f));
        ed::BeginNode(1);
        ImGui::TextUnformatted("Node");
        inside = ed::GetNodeBackgroundDrawList(1);
        hint   = ed::BeginGroupHint(1);
        ed::EndNode();
        after = ed::GetNodeBackgroundDrawList(1);
    };

    session.Run(3);
    test::Check(inside == nullptr && !hint, "nothing returned inside the node: draw list %p, group hint %d", (void*)inside, hint ? 1 : 0);
    test::Check(after != nullptr, "background draw list available after EndNode()");
}

TEST_CASE(CallWithoutEditor, "api.call_without_current_editor_asserts", "API-8", test::TestFlags_None)
{
    test::ExpectAsserts(1);
    test::Host host;
    ed::SetCurrentEditor(nullptr);

    bool asserted = false;
    try
    {
        test::ThrowOnAssert throwOnAssert;
        ed::GetStyle();
    }
    catch (const test::AssertFailure&)
    {
        asserted = true;
    }
    test::Check(asserted, "ed::GetStyle() without a current editor asserts");
}
