//------------------------------------------------------------------------------
// blueprints2-example's blueprint model and documents, without its window.
//------------------------------------------------------------------------------
# include "harness.h"
# include "blueprint_editor_document.h"
# include "blueprint_editor_utilities.h"
# include "crude_blueprint.h"
# include "crude_blueprint_library.h"
# include "crude_logger.h"
# include <crude_json.h>
# include <cstdint>
# include <cstdio>
# include <functional>
# include <string>
# include <utility>
# include <vector>

using namespace crude_blueprint;
using blueprint_editor::Document;
using blueprint_editor_utilities::DebugOverlay;

namespace {

std::vector<std::string> s_Printed;

// What Print nodes print, while it lives.
struct PrintCapture
{
    PrintCapture()
    {
        s_Printed.clear();
        PrintNode::s_PrintFunction = [](const PrintNode&, string_view message) { s_Printed.push_back(message.to_string()); };
    }

    ~PrintCapture()
    {
        PrintNode::s_PrintFunction = nullptr;
    }
};

std::string Printed()
{
    std::string printed;
    for (auto& message : s_Printed)
        printed += message + "|";
    return printed;
}

ed::Config UnnamedConfig()
{
    ed::Config config;
    config.SettingsName = nullptr;
    return config;
}

// The example logs through the overlay logger it shows, which needs an ImGui context.
struct OverlayLog
{
    OverlayLog()  { crude_logger::OverlayLogger::SetCurrent(&m_Logger); }
    ~OverlayLog() { crude_logger::OverlayLogger::SetCurrent(nullptr); }

    crude_logger::OverlayLogger m_Logger;
};

struct TestFile
{
    explicit TestFile(const char* path): Path(path) { std::remove(Path.c_str()); }
    ~TestFile() { std::remove(Path.c_str()); }

    std::string Path;
};

// The example's document: Start runs Do N five times, and Flip Flop alternates
// between printing "FlipFlop slot A!" and the counter plus 3.
struct ExampleBlueprint
{
    explicit ExampleBlueprint(Blueprint& blueprint): m_Blueprint(blueprint)
    {
        Link();
    }

    Blueprint&      m_Blueprint;
    PrintNode*      m_PrintB     = m_Blueprint.CreateNode<PrintNode>();
    EntryPointNode* m_Entry      = m_Blueprint.CreateNode<EntryPointNode>();
    PrintNode*      m_PrintA     = m_Blueprint.CreateNode<PrintNode>();
    FlipFlopNode*   m_FlipFlop   = m_Blueprint.CreateNode<FlipFlopNode>();
    ToStringNode*   m_ToString   = m_Blueprint.CreateNode<ToStringNode>();
    DoNNode*        m_DoN        = m_Blueprint.CreateNode<DoNNode>();
    AddNode*        m_Add        = m_Blueprint.CreateNode<AddNode>();

    void Link()
    {
        m_Entry->m_Exit.LinkTo(m_DoN->m_Enter);
        m_DoN->m_N.SetValue(5);
        m_DoN->m_Exit.LinkTo(m_FlipFlop->m_Enter);
        m_ToString->m_Value.LinkTo(m_Add->m_Result);
        m_Add->m_A.LinkTo(m_DoN->m_Counter);
        m_Add->m_B.SetValue(3);
        m_ToString->m_Exit.LinkTo(m_PrintB->m_Enter);
        m_PrintA->m_String.SetValue("FlipFlop slot A!");
        m_PrintB->m_String.SetValue("FlipFlop slot B!");
        m_PrintB->m_String.LinkTo(m_ToString->m_String);
        m_FlipFlop->m_A.LinkTo(m_PrintA->m_Enter);
        m_FlipFlop->m_B.LinkTo(m_ToString->m_Enter);
    }

