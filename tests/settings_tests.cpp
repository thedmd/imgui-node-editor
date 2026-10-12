//------------------------------------------------------------------------------
// Settings saved and loaded by the editor.
//------------------------------------------------------------------------------
# include "harness.h"
# include <clocale>
# include <cmath>
# include <cstdio>
# include <locale>
# include <string>
# include <vector>

namespace {

// Locales that write numbers with a decimal comma, in Windows and POSIX spelling.
// de-DE groups thousands with '.', pl-PL and fr-FR with a non-breaking space.
const char* const c_CommaLocales[] = { "de-DE", "pl-PL", "fr-FR", "de_DE.UTF-8", "de_DE.utf8", "pl_PL.UTF-8", "fr_FR.UTF-8", "de_DE" };

// A view with fractional scroll and zoom.
const char* const c_ViewState = R"({"view":{"scroll":{"x":-150.5,"y":-90.25},"zoom":1.5}})";

struct RoundTripResult
{
    ImVec2 Position;
    float  Zoom[2] = {}; // in the editor that saved, and in the one that loaded
};

// One editor saves node 1 at (1234, 2048) and the view it loaded, another loads them.
RoundTripResult RoundTrip(test::Storage& storage)
{
    RoundTripResult result;
    storage.Global = c_ViewState;
    {
        test::Session session(&storage);
        for (int i = 0; i < 4; ++i)
            session.Frame([](int frame)
            {
                if (frame == 0)
                    ed::SetNodePosition(1, ImVec2(1234.0f, 2048.0f));
                ed::BeginNode(1);
                ImGui::TextUnformatted("Node");
                ed::EndNode();
            });
        result.Zoom[0] = session.Use([] { return ed::GetCurrentZoom(); });
    }

    test::Session session(&storage);
    for (int i = 0; i < 4; ++i)
        session.Frame([](int)
        {
            ed::BeginNode(1);
            ImGui::TextUnformatted("Node");
            ed::EndNode();
        });
    result.Position = session.Use([] { return ed::GetNodePosition(1); });
    result.Zoom[1]  = session.Use([] { return ed::GetCurrentZoom(); });
    return result;
}

void CheckRoundTrip(const RoundTripResult& result, const test::Storage& storage, const char* locale)
{
    test::Check(result.Position.x == 1234.0f && result.Position.y == 2048.0f, "node position survives a round trip under %s: (%g, %g), saved %s", locale, result.Position.x, result.Position.y, storage.Global.c_str());
    test::Check(result.Zoom[0] != 1.0f && std::fabs(result.Zoom[1] - result.Zoom[0]) < 1e-4f, "fractional view survives a round trip under %s: zoom %g, then %g", locale, result.Zoom[0], result.Zoom[1]);
}

} // namespace

TEST_CASE(GlobalCppLocale, "settings.global_cpp_locale_round_trip", "JSON-1", test::TestFlags_None)
{
    int tested = 0;
    for (const char* name : c_CommaLocales)
    {
        std::locale locale;
        try
        {
            locale = std::locale(name);
        }
        catch (const std::exception&)
        {
            continue;
        }
        if (std::use_facet<std::numpunct<char>>(locale).decimal_point() != ',')
            continue;

        const std::locale previous = std::locale::global(locale);
        test::Storage storage;
        const auto result = RoundTrip(storage);
        std::locale::global(previous);

        CheckRoundTrip(result, storage, name);
        ++tested;
    }

    if (tested == 0)
        test::Skip("no locale with a decimal comma is installed");
}

