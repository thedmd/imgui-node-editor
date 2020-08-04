//------------------------------------------------------------------------------
// LICENSE
//   This software is dual-licensed to the public domain and under the following
//   license: you are granted a perpetual, irrevocable license to copy, modify,
//   publish, and distribute this file as you see fit.
//
// CREDITS
//   Written by Michal Cichon
//------------------------------------------------------------------------------
# ifndef __IMGUI_BEZIER_MATH_INL__
# define __IMGUI_BEZIER_MATH_INL__
# pragma once


//------------------------------------------------------------------------------
# include "imgui_bezier_math.h"
# include <map> // used in ImCubicBezierFixedStep


//------------------------------------------------------------------------------
template <typename T>
inline T ImLinearBezier(const T& p0, const T& p1, float t)
{
    return p0 + t * (p1 - p0);
}

template <typename T>
inline T ImLinearBezierDt(const T& p0, const T& p1, float t)
{
    IM_UNUSED(t);

    return p1 - p0;
}

template <typename T>
inline T ImQuadraticBezier(const T& p0, const T& p1, const T& p2, float t)
{
    const auto a = 1 - t;

    return a * a * p0 + 2 * t * a * p1 + t * t * p2;
}

template <typename T>
inline T ImQuadraticBezierDt(const T& p0, const T& p1, const T& p2, float t)
{
    return 2 * (1 - t) * (p1 - p0) + 2 * t * (p2 - p1);
}

template <typename T>
inline T ImCubicBezier(const T& p0, const T& p1, const T& p2, const T& p3, float t)
{
    const auto a = 1 - t;
    const auto b = a * a * a;
    const auto c = t * t * t;

    return b * p0 + 3 * t * a * a * p1 + 3 * t * t * a * p2 + c * p3;
}

template <typename T>
inline T ImCubicBezierDt(const T& p0, const T& p1, const T& p2, const T& p3, float t)
{
    const auto a = 1 - t;
    const auto b = a * a;
    const auto c = t * t;
    const auto d = 2 * t * a;

    return -3 * p0 * b + 3 * p1 * (b - d) + 3 * p2 * (d - c) + 3 * p3 * c;
}

template <typename T>
inline T ImCubicBezierSample(const T& p0, const T& p1, const T& p2, const T& p3, float t)
{
    const auto cp0_zero = ImLengthSqr(p1 - p0) < 1e-5f;
    const auto cp1_zero = ImLengthSqr(p3 - p2) < 1e-5f;

    if (cp0_zero && cp1_zero)
        return ImLinearBezier(p0, p3, t);
    else if (cp0_zero)
        return ImQuadraticBezier(p0, p2, p3, t);
    else if (cp1_zero)
        return ImQuadraticBezier(p0, p1, p3, t);
    else
        return ImCubicBezier(p0, p1, p2, p3, t);
}

template <typename T>
inline T ImCubicBezierSample(const ImCubicBezierPointsT<T>& curve, float t)
{
    return ImCubicBezierSample(curve.P0, curve.P1, curve.P2, curve.P3, t);
}

template <typename T>
inline T ImCubicBezierTangent(const T& p0, const T& p1, const T& p2, const T& p3, float t)
{
    const auto cp0_zero = ImLengthSqr(p1 - p0) < 1e-5f;
    const auto cp1_zero = ImLengthSqr(p3 - p2) < 1e-5f;

    if (cp0_zero && cp1_zero)
        return ImLinearBezierDt(p0, p3, t);
    else if (cp0_zero)
        return ImQuadraticBezierDt(p0, p2, p3, t);
    else if (cp1_zero)
        return ImQuadraticBezierDt(p0, p1, p3, t);
    else
        return ImCubicBezierDt(p0, p1, p2, p3, t);
}

template <typename T>
inline T ImCubicBezierTangent(const ImCubicBezierPointsT<T>& curve, float t)
{
    return ImCubicBezierTangent(curve.P0, curve.P1, curve.P2, curve.P3, t);
}

