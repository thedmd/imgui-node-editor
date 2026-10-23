//------------------------------------------------------------------------------
// blueprints2-example's runtime: programs compiled from blueprints, and the
// contexts that run them.
//------------------------------------------------------------------------------
# include "blueprints2_fixture.h"
# include <crude_json.h>
# include <algorithm>
# include <chrono>
# include <functional>
# include <memory>
# include <stdexcept>
# include <string>
# include <vector>

using namespace blueprints2_tests;

namespace {

// Steps the fiber until it isn't ready, at most so many times.
FiberState Finish(Context& context, FiberId fiber, int steps = 1000)
{
    auto state = FiberState::Ready;
    for (int i = 0; i < steps && state == FiberState::Ready; ++i)
    {
        auto stepped = context.Step(fiber);
        if (!stepped)
        {
            test::Check(false, "fiber %d steps: %s", static_cast<int>(fiber), std::string(stepped.GetError().Message()).c_str());
            return FiberState::Failed;
        }
        state = stepped.GetValue();
    }
    return state;
}

struct Logs final : ContextMonitor
{
    void OnLog(const Context&, Id node, std::string_view text) override
    {
        m_Text += (m_Text.empty() ? "" : " ") + std::string(text);
        m_Nodes.push_back(node);
    }

    std::string     m_Text;
    std::vector<Id> m_Nodes;
};

std::string Messages(std::span<const Diagnostic> diagnostics)
{
    std::string text;
    for (auto& diagnostic : diagnostics)
        text += (text.empty() ? "" : "; ") + std::string(diagnostic.m_Error.Message());
    return text;
}

template <typename T>
Result<Exit> Failed(const Result<T>& result)
{
    return result.GetError();
}

TestNodeType s_Start("Start", [](NodeDeclaration& d, const NodeConfig&, const Bindings&) { d.FlowOutput("then"); },
    [](ExecContext&, std::string_view) -> Result<Exit> { return Exit::To("then"); }, {}, "Start");

TestNodeType s_Log("Log", [](NodeDeclaration& d, const NodeConfig&, const Bindings&)
{
    d.FlowInput("enter");
    d.Input("text", "Text", StringType());
    d.FlowOutput("then");
},
[](ExecContext& context, std::string_view) -> Result<Exit>
{
    auto text = context.Get<std::string>("text");
    if (!text)
        return Failed(text);
    context.Log(text.GetValue());
    return Exit::To("then");
});

// Logs "a,b".
TestNodeType s_Twice("Twice", [](NodeDeclaration& d, const NodeConfig&, const Bindings&)
{
    d.FlowInput("enter");
    d.Input("a", "A", Int32Type());
    d.Input("b", "B", Int32Type());
    d.FlowOutput("then");
},
[](ExecContext& context, std::string_view) -> Result<Exit>
{
    auto a = context.Get<int32_t>("a");
    auto b = context.Get<int32_t>("b");
    if (!a || !b)
        return !a ? Failed(a) : Failed(b);
    context.Log(std::to_string(a.GetValue()) + "," + std::to_string(b.GetValue()));
    return Exit::To("then");
});

// Counts the times it ran, in its state.
TestNodeType s_Count("Count", [](NodeDeclaration& d, const NodeConfig&, const Bindings&)
{
    d.FlowInput("enter");
    d.FlowOutput("then");
    d.Output("count", "Count", Int32Type());
},
[](ExecContext& context, std::string_view) -> Result<Exit>
{
    auto& count = context.State<int32_t>();
    context.Set("count", Value::Int32(++count));
    return Exit::To("then");
});

int s_PlusRuns = 0;

TestNodeType s_Plus("Plus", [](NodeDeclaration& d, const NodeConfig&, const Bindings&)
{
    d.Input("a", "A", Int32Type());
    d.Input("b", "B", Int32Type(), { .m_Default = Value::Int32(1) });
    d.Output("sum", "Sum", Int32Type());
},
{}, [](EvalContext& context) -> Result<void>
{
    ++s_PlusRuns;
    auto a = context.Get<int32_t>("a");
    auto b = context.Get<int32_t>("b");
    if (!a || !b)
        return !a ? a.GetError() : b.GetError();
    context.Set("sum", Value::Int32(a.GetValue() + b.GetValue()));
    return {};
});

TestNodeType s_Text("Text", [](NodeDeclaration& d, const NodeConfig&, const Bindings&)
{
    d.Input("number", "Number", Int32Type());
    d.Output("text", "Text", StringType());
},
{}, [](EvalContext& context) -> Result<void>
{
    auto number = context.Get<int32_t>("number");
    if (!number)
        return number.GetError();
    context.Set("text", Value::String(std::to_string(number.GetValue())));
    return {};
});

// Gives the value its config holds, as a Constant does.
TestNodeType Holding(const char* title, const Type& (*type)())
{
    return TestNodeType(title, [type](NodeDeclaration& d, const NodeConfig&, const Bindings&) { d.Output("value", "Value", type()); }, {}, [type](EvalContext& context) -> Result<void>
    {
        auto value = context.Config().m_Data.find("value");
        context.Set("value", value != context.Config().m_Data.end() ? value->second : type().m_Head->Default(type()));
        return {};
    });
}

TestNodeType s_Number = Holding("Number", Int32Type);
TestNodeType s_Word   = Holding("Word", StringType);

// Runs its body "times" times, giving the index, then goes on to "done".
TestNodeType s_Repeat("Repeat", [](NodeDeclaration& d, const NodeConfig&, const Bindings&)
{
    d.FlowInput("enter");
    d.Input("times", "Times", Int32Type(), { .m_Default = Value::Int32(3) });
    d.FlowOutput("body", "Body");
    d.Output("index", "Index", Int32Type());
    d.FlowOutput("done", "Done");
},
[](ExecContext& context, std::string_view) -> Result<Exit>
{
    auto& index = context.State<int32_t>();
    index = context.IsAgain() ? index + 1 : 0;
    auto times = context.Get<int32_t>("times");
    if (!times)
        return Failed(times);
    if (index >= times.GetValue())
        return Exit::To("done");
    context.Set("index", Value::Int32(index));
    return Exit::Loop("body");
});

// Writes its output, logs what it reads, writes it again, and logs what it reads again.
TestNodeType s_Probe("Probe", [](NodeDeclaration& d, const NodeConfig&, const Bindings&)
{
    d.FlowInput("enter");
    d.Input("check", "Check", Int32Type());
    d.FlowOutput("then");
    d.Output("value", "Value", Int32Type());
},
[](ExecContext& context, std::string_view) -> Result<Exit>
{
    context.Set("value", Value::Int32(0));
    auto before = context.Get<int32_t>("check");
    context.Set("value", Value::Int32(10));
    auto after = context.Get<int32_t>("check");
    if (!before || !after)
        return !before ? Failed(before) : Failed(after);
    context.Log(std::to_string(before.GetValue()) + "->" + std::to_string(after.GetValue()));
    return Exit::To("then");
});

int s_Ticks = 0;

// A new number each time it computes, as Random gives.
TestNodeType s_Tick("Tick", [](NodeDeclaration& d, const NodeConfig&, const Bindings&) { d.Output("tick", "Tick", Int32Type()); },
    {}, [](EvalContext& context) -> Result<void>
{
    context.Set("tick", Value::Int32(++s_Ticks));
    return {};
});

void FlowThrough(NodeDeclaration& d, const NodeConfig&, const Bindings&)
{
    d.FlowInput("enter");
    d.FlowOutput("then");
}

TestNodeType s_Fail("Fail", FlowThrough, [](ExecContext&, std::string_view) -> Result<Exit> { return Error(BlueprintError::OutOfRange, "it fails"); });
TestNodeType s_Throw("Throw", FlowThrough, [](ExecContext&, std::string_view) -> Result<Exit> { throw std::runtime_error("boom"); });
TestNodeType s_Lost("Lost", FlowThrough, [](ExecContext&, std::string_view) -> Result<Exit> { return Exit::To("elsewhere"); });
TestNodeType s_Idle("Idle", FlowThrough);
// Suspends as a latent node does, but isn't one.
TestNodeType s_Pretend("Pretend", FlowThrough, [](ExecContext&, std::string_view) -> Result<Exit> { return Exit::Suspend(Wake::At(0.0)); });

// Waits so many seconds on the context's clock.
TestNodeType s_Sleep("Sleep", [](NodeDeclaration& d, const NodeConfig&, const Bindings&)
{
    d.FlowInput("enter");
    d.Input("seconds", "Seconds", FloatType(), { .m_Default = Value::Float(1.0f) });
    d.FlowOutput("then");
},
[](ExecContext& context, std::string_view) -> Result<Exit>
{
    if (context.IsResumed())
        return Exit::To("then");
    auto seconds = context.Get<float>("seconds");
    if (!seconds)
        return Failed(seconds);
    return Exit::Suspend(Wake::At(context.Time() + seconds.GetValue()));
}, {}, {}, true);

// Waits for the signal it's given.
TestNodeType s_Await("Await", [](NodeDeclaration& d, const NodeConfig&, const Bindings&)
{
    d.FlowInput("enter");
    d.Input("signal", "Signal", StringType());
    d.FlowOutput("then");
},
[](ExecContext& context, std::string_view) -> Result<Exit>
{
    if (context.IsResumed())
        return Exit::To("then");
    auto signal = context.Get<std::string>("signal");
    if (!signal)
        return Failed(signal);
    return Exit::Suspend(Wake::OnSignal(signal.GetValue()));
}, {}, {}, true);

TestNodeType s_Broken("Broken", [](NodeDeclaration& d, const NodeConfig&, const Bindings&) { d.Output("text", "Text", StringType()); },
    {}, [](EvalContext&) -> Result<void> { return Error(BlueprintError::NotFinite, "it can't compute"); });

TestNodeType s_Pass("Pass", [](NodeDeclaration& d, const NodeConfig&, const Bindings&)
{
    auto T = d.Variable("T");
    d.Input("in", "In", T);
    d.Output("out", "Out", T);
});

TestNodeType s_Need("Need", [](NodeDeclaration& d, const NodeConfig&, const Bindings&)
{
    d.FlowInput("enter");
    d.Input("text", "Text", StringType(), { .m_Requirement = Requirement::Required });
    d.FlowOutput("then");
});

TestNodeType s_Hint("Hint", [](NodeDeclaration& d, const NodeConfig&, const Bindings&)
{
    d.FlowInput("enter");
    d.Input("text", "Text", StringType(), { .m_Requirement = Requirement::Recommended });
    d.FlowOutput("then");
},
[](ExecContext&, std::string_view) -> Result<Exit> { return Exit::To("then"); });

StructType s_Point("test.Point", { { "x", &Int32Type() }, { "y", &Int32Type() } });
StructType s_Box("test.Box", { { "min", &s_Point.AsType() }, { "max", &s_Point.AsType() } });

// Moves a point by as much on both axes.
TestNodeType s_Shift("Shift", [](NodeDeclaration& d, const NodeConfig&, const Bindings&)
{
    d.Input("point", "Point", s_Point.AsType());
    d.Input("by", "By", Int32Type());
    d.Output("moved", "Moved", s_Point.AsType());
},
{}, [](EvalContext& context) -> Result<void>
{
    auto point = context.Get("point");
    auto by    = context.Get<int32_t>("by");
    if (!point || !by)
        return !point ? point.GetError() : by.GetError();
    auto fields = point.GetValue().AsStruct();
    context.Set("moved", Value::Struct(s_Point.AsType(), { Value::Int32(fields[0].AsInt32() + by.GetValue()), Value::Int32(fields[1].AsInt32() + by.GetValue()) }));
    return {};
});

// Logs "x,y" of a point, and the width of a box.
TestNodeType s_Show("Show", [](NodeDeclaration& d, const NodeConfig&, const Bindings&)
{
    d.FlowInput("enter");
    d.Input("point", "Point", s_Point.AsType());
    d.Input("box", "Box", s_Box.AsType());
    d.FlowOutput("then");
},
[](ExecContext& context, std::string_view) -> Result<Exit>
{
    auto point = context.Get("point");
    auto box   = context.Get("box");
    if (!point || !box)
        return !point ? Failed(point) : Failed(box);
    auto xy     = point.GetValue().AsStruct();
    auto bounds = box.GetValue().AsStruct();
    context.Log(std::to_string(xy[0].AsInt32()) + "," + std::to_string(xy[1].AsInt32()) + " " + std::to_string(bounds[1].AsStruct()[0].AsInt32() - bounds[0].AsStruct()[0].AsInt32()));
    return Exit::To("then");
});

// A service of the host's, which its nodes count on, and another.
struct Tally final : Service
{
    CRUDE_BP_RTTI(Tally, Service)

