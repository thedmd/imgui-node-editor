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

// Events, flow, coroutines, values, math, logic, text, arrays, maps and debugging, each named "crude."
// and its title without spaces: "crude.ForLoop".
Result<void> RegisterLibrary(Registry& registry);

// Make and Break for a struct, "make:example.Vector2" and "break:example.Vector2", titled
// "Make Vector2" and "Break Vector2". The struct lives as long as the registry; a field named
// "struct", the key of their struct pins, is refused.
Result<void> RegisterStructNodes(Registry& registry, const StructType& type);

// Switch on Enum, "crude.SwitchOnEnum", of every enumeration the registry has once it's built; refused
// while it has none.
Result<void> RegisterEnumNodes(Registry& registry);

} // namespace crude_blueprint
