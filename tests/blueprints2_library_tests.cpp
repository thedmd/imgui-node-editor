//------------------------------------------------------------------------------
// blueprints2-example's runtime: the node types of its library, run.
//------------------------------------------------------------------------------
# include "blueprints2_fixture.h"
# include "crude_blueprint_library.h"
# include <chrono>
# include <cstdint>
# include <cstdio>
# include <limits>
# include <memory>
# include <span>
# include <string>
# include <utility>
# include <vector>

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

    // Start prints a node's output, through a cast unless it's a String, of the inputs it's given
    // as Constants; inputs a group lacks are added to it. The message of an error that stops it
    // comes after "error: ".
    std::string Compute(const char* type, std::span<const std::pair<const char*, Value>> inputs, const char* output = "result")
    {
        auto start = Add("Start"), print = Add("Print"), node = Add(type);
        for (auto& [key, value] : inputs)
        {
            while (!m_Graph.FindNode(node)->FindPin(key) && m_Graph.AddGroupPin(node, "in"))
                ;
            Link(Constant(value), "value", node, key);
        }
        Link(start, "then", print, "enter");
        Link(node, output, print, "text", Type(node, output) == "String" ? LinkMode::Plain : LinkMode::Cast);

        FiberState                  end = FiberState::Ready;
        std::span<const Diagnostic> diagnostics;
        auto printed = Run(&end, &diagnostics);
        if (end == FiberState::Failed)
            return "error: " + (diagnostics.empty() ? std::string() : std::string(diagnostics.back().m_Error.Message()));
        return printed;
    }

    // A Print of the text.
    Id Printer(const char* text)
    {
        auto print = Add("Print");
        if (auto set = m_Graph.SetValue(Pin(print, "text"), Value::String(text)); !set)
            test::Check(false, "a Print of %s is made: %s", text, Message(set).c_str());
        return print;
    }

    // A node of a group grown to the count.
    Id Grown(const char* type, const char* group, int count)
    {
        auto node = Add(type);
        NodeConfig config = m_Graph.FindNode(node)->m_Config;
        config.m_Groups[group] = count;
        if (auto set = m_Graph.SetConfig(node, std::move(config)); !set)
            test::Check(false, "%s's %s has %d pins: %s", type, group, count, Message(set).c_str());
        return node;
    }

    std::unique_ptr<Context> m_Context;
};

using Inputs = std::vector<std::pair<const char*, Value>>;

std::string Describe(const Inputs& inputs)
{
    std::string text;
    for (auto& [key, value] : inputs)
        text += (text.empty() ? "" : ", ") + std::string(key) + " " + value.GetType()->m_Head->Format(value);
    return text;
}

struct ComputeCase
{
    const char* type;
    Inputs      inputs;
    const char* expected;
};

void CheckComputed(std::span<const ComputeCase> cases)
{
    for (auto& c : cases)
    {
        Fixture f;
        auto got = f.Compute(c.type, c.inputs);
        test::Check(got == c.expected, "%s of %s: %s", c.type, Describe(c.inputs).c_str(), got.c_str());
    }
}

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