    int m_Count = 0;
};

struct Elsewhere final : Service
{
    CRUDE_BP_RTTI(Elsewhere, Service)
};

TestNodeType s_Counted("Counted", [](NodeDeclaration& d, const NodeConfig&, const Bindings&)
{
    d.FlowInput("enter");
    d.FlowOutput("then");
},
[](ExecContext& context, std::string_view) -> Result<Exit>
{
    auto tally = context.FindService<Tally>();
    if (!tally)
        return Error(BlueprintError::Unsupported, "no tally");
    ++tally->m_Count;
    return Exit::To("then");
});

std::shared_ptr<const Registry> ContextRegistry()
{
    static const std::shared_ptr<const Registry> registry = []
    {
        auto made = std::make_shared<Registry>();
        auto ok = RegisterBuiltins(*made).IsOk();
        for (auto type : { &s_Start, &s_Log, &s_Twice, &s_Count, &s_Plus, &s_Text, &s_Number, &s_Word, &s_Repeat, &s_Fail, &s_Throw, &s_Lost, &s_Idle,
                           &s_Pretend, &s_Sleep, &s_Await, &s_Broken, &s_Pass, &s_Need, &s_Hint, &s_Probe, &s_Tick, &s_Shift, &s_Show, &s_Counted })
            ok = made->AddNodeType(*type).IsOk() && ok;
        if (!ok)
            test::Check(false, "the test registry is made");
        return made;
    }();
    return registry;
}

struct Fixture : BlueprintFixture
{
    Fixture(): BlueprintFixture(ContextRegistry()) {}

    // One that isn't made fails the test.
    std::shared_ptr<const Program> Compiled() const
    {
        auto compiled = Compile(m_Blueprint);
        if (!compiled.m_Program)
            test::Check(false, "the blueprint compiles: %s", Messages(compiled.m_Diagnostics).c_str());
        return compiled.m_Program;
    }

    void SetValue(Id node, const char* input, Value value)
    {
        if (auto set = m_Graph.SetValue(Pin(node, input), std::move(value)); !set)
            test::Check(false, "%s.%s takes the value: %s", Name(node).c_str(), input, std::string(set.GetError().Message()).c_str());
    }
};

NodeConfig Holds(Value value)
{
    NodeConfig config;
    config.m_Data["value"] = std::move(value);
    return config;
}

NodeConfig Event(const char* name)
{
    NodeConfig config;
    config.m_Data["event"] = Value::String(name);
    return config;
}

} // namespace

