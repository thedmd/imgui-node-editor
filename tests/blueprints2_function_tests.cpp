//------------------------------------------------------------------------------
// blueprints2-example's runtime: functions, their bodies, and the nodes that call
// them.
//------------------------------------------------------------------------------
# include "blueprints2_fixture.h"
# include "crude_blueprint_library.h"
# include <crude_json.h>
# include <algorithm>
# include <chrono>
# include <memory>
# include <string>
# include <vector>

using namespace blueprints2_tests;

namespace {

// Counts the times it's computed, in its state.
TestNodeType s_Tick("Tick", [](NodeDeclaration& d, const NodeConfig&, const Bindings&) { d.Output("tick", "Tick", Int32Type()); }, {}, [](EvalContext& context) -> Result<void>
{
    context.Set("tick", Value::Int32(++context.State<int32_t>()));
    return {};
});

// Waits a second on the context's clock.
TestNodeType s_Wait("Wait", [](NodeDeclaration& d, const NodeConfig&, const Bindings&) { d.FlowInput("enter"); d.FlowOutput("then"); }, [](ExecContext& context, std::string_view) -> Result<Exit>
{
    return context.IsResumed() ? Exit::To("then") : Exit::Suspend(Wake::At(context.Time() + 1.0));
}, {}, {}, true);

// Has an output only while its input's type is decided.
TestNodeType s_Pick("Pick", [](NodeDeclaration& d, const NodeConfig&, const Bindings& bindings)
{
    d.Input("in", "In", d.Variable("T"));
    if (bindings.Get("T"))
        d.Output("out", "Out", Int32Type());
});

std::shared_ptr<const Registry> FunctionRegistry()
{
    static const std::shared_ptr<const Registry> registry = []
    {
        auto made = std::make_shared<Registry>();
        if (!RegisterBuiltins(*made) || !RegisterLibrary(*made) || !made->AddNodeType(s_Tick) || !made->AddNodeType(s_Wait) || !made->AddNodeType(s_Pick))
            test::Check(false, "the library is registered");
        return made;
    }();
    return registry;
}

TypeTerm T(int index = 0)
{
    return TypeVariable{ index };
}

struct Logs final : ContextMonitor
{
    void OnLog(const Context&, Id, std::string_view text) override
    {
        m_Text += (m_Text.empty() ? "" : " ") + std::string(text);
    }

    std::string m_Text;
};

// Lerp<T: Int32 | Float>(a: T, b: T, t: Float) -> value: T
Signature Lerp()
{
    Signature signature;
    signature.m_Parameters = { { "T", { &Int32ValueType(), &FloatValueType() } } };
    signature.m_Inputs     = { { "a", "A", T() }, { "b", "B", T() }, { "t", "T", FloatType() } };
    signature.m_Outputs    = { { "value", "Value", T() } };
    return signature;
}

// A blueprint of the library's node types, with an event graph to call functions from.
struct Fixture : BlueprintFixture
{
    Fixture(): BlueprintFixture(FunctionRegistry(), "crude.") {}

    Graph& Function(const char* name, Signature signature)
    {
        auto added = m_Blueprint.AddFunction(name, std::move(signature));
        if (!added)
            test::Check(false, "function %s is added: %s", name, std::string(added.GetError().Message()).c_str());
        return added ? *added.GetValue() : m_Graph;
    }

    // In a graph of its own: a node of the blueprint's, or the library's.
    static Id In(Graph& graph, const std::string& type, NodeConfig config = {})
    {
        auto node = graph.AddNode(type, std::move(config));
        if (!node)
            test::Check(false, "%s is added: %s", type.c_str(), std::string(node.GetError().Message()).c_str());
        return node.ValueOr(0);
    }

    static Id PinOf(const Graph& graph, Id node, std::string_view key)
    {
        auto found = graph.FindNode(node);
        auto pin   = found ? found->FindPin(key) : nullptr;
        if (!pin)
            test::Check(false, "node %d has a pin %s", static_cast<int>(node), std::string(key).c_str());
        return pin ? pin->m_Id : 0;
    }

    static Result<Id> Join(Graph& graph, Id from, std::string_view output, Id to, std::string_view input, LinkMode mode = LinkMode::Plain)
    {
        return graph.Connect(PinOf(graph, from, output), PinOf(graph, to, input), mode);
    }

    // One that isn't made fails the test.
    static void Wire(Graph& graph, Id from, std::string_view output, Id to, std::string_view input, LinkMode mode = LinkMode::Plain)
    {
        if (auto link = Join(graph, from, output, to, input, mode); !link)
            test::Check(false, "%d.%s -> %d.%s is made: %s", static_cast<int>(from), std::string(output).c_str(), static_cast<int>(to), std::string(input).c_str(), std::string(link.GetError().Message()).c_str());
    }

    static void Assign(Graph& graph, Id node, std::string_view input, Value value)
    {
        if (auto set = graph.SetValue(PinOf(graph, node, input), std::move(value)); !set)
            test::Check(false, "%d.%s takes the value: %s", static_cast<int>(node), std::string(input).c_str(), std::string(set.GetError().Message()).c_str());
    }

    // Runs Start's fiber to its end, and gives what it printed.
    std::string Run(FiberState* end = nullptr, std::string* problem = nullptr)
    {
        auto compiled = Compile(m_Blueprint);
        if (!compiled.m_Program)
        {
            test::Check(false, "the blueprint compiles: %s", compiled.m_Diagnostics.empty() ? "" : std::string(compiled.m_Diagnostics[0].m_Error.Message()).c_str());
            return {};
        }
        Context context(compiled.m_Program);
        Logs    logs;
        context.SetMonitor(&logs);
        [[maybe_unused]] auto fiber = context.Start("Start");
        context.Run(std::chrono::seconds(5));
        if (end)
            *end = context.Fibers().empty() ? FiberState::Failed : context.Fibers()[0].m_State;
        if (problem && !context.Diagnostics().empty())
            *problem = std::string(context.Diagnostics()[0].m_Error.Message());
        return logs.m_Text;
    }

    static std::string TypeIn(const Graph& graph, Id node, std::string_view key)
    {
        return graph.TypeText(PinOf(graph, node, key));
    }

    Id Call(const char* function)
    {
        return In(m_Graph, std::string("call:") + function);
    }

    Id Constant(const crude_blueprint::Type& type)
    {
        return Add("Constant", Chosen("T", type));
    }
};

} // namespace

