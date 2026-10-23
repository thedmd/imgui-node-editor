//------------------------------------------------------------------------------
// blueprints2-example's screens: the nodes that open and draw on them, and the
// windows that show them.
//------------------------------------------------------------------------------
# include "blueprints2_fixture.h"
# include "blueprint_editor_document.h"
# include "blueprint_editor_utilities.h"
# include "blueprint_screen.h"
# include "crude_logger.h"
# include <crude_json.h>
# include <algorithm>
# include <chrono>
# include <initializer_list>
# include <memory>
# include <string>

using namespace blueprints2_tests;
using blueprint_editor::Document;
using blueprint_editor::ExampleRegistry;
using blueprint_screen::Align;
using blueprint_screen::ScreenHost;
using blueprint_screen::Shape;

namespace {

ed::Config UnnamedConfig()
{
    ed::Config config;
    config.SettingsName = nullptr;
    return config;
}

// The example's logs go to the overlay logger, which needs an ImGui context.
struct OverlayLog
{
    OverlayLog()  { crude_logger::OverlayLogger::SetCurrent(&m_Logger); }
    ~OverlayLog() { crude_logger::OverlayLogger::SetCurrent(nullptr); }

    crude_logger::OverlayLogger m_Logger;
};

struct Fixture : BlueprintFixture
{
    Fixture(): BlueprintFixture(ExampleRegistry(), "") {}

    Value Vector(float x, float y) const
    {
        return Value::Struct(m_Blueprint.GetRegistry().FindValueType("example.Vector2")->AsType(), { Value::Float(x), Value::Float(y) });
    }

    Value Color(float r, float g, float b, float a = 1.0f) const
    {
        return Value::Struct(m_Blueprint.GetRegistry().FindValueType("example.Color")->AsType(), { Value::Float(r), Value::Float(g), Value::Float(b), Value::Float(a) });
    }

    void Set(Id node, std::string_view input, Value value)
    {
        if (auto set = m_Graph.SetValue(Pin(node, input), std::move(value)); !set)
            test::Check(false, "%s.%s takes the value: %s", Name(node).c_str(), std::string(input).c_str(), Message(set).c_str());
    }

    // Start, then the nodes in their order, along their flow pins.
    void Chain(std::initializer_list<Id> nodes)
    {
        Id previous = 0;
        for (auto node : nodes)
        {
            if (previous)
                Link(previous, "then", node, "enter");
            previous = node;
        }
    }

    // Runs Start's fiber with the host, if any, to its end.
    FiberState Run(ScreenHost* host, std::string* error = nullptr)
    {
        auto compiled = Compile(m_Blueprint);
        if (!compiled.m_Program)
        {
            test::Check(false, "the blueprint compiles: %s", compiled.m_Diagnostics.empty() ? "" : std::string(compiled.m_Diagnostics[0].m_Error.Message()).c_str());
            return FiberState::Failed;
        }
        Context context(compiled.m_Program);
        if (host)
            context.AddService(*host);
        auto fiber = context.Start("Start");
        context.Run(std::chrono::seconds(5));
        if (error && !context.Diagnostics().empty())
            *error = std::string(context.Diagnostics().back().m_Error.Message());
        return fiber && !context.Fibers().empty() ? context.Fibers()[0].m_State : FiberState::Failed;
    }
};

bool Same(const ImVec2& a, const ImVec2& b) { return a.x == b.x && a.y == b.y; }
bool Same(const ImVec4& a, const ImVec4& b) { return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w; }

} // namespace

