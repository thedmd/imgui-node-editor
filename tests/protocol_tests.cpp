//------------------------------------------------------------------------------
// Create, delete and shortcut protocols.
//------------------------------------------------------------------------------
# include "harness.h"
# include <map>

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
            ed::EndCreate();
        }
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
            ed::EndCreate();
        }
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

TEST_CASE(EndCreateAfterFailedBegin, "protocols.end_create_after_failed_begin_asserts", "API-11", test::TestFlags_None)
{
    test::ExpectAsserts(1);
    test::Storage storage;
    test::Session session(&storage);
    session.Frame([](int)
    {
        if (!ed::BeginCreate())
            ed::EndCreate();
    });
}

TEST_CASE(EndDeleteAfterFailedBegin, "protocols.end_delete_after_failed_begin_asserts", "DEV-2", test::TestFlags_None)
{
    test::ExpectAsserts(1);
    test::Storage storage;
    test::Session session(&storage);
    session.Frame([](int)
    {
        if (!ed::BeginDelete())
            ed::EndDelete();
    });
}

TEST_CASE(EndShortcutAfterFailedBegin, "protocols.end_shortcut_after_failed_begin_asserts", "DEV-2", test::TestFlags_None)
{
    test::ExpectAsserts(1);
    test::Storage storage;
    test::Session session(&storage);
    session.Frame([](int)
    {
        if (!ed::BeginShortcut())
            ed::EndShortcut();
    });
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
            ed::EndCreate();
        }
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
