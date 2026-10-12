//------------------------------------------------------------------------------
// crude_json, which reads and writes the editor's settings.
//------------------------------------------------------------------------------
# include "harness.h"
# include <crude_json.h>
# include <limits>

namespace json = crude_json;

TEST_CASE(JsonNonFinite, "json.non_finite_numbers_are_written_as_null", "JSON-4", test::TestFlags_None)
{
    json::value document;
    document["nan"]      = std::numeric_limits<double>::quiet_NaN();
    document["infinity"] = std::numeric_limits<double>::infinity();
    document["negative"] = -std::numeric_limits<double>::infinity();
    document["one"]      = 1.0;

    const auto text = document.dump();
    test::Check(text == R"({"infinity":null,"nan":null,"negative":null,"one":1})", "written as %s", text.c_str());

    const auto parsed = json::value::parse(text);
    test::Check(!parsed.is_discarded() && parsed["nan"].is_null() && parsed["one"].is_number(), "and read back");
}
