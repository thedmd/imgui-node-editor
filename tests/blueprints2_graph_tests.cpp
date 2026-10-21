//------------------------------------------------------------------------------
// blueprints2-example's runtime: node types, and graphs whose pin types follow their links.
//------------------------------------------------------------------------------
# include "harness.h"
# include "crude_blueprint.h"
# include <algorithm>
# include <array>
# include <functional>
# include <memory>
# include <string>
# include <vector>

using namespace crude_blueprint;

namespace {

// A node type that declares its pins with a function.
struct TestNodeType final : NodeType
{
    using Declaration = std::function<void(NodeDeclaration&, const NodeConfig&, const Bindings&)>;

    TestNodeType(std::string title, Declaration declare)
        : NodeType("test." + title, title)
        , m_Declare(std::move(declare))
    {
    }

    void Declare(NodeDeclaration& declaration, const NodeConfig& config, const Bindings& bindings) const override
    {
        m_Declare(declaration, config, bindings);
    }

    const Declaration m_Declare;
};

std::vector<const ValueType*> Numbers()
{
    return { &Int32ValueType(), &FloatValueType(), &StringValueType() };
}

// Add<T: Int32 | Float | String>(in.0 ... in.N: T) -> out: T
TestNodeType s_Add("Add", [](NodeDeclaration& d, const NodeConfig&, const Bindings&)
{
    auto T = d.Variable("T", Numbers());
    auto count = d.Group("in", 2, 16);
    for (int i = 0; i < count; ++i)
        d.Input("in." + std::to_string(i), "", T);
    d.Output("out", "Result", T);
});

// MakeMap<K: Int32 | String, V>(key.0, value.0 ... key.N, value.N) -> map: Map<K, V>
TestNodeType s_MakeMap("MakeMap", [](NodeDeclaration& d, const NodeConfig&, const Bindings&)
{
    auto K = d.Variable("K", { &Int32ValueType(), &StringValueType() });
    auto V = d.Variable("V");
    auto count = d.Group("pair", 1, 16, { "key", "value" });
    for (int i = 0; i < count; ++i)
    {
        d.Input("key." + std::to_string(i), "Key", K);
        d.Input("value." + std::to_string(i), "Value", V);
    }
    d.Output("map", "Map", TypeTerm(MapValueType(), { K, V }));
});

// A Break whose field pins follow the type of its input.
TestNodeType s_BreakAny("BreakAny", [](NodeDeclaration& d, const NodeConfig&, const Bindings& bindings)
{
    auto T = d.Variable("T");
    d.Input("value", "Value", T);
    if (auto type = bindings.Get("T"); type && IsA<StructType>(type->m_Head))
        for (auto& field : static_cast<const StructType*>(type->m_Head)->Fields())
            d.Output(field.m_Name, field.m_Name, *field.m_Type);
});

TestNodeType s_ForLoop("ForLoop", [](NodeDeclaration& d, const NodeConfig&, const Bindings&)
{
    d.FlowInput("enter", "", { .m_Requirement = Requirement::Recommended });
    d.Input("first", "First Index", Int32Type());
    d.Input("last", "Last Index", Int32Type(), { .m_Default = Value::Int32(10) });
    d.FlowOutput("body", "Loop Body");
    d.Output("index", "Index", Int32Type());
    d.FlowOutput("completed", "Completed");
});

TestNodeType Constant(const char* title, const Type& (*type)())
{
    return TestNodeType(title, [type](NodeDeclaration& d, const NodeConfig&, const Bindings&) { d.Output("value", "Value", type()); });
}

TestNodeType s_ConstBool   = Constant("ConstBool",   BoolType);
TestNodeType s_ConstInt32  = Constant("ConstInt32",  Int32Type);
TestNodeType s_ConstFloat  = Constant("ConstFloat",  FloatType);
TestNodeType s_ConstString = Constant("ConstString", StringType);

TestNodeType s_ToString("ToString", [](NodeDeclaration& d, const NodeConfig&, const Bindings&)
{
    auto T = d.Variable("T");
    d.Input("value", "Value", T);
    d.Output("text", "Text", StringType());
});

TestNodeType s_Print("Print", [](NodeDeclaration& d, const NodeConfig&, const Bindings&)
{
    d.FlowInput("enter");
    d.Input("text", "Text", StringType());
    d.FlowOutput("exit");
});

TestNodeType s_Pass("Pass", [](NodeDeclaration& d, const NodeConfig&, const Bindings&)
{
    auto T = d.Variable("T");
    d.Input("in", "In", T);
    d.Output("out", "Out", T);
});

TestNodeType s_Pair("Pair", [](NodeDeclaration& d, const NodeConfig&, const Bindings&)
{
    auto T = d.Variable("T");
    auto U = d.Variable("U");
    d.Input("first", "First", T);
    d.Input("second", "Second", U);
    d.Output("a", "A", T);
    d.Output("b", "B", U);
});

TestNodeType s_MakeArray("MakeArray", [](NodeDeclaration& d, const NodeConfig&, const Bindings&)
{
    auto T = d.Variable("T");
    auto count = d.Group("in", 1, 16);
    for (int i = 0; i < count; ++i)
        d.Input("in." + std::to_string(i), "", T);
    d.Output("array", "Array", TypeTerm(ArrayValueType(), { T }));
});

TestNodeType s_Get("Get", [](NodeDeclaration& d, const NodeConfig&, const Bindings&)
{
    auto T = d.Variable("T");
    d.Input("array", "Array", TypeTerm(ArrayValueType(), { T }));
    d.Input("index", "Index", Int32Type());
    d.Output("item", "Item", T);
});

// Any Array<T>: an allowed set lists heads.
TestNodeType s_Length("Length", [](NodeDeclaration& d, const NodeConfig&, const Bindings&)
{
    auto T = d.Variable("T", { &ArrayValueType() });
    d.Input("array", "Array", T);
    d.Output("length", "Length", Int32Type());
});

TestNodeType s_Concat("Concat", [](NodeDeclaration& d, const NodeConfig&, const Bindings&)
{
    auto T = d.Variable("T");
    d.Input("a", "A", TypeTerm(ArrayValueType(), { T }));
    d.Input("b", "B", TypeTerm(ArrayValueType(), { T }));
    d.Output("out", "Out", TypeTerm(ArrayValueType(), { T }));
});

TestNodeType s_Find("Find", [](NodeDeclaration& d, const NodeConfig&, const Bindings&)
{
    auto K = d.Variable("K", { &Int32ValueType(), &StringValueType() });
    auto V = d.Variable("V");
    d.Input("map", "Map", TypeTerm(MapValueType(), { K, V }));
    d.Input("key", "Key", K);
    d.Output("value", "Value", V);
    d.Output("found", "Found", BoolType());
});

StructType s_Vector2("example.Vector2", { { "x", &FloatType() }, { "y", &FloatType() } });

TestNodeType s_MakeVector2("MakeVector2", [](NodeDeclaration& d, const NodeConfig&, const Bindings&)
{
    for (auto& field : s_Vector2.Fields())
        d.Input(field.m_Name, field.m_Name, *field.m_Type);
    d.Output("value", "Value", s_Vector2.AsType());
});

TestNodeType s_BreakVector2("BreakVector2", [](NodeDeclaration& d, const NodeConfig&, const Bindings&)
{
    d.Input("value", "Value", s_Vector2.AsType());
    for (auto& field : s_Vector2.Fields())
        d.Output(field.m_Name, field.m_Name, *field.m_Type);
});

// Its output's type flips with its input's, so types linked back to it never settle.
TestNodeType s_Flipper("Flipper", [](NodeDeclaration& d, const NodeConfig&, const Bindings& bindings)
{
    auto T = d.Variable("T");
    d.Input("value", "Value", T);
    d.Output("out", "Out", bindings.Get("T") == &Int32Type() ? FloatType() : Int32Type());
});

std::shared_ptr<const Registry> TestRegistry()
{
    static const std::shared_ptr<const Registry> registry = []
    {
        auto made = std::make_shared<Registry>();
        auto ok = RegisterBuiltins(*made).IsOk() && made->AddValueType(s_Vector2).IsOk();
        for (auto type : { &s_Add, &s_MakeMap, &s_BreakAny, &s_ForLoop, &s_ConstBool, &s_ConstInt32, &s_ConstFloat, &s_ConstString, &s_ToString, &s_Print,
                           &s_Pass, &s_Pair, &s_MakeArray, &s_Get, &s_Length, &s_Concat, &s_Find, &s_MakeVector2, &s_BreakVector2, &s_Flipper })
            ok = made->AddNodeType(*type).IsOk() && ok;
        if (!ok)
            test::Check(false, "the test registry is made");
        return made;
    }();
    return registry;
}

// A blueprint with one graph, and its test node types.
struct Fixture
{
    Fixture()
        : m_Blueprint(TestRegistry(), std::make_shared<TypeTable>())
        , m_Graph(m_Blueprint.AddGraph("Main"))
    {
    }