TEST_CASE(GenericBodies, "function.a_generic_body_takes_nodes_whose_sets_hold_all_its_parameter_allows", nullptr, test::TestFlags_None)
{
    // Entry.a and Entry.b into an Add, and its result to the Return.
    Fixture f;
    auto& lerp  = f.Function("Lerp", Lerp());
    auto  entry = f.In(lerp, "entry:Lerp"), add = f.In(lerp, "crude.Add"), ret = f.In(lerp, "return:Lerp");
    auto  a     = f.Join(lerp, entry, "a", add, "in.0");
    auto  b     = f.Join(lerp, entry, "b", add, "in.1");
    auto  r     = f.Join(lerp, add, "result", ret, "value");
    test::Check(a && b && r && !lerp.IsBroken(), "Entry.a and Entry.b -> Add -> Return.value: %s", Message(a ? (b ? r : b) : a).c_str());
    test::Check(f.TypeIn(lerp, add, "result") == "Lerp.T", "Add's result is Lerp.T, which stays a parameter: %s", f.TypeIn(lerp, add, "result").c_str());

    auto print   = f.In(lerp, "crude.Print");
    auto refused = f.Join(lerp, entry, "a", print, "text");
    test::Check(!refused && refused.GetError().Is(BlueprintError::TypeMismatch), "a body can't make its parameter concrete: Lerp.T into Print's String is refused: %s", Message(refused).c_str());
}

TEST_CASE(AnyTypeParameter, "function.a_parameter_of_any_type_fits_only_pins_of_any_type", nullptr, test::TestFlags_None)
{
    // Show<T>(x: T)
    Fixture f;
    Signature signature;
    signature.m_Parameters = { { "T", {} } };
    signature.m_Inputs     = { { "x", "X", T() } };
    auto& show    = f.Function("Show", signature);
    auto  entry   = f.In(show, "entry:Show"), add = f.In(show, "crude.Add"), text = f.In(show, "crude.ToString");
    auto  refused = f.Join(show, entry, "x", add, "in.0");
    auto  taken   = f.Join(show, entry, "x", text, "value");
    test::Check(!refused && Message(refused).find("may be any type") != std::string::npos, "Entry.x into Add is refused: %s", Message(refused).c_str());
    test::Check(taken.IsOk(), "and into To String, which takes any type, accepted: %s", Message(taken).c_str());
}

TEST_CASE(ParametersApart, "function.two_parameters_stay_apart_even_when_they_allow_the_same", nullptr, test::TestFlags_None)
{
    // Both<T: Int32 | Float, U: Int32 | Float>(a: T, b: U)
    Fixture f;
    Signature signature;
    signature.m_Parameters = { { "T", { &Int32ValueType(), &FloatValueType() } }, { "U", { &Int32ValueType(), &FloatValueType() } } };
    signature.m_Inputs     = { { "a", "A", T(0) }, { "b", "B", T(1) } };
    auto& both  = f.Function("Both", signature);
    auto  entry = f.In(both, "entry:Both"), add = f.In(both, "crude.Add");
    auto  a     = f.Join(both, entry, "a", add, "in.0");
    auto  b     = f.Join(both, entry, "b", add, "in.1");
    test::Check(a.IsOk(), "Entry.a (Both.T) into Add is accepted");
    test::Check(!b && Message(b).find("Both.U") != std::string::npos && Message(b).find("Both.T") != std::string::npos, "and Entry.b (Both.U) into the same Add refused: U isn't T: %s", Message(b).c_str());
}

TEST_CASE(CallNodes, "function.every_call_node_instantiates_the_parameters_on_its_own", nullptr, test::TestFlags_None)
{
    Fixture f;
    f.Function("Lerp", Lerp());
    auto ints = f.Constant(Int32Type()), floats = f.Constant(FloatType()), strings = f.Constant(StringType());
    auto first = f.Call("Lerp"), second = f.Call("Lerp"), third = f.Call("Lerp");
    f.Link(ints, "value", first, "a");
    f.Link(floats, "value", second, "a");
    auto refused = f.Connect(strings, "value", third, "a");
    test::Check(f.Type(first, "b") == "Int32" && f.Type(first, "value") == "Int32", "Int32 into one call makes its b and value Int32: %s", f.Type(first, "value").c_str());
    test::Check(f.Type(second, "value") == "Float" && f.Type(third, "value") == "?{Int32|Float}", "Float into another makes its value Float, and a third's stays open");
    test::Check(!refused && refused.GetError().Is(BlueprintError::TypeMismatch), "String, which T doesn't allow, is refused: %s", Message(refused).c_str());
    test::Check(f.Keys(first) == "enter then a b t value", "a call has flow pins, then the inputs and the outputs: %s", f.Keys(first).c_str());
}

TEST_CASE(ConstructedParameters, "function.a_parameter_inside_a_constructed_type", nullptr, test::TestFlags_None)
{
    // First<T>(items: Array<T>) -> value: T, a Get of the items' first.
    Fixture f;
    Signature signature;
    signature.m_Parameters = { { "T", {} } };
    signature.m_Inputs     = { { "items", "Items", TypeTerm(ArrayValueType(), { T() }) } };
    signature.m_Outputs    = { { "value", "Value", T() } };
    auto& first = f.Function("First", signature);
    auto  entry = f.In(first, "entry:First"), get = f.In(first, "crude.Get"), ret = f.In(first, "return:First");
    auto  in    = f.Join(first, entry, "items", get, "array");
    auto  out   = f.Join(first, get, "item", ret, "value");
    test::Check(in && out && f.TypeIn(first, get, "item") == "First.T", "Entry.items (Array<First.T>) -> Get -> Return.value: %s", f.TypeIn(first, get, "item").c_str());

    auto ints = f.Constant(Int32Type()), make = f.Add("MakeArray"), call = f.Call("First");
    f.Link(ints, "value", make, "in.0");
    f.Link(make, "array", call, "items");
    test::Check(f.Type(call, "value") == "Int32", "a call given Array<Int32> gives Int32: %s", f.Type(call, "value").c_str());
}

TEST_CASE(PureFunctions, "function.a_pure_functions_nodes_have_no_flow_pins", nullptr, test::TestFlags_None)
{
    Fixture f;
    auto signature = Lerp();
    signature.m_Pure = true;
    auto& lerp  = f.Function("Lerp", signature);
    auto  entry = f.In(lerp, "entry:Lerp"), ret = f.In(lerp, "return:Lerp");
    auto  call  = f.Call("Lerp");
    test::Check(f.Keys(call) == "a b t value", "its call has only the inputs and the outputs: %s", f.Keys(call).c_str());
    test::Check(lerp.FindNode(entry)->m_Pins.size() == 3 && lerp.FindNode(ret)->m_Pins.size() == 1, "and its entry and return only data pins");
}

