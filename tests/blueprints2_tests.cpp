//------------------------------------------------------------------------------
// blueprints2-example's blueprint model and documents, without its window.
//------------------------------------------------------------------------------
# include "harness.h"
# include "blueprint_editor_document.h"
# include "crude_blueprint.h"
# include "crude_blueprint_library.h"
# include "crude_logger.h"
# include <cstdio>
# include <string>
# include <vector>

using namespace crude_blueprint;
using blueprint_editor::Document;

namespace {

std::vector<std::string> s_Printed;

void Capture(const PrintNode&, string_view message)
{
    s_Printed.push_back(message.to_string());
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

        s_Printed.clear();
        PrintNode::s_PrintFunction = Capture;
    }

    ~ExampleBlueprint()
    {
        PrintNode::s_PrintFunction = nullptr;
    }
};

} // namespace

TEST_CASE(Blueprints2Runs, "blueprints2.example_blueprint_runs", nullptr, test::TestFlags_None)
{
    Blueprint        blueprint;
    ExampleBlueprint example(blueprint);
    const auto result = example.m_Blueprint.Execute(*example.m_Entry);

    std::string printed;
    for (auto& message : s_Printed)
        printed += message + "|";
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
