//------------------------------------------------------------------------------
// crude_blueprint_library: the node types blueprints2-example's blueprints are
// made of.
//------------------------------------------------------------------------------
# pragma once
# include "crude_blueprint.h"

namespace crude_blueprint {

// Events, flow, values and math, each named "crude." and its title without spaces: "crude.ForLoop".
Result<void> RegisterLibrary(Registry& registry);

} // namespace crude_blueprint