TEST_CASE(MalformedSettings, "settings.malformed_settings_load_safely", "CORE-12", test::TestFlags_None)
{
    // Each document has one problem, and a valid entry for node 2 the editor must apply.
    const char* const c_Node2 = R"("node:2":{"location":{"x":300,"y":40}})";
    const std::string documents[] =
    {
        std::string(R"({"nodes":{"node:1":{},)") + c_Node2 + "}}",
        std::string(R"({"nodes":{"node:1":{"location":{"y":1}},)") + c_Node2 + "}}",
        std::string(R"({"nodes":{"node:1":{"location":5},)") + c_Node2 + "}}",
        std::string(R"({"nodes":{"pin:1":{"location":{"x":5,"y":5}},"link:1":{},"node:x":{},)") + c_Node2 + "}}",
        std::string(R"({"nodes":{)") + c_Node2 + R"(},"view":{"scroll":{"x":0,"y":0},"zoom":0}})",
        std::string(R"({"nodes":{)") + c_Node2 + R"(},"view":{"scroll":{"x":0,"y":0},"zoom":-1}})",
        std::string(R"({"nodes":{)") + c_Node2 + R"(},"view":{"scroll":{"x":0,"y":0},"zoom":1e-6}})",
        std::string(R"({"nodes":{)") + c_Node2 + R"(},"view":{"visible_rect":5}})",
        std::string(R"({"nodes":{)") + c_Node2 + R"(},"view":{"visible_rect":{"min":{"x":10,"y":10},"max":{"x":10,"y":20}}}})",
        std::string(R"({"nodes":{)") + c_Node2 + R"(},"view":{"visible_rect":{"min":{"x":0,"y":0},"max":{"x":1e-9,"y":1e-9}}}})",
        std::string(R"({"nodes":{)") + c_Node2 + R"(},"selection":["pin:2","node:2",5],"view":[]})",
    };

    for (auto& document : documents)
    {
        test::Storage storage;
        storage.Global = document;
        test::Session session(&storage);
        for (int i = 0; i < 3; ++i)
            session.Frame([](int)
            {
                for (uintptr_t id : { 1, 2 })
                {
                    ed::BeginNode(id);
                    ImGui::TextUnformatted("Node");
                    ed::EndNode();
                }
            });

        const ImVec2 position = session.Use([] { return ed::GetNodePosition(2); });
        const float  zoom     = 1.0f / session.Use([] { return ed::GetCurrentZoom(); });
        test::Check(position.x == 300.0f && position.y == 40.0f && zoom >= 0.1f && zoom <= 8.0f,
            "node 2 at (%g, %g), zoom %g, from %s", position.x, position.y, zoom, document.c_str());
    }
}

namespace {

// An editor whose settings the application keeps itself.
ed::Config UnnamedConfig()
{
    ed::Config config;
    config.SettingsName = nullptr;
    return config;
}

void NodesOneAndTwo(int)
{
    for (uintptr_t id : { 1, 2 })
    {
        ed::BeginNode(id);
        ImGui::TextUnformatted("Node");
        ed::EndNode();
    }
}

} // namespace

TEST_CASE(IdleFramesWantNoSave, "settings.idle_frames_want_no_save", "API-9", test::TestFlags_None)
{
    test::Session session(UnnamedConfig());
    session.DefaultContent = NodesOneAndTwo;
    session.Run(10);
    session.Use([] { ed::ClearWantSaveSettings(); });

    int changedFrames = 0;
    for (int i = 0; i < 400; ++i)
    {
        session.Frame();
        if (session.Use([] { return ed::WantSaveSettings() || ed::GetSettingsChangeReason() != ed::SaveReasonFlags::None; }))
            ++changedFrames;
    }
    test::Check(changedFrames == 0, "idle frames change no settings: %d of 400 frames did", changedFrames);
}