template <typename T>
inline float ImCubicBezierLength(const T& p0, const T& p1, const T& p2, const T& p3)
{
    // Legendre-Gauss abscissae with n=24 (x_i values, defined at i=n as the roots of the nth order Legendre polynomial Pn(x))
    static const float t_values[] =
    {
        -0.0640568928626056260850430826247450385909f,
         0.0640568928626056260850430826247450385909f,
        -0.1911188674736163091586398207570696318404f,
         0.1911188674736163091586398207570696318404f,
        -0.3150426796961633743867932913198102407864f,
         0.3150426796961633743867932913198102407864f,
        -0.4337935076260451384870842319133497124524f,
         0.4337935076260451384870842319133497124524f,
        -0.5454214713888395356583756172183723700107f,
         0.5454214713888395356583756172183723700107f,
        -0.6480936519369755692524957869107476266696f,
         0.6480936519369755692524957869107476266696f,
        -0.7401241915785543642438281030999784255232f,
         0.7401241915785543642438281030999784255232f,
        -0.8200019859739029219539498726697452080761f,
         0.8200019859739029219539498726697452080761f,
        -0.8864155270044010342131543419821967550873f,
         0.8864155270044010342131543419821967550873f,
        -0.9382745520027327585236490017087214496548f,
         0.9382745520027327585236490017087214496548f,
        -0.9747285559713094981983919930081690617411f,
         0.9747285559713094981983919930081690617411f,
        -0.9951872199970213601799974097007368118745f,
         0.9951872199970213601799974097007368118745f
    };

    // Legendre-Gauss weights with n=24 (w_i values, defined by a function linked to in the Bezier primer article)
    static const float c_values[] =
    {
        0.1279381953467521569740561652246953718517f,
        0.1279381953467521569740561652246953718517f,
        0.1258374563468282961213753825111836887264f,
        0.1258374563468282961213753825111836887264f,
        0.1216704729278033912044631534762624256070f,
        0.1216704729278033912044631534762624256070f,
        0.1155056680537256013533444839067835598622f,
        0.1155056680537256013533444839067835598622f,
        0.1074442701159656347825773424466062227946f,
        0.1074442701159656347825773424466062227946f,
        0.0976186521041138882698806644642471544279f,
        0.0976186521041138882698806644642471544279f,
        0.0861901615319532759171852029837426671850f,
        0.0861901615319532759171852029837426671850f,
        0.0733464814110803057340336152531165181193f,
        0.0733464814110803057340336152531165181193f,
        0.0592985849154367807463677585001085845412f,
        0.0592985849154367807463677585001085845412f,
        0.0442774388174198061686027482113382288593f,
        0.0442774388174198061686027482113382288593f,
        0.0285313886289336631813078159518782864491f,
        0.0285313886289336631813078159518782864491f,
        0.0123412297999871995468056670700372915759f,
        0.0123412297999871995468056670700372915759f
    };

    static_assert(sizeof(t_values) / sizeof(*t_values) == sizeof(c_values) / sizeof(*c_values), "");

    auto arc = [p0, p1, p2, p3](float t)
    {
        const auto p = ImCubicBezierDt(p0, p1, p2, p3, t);
        const auto l = ImLength(p);
        return l;
    };

    const auto z = 0.5f;
    const auto n = sizeof(t_values) / sizeof(*t_values);

    auto accumulator = 0.0f;
    for (size_t i = 0; i < n; ++i)
    {
        const auto t = z * t_values[i] + z;
        accumulator += c_values[i] * arc(t);
    }

    return z * accumulator;
}

template <typename T>
inline float ImCubicBezierLength(const ImCubicBezierPointsT<T>& curve)
{
    return ImCubicBezierLength(curve.P0, curve.P1, curve.P2, curve.P3);
}

template <typename T>
inline ImCubicBezierSplitResultT<T> ImCubicBezierSplit(const T& p0, const T& p1, const T& p2, const T& p3, float t)
{
    const auto z1 = t;
    const auto z2 = z1 * z1;
    const auto z3 = z1 * z1 * z1;
    const auto s1 = z1 - 1;
    const auto s2 = s1 * s1;
    const auto s3 = s1 * s1 * s1;

    return ImCubicBezierSplitResultT<T>
    {
        ImCubicBezierPointsT<T>
        {
                                                                 p0,
                                             z1      * p1 - s1 * p0,
                          z2      * p2 - 2 * z1 * s1 * p1 + s2 * p0,
            z3 * p3 - 3 * z2 * s1 * p2 + 3 * z1 * s2 * p1 - s3 * p0
        },
        ImCubicBezierPointsT<T>
        {
            z3 * p0 - 3 * z2 * s1 * p1 + 3 * z1 * s2 * p2 - s3 * p3,
                          z2      * p1 - 2 * z1 * s1 * p2 + s2 * p3,
                                             z1      * p2 - s1 * p3,
                                                                 p3,
        }
    };
}