TEST_CASE(FiberRuns, "context.a_fiber_runs_from_its_event_along_flow_links", nullptr, test::TestFlags_None)
{
    Fixture f;
    auto start = f.Add("Start"), hello = f.Add("Log"), world = f.Add("Log");
    auto first = f.Link(start, "then", hello, "enter");
    f.Link(hello, "then", world, "enter");
    f.SetValue(hello, "text", Value::String("hello"));
    f.SetValue(world, "text", Value::String("world"));
    auto program = f.Compiled();
    if (!program)
        return;

    Context context(program);
    Logs    logs;
    context.SetMonitor(&logs);
    auto fiber = context.Start("Start");
    test::Check(fiber && context.Fibers().size() == 1 && context.Fibers()[0].m_Node == start, "a fiber starts at its event");
    auto stepped = context.Step(fiber.ValueOr(0));
    test::Check(stepped && stepped.GetValue() == FiberState::Ready && context.Fibers()[0].m_Node == hello && logs.m_Text.empty(), "a step runs the event, and the fiber's next node follows its flow link");
    test::Check(context.Fibers()[0].m_Link == first, "which the fiber says it took");
    auto end = Finish(context, fiber.ValueOr(0));
    test::Check(end == FiberState::Done && logs.m_Text == "hello world" && context.StepCount() == 3, "it runs to the end: %s, %d steps", logs.m_Text.c_str(), static_cast<int>(context.StepCount()));
    test::Check(logs.m_Nodes == std::vector<Id>{ hello, world } && context.Fibers()[0].m_Node == 0 && context.Fibers()[0].m_Link == 0, "each log names its node, and a fiber that's done has no next node, through no link");
    auto after   = context.Step(fiber.ValueOr(0));
    auto unknown = context.Start("Stop");
    test::Check(!after && after.GetError().Is(BlueprintError::Unsupported) && !unknown && unknown.GetError().Is(BlueprintError::NotFound), "a fiber that's done takes no step, and no node starts Stop: %s", Message(unknown).c_str());
}

TEST_CASE(Services, "context.a_node_finds_the_hosts_service_by_its_class", nullptr, test::TestFlags_None)
{
    // Start, then two nodes that count on the host's tally.
    Fixture f;
    auto start = f.Add("Start"), first = f.Add("Counted"), second = f.Add("Counted");
    f.Link(start, "then", first, "enter");
    f.Link(first, "then", second, "enter");
    auto program = f.Compiled();
    if (!program)
        return;

    Tally     tally;
    Elsewhere elsewhere;
    Context   given(program);
    given.AddService(elsewhere);
    given.AddService(tally);
    auto counted = Finish(given, given.Start("Start").ValueOr(0));
    test::Check(counted == FiberState::Done && tally.m_Count == 2, "a context's nodes find the tally among its services: %d", tally.m_Count);

    Context without(program);
    without.AddService(elsewhere);
    auto failed = Finish(without, without.Start("Start").ValueOr(0));
    test::Check(failed == FiberState::Failed && Messages(without.Diagnostics()) == "no tally" && tally.m_Count == 2, "and one without fails its fiber: %s", Messages(without.Diagnostics()).c_str());
}

TEST_CASE(ForgottenFibers, "context.a_fiber_that_ended_is_forgotten_and_one_that_hasnt_stays", nullptr, test::TestFlags_None)
{
    // Start sleeps; Ping logs, and Fail fails.
    Fixture f;
    auto start = f.Add("Start"), sleep = f.Add("Sleep"), ping = f.Add("Start", Event("Ping")), log = f.Add("Log"), fails = f.Add("Start", Event("Fails")), fail = f.Add("Fail");
    f.Link(start, "then", sleep, "enter");
    f.Link(ping, "then", log, "enter");
    f.Link(fails, "then", fail, "enter");
    f.SetValue(log, "text", Value::String("pong"));
    auto program = f.Compiled();
    if (!program)
        return;

    Context context(program);
    auto sleeping = context.Start("Start"), pinged = context.Start("Ping"), failed = context.Start("Fails");
    context.Run(std::chrono::seconds(5));
    for (auto fiber : { sleeping, pinged, failed })
        context.Forget(fiber.ValueOr(0));
    auto fibers = context.Fibers();
    auto again  = context.Start("Ping");
    test::Check(fibers.size() == 1 && fibers[0].m_Id == sleeping.ValueOr(0) && fibers[0].m_State == FiberState::Suspended, "the done and the failed fibers go, and the sleeping one stays: %d fibers", static_cast<int>(fibers.size()));
    test::Check(again && again.GetValue() > failed.ValueOr(0) && context.Diagnostics().size() == 1, "a new fiber takes a new ID, and the failure's diagnostic stays");
}

TEST_CASE(InputsWithoutLinks, "context.unlinked_inputs_read_typed_values_then_defaults", nullptr, test::TestFlags_None)
{
    Fixture f;
    auto start = f.Add("Start"), typed = f.Add("Log"), plain = f.Add("Log"), sum = f.Add("Log"), plus = f.Add("Plus"), text = f.Add("Text");
    f.Link(start, "then", typed, "enter");
    f.Link(typed, "then", plain, "enter");
    f.Link(plain, "then", sum, "enter");
    f.Link(plus, "sum", text, "number");
    f.Link(text, "text", sum, "text");
    f.SetValue(typed, "text", Value::String("typed"));

    auto run = [&f]
    {
        Logs logs;
        if (auto program = f.Compiled())
        {
            Context context(program);
            context.SetMonitor(&logs);
            Finish(context, context.Start("Start").ValueOr(0));
        }
        return logs.m_Text;
    };
    auto before = run();
    test::Check(before == "typed  1", "a typed value, then the type's default, then a declared one: Plus is 0 + 1: [%s]", before.c_str());
    f.SetValue(plus, "a", Value::Int32(41));
    auto after = run();
    test::Check(after == "typed  42", "a program takes the values typed when it's compiled: [%s]", after.c_str());
}

TEST_CASE(PureOnce, "context.a_pure_output_is_computed_once_for_what_it_reads", nullptr, test::TestFlags_None)
{
    Fixture f;
    auto start = f.Add("Start"), twice = f.Add("Twice"), plus = f.Add("Plus");
    f.Link(start, "then", twice, "enter");
    f.Link(plus, "sum", twice, "a");
    f.Link(plus, "sum", twice, "b");
    auto program = f.Compiled();
    if (!program)
        return;
    Context context(program);
    Logs    logs;
    context.SetMonitor(&logs);
    s_PlusRuns = 0;
    Finish(context, context.Start("Start").ValueOr(0));
    test::Check(logs.m_Text == "1,1" && s_PlusRuns == 1, "two inputs read Plus, which computes once: %s, %d runs", logs.m_Text.c_str(), s_PlusRuns);

    // A node with flow pins that writes an output makes what reads it compute again.
    Fixture g;
    auto start2 = g.Add("Start"), repeat = g.Add("Repeat"), count = g.Add("Count"), twice2 = g.Add("Twice"), plus2 = g.Add("Plus");
    g.Link(start2, "then", repeat, "enter");
    g.Link(repeat, "body", count, "enter");
    g.Link(count, "then", twice2, "enter");
    g.Link(count, "count", twice2, "a");
    g.Link(count, "count", plus2, "a");
    g.Link(plus2, "sum", twice2, "b");
    g.SetValue(repeat, "times", Value::Int32(2));
    auto program2 = g.Compiled();
    if (!program2)
        return;
    Context context2(program2);
    Logs    logs2;
    context2.SetMonitor(&logs2);
    s_PlusRuns = 0;
    Finish(context2, context2.Start("Start").ValueOr(0));
    test::Check(logs2.m_Text == "1,2 2,3" && s_PlusRuns == 2, "each count gives a new sum: %s, %d runs", logs2.m_Text.c_str(), s_PlusRuns);
}