TEST_CASE(ScreenDrawing, "screen.open_screen_opens_a_screen_which_its_nodes_draw_on", nullptr, test::TestFlags_None)
{
    // A screen of 100 x 50, cleared to red: a green rect, a white circle, and "Hi" to the right.
    Fixture f;
    auto start = f.Add("crude.Start"), open = f.Add("example.OpenScreen"), clear = f.Add("example.ClearScreen");
    auto rect  = f.Add("example.FillRect"), circle = f.Add("example.FillCircle"), text = f.Add("example.DrawText");
    f.Chain({ start, open, clear, rect, circle, text });
    for (auto node : { clear, rect, circle, text })
        f.Link(open, "screen", node, "screen");
    f.Set(open, "title", Value::String("Test"));
    f.Set(open, "width", Value::Int32(100));
    f.Set(open, "height", Value::Int32(50));
    f.Set(clear, "color", f.Color(1.0f, 0.0f, 0.0f));
    f.Set(rect, "position", f.Vector(10.0f, 5.0f));
    f.Set(rect, "size", f.Vector(20.0f, 10.0f));
    f.Set(rect, "color", f.Color(0.0f, 1.0f, 0.0f));
    f.Set(circle, "center", f.Vector(50.0f, 25.0f));
    f.Set(circle, "radius", Value::Float(8.0f));
    f.Set(text, "position", f.Vector(90.0f, 40.0f));
    f.Set(text, "text", Value::String("Hi"));
    f.Set(text, "size", Value::Float(12.0f));
    f.Set(text, "align", Value::Enum(f.m_Blueprint.GetRegistry().FindValueType("example.Align")->AsType(), 2));

    ScreenHost host;
    auto end = f.Run(&host);
    if (end != FiberState::Done || host.Screens().size() != 1)
        return test::Check(false, "the run ends with a screen open: %d screens", static_cast<int>(host.Screens().size()));
    auto& screen = *host.Screens()[0];
    auto& shapes = screen.m_Shapes;
    test::Check(screen.m_Title == "Test" && screen.m_Width == 100 && screen.m_Height == 50 && Same(screen.m_Background, ImVec4(1.0f, 0.0f, 0.0f, 1.0f)), "it's of the title and the size, cleared to red");
    test::Check(shapes.size() == 3 && shapes[0].m_Kind == Shape::Kind::Rect && Same(shapes[0].m_At, ImVec2(10.0f, 5.0f)) && Same(shapes[0].m_Size, ImVec2(20.0f, 10.0f)) && Same(shapes[0].m_Color, ImVec4(0.0f, 1.0f, 0.0f, 1.0f)),
        "the rect is drawn on it: %d shapes", static_cast<int>(shapes.size()));
    test::Check(shapes.size() == 3 && shapes[1].m_Kind == Shape::Kind::Circle && Same(shapes[1].m_At, ImVec2(50.0f, 25.0f)) && shapes[1].m_Size.x == 8.0f && Same(shapes[1].m_Color, ImVec4(1.0f, 1.0f, 1.0f, 1.0f)),
        "then the circle, white by default");
    test::Check(shapes.size() == 3 && shapes[2].m_Kind == Shape::Kind::Text && shapes[2].m_Text == "Hi" && shapes[2].m_Size.y == 12.0f && shapes[2].m_Align == Align::Right, "then the text");
    test::Check(screen.m_Shown.empty(), "and none of it shows before it's presented");

    host.Present();
    test::Check(screen.m_Shown.size() == 3 && Same(screen.m_ShownBackground, ImVec4(1.0f, 0.0f, 0.0f, 1.0f)), "once presented, it shows what was drawn");
}

TEST_CASE(ScreenReopened, "screen.a_screen_of_a_title_open_already_opens_again_cleared", nullptr, test::TestFlags_None)
{
    // A, a rect on it, then A again, and B.
    Fixture f;
    auto start = f.Add("crude.Start"), first = f.Add("example.OpenScreen"), rect = f.Add("example.FillRect"), again = f.Add("example.OpenScreen"), other = f.Add("example.OpenScreen");
    f.Chain({ start, first, rect, again, other });
    f.Link(first, "screen", rect, "screen");
    f.Set(first, "title", Value::String("A"));
    f.Set(again, "title", Value::String("A"));
    f.Set(again, "width", Value::Int32(64));
    f.Set(other, "title", Value::String("B"));

    ScreenHost host;
    auto end = f.Run(&host);
    auto screens = host.Screens();
    test::Check(end == FiberState::Done && screens.size() == 2 && screens[0]->m_Title == "A" && screens[0]->m_Width == 64 && screens[0]->m_Shapes.empty() && screens[1]->m_Title == "B",
        "A opens again, of its new size and cleared, and B is a screen of its own: %d screens", static_cast<int>(screens.size()));
}