template <typename T>
inline ImCubicBezierSplitResultT<T> ImCubicBezierSplit(const ImCubicBezierPointsT<T>& curve, float t)
{
    return ImCubicBezierSplit(curve.P0, curve.P1, curve.P2, curve.P3, t);
}

inline ImRect ImCubicBezierBoundingRect(const ImVec2& p0, const ImVec2& p1, const ImVec2& p2, const ImVec2& p3)
{
    auto a = 3 * p3 - 9 * p2 + 9 * p1 - 3 * p0;
    auto b = 6 * p0 - 12 * p1 + 6 * p2;
    auto c = 3 * p1 - 3 * p0;
    auto delta_squared = ImMul(b, b) - 4 * ImMul(a, c);

    auto tl = ImMin(p0, p3);
    auto rb = ImMax(p0, p3);

# define IM_VEC2_INDEX(v, i) *(&v.x + i)

    for (int i = 0; i < 2; ++i)
    {
        if (IM_VEC2_INDEX(delta_squared, i) >= 0)
        {
            auto delta = ImSqrt(IM_VEC2_INDEX(delta_squared, i));

            auto t0 = (-IM_VEC2_INDEX(b, i) + delta) / (2 * IM_VEC2_INDEX(a, i));
            if (t0 > 0 && t0 < 1)
            {
                auto p = ImCubicBezier(IM_VEC2_INDEX(p0, i), IM_VEC2_INDEX(p1, i), IM_VEC2_INDEX(p2, i), IM_VEC2_INDEX(p3, i), t0);
                IM_VEC2_INDEX(tl, i) = ImMin(IM_VEC2_INDEX(tl, i), p);
                IM_VEC2_INDEX(rb, i) = ImMax(IM_VEC2_INDEX(rb, i), p);
            }

            auto t1 = (-IM_VEC2_INDEX(b, i) - delta) / (2 * IM_VEC2_INDEX(a, i));
            if (t1 > 0 && t1 < 1)
            {
                auto p = ImCubicBezier(IM_VEC2_INDEX(p0, i), IM_VEC2_INDEX(p1, i), IM_VEC2_INDEX(p2, i), IM_VEC2_INDEX(p3, i), t1);
                IM_VEC2_INDEX(tl, i) = ImMin(IM_VEC2_INDEX(tl, i), p);
                IM_VEC2_INDEX(rb, i) = ImMax(IM_VEC2_INDEX(rb, i), p);
            }
        }
    }

# undef IM_VEC2_INDEX

    return ImRect(tl, rb);
}

inline ImRect ImCubicBezierBoundingRect(const ImCubicBezierPoints& curve)
{
    return ImCubicBezierBoundingRect(curve.P0, curve.P1, curve.P2, curve.P3);
}

inline ImProjectResult ImProjectOnCubicBezier(const ImVec2& point, const ImVec2& p0, const ImVec2& p1, const ImVec2& p2, const ImVec2& p3, const int subdivisions)
{
    // http://pomax.github.io/bezierinfo/#projections

    const float epsilon    = 1e-5f;
    const float fixed_step = 1.0f / static_cast<float>(subdivisions - 1);

    ImProjectResult result;
    result.Point    = point;
    result.Time     = 0.0f;
    result.Distance = FLT_MAX;

    // Step 1: Coarse check
    for (int i = 0; i < subdivisions; ++i)
    {
        auto t = i * fixed_step;
        auto p = ImCubicBezier(p0, p1, p2, p3, t);
        auto s = point - p;
        auto d = ImDot(s, s);

        if (d < result.Distance)
        {
            result.Point    = p;
            result.Time     = t;
            result.Distance = d;
        }
    }

    if (result.Time == 0.0f || ImFabs(result.Time - 1.0f) <= epsilon)
    {
        result.Distance = ImSqrt(result.Distance);
        return result;
    }

    // Step 2: Fine check
    auto left  = result.Time - fixed_step;
    auto right = result.Time + fixed_step;
    auto step  = fixed_step * 0.1f;

    for (auto t = left; t < right + step; t += step)
    {
        auto p = ImCubicBezier(p0, p1, p2, p3, t);
        auto s = point - p;
        auto d = ImDot(s, s);

        if (d < result.Distance)
        {
            result.Point    = p;
            result.Time     = t;
            result.Distance = d;
        }
    }

    result.Distance = ImSqrt(result.Distance);

    return result;
}

inline ImProjectResult ImProjectOnCubicBezier(const ImVec2& p, const ImCubicBezierPoints& curve, const int subdivisions)
{
    return ImProjectOnCubicBezier(p, curve.P0, curve.P1, curve.P2, curve.P3, subdivisions);
}