TEST_CASE(Generations, "context.pure_outputs_compute_again_after_a_write_or_in_a_new_step", nullptr, test::TestFlags_None)
{
    // Probe reads Plus of its own output, written once and then again.
    Fixture f;
    auto start = f.Add("Start"), probe = f.Add("Probe"), plus = f.Add("Plus");
    f.Link(start, "then", probe, "enter");
    f.Link(probe, "value", plus, "a");
    f.Link(plus, "sum", probe, "check");
    if (auto program = f.Compiled())
    {
        Context context(program);
        Logs    logs;
        context.SetMonitor(&logs);
        Finish(context, context.Start("Start").ValueOr(0));
        test::Check(logs.m_Text == "1->11", "a write in the step makes Plus compute again: %s", logs.m_Text.c_str());
    }

    // Two steps read one Tick, which has no inputs.
    Fixture g;
    auto start2 = g.Add("Start"), first = g.Add("Log"), second = g.Add("Log"), tick = g.Add("Tick"), text = g.Add("Text");
    g.Link(start2, "then", first, "enter");
    g.Link(first, "then", second, "enter");
    g.Link(tick, "tick", text, "number");
    g.Link(text, "text", first, "text");
    g.Link(text, "text", second, "text");
    if (auto program = g.Compiled())
    {
        Context context(program);
        Logs    logs;
        context.SetMonitor(&logs);
        s_Ticks = 0;
        Finish(context, context.Start("Start").ValueOr(0));
        test::Check(logs.m_Text == "1 2", "each step computes it once: %s", logs.m_Text.c_str());
    }
}

TEST_CASE(CastsRun, "context.a_cast_link_converts_and_a_failing_cast_stops_the_fiber", nullptr, test::TestFlags_None)
{
    Fixture f;
    auto start = f.Add("Start"), log = f.Add("Log"), number = f.Add("Number", Holds(Value::Int32(42)));
    f.Link(start, "then", log, "enter");
    f.Link(number, "value", log, "text", LinkMode::Cast);
    if (auto program = f.Compiled())
    {
        Context context(program);
        Logs    logs;
        context.SetMonitor(&logs);
        auto end = Finish(context, context.Start("Start").ValueOr(0));
        test::Check(end == FiberState::Done && logs.m_Text == "42", "Int32 -> String gives the text: %s", logs.m_Text.c_str());
    }

    Fixture g;
    auto start2 = g.Add("Start"), repeat = g.Add("Repeat"), word = g.Add("Word", Holds(Value::String("abc")));
    g.Link(start2, "then", repeat, "enter");
    auto cast = g.Link(word, "value", repeat, "times", LinkMode::Cast);
    if (auto program = g.Compiled())
    {
        Context context(program);
        auto end = Finish(context, context.Start("Start").ValueOr(0));
        auto diagnostics = context.Diagnostics();
        test::Check(end == FiberState::Failed && diagnostics.size() == 1 && diagnostics[0].m_Error.Is(BlueprintError::CastFailed), "String -> Int32 of \"abc\" fails the fiber: %s", Messages(diagnostics).c_str());
        test::Check(!diagnostics.empty() && diagnostics[0].m_Link == cast && diagnostics[0].m_Node == repeat, "at the link, on the node that reads it");
    }
}

TEST_CASE(ErrorStopsItsFiber, "context.a_run_time_error_stops_only_its_fiber", nullptr, test::TestFlags_None)
{
    Fixture f;
    auto start = f.Add("Start"), fail = f.Add("Fail"), other = f.Add("Start", Event("Other")), log = f.Add("Log");
    f.Link(start, "then", fail, "enter");
    f.Link(other, "then", log, "enter");
    f.SetValue(log, "text", Value::String("still"));
    auto program = f.Compiled();
    if (!program)
        return;
    Context context(program);
    Logs    logs;
    context.SetMonitor(&logs);
    auto failing = context.Start("Start");
    auto running = context.Start("Other");
    auto ready   = context.Run(std::chrono::seconds(1));
    auto fibers  = context.Fibers();
    auto diagnostics = context.Diagnostics();
    test::Check(failing && running && ready == RunState::Idle && fibers.size() == 2 && fibers[0].m_State == FiberState::Failed && fibers[1].m_State == FiberState::Done, "one fiber fails, and the other runs to its end");
    test::Check(logs.m_Text == "still" && diagnostics.size() == 1 && diagnostics[0].m_Node == fail && diagnostics[0].m_Error.Is(BlueprintError::OutOfRange) && diagnostics[0].m_Graph == f.m_Graph.GetId(), "the error is a diagnostic on its node: %s", Messages(diagnostics).c_str());
    test::Check(fibers[0].m_Node == fail, "and the failed fiber stays at that node");
}

TEST_CASE(ErrorsLocated, "context.exceptions_and_errors_of_pure_inputs_are_errors_where_they_arose", nullptr, test::TestFlags_None)
{
    Fixture f;
    auto start = f.Add("Start"), thrower = f.Add("Throw");
    f.Link(start, "then", thrower, "enter");
    if (auto program = f.Compiled())
    {
        Context context(program);
        auto end = Finish(context, context.Start("Start").ValueOr(0));
        auto diagnostics = context.Diagnostics();
        test::Check(end == FiberState::Failed && diagnostics.size() == 1 && diagnostics[0].m_Error.Is(BlueprintError::Threw) && Messages(diagnostics) == "boom" && diagnostics[0].m_Node == thrower, "what node code throws stops its fiber: %s", Messages(diagnostics).c_str());
    }

    Fixture g;
    auto start2 = g.Add("Start"), log = g.Add("Log"), broken = g.Add("Broken");
    g.Link(start2, "then", log, "enter");
    g.Link(broken, "text", log, "text");
    if (auto program = g.Compiled())
    {
        Context context(program);
        auto end = Finish(context, context.Start("Start").ValueOr(0));
        auto diagnostics = context.Diagnostics();
        test::Check(end == FiberState::Failed && diagnostics.size() == 1 && diagnostics[0].m_Node == broken && diagnostics[0].m_Error.Is(BlueprintError::NotFinite), "a pure node's error is on that node, not on the node that read it: %s", Messages(diagnostics).c_str());
    }
}

TEST_CASE(Loops, "context.a_loop_runs_its_branch_then_enters_its_node_again", nullptr, test::TestFlags_None)
{
    Fixture f;
    auto start = f.Add("Start"), repeat = f.Add("Repeat"), log = f.Add("Log"), text = f.Add("Text"), done = f.Add("Log");
    f.Link(start, "then", repeat, "enter");
    f.Link(repeat, "body", log, "enter");
    f.Link(repeat, "index", text, "number");
    f.Link(text, "text", log, "text");
    f.Link(repeat, "done", done, "enter");
    f.SetValue(done, "text", Value::String("done"));
    if (auto program = f.Compiled())
    {
        Context context(program);
        Logs    logs;
        context.SetMonitor(&logs);
        Finish(context, context.Start("Start").ValueOr(0));
        test::Check(logs.m_Text == "0 1 2 done", "the body runs for each index, then done: %s", logs.m_Text.c_str());
    }

    Fixture g;
    auto start2 = g.Add("Start"), outer = g.Add("Repeat"), inner = g.Add("Repeat"), log2 = g.Add("Log"), text2 = g.Add("Text"), bar = g.Add("Log");
    g.Link(start2, "then", outer, "enter");
    g.Link(outer, "body", inner, "enter");
    g.Link(inner, "body", log2, "enter");
    g.Link(inner, "index", text2, "number");
    g.Link(text2, "text", log2, "text");
    g.Link(inner, "done", bar, "enter");
    g.SetValue(outer, "times", Value::Int32(2));
    g.SetValue(inner, "times", Value::Int32(2));
    g.SetValue(bar, "text", Value::String("|"));
    if (auto program = g.Compiled())
    {
        Context context(program);
        Logs    logs;
        context.SetMonitor(&logs);
        auto end = Finish(context, context.Start("Start").ValueOr(0));
        test::Check(end == FiberState::Done && logs.m_Text == "0 1 | 0 1 |", "loops nest: %s", logs.m_Text.c_str());
    }
}

