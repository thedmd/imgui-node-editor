//------------------------------------------------------------------------------
// blueprints2-example's runtime: blueprints saved to files, and loaded back.
//------------------------------------------------------------------------------
# include "blueprints2_fixture.h"
# include <crude_json.h>
# include <algorithm>
# include <functional>
# include <memory>
# include <string>

using namespace blueprints2_tests;

namespace {

StructType s_Vector2("example.Vector2", { { "x", &FloatType() }, { "y", &FloatType() } });

TestNodeType Constant(const char* title, const Type& (*type)())
{
    return TestNodeType(title, [type](NodeDeclaration& d, const NodeConfig&, const Bindings&) { d.Output("value", "Value", type()); });
}

TestNodeType s_ConstInt32 = Constant("ConstInt32", Int32Type);
TestNodeType s_ConstFloat = Constant("ConstFloat", FloatType);

TestNodeType s_Add("Add", [](NodeDeclaration& d, const NodeConfig&, const Bindings&)
{
    auto T = d.Variable("T", { &Int32ValueType(), &FloatValueType(), &StringValueType() });
    auto count = d.Group("in", 2, 16);
    for (int i = 0; i < count; ++i)
        d.Input("in." + std::to_string(i), "", T);
    d.Output("result", "Result", T);
});

// Its value is in its config, as a Constant's is.
TestNodeType s_Hold("Hold", [](NodeDeclaration& d, const NodeConfig&, const Bindings&)
{
    auto T = d.Variable("T");
    d.Output("value", "Value", T);
});

TestNodeType s_Start("Start", [](NodeDeclaration& d, const NodeConfig&, const Bindings&) { d.FlowOutput("then"); }, {}, {}, "Start");

TestNodeType s_Print("Print", [](NodeDeclaration& d, const NodeConfig&, const Bindings&)
{
    d.FlowInput("enter");
    d.Input("text", "Text", StringType());
    d.FlowOutput("then");
});

TestNodeType s_MoveBy("MoveBy", [](NodeDeclaration& d, const NodeConfig&, const Bindings&)
{
    d.Input("offset", "Offset", s_Vector2.AsType());
    d.Output("moved", "Moved", s_Vector2.AsType());
});

TestNodeType s_MakeVector2("MakeVector2", [](NodeDeclaration& d, const NodeConfig&, const Bindings&)
{
    d.Input("x", "X", FloatType());
    d.Input("y", "Y", FloatType());
    d.Output("value", "Value", s_Vector2.AsType());
});

TestNodeType s_BreakAny("BreakAny", [](NodeDeclaration& d, const NodeConfig&, const Bindings& bindings)
{
    auto T = d.Variable("T");
    d.Input("value", "Value", T);
    if (auto type = bindings.Get("T"); type && IsA<StructType>(type->m_Head))
        for (auto& field : static_cast<const StructType*>(type->m_Head)->Fields())
            d.Output(field.m_Name, field.m_Name, *field.m_Type);
});

// Version 1 called its input "value", and version 2 calls it "amount".
struct Renamed final : NodeType
{
    Renamed(): NodeType("test.Renamed", "Renamed", {}, 2) {}

    void Declare(NodeDeclaration& d, const NodeConfig&, const Bindings&) const override
    {
        d.Input("amount", "Amount", Int32Type());
    }