TEST_CASE(ScreenRefusals, "screen.drawing_needs_a_host_an_open_screen_and_a_size_that_fits", nullptr, test::TestFlags_None)
{
    {
        Fixture f;
        auto start = f.Add("crude.Start"), open = f.Add("example.OpenScreen");
        f.Chain({ start, open });
        std::string error;
        auto end = f.Run(nullptr, &error);
        test::Check(end == FiberState::Failed && error == "no host shows screens here", "Open Screen without a host fails: %s", error.c_str());
    }
    {
        Fixture f;
        auto start = f.Add("crude.Start"), open = f.Add("example.OpenScreen");
        f.Chain({ start, open });
        f.Set(open, "width", Value::Int32(0));
        ScreenHost  host;
        std::string error;
        auto end = f.Run(&host, &error);
        test::Check(end == FiberState::Failed && error == "a screen is from 1 to 4096 wide and high, not 0 x 320" && host.Screens().empty(), "and so does a screen of no width: %s", error.c_str());
    }
    {
        // A variable's screen, none by default.
        Fixture f;
        auto type  = f.m_Blueprint.GetRegistry().FindValueType("example.Screen");
        auto added = f.m_Blueprint.AddVariable({ "Screen", type->Default(type->AsType()) });
        auto start = f.Add("crude.Start"), get = f.Add("get:Screen"), rect = f.Add("example.FillRect");
        f.Chain({ start, rect });
        f.Link(get, "value", rect, "screen");
        ScreenHost  host;
        std::string error;
        auto end = f.Run(&host, &error);
        test::Check(added && end == FiberState::Failed && error == "the screen isn't open: Open Screen opens one", "a node drawing on no screen fails: %s", error.c_str());
    }
    {
        Fixture f;
        auto start = f.Add("crude.Start"), rect = f.Add("example.FillRect");
        f.Chain({ start, rect });
        auto compiled = Compile(f.m_Blueprint);
        test::Check(!compiled.m_Program && !compiled.m_Diagnostics.empty() && compiled.m_Diagnostics[0].m_Error.Is(BlueprintError::NeedsLink), "and one without a link to its screen doesn't run");
    }
}

TEST_CASE(ScreenFiles, "screen.a_file_holds_a_screen_as_none", nullptr, test::TestFlags_None)
{
    Fixture f;
    auto type    = f.m_Blueprint.GetRegistry().FindValueType("example.Screen");
    auto none    = type->Default(type->AsType());
    auto saved   = crude_json::value(type->Save(none)).dump();
    auto loaded  = type->Load(type->AsType(), crude_json::value());
    auto refused = type->Load(type->AsType(), crude_json::value(1.0));
    test::Check(saved == "null" && loaded && loaded.GetValue() == none && type->Format(none) == "no screen" && !refused, "none is saved as null, which loads as none, and nothing else loads: %s", saved.c_str());
}

TEST_CASE(AlignSwitches, "screen.switch_on_enum_takes_align", nullptr, test::TestFlags_None)
{
    // Start, then Switch on Enum of Center, an output for each case; each sets Taken to its name.
    Fixture f;
    auto align = f.m_Blueprint.GetRegistry().FindValueType("example.Align");
    auto made  = f.m_Blueprint.AddVariable({ "Taken", Value::String("") }).IsOk();
    NodeConfig config;
    config.m_Groups["case"] = 3;
    config.m_Types["E"]     = &align->AsType();
    auto start = f.Add("crude.Start"), on = f.Add("crude.SwitchOnEnum", std::move(config));
    f.Link(start, "then", on, "enter");
    f.Set(on, "selection", Value::Enum(align->AsType(), static_cast<const EnumType*>(align)->FindCase("Center")));
    for (int i = 0; i < 3; ++i)
    {
        auto set = f.Add("set:Taken");
        f.Link(on, "case." + std::to_string(i), set, "enter");
        f.Set(set, "value", Value::String(f.m_Graph.FindNode(on)->FindPin("case." + std::to_string(i))->m_Name));
    }
    auto compiled = Compile(f.m_Blueprint);
    if (!compiled.m_Program)
        return test::Check(false, "the blueprint compiles");
    Context context(compiled.m_Program);
    auto started = context.Start("Start");
    context.Run(std::chrono::seconds(5));
    auto taken = context.GetVariable("Taken");
    test::Check(made && started && taken && taken.GetValue() == Value::String("Center"), "the example's registry has Switch on Enum, and it takes Align: %s", taken ? std::string(taken.GetValue().AsString()).c_str() : "");
}

