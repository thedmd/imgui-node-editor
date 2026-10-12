//------------------------------------------------------------------------------
// Objects the editor frees while other state still points at them. These are
// use-after-free errors, which only AddressSanitizer reports reliably.
//------------------------------------------------------------------------------
# include "harness.h"

namespace {

void TwoNodes(int frame)
{
    for (int n = 0; n < 2; ++n)
    {
        const uintptr_t id = 1 + n * 10;
        if (frame == 0)
            ed::SetNodePosition(id, ImVec2(100.0f + n * 300.0f, 100.0f));
        ed::BeginNode(id);
        ImGui::TextUnformatted("Node");
        ed::BeginPin(id + 1, ed::PinKind::Input);
        ImGui::TextUnformatted("> in");
        ed::EndPin();
        ImGui::SameLine();
        ed::BeginPin(id + 2, ed::PinKind::Output);
        ImGui::TextUnformatted("out >");
        ed::EndPin();
        ed::EndNode();
    }
}

} // namespace

TEST_CASE(FlowOfDeletedLink, "lifetime.flow_animation_of_deleted_link", "INT-1", test::TestFlags_KnownFailure | test::TestFlags_NeedsSanitizer)
{
    test::Storage storage;
    test::Session session(&storage);
    bool linkAlive = true;
    for (int i = 0; i < 30; ++i)
        session.Frame([&](int frame)
        {
            TwoNodes(frame);
            if (linkAlive)
                ed::Link(100, 3, 12);
            if (frame == 3)
                ed::Flow(100);
            if (frame == 6)
                ed::DeleteLink(100);
            if (ed::BeginDelete())
            {
                ed::LinkId linkId;
                while (ed::QueryDeletedLink(&linkId))
                    if (ed::AcceptDeletedItem())
                        linkAlive = false;
                ed::EndDelete();
            }
        });
    test::Check(!linkAlive, "link deleted while its flow animation was playing");
}

TEST_CASE(SelectedLinkDeletedWithNode, "lifetime.selected_link_deleted_with_node", "CORE-1", test::TestFlags_KnownFailure | test::TestFlags_NeedsSanitizer)
{
    test::Storage storage;
    test::Session session(&storage);
    bool linkAlive = true, node2Alive = true;
    for (int i = 0; i < 12; ++i)
        session.Frame([&](int frame)
        {
            for (int n = 0; n < 2; ++n)
            {
                if (n == 1 && !node2Alive)
                    continue;
                const uintptr_t id = 1 + n * 10;
                if (frame == 0)
                    ed::SetNodePosition(id, ImVec2(100.0f + n * 300.0f, 100.0f));
                ed::BeginNode(id);
                ImGui::TextUnformatted("Node");
                ed::BeginPin(id + 1, ed::PinKind::Input);
                ImGui::TextUnformatted("> in");
                ed::EndPin();
                ImGui::SameLine();
                ed::BeginPin(id + 2, ed::PinKind::Output);
                ImGui::TextUnformatted("out >");
                ed::EndPin();
                ed::EndNode();
            }
            if (linkAlive)
                ed::Link(100, 3, 12);
            if (frame == 3)
                ed::SelectLink(100);
            if (frame == 5)
                ed::DeleteNode(11);

            if (ed::BeginDelete())
            {
                // Links first, the order imgui_node_editor.h declares the queries in.
                ed::LinkId linkId;
                while (ed::QueryDeletedLink(&linkId))
                    if (ed::AcceptDeletedItem())
                        linkAlive = false;
                ed::NodeId nodeId;
                while (ed::QueryDeletedNode(&nodeId))
                {
                    if (ed::AcceptDeletedItem())
                    {
                        node2Alive = false;
                        linkAlive  = false; // the application drops the links of deleted nodes
                    }
                }
                ed::EndDelete();
            }
        });
    test::Check(!node2Alive, "node deleted together with its selected link");
}

TEST_CASE(ManualDeleteQueue, "lifetime.manual_delete_queue_outlives_objects", "API-1", test::TestFlags_KnownFailure | test::TestFlags_NeedsSanitizer)
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
            title = ed::CanvasToScreen((ImGui::GetItemRectMin() + ImGui::GetItemRectMax()) * 0.5f);
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
            ed::EndDelete();
        }
    };

    session.Run(3);
    session.Click(title);
    session.PressKey(ImGuiKey_Delete);
    session.Run(10);
    test::Check(!nodeAlive, "node deleted");
}
