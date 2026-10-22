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
NodeText ShowNodeText(float zoom, bool rasterizeTextForZoom = true)
{
    test::Storage storage;
    storage.Global = R"({"view":{"scroll":{"x":0,"y":0},"zoom":)" + std::to_string(zoom) + "}}";
    auto config = test::MakeConfig(&storage);
    config.CustomZoomLevels.push_back(1.0f);
    config.CustomZoomLevels.push_back(3.0f);
    config.RasterizeTextForZoom = rasterizeTextForZoom;
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

TEST_CASE(ZoomedTextOff, "view.text_keeps_its_density_without_rasterize_text_for_zoom", nullptr, test::TestFlags_None)
{
# if IMGUI_VERSION_NUM >= 19200
    const auto zoomed = ShowNodeText(3.0f, false);
    test::Check(zoomed.Zoom == 3.0f, "view at zoom 3: %g", zoomed.Zoom);
    test::Check(zoomed.Density == 1.0f, "with Config::RasterizeTextForZoom off, the node's text is rasterized at 1: %g", zoomed.Density);
# else
    test::Skip("needs Dear ImGui 1.92, which rasterizes glyphs on demand");
# endif
}

TEST_CASE(ResizedViewStaysCentered, "view.a_canvas_resized_frame_by_frame_keeps_the_center_of_its_view", nullptr, test::TestFlags_None)
{
    // Its height shrinks by a pixel a frame, as when a bar is dragged over its bottom edge, and
    // grows back.
    test::Storage storage;
    test::Session session(test::MakeConfig(&storage));
    session.EditorSize = ImVec2(600.0f, 400.0f);
    session.Run(3);
    auto center = [&]
    {
        const auto state = session.Use([] { return ed::GetViewState(); });
        return (state.VisibleMin + state.VisibleMax) * 0.5f;
    };
    const auto before = center();
    for (int i = 0; i < 120; ++i)
    {
        session.EditorSize.y -= 1.0f;
        session.Run(1);
    }
    for (int i = 0; i < 120; ++i)
    {
        session.EditorSize.y += 1.0f;
        session.Run(1);
    }
    const auto after = center();
    test::Check(ImFabs(after.x - before.x) < 1.0f && ImFabs(after.y - before.y) < 1.0f, "the view's center stays at (%g, %g): it is at (%g, %g)", before.x, before.y, after.x, after.y);
}

TEST_CASE(ResizedViewKeepsTopLeft, "view.a_canvas_that_keeps_its_top_left_shows_its_content_where_it_was", nullptr, test::TestFlags_None)
{
    // With CanvasSizeMode::KeepTopLeft, a canvas resized at its bottom and right edges, as by a
    // panel's bar over it, neither moves nor zooms what it shows.
    test::Storage storage;
    auto config = test::MakeConfig(&storage);
    config.CanvasSizeMode = ed::CanvasSizeMode::KeepTopLeft;
    test::Session session(config);
    session.EditorSize = ImVec2(600.0f, 400.0f);
    session.Run(3);
    auto shown = [&] { return session.Use([] { return ed::CanvasToScreen(ImVec2(100.0f, 50.0f)); }); };
    auto zoom  = [&] { return session.Use([] { return ed::GetCurrentZoom(); }); };
    const auto before     = shown();
    const auto zoomBefore = zoom();
    for (int i = 0; i < 60; ++i)
    {
        session.EditorSize += ImVec2(1.0f, -2.0f);
        session.Run(1);
    }
    const auto after     = shown();
    const auto zoomAfter = zoom();
    test::Check(after.x == before.x && after.y == before.y && zoomAfter == zoomBefore, "a point shown at (%g, %g), zoom %g, stays: (%g, %g), zoom %g", before.x, before.y, zoomBefore, after.x, after.y, zoomAfter);
}