    Id Add(const char* type, NodeConfig config = {})
    {
        auto node = m_Graph.AddNode(std::string("test.") + type, std::move(config));
        if (!node)
            test::Check(false, "%s is added: %s", type, std::string(node.GetError().Message()).c_str());
        return node.ValueOr(0);
    }

    Id Pin(Id node, std::string_view key) const
    {
        auto found = m_Graph.FindNode(node);
        auto pin   = found ? found->FindPin(key) : nullptr;
        if (!pin)
            test::Check(false, "node %d has a pin %s", static_cast<int>(node), std::string(key).c_str());
        return pin ? pin->m_Id : 0;
    }

    std::string Type(Id node, std::string_view key) const
    {
        return m_Graph.TypeText(Pin(node, key));
    }

    Result<Id> Connect(Id from, std::string_view output, Id to, std::string_view input, LinkMode mode = LinkMode::Plain)
    {
        return m_Graph.Connect(Pin(from, output), Pin(to, input), mode);
    }

    // A link the test needs: one that isn't made fails the test.
    Id Link(Id from, std::string_view output, Id to, std::string_view input, LinkMode mode = LinkMode::Plain)
    {
        auto link = Connect(from, output, to, input, mode);
        if (!link)
            test::Check(false, "%s.%s -> %s.%s is made: %s", Name(from).c_str(), std::string(output).c_str(), Name(to).c_str(), std::string(input).c_str(), std::string(link.GetError().Message()).c_str());
        return link.ValueOr(0);
    }

    void Unlink(Id link)
    {
        if (auto result = m_Graph.Disconnect(link); !result)
            test::Check(false, "link %d is removed: %s", static_cast<int>(link), std::string(result.GetError().Message()).c_str());
    }

    void Remove(Id node)
    {
        if (auto result = m_Graph.RemoveNode(node); !result)
            test::Check(false, "node %d is removed: %s", static_cast<int>(node), std::string(result.GetError().Message()).c_str());
    }

    size_t PinCount(Id node) const
    {
        auto found = m_Graph.FindNode(node);
        return found ? found->m_Pins.size() : 0;
    }

    std::string Name(Id node) const
    {
        auto found = m_Graph.FindNode(node);
        return found ? found->Name() : "?";
    }

    Blueprint m_Blueprint;
    Graph&    m_Graph;
};

template <typename T>
std::string Message(const Result<T>& result)
{
    return result ? std::string() : std::string(result.GetError().Message());
}

NodeConfig Chosen(const char* variable, const Type& type)
{
    NodeConfig config;
    config.m_Types[variable] = &type;
    return config;
}

bool IsActive(const Graph& graph, const Result<Id>& link)
{
    auto found = link ? graph.FindLink(link.GetValue()) : nullptr;
    return found && found->m_Active;
}

std::string Reason(const Graph& graph, const Result<Id>& link)
{
    auto found = link ? graph.FindLink(link.GetValue()) : nullptr;
    return found ? std::string(found->m_Reason.Message()) : "no link";
}

NodeDeclaration Declared(const NodeType& type, const NodeConfig& config, const Bindings& bindings = {})
{
    NodeDeclaration declaration(config);
    type.Declare(declaration, config, bindings);
    return declaration;
}

std::string Keys(const NodeDeclaration& declaration)
{
    std::string keys;
    for (auto& pin : declaration.Pins())
        keys += (keys.empty() ? "" : " ") + pin.m_Key;
    return keys;
}

NodeConfig Groups(const char* group, int count)
{
    NodeConfig config;
    config.m_Groups[group] = count;
    return config;
}

} // namespace

TEST_CASE(NodeTypesDeclareFromConfig, "graph.node_types_declare_pins_from_their_config", nullptr, test::TestFlags_None)
{
    const NodeConfig none;
    auto add = Declared(s_Add, none);
    test::Check(Keys(add) == "in.0 in.1 out", "Add has its group's minimum of inputs: %s", Keys(add).c_str());
    test::Check(add.Variables().size() == 1 && add.Variables()[0].m_Name == "T" && add.Variables()[0].m_Allowed.size() == 3, "and one variable, T, that allows three types");
    test::Check(add.Pins()[0].m_Type.m_Variable == 0 && !add.Pins()[0].m_Type.m_Head && add.Pins()[2].m_Direction == Direction::Output, "its pins are of T, and out is an output");
    test::Check(add.Groups().size() == 1 && add.Groups()[0].m_Name == "in" && add.Groups()[0].m_Min == 2 && add.Groups()[0].m_Max == 16 && add.Groups()[0].m_Prefixes == std::vector<std::string>{ "in" }, "the group is named, with its bounds, and its prefix is its name");

    const auto five = Groups("in", 5), many = Groups("in", 99), few = Groups("in", 0);
    test::Check(Declared(s_Add, five).Pins().size() == 6, "a config gives the group 5 indices");
    test::Check(Declared(s_Add, many).Pins().size() == 17 && Declared(s_Add, few).Pins().size() == 3, "within the group's bounds");

    const auto pairs = Groups("pair", 2);
    auto map = Declared(s_MakeMap, pairs);
    test::Check(Keys(map) == "key.0 value.0 key.1 value.1 map", "a group's index can have a pin per prefix: %s", Keys(map).c_str());
    auto& out = map.Pins()[4].m_Type;
    test::Check(out.m_Head == &MapValueType() && out.m_Args.size() == 2 && out.m_Args[0].m_Variable == 0 && out.m_Args[1].m_Variable == 1, "and a type constructor takes variables: Map<K, V>");
}

