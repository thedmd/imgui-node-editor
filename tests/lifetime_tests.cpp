//------------------------------------------------------------------------------
// Objects the editor frees while other state still points at them. These are
// use-after-free errors, which only AddressSanitizer reports reliably.
//------------------------------------------------------------------------------
# include "harness.h"

namespace {

ImVec2 ItemCenterOnScreen()
{
    return ed::CanvasToScreen((ImGui::GetItemRectMin() + ImGui::GetItemRectMax()) * 0.5f);
}

// Nodes 1 and 11 with an input and an output pin each (node ID + 1 and + 2),
// and link 100 from the output of node 1 to the input of node 11.
struct TwoNodes
{
    bool   NodeAlive[2] = { true, true };
    bool   LinkAlive    = true;
    ImVec2 Input[2], Output[2]; // pin centers on screen

    void Submit(int frame)
    {
        for (int n = 0; n < 2; ++n)
        {
            if (!NodeAlive[n])
                continue;
            const uintptr_t id = 1 + n * 10;
            if (frame == 0)
                ed::SetNodePosition(id, ImVec2(100.0f + n * 300.0f, 100.0f));
            ed::BeginNode(id);
            ImGui::TextUnformatted("Node");
            ed::BeginPin(id + 1, ed::PinKind::Input);
            ImGui::TextUnformatted("> in");
            ed::EndPin();
            Input[n] = ItemCenterOnScreen();
            ImGui::SameLine();
            ed::BeginPin(id + 2, ed::PinKind::Output);
            ImGui::TextUnformatted("out >");
            ed::EndPin();
            Output[n] = ItemCenterOnScreen();
            ed::EndNode();
        }
        if (LinkAlive)
            ed::Link(100, 3, 12);
    }

    // Accepts every deletion, links first, the order imgui_node_editor.h declares
    // the queries in. Like an application model, drops the link with its nodes.
    void AcceptDeletions()
    {
        if (!ed::BeginDelete())
        {
            ed::EndDelete();
            return;
        }
        ed::LinkId linkId;
        while (ed::QueryDeletedLink(&linkId))
            if (ed::AcceptDeletedItem())
                LinkAlive = false;
        ed::NodeId nodeId;
        while (ed::QueryDeletedNode(&nodeId))
        {
            if (ed::AcceptDeletedItem())
            {
                NodeAlive[nodeId.Get() == 1 ? 0 : 1] = false;
                LinkAlive = false;
            }
        }
        ed::EndDelete();
    }
};

} // namespace

TEST_CASE(FlowOfDeletedLink, "lifetime.flow_animation_of_deleted_link", "INT-1", test::TestFlags_NeedsSanitizer)
{
    test::Storage storage;
    test::Session session(&storage);
    TwoNodes scene;
    for (int i = 0; i < 30; ++i)
        session.Frame([&](int frame)
        {
            scene.Submit(frame);
            if (frame == 3)
                ed::Flow(100);
            if (frame == 6)
                ed::DeleteLink(100);
            scene.AcceptDeletions();
        });
    test::Check(!scene.LinkAlive, "link deleted while its flow animation was playing");
}

TEST_CASE(SelectedLinkDeletedWithNode, "lifetime.selected_link_deleted_with_node", "CORE-1", test::TestFlags_None)
{
    test::Storage storage;
    test::Session session(&storage);
    TwoNodes scene;
    for (int i = 0; i < 12; ++i)
        session.Frame([&](int frame)
        {
            scene.Submit(frame);
            if (frame == 3)
                ed::SelectLink(100);
            if (frame == 5)
                ed::DeleteNode(11);
            scene.AcceptDeletions();
        });
    test::Check(!scene.NodeAlive[1], "node deleted together with its selected link");

    const int selectedObjects = session.Use([] { return ed::GetSelectedObjectCount(); });
    const int selectedLinks   = session.Use([] { return ed::GetSelectedLinks(nullptr, 0); });
    test::Check(selectedObjects == 0 && selectedLinks == 0, "deleted link isn't selected: %d objects, %d links", selectedObjects, selectedLinks);
}

