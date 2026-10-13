//------------------------------------------------------------------------------
// Create, delete and shortcut protocols.
//------------------------------------------------------------------------------
# include "harness.h"
# include <map>
# include <vector>

namespace {

// Two nodes; pin 12 is the output of node 1, pin 21 the input of node 2.
void TwoNodes(int frame, std::map<int, ImVec2>& pins)
{
    for (int n = 1; n <= 2; ++n)
    {
        if (frame == 0)
            ed::SetNodePosition(n, ImVec2(100.0f + (n - 1) * 300.0f, 100.0f));
        ed::BeginNode(n);
        ImGui::Text("Node %d", n);
        const int pin = n == 1 ? 12 : 21;
        ed::BeginPin(pin, n == 1 ? ed::PinKind::Output : ed::PinKind::Input);
        ImGui::Text(n == 1 ? "Out ->" : "-> In");
        ed::EndPin();
        pins[pin] = ed::CanvasToScreen((ImGui::GetItemRectMin() + ImGui::GetItemRectMax()) * 0.5f);
        ed::EndNode();
    }
}

// Presses on pin 12 and moves over pin 21, leaving the button down.
void DragLinkOverTarget(test::Session& session, std::map<int, ImVec2>& pins)
{
    test::MouseMove(pins[12]);
    session.Run(2);
    test::MouseButton(0, true);
    session.Run(2);
    for (int i = 1; i <= 10; ++i)
    {
        test::MouseMove(pins[12] + (pins[21] - pins[12]) * (i / 10.0f));
        session.Run(1);
    }
    session.Run(2);
}

} // namespace

TEST_CASE(RejectedLinkOfferedOnce, "protocols.rejected_new_link_offered_once", "API-3", test::TestFlags_None)
{
    test::Storage storage;
    test::Session session(&storage);
    std::map<int, ImVec2> pins;
    bool released = false;
    int  offered  = 0;
    session.DefaultContent = [&](int frame)
    {
        TwoNodes(frame, pins);
        if (ed::BeginCreate())
        {
            ed::PinId start, end;
            if (ed::QueryNewLink(&start, &end) && start && end)
            {
                // Accepted while dragging, rejected once the link is to be created.
                if (released)
                {
                    ++offered;
                    ed::RejectNewItem();
                }
                else
                    ed::AcceptNewItem();
            }
        }
        ed::EndCreate();
    };

    session.Run(3);
    DragLinkOverTarget(session, pins);
    test::MouseButton(0, false);
    session.Run(1);
    released = true;
    session.Run(120);

    test::Check(offered == 1, "rejected link offered once, not on every frame: %d times in 120 frames", offered);
}

TEST_CASE(CancelledDragCreatesNothing, "protocols.cancelled_drag_creates_nothing", "API-3", test::TestFlags_None)
{
    test::Storage storage;
    test::Session session(&storage);
    std::map<int, ImVec2> pins;
    bool stealFocus   = false;
    bool counting     = false;
    int  beginCreates = 0;
    session.BeforeEditor = [&]
    {
        if (stealFocus)
            ImGui::SetWindowFocus(nullptr);
    };
    session.DefaultContent = [&](int frame)
    {
        TwoNodes(frame, pins);
        if (ed::BeginCreate())
        {
            if (counting)
                ++beginCreates;
            ed::PinId start, end;
            if (ed::QueryNewLink(&start, &end) && start && end)
                ed::AcceptNewItem();
        }
        ed::EndCreate();
    };

    session.Run(3);
    DragLinkOverTarget(session, pins);

    // Focus leaves the editor in the frame the application accepts the link,
    // which cancels the drag.
    stealFocus = true;
    session.Run(1);
    stealFocus = false;
    test::MouseButton(0, false);
    session.Run(1);
    counting = true;
    session.Run(10);

    test::Check(beginCreates == 0, "BeginCreate() is false after the drag was cancelled: true on %d of 10 frames", beginCreates);
}

