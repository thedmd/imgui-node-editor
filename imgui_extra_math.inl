//------------------------------------------------------------------------------
// VERSION 0.9.1
//
// LICENSE
//   This software is dual-licensed to the public domain and under the following
//   license: you are granted a perpetual, irrevocable license to copy, modify,
//   publish, and distribute this file as you see fit.
//
// CREDITS
//   Written by Michal Cichon
//------------------------------------------------------------------------------
# ifndef __IMGUI_EXTRA_MATH_INL__
# define __IMGUI_EXTRA_MATH_INL__
# pragma once


//------------------------------------------------------------------------------
# include "imgui_extra_math.h"


//------------------------------------------------------------------------------
# if IMGUI_VERSION_NUM < 19002
inline bool operator==(const ImVec2& lhs, const ImVec2& rhs)
{
    return lhs.x == rhs.x && lhs.y == rhs.y;
}

inline bool operator!=(const ImVec2& lhs, const ImVec2& rhs)
{
    return lhs.x != rhs.x || lhs.y != rhs.y;
}
# endif

inline ImVec2 operator*(const float lhs, const ImVec2& rhs)
{
    return ImVec2(lhs * rhs.x, lhs * rhs.y);
}

# if IMGUI_VERSION_NUM < 18955
inline ImVec2 operator-(const ImVec2& lhs)
{
    return ImVec2(-lhs.x, -lhs.y);
}
# endif


//------------------------------------------------------------------------------
inline float ImLength(float v)
{
    return v;
}

inline float ImLength(const ImVec2& v)
{
    return ImSqrt(ImLengthSqr(v));
}

inline float ImLengthSqr(float v)
{
    return v * v;
}

inline ImVec2 ImNormalized(const ImVec2& v)
{
    return v * ImInvLength(v, 0.0f);
}




//------------------------------------------------------------------------------
inline bool ImRect_IsEmpty(const ImRect& rect)
{
    return rect.Min.x >= rect.Max.x
        || rect.Min.y >= rect.Max.y;
}

inline ImVec2 ImRect_ClosestPoint(const ImRect& rect, const ImVec2& p, bool snap_to_edge)
{
    if (!snap_to_edge && rect.Contains(p))
        return p;

    return ImVec2(
        (p.x > rect.Max.x) ? rect.Max.x : (p.x < rect.Min.x ? rect.Min.x : p.x),
        (p.y > rect.Max.y) ? rect.Max.y : (p.y < rect.Min.y ? rect.Min.y : p.y)
    );
}

inline ImVec2 ImRect_ClosestPoint(const ImRect& rect, const ImVec2& p, bool snap_to_edge, float radius)
{
    auto point = ImRect_ClosestPoint(rect, p, snap_to_edge);

    const auto offset      = p - point;
    const auto distance_sq = offset.x * offset.x + offset.y * offset.y;
    if (distance_sq <= 0)
        return point;

    const auto distance = ImSqrt(distance_sq);

    return point + offset * (ImMin(distance, radius) * (1.0f / distance));
}

inline ImVec2 ImRect_ClosestPoint(const ImRect& rect, const ImRect& other)
{
    ImVec2 result;
    if (other.Min.x >= rect.Max.x)
        result.x = rect.Max.x;
    else if (other.Max.x <= rect.Min.x)
        result.x = rect.Min.x;
    else
        result.x = (ImMax(rect.Min.x, other.Min.x) + ImMin(rect.Max.x, other.Max.x)) / 2;

    if (other.Min.y >= rect.Max.y)
        result.y = rect.Max.y;
    else if (other.Max.y <= rect.Min.y)
        result.y = rect.Min.y;
    else
        result.y = (ImMax(rect.Min.y, other.Min.y) + ImMin(rect.Max.y, other.Max.y)) / 2;

    return result;
}

inline ImLine ImRect_ClosestLine(const ImRect& rect_a, const ImRect& rect_b)
{
    ImLine result;
    result.A = ImRect_ClosestPoint(rect_a, rect_b);
    result.B = ImRect_ClosestPoint(rect_b, rect_a);

    auto distribute = [](float& a, float& b, float a0, float a1, float b0, float b1)
    {
        if (a0 >= b1 || a1 <= b0)
            return;

        const auto aw = a1 - a0;
        const auto bw = b1 - b0;

        if (aw > bw)
        {
            b = b0 + bw - bw * (a - a0) / aw;
            a = b;
        }
        else if (aw < bw)
        {
            a = a0 + aw - aw * (b - b0) / bw;
            b = a;
        }
    };

    distribute(result.A.x, result.B.x, rect_a.Min.x, rect_a.Max.x, rect_b.Min.x, rect_b.Max.x);
    distribute(result.A.y, result.B.y, rect_a.Min.y, rect_a.Max.y, rect_b.Min.y, rect_b.Max.y);

    return result;
}

inline ImLine ImRect_ClosestLine(const ImRect& rect_a, const ImRect& rect_b, float radius_a, float radius_b)
{
    auto line = ImRect_ClosestLine(rect_a, rect_b);
    if (radius_a < 0)
        radius_a = 0;
    if (radius_b < 0)
        radius_b = 0;

    if (radius_a == 0 && radius_b == 0)
        return line;

    const auto offset      = line.B - line.A;
    const auto length_sq   = offset.x * offset.x + offset.y * offset.y;
    const auto radius_a_sq = radius_a * radius_a;
    const auto radius_b_sq = radius_b * radius_b;

    if (length_sq <= 0)
        return line;

    const auto length    = ImSqrt(length_sq);
    const auto direction = ImVec2(offset.x / length, offset.y / length);

    const auto total_radius_sq = radius_a_sq + radius_b_sq;
    if (total_radius_sq > length_sq)
    {
        const auto scale = length / (radius_a + radius_b);
        radius_a *= scale;
        radius_b *= scale;
    }

    line.A = line.A + (direction * radius_a);
    line.B = line.B - (direction * radius_b);

    return line;
}


//------------------------------------------------------------------------------
# endif // __IMGUI_EXTRA_MATH_INL__
