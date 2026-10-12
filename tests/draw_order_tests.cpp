//------------------------------------------------------------------------------
// Layering of what the editor draws, which EditorContext::End() establishes by
// reordering draw list channels.
//------------------------------------------------------------------------------
# include "harness.h"

namespace {

const ImU32 c_Red     = IM_COL32(255,   0,   0, 255);
const ImU32 c_Green   = IM_COL32(  0, 255,   0, 255);
const ImU32 c_Blue    = IM_COL32(  0,   0, 255, 255);
const ImU32 c_Magenta = IM_COL32(255,   0, 255, 255);

test::Geometry RunScene(const test::Session::Content& scene)
{
    test::Storage storage;
    test::Session session(&storage);
    for (int i = 0; i < 4; ++i)
        session.Frame(scene);
    return test::Capture(session.DrawList);
}

// A link across a green group, from a node on either side of it. Node 3, when
// present, is unrelated and below every other node in Z.
void CheckLinkOverGroup(bool withLowerNode)
{
    const auto geometry = RunScene([withLowerNode](int frame)
    {
        if (frame == 0)
        {
            ed::SetNodePosition(10, ImVec2(   0.0f,  0.0f));
            ed::SetNodePosition( 1, ImVec2(-260.0f, 60.0f));
            ed::SetNodePosition( 2, ImVec2( 520.0f, 60.0f));
            ed::SetNodePosition( 3, ImVec2(   0.0f, 400.0f));
        }

        ed::PushStyleColor(ed::StyleColor_GroupBg, ImVec4(0.0f, 1.0f, 0.0f, 1.0f));
        ed::BeginNode(10);
        ImGui::Text("Group");
        ed::Group(ImVec2(400.0f, 200.0f));
        ed::EndNode();
        ed::PopStyleColor();

        ed::BeginNode(1);
        ImGui::Text("Source");
        ed::BeginPin(11, ed::PinKind::Output);
        ImGui::Text("Out ->");
        ed::EndPin();
        ed::EndNode();

        ed::BeginNode(2);
        ImGui::Text("Target");
        ed::BeginPin(21, ed::PinKind::Input);
        ImGui::Text("-> In");
        ed::EndPin();
        ed::EndNode();

        if (withLowerNode)
        {
            if (frame == 0)
                ed::SetNodeZPosition(3, -1.0f);
            ed::BeginNode(3);
            ImGui::Text("Unrelated");
            ed::EndNode();
        }

        ed::Link(100, 11, 21, ImVec4(1.0f, 0.0f, 0.0f, 1.0f), 3.0f);
    });

    const auto link  = test::FirstIndexOf(geometry, c_Red);
    const auto group = test::FirstIndexOf(geometry, c_Green);
    test::Check(link >= 0 && group >= 0, "link and group background are drawn");
    test::Check(link > group, "link is drawn over the group background: link at index %ld, group at %ld", link, group);
}

} // namespace

TEST_CASE(DrawOrderLinksOverGroups, "draw_order.links_over_groups", nullptr, test::TestFlags_None)
{
    CheckLinkOverGroup(false);
}

TEST_CASE(DrawOrderLinksOverGroupsWithLowerNode, "draw_order.links_over_groups_under_any_z", "CORE-9", test::TestFlags_None)
{
    CheckLinkOverGroup(true);
}