TEST_CASE(ActiveLinkDeletedWithNode, "lifetime.active_link_deleted_with_node", "CORE-1", test::TestFlags_NeedsSanitizer)
{
    // The mouse is held on a link while the Delete key removes one of its nodes.
    test::Storage storage;
    test::Session session(&storage);
    TwoNodes scene;
    session.DefaultContent = [&](int frame)
    {
        scene.Submit(frame);
        if (frame == 3)
            ed::SelectNode(11);
        scene.AcceptDeletions();
    };

    session.Run(5);
    test::MouseMove((scene.Output[0] + scene.Input[1]) * 0.5f);
    session.Run(2);
    test::Check(session.Use([] { return ed::GetHoveredLink() == ed::LinkId(100); }), "mouse is over the link");
    test::MouseButton(0, true);
    session.Run(2);
    session.PressKey(ImGuiKey_Delete);
    session.Run(3);
    test::MouseButton(0, false);
    session.Run(3);
    test::Check(!scene.NodeAlive[1] && !scene.LinkAlive, "node deleted together with the link under the mouse");
}

TEST_CASE(LinkPinsAfterNodeDeleted, "lifetime.link_pins_after_node_deleted", "CORE-1", test::TestFlags_None)
{
    // The application handles deletions before it submits links. The link isn't
    // live when its node is accepted, so it outlives the node's pins.
    test::Storage storage;
    test::Session session(&storage);
    TwoNodes scene;
    bool pinsReported = false;
    for (int i = 0; i < 10; ++i)
        session.Frame([&](int frame)
        {
            scene.AcceptDeletions();
            scene.Submit(frame);
            if (frame == 3)
                ed::DeleteNode(11);
            ed::PinId startPinId, endPinId;
            pinsReported = ed::GetLinkPins(100, &startPinId, &endPinId);
        });
    test::Check(!scene.NodeAlive[1], "node deleted");
    test::Check(!pinsReported, "GetLinkPins() reports no pins for a link whose pin was deleted");
}

TEST_CASE(PendingNewLinkToDeletedPin, "lifetime.pending_new_link_to_deleted_pin", "CORE-1", test::TestFlags_None)
{
    // The application accepts a new link during the drag, then skips BeginCreate()
    // while the node of the link's end pin is deleted.
    test::Storage storage;
    test::Session session(&storage);
    TwoNodes scene;
    scene.LinkAlive = false;
    bool handleCreate = false, deleteNode = false;
    int  created = 0;
    session.DefaultContent = [&](int frame)
    {
        scene.Submit(frame);
        if (handleCreate)
        {
            if (ed::BeginCreate())
            {
                ed::PinId startPinId, endPinId;
                if (ed::QueryNewLink(&startPinId, &endPinId) && ed::AcceptNewItem())
                    ++created;
            }
            ed::EndCreate();
        }
        if (deleteNode)
        {
            ed::DeleteNode(11);
            deleteNode = false;
        }
        scene.AcceptDeletions();
    };

    session.Run(3);
    handleCreate = true;
    test::MouseMove(scene.Output[0]);
    session.Run(2);
    test::MouseButton(0, true);
    session.Run(2);
    for (int i = 1; i <= 10; ++i)
    {
        test::MouseMove(scene.Output[0] + (scene.Input[1] - scene.Output[0]) * (i / 10.0f));
        session.Run(1);
    }
    session.Run(2);
    handleCreate = false;
    test::MouseButton(0, false);
    session.Run(2);
    deleteNode = true;
    session.Run(5);
    handleCreate = true;
    session.Run(3);

    test::Check(!scene.NodeAlive[1], "node deleted while the new link was pending");
    test::Check(created == 0, "no link created to a deleted pin: %d created", created);
}

