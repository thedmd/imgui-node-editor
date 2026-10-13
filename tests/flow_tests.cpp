//------------------------------------------------------------------------------
// Flow animations along links.
//------------------------------------------------------------------------------
# include "harness.h"
# include <cmath>

namespace {

// Flow and flow markers are drawn magenta, which nothing else uses.
const ImU32 c_FlowColor = IM_COL32(255, 0, 255, 0);

// Nodes 1 and 11 with an input and an output pin each (node ID + 1 and + 2),
// links 100 from node 1 to node 11 and 101 from node 11 to node 1.
struct FlowScene
{
    bool   NodeAlive[2] = { true, true };
    bool   LinkAlive[2] = { true, true };
    ImVec2 Input[2], Output[2]; // pin centers on screen
    int    AcceptFrame  = -1;    // frame in which a deletion was accepted

    void Submit(int frame)
    {
        if (frame == 0)
        {
            auto& style = ed::GetStyle();
            style.Colors[ed::StyleColor_Flow]       = ImColor(c_FlowColor | IM_COL32_A_MASK);
            style.Colors[ed::StyleColor_FlowMarker] = ImColor(c_FlowColor | IM_COL32_A_MASK);
            style.FlowDuration = 100.0f;
        }

        for (int n = 0; n < 2; ++n)
        {
            if (!NodeAlive[n])
                continue;
            const uintptr_t id = 1 + n * 10;
            if (frame == 0)
                ed::SetNodePosition(id, ImVec2(100.0f + n * 300.0f, 100.0f + n * 100.0f));
            ed::BeginNode(id);
            ImGui::TextUnformatted("Node");
            ed::BeginPin(id + 1, ed::PinKind::Input);
            ImGui::TextUnformatted("> in");
            ed::EndPin();
            Input[n] = ed::CanvasToScreen((ImGui::GetItemRectMin() + ImGui::GetItemRectMax()) * 0.5f);
            ImGui::SameLine();
            ed::BeginPin(id + 2, ed::PinKind::Output);
            ImGui::TextUnformatted("out >");
            ed::EndPin();
            Output[n] = ed::CanvasToScreen((ImGui::GetItemRectMin() + ImGui::GetItemRectMax()) * 0.5f);
            ed::EndNode();
        }
        if (LinkAlive[0])
            ed::Link(100, 3, 12);
        if (LinkAlive[1])
            ed::Link(101, 13, 2);
    }

    void AcceptDeletions(int frame)
    {
        if (!ed::BeginDelete())
            return;
        ed::NodeId nodeId;
        while (ed::QueryDeletedNode(&nodeId))
            if (ed::AcceptDeletedItem())
                NodeAlive[nodeId.Get() == 1 ? 0 : 1] = false;
        ed::LinkId linkId;
        while (ed::QueryDeletedLink(&linkId))
        {
            if (ed::AcceptDeletedItem())
            {
                LinkAlive[linkId.Get() == 100 ? 0 : 1] = false;
                if (AcceptFrame < 0)
                    AcceptFrame = frame;
            }
        }
        ed::EndDelete();
    }

    // Middle of link 100, which is straight enough there for a click to hit it.
    ImVec2 OnLink() const
    {
        return (Output[0] + Input[1]) * 0.5f;
    }
};

int FlowVertices(const ImDrawList* drawList)
{
    int count = 0;
    for (auto color : test::Capture(drawList).Colors)
        if ((color & ~IM_COL32_A_MASK) == c_FlowColor)
            ++count;
    return count;
}

// Flows link 100, runs 'remove' to delete it, then checks that the flow isn't
// drawn from the frame the deletion is accepted on.
template <typename Remove>
void CheckFlowEndsWithLink(Remove&& remove)
{
    test::Storage storage;
    test::Session session(&storage);
    FlowScene scene;
    std::vector<int> flowVertices;
    session.DefaultContent = [&](int frame)
    {
        scene.Submit(frame);
        if (frame == 3)
            ed::Flow(100);
        scene.AcceptDeletions(frame);
    };
    session.AfterEditor = [&]
    {
        flowVertices.push_back(FlowVertices(session.DrawList));
    };

    session.Run(6);
    test::Check(flowVertices.back() > 0, "link 100 flows: %d flow vertices", flowVertices.back());

    remove(session, scene);
    session.Run(5);

    test::Check(!scene.LinkAlive[0], "link 100 deleted");
    if (scene.AcceptFrame >= 0)
    {
        int drawn = 0;
        for (int frame = scene.AcceptFrame; frame < (int)flowVertices.size(); ++frame)
            drawn += flowVertices[frame];
        test::Check(drawn == 0, "no flow drawn from the frame the deletion was accepted on (%d): %d vertices", scene.AcceptFrame, drawn);
    }
}

} // namespace

TEST_CASE(FlowDeleteKey, "flow.ends_when_link_deleted_with_delete_key", "INT-1", test::TestFlags_None)
{
    CheckFlowEndsWithLink([](test::Session& session, FlowScene&)
    {
        session.Click(ImVec2(900.0f, 600.0f));
        session.Use([] { ed::SelectLink(100); });
        session.PressKey(ImGuiKey_Delete);
    });
}

