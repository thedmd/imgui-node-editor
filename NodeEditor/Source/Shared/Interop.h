//------------------------------------------------------------------------------
// LICENSE
//   This software is dual-licensed to the public domain and under the following
//   license: you are granted a perpetual, irrevocable license to copy, modify,
//   publish, and distribute this file as you see fit.
//
// CREDITS
//   Written by Michal Cichon
//------------------------------------------------------------------------------
# pragma once
# include "imgui.h"
# include "Math2D.h"


//------------------------------------------------------------------------------
namespace ax {
namespace ImGuiInterop {


//------------------------------------------------------------------------------
static inline bool operator==(const ImVec2& lhs, const ImVec2& rhs) { return lhs.x == rhs.x && lhs.y == rhs.y; }
static inline bool operator!=(const ImVec2& lhs, const ImVec2& rhs) { return lhs.x != rhs.x || lhs.y != rhs.y; }
static inline ImVec2 operator+(const ImVec2& lhs) { return ImVec2( lhs.x,  lhs.y); }
static inline ImVec2 operator-(const ImVec2& lhs) { return ImVec2(-lhs.x, -lhs.y); }
static inline ImVec2 operator+(const ImVec2& lhs, const ImVec2& rhs) { return ImVec2(lhs.x+rhs.x, lhs.y+rhs.y); }
static inline ImVec2 operator-(const ImVec2& lhs, const ImVec2& rhs) { return ImVec2(lhs.x-rhs.x, lhs.y-rhs.y); }
static inline ImVec2 operator*(const ImVec2& lhs, float rhs)         { return ImVec2(lhs.x * rhs,   lhs.y * rhs); }
static inline ImVec2 operator*(float lhs,         const ImVec2& rhs) { return ImVec2(lhs   * rhs.x, lhs   * rhs.y); }

static inline int    roundi(float value)            { return static_cast<int>(value); }
static inline point  to_point(const ImVec2& value)  { return point(roundi(value.x), roundi(value.y)); }
static inline pointf to_pointf(const ImVec2& value) { return pointf(value.x, value.y); }
static inline pointf to_pointf(const point& value)  { return pointf(static_cast<float>(value.x), static_cast<float>(value.y)); }
static inline size   to_size (const ImVec2& value)  { return size (roundi(value.x), roundi(value.y)); }
static inline sizef  to_sizef(const ImVec2& value)  { return sizef(value.x, value.y); }
static inline ImVec2 to_imvec(const point& value)   { return ImVec2(static_cast<float>(value.x), static_cast<float>(value.y)); }
static inline ImVec2 to_imvec(const pointf& value)  { return ImVec2(value.x, value.y); }
static inline ImVec2 to_imvec(const size& value)    { return ImVec2(static_cast<float>(value.w), static_cast<float>(value.h)); }
static inline ImVec2 to_imvec(const sizef& value)   { return ImVec2(value.w, value.h); }
static inline rect   ImGui_GetItemRect()            { return rect(to_point(ImGui::GetItemRectMin()), to_point(ImGui::GetItemRectMax())); }


//------------------------------------------------------------------------------
} // namespace ImGuiInterop
} // namespace ax
