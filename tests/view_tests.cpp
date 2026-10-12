//------------------------------------------------------------------------------
// The view: where navigation takes it, and the grid drawn under it.
//------------------------------------------------------------------------------
# include "harness.h"

namespace {

// Vertices in the color of the grid at zoom 1.
int GridVertices(const ImDrawList* drawList)
{
    const ImU32 color = ImGui::ColorConvertFloat4ToU32(ed::GetStyle().Colors[ed::StyleColor_Grid]);
    int count = 0;
    for (auto& vertex : drawList->VtxBuffer)
        if (vertex.col == color)
            ++count;
    return count;
}

} // namespace

TEST_CASE(GridAtTinyZoom, "view.grid_at_tiny_zoom_is_bounded", "CORE-10", test::TestFlags_None)
{
    test::Storage storage;
    test::Session session(&storage);
    auto content = [](int frame)
    {
        if (frame == 0)
            ed::SetNodePosition(2, ImVec2(1.0e9f, 1.0e9f));
        for (uintptr_t id : { 1, 2 })
        {
            ed::BeginNode(id);
            ImGui::TextUnformatted("Node");
            ed::EndNode();
        }
        if (frame == 2)
            ed::NavigateToContent(0.0f);
    };

    session.Frame(content);
    session.Frame(content);
    const int gridVertices = session.Use([&] { return GridVertices(session.DrawList); });
    test::Check(gridVertices > 0, "grid drawn at zoom 1: %d vertices", gridVertices);

    // Content 10^9 units across: the view zooms out far below the zoom levels.
    for (int i = 0; i < 3; ++i)
        session.Frame(content);
    const float zoom = 1.0f / session.Use([] { return ed::GetCurrentZoom(); });
    test::Check(zoom < 1e-5f, "view zoomed out to %g", zoom);
}