TEST_CASE(FlowAltClick, "flow.ends_when_link_deleted_with_alt_click", "INT-1", test::TestFlags_None)
{
    CheckFlowEndsWithLink([](test::Session& session, FlowScene& scene)
    {
        test::Key(ImGuiMod_Alt, true);
        session.Run(1);
        session.Click(scene.OnLink());
        test::Key(ImGuiMod_Alt, false);
        session.Run(1);
    });
}

TEST_CASE(FlowDeleteLink, "flow.ends_when_link_deleted_with_delete_link", "INT-1", test::TestFlags_None)
{
    CheckFlowEndsWithLink([](test::Session& session, FlowScene&)
    {
        session.Use([] { ed::DeleteLink(100); });
    });
}

TEST_CASE(FlowDeleteNode, "flow.ends_when_link_deleted_with_its_node", "INT-1", test::TestFlags_None)
{
    CheckFlowEndsWithLink([](test::Session& session, FlowScene&)
    {
        session.Use([] { ed::DeleteNode(11); });
    });
}

TEST_CASE(FlowMarkerDistance, "flow.marker_distance_not_positive", "INT-17", test::TestFlags_None)
{
    for (float distance : { 0.0f, -10.0f, NAN })
    {
        test::Storage storage;
        test::Session session(&storage);
        FlowScene scene;
        session.DefaultContent = [&](int frame)
        {
            scene.Submit(frame);
            if (frame == 0)
                ed::GetStyle().FlowMarkerDistance = distance;
            if (frame == 3)
                ed::Flow(100);
        };

        session.Run(3);
        const long long before = session.HostContext.Vertices;
        session.Run(10);
        const long long perFrame = (session.HostContext.Vertices - before) / 10;
        test::Check(perFrame < 100000, "FlowMarkerDistance %g: %lld vertices per frame", distance, perFrame);
    }
}

TEST_CASE(FlowReusesFinishedAnimation, "flow.finished_animation_is_reused", "INT-17", test::TestFlags_None)
{
    test::Storage storage;
    test::Session session(&storage);
    FlowScene scene;
    int flowVertices = 0;
    session.DefaultContent = [&](int frame)
    {
        scene.Submit(frame);
        if (frame == 0)
            ed::GetStyle().FlowDuration = 0.1f;
        if (frame == 3)
            ed::Flow(100);
        if (frame == 20)
            ed::Flow(101);
    };
    session.AfterEditor = [&]
    {
        flowVertices = FlowVertices(session.DrawList);
    };

    session.Run(19);
    test::Check(flowVertices == 0, "flow of link 100 finished: %d flow vertices", flowVertices);
    session.Run(2);
    test::Check(flowVertices > 0, "link 101 flows: %d flow vertices", flowVertices);
}

TEST_CASE(FlowBeforeLink, "flow.flow_called_before_the_link_is_submitted", "INT-18", test::TestFlags_None)
{
    test::Storage storage;
    test::Session session(&storage);
    FlowScene scene;
    bool flowFirst = false;
    session.DefaultContent = [&](int frame)
    {
        if (flowFirst)
            ed::Flow(100);
        flowFirst = false;
        scene.Submit(frame);
    };
    session.Run(3);
    flowFirst = true;
    session.Run(3);

    const int vertices = FlowVertices(session.DrawList);
    test::Check(vertices > 0, "link 100 flows when ed::Flow() comes before ed::Link() in the frame: %d flow vertices", vertices);
}

TEST_CASE(FlowOnLongLink, "flow.markers_stay_on_a_very_long_link", "MATH-9", test::TestFlags_None)
{
    // Nodes 80,000 units apart, with the link between them across the view and pulled
    // out by its strength. Markers that left the link would be drawn at non-finite
    // positions, which the harness fails.
    test::Storage storage;
    test::Session session(&storage);
    int vertices = 0;
    session.DefaultContent = [&](int frame)
    {
        if (frame == 0)
        {
            auto& style = ed::GetStyle();
            style.Colors[ed::StyleColor_Flow]       = ImColor(c_FlowColor | IM_COL32_A_MASK);
            style.Colors[ed::StyleColor_FlowMarker] = ImColor(c_FlowColor | IM_COL32_A_MASK);
            style.FlowDuration = 100.0f;
            style.LinkStrength = 20000.0f;
            ed::SetNodePosition(1, ImVec2(-40000.0f, 300.0f));
            ed::SetNodePosition(11, ImVec2(40000.0f, 300.0f));
        }
        ed::BeginNode(1);
        ed::BeginPin(3, ed::PinKind::Output);
        ImGui::TextUnformatted("out >");
        ed::EndPin();
        ed::EndNode();
        ed::BeginNode(11);
        ed::BeginPin(12, ed::PinKind::Input);
        ImGui::TextUnformatted("> in");
        ed::EndPin();
        ed::EndNode();
        ed::Link(100, 3, 12);
        if (frame == 3)
            ed::Flow(100);
    };
    session.AfterEditor = [&] { vertices = FlowVertices(session.DrawList); };
    session.Run(6);

    test::Check(vertices > 0, "the long link flows: %d flow vertices", vertices);
}