TEST_CASE(WantSaveAfterSavingRate, "settings.want_save_after_saving_rate", "API-9", test::TestFlags_None)
{
    test::Session session(UnnamedConfig());
    session.HostContext.Context->IO.IniSavingRate = 1.0f;
    session.DefaultContent = NodesOneAndTwo;
    session.Run(10);
    session.Use([] { ed::ClearWantSaveSettings(); });

    session.Frame([](int frame) { ed::SetNodePosition(1, ImVec2(500.0f, 300.0f)); NodesOneAndTwo(frame); });
    const auto reason = session.Use([] { return ed::GetSettingsChangeReason(); });
    session.Run(50);
    const bool early = session.Use([] { return ed::WantSaveSettings(); });
    session.Run(20);
    const bool late = session.Use([] { return ed::WantSaveSettings(); });
    session.Use([] { ed::ClearWantSaveSettings(); });
    const bool cleared = !session.Use([] { return ed::WantSaveSettings(); }) && session.Use([] { return ed::GetSettingsChangeReason(); }) == ed::SaveReasonFlags::None;

    test::Check(reason == ed::SaveReasonFlags::Position, "change reported as a position change: 0x%02X", (unsigned)reason);
    test::Check(!early && late, "WantSaveSettings() turns true after io.IniSavingRate: %d after 50 frames, %d after 70", early, late);
    test::Check(cleared, "ClearWantSaveSettings() clears it and the reason");
}

TEST_CASE(MemoryRoundTrip, "settings.memory_round_trip", "API-9", test::TestFlags_None)
{
    std::string data;
    ImVec2 view;
    {
        test::Session session(UnnamedConfig());
        session.DefaultContent = NodesOneAndTwo;
        session.Frame([](int frame) { ed::SetNodePosition(1, ImVec2(500.0f, 300.0f)); ed::SetNodePosition(2, ImVec2(-80.0f, 40.0f)); NodesOneAndTwo(frame); });
        session.Run(2);
        session.Drag(ImVec2(700.0f, 400.0f), ImVec2(600.0f, 350.0f), 1);
        session.Click(session.Use([] { return ed::CanvasToScreen(ImVec2(510.0f, 305.0f)); }));
        data = session.Use([] { return std::string(ed::SaveSettingsToMemory()); });
        view = session.Use([] { return ed::CanvasToScreen(ImVec2(0.0f, 0.0f)); });
    }

    test::Session session(UnnamedConfig());
    session.Use([&] { ed::LoadSettingsFromMemory(data.c_str()); });
    session.DefaultContent = NodesOneAndTwo;
    session.Run(3);

    const auto position1 = session.Use([] { return ed::GetNodePosition(1); });
    const auto position2 = session.Use([] { return ed::GetNodePosition(2); });
    const auto origin    = session.Use([] { return ed::CanvasToScreen(ImVec2(0.0f, 0.0f)); });
    const bool selected  = session.Use([] { return ed::IsNodeSelected(1) && !ed::IsNodeSelected(2); });
    test::Check(position1.x == 500.0f && position1.y == 300.0f && position2.x == -80.0f && position2.y == 40.0f, "node positions restored: (%g, %g), (%g, %g), from %s", position1.x, position1.y, position2.x, position2.y, data.c_str());
    test::Check(origin.x == view.x && origin.y == view.y, "view restored: origin at (%g, %g), saved (%g, %g)", origin.x, origin.y, view.x, view.y);
    test::Check(selected, "selection restored, from %s", data.c_str());
}