    Result<void> Upgrade(crude_json::value& node, uint32_t version) const override
    {
        if (version != 1)
            return NodeType::Upgrade(node, version);
        auto& pins = node["pins"];
        if (auto value = pins.find("value"))
        {
            pins["amount"] = *value;
            pins.erase("value");
        }
        return {};
    }
};

Renamed s_Renamed;

// What a file elsewhere would have.
TestNodeType s_Thing("Thing", [](NodeDeclaration& d, const NodeConfig&, const Bindings&)
{
    d.FlowInput("enter");
    d.Input("amount", "Amount", Int32Type());
});

std::shared_ptr<Registry> Registered(bool thing)
{
    auto made = std::make_shared<Registry>();
    auto ok = RegisterBuiltins(*made).IsOk() && made->AddValueType(s_Vector2).IsOk();
    for (auto type : std::initializer_list<const NodeType*>{ &s_ConstInt32, &s_ConstFloat, &s_Add, &s_Hold, &s_Start, &s_Print, &s_MoveBy, &s_MakeVector2, &s_BreakAny, &s_Renamed })
        ok = made->AddNodeType(*type).IsOk() && ok;
    if (thing)
        ok = made->AddNodeType(s_Thing).IsOk() && ok;
    if (!ok)
        test::Check(false, "the test registry is made");
    return made;
}

std::shared_ptr<const Registry> FileRegistry()
{
    static const std::shared_ptr<const Registry> registry = Registered(false);
    return registry;
}

struct Fixture : BlueprintFixture
{
    Fixture(): BlueprintFixture(FileRegistry()) {}
};

// A new blueprint of a file's text.
std::unique_ptr<Blueprint> Loaded(const std::string& text, std::shared_ptr<const Registry> registry = FileRegistry())
{
    auto loaded = std::make_unique<Blueprint>(std::move(registry), std::make_shared<TypeTable>());
    if (auto read = Load(*loaded, crude_json::value::parse(text)); !read)
        test::Check(false, "the file loads: %s", std::string(read.GetError().Message()).c_str());
    return loaded;
}

// Its pins' names and types, its orphans, its links' states and its diagnostics.
std::string Describe(const Graph& graph)
{
    std::string text = graph.GetName() + "\n";
    for (auto& node : graph.Nodes())
    {
        text += node.Name() + ":";
        for (auto& pin : node.m_Pins)
            text += " " + pin.m_Key + "#" + std::to_string(pin.m_Id) + "'" + pin.m_Name + "'=" + (pin.m_Flow ? std::string(">") : graph.TypeText(pin.m_Id));
        for (auto& [pin, value] : node.m_Values)
            text += " value#" + std::to_string(pin) + "=" + value.GetType()->m_Head->Format(value);
        for (auto& orphan : node.m_Orphans)
            text += " orphan " + orphan.m_Key + "#" + std::to_string(orphan.m_Id) + (orphan.m_Direction == Direction::Input ? " in" : " out");
        text += "\n";
    }
    for (auto& link : graph.Links())
        text += "link#" + std::to_string(link.m_Id) + " " + std::to_string(link.m_From) + "->" + std::to_string(link.m_To) + (link.m_Active ? " on" : " off: " + std::string(link.m_Reason.Message())) + "\n";
    for (auto& diagnostic : graph.Diagnostics())
        text += "diagnostic: " + std::string(diagnostic.m_Error.Message()) + "\n";
    return text;
}

// Where two texts part: "at 120: ...abc... and ...abd...".
std::string Difference(const std::string& a, const std::string& b)
{
    size_t at = 0;
    while (at < a.size() && at < b.size() && a[at] == b[at])
        ++at;
    if (at == a.size() && at == b.size())
        return "the same";
    auto from = at < 40 ? 0 : at - 40;
    return "at " + std::to_string(at) + ": ..." + a.substr(from, 80) + "... and ..." + b.substr(from, 80) + "...";
}

std::string Describe(const Blueprint& blueprint)
{
    std::string text;
    for (auto& graph : blueprint.Graphs())
        text += Describe(*graph);
    return text;
}

// Start -> Print, and what files hold: groups, a typed value, a chosen type, a cast link, a forced
// link and the one it disabled, a split pin, data in a config, a pin's name, and a pin gone with
// its link.
struct Rich : Fixture
{
    Rich()
    {
        start = Add("Start"), print = Add("Print"), ints = Add("ConstInt32"), floats = Add("ConstFloat"), add = Add("Add"), sum = Add("Add", Chosen("T", FloatType()));
        Link(start, "then", print, "enter");
        auto grown = m_Graph.AddGroupPin(add, "in");
        Link(ints, "value", add, "in.0");
        auto typed = m_Graph.SetValue(Pin(add, "in.1"), Value::Int32(5));
        cast = Link(add, "result", sum, "in.0", LinkMode::Cast);

        auto contested = Add("Add");
        plain  = Link(ints, "value", contested, "in.0");
        forced = Link(floats, "value", contested, "in.1", LinkMode::Force);

        NodeConfig held = Chosen("T", StringType());
        held.m_Data["value"] = Value::String("Za\xC5\xBC\xC3\xB3\xC5\x82\xC4\x87");
        held.m_Names["value"] = "Greeting";
        hold = Add("Hold", held);
        Link(hold, "value", print, "text");

        move = Add("MoveBy");
        auto split = m_Graph.SplitPin(move, "offset", true);

        auto make = Add("MakeVector2"), unpacked = Add("BreakAny"), add2 = Add("Add");
        auto typing = Link(make, "value", unpacked, "value");
        gone = Link(unpacked, "x", add2, "in.0");
        Unlink(typing);
        if (!grown || !typed || !split)
            test::Check(false, "the blueprint is made");
    }

