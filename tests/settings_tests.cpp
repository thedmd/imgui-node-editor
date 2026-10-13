//------------------------------------------------------------------------------
// Settings saved and loaded by the editor.
//------------------------------------------------------------------------------
# include "harness.h"
# include <crude_json.h>
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
        result.Zoom[0] = session.Use([] { return test::CurrentZoom(); });
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
    result.Zoom[1]  = session.Use([] { return test::CurrentZoom(); });
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
        const float  zoom     = session.Use([] { return test::CurrentZoom(); });
        test::Check(position.x == 300.0f && position.y == 40.0f && zoom >= 0.1f && zoom <= 8.0f,
            "node 2 at (%g, %g), zoom %g, from %s", position.x, position.y, zoom, document.c_str());
    }
}

namespace {

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

namespace {

// The zoom the settings hold.
float SavedZoom(const std::string& settings)
{
    const auto document = crude_json::value::parse(settings);
    const auto zoom = document["view"]["zoom"].get_ptr<crude_json::number>();
    return zoom ? static_cast<float>(*zoom) : 0.0f;
}

// Whether the editor saved its settings since the last call.
bool TakeSettingsChange(const test::Storage& storage, int& saves)
{
    const bool changed = storage.GlobalSaves != saves;
    saves = storage.GlobalSaves;
    return changed;
}

} // namespace

TEST_CASE(NavigationSettles, "settings.navigation_changes_settings_when_it_settles", "INT-9", test::TestFlags_None)
{
    test::Storage storage;
    test::Session session(&storage);
    session.DefaultContent = NodesOneAndTwo;
    session.Frame([](int frame) { ed::SetNodePosition(2, ImVec2(1500.0f, 900.0f)); NodesOneAndTwo(frame); });
    test::MouseMove(ImVec2(500.0f, 300.0f));
    session.Run(10);
    int saves = storage.GlobalSaves;

    // Counts the saves over 60 frames.
    auto countChanges = [&]
    {
        int changes = 0;
        for (int i = 0; i < 60; ++i)
        {
            session.Frame();
            if (TakeSettingsChange(storage, saves))
                ++changes;
        }
        return changes;
    };

    auto settledZoom = [&] { return session.Use([] { return test::CurrentZoom(); }); };

    test::MouseWheel(1.0f);
    const int wheelChanges = countChanges();
    const float wheelSaved = SavedZoom(storage.Global), wheelSettled = settledZoom();
    test::Check(wheelChanges == 1 && std::fabs(wheelSaved - wheelSettled) < 1e-4f, "one wheel notch changes the settings once, when the zoom settles: %d changes, to zoom %g, settled %g", wheelChanges, wheelSaved, wheelSettled);

    session.Use([] { ed::NavigateToContent(); });
    const int contentChanges = countChanges();
    const float contentSaved = SavedZoom(storage.Global), contentSettled = settledZoom();
    test::Check(contentChanges == 1 && std::fabs(contentSaved - contentSettled) < 1e-4f, "NavigateToContent() changes the settings once, when the view settles: %d changes, to zoom %g, settled %g", contentChanges, contentSaved, contentSettled);
}

TEST_CASE(FractionalPositionChangesOnce, "settings.fractional_position_changes_settings_once", "PERF-4", test::TestFlags_None)
{
    test::Storage storage;
    test::Session session(&storage);
    session.DefaultContent = [](int frame)
    {
        ed::SetNodePosition(1, ImVec2(10.5f, 20.5f));
        ed::SetGroupSize(2, ImVec2(300.5f, 200.5f));
        NodesOneAndTwo(frame);
    };
    session.Run(10);
    int saves = storage.GlobalSaves;
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
        if (TakeSettingsChange(storage, saves))
            ++changes;
    }
    test::Check(changes == 1, "a fractional position and group size set on every frame change the settings once: %d changes in 30 frames", changes);
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
