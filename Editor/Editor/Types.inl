//------------------------------------------------------------------------------
// Low level stuff responsible for talking with OS/Platform API and
// discovering running configuration.
//------------------------------------------------------------------------------
# if !defined(__SPARK_CE_PROMO_TYPES_INL__)
# define __SPARK_CE_PROMO_TYPES_INL__


//------------------------------------------------------------------------------
# if defined(__cplusplus)


//------------------------------------------------------------------------------
# include <utility>


//------------------------------------------------------------------------------
inline void Matrix::Zero()
{
    *this = Matrix(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
}

inline void Matrix::Reset()
{
    *this = Matrix();
}

inline bool Matrix::Invert()
{
    const float det = (m11 * m22 - m21 * m12);
    if (det == 0.0f)
        return false;

    const float invDet = 1.0f / det;

    *this = Matrix(
         m22 * invDet,
        -m12 * invDet,
        -m21 * invDet,
         m11 * invDet,
        -(-m21 * m32 + m22 * m31) * invDet,
        -( m11 * m32 - m12 * m31) * invDet);

    return true;
}

inline void Matrix::Translate(float x, float y, MatrixOrder order/* = MatrixOrder_Prepend*/)
{
    Multiply(Matrix(1.0f, 0.0f, 0.0f, 1.0f, x, y), order);
}

inline void Matrix::Rotate(float angle, MatrixOrder order/* = MatrixOrder_Prepend*/)
{
    float angleRad = angle * SK_PI / 180.0f;

    const float c = cosf(angleRad);
    const float s = sinf(angleRad);

    Multiply(Matrix(c, s, -s, c, 0.0f, 0.0f), order);
}

inline void Matrix::RotateAt(float angle, float cx, float cy, MatrixOrder order/* = MatrixOrder_Prepend*/)
{
    const float angleRad = angle * SK_PI / 180.0f;

    const float c = cosf(angleRad);
    const float s = sinf(angleRad);

    Multiply(Matrix(c, s, -s, c,
        -cx * c - cy * -s + cx,
        -cx * s - cy *  c + cy), order);
}

inline void Matrix::Scale(float x, float y, MatrixOrder order/* = MatrixOrder_Prepend*/)
{
    Multiply(Matrix(x, 0.0f, 0.0f, y, 0.0f, 0.0f), order);

}

inline void Matrix::Shear(float x, float y, MatrixOrder order/* = MatrixOrder_Prepend*/)
{
    Multiply(Matrix(1.0f, y, x, 1.0f, 0.0f, 0.0f), order);
}

inline void Matrix::Multiply(const Matrix& matrix, MatrixOrder order/* = MatrixOrder_Prepend*/)
{
    if (order == MatrixOrder_Set)
    {
        if (this != &matrix)
            *this = matrix;
        return;
    }

    const Matrix* am = this;
    const Matrix* bm = &matrix;

    if (order == MatrixOrder_Append)
    {
        using std::swap;
        swap(am, bm);
    }

    *this = Matrix(
        am->m11 * bm->m11 + am->m21 * bm->m12,
        bm->m11 * am->m12 + bm->m12 * am->m22,
        bm->m21 * am->m11 + bm->m22 * am->m21,
        am->m12 * bm->m21 + am->m22 * bm->m22,
        bm->m31 * am->m11 + bm->m32 * am->m21 + am->m31,
        bm->m31 * am->m12 + bm->m32 * am->m22 + am->m32);
}

inline Matrix Matrix::Inverse() const
{
    Matrix inverted = *this;
    inverted.Invert();
    return inverted;
}


//------------------------------------------------------------------------------
inline void Matrix::TransformPoints(Point* points, int count/* = 1*/) const
{
    for (int i = 0; i < count; ++i, ++points)
    {
        float x = m11 * points->x + m21 * points->y + m31;
        float y = m12 * points->x + m22 * points->y + m32;

        points->x = (int)x;
        points->y = (int)y;
    }
}

inline void Matrix::TransformPoints(PointF* points, int count/* = 1*/) const
{
    for (int i = 0; i < count; ++i, ++points)
    {
        float x = m11 * points->x + m21 * points->y + m31;
        float y = m12 * points->x + m22 * points->y + m32;

        points->x = x;
        points->y = y;
    }
}

inline void Matrix::TransformVectors(Point* points, int count/* = 1*/) const
{
    for (int i = 0; i < count; ++i, ++points)
    {
        float x = m11 * points->x + m21 * points->y;
        float y = m12 * points->x + m22 * points->y;

        points->x = (int)x;
        points->y = (int)y;
    }
}

inline void Matrix::TransformVectors(PointF* points, int count/* = 1*/) const
{
    for (int i = 0; i < count; ++i, ++points)
    {
        float x = m11 * points->x + m21 * points->y;
        float y = m12 * points->x + m22 * points->y;

        points->x = x;
        points->y = y;
    }
}


//------------------------------------------------------------------------------
inline void Matrix4::Zero()
{
    *this = Matrix4(
        0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f);
}

inline void Matrix4::Reset()
{
    *this = Matrix4();
}

inline bool Matrix4::Invert()
{
# if 0
    // wild magic 4 inverse - http://www.geometrictools.com/Documentation/LaplaceExpansionTheorem.pdf
    // 84 multiplications
    // 66 adds/subs
    //  1 division

    // 24
    const float fA0 = m11 * m22 - m12 * m21;
    const float fA1 = m11 * m23 - m13 * m21;
    const float fA2 = m11 * m24 - m14 * m21;
    const float fA3 = m12 * m23 - m13 * m22;
    const float fA4 = m12 * m24 - m14 * m22;
    const float fA5 = m13 * m24 - m14 * m23;
    const float fB0 = m31 * m42 - m32 * m41;
    const float fB1 = m31 * m43 - m33 * m41;
    const float fB2 = m31 * m44 - m34 * m41;
    const float fB3 = m32 * m43 - m33 * m42;
    const float fB4 = m32 * m44 - m34 * m42;
    const float fB5 = m33 * m44 - m34 * m43;

    // 6
    const float det = fA0 * fB5 - fA1 * fB4 + fA2 * fB3 + fA3 * fB2 - fA4 * fB1 + fA5 * fB0;

    if (det == 0.0f)
    {
        Zero();
        return false;
    }

    const float invDet = 1.0f / det;

    // 36 + 16
    *this = Matrix4(
        ( m22 * fB5 - m23 * fB4 + m24 * fB3) * invDet,
        (-m12 * fB5 + m13 * fB4 - m14 * fB3) * invDet,
        ( m42 * fA5 - m43 * fA4 + m44 * fA3) * invDet,
        (-m32 * fA5 + m33 * fA4 - m34 * fA3) * invDet,
        (-m21 * fB5 + m23 * fB2 - m24 * fB1) * invDet,
        ( m11 * fB5 - m13 * fB2 + m14 * fB1) * invDet,
        (-m41 * fA5 + m43 * fA2 - m44 * fA1) * invDet,
        ( m31 * fA5 - m33 * fA2 + m34 * fA1) * invDet,
        ( m21 * fB4 - m22 * fB2 + m24 * fB0) * invDet,
        (-m11 * fB4 + m12 * fB2 - m14 * fB0) * invDet,
        ( m41 * fA4 - m42 * fA2 + m44 * fA0) * invDet,
        (-m31 * fA4 + m32 * fA2 - m34 * fA0) * invDet,
        (-m21 * fB3 + m22 * fB1 - m23 * fB0) * invDet,
        ( m11 * fB3 - m12 * fB1 + m13 * fB0) * invDet,
        (-m41 * fA3 + m42 * fA1 - m43 * fA0) * invDet,
        ( m31 * fA3 - m32 * fA1 + m33 * fA0) * invDet);

    return true;
# else
    // 214 multiplications
    //  80 adds/subs
    //   1 division
    const float d00 = m22 * m33 * m44 + m32 * m43 * m24 + m42 * m23 * m34 - m24 * m33 * m42 - m34 * m43 * m22 - m44 * m23 * m32;
    const float d01 = m12 * m33 * m44 + m32 * m43 * m14 + m42 * m13 * m34 - m14 * m33 * m42 - m34 * m43 * m12 - m44 * m13 * m32;
    const float d02 = m12 * m23 * m44 + m22 * m43 * m14 + m42 * m13 * m24 - m14 * m23 * m42 - m24 * m43 * m12 - m44 * m13 * m22;
    const float d03 = m12 * m23 * m34 + m22 * m33 * m14 + m32 * m13 * m24 - m14 * m23 * m32 - m24 * m33 * m12 - m34 * m13 * m22;

    const float d10 = m21 * m33 * m44 + m31 * m43 * m24 + m41 * m23 * m34 - m24 * m33 * m41 - m34 * m43 * m21 - m44 * m23 * m31;
    const float d11 = m11 * m33 * m44 + m31 * m43 * m14 + m41 * m13 * m34 - m14 * m33 * m41 - m34 * m43 * m11 - m44 * m13 * m31;
    const float d12 = m11 * m23 * m44 + m21 * m43 * m14 + m41 * m13 * m24 - m14 * m23 * m41 - m24 * m43 * m11 - m44 * m13 * m21;
    const float d13 = m11 * m23 * m34 + m21 * m33 * m14 + m31 * m13 * m24 - m14 * m23 * m31 - m24 * m33 * m11 - m34 * m13 * m21;

    const float d20 = m21 * m32 * m44 + m31 * m42 * m24 + m41 * m22 * m34 - m24 * m32 * m41 - m34 * m42 * m21 - m44 * m22 * m31;
    const float d21 = m11 * m32 * m44 + m31 * m42 * m14 + m41 * m12 * m34 - m14 * m32 * m41 - m34 * m42 * m11 - m44 * m12 * m31;
    const float d22 = m11 * m22 * m44 + m21 * m42 * m14 + m41 * m12 * m24 - m14 * m22 * m41 - m24 * m42 * m11 - m44 * m12 * m21;
    const float d23 = m11 * m22 * m34 + m21 * m32 * m14 + m31 * m12 * m24 - m14 * m22 * m31 - m24 * m32 * m11 - m34 * m12 * m21;

    const float d30 = m21 * m32 * m43 + m31 * m42 * m23 + m41 * m22 * m33 - m23 * m32 * m41 - m33 * m42 * m21 - m43 * m22 * m31;
    const float d31 = m11 * m32 * m43 + m31 * m42 * m13 + m41 * m12 * m33 - m13 * m32 * m41 - m33 * m42 * m11 - m43 * m12 * m31;
    const float d32 = m11 * m22 * m43 + m21 * m42 * m13 + m41 * m12 * m23 - m13 * m22 * m41 - m23 * m42 * m11 - m43 * m12 * m21;
    const float d33 = m11 * m22 * m33 + m21 * m32 * m13 + m31 * m12 * m23 - m13 * m22 * m31 - m23 * m32 * m11 - m33 * m12 * m21;

    const float det = m11 * d00 - m21 * d01 + m31 * d02 - m41 * d03;

    if (det == 0.0f)
    {
        Zero();
        return false;
    }

    const float invDet = 1.0f / det;

    m11 =  d00 * invDet; m21 = -d10 * invDet;  m31 =  d20 * invDet; m41 = -d30 * invDet;
    m12 = -d01 * invDet; m22 =  d11 * invDet;  m32 = -d21 * invDet; m42 =  d31 * invDet;
    m13 =  d02 * invDet; m23 = -d12 * invDet;  m33 =  d22 * invDet; m43 = -d32 * invDet;
    m14 = -d03 * invDet; m24 =  d13 * invDet;  m34 = -d23 * invDet; m44 =  d33 * invDet;

    return true;
# endif
}

inline void Matrix4::Transpose()
{
    using std::swap;

    swap(m12, m21);
    swap(m13, m31);
    swap(m14, m41);
    swap(m23, m32);
    swap(m24, m42);
    swap(m34, m43);
}

inline void Matrix4::Translate(float x, float y, float z, MatrixOrder order/* = MatrixOrder_Prepend*/)
{
    Multiply(Matrix4(
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
           x,    y,    z, 1.0f), order);
}

inline void Matrix4::RotateX(float angle, MatrixOrder order/* = MatrixOrder_Prepend*/)
{
    float angleRad = angle * SK_PI / 180.0f;

    const float c = cosf(angleRad);
    const float s = sinf(angleRad);

    Multiply(Matrix4(
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f,    c,   -s, 0.0f,
        0.0f,    s,    c, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f), order);
}

inline void Matrix4::RotateY(float angle, MatrixOrder order/* = MatrixOrder_Prepend*/)
{
    float angleRad = angle * SK_PI / 180.0f;

    const float c = cosf(angleRad);
    const float s = sinf(angleRad);

    Multiply(Matrix4(
           c, 0.0f,   -s, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
           s, 0.0f,    c, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f), order);
}

inline void Matrix4::RotateZ(float angle, MatrixOrder order/* = MatrixOrder_Prepend*/)
{
    float angleRad = angle * SK_PI / 180.0f;

    const float c = cosf(angleRad);
    const float s = sinf(angleRad);

    Multiply(Matrix4(
           c,   -s, 0.0f, 0.0f,
           s,    c, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f), order);
}

inline void Matrix4::RotateAxis(float angle, float x, float y, float z, MatrixOrder order/* = MatrixOrder_Prepend*/)
{
    const float angleRad = angle * SK_PI / 180.0f;

    const float c = cosf(angleRad);
    const float s = sinf(angleRad);

    const float rc = 1.0f - c;

    Multiply(Matrix4(
        (rc * x * x) +       c, (rc * x * y) + (z * s), (rc * x * z) - (y * s), 0.0f,
        (rc * x * y) - (z * s), (rc * y * y) +       c, (rc * z * y) + (x * s), 0.0f,
        (rc * x * z) + (y * s), (rc * y * z) - (x * s), (rc * z * z) +       c, 0.0f,
                          0.0f,                   0.0f,                   0.0f, 1.0f), order);
}

inline void Matrix4::Scale(float x, float y, float z, MatrixOrder order/* = MatrixOrder_Prepend*/)
{
    Multiply(Matrix4(
           x, 0.0f, 0.0f, 0.0f,
        0.0f,    y, 0.0f, 0.0f,
        0.0f, 0.0f,    z, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f), order);
}

inline void Matrix4::Multiply(const Matrix4& matrix, MatrixOrder order/* = MatrixOrder_Prepend*/)
{
    if (order == MatrixOrder_Set)
    {
        if (this != &matrix)
            *this = matrix;
        return;
    }

    const Matrix4* am = this;
    const Matrix4* bm = &matrix;

    if (order == MatrixOrder_Append)
    {
        using std::swap;
        swap(am, bm);
    }

    *this = Matrix4(
        am->m11 * bm->m11 + am->m21 * bm->m12 + am->m31 * bm->m13 + am->m41 * bm->m14,
        am->m12 * bm->m11 + am->m22 * bm->m12 + am->m32 * bm->m13 + am->m42 * bm->m14,
        am->m13 * bm->m11 + am->m23 * bm->m12 + am->m33 * bm->m13 + am->m43 * bm->m14,
        am->m14 * bm->m11 + am->m24 * bm->m12 + am->m34 * bm->m13 + am->m44 * bm->m14,

        am->m11 * bm->m21 + am->m21 * bm->m22 + am->m31 * bm->m23 + am->m41 * bm->m24,
        am->m12 * bm->m21 + am->m22 * bm->m22 + am->m32 * bm->m23 + am->m42 * bm->m24,
        am->m13 * bm->m21 + am->m23 * bm->m22 + am->m33 * bm->m23 + am->m43 * bm->m24,
        am->m14 * bm->m21 + am->m24 * bm->m22 + am->m34 * bm->m23 + am->m44 * bm->m24,

        am->m11 * bm->m31 + am->m21 * bm->m32 + am->m31 * bm->m33 + am->m41 * bm->m34,
        am->m12 * bm->m31 + am->m22 * bm->m32 + am->m32 * bm->m33 + am->m42 * bm->m34,
        am->m13 * bm->m31 + am->m23 * bm->m32 + am->m33 * bm->m33 + am->m43 * bm->m34,
        am->m14 * bm->m31 + am->m24 * bm->m32 + am->m34 * bm->m33 + am->m44 * bm->m34,

        am->m11 * bm->m41 + am->m21 * bm->m42 + am->m31 * bm->m43 + am->m41 * bm->m44,
        am->m12 * bm->m41 + am->m22 * bm->m42 + am->m32 * bm->m43 + am->m42 * bm->m44,
        am->m13 * bm->m41 + am->m23 * bm->m42 + am->m33 * bm->m43 + am->m43 * bm->m44,
        am->m14 * bm->m41 + am->m24 * bm->m42 + am->m34 * bm->m43 + am->m44 * bm->m44);
}

inline Matrix4 Matrix4::Inverse() const
{
    Matrix4 inverted = *this;
    inverted.Invert();
    return inverted;
}

inline Matrix4 Matrix4::Transposed() const
{
    Matrix4 transposed = *this;
    transposed.Transpose();
    return transposed;
}

inline void Matrix4::TransformPoints(Point* points, int count/* = 1*/) const
{
    Point* p = points;
    for (int i = 0; i < count; ++i, ++p)
    {
        float x = m11 * p->x + m21 * p->y + m41;
        float y = m12 * p->x + m22 * p->y + m42;
        //float w = m14 * p->x + m24 * p->y + m44;

        //if (w != 0.0f && w != 1.0f)
        //{
        //    x /= w;
        //    y /= w;
        //}

        p->x = (int)x;
        p->y = (int)y;
    }
}

inline void Matrix4::TransformPoints(PointF* points, int count/* = 1*/) const
{
    PointF* p = points;
    for (int i = 0; i < count; ++i, ++p)
    {
        float x = m11 * p->x + m21 * p->y + m41;
        float y = m12 * p->x + m22 * p->y + m42;
        //float w = m14 * p->x + m24 * p->y + m44;

        //if (w != 0.0f && w != 1.0f)
        //{
        //    x /= w;
        //    y /= w;
        //}

        p->x = x;
        p->y = y;
    }
}

inline void Matrix4::TransformPoints(Point4F* points, int count/* = 1*/) const
{
    Point4F* p = points;
    for (int i = 0; i < count; ++i, ++p)
    {
        const float x = m11 * p->x + m21 * p->y + m31 * p->z + m41 * p->w;
        const float y = m12 * p->x + m22 * p->y + m32 * p->z + m42 * p->w;
        const float z = m13 * p->x + m23 * p->y + m33 * p->z + m43 * p->w;
        const float w = m14 * p->x + m24 * p->y + m34 * p->z + m44 * p->w;

        p->x = x;
        p->y = y;
        p->z = z;
        p->w = w;
    }
}

inline void Matrix4::TransformVectors(Point* points, int count/* = 1*/) const
{
    Point* p = points;
    for (int i = 0; i < count; ++i, ++p)
    {
        float x = m11 * p->x + m21 * p->y;
        float y = m12 * p->x + m22 * p->y;
        //float w = m14 * p->x + m24 * p->y;

        //if (w != 0.0f && w != 1.0f)
        //{
        //    x /= w;
        //    y /= w;
        //}

        p->x = (int)x;
        p->y = (int)y;
    }
}

inline void Matrix4::TransformVectors(PointF* points, int count/* = 1*/) const
{
    PointF* p = points;
    for (int i = 0; i < count; ++i, ++p)
    {
        float x = m11 * p->x + m21 * p->y;
        float y = m12 * p->x + m22 * p->y;
        //float w = m14 * p->x + m24 * p->y;

        //if (w != 0.0f && w != 1.0f)
        //{
        //    x /= w;
        //    y /= w;
        //}

        p->x = x;
        p->y = y;
    }
}


//------------------------------------------------------------------------------
# endif // defined(__cplusplus)


//------------------------------------------------------------------------------
# endif // __SPARK_CE_PROMO_TYPES_INL__