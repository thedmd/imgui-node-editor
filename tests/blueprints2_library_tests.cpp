//------------------------------------------------------------------------------
// blueprints2-example's runtime: the node types of its library, run.
//------------------------------------------------------------------------------
# include "blueprints2_fixture.h"
# include "crude_blueprint_library.h"
# include <chrono>
# include <cstdint>
# include <memory>
# include <string>

using namespace blueprints2_tests;

namespace {

std::shared_ptr<const Registry> LibraryRegistry()
{
    static const std::shared_ptr<const Registry> registry = []
    {
        auto made = std::make_shared<Registry>();
        if (!RegisterBuiltins(*made) || !RegisterLibrary(*made))
            test::Check(false, "the library is registered");
        return made;
    }();
    return registry;
}

struct Logs final : ContextMonitor
{
    void OnLog(const Context&, Id, std::string_view text) override
    {
        m_Text += (m_Text.empty() ? "" : " ") + std::string(text);
    }

    std::string m_Text;
};

struct Fixture : BlueprintFixture
{
    Fixture(): BlueprintFixture(LibraryRegistry(), "crude.") {}

    Id Constant(Value value)
    {
        NodeConfig config;
        config.m_Types["T"]     = value.GetType();
        config.m_Data["value"] = std::move(value);
        return Add("Constant", std::move(config));
    }

    // Runs Start's fiber to its end, and gives what it printed.
    std::string Run(FiberState* end = nullptr, std::span<const Diagnostic>* diagnostics = nullptr)
    {
        auto compiled = Compile(m_Blueprint);
        if (!compiled.m_Program)
        {
            test::Check(false, "the blueprint compiles: %s", compiled.m_Diagnostics.empty() ? "" : std::string(compiled.m_Diagnostics[0].m_Error.Message()).c_str());
            return {};
        }
        m_Context = std::make_unique<Context>(compiled.m_Program);
        Logs logs;
        m_Context->SetMonitor(&logs);
        [[maybe_unused]] auto fiber = m_Context->Start("Start");
        m_Context->Run(std::chrono::seconds(5));
        m_Context->SetMonitor(nullptr);
        if (end)
            *end = m_Context->Fibers().empty() ? FiberState::Failed : m_Context->Fibers()[0].m_State;
        if (diagnostics)
            *diagnostics = m_Context->Diagnostics();
        return logs.m_Text;
    }

    std::unique_ptr<Context> m_Context;
};

} // namespace

TEST_CASE(ForLoopPrints, "library.a_for_loop_prints_its_sums_then_completes", nullptr, test::TestFlags_None)
{
    Fixture f;
    auto start = f.Add("Start"), loop = f.Add("ForLoop"), print = f.Add("Print"), add = f.Add("Add"), ten = f.Constant(Value::Int32(10)), done = f.Add("Print");
    f.Link(start, "then", loop, "enter");
    f.Link(loop, "body", print, "enter");
    f.Link(loop, "index", add, "in.0");
    f.Link(ten, "value", add, "in.1");
    f.Link(add, "result", print, "text", LinkMode::Cast);
    f.Link(loop, "completed", done, "enter");
    if (auto set = f.m_Graph.SetValue(f.Pin(loop, "last"), Value::Int32(2)); !set)
        test::Check(false, "the last index is set: %s", Message(set).c_str());
    if (auto set = f.m_Graph.SetValue(f.Pin(done, "text"), Value::String("done")); !set)
        test::Check(false, "the text is set: %s", Message(set).c_str());
    auto printed = f.Run();
    test::Check(printed == "10 11 12 done", "Start -> For Loop 0..2 -> Print(index + 10), then done: %s", printed.c_str());
    test::Check(f.Type(add, "result") == "Int32" && f.Type(ten, "value") == "Int32", "Add and the Constant are Int32");
}

TEST_CASE(BranchChooses, "library.branch_takes_the_way_its_condition_says", nullptr, test::TestFlags_None)
{
    for (auto condition : { true, false })
    {
        Fixture f;
        auto start = f.Add("Start"), branch = f.Add("Branch"), yes = f.Add("Print"), no = f.Add("Print"), constant = f.Constant(Value::Bool(condition));
        f.Link(start, "then", branch, "enter");
        f.Link(constant, "value", branch, "condition");
        f.Link(branch, "true", yes, "enter");
        f.Link(branch, "false", no, "enter");
        f.Link(f.Constant(Value::String("yes")), "value", yes, "text");
        f.Link(f.Constant(Value::String("no")), "value", no, "text");
        auto printed = f.Run();
        test::Check(printed == (condition ? "yes" : "no"), "%s goes its way: %s", condition ? "true" : "false", printed.c_str());
    }
}

