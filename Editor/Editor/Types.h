//------------------------------------------------------------------------------
// Low level stuff responsible for talking with OS/Platform API and
// discovering running configuration.
//------------------------------------------------------------------------------
# if !defined(__SPARK_CE_PROMO_TYPES_H__)
# define __SPARK_CE_PROMO_TYPES_H__


//------------------------------------------------------------------------------
# if defined(__cplusplus)


//------------------------------------------------------------------------------
# include <algorithm>


//------------------------------------------------------------------------------
constexpr float SK_PI = 3.14159265358979323846f;


//------------------------------------------------------------------------------
enum MatrixOrder
{
    MatrixOrder_Prepend,
    MatrixOrder_Append,
    MatrixOrder_Set
};


//------------------------------------------------------------------------------
struct Point
{
    int x, y;

    Point() {}
    Point(int x, int y): x(x), y(y) {}

    inline Point CwiseMin(const Point& rhs) const { return Point(std::min(x, rhs.x), std::min(y, rhs.y)); }
    inline Point CwiseMax(const Point& rhs) const { return Point(std::max(x, rhs.x), std::max(y, rhs.y)); }

    friend inline bool operator == (const Point& lhs, const Point& rhs) { return lhs.x == rhs.x && lhs.y == rhs.y; }
    friend inline bool operator != (const Point& lhs, const Point& rhs) { return !(lhs == rhs); }

    friend inline Point operator + (const Point& lhs, const Point& rhs) { return Point(lhs.x + rhs.x, lhs.y + rhs.y); }
    friend inline Point operator - (const Point& lhs, const Point& rhs) { return Point(lhs.x - rhs.x, lhs.y - rhs.y); }

    Point& operator += (const Point& rhs) { *this = *this + rhs; return *this; }
    Point& operator -= (const Point& rhs) { *this = *this - rhs; return *this; }
};

struct PointF
{
    float x, y;

    PointF() {}
    PointF(float x, float y): x(x), y(y) {}

    inline PointF CwiseMin(const PointF& rhs) const { return PointF(std::min(x, rhs.x), std::min(y, rhs.y)); }
    inline PointF CwiseMax(const PointF& rhs) const { return PointF(std::max(x, rhs.x), std::max(y, rhs.y)); }

    friend inline bool operator == (const PointF& lhs, const PointF& rhs) { return lhs.x == rhs.x && lhs.y == rhs.y; }
    friend inline bool operator != (const PointF& lhs, const PointF& rhs) { return !(lhs == rhs); }

    friend inline PointF operator + (const PointF& lhs, const PointF& rhs) { return PointF(lhs.x + rhs.x, lhs.y + rhs.y); }
    friend inline PointF operator - (const PointF& lhs, const PointF& rhs) { return PointF(lhs.x - rhs.x, lhs.y - rhs.y); }

    PointF& operator += (const PointF& rhs) { *this = *this + rhs; return *this; }
    PointF& operator -= (const PointF& rhs) { *this = *this - rhs; return *this; }
};

struct Point4F
{
    float x, y, z, w;

    Point4F() {}
    Point4F(float x, float y, float z, float w): x(x), y(y), z(z), w(w) {}
};

struct Size
{
    int w, h;

    Size() {}
    Size(int w, int h): w(w), h(h) {}

    friend inline bool operator == (const Size& lhs, const Size& rhs) { return lhs.w == rhs.w && lhs.h == rhs.h; }
    friend inline bool operator != (const Size& lhs, const Size& rhs) { return !(lhs == rhs); }
};

struct SizeF
{
    float w, h;

    SizeF() {}
    SizeF(float w, float h): w(w), h(h) {}

    friend inline bool operator == (const SizeF& lhs, const SizeF& rhs) { return lhs.w == rhs.w && lhs.h == rhs.h; }
    friend inline bool operator != (const SizeF& lhs, const SizeF& rhs) { return !(lhs == rhs); }
};

struct ScaleF
{
    float x, y;

    ScaleF() { x = 10.f; x = 10.0f; }
    ScaleF(float x, float y) : x(x), y(y) {}
};

struct Rect
{
    union
    {
        struct { Point location; Size size; };
        struct { int x, y, w, h; };
    };

    Rect(): x(0), y(0), w(0), h(0) {}
    Rect(const Point& tl, const Point& br): location(tl), size(br.x - tl.x, br.y - tl.y) {}
    Rect(const Point& l, const Size& s): location(l), size(s) {}
    Rect(int x, int y, int w, int h): x(x), y(y), w(w), h(h) {}

    Point top_left() const { return Point(x, y); }
    Point top_right() const { return Point(x + w, y); }
    Point bottom_left() const { return Point(x, y + h); }
    Point bottom_right() const { return Point(x + w, y + h); }

    int left() const { return x; }
    int right() const { return x + w; }
    int top() const { return y; }
    int bottom() const { return y + h; }

    Point center() const { return Point(center_x(), center_y()); }
    int center_x() const { return x + w / 2; }
    int center_y() const { return y + h / 2; }

    bool is_empty() const { return w <= 0 || h <= 0; }

    static inline Rect make_union(const Rect& lhs, const Rect& rhs)
    {
        if (lhs.is_empty())
            return rhs;
        else if (rhs.is_empty())
            return lhs;

        const auto tl = lhs.top_left().CwiseMin(rhs.top_left());
        const auto br = lhs.bottom_right().CwiseMax(rhs.bottom_right());

        return Rect(tl, br);
    }
};