TEST_CASE(MathNodes, "library.math_works_on_int32s_and_floats", nullptr, test::TestFlags_None)
{
    const auto i = [](int32_t value) { return Value::Int32(value); };
    const auto f = [](float value) { return Value::Float(value); };
    const auto lowest = std::numeric_limits<int32_t>::min();
    const ComputeCase cases[] =
    {
        { "Subtract", { { "a", i(7) },      { "b", i(10) } },  "-3" },
        { "Subtract", { { "a", f(1.5f) },   { "b", f(0.25f) } }, "1.25" },
        { "Subtract", { { "a", i(lowest) }, { "b", i(1) } },   "error: the difference, -2147483649, is outside Int32's range" },
        { "Multiply", { { "in.0", i(3) },   { "in.1", i(4) }, { "in.2", i(5) } }, "60" },
        { "Multiply", { { "in.0", i(65536) }, { "in.1", i(65536) } }, "error: the result, 4294967296, is outside Int32's range" },
        { "Multiply", { { "in.0", f(1.5f) }, { "in.1", f(2.0f) } }, "3" },
        { "Divide",   { { "a", i(7) },      { "b", i(2) } },   "3" },
        { "Divide",   { { "a", i(-7) },     { "b", i(2) } },   "-3" },
        { "Divide",   { { "a", i(7) },      { "b", i(0) } },   "error: division by zero" },
        { "Divide",   { { "a", i(lowest) }, { "b", i(-1) } },  "error: the quotient, 2147483648, is outside Int32's range" },
        { "Divide",   { { "a", f(1.0f) },   { "b", f(4.0f) } }, "0.25" },
        { "Modulo",   { { "a", i(7) },      { "b", i(3) } },   "1" },
        { "Modulo",   { { "a", i(-7) },     { "b", i(3) } },   "-1" },
        { "Modulo",   { { "a", i(lowest) }, { "b", i(-1) } },  "0" },
        { "Modulo",   { { "a", i(7) },      { "b", i(0) } },   "error: modulo by zero" },
        { "Modulo",   { { "a", f(7.5f) },   { "b", f(2.0f) } }, "1.5" },
        { "Negate",   { { "value", i(5) } },      "-5" },
        { "Negate",   { { "value", i(lowest) } }, "error: the result, 2147483648, is outside Int32's range" },
        { "Negate",   { { "value", f(1.5f) } },   "-1.5" },
        { "Abs",      { { "value", i(-5) } },     "5" },
        { "Abs",      { { "value", i(lowest) } }, "error: the result, 2147483648, is outside Int32's range" },
        { "Abs",      { { "value", f(-2.5f) } },  "2.5" },
        { "Min",      { { "in.0", i(3) },   { "in.1", i(-1) }, { "in.2", i(2) } }, "-1" },
        { "Min",      { { "in.0", f(0.5f) }, { "in.1", f(-0.25f) } }, "-0.25" },
        { "Max",      { { "in.0", i(3) },   { "in.1", i(-1) }, { "in.2", i(2) } }, "3" },
        { "Clamp",    { { "value", i(5) },  { "min", i(0) }, { "max", i(3) } }, "3" },
        { "Clamp",    { { "value", i(-1) }, { "min", i(0) }, { "max", i(3) } }, "0" },
        { "Clamp",    { { "value", i(1) },  { "min", i(3) }, { "max", i(0) } }, "error: the min, 3, is above the max, 0" },
        { "Clamp",    { { "value", f(0.5f) }, { "min", f(0.0f) }, { "max", f(1.0f) } }, "0.5" },
    };
    CheckComputed(cases);
}

TEST_CASE(LogicNodes, "library.comparisons_and_logic_give_bools", nullptr, test::TestFlags_None)
{
    const auto i = [](int32_t value) { return Value::Int32(value); };
    const auto f = [](float value) { return Value::Float(value); };
    const auto s = [](const char* text) { return Value::String(text); };
    const auto b = [](bool value) { return Value::Bool(value); };
    const ComputeCase cases[] =
    {
        { "Equal",          { { "a", i(3) },      { "b", i(3) } },      "true" },
        { "Equal",          { { "a", s("a") },    { "b", s("b") } },    "false" },
        { "Equal",          { { "a", f(0.5f) },   { "b", f(0.5f) } },   "true" },
        { "NotEqual",       { { "a", i(3) },      { "b", i(4) } },      "true" },
        { "Less",           { { "a", i(3) },      { "b", i(4) } },      "true" },
        { "Less",           { { "a", f(4.5f) },   { "b", f(1.0f) } },   "false" },
        { "Less",           { { "a", s("ab") },   { "b", s("b") } },    "true" },
        { "LessOrEqual",    { { "a", i(4) },      { "b", i(4) } },      "true" },
        { "Greater",        { { "a", i(5) },      { "b", i(4) } },      "true" },
        { "GreaterOrEqual", { { "a", i(3) },      { "b", i(4) } },      "false" },
        { "And",            { { "in.0", b(true) },  { "in.1", b(true) }, { "in.2", b(false) } }, "false" },
        { "And",            { { "in.0", b(true) },  { "in.1", b(true) } },  "true" },
        { "Or",             { { "in.0", b(false) }, { "in.1", b(false) }, { "in.2", b(true) } }, "true" },
        { "Or",             { { "in.0", b(false) }, { "in.1", b(false) } }, "false" },
        { "Not",            { { "value", b(true) } }, "false" },
        { "Xor",            { { "a", b(true) },   { "b", b(false) } },  "true" },
        { "Xor",            { { "a", b(true) },   { "b", b(true) } },   "false" },
    };
    CheckComputed(cases);
}

