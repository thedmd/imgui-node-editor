//------------------------------------------------------------------------------
// Smoke test driven by scripted input: dragging, linking, selection, deletion,
// shortcuts, the background context menu, zoom and a settings round trip.
//------------------------------------------------------------------------------
# include "harness.h"
# include <cmath>
# include <cstdio>
# include <map>

namespace {

struct SmokeApp
{
    struct Node { int Id, InPin, OutPin; ImVec2 Position; bool Alive; };
    struct Link { ed::LinkId Id; ed::PinId Start, End; };

    std::vector<Node>     Nodes;
    std::vector<Link>     Links;
    int                   NextLinkId   = 1000;
    bool                  SetPositions = true;

    std::map<int, ImVec2> PinScreen;      // pin id -> center on screen
    std::map<int, ImVec2> TitleScreen;    // node id -> center of the title on screen
    std::map<int, ImVec2> NodePosition;   // node id -> position on canvas
    std::map<int, ImVec2> NodeScreenSize; // node id -> size on screen

    int  Created = 0, DeletedNodes = 0, DeletedLinks = 0;
    bool BackgroundMenu = false, Copy = false, Paste = false, Cut = false, CreateNode = false;
    int  SelectedCount = 0;

    static ImVec2 ItemCenterOnScreen()
    {
        return ed::CanvasToScreen((ImGui::GetItemRectMin() + ImGui::GetItemRectMax()) * 0.5f);
    }

    void Frame(int frame)
    {
        for (auto& node : Nodes)
        {
            if (!node.Alive)
                continue;
            if (SetPositions && frame == 0)
                ed::SetNodePosition(node.Id, node.Position);
            ed::BeginNode(node.Id);
            ImGui::Text("Node %d", node.Id);
            TitleScreen[node.Id] = ItemCenterOnScreen();
            ed::BeginPin(node.InPin, ed::PinKind::Input);
            ImGui::Text("-> In");
            ed::EndPin();
            PinScreen[node.InPin] = ItemCenterOnScreen();
            ImGui::SameLine();
            ed::BeginPin(node.OutPin, ed::PinKind::Output);
            ImGui::Text("Out ->");
            ed::EndPin();
            PinScreen[node.OutPin] = ItemCenterOnScreen();
            ed::EndNode();

            NodePosition[node.Id]   = ed::GetNodePosition(node.Id);
            NodeScreenSize[node.Id] = ed::CanvasToScreen(NodePosition[node.Id] + ed::GetNodeSize(node.Id)) - ed::CanvasToScreen(NodePosition[node.Id]);
        }

        for (auto& link : Links)
            ed::Link(link.Id, link.Start, link.End);

        if (ed::BeginCreate())
        {
            ed::PinId start, end;
            if (ed::QueryNewLink(&start, &end) && start && end && ed::AcceptNewItem())
            {
                Links.push_back({ ed::LinkId(NextLinkId++), start, end });
                ++Created;
                ed::Link(Links.back().Id, start, end);
            }
        }
        ed::EndCreate();

        if (ed::BeginDelete())
        {
            ed::NodeId nodeId;
            while (ed::QueryDeletedNode(&nodeId))
            {
                if (ed::AcceptDeletedItem())
                {
                    for (auto& node : Nodes)
                        if (ed::NodeId(node.Id) == nodeId)
                            node.Alive = false;
                    ++DeletedNodes;
                }
            }

            ed::LinkId linkId;
            while (ed::QueryDeletedLink(&linkId))
            {
                if (ed::AcceptDeletedItem())
                {
                    for (size_t i = 0; i < Links.size(); ++i)
                    {
                        if (Links[i].Id == linkId)
                        {
                            Links.erase(Links.begin() + i);
                            break;
                        }
                    }
                    ++DeletedLinks;
                }
            }
        }
        ed::EndDelete();

        if (ed::BeginShortcut())
        {
            if (ed::AcceptCopy())       Copy = true;
            if (ed::AcceptPaste())      Paste = true;
            if (ed::AcceptCut())        Cut = true;
            if (ed::AcceptCreateNode()) CreateNode = true;
        }
        ed::EndShortcut();

        ed::Suspend();
        if (ed::ShowBackgroundContextMenu())
        {
            BackgroundMenu = true;
            ImGui::OpenPopup("background");
        }
        if (ImGui::BeginPopup("background"))
        {
            ImGui::Text("menu");
            ImGui::EndPopup();
        }
        ed::Resume();

        SelectedCount = ed::GetSelectedObjectCount();
    }
};

void ConfigureHost(test::Session& session, SmokeApp& app)
{
    session.HostPos        = ImVec2(0.0f, 0.0f);
    session.HostSize       = ImVec2(1280.0f, 720.0f);
    session.DefaultContent = [&app](int frame) { app.Frame(frame); };
}

} // namespace

