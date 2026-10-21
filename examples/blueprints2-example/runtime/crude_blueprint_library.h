//------------------------------------------------------------------------------
// crude_blueprint_library: the node types blueprints2-example's blueprints are
// made of.
//------------------------------------------------------------------------------
# pragma once
# include "crude_blueprint.h"

namespace crude_blueprint {

// "crude.Start", "crude.Print", "crude.Constant", "crude.Add", "crude.Branch" and "crude.ForLoop".
Result<void> RegisterLibrary(Registry& registry);

} // namespace crude_blueprint
