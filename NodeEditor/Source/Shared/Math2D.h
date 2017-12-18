//------------------------------------------------------------------------------
// 2D friendly math library
//
// LICENSE
//   This software is dual-licensed to the public domain and under the following
//   license: you are granted a perpetual, irrevocable license to copy, modify,
//   publish, and distribute this file as you see fit.
//
// CREDITS
//   Written by Michal Cichon
//------------------------------------------------------------------------------
# if !defined(__AX_MATH_2D_H__)
# define __AX_MATH_2D_H__
# pragma once


//------------------------------------------------------------------------------
# if defined(__cplusplus)


//------------------------------------------------------------------------------
# include <algorithm>
# include <type_traits>
# include <vector>
# include <map>
# include <cmath>


//------------------------------------------------------------------------------
namespace ax {


//------------------------------------------------------------------------------
constexpr float AX_PI = 3.14159265358979323846f;


//------------------------------------------------------------------------------
enum class matrix_order
{
    prepend,
    append,
    set
};


//------------------------------------------------------------------------------
template <typename T>
struct basic_size;


//------------------------------------------------------------------------------
template <typename T>
struct basic_point
{
    typedef T value_type;

    union
    {
        struct { T x, y; };
        T values[2];
    };

    basic_point(): x(0), y(0) {}
    basic_point(T x, T y): x(x), y(y) {}

    template <typename P>
    explicit basic_point(const basic_point<P>& p) { x = static_cast<T>(p.x); y = static_cast<T>(p.y); }

    basic_point(const basic_point&) = default;
    basic_point(basic_point&&) = default;
    basic_point& operator=(const basic_point&) = default;
    basic_point& operator=(basic_point&&) = default;

    template <typename P>
    explicit operator basic_size<P>() const { return basic_size<P>(static_cast<P>(x), static_cast<P>(y)); }

    friend inline value_type dot(const basic_point& lhs, const basic_point& rhs) { return lhs.x * rhs.x + lhs.y * rhs.y; }

    inline T length_sq() const
    {
        return dot(*this, *this);
    }

    inline T length() const
    {
        return static_cast<T>(sqrtf(static_cast<float>(length_sq())));
    }

    inline basic_point normalized() const
    {
        const auto self      = static_cast<basic_point<float>>(*this);
        const auto lenght_sq = dot(self, self);
        if (lenght_sq == 1) return *this;
        if (lenght_sq == 0) return basic_point(0, 0);
        const auto inv_length_sq = 1.0f / sqrtf(lenght_sq);
        return static_cast<basic_point>(self * inv_length_sq);
    }

    inline void normalize() { *this = normalized(); }

    inline basic_point followed(const basic_point& target, T distance) const
    {
        return *this + static_cast<basic_point>(static_cast<basic_point<float>>(target - *this).normalized() * static_cast<float>(distance));
    }

    inline void follow(const basic_point& target, T distance)
    {
        *this = followed(target, distance);
    }

    inline basic_point cwise_min(const basic_point& rhs) const { return basic_point(std::min(x, rhs.x), std::min(y, rhs.y)); }
    inline basic_point cwise_max(const basic_point& rhs) const { return basic_point(std::max(x, rhs.x), std::max(y, rhs.y)); }
    inline basic_point cwise_product(const basic_point& rhs) const { return basic_point(x * rhs.x, y * rhs.y); }
    inline basic_point cwise_quotient(const basic_point& rhs) const { return basic_point(x / rhs.x, y / rhs.y); }
    inline basic_point cwise_safe_quotient(const basic_point& rhs, const basic_point& alt = basic_point()) const { return basic_point(rhs.x ? x / rhs.x : alt.x, rhs.y ? y / rhs.y : alt.x); }
    inline basic_point cwise_abs()   const { return basic_point(fabsf(x), fabsf(y)); }
    inline basic_point cwise_sqrt()  const { return basic_point(sqrtf(x), sqrtf(y)); }
    inline basic_point cwise_floor() const { return basic_point(floorf(x), floorf(y)); }
    inline basic_point cwise_ceil()  const { return basic_point(ceilf(x), ceilf(y)); }
    inline basic_point cwise_round() const { return basic_point(roundf(x), roundf(y)); }

    friend inline bool operator == (const basic_point& lhs, const basic_point& rhs) { return lhs.x == rhs.x && lhs.y == rhs.y; }
    friend inline bool operator != (const basic_point& lhs, const basic_point& rhs) { return !(lhs == rhs); }

          T& operator[](size_t i)       { return values[i]; }
    const T& operator[](size_t i) const { return values[i]; }

    friend inline basic_point operator + (const basic_point& lhs) { return lhs; }
    friend inline basic_point operator - (const basic_point& lhs) { return basic_point(-lhs.x, -lhs.y); }