TEST_CASE(ScreenWindows, "screen.an_open_screens_window_shows_its_picture", nullptr, test::TestFlags_None)
{
    test::Session session(UnnamedConfig());
    ScreenHost    host;
    auto screen = host.Open("Test", 100, 50);
    screen->m_Shapes.push_back({ Shape::Kind::Rect, ImVec2(0.0f, 0.0f), ImVec2(50.0f, 50.0f), ImVec4(0.0f, 1.0f, 0.0f, 1.0f), {}, Align::Left });
    host.Present();
    auto vertices = 0;
    session.AfterHost = [&]
    {
        host.ShowWindows(false);
        auto window = ImGui::FindWindowByName("Test###screen:Test");
        vertices = window && window->Active ? window->DrawList->VtxBuffer.Size : 0;
    };
    session.Run(3);
    auto shown = vertices;
    screen->m_Open = false;
    session.Run(2);
    auto window = ImGui::FindWindowByName("Test###screen:Test");
    test::Check(shown > 0 && window && !window->Active, "an open screen shows in a window, %d vertices, which goes once it's closed", shown);
}

TEST_CASE(KeysWaited, "screen.wait_for_key_goes_on_once_its_key_is_pressed", nullptr, test::TestFlags_None)
{
    // Start waits for Space, then ends.
    Fixture f;
    auto start = f.Add("crude.Start"), wait = f.Add("example.WaitForKey");
    f.Chain({ start, wait });
    auto compiled = Compile(f.m_Blueprint);
    if (!compiled.m_Program)
        return test::Check(false, "the blueprint compiles");
    Context context(compiled.m_Program);
    auto fiber = context.Start("Start");
    context.Run(std::chrono::seconds(5));
    auto waits = context.Fibers()[0].m_Wake;
    context.Signal(blueprint_screen::KeySignal("Left"));
    context.Run(std::chrono::seconds(5));
    auto still = context.Fibers()[0].m_State;
    context.Signal(blueprint_screen::KeySignal("Space"));
    context.Run(std::chrono::seconds(5));
    test::Check(fiber && waits == Wake::OnSignal("key:Space") && still == FiberState::Suspended && context.Fibers()[0].m_State == FiberState::Done, "it waits for Space's signal, which Left's doesn't end, and goes on once it comes");
}

TEST_CASE(KeysPressed, "screen.a_key_pressed_starts_its_event_with_the_key", nullptr, test::TestFlags_None)
{
    // Start waits for a signal that never comes, and Key Pressed keeps its key in Last.
    test::Session session(UnnamedConfig());
    OverlayLog    log;
    Document      document{ ExampleRegistry(), std::make_shared<TypeTable>() };
    auto& graph = document.GetGraph();
    auto  key   = document.GetBlueprint().GetRegistry().FindValueType("example.Key");
    auto  pin   = [&graph](Id node, std::string_view name) { auto found = graph.FindNode(node); auto pin = found ? found->FindPin(name) : nullptr; return pin ? pin->m_Id : 0; };
    auto  made  = document.GetBlueprint().AddVariable({ "Last", key->Default(key->AsType()) }).IsOk();
    auto  start = graph.AddNode("crude.Start").ValueOr(0), wait = graph.AddNode("crude.WaitSignal").ValueOr(0);
    auto  event = graph.AddNode("example.KeyPressed").ValueOr(0), set = graph.AddNode("set:Last").ValueOr(0);
    made = made && graph.Connect(pin(start, "then"), pin(wait, "enter")).IsOk() && graph.SetValue(pin(wait, "signal"), Value::String("never")).IsOk();
    made = made && graph.Connect(pin(event, "then"), pin(set, "enter")).IsOk() && graph.Connect(pin(event, "key"), pin(set, "value")).IsOk();

    blueprint_editor_utilities::Debugger debugger;
    debugger.Start(document, true);
    debugger.Update(document);
    const Value up[] = { Value::Enum(key->AsType(), static_cast<const EnumType*>(key)->FindCase("Up")) };
    auto fired = debugger.Fire("Key Pressed", up);
    debugger.Update(document);
    auto last = debugger.GetContext()->GetVariable("Last");
    test::Check(made && fired && last && last.GetValue() == up[0], "Key Pressed starts with the key, Up: %s", last ? key->Format(last.GetValue()).c_str() : "");
}