TEST_CASE(NodeTypesDeclareFromBindings, "graph.node_types_declare_pins_from_bindings", nullptr, test::TestFlags_None)
{
    StructType vector2("example.Vector2", { { "x", &FloatType() }, { "y", &FloatType() } });
    const NodeConfig none;
    Bindings bound;
    bound.m_Types["T"] = &vector2.AsType();

    auto unbound = Declared(s_BreakAny, none);
    auto broken  = Declared(s_BreakAny, none, bound);
    test::Check(Keys(unbound) == "value", "without bindings, Break has its input only: %s", Keys(unbound).c_str());
    test::Check(Keys(broken) == "value x y" && broken.Pins()[1].m_Type.m_Head == &FloatValueType(), "with T bound to a struct, its fields too: %s", Keys(broken).c_str());
    test::Check(bound.Get("T") == &vector2.AsType() && !bound.Get("U"), "bindings give a variable's type, or null");
}

TEST_CASE(PinDeclarations, "graph.pins_declare_their_kind_default_and_requirement", nullptr, test::TestFlags_None)
{
    const NodeConfig none;
    auto loop = Declared(s_ForLoop, none);
    auto pins = loop.Pins();

    test::Check(Keys(loop) == "enter first last body index completed", "pins keep the order they were declared in: %s", Keys(loop).c_str());
    test::Check(pins[0].m_Flow && pins[0].m_Direction == Direction::Input && pins[3].m_Flow && pins[3].m_Direction == Direction::Output, "flow pins are flow pins");
    test::Check(!pins[1].m_Flow && pins[1].m_Type.m_Head == &Int32ValueType() && pins[1].m_Name == "First Index", "a data pin has its type and name");
    test::Check(pins[2].m_Default == Value::Int32(10) && !pins[1].m_Default.IsValid(), "a declared default, or none");
    test::Check(pins[0].m_Requirement == Requirement::Recommended && pins[1].m_Requirement == Requirement::Optional, "and a requirement, optional unless said");
}

TEST_CASE(TypeTermsOfTypes, "graph.type_terms_of_types", nullptr, test::TestFlags_None)
{
    Registry registry;
    TypeTable table;
    auto parsed = RegisterBuiltins(registry) ? table.Parse(registry, "Map<String,Array<Int32>>") : Result<const Type*>(BlueprintError::UnknownType);
    test::Check(parsed.IsOk(), "the type parses");
    if (!parsed)
        return;

    TypeTerm term = *parsed.GetValue();
    test::Check(term.m_Head == &MapValueType() && term.m_Args.size() == 2 && term.m_Args[0].m_Head == &StringValueType(), "a type's term has its head and arguments");
    test::Check(term.m_Args[1].m_Head == &ArrayValueType() && term.m_Args[1].m_Args.size() == 1 && term.m_Args[1].m_Args[0].m_Head == &Int32ValueType() && term.m_Args[1].m_Args[0].m_Args.empty(), "all the way down");
}

TEST_CASE(RegistryHoldsNodeTypes, "graph.registry_holds_node_types", nullptr, test::TestFlags_None)
{
    Registry registry;
    auto added   = registry.AddNodeType(s_Add);
    auto owned   = registry.AddNodeType(std::make_unique<TestNodeType>("MakeMap", s_MakeMap.m_Declare));
    auto twice   = registry.AddNodeType(std::make_unique<TestNodeType>("Add", s_Add.m_Declare));
    auto makeMap = registry.FindNodeType("test.MakeMap");

    test::Check(added && registry.FindNodeType("test.Add") == &s_Add, "a node type is found by its name");
    test::Check(owned && makeMap && makeMap != &s_MakeMap && makeMap->m_Title == "MakeMap", "one the registry keeps, too");
    test::Check(!twice && twice.GetError().Is(BlueprintError::Duplicate) && registry.FindNodeType("test.Add") == &s_Add, "a second node type of a name is refused");
    test::Check(!registry.FindNodeType("test.Subtract"), "and one nothing registered isn't found");
}

TEST_CASE(LinkDecidesTypes, "graph.a_link_decides_types_and_unlinking_undoes_it", nullptr, test::TestFlags_None)
{
    Fixture f;
    auto constant = f.Add("ConstInt32"), add = f.Add("Add");
    auto link = f.Connect(constant, "value", add, "in.0");
    test::Check(link.IsOk(), "Const Int32 -> Add.in.0 is made: %s", Message(link).c_str());
    test::Check(f.Type(add, "in.1") == "Int32" && f.Type(add, "out") == "Int32", "in.1 and out become Int32: %s, %s", f.Type(add, "in.1").c_str(), f.Type(add, "out").c_str());
    auto unlinked = f.m_Graph.Disconnect(link.ValueOr(0));
    test::Check(unlinked && f.Type(add, "out") == "?{Int32|Float|String}", "unlinked, out is a wildcard again: %s", f.Type(add, "out").c_str());
    test::Check(!f.m_Graph.IsBroken() && f.m_Graph.Diagnostics().empty(), "and nothing is wrong");
}

TEST_CASE(TypesFlowAnyWay, "graph.types_flow_both_ways_in_any_order", nullptr, test::TestFlags_None)
{
    std::string first;
    for (int order = 0; order < 2; ++order)
    {
        Fixture f;
        auto add1 = f.Add("Add"), add2 = f.Add("Add"), text = f.Add("ToString"), constant = f.Add("ConstFloat");
        struct Wire { Id from; const char* output; Id to; const char* input; };
        std::vector<Wire> wires = { { add1, "out", add2, "in.0" }, { add2, "out", text, "value" }, { constant, "value", add2, "in.1" } };
        if (order)
            std::reverse(wires.begin(), wires.end());
        bool made = true;
        for (auto& wire : wires)
            made = f.Connect(wire.from, wire.output, wire.to, wire.input).IsOk() && made;
        auto types = f.Type(add1, "in.0") + " " + f.Type(add1, "out") + " " + f.Type(text, "value");
        test::Check(made && types == "Float Float Float", "%s: the first Add's pins and To String's value are Float: %s", order ? "reversed" : "in order", types.c_str());
        if (order == 0)
            first = types;
        else
            test::Check(types == first, "both orders give the same types");
    }
}