    friend inline basic_point operator + (const basic_point& lhs, const basic_point& rhs) { return basic_point(lhs.x + rhs.x, lhs.y + rhs.y); }
    friend inline basic_point operator - (const basic_point& lhs, const basic_point& rhs) { return basic_point(lhs.x - rhs.x, lhs.y - rhs.y); }

    template <typename P> friend basic_point operator + (const basic_size<P>& lhs, const basic_point&   rhs);
    template <typename P> friend basic_point operator - (const basic_point&   lhs, const basic_size<P>& rhs);

    friend inline basic_point operator * (T lhs, const basic_point& rhs) { return basic_point(lhs * rhs.x, lhs * rhs.y); }
    friend inline basic_point operator * (const basic_point& lhs, T rhs) { return basic_point(lhs.x * rhs, lhs.y * rhs); }

    basic_point& operator += (const basic_point& rhs) { *this = *this + rhs; return *this; }
    basic_point& operator -= (const basic_point& rhs) { *this = *this - rhs; return *this; }
};

typedef basic_point<int>    point;
typedef basic_point<float>  pointf;


//------------------------------------------------------------------------------
template <typename T>
struct basic_size
{
    T w, h;

    basic_size(): w(0), h(0) {}
    basic_size(T w, T h): w(w), h(h) {}

    template <typename P>
    explicit basic_size(const basic_size<P>& p) { w = static_cast<T>(p.w); h = static_cast<T>(p.h); }

    basic_size(const basic_size&) = default;
    basic_size(basic_size&&) = default;
    basic_size& operator=(const basic_size&) = default;
    basic_size& operator=(basic_size&&) = default;

    template <typename P>
    explicit operator basic_point<P>() const { return basic_point<P>(static_cast<P>(w), static_cast<P>(h)); }

    friend inline bool operator == (const basic_size& lhs, const basic_size& rhs) { return lhs.w == rhs.w && lhs.h == rhs.h; }
    friend inline bool operator != (const basic_size& lhs, const basic_size& rhs) { return !(lhs == rhs); }

    bool is_empty() const { return w <= 0 || h <= 0; }
};

typedef basic_size<int>    size;
typedef basic_size<float>  sizef;


//------------------------------------------------------------------------------
template <typename T>
struct basic_line
{
    typedef basic_point<T> point_t;

    union
    {
        struct { point_t a; point_t b; };
        struct { T x0, y0, x1, y1; };
    };

    basic_line(): x0(0), y0(0), x1(0), y1(0) {}
    basic_line(const point_t& a, const point_t& b): a(a), b(b) {}
    basic_line(const point_t& p, const size_t& s): a(p), b(p + s) {}
    basic_line(T x0, T y0, T x1, T y1): x0(x0), y0(y0), x1(x1), y1(y1) {}

    template <typename P>
    explicit basic_line(const basic_line<P>& l)
    {
        a = static_cast<basic_point<T>>(l.a);
        b = static_cast<basic_point<T>>(l.b);
    }

    basic_line(const basic_line&) = default;
    basic_line(basic_line&&) = default;
    basic_line& operator=(const basic_line&) = default;
    basic_line& operator=(basic_line&&) = default;

    template <typename P>
    explicit operator basic_line<P>() const
    {
        return basic_line<P>(
            static_cast<basic_point<T>>(this->l.a),
            static_cast<basic_point<T>>(this->l.b));
    }
};

typedef basic_line<int>   line;
typedef basic_line<float> line_f;


//------------------------------------------------------------------------------
enum class rect_region
{
    center          = 0x00000,
    top             = 0x00001,
    bottom          = 0x00002,
    left            = 0x00004,
    right           = 0x00008,
    top_left        = top | left,
    top_right       = top | right,
    bottom_left     = bottom | left,
    bottom_right    = bottom | right,
};

inline rect_region operator |(rect_region lhs, rect_region rhs) { return static_cast<rect_region>(static_cast<int>(lhs) | static_cast<int>(rhs)); }
inline rect_region operator &(rect_region lhs, rect_region rhs) { return static_cast<rect_region>(static_cast<int>(lhs) & static_cast<int>(rhs)); }


//------------------------------------------------------------------------------
template <typename T>
struct basic_rect
{
    typedef basic_point<T> point_t;
    typedef basic_size<T>  size_t;

    union
    {
        struct { point_t location; size_t size; };
        struct { T x, y, w, h; };
    };

    basic_rect(): x(0), y(0), w(0), h(0) {}
    basic_rect(const point_t& tl, const point_t& br): location(tl), size(br.x - tl.x, br.y - tl.y) {}
    basic_rect(const point_t& l, const size_t& s): location(l), size(s) {}
    basic_rect(T x, T y, T w, T h): x(x), y(y), w(w), h(h) {}

    template <typename P>
    explicit basic_rect(const basic_rect<P>& r)
    {
        const auto tl = static_cast<basic_point<T>>(r.top_left());
        const auto br = static_cast<basic_point<T>>(r.bottom_right());

        location = tl;
        size     = size_t(br - tl);
    }

