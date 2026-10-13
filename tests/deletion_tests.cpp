//------------------------------------------------------------------------------
// The delete protocol: what the application is told, and when.
//------------------------------------------------------------------------------
# include "harness.h"
# include <algorithm>
# include <cstdio>
# include <functional>
# include <string>
# include <vector>

namespace {

// An application's document: nodes 1, 11 and 21 in a row, each with an input
// (node ID + 1) and an output (node ID + 2) pin, and links 100 and 101 that
// connect them. The document drops a link only when the editor reports it.
struct Document
{
    struct LinkData { uintptr_t ID, Start, End; };

    std::vector<uintptr_t> Nodes = { 1, 11, 21 };
    std::vector<LinkData>  Links = { { 100, 3, 12 }, { 101, 13, 22 } };

    bool LinksFirst        = true;  // query order inside BeginDelete()/EndDelete()
    bool DeleteBeforeLinks = false; // handle deletions before the links are submitted
    int  RejectLink        = 0;     // reject this link the first time it is reported

    std::function<void()>          BeforeQueries; // runs inside BeginDelete()/EndDelete()
    std::function<void(uintptr_t)> NodeDeleted;

    std::vector<std::string> Log;
    std::vector<uintptr_t>   FailedLinks; // ed::Link() calls of the last frame that returned false

    void Frame(int frame)
    {
        SubmitNodes(frame);
        if (DeleteBeforeLinks)
            HandleDeletions(frame);
        SubmitLinks();
        if (!DeleteBeforeLinks)
            HandleDeletions(frame);
    }

