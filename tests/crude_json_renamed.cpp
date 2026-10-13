//------------------------------------------------------------------------------
// A second copy of crude_json in a namespace of its own, as another library's
// copy would be, linked into one program with the editor's.
//------------------------------------------------------------------------------
# define CRUDE_JSON_NAMESPACE crude_json_renamed
# include "crude_json.cpp"
# include <string>

std::string RenamedCrudeJsonRoundTrip(const std::string& text)
{
    return crude_json_renamed::value::parse(text).dump();
}
