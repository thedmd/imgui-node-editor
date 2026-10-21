//------------------------------------------------------------------------------
// crude_blueprint_library: the node types blueprints2-example's blueprints are
// made of.
//------------------------------------------------------------------------------
# pragma once
# include "crude_blueprint.h"

namespace crude_blueprint {

enum class LibraryError : uint16_t
{
    AssertFailed = 1, // an Assert's condition was false
};

constexpr ErrorCategory CategoryOf(LibraryError) { return ErrorCategory::Library; }

// Events, flow, values, math, logic, text, arrays, maps and debugging, each named "crude." and its
// title without spaces: "crude.ForLoop".
Result<void> RegisterLibrary(Registry& registry);

} // namespace crude_blueprint
