#pragma once
#include "imgui/imgui.h"

namespace ax {
namespace Drawing {

enum class IconType { Flow, Circle, Square, Grid, RoundSquare, Diamond };

void DrawIcon(ImDrawList* drawList, const ImVec2& a, const ImVec2& b, IconType type, bool filled, ImU32 color, ImU32 innerColor);

//void DrawHeader(ImDrawList* drawList, ImTextureID textureId, const ImVec2& a, const ImVec2& b, ImU32 color, float rounding, float zoom = 1.0f);

//void DrawLink(ImDrawList* drawList, const ImVec2& a, const ImVec2& b, ImU32 color, float thickness = 1.0f, float strength = 1.0f, const ImVec2& a_dir = ImVec2(1, 0), const ImVec2& b_dir = ImVec2(-1, 0));

// returns distance to link compatible
//float LinkDistance(const ImVec2& p, const ImVec2& a, const ImVec2& b, float strength = 1.0f, const ImVec2& a_dir = ImVec2(1, 0), const ImVec2& b_dir = ImVec2(-1, 0));
//ax::rectf GetLinkBounds(const ImVec2& a, const ImVec2& b, float strength = 1.0f, const ImVec2& a_dir = ImVec2(1, 0), const ImVec2& b_dir = ImVec2(-1, 0));
//bool CollideLinkWithRect(const ax::rectf& r, const ImVec2& a, const ImVec2& b, float strength = 1.0f, const ImVec2& a_dir = ImVec2(1, 0), const ImVec2& b_dir = ImVec2(-1, 0));
//ax::cubic_bezier_t GetLinkBezier(const ImVec2& a, const ImVec2& b, float strength = 1.0f, const ImVec2& a_dir = ImVec2(1, 0), const ImVec2& b_dir = ImVec2(-1, 0));

} // namespace Drawing
} // namespace ax