TEST_CASE(ContradictingLinkRefused, "graph.a_link_that_contradicts_a_type_is_refused", nullptr, test::TestFlags_None)
{
    Fixture f;
    auto ints = f.Add("ConstInt32"), floats = f.Add("ConstFloat"), add = f.Add("Add");
    auto first   = f.Connect(ints, "value", add, "in.0");
    auto refused = f.Connect(floats, "value", add, "in.1");
    auto expected = "Float doesn't match Int32 (from " + f.Name(ints) + ".value -> " + f.Name(add) + ".in.0)";
    test::Check(first && !refused && refused.GetError().Is(BlueprintError::TypeMismatch), "Const Float -> Add.in.1 is refused");
    test::Check(Message(refused) == expected, "naming what decided the type: %s", Message(refused).c_str());
    test::Check(f.m_Graph.Links().size() == 1 && IsActive(f.m_Graph, first) && !f.m_Graph.IsBroken(), "the Int32 link stays, active");

    Fixture g;
    auto bools = g.Add("ConstBool"), add2 = g.Add("Add");
    auto notAllowed = g.Connect(bools, "value", add2, "in.0");
    test::Check(Message(notAllowed) == g.Name(add2) + ".T takes only {Int32|Float|String}, not Bool", "Const Bool -> Add is refused by Add's allowed set: %s", Message(notAllowed).c_str());
}

TEST_CASE(ChosenTypeHolds, "graph.a_chosen_type_holds_against_links", nullptr, test::TestFlags_None)
{
    Fixture f;
    auto ints = f.Add("ConstInt32"), add = f.Add("Add", Chosen("T", FloatType()));
    test::Check(f.Type(add, "out") == "Float", "an unlinked Add chosen as Float is Float");
    auto refused = f.Connect(ints, "value", add, "in.0");
    test::Check(!refused && Message(refused).find("the type chosen on " + f.Name(add)) != std::string::npos, "Const Int32 -> it is refused: %s", Message(refused).c_str());
}

TEST_CASE(VariadicPins, "graph.group_pins_are_added_and_removed", nullptr, test::TestFlags_None)
{
    Fixture f;
    auto a = f.Add("ConstInt32"), b = f.Add("ConstInt32"), add = f.Add("Add");
    auto grew = f.m_Graph.AddGroupPin(add, "in").IsOk() && f.m_Graph.AddGroupPin(add, "in").IsOk();
    test::Check(grew && f.PinCount(add) == 5, "Add grows to 4 inputs: %d pins", static_cast<int>(f.PinCount(add)));
    auto removedLink = f.Connect(a, "value", add, "in.1");
    auto movedLink   = f.Connect(b, "value", add, "in.3");
    test::Check(f.Type(add, "in.0") == "Int32" && f.Type(add, "in.2") == "Int32", "a link to any input types them all");

    auto moved   = f.Pin(add, "in.3");
    auto removed = f.m_Graph.RemoveGroupPin(add, "in", 1);
    test::Check(removed.IsOk(), "removing in.1 is allowed, above the minimum of 2: %s", Message(removed).c_str());
    test::Check(f.PinCount(add) == 4 && f.Pin(add, "in.2") == moved, "in.3 is in.2 now, with its ID");
    test::Check(f.m_Graph.Links().size() == 1 && !f.m_Graph.FindLink(removedLink.ValueOr(0)) && IsActive(f.m_Graph, movedLink), "its link stays; the removed pin's link goes");

    auto tooMany = Result<void>();
    for (int i = 0; i < 20 && tooMany; ++i)
        tooMany = f.m_Graph.AddGroupPin(add, "in");
    test::Check(!tooMany && tooMany.GetError().Is(BlueprintError::OutOfRange) && f.PinCount(add) == 17, "a group grows only to its maximum: %s", Message(tooMany).c_str());

    Fixture g;
    auto two = g.Add("Add");
    auto fewest = g.m_Graph.RemoveGroupPin(two, "in", 0);
    auto none   = g.m_Graph.AddGroupPin(two, "out");
    test::Check(!fewest && fewest.GetError().Is(BlueprintError::OutOfRange), "a 2-input Add can't go below its minimum: %s", Message(fewest).c_str());
    test::Check(!none && none.GetError().Is(BlueprintError::NotFound), "and has no group out");
}

TEST_CASE(ArrayElementTypes, "graph.arrays_pass_element_types_both_ways", nullptr, test::TestFlags_None)
{
    Fixture f;
    auto ints = f.Add("ConstInt32"), make = f.Add("MakeArray"), get = f.Add("Get");
    f.Link(ints, "value", make, "in.0");
    f.Link(make, "array", get, "array");
    test::Check(f.Type(make, "array") == "Array<Int32>" && f.Type(get, "item") == "Int32", "Int32 -> Make Array -> Get gives Array<Int32> and Int32: %s, %s", f.Type(make, "array").c_str(), f.Type(get, "item").c_str());

    Fixture g;
    auto make2 = g.Add("MakeArray"), get2 = g.Add("Get"), print = g.Add("Print"), ints2 = g.Add("ConstInt32");
    g.Link(make2, "array", get2, "array");
    g.Link(get2, "item", print, "text");
    test::Check(g.Type(make2, "in.0") == "String", "Get.item -> Print makes Make Array's inputs String: %s", g.Type(make2, "in.0").c_str());
    auto refused = g.Connect(ints2, "value", make2, "in.0");
    test::Check(!refused, "then Const Int32 -> Make Array is refused: %s", Message(refused).c_str());
}

TEST_CASE(SelfContainingTypes, "graph.a_type_that_would_contain_itself_is_refused", nullptr, test::TestFlags_None)
{
    Fixture f;
    auto first = f.Add("MakeArray"), second = f.Add("MakeArray");
    f.Link(first, "array", second, "in.0");
    auto refused = f.Connect(second, "array", first, "in.0");
    test::Check(!refused && Message(refused).find("which contains it") != std::string::npos, "Make Array 2 -> Make Array 1 is refused: %s", Message(refused).c_str());

    Fixture g;
    auto pass = g.Add("Pass"), make = g.Add("MakeArray");
    g.Link(pass, "out", make, "in.0");
    auto merged = g.Connect(make, "array", pass, "in");
    test::Check(!merged, "so is Make Array -> Pass, which feeds it, through a merged variable: %s", Message(merged).c_str());
}

TEST_CASE(StructPins, "graph.make_and_break_pins_come_from_the_struct", nullptr, test::TestFlags_None)
{
    Fixture f;
    auto make = f.Add("MakeVector2"), split = f.Add("BreakVector2"), add = f.Add("Add");
    f.Link(make, "value", split, "value");
    f.Link(split, "x", add, "in.0");
    test::Check(f.PinCount(split) == 3 && f.Type(make, "value") == "example.Vector2" && f.Type(add, "out") == "Float", "Break Vector2 has value, x and y, and x makes Add Float: %s", f.Type(add, "out").c_str());
}