inline ImCubicBezierIntersectResult ImCubicBezierLineIntersect(const ImVec2& p0, const ImVec2& p1, const ImVec2& p2, const ImVec2& p3, const ImVec2& a0, const ImVec2& a1)
{
    auto cubic_roots = [](float a, float b, float c, float d, float* roots) -> int
    {
        int count = 0;

        auto sign = [](float x) -> float { return x < 0 ? -1.0f : 1.0f; };

        auto A = b / a;
        auto B = c / a;
        auto C = d / a;

        auto Q = (3 * B - ImPow(A, 2)) / 9;
        auto R = (9 * A * B - 27 * C - 2 * ImPow(A, 3)) / 54;
        auto D = ImPow(Q, 3) + ImPow(R, 2);               // polynomial discriminant

        if (D >= 0) // complex or duplicate roots
        {
            auto S = sign(R + ImSqrt(D)) * ImPow(ImFabs(R + ImSqrt(D)), (1.0f / 3.0f));
            auto T = sign(R - ImSqrt(D)) * ImPow(ImFabs(R - ImSqrt(D)), (1.0f / 3.0f));

            roots[0] = -A / 3 + (S + T);                // real root
            roots[1] = -A / 3 - (S + T) / 2;            // real part of complex root
            roots[2] = -A / 3 - (S + T) / 2;            // real part of complex root
            auto Im = ImFabs(ImSqrt(3) * (S - T) / 2);  // complex part of root pair

                                                        // discard complex roots
            if (Im != 0)
                count = 1;
            else
                count = 3;
        }
        else                                            // distinct real roots
        {
            auto th = ImAcos(R / ImSqrt(-ImPow(Q, 3)));

            roots[0] = 2 * ImSqrt(-Q) * ImCos(th / 3) - A / 3;
            roots[1] = 2 * ImSqrt(-Q) * ImCos((th + 2 * IM_PI) / 3) - A / 3;
            roots[2] = 2 * ImSqrt(-Q) * ImCos((th + 4 * IM_PI) / 3) - A / 3;

            count = 3;
        }

        return count;
    };

    // https://github.com/kaishiqi/Geometric-Bezier/blob/master/GeometricBezier/src/kaishiqi/geometric/intersection/Intersection.as
    //
    // Start with Bezier using Bernstein polynomials for weighting functions:
    //     (1-t^3)P0 + 3t(1-t)^2P1 + 3t^2(1-t)P2 + t^3P3
    //
    // Expand and collect terms to form linear combinations of original Bezier
    // controls.  This ends up with a vector cubic in t:
    //     (-P0+3P1-3P2+P3)t^3 + (3P0-6P1+3P2)t^2 + (-3P0+3P1)t + P0
    //             /\                  /\                /\       /\
    //             ||                  ||                ||       ||
    //             c3                  c2                c1       c0

    // Calculate the coefficients
    auto c3 =     -p0 + 3 * p1 - 3 * p2 + p3;
    auto c2 =  3 * p0 - 6 * p1 + 3 * p2;
    auto c1 = -3 * p0 + 3 * p1;
    auto c0 =      p0;

    // Convert line to normal form: ax + by + c = 0
    auto a = a1.y - a0.y;
    auto b = a0.x - a1.x;
    auto c = a0.x * (a0.y - a1.y) + a0.y * (a1.x - a0.x);

    // Rotate each cubic coefficient using line for new coordinate system?
    // Find roots of rotated cubic
    float roots[3];
    auto rootCount = cubic_roots(
        a * c3.x + b * c3.y,
        a * c2.x + b * c2.y,
        a * c1.x + b * c1.y,
        a * c0.x + b * c0.y + c,
        roots);

    // Any roots in closed interval [0,1] are intersections on Bezier, but
    // might not be on the line segment.
    // Find intersections and calculate point coordinates

    auto min = ImMin(a0, a1);
    auto max = ImMax(a0, a1);

    ImCubicBezierIntersectResult result;
    auto points = result.Points;

    for (int i = 0; i < rootCount; ++i)
    {
        auto root = roots[i];

        if (0 <= root && root <= 1)
        {
            // We're within the Bezier curve
            // Find point on Bezier
            auto p = ImCubicBezier(p0, p1, p2, p3, root);

            // See if point is on line segment
            // Had to make special cases for vertical and horizontal lines due
            // to slight errors in calculation of p00
            if (a0.x == a1.x)
            {
                if (min.y <= p.y && p.y <= max.y)
                    *points++ = p;
            }
            else if (a0.y == a1.y)
            {
                if (min.x <= p.x && p.x <= max.x)
                    *points++ = p;
            }
            else if (p.x >= min.x && p.y >= min.y && p.x <= max.x && p.y <= max.y)
            {
                *points++ = p;
            }
        }
    }

    result.Count = static_cast<int>(points - result.Points);

    return result;
}