TEST_CASE(Version09Settings, "settings.settings_written_by_0_9_load", "API-9", test::TestFlags_None)
{
    // As 0.9.2 wrote them to its SettingsFile.
    const char* const c_Settings09 = R"({"nodes":{"node:1":{"location":{"x":100,"y":50}},"node:2":{"group_size":{"x":300,"y":200},"location":{"x":400,"y":60}}},"selection":["node:2"],"view":{"scroll":{"x":-120,"y":-40},"visible_rect":{"max":{"x":0,"y":0},"min":{"x":0,"y":0}},"zoom":1}})";

    auto content = [](int)
    {
        ed::BeginNode(1);
        ImGui::TextUnformatted("Node");
        ed::EndNode();
        ed::BeginNode(2);
        ImGui::TextUnformatted("Group");
        ed::Group(ImVec2(300.0f, 200.0f));
        ed::EndNode();
    };

    // Where the canvas origin is without settings: the top left corner of the editor.
    ImVec2 corner;
    {
        test::Session session(UnnamedConfig());
        session.DefaultContent = content;
        session.Run(3);
        corner = session.Use([] { return ed::CanvasToScreen(ImVec2(0.0f, 0.0f)); });
    }

    test::Session session(UnnamedConfig());
    session.Use([&] { ed::LoadSettingsFromMemory(c_Settings09); });
    session.DefaultContent = content;
    session.Run(3);

    const auto position1 = session.Use([] { return ed::GetNodePosition(1); });
    const auto position2 = session.Use([] { return ed::GetNodePosition(2); });
    const auto size2     = session.Use([] { return ed::GetNodeSize(2); });
    const bool selected  = session.Use([] { return ed::IsNodeSelected(2) && !ed::IsNodeSelected(1); });
    const auto origin    = session.Use([] { return ed::CanvasToScreen(ImVec2(0.0f, 0.0f)); });
    test::Check(position1.x == 100.0f && position1.y == 50.0f && position2.x == 400.0f && position2.y == 60.0f, "node positions loaded: (%g, %g), (%g, %g)", position1.x, position1.y, position2.x, position2.y);
    test::Check(size2.x >= 300.0f && size2.y >= 200.0f, "group size loaded: (%g, %g)", size2.x, size2.y);
    test::Check(selected, "selection loaded");
    test::Check(origin.x == corner.x + 120.0f && origin.y == corner.y + 40.0f, "view loaded: canvas origin at (%g, %g), editor corner at (%g, %g)", origin.x, origin.y, corner.x, corner.y);
}

namespace {

// Whether the settings changed since the last call.
bool TakeSettingsChange(test::Session& session)
{
    return session.Use([]
    {
        const bool changed = ed::GetSettingsChangeReason() != ed::SaveReasonFlags::None;
        ed::ClearWantSaveSettings();
        return changed;
    });
}

} // namespace

TEST_CASE(WheelNotchSavesOnce, "settings.wheel_notch_saves_once_after_saving_rate", "INT-9", test::TestFlags_None)
{
    test::Session session(UnnamedConfig());
    session.DefaultContent = NodesOneAndTwo;
    test::MouseMove(ImVec2(500.0f, 300.0f));
    session.Run(10);
    session.Use([] { ed::ClearWantSaveSettings(); });

    test::MouseWheel(1.0f);
    std::vector<int> saveFrames;
    for (int frame = 0; frame < 400; ++frame)
    {
        session.Frame();
        session.Use([&]
        {
            if (!ed::WantSaveSettings())
                return;
            ed::SaveSettingsToMemory();
            ed::ClearWantSaveSettings();
            saveFrames.push_back(frame);
        });
    }

    test::Check(saveFrames.size() == 1 && saveFrames[0] >= 300, "one wheel notch is saved once, after io.IniSavingRate: %d saves, first in frame %d", (int)saveFrames.size(), saveFrames.empty() ? -1 : saveFrames[0]);
}

TEST_CASE(NavigationSettles, "settings.navigation_changes_settings_when_it_settles", "INT-9", test::TestFlags_None)
{
    test::Session session(UnnamedConfig());
    session.DefaultContent = NodesOneAndTwo;
    session.Frame([](int frame) { ed::SetNodePosition(2, ImVec2(1500.0f, 900.0f)); NodesOneAndTwo(frame); });
    test::MouseMove(ImVec2(500.0f, 300.0f));
    session.Run(10);
    TakeSettingsChange(session);

    // Counts the changes over 60 frames, and keeps the settings of the last one.
    auto countChanges = [&](std::string& changed)
    {
        int changes = 0;
        for (int i = 0; i < 60; ++i)
        {
            session.Frame();
            if (TakeSettingsChange(session))
            {
                ++changes;
                changed = session.Use([] { return std::string(ed::SaveSettingsToMemory()); });
            }
        }
        return changes;
    };

    std::string changed;
    test::MouseWheel(1.0f);
    const int wheelChanges = countChanges(changed);
    const auto wheelSettled = session.Use([] { return std::string(ed::SaveSettingsToMemory()); });
    test::Check(wheelChanges == 1 && changed == wheelSettled, "one wheel notch changes the settings once, when the zoom settles: %d changes, to %s, settled %s", wheelChanges, changed.c_str(), wheelSettled.c_str());

    session.Use([] { ed::NavigateToContent(); });
    const int contentChanges = countChanges(changed);
    const auto contentSettled = session.Use([] { return std::string(ed::SaveSettingsToMemory()); });
    test::Check(contentChanges == 1 && changed == contentSettled, "NavigateToContent() changes the settings once, when the view settles: %d changes, to %s, settled %s", contentChanges, changed.c_str(), contentSettled.c_str());
}