TEST_CASE(SignatureChecks, "function.a_signature_has_unique_names_and_keys_of_its_own", nullptr, test::TestFlags_None)
{
    Fixture f;
    f.Function("Lerp", Lerp());
    auto again = f.m_Blueprint.AddFunction("Lerp", Lerp());
    test::Check(!again && again.GetError().Is(BlueprintError::Duplicate), "a second function of a name is refused: %s", Message(again).c_str());

    struct Case { const char* what; Signature signature; };
    auto flowKey = Lerp(), twice = Lerp(), parameters = Lerp(), stray = Lerp();
    flowKey.m_Outputs[0].m_Key = "then";
    twice.m_Inputs[1].m_Key    = "a";
    parameters.m_Parameters.push_back({ "T", {} });
    stray.m_Inputs[2].m_Type   = T(1);
    const Case cases[] = { { "a flow pin's key", flowKey }, { "a key twice", twice }, { "a parameter twice", parameters }, { "a variable that isn't a parameter", stray } };
    for (auto& c : cases)
    {
        auto refused = f.m_Blueprint.AddFunction("Other", c.signature);
        test::Check(!refused, "a signature of %s is refused: %s", c.what, Message(refused).c_str());
    }
    test::Check(f.m_Blueprint.FindNodeType("call:Lerp") && !f.m_Blueprint.FindNodeType("call:Other"), "only the function added has a call node type");
}

TEST_CASE(SignatureChanges, "function.call_nodes_follow_their_functions_signature", nullptr, test::TestFlags_None)
{
    // A call of Lerp, its t from a Float.
    Fixture f;
    auto& lerp   = f.Function("Lerp", Lerp());
    auto  floats = f.Constant(FloatType()), call = f.Call("Lerp");
    auto  link   = f.Link(floats, "value", call, "t");

    auto grown = Lerp();
    grown.m_Inputs.push_back({ "c", "C", T() });
    auto set = f.m_Blueprint.SetSignature(lerp.GetId(), grown);
    test::Check(set && f.Keys(call) == "enter then a b t c value", "an input added to the signature is added to its calls: %s", f.Keys(call).c_str());

    auto shrunk = Lerp();
    shrunk.m_Inputs.pop_back();
    set = f.m_Blueprint.SetSignature(lerp.GetId(), shrunk);
    test::Check(set && f.Keys(call) == "enter then a b value" && !f.m_Graph.FindLink(link) && !f.m_Graph.IsBroken(), "and the link of a removed one goes with it");

    set = f.m_Blueprint.SetSignature(lerp.GetId(), Lerp());
    test::Check(set && f.Keys(call) == "enter then a b t value" && f.m_Graph.Links().empty(), "the pin comes back without it");
    auto missing = f.m_Blueprint.SetSignature(f.m_Graph.GetId(), Lerp());
    test::Check(!missing && missing.GetError().Is(BlueprintError::NotFound), "an event graph has no signature: %s", Message(missing).c_str());
}

TEST_CASE(CallsRun, "function.a_call_runs_its_function_and_goes_on_with_its_outputs", nullptr, test::TestFlags_None)
{
    // Twice(x: Int32) -> y: Int32 prints x, and returns x + x. Start calls it with 21, then prints y.
    Fixture f;
    Signature twice;
    twice.m_Inputs  = { { "x", "X", Int32Type() } };
    twice.m_Outputs = { { "y", "Y", Int32Type() } };
    auto& body  = f.Function("Twice", twice);
    auto  entry = f.In(body, "entry:Twice"), shown = f.In(body, "crude.Print"), add = f.In(body, "crude.Add"), ret = f.In(body, "return:Twice");
    f.Wire(body, entry, "then", shown, "enter");
    f.Wire(body, shown, "then", ret, "enter");
    f.Wire(body, entry, "x", shown, "text", LinkMode::Cast);
    f.Wire(body, entry, "x", add, "in.0");
    f.Wire(body, entry, "x", add, "in.1");
    f.Wire(body, add, "result", ret, "y");

    auto start = f.Add("Start"), call = f.Call("Twice"), print = f.Add("Print");
    f.Link(start, "then", call, "enter");
    f.Link(call, "then", print, "enter");
    f.Link(call, "y", print, "text", LinkMode::Cast);
    f.Assign(f.m_Graph, call, "x", Value::Int32(21));
    auto end = FiberState::Ready;
    auto printed = f.Run(&end);
    test::Check(printed == "21 42" && end == FiberState::Done, "the body prints 21, and Start prints its 42: %s", printed.c_str());
}

TEST_CASE(ImplicitReturns, "function.a_function_that_ends_without_a_return_gives_its_outputs_defaults", nullptr, test::TestFlags_None)
{
    // Maybe(given: Bool) -> y: Int32 returns 5 when given, and else ends. A loop calls it twice.
    Fixture f;
    Signature maybe;
    maybe.m_Inputs  = { { "given", "Given", BoolType() } };
    maybe.m_Outputs = { { "y", "Y", Int32Type() } };
    auto& body   = f.Function("Maybe", maybe);
    auto  entry  = f.In(body, "entry:Maybe"), branch = f.In(body, "crude.Branch"), ret = f.In(body, "return:Maybe");
    f.Wire(body, entry, "then", branch, "enter");
    f.Wire(body, entry, "given", branch, "condition");
    f.Wire(body, branch, "true", ret, "enter");
    f.Assign(body, ret, "y", Value::Int32(5));

    auto start = f.Add("Start"), loop = f.Add("ForLoop"), first = f.Add("Equal"), call = f.Call("Maybe"), print = f.Add("Print");
    f.Link(start, "then", loop, "enter");
    f.Link(loop, "body", call, "enter");
    f.Link(loop, "index", first, "a");
    f.Link(first, "result", call, "given");
    f.Link(call, "then", print, "enter");
    f.Link(call, "y", print, "text", LinkMode::Cast);
    f.Assign(f.m_Graph, loop, "last", Value::Int32(1));
    f.Assign(f.m_Graph, first, "b", Value::Int32(0));
    auto end     = FiberState::Ready;
    auto printed = f.Run(&end);
    test::Check(printed == "5 0" && end == FiberState::Done, "the first call returns 5, and the second, ending, the default: %s", printed.c_str());
}