TEST_CASE(TypeDecidedLater, "graph.a_wildcard_takes_a_type_decided_later", nullptr, test::TestFlags_None)
{
    Fixture f;
    auto add = f.Add("Add"), text = f.Add("ToString"), strings = f.Add("ConstString");
    f.Link(add, "out", text, "value");
    auto before = f.Type(text, "value");
    f.Link(strings, "value", add, "in.0");
    test::Check(before == "?{Int32|Float|String}" && f.Type(text, "value") == "String", "To String's value is Add's wildcard, then String: %s -> %s", before.c_str(), f.Type(text, "value").c_str());
}

TEST_CASE(ChosenTypeChecked, "graph.a_chosen_type_is_checked_like_a_link", nullptr, test::TestFlags_None)
{
    Fixture f;
    auto ints = f.Add("ConstInt32"), linked = f.Add("Add"), unlinked = f.Add("Add"), strings = f.Add("ConstString"), print = f.Add("Print");
    f.Link(ints, "value", linked, "in.0");
    auto refused  = f.m_Graph.SetConfig(linked, Chosen("T", FloatType()));
    auto accepted = f.m_Graph.SetConfig(unlinked, Chosen("T", FloatType()));
    test::Check(!refused && refused.GetError().Is(BlueprintError::TypeMismatch), "Float on the Add linked to an Int32 is refused: %s", Message(refused).c_str());
    test::Check(accepted.IsOk() && f.Type(unlinked, "out") == "Float" && f.Type(linked, "out") == "Int32", "Float on an unlinked Add is accepted: %s", Message(accepted).c_str());
    auto later = f.Connect(strings, "value", print, "text");
    test::Check(later && !f.m_Graph.IsBroken(), "an unrelated link is still made, and nothing is wrong: %s", Message(later).c_str());

    auto forced = f.m_Graph.SetConfig(linked, Chosen("T", FloatType()), true);
    test::Check(forced.IsOk() && f.Type(linked, "out") == "Float" && f.m_Graph.IsBroken(), "forced, Float is chosen, and the Int32 link goes disabled: %s", Message(forced).c_str());
}

TEST_CASE(ChosenTypeOutsideSet, "graph.a_chosen_type_its_variable_doesnt_take_is_refused", nullptr, test::TestFlags_None)
{
    Fixture f;
    auto refused = f.m_Graph.AddNode("test.Add", Chosen("T", BoolType()));
    test::Check(!refused && f.m_Graph.Nodes().empty(), "a new Add chosen as Bool is refused: %s", Message(refused).c_str());
    auto add = f.Add("Add");
    auto changed = f.m_Graph.SetConfig(add, Chosen("T", BoolType()));
    auto forced  = f.m_Graph.SetConfig(add, Chosen("T", BoolType()), true);
    test::Check(!changed && !forced, "and so is choosing Bool on an Add there is, even when forced: %s", Message(forced).c_str());
}

TEST_CASE(RefusalNamesTheNode, "graph.a_refusal_names_the_node_whose_set_applies", nullptr, test::TestFlags_None)
{
    Fixture f;
    auto pass = f.Add("Pass"), add = f.Add("Add"), bools = f.Add("ConstBool");
    f.Link(pass, "out", add, "in.0");
    auto refused = f.Connect(bools, "value", pass, "in");
    test::Check(Message(refused).starts_with(f.Name(add) + ".T"), "Const Bool -> Pass, which feeds Add, is refused for Add's set: %s", Message(refused).c_str());
}

TEST_CASE(TwoVariables, "graph.two_type_variables_of_a_node_stay_apart", nullptr, test::TestFlags_None)
{
    Fixture f;
    auto ints = f.Add("ConstInt32"), strings = f.Add("ConstString"), pair = f.Add("Pair"), add = f.Add("Add");
    f.Link(ints, "value", pair, "first");
    f.Link(strings, "value", pair, "second");
    test::Check(f.Type(pair, "a") == "Int32" && f.Type(pair, "b") == "String", "Pair's T is Int32, and its U is String");
    f.Link(pair, "a", add, "in.0");
    auto refused = f.Connect(pair, "b", add, "in.1");
    test::Check(!refused, "Pair's a and b into one Add conflict: %s", Message(refused).c_str());
}

TEST_CASE(AllowedHeads, "graph.an_allowed_set_takes_any_type_of_a_head", nullptr, test::TestFlags_None)
{
    Fixture f;
    auto make = f.Add("MakeArray"), length = f.Add("Length"), length2 = f.Add("Length"), ints = f.Add("ConstInt32");
    auto accepted = f.Connect(make, "array", length, "array");
    test::Check(accepted && f.Type(length, "array") == "Array<?>", "Make Array -> Length is made, as Array<?>: %s", f.Type(length, "array").c_str());
    auto refused = f.Connect(ints, "value", length2, "array");
    test::Check(!refused, "Const Int32 -> Length is refused: %s", Message(refused).c_str());
}

TEST_CASE(NestedConflict, "graph.a_conflict_inside_a_constructed_type_names_both_sources", nullptr, test::TestFlags_None)
{
    Fixture f;
    auto ints = f.Add("ConstInt32"), floats = f.Add("ConstFloat"), make1 = f.Add("MakeArray"), make2 = f.Add("MakeArray"), concat = f.Add("Concat");
    f.Link(ints, "value", make1, "in.0");
    f.Link(floats, "value", make2, "in.0");
    f.Link(make1, "array", concat, "a");
    auto refused = f.Connect(make2, "array", concat, "b");
    auto expected = "Float (from " + f.Name(floats) + ".value -> " + f.Name(make2) + ".in.0) doesn't match Int32 (from " + f.Name(make1) + ".array -> " + f.Name(concat) + ".a)";
    test::Check(Message(refused) == expected, "Array<Float> into Concat, which has Array<Int32>, is refused, naming the links that decided both: %s", Message(refused).c_str());
}

TEST_CASE(ChosenTypesMeet, "graph.two_chosen_types_meeting_are_refused", nullptr, test::TestFlags_None)
{
    Fixture f;
    auto ints = f.Add("Add", Chosen("T", Int32Type())), floats = f.Add("Add", Chosen("T", FloatType()));
    auto refused = f.Connect(ints, "out", floats, "in.0");
    auto message = Message(refused);
    test::Check(!refused && message.find(f.Name(ints)) != std::string::npos && message.find(f.Name(floats)) != std::string::npos, "refused, naming both choices: %s", message.c_str());
}

TEST_CASE(ChosenConstructedType, "graph.a_chosen_type_can_be_constructed", nullptr, test::TestFlags_None)
{
    Fixture f;
    const std::array<const Type*, 1> floats = { &FloatType() };
    auto make = f.Add("MakeArray", Chosen("T", *f.m_Blueprint.GetTypes().Get(ArrayValueType(), floats)));
    test::Check(f.Type(make, "array") == "Array<Array<Float>>", "Make Array with T chosen as Array<Float> makes Array<Array<Float>>: %s", f.Type(make, "array").c_str());
}