    basic_rect(const basic_rect&) = default;
    basic_rect(basic_rect&&) = default;
    basic_rect& operator=(const basic_rect&) = default;
    basic_rect& operator=(basic_rect&&) = default;

    template <typename P>
    explicit operator basic_rect<P>() const
    {
        return basic_rect<P>(
            static_cast<basic_point<P>>(top_left()),
            static_cast<basic_point<P>>(bottom_right()));
    }

    friend inline bool operator == (const basic_rect& lhs, const basic_rect& rhs) { return lhs.location == rhs.location && lhs.size == rhs.size; }
    friend inline bool operator != (const basic_rect& lhs, const basic_rect& rhs) { return !(lhs == rhs); }

    point_t top_left() const { return point_t(x, y); }
    point_t top_right() const { return point_t(x + w, y); }
    point_t bottom_left() const { return point_t(x, y + h); }
    point_t bottom_right() const { return point_t(x + w, y + h); }

    T left()   const { return x;     }
    T right()  const { return x + w; }
    T top()    const { return y;     }
    T bottom() const { return y + h; }

    point_t center()   const { return point_t(center_x(), center_y()); }
    T       center_x() const { return x + w / 2; }
    T       center_y() const { return y + h / 2; }

    pointf centerf()   const { return pointf(centerf_x(), centerf_y()); }
    float  centerf_x() const { return x + w / 2.0f; }
    float  centerf_y() const { return y + h / 2.0f; }

    bool is_empty() const { return size.is_empty(); }

    template <typename P>
    bool contains(const basic_point<P>& p) const
    {
        return p.x >= x && p.y >= y && p.x < right() && p.y < bottom();
    }

    template <typename P>
    bool contains(const basic_rect<P>& r) const
    {
        return r.x >= x && r.y >= y && r.right() <= right() && r.bottom() <= bottom();
    }

    template <typename P>
    bool intersects(const basic_rect<P>& r) const
    {
        return left() < r.right() && right() > r.left() && top() < r.bottom() && bottom() > r.top();
    }

    template <typename P>
    void expand(P extent)
    {
        expand(extent, extent);
    }

    template <typename P>
    void expand(P hotizontal, P vertical)
    {
        expand(hotizontal, vertical, hotizontal, vertical);
    }

    template <typename P>
    void expand(P left, P top, P right, P bottom)
    {
        x -= left;
        y -= top;
        w += left + right;
        h += top + bottom;
    }

    template <typename P>
    basic_rect expand(P hotizontal, P vertical)  const
    {
        auto result = basic_rect(*this);
        result.expand(hotizontal, vertical);
        return result;
    }

    template <typename P>
    basic_rect expand(P left, P top, P right, P bottom)  const
    {
        auto result = basic_rect(*this);
        result.expand(left, top, right, bottom);
        return result;
    }

    template <typename P>
    basic_rect expanded(P extent) const
    {
        auto result = basic_rect(*this);
        result.expand(extent);
        return result;
    }

    template <typename P>
    basic_rect expanded(P hotizontal, P vertical) const
    {
        auto result = basic_rect(*this);
        result.expand(hotizontal, vertical);
        return result;
    }

    template <typename P>
    basic_rect expanded(P left, P top, P right, P bottom) const
    {
        auto result = basic_rect(*this);
        result.expand(left, top, right, bottom);
        return result;
    }

    point_t get_closest_point(const point_t& p, bool on_edge) const
    {
        if (!on_edge && contains(p))
            return p;

        return point_t(
            (p.x > right())  ? right()  : (p.x < left() ? left() : p.x),
            (p.y > bottom()) ? bottom() : (p.y < top()  ? top()  : p.y));
    }

    point_t get_closest_point(const point_t& p, bool on_edge, float radius) const;

    point_t get_closest_point(const basic_rect& r) const;
    point_t get_closest_point_hollow(const point_t& p, T rounding = 0, rect_region* region = nullptr) const;

