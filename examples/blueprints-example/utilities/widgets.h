#pragma once
#include <imgui.h>
#include "drawing.h"

namespace ax {
namespace Widgets {

using Drawing::IconType;

void Icon(const ImVec2& size, IconType type, bool filled, const ImVec4& color = ImVec4(1, 1, 1, 1), const ImVec4& innerColor = ImVec4(0, 0, 0, 0));

} // namespace Widgets
} // namespace ax