TEST_CASE(GenericCallsRun, "function.each_call_runs_with_its_own_types_and_slots", nullptr, test::TestFlags_None)
{
    // Sum<T: Int32 | Float>(a: T, b: T) -> value: T, called with Int32s and then with Floats.
    Fixture f;
    Signature sum;
    sum.m_Parameters = { { "T", { &Int32ValueType(), &FloatValueType() } } };
    sum.m_Inputs     = { { "a", "A", T() }, { "b", "B", T() } };
    sum.m_Outputs    = { { "value", "Value", T() } };
    auto& body  = f.Function("Sum", sum);
    auto  entry = f.In(body, "entry:Sum"), add = f.In(body, "crude.Add"), ret = f.In(body, "return:Sum");
    f.Wire(body, entry, "then", ret, "enter");
    f.Wire(body, entry, "a", add, "in.0");
    f.Wire(body, entry, "b", add, "in.1");
    f.Wire(body, add, "result", ret, "value");

    NodeConfig one = Chosen("T", Int32Type()), half = Chosen("T", FloatType());
    one.m_Data["value"]  = Value::Int32(1);
    half.m_Data["value"] = Value::Float(1.5f);
    auto start = f.Add("Start"), ints = f.Call("Sum"), floats = f.Call("Sum"), first = f.Add("Print"), second = f.Add("Print");
    auto ones  = f.Add("Constant", one), halves = f.Add("Constant", half), text = f.Add("ToString"), text2 = f.Add("ToString");
    f.Link(start, "then", ints, "enter");
    f.Link(ints, "then", first, "enter");
    f.Link(first, "then", floats, "enter");
    f.Link(floats, "then", second, "enter");
    f.Link(ones, "value", ints, "a");
    f.Link(halves, "value", floats, "a");
    f.Assign(f.m_Graph, ints, "b", Value::Int32(2));
    f.Assign(f.m_Graph, floats, "b", Value::Float(2.25f));
    f.Link(ints, "value", text, "value");
    f.Link(floats, "value", text2, "value");
    f.Link(text, "text", first, "text");
    f.Link(text2, "text", second, "text");
    auto printed = f.Run();
    test::Check(printed == "3 3.75", "one call adds Int32s, the other Floats, through one body: %s", printed.c_str());
}

TEST_CASE(PureCallsRun, "function.a_pure_call_runs_its_body_when_its_outputs_are_read", nullptr, test::TestFlags_None)
{
    // Double(x: Int32) -> y: Int32 is pure: x + x.
    Fixture f;
    Signature twice;
    twice.m_Inputs  = { { "x", "X", Int32Type() } };
    twice.m_Outputs = { { "y", "Y", Int32Type() } };
    twice.m_Pure    = true;
    auto& body  = f.Function("Double", twice);
    auto  entry = f.In(body, "entry:Double"), add = f.In(body, "crude.Add"), ret = f.In(body, "return:Double");
    f.Wire(body, entry, "x", add, "in.0");
    f.Wire(body, entry, "x", add, "in.1");
    f.Wire(body, add, "result", ret, "y");

    auto start = f.Add("Start"), call = f.Call("Double"), print = f.Add("Print");
    f.Link(start, "then", print, "enter");
    f.Link(call, "y", print, "text", LinkMode::Cast);
    f.Assign(f.m_Graph, call, "x", Value::Int32(5));
    auto printed = f.Run();
    test::Check(printed == "10", "Print reads 5 doubled: %s", printed.c_str());
}

TEST_CASE(PureCallsOncePerStep, "function.a_pure_call_leaves_what_its_step_computed_as_it_was", nullptr, test::TestFlags_None)
{
    // Echo(x: Int32) -> y: Int32 is pure. An Add sums Echo's y of a Tick, and the same Tick.
    Fixture f;
    Signature echo;
    echo.m_Inputs  = { { "x", "X", Int32Type() } };
    echo.m_Outputs = { { "y", "Y", Int32Type() } };
    echo.m_Pure    = true;
    auto& body  = f.Function("Echo", echo);
    auto  entry = f.In(body, "entry:Echo"), ret = f.In(body, "return:Echo");
    f.Wire(body, entry, "x", ret, "y");

    auto start = f.Add("Start"), tick = f.In(f.m_Graph, "test.Tick"), call = f.Call("Echo"), add = f.Add("Add"), print = f.Add("Print");
    f.Link(start, "then", print, "enter");
    f.Link(tick, "tick", call, "x");
    f.Link(call, "y", add, "in.0");
    f.Link(tick, "tick", add, "in.1");
    f.Link(add, "result", print, "text", LinkMode::Cast);
    auto printed = f.Run();
    test::Check(printed == "2", "the Tick is computed once in the step, through the call and beside it: %s", printed.c_str());
}

TEST_CASE(Recursion, "function.recursion_runs_down_to_its_base_and_too_deep_fails_its_fiber", nullptr, test::TestFlags_None)
{
    // Down(n: Int32), while n > 0, prints n, calls itself with n - 1, and prints n again, which each
    // call keeps in its own slots.
    Fixture f;
    Signature down;
    down.m_Inputs = { { "n", "N", Int32Type() } };
    auto& body    = f.Function("Down", down);
    auto  entry   = f.In(body, "entry:Down"), branch = f.In(body, "crude.Branch"), greater = f.In(body, "crude.Greater");
    auto  print   = f.In(body, "crude.Print"), less = f.In(body, "crude.Subtract"), again = f.In(body, "call:Down"), after = f.In(body, "crude.Print");
    f.Wire(body, entry, "then", branch, "enter");
    f.Wire(body, entry, "n", greater, "a");
    f.Wire(body, greater, "result", branch, "condition");
    f.Wire(body, branch, "true", print, "enter");
    f.Wire(body, entry, "n", print, "text", LinkMode::Cast);
    f.Wire(body, print, "then", again, "enter");
    f.Wire(body, again, "then", after, "enter");
    f.Wire(body, entry, "n", after, "text", LinkMode::Cast);
    f.Wire(body, entry, "n", less, "a");
    f.Wire(body, less, "result", again, "n");
    f.Assign(body, greater, "b", Value::Int32(0));
    f.Assign(body, less, "b", Value::Int32(1));

    auto start = f.Add("Start"), call = f.Call("Down");
    auto into  = f.Link(start, "then", call, "enter");
    f.Assign(f.m_Graph, call, "n", Value::Int32(3));
    auto end     = FiberState::Ready;
    auto printed = f.Run(&end);
    test::Check(printed == "3 2 1 1 2 3" && end == FiberState::Done, "it counts down to its base, and each call back up from its own n: %s", printed.c_str());

    // Without a base, it fails once it's 256 calls deep.
    f.Unlink(into);
    auto& forever = f.Function("Forever", {});
    auto  in      = f.In(forever, "entry:Forever"), self = f.In(forever, "call:Forever");
    f.Wire(forever, in, "then", self, "enter");
    auto endless = f.Call("Forever");
    f.Link(start, "then", endless, "enter");
    std::string problem;
    f.Run(&end, &problem);
    test::Check(end == FiberState::Failed && problem.find("more than 256 calls") != std::string::npos, "a call too deep fails its fiber: %s", problem.c_str());
}

