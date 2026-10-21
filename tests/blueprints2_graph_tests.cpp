//------------------------------------------------------------------------------
// blueprints2-example's runtime: node types.
//------------------------------------------------------------------------------
# include "harness.h"
# include "crude_blueprint.h"
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