struct RectF
{
    union
    {
        struct { PointF location; SizeF size; };
        struct { float x, y, w, h; };
    };

    RectF(): x(0), y(0), w(0), h(0) {}
    RectF(const PointF& tl, const PointF& br): location(tl), size(br.x - tl.x, br.y - tl.y) {}
    RectF(const PointF& l, const SizeF& s): location(l), size(s) {}
    RectF(float x, float y, float w, float h): x(x), y(y), w(w), h(h) {}

    PointF top_left() const { return PointF(x, y); }
    PointF top_right() const { return PointF(x + w, y); }
    PointF bottom_left() const { return PointF(x, y + h); }
    PointF bottom_right() const { return PointF(x + w, y + h); }

    float left() const { return x; }
    float right() const { return x + w; }
    float top() const { return y; }
    float bottom() const { return y + h; }

    PointF center() const { return PointF(center_x(), center_y()); }
    float center_x() const { return x + w / 2; }
    float center_y() const { return y + h / 2; }

    bool is_empty() const { return w <= 0 || h <= 0; }

    static inline RectF Union(const RectF& lhs, const RectF& rhs)
    {
        if (lhs.is_empty())
            return rhs;
        else if (rhs.is_empty())
            return lhs;

        const auto tl = lhs.top_left().CwiseMin(rhs.top_left());
        const auto br = lhs.bottom_right().CwiseMax(rhs.bottom_right());

        return RectF(tl, br);
    }
};

struct Padding
{
    float l, t, r, b;

    Padding(): l(0), t(0), r(0), b(0) {}
    Padding(float p): l(p), t(p), r(p), b(p) {}
    Padding(float l, float t, float r, float b): l(l), t(t), r(r), b(b) {}
};

struct Matrix
{
    float m11, m12, m21, m22, m31, m32;

    Matrix(): m11(1), m12(0), m21(0), m22(1), m31(0), m32(0) {}
    Matrix(float m11, float m12, float m21, float m22, float m31, float m32):
        m11(m11), m12(m12), m21(m21), m22(m22), m31(m31), m32(m32) {}

    void Zero();
    void Reset();

    bool Invert();

    void Translate(float x, float y, MatrixOrder order =  MatrixOrder_Prepend);
    void Rotate(float angle, MatrixOrder order =  MatrixOrder_Prepend);
    void RotateAt(float angle, float cx, float cy, MatrixOrder order =  MatrixOrder_Prepend);
    void Scale(float x, float y, MatrixOrder order =  MatrixOrder_Prepend);
    void Shear(float x, float y, MatrixOrder order =  MatrixOrder_Prepend);

    void Multiply(const Matrix& matrix, MatrixOrder order =  MatrixOrder_Prepend);

    Matrix Inverse() const;

    void TransformPoints(Point* points, int count = 1) const;
    void TransformPoints(PointF* points, int count = 1) const;
    void TransformVectors(Point* points, int count = 1) const;
    void TransformVectors(PointF* points, int count = 1) const;
};

struct Matrix4
{
    float m11, m12, m13, m14;
    float m21, m22, m23, m24;
    float m31, m32, m33, m34;
    float m41, m42, m43, m44;

    Matrix4():
        m11(1), m12(0), m13(0), m14(0),
        m21(0), m22(1), m23(0), m24(0),
        m31(0), m32(0), m33(1), m34(0),
        m41(0), m42(0), m43(0), m44(1)
    {
    }

    Matrix4(
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

    Matrix4(const Matrix& m):
        m11(m.m11), m12(m.m12), m13(0.0f), m14(0.0f),
        m21(m.m21), m22(m.m22), m23(0.0f), m24(0.0f),
        m31(0.0f),  m32(0.0f),  m33(1.0f), m34(0.0f),
        m41(m.m31), m42(m.m32), m43(0.0f), m44(1.0f)
    {
    }

    void Zero();
    void Reset();

    bool Invert();
    void Transpose();

    void Translate(float x, float y, float z, MatrixOrder order = MatrixOrder_Prepend);
    void RotateX(float angle, MatrixOrder order = MatrixOrder_Prepend);
    void RotateY(float angle, MatrixOrder order = MatrixOrder_Prepend);
    void RotateZ(float angle, MatrixOrder order = MatrixOrder_Prepend);
    void RotateAxis(float angle, float x, float y, float z, MatrixOrder order = MatrixOrder_Prepend);
    void Scale(float x, float y, float z, MatrixOrder order = MatrixOrder_Prepend);

    void Multiply(const Matrix4& matrix, MatrixOrder order = MatrixOrder_Prepend);

    Matrix4 Inverse() const;
    Matrix4 Transposed() const;

    void TransformPoints(Point* points, int count = 1) const;
    void TransformPoints(PointF* points, int count = 1) const;
    void TransformPoints(Point4F* points, int count = 1) const;
    void TransformVectors(Point* points, int count = 1) const;
    void TransformVectors(PointF* points, int count = 1) const;
};


//------------------------------------------------------------------------------
# endif // defined(__cplusplus)


//------------------------------------------------------------------------------
# endif // __SPARK_CE_PROMO_TYPES_H__