TEST_CASE(NaNUnordered, "library.nan_is_in_no_order", nullptr, test::TestFlags_None)
{
    // 0 / 0, as no Constant holds NaN.
    for (auto type : { "Less", "LessOrEqual", "Greater", "GreaterOrEqual" })
    {
        Fixture f;
        auto start = f.Add("Start"), print = f.Add("Print"), node = f.Add(type), divide = f.Add("Divide");
        f.Link(start, "then", print, "enter");
        f.Link(f.Constant(Value::Float(0.0f)), "value", divide, "a");
        f.Link(f.Constant(Value::Float(0.0f)), "value", divide, "b");
        f.Link(divide, "result", node, "a");
        f.Link(f.Constant(Value::Float(1.0f)), "value", node, "b");
        f.Link(node, "result", print, "text", LinkMode::Cast);
        auto printed = f.Run();
        test::Check(printed == "false", "%s of NaN and 1: %s", type, printed.c_str());
    }
}

TEST_CASE(ConnectivesStop, "library.and_and_or_stop_at_the_input_that_decides", nullptr, test::TestFlags_None)
{
    // in.1 reads whether 7 / 0 is 0, and computing it fails.
    struct Case
    {
        const char* type;
        bool        first;
        const char* printed;
    };
    for (auto& c : { Case{ "And", false, "false" }, Case{ "Or", true, "true" }, Case{ "And", true, "" }, Case{ "Or", false, "" } })
    {
        Fixture f;
        auto start = f.Add("Start"), print = f.Add("Print"), node = f.Add(c.type), equal = f.Add("Equal"), divide = f.Add("Divide");
        f.Link(start, "then", print, "enter");
        f.Link(f.Constant(Value::Bool(c.first)), "value", node, "in.0");
        f.Link(f.Constant(Value::Int32(7)), "value", divide, "a");
        f.Link(f.Constant(Value::Int32(0)), "value", divide, "b");
        f.Link(divide, "result", equal, "a");
        f.Link(f.Constant(Value::Int32(0)), "value", equal, "b");
        f.Link(equal, "result", node, "in.1");
        f.Link(node, "result", print, "text", LinkMode::Cast);
        FiberState end = FiberState::Ready;
        auto printed = f.Run(&end);
        const auto decided = *c.printed != 0;
        test::Check(printed == c.printed && (end == FiberState::Failed) != decided, "%s with in.0 %s %s: %s", c.type, c.first ? "true" : "false", decided ? "decides without in.1" : "reads in.1, which fails", printed.c_str());
    }
}

TEST_CASE(SequenceTurns, "library.a_sequence_runs_its_outputs_in_turn", nullptr, test::TestFlags_None)
{
    // In For Loop's body, so it starts over in each round.
    Fixture f;
    auto start = f.Add("Start"), loop = f.Add("ForLoop"), sequence = f.Grown("Sequence", "then", 3), done = f.Printer("done");
    f.Link(start, "then", loop, "enter");
    f.Link(f.Constant(Value::Int32(1)), "value", loop, "last");
    f.Link(loop, "body", sequence, "enter");
    f.Link(sequence, "then.0", f.Printer("a"), "enter");
    f.Link(sequence, "then.1", f.Printer("b"), "enter");
    f.Link(sequence, "then.2", f.Printer("c"), "enter");
    f.Link(loop, "completed", done, "enter");
    auto printed = f.Run();
    test::Check(printed == "a b c a b c done", "two rounds of a, b and c, then done: %s", printed.c_str());
}

