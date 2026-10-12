//------------------------------------------------------------------------------
// The delete protocol: what the application is told, and when.
//------------------------------------------------------------------------------
# include "harness.h"
# include <algorithm>
# include <cstdio>
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
            return;
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
            }
        }
    }

    bool HasLink(uintptr_t id) const
    {
        return std::any_of(Links.begin(), Links.end(), [id](const LinkData& link) { return link.ID == id; });
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

TEST_CASE(DependentLinksLinksFirst, "deletion.dependent_links_reported_when_links_are_queried_first", "CORE-1", test::TestFlags_None)
{
    test::Storage storage;
    test::Session session(&storage);
    Document document;
    DeleteMiddleNode(session, document);

    test::Check(Contains(document.Log, "frame 4: node 11 accepted"), "node 11 deleted");
    test::Check(Contains(document.Log, "frame 5: link 100 (3 -> 12) accepted") && Contains(document.Log, "frame 5: link 101 (13 -> 22) accepted"),
        "links of node 11 reported in the next frame, with their pins");
    test::Check(document.Log.size() == 3, "nothing else reported: %d entries", (int)document.Log.size());
    test::Check(document.Links.empty() && document.FailedLinks.empty(), "document and editor agree on links: %d in document, %d rejected by the editor", (int)document.Links.size(), (int)document.FailedLinks.size());
}

TEST_CASE(DependentLinksNodesFirst, "deletion.dependent_links_reported_when_nodes_are_queried_first", "CORE-1", test::TestFlags_None)
{
    test::Storage storage;
    test::Session session(&storage);
    Document document;
    document.LinksFirst = false;
    DeleteMiddleNode(session, document);

    test::Check(Contains(document.Log, "frame 4: node 11 accepted"), "node 11 deleted");
    test::Check(Contains(document.Log, "frame 4: link 100 (3 -> 12) accepted") && Contains(document.Log, "frame 4: link 101 (13 -> 22) accepted"),
        "links of node 11 reported in the same interaction, with their pins");
    test::Check(document.Log.size() == 3, "nothing else reported: %d entries", (int)document.Log.size());
    test::Check(document.Links.empty() && document.FailedLinks.empty(), "document and editor agree on links: %d in document, %d rejected by the editor", (int)document.Links.size(), (int)document.FailedLinks.size());
}

TEST_CASE(DependentLinksOfUnsubmittedLinks, "deletion.dependent_links_reported_when_deletions_come_before_links", "CORE-1", test::TestFlags_None)
{
    // Links aren't live yet when the node is accepted.
    test::Storage storage;
    test::Session session(&storage);
    Document document;
    document.DeleteBeforeLinks = true;
    DeleteMiddleNode(session, document);

    test::Check(Contains(document.Log, "frame 4: node 11 accepted"), "node 11 deleted");
    test::Check(Contains(document.Log, "frame 5: link 100 (3 -> 12) accepted") && Contains(document.Log, "frame 5: link 101 (13 -> 22) accepted"),
        "links of node 11 reported in the next frame, with their pins");
    test::Check(document.Links.empty() && document.FailedLinks.empty(), "document and editor agree on links: %d in document, %d rejected by the editor", (int)document.Links.size(), (int)document.FailedLinks.size());

    ed::PinId startId, endId;
    const bool pins = session.Use([&] { return ed::GetLinkPins(100, &startId, &endId); });
    test::Check(!pins, "link 100 is gone from the editor");
}

TEST_CASE(LinkRejectedThenDeletedWithNode, "deletion.link_rejected_before_its_node_is_deleted", "CORE-1", test::TestFlags_None)
{
    // Link 100 and node 11 are deleted together. The application keeps the link,
    // then deletes the node, which takes the link with it.
    test::Storage storage;
    test::Session session(&storage);
    Document document;
    document.RejectLink = 100;
    for (int i = 0; i < 10; ++i)
        session.Frame([&](int frame)
        {
            if (frame == 3)
            {
                ed::DeleteLink(100);
                ed::DeleteNode(11);
            }
            document.Frame(frame);
        });
    document.PrintLog();

    test::Check(Contains(document.Log, "frame 4: link 100 (3 -> 12) rejected") && Contains(document.Log, "frame 4: node 11 accepted"), "link kept, node deleted");
    test::Check(Contains(document.Log, "frame 5: link 100 (3 -> 12) accepted"), "link reported again once its node took it");
    test::Check(document.Links.empty() && document.FailedLinks.empty(), "document and editor agree on links: %d in document, %d rejected by the editor", (int)document.Links.size(), (int)document.FailedLinks.size());
}

TEST_CASE(RejectedDependentLink, "deletion.rejected_dependent_link_asserts", "CORE-1", test::TestFlags_None)
{
    // A link deleted together with its node can't be kept.
    test::ExpectAsserts(1);
    test::Storage storage;
    test::Session session(&storage);
    Document document;
    document.LinksFirst = false;
    document.RejectLink = 100;
    DeleteMiddleNode(session, document);

    test::Check(Contains(document.Log, "frame 4: link 100 (3 -> 12) rejected"), "link 100 rejected");
    test::Check(document.HasLink(100) && std::find(document.FailedLinks.begin(), document.FailedLinks.end(), 100) != document.FailedLinks.end(),
        "the editor deleted it anyway: ed::Link(100) fails");
}

TEST_CASE(DependentLinkDeselected, "deletion.dependent_link_deselected_when_deleted", "CORE-1", test::TestFlags_None)
{
    test::Storage storage;
    test::Session session(&storage);
    Document document;
    bool selectedAfterDelete = true;
    for (int i = 0; i < 6; ++i)
        session.Frame([&](int frame)
        {
            if (frame == 2)
                ed::SelectLink(100);
            if (frame == 3)
                ed::DeleteNode(11);
            document.Frame(frame);
            if (frame == 4)
                selectedAfterDelete = ed::IsLinkSelected(100) || ed::GetSelectedObjectCount() != 0;
        });
    document.PrintLog();

    test::Check(Contains(document.Log, "frame 4: node 11 accepted"), "node 11 deleted");
    test::Check(!selectedAfterDelete, "link 100 deselected in the frame it was deleted with its node");
}