    PrintCapture    m_Capture;
};

// Start runs For Loop from the first to the last index, and its body prints the index.
struct ForLoopBlueprint
{
    ForLoopBlueprint(int32_t firstIndex, int32_t lastIndex)
    {
        m_Entry->m_Exit.LinkTo(m_ForLoop->m_Enter);
        m_ForLoop->m_FirstIndex.SetValue(firstIndex);
        m_ForLoop->m_LastIndex.SetValue(lastIndex);
        m_ForLoop->m_LoopBody.LinkTo(m_ToString->m_Enter);
        m_ToString->m_Value.LinkTo(m_ForLoop->m_Index);
        m_ToString->m_Exit.LinkTo(m_Print->m_Enter);
        m_Print->m_String.LinkTo(m_ToString->m_String);
    }

    // Steps until execution ends, or for 100 steps.
    StepResult Run()
    {
        m_Blueprint.Start(*m_Entry);
        for (int steps = 0; steps < 100 && m_Blueprint.LastStepResult() == StepResult::Success; ++steps)
            m_Blueprint.Step();
        return m_Blueprint.LastStepResult();
    }

    Blueprint       m_Blueprint;
    EntryPointNode* m_Entry    = m_Blueprint.CreateNode<EntryPointNode>();
    ForLoopNode*    m_ForLoop  = m_Blueprint.CreateNode<ForLoopNode>();
    ToStringNode*   m_ToString = m_Blueprint.CreateNode<ToStringNode>();
    PrintNode*      m_Print    = m_Blueprint.CreateNode<PrintNode>();
    PrintCapture    m_Capture;
};

// The entry of node 'id' in a saved blueprint.
crude_json::value& NodeEntry(crude_json::value& blueprint, uint32_t id)
{
    for (auto& node : blueprint["nodes"].get<crude_json::array>())
        if (node["id"].get<crude_json::number>() == id)
            return node;

    test::Fail("no node %u in the saved blueprint", id);
    static crude_json::value none;
    return none;
}

// The entry of pin 'id' in a saved blueprint.
crude_json::value& PinEntry(crude_json::value& blueprint, uint32_t id)
{
    for (auto& node : blueprint["nodes"].get<crude_json::array>())
        for (auto key : { "input_pins", "output_pins" })
            if (node.contains(key))
                for (auto& pin : node[key].get<crude_json::array>())
                    if (pin["id"].get<crude_json::number>() == id)
                        return pin;

    test::Fail("no pin %u in the saved blueprint", id);
    static crude_json::value none;
    return none;
}

} // namespace

TEST_CASE(Blueprints2Runs, "blueprints2.example_blueprint_runs", nullptr, test::TestFlags_None)
{
    Blueprint        blueprint;
    ExampleBlueprint example(blueprint);
    const auto result = example.m_Blueprint.Execute(*example.m_Entry);

    const auto printed = Printed();
    test::Check(result == StepResult::Done, "the example runs to its end: %s", StepResultToString(result));
    test::Check(printed == "FlipFlop slot A!|5|FlipFlop slot A!|7|FlipFlop slot A!|", "and prints: %s", printed.c_str());
}
TEST_CASE(DeleteStopsExecution, "blueprints2.deleting_a_node_stops_execution", "EXB-1", test::TestFlags_None)
{
    // Execution paused after Do N's first step, with Flip Flop to run next; the user deletes Flip Flop.
    Blueprint        blueprint;
    ExampleBlueprint example(blueprint);
    blueprint.Start(*example.m_Entry);
    blueprint.Step();
    const bool paused = blueprint.CurrentNode() == example.m_DoN && blueprint.NextNode() == example.m_FlipFlop;

    blueprint.DeleteNode(example.m_FlipFlop);
    const bool stopped = blueprint.CurrentNode() == nullptr && blueprint.NextNode() == nullptr && blueprint.LastStepResult() == StepResult::Done;
    const auto step    = blueprint.Step();

    test::Check(paused, "execution paused in Do N, before Flip Flop");
    test::Check(stopped, "deleting Flip Flop stops it: the toolbar finds no current or next node");
    test::Check(step == StepResult::Done, "and a step does nothing: %s", StepResultToString(step));
}