TEST_CASE(SwitchCases, "library.switch_on_int_takes_its_case_or_the_default", nullptr, test::TestFlags_None)
{
    struct Case
    {
        int32_t     selection;
        const char* printed;
    };
    for (auto& c : { Case{ 0, "zero" }, Case{ 2, "two" }, Case{ 3, "default" }, Case{ -1, "default" } })
    {
        Fixture f;
        auto start = f.Add("Start"), choice = f.Grown("SwitchOnInt", "case", 3);
        f.Link(start, "then", choice, "enter");
        f.Link(f.Constant(Value::Int32(c.selection)), "value", choice, "selection");
        f.Link(choice, "case.0", f.Printer("zero"), "enter");
        f.Link(choice, "case.1", f.Printer("one"), "enter");
        f.Link(choice, "case.2", f.Printer("two"), "enter");
        f.Link(choice, "default", f.Printer("default"), "enter");
        auto printed = f.Run();
        test::Check(printed == c.printed, "%d of 3 cases: %s", c.selection, printed.c_str());
    }
}

TEST_CASE(ForEachItems, "library.for_each_gives_each_item_and_its_index", nullptr, test::TestFlags_None)
{
    Fixture f;
    auto  start = f.Add("Start"), loop = f.Add("ForEach"), index = f.Add("Print"), element = f.Add("Print"), done = f.Printer("done");
    auto& ints  = *f.m_Blueprint.GetTypes().Get(ArrayValueType(), std::vector<const Type*>{ &Int32Type() });
    f.Link(start, "then", loop, "enter");
    f.Link(f.Constant(Value::Array(ints, { Value::Int32(10), Value::Int32(20), Value::Int32(30) })), "value", loop, "array");
    f.Link(loop, "body", index, "enter");
    f.Link(index, "then", element, "enter");
    f.Link(loop, "index", index, "text", LinkMode::Cast);
    f.Link(loop, "element", element, "text", LinkMode::Cast);
    f.Link(loop, "completed", done, "enter");
    auto printed = f.Run();
    test::Check(printed == "0 10 1 20 2 30 done" && f.Type(loop, "element") == "Int32", "each index and item of [10, 20, 30], then done: %s", printed.c_str());
}

TEST_CASE(WhileLoopRounds, "library.a_while_loop_reads_its_condition_before_each_round", nullptr, test::TestFlags_None)
{
    // The condition is that Flip Flop isn't at A: true until the body enters it once.
    Fixture f;
    auto start = f.Add("Start"), loop = f.Add("WhileLoop"), flipFlop = f.Add("FlipFlop"), isB = f.Add("Not"), done = f.Printer("done");
    f.Link(start, "then", loop, "enter");
    f.Link(flipFlop, "isA", isB, "value");
    f.Link(isB, "result", loop, "condition");
    f.Link(loop, "body", flipFlop, "enter");
    f.Link(flipFlop, "a", f.Printer("a"), "enter");
    f.Link(loop, "completed", done, "enter");
    auto printed = f.Run();
    test::Check(printed == "a done", "one round, then done: %s", printed.c_str());
}

TEST_CASE(DoOnceReset, "library.do_once_goes_on_once_until_its_reset", nullptr, test::TestFlags_None)
{
    Fixture f;
    auto start = f.Add("Start"), sequence = f.Grown("Sequence", "then", 4), once = f.Add("DoOnce");
    f.Link(start, "then", sequence, "enter");
    f.Link(sequence, "then.0", once, "enter");
    f.Link(sequence, "then.1", once, "enter");
    f.Link(sequence, "then.2", once, "reset");
    f.Link(sequence, "then.3", once, "enter");
    f.Link(once, "completed", f.Printer("once"), "enter");
    auto printed = f.Run();
    test::Check(printed == "once once", "entered twice, reset, entered again: %s", printed.c_str());
}