TEST_CASE(CompileRefuses, "context.compiling_refuses_what_cant_run", nullptr, test::TestFlags_None)
{
    struct Case
    {
        const char*                   what;
        std::function<void(Fixture&)> build;
        BlueprintError                code;
    };
    const Case cases[] =
    {
        { "an undecided type", [](Fixture& f) { f.Add("Pass"); }, BlueprintError::Undecided },
        { "a required pin without a link", [](Fixture& f) { f.Add("Need"); }, BlueprintError::NeedsLink },
        { "two nodes that start one event", [](Fixture& f) { f.Add("Start"); f.Add("Start"); }, BlueprintError::Duplicate },
        { "a disabled link", [](Fixture& f) { auto number = f.Add("Number"), log = f.Add("Log"); f.Link(number, "value", log, "text", LinkMode::Keep); }, BlueprintError::TypeMismatch },
    };
    for (auto& c : cases)
    {
        Fixture f;
        c.build(f);
        auto compiled = Compile(f.m_Blueprint);
        auto found = std::any_of(compiled.m_Diagnostics.begin(), compiled.m_Diagnostics.end(), [&c](const Diagnostic& d) { return d.m_Error.Is(c.code) && d.m_Severity == Severity::Error; });
        test::Check(!compiled.m_Program && found, "%s is refused: %s", c.what, Messages(compiled.m_Diagnostics).c_str());
    }

    // A loop that a file can hold: the error names a node on it, not the reader the search starts at.
    Fixture f;
    auto reader = f.Add("Text"), first = f.Add("Plus"), second = f.Add("Plus");
    f.Link(first, "sum", second, "a");
    f.Link(second, "sum", first, "a", LinkMode::Keep);
    f.Link(second, "sum", reader, "number");
    auto compiled = Compile(f.m_Blueprint);
    auto loops = std::count_if(compiled.m_Diagnostics.begin(), compiled.m_Diagnostics.end(), [](const Diagnostic& d) { return d.m_Error.Is(BlueprintError::Loop); });
    auto named = std::any_of(compiled.m_Diagnostics.begin(), compiled.m_Diagnostics.end(), [&](const Diagnostic& d) { return d.m_Error.Is(BlueprintError::Loop) && (d.m_Node == first || d.m_Node == second); });
    test::Check(!compiled.m_Program && loops == 1 && named, "a kept loop of pure nodes is refused once, on the loop: %s", Messages(compiled.m_Diagnostics).c_str());

    Fixture g;
    auto start = g.Add("Start"), hint = g.Add("Hint");
    g.Link(start, "then", hint, "enter");
    auto warned = Compile(g.m_Blueprint);
    test::Check(warned.m_Program && warned.m_Diagnostics.size() == 1 && warned.m_Diagnostics[0].m_Severity == Severity::Warning, "warnings alone don't stop it, and come with the program: %s", Messages(warned.m_Diagnostics).c_str());
}

TEST_CASE(Debugger, "context.the_debugger_reads_any_pin_without_running_a_node", nullptr, test::TestFlags_None)
{
    // A second Plus reads only its constants.
    Fixture f;
    auto start = f.Add("Start"), count = f.Add("Count"), log = f.Add("Log"), plus = f.Add("Plus"), text = f.Add("Text"), alone = f.Add("Plus");
    f.Link(start, "then", count, "enter");
    f.Link(count, "then", log, "enter");
    f.Link(count, "count", plus, "a");
    f.Link(plus, "sum", text, "number");
    f.Link(text, "text", log, "text");
    auto program = f.Compiled();
    if (!program)
        return;
    Context context(program);

    auto stored = context.Evaluate(f.Pin(count, "count"));
    auto sum    = context.Evaluate(f.Pin(plus, "sum"));
    test::Check(!stored && stored.GetError().Is(BlueprintError::NotRun) && stored.GetError().Message() == "Count#" + std::to_string(count) + ".count is undefined: nothing wrote it yet",
        "before a run, an output a node of flow hasn't written is undefined: %s", Message(stored).c_str());
    test::Check(!sum && sum.GetError().Is(BlueprintError::NotRun), "and so is what a pure node computes from it: %s", Message(sum).c_str());

    s_PlusRuns = 0;
    auto constants = context.Evaluate(f.Pin(alone, "sum"));
    auto again     = context.Evaluate(f.Pin(alone, "sum"));
    test::Check(constants && constants.GetValue().m_Value == Value::Int32(1) && constants.GetValue().m_Step == 0u && again && s_PlusRuns == 1 && context.StepCount() == 0,
        "a pure output of constants is computed once, before any step, and nothing runs: %d runs", s_PlusRuns);

    auto fiber = context.Start("Start");
    auto event = context.Step(fiber.ValueOr(0));
    auto run   = context.Step(fiber.ValueOr(0));
    auto counted = context.Evaluate(f.Pin(count, "count"));
    auto input   = context.Evaluate(f.Pin(log, "text"));
    auto constant = context.Evaluate(f.Pin(plus, "b"));
    test::Check(event && run && counted && counted.GetValue().m_Value == Value::Int32(1) && counted.GetValue().m_Step == 2, "an output gives what its node stored, and when");
    test::Check(input && input.GetValue().m_Value == Value::String("2") && constant && constant.GetValue().m_Value == Value::Int32(1) && !constant.GetValue().m_Step, "an input gives what it reads, or its constant, of no step");

    auto flow    = context.Evaluate(f.Pin(count, "then"));
    auto unknown = context.Evaluate(99999);
    test::Check(!flow && !unknown && unknown.GetError().Is(BlueprintError::NotFound), "a flow pin has no value: %s", Message(flow).c_str());
}

// Start -> Log(Text(Plus(Plus(...)))), each Plus adding 1: as a file holds it, for chains longer than
// edits make in good time.
crude_json::value Chain(int length, bool loop)
{
    crude_json::array nodes, links;
    Id next = 2;
    auto node = [&nodes, &next](const char* type, std::initializer_list<std::pair<const char*, const char*>> pins)
    {
        crude_json::object saved, keys;
        saved["id"]   = next++;
        saved["type"] = std::string("test.") + type;
        for (auto& [key, direction] : pins)
        {
            crude_json::object pin;
            pin["id"]  = next++;
            pin["dir"] = direction;
            if (std::string_view(type) == "Start" || std::string_view(key) == "enter" || std::string_view(key) == "then")
                pin["flow"] = true;
            keys[key] = std::move(pin);
        }
        saved["pins"] = std::move(keys);
        nodes.push_back(std::move(saved));
        return next - 1 - static_cast<Id>(pins.size());
    };
    auto link = [&links, &next](Id from, Id to)
    {
        crude_json::object saved;
        saved["id"]   = next++;
        saved["from"] = from;
        saved["to"]   = to;
        links.push_back(std::move(saved));
    };

    auto start = node("Start", { { "then", "out" } });
    auto log   = node("Log", { { "enter", "in" }, { "text", "in" }, { "then", "out" } });
    auto text  = node("Text", { { "number", "in" }, { "text", "out" } });
    link(start + 1, log + 1);
    link(text + 2, log + 2);
    Id first = 0, last = 0;
    for (int i = 0; i < length; ++i)
    {
        auto plus = node("Plus", { { "a", "in" }, { "b", "in" }, { "sum", "out" } });
        if (i == 0)
            first = plus;
        else
            link(last + 3, plus + 1);
        last = plus;
    }
    link(last + 3, text + 1);
    if (loop)
        link(last + 3, first + 2);

    crude_json::object graph, file;
    graph["id"]    = 1;
    graph["kind"]  = "event";
    graph["name"]  = "Main";
    graph["nodes"] = std::move(nodes);
    graph["links"] = std::move(links);
    file["format"]  = "crude_blueprint";
    file["version"] = 2;
    file["next_id"] = next;
    file["graphs"]  = crude_json::array{ std::move(graph) };
    return file;
}