    basic_line<T> get_closest_line(const basic_rect& r) const;
    basic_line<T> get_closest_line(const basic_rect& r, T radius) const;
    basic_line<T> get_closest_line(const basic_rect& r, T radius_a, T radius_b) const;
};

template <typename T>
static inline basic_rect<T> make_intersection(const basic_rect<T>& lhs, const basic_rect<T>& rhs)
{
    if (lhs.is_empty())
        return lhs;
    else if (rhs.is_empty())
        return rhs;

    const auto tl = lhs.top_left().cwise_max(rhs.top_left());
    const auto br = lhs.bottom_right().cwise_min(rhs.bottom_right());

    return basic_rect<T>(tl, br);
}

template <typename T>
static inline basic_rect<T> make_union(const basic_rect<T>& lhs, const basic_rect<T>& rhs)
{
    if (lhs.is_empty())
        return rhs;
    else if (rhs.is_empty())
        return lhs;

    const auto tl = lhs.top_left().cwise_min(rhs.top_left());
    const auto br = lhs.bottom_right().cwise_max(rhs.bottom_right());

    return basic_rect<T>(tl, br);
}

template <typename T>
static inline basic_rect<T> make_union(const basic_rect<T>& lhs, const basic_point<T>& p)
{
    if (lhs.is_empty())
        return basic_rect<T>(p, p);

    const auto tl = lhs.top_left().cwise_min(p);
    const auto br = lhs.bottom_right().cwise_max(p);

    return basic_rect<T>(tl, br);
}

typedef basic_rect<int>   rect;
typedef basic_rect<float> rectf;


//------------------------------------------------------------------------------
struct matrix
{
    float m11, m12, m21, m22, m31, m32;

    matrix(): m11(1), m12(0), m21(0), m22(1), m31(0), m32(0) {}
    matrix(float m11, float m12, float m21, float m22, float m31, float m32):
        m11(m11), m12(m12), m21(m21), m22(m22), m31(m31), m32(m32) {}

    void zero();
    void reset();

    bool invert();

    void translate(float x, float y, matrix_order order = matrix_order::prepend);
    void rotate(float angle, matrix_order order = matrix_order::prepend);
    void rotate_at(float angle, float cx, float cy, matrix_order order = matrix_order::prepend);
    void scale(float x, float y, matrix_order order = matrix_order::prepend);
    void shear(float x, float y, matrix_order order = matrix_order::prepend);

    void combine(const matrix& matrix, matrix_order order = matrix_order::prepend);

    matrix inverted() const;
};

struct matrix4
{
    float m11, m12, m13, m14;
    float m21, m22, m23, m24;
    float m31, m32, m33, m34;
    float m41, m42, m43, m44;

    matrix4():
        m11(1), m12(0), m13(0), m14(0),
        m21(0), m22(1), m23(0), m24(0),
        m31(0), m32(0), m33(1), m34(0),
        m41(0), m42(0), m43(0), m44(1)
    {
    }

    matrix4(
        float m11, float m12, float m13, float m14,
        float m21, float m22, float m23, float m24,
        float m31, float m32, float m33, float m34,
        float m41, float m42, float m43, float m44):
        m11(m11), m12(m12), m13(m13), m14(m14),
        m21(m21), m22(m22), m23(m23), m24(m24),
        m31(m31), m32(m32), m33(m33), m34(m34),
        m41(m41), m42(m42), m43(m43), m44(m44)
    {
    }

    matrix4(const matrix& m):
        m11(m.m11), m12(m.m12), m13(0.0f), m14(0.0f),
        m21(m.m21), m22(m.m22), m23(0.0f), m24(0.0f),
        m31(0.0f),  m32(0.0f),  m33(1.0f), m34(0.0f),
        m41(m.m31), m42(m.m32), m43(0.0f), m44(1.0f)
    {
    }

    void zero();
    void reset();

    bool invert();
    void transpose();

    void translate(float x, float y, float z, matrix_order order = matrix_order::prepend);
    void rotate_x(float angle, matrix_order order = matrix_order::prepend);
    void rotate_y(float angle, matrix_order order = matrix_order::prepend);
    void rotate_z(float angle, matrix_order order = matrix_order::prepend);
    void rotate_axis(float angle, float x, float y, float z, matrix_order order = matrix_order::prepend);
    void scale(float x, float y, float z, matrix_order order = matrix_order::prepend);

    void combine(const matrix4& matrix, matrix_order order = matrix_order::prepend);