    Id start = 0, print = 0, ints = 0, floats = 0, add = 0, sum = 0, hold = 0, move = 0;
    Id plain = 0, cast = 0, forced = 0, gone = 0;
};

} // namespace

TEST_CASE(RoundTrip, "file.a_blueprint_round_trips", nullptr, test::TestFlags_None)
{
    Rich f;
    auto& other = f.m_Blueprint.AddGraph("Za\xC5\xBC\xC3\xB3\xC5\x82\xC4\x87");
    auto otherNode = other.AddNode("test.Start");

    auto text     = Save(f.m_Blueprint).dump();
    auto loaded   = Loaded(text);
    auto reloaded = Save(*loaded).dump();
    auto before   = Describe(f.m_Blueprint);
    auto after    = Describe(*loaded);
    test::Check(otherNode && after == before, "the loaded blueprint is the one saved:\n%s\n--- loaded:\n%s", before.c_str(), after.c_str());
    test::Check(reloaded == text, "and saves the same text: %s", Difference(text, reloaded).c_str());
    auto graph = loaded->Graphs().empty() ? nullptr : loaded->Graphs()[0].get();
    auto link  = graph ? graph->FindLink(f.gone) : nullptr;
    test::Check(link && !link->m_Active && link->m_Reason.Is(BlueprintError::NoPin), "a link to a pin that's gone loads disabled, and keeps its pin's ID");
}

TEST_CASE(SavedShape, "file.saved_files_hold_what_the_format_says", nullptr, test::TestFlags_None)
{
    Rich f;
    auto file  = Save(f.m_Blueprint);
    auto& node = file["graphs"][0]["nodes"][4];
    auto& sum  = file["graphs"][0]["nodes"][5];
    auto& hold = file["graphs"][0]["nodes"][7];
    test::Check(file["format"].get<crude_json::string>() == "crude_blueprint" && file["version"].get<crude_json::number>() == 2 && file["graphs"].get<crude_json::array>().size() == 1, "the format, version 2, and the graphs");
    test::Check(file["graphs"][0]["kind"].get<crude_json::string>() == "event" && file["graphs"][0]["name"].get<crude_json::string>() == "Main", "a graph has its kind and name");
    test::Check(node["type"].get<crude_json::string>() == "test.Add" && node["type_version"].get<crude_json::number>() == 1 && node["config"]["groups"]["in"].get<crude_json::number>() == 3, "a node, its type, its type's version, and its config: %s", node.dump().c_str());
    test::Check(node["pins"]["in.1"]["value"].dump() == "{\"data\":5,\"type\":\"Int32\"}" && !node["pins"]["in.0"].contains("value") && node["pins"]["in.0"]["dir"].get<crude_json::string>() == "in", "only the values the user typed, each with its type");
    test::Check(sum["config"]["types"]["T"].get<crude_json::string>() == "Float" && hold["config"]["data"]["value"]["type"].get<crude_json::string>() == "String", "chosen types and data as type text: %s", hold.dump().c_str());
    test::Check(hold["config"]["names"]["value"].get<crude_json::string>() == "Greeting" && !sum["config"].contains("names"), "and the names of the pins the user named");
    test::Check(file["graphs"][0]["nodes"][0]["pins"]["then"]["flow"].get<crude_json::boolean>() && !file["graphs"][0]["nodes"][0].contains("config"), "a flow pin says so, and a node without config has none");

    auto& links = file["graphs"][0]["links"].get<crude_json::array>();
    auto find = [&links](Id id) -> const crude_json::value* { for (auto& link : links) if (link["id"].get<crude_json::number>() == id) return &link; return nullptr; };
    auto cast = find(f.cast), forced = find(f.forced), plain = find(f.plain);
    test::Check(cast && (*cast)["cast"].get<crude_json::string>() == "Int32->Float" && !(*cast).contains("forced"), "a cast link names its cast");
    test::Check(forced && (*forced)["forced"].get<crude_json::number>() == 1 && plain && !(*plain).contains("forced") && !(*plain).contains("active"), "a forced link its order, and whether a link is active isn't saved");
    test::Check(!f.m_Graph.FindLink(f.plain)->m_Active, "though the forced link disabled the other");
}

