//------------------------------------------------------------------------------
// Bezier and rectangle math, checked against references computed in double.
//------------------------------------------------------------------------------
# include "harness.h"
# include "imgui_bezier_math.h"
# include <cmath>
# include <cstdint>
# include <vector>

namespace {

// Pseudo-random numbers, the same on every platform.
struct Random
{
    uint64_t State = 0x9E3779B97F4A7C15ull;

    double Next() // in [0, 1)
    {
        State = State * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<double>(State >> 11) * (1.0 / 9007199254740992.0);
    }

    float Range(float min, float max)
    {
        return min + static_cast<float>(Next()) * (max - min);
    }

    ImVec2 Point(float min, float max)
    {
        const float x = Range(min, max);
        return ImVec2(x, Range(min, max));
    }
};

struct Point2
{
    double x, y;
};

Point2 Cubic(const ImCubicBezierPoints& curve, double t)
{
    const double u = 1 - t;
    const double w0 = u * u * u, w1 = 3 * u * u * t, w2 = 3 * u * t * t, w3 = t * t * t;
    return Point2{ w0 * curve.P0.x + w1 * curve.P1.x + w2 * curve.P2.x + w3 * curve.P3.x,
                   w0 * curve.P0.y + w1 * curve.P1.y + w2 * curve.P2.y + w3 * curve.P3.y };
}

double Distance(const Point2& a, const ImVec2& b)
{
    return std::hypot(a.x - b.x, a.y - b.y);
}

// Where the curve crosses the segment a0-a1, found by bisecting sign changes of the
// distance to its line. Crossings near either end of the segment or of the curve,
// or nearly tangent, are left out: float rounding may decide those either way.
std::vector<Point2> ReferenceIntersections(const ImCubicBezierPoints& curve, const ImVec2& a0, const ImVec2& a1)
{
    const double a = static_cast<double>(a1.y) - a0.y;
    const double b = static_cast<double>(a0.x) - a1.x;
    const double c = -(a * a0.x + b * a0.y);
    auto f = [&](double t) { const auto p = Cubic(curve, t); return a * p.x + b * p.y + c; };
    const double norm = std::hypot(a, b);

    std::vector<Point2> result;
    const int samples = 2000;
    for (int i = 0; i < samples; ++i)
    {
        double t0 = static_cast<double>(i) / samples, t1 = static_cast<double>(i + 1) / samples;
        double f0 = f(t0), f1 = f(t1);
        if ((f0 < 0) == (f1 < 0))
            continue;

        for (int k = 0; k < 60; ++k)
        {
            const double tm = (t0 + t1) / 2, fm = f(tm);
            if ((fm < 0) == (f0 < 0)) { t0 = tm; f0 = fm; } else { t1 = tm; f1 = fm; }
        }

        const double t = (t0 + t1) / 2;
        const double delta = 1e-2;
        if (t < delta || t > 1 - delta || std::fabs(f(t - delta)) < 0.5 * norm || std::fabs(f(t + delta)) < 0.5 * norm)
            continue;

        const auto p = Cubic(curve, t);
        const double s = ((p.x - a0.x) * (a1.x - a0.x) + (p.y - a0.y) * (a1.y - a0.y)) / (norm * norm);
        if (s > 0.01 && s < 0.99)
            result.push_back(p);
    }
    return result;
}

// Whether any of 1001 points along the curve lies more than 0.01 outside its bounds.
bool ReachesOutsideBounds(const ImCubicBezierPoints& curve)
{
    const auto bounds = ImCubicBezierBoundingRect(curve);
    for (int k = 0; k <= 1000; ++k)
    {
        const auto p = Cubic(curve, k / 1000.0);
        if (p.x < bounds.Min.x - 0.01 || p.x > bounds.Max.x + 0.01 || p.y < bounds.Min.y - 0.01 || p.y > bounds.Max.y + 0.01)
            return true;
    }
    return false;
}

} // namespace