TEST_CASE(FractionalPositionChangesOnce, "settings.fractional_position_changes_settings_once", "PERF-4", test::TestFlags_None)
{
    test::Session session(UnnamedConfig());
    session.DefaultContent = [](int frame)
    {
        ed::SetNodePosition(1, ImVec2(10.5f, 20.5f));
        ed::SetGroupSize(2, ImVec2(300.5f, 200.5f));
        NodesOneAndTwo(frame);
    };
    session.Run(10);
    TakeSettingsChange(session);
    session.DefaultContent = [](int frame)
    {
        ed::SetNodePosition(1, ImVec2(110.5f, 20.5f));
        ed::SetGroupSize(2, ImVec2(400.5f, 200.5f));
        NodesOneAndTwo(frame);
    };

    int changes = 0;
    for (int i = 0; i < 30; ++i)
    {
        session.Frame();
        if (TakeSettingsChange(session))
            ++changes;
    }
    test::Check(changes == 1, "a fractional position and group size set on every frame change the settings once: %d changes in 30 frames", changes);
}

TEST_CASE(ContentWithoutNodes, "settings.content_without_nodes_leaves_layout_out", "API-9", test::TestFlags_None)
{
    auto config = UnnamedConfig();
    config.SettingsContent = ed::SettingsContentFlags::Selection | ed::SettingsContentFlags::View;
    test::Session session(config);
    session.DefaultContent = NodesOneAndTwo;
    session.Frame([](int frame) { ed::SetNodePosition(1, ImVec2(500.0f, 300.0f)); NodesOneAndTwo(frame); });
    session.Run(2);
    const auto saved = session.Use([] { return std::string(ed::SaveSettingsToMemory()); });

    session.Use([] { ed::LoadSettingsFromMemory(R"({"nodes":{"node:1":{"location":{"x":10,"y":20}}},"view":{"scroll":{"x":0,"y":0},"zoom":2}})"); });
    session.Run(2);
    const auto position = session.Use([] { return ed::GetNodePosition(1); });
    const auto zoom     = 1.0f / session.Use([] { return ed::GetCurrentZoom(); });

    test::Check(saved.find("\"nodes\"") == std::string::npos && saved.find("\"view\"") != std::string::npos, "saved settings hold the view but no node layout: %s", saved.c_str());
    test::Check(position.x == 500.0f && position.y == 300.0f, "loaded settings leave nodes in place: (%g, %g)", position.x, position.y);
    test::Check(zoom == 2.0f, "and apply the view: zoom %g", zoom);
}

TEST_CASE(CLocalePreserved, "settings.c_locale_is_preserved", "JSON-2", test::TestFlags_None)
{
    int tested = 0;
    for (const char* name : c_CommaLocales)
    {
        if (!setlocale(LC_ALL, name) || localeconv()->decimal_point[0] != ',')
            continue;

        const std::string before = setlocale(LC_NUMERIC, nullptr);
        test::Storage storage;
        const auto result = RoundTrip(storage);
        const std::string after = setlocale(LC_NUMERIC, nullptr);

        test::Check(after == before, "LC_NUMERIC left as the application set it: %s, was %s", after.c_str(), before.c_str());
        CheckRoundTrip(result, storage, name);
        ++tested;
    }
    setlocale(LC_ALL, "C");

    if (tested == 0)
        test::Skip("no locale with a decimal comma is installed");
}
