//------------------------------------------------------------------------------
// crude_json, which reads and writes the editor's settings.
//------------------------------------------------------------------------------
# include "harness.h"
# include <crude_json.h>
# include <limits>
# include <type_traits>
# include <utility>

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

namespace {

// The string in a parsed document, or "<not a string>".
std::string ParsedString(const char* text)
{
    const auto parsed = json::value::parse(text);
    return parsed.is_string() ? parsed.get<json::string>() : std::string("<not a string>");
}

} // namespace

TEST_CASE(JsonUtf8, "json.utf8_strings_round_trip", "JSON-5", test::TestFlags_None)
{
    // zażółć, é, € and an emoji, in UTF-8.
    const char* texts[] = { "za\xC5\xBC\xC3\xB3\xC5\x82\xC4\x87", "\xC3\xA9", "\xE2\x82\xAC", "\xF0\x9F\x98\x80" };
    int differing = 0;
    for (auto text : texts)
    {
        const json::value written = std::string(text);
        if (ParsedString(written.dump().c_str()) != text)
            ++differing;
    }
    test::Check(differing == 0, "UTF-8 strings round-trip: %d of 4 differ", differing);

    test::Check(ParsedString(R"("\u00e9")") == "\xC3\xA9", "\\u00e9 reads as UTF-8 for e acute");
    test::Check(ParsedString(R"("\u20ac")") == "\xE2\x82\xAC", "\\u20ac reads as UTF-8 for the euro sign");
    test::Check(ParsedString(R"("\ud83d\ude00")") == "\xF0\x9F\x98\x80", "a surrogate pair reads as the 4 bytes of one code point");
    test::Check(json::value::parse(R"("\ud83d")").is_discarded() && json::value::parse(R"("\ude00x")").is_discarded(), "lone surrogates are rejected");
}

namespace {

// Whether 'lhs == rhs' compiles for the types.
template <typename T, typename U, typename = void>
struct IsEqualityComparable : std::false_type {};

template <typename T, typename U>
struct IsEqualityComparable<T, U, decltype(void(std::declval<const T&>() == std::declval<const U&>()))> : std::true_type {};

} // namespace

TEST_CASE(JsonNoTypeComparison, "json.values_dont_compare_by_type", "JSON-8", test::TestFlags_None)
{
    // Through the conversion to type_t, value(1.0) == value(2.0) compared only the
    // type tags, and held.
    test::Check(!IsEqualityComparable<json::value, json::value>::value, "values don't compare with ==");
    test::Check(static_cast<json::type_t>(json::value(1.0)) == json::type_t::number, "the type is still there for an explicit conversion");
}

TEST_CASE(JsonDepth, "json.nesting_beyond_the_limit_is_rejected", "JSON-7", test::TestFlags_None)
{
    // Arrays nested to the default limit of 512, one past it, and 2500 deep, which
    // the recursive parser would follow until the stack overflows.
    auto nested = [](int depth) { return std::string(depth, '[') + std::string(depth, ']'); };

    test::Check(!json::value::parse(nested(512)).is_discarded(), "512 nested arrays parse");
    test::Check(json::value::parse(nested(513)).is_discarded(), "513 are discarded");
    test::Check(json::value::parse(nested(2500)).is_discarded(), "2500 are discarded");
}

TEST_CASE(JsonEscapes, "json.keys_and_values_are_escaped_alike", "JSON-6", test::TestFlags_None)
{
    // Keys with a quote and a backslash, values with a tab and a 0x01 byte.
    json::value document;
    document["a\"b"]  = std::string("tab\there");
    document["a\\b"]  = std::string("one\x01" "end");
    document["plain"] = std::string("x/y");

    const auto text = document.dump();
    test::Check(text == R"({"a\"b":"tab\there","a\\b":"one\u0001end","plain":"x/y"})", "written as %s", text.c_str());

    const auto parsed = json::value::parse(text);
    test::Check(!parsed.is_discarded() && parsed["a\"b"].get<json::string>() == "tab\there" && parsed["a\\b"].get<json::string>() == "one\x01" "end",
        "and read back");
}
