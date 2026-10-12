//------------------------------------------------------------------------------
// ed::Suspend()/ed::Resume() at every call site applications use. A render with
// Suspend()/Resume() must be identical to one without, and must not leave the
// canvas sentinel callback in the draw list.
//------------------------------------------------------------------------------
# include "harness.h"
# include <cstdio>

namespace {

enum class Site { None, BeforeNodes, BetweenNodes, InNode, InNodePopup, InNodePopupPairs, InNodeSplit, InPin, InTableCell };

const char* c_ViewState = R"({"view":{"scroll":{"x":-150.0,"y":-90.0},"zoom":1.5}})";

struct Variant
{
    int  HostCommands = 0;     // draw commands with distinct clip rectangles in the host window, before the editor
    int  NodeCommands = 0;     // the same in node content, before Suspend()
    bool BareHost     = false; // transparent host without padding, rounding or border, as in issue #282
    bool Split        = false; // node content uses ChannelsSplit()
    bool Table        = false; // node content has a table
    int  Nesting      = 1;     // Suspend()/Resume() pairs nested in each other
    bool Draw         = false; // draw in screen space inside the innermost pair
};

int  s_SuspendStateErrors = 0; // IsSuspended() didn't match the nesting
int  s_Nesting            = 1;
bool s_Draw               = false;

void EmitClippedCommands(int count, bool canvasSpace, ImVec2 hostPos)
{
    for (int i = 0; i < count; ++i)
    {
        const auto origin = canvasSpace ? ImGui::GetCursorScreenPos() : hostPos;
        ImGui::PushClipRect(origin, origin + ImVec2(200.0f + i * 7.0f, 400.0f), true);
        if (canvasSpace)
            ImGui::Text("Clipped %d", i);
        else
            ImGui::GetWindowDrawList()->AddRectFilled(origin + ImVec2(2.0f + i * 7.0f, 2.0f), origin + ImVec2(6.0f + i * 7.0f, 6.0f), IM_COL32(200, 200, 200, 255));
        ImGui::PopClipRect();
    }
}

void SuspendResume()
{
    for (int i = 0; i < s_Nesting; ++i)
    {
        ed::Suspend();
        if (!ed::IsSuspended())
            ++s_SuspendStateErrors;
    }
    if (s_Draw)
    {
        // Splitting channels needs the host window's splitter, as in ImGui::Columns().
        auto drawList = ImGui::GetWindowDrawList();
        drawList->ChannelsSplit(2);
        drawList->ChannelsSetCurrent(1);
        drawList->AddRectFilled(ImVec2(20.0f, 20.0f), ImVec2(50.0f, 40.0f), IM_COL32(255, 0, 255, 255));
        drawList->ChannelsSetCurrent(0);
        drawList->AddRectFilled(ImVec2(30.0f, 30.0f), ImVec2(60.0f, 50.0f), IM_COL32(0, 255, 255, 255));
        drawList->ChannelsMerge();
    }
    for (int i = 0; i < s_Nesting; ++i)
    {
        if (!ed::IsSuspended())
            ++s_SuspendStateErrors;
        ed::Resume();
    }
    if (ed::IsSuspended())
        ++s_SuspendStateErrors;
}

void Scene(int frame, Site site, const Variant& variant)
{
    s_Nesting = variant.Nesting;
    s_Draw    = variant.Draw;

    if (frame == 0)
    {
        ed::SetNodePosition(1, ImVec2(0.0f, 0.0f));
        ed::SetNodePosition(2, ImVec2(300.0f, 120.0f));
    }

    if (site == Site::BeforeNodes)
        SuspendResume();

    ed::BeginNode(1);
    ImGui::Text("Node A");
    EmitClippedCommands(variant.NodeCommands, true, ImVec2());
    if (site == Site::InNode)
        SuspendResume();
    if (site == Site::InNodePopup)
    {
        ed::Suspend();
        ImGui::OpenPopup("popup");
        ImGui::SetNextWindowPos(ImVec2(700.0f, 500.0f));
        if (ImGui::BeginPopup("popup"))
        {
            ImGui::Text("Popup");
            ImGui::EndPopup();
        }
        ed::Resume();
    }
    if (site == Site::InNodePopupPairs)
    {
        // Pattern from issue #282: OpenPopup() and BeginPopup() in separate Suspend()/Resume() pairs.
        if (frame == 1)
        {
            ed::Suspend();
            ImGui::OpenPopup("popup");
            ed::Resume();
        }
        ed::Suspend();
        if (ImGui::BeginPopup("popup"))
        {
            ImGui::Text("Popup");
            ImGui::EndPopup();
        }
        ed::Resume();
    }
    if (variant.Split)
    {
        auto drawList = ImGui::GetWindowDrawList();
        drawList->ChannelsSplit(2);
        drawList->ChannelsSetCurrent(1);
        ImGui::Text("Front");
        if (site == Site::InNodeSplit)
            SuspendResume();
        drawList->ChannelsSetCurrent(0);
        ImGui::Text("Back");
        drawList->ChannelsMerge();
    }
    if (variant.Table && ImGui::BeginTable("table", 2, ImGuiTableFlags_SizingFixedFit, ImVec2(200.0f, 0.0f)))
    {
        // Tables draw through their own ImDrawListSplitter.
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("Cell A");
        if (site == Site::InTableCell)
            SuspendResume();
        ImGui::TableNextColumn();
        ImGui::Text("Cell B");
        ImGui::EndTable();
    }
    ImGui::Text("More content");
    ed::BeginPin(11, ed::PinKind::Output);
    ImGui::Text("Out ->");
    ed::EndPin();
    ed::EndNode();

    if (site == Site::BetweenNodes)
        SuspendResume();

    ed::BeginNode(2);
    ImGui::Text("Node B");
    ed::BeginPin(21, ed::PinKind::Input);
    ImGui::Text("-> In");
    if (site == Site::InPin)
        SuspendResume();
    ed::EndPin();
    ed::EndNode();

    ed::Link(100, 11, 21);
}

struct Result
{
    test::Geometry Geometry;
    int            Asserts       = 0;
    ImVec2         PopupPosition = ImVec2(-1.0f, -1.0f);
};

Result RunScene(Site site, const Variant& variant)
{
    test::Storage storage;
    storage.Global = c_ViewState;
    test::Session session(&storage);
    session.BareHost     = variant.BareHost;
    session.BeforeEditor = [&session, &variant] { EmitClippedCommands(variant.HostCommands, false, session.HostPos); };

    Result result;
    const int assertsBefore = test::AssertCount();
    for (int i = 0; i < 4; ++i)
        session.Frame([&](int frame) { Scene(frame, site, variant); });
    result.Asserts = test::AssertCount() - assertsBefore;

    for (ImGuiWindow* window : GImGui->Windows)
        if ((window->Flags & ImGuiWindowFlags_Popup) && window->Active)
            result.PopupPosition = window->Pos;

    result.Geometry = test::Capture(session.DrawList);
    return result;
}

// Compares the site with the same scene without Suspend()/Resume() in every
// variant. Nested pairs draw inside, and are compared with a single pair that
// draws the same.
void CheckSite(Site site, int nesting = 1)
{
    int total = 0, differing = 0, stray = 0, asserts = 0;
    s_SuspendStateErrors = 0;
    for (bool bareHost : { false, true })
    {
        for (int hostCommands = 0; hostCommands <= 4; ++hostCommands)
        {
            for (int nodeCommands = 0; nodeCommands <= 4; ++nodeCommands)
            {
                Variant variant;
                variant.HostCommands = hostCommands;
                variant.NodeCommands = nodeCommands;
                variant.BareHost     = bareHost;
                variant.Split        = site == Site::InNodeSplit;
                variant.Table        = site == Site::InTableCell;
                variant.Draw         = nesting > 1;

                const auto baseline = RunScene(nesting > 1 ? site : Site::None, variant);
                variant.Nesting = nesting;
                const auto result   = RunScene(site, variant);

                ++total;
                const auto diff = test::Compare(baseline.Geometry, result.Geometry);
                if (!diff.Identical() && differing++ == 0)
                    printf("    first difference: %s host, %d host and %d node commands: %s\n",
                        bareHost ? "bare" : "plain", hostCommands, nodeCommands, test::Describe(diff).c_str());
                if (result.Geometry.Callbacks > 0)
                    ++stray;
                asserts += result.Asserts;

                if (site == Site::InNodePopup && total == 1)
                    test::Check(result.PopupPosition.x == 700.0f && result.PopupPosition.y == 500.0f,
                        "popup opened while suspended is placed in screen space: (%g, %g)", result.PopupPosition.x, result.PopupPosition.y);
            }
        }
    }

    test::Check(differing == 0, "render identical to %s: %d of %d variants differ", nesting > 1 ? "a single drawing pair" : "one without Suspend()/Resume()", differing, total);
    test::Check(stray == 0, "no canvas callback left for the renderer: %d of %d variants leave one", stray, total);
    test::Check(asserts == 0, "no IM_ASSERT failures: %d", asserts);
    test::Check(s_SuspendStateErrors == 0, "IsSuspended() follows %d nested pairs: %d mismatches", nesting, s_SuspendStateErrors);
}

} // namespace