TEST_CASE(DeepChains, "context.chains_deeper_than_the_stack_fail_their_fiber_and_dont_crash", nullptr, test::TestFlags_None)
{
    for (auto length : { 100, 20000 })
    {
        Blueprint blueprint(ContextRegistry(), std::make_shared<TypeTable>());
        auto loaded = Load(blueprint, Chain(length, false));
        auto compiled = loaded ? Compile(blueprint) : CompileResult{};
        if (!compiled.m_Program)
        {
            test::Check(false, "a chain of %d compiles: %s %s", length, Message(loaded).c_str(), Messages(compiled.m_Diagnostics).c_str());
            continue;
        }
        Context context(compiled.m_Program);
        Logs    logs;
        context.SetMonitor(&logs);
        auto end = Finish(context, context.Start("Start").ValueOr(0));
        auto diagnostics = context.Diagnostics();
        if (length == 100)
            test::Check(end == FiberState::Done && logs.m_Text == "100", "a chain of 100 computes: %s", logs.m_Text.c_str());
        else
            test::Check(end == FiberState::Failed && diagnostics.size() == 1 && diagnostics[0].m_Error.Is(BlueprintError::TooDeep), "a chain of 20000 fails its fiber: %s", Messages(diagnostics).c_str());
    }

    Blueprint looped(ContextRegistry(), std::make_shared<TypeTable>());
    auto loaded   = Load(looped, Chain(20000, true));
    auto compiled = loaded ? Compile(looped) : CompileResult{};
    auto refused  = loaded && !compiled.m_Program && std::any_of(compiled.m_Diagnostics.begin(), compiled.m_Diagnostics.end(), [](const Diagnostic& d) { return d.m_Error.Is(BlueprintError::Loop); });
    test::Check(refused, "and a loop at the end of one is found: %s", Messages(compiled.m_Diagnostics).c_str());
}

TEST_CASE(RunAndStop, "context.run_steps_fibers_until_they_end_or_the_time_is_up", nullptr, test::TestFlags_None)
{
    Fixture f;
    auto start = f.Add("Start"), first = f.Add("Count"), second = f.Add("Count");
    f.Link(start, "then", first, "enter");
    f.Link(first, "then", second, "enter");
    f.Link(second, "then", first, "enter");
    auto program = f.Compiled();
    if (!program)
        return;
    Context context(program);
    auto fiber   = context.Start("Start");
    auto running = context.Run(std::chrono::milliseconds(2));
    test::Check(running == RunState::Running && context.StepCount() > 2, "an endless fiber is still ready once the time is up: %d steps", static_cast<int>(context.StepCount()));
    context.Stop(fiber.ValueOr(0));
    auto stopped = context.Run(std::chrono::seconds(1));
    test::Check(stopped == RunState::Idle && context.Fibers()[0].m_State == FiberState::Stopped && !context.Step(fiber.ValueOr(0)), "stopped, it takes no more steps");
}

TEST_CASE(Breakpoints, "context.run_stops_before_a_node_with_a_breakpoint_and_step_doesnt", nullptr, test::TestFlags_None)
{
    // Start, then Count and a Log in a loop, with a breakpoint on the Log.
    Fixture f;
    auto start = f.Add("Start"), count = f.Add("Count"), log = f.Add("Log");
    f.Link(start, "then", count, "enter");
    f.Link(count, "then", log, "enter");
    f.Link(log, "then", count, "enter");
    f.SetValue(log, "text", Value::String("a"));
    auto program = f.Compiled();
    if (!program)
        return;
    Context context(program);
    Logs    logs;
    context.SetMonitor(&logs);
    auto fiber = context.Start("Start").ValueOr(0);
    context.SetBreakpoint(log, true);

    auto first = context.Run(std::chrono::seconds(1));
    auto info  = context.Fibers()[0];
    test::Check(first == RunState::Breakpoint && info.m_Node == log && info.m_Ran == count && info.m_AtBreakpoint && logs.m_Text.empty(), "Run stops before the Log, once Count ran");
    auto second = context.Run(std::chrono::seconds(1));
    test::Check(second == RunState::Breakpoint && logs.m_Text == "a" && context.Fibers()[0].m_Node == log, "run again, the Log runs, and the loop stops before it again: %s", logs.m_Text.c_str());

    auto stepped = context.Step(fiber);
    auto ran     = context.Fibers()[0];
    test::Check(stepped && logs.m_Text == "a a" && ran.m_Ran == log && !ran.m_AtBreakpoint, "Step runs the Log, breakpoint or not");
    test::Check(context.Step(fiber) && context.Step(fiber) && logs.m_Text == "a a a", "and steps through it each time round");

    context.SetBreakpoint(log, false);
    auto running = context.Run(std::chrono::milliseconds(2));
    test::Check(running == RunState::Running && context.StepCount() > 10, "without the breakpoint, the loop runs on until the time is up: %d steps", static_cast<int>(context.StepCount()));
    context.SetBreakpoint(log, true);
    auto again = context.Run(std::chrono::seconds(1));
    context.Stop(fiber);
    test::Check(again == RunState::Breakpoint && !context.Fibers()[0].m_AtBreakpoint && context.Run(std::chrono::seconds(1)) == RunState::Idle, "stopped there, a fiber is at no breakpoint");
}

TEST_CASE(Trace, "context.the_trace_keeps_the_flow_links_the_last_steps_took", nullptr, test::TestFlags_None)
{
    // Start, then Count and a Log in a loop; another Start's Log leads nowhere.
    Fixture f;
    auto start = f.Add("Start"), count = f.Add("Count"), log = f.Add("Log"), other = f.Add("Start", Event("Other")), last = f.Add("Log");
    auto into  = f.Link(start, "then", count, "enter");
    auto ahead = f.Link(count, "then", log, "enter");
    auto back  = f.Link(log, "then", count, "enter");
    auto end   = f.Link(other, "then", last, "enter");
    auto program = f.Compiled();
    if (!program)
        return;
    Context context(program);
    auto looping = context.Start("Start").ValueOr(0);
    auto ending  = context.Start("Other").ValueOr(0);
    auto stepped = true;
    for (auto fiber : { looping, looping, looping, ending, ending })
        stepped = context.Step(fiber).IsOk() && stepped;

    auto trace = context.Trace();
    test::Check(stepped && trace.size() == 4, "of five steps, four took a link, and the last Log's took none: %d", static_cast<int>(trace.size()));
    if (trace.size() != 4)
        return;
    test::Check(trace[0].m_Link == into && trace[1].m_Link == ahead && trace[2].m_Link == back && trace[3].m_Link == end, "each link is there, the oldest first");
    test::Check(trace[0].m_Step == 1 && trace[2].m_Step == 3 && trace[3].m_Step == 4 && trace[2].m_Fiber == looping && trace[3].m_Fiber == ending, "with its step and its fiber");

    for (int i = 0; i < 100; ++i)
        stepped = context.Step(looping).IsOk() && stepped;
    trace = context.Trace();
    test::Check(stepped && trace.size() == Context::c_TraceLength && trace.back().m_Step == context.StepCount() && trace.front().m_Step == context.StepCount() - Context::c_TraceLength + 1,
        "after many steps, it keeps the last %d", static_cast<int>(Context::c_TraceLength));
}

TEST_CASE(Patching, "context.a_run_goes_on_with_a_program_of_other_values", nullptr, test::TestFlags_None)
{
    // Start, then Count and a Twice in a loop. Twice logs its a, from a Number, and its b, typed in;
    // another Number goes unused.
    Fixture f;
    auto start = f.Add("Start"), count = f.Add("Count"), twice = f.Add("Twice"), number = f.Add("Number", Holds(Value::Int32(1)));
    auto other = f.Add("Number", Holds(Value::Int32(9)));
    f.Link(start, "then", count, "enter");
    f.Link(count, "then", twice, "enter");
    f.Link(twice, "then", count, "enter");
    f.Link(number, "value", twice, "a");
    f.SetValue(twice, "b", Value::Int32(2));
    auto program = f.Compiled();
    if (!program)
        return;
    Context context(program);
    Logs    logs;
    context.SetMonitor(&logs);
    auto fiber   = context.Start("Start").ValueOr(0);
    auto stepped = context.Step(fiber).IsOk() && context.Step(fiber).IsOk() && context.Step(fiber).IsOk();
    test::Check(stepped && logs.m_Text == "1,2", "Twice logs the values it was compiled with: %s", logs.m_Text.c_str());

    // Values in the Number's config, and typed into Twice's b.
    auto edited = f.m_Graph.SetConfig(number, Holds(Value::Int32(5))).IsOk() && f.m_Graph.SetValue(f.Pin(twice, "b"), Value::Int32(6)).IsOk();
    auto patched = edited ? context.Patch(f.Compiled()) : Result<void>(BlueprintError::NotFound);
    auto read    = context.Evaluate(f.Pin(number, "value"));
    test::Check(patched && read && read.GetValue().m_Value == Value::Int32(5), "the run takes a program of other values, and reads them at once: %s", Message(patched).c_str());
    stepped = context.Step(fiber).IsOk() && context.Step(fiber).IsOk();
    auto counted = context.Evaluate(f.Pin(count, "count"));
    test::Check(stepped && logs.m_Text == "1,2 5,6" && counted && counted.GetValue().m_Value == Value::Int32(2), "and goes on with them, Count's state as it was: %s", logs.m_Text.c_str());

    f.Link(other, "value", twice, "a");
    auto refused = context.Patch(f.Compiled());
    stepped = context.Step(fiber).IsOk() && context.Step(fiber).IsOk();
    test::Check(!refused && refused.GetError().Is(BlueprintError::Unsupported), "a program whose Twice reads the other Number is refused: %s", Message(refused).c_str());
    test::Check(stepped && logs.m_Text == "1,2 5,6 5,6", "and the run goes on as it was");
}