TEST_CASE(MalformedFiles, "file.malformed_files_are_refused_and_leave_the_blueprint_empty", nullptr, test::TestFlags_None)
{
    Rich f;
    const auto saved = Save(f.m_Blueprint);

    struct Case
    {
        const char*                                 what;
        std::function<void(crude_json::value& file)> change;
        const char*                                 message; // a part of it
    };
    const Case cases[] =
    {
        { "an array",                    [](crude_json::value& file) { file = crude_json::array{}; },                                         "is an object"               },
        { "another format",              [](crude_json::value& file) { file["format"] = "other"; },                                           "isn't a crude_blueprint"    },
        { "version 1",                   [](crude_json::value& file) { file["version"] = 1; },                                                "its version is 1"           },
        { "no graphs",                   [](crude_json::value& file) { file.erase("graphs"); },                                               "no array of graphs"         },
        { "no next ID",                  [](crude_json::value& file) { file.erase("next_id"); },                                              "has no next_id"             },
        { "a graph without an ID",       [](crude_json::value& file) { file["graphs"][0].erase("id"); },                                      "graphs[0] has no id"        },
        { "a graph of another kind",     [](crude_json::value& file) { file["graphs"][0]["kind"] = "macro"; },                                "doesn't know"               },
        { "links that aren't an array",  [](crude_json::value& file) { file["graphs"][0]["links"] = 5; },                                     "no array of links"          },
        { "an ID used twice",            [](crude_json::value& file) { file["graphs"][0]["nodes"][1]["id"] = file["graphs"][0]["nodes"][0]["id"]; }, "another part has too" },
        { "a pin's ID that isn't one",   [](crude_json::value& file) { file["graphs"][0]["nodes"][0]["pins"]["then"]["id"] = -3; },           "pins.then.id isn't an ID"   },
        { "a pin that isn't an object",  [](crude_json::value& file) { file["graphs"][0]["nodes"][0]["pins"]["then"] = 5; },                  "isn't an object"            },
        { "a link to a pin none has",    [](crude_json::value& file) { file["graphs"][0]["links"][0]["to"] = 99999; },                       "no node of its graph has"   },
        { "a link without its end",      [](crude_json::value& file) { file["graphs"][0]["links"][0].erase("from"); },                        "links[0] has no from"       },
        { "a forced order not a count",  [](crude_json::value& file) { file["graphs"][0]["links"][0]["forced"] = 1.5; },                      "forced isn't a count"       },
    };

    for (auto& c : cases)
    {
        auto file = saved;
        c.change(file);
        Blueprint blueprint(FileRegistry(), std::make_shared<TypeTable>());
        auto read = Load(blueprint, file);
        auto refused = !read && read.GetError().Is(BlueprintError::BadFile) && read.GetError().Message().find(c.message) != std::string_view::npos;
        test::Check(refused && blueprint.Graphs().empty(), "%s is refused: %s", c.what, Message(read).c_str());
    }

    Blueprint full(FileRegistry(), std::make_shared<TypeTable>());
    full.AddGraph("Main");
    auto twice = Load(full, saved);
    test::Check(!twice && full.Graphs().size() == 1, "and a blueprint loads only while it's empty: %s", Message(twice).c_str());
}