TEST_CASE(DrawOrderSelectionBorder, "draw_order.selection_border_over_lower_node", nullptr, test::TestFlags_None)
{
    const auto geometry = RunScene([](int frame)
    {
        if (frame == 0)
        {
            ed::SetNodePosition(1, ImVec2( 0.0f,  0.0f));
            ed::SetNodePosition(2, ImVec2(60.0f, 40.0f));
            ed::GetStyle().Colors[ed::StyleColor_SelNodeBorder] = ImVec4(1.0f, 0.0f, 1.0f, 1.0f);
        }

        ed::PushStyleColor(ed::StyleColor_NodeBg, ImVec4(0.0f, 0.0f, 1.0f, 1.0f));
        ed::BeginNode(1);
        ImGui::Text("Bottom");
        ImGui::Dummy(ImVec2(160.0f, 80.0f));
        ed::EndNode();
        ed::PopStyleColor();

        ed::BeginNode(2);
        ImGui::Text("Top, selected");
        ImGui::Dummy(ImVec2(160.0f, 80.0f));
        ed::EndNode();

        if (frame == 1)
            ed::SelectNode(2);
    });

    const auto selection = test::FirstIndexOf(geometry, c_Magenta);
    const auto bottom    = test::FirstIndexOf(geometry, c_Blue);
    test::Check(selection >= 0 && bottom >= 0, "selection border and lower node are drawn");
    test::Check(selection > bottom, "selection border of the upper node is drawn over the lower node: border at index %ld, node at %ld", selection, bottom);
}

TEST_CASE(DrawOrderContentBeforeNodes, "draw_order.content_before_nodes_over_background", "CORE-16", test::TestFlags_None)
{
    // A rectangle drawn right after ed::Begin(), before any node, on a blue background
    // in a green host window.
    const auto geometry = RunScene([](int frame)
    {
        if (frame == 0)
        {
            ImGui::GetStyle().Colors[ImGuiCol_WindowBg] = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
            ed::GetStyle().Colors[ed::StyleColor_Bg] = ImVec4(0.0f, 0.0f, 1.0f, 1.0f);
        }

        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(10.0f, 10.0f), ImVec2(60.0f, 60.0f), c_Red);

        ed::BeginNode(1);
        ImGui::Text("Node");
        ed::EndNode();
    });

    const auto content    = test::FirstIndexOf(geometry, c_Red);
    const auto background = test::FirstIndexOf(geometry, c_Blue);
    const auto window     = test::FirstIndexOf(geometry, c_Green);
    test::Check(content >= 0 && background >= 0 && window >= 0, "content, background and host window are drawn");
    test::Check(window < background && background < content, "content is drawn over the background, which is over the host window: window at index %ld, background at %ld, content at %ld", window, background, content);
}

TEST_CASE(DrawOrderOverlayAfterContextMenu, "draw_order.overlay_after_context_menu_is_not_clipped", "CORE-16", test::TestFlags_None)
{
    // At zoom 2, a rectangle drawn at the mouse in canvas space, without Suspend(), in
    // the frame ShowBackgroundContextMenu() returns true.
    test::Storage storage;
    storage.Global = R"({"view":{"scroll":{"x":0,"y":0},"zoom":2}})";
    test::Session session(&storage);
    bool shown = false;
    session.DefaultContent = [&shown](int)
    {
        shown = ed::ShowBackgroundContextMenu();
        if (shown)
        {
            const auto mouse = ImGui::GetMousePos();
            ImGui::GetWindowDrawList()->AddRectFilled(mouse, mouse + ImVec2(20.0f, 20.0f), c_Red);
        }
    };
    session.Run(2);

    test::MouseMove(session.Use([] { return ed::CanvasToScreen(ImVec2(200.0f, 120.0f)); }));
    session.Run(2);
    test::MouseButton(1, true);
    session.Run(2);
    test::MouseButton(1, false);
    for (int i = 0; i < 4 && !shown; ++i)
        session.Frame();

    const auto geometry = test::Capture(session.DrawList);
    int drawn = 0, outside = 0;
    for (size_t i = 0; i < geometry.Colors.size(); ++i)
    {
        if (geometry.Colors[i] != c_Red)
            continue;
        ++drawn;
        const auto& p    = geometry.Positions[i];
        const auto& clip = geometry.ClipRects[i];
        if (p.x < clip.x || p.y < clip.y || p.x > clip.z || p.y > clip.w)
            ++outside;
    }

    test::Check(shown, "the background context menu is shown");
    test::Check(drawn > 0 && outside == 0, "the overlay is inside its clip rectangle: %d of %d vertices outside", outside, drawn);
}
