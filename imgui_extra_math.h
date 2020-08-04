//------------------------------------------------------------------------------
// LICENSE
//   This software is dual-licensed to the public domain and under the following
//   license: you are granted a perpetual, irrevocable license to copy, modify,
//   publish, and distribute this file as you see fit.
//
// CREDITS
//   Written by Michal Cichon
//------------------------------------------------------------------------------
# ifndef __IMGUI_EXTRA_MATH_H__
# define __IMGUI_EXTRA_MATH_H__
# pragma once


//------------------------------------------------------------------------------
# include <imgui.h>
# ifndef IMGUI_DEFINE_MATH_OPERATORS
#     define IMGUI_DEFINE_MATH_OPERATORS
# endif
# include <imgui_internal.h>


//------------------------------------------------------------------------------
struct ImLine
{
    ImVec2 A, B;
};


//------------------------------------------------------------------------------
inline bool operator==(const ImVec2& lhs, const ImVec2& rhs);
inline bool operator!=(const ImVec2& lhs, const ImVec2& rhs);
inline ImVec2 operator*(const float lhs, const ImVec2& rhs);
inline ImVec2 operator-(const ImVec2& lhs);


//------------------------------------------------------------------------------
inline float  ImLength(float v);
inline float  ImLength(const ImVec2& v);
inline float  ImLengthSqr(float v);
inline ImVec2 ImNormalized(const ImVec2& v);


//------------------------------------------------------------------------------
inline bool   ImRect_IsEmpty(const ImRect& rect);
inline ImVec2 ImRect_ClosestPoint(const ImRect& rect, const ImVec2& p, bool snap_to_edge);
inline ImVec2 ImRect_ClosestPoint(const ImRect& rect, const ImVec2& p, bool snap_to_edge, float radius);
inline ImVec2 ImRect_ClosestPoint(const ImRect& rect, const ImRect& b);
inline ImLine ImRect_ClosestLine(const ImRect& rect_a, const ImRect& rect_b);
inline ImLine ImRect_ClosestLine(const ImRect& rect_a, const ImRect& rect_b, float radius_a, float radius_b);



//------------------------------------------------------------------------------
namespace ImEasing {

template <typename V, typename T>
inline V EaseOutQuad(V b, V c, T t)
{
    return b - c * (t * (t - 2));
}

} // namespace ImEasing


//------------------------------------------------------------------------------
# include "imgui_extra_math.inl"


//------------------------------------------------------------------------------
# endif // __IMGUI_EXTRA_MATH_H__
