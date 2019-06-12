//------------------------------------------------------------------------------
// LICENSE
//   This software is dual-licensed to the public domain and under the following
//   license: you are granted a perpetual, irrevocable license to copy, modify,
//   publish, and distribute this file as you see fit.
//
// CREDITS
//   Written by Michal Cichon
//------------------------------------------------------------------------------
# ifndef __IMGUI_BEZIER_MATH_H__
# define __IMGUI_BEZIER_MATH_H__
# pragma once


//------------------------------------------------------------------------------
# include "imgui_extra_math.h"


//------------------------------------------------------------------------------
template <typename T>
struct ImCubicBezierPointsT
{
    T P0;
    T P1;
    T P2;
    T P3;
};
using ImCubicBezierPoints = ImCubicBezierPointsT<ImVec2>;


//------------------------------------------------------------------------------
// Low-level Bezier curve sampling.
template <typename T> inline T ImLinearBezier(const T& p0, const T& p1, float t);
template <typename T> inline T ImLinearBezierDt(const T& p0, const T& p1, float t);
template <typename T> inline T ImQuadraticBezier(const T& p0, const T& p1, const T& p2, float t);
template <typename T> inline T ImQuadraticBezierDt(const T& p0, const T& p1, const T& p2, float t);
template <typename T> inline T ImCubicBezier(const T& p0, const T& p1, const T& p2, const T& p3, float t);
template <typename T> inline T ImCubicBezierDt(const T& p0, const T& p1, const T& p2, const T& p3, float t);


// High-level Bezier sampling, automatically collapse to lower level Bezier curves if control points overlap.
template <typename T> inline T ImCubicBezierSample(const T& p0, const T& p1, const T& p2, const T& p3, float t);
template <typename T> inline T ImCubicBezierSample(const ImCubicBezierPointsT<T>& curve, float t);
template <typename T> inline T ImCubicBezierTangent(const T& p0, const T& p1, const T& p2, const T& p3, float t);
template <typename T> inline T ImCubicBezierTangent(const ImCubicBezierPointsT<T>& curve, float t);


// Calculate approximate length of Cubic Bezier curve.
template <typename T> inline float ImCubicBezierLength(const T& p0, const T& p1, const T& p2, const T& p3);
template <typename T> inline float ImCubicBezierLength(const ImCubicBezierPointsT<T>& curve);


// Splits Cubic Bezier curve into two curves.
template <typename T>
struct ImCubicBezierSplitResultT
{
    ImCubicBezierPointsT<T> Left;
    ImCubicBezierPointsT<T> Right;
};
using ImCubicBezierSplitResult = ImCubicBezierSplitResultT<ImVec2>;

template <typename T> inline ImCubicBezierSplitResultT<T> ImCubicBezierSplit(const T& p0, const T& p1, const T& p2, const T& p3, float t);
template <typename T> inline ImCubicBezierSplitResultT<T> ImCubicBezierSplit(const ImCubicBezierPointsT<T>& curve, float t);


// Returns bounding rectangle of Cubic Bezier curve.
inline ImRect ImCubicBezierBoundingRect(const ImVec2& p0, const ImVec2& p1, const ImVec2& p2, const ImVec2& p3);
inline ImRect ImCubicBezierBoundingRect(const ImCubicBezierPoints& curve);


// Project point on Cubic Bezier curve.
struct ImProjectResult
{
    ImVec2 Point;    // Point on curve
    float  Time;     // [0 - 1]
    float  Distance; // Distance to curve
};

inline ImProjectResult ImProjectOnCubicBezier(const ImVec2& p, const ImVec2& p0, const ImVec2& p1, const ImVec2& p2, const ImVec2& p3, const int subdivisions = 100);
inline ImProjectResult ImProjectOnCubicBezier(const ImVec2& p, const ImCubicBezierPoints& curve, const int subdivisions = 100);


// Calculate intersection between line and a Cubic Bezier curve.
struct ImCubicBezierIntersectResult
{
    int Count;
    ImVec2 Points[3];
};

inline ImCubicBezierIntersectResult ImCubicBezierLineIntersect(const ImVec2& p0, const ImVec2& p1, const ImVec2& p2, const ImVec2& p3, const ImVec2& a0, const ImVec2& a1);
inline ImCubicBezierIntersectResult ImCubicBezierLineIntersect(const ImCubicBezierPoints& curve, const ImLine& line);


// Adaptive Cubic Bezier subdivision.
enum ImCubicBezierSubdivideFlags
{
    ImCubicBezierSubdivide_None        = 0,
    ImCubicBezierSubdivide_SkipFirst   = 1
};

struct ImCubicBezierSubdivideSample
{
    ImVec2 Point;
    ImVec2 Tangent;
};

using ImCubicBezierSubdivideCallback = void (*)(const ImCubicBezierSubdivideSample& p, void* user_pointer);

inline void ImCubicBezierSubdivide(ImCubicBezierSubdivideCallback callback, void* user_pointer, const ImVec2& p0, const ImVec2& p1, const ImVec2& p2, const ImVec2& p3, float tess_tol = -1.0f, ImCubicBezierSubdivideFlags flags = ImCubicBezierSubdivide_None);
inline void ImCubicBezierSubdivide(ImCubicBezierSubdivideCallback callback, void* user_pointer, const ImCubicBezierPoints& curve, float tess_tol = -1.0f, ImCubicBezierSubdivideFlags flags = ImCubicBezierSubdivide_None);


// F has signature void(const ImCubicBezierSubdivideSample& p)
template <typename F> inline void ImCubicBezierSubdivide(F& callback, const ImVec2& p0, const ImVec2& p1, const ImVec2& p2, const ImVec2& p3, float tess_tol = -1.0f, ImCubicBezierSubdivideFlags flags = ImCubicBezierSubdivide_None);
template <typename F> inline void ImCubicBezierSubdivide(F& callback, const ImCubicBezierPoints& curve, float tess_tol = -1.0f, ImCubicBezierSubdivideFlags flags = ImCubicBezierSubdivide_None);

// Fixed step Cubic Bezier subdivision.
struct ImCubicBezierFixedStepSample
{
    float  T;
    float  Length;
    ImVec2 Point;
    bool   BreakSearch;
};

using ImCubicBezierFixedStepCallback = void (*)(ImCubicBezierFixedStepSample& sample, void* user_pointer);

inline void ImCubicBezierFixedStep(ImCubicBezierFixedStepCallback callback, void* user_pointer, const ImVec2& p0, const ImVec2& p1, const ImVec2& p2, const ImVec2& p3, float step, bool overshoot = false, float max_value_error = 1e-3f, float max_t_error = 1e-5f);
inline void ImCubicBezierFixedStep(ImCubicBezierFixedStepCallback callback, void* user_pointer, const ImCubicBezierPoints& curve, float step, bool overshoot = false, float max_value_error = 1e-3f, float max_t_error = 1e-5f);


// F has signature void(const ImCubicBezierFixedStepSample& p)
template <typename F> inline void ImCubicBezierFixedStep(F& callback, const ImVec2& p0, const ImVec2& p1, const ImVec2& p2, const ImVec2& p3, float step, bool overshoot = false, float max_value_error = 1e-3f, float max_t_error = 1e-5f);
template <typename F> inline void ImCubicBezierFixedStep(F& callback, const ImCubicBezierPoints& curve, float step, bool overshoot = false, float max_value_error = 1e-3f, float max_t_error = 1e-5f);


//------------------------------------------------------------------------------
# include "imgui_bezier_math.inl"


//------------------------------------------------------------------------------
# endif // __IMGUI_BEZIER_MATH_H__
