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

TEST_CASE(JsonConstLookups, "json.const_lookups_of_missing_entries_return_null", "JSON-3", test::TestFlags_None)
{
    const auto document = json::value::parse(R"({"location":{"y":1},"list":[1,2],"number":5})");

    test::Check(document["missing"].is_null(), "missing key");
    test::Check(document["location"]["x"].is_null(), "missing nested key");
    test::Check(document["number"]["min"].is_null() && document["number"][0].is_null(), "key and index of a number");
    test::Check(document["list"][2].is_null() && document["list"][static_cast<size_t>(-1)].is_null(), "index past the end");
    test::Check(document.find("location") != nullptr && document.find("missing") == nullptr && document["list"].find("x") == nullptr, "find()");
}

TEST_CASE(JsonMutableLookups, "json.mutable_lookups_on_wrong_types_assert", "JSON-3", test::TestFlags_None)
{
    // Each lookup asserts once, and leaves the document unchanged.
    test::ExpectAsserts(3);
    auto document = json::value::parse(R"({"list":[],"number":5})");
    const auto before = document.dump();

    document["number"]["min"] = 1.0;
    document["number"][0]     = 1.0;
    document["list"][static_cast<size_t>(-1)] = 1.0;

    test::Check(document.dump() == before, "document unchanged: %s", document.dump().c_str());
}
