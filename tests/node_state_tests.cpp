//------------------------------------------------------------------------------
// Node layout the application keeps in its documents, as FormatNodeState() text.
//------------------------------------------------------------------------------
# include "harness.h"
# include <cmath>
# include <map>
# include <string>
# include <vector>

namespace {

using Document = std::map<uintptr_t, std::string>;

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

// The layout of every node the editor knows.
Document Keep(test::Session& session)
{
    return session.Use([]
    {
        std::vector<ed::NodeState> states(static_cast<size_t>(ed::GetNodeStates(nullptr, 0)));
        states.resize(static_cast<size_t>(ed::GetNodeStates(states.data(), static_cast<int>(states.size()))));

        Document document;
        for (auto& state : states)
            document[state.Id.Get()] = ed::FormatNodeState(state);
        return document;
    });
}

void Restore(test::Session& session, const Document& document)
{
    session.Use([&]
    {
        for (auto& node : document)
        {
            ed::NodeState state;
            state.Id = node.first;
            if (ed::ParseNodeState(node.second.c_str(), node.second.size(), &state))
                ed::SetNodeState(state);
        }
    });
}

// Document authored in an earlier session: node 1 at (320, 180).
Document AuthorDocument()
{
    test::Storage storage;
    test::Session session(&storage);
    for (int i = 0; i < 3; ++i)
        session.Frame([](int frame)
        {
            if (frame == 0)
                ed::SetNodePosition(1, ImVec2(320.0f, 180.0f));
            SimpleNode(1);
        });
    return Keep(session);
}

} // namespace

TEST_CASE(NodeStateReopen, "node_state.reopen_restores_document_state", "CORE-11", test::TestFlags_None)
{
    const auto document = AuthorDocument();

    test::Storage storage;
    test::Session session(&storage);
    Restore(session, document);
    for (int i = 0; i < 3; ++i)
        session.Frame([](int) { SimpleNode(1); });

    const auto position = session.Use([] { return ed::GetNodePosition(1); });
    test::Check(document.size() == 1, "the document holds node 1: %d nodes", static_cast<int>(document.size()));
    test::Check(Near(position, ImVec2(320.0f, 180.0f)), "node restored from the document: (%g, %g)", position.x, position.y);
}

TEST_CASE(NodeStateStaleSession, "node_state.stale_session_state_loses", "CORE-11", test::TestFlags_None)
{
    // The editor's own settings, loaded after the document, still place node 1 elsewhere.
    const auto document = AuthorDocument();

    test::Storage storage;
    storage.Global = R"({"nodes":{"node:1":{"location":{"x":10.0,"y":10.0}}}})";
    test::Session session(&storage);
    Restore(session, document);
    for (int i = 0; i < 3; ++i)
        session.Frame([](int) { SimpleNode(1); });

    const auto position = session.Use([] { return ed::GetNodePosition(1); });
    test::Check(Near(position, ImVec2(320.0f, 180.0f)), "the document wins over stale session state: (%g, %g)", position.x, position.y);
}

TEST_CASE(NodeStateExplicitRestore, "node_state.explicit_restore", "CORE-11", test::TestFlags_None)
{
    // The application restores node 1 from its document while the editor shows it.
    test::Storage storage;
    test::Session session(&storage);
    for (int i = 0; i < 2; ++i)
        session.Frame([](int) { SimpleNode(1); });

    Restore(session, { { 1, R"({"location":{"x":500.0,"y":260.0}})" } });
    for (int i = 0; i < 2; ++i)
        session.Frame([](int) { SimpleNode(1); });

    const auto position = session.Use([] { return ed::GetNodePosition(1); });
    test::Check(Near(position, ImVec2(500.0f, 260.0f)), "the document's state applies: (%g, %g)", position.x, position.y);
}

// Issue #153: a node deleted through the editor is created again with the same ID.
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
                        exists = false;
                }
                ed::EndDelete();
            }
        });

    const auto position = session.Use([] { return ed::GetNodePosition(1); });
    test::Check(Near(position, ImVec2(0.0f, 0.0f)), "re-created node starts at the origin: (%g, %g)", position.x, position.y);
}
