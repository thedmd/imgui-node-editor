//------------------------------------------------------------------------------
// blueprints2-example's runtime: variables, which every graph of a blueprint
// shares in each context.
//------------------------------------------------------------------------------
# include "blueprints2_fixture.h"
# include "crude_blueprint_library.h"
# include <crude_json.h>
# include <chrono>
# include <limits>
# include <memory>
# include <string>

using namespace blueprints2_tests;

namespace {

std::shared_ptr<const Registry> VariableRegistry()
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

// A blueprint of the library's node types, and of its own: its variables' and its functions'.
struct Fixture : BlueprintFixture
{
    Fixture(): BlueprintFixture(VariableRegistry(), "") {}

    void Variable(const char* name, Value value)
    {
        if (auto added = m_Blueprint.AddVariable({ name, std::move(value) }); !added)
            test::Check(false, "variable %s is added: %s", name, Message(added).c_str());
    }

    static Id In(Graph& graph, const std::string& type)
    {
        auto node = graph.AddNode(type);
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

    static Id Wire(Graph& graph, Id from, std::string_view output, Id to, std::string_view input, LinkMode mode = LinkMode::Plain)
    {
        auto link = graph.Connect(PinOf(graph, from, output), PinOf(graph, to, input), mode);
        if (!link)
            test::Check(false, "%d.%s -> %d.%s is made: %s", static_cast<int>(from), std::string(output).c_str(), static_cast<int>(to), std::string(input).c_str(), std::string(link.GetError().Message()).c_str());
        return link.ValueOr(0);
    }

    // Prints what Get of the variable gives, as text, after the flow output.
    Id PrintVariable(Graph& graph, Id after, std::string_view output, const char* variable)
    {
        auto print = In(graph, "crude.Print"), get = In(graph, std::string("get:") + variable);
        Wire(graph, after, output, print, "enter");
        Wire(graph, get, "value", print, "text", LinkMode::Cast);
        return print;
    }

    // One that isn't made fails the test.
    std::shared_ptr<const Program> Compiled() const
    {
        auto compiled = Compile(m_Blueprint);
        if (!compiled.m_Program)
            test::Check(false, "the blueprint compiles: %s", compiled.m_Diagnostics.empty() ? "" : std::string(compiled.m_Diagnostics[0].m_Error.Message()).c_str());
        return compiled.m_Program;
    }

    // Runs Start's fiber to its end in the context, and gives what it printed.
    static std::string Run(Context& context)
    {
        Logs logs;
        context.SetMonitor(&logs);
        auto started = context.Start("Start");
        context.Run(std::chrono::seconds(5));
        context.SetMonitor(nullptr);
        if (!started || context.Fibers().back().m_State != FiberState::Done)
            return "not done: " + (context.Diagnostics().empty() ? std::string() : std::string(context.Diagnostics().back().m_Error.Message()));
        return logs.m_Text;
    }
};

} // namespace

TEST_CASE(VariableChecks, "variable.a_variable_has_a_name_of_its_own_and_a_default_a_file_holds", nullptr, test::TestFlags_None)
{
    Fixture f;
    auto health = f.m_Blueprint.AddVariable({ "Health", Value::Int32(100) });
    auto name   = f.m_Blueprint.AddVariable({ "Name", Value::String("Ada") });
    auto names  = std::string();
    for (auto variable : f.m_Blueprint.Variables())
        names += (names.empty() ? "" : " ") + variable->m_Name;
    test::Check(health && name && names == "Health Name", "variables are added, and listed in that order: %s", names.c_str());

    auto get = f.m_Blueprint.FindNodeType("get:Health"), set = f.m_Blueprint.FindNodeType("set:Health");
    test::Check(get && set && get->m_Title == "Get Health" && set->m_Title == "Set Health" && get->m_Category == "Variables", "each has a Get and a Set of the blueprint's own");

    struct Refused { Variable m_Variable; BlueprintError m_Code; const char* m_Why; };
    const Refused refused[] =
    {
        { { "Health", Value::Int32(1) },                                  BlueprintError::Duplicate,   "a name another has" },
        { { "two words", Value::Int32(1) },                               BlueprintError::BadTypeText, "a name with a space" },
        { { "", Value::Int32(1) },                                        BlueprintError::BadTypeText, "no name" },
        { { "Nothing", Value() },                                         BlueprintError::Undecided,   "no default, which gives the type" },
        { { "Odd", Value::Float(std::numeric_limits<float>::quiet_NaN()) }, BlueprintError::WrongType,  "a default no file holds" },
    };
    for (auto& [variable, code, why] : refused)
    {
        auto added = f.m_Blueprint.AddVariable(variable);
        test::Check(!added && added.GetError().Is(code), "one of %s is refused: %s", why, Message(added).c_str());
    }
    test::Check(f.m_Blueprint.Variables().size() == 2, "and none of those is added");
}

TEST_CASE(VariablesRun, "variable.every_graph_reads_and_writes_it_in_each_context_from_its_default", nullptr, test::TestFlags_None)
{
    // Start prints Health, calls Heal, which sets Health to Health + 10, then prints Health again.
    Fixture f;
    f.Variable("Health", Value::Int32(100));
    auto& heal  = *f.m_Blueprint.AddFunction("Heal", {}).ValueOr(nullptr);
    auto  entry = f.In(heal, "entry:Heal"), set = f.In(heal, "set:Health"), add = f.In(heal, "crude.Add"), get = f.In(heal, "get:Health");
    f.Wire(heal, entry, "then", set, "enter");
    f.Wire(heal, get, "value", add, "in.0");
    f.Wire(heal, add, "result", set, "value");
    if (!heal.SetValue(f.PinOf(heal, add, "in.1"), Value::Int32(10)))
        test::Check(false, "Heal adds 10");

    auto& main  = f.m_Graph;
    auto  start = f.In(main, "crude.Start"), call = f.In(main, "call:Heal");
    auto  first = f.PrintVariable(main, start, "then", "Health");
    f.Wire(main, first, "then", call, "enter");
    f.PrintVariable(main, call, "then", "Health");
    auto program = f.Compiled();
    if (!program)
        return;

    Context context(program);
    auto printed = f.Run(context);
    auto health  = context.GetVariable("Health");
    test::Check(printed == "100 110" && health && health.GetValue() == Value::Int32(110), "a function sets it, and the event graph reads what it set: %s", printed.c_str());
    auto again = f.Run(context);
    test::Check(again == "110 120", "it keeps its value in its context: %s", again.c_str());

    Context other(program);
    auto fresh = f.Run(other);
    test::Check(fresh == "100 110", "and another context starts from its default: %s", fresh.c_str());

    auto wrong = other.SetVariable("Health", Value::String("full"));
    auto none  = other.GetVariable("Mana");
    auto set7  = other.SetVariable("Health", Value::Int32(7));
    auto read  = other.Evaluate(f.PinOf(main, main.NodeOf(main.Links().back().m_From)->m_Id, "value"));
    auto host  = f.Run(other);
    test::Check(!wrong && wrong.GetError().Is(BlueprintError::WrongType) && !none && none.GetError().Is(BlueprintError::NotFound), "the host can't give it a value of another type, nor read one there isn't: %s", Message(wrong).c_str());
    test::Check(set7 && read && read.GetValue().m_Value == Value::Int32(7), "but sets it, and a Get that read it before reads it again");
    test::Check(host == "7 17", "as the run does: %s", host.c_str());
}

TEST_CASE(VariableChanges, "variable.its_nodes_follow_its_type_and_name", nullptr, test::TestFlags_None)
{
    // A Get of Health prints, through the cast from Int32 to String.
    Fixture f;
    f.Variable("Health", Value::Int32(100));
    auto& main  = f.m_Graph;
    auto  start = f.In(main, "crude.Start");
    f.PrintVariable(main, start, "then", "Health");
    auto  get   = main.NodeOf(main.Links().back().m_From)->m_Id;
    auto  cast  = main.Links().back().m_Id;

    auto retyped = f.m_Blueprint.SetVariable("Health", { "Health", Value::Float(1.5f) });
    auto active  = main.FindLink(cast)->m_Active;
    test::Check(retyped && f.Type(get, "value") == "Float" && !active, "of another type, its Get gives it, and the cast from Int32 goes disabled");

    auto renamed = f.m_Blueprint.SetVariable("Health", { "Life", Value::Float(1.5f) });
    auto node    = main.FindNode(get);
    test::Check(renamed && node && node->m_Type->m_Name == "get:Life" && node->Name() == "Get Life#" + std::to_string(get) && !f.m_Blueprint.FindNodeType("get:Health"), "renamed, its Get is Get Life, of the same ID");

    auto back = f.m_Blueprint.SetVariable("Life", { "Life", Value::Int32(3) });
    test::Check(back && main.FindLink(cast)->m_Active && main.FindLink(cast)->m_From == f.Pin(get, "value"), "of Int32 again, the cast comes back");

    auto refused = f.m_Blueprint.SetVariable("Life", { "two words", Value::Int32(3) });
    auto missing = f.m_Blueprint.SetVariable("Health", { "Health", Value::Int32(3) });
    test::Check(!refused && f.m_Blueprint.FindVariable("Life") && !missing && missing.GetError().Is(BlueprintError::NotFound), "a change it can't take is refused, as is one of a variable there isn't");

    auto used = f.m_Blueprint.RemoveVariable("Life");
    f.Remove(get);
    auto removed = f.m_Blueprint.RemoveVariable("Life");
    test::Check(!used && removed && !f.m_Blueprint.FindVariable("Life") && !f.m_Blueprint.FindNodeType("set:Life"), "it isn't removed while a node uses it, and then is, with its node types: %s", Message(used).c_str());
}

TEST_CASE(VariablePatches, "variable.a_run_keeps_its_values_through_a_patch_of_a_default", nullptr, test::TestFlags_None)
{
    // Start sets Score to 5.
    Fixture f;
    f.Variable("Score", Value::Int32(0));
    auto& main  = f.m_Graph;
    auto  start = f.In(main, "crude.Start"), set = f.In(main, "set:Score");
    f.Wire(main, start, "then", set, "enter");
    if (!main.SetValue(f.PinOf(main, set, "value"), Value::Int32(5)))
        test::Check(false, "Set Score takes 5");
    auto program = f.Compiled();
    if (!program)
        return;
    Context context(program);
    f.Run(context);

    auto changed = f.m_Blueprint.SetVariable("Score", { "Score", Value::Int32(9) }).IsOk();
    auto patched = changed ? context.Patch(f.Compiled()) : Result<void>(BlueprintError::NotFound);
    auto kept    = context.GetVariable("Score");
    test::Check(patched && kept && kept.GetValue() == Value::Int32(5), "a new default patches the run, which keeps the value it set: %s", Message(patched).c_str());
    Context later(f.Compiled());
    auto fresh = later.GetVariable("Score");
    test::Check(fresh && fresh.GetValue() == Value::Int32(9), "and a later context starts at the new default");

    f.Variable("Lives", Value::Int32(3));
    auto added = context.Patch(f.Compiled());
    auto retyped = f.m_Blueprint.SetVariable("Score", { "Score", Value::Float(0.0f) }).IsOk();
    auto refused = context.Patch(f.Compiled());
    test::Check(!added && retyped && !refused, "another variable, or a new type, is more than a value: %s", Message(refused).c_str());

    // The program of before the rename runs on with node types that the blueprint has no more.
    auto renamed = f.m_Blueprint.SetVariable("Score", { "Points", Value::Float(0.0f) }).IsOk();
    Context old(program);
    auto ran = f.Run(old);
    auto score = old.GetVariable("Score");
    test::Check(renamed && ran.empty() && score && score.GetValue() == Value::Int32(5), "a program compiled before a rename runs as it was: %s", ran.c_str());
}

TEST_CASE(VariableFiles, "variable.a_file_keeps_variables_and_their_nodes", nullptr, test::TestFlags_None)
{
    Fixture f;
    f.Variable("Health", Value::Int32(100));
    f.Variable("Tags", Value::String("hero"));
    auto& main = f.m_Graph;
    auto  get  = f.In(main, "get:Tags");
    auto  file = Save(f.m_Blueprint);

    Blueprint loaded(VariableRegistry(), std::make_shared<TypeTable>());
    auto read   = Load(loaded, file);
    auto names  = std::string();
    for (auto variable : loaded.Variables())
        names += (names.empty() ? "" : " ") + variable->m_Name;
    auto health = loaded.FindVariable("Health"), tags = loaded.FindVariable("Tags");
    auto node   = read ? loaded.Graphs().front()->FindNode(get) : nullptr;
    test::Check(read && names == "Health Tags" && health->m_Default == Value::Int32(100) && tags->m_Default == Value::String("hero"), "they load back, in order, with their defaults: %s", names.c_str());
    test::Check(node && node->m_Type == loaded.FindNodeType("get:Tags"), "and their nodes with them");

    auto without = Save(Blueprint(VariableRegistry(), std::make_shared<TypeTable>()));
    test::Check(!without.contains("variables"), "a blueprint without variables saves none");

    struct Malformed { const char* m_Variables; const char* m_Why; };
    const Malformed malformed[] =
    {
        { R"([ { "name": "Health" } ])",                        "a variable without a value" },
        { R"(5)",                                               "variables that aren't an array" },
        { R"([ { "value": { "type": "Int32", "data": 1 } } ])", "a variable without a name" },
        { R"([ { "name": "A", "value": { "type": "Int32", "data": 1 } }, { "name": "A", "value": { "type": "Int32", "data": 2 } } ])", "two of one name" },
    };
    for (auto& [variables, why] : malformed)
    {
        auto broken = file;
        broken["variables"] = crude_json::value::parse(variables);
        Blueprint target(VariableRegistry(), std::make_shared<TypeTable>());
        auto refused = Load(target, broken);
        test::Check(!refused && refused.GetError().Is(BlueprintError::BadFile) && target.Variables().empty() && target.Graphs().empty(), "%s is refused, and leaves the blueprint empty: %s", why, Message(refused).c_str());
    }
}
