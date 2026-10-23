//------------------------------------------------------------------------------
// blueprints2-example's samples, as the example ships them: each loads, compiles,
// and runs, played where it's a game.
//------------------------------------------------------------------------------
# include "harness.h"
# include "blueprint_editor_document.h"
# include "blueprint_screen.h"
# include "crude_blueprint.h"
# include <crude_json.h>
# include <algorithm>
# include <chrono>
# include <memory>
# include <string>
# include <string_view>

using namespace crude_blueprint;
using blueprint_editor::ExampleRegistry;
using blueprint_screen::ScreenHost;
using blueprint_screen::Shape;

namespace {

// The sample's blueprint, of the example's registry; null, failing the test, for one that doesn't load.
std::unique_ptr<Blueprint> LoadSample(const char* name)
{
    auto path   = std::string(BLUEPRINTS2_EXAMPLE_DIR) + "/data/" + name;
    auto loaded = crude_json::value::load(path);
    if (!loaded.second || !loaded.first.contains("document"))
    {
        test::Check(false, "%s loads as a document", path.c_str());
        return nullptr;
    }
    auto blueprint = std::make_unique<Blueprint>(ExampleRegistry(), std::make_shared<TypeTable>());
    if (auto read = Load(*blueprint, loaded.first["document"]["blueprint"]); !read)
    {
        test::Check(false, "%s's blueprint loads: %s", name, std::string(read.GetError().Message()).c_str());
        return nullptr;
    }
    for (auto& graph : blueprint->Graphs())
        for (auto& diagnostic : graph->Diagnostics())
            test::Check(false, "%s's %s has no problem: %s", name, graph->GetName().c_str(), std::string(diagnostic.m_Error.Message()).c_str());
    return blueprint;
}

bool Shows(const blueprint_screen::Screen& screen, std::string_view text)
{
    return std::any_of(screen.m_Shapes.begin(), screen.m_Shapes.end(), [text](const Shape& shape) { return shape.m_Kind == Shape::Kind::Text && shape.m_Text == text; });
}

} // namespace

TEST_CASE(SnakePlays, "samples.snake_is_played_to_a_score_and_its_end", nullptr, test::TestFlags_None)
{
    auto blueprint = LoadSample("Snake.bp");
    if (!blueprint)
        return;
    auto compiled = Compile(*blueprint);
    if (!compiled.m_Program || !compiled.m_Diagnostics.empty())
        return test::Check(false, "Snake compiles, without a warning: %s", compiled.m_Diagnostics.empty() ? "" : std::string(compiled.m_Diagnostics[0].m_Error.Message()).c_str());

    ScreenHost host;
    Context    context(compiled.m_Program);
    context.AddService(host);
    context.Seed(1);
    auto run = [&context](double seconds)
    {
        context.Advance(seconds);
        context.Run(std::chrono::seconds(5));
    };
    auto read = [&context](const char* name) { auto value = context.GetVariable(name); return value ? value.GetValue() : Value(); };
    auto main = context.Start("Start");
    run(0.0);

    auto screens = host.Screens();
    auto titled  = screens.size() == 1 && screens[0]->m_Title == "Snake" && screens[0]->m_Width == 480 && screens[0]->m_Height == 320 && Shows(*screens[0], "SNAKE") && Shows(*screens[0], "Best: 0");
    auto waits   = !context.Fibers().empty() && context.Fibers()[0].m_Wake == Wake::OnSignal(blueprint_screen::KeySignal("Space"));
    test::Check(main && titled && waits, "Start opens a screen of 480 x 320 and shows the title until Space");
    if (!main || !titled)
        return;
    auto& screen = *screens[0];

    // Space starts a round. Each move, a key turns the head toward the food, along x first, but never
    // back, until the score is 3; then the snake runs into a wall.
    context.Signal(blueprint_screen::KeySignal("Space"));
    run(0.0);
    auto keyType = blueprint->GetRegistry().FindValueType("example.Key");
    auto press   = [&](const char* name)
    {
        const Value key[] = { Value::Enum(keyType->AsType(), static_cast<const EnumType*>(keyType)->FindCase(name)) };
        auto fiber = context.Start("Key Pressed", key);
        context.Run(std::chrono::seconds(5));
        context.Forget(fiber.ValueOr(0));
    };
    auto coordinates = [](const Value& point) { auto fields = point.AsStruct(); return std::pair<int, int>(fields[0].AsInt32(), fields[1].AsInt32()); };
    auto score = 0, longest = 0;
    auto ended = false, over = false;
    for (int tick = 0; tick < 4000 && !ended; ++tick)
    {
        auto alive = read("Alive");
        auto body  = read("Body");
        if (alive.IsValid() && alive.AsBool() && body.IsValid() && !body.AsArray().empty() && read("Score").AsInt32() < 3)
        {
            auto [hx, hy] = coordinates(body.AsArray().back());
            auto [fx, fy] = coordinates(read("Food"));
            auto [dx, dy] = coordinates(read("Heading"));
            const char* want = fx > hx ? "Right" : fx < hx ? "Left" : fy > hy ? "Down" : fy < hy ? "Up" : nullptr;
            const auto back = (want == std::string_view("Right") && dx < 0) || (want == std::string_view("Left") && dx > 0) || (want == std::string_view("Down") && dy < 0) || (want == std::string_view("Up") && dy > 0);
            if (want && back)
                want = dx != 0 ? (fy >= hy ? "Down" : "Up") : (fx >= hx ? "Right" : "Left");
            if (want)
                press(want);
        }
        run(0.02);
        score   = std::max(score, read("Score").AsInt32());
        longest = std::max(longest, body.IsValid() ? static_cast<int>(body.AsArray().size()) : 0);
        over    = over || Shows(screen, "GAME OVER");
        ended   = over && !context.Fibers().empty() && context.Fibers()[0].m_State == FiberState::Suspended && context.Fibers()[0].m_Wake.m_Kind == Wake::Kind::Signal;
    }
    test::Check(score >= 3 && longest >= 6, "the snake eats, and grows: a score of %d, %d cells at most", score, longest);
    test::Check(over && ended && read("Best") == Value::Int32(score) && Shows(screen, "Best: " + std::to_string(score)) && context.Diagnostics().empty(),
        "a wall ends the round: the score shows, is the best, and the title waits for Space again: %s", context.Diagnostics().empty() ? "" : std::string(context.Diagnostics()[0].m_Error.Message()).c_str());
}
