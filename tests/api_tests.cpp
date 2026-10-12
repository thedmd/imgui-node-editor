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
