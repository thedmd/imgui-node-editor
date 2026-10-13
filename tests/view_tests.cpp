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
    auto content = [](int)
    {
        ed::BeginNode(1);
        ImGui::TextUnformatted("Node");
        ed::EndNode();
    };

    {
        test::Storage storage;
        test::Session session(&storage);
        session.Frame(content);
        session.Frame(content);
        const int gridVertices = session.Use([&] { return GridVertices(session.DrawList); });
        test::Check(gridVertices > 0, "grid drawn at zoom 1: %d vertices", gridVertices);
    }

    // Zoom levels down to 1e-6, where the view is 10^9 units across. Content can't
    // be spread that far, since the canvas ends 250000 from the origin.
    test::Storage storage;
    storage.Global = R"({"view":{"scroll":{"x":0,"y":0},"zoom":0.000001}})";
    auto config = test::MakeConfig(&storage);
    config.CustomZoomLevels.push_back(1e-6f);
    config.CustomZoomLevels.push_back(1.0f);
    test::Session session(config);
    session.Settings = &storage;
    for (int i = 0; i < 3; ++i)
        session.Frame(content);
    const float zoom = session.Use([] { return ed::GetCurrentZoom(); });
    test::Check(zoom < 1e-5f, "view zoomed out to %g", zoom);
}