// Main calls Lerp<T: Int32 | Float>(a: T, b: T) -> value: T, an Add of a and b, and Count<T>(items: Array<T>) is pure.
struct WithFunctions : Fixture
{
    WithFunctions()
    {
        Signature lerp;
        lerp.m_Parameters = { { "T", { &Int32ValueType(), &FloatValueType() } } };
        lerp.m_Inputs     = { { "a", "A", TypeVariable{ 0 } }, { "b", "B", TypeVariable{ 0 } } };
        lerp.m_Outputs    = { { "value", "Value", TypeVariable{ 0 } } };
        Signature count;
        count.m_Parameters = { { "T", {} } };
        count.m_Inputs     = { { "items", "Items", TypeTerm(ArrayValueType(), { TypeVariable{ 0 } }) } };
        count.m_Pure       = true;

        auto ints = Add("ConstInt32");
        auto lerpGraph  = m_Blueprint.AddFunction("Lerp", lerp);
        auto countGraph = m_Blueprint.AddFunction("Count", count);
        if (!lerpGraph || !countGraph)
        {
            test::Check(false, "the functions are added");
            return;
        }
        auto& body  = *lerpGraph.GetValue();
        auto  entry = body.AddNode("entry:Lerp"), add = body.AddNode("test.Add"), ret = body.AddNode("return:Lerp");
        auto  call  = m_Graph.AddNode("call:Lerp");
        auto  pin   = [](const Graph& graph, const Result<Id>& node, const char* key) { return graph.FindNode(node.ValueOr(0))->FindPin(key)->m_Id; };
        auto  made  = entry && add && ret && call && countGraph.GetValue()->AddNode("entry:Count")
            && body.Connect(pin(body, entry, "a"), pin(body, add, "in.0")) && body.Connect(pin(body, entry, "b"), pin(body, add, "in.1"))
            && body.Connect(pin(body, add, "result"), pin(body, ret, "value")) && m_Graph.Connect(Pin(ints, "value"), pin(m_Graph, call, "a"));
        if (!made)
            test::Check(false, "the bodies and the call are made");
        m_Lerp  = body.GetId();
        m_Count = countGraph.GetValue()->GetId();
    }

    Id m_Lerp = 0, m_Count = 0;
};

TEST_CASE(Functions, "file.functions_round_trip_with_their_signatures", nullptr, test::TestFlags_None)
{
    WithFunctions f;
    auto file     = Save(f.m_Blueprint);
    auto text     = file.dump();
    auto loaded   = Loaded(text);
    auto before   = Describe(f.m_Blueprint);
    auto after    = Describe(*loaded);
    test::Check(after == before, "the loaded blueprint is the one saved, though Main calls Lerp before the file has it:\n%s\n--- loaded:\n%s", before.c_str(), after.c_str());
    test::Check(Save(*loaded).dump() == text, "and saves the same text");
    auto lerp  = loaded->FindFunction(f.m_Lerp);
    auto count = loaded->FindFunction(f.m_Count);
    test::Check(lerp && lerp->m_Signature.m_Parameters.size() == 1 && lerp->m_Signature.m_Parameters[0].m_Allowed.size() == 2 && count && count->m_Signature.m_Pure, "with their signatures");

    auto& signature = file["graphs"][1]["signature"];
    test::Check(file["graphs"][1]["kind"].get<crude_json::string>() == "function" && signature["parameters"][0]["allowed"].dump() == "[\"Int32\",\"Float\"]", "a function's graph has its signature: %s", signature.dump().c_str());
    test::Check(signature["inputs"][1]["type"].get<crude_json::string>() == "T" && !signature.contains("pure") && file["graphs"][2]["signature"]["inputs"][0]["type"].get<crude_json::string>() == "Array<T>" && file["graphs"][2]["signature"]["pure"].get<crude_json::boolean>(),
        "its types as text that names its parameters, and whether it's pure");
}

TEST_CASE(MalformedFunctions, "file.malformed_functions_are_refused", nullptr, test::TestFlags_None)
{
    WithFunctions f;
    const auto saved = Save(f.m_Blueprint);

    struct Case
    {
        const char*                                 what;
        std::function<void(crude_json::value& file)> change;
        const char*                                 message; // a part of it
    };
    const Case cases[] =
    {
        { "a function without a signature", [](crude_json::value& file) { file["graphs"][1].erase("signature"); },                                 "has no signature"            },
        { "a type no one registered",       [](crude_json::value& file) { file["graphs"][1]["signature"]["inputs"][0]["type"] = "Array<U>"; },     "no value type is named U"    },
        { "an allowed type no one has",     [](crude_json::value& file) { file["graphs"][1]["signature"]["parameters"][0]["allowed"][1] = "Big"; }, "which no value type is named" },
        { "a pin without a key",            [](crude_json::value& file) { file["graphs"][1]["signature"]["outputs"][0].erase("key"); },            "has no key"                  },
        { "two functions of a name",        [](crude_json::value& file) { file["graphs"][2]["name"] = "Lerp"; },                                   "a function is named Lerp"    },
        { "pure that isn't true or false",  [](crude_json::value& file) { file["graphs"][2]["signature"]["pure"] = 1; },                            "isn't true or false"         },
    };
    for (auto& c : cases)
    {
        auto file = saved;
        c.change(file);
        Blueprint blueprint(FileRegistry(), std::make_shared<TypeTable>());
        auto read = Load(blueprint, file);
        auto refused = !read && read.GetError().Message().find(c.message) != std::string_view::npos;
        test::Check(refused && blueprint.Graphs().empty() && !blueprint.FindNodeType("call:Lerp"), "%s is refused, and leaves no function: %s", c.what, Message(read).c_str());
    }
}

