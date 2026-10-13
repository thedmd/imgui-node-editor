//------------------------------------------------------------------------------
// The view: where navigation takes it, and the grid drawn under it.
//------------------------------------------------------------------------------
# include "harness.h"
# include <cmath>
# include <string>

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

# if IMGUI_VERSION_NUM >= 19200
struct NodeText
{
    float  Zoom    = 0.0f;
    float  Density = 0.0f; // the density the node's text is rasterized at
    ImVec2 Size;
};

// A node with a line of text, in a view loaded at 'zoom'.
NodeText ShowNodeText(float zoom)
{
    test::Storage storage;
    storage.Global = R"({"view":{"scroll":{"x":0,"y":0},"zoom":)" + std::to_string(zoom) + "}}";
    auto config = test::MakeConfig(&storage);
    config.CustomZoomLevels.push_back(1.0f);
    config.CustomZoomLevels.push_back(3.0f);
    test::Session session(config);
    session.Settings = &storage;

    NodeText result;
    for (int i = 0; i < 3; ++i)
    {
        session.Frame([&](int)
        {
            ed::BeginNode(1);
            ImGui::TextUnformatted("A node with a line of text");
            result.Density = ImGui::GetFontBaked()->RasterizerDensity;
            ed::EndNode();
        });
    }
    session.Use([&]
    {
        result.Zoom = ed::GetCurrentZoom();
        result.Size = ed::GetNodeSize(1);
    });
    return result;
}
# endif

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

TEST_CASE(ZoomedText, "view.text_is_rasterized_for_the_zoom_and_nodes_keep_their_size", nullptr, test::TestFlags_None)
{
# if IMGUI_VERSION_NUM >= 19200
    const auto normal = ShowNodeText(1.0f);
    const auto zoomed = ShowNodeText(3.0f);
    test::Check(normal.Zoom == 1.0f && zoomed.Zoom == 3.0f, "views at zoom 1 and 3: %g and %g", normal.Zoom, zoomed.Zoom);
    test::Check(normal.Density == 1.0f && zoomed.Density == powf(2.0f, 1.5f), "the node's text is rasterized at 1 and 2^1.5: %g and %g", normal.Density, zoomed.Density);
    test::Check(zoomed.Size.x == normal.Size.x && zoomed.Size.y == normal.Size.y, "the node keeps its size: %g x %g, %g x %g at zoom 1", zoomed.Size.x, zoomed.Size.y, normal.Size.x, normal.Size.y);
# else
    test::Skip("needs Dear ImGui 1.92, which rasterizes glyphs on demand");
# endif
}
