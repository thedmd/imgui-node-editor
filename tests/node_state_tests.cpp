//------------------------------------------------------------------------------
// Per-node state provided by the application through Config::LoadNodeSettings.
//------------------------------------------------------------------------------
# include "harness.h"
# include <cmath>

namespace {

void SimpleNode(int nodeId)
{
    ed::BeginNode(nodeId);
    ImGui::Text("Node %d", nodeId);
    ImGui::Dummy(ImVec2(120.0f, 40.0f));
    ed::EndNode();
}

bool Near(const ImVec2& a, const ImVec2& b)
{
    return fabsf(a.x - b.x) <= 0.01f && fabsf(a.y - b.y) <= 0.01f;
}

// Document authored in an earlier session: node 1 at (320, 180), stored per node.
test::Storage AuthorDocument()
{
    test::Storage document;
    test::Session session(&document);
    for (int i = 0; i < 3; ++i)
        session.Frame([](int frame)
        {
            if (frame == 0)
                ed::SetNodePosition(1, ImVec2(320.0f, 180.0f));
            SimpleNode(1);
        });
    return document;
}

} // namespace

TEST_CASE(NodeStateReopen, "node_state.reopen_restores_document_state", "CORE-11", test::TestFlags_None)
{
    const auto document = AuthorDocument();

    test::Storage storage;
    storage.Nodes = document.Nodes;
    test::Session session(&storage);
    for (int i = 0; i < 3; ++i)
        session.Frame([](int) { SimpleNode(1); });

    const auto position = session.Use([] { return ed::GetNodePosition(1); });
    test::Check(Near(position, ImVec2(320.0f, 180.0f)), "node restored from the document: (%g, %g)", position.x, position.y);
    test::Check(storage.Nodes[1] == document.Nodes.at(1), "stored state left intact: %s", storage.Nodes[1].c_str());
}

TEST_CASE(NodeStateStaleSession, "node_state.stale_session_state_loses", "CORE-11", test::TestFlags_None)
{
    const auto document = AuthorDocument();

    test::Storage storage;
    storage.Nodes  = document.Nodes;
    storage.Global = R"({"nodes":{"node:1":{"location":{"x":10.0,"y":10.0}}}})";
    test::Session session(&storage);
    for (int i = 0; i < 3; ++i)
        session.Frame([](int) { SimpleNode(1); });

    const auto position = session.Use([] { return ed::GetNodePosition(1); });
    test::Check(Near(position, ImVec2(320.0f, 180.0f)), "per-node state wins over stale session state: (%g, %g)", position.x, position.y);
}

TEST_CASE(NodeStateExplicitRestore, "node_state.explicit_restore", "CORE-11", test::TestFlags_None)
{
    test::Storage storage;
    test::Session session(&storage);
    for (int i = 0; i < 2; ++i)
        session.Frame([](int) { SimpleNode(1); });

    storage.Nodes[1] = R"({"location":{"x":500.0,"y":260.0}})";
    session.Use([] { ed::RestoreNodeState(1); });
    for (int i = 0; i < 2; ++i)
        session.Frame([](int) { SimpleNode(1); });

    const auto position = session.Use([] { return ed::GetNodePosition(1); });
    test::Check(Near(position, ImVec2(500.0f, 260.0f)), "RestoreNodeState() applies the stored state: (%g, %g)", position.x, position.y);
}

// Issue #153: a node deleted through the editor, whose state the application
// dropped, is created again with the same ID.
TEST_CASE(NodeStateRecreate, "node_state.recreated_node_starts_fresh", "CORE-11", test::TestFlags_None)
{
    test::Storage storage;
    test::Session session(&storage);
    bool exists = true;
    for (int i = 0; i < 7; ++i)
        session.Frame([&](int frame)
        {
            if (frame == 0)
                ed::SetNodePosition(1, ImVec2(320.0f, 180.0f));
            if (frame == 4)
                exists = true;
            if (exists)
                SimpleNode(1);
            if (frame == 1)
                ed::DeleteNode(1);
            if (ed::BeginDelete())
            {
                ed::NodeId nodeId;
                while (ed::QueryDeletedNode(&nodeId))
                {
                    if (ed::AcceptDeletedItem())
                    {
                        storage.Nodes.erase(nodeId.Get());
                        exists = false;
                    }
                }
                ed::EndDelete();
            }
        });

    const auto position = session.Use([] { return ed::GetNodePosition(1); });
    test::Check(Near(position, ImVec2(0.0f, 0.0f)), "re-created node starts at the origin: (%g, %g)", position.x, position.y);
}