TEST_CASE(ManualDeleteQueue, "lifetime.manual_delete_queue_outlives_objects", "API-1", test::TestFlags_NeedsSanitizer)
{
    // The application binds its own delete command to the Delete key, while the
    // editor's Delete shortcut is enabled too.
    test::Storage storage;
    test::Session session(&storage);
    bool nodeAlive = true;
    ImVec2 title;
    session.DefaultContent = [&](int frame)
    {
        if (nodeAlive)
        {
            if (frame == 0)
                ed::SetNodePosition(1, ImVec2(100.0f, 100.0f));
            ed::BeginNode(1);
            ImGui::TextUnformatted("Node");
            title = ItemCenterOnScreen();
            ed::EndNode();
        }
        if (nodeAlive && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
            ed::DeleteNode(1);
        if (ed::BeginDelete())
        {
            ed::NodeId nodeId;
            while (ed::QueryDeletedNode(&nodeId))
                if (ed::AcceptDeletedItem())
                    nodeAlive = false;
        }
        ed::EndDelete();
    };

    session.Run(3);
    session.Click(title);
    session.PressKey(ImGuiKey_Delete);
    session.Run(10);
    test::Check(!nodeAlive, "node deleted");
}

TEST_CASE(NavigateAfterDeletingRememberedObject, "lifetime.navigate_after_deleting_remembered_object", "CORE-1", test::TestFlags_None)
{
    // F centers the hovered node, and zooms to the content when pressed again over
    // the same node. A node created after that node is deleted may get its address.
    test::Storage storage;
    test::Session session(&storage);
    uintptr_t nodeId = 1;
    bool deleteNode = false, replaceNode = false;
    ImVec2 nodePosition(100.0f, 100.0f), nodeCenter;
    session.DefaultContent = [&](int frame)
    {
        if (frame == 0 || replaceNode)
        {
            ed::SetNodePosition(nodeId, nodePosition);
            replaceNode = false;
        }
        ed::BeginNode(nodeId);
        ImGui::TextUnformatted("Node");
        ImGui::Dummy(ImVec2(80.0f, 40.0f));
        ed::EndNode();
        nodeCenter = ed::CanvasToScreen(ed::GetNodePosition(nodeId) + ed::GetNodeSize(nodeId) * 0.5f);
        if (deleteNode)
        {
            ed::DeleteNode(nodeId);
            deleteNode = false;
        }
        if (ed::BeginDelete())
        {
            ed::NodeId deletedId;
            while (ed::QueryDeletedNode(&deletedId))
            {
                if (ed::AcceptDeletedItem())
                {
                    // Created in the next frame, after node 1 is freed.
                    nodePosition = ed::GetNodePosition(nodeId);
                    nodeId       = 2;
                    replaceNode  = true;
                }
            }
        }
        ed::EndDelete();
    };

    session.Run(3);
    test::MouseMove(nodeCenter);
    session.Run(2);
    session.PressKey(ImGuiKey_F);
    session.Run(60);
    const float zoom = session.Use([] { return test::CurrentZoom(); });

    deleteNode = true;
    session.Run(5);
    test::Check(nodeId == 2, "node 1 replaced by node 2");
    test::MouseMove(nodeCenter);
    session.Run(2);
    session.PressKey(ImGuiKey_F);
    session.Run(60);
    const float zoomAfter = session.Use([] { return test::CurrentZoom(); });
    test::Check(zoomAfter == zoom, "F over the new node centers it without zooming: zoom %g -> %g", zoom, zoomAfter);
}

TEST_CASE(EditorDestroyedWhileAnimating, "lifetime.editor_destroyed_while_navigating_and_flowing", "INT-20", test::TestFlags_None)
{
    // The editor ends while navigation and a flow are still playing.
    ImVec2 before, after;
    {
        TwoNodes nodes;
        test::Storage storage;
        test::Session session(&storage);
        session.DefaultContent = [&](int frame)
        {
            nodes.Submit(frame);
            if (frame == 3)
            {
                ed::Flow(100);
                ed::NavigateToContent(1.0f);
            }
        };
        session.Run(5);
        before = session.Use([] { return ed::ScreenToCanvas(ImVec2(0.0f, 0.0f)); });
        session.Run(1);
        after = session.Use([] { return ed::ScreenToCanvas(ImVec2(0.0f, 0.0f)); });
    }
    test::Check(before.x != after.x || before.y != after.y, "the view was still moving when the editor ended");
}
