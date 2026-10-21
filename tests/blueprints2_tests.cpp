//------------------------------------------------------------------------------
// blueprints2-example's blueprint model and documents, without its window.
//------------------------------------------------------------------------------
# include "harness.h"
# include "blueprint_editor_document.h"
# include "blueprint_editor_utilities.h"
# include "crude_blueprint.h"
# include "crude_blueprint_library.h"
# include "crude_logger.h"
# include "imgui_extras.h"
# include <crude_json.h>
# include <cmath>
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

// A node type that counts its nodes.
struct CountedNode final : Node
{
    CRUDE_BP_NODE(CountedNode, "Counted")

    CountedNode(Blueprint& blueprint): Node(blueprint) { ++s_Alive; }
    ~CountedNode() override { --s_Alive; }

    static int s_Alive;
};

int CountedNode::s_Alive = 0;

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

TEST_CASE(DebugOverlayFlow, "blueprints2.debug_overlay_flows_only_the_next_branch", nullptr, test::TestFlags_None)
{
    // Execution paused in Flip Flop, with Print A to run next through Flip Flop's A. Every node
    // shows its pins with the overlay's values beside them, as in the example, in two rows that
    // leave the link from Flip Flop's A to Print A alone between those two pins.
    test::Session    session(UnnamedConfig());
    Blueprint        blueprint;
    ExampleBlueprint example(blueprint);
    blueprint.Start(*example.m_Entry);
    blueprint.Step();
    blueprint.Step();

    const std::pair<const Node*, ImVec2> layout[] =
    {
        { example.m_Entry,    ImVec2(  0.0f,   0.0f) },
        { example.m_DoN,      ImVec2(200.0f,   0.0f) },
        { example.m_FlipFlop, ImVec2(420.0f,   0.0f) },
        { example.m_PrintA,   ImVec2(700.0f,   0.0f) },
        { example.m_Add,      ImVec2(200.0f, 260.0f) },
        { example.m_ToString, ImVec2(420.0f, 260.0f) },
        { example.m_PrintB,   ImVec2(700.0f, 260.0f) },
    };

    const ImU32 flowColor = IM_COL32(255, 0, 255, 255);
    ImVec2 flipFlopA, printAEnter;
    session.DefaultContent = [&](int frame)
    {
        if (frame == 0)
        {
            auto& style = ed::GetStyle();
            style.Colors[ed::StyleColor_Flow]       = ImColor(flowColor);
            style.Colors[ed::StyleColor_FlowMarker] = ImColor(flowColor);
            for (auto& entry : layout)
                ed::SetNodePosition(entry.first->m_Id, entry.second);
        }

        DebugOverlay overlay(blueprint);
        overlay.Begin();
        for (auto node : blueprint.GetNodes())
        {
            ed::BeginNode(node->m_Id);
            const auto name = node->GetName();
            ImGui::TextUnformatted(name.data(), name.data() + name.size());
            for (auto kind : { ed::PinKind::Input, ed::PinKind::Output })
            {
                if (kind == ed::PinKind::Output)
                    ImGui::SameLine(0.0f, 40.0f);
                ImGui::BeginGroup();
                for (auto pin : kind == ed::PinKind::Input ? node->GetInputPins() : node->GetOutputPins())
                {
                    ed::BeginPin(pin->m_Id, kind);
                    ImGui::TextUnformatted(kind == ed::PinKind::Input ? "> in" : "out >");
                    ed::EndPin();
                    const auto center = ed::CanvasToScreen((ImGui::GetItemRectMin() + ImGui::GetItemRectMax()) * 0.5f);
                    if (pin == &example.m_FlipFlop->m_A)
                        flipFlopA = center;
                    if (pin == &example.m_PrintA->m_Enter)
                        printAEnter = center;
                    if (kind == ed::PinKind::Input)
                        overlay.DrawInputPin(*pin);
                    else
                        overlay.DrawOutputPin(*pin);
                }
                ImGui::EndGroup();
            }
            ed::EndNode();
        }
        for (auto pin : blueprint.GetPins())
            if (pin->m_Link)
                ed::Link(pin->m_Id, pin->m_Id, pin->m_Link->m_Id);
        overlay.End();
    };

    int inside = 0, outside = 0;
    session.AfterEditor = [&]
    {
        // Flow is drawn over the whole link: its vertices may stray from the line between the
        // pins by the link's thickness.
        const ImRect nextBranch(ImMin(flipFlopA, printAEnter) - ImVec2(8.0f, 8.0f), ImMax(flipFlopA, printAEnter) + ImVec2(8.0f, 8.0f));
        const auto geometry = test::Capture(session.DrawList);
        inside = outside = 0;
        for (size_t i = 0; i < geometry.Colors.size(); ++i)
            if ((geometry.Colors[i] & ~IM_COL32_A_MASK) == (flowColor & ~IM_COL32_A_MASK))
                ++(nextBranch.Contains(geometry.Positions[i]) ? inside : outside);
    };
    session.Run(3);

    test::Check(blueprint.CurrentNode() == example.m_FlipFlop && blueprint.NextNode() == example.m_PrintA, "execution paused in Flip Flop, with Print A next");
    test::Check(inside > 0, "the link from Flip Flop's A to Print A flows: %d flow vertices", inside);
    test::Check(outside == 0, "and no other link does: %d flow vertices elsewhere", outside);

    // The next step runs Print A and returns to Do N, through no link. Frames take 1/60 s.
    blueprint.Step();
    session.Run(15);
    const int fading = inside + outside;
    session.Run(21);
    const int faded = inside + outside;
    test::Check(fading > 0, "a quarter of a second after the step, the link it took still fades: %d flow vertices", fading);
    test::Check(faded == 0, "and 0.6 seconds after it, no link flows: %d flow vertices", faded);
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

TEST_CASE(FloatPinsStayFinite, "blueprints2.float_pins_take_only_finite_values", "EXB-9", test::TestFlags_None)
{
    // As the value editor sets what the user typed: "nan", "inf" or "1e39".
    Blueprint blueprint;
    auto scale = blueprint.CreateNode<ConstFloatNode>();
    scale->m_Float.SetValue(2.5f);

    int taken = 0;
    for (auto value : { std::nanf(""), INFINITY, -INFINITY })
        taken += scale->m_Float.SetValue(value);

    test::Check(taken == 0 && scale->m_Float.m_Value == 2.5f, "a NaN or infinity doesn't replace 2.5: %d taken, %g", taken, scale->m_Float.m_Value);
    test::Check(scale->m_Float.SetValue(-1e30f) && scale->m_Float.m_Value == -1e30f, "a finite value does");
}

TEST_CASE(SaveKeepsLoadableFile, "blueprints2.a_document_that_wouldnt_load_isnt_saved", "EXB-9", test::TestFlags_None)
{
    // A Float pin set around SetValue(), as code may, to a NaN, which documents have no number for.
    test::Session    session(UnnamedConfig());
    Document         document;
    ExampleBlueprint example(document.m_Blueprint);
    auto scale = document.m_Blueprint.CreateNode<ConstFloatNode>();

    TestFile file("blueprints2_unloadable.bp");
    const bool first = session.Use([&] { return document.Save(file.Path); });
    scale->m_Float.m_Value = std::nanf("");
    const bool second = session.Use([&] { return document.Save(file.Path); });

    Document reloaded;
    const bool loaded = reloaded.Load(file.Path);
    auto reloadedScale = loaded ? static_cast<ConstFloatNode*>(reloaded.m_Blueprint.FindNode(scale->m_Id)) : nullptr;

    test::Check(first, "the document saves");
    test::Check(!second, "with a NaN in a Float pin, it doesn't");
    test::Check(reloadedScale && reloadedScale->m_Float.m_Value == 0.0f, "and the file it saved before still loads");
}

TEST_CASE(Utf8Documents, "blueprints2.utf8_text_and_paths_round_trip", "EXB-9", test::TestFlags_None)
{
    // Polish text in a Print pin and in the file name, in UTF-8, as the file dialogs give paths.
    const char* text = "za\xC5\xBC\xC3\xB3\xC5\x82\xC4\x87";
    const char* path = "blueprints2_za\xC5\xBC\xC3\xB3\xC5\x82\xC4\x87.bp";
    test::Session    session(UnnamedConfig());
    Document         document;
    ExampleBlueprint example(document.m_Blueprint);
    example.m_PrintA->m_String.SetValue(text);

    const bool saved = session.Use([&] { return document.Save(path); });
    Document reloaded;
    const bool loaded = reloaded.Load(path);
    auto print = loaded ? static_cast<PrintNode*>(reloaded.m_Blueprint.FindNode(example.m_PrintA->m_Id)) : nullptr;

    test::Check(saved && loaded, "a document saves under a UTF-8 path and loads back");
    test::Check(print && print->m_String.m_Value == text, "with the UTF-8 text of its pin");

# ifdef _WIN32
    test::Check(_wremove(L"blueprints2_za\u017C\u00F3\u0142\u0107.bp") == 0, "the file has the name the path spells");
# else
    std::remove(path);
# endif
}

TEST_CASE(RecentFilesIni, "blueprints2.recent_files_take_only_indices_in_order", "EXB-11", test::TestFlags_None)
{
    // An imgui.ini edited by hand. The list writes its entries from index 0 up.
    test::Host host;
    ImEx::MostRecentlyUsedList::Install(host.Context);
    ImGui::LoadIniSettingsFromMemory(
        "[MostRecentlyUsedList][EXB-11]\n"
        "0=a.bp\n"
        "-1=negative.bp\n"
        "1=b.bp\n"
        "7=gap.bp\n"
        "4294967296=wraps.bp\n"
        "2000000000=huge.bp\n"
        "1=b2.bp\n");

    ImEx::MostRecentlyUsedList list("EXB-11");
    std::string entries;
    for (auto& entry : list.GetList())
        entries += entry + "|";

    test::Check(entries == "a.bp|b2.bp|", "only entries at an index of the list, or the next one, are read: %s", entries.c_str());
}

TEST_CASE(RegisteredNameKept, "blueprints2.a_registered_node_type_keeps_its_name", "CBP-1", test::TestFlags_NeedsSanitizer)
{
    // A name built for the call, as an application building names does; too long for a string's own buffer.
    auto registry = std::make_shared<NodeRegistry>();
    const std::string name = std::string("ExampleNodeType") + "WithALongName";
    registry->RegisterNodeType(name, ConstInt32Node::GetStaticTypeInfo().m_Factory);

    Blueprint blueprint(registry);
    const bool created = blueprint.CreateNode(string_view(name)) != nullptr;
    registry->UnregisterNodeType(name);
    const bool unregistered = blueprint.CreateNode(string_view(name)) == nullptr;

    test::Check(created, "a node of the type is created by its name");
    test::Check(unregistered, "and the type is unregistered by it");
}

TEST_CASE(DeleteBreaksLinks, "blueprints2.deleting_a_node_breaks_its_links", "CBP-5", test::TestFlags_None)
{
    // Nodes deleted through the API, as a cut or a delete command does, with links on both sides.
    Blueprint blueprint;
    auto constant = blueprint.CreateNode<ConstInt32Node>();
    auto toString = blueprint.CreateNode<ToStringNode>();
    auto print    = blueprint.CreateNode<PrintNode>();
    toString->m_Value.LinkTo(constant->m_Int32);
    print->m_String.LinkTo(toString->m_String);
    const bool typed = toString->m_Value.GetValueType() == PinType::Int32;

    blueprint.DeleteNode(constant);
    const bool valueUnlinked = !toString->m_Value.IsLinked() && toString->m_Value.GetValueType() == PinType::Any;
    blueprint.DeleteNode(toString);
    const bool stringUnlinked = !print->m_String.IsLinked();

    crude_json::value saved;
    blueprint.Save(saved);

    test::Check(typed, "To String took Int32 from the constant");
    test::Check(valueUnlinked, "deleting the constant unlinks To String, which takes any type again");
    test::Check(stringUnlinked, "deleting To String unlinks Print");
}

TEST_CASE(MoveDeletesNodes, "blueprints2.a_blueprint_moved_over_deletes_its_nodes", "CBP-9", test::TestFlags_None)
{
    auto registry = std::make_shared<NodeRegistry>();
    registry->RegisterNodeType("CountedNode", CountedNode::GetStaticTypeInfo().m_Factory);
    const auto alive = CountedNode::s_Alive;
    int afterMove = 0;
    {
        Blueprint target(registry);
        target.CreateNode<CountedNode>();
        Blueprint source(registry);
        target = std::move(source);
        afterMove = CountedNode::s_Alive - alive;
    }

    test::Check(afterMove == 0, "moving another blueprint over one deletes its node: %d alive", afterMove);
    test::Check(CountedNode::s_Alive == alive, "and none is left when both go: %d alive", CountedNode::s_Alive - alive);
}

TEST_CASE(Int32AddWraps, "blueprints2.int32_add_wraps_around", "CBP-13", test::TestFlags_None)
{
    // A sum out of Int32's range is undefined in C++; UndefinedBehaviorSanitizer reports it.
    Blueprint blueprint;
    auto constant = blueprint.CreateNode<ConstInt32Node>();
    auto add      = blueprint.CreateNode<AddNode>();
    add->m_A.LinkTo(constant->m_Int32);
    auto sum = [&](int32_t a, int32_t b)
    {
        constant->m_Int32.SetValue(a);
        add->m_B.SetValue(b);
        const auto value = blueprint.GetContext().GetPinValue(add->m_Result);
        return value.GetType() == PinType::Int32 ? value.As<int32_t>() : 0;
    };
    const auto up   = sum(INT32_MAX, 1);
    const auto down = sum(INT32_MIN, -1);
    const auto far  = sum(INT32_MAX, INT32_MAX);

    test::Check(up == INT32_MIN, "INT32_MAX + 1 is INT32_MIN: %d", up);
    test::Check(down == INT32_MAX, "INT32_MIN - 1 is INT32_MAX: %d", down);
    test::Check(far == -2, "INT32_MAX + INT32_MAX is -2: %d", far);
}
