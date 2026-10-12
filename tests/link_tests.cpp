//------------------------------------------------------------------------------
// What the editor draws, culls and hit tests of a link.
//------------------------------------------------------------------------------
# include "harness.h"

namespace {

const ImU32 c_LinkColor = IM_COL32(255, 0, 0, 255);

// Link 100 between pins whose pivots are points in canvas space; their nodes are
// out of the way.
struct LinkScene
{
    ImVec2 Start;
    ImVec2 End;
    ImVec2 StartDirection = ImVec2(1.0f, 0.0f);
    ImVec2 EndDirection   = ImVec2(-1.0f, 0.0f);
    float  Thickness      = 1.0f;

    void Submit(int frame) const
    {
        const ImVec2 pivots[2] = { Start, End };
        ed::PushStyleVar(ed::StyleVar_SourceDirection, StartDirection);
        ed::PushStyleVar(ed::StyleVar_TargetDirection, EndDirection);
        for (int n = 0; n < 2; ++n)
        {
            if (frame == 0)
                ed::SetNodePosition(n + 1, ImVec2(n * 300.0f, 2000.0f));
            ed::BeginNode(n + 1);
            ed::BeginPin(11 + n, n == 0 ? ed::PinKind::Output : ed::PinKind::Input);
            ImGui::TextUnformatted("Pin");
            ed::PinPivotRect(pivots[n], pivots[n]);
            ed::EndPin();
            ed::EndNode();
        }
        ed::PopStyleVar(2);
        ed::Link(100, 11, 12, ImColor(c_LinkColor), Thickness);
    }
};

int CountLinkVertices(const test::Session& session)
{
    int count = 0;
    for (auto color : test::Capture(session.DrawList).Colors)
        if (color == c_LinkColor)
            ++count;
    return count;
}

} // namespace

TEST_CASE(ThickLinkAtEdge, "links.thick_link_partly_on_screen_is_drawn", "CORE-15", test::TestFlags_None)
{
    // A link 10 thick along the editor's top edge, its centerline 2 above it.
    test::Storage storage;
    test::Session session(&storage);
    ImVec2 editorOrigin;
    session.BeforeEditor = [&] { editorOrigin = ImGui::GetCursorScreenPos(); };
    LinkScene scene;
    scene.Thickness = 10.0f;
    session.DefaultContent = [&scene](int frame) { scene.Submit(frame); };
    session.Run(2);

    const auto top = session.Use([&] { return ed::ScreenToCanvas(editorOrigin).y; });
    scene.Start = ImVec2(100.0f, top - 2.0f);
    scene.End   = ImVec2(600.0f, top - 2.0f);
    session.Run(2);

    const auto vertices = CountLinkVertices(session);
    test::Check(vertices > 0, "the 3 of the link on screen are drawn: %d vertices", vertices);
}

TEST_CASE(HitBand, "links.hit_band_is_the_same_in_every_direction", "CORE-15", test::TestFlags_None)
{
    // A link is hovered up to its thickness and 5 more from its centerline; the mouse
    // is 0.25 inside or 0.75 outside of that.
    struct Case { bool Vertical; float Thickness; float Distance; bool Hovered; };
    const Case cases[] =
    {
        { false,  1.0f,  5.75f, true }, { false,  1.0f,  6.75f, false },
        { true,   1.0f,  5.75f, true }, { true,   1.0f,  6.75f, false },
        { false, 10.0f, 14.75f, true }, { false, 10.0f, 15.75f, false },
        { true,  10.0f, 14.75f, true }, { true,  10.0f, 15.75f, false },
    };
    int wrong = 0;
    for (const auto& c : cases)
    {
        test::Storage storage;
        test::Session session(&storage);
        LinkScene scene;
        scene.Thickness = c.Thickness;
        session.DefaultContent = [&scene](int frame) { scene.Submit(frame); };
        session.Run(2);

        const auto mouse = session.Use([] { return ImFloor(ed::CanvasToScreen(ImVec2(300.0f, 300.0f))); });
        const auto at    = session.Use([&mouse] { return ed::ScreenToCanvas(mouse); });
        if (c.Vertical)
        {
            scene.Start          = ImVec2(at.x - c.Distance, at.y - 200.0f);
            scene.End            = ImVec2(at.x - c.Distance, at.y + 200.0f);
            scene.StartDirection = ImVec2(0.0f, 1.0f);
            scene.EndDirection   = ImVec2(0.0f, -1.0f);
        }
        else
        {
            scene.Start = ImVec2(at.x - 200.0f, at.y - c.Distance);
            scene.End   = ImVec2(at.x + 200.0f, at.y - c.Distance);
        }
        test::MouseMove(mouse);
        session.Run(2);

        const bool hovered = session.Use([] { return ed::GetHoveredLink() == ed::LinkId(100); });
        if (hovered != c.Hovered)
        {
            ++wrong;
            test::Check(false, "a %s link %g thick is %s at %g from it", c.Vertical ? "vertical" : "horizontal", c.Thickness, hovered ? "hovered" : "not hovered", c.Distance);
        }
    }

    test::Check(wrong == 0, "horizontal and vertical links are hovered up to their thickness and 5 from them");
}