inline ImCubicBezierIntersectResult ImCubicBezierLineIntersect(const ImCubicBezierPoints& curve, const ImLine& line)
{
    return ImCubicBezierLineIntersect(curve.P0, curve.P1, curve.P2, curve.P3, line.A, line.B);
}

inline void ImCubicBezierSubdivide(ImCubicBezierSubdivideCallback callback, void* user_pointer, const ImVec2& p0, const ImVec2& p1, const ImVec2& p2, const ImVec2& p3, float tess_tol, ImCubicBezierSubdivideFlags flags)
{
    return ImCubicBezierSubdivide(callback, user_pointer, ImCubicBezierPoints{ p0, p1, p2, p3 }, tess_tol, flags);
}

inline void ImCubicBezierSubdivide(ImCubicBezierSubdivideCallback callback, void* user_pointer, const ImCubicBezierPoints& curve, float tess_tol, ImCubicBezierSubdivideFlags flags)
{
    struct Tesselator
    {
        ImCubicBezierSubdivideCallback  Callback;
        void*                           UserPointer;
        float                           TesselationTollerance;
        ImCubicBezierSubdivideFlags     Flags;

        void Commit(const ImVec2& p, const ImVec2& t)
        {
            ImCubicBezierSubdivideSample sample;
            sample.Point = p;
            sample.Tangent = t;
            Callback(sample, UserPointer);
        }

        void Subdivide(const ImCubicBezierPoints& curve, int level = 0)
        {
            float dx = curve.P3.x - curve.P0.x;
            float dy = curve.P3.y - curve.P0.y;
            float d2 = ((curve.P1.x - curve.P3.x) * dy - (curve.P1.y - curve.P3.y) * dx);
            float d3 = ((curve.P2.x - curve.P3.x) * dy - (curve.P2.y - curve.P3.y) * dx);
            d2 = (d2 >= 0) ? d2 : -d2;
            d3 = (d3 >= 0) ? d3 : -d3;
            if ((d2 + d3) * (d2 + d3) < TesselationTollerance * (dx * dx + dy * dy))
            {
                Commit(curve.P3, ImCubicBezierTangent(curve, 1.0f));
            }
            else if (level < 10)
            {
                const auto p12 = (curve.P0 + curve.P1) * 0.5f;
                const auto p23 = (curve.P1 + curve.P2) * 0.5f;
                const auto p34 = (curve.P2 + curve.P3) * 0.5f;
                const auto p123 = (p12 + p23) * 0.5f;
                const auto p234 = (p23 + p34) * 0.5f;
                const auto p1234 = (p123 + p234) * 0.5f;

                Subdivide(ImCubicBezierPoints { curve.P0, p12, p123, p1234 }, level + 1);
                Subdivide(ImCubicBezierPoints { p1234, p234, p34, curve.P3 }, level + 1);
            }
        }
    };

    if (tess_tol < 0)
        tess_tol = 1.118f; // sqrtf(1.25f)

    Tesselator tesselator;
    tesselator.Callback              = callback;
    tesselator.UserPointer           = user_pointer;
    tesselator.TesselationTollerance = tess_tol * tess_tol;
    tesselator.Flags                 = flags;

    if (!(tesselator.Flags & ImCubicBezierSubdivide_SkipFirst))
        tesselator.Commit(curve.P0, ImCubicBezierTangent(curve, 0.0f));

    tesselator.Subdivide(curve, 0);
}

template <typename F> inline void ImCubicBezierSubdivide(F& callback, const ImVec2& p0, const ImVec2& p1, const ImVec2& p2, const ImVec2& p3, float tess_tol, ImCubicBezierSubdivideFlags flags)
{
    auto handler = [](const ImCubicBezierSubdivideSample& p, void* user_pointer)
    {
        auto& callback = *reinterpret_cast<F*>(user_pointer);
        callback(p);
    };

    ImCubicBezierSubdivide(handler, &callback, ImCubicBezierPoints{ p0, p1, p2, p3 }, tess_tol, flags);
}

