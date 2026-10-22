//------------------------------------------------------------------------------
// blueprints2-example's runtime: the node types of its library, run.
//------------------------------------------------------------------------------
# include "blueprints2_fixture.h"
# include "crude_blueprint_library.h"
# include <crude_json.h>
# include <algorithm>
# include <chrono>
# include <cstdint>
# include <cstdio>
# include <limits>
# include <memory>
# include <span>
# include <string>
# include <utility>
# include <variant>
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
    explicit Fixture(std::shared_ptr<const Registry> registry = LibraryRegistry()): BlueprintFixture(std::move(registry), "crude.") {}

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

    // Start prints a node's output, of the inputs it's given as Constants; inputs a group lacks are
    // added to it. An output that isn't a String goes through the cast to String, or To String for
    // a type without one. The message of an error that stops it comes after "error: ".
    std::string Compute(const char* type, std::span<const std::pair<const char*, Value>> inputs, const char* output = "result")
    {
        auto start = Add("Start"), print = Add("Print"), node = Add(type);
        for (auto& [key, value] : inputs)
        {
            while (!m_Graph.FindNode(node)->FindPin(key))
            {
                auto& groups = m_Graph.FindNode(node)->m_Groups;
                auto  group  = std::find_if(groups.begin(), groups.end(), [key = key](const GroupDeclaration& other) { return GroupIndex(other, key) >= 0; });
                if (group == groups.end() || !m_Graph.AddGroupPin(node, std::string(group->m_Name)))
                    break;
            }
            Link(Constant(value), "value", node, key);
        }
        Link(start, "then", print, "enter");
        auto decided = m_Graph.FindPin(Pin(node, output))->m_DecidedType;
        if (decided == &StringType())
            Link(node, output, print, "text");
        else if (m_Blueprint.GetRegistry().FindCast(decided, &StringType()))
            Link(node, output, print, "text", LinkMode::Cast);
        else
        {
            auto text = Add("ToString");
            Link(node, output, text, "value");
            Link(text, "text", print, "text");
        }

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

TEST_CASE(ForEachEntries, "library.for_each_entry_gives_each_key_and_value_in_order", nullptr, test::TestFlags_None)
{
    Fixture f;
    auto  start = f.Add("Start"), loop = f.Add("ForEachEntry"), key = f.Add("Print"), value = f.Add("Print"), done = f.Printer("done");
    auto& type  = *f.m_Blueprint.GetTypes().Get(MapValueType(), std::vector<const Type*>{ &Int32Type(), &StringType() });
    f.Link(start, "then", loop, "enter");
    f.Link(f.Constant(Value::Map(type, { { Value::Int32(2), Value::String("b") }, { Value::Int32(1), Value::String("a") } })), "value", loop, "map");
    f.Link(loop, "body", key, "enter");
    f.Link(key, "then", value, "enter");
    f.Link(loop, "key", key, "text", LinkMode::Cast);
    f.Link(loop, "value", value, "text");
    f.Link(loop, "completed", done, "enter");
    auto printed = f.Run();
    test::Check(printed == "1 a 2 b done", "each entry of {2: b, 1: a}, by key, then done: %s", printed.c_str());
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

TEST_CASE(ValueNodes, "library.select_text_and_arrays_give_their_values", nullptr, test::TestFlags_None)
{
    // An array's type comes from the type table of the blueprint it's for.
    using Array = std::vector<int32_t>;
    struct Case
    {
        const char*                       type;
        const char*                       output;
        std::vector<std::pair<const char*, std::variant<Value, Array>>> inputs;
        const char*                       expected;
    };
    const auto i = [](int32_t value) { return Value::Int32(value); };
    const auto s = [](const char* text) { return Value::String(text); };
    const auto b = [](bool value) { return Value::Bool(value); };
    const Case cases[] =
    {
        { "Select",    "result", { { "pickA", b(true) },  { "a", i(1) }, { "b", i(2) } }, "1" },
        { "Select",    "result", { { "pickA", b(false) }, { "a", i(1) }, { "b", i(2) } }, "2" },
        { "ToString",  "text",   { { "value", i(5) } },                 "5" },
        { "ToString",  "text",   { { "value", Array{ 1, 2 } } },        "[1, 2]" },
        { "Length",    "length", { { "value", s("za\xC5\xBC\xC3\xB3\xC5\x82\xC4\x87") } }, "6" },
        { "Length",    "length", { { "value", Array{ 1, 2, 3 } } },     "3" },
        { "MakeArray", "array",  { { "in.0", i(1) }, { "in.1", i(2) }, { "in.2", i(3) } }, "[1, 2, 3]" },
        { "Get",       "item",   { { "array", Array{ 10, 20, 30 } }, { "index", i(1) } }, "20" },
        { "Get",       "item",   { { "array", Array{ 10, 20, 30 } }, { "index", i(3) } }, "error: index 3 is outside the array of 3 items" },
        { "Get",       "item",   { { "array", Array{ 10, 20, 30 } }, { "index", i(-1) } }, "error: index -1 is outside the array of 3 items" },
        { "Set",       "result", { { "array", Array{ 10, 20, 30 } }, { "index", i(1) }, { "item", i(99) } }, "[10, 99, 30]" },
        { "Set",       "result", { { "array", Array{ 10 } },         { "index", i(5) }, { "item", i(99) } }, "error: index 5 is outside the array of 1 item" },
        { "Append",    "result", { { "array", Array{ 10 } },         { "item", i(20) } }, "[10, 20]" },
    };
    for (auto& c : cases)
    {
        Fixture f;
        auto&  ints = *f.m_Blueprint.GetTypes().Get(ArrayValueType(), std::vector<const Type*>{ &Int32Type() });
        Inputs inputs;
        for (auto& [key, input] : c.inputs)
        {
            if (auto value = std::get_if<Value>(&input))
                inputs.emplace_back(key, *value);
            else
            {
                std::vector<Value> items;
                for (auto item : std::get<Array>(input))
                    items.push_back(Value::Int32(item));
                inputs.emplace_back(key, Value::Array(ints, std::move(items)));
            }
        }
        auto got = f.Compute(c.type, inputs, c.output);
        test::Check(got == c.expected, "%s of %s: %s", c.type, Describe(inputs).c_str(), got.c_str());
    }
}

TEST_CASE(SelectPicks, "library.select_computes_only_the_input_it_picks", nullptr, test::TestFlags_None)
{
    // B reads 7 / 0, and computing it fails.
    for (auto pickA : { true, false })
    {
        Fixture f;
        auto start = f.Add("Start"), print = f.Add("Print"), select = f.Add("Select"), divide = f.Add("Divide");
        f.Link(start, "then", print, "enter");
        f.Link(f.Constant(Value::Bool(pickA)), "value", select, "pickA");
        f.Link(f.Constant(Value::Int32(1)), "value", select, "a");
        f.Link(f.Constant(Value::Int32(7)), "value", divide, "a");
        f.Link(f.Constant(Value::Int32(0)), "value", divide, "b");
        f.Link(divide, "result", select, "b");
        f.Link(select, "result", print, "text", LinkMode::Cast);
        FiberState end = FiberState::Ready;
        auto printed = f.Run(&end);
        test::Check(pickA ? printed == "1" && end == FiberState::Done : end == FiberState::Failed, "picking %s: %s", pickA ? "A, which doesn't compute B" : "B, which fails", printed.c_str());
    }
}

TEST_CASE(BreakArrays, "library.break_array_gives_an_item_for_each_output", nullptr, test::TestFlags_None)
{
    for (auto count : { 2, 3 })
    {
        // [10, 20] into outputs 0 and 1, or into 0 to 2.
        Fixture f;
        auto& ints  = *f.m_Blueprint.GetTypes().Get(ArrayValueType(), std::vector<const Type*>{ &Int32Type() });
        auto  start = f.Add("Start"), print = f.Add("Print"), node = f.Grown("BreakArray", "out", count);
        f.Link(start, "then", print, "enter");
        f.Link(f.Constant(Value::Array(ints, { Value::Int32(10), Value::Int32(20) })), "value", node, "array");
        f.Link(node, "out.1", print, "text", LinkMode::Cast);
        FiberState                  end = FiberState::Ready;
        std::span<const Diagnostic> diagnostics;
        auto printed = f.Run(&end, &diagnostics);
        if (count == 2)
            test::Check(printed == "20", "into 2 outputs, the second is 20: %s", printed.c_str());
        else
            test::Check(end == FiberState::Failed && !diagnostics.empty() && diagnostics[0].m_Error.Message() == "the array has 2 items, not the 3 it breaks into", "into 3, it's too short: %s", diagnostics.empty() ? "" : std::string(diagnostics[0].m_Error.Message()).c_str());
    }
}

TEST_CASE(MapNodes, "library.maps_are_made_read_and_changed", nullptr, test::TestFlags_None)
{
    // Maps of Int32 to String; a map's type comes from the type table of the blueprint it's for.
    using Entries = std::vector<std::pair<int32_t, const char*>>;
    struct Case
    {
        const char* type;
        const char* output;
        std::vector<std::pair<const char*, std::variant<Value, Entries>>> inputs;
        const char* expected;
    };
    const auto i = [](int32_t value) { return Value::Int32(value); };
    const auto s = [](const char* text) { return Value::String(text); };
    const Entries ab = { { 1, "a" }, { 2, "b" } };
    const Case cases[] =
    {
        { "MakeMap",  "map",    { { "key.0", i(2) }, { "value.0", s("b") }, { "key.1", i(1) }, { "value.1", s("a") } }, "{1: \"a\", 2: \"b\"}" },
        { "MakeMap",  "map",    { { "key.0", i(1) }, { "value.0", s("x") }, { "key.1", i(1) }, { "value.1", s("y") } }, "{1: \"y\"}" },
        { "Find",     "value",  { { "map", ab }, { "key", i(2) } }, "b" },
        { "Find",     "found",  { { "map", ab }, { "key", i(2) } }, "true" },
        { "Find",     "value",  { { "map", ab }, { "key", i(3) } }, "" },
        { "Find",     "found",  { { "map", ab }, { "key", i(3) } }, "false" },
        { "Contains", "result", { { "map", ab }, { "key", i(1) } }, "true" },
        { "Contains", "result", { { "map", ab }, { "key", i(5) } }, "false" },
        { "Insert",   "result", { { "map", ab }, { "key", i(3) }, { "value", s("c") } }, "{1: \"a\", 2: \"b\", 3: \"c\"}" },
        { "Insert",   "result", { { "map", ab }, { "key", i(1) }, { "value", s("z") } }, "{1: \"z\", 2: \"b\"}" },
        { "Remove",   "result", { { "map", ab }, { "key", i(1) } }, "{2: \"b\"}" },
        { "Remove",   "result", { { "map", ab }, { "key", i(5) } }, "{1: \"a\", 2: \"b\"}" },
        { "Keys",     "items",  { { "map", ab } }, "[1, 2]" },
        { "Values",   "items",  { { "map", ab } }, "[\"a\", \"b\"]" },
    };
    for (auto& c : cases)
    {
        Fixture f;
        auto&  type = *f.m_Blueprint.GetTypes().Get(MapValueType(), std::vector<const Type*>{ &Int32Type(), &StringType() });
        Inputs inputs;
        for (auto& [key, input] : c.inputs)
        {
            if (auto value = std::get_if<Value>(&input))
            {
                inputs.emplace_back(key, *value);
                continue;
            }
            std::vector<std::pair<Value, Value>> entries;
            for (auto& [entryKey, entryValue] : std::get<Entries>(input))
                entries.emplace_back(Value::Int32(entryKey), Value::String(entryValue));
            inputs.emplace_back(key, Value::Map(type, std::move(entries)));
        }
        auto got = f.Compute(c.type, inputs, c.output);
        test::Check(got == c.expected, "%s of %s, %s: %s", c.type, Describe(inputs).c_str(), c.output, got.c_str());
    }
}

TEST_CASE(AssertStops, "library.a_failing_assert_stops_its_fiber", nullptr, test::TestFlags_None)
{
    for (auto condition : { true, false })
    {
        Fixture f;
        auto start = f.Add("Start"), check = f.Add("Assert"), after = f.Printer("after");
        f.Link(start, "then", check, "enter");
        f.Link(f.Constant(Value::Bool(condition)), "value", check, "condition");
        f.Link(f.Constant(Value::String("it holds")), "value", check, "message");
        f.Link(check, "then", after, "enter");
        FiberState                  end = FiberState::Ready;
        std::span<const Diagnostic> diagnostics;
        auto printed = f.Run(&end, &diagnostics);
        if (condition)
            test::Check(printed == "after" && end == FiberState::Done, "true goes on: %s", printed.c_str());
        else
            test::Check(printed.empty() && end == FiberState::Failed && diagnostics.size() == 1 && diagnostics[0].m_Node == check && diagnostics[0].m_Error.Is(LibraryError::AssertFailed) && diagnostics[0].m_Error.Message() == "assertion failed: it holds",
                "false stops the fiber at Assert: %s", diagnostics.empty() ? "" : std::string(diagnostics[0].m_Error.Message()).c_str());
    }
}

TEST_CASE(StructNodes, "library.a_struct_is_made_and_broken_by_nodes_of_its_own", nullptr, test::TestFlags_None)
{
    static const StructType vector2("test.Vector2", { { "x", &FloatType() }, { "y", &FloatType() } });
    static const std::shared_ptr<const Registry> registry = []
    {
        auto made = std::make_shared<Registry>();
        if (!RegisterBuiltins(*made) || !RegisterLibrary(*made) || !RegisterStructNodes(*made, vector2))
            test::Check(false, "the library and Vector2's nodes are registered");
        return made;
    }();
    auto makeType = registry->FindNodeType("make:test.Vector2"), breakType = registry->FindNodeType("break:test.Vector2");
    test::Check(makeType && makeType->m_Title == "Make Vector2" && breakType && breakType->m_Title == "Break Vector2", "they're named after the struct, and titled after its short name");

    // Make (1.5, 2) printed whole, and its y through Break; then its x, read off its split output.
    Fixture f(registry);
    auto node  = [&f](const char* type) { return f.m_Graph.AddNode(type).ValueOr(0); };
    auto start = f.Add("Start"), whole = f.Add("Print"), y = f.Add("Print"), x = f.Add("Print"), text = f.Add("ToString");
    auto make  = node("make:test.Vector2"), take = node("break:test.Vector2"), split = node("make:test.Vector2");
    for (auto made : { make, split })
    {
        f.Link(f.Constant(Value::Float(1.5f)), "value", made, "x");
        f.Link(f.Constant(Value::Float(2.0f)), "value", made, "y");
    }
    auto splitting = f.m_Graph.SplitPin(split, "struct", true);
    f.Link(start, "then", whole, "enter");
    f.Link(whole, "then", y, "enter");
    f.Link(y, "then", x, "enter");
    f.Link(make, "struct", text, "value");
    f.Link(text, "text", whole, "text");
    f.Link(make, "struct", take, "struct");
    f.Link(take, "y", y, "text", LinkMode::Cast);
    f.Link(split, "struct.x", x, "text", LinkMode::Cast);
    auto printed = f.Run();
    test::Check(splitting && printed == "{x: 1.5, y: 2} 2 1.5", "the struct, its y, and its x: %s", printed.c_str());

    Registry other;
    const StructType clash("test.Clash", { { "struct", &Int32Type() } });
    auto refused = RegisterStructNodes(other, clash);
    test::Check(!refused && refused.GetError().Is(BlueprintError::Duplicate) && !other.FindNodeType("make:test.Clash"), "a field named struct is refused: %s", Message(refused).c_str());
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

TEST_CASE(DelayWaits, "library.delay_goes_on_once_its_time_passed_and_ends_the_flows_entering_meanwhile", nullptr, test::TestFlags_None)
{
    // Start, then a Delay of 1.5 s, then a Print. Start runs again while it counts down, and again
    // once it's done.
    Fixture f;
    auto start = f.Add("Start"), delay = f.Add("Delay");
    f.Link(start, "then", delay, "enter");
    f.Link(delay, "completed", f.Printer("done"), "enter");
    f.Link(f.Constant(Value::Float(1.5f)), "value", delay, "duration");
    auto end     = FiberState::Ready;
    auto printed = f.Run(&end);
    if (!f.m_Context)
        return;
    auto& context = *f.m_Context;
    test::Check(printed.empty() && end == FiberState::Suspended && context.Fibers()[0].m_Wake == Wake::At(1.5), "the fiber waits until 1.5");

    Logs logs;
    context.SetMonitor(&logs);
    auto again = context.Start("Start").IsOk() && context.Run(std::chrono::seconds(1)) == RunState::Suspended;
    test::Check(again && context.Fibers()[1].m_State == FiberState::Done && logs.m_Text.empty(), "another fiber that enters meanwhile ends there");
    context.Advance(1.0);
    auto early = context.Run(std::chrono::seconds(1));
    context.Advance(0.5);
    auto late = context.Run(std::chrono::seconds(1));
    test::Check(early == RunState::Suspended && late == RunState::Idle && logs.m_Text == "done", "at 1.5, not before, the first goes on: %s", logs.m_Text.c_str());

    auto third = context.Start("Start").IsOk() && context.Run(std::chrono::seconds(1)) == RunState::Suspended;
    test::Check(third && context.Fibers()[2].m_Wake == Wake::At(3.0), "once it's done, a fiber that enters waits in turn");
}

TEST_CASE(DelayOfNoNumber, "library.a_delay_of_no_number_waits_for_the_next_advance", nullptr, test::TestFlags_None)
{
    // 0 / 0, as no Constant holds NaN.
    Fixture f;
    auto start = f.Add("Start"), delay = f.Add("Delay"), divide = f.Add("Divide");
    f.Link(start, "then", delay, "enter");
    f.Link(delay, "completed", f.Printer("done"), "enter");
    f.Link(f.Constant(Value::Float(0.0f)), "value", divide, "a");
    f.Link(f.Constant(Value::Float(0.0f)), "value", divide, "b");
    f.Link(divide, "result", delay, "duration");
    auto end = FiberState::Ready;
    f.Run(&end);
    if (!f.m_Context)
        return;
    Logs logs;
    f.m_Context->SetMonitor(&logs);
    f.m_Context->Advance(0.0);
    auto went = f.m_Context->Run(std::chrono::seconds(1));
    test::Check(end == FiberState::Suspended && went == RunState::Idle && logs.m_Text == "done", "it waits as long as one of 0: %s", logs.m_Text.c_str());
}

TEST_CASE(WaitSignalWaits, "library.wait_signal_goes_on_once_its_signal_comes", nullptr, test::TestFlags_None)
{
    Fixture f;
    auto start = f.Add("Start"), wait = f.Add("WaitSignal");
    f.Link(start, "then", wait, "enter");
    f.Link(wait, "then", f.Printer("went"), "enter");
    f.Link(f.Constant(Value::String("go")), "value", wait, "signal");
    auto end     = FiberState::Ready;
    auto printed = f.Run(&end);
    if (!f.m_Context)
        return;
    auto& context = *f.m_Context;
    test::Check(printed.empty() && end == FiberState::Suspended && context.Fibers()[0].m_Wake == Wake::OnSignal("go"), "the fiber waits for go");

    Logs logs;
    context.SetMonitor(&logs);
    context.Advance(100.0);
    auto waiting = context.Run(std::chrono::seconds(1));
    context.Signal("go");
    auto went = context.Run(std::chrono::seconds(1));
    test::Check(waiting == RunState::Suspended && went == RunState::Idle && logs.m_Text == "went", "however long it waits, until go comes: %s", logs.m_Text.c_str());
}

TEST_CASE(CustomEvents, "library.a_custom_event_starts_with_the_arguments_it_is_given", nullptr, test::TestFlags_None)
{
    // Greet, of a String and an Int32, prints its String, then its Int32; another Custom Event has
    // no name of its own.
    Fixture f;
    NodeConfig config;
    config.m_Data["event"]   = Value::String("Greet");
    config.m_Groups["param"] = 2;
    config.m_Types["T1"]     = &Int32Type();
    auto greet = f.Add("CustomEvent", config), first = f.Add("Print"), second = f.Add("Print"), unnamed = f.Add("CustomEvent");
    f.Link(greet, "then", first, "enter");
    f.Link(first, "then", second, "enter");
    f.Link(greet, "param.0", first, "text");
    f.Link(greet, "param.1", second, "text", LinkMode::Cast);
    auto compiled = Compile(f.m_Blueprint);
    if (!compiled.m_Program)
        return test::Check(false, "the blueprint compiles: %s", compiled.m_Diagnostics.empty() ? "" : std::string(compiled.m_Diagnostics[0].m_Error.Message()).c_str());
    test::Check(f.Type(greet, "param.0") == "String" && compiled.m_Program->FindEvent("Event") == compiled.m_Program->FindNode(unnamed), "a parameter takes its type from its link, and an event of no name is Event");

    Context context(compiled.m_Program);
    Logs    logs;
    context.SetMonitor(&logs);
    const Value arguments[] = { Value::String("hi"), Value::Int32(3) };
    auto started = context.Start("Greet", arguments);
    context.Run(std::chrono::seconds(1));
    test::Check(started && logs.m_Text == "hi 3", "started with its arguments, it prints them: %s", logs.m_Text.c_str());

    const Value few[]   = { Value::String("hi") };
    const Value wrong[] = { Value::Int32(1), Value::Int32(3) };
    auto fewer  = context.Start("Greet", few);
    auto typed  = context.Start("Greet", wrong);
    test::Check(!fewer && fewer.GetError().Is(BlueprintError::OutOfRange) && !typed && typed.GetError().Is(BlueprintError::WrongType) && context.Fibers().size() == 1, "too few arguments, or one of another type, start none: %s", Message(typed).c_str());
}

TEST_CASE(EventParameterNames, "library.a_custom_events_parameters_take_the_names_given_them", nullptr, test::TestFlags_None)
{
    // Spawn's three parameters, the first named Who and the last Times.
    Fixture f;
    NodeConfig config;
    config.m_Data["event"]   = Value::String("Spawn");
    config.m_Groups["param"] = 3;
    config.m_Names           = { { "param.0", "Who" }, { "param.2", "Times" } };
    auto event = f.Add("CustomEvent", config);
    auto names = [&f, event]
    {
        std::string text;
        for (auto& pin : f.m_Graph.FindNode(event)->m_Pins)
            if (!pin.m_Flow)
                text += (text.empty() ? "" : ", ") + pin.m_Name;
        return text;
    };
    test::Check(names() == "Who, Param 1, Times", "a named parameter shows its name, the others theirs: %s", names().c_str());

    auto removed = f.m_Graph.RemoveGroupPin(event, "param", 1).IsOk();
    auto kept    = f.m_Graph.FindNode(event)->m_Config.m_Names;
    test::Check(removed && names() == "Who, Times" && kept.size() == 2 && kept["param.1"] == "Times", "a name moves with its parameter as one before it goes: %s", names().c_str());

    auto gone = f.m_Graph.RemoveGroupPin(event, "param", 0).IsOk();
    test::Check(gone && names() == "Times" && f.m_Graph.FindNode(event)->m_Config.m_Names.size() == 1, "and goes with it: %s", names().c_str());
}

TEST_CASE(TypeSwitches, "library.switch_on_type_takes_the_case_of_its_values_type", nullptr, test::TestFlags_None)
{
    // Start -> Switch on Type of a Constant, its cases Int32 and Float each printing its value, and
    // Otherwise printing "other".
    struct Case { Value m_Value; const char* m_Printed; };
    const Case cases[] = { { Value::Int32(7), "7" }, { Value::Float(2.5f), "2.5" }, { Value::String("x"), "other" } };
    for (auto& [value, printed] : cases)
    {
        Fixture f;
        NodeConfig config;
        config.m_Groups["case"] = 2;
        config.m_Types["C0"]    = &Int32Type();
        config.m_Types["C1"]    = &FloatType();
        auto start = f.Add("Start"), choose = f.Add("SwitchOnType", config), ints = f.Add("Print"), reals = f.Add("Print"), other = f.Printer("other");
        f.Link(start, "then", choose, "enter");
        f.Link(f.Constant(value), "value", choose, "value");
        f.Link(choose, "is.0", ints, "enter");
        f.Link(choose, "as.0", ints, "text", LinkMode::Cast);
        f.Link(choose, "is.1", reals, "enter");
        f.Link(choose, "as.1", reals, "text", LinkMode::Cast);
        f.Link(choose, "otherwise", other, "enter");
        auto end  = FiberState::Ready;
        auto text = f.Run(&end);
        test::Check(text == printed && end == FiberState::Done, "%s takes its case: %s", value.GetType()->m_Name.c_str(), text.c_str());

        auto node = f.m_Graph.FindNode(choose);
        test::Check(node->FindPin("is.0")->m_Name == "Int32" && node->FindPin("as.1")->m_Name == "Float", "and the cases are named by their types");
    }

    Fixture f;
    auto undecided = f.Add("SwitchOnType");
    test::Check(f.m_Graph.FindNode(undecided)->FindPin("is.0")->m_Name == "?", "a case of no type yet is a ?");
}

TEST_CASE(TypeViews, "library.as_type_and_expect_type_give_a_value_as_a_type", nullptr, test::TestFlags_None)
{
    // Start prints As Type's Result and Is, and then Expect Type's Result, each of a Constant, as an
    // Int32.
    auto run = [](Value value)
    {
        Fixture f;
        auto start = f.Add("Start"), result = f.Add("Print"), is = f.Add("Print"), expected = f.Add("Print");
        auto as = f.Add("AsType", Chosen("R", Int32Type())), expect = f.Add("ExpectType", Chosen("R", Int32Type()));
        auto constant = f.Constant(value);
        f.Link(constant, "value", as, "value");
        f.Link(constant, "value", expect, "value");
        f.Link(start, "then", result, "enter");
        f.Link(result, "then", is, "enter");
        f.Link(is, "then", expected, "enter");
        f.Link(as, "result", result, "text", LinkMode::Cast);
        auto isText = f.Add("ToString");
        f.Link(as, "is", isText, "value");
        f.Link(isText, "text", is, "text");
        f.Link(expect, "result", expected, "text", LinkMode::Cast);
        auto end = FiberState::Ready;
        std::span<const Diagnostic> diagnostics;
        auto text = f.Run(&end, &diagnostics);
        return text + (end == FiberState::Failed && !diagnostics.empty() ? " error: " + std::string(diagnostics.back().m_Error.Message()) : std::string());
    };
    auto same  = run(Value::Int32(5));
    auto other = run(Value::Float(2.5f));
    test::Check(same == "5 true 5", "a value of the type is itself, and Is: %s", same.c_str());
    test::Check(other == "0 false error: Value is of Float, not Int32", "of another, As Type gives the type's default, and Expect Type stops the fiber: %s", other.c_str());
}

TEST_CASE(Comments, "library.a_comment_never_runs_and_keeps_its_text", nullptr, test::TestFlags_None)
{
    // A comment beside Start -> Print.
    Fixture f;
    NodeConfig note;
    note.m_Data["text"] = Value::String("Greets");
    auto comment = f.Add("Comment", note), start = f.Add("Start"), print = f.Add("Print");
    f.Link(start, "then", print, "enter");
    if (auto set = f.m_Graph.SetValue(f.Pin(print, "text"), Value::String("hello")); !set)
        test::Check(false, "the text is set: %s", Message(set).c_str());
    auto printed = f.Run();
    auto node    = f.m_Graph.FindNode(comment);
    test::Check(printed == "hello" && node && node->m_Pins.empty(), "it has no pins, and the graph runs as without it: %s", printed.c_str());

    Blueprint loaded(LibraryRegistry(), std::make_shared<TypeTable>());
    auto read  = Load(loaded, Save(f.m_Blueprint));
    auto again = read && !loaded.Graphs().empty() ? loaded.Graphs()[0]->FindNode(comment) : nullptr;
    std::string text;
    if (again)
        if (auto found = again->m_Config.m_Data.find("text"); found != again->m_Config.m_Data.end())
            text = found->second.AsString();
    test::Check(text == "Greets", "and a file keeps its text: \"%s\"", text.c_str());
}

TEST_CASE(Reroutes, "library.a_reroute_passes_its_value_on_of_the_type_its_links_decide", nullptr, test::TestFlags_None)
{
    // A String Constant into a reroute, which two Prints read.
    Fixture f;
    auto start = f.Add("Start"), print = f.Add("Print"), again = f.Add("Print"), reroute = f.Add("Reroute"), text = f.Constant(Value::String("hi"));
    f.Link(start, "then", print, "enter");
    f.Link(print, "then", again, "enter");
    f.Link(text, "value", reroute, "in");
    f.Link(reroute, "out", print, "text");
    f.Link(reroute, "out", again, "text");
    auto printed = f.Run();
    test::Check(printed == "hi hi" && f.Type(reroute, "in") == "String" && f.Type(reroute, "out") == "String", "both Prints read it, and it is of String: %s", printed.c_str());

    // Unfed, it gives a warning and its type's default.
    Fixture unfed;
    auto first = unfed.Add("Start"), shows = unfed.Add("Print"), alone = unfed.Add("Reroute");
    unfed.Link(first, "then", shows, "enter");
    unfed.Link(alone, "out", shows, "text");
    auto compiled = Compile(unfed.m_Blueprint);
    auto warned   = std::any_of(compiled.m_Diagnostics.begin(), compiled.m_Diagnostics.end(), [alone](const Diagnostic& diagnostic) { return diagnostic.m_Severity == Severity::Warning && diagnostic.m_Node == alone; });
    auto empty    = unfed.Run();
    test::Check(compiled.m_Program && warned && empty.empty(), "unfed, it warns, and passes \"\": \"%s\"", empty.c_str());
}

TEST_CASE(FlowReroutes, "library.flow_goes_past_flow_reroutes_without_a_step_of_theirs", nullptr, test::TestFlags_None)
{
    // Start through two flow reroutes into Print.
    Fixture f;
    auto start = f.Add("Start"), first = f.Add("FlowReroute"), second = f.Add("FlowReroute"), print = f.Add("Print");
    f.Link(start, "then", first, "in");
    f.Link(first, "out", second, "in");
    f.Link(second, "out", print, "enter");
    if (auto set = f.m_Graph.SetValue(f.Pin(print, "text"), Value::String("hi")); !set)
        test::Check(false, "the text is set: %s", Message(set).c_str());
    auto printed  = f.Run();
    auto compiled = Compile(f.m_Blueprint);
    auto program  = compiled.m_Program;
    auto target   = -2;
    if (program)
        for (auto& pin : program->Nodes()[static_cast<size_t>(program->FindNode(start))].m_Pins)
            if (pin.m_Key == "then")
                target = pin.m_Target;
    test::Check(printed == "hi" && program && target == program->FindNode(print), "Start's flow goes on to Print itself: %s", printed.c_str());

    // A loop of reroutes alone leads nowhere.
    Fixture loop;
    auto begin = loop.Add("Start"), a = loop.Add("FlowReroute"), b = loop.Add("FlowReroute");
    loop.Link(begin, "then", a, "in");
    loop.Link(a, "out", b, "in");
    loop.Link(b, "out", a, "in");
    auto looped = Compile(loop.m_Blueprint);
    auto named  = looped.m_Diagnostics.empty() ? std::string() : std::string(looped.m_Diagnostics.back().m_Error.Message());
    test::Check(!looped.m_Program && looped.m_Diagnostics.size() == 1 && looped.m_Diagnostics.back().m_Node == begin && named.find("loop of reroutes") != std::string::npos,
        "and a loop of reroutes alone is an error, once, where Start's flow enters it: %s", named.c_str());
}