TEST_CASE(NoSelfLink, "protocols.dropping_on_the_source_pin_offers_nothing", "API-11", test::TestFlags_None)
{
    test::Storage storage;
    test::Session session(&storage);
    std::map<int, ImVec2> pins;
    int selfLinks = 0;
    session.DefaultContent = [&](int frame)
    {
        TwoNodes(frame, pins);
        if (ed::BeginCreate())
        {
            ed::PinId start, end;
            if (ed::QueryNewLink(&start, &end) && start == end)
            {
                ++selfLinks;
                ed::AcceptNewItem();
            }
        }
        ed::EndCreate();
    };
    session.Run(3);

    // Away from pin 12 and back onto it, then release.
    test::MouseMove(pins[12]);
    session.Run(2);
    test::MouseButton(0, true);
    session.Run(2);
    for (int i = 1; i <= 10; ++i)
    {
        test::MouseMove(pins[12] + ImVec2(i <= 5 ? 12.0f * i : 12.0f * (10 - i), 0.0f));
        session.Run(1);
    }
    session.Run(2);
    test::MouseButton(0, false);
    session.Run(3);

    test::Check(selfLinks == 0, "pin 12 isn't offered a link to itself: %d frames did", selfLinks);
}

TEST_CASE(RepeatedQueries, "protocols.second_query_leaves_the_cursor", "API-11", test::TestFlags_None)
{
    test::Storage storage;
    test::Session session(&storage);
    std::map<int, ImVec2> pins;
    bool   queriedTwice = false;
    ImVec2 afterFirst, afterSecond;
    session.DefaultContent = [&](int frame)
    {
        TwoNodes(frame, pins);
        if (ed::BeginCreate())
        {
            ed::PinId start, end;
            if (ed::QueryNewLink(&start, &end))
            {
                ImGui::TextUnformatted("Link");
                afterFirst = ImGui::GetCursorScreenPos();
                if (ed::QueryNewLink(&start, &end))
                {
                    afterSecond  = ImGui::GetCursorScreenPos();
                    queriedTwice = true;
                }
            }
        }
        ed::EndCreate();
    };
    session.Run(3);
    DragLinkOverTarget(session, pins);
    test::MouseButton(0, false);
    session.Run(2);

    test::Check(queriedTwice && afterSecond.x == afterFirst.x && afterSecond.y == afterFirst.y,
        "a second QueryNewLink() leaves the cursor below the text: (%g, %g), was (%g, %g)", afterSecond.x, afterSecond.y, afterFirst.x, afterFirst.y);
}

TEST_CASE(PreviewThickness, "protocols.new_link_preview_has_the_requested_thickness", "API-11", test::TestFlags_None)
{
    // Link 100 joins the pins the preview snaps to, 3 px thick like the preview.
    const ImU32 linkColor    = IM_COL32(255, 0, 0, 255);
    const ImU32 previewColor = IM_COL32(0, 0, 255, 255);
    test::Storage storage;
    test::Session session(&storage);
    std::map<int, ImVec2> pins;
    session.DefaultContent = [&](int frame)
    {
        TwoNodes(frame, pins);
        ed::Link(100, 12, 21, ImColor(linkColor), 3.0f);
        if (ed::BeginCreate(ImColor(previewColor), 3.0f))
        {
            ed::PinId start, end;
            if (ed::QueryNewLink(&start, &end))
                ed::AcceptNewItem(ImColor(previewColor), 3.0f);
        }
        ed::EndCreate();
    };
    session.Run(3);
    DragLinkOverTarget(session, pins);

    auto positionsOf = [](const test::Geometry& geometry, ImU32 color)
    {
        std::vector<ImVec2> positions;
        for (size_t i = 0; i < geometry.Colors.size(); ++i)
            if ((geometry.Colors[i] & ~IM_COL32_A_MASK) == (color & ~IM_COL32_A_MASK))
                positions.push_back(geometry.Positions[i]);
        return positions;
    };
    const auto geometry = test::Capture(session.DrawList);
    const auto link     = positionsOf(geometry, linkColor);
    const auto preview  = positionsOf(geometry, previewColor);
    test::MouseButton(0, false);
    session.Run(2);

    bool same = !link.empty() && link.size() == preview.size();
    for (size_t i = 0; same && i < link.size(); ++i)
        same = ImFabs(link[i].x - preview[i].x) < 0.01f && ImFabs(link[i].y - preview[i].y) < 0.01f;
    test::Check(same, "the preview is drawn like a link of the requested thickness: %d and %d vertices", (int)preview.size(), (int)link.size());
}