TEST_CASE(LineIntersectionFlatCubic, "math.line_intersection_with_a_vanishing_cubic_term", "MATH-2", test::TestFlags_None)
{
    // A link from (0, 100) to (300, 150) with the default strength: x is linear in t.
    const ImCubicBezierPoints curve{ ImVec2(0, 100), ImVec2(100, 100), ImVec2(200, 150), ImVec2(300, 150) };
    const auto result = ImCubicBezierLineIntersect(curve, ImLine{ ImVec2(150, 50), ImVec2(150, 200) });

    test::Check(result.Count == 1 && ImFabs(result.Points[0].x - 150.0f) < 0.01f && ImFabs(result.Points[0].y - 125.0f) < 0.01f,
        "the link crosses x = 150 at (150, 125): %d intersections, the first at (%g, %g)", result.Count, result.Count ? result.Points[0].x : 0.0f, result.Count ? result.Points[0].y : 0.0f);
}

TEST_CASE(RectangleSelectsFlatLink, "math.rectangle_selects_a_link_with_a_vanishing_cubic_term", "CORE-15", test::TestFlags_None)
{
    // Link 100 runs from (0, 100) to (300, 150). An Alt-drag rectangle from (150, 50)
    // to (400, 200) crosses it on its left edge only.
    test::Storage storage;
    test::Session session(&storage);
    session.DefaultContent = [](int frame)
    {
        const ImVec2 pivots[2] = { ImVec2(0.0f, 100.0f), ImVec2(300.0f, 150.0f) };
        for (int n = 0; n < 2; ++n)
        {
            if (frame == 0)
                ed::SetNodePosition(n + 1, n == 0 ? ImVec2(-150.0f, 80.0f) : ImVec2(320.0f, 130.0f));
            ed::BeginNode(n + 1);
            ed::BeginPin(11 + n, n == 0 ? ed::PinKind::Output : ed::PinKind::Input);
            ImGui::TextUnformatted("Pin");
            ed::PinPivotRect(pivots[n], pivots[n]);
            ed::EndPin();
            ed::EndNode();
        }
        ed::Link(100, 11, 12);
    };
    session.Run(3);
    session.Use([] { ed::NavigateToContent(0.0f); });
    session.Run(3);

    const auto from = session.Use([] { return ed::CanvasToScreen(ImVec2(150.0f, 50.0f)); });
    const auto to   = session.Use([] { return ed::CanvasToScreen(ImVec2(400.0f, 200.0f)); });
    ImGui::GetIO().AddKeyEvent(ImGuiMod_Alt, true);
    session.Drag(from, to);
    ImGui::GetIO().AddKeyEvent(ImGuiMod_Alt, false);
    session.Run(2);

    const bool selected = session.Use([] { return ed::IsLinkSelected(100); });
    test::Check(selected, "the rectangle selects link 100");
}

TEST_CASE(LineIntersectionSweep, "math.line_intersection_finds_every_crossing", "MATH-2", test::TestFlags_None)
{
    // General curves and segments, and curves with evenly spaced control points in x
    // or y against vertical or horizontal segments, whose cubic term vanishes.
    Random random;
    int crossings = 0, misses = 0;
    for (int i = 0; i < 3000; ++i)
    {
        ImCubicBezierPoints curve{ random.Point(0, 400), random.Point(0, 400), random.Point(0, 400), random.Point(0, 400) };
        ImVec2 a0 = random.Point(0, 400), a1 = random.Point(0, 400);
        if (i % 3 != 0)
        {
            const int axis = i % 3 == 1 ? 0 : 1;
            const float start = random.Range(0, 100), step = random.Range(20, 100);
            float* coordinates[] = { &curve.P0.x, &curve.P1.x, &curve.P2.x, &curve.P3.x };
            for (int k = 0; k < 4; ++k)
                coordinates[k][axis] = start + step * k;
            const float across = random.Range(start, start + 3 * step);
            (&a0.x)[axis] = across;
            (&a1.x)[axis] = across;
            (&a0.x)[1 - axis] = -100.0f;
            (&a1.x)[1 - axis] = 500.0f;
        }

        const auto result = ImCubicBezierLineIntersect(curve, ImLine{ a0, a1 });
        for (const auto& reference : ReferenceIntersections(curve, a0, a1))
        {
            ++crossings;
            bool found = false;
            for (int k = 0; k < result.Count; ++k)
                found = found || Distance(reference, result.Points[k]) < 0.1;
            if (!found)
                ++misses;
        }
    }

    test::Check(crossings > 1000 && misses == 0, "every crossing is found: %d missed of %d", misses, crossings);
}