TEST_CASE(DoNCounts, "library.do_n_goes_on_n_times_counting_from_1", nullptr, test::TestFlags_None)
{
    Fixture f;
    auto start = f.Add("Start"), loop = f.Add("ForLoop"), doN = f.Add("DoN"), print = f.Add("Print"), done = f.Printer("done");
    f.Link(start, "then", loop, "enter");
    f.Link(f.Constant(Value::Int32(4)), "value", loop, "last");
    f.Link(f.Constant(Value::Int32(3)), "value", doN, "n");
    f.Link(loop, "body", doN, "enter");
    f.Link(doN, "exit", print, "enter");
    f.Link(doN, "counter", print, "text", LinkMode::Cast);
    f.Link(loop, "completed", done, "enter");
    auto printed = f.Run();
    test::Check(printed == "1 2 3 done", "five rounds, three of them through: %s", printed.c_str());
}

TEST_CASE(GateOpens, "library.a_gate_lets_the_flow_through_while_its_open", nullptr, test::TestFlags_None)
{
    for (auto startClosed : { true, false })
    {
        // Entered, opened, entered, toggled, entered.
        Fixture f;
        auto start = f.Add("Start"), sequence = f.Grown("Sequence", "then", 5), gate = f.Add("Gate");
        f.Link(start, "then", sequence, "enter");
        f.Link(f.Constant(Value::Bool(startClosed)), "value", gate, "startClosed");
        f.Link(sequence, "then.0", gate, "enter");
        f.Link(sequence, "then.1", gate, "open");
        f.Link(sequence, "then.2", gate, "enter");
        f.Link(sequence, "then.3", gate, "toggle");
        f.Link(sequence, "then.4", gate, "enter");
        f.Link(gate, "exit", f.Printer("through"), "enter");
        auto printed = f.Run();
        auto expected = startClosed ? "through" : "through through";
        test::Check(printed == expected, "starting %s: %s", startClosed ? "closed" : "open", printed.c_str());
    }
}

TEST_CASE(FlipFlopAlternates, "library.a_flip_flop_alternates_from_a", nullptr, test::TestFlags_None)
{
    Fixture f;
    auto start = f.Add("Start"), loop = f.Add("ForLoop"), flipFlop = f.Add("FlipFlop");
    f.Link(start, "then", loop, "enter");
    f.Link(f.Constant(Value::Int32(3)), "value", loop, "last");
    f.Link(loop, "body", flipFlop, "enter");
    f.Link(flipFlop, "a", f.Printer("a"), "enter");
    f.Link(flipFlop, "b", f.Printer("b"), "enter");
    auto printed = f.Run();
    test::Check(printed == "a b a b", "four rounds: %s", printed.c_str());
}

TEST_CASE(RandomPerStep, "library.random_gives_one_value_a_step", nullptr, test::TestFlags_None)
{
    // Random - Random reads one value; the Prints after it, each a step of its own, read two.
    Fixture f;
    auto start = f.Add("Start"), difference = f.Add("Print"), first = f.Add("Print"), second = f.Add("Print"), random = f.Add("Random"), subtract = f.Add("Subtract");
    f.Link(start, "then", difference, "enter");
    f.Link(difference, "then", first, "enter");
    f.Link(first, "then", second, "enter");
    f.Link(random, "value", subtract, "a");
    f.Link(random, "value", subtract, "b");
    f.Link(subtract, "result", difference, "text", LinkMode::Cast);
    f.Link(random, "value", first, "text", LinkMode::Cast);
    f.Link(random, "value", second, "text", LinkMode::Cast);
    auto printed = f.Run();

    float a = -1.0f, b = -1.0f;
    const auto read = std::sscanf(printed.c_str(), "0 %f %f", &a, &b);
    test::Check(read == 2 && a != b && a >= 0.0f && a < 1.0f && b >= 0.0f && b < 1.0f, "0, then two values from 0 up to 1: %s", printed.c_str());
}

TEST_CASE(LibraryOnce, "library.is_registered_once", nullptr, test::TestFlags_None)
{
    Registry registry;
    auto first  = RegisterLibrary(registry);
    auto second = RegisterLibrary(registry);
    test::Check(first && registry.FindNodeType("crude.ForLoop") && registry.FindNodeType("crude.ForLoop")->m_Title == "For Loop", "the library's node types are found by name");
    test::Check(!second && second.GetError().Is(BlueprintError::Duplicate), "and registering it again is refused: %s", Message(second).c_str());
}