TEST_CASE(PureRecursion, "function.a_pure_call_too_deep_fails_its_fiber", nullptr, test::TestFlags_None)
{
    // Endless(x: Int32) -> y: Int32 is pure, and its y is its own call's.
    Fixture f;
    Signature endless;
    endless.m_Inputs  = { { "x", "X", Int32Type() } };
    endless.m_Outputs = { { "y", "Y", Int32Type() } };
    endless.m_Pure    = true;
    auto& body  = f.Function("Endless", endless);
    auto  entry = f.In(body, "entry:Endless"), self = f.In(body, "call:Endless"), ret = f.In(body, "return:Endless");
    f.Wire(body, entry, "x", self, "x");
    f.Wire(body, self, "y", ret, "y");

    auto start = f.Add("Start"), call = f.Call("Endless"), print = f.Add("Print");
    f.Link(start, "then", print, "enter");
    f.Link(call, "y", print, "text", LinkMode::Cast);
    auto end = FiberState::Ready;
    std::string problem;
    f.Run(&end, &problem);
    test::Check(end == FiberState::Failed && problem.find("deeper than 128") != std::string::npos, "its fiber fails, as too deep: %s", problem.c_str());
}

TEST_CASE(DebuggingCalls, "function.the_debugger_reads_a_call_in_progress", nullptr, test::TestFlags_None)
{
    // Echo(x: Int32) -> y: Int32 returns x; Start calls it with 7.
    Fixture f;
    Signature echo;
    echo.m_Inputs  = { { "x", "X", Int32Type() } };
    echo.m_Outputs = { { "y", "Y", Int32Type() } };
    auto& body  = f.Function("Echo", echo);
    auto  entry = f.In(body, "entry:Echo"), ret = f.In(body, "return:Echo");
    f.Wire(body, entry, "then", ret, "enter");
    f.Wire(body, entry, "x", ret, "y");
    auto start = f.Add("Start"), call = f.Call("Echo");
    f.Link(start, "then", call, "enter");
    f.Assign(f.m_Graph, call, "x", Value::Int32(7));
    auto compiled = Compile(f.m_Blueprint);
    if (!compiled.m_Program)
        return test::Check(false, "the blueprint compiles");

    Context context(compiled.m_Program);
    auto fiber   = context.Start("Start").ValueOr(0);
    auto outside = context.Evaluate(f.PinOf(body, entry, "x"));
    test::Check(!outside && outside.GetError().Is(BlueprintError::Unsupported), "before the call, its pins have no values: %s", Message(outside).c_str());

    auto stepped = context.Step(fiber).IsOk() && context.Step(fiber).IsOk();
    auto inside  = context.Evaluate(f.PinOf(body, entry, "x"));
    auto info    = context.Fibers()[0];
    test::Check(stepped && inside && inside.GetValue().m_Value == Value::Int32(7) && info.m_Node == entry && info.m_Calls == std::vector<Id>{ call }, "in the call, its entry gives 7, and the fiber says which call it's in");

    stepped = context.Step(fiber).IsOk() && context.Step(fiber).IsOk();
    auto returned = context.Evaluate(f.Pin(call, "y"));
    test::Check(stepped && returned && returned.GetValue().m_Value == Value::Int32(7) && context.Fibers()[0].m_Calls.empty() && context.Fibers()[0].m_State == FiberState::Done, "and once it returned, the call's y is 7");
}

TEST_CASE(CompilingFunctions, "function.compiling_checks_entries_returns_and_pure_bodies", nullptr, test::TestFlags_None)
{
    auto compiled = [](Fixture& f) { auto result = Compile(f.m_Blueprint); return result.m_Program ? std::string() : std::string(result.m_Diagnostics.back().m_Error.Message()); };

    Fixture none;
    none.Function("Lerp", Lerp());
    test::Check(compiled(none).find("Lerp has no entry") != std::string::npos, "a function needs an entry: %s", compiled(none).c_str());

    Fixture elsewhere;
    auto& lerp = elsewhere.Function("Lerp", Lerp());
    elsewhere.In(lerp, "entry:Lerp");
    elsewhere.In(elsewhere.m_Graph, "return:Lerp");
    test::Check(compiled(elsewhere).find("belongs in Lerp's graph") != std::string::npos, "its return belongs in its graph: %s", compiled(elsewhere).c_str());

    Fixture flow;
    auto pure = Lerp();
    pure.m_Pure = true;
    auto& body = flow.Function("Lerp", pure);
    flow.In(body, "entry:Lerp");
    flow.In(body, "crude.Print");
    test::Check(compiled(flow).find("which has flow pins") != std::string::npos, "a pure body holds no node with flow pins: %s", compiled(flow).c_str());

    Fixture twice;
    auto& both = twice.Function("Lerp", pure);
    twice.In(both, "entry:Lerp");
    twice.In(both, "return:Lerp");
    twice.In(both, "return:Lerp");
    test::Check(compiled(twice).find("two returns") != std::string::npos, "and a pure one has one return at most: %s", compiled(twice).c_str());
}

TEST_CASE(SuspendingCalls, "function.a_call_suspends_with_its_frame_and_goes_on_once_resumed", nullptr, test::TestFlags_None)
{
    // Later(x: Int32) -> y: Int32 waits a second, then returns x + x. Start calls it with 4, then
    // prints y.
    Fixture f;
    Signature later;
    later.m_Inputs  = { { "x", "X", Int32Type() } };
    later.m_Outputs = { { "y", "Y", Int32Type() } };
    auto& body  = f.Function("Later", later);
    auto  entry = f.In(body, "entry:Later"), wait = f.In(body, "test.Wait"), add = f.In(body, "crude.Add"), ret = f.In(body, "return:Later");
    f.Wire(body, entry, "then", wait, "enter");
    f.Wire(body, wait, "then", ret, "enter");
    f.Wire(body, entry, "x", add, "in.0");
    f.Wire(body, entry, "x", add, "in.1");
    f.Wire(body, add, "result", ret, "y");
    auto start = f.Add("Start"), call = f.Call("Later"), print = f.Add("Print");
    f.Link(start, "then", call, "enter");
    f.Link(call, "then", print, "enter");
    f.Link(call, "y", print, "text", LinkMode::Cast);
    f.Assign(f.m_Graph, call, "x", Value::Int32(4));
    auto compiled = Compile(f.m_Blueprint);
    if (!compiled.m_Program)
        return test::Check(false, "the blueprint compiles");

    Context context(compiled.m_Program);
    Logs    logs;
    context.SetMonitor(&logs);
    auto started   = context.Start("Start").IsOk();
    auto suspended = context.Run(std::chrono::seconds(1));
    auto info      = context.Fibers()[0];
    auto x         = context.Evaluate(f.PinOf(body, entry, "x"));
    test::Check(started && suspended == RunState::Suspended && info.m_Node == wait && info.m_Calls == std::vector<Id>{ call } && x && x.GetValue().m_Value == Value::Int32(4), "the fiber suspends in the call, whose x the debugger reads");
    context.Advance(1.0);
    auto end = context.Run(std::chrono::seconds(1));
    test::Check(end == RunState::Idle && logs.m_Text == "8" && context.Fibers()[0].m_State == FiberState::Done, "resumed, the call returns, and Start prints 8: %s", logs.m_Text.c_str());
}

