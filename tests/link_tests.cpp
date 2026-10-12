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
