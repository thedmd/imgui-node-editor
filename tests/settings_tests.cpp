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