TEST_CASE(LatentCalls, "function.a_call_may_suspend_when_its_function_holds_a_latent_node", nullptr, test::TestFlags_None)
{
    // Later holds a Wait, and Outer calls Later. Plain prints, and Self calls itself and Plain.
    Fixture f;
    auto& later = f.Function("Later", {});
    auto& outer = f.Function("Outer", {});
    auto& plain = f.Function("Plain", {});
    auto& self  = f.Function("Self", {});
    for (auto [graph, type] : { std::pair(&later, "test.Wait"), std::pair(&outer, "call:Later"), std::pair(&plain, "crude.Print"), std::pair(&self, "call:Self"), std::pair(&self, "call:Plain") })
        f.In(*graph, type);
    auto may = [&f](const char* type)
    {
        auto found = f.m_Blueprint.FindNodeType(type);
        return found && f.m_Blueprint.MaySuspend(*found);
    };
    test::Check(may("test.Wait") && !may("crude.Print"), "a latent node type's nodes may suspend, and others' not");
    test::Check(may("call:Later") && may("call:Outer"), "so may a call of a function that holds one, or of one that calls it");
    test::Check(!may("call:Plain") && !may("call:Self") && !may("entry:Later") && !may("return:Later"), "a call of a function that doesn't, through itself or not, may not, nor may its entry and returns");
}

TEST_CASE(PureSignatures, "function.made_pure_a_function_drops_the_links_of_its_flow_pins", nullptr, test::TestFlags_None)
{
    // Echo(x: Int32) -> y: Int32, its entry linked to its return, and x to y; Start calls it.
    Fixture f;
    Signature echo;
    echo.m_Inputs  = { { "x", "X", Int32Type() } };
    echo.m_Outputs = { { "y", "Y", Int32Type() } };
    auto& body  = f.Function("Echo", echo);
    auto  entry = f.In(body, "entry:Echo"), ret = f.In(body, "return:Echo");
    f.Wire(body, entry, "then", ret, "enter");
    f.Wire(body, entry, "x", ret, "y");
    auto start = f.Add("Start"), call = f.Call("Echo");
    f.Link(start, "then", call, "enter");

    // A Pick's output, linked, then gone as its input's type goes undecided.
    auto pick   = f.In(f.m_Graph, "test.Pick"), typing = f.Link(f.Constant(Int32Type()), "value", pick, "in");
    auto other  = f.Link(pick, "out", f.Add("Print"), "text", LinkMode::Cast);
    f.Unlink(typing);
    auto shrunk = !f.m_Graph.FindLink(other)->m_Active;

    echo.m_Pure = true;
    auto pure = f.m_Blueprint.SetSignature(body.GetId(), echo);
    auto data = body.Links().size() == 1 && body.Links()[0].m_Active;
    test::Check(shrunk && pure && data && !body.IsBroken() && f.m_Graph.Links().size() == 1 && f.m_Graph.FindLink(other), "the flow links of its entry, its return and its calls go, its data link stays, and so does another's disabled link");
    f.Remove(pick);
    auto compiled = Compile(f.m_Blueprint);
    test::Check(compiled.m_Program != nullptr, "and it compiles: %s", compiled.m_Diagnostics.empty() ? "" : std::string(compiled.m_Diagnostics[0].m_Error.Message()).c_str());
}

TEST_CASE(RenamedFunctions, "function.a_renamed_functions_graph_and_nodes_take_its_name", nullptr, test::TestFlags_None)
{
    // Lerp<T> returns a, called from Start with Int32s, the result printed. Renamed Mix.
    Fixture f;
    auto& body  = f.Function("Lerp", Lerp());
    auto  entry = f.In(body, "entry:Lerp"), ret = f.In(body, "return:Lerp");
    f.Wire(body, entry, "then", ret, "enter");
    f.Wire(body, entry, "a", ret, "value");
    auto start = f.Add("Start"), call = f.Call("Lerp"), print = f.Add("Print");
    f.Link(start, "then", call, "enter");
    f.Link(call, "then", print, "enter");
    f.Link(f.Constant(Int32Type()), "value", call, "a");
    f.Link(call, "value", print, "text", LinkMode::Cast);
    f.In(f.Function("Other", {}), "entry:Other");
    auto before = Compile(f.m_Blueprint).m_Program;
    if (!before)
        return test::Check(false, "the blueprint compiles");

    auto renamed = f.m_Blueprint.RenameFunction(body.GetId(), "Mix");
    auto node    = f.m_Graph.FindNode(call);
    test::Check(renamed && body.GetName() == "Mix" && f.m_Blueprint.FindFunction(body.GetId())->m_Name == "Mix", "renamed, a function's graph has its name");
    test::Check(node && node->m_Type->m_Name == "call:Mix" && node->Name() == "Mix#" + std::to_string(call) && body.FindNode(entry)->m_Type->m_Name == "entry:Mix" && !f.m_Blueprint.FindNodeType("call:Lerp"), "and its nodes take node types of the new name, keeping their IDs");
    test::Check(f.TypeIn(body, entry, "a") == "Mix.T" && !body.IsBroken() && !f.m_Graph.IsBroken(), "its type parameters are named after it: %s", f.TypeIn(body, entry, "a").c_str());

    auto end     = FiberState::Ready;
    auto printed = f.Run(&end);
    test::Check(printed == "0" && end == FiberState::Done, "it runs as it did: %s", printed.c_str());
    Context old(before);
    Logs    logs;
    old.SetMonitor(&logs);
    auto ran = old.Start("Start").IsOk() && old.Run(std::chrono::seconds(1)) == RunState::Idle;
    test::Check(ran && logs.m_Text == "0", "and so does a program compiled before: %s", logs.m_Text.c_str());

    auto taken   = f.m_Blueprint.RenameFunction(body.GetId(), "Other");
    auto bad     = f.m_Blueprint.RenameFunction(body.GetId(), "two words");
    auto missing = f.m_Blueprint.RenameFunction(f.m_Graph.GetId(), "Main");
    auto same    = f.m_Blueprint.RenameFunction(body.GetId(), "Mix");
    test::Check(!taken && taken.GetError().Is(BlueprintError::Duplicate) && !bad && !missing && missing.GetError().Is(BlueprintError::NotFound) && same, "another function's name, a bad one, and an event graph's are refused: %s", Message(taken).c_str());
}