TEST_CASE(NextId, "file.new_ids_are_above_every_id_a_file_has", nullptr, test::TestFlags_None)
{
    Rich f;
    auto file = Save(f.m_Blueprint);
    file["next_id"] = 1;
    auto loaded = Loaded(file.dump());
    auto added  = loaded->Graphs().empty() ? Result<Id>(BlueprintError::NotFound) : loaded->Graphs()[0]->AddNode("test.Add");
    auto node   = added && !loaded->Graphs().empty() ? loaded->Graphs()[0]->FindNode(added.GetValue()) : nullptr;
    auto above  = node && node->m_Id > f.gone && std::all_of(node->m_Pins.begin(), node->m_Pins.end(), [&](const Pin& pin) { return pin.m_Id > f.gone; });
    test::Check(above, "a node added after loading takes IDs no loaded part has");
}

TEST_CASE(Placeholders, "file.a_node_it_cant_use_loads_as_a_placeholder_and_saves_back_unchanged", nullptr, test::TestFlags_None)
{
    // Thing is registered where the file was made.
    auto elsewhere = Registered(true);
    BlueprintFixture made(elsewhere);
    auto start = made.Add("Start"), ints = made.Add("ConstInt32"), thing = made.Add("Thing");
    auto flow  = made.Link(start, "then", thing, "enter");
    auto data  = made.Link(ints, "value", thing, "amount");
    auto text  = Save(made.m_Blueprint).dump();

    auto loaded = Loaded(text);
    auto graph  = loaded->Graphs().empty() ? nullptr : loaded->Graphs()[0].get();
    auto node   = graph ? graph->FindNode(thing) : nullptr;
    if (!graph || !node)
        return test::Check(false, "the placeholder loads");
    test::Check(node->Name() == "test.Thing#" + std::to_string(thing) && node->FindPin("enter") && node->FindPin("amount"), "it has its pins as the file lists them: %s", node->Name().c_str());
    test::Check(!graph->FindLink(flow)->m_Active && !graph->FindLink(data)->m_Active, "its links load disabled: %s", std::string(graph->FindLink(flow)->m_Reason.Message()).c_str());
    auto diagnostics = graph->Diagnostics();
    auto named = std::any_of(diagnostics.begin(), diagnostics.end(), [&](const Diagnostic& d) { return d.m_Node == thing && d.m_Error.Message() == "no node type is named test.Thing"; });
    test::Check(named && graph->IsBroken(), "and an error on it names its type");
    auto edited = loaded->Graphs()[0]->SplitPin(thing, "amount", true);
    test::Check(!edited && edited.GetError().Is(BlueprintError::Unsupported), "it takes no edit: %s", Message(edited).c_str());

    auto resaved = Save(*loaded);
    auto original = crude_json::value::parse(text);
    test::Check(resaved["graphs"][0]["nodes"][2].dump() == original["graphs"][0]["nodes"][2].dump() && resaved.dump() == text, "it saves back as the file had it: %s", Difference(text, resaved.dump()).c_str());

    auto back = Loaded(resaved.dump(), elsewhere);
    auto active = !back->Graphs().empty() && back->Graphs()[0]->FindLink(flow)->m_Active && back->Graphs()[0]->FindLink(data)->m_Active && !back->Graphs()[0]->IsBroken();
    test::Check(active, "loaded where Thing is registered, its links are active again");

    struct Case
    {
        const char*                                 what;
        std::function<void(crude_json::value& node)> change;
        const char*                                 reason; // a part of it
    };
    const Case cases[] =
    {
        { "a value of a type nothing registered", [](crude_json::value& node) { node["pins"]["in.1"]["value"]["type"] = "example.Colour"; }, "example.Colour" },
        { "a value its type can't load",          [](crude_json::value& node) { node["pins"]["in.1"]["value"]["data"] = "five"; },           "not an Int32"   },
        { "a newer version of its node",          [](crude_json::value& node) { node["type_version"] = 99; },                                "version 99"     },
        { "an older one without an upgrade",      [](crude_json::value& node) { node["type_version"] = 0; },                                 "can't read version 0" },
        { "a pin's name that isn't text",         [](crude_json::value& node) { node["config"]["names"]["in.0"] = 5; },                      "names.in.0 isn't a name" },
    };
    for (auto& c : cases)
    {
        Rich f;
        auto file = Save(f.m_Blueprint);
        c.change(file["graphs"][0]["nodes"][4]);
        auto changed = Loaded(file.dump());
        auto add = !changed->Graphs().empty() ? changed->Graphs()[0]->FindNode(f.add) : nullptr;
        auto all = !changed->Graphs().empty() ? changed->Graphs()[0]->Diagnostics() : std::span<const Diagnostic>();
        auto found = std::any_of(all.begin(), all.end(), [&](const Diagnostic& d) { return d.m_Node == f.add && d.m_Error.Message().find(c.reason) != std::string_view::npos; });
        test::Check(add && found && Save(*changed)["graphs"][0]["nodes"][4].dump() == file["graphs"][0]["nodes"][4].dump(), "%s makes a placeholder, and saves back unchanged", c.what);
    }
}