TEST_CASE(AddsByType, "library.add_sums_numbers_and_joins_strings", nullptr, test::TestFlags_None)
{
    struct Case
    {
        Value       a, b, c;
        const char* printed;
    };
    const Case cases[] =
    {
        { Value::Float(1.5f), Value::Float(2.25f), Value::Float(-0.5f), "3.25" },
        { Value::String("a"), Value::String("b"),  Value::String("c"),  "abc"  },
        { Value::Int32(-7),   Value::Int32(3),     Value::Int32(4),     "0"    },
    };
    for (auto& c : cases)
    {
        Fixture f;
        auto start = f.Add("Start"), print = f.Add("Print"), add = f.Add("Add");
        auto grown = f.m_Graph.AddGroupPin(add, "in");
        f.Link(start, "then", print, "enter");
        f.Link(f.Constant(c.a), "value", add, "in.0");
        f.Link(f.Constant(c.b), "value", add, "in.1");
        f.Link(f.Constant(c.c), "value", add, "in.2");
        f.Link(add, "result", print, "text", c.a.GetType() == &StringType() ? LinkMode::Plain : LinkMode::Cast);
        auto printed = f.Run();
        test::Check(grown && printed == c.printed, "three %s inputs: %s", c.a.GetType()->m_Name.c_str(), printed.c_str());
    }
}

TEST_CASE(AddOverflows, "library.add_refuses_to_leave_int32s_range", nullptr, test::TestFlags_None)
{
    Fixture f;
    auto start = f.Add("Start"), print = f.Add("Print"), add = f.Add("Add");
    f.Link(start, "then", print, "enter");
    f.Link(f.Constant(Value::Int32(INT32_MAX)), "value", add, "in.0");
    f.Link(f.Constant(Value::Int32(1)), "value", add, "in.1");
    f.Link(add, "result", print, "text", LinkMode::Cast);
    FiberState                  end = FiberState::Ready;
    std::span<const Diagnostic> diagnostics;
    auto printed = f.Run(&end, &diagnostics);
    test::Check(end == FiberState::Failed && printed.empty() && diagnostics.size() == 1 && diagnostics[0].m_Node == add && diagnostics[0].m_Error.Is(BlueprintError::OutOfRange), "INT32_MAX + 1 stops the fiber at Add: %s", diagnostics.empty() ? "" : std::string(diagnostics[0].m_Error.Message()).c_str());
}

TEST_CASE(ConstantResets, "library.a_constant_of_another_type_gives_its_default", nullptr, test::TestFlags_None)
{
    // Kept while its type was undecided, as values are.
    Fixture f;
    auto start = f.Add("Start"), print = f.Add("Print");
    NodeConfig config;
    config.m_Types["T"]     = &Int32Type();
    config.m_Data["value"] = Value::Float(1.5f);
    auto constant = f.Add("Constant", config);
    f.Link(start, "then", print, "enter");
    f.Link(constant, "value", print, "text", LinkMode::Cast);
    auto printed = f.Run();
    test::Check(printed == "0", "an Int32 Constant holding a Float gives 0: %s", printed.c_str());
}

TEST_CASE(ForLoopBounds, "library.a_for_loop_runs_from_its_first_to_its_last_index", nullptr, test::TestFlags_None)
{
    struct Case
    {
        int32_t     first, last;
        const char* printed;
    };
    const Case cases[] =
    {
        { 3,         1,         "done"                       },
        { -1,        1,         "-1 0 1 done"                },
        { INT32_MAX, INT32_MAX, "2147483647 done"            },
    };
    for (auto& c : cases)
    {
        Fixture f;
        auto start = f.Add("Start"), loop = f.Add("ForLoop"), print = f.Add("Print"), done = f.Add("Print");
        f.Link(start, "then", loop, "enter");
        f.Link(loop, "body", print, "enter");
        f.Link(loop, "index", print, "text", LinkMode::Cast);
        f.Link(loop, "completed", done, "enter");
        f.Link(f.Constant(Value::Int32(c.first)), "value", loop, "first");
        f.Link(f.Constant(Value::Int32(c.last)), "value", loop, "last");
        f.Link(f.Constant(Value::String("done")), "value", done, "text");
        auto printed = f.Run();
        test::Check(printed == c.printed, "from %d to %d: %s", c.first, c.last, printed.c_str());
    }
}

TEST_CASE(LibraryOnce, "library.is_registered_once", nullptr, test::TestFlags_None)
{
    Registry registry;
    auto first  = RegisterLibrary(registry);
    auto second = RegisterLibrary(registry);
    test::Check(first && registry.FindNodeType("crude.ForLoop") && registry.FindNodeType("crude.ForLoop")->m_Title == "For Loop", "the library's node types are found by name");
    test::Check(!second && second.GetError().Is(BlueprintError::Duplicate), "and registering it again is refused: %s", Message(second).c_str());
}