TEST_CASE(EventsInFunctions, "function.an_event_in_a_function_graph_is_refused", nullptr, test::TestFlags_None)
{
    Fixture f;
    auto& body  = f.Function("Spawn", {});
    auto  entry = f.In(body, "entry:Spawn"), start = f.In(body, "crude.Start");
    auto compiled = Compile(f.m_Blueprint);
    auto refused  = !compiled.m_Program && std::any_of(compiled.m_Diagnostics.begin(), compiled.m_Diagnostics.end(), [start](const Diagnostic& diagnostic) { return diagnostic.m_Node == start && diagnostic.m_Error.Is(BlueprintError::Unsupported); });
    test::Check(entry && refused, "a function's graph can't hold a Start: %s", compiled.m_Diagnostics.empty() ? "" : std::string(compiled.m_Diagnostics[0].m_Error.Message()).c_str());
}

TEST_CASE(HostCalls, "function.the_host_calls_a_function_and_reads_its_results", nullptr, test::TestFlags_None)
{
    // Twice(x: Int32) -> y: Int32 prints x, and returns x + x. Lerp<T> returns its a. Maybe(given:
    // Bool) -> y: Int32 returns 5 when given, and else ends. Later(x) -> y waits a second, then
    // returns x. Halve is pure.
    Fixture f;
    Signature twice;
    twice.m_Inputs  = { { "x", "X", Int32Type() } };
    twice.m_Outputs = { { "y", "Y", Int32Type() } };
    auto& doubling = f.Function("Twice", twice);
    auto  entry    = f.In(doubling, "entry:Twice"), shown = f.In(doubling, "crude.Print"), add = f.In(doubling, "crude.Add"), ret = f.In(doubling, "return:Twice");
    f.Wire(doubling, entry, "then", shown, "enter");
    f.Wire(doubling, shown, "then", ret, "enter");
    f.Wire(doubling, entry, "x", shown, "text", LinkMode::Cast);
    f.Wire(doubling, entry, "x", add, "in.0");
    f.Wire(doubling, entry, "x", add, "in.1");
    f.Wire(doubling, add, "result", ret, "y");

    auto& lerp      = f.Function("Lerp", Lerp());
    auto  lerpEntry = f.In(lerp, "entry:Lerp"), lerpReturn = f.In(lerp, "return:Lerp");
    f.Wire(lerp, lerpEntry, "then", lerpReturn, "enter");
    f.Wire(lerp, lerpEntry, "a", lerpReturn, "value");

    Signature maybe;
    maybe.m_Inputs  = { { "given", "Given", BoolType() } };
    maybe.m_Outputs = { { "y", "Y", Int32Type() } };
    auto& maybeBody = f.Function("Maybe", maybe);
    auto  maybeEntry = f.In(maybeBody, "entry:Maybe"), branch = f.In(maybeBody, "crude.Branch"), maybeReturn = f.In(maybeBody, "return:Maybe");
    f.Wire(maybeBody, maybeEntry, "then", branch, "enter");
    f.Wire(maybeBody, maybeEntry, "given", branch, "condition");
    f.Wire(maybeBody, branch, "true", maybeReturn, "enter");
    f.Assign(maybeBody, maybeReturn, "y", Value::Int32(5));

    auto& later      = f.Function("Later", twice);
    auto  laterEntry = f.In(later, "entry:Later"), wait = f.In(later, "test.Wait"), laterReturn = f.In(later, "return:Later");
    f.Wire(later, laterEntry, "then", wait, "enter");
    f.Wire(later, wait, "then", laterReturn, "enter");
    f.Wire(later, laterEntry, "x", laterReturn, "y");

    Signature halve;
    halve.m_Pure = true;
    f.In(f.Function("Halve", halve), "entry:Halve");
    Signature make;
    make.m_Parameters = { { "T", {} } };
    make.m_Outputs    = { { "v", "V", T() } };
    f.In(f.Function("Make", make), "entry:Make");

    auto compiled = Compile(f.m_Blueprint);
    if (!compiled.m_Program)
        return test::Check(false, "the blueprint compiles: %s", compiled.m_Diagnostics.empty() ? "" : std::string(compiled.m_Diagnostics[0].m_Error.Message()).c_str());
    Context context(compiled.m_Program);
    Logs    logs;
    context.SetMonitor(&logs);
    auto call = [&context](const char* function, std::vector<Value> arguments) -> std::string
    {
        auto fiber = context.Call(function, arguments);
        if (!fiber)
            return "refused: " + std::string(fiber.GetError().Message());
        context.Run(std::chrono::seconds(1));
        auto info = context.Fibers().back();
        if (info.m_State != FiberState::Done)
            return "not done";
        std::string text;
        for (auto& result : info.m_Results)
            text += (text.empty() ? "" : " ") + result.GetType()->m_Name + " " + result.GetType()->m_Head->Format(result);
        return text;
    };

    auto doubled = call("Twice", { Value::Int32(21) });
    test::Check(doubled == "Int32 42" && logs.m_Text == "21", "a call runs the function, which returns its results: %s", doubled.c_str());
    auto floats = call("Lerp", { Value::Float(1.5f), Value::Float(2.0f), Value::Float(0.5f) });
    auto ints   = call("Lerp", { Value::Int32(7), Value::Int32(9), Value::Float(0.5f) });
    test::Check(floats == "Float 1.5" && ints == "Int32 7", "a generic one takes its parameters' types from the arguments: %s; %s", floats.c_str(), ints.c_str());
    auto given = call("Maybe", { Value::Bool(true) }), ended = call("Maybe", { Value::Bool(false) });
    test::Check(given == "Int32 5" && ended == "Int32 0", "one that ends without a return gives its outputs' defaults: %s", ended.c_str());

    auto fiber = context.Call("Later", std::vector<Value>{ Value::Int32(3) }).ValueOr(0);
    auto waits = context.Run(std::chrono::seconds(1)) == RunState::Suspended && context.Fibers().back().m_Calls.empty();
    context.Advance(1.0);
    context.Run(std::chrono::seconds(1));
    auto info = context.Fibers().back();
    test::Check(fiber && waits && info.m_State == FiberState::Done && info.m_Results == std::vector<Value>{ Value::Int32(3) }, "one that suspends gives its results once it's done");

    const std::pair<std::string, const char*> refusals[] =
    {
        { call("Lerp", { Value::Int32(1), Value::Float(2.0f), Value::Float(0.0f) }), "arguments of two types for one parameter" },
        { call("Lerp", { Value::String("a"), Value::String("b"), Value::Float(0.0f) }), "a type its parameter doesn't allow" },
        { call("Twice", {}), "too few arguments" },
        { call("Twice", { Value::Float(1.0f) }), "an argument of another type" },
        { call("Halve", {}), "a pure function" },
        { call("Make", {}), "a parameter no argument decides" },
        { call("Nothing", {}), "a function there isn't" },
    };
    for (auto& [result, why] : refusals)
        test::Check(result.starts_with("refused: "), "%s is refused: %s", why, result.c_str());
}