TEST_CASE(PinCopyOutlivesNode, "blueprints2.a_pin_copy_outlives_its_node", "EXB-1", test::TestFlags_NeedsSanitizer)
{
    // The debug overlay keeps a copy of the current flow pin for the frame, in which the user may delete its node.
    Blueprint        blueprint;
    ExampleBlueprint example(blueprint);
    {
        FlowPin copy = example.m_DoN->m_Exit;
        example.m_Blueprint.DeleteNode(example.m_DoN);
    }
    test::Check(example.m_Blueprint.FindNode(example.m_Add->m_Id) == example.m_Add, "the copy went away without touching the deleted node");
}
TEST_CASE(DataLoopRefused, "blueprints2.a_link_that_makes_a_data_loop_is_refused", "EXB-3", test::TestFlags_None)
{
    // Add 2 takes Add 1's result; Add 1 taking Add 2's result would evaluate forever.
    Blueprint blueprint;
    auto add1 = blueprint.CreateNode<AddNode>();
    auto add2 = blueprint.CreateNode<AddNode>();
    auto add3 = blueprint.CreateNode<AddNode>();
    const bool first  = add2->m_A.LinkTo(add1->m_Result);
    const bool chain  = add3->m_A.LinkTo(add2->m_Result);
    const auto query  = add1->m_A.CanLinkTo(add3->m_Result);
    const bool closed = add1->m_A.LinkTo(add3->m_Result);
    const bool other  = add1->m_B.LinkTo(add2->m_Result);

    test::Check(first && chain, "Add 1 feeds Add 2, which feeds Add 3");
    test::Check(!query && query.Reason().find("loop") != std::string::npos, "Add 3 feeding Add 1 is refused: %s", query.Reason().data());
    test::Check(!closed && !other && add1->m_A.m_Link == nullptr && add1->m_B.m_Link == nullptr, "and no loop is linked");
}

TEST_CASE(DeepEvaluation, "blueprints2.a_data_loop_or_long_chain_evaluates_without_overflow", "EXB-3", test::TestFlags_None)
{
    // A loop from a file, which no link check saw, and a chain as long as a file may make it.
    Blueprint blueprint;
    auto add1 = blueprint.CreateNode<AddNode>();
    auto add2 = blueprint.CreateNode<AddNode>();
    add2->m_A.LinkTo(add1->m_Result);
    add1->m_A.m_Link = &add2->m_Result;
    const auto loop = blueprint.GetContext().GetPinValue(add1->m_Result);

    std::vector<AddNode*> chain(1, blueprint.CreateNode<AddNode>());
    for (int i = 1; i < 20000; ++i)
    {
        chain.push_back(blueprint.CreateNode<AddNode>());
        chain.back()->m_A.m_Link = &chain[chain.size() - 2]->m_Result;
    }
    const auto deep = blueprint.GetContext().GetPinValue(chain.back()->m_Result);

    // Evaluation afterwards goes on as usual.
    auto constant = blueprint.CreateNode<ConstInt32Node>();
    constant->m_Int32.SetValue(7);
    const auto value = blueprint.GetContext().GetPinValue(constant->m_Int32);

    test::Check(loop.GetType() == PinType::Any && deep.GetType() == PinType::Any, "both evaluate to no value");
    test::Check(value.GetType() == PinType::Int32 && value.As<int32_t>() == 7, "a pin evaluated after them has its value");
}

