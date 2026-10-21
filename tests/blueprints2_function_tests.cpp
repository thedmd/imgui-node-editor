//------------------------------------------------------------------------------
// blueprints2-example's runtime: functions, their bodies, and the nodes that call
// them.
//------------------------------------------------------------------------------
# include "blueprints2_fixture.h"
# include "crude_blueprint_library.h"
# include <memory>
# include <string>
# include <vector>

using namespace blueprints2_tests;

namespace {

std::shared_ptr<const Registry> FunctionRegistry()
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

TypeTerm T(int index = 0)
{
    return TypeVariable{ index };
}

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

    static Result<Id> Join(Graph& graph, Id from, std::string_view output, Id to, std::string_view input)
    {
        return graph.Connect(PinOf(graph, from, output), PinOf(graph, to, input));
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
    auto disabled = f.m_Graph.FindLink(link);
    test::Check(set && disabled && !disabled->m_Active && disabled->m_Reason.Is(BlueprintError::NoPin) && f.m_Graph.IsBroken(), "and the link to a removed one stays, disabled: %s", disabled ? std::string(disabled->m_Reason.Message()).c_str() : "gone");

    set = f.m_Blueprint.SetSignature(lerp.GetId(), Lerp());
    test::Check(set && IsActive(f.m_Graph, link) && !f.m_Graph.IsBroken(), "it's active again once the pin comes back");
    auto missing = f.m_Blueprint.SetSignature(f.m_Graph.GetId(), Lerp());
    test::Check(!missing && missing.GetError().Is(BlueprintError::NotFound), "an event graph has no signature: %s", Message(missing).c_str());
}