TEST_CASE(SuspendBeforeNodes, "suspend.before_nodes", "CORE-16", test::TestFlags_None)
{
    CheckSite(Site::BeforeNodes);
    CheckSite(Site::BeforeNodes, 2);
}

TEST_CASE(SuspendBetweenNodes, "suspend.between_nodes", "CNV-1", test::TestFlags_None)
{
    CheckSite(Site::BetweenNodes);
}

TEST_CASE(SuspendInNode, "suspend.in_node", "CNV-1", test::TestFlags_None)
{
    CheckSite(Site::InNode);
}

TEST_CASE(SuspendInNodePopup, "suspend.in_node_popup", "CNV-1", test::TestFlags_None)
{
    CheckSite(Site::InNodePopup);
}

TEST_CASE(SuspendInNodePopupPairs, "suspend.in_node_popup_pairs", "CNV-1", test::TestFlags_None)
{
    CheckSite(Site::InNodePopupPairs);
}

TEST_CASE(SuspendInNodeSplit, "suspend.in_node_split", "CNV-1", test::TestFlags_None)
{
    CheckSite(Site::InNodeSplit);
}

TEST_CASE(SuspendInPin, "suspend.in_pin", "CNV-1", test::TestFlags_None)
{
    CheckSite(Site::InPin);
}

TEST_CASE(SuspendInTableCell, "suspend.in_table_cell", "CNV-1", test::TestFlags_None)
{
    CheckSite(Site::InTableCell);
}

TEST_CASE(NestedSuspendBetweenNodes, "suspend.nested_between_nodes", "CORE-16", test::TestFlags_None)
{
    CheckSite(Site::BetweenNodes, 2);
    CheckSite(Site::BetweenNodes, 3);
}

TEST_CASE(NestedSuspendInNode, "suspend.nested_in_node", "CORE-16", test::TestFlags_None)
{
    CheckSite(Site::InNode, 2);
    CheckSite(Site::InNode, 3);
}

TEST_CASE(NestedSuspendInPin, "suspend.nested_in_pin", "CORE-16", test::TestFlags_None)
{
    CheckSite(Site::InPin, 2);
    CheckSite(Site::InPin, 3);
}