TEST_CASE(DebugOverlayLayout, "blueprints2.debug_overlay_leaves_the_layout_alone", nullptr, test::TestFlags_None)
{
    // While execution runs, the overlay draws values beside pins: after an output pin, before an
    // input pin. Each pin's cell in the node's layout is a group that ends after the overlay, and
    // has to end with the pin, as Flip Flop's A does with no value to show.
    test::Session    session(UnnamedConfig());
    Blueprint        blueprint;
    ExampleBlueprint example(blueprint);
    example.m_ToString->m_String.SetValue("a value much wider than its pin");
    blueprint.Start(*example.m_Entry);

    struct Cell
    {
        float PinEnd  = 0.0f;
        float CellEnd = 0.0f;
    };
    Cell flipFlopA, startExit, flipFlopIsA, printString;

    session.Frame([&](int)
    {
        DebugOverlay overlay(blueprint);
        auto cell = [&overlay](Pin& pin, ed::PinKind kind, Cell& result)
        {
            ImGui::BeginGroup();
            ed::BeginPin(pin.m_Id, kind);
            ImGui::TextUnformatted("Pin");
            ed::EndPin();
            result.PinEnd = ImGui::GetItemRectMax().x;
            if (kind == ed::PinKind::Output)
                overlay.DrawOutputPin(pin);
            else
                overlay.DrawInputPin(pin);
            ImGui::EndGroup();
            result.CellEnd = ImGui::GetItemRectMax().x;
        };

        overlay.Begin();
        ed::BeginNode(example.m_FlipFlop->m_Id);
        cell(example.m_FlipFlop->m_A, ed::PinKind::Output, flipFlopA);
        cell(example.m_FlipFlop->m_IsA, ed::PinKind::Output, flipFlopIsA);
        ed::EndNode();
        ed::BeginNode(example.m_Entry->m_Id);
        cell(example.m_Entry->m_Exit, ed::PinKind::Output, startExit);
        ed::EndNode();
        ed::BeginNode(example.m_PrintB->m_Id);
        cell(example.m_PrintB->m_String, ed::PinKind::Input, printString);
        ed::EndNode();
        overlay.End();
    });

    test::Check(test::AssertCount() == 0, "the groups end without an IM_ASSERT: %d", test::AssertCount());
    test::Check(flipFlopA.CellEnd == flipFlopA.PinEnd, "Flip Flop's A ends its cell: at %g, the pin at %g", flipFlopA.CellEnd, flipFlopA.PinEnd);
    test::Check(startExit.CellEnd == startExit.PinEnd, "so does Start's Exit, with the current flow marker after it: at %g, the pin at %g", startExit.CellEnd, startExit.PinEnd);
    test::Check(flipFlopIsA.CellEnd == flipFlopIsA.PinEnd, "Flip Flop's Is A, with false after it: at %g, the pin at %g", flipFlopIsA.CellEnd, flipFlopIsA.PinEnd);
    test::Check(printString.CellEnd == printString.PinEnd, "and Print's String, with a value wider than it before it: at %g, the pin at %g", printString.CellEnd, printString.PinEnd);
}

TEST_CASE(SaveLiveState, "blueprints2.save_writes_the_blueprint_as_it_is", "EXB-5", test::TestFlags_None)
{
    // Do N's count changes, as an edit in place did, after the last undo step.
    test::Session    session(UnnamedConfig());
    Document         document;
    ExampleBlueprint example(document.m_Blueprint);
    session.Use([&] { document.m_DocumentState = document.BuildDocumentState(); });
    example.m_DoN->m_N.SetValue(7);

    TestFile file("blueprints2_save.bp");
    const bool saved  = session.Use([&] { return document.Save(file.Path); });
    Document reloaded;
    const bool loaded = reloaded.Load(file.Path);
    auto doN = loaded ? reloaded.m_Blueprint.FindNode(example.m_DoN->m_Id) : nullptr;
    const auto count = doN ? static_cast<DoNNode*>(doN)->m_N.GetValue() : PinValue();

    test::Check(saved && loaded && doN, "the document saves and loads");
    test::Check(count.GetType() == PinType::Int32 && count.As<int32_t>() == 7, "with the count Do N had when saved");
}
TEST_CASE(StepsMarkModified, "blueprints2.undo_steps_mark_the_document_modified", "EXB-5", test::TestFlags_None)
{
    test::Session    session(UnnamedConfig());
    OverlayLog       log;
    Document         document;
    ExampleBlueprint example(document.m_Blueprint);
    bool fresh = true, edited = false, undone = false, redone = false;
    session.Use([&]
    {
        document.m_DocumentState = document.BuildDocumentState();
        fresh = document.m_IsModified;
        {
            auto transaction = document.BeginUndoTransaction("Edit Value");
            example.m_DoN->m_N.SetValue(7);
            transaction->AddAction("N changed");
        }
        edited = document.m_IsModified;

        // As saving does.
        document.m_IsModified = false;
        document.Undo();
        undone = document.m_IsModified;

        document.m_IsModified = false;
        document.Redo();
        redone = document.m_IsModified;
    });

    test::Check(!fresh, "a new document has no changes");
    test::Check(edited, "an undo step marks it modified");
    test::Check(undone && redone, "and so do undo and redo: %d, %d", undone, redone);
}