TEST_CASE(SplitPinsRun, "context.a_split_pin_reads_and_writes_its_struct_whole", nullptr, test::TestFlags_None)
{
    // Shift's point and its moved point are split, and so are Show's point, its box and the box's
    // min, two deep; neither node's code changes.
    Fixture f;
    auto start = f.Add("Start"), shift = f.Add("Shift"), show = f.Add("Show");
    auto split = true;
    for (auto [node, key] : { std::pair(shift, "point"), std::pair(shift, "moved"), std::pair(show, "point"), std::pair(show, "box"), std::pair(show, "box.min") })
        split = f.m_Graph.SplitPin(node, key, true).IsOk() && split;
    f.Link(start, "then", show, "enter");
    f.SetValue(shift, "point.x", Value::Int32(1));
    f.SetValue(shift, "point.y", Value::Int32(2));
    f.SetValue(shift, "by", Value::Int32(10));
    f.Link(shift, "moved.x", show, "point.x");
    f.Link(shift, "moved.y", show, "point.y");
    f.SetValue(show, "box.min.x", Value::Int32(3));
    f.SetValue(show, "box.max", Value::Struct(s_Point.AsType(), { Value::Int32(8), Value::Int32(0) }));
    auto program = f.Compiled();
    if (!split || !program)
        return test::Check(false, "the pins are split, and the blueprint compiles");

    Context context(program);
    Logs    logs;
    context.SetMonitor(&logs);
    auto end = Finish(context, context.Start("Start").ValueOr(0));
    test::Check(end == FiberState::Done && logs.m_Text == "11,12 5", "Shift moves (1, 2) by 10, and Show gets it and a box from 3 to 8: %s", logs.m_Text.c_str());
}

TEST_CASE(NodeState, "context.state_belongs_to_a_node_in_a_context", nullptr, test::TestFlags_None)
{
    Fixture f;
    auto start = f.Add("Start"), first = f.Add("Count"), second = f.Add("Count"), log = f.Add("Twice");
    f.Link(start, "then", first, "enter");
    f.Link(first, "then", second, "enter");
    f.Link(second, "then", log, "enter");
    f.Link(first, "count", log, "a");
    f.Link(second, "count", log, "b");
    auto program = f.Compiled();
    if (!program)
        return;
    Context one(program), two(program);
    Logs    logsOne, logsTwo;
    one.SetMonitor(&logsOne);
    two.SetMonitor(&logsTwo);
    Finish(one, one.Start("Start").ValueOr(0));
    Finish(one, one.Start("Start").ValueOr(0));
    Finish(two, two.Start("Start").ValueOr(0));
    test::Check(logsOne.m_Text == "1,1 2,2" && logsTwo.m_Text == "1,1", "each node counts on its own, in each context: %s | %s", logsOne.m_Text.c_str(), logsTwo.m_Text.c_str());
}

TEST_CASE(NodesThatCantRun, "context.a_node_that_cant_run_fails_its_fiber", nullptr, test::TestFlags_None)
{
    for (auto type : { "Lost", "Idle", "Pretend" })
    {
        Fixture f;
        auto start = f.Add("Start"), node = f.Add(type);
        f.Link(start, "then", node, "enter");
        if (auto program = f.Compiled())
        {
            Context context(program);
            auto end = Finish(context, context.Start("Start").ValueOr(0));
            auto diagnostics = context.Diagnostics();
            test::Check(end == FiberState::Failed && diagnostics.size() == 1 && diagnostics[0].m_Node == node, "%s fails: %s", type, Messages(diagnostics).c_str());
        }
    }
}

TEST_CASE(Suspending, "context.a_fiber_suspends_until_its_time_and_its_node_resumes", nullptr, test::TestFlags_None)
{
    // Start, then a Repeat of three rounds, whose body Sleeps for a second, then logs the index.
    Fixture f;
    auto start = f.Add("Start"), repeat = f.Add("Repeat"), sleep = f.Add("Sleep"), log = f.Add("Log"), text = f.Add("Text");
    f.Link(start, "then", repeat, "enter");
    f.Link(repeat, "body", sleep, "enter");
    f.Link(sleep, "then", log, "enter");
    f.Link(repeat, "index", text, "number");
    f.Link(text, "text", log, "text");
    auto program = f.Compiled();
    if (!program)
        return;
    Context context(program);
    Logs    logs;
    context.SetMonitor(&logs);
    auto fiber = context.Start("Start").ValueOr(0);

    auto first = context.Run(std::chrono::seconds(1));
    auto info  = context.Fibers()[0];
    test::Check(first == RunState::Suspended && info.m_State == FiberState::Suspended && info.m_Node == sleep && info.m_Wake == Wake::At(1.0) && logs.m_Text.empty(), "the fiber suspends at Sleep, until the clock reaches 1");
    auto refused = context.Step(fiber);
    test::Check(!refused && refused.GetError().Is(BlueprintError::Unsupported), "and takes no step: %s", Message(refused).c_str());

    context.Advance(0.5);
    context.Signal("");
    auto early = context.Run(std::chrono::seconds(1));
    test::Check(early == RunState::Suspended && logs.m_Text.empty(), "half a second on, and after a signal of no name, it still waits");
    context.Advance(0.5);
    auto woken  = context.Fibers()[0].m_State;
    auto second = context.Run(std::chrono::seconds(1));
    test::Check(woken == FiberState::Ready && second == RunState::Suspended && logs.m_Text == "0" && context.Fibers()[0].m_Wake == Wake::At(2.0), "at 1, Sleep goes on, and the next round sleeps until 2: %s", logs.m_Text.c_str());

    context.Advance(5.0);
    auto third = context.Run(std::chrono::seconds(1));
    test::Check(third == RunState::Suspended && logs.m_Text == "0 1" && context.Fibers()[0].m_Wake == Wake::At(7.0), "the clock moves on by as much as it's told, and the last round sleeps from there");
    context.Advance(1.0);
    auto end = context.Run(std::chrono::seconds(1));
    test::Check(end == RunState::Idle && logs.m_Text == "0 1 2" && context.Fibers()[0].m_State == FiberState::Done && context.Time() == 7.0, "then the loop ends: %s", logs.m_Text.c_str());
}