TEST_CASE(ForcedLinkWins, "graph.a_forced_link_disables_the_links_it_conflicts_with", nullptr, test::TestFlags_None)
{
    Fixture f;
    auto ints = f.Add("ConstInt32"), floats = f.Add("ConstFloat"), add = f.Add("Add");
    auto plain  = f.Connect(ints, "value", add, "in.0");
    auto forced = f.Connect(floats, "value", add, "in.1", LinkMode::Force);
    test::Check(forced && f.m_Graph.Links().size() == 2, "Const Float -> Add.in.1, forced, is made, and both links are there: %s", Message(forced).c_str());
    test::Check(!IsActive(f.m_Graph, plain) && Reason(f.m_Graph, plain).find("doesn't match") != std::string::npos, "Const Int32 -> Add.in.0 is disabled: %s", Reason(f.m_Graph, plain).c_str());
    auto diagnostics = f.m_Graph.Diagnostics();
    test::Check(f.Type(add, "out") == "Float" && f.m_Graph.IsBroken(), "Add is Float, and the graph is broken until the conflict is resolved");
    test::Check(diagnostics.size() == 1 && diagnostics[0].m_Link == plain.ValueOr(0) && diagnostics[0].m_Severity == Severity::Error && diagnostics[0].m_Graph == f.m_Graph.GetId(), "the disabled link has an error diagnostic");
}

TEST_CASE(ForcedLinkGoes, "graph.removing_a_forced_link_activates_what_it_disabled", nullptr, test::TestFlags_None)
{
    Fixture f;
    auto ints = f.Add("ConstInt32"), floats = f.Add("ConstFloat"), add = f.Add("Add");
    auto plain  = f.Connect(ints, "value", add, "in.0");
    auto forced = f.Connect(floats, "value", add, "in.1", LinkMode::Force);
    f.Unlink(forced.ValueOr(0));
    test::Check(IsActive(f.m_Graph, plain) && f.Type(add, "out") == "Int32" && !f.m_Graph.IsBroken(), "Const Int32 -> Add.in.0 is active again, and Add is Int32: %s", f.Type(add, "out").c_str());

    // The latest forced link wins.
    auto first  = f.Connect(floats, "value", add, "in.1", LinkMode::Force);
    auto second = f.Connect(ints, "value", add, "in.0", LinkMode::Force);
    test::Check(IsActive(f.m_Graph, second) && !IsActive(f.m_Graph, first) && f.Type(add, "out") == "Int32", "of two forced links, the latest wins");
}

TEST_CASE(KeptLinks, "graph.kept_links_that_dont_fit_are_disabled_in_their_order", nullptr, test::TestFlags_None)
{
    Fixture f;
    auto ints = f.Add("ConstInt32"), floats = f.Add("ConstFloat"), add = f.Add("Add");
    auto first  = f.Connect(ints, "value", add, "in.0", LinkMode::Keep);
    auto second = f.Connect(floats, "value", add, "in.1", LinkMode::Keep);
    test::Check(first && second && IsActive(f.m_Graph, first) && !IsActive(f.m_Graph, second), "the first is active, the second is disabled: %s", Reason(f.m_Graph, second).c_str());
    f.Unlink(first.ValueOr(0));
    test::Check(IsActive(f.m_Graph, second) && f.Type(add, "out") == "Float", "without the first, the second is active, and Add is Float: %s", f.Type(add, "out").c_str());
}

TEST_CASE(CastLinks, "graph.a_cast_link_joins_two_types_and_changes_neither", nullptr, test::TestFlags_None)
{
    Fixture f;
    auto ints = f.Add("Add", Chosen("T", Int32Type())), floats = f.Add("Add", Chosen("T", FloatType())), bools = f.Add("ConstBool"), floats2 = f.Add("Add", Chosen("T", FloatType()));
    auto plain = f.Connect(ints, "out", floats, "in.0");
    auto cast  = f.Connect(ints, "out", floats, "in.0", LinkMode::Cast);
    auto made  = cast ? f.m_Graph.FindLink(cast.GetValue()) : nullptr;
    test::Check(!plain && cast, "Int32 -> Float is refused as a plain link, and made as a cast: %s", Message(cast).c_str());
    test::Check(made && made->m_Cast == "Int32->Float" && f.Type(ints, "out") == "Int32" && f.Type(floats, "in.0") == "Float", "the link carries the cast, and both ends keep their types");
    auto noCast = f.Connect(bools, "value", floats2, "in.0", LinkMode::Cast);
    test::Check(!noCast && noCast.GetError().Is(BlueprintError::NoCast), "Bool -> Float has no cast, and is refused: %s", Message(noCast).c_str());

    Fixture g;
    auto add = g.Add("Add"), floats3 = g.Add("Add", Chosen("T", FloatType()));
    auto undecided = g.Connect(add, "out", floats3, "in.0", LinkMode::Cast);
    test::Check(!undecided && undecided.GetError().Is(BlueprintError::Undecided), "a cast from a wildcard is refused: %s", Message(undecided).c_str());
}

TEST_CASE(CastLinkUndecided, "graph.a_cast_link_whose_end_is_undecided_is_disabled", nullptr, test::TestFlags_None)
{
    Fixture f;
    auto ints = f.Add("ConstInt32"), add = f.Add("Add"), floats = f.Add("Add", Chosen("T", FloatType()));
    auto typing = f.Connect(ints, "value", add, "in.0");
    auto cast   = f.Connect(add, "out", floats, "in.0", LinkMode::Cast);
    test::Check(cast.IsOk(), "Add (Int32) -> Add (Float), as a cast, is made: %s", Message(cast).c_str());
    f.Unlink(typing.ValueOr(0));
    test::Check(!IsActive(f.m_Graph, cast) && f.m_Graph.IsBroken(), "unlinking the Int32 disables the cast: %s", Reason(f.m_Graph, cast).c_str());
    f.Link(ints, "value", add, "in.0");
    test::Check(IsActive(f.m_Graph, cast) && !f.m_Graph.IsBroken(), "linking it again makes the cast active");

    auto strings = f.Add("ConstString");
    f.Link(strings, "value", add, "in.0", LinkMode::Force);
    test::Check(!IsActive(f.m_Graph, cast) && Reason(f.m_Graph, cast) == "its cast is Int32->Float, not String->Float", "a cast keeps its types: %s", Reason(f.m_Graph, cast).c_str());
}

