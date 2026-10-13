//------------------------------------------------------------------------------
// Public API misuse.
//------------------------------------------------------------------------------
# include "harness.h"
# include <limits>

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

TEST_CASE(StyleStackMisuse, "api.style_stack_misuse_asserts", "API-7", test::TestFlags_None)
{
    // An extra pop of each stack, and a float pushed to the ImVec4 NodePadding.
    test::ExpectAsserts(3);
    test::Storage storage;
    test::Session session(&storage);
    ImVec4 padding;
    session.Frame([&](int)
    {
        ed::PopStyleColor();
        ed::PopStyleVar();
        ed::PushStyleVar(ed::StyleVar_NodePadding, 1.0f);
        padding = ed::GetStyle().NodePadding;
    });

    const ImVec4 defaultPadding = ed::Style().NodePadding;
    test::Check(padding.x == defaultPadding.x && padding.y == defaultPadding.y && padding.z == defaultPadding.z && padding.w == defaultPadding.w,
        "NodePadding unchanged by the wrong push: (%g, %g, %g, %g)", padding.x, padding.y, padding.z, padding.w);
}

TEST_CASE(StyleStackUnbalanced, "api.style_stack_unbalanced_in_frame_asserts", "API-7", test::TestFlags_None)
{
    // Pushed before ed::Begin() and popped after ed::End() is fine; a push left inside is not.
    test::ExpectAsserts(1);
    test::Storage storage;
    test::Session session(&storage);
    session.Use([] { ed::PushStyleColor(ed::StyleColor_Bg, ImVec4(1.0f, 0.0f, 0.0f, 1.0f)); });
    session.Frame([](int) {});
    session.Use([] { ed::PopStyleColor(); });
    session.Frame([](int) { ed::PushStyleVar(ed::StyleVar_NodeRounding, 2.0f); });
    session.Use([] { ed::PopStyleVar(); });
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

TEST_CASE(NonFinitePositions, "api.non_finite_positions_are_ignored", "CORE-17", test::TestFlags_None)
{
    test::Storage storage;
    test::Session session(&storage);
    session.DefaultContent = [](int frame)
    {
        if (frame == 0)
        {
            ed::SetNodePosition(1, ImVec2(100.0f, 50.0f));
            ed::SetGroupSize(1, ImVec2(200.0f, 120.0f));
        }
        ed::BeginNode(1);
        ImGui::TextUnformatted("Group");
        ed::Group(ImVec2(200.0f, 120.0f));
        ed::EndNode();
    };
    session.Run(3);
    const auto before = session.Use([] { return ed::GetNodePosition(1); });

    session.Use([]
    {
        const auto nan = std::numeric_limits<float>::quiet_NaN();
        const auto inf = std::numeric_limits<float>::infinity();
        ed::SetNodePosition(1, ImVec2(nan, 0.0f));
        ed::SetNodePosition(1, ImVec2(0.0f, inf));
        ed::SetNodePosition(2, ImVec2(-inf, nan));
        ed::SetGroupSize(1, ImVec2(nan, nan));
    });
    session.Run(2);

    const auto after = session.Use([] { return ed::GetNodePosition(1); });
    const auto other = session.Use([] { return ed::GetNodePosition(2); });
    test::Check(after.x == before.x && after.y == before.y, "node 1 stays at (%g, %g): at (%g, %g)", before.x, before.y, after.x, after.y);
    test::Check(other.x == FLT_MAX && other.y == FLT_MAX, "node 2 isn't created: GetNodePosition() reports (%g, %g)", other.x, other.y);
}

TEST_CASE(NodeIdScope, "api.widgets_in_nodes_have_node_scoped_ids", "API-12", test::TestFlags_None)
{
    // Nodes 1 and 2 each have a button labeled "X"; the mouse is over node 1's.
    test::Storage storage;
    test::Session session(&storage);
    ImGuiID inside = 0, outside = 0;
    ImVec2 button;
    session.DefaultContent = [&](int frame)
    {
        for (int id : { 1, 2 })
        {
            if (frame == 0)
                ed::SetNodePosition(id, ImVec2(id * 200.0f, 100.0f));
            ed::BeginNode(id);
            ImGui::Button("X");
            if (id == 1)
            {
                inside = ImGui::GetID("X");
                button = ImGui::GetItemRectMin() + ImGui::GetItemRectSize() * 0.5f;
            }
            ed::EndNode();
        }
        outside = ImGui::GetID("X");
    };
    session.Run(2);
    test::MouseMove(session.Use([&] { return ed::CanvasToScreen(button); }));
    session.Run(3);

    test::Check(inside != outside, "GetID() inside a node differs from outside: 0x%08X and 0x%08X", inside, outside);
# if IMGUI_VERSION_NUM >= 19120
#     if IMGUI_VERSION_NUM >= 19210
    const auto conflict = GImGui->DebugDrawIdConflictsId;
#     else
    const auto conflict = GImGui->DebugDrawIdConflicts;
#     endif
    test::Check(conflict == 0, "the buttons don't share an ID: conflict on 0x%08X", conflict);
# endif
}

TEST_CASE(CurrentZoom, "api.current_zoom_is_the_zoom_factor", "API-13", test::TestFlags_None)
{
    // The view saved at 200%.
    test::Storage storage;
    storage.Global = R"({"view":{"scroll":{"x":0,"y":0},"zoom":2}})";
    test::Session session(&storage);
    session.Run(2);

    const float zoom = session.Use([] { return ed::GetCurrentZoom(); });
    test::Check(zoom == 2.0f, "GetCurrentZoom() reads 2 at 200%%: %g", zoom);
}

TEST_CASE(IsFocused, "api.is_focused_follows_the_host_window", "API-13", test::TestFlags_None)
{
    // Another window takes focus from the host, then gives it back.
    test::Storage storage;
    test::Session session(&storage);
    bool focusOther = false, focusHost = false;
    session.AfterHost = [&]
    {
        ImGui::SetNextWindowPos(ImVec2(1100.0f, 600.0f));
        ImGui::Begin("Other");
        ImGui::End();
        if (focusOther)
            ImGui::SetWindowFocus("Other");
        if (focusHost)
            ImGui::SetWindowFocus("Host");
        focusOther = focusHost = false;
    };
    focusHost = true;
    session.Run(3);
    const bool focused     = session.Use([] { return ed::IsFocused() && ed::IsActive(); });
    focusOther = true;
    session.Run(3);
    const bool unfocused   = session.Use([] { return !ed::IsFocused() && !ed::IsActive(); });
    focusHost = true;
    session.Run(3);
    const bool refocused   = session.Use([] { return ed::IsFocused() && ed::IsActive(); });

    test::Check(focused && unfocused && refocused, "IsFocused() and IsActive() follow the host window's focus: %d, %d, %d", focused, unfocused, refocused);
}

TEST_CASE(OrderedNodeCount, "api.ordered_node_ids_count_without_an_array", "API-13", test::TestFlags_None)
{
    // Three nodes, counted without an array as GetSelectedNodes() counts.
    test::Storage storage;
    test::Session session(&storage);
    int counts[2] = {};
    session.DefaultContent = [&](int)
    {
        for (int id : { 1, 2, 3 })
        {
            ed::BeginNode(id);
            ImGui::Text("Node %d", id);
            ed::EndNode();
        }
    };
    session.Run(2);
    counts[0] = session.Use([] { return ed::GetOrderedNodeIds(nullptr, 0); });
    counts[1] = session.Use([] { return ed::GetOrderedNodeIds(nullptr, 10); });

    test::Check(counts[0] == 3 && counts[1] == 3, "the count is 3 whatever the size: %d and %d", counts[0], counts[1]);
}

TEST_CASE(ZeroIds, "api.zero_ids_are_refused", "API-13", test::TestFlags_None)
{
    // Node 0 with pin 1 in it, pin 0 in node 1, and link 0, each asserting on both
    // frames, next to valid nodes 1 and 2 linked by link 100.
    test::ExpectAsserts(6);
    test::Storage storage;
    test::Session session(&storage);
    bool zeroLinked = true, linked = false;
    session.DefaultContent = [&](int frame)
    {
        if (frame == 0)
        {
            ed::SetNodePosition(1, ImVec2(0.0f, 0.0f));
            ed::SetNodePosition(2, ImVec2(300.0f, 0.0f));
        }

        ed::BeginNode(0);
        ImGui::TextUnformatted("Zero");
        ed::BeginPin(1, ed::PinKind::Input);
        ed::PinPivotAlignment(ImVec2(0.0f, 0.5f));
        ImGui::TextUnformatted("In");
        ed::EndPin();
        ed::EndNode();

        ed::BeginNode(1);
        ed::BeginPin(0, ed::PinKind::Output);
        ed::PinRect(ImVec2(0.0f, 0.0f), ImVec2(10.0f, 10.0f));
        ImGui::TextUnformatted("Zero");
        ed::EndPin();
        ed::BeginPin(11, ed::PinKind::Output);
        ImGui::TextUnformatted("Out");
        ed::EndPin();
        ed::EndNode();

        ed::BeginNode(2);
        ed::BeginPin(21, ed::PinKind::Input);
        ImGui::TextUnformatted("In");
        ed::EndPin();
        ed::EndNode();

        zeroLinked = ed::Link(0, 11, 21);
        linked     = ed::Link(100, 11, 21);
    };
    session.Run(2);

    const auto zeroPosition = session.Use([] { return ed::GetNodePosition(0); });
    test::Check(zeroPosition.x == FLT_MAX, "node 0 isn't created: at (%g, %g)", zeroPosition.x, zeroPosition.y);
    test::Check(!zeroLinked && linked, "link 0 is refused, link 100 between the same pins isn't");
}

TEST_CASE(MisnestedBuilding, "api.misnested_building_is_ignored", "API-13", test::TestFlags_None)
{
    // Seven misuses, each asserting on both frames, before nodes 1 and 2 linked by
    // link 100.
    test::ExpectAsserts(14);
    test::Storage storage;
    test::Session session(&storage);
    bool linked = false;
    session.DefaultContent = [&](int frame)
    {
        if (frame == 0)
        {
            ed::SetNodePosition(1, ImVec2(0.0f, 0.0f));
            ed::SetNodePosition(2, ImVec2(300.0f, 0.0f));
            ed::SetNodePosition(3, ImVec2(0.0f, 200.0f));
        }

        ed::BeginPin(5, ed::PinKind::Input);            // outside a node
        ed::EndPin();
        ed::EndNode();                                  // without BeginNode()
        ed::EndPin();                                   // without BeginPin()
        ed::PinRect(ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f)); // outside a pin

        ed::BeginNode(1);
        ed::BeginNode(4);                               // inside another node
        ImGui::TextUnformatted("Nested");
        ed::EndNode();
        ed::BeginPin(11, ed::PinKind::Output);
        ed::BeginPin(12, ed::PinKind::Output);          // inside another pin
        ed::EndPin();
        ImGui::TextUnformatted("Out");
        ed::EndPin();
        ed::EndNode();

        ed::BeginNode(3);
        ImGui::TextUnformatted("Group");
        ed::Group(ImVec2(100.0f, 50.0f));
        ed::BeginPin(31, ed::PinKind::Input);           // in a group
        ed::EndPin();
        ed::EndNode();

        ed::BeginNode(2);
        ed::BeginPin(21, ed::PinKind::Input);
        ImGui::TextUnformatted("In");
        ed::EndPin();
        ed::EndNode();

        linked = ed::Link(100, 11, 21);
    };
    session.Run(2);

    const auto nested = session.Use([] { return ed::GetNodePosition(4); });
    const auto size   = session.Use([] { return ed::GetNodeSize(1); });
    test::Check(nested.x == FLT_MAX, "the nested node isn't created: at (%g, %g)", nested.x, nested.y);
    test::Check(size.x > 0.0f && size.y > 0.0f, "the node around it is built: %g by %g", size.x, size.y);
    test::Check(linked, "link 100 joins the pins built after the misuse");
}