TEST_CASE(ForLoopCounts, "blueprints2.for_loop_runs_its_body_once_per_index", "EXB-7", test::TestFlags_None)
{
    ForLoopBlueprint loop(0, 2);
    const auto result  = loop.Run();
    const auto printed = Printed();

    test::Check(result == StepResult::Done, "a loop from 0 to 2 ends: %s", StepResultToString(result));
    test::Check(printed == "0|1|2|", "and its body sees each index once: %.60s", printed.c_str());
}

TEST_CASE(ForLoopLargestIndex, "blueprints2.for_loop_ends_at_the_largest_index", "EXB-7", test::TestFlags_None)
{
    ForLoopBlueprint loop(INT32_MAX - 1, INT32_MAX);
    const auto result  = loop.Run();
    const auto printed = Printed();

    test::Check(result == StepResult::Done, "a loop up to INT32_MAX ends: %s", StepResultToString(result));
    test::Check(printed == "2147483646|2147483647|", "after its last two indices: %.60s", printed.c_str());
}

TEST_CASE(MalformedBlueprints, "blueprints2.malformed_blueprints_are_refused", "EXB-8", test::TestFlags_None)
{
    // The example's blueprint, with constants of the value types and typed nodes of no links,
    // saved and changed as a file may be.
    Blueprint        source;
    ExampleBlueprint example(source);
    auto text  = source.CreateNode<ConstStringNode>();
    auto count = source.CreateNode<ConstInt32Node>();
    auto scale = source.CreateNode<ConstFloatNode>();
    auto sum   = source.CreateNode<AddNode>();
    auto label = source.CreateNode<ToStringNode>();
    sum->SetType(PinType::Int32);
    label->SetType(PinType::Bool);
    crude_json::value saved;
    source.Save(saved);

    using Change = std::function<void(crude_json::value&)>;
    const std::pair<const char*, Change> changes[] =
    {
        { "an Any pin of type Void",               [&](crude_json::value& v) { PinEntry(v, example.m_Add->m_A.m_Id)["type"] = -1; } },
        { "an Any pin of type 42",                 [&](crude_json::value& v) { PinEntry(v, example.m_Add->m_A.m_Id)["type"] = 42; } },
        { "an Any pin of type Flow",               [&](crude_json::value& v) { PinEntry(v, example.m_Add->m_A.m_Id)["type"] = 1; } },
        { "an Add of another type than its pins",  [&](crude_json::value& v) { NodeEntry(v, example.m_Add->m_Id)["type"] = "Float"; } },
        { "a To String of another type than its value", [&](crude_json::value& v) { NodeEntry(v, example.m_ToString->m_Id)["type"] = "String"; } },
        { "an unlinked Add of another type than its pins", [&](crude_json::value& v) { NodeEntry(v, sum->m_Id)["type"] = "Float"; } },
        { "an Add of Bool values",                 [&](crude_json::value& v)
        {
            NodeEntry(v, sum->m_Id)["type"] = "Bool";
            for (auto pin : { &sum->m_A, &sum->m_B, &sum->m_Result })
            {
                auto& entry = PinEntry(v, pin->m_Id);
                entry["type"] = static_cast<int>(PinType::Bool);
                entry["inner"]["value"] = false;
            }
        } },
        { "an unlinked To String of another type than its value", [&](crude_json::value& v) { NodeEntry(v, label->m_Id)["type"] = "Int32"; } },
        { "a String pin linked to a Flow pin",     [&](crude_json::value& v) { PinEntry(v, example.m_PrintA->m_String.m_Id)["link"] = example.m_Entry->m_Exit.m_Id; } },
        { "a Flow pin linked to a String pin",     [&](crude_json::value& v) { PinEntry(v, example.m_DoN->m_Exit.m_Id)["link"] = text->m_String.m_Id; } },
        { "an Int32 Add linked to a String",       [&](crude_json::value& v) { PinEntry(v, example.m_Add->m_B.m_Id)["link"] = text->m_String.m_Id; } },
        { "a link to no pin",                      [&](crude_json::value& v) { PinEntry(v, example.m_PrintA->m_String.m_Id)["link"] = 99999; } },
        { "a node ID of -1",                       [&](crude_json::value& v) { NodeEntry(v, example.m_Add->m_Id)["id"] = -1; } },
        { "a node ID of 2^32",                     [&](crude_json::value& v) { NodeEntry(v, example.m_Add->m_Id)["id"] = 4294967296.0; } },
        { "a pin ID of 1.5",                       [&](crude_json::value& v) { PinEntry(v, example.m_Add->m_B.m_Id)["id"] = 1.5; } },
        { "a pin ID of 0",                         [&](crude_json::value& v) { PinEntry(v, example.m_Add->m_B.m_Id)["id"] = 0; } },
        { "two pins of one ID",                    [&](crude_json::value& v) { PinEntry(v, example.m_Add->m_B.m_Id)["id"] = example.m_Add->m_A.m_Id; } },
        { "an Int32 value of 1e10",                [&](crude_json::value& v) { PinEntry(v, count->m_Int32.m_Id)["value"] = 1e10; } },
        { "an Int32 value of 2.5",                 [&](crude_json::value& v) { PinEntry(v, count->m_Int32.m_Id)["value"] = 2.5; } },
        { "a Float value of 1e39",                 [&](crude_json::value& v) { PinEntry(v, scale->m_Float.m_Id)["value"] = 1e39; } },
    };

    Blueprint control;
    test::Check(control.Load(saved) && control.GetNodes().size() == source.GetNodes().size(), "the saved blueprint loads");

    for (auto& change : changes)
    {
        auto changed = saved;
        change.second(changed);

        Blueprint target;
        auto kept = target.CreateNode<PrintNode>();
        const bool loaded = target.Load(changed);
        test::Check(!loaded && target.GetNodes().size() == 1 && target.GetNodes()[0] == kept, "%s is refused, and the blueprint kept", change.first);
    }
}