TEST_CASE(PinsFollowTypes, "graph.pins_that_follow_types_keep_their_ids_and_links", nullptr, test::TestFlags_None)
{
    Fixture f;
    auto make = f.Add("MakeVector2"), split = f.Add("BreakAny"), add = f.Add("Add");
    test::Check(f.PinCount(split) == 1, "an unlinked Break has its input only");
    auto typing = f.Connect(make, "value", split, "value");
    test::Check(f.PinCount(split) == 3 && f.Type(split, "x") == "Float", "linked to a Vector2, it has x and y: %d pins", static_cast<int>(f.PinCount(split)));
    auto x = f.Pin(split, "x");
    auto fromX = f.m_Graph.Connect(x, f.Pin(add, "in.0"));
    test::Check(fromX && f.Type(add, "out") == "Float", "x makes Add Float");

    f.Unlink(typing.ValueOr(0));
    test::Check(f.PinCount(split) == 1 && !IsActive(f.m_Graph, fromX) && f.Type(add, "out") == "?{Int32|Float|String}", "unlinked, x and y go, the link from x stays disabled, and Add is a wildcard again: %s", Reason(f.m_Graph, fromX).c_str());
    test::Check(f.m_Graph.FindLink(fromX.ValueOr(0)) && f.m_Graph.FindLink(fromX.ValueOr(0))->m_Reason.Is(BlueprintError::NoPin), "the reason is its pin");
    f.Link(make, "value", split, "value");
    test::Check(f.Pin(split, "x") == x && IsActive(f.m_Graph, fromX) && f.Type(add, "out") == "Float", "linked again, x comes back with its ID, and its link is active again");
}

TEST_CASE(TypesNeverSettle, "graph.types_that_never_settle_are_refused_or_reported", nullptr, test::TestFlags_None)
{
    Fixture f;
    auto flipper = f.Add("Flipper"), pass = f.Add("Pass");
    auto first  = f.Connect(flipper, "out", pass, "in");
    auto second = f.Connect(pass, "out", flipper, "value");
    test::Check(first && !second && second.GetError().Is(BlueprintError::Unsettled), "Flipper -> Pass is made; Pass -> Flipper, which flips its pins, is refused: %s", Message(second).c_str());
    auto forced = f.Connect(pass, "out", flipper, "value", LinkMode::Force);
    auto diagnostics = f.m_Graph.Diagnostics();
    auto unsettled = std::any_of(diagnostics.begin(), diagnostics.end(), [](const Diagnostic& d) { return d.m_Error.Is(BlueprintError::Unsettled) && d.m_Node; });
    test::Check(forced && f.m_Graph.IsBroken() && unsettled, "forced, it's made, and the graph is broken");
}

TEST_CASE(MapTypes, "graph.maps_take_key_and_value_types", nullptr, test::TestFlags_None)
{
    Fixture f;
    auto strings = f.Add("ConstString"), ints = f.Add("ConstInt32"), make = f.Add("MakeMap"), find = f.Add("Find"), bools = f.Add("ConstBool"), make2 = f.Add("MakeMap");
    f.Link(strings, "value", make, "key.0");
    f.Link(ints, "value", make, "value.0");
    f.Link(make, "map", find, "map");
    test::Check(f.Type(make, "map") == "Map<String,Int32>" && f.Type(find, "key") == "String" && f.Type(find, "value") == "Int32", "String keys and Int32 values make Map<String,Int32>, and Find takes a String for an Int32: %s", f.Type(make, "map").c_str());
    auto refused = f.Connect(bools, "value", make2, "key.0");
    test::Check(!refused, "a Bool key is refused: %s", Message(refused).c_str());

    auto added   = f.m_Graph.AddGroupPin(make, "pair");
    auto removed = f.m_Graph.RemoveGroupPin(make, "pair", 0);
    test::Check(added && removed && f.PinCount(make) == 3 && f.Type(make, "map") == "Map<?{Int32|String},?>", "a pair is added, and the first pair goes with its links: %s", f.Type(make, "map").c_str());
}

TEST_CASE(LinksJoinOutputsToInputs, "graph.links_join_an_output_to_an_input_of_another_node", nullptr, test::TestFlags_None)
{
    Fixture f;
    auto loop = f.Add("ForLoop"), print = f.Add("Print"), ints = f.Add("ConstInt32"), add = f.Add("Add");
    struct Case { Id from; Id to; BlueprintError code; const char* what; };
    const Case cases[] =
    {
        { f.Pin(add, "in.0"),   f.Pin(add, "in.1"),    BlueprintError::BadLink,  "two inputs"           },
        { f.Pin(ints, "value"), f.Pin(print, "enter"), BlueprintError::BadLink,  "data to flow"         },
        { f.Pin(add, "out"),    f.Pin(add, "in.0"),    BlueprintError::BadLink,  "a node to itself"     },
        { f.Pin(ints, "value"), 99999,                 BlueprintError::NotFound, "a pin nothing has"    },
    };
    for (auto& c : cases)
    {
        auto refused = f.m_Graph.Connect(c.from, c.to);
        test::Check(!refused && refused.GetError().Is(c.code), "a link of %s is refused: %s", c.what, Message(refused).c_str());
    }
    auto unknown = f.m_Graph.AddNode("test.Subtract");
    test::Check(!unknown && unknown.GetError().Is(BlueprintError::UnknownType), "so is a node of a type nothing registered: %s", Message(unknown).c_str());

    // A flow output takes one link, and a flow input many.
    auto print2 = f.Add("Print");
    auto toFirst  = f.Connect(loop, "body", print, "enter");
    auto toSecond = f.Connect(loop, "body", print2, "enter");
    auto fromDone = f.Connect(loop, "completed", print2, "enter");
    test::Check(toSecond && fromDone && !f.m_Graph.FindLink(toFirst.ValueOr(0)) && f.m_Graph.Links().size() == 2, "a new flow link from an output replaces its old one, and an input takes both");

    // A data input takes one link.
    auto ints2 = f.Add("ConstInt32");
    auto old   = f.Connect(ints, "value", add, "in.0");
    auto now   = f.Connect(ints2, "value", add, "in.0");
    test::Check(now && !f.m_Graph.FindLink(old.ValueOr(0)), "a new data link to an input replaces its old one");
}

TEST_CASE(CanConnectChangesNothing, "graph.asking_whether_a_link_fits_changes_nothing", nullptr, test::TestFlags_None)
{
    Fixture f;
    auto ints = f.Add("ConstInt32"), floats = f.Add("ConstFloat"), add = f.Add("Add");
    f.Link(ints, "value", add, "in.0");
    auto fits    = f.m_Graph.CanConnect(f.Pin(ints, "value"), f.Pin(add, "in.1"));
    auto doesnt  = f.m_Graph.CanConnect(f.Pin(floats, "value"), f.Pin(add, "in.1"));
    auto forcing = f.m_Graph.CanConnect(f.Pin(floats, "value"), f.Pin(add, "in.1"), LinkMode::Force);
    test::Check(fits && !doesnt && doesnt.GetError().Is(BlueprintError::TypeMismatch) && forcing, "Int32 fits, Float doesn't, and Float forced does: %s", Message(doesnt).c_str());
    test::Check(f.m_Graph.Links().size() == 1 && f.Type(add, "out") == "Int32" && !f.m_Graph.IsBroken(), "and the graph is as it was");
}

