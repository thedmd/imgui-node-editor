//------------------------------------------------------------------------------
// blueprints2-example's blueprint model and documents, without its window.
//------------------------------------------------------------------------------
# include "harness.h"
# include "crude_blueprint.h"
# include "crude_blueprint_library.h"
# include <string>
# include <vector>

using namespace crude_blueprint;

namespace {

std::vector<std::string> s_Printed;

void Capture(const PrintNode&, string_view message)
{
    s_Printed.push_back(message.to_string());
}

// The example's document: Start runs Do N five times, and Flip Flop alternates
// between printing "FlipFlop slot A!" and the counter plus 3.
struct ExampleBlueprint
{
    Blueprint       m_Blueprint;
    PrintNode*      m_PrintB     = m_Blueprint.CreateNode<PrintNode>();
    EntryPointNode* m_Entry      = m_Blueprint.CreateNode<EntryPointNode>();
    PrintNode*      m_PrintA     = m_Blueprint.CreateNode<PrintNode>();
    FlipFlopNode*   m_FlipFlop   = m_Blueprint.CreateNode<FlipFlopNode>();
    ToStringNode*   m_ToString   = m_Blueprint.CreateNode<ToStringNode>();
    DoNNode*        m_DoN        = m_Blueprint.CreateNode<DoNNode>();
    AddNode*        m_Add        = m_Blueprint.CreateNode<AddNode>();

    ExampleBlueprint()
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
    ExampleBlueprint example;
    const auto result = example.m_Blueprint.Execute(*example.m_Entry);

    std::string printed;
    for (auto& message : s_Printed)
        printed += message + "|";
    test::Check(result == StepResult::Done, "the example runs to its end: %s", StepResultToString(result));
    test::Check(printed == "FlipFlop slot A!|5|FlipFlop slot A!|7|FlipFlop slot A!|", "and prints: %s", printed.c_str());
}