TEST_CASE(Signals, "context.a_signal_wakes_the_fibers_waiting_for_it_and_is_lost_without_one", nullptr, test::TestFlags_None)
{
    // Events One and Two wait for "go", and Three for "stop"; each logs its name.
    Fixture f;
    std::vector<Id> waits;
    for (auto name : { "One", "Two", "Three" })
    {
        auto event = f.Add("Start", Event(name)), wait = f.Add("Await"), log = f.Add("Log");
        f.Link(event, "then", wait, "enter");
        f.Link(wait, "then", log, "enter");
        f.SetValue(wait, "signal", Value::String(name == std::string("Three") ? "stop" : "go"));
        f.SetValue(log, "text", Value::String(name));
        waits.push_back(wait);
    }
    auto program = f.Compiled();
    if (!program)
        return;
    Context context(program);
    Logs    logs;
    context.SetMonitor(&logs);
    auto started = context.Start("One").IsOk() && context.Start("Two").IsOk();
    context.Signal("go");
    auto waiting = context.Run(std::chrono::seconds(1));
    auto fibers  = context.Fibers();
    test::Check(started && waiting == RunState::Suspended && fibers[0].m_Wake == Wake::OnSignal("go") && fibers[1].m_Node == waits[1] && logs.m_Text.empty(), "a signal sent before they wait is lost on them");

    context.Signal("stop");
    auto still = context.Run(std::chrono::seconds(1));
    test::Check(still == RunState::Suspended && logs.m_Text.empty(), "nor does another signal wake them");
    context.Signal("go");
    auto went = context.Run(std::chrono::seconds(1));
    test::Check(went == RunState::Idle && logs.m_Text == "One Two", "theirs wakes both: %s", logs.m_Text.c_str());

    auto third = context.Start("Three").ValueOr(0);
    auto stopping = context.Run(std::chrono::seconds(1));
    context.Stop(third);
    context.Signal("stop");
    auto stopped = context.Run(std::chrono::seconds(1));
    test::Check(stopping == RunState::Suspended && stopped == RunState::Idle && context.Fibers()[2].m_State == FiberState::Stopped && logs.m_Text == "One Two", "a suspended fiber stops, and its signal doesn't wake it");
}

TEST_CASE(ResumedBreakpoints, "context.a_fiber_resumed_at_a_breakpoint_doesnt_stop_there_again", nullptr, test::TestFlags_None)
{
    Fixture f;
    auto start = f.Add("Start"), sleep = f.Add("Sleep"), log = f.Add("Log");
    f.Link(start, "then", sleep, "enter");
    f.Link(sleep, "then", log, "enter");
    f.SetValue(log, "text", Value::String("woke"));
    auto program = f.Compiled();
    if (!program)
        return;
    Context context(program);
    Logs    logs;
    context.SetMonitor(&logs);
    auto started = context.Start("Start").IsOk();
    context.SetBreakpoint(sleep, true);
    auto before    = context.Run(std::chrono::seconds(1));
    auto suspended = context.Run(std::chrono::seconds(1));
    context.Advance(1.0);
    auto end = context.Run(std::chrono::seconds(1));
    test::Check(started && before == RunState::Breakpoint && suspended == RunState::Suspended && end == RunState::Idle && logs.m_Text == "woke", "Run stops before Sleep once, and Sleep resumes without stopping");
}

TEST_CASE(ClockJumps, "context.the_clock_moves_on_to_a_time_but_never_back", nullptr, test::TestFlags_None)
{
    // Start Sleeps for 0.7 s from 0.1. At 0.2, adding the difference to its time rounds short of it.
    Fixture f;
    auto start = f.Add("Start"), sleep = f.Add("Sleep");
    f.Link(start, "then", sleep, "enter");
    f.SetValue(sleep, "seconds", Value::Float(0.7f));
    auto program = f.Compiled();
    if (!program)
        return;
    Context context(program);
    context.Advance(0.1);
    auto started = context.Start("Start").IsOk() && context.Run(std::chrono::seconds(1)) == RunState::Suspended;
    auto wake    = context.Fibers()[0].m_Wake.m_Time;
    context.Advance(0.1);
    context.AdvanceTo(0.1);
    context.Advance(-1.0);
    auto kept = context.Time();
    test::Check(started && kept == 0.2 && kept + (wake - kept) < wake, "the clock stays at 0.2 for an earlier time or a negative step, and adding the difference to it falls short of the fiber's time");
    context.AdvanceTo(wake);
    test::Check(context.Time() == wake && context.Fibers()[0].m_State == FiberState::Ready, "and moved on to the fiber's time, it wakes it");
}

namespace {

// Doubles its number, from handles of its fixed pins; a group of tags follows them.
struct Doubling final : NodeType
{
    Doubling(): NodeType("test.Doubling", "Doubling") {}

    void Declare(NodeDeclaration& declaration, const NodeConfig&, const Bindings&) const override
    {
        auto count = declaration.Group("tag", 0, 4);
        for (int i = 0; i < count; ++i)
            declaration.Input("tag." + std::to_string(i), "Tag", StringType());
    }

    Result<Exit> Execute(ExecContext& context, std::string_view entry) const override
    {
        if (!(m_Enter == entry))
            return Error(BlueprintError::NotFound, "entered elsewhere");
        auto number = context.Get(m_Number);
        if (!number)
            return number.GetError();
        context.Set(m_Double, number.GetValue() * 2);
        context.Set(m_Text, std::to_string(number.GetValue()));
        return Exit::To(m_Then);
    }

    FlowPin                m_Enter  = FlowInput("enter");
    InputPin<int32_t>      m_Number = Input<int32_t>("number", "Number", { .m_Default = Value::Int32(4) });
    FlowPin                m_Then   = FlowOutput("then");
    OutputPin<int32_t>     m_Double = Output<int32_t>("double", "Double");
    OutputPin<std::string> m_Text   = Output<std::string>("text", "Text");
};

} // namespace

TEST_CASE(PinHandles, "context.a_node_type_reaches_its_fixed_pins_by_handle", nullptr, test::TestFlags_None)
{
    // Start, then Doubling of a Number, then a Log of its double, cast, and of its text.
    static Doubling doubling;
    auto registry = std::make_shared<Registry>();
    auto made     = RegisterBuiltins(*registry).IsOk() && registry->AddNodeType(s_Start).IsOk() && registry->AddNodeType(s_Log).IsOk() && registry->AddNodeType(s_Number).IsOk() && registry->AddNodeType(doubling).IsOk();
    BlueprintFixture f(registry);
    NodeConfig tagged;
    tagged.m_Groups["tag"] = 2;
    auto start = f.Add("Start"), twice = f.Add("Doubling", tagged), number = f.Add("Number", Holds(Value::Int32(21))), log = f.Add("Log"), text = f.Add("Log");
    f.Link(start, "then", twice, "enter");
    f.Link(twice, "then", log, "enter");
    f.Link(log, "then", text, "enter");
    f.Link(number, "value", twice, "number");
    f.Link(twice, "double", log, "text", LinkMode::Cast);
    f.Link(twice, "text", text, "text");
    test::Check(made && f.Keys(twice) == "enter number then double text tag.0 tag.1", "its fixed pins come first, in the order declared: %s", f.Keys(twice).c_str());
    const NodeType* types[] = { &doubling };
    auto fits = f.m_Graph.FitNewNodes(f.Pin(number, "value"), types);
    test::Check(fits && fits.GetValue().size() == 1 && fits.GetValue()[0].m_Pin == "number", "a new node of the type fits a link by its fixed pins");

    auto compiled = Compile(f.m_Blueprint);
    if (!compiled.m_Program)
        return test::Check(false, "the blueprint compiles: %s", Messages(compiled.m_Diagnostics).c_str());
    Context context(compiled.m_Program);
    Logs    logs;
    context.SetMonitor(&logs);
    auto end = Finish(context, context.Start("Start").ValueOr(0));
    test::Check(end == FiberState::Done && logs.m_Text == "42 21", "it reads and writes them by handle: %s", logs.m_Text.c_str());

    auto defaulted = f.m_Graph.Disconnect(f.m_Graph.Links()[3].m_Id).IsOk();
    Context again(Compile(f.m_Blueprint).m_Program);
    Logs    more;
    again.SetMonitor(&more);
    Finish(again, again.Start("Start").ValueOr(0));
    test::Check(defaulted && more.m_Text == "8 4", "and an unlinked one gives its declared default: %s", more.m_Text.c_str());
}