TEST_CASE(GroupHintAlpha, "api.group_hint_multiplies_style_alpha", "API-13", test::TestFlags_None)
{
    // At zoom 0.6 a group hint fades to 0.6; the application's style alpha is 0.5.
    test::Storage storage;
    storage.Global = R"({"view":{"scroll":{"x":0,"y":0},"zoom":0.6}})";
    test::Session session(&storage);
    ImGui::GetStyle().Alpha = 0.5f;
    float alpha = -1.0f;
    session.DefaultContent = [&](int frame)
    {
        if (frame == 0)
            ed::SetNodePosition(10, ImVec2(0.0f, 0.0f));
        ed::BeginNode(10);
        ImGui::TextUnformatted("Group");
        ed::Group(ImVec2(300.0f, 200.0f));
        ed::EndNode();
        if (ed::BeginGroupHint(10))
        {
            alpha = ImGui::GetStyle().Alpha;
            ed::EndGroupHint();
        }
    };
    session.Run(3);

    test::Check(ImFabs(alpha - 0.3f) < 1e-4f, "the hint draws at 0.5 times 0.6: %g", alpha);
}

TEST_CASE(PinAlpha, "api.pin_colors_follow_style_alpha", "API-13", test::TestFlags_None)
{
    // A hovered pin draws its rectangle in opaque red, with the style alpha at 0.5.
    test::Storage storage;
    test::Session session(&storage);
    ImGui::GetStyle().Alpha = 0.5f;
    ImVec2 pinCenter;
    session.DefaultContent = [&](int frame)
    {
        if (frame == 0)
            ed::SetNodePosition(1, ImVec2(100.0f, 100.0f));
        ed::BeginNode(1);
        ed::PushStyleColor(ed::StyleColor_PinRect, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
        ed::BeginPin(11, ed::PinKind::Output);
        ImGui::TextUnformatted("Out ->");
        pinCenter = ImGui::GetItemRectMin() + ImGui::GetItemRectSize() * 0.5f;
        ed::EndPin();
        ed::PopStyleColor();
        ed::EndNode();
    };
    session.Run(2);
    test::MouseMove(session.Use([&] { return ed::CanvasToScreen(pinCenter); }));
    session.Run(2);

    const bool hovered = session.Use([] { return ed::GetHoveredPin() == ed::PinId(11); });
    int opaque = 0, halved = 0;
    for (auto color : test::Capture(session.DrawList).Colors)
    {
        if ((color & ~IM_COL32_A_MASK) != IM_COL32(255, 0, 0, 0) || (color & IM_COL32_A_MASK) == 0)
            continue;
        if ((color >> IM_COL32_A_SHIFT) == 255)
            ++opaque;
        else if ((color >> IM_COL32_A_SHIFT) == 128)
            ++halved;
    }

    test::Check(hovered, "the pin is hovered");
    test::Check(halved > 0 && opaque == 0, "the pin rectangle is drawn at half alpha: %d vertices at 128, %d at 255", halved, opaque);
}

TEST_CASE(AltClickWithoutShortcuts, "api.alt_click_deletion_follows_enable_shortcuts", "API-13", test::TestFlags_None)
{
    // Alt+click in the middle of link 100, with shortcuts off and then on.
    bool offered[2] = {};
    for (int enabled = 0; enabled < 2; ++enabled)
    {
        test::Storage storage;
        test::Session session(&storage);
        session.DefaultContent = [&](int frame)
        {
            ed::EnableShortcuts(enabled != 0);
            const ImVec2 pivots[2] = { ImVec2(100.0f, 100.0f), ImVec2(400.0f, 100.0f) };
            for (int n = 0; n < 2; ++n)
            {
                if (frame == 0)
                    ed::SetNodePosition(n + 1, ImVec2(n * 300.0f, 300.0f));
                ed::BeginNode(n + 1);
                ed::BeginPin(11 + n, n == 0 ? ed::PinKind::Output : ed::PinKind::Input);
                ImGui::TextUnformatted("Pin");
                ed::PinPivotRect(pivots[n], pivots[n]);
                ed::EndPin();
                ed::EndNode();
            }
            ed::Link(100, 11, 12);
            if (ed::BeginDelete())
            {
                ed::LinkId linkId;
                while (ed::QueryDeletedLink(&linkId))
                {
                    offered[enabled] = offered[enabled] || linkId == ed::LinkId(100);
                    ed::RejectDeletedItem();
                }
                ed::EndDelete();
            }
        };
        session.Run(2);
        const auto middle = session.Use([] { return ed::CanvasToScreen(ImVec2(250.0f, 100.0f)); });
        ImGui::GetIO().AddKeyEvent(ImGuiMod_Alt, true);
        session.Click(middle);
        session.Run(3);
        ImGui::GetIO().AddKeyEvent(ImGuiMod_Alt, false);
        session.Run(1);
    }

    test::Check(!offered[0], "with shortcuts off, Alt+click doesn't offer the link for deletion");
    test::Check(offered[1], "with shortcuts on, it does");
}

TEST_CASE(SelectionChangeInFrame, "api.selection_change_is_seen_between_begin_and_end", "CORE-13", test::TestFlags_None)
{
    // A click selects node 1; the content asks HasSelectionChanged() every frame.
    test::Storage storage;
    test::Session session(&storage);
    int changedFrames = 0;
    ImVec2 center;
    session.DefaultContent = [&](int frame)
    {
        if (frame == 0)
            ed::SetNodePosition(1, ImVec2(100.0f, 100.0f));
        ed::BeginNode(1);
        ImGui::TextUnformatted("Node");
        ed::EndNode();
        center = ed::GetNodePosition(1) + ed::GetNodeSize(1) * 0.5f;
        if (ed::HasSelectionChanged())
            ++changedFrames;
    };
    session.Run(2);
    session.Click(session.Use([&] { return ed::CanvasToScreen(center); }));
    session.Run(3);

    const bool selected = session.Use([] { return ed::IsNodeSelected(1); });
    test::Check(selected, "the click selects node 1");
    test::Check(changedFrames == 1, "the change is seen on one frame between Begin() and End(): %d", changedFrames);
}

TEST_CASE(LinkQueryPhase, "api.link_queries_see_links_submitted_so_far", "CORE-13", test::TestFlags_None)
{
    // Link 100 connects pins 11 and 21, and is submitted after the nodes.
    test::Storage storage;
    test::Session session(&storage);
    bool inPin = true, hadInPin = false, afterLinks = false;
    session.DefaultContent = [&](int frame)
    {
        for (int n = 1; n <= 2; ++n)
        {
            if (frame == 0)
                ed::SetNodePosition(n, ImVec2(n * 200.0f, 100.0f));
            ed::BeginNode(n);
            ed::BeginPin(n * 10 + 1, n == 1 ? ed::PinKind::Output : ed::PinKind::Input);
            if (n == 1 && frame == 2)
            {
                inPin    = ed::HasAnyLinks(ed::PinId(11));
                hadInPin = ed::PinHadAnyLinks(ed::PinId(11));
            }
            ImGui::TextUnformatted("Pin");
            ed::EndPin();
            ed::EndNode();
        }
        ed::Link(100, 11, 21);
        if (frame == 2)
            afterLinks = ed::HasAnyLinks(ed::PinId(11));
    };
    session.Run(3);

    test::Check(!inPin && hadInPin, "inside the pin, before the frame's links, HasAnyLinks() is false and PinHadAnyLinks() true");
    test::Check(afterLinks, "after the frame's links, HasAnyLinks() is true");
}