TEST_CASE(Smoke, "smoke", nullptr, test::TestFlags_None)
{
    using test::Check;

    test::Storage storage;
    SmokeApp app;
    app.Nodes = { { 1, 11, 12, ImVec2(100, 100), true }, { 2, 21, 22, ImVec2(500, 150), true }, { 3, 31, 32, ImVec2(300, 400), true } };

    ImVec2 savedPosition;
    {
        test::Session session(&storage);
        ConfigureHost(session, app);

        session.Run(5);
        Check(session.HostContext.Vertices > 0, "editor draws");
        Check(fabsf(app.NodePosition[1].x - 100) < 0.5f && fabsf(app.NodePosition[1].y - 100) < 0.5f,
            "SetNodePosition() applied: (%.1f, %.1f)", app.NodePosition[1].x, app.NodePosition[1].y);

        // Dragging snaps pin pivots to the grid unless Alt is held, so allow one grid cell of deviation.
        const ImVec2 before = app.NodePosition[1];
        const int savesBefore = storage.GlobalSaves;
        session.Drag(app.TitleScreen[1], app.TitleScreen[1] + ImVec2(60, 40));
        const ImVec2 after = app.NodePosition[1];
        Check(fabsf(after.x - before.x - 60) <= 16.0f && fabsf(after.y - before.y - 40) <= 16.0f,
            "node dragged by (60, 40), grid snapped: (%.1f, %.1f) -> (%.1f, %.1f)", before.x, before.y, after.x, after.y);
        Check(storage.GlobalSaves > savesBefore, "settings saved after the drag (%d saves, %d before)", storage.GlobalSaves, savesBefore);

        session.Drag(app.PinScreen[12], app.PinScreen[21]);
        Check(app.Created == 1, "link created from node 1 to node 2 (%d created)", app.Created);
        Check(!app.Links.empty() && app.Links[0].Start == ed::PinId(12) && app.Links[0].End == ed::PinId(21), "link connects pin 12 to pin 21");

        session.Click(ImVec2(1100, 650));
        Check(app.SelectedCount == 0, "clicking the background clears the selection (%d selected)", app.SelectedCount);
        session.Click(app.TitleScreen[2]);
        Check(app.SelectedCount == 1, "clicking a node selects it (%d selected)", app.SelectedCount);
        session.PressKey(ImGuiKey_Delete);
        session.Run(3);
        Check(app.DeletedNodes == 1, "Delete deletes the selected node (%d deleted)", app.DeletedNodes);
        Check(app.DeletedLinks == 1, "the node's link is deleted with it (%d deleted, %d left)", app.DeletedLinks, (int)app.Links.size());

        session.Click(app.TitleScreen[1]);
        session.PressKey(ImGuiKey_C, true);
        Check(app.Copy, "Ctrl+C reaches AcceptCopy()");
        session.PressKey(ImGuiKey_V, true);
        Check(app.Paste, "Ctrl+V reaches AcceptPaste()");
        session.PressKey(ImGuiKey_X, true);
        Check(app.Cut, "Ctrl+X reaches AcceptCut()");
        test::MouseMove(ImVec2(1100, 650));
        session.Run(2);
        session.PressKey(ImGuiKey_Space);
        Check(app.CreateNode, "Space reaches AcceptCreateNode()");

        session.Click(ImVec2(1000, 600), 1);
        Check(app.BackgroundMenu, "right click on the background opens the background context menu");
        session.Click(ImVec2(1200, 700));

        const float width = app.NodeScreenSize[1].x;
        test::MouseMove(ImVec2(640, 360));
        session.Run(2);
        test::MouseWheel(1.0f);
        session.Run(30);
        Check(app.NodeScreenSize[1].x > width * 1.05f, "mouse wheel zooms in: node width %.1f -> %.1f", width, app.NodeScreenSize[1].x);
        test::MouseWheel(-1.0f);
        session.Run(30);
        Check(fabsf(app.NodeScreenSize[1].x - width) < 0.5f, "mouse wheel zooms back out: node width %.1f, was %.1f", app.NodeScreenSize[1].x, width);

        savedPosition = app.NodePosition[1];
        session.Run(2);
    }

    // A new editor restores the node from the settings saved by the previous one.
    {
        app.SetPositions = false;
        test::Session session(&storage);
        ConfigureHost(session, app);
        session.Run(5);
        Check(fabsf(app.NodePosition[1].x - savedPosition.x) < 0.5f && fabsf(app.NodePosition[1].y - savedPosition.y) < 0.5f,
            "node position restored from settings: (%.1f, %.1f), saved (%.1f, %.1f)", app.NodePosition[1].x, app.NodePosition[1].y, savedPosition.x, savedPosition.y);
    }
}
