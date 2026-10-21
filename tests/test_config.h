//------------------------------------------------------------------------------
// Force-included into Dear ImGui, the editor and the tests, so a failed
// assertion is counted by the test runner instead of aborting the run.
//------------------------------------------------------------------------------
# pragma once

void NodeEditorTestAssert(const char* expression, const char* file, int line);

# define IM_ASSERT(_EXPR)       ((_EXPR) ? (void)0 : NodeEditorTestAssert(#_EXPR, __FILE__, __LINE__))
# define CRUDE_ASSERT(_EXPR)    IM_ASSERT(_EXPR)
# define CRUDE_BP_ASSERT(_EXPR) IM_ASSERT(_EXPR)
