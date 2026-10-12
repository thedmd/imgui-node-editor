//------------------------------------------------------------------------------
// Settings saved and loaded by the editor.
//------------------------------------------------------------------------------
# include "harness.h"
# include <clocale>
# include <cstdio>
# include <locale>
# include <string>

namespace {

// Locales that write numbers with a decimal comma, in Windows and POSIX spelling.
const char* const c_CommaLocales[] = { "de-DE", "pl-PL", "fr-FR", "de_DE.UTF-8", "de_DE.utf8", "pl_PL.UTF-8", "fr_FR.UTF-8", "de_DE" };

ImVec2 RoundTrip(test::Storage& storage)
{
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
    }

    test::Session session(&storage);
    for (int i = 0; i < 4; ++i)
        session.Frame([](int)
        {
            ed::BeginNode(1);
            ImGui::TextUnformatted("Node");
            ed::EndNode();
        });
    return session.Use([] { return ed::GetNodePosition(1); });
}

} // namespace

TEST_CASE(GlobalCppLocale, "settings.global_cpp_locale_round_trip", "JSON-1", test::TestFlags_KnownFailure)
{
    std::locale commaLocale;
    const char* name = nullptr;
    for (const char* candidate : c_CommaLocales)
    {
        try
        {
            std::locale locale(candidate);
            if (std::use_facet<std::numpunct<char>>(locale).decimal_point() == ',')
            {
                commaLocale = locale;
                name = candidate;
                break;
            }
        }
        catch (const std::exception&)
        {
        }
    }
    if (!name)
    {
        test::Skip("no locale with a decimal comma is installed");
        return;
    }

    const std::locale previous = std::locale::global(commaLocale);
    test::Storage storage;
    const auto position = RoundTrip(storage);
    std::locale::global(previous);

    test::Check(position.x == 1234.0f && position.y == 2048.0f, "node position survives a round trip under global C++ locale %s: (%g, %g), saved %s", name, position.x, position.y, storage.Global.c_str());
}

TEST_CASE(CLocalePreserved, "settings.c_locale_is_preserved", "JSON-2", test::TestFlags_KnownFailure)
{
    const char* name = nullptr;
    for (const char* candidate : c_CommaLocales)
    {
        if (setlocale(LC_ALL, candidate) && localeconv()->decimal_point[0] == ',')
        {
            name = candidate;
            break;
        }
    }
    if (!name)
    {
        setlocale(LC_ALL, "C");
        test::Skip("no locale with a decimal comma is installed");
        return;
    }

    const std::string before = setlocale(LC_NUMERIC, nullptr);
    test::Storage storage;
    RoundTrip(storage);
    const std::string after = setlocale(LC_NUMERIC, nullptr);
    setlocale(LC_ALL, "C");

    test::Check(after == before, "LC_NUMERIC left as the application set it: %s, was %s", after.c_str(), before.c_str());
}