TEST_CASE(RemovedFunctions, "function.a_function_goes_with_its_graph_once_no_other_graph_calls_it", nullptr, test::TestFlags_None)
{
    // Countdown calls itself; Start calls it, and prints after.
    Fixture f;
    auto& body  = f.Function("Countdown", {});
    auto  entry = f.In(body, "entry:Countdown"), self = f.In(body, "call:Countdown");
    f.Wire(body, entry, "then", self, "enter");
    auto start = f.Add("Start"), call = f.Call("Countdown"), print = f.Add("Print");
    f.Link(start, "then", print, "enter");
    auto before = Compile(f.m_Blueprint).m_Program;
    auto refused = f.m_Blueprint.RemoveFunction(body.GetId());
    test::Check(!refused && refused.GetError().Is(BlueprintError::Unsupported) && f.m_Blueprint.FindFunction(body.GetId()), "it stays while another graph calls it: %s", Message(refused).c_str());

    f.Remove(call);
    const auto graph   = body.GetId();
    auto       removed = f.m_Blueprint.RemoveFunction(graph);
    test::Check(removed && !f.m_Blueprint.FindFunction(graph) && !f.m_Blueprint.FindGraph(graph) && !f.m_Blueprint.FindNodeType("call:Countdown") && !f.m_Blueprint.FindNodeType("entry:Countdown"), "then it goes, its own call and all, with its graph and node types");

    auto missing = f.m_Blueprint.RemoveFunction(f.m_Graph.GetId());
    test::Check(!missing && missing.GetError().Is(BlueprintError::NotFound), "an event graph isn't a function's: %s", Message(missing).c_str());

    if (!before)
        return test::Check(false, "the blueprint compiled before");
    Context old(before);
    auto ran = old.Start("Start").IsOk() && old.Run(std::chrono::seconds(1)) == RunState::Idle;
    test::Check(ran && old.Fibers()[0].m_State == FiberState::Done, "and a program compiled before runs as it was");
}

TEST_CASE(RenamedTypeParameters, "function.a_renamed_type_parameter_keeps_the_types_its_calls_chose", nullptr, test::TestFlags_None)
{
    // Lerp<T> returns a; Start calls it with T chosen Float, the result printed. Pair<T, U> has two.
    Fixture f;
    auto& body  = f.Function("Lerp", Lerp());
    auto  entry = f.In(body, "entry:Lerp"), ret = f.In(body, "return:Lerp");
    f.Wire(body, entry, "then", ret, "enter");
    f.Wire(body, entry, "a", ret, "value");
    auto start = f.Add("Start"), call = f.In(f.m_Graph, "call:Lerp", Chosen("T", FloatType())), print = f.Add("Print");
    f.Link(start, "then", call, "enter");
    f.Link(call, "then", print, "enter");
    f.Link(call, "value", print, "text", LinkMode::Cast);
    Signature pair;
    pair.m_Parameters = { { "T", {} }, { "U", {} } };
    auto& other = f.Function("Pair", pair);
    f.In(other, "entry:Pair");

    auto renamed = f.m_Blueprint.RenameTypeParameter(body.GetId(), 0, "Item");
    auto chosen  = f.m_Graph.FindNode(call)->m_Config.m_Types;
    test::Check(renamed && f.m_Blueprint.FindFunction(body.GetId())->m_Signature.m_Parameters[0].m_Name == "Item" && f.TypeIn(body, entry, "a") == "Lerp.Item", "renamed, a type parameter has its new name in the body: %s", f.TypeIn(body, entry, "a").c_str());
    test::Check(chosen.size() == 1 && chosen["Item"] == &FloatType() && f.TypeIn(f.m_Graph, call, "a") == "Float" && !f.m_Graph.IsBroken(), "and a call keeps the type chosen for it, under the new name: %s", f.TypeIn(f.m_Graph, call, "a").c_str());
    auto end     = FiberState::Ready;
    auto printed = f.Run(&end);
    test::Check(printed == "0" && end == FiberState::Done, "it runs as it did: %s", printed.c_str());

    Blueprint loaded(FunctionRegistry(), std::make_shared<TypeTable>());
    auto read  = Load(loaded, Save(f.m_Blueprint));
    auto again = read ? loaded.FindFunction(body.GetId()) : nullptr;
    auto node  = read ? loaded.FindGraph(f.m_Graph.GetId())->FindNode(call) : nullptr;
    test::Check(again && again->m_Signature.m_Parameters[0].m_Name == "Item" && node && node->m_Config.m_Types.contains("Item"), "a file keeps the new name: %s", Message(read).c_str());

    auto typeName = f.m_Blueprint.RenameTypeParameter(body.GetId(), 0, "Int32");
    auto taken    = f.m_Blueprint.RenameTypeParameter(other.GetId(), 1, "T");
    auto bad      = f.m_Blueprint.RenameTypeParameter(body.GetId(), 0, "two words");
    auto none     = f.m_Blueprint.RenameTypeParameter(body.GetId(), 1, "U");
    auto missing  = f.m_Blueprint.RenameTypeParameter(f.m_Graph.GetId(), 0, "U");
    auto same     = f.m_Blueprint.RenameTypeParameter(body.GetId(), 0, "Item");
    test::Check(!typeName && typeName.GetError().Is(BlueprintError::Duplicate) && !taken && taken.GetError().Is(BlueprintError::Duplicate) && !bad, "a value type's name, another parameter's, and a bad one are refused: %s", Message(typeName).c_str());
    test::Check(!none && none.GetError().Is(BlueprintError::OutOfRange) && !missing && missing.GetError().Is(BlueprintError::NotFound) && same, "as are a parameter there isn't and an event graph; its own name changes nothing");
    test::Check(f.m_Graph.FindNode(call)->m_Config.m_Types.contains("Item") && f.m_Blueprint.FindFunction(body.GetId())->m_Signature.m_Parameters[0].m_Name == "Item", "a refused rename leaves the parameter and its calls as they were");
}