    void SubmitNodes(int frame)
    {
        for (auto id : Nodes)
        {
            if (frame == 0)
                ed::SetNodePosition(id, ImVec2(100.0f + (id / 10) * 250.0f, 100.0f));
            ed::BeginNode(id);
            ImGui::Text("Node %d", (int)id);
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

    void SubmitLinks()
    {
        FailedLinks.clear();
        for (auto& link : Links)
            if (!ed::Link(link.ID, link.Start, link.End))
                FailedLinks.push_back(link.ID);
    }

    void HandleDeletions(int frame)
    {
        if (!ed::BeginDelete())
        {
            ed::EndDelete();
            return;
        }
        if (BeforeQueries)
            BeforeQueries();
        if (LinksFirst)
        {
            DeleteLinks(frame);
            DeleteNodes(frame);
        }
        else
        {
            DeleteNodes(frame);
            DeleteLinks(frame);
        }
        ed::EndDelete();
    }

    void DeleteLinks(int frame)
    {
        ed::LinkId linkId;
        ed::PinId  startId, endId;
        while (ed::QueryDeletedLink(&linkId, &startId, &endId))
        {
            char entry[128];
            snprintf(entry, sizeof(entry), "frame %d: link %d (%d -> %d)", frame, (int)linkId.Get(), (int)startId.Get(), (int)endId.Get());
            if ((int)linkId.Get() == RejectLink)
            {
                ed::RejectDeletedItem();
                RejectLink = 0;
                Log.push_back(std::string(entry) + " rejected");
            }
            else if (ed::AcceptDeletedItem())
            {
                Links.erase(std::remove_if(Links.begin(), Links.end(), [&](const LinkData& link) { return link.ID == linkId.Get(); }), Links.end());
                Log.push_back(std::string(entry) + " accepted");
            }
        }
    }

    void DeleteNodes(int frame)
    {
        ed::NodeId nodeId;
        while (ed::QueryDeletedNode(&nodeId))
        {
            if (ed::AcceptDeletedItem())
            {
                Nodes.erase(std::remove(Nodes.begin(), Nodes.end(), nodeId.Get()), Nodes.end());
                Log.push_back("frame " + std::to_string(frame) + ": node " + std::to_string(nodeId.Get()) + " accepted");
                if (NodeDeleted)
                    NodeDeleted(nodeId.Get());
            }
        }
    }

    bool HasLink(uintptr_t id) const
    {
        return std::any_of(Links.begin(), Links.end(), [id](const LinkData& link) { return link.ID == id; });
    }

    int Reports(const char* item) const
    {
        return (int)std::count_if(Log.begin(), Log.end(), [item](const std::string& entry) { return entry.find(item) != std::string::npos; });
    }

    int FrameOf(const char* item) const
    {
        for (auto& entry : Log)
            if (entry.find(item) != std::string::npos)
                return std::stoi(entry.substr(6)); // "frame N: ..."
        return -1;
    }

    void PrintLog() const
    {
        for (auto& entry : Log)
            printf("    | %s\n", entry.c_str());
    }
};

bool Contains(const std::vector<std::string>& log, const char* entry)
{
    return std::find(log.begin(), log.end(), entry) != log.end();
}

// Deletes node 11, with links 100 and 101 attached, through ed::DeleteNode() in frame 3.
void DeleteMiddleNode(test::Session& session, Document& document, int frames = 10)
{
    for (int i = 0; i < frames; ++i)
        session.Frame([&](int frame)
        {
            if (frame == 3)
                ed::DeleteNode(11);
            document.Frame(frame);
        });
    document.PrintLog();
}

} // namespace

TEST_CASE(AcceptTwice, "deletion.second_accept_leaves_next_candidate", "API-2", test::TestFlags_None)
{
    // Nodes 1 and 21 are deleted together. The application accepts node 1 twice
    // and keeps node 21.
    test::ExpectAsserts(1);
    test::Storage storage;
    test::Session session(&storage);
    Document document;
    document.Links.clear();
    bool secondAccept = true;
    std::string offered;
    for (int i = 0; i < 8; ++i)
        session.Frame([&](int frame)
        {
            document.SubmitNodes(frame);
            if (frame == 3)
            {
                ed::DeleteNode(1);
                ed::DeleteNode(21);
            }
            if (ed::BeginDelete())
            {
                ed::NodeId nodeId;
                while (ed::QueryDeletedNode(&nodeId))
                {
                    offered += " " + std::to_string(nodeId.Get());
                    if (nodeId.Get() == 1 && ed::AcceptDeletedItem())
                    {
                        document.Nodes.erase(std::remove(document.Nodes.begin(), document.Nodes.end(), 1), document.Nodes.end());
                        secondAccept = ed::AcceptDeletedItem();
                    }
                    else
                        ed::RejectDeletedItem();
                }
            }
            ed::EndDelete();
        });

    const ImVec2 position = session.Use([] { return ed::GetNodePosition(21); });
    test::Check(!secondAccept, "second AcceptDeletedItem() for node 1 returns false");
    test::Check(offered == " 1 21", "both nodes offered:%s", offered.c_str());
    test::Check(position.x == 600.0f && position.y == 100.0f, "node 21 kept in place: (%g, %g)", position.x, position.y);
}

TEST_CASE(RejectAfterLoop, "deletion.reject_after_query_loop_does_nothing", "API-2", test::TestFlags_None)
{
    test::ExpectAsserts(1);
    test::Storage storage;
    test::Session session(&storage);
    Document document;
    document.LinksFirst = false;
    bool rejected = false;
    for (int i = 0; i < 8; ++i)
        session.Frame([&](int frame)
        {
            document.SubmitNodes(frame);
            document.SubmitLinks();
            if (frame == 3)
                ed::DeleteNode(11);
            if (ed::BeginDelete())
            {
                document.DeleteNodes(frame);
                document.DeleteLinks(frame);
                if (!rejected)
                {
                    ed::RejectDeletedItem();
                    rejected = true;
                }
            }
            ed::EndDelete();
        });
    document.PrintLog();

    test::Check(rejected && document.Reports("node 11 accepted") == 1, "node 11 deleted, then RejectDeletedItem() called");
    test::Check(document.Links.empty(), "links of node 11 deleted with it: %d left", (int)document.Links.size());
}