template <typename F> inline void ImCubicBezierSubdivide(F& callback, const ImCubicBezierPoints& curve, float tess_tol, ImCubicBezierSubdivideFlags flags)
{
    auto handler = [](const ImCubicBezierSubdivideSample& p, void* user_pointer)
    {
        auto& callback = *reinterpret_cast<F*>(user_pointer);
        callback(p);
    };

    ImCubicBezierSubdivide(handler, &callback, curve, tess_tol, flags);
}

inline void ImCubicBezierFixedStep(ImCubicBezierFixedStepCallback callback, void* user_pointer, const ImVec2& p0, const ImVec2& p1, const ImVec2& p2, const ImVec2& p3, float step, bool overshoot, float max_value_error, float max_t_error)
{
    if (step <= 0.0f || !callback || max_value_error <= 0 || max_t_error <= 0)
        return;

    ImCubicBezierFixedStepSample sample;
    sample.T           = 0.0f;
    sample.Length      = 0.0f;
    sample.Point       = p0;
    sample.BreakSearch = false;

    callback(sample, user_pointer);
    if (sample.BreakSearch)
        return;

    const auto total_length = ImCubicBezierLength(p0, p1, p2, p3);
    const auto point_count  = static_cast<int>(total_length / step) + (overshoot ? 2 : 1);
    const auto t_min        = 0.0f;
    const auto t_max        = step * point_count / total_length;
    const auto t_0          = (t_min + t_max) * 0.5f;

    // #todo: replace map with ImVector + binary search
    std::map<float, float> cache;
    for (int point_index = 1; point_index < point_count; ++point_index)
    {
        const auto targetLength = point_index * step;

        float t_start = t_min;
        float t_end   = t_max;
        float t       = t_0;

        float t_best     = t;
        float error_best = total_length;

        while (true)
        {
            auto cacheIt = cache.find(t);
            if (cacheIt == cache.end())
            {
                const auto front        = ImCubicBezierSplit(p0, p1, p2, p3, t).Left;
                const auto split_length = ImCubicBezierLength(front);

                cacheIt = cache.emplace(t, split_length).first;
            }

            const auto length   = cacheIt->second;
            const auto error    = targetLength - length;

            if (error < error_best)
            {
                error_best = error;
                t_best     = t;
            }

            if (ImFabs(error) <= max_value_error || ImFabs(t_start - t_end) <= max_t_error)
            {
                sample.T      = t;
                sample.Length = length;
                sample.Point  = ImCubicBezier(p0, p1, p2, p3, t);

                callback(sample, user_pointer);
                if (sample.BreakSearch)
                    return;

                break;
            }
            else if (error < 0.0f)
                t_end = t;
            else // if (error > 0.0f)
                t_start = t;

            t = (t_start + t_end) * 0.5f;
        }
    }
}

inline void ImCubicBezierFixedStep(ImCubicBezierFixedStepCallback callback, void* user_pointer, const ImCubicBezierPoints& curve, float step, bool overshoot, float max_value_error, float max_t_error)
{
    ImCubicBezierFixedStep(callback, user_pointer, curve.P0, curve.P1, curve.P2, curve.P3, step, overshoot, max_value_error, max_t_error);
}

// F has signature void(const ImCubicBezierFixedStepSample& p)
template <typename F>
inline void ImCubicBezierFixedStep(F& callback, const ImVec2& p0, const ImVec2& p1, const ImVec2& p2, const ImVec2& p3, float step, bool overshoot, float max_value_error, float max_t_error)
{
    auto handler = [](ImCubicBezierFixedStepSample& sample, void* user_pointer)
    {
        auto& callback = *reinterpret_cast<F*>(user_pointer);
        callback(sample);
    };

    ImCubicBezierFixedStep(handler, &callback, p0, p1, p2, p3, step, overshoot, max_value_error, max_t_error);
}

template <typename F>
inline void ImCubicBezierFixedStep(F& callback, const ImCubicBezierPoints& curve, float step, bool overshoot, float max_value_error, float max_t_error)
{
    auto handler = [](ImCubicBezierFixedStepSample& sample, void* user_pointer)
    {
        auto& callback = *reinterpret_cast<F*>(user_pointer);
        callback(sample);
    };

    ImCubicBezierFixedStep(handler, &callback, curve.P0, curve.P1, curve.P2, curve.P3, step, overshoot, max_value_error, max_t_error);
}


//------------------------------------------------------------------------------
# endif // __IMGUI_BEZIER_MATH_INL__