    matrix4 inverted() const;
    matrix4 transposed() const;
};


//------------------------------------------------------------------------------
namespace detail {
template <typename M, typename T> void transform_points(const M& matrix, basic_point<T>* points, size_t count);
template <typename M, typename T> void transform_vectors(const M& matrix, basic_point<T>* points, size_t count);
} // namespace detail


//------------------------------------------------------------------------------
template <typename M, typename T>
inline basic_point<T> transformed(const basic_point<T>& point, const M& matrix)
{
    auto result = point;
    detail::transform_points(matrix, &result, 1);
    return result;
}

template <typename M, typename T>
inline void transform(basic_point<T>& point, const M& matrix)
{
    detail::transform_points(matrix, &point, 1);
}

template <typename M, typename T, size_t N>
inline void transform(basic_point<T> (&point)[N], const M& matrix)
{
    detail::transform_points(matrix, point, N);
}

template <typename M, typename T>
inline void transform(basic_point<T>* point, size_t n, const M& matrix)
{
    detail::transform_points(matrix, point, n);
}

template <typename M, typename T>
inline basic_point<T> transformed_v(const basic_point<T>& vector, const M& matrix)
{
    auto result = vector;
    detail::transform_vectors(matrix, &result, 1);
    return result;
}

template <typename M, typename T>
inline void transform_v(basic_point<T>& point, const M& matrix)
{
    detail::transform_vectors(matrix, &point, 1);
}

template <typename M, typename T, size_t N>
inline void transform_v(basic_point<T>(&point)[N], const M& matrix)
{
    detail::transform_vectors(matrix, point, N);
}

template <typename M, typename T>
inline void transform_v(basic_point<T>* point, size_t n, const M& matrix)
{
    detail::transform_vectors(matrix, point, n);
}


//------------------------------------------------------------------------------
template <typename P, typename T>
inline P linear_bezier(const P& p0, const P& p1, T t)
{
    return p0 + t * (p1 - p0);
}

template <typename P, typename T>
inline P linear_bezier_dt(const P& p0, const P& p1, T t)
{
    return p1 - p0;
}


//------------------------------------------------------------------------------
template <typename P, typename T>
inline P quadratic_bezier(const P& p0, const P& p1, const P& p2, T t)
{
    const auto a = 1 - t;

    return a * a * p0 + 2 * t * a * p1 + t * t * p2;
}

template <typename P, typename T>
inline P quadratic_bezier_dt(const P& p0, const P& p1, const P& p2, T t)
{
    return 2 * (1 - t) * (p1 - p0) + 2 * t * (p2 - p1);
}


//------------------------------------------------------------------------------
template <typename P, typename T>
inline P cubic_bezier(const P& p0, const P& p1, const P& p2, const P& p3, T t)
{
    const auto a = 1 - t;
    const auto b = a * a * a;
    const auto c = t * t * t;

    return b * p0 + 3 * t * a * a * p1 + 3 * t * t * a * p2 + c * p3;
}

template <typename P, typename T>
inline P cubic_bezier_dt(const P& p0, const P& p1, const P& p2, const P& p3, T t)
{
    const auto a = 1 - t;
    const auto b = a * a;
    const auto c = t * t;
    const auto d = 2 * t * a;

    return -3 * p0 * b + 3 * p1 * (b - d) + 3 * p2 * (d - c) + 3 * p3 * c;
}


//------------------------------------------------------------------------------
struct cubic_bezier_t
{
    pointf p0;
    pointf p1;
    pointf p2;
    pointf p3;

    inline pointf sample(float t) const
    {
        const auto cp0_zero = (p1 - p0).length_sq() < 0.0001f;
        const auto cp1_zero = (p3 - p2).length_sq() < 0.0001f;

        if (cp0_zero && cp1_zero)
            return linear_bezier(p0, p3, t);
        else if (cp0_zero)
            return quadratic_bezier(p0, p2, p3, t);
        else if (cp1_zero)
            return quadratic_bezier(p0, p1, p3, t);
        else
            return cubic_bezier(p0, p1, p2, p3, t);
    }

    inline pointf tangent(float t) const
    {
        const auto cp0_zero = (p1 - p0).length_sq() < 0.0001f;
        const auto cp1_zero = (p3 - p2).length_sq() < 0.0001f;

        if (cp0_zero && cp1_zero)
            return linear_bezier_dt(p0, p3, t);
        else if (cp0_zero)
            return quadratic_bezier_dt(p0, p2, p3, t);
        else if (cp1_zero)
            return quadratic_bezier_dt(p0, p1, p3, t);
        else
            return cubic_bezier_dt(p0, p1, p2, p3, t);
    }