TEST_CASE(BoundsWithLinearDerivative, "math.bounds_where_the_derivative_is_linear", "MATH-3", test::TestFlags_None)
{
    // Pins at (0, 0) and (0, 200), both facing (1, 0), strength 100: x peaks at 75.
    const auto bounds = ImCubicBezierBoundingRect(ImVec2(0, 0), ImVec2(100, 0), ImVec2(100, 200), ImVec2(0, 200));
    test::Check(ImFabs(bounds.Max.x - 75.0f) < 0.01f, "the bounds reach x = 75: (%g, %g) - (%g, %g)", bounds.Min.x, bounds.Min.y, bounds.Max.x, bounds.Max.y);
}

TEST_CASE(BoundsSweep, "math.bounds_contain_the_curve", "MATH-3", test::TestFlags_None)
{
    // Links between pins on whole units, aligned on an axis and both facing the same
    // way along it, which makes the derivative linear in that axis; and general curves.
    Random random;
    auto wholePoint = [&random] { const auto p = random.Point(-200, 200); return ImVec2(std::floor(p.x), std::floor(p.y)); };

    int alignedOutside = 0, generalOutside = 0;
    for (int i = 0; i < 4000; ++i)
    {
        const int axis = i % 2;
        const auto start = wholePoint();
        auto end = wholePoint();
        (&end.x)[axis] = (&start.x)[axis];
        ImVec2 direction(0, 0);
        (&direction.x)[axis] = random.Next() < 0.5 ? -1.0f : 1.0f;
        const auto strength = std::floor(random.Range(10, 200));
        if (ReachesOutsideBounds(ImCubicBezierPoints{ start, start + direction * strength, end + direction * strength, end }))
            ++alignedOutside;

        if (ReachesOutsideBounds(ImCubicBezierPoints{ wholePoint(), wholePoint(), wholePoint(), wholePoint() }))
            ++generalOutside;
    }

    test::Check(alignedOutside == 0 && generalOutside == 0, "every curve lies within its bounds: %d of 4000 aligned links and %d of 4000 general curves reach outside", alignedOutside, generalOutside);
}

TEST_CASE(BoundsNearlyLinear, "math.bounds_where_the_derivative_is_nearly_linear", "MATH-3", test::TestFlags_None)
{
    // The quadratic coefficient of the derivative in x is 1e-6 to 1e-3 of the linear
    // one, where the textbook quadratic formula cancels.
    Random random;
    int outside = 0;
    for (int i = 0; i < 4000; ++i)
    {
        const double p0 = random.Range(-200, 200), p1 = random.Range(-200, 200), p2 = random.Range(-200, 200);
        const double b = 6 * p0 - 12 * p1 + 6 * p2;
        const double magnitude = std::pow(10.0, -6.0 + 3.0 * random.Next());
        const double ratio = random.Next() < 0.5 ? -magnitude : magnitude;
        const double p3 = (ratio * std::fabs(b) + 9 * p2 - 9 * p1 + 3 * p0) / 3;
        const ImCubicBezierPoints curve{ ImVec2(static_cast<float>(p0), 0), ImVec2(static_cast<float>(p1), 100), ImVec2(static_cast<float>(p2), 200), ImVec2(static_cast<float>(p3), 300) };
        if (ReachesOutsideBounds(curve))
            ++outside;
    }

    test::Check(outside == 0, "every curve lies within its bounds: %d of 4000 reach outside", outside);
}