TEST_CASE(KeySwitches, "screen.switch_on_enum_goes_on_from_the_case_of_the_key_pressed", nullptr, test::TestFlags_None)
{
    // Key Pressed, then Switch on Enum of its key, for Up and Down, which set Taken to their names.
    Fixture f;
    auto key   = f.m_Blueprint.GetRegistry().FindValueType("example.Key");
    auto made  = f.m_Blueprint.AddVariable({ "Taken", Value::String("") }).IsOk();
    NodeConfig config;
    config.m_Groups["case"] = 2;
    config.m_Data["case.0"] = Value::String("Up");
    config.m_Data["case.1"] = Value::String("Down");
    auto event = f.Add("example.KeyPressed"), on = f.Add("crude.SwitchOnEnum", std::move(config));
    f.Link(event, "then", on, "enter");
    f.Link(event, "key", on, "selection");
    for (auto name : { "Up", "Down" })
    {
        auto set = f.Add("set:Taken");
        f.Link(on, std::string(name) == "Up" ? "case.0" : "case.1", set, "enter");
        f.Set(set, "value", Value::String(name));
    }
    auto most = f.m_Graph.FindNode(on)->m_Groups[0].m_Max;
    auto compiled = Compile(f.m_Blueprint);
    if (!compiled.m_Program)
        return test::Check(false, "the blueprint compiles");
    Context context(compiled.m_Program);
    const Value up[] = { Value::Enum(key->AsType(), static_cast<const EnumType*>(key)->FindCase("Up")) };
    auto fiber = context.Start("Key Pressed", up);
    context.Run(std::chrono::seconds(5));
    auto taken = context.GetVariable("Taken");
    test::Check(made && fiber && most == static_cast<int>(blueprint_screen::ScreenKeyNames().size()) && taken && taken.GetValue() == Value::String("Up"),
        "it takes a case for each of the %d keys at most, and goes on from the pressed key's, Up's: %s", most, taken ? std::string(taken.GetValue().AsString()).c_str() : "");
}

TEST_CASE(KeysTaken, "screen.a_focused_screen_takes_its_keys_before_the_applications_routes", nullptr, test::TestFlags_None)
{
    // A screen, focused once it opens, and a global route of the application's to A, which the node
    // editor of the host, focused once the screen closes, doesn't route as it does Space.
    test::Session session(UnnamedConfig());
    ScreenHost    host;
    auto screen = host.Open("Test", 100, 50);
    auto global = 0;
    session.AfterHost = [&]
    {
        host.ShowWindows(false);
        if (ImGui::Shortcut(ImGuiKey_A, ImGuiInputFlags_RouteGlobal))
            ++global;
    };
    auto press = [&session](ImGuiKey key)
    {
        test::Key(key, true);
        session.Run(1);
        test::Key(key, false);
        session.Run(1);
    };
    const auto a = static_cast<int>(std::find(blueprint_screen::ScreenKeyNames().begin(), blueprint_screen::ScreenKeyNames().end(), "A") - blueprint_screen::ScreenKeyNames().begin());
    session.Run(3);
    press(ImGuiKey_A);
    auto taken = host.TakeKeys();
    test::Check(taken == std::vector<int>{ a } && global == 0, "a key pressed on the focused screen is its own, the application's route doesn't fire: %d taken, %d global", static_cast<int>(taken.size()), global);

    screen->m_Open = false;
    session.Run(2);
    press(ImGuiKey_A);
    test::Check(host.TakeKeys().empty() && global == 1, "with the screen closed, the application's route takes it: %d", global);
}

TEST_CASE(ScreenRuns, "screen.a_debuggers_runs_open_screens_of_its_host", nullptr, test::TestFlags_None)
{
    test::Session session(UnnamedConfig());
    OverlayLog    log;
    Document      document{ ExampleRegistry(), std::make_shared<TypeTable>() };
    auto& graph = document.GetGraph();
    auto  pin   = [&graph](Id node, std::string_view key) { auto found = graph.FindNode(node); auto pin = found ? found->FindPin(key) : nullptr; return pin ? pin->m_Id : 0; };
    auto  start = graph.AddNode("crude.Start").ValueOr(0), open = graph.AddNode("example.OpenScreen").ValueOr(0);
    auto  made  = graph.Connect(pin(start, "then"), pin(open, "enter")).IsOk();

    ScreenHost host;
    blueprint_editor_utilities::Debugger debugger;
    debugger.AddService(host);
    auto started = debugger.Start(document, true);
    debugger.Update(document);
    test::Check(made && started && host.Screens().size() == 1 && debugger.IsWaiting(), "a run of a debugger with the host opens a screen there, and then waits for nothing");
}
