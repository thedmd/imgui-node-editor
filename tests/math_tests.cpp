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

// Distance from the point to the curve: the closest of the samples, refined by
// ternary search between its neighbors.
double ReferenceDistance(const ImCubicBezierPoints& curve, const std::vector<Point2>& samples, const ImVec2& point)
{
    const int last = static_cast<int>(samples.size()) - 1;
    int closest = 0;
    for (int i = 1; i <= last; ++i)
        if (Distance(samples[i], point) < Distance(samples[closest], point))
            closest = i;

    double t0 = static_cast<double>(ImMax(closest - 1, 0)) / last;
    double t1 = static_cast<double>(ImMin(closest + 1, last)) / last;
    for (int i = 0; i < 60; ++i)
    {
        const double m0 = t0 + (t1 - t0) / 3, m1 = t1 - (t1 - t0) / 3;
        if (Distance(Cubic(curve, m0), point) < Distance(Cubic(curve, m1), point))
            t1 = m1;
        else
            t0 = m0;
    }
    return Distance(Cubic(curve, (t0 + t1) / 2), point);
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

TEST_CASE(ProjectionNearEnd, "math.projection_near_a_curve_end", "MATH-4", test::TestFlags_None)
{
    // A link from (0, 0) to (600, 0) with the default strength; the closest point lies
    // between the first two samples.
    const auto result = ImProjectOnCubicBezier(ImVec2(3.15f, 5.50f), ImVec2(0, 0), ImVec2(100, 0), ImVec2(500, 0), ImVec2(600, 0), 50);
    test::Check(ImFabs(result.Distance - 5.50f) < 0.01f && ImFabs(result.Point.x - 3.15f) < 0.01f,
        "(3.15, 5.50) projects to (3.15, 0) at 5.5: (%g, %g) at %g", result.Point.x, result.Point.y, result.Distance);
}

TEST_CASE(HoverNearLinkStart, "math.link_is_hovered_near_its_start", "MATH-4", test::TestFlags_None)
{
    // Link 100 runs 600 to the right of its start; the mouse is (3.15, 5.40) from it,
    // inside the box that precedes the distance check.
    test::Storage storage;
    test::Session session(&storage);
    ImVec2 start(100.0f, 100.0f);
    session.DefaultContent = [&start](int frame)
    {
        const ImVec2 pivots[2] = { start, start + ImVec2(600.0f, 0.0f) };
        for (int n = 0; n < 2; ++n)
        {
            if (frame == 0)
                ed::SetNodePosition(n + 1, ImVec2(n * 600.0f, 400.0f));
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

    const auto mouse = session.Use([] { return ImFloor(ed::CanvasToScreen(ImVec2(200.0f, 200.0f))); });
    start = session.Use([&mouse] { return ed::ScreenToCanvas(mouse); }) - ImVec2(3.15f, 5.40f);
    test::MouseMove(mouse);
    session.Run(2);

    const bool hovered = session.Use([] { return ed::GetHoveredLink() == ed::LinkId(100); });
    test::Check(hovered, "link 100 is hovered 5.4 from it");
}

TEST_CASE(ProjectionSweep, "math.projection_finds_the_closest_point", "MATH-4", test::TestFlags_None)
{
    // Points around links shaped as the editor shapes them and around general curves,
    // against the editor's hover band: link thickness 1 and 5 to spare.
    const double band = 6.0;
    Random random;
    int points = 0, misses = 0;
    double worst = 0.0;
    for (int i = 0; i < 400; ++i)
    {
        ImCubicBezierPoints curve;
        if (i % 2 == 0)
        {
            const auto a = random.Point(0, 1000);
            const auto b = random.Point(0, 600);
            const auto start = ImVec2(std::floor(a.x), std::floor(a.y));
            const auto end   = ImVec2(std::floor(b.x), std::floor(b.y));
            const auto half  = ImLength(end - start) * 0.5f;
            const auto strength = half < 100.0f ? 100.0f * ImSin(IM_PI * 0.5f * half / 100.0f) : 100.0f;
            curve = ImCubicBezierPoints{ start, start + ImVec2(strength, 0), end - ImVec2(strength, 0), end };
        }
        else
            curve = ImCubicBezierPoints{ random.Point(0, 600), random.Point(0, 600), random.Point(0, 600), random.Point(0, 600) };

        std::vector<Point2> samples(2001);
        for (int k = 0; k <= 2000; ++k)
            samples[k] = Cubic(curve, k / 2000.0);

        for (int k = 0; k < 100; ++k)
        {
            const auto on     = Cubic(curve, random.Next());
            const auto offset = random.Point(-8, 8);
            const auto point  = ImVec2(static_cast<float>(on.x) + offset.x, static_cast<float>(on.y) + offset.y);
            const auto reference = ReferenceDistance(curve, samples, point);
            const auto result    = ImProjectOnCubicBezier(point, curve, 50);
            if (reference <= band - 0.01 && result.Distance > band)
                ++misses;
            worst = ImMax(worst, std::fabs(result.Distance - reference));
            ++points;
        }
    }

    test::Check(misses == 0 && worst < 0.1, "points within the band are found: %d of %d missed, distances off by up to %g", misses, points, worst);
}

TEST_CASE(ClosestLineRadii, "math.closest_line_endpoints_do_not_cross", "MATH-5", test::TestFlags_None)
{
    // Pivots up to 40 apart with radii that fit between them or don't.
    const float radii[] = { 0.0f, 1.0f, 5.0f, 15.0f, 30.0f };
    int crossed = 0, cases = 0;
    for (int i = 1; i <= 160; ++i)
    {
        const float length = i * 0.25f;
        for (auto radiusA : radii)
            for (auto radiusB : radii)
            {
                const auto line = ImRect_ClosestLine(ImRect(0, 0, 0, 0), ImRect(length, 0, length, 0), radiusA, radiusB);
                const auto expectedA = radiusA + radiusB > length ? length * radiusA / (radiusA + radiusB) : radiusA;
                if (line.B.x < line.A.x - 1e-4f || ImFabs(line.A.x - expectedA) > 1e-4f)
                    ++crossed;
                ++cases;
            }
    }

    test::Check(crossed == 0, "the endpoints stay apart, or meet where the radii overlap: %d of %d cases differ", crossed, cases);
}

TEST_CASE(StraightLinkArrow, "math.straight_link_arrow_does_not_flip", "MATH-5", test::TestFlags_None)
{
    // Pins 16 apart, with radius 5 and radius 5 plus a 10 long arrow, as the blueprints
    // example's tree nodes have; a straight link between them.
    const ImU32 linkColor = IM_COL32(255, 0, 0, 255);
    test::Storage storage;
    test::Session session(&storage);
    session.DefaultContent = [&](int frame)
    {
        const ImVec2 pivots[2] = { ImVec2(100.0f, 100.0f), ImVec2(116.0f, 100.0f) };
        ed::PushStyleVar(ed::StyleVar_LinkStrength, 0.0f);
        ed::PushStyleVar(ed::StyleVar_PinRadius, 5.0f);
        for (int n = 0; n < 2; ++n)
        {
            if (frame == 0)
                ed::SetNodePosition(n + 1, ImVec2(n * 300.0f, 300.0f));
            ed::BeginNode(n + 1);
            if (n == 1)
            {
                ed::PushStyleVar(ed::StyleVar_PinArrowSize, 10.0f);
                ed::PushStyleVar(ed::StyleVar_PinArrowWidth, 10.0f);
            }
            ed::BeginPin(11 + n, n == 0 ? ed::PinKind::Output : ed::PinKind::Input);
            ImGui::TextUnformatted("Pin");
            ed::PinPivotRect(pivots[n], pivots[n]);
            ed::EndPin();
            if (n == 1)
                ed::PopStyleVar(2);
            ed::EndNode();
        }
        ed::PopStyleVar(2);
        ed::Link(100, 11, 12, ImColor(linkColor));
    };
    session.Run(3);

    const auto left  = session.Use([] { return ed::CanvasToScreen(ImVec2(100.0f, 100.0f)); });
    const auto right = session.Use([] { return ed::CanvasToScreen(ImVec2(116.0f, 100.0f)); });
    const auto geometry = test::Capture(session.DrawList);
    int vertices = 0, outside = 0;
    float minX = FLT_MAX;
    for (size_t i = 0; i < geometry.Colors.size(); ++i)
    {
        if (geometry.Colors[i] != linkColor)
            continue;
        ++vertices;
        minX = ImMin(minX, geometry.Positions[i].x);
        if (geometry.Positions[i].x < left.x - 1.0f || geometry.Positions[i].x > right.x + 1.0f)
            ++outside;
    }

    test::Check(vertices > 0 && outside == 0, "the link and its arrow stay between the pins: %d of %d vertices outside, the leftmost at %+g from the left pin", outside, vertices, minX - left.x);
}

TEST_CASE(SplitHalves, "math.split_halves_follow_the_curve", "MATH-6", test::TestFlags_None)
{
    // The left half at u is the curve at t u, the right half at u the curve at t + (1 - t) u.
    Random random;
    int leftOff = 0, rightOff = 0;
    for (int i = 0; i < 1000; ++i)
    {
        const ImCubicBezierPoints curve{ random.Point(-400, 400), random.Point(-400, 400), random.Point(-400, 400), random.Point(-400, 400) };
        const auto t = random.Range(0.05f, 0.95f);
        const auto split = ImCubicBezierSplit(curve, t);
        for (int k = 0; k <= 4; ++k)
        {
            const double u = k / 4.0;
            const auto left  = Cubic(split.Left, u);
            const auto right = Cubic(split.Right, u);
            const auto expectedLeft  = Cubic(curve, t * u);
            const auto expectedRight = Cubic(curve, t + (1 - t) * u);
            if (std::hypot(left.x - expectedLeft.x, left.y - expectedLeft.y) > 0.01)
                ++leftOff;
            if (std::hypot(right.x - expectedRight.x, right.y - expectedRight.y) > 0.01)
                ++rightOff;
        }
    }

    test::Check(leftOff == 0 && rightOff == 0, "both halves follow the curve: %d of 5000 left and %d of 5000 right samples are off", leftOff, rightOff);
}
