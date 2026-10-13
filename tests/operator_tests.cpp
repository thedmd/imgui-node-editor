//------------------------------------------------------------------------------
// The editor's headers next to ImVec2 operators an application defines itself.
//------------------------------------------------------------------------------
# include "harness.h"

# if IMGUI_VERSION_NUM < 19268
// Dear ImGui defines it from 1.92.7 on.
inline ImVec2 operator*(const float lhs, const ImVec2& rhs)
{
    return ImVec2(lhs * rhs.x, lhs * rhs.y);
}
# endif

# include "imgui_bezier_math.h"
# include "imgui_canvas.h"

TEST_CASE(ApplicationOperators, "build.application_defines_float_times_vector", "BLD-2", test::TestFlags_None)
{
    // Before 1.92.7 the editor defined this operator too, so this didn't compile.
    const auto scaled = 2.0f * ImVec2(1.0f, 2.0f);
    const auto middle = ImCubicBezier(ImVec2(0.0f, 0.0f), ImVec2(1.0f, 0.0f), ImVec2(2.0f, 0.0f), ImVec2(3.0f, 0.0f), 0.5f);
    test::Check(scaled.x == 2.0f && scaled.y == 4.0f && middle.x == 1.5f && middle.y == 0.0f,
        "the application's operator and the editor's math both work: (%g, %g), (%g, %g)", scaled.x, scaled.y, middle.x, middle.y);
}