    inline pointf normal(float t) const
    {
        const auto tangent = this->tangent(t);
        return pointf(-tangent.y, tangent.x);
    }
};

struct cubic_bezier_split_t
{
    cubic_bezier_t left;
    cubic_bezier_t right;
};

struct cubic_bezier_project_result
{
    float position;
    float distance;
};

inline cubic_bezier_project_result cubic_bezier_project_point(const pointf& f, const pointf& p0, const pointf& p1, const pointf& p2, const pointf& p3, const int subdivisions = 100)
{
    // http://pomax.github.io/bezierinfo/#projections

    const float epsilon = 1e-6f;
    const float fixed_step = 1.0f / static_cast<float>(subdivisions - 1);

    float distance = std::numeric_limits<float>::max();
    float position = 0.0f;

    // Step 1: Coarse check
    for (int i = 0; i < subdivisions; ++i)
    {
        auto t = i * fixed_step;
        auto p = cubic_bezier(p0, p1, p2, p3, t);
        auto s = f - p;
        auto d = dot(s, s);

        if (d < distance)
        {
            distance = d;
            position = t;
        }
    }

    if (position == 0.0f || fabsf(position - 1.0f) <= epsilon)
        return cubic_bezier_project_result{position, sqrtf(distance)};

    // Step 2: Fine check
    auto left = position - fixed_step;
    auto right = position + fixed_step;
    auto step = fixed_step * 0.1f;

    for (auto t = left; t < right + step; t += step)
    {
        auto p = cubic_bezier(p0, p1, p2, p3, t);
        auto s = f - p;
        auto d = dot(s, s);

        if (d < distance)
        {
            distance = d;
            position = t;
        }
    }

    return cubic_bezier_project_result{ position, sqrtf(distance) };
}

inline int cubic_bezier_line_intersect(const pointf& p0, const pointf& p1, const pointf& p2, const pointf& p3, const pointf& a0, const pointf& a1, pointf results[3])
{
    auto cubic_roots = [](float a, float b, float c, float d, float* roots) -> int
    {
        int count = 0;

        auto sign = [](float x) -> float { return x < 0 ? -1.0f : 1.0f; };

        auto A = b / a;
        auto B = c / a;
        auto C = d / a;

        auto Q = (3 * B - powf(A, 2)) / 9;
        auto R = (9 * A * B - 27 * C - 2 * powf(A, 3)) / 54;
        auto D = powf(Q, 3) + powf(R, 2);               // polynomial discriminant

        if (D >= 0) // complex or duplicate roots
        {
            auto S = sign(R + sqrtf(D)) * powf(fabsf(R + sqrtf(D)), (1.0f / 3.0f));
            auto T = sign(R - sqrtf(D)) * powf(fabsf(R - sqrtf(D)), (1.0f / 3.0f));

            roots[0] = -A / 3 + (S + T);                // real root
            roots[1] = -A / 3 - (S + T) / 2;            // real part of complex root
            roots[2] = -A / 3 - (S + T) / 2;            // real part of complex root
            auto Im = fabsf(sqrtf(3) * (S - T) / 2);    // complex part of root pair

                                                        // discard complex roots
            if (Im != 0)
                count = 1;
            else
                count = 3;
        }
        else                                            // distinct real roots
        {
            auto th = acosf(R / sqrtf(-powf(Q, 3)));

            roots[0] = 2 * sqrtf(-Q) * cosf(th / 3) - A / 3;
            roots[1] = 2 * sqrtf(-Q) * cosf((th + 2 * AX_PI) / 3) - A / 3;
            roots[2] = 2 * sqrtf(-Q) * cosf((th + 4 * AX_PI) / 3) - A / 3;

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

    auto min = a0.cwise_min(a1);
    auto max = a0.cwise_max(a1);

    auto result = results;
    for (int i = 0; i < rootCount; ++i)
    {
        auto root = roots[i];

        if (0 <= root && root <= 1)
        {
            // We're within the Bezier curve
            // Find point on Bezier
            auto p = cubic_bezier(p0, p1, p2, p3, root);

            // See if point is on line segment
            // Had to make special cases for vertical and horizontal lines due
            // to slight errors in calculation of p00
            if (a0.x == a1.x)
            {
                if (min.y <= p.y && p.y <= max.y)
                    *result++ = p;
            }
            else if (a0.y == a1.y)
            {
                if (min.x <= p.x && p.x <= max.x)
                    *result++ = p;
            }
            else if (p.x >= min.x && p.y >= min.y && p.x <= max.x && p.y <= max.y)
            {
                *result++ = p;
            }
        }
    }

    return static_cast<int>(result - results);
}

inline rectf cubic_bezier_bounding_rect(const pointf& p0, const pointf& p1, const pointf& p2, const pointf& p3)
{
    auto a = 3 * p3 - 9 * p2 + 9 * p1 - 3 * p0;
    auto b = 6 * p0 - 12 * p1 + 6 * p2;
    auto c = 3 * p1 - 3 * p0;
    auto delta_squared = b.cwise_product(b) - 4 * a.cwise_product(c);

    auto tl = p0.cwise_min(p3);
    auto rb = p0.cwise_max(p3);

    for (int i = 0; i < 2; ++i)
    {
        if (delta_squared[i] >= 0)
        {
            auto delta = sqrtf(delta_squared[i]);

            auto t0 = (-b[i] + delta) / (2 * a[i]);
            if (t0 > 0 && t0 < 1)
            {
                auto p = cubic_bezier(p0[i], p1[i], p2[i], p3[i], t0);
                tl[i] = std::min(tl[i], p);
                rb[i] = std::max(rb[i], p);
            }

            auto t1 = (-b[i] - delta) / (2 * a[i]);
            if (t1 > 0 && t1 < 1)
            {
                auto p = cubic_bezier(p0[i], p1[i], p2[i], p3[i], t1);
                tl[i] = std::min(tl[i], p);
                rb[i] = std::max(rb[i], p);
            }
        }
    }

    return rectf(tl, rb);
}

inline float cubic_bezier_length(const pointf& p0, const pointf& p1, const pointf& p2, const pointf& p3)
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
        const auto p = cubic_bezier_dt(p0, p1, p2, p3, t);
        const auto l = p.x * p.x + p.y * p.y;
        return sqrtf(l);
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

inline cubic_bezier_split_t cubic_bezier_split(const pointf& p0, const pointf& p1, const pointf& p2, const pointf& p3, float t)
{
    const auto z1 = t;
    const auto z2 = z1 * z1;
    const auto z3 = z1 * z1 * z1;
    const auto s1 = z1 - 1;
    const auto s2 = s1 * s1;
    const auto s3 = s1 * s1 * s1;

    return cubic_bezier_split_t
    {
        cubic_bezier_t
        {
                                                                 p0,
                                             z1      * p1 - s1 * p0,
                          z2      * p2 - 2 * z1 * s1 * p1 + s2 * p0,
            z3 * p3 - 3 * z2 * s1 * p2 + 3 * z1 * s2 * p1 - s3 * p0
        },
        cubic_bezier_t
        {
            z3 * p0 - 3 * z2 * s1 * p1 + 3 * z1 * s2 * p2 - s3 * p3,
                          z2      * p1 - 2 * z1 * s1 * p2 + s2 * p3,
                                             z1      * p2 - s1 * p3,
                                                                 p3,
        }
    };
};

struct bezier_fixed_step_result_t
{
    float  t;
    float  length;
    pointf point;
    bool   break_search;

    bezier_fixed_step_result_t(): t(0), length(0), point(), break_search(false) {}
};

typedef void(*bezier_fixed_step_callback_t)(bezier_fixed_step_result_t& result, void* user_pointer);

inline void cubic_bezier_fixed_step(bezier_fixed_step_callback_t callback, void* user_pointer, const pointf& p0, const pointf& p1, const pointf& p2, const pointf& p3, float step, bool overshoot = false, float max_value_error = 1e-3f, float max_t_error = 1e-5f)
{
    if (step <= 0.0f || !callback || max_value_error <= 0 || max_t_error <= 0)
        return;

    bezier_fixed_step_result_t result;
    result.point        = p0;

    callback(result, user_pointer);
    if (result.break_search)
        return;

    const auto length       = cubic_bezier_length(p0, p1, p2, p3);
    const auto point_count  = static_cast<int>(length / step) + (overshoot ? 2 : 1);
    const auto t_min        = 0.0f;
    const auto t_max        = step * point_count / length;
    const auto t_0          = (t_min + t_max) * 0.5f;

    std::map<float, float> cache;
    for (int point_index = 1; point_index < point_count; ++point_index)
    {
        const auto targetLength = point_index * step;

        float t_start = t_min;
        float t_end   = t_max;
        float t       = t_0;

        float t_best     = t;
        float error_best = length;

        while (true)
        {
            auto cacheIt = cache.find(t);
            if (cacheIt == cache.end())
            {
                const auto front    = cubic_bezier_split(p0, p1, p2, p3, t).left;
                const auto length   = cubic_bezier_length(front.p0, front.p1, front.p2, front.p3);

                cacheIt = cache.emplace(t, length).first;
            }

            const auto length   = cacheIt->second;
            const auto error    = targetLength - length;

            if (error < error_best)
            {
                error_best = error;
                t_best     = t;
            }

            if (fabsf(error) <= max_value_error || fabsf(t_start - t_end) <= max_t_error)
            {
                result.t      = t;
                result.length = length;
                result.point  = cubic_bezier(p0, p1, p2, p3, t);

                callback(result, user_pointer);
                if (result.break_search)
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

template <typename F>
inline void cubic_bezier_fixed_step(F& callback, const pointf& p0, const pointf& p1, const pointf& p2, const pointf& p3, float step, bool overshoot = false, float max_value_error = 1e-3f, float max_t_error = 1e-5f)
{
    auto wrapper = [](bezier_fixed_step_result_t& result, void* user_pointer)
    {
        F& callback = *reinterpret_cast<F*>(user_pointer);
        callback(result);
    };

    cubic_bezier_fixed_step(wrapper, &callback, p0, p1, p2, p3, step, overshoot, max_value_error, max_t_error);
}

inline void cubic_bezier_fixed_step(std::vector<pointf>& points, const pointf& p0, const pointf& p1, const pointf& p2, const pointf& p3, float step, bool overshoot = false, float max_value_error = 1e-3f, float max_t_error = 1e-5f)
{
    points.resize(0);

    auto callback = [](bezier_fixed_step_result_t& result, void* user_pointer)
    {
        auto& points = *reinterpret_cast<std::vector<pointf>*>(user_pointer);
        points.push_back(result.point);
    };

    cubic_bezier_fixed_step(callback, &points, p0, p1, p2, p3, step, overshoot, max_value_error, max_t_error);
}

inline std::vector<pointf> cubic_bezier_fixed_step(const pointf& p0, const pointf& p1, const pointf& p2, const pointf& p3, float step, bool overshoot = false, float max_value_error = 1e-3f, float max_t_error = 1e-5f)
{
    std::vector<pointf> result;
    cubic_bezier_fixed_step(result, p0, p1, p2, p3, step, overshoot, max_value_error, max_t_error);
    return result;
}

enum bezier_subdivide_flags_t
{
    bezier_subdivide_flags_none         = 0,
    bezier_subdivide_flags_skip_first   = 1
};

struct bezier_subdivide_result_t
{
    pointf point;
    pointf tangent;
};

typedef void(*bezier_subdivide_callback_t)(const bezier_subdivide_result_t& p, void* user_pointer);

namespace detail {
struct bezier_subdivide_context_impl_t: bezier_subdivide_result_t
{
    bezier_subdivide_callback_t callback;
    void*                       user_pointer;
    float                       tesselation_tollerance;
    bezier_subdivide_flags_t    flags;

    void commit(const pointf& p, const pointf& t)
    {
        point   = p;
        tangent = t;
        callback(*this, user_pointer);
    }
};

inline void cubic_bezier_subdivide_impl(bezier_subdivide_context_impl_t& context, const cubic_bezier_t& curve, int level = 0)
{
    float dx = curve.p3.x - curve.p0.x;
    float dy = curve.p3.y - curve.p0.y;
    float d2 = ((curve.p1.x - curve.p3.x) * dy - (curve.p1.y - curve.p3.y) * dx);
    float d3 = ((curve.p2.x - curve.p3.x) * dy - (curve.p2.y - curve.p3.y) * dx);
    d2 = (d2 >= 0) ? d2 : -d2;
    d3 = (d3 >= 0) ? d3 : -d3;
    if ((d2 + d3) * (d2 + d3) < context.tesselation_tollerance * (dx * dx + dy * dy))
    {
        context.commit(curve.p3, curve.tangent(1.0f));
    }
    else if (level < 10)
    {
        const auto p12 = (curve.p0 + curve.p1) * 0.5f;
        const auto p23 = (curve.p1 + curve.p2) * 0.5f;
        const auto p34 = (curve.p2 + curve.p3) * 0.5f;
        const auto p123 = (p12 + p23) * 0.5f;
        const auto p234 = (p23 + p34) * 0.5f;
        const auto p1234 = (p123 + p234) * 0.5f;

        cubic_bezier_subdivide_impl(context, cubic_bezier_t { curve.p0, p12, p123, p1234 }, level + 1);
        cubic_bezier_subdivide_impl(context, cubic_bezier_t { p1234, p234, p34, curve.p3 }, level + 1);
    }
}
} // namespace detail

inline void cubic_bezier_subdivide(bezier_subdivide_callback_t callback, void* user_pointer, const cubic_bezier_t& curve, float tess_tol = -1.0f, bezier_subdivide_flags_t flags = bezier_subdivide_flags_none)
{
    if (tess_tol < 0)
        tess_tol = 1.118f; // sqrtf(1.25f)

    detail::bezier_subdivide_context_impl_t context;
    context.callback               = callback;
    context.user_pointer           = user_pointer;
    context.tesselation_tollerance = tess_tol * tess_tol;
    context.flags                  = flags;

    if (!(context.flags & bezier_subdivide_flags_skip_first))
        context.commit(curve.p0, curve.tangent(0));

    detail::cubic_bezier_subdivide_impl(context, curve, 0);
}

template <typename F>
inline void cubic_bezier_subdivide(F& callback, const cubic_bezier_t& curve, float tess_tol = -1.0f, bezier_subdivide_flags_t flags = bezier_subdivide_flags_none)
{
    auto wrapper = [](const bezier_subdivide_result_t& r, void* user_pointer)
    {
        F& callback = *reinterpret_cast<F*>(user_pointer);
        callback(r);
    };

    cubic_bezier_subdivide(wrapper, &callback, curve, tess_tol, flags);
}

template <typename F>
inline void cubic_bezier_subdivide(F& callback, const pointf& p0, const pointf& p1, const pointf& p2, const pointf& p3, float tess_tol = -1.0f, bezier_subdivide_flags_t flags = bezier_subdivide_flags_none)
{
    cubic_bezier_subdivide(callback, cubic_bezier_t { p0, p1, p2, p3 }, tess_tol, flags);
}


//------------------------------------------------------------------------------
namespace easing {


//------------------------------------------------------------------------------
// http://gizma.com/easing/#quint2
//
// t - current time
// b - start value
// c - change in value

template <typename V, typename T>
inline V ease_out_quad(V b, V c, T t)
{
    return b - c * (t * (t - 2));
}


//------------------------------------------------------------------------------
} // namespace easing


//------------------------------------------------------------------------------
} // namespace ax


//------------------------------------------------------------------------------
# endif // defined(__cplusplus)


//------------------------------------------------------------------------------
# include "Math2D.inl"


//------------------------------------------------------------------------------
# endif // __AX_MATH_2D_H__