TEST_CASE(Upgrades, "file.a_node_an_older_version_saved_is_upgraded", nullptr, test::TestFlags_None)
{
    Fixture f;
    auto renamed = f.Add("Renamed");
    if (auto set = f.m_Graph.SetValue(f.Pin(renamed, "amount"), Value::Int32(7)); !set)
        test::Check(false, "the value is set: %s", Message(set).c_str());
    auto file  = Save(f.m_Blueprint);
    auto& node = file["graphs"][0]["nodes"][0];
    auto id    = node["pins"]["amount"]["id"].get<crude_json::number>();
    test::Check(node["type_version"].get<crude_json::number>() == 2, "the node type is at version 2");

    node["type_version"] = 1;
    node["pins"]["value"] = node["pins"]["amount"];
    node["pins"].erase("amount");
    auto loaded = Loaded(file.dump());
    auto graph  = loaded->Graphs().empty() ? nullptr : loaded->Graphs()[0].get();
    auto pin    = graph && graph->FindNode(renamed) ? graph->FindNode(renamed)->FindPin("amount") : nullptr;
    auto value  = pin ? graph->FindValue(pin->m_Id) : nullptr;
    test::Check(pin && pin->m_Id == id && value && *value == Value::Int32(7) && !graph->IsBroken(), "version 1's value is version 2's amount, with its ID and value");
}

TEST_CASE(LinkOrder, "file.links_load_forced_first_then_in_the_order_the_file_lists_them", nullptr, test::TestFlags_None)
{
    Fixture f;
    auto ints = f.Add("ConstInt32"), floats = f.Add("ConstFloat"), add = f.Add("Add");
    auto plain  = f.Link(ints, "value", add, "in.0");
    auto forced = f.Link(floats, "value", add, "in.1", LinkMode::Force);
    auto file   = Save(f.m_Blueprint);

    auto loaded = Loaded(file.dump());
    auto graph  = loaded->Graphs().empty() ? nullptr : loaded->Graphs()[0].get();
    test::Check(graph && graph->FindLink(forced)->m_Active && !graph->FindLink(plain)->m_Active && graph->TypeText(f.Pin(add, "result")) == "Float", "the forced link wins again");

    file["graphs"][0]["links"][1].erase("forced");
    auto unforced = Loaded(file.dump());
    auto graph2   = unforced->Graphs().empty() ? nullptr : unforced->Graphs()[0].get();
    test::Check(graph2 && graph2->FindLink(plain)->m_Active && !graph2->FindLink(forced)->m_Active && graph2->TypeText(f.Pin(add, "result")) == "Int32", "unforced, the first link the file lists wins");
}