TEST_CASE(LoadedIdsStayUnique, "blueprints2.new_nodes_take_ids_no_loaded_one_has", "EXB-8", test::TestFlags_None)
{
    // A file whose generator state is below its IDs.
    Blueprint        source;
    ExampleBlueprint example(source);
    crude_json::value saved;
    source.Save(saved);
    saved["state"]["generator_state"] = 1;

    Blueprint loaded;
    const bool ok = loaded.Load(saved);
    auto print = loaded.CreateNode<PrintNode>();

    int reused = 0;
    for (auto id : { print->m_Id, print->m_Enter.m_Id, print->m_Exit.m_Id, print->m_String.m_Id })
    {
        for (auto node : loaded.GetNodes())
            reused += node != print && node->m_Id == id;
        for (auto pin : loaded.GetPins())
            reused += pin->m_Node != print && pin->m_Id == id;
    }

    test::Check(ok, "the blueprint loads");
    test::Check(reused == 0, "a node made after it takes IDs no loaded node or pin has: %d reused", reused);
}

TEST_CASE(WrongTypeFailsStep, "blueprints2.a_value_of_another_type_fails_the_step", "EXB-8", test::TestFlags_None)
{
    // Do N counts to the end of a chain of links too long to evaluate, which gives no value, not an Int32.
    Blueprint blueprint;
    auto entry = blueprint.CreateNode<EntryPointNode>();
    auto doN   = blueprint.CreateNode<DoNNode>();
    entry->m_Exit.LinkTo(doN->m_Enter);

    std::vector<AddNode*> chain(1, blueprint.CreateNode<AddNode>());
    for (int i = 1; i < 300; ++i)
    {
        chain.push_back(blueprint.CreateNode<AddNode>());
        chain.back()->m_A.m_Link = &chain[chain.size() - 2]->m_Result;
    }
    doN->m_N.m_Link = &chain.back()->m_Result;

    const auto result = blueprint.Execute(*entry);

    test::Check(result == StepResult::Error, "the step that reads it fails: %s", StepResultToString(result));
    test::Check(blueprint.CurrentNode() == doN, "in Do N");
}