TEST_CASE(RemovingNodes, "graph.removing_a_node_removes_its_links", nullptr, test::TestFlags_None)
{
    Fixture f;
    auto ints = f.Add("ConstInt32"), floats = f.Add("ConstFloat"), add = f.Add("Add"), text = f.Add("ToString");
    auto plain  = f.Connect(ints, "value", add, "in.0");
    auto forced = f.Connect(floats, "value", add, "in.1", LinkMode::Force);
    auto out    = f.Connect(add, "out", text, "value");
    auto removed = f.m_Graph.RemoveNode(floats);
    test::Check(removed && !f.m_Graph.FindNode(floats) && !f.m_Graph.FindLink(forced.ValueOr(0)), "the node and its link go");
    test::Check(IsActive(f.m_Graph, plain) && IsActive(f.m_Graph, out) && f.Type(text, "value") == "Int32" && !f.m_Graph.IsBroken(), "and the link it disabled is active again");
    f.Remove(add);
    test::Check(f.m_Graph.Links().empty() && f.Type(text, "value") == "?", "removing Add takes both of its links");
    auto again = f.m_Graph.RemoveNode(add);
    test::Check(!again && again.GetError().Is(BlueprintError::NotFound), "a node is removed once");
}

TEST_CASE(OneIdSpace, "graph.graphs_nodes_pins_and_links_share_one_space_of_ids", nullptr, test::TestFlags_None)
{
    Fixture f;
    auto& other = f.m_Blueprint.AddGraph("Other");
    auto ints = f.Add("ConstInt32"), add = f.Add("Add");
    auto link = f.Connect(ints, "value", add, "in.0");
    auto elsewhere = other.AddNode("test.Add");

    std::vector<Id> ids = { f.m_Graph.GetId(), other.GetId(), ints, add, link.ValueOr(0), elsewhere.ValueOr(0) };
    for (auto& node : f.m_Graph.Nodes())
        for (auto& pin : node.m_Pins)
            ids.push_back(pin.m_Id);
    for (auto& node : other.Nodes())
        for (auto& pin : node.m_Pins)
            ids.push_back(pin.m_Id);
    std::sort(ids.begin(), ids.end());
    test::Check(ids.front() > 0 && std::adjacent_find(ids.begin(), ids.end()) == ids.end(), "every ID is unique: %d IDs", static_cast<int>(ids.size()));

    auto across = f.m_Graph.Connect(f.Pin(ints, "value"), other.FindNode(elsewhere.ValueOr(0))->FindPin("in.0")->m_Id);
    test::Check(!across && across.GetError().Is(BlueprintError::NotFound), "and a link can't join two graphs: %s", Message(across).c_str());
    test::Check(f.m_Blueprint.FindGraph(other.GetId()) == &other && f.m_Blueprint.Graphs().size() == 2, "the blueprint finds its graphs");
}

TEST_CASE(FailedLinkLeavesNoTrace, "graph.a_link_that_fails_part_way_leaves_no_trace", nullptr, test::TestFlags_None)
{
    // Map<String,Int32> into a Find whose V is String: K would be String, then V fails.
    Fixture f;
    auto strings = f.Add("ConstString"), ints = f.Add("ConstInt32"), make = f.Add("MakeMap"), find = f.Add("Find"), print = f.Add("Print");
    f.Link(strings, "value", make, "key.0");
    f.Link(ints, "value", make, "value.0");
    f.Link(find, "value", print, "text");
    auto kept = f.Connect(make, "map", find, "map", LinkMode::Keep);
    test::Check(kept && !IsActive(f.m_Graph, kept), "Map<String,Int32> into a Find of String values is kept, disabled: %s", Reason(f.m_Graph, kept).c_str());
    test::Check(f.Type(find, "key") == "?{Int32|String}" && f.Type(find, "map") == "Map<?{Int32|String},String>", "and Find's key isn't String: %s, %s", f.Type(find, "key").c_str(), f.Type(find, "map").c_str());
}

TEST_CASE(PlainLinkDisablingAnother, "graph.a_plain_link_that_would_disable_another_is_refused", nullptr, test::TestFlags_None)
{
    // Linking a Float into Break Any takes its x away, and with it the link from x.
    Fixture f;
    auto make = f.Add("MakeVector2"), split = f.Add("BreakAny"), add = f.Add("Add"), floats = f.Add("ConstFloat");
    auto typing = f.Connect(make, "value", split, "value");
    auto fromX  = f.Connect(split, "x", add, "in.0");
    auto plain  = f.Connect(floats, "value", split, "value");
    auto expected = f.Name(split) + ".x -> " + f.Name(add) + ".in.0 would be disabled: its pin isn't on the node now";
    test::Check(typing && fromX && !plain && plain.GetError().Is(BlueprintError::NoPin) && Message(plain) == expected, "the plain link is refused, for the link it would disable: %s", Message(plain).c_str());
    test::Check(IsActive(f.m_Graph, typing) && IsActive(f.m_Graph, fromX) && f.PinCount(split) == 3, "and the graph is as it was");
    auto forced = f.Connect(floats, "value", split, "value", LinkMode::Force);
    test::Check(forced && !IsActive(f.m_Graph, fromX) && !f.m_Graph.FindLink(typing.ValueOr(0)) && f.PinCount(split) == 1, "forced, it replaces the Vector2 link, and the link from x goes disabled");
}

TEST_CASE(RememberedKeysMove, "graph.removing_a_group_pin_moves_remembered_keys_too", nullptr, test::TestFlags_None)
{
    // A pin the config took away keeps its ID and its link, and moves down with the others.
    Fixture f;
    auto ints = f.Add("ConstInt32"), add = f.Add("Add", [] { NodeConfig config; config.m_Groups["in"] = 4; return config; }());
    auto link = f.Connect(ints, "value", add, "in.3");
    auto last = f.Pin(add, "in.3");
    NodeConfig three;
    three.m_Groups["in"] = 3;
    auto plain  = f.m_Graph.SetConfig(add, three);
    auto forced = f.m_Graph.SetConfig(add, three, true);
    test::Check(!plain && forced && f.PinCount(add) == 4 && !IsActive(f.m_Graph, link), "taking in.3 away disables its link, so it needs forcing: %s", Message(plain).c_str());
    auto removed = f.m_Graph.RemoveGroupPin(add, "in", 0);
    auto added   = f.m_Graph.AddGroupPin(add, "in");
    test::Check(removed && added && f.Pin(add, "in.2") == last && IsActive(f.m_Graph, link), "after in.0 goes, the remembered in.3 comes back as in.2, with its link");
}

TEST_CASE(AllowedSetsMeet, "graph.allowed_sets_of_linked_variables_meet", nullptr, test::TestFlags_None)
{
    Fixture f;
    auto add = f.Add("Add"), make = f.Add("MakeMap"), length = f.Add("Length"), add2 = f.Add("Add");
    auto keys = f.Connect(add, "out", make, "key.0");
    test::Check(keys && f.Type(add, "in.0") == "?{Int32|String}", "Add's T linked to a map's key takes the types both take: %s", f.Type(add, "in.0").c_str());
    auto refused = f.Connect(add2, "out", length, "array");
    test::Check(Message(refused) == f.Name(add2) + ".T takes {Int32|Float|String} and " + f.Name(length) + ".T takes {Array}: no type is both", "two sets without a common type are refused: %s", Message(refused).c_str());
}
