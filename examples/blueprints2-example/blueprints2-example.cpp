# include <application.h>
# define IMGUI_DEFINE_MATH_OPERATORS
# include <imgui_internal.h>

# include "crude_blueprint.h"
# include "crude_layout.h"
# include "imgui_node_editor.h"

# include "crude_json.h"

namespace ed = ax::NodeEditor;

using namespace crude_blueprint;

static ed::EditorContext* g_Editor = nullptr;
static Blueprint g_Blueprint;


const char* Application_GetName()
{
    return "Blueprints2";
}

void Application_Initialize()
{
    using namespace crude_blueprint;

    //IdGenerator g;

    //Context executor;

    //ConstBoolNode valueA{g};
    //valueA.m_Bool.m_Value = true;

    //ToStringNode toStringNode{g};
    //toStringNode.m_Value.m_Link = &valueA.m_Bool;

    //PrintNode truePrintNode{g};
    //truePrintNode.m_String.m_Value = "It's true!";
    //truePrintNode.m_String.m_Link = &toStringNode.m_String;
    //toStringNode.m_Exit.m_Link = &truePrintNode.m_Enter;

    //PrintNode falsePrintNode{g};
    //falsePrintNode.m_String.m_Value = "It's false!";


    //BranchNode branchNode{g};
    //branchNode.m_Condition.m_Link = &valueA.m_Bool;
    //branchNode.m_True.m_Link = &toStringNode.m_Enter;
    //branchNode.m_False.m_Link = &falsePrintNode.m_Enter;

    //FlipFlopNode flipFlopNode{g};

    //DoNNode doNNode{g};
    //doNNode.m_Exit.m_Link = &flipFlopNode.m_Enter;
    //doNNode.m_N.m_Value = 5;
    //toStringNode.m_Value.m_Link = &doNNode.m_Counter;

    //flipFlopNode.m_A.m_Link = &branchNode.m_Enter;
    //flipFlopNode.m_B.m_Link = &falsePrintNode.m_Enter;

    //executor.Execute(doNNode.m_Enter);





    auto printNode2Node = g_Blueprint.CreateNode<PrintNode>();
    auto entryPointNode = g_Blueprint.CreateNode<EntryPointNode>();
    auto printNode1Node = g_Blueprint.CreateNode<PrintNode>();
    auto flipFlopNode = g_Blueprint.CreateNode<FlipFlopNode>();
    auto toStringNode = g_Blueprint.CreateNode<ToStringNode>();
    auto doNNode = g_Blueprint.CreateNode<DoNNode>();

    entryPointNode->m_Exit.m_Link = &doNNode->m_Enter;

    doNNode->m_N.m_Value = 5;
    doNNode->m_Exit.m_Link = &flipFlopNode->m_Enter;

    toStringNode->m_Value.m_Link = &doNNode->m_Counter;
    toStringNode->m_Exit.m_Link = &printNode2Node->m_Enter;

    printNode1Node->m_String.m_Value = "FlipFlop slot A!";
    printNode2Node->m_String.m_Value = "FlipFlop slot B!";
    printNode2Node->m_String.m_Link = &toStringNode->m_String;

    //printNode2Node->m_String.m_Link = &toStringNode.m_Enter;

    flipFlopNode->m_A.m_Link = &printNode1Node->m_Enter;
    flipFlopNode->m_B.m_Link = &toStringNode->m_Enter;


    //auto constBoolNode  = g_Blueprint.CreateNode<ConstBoolNode>();
    //auto toStringNode   = g_Blueprint.CreateNode<ToStringNode>();
    //auto printNodeNode  = g_Blueprint.CreateNode<PrintNode>();

    //crude_json::value value;
    //g_Blueprint.Save(value);
    //auto yyy = value.dump();

    //g_Blueprint.Execute(*entryPointNode);

    //Blueprint b2;

    //b2.Load(value);

    //crude_json::value value2;
    //b2.Save(value2);
    //auto zzz = value2.dump();

    //bool ok = yyy == zzz;

    //auto b3 = b2;


    ed::Config config;
    config.SettingsFile = "blueprint2-example.cfg";


    g_Editor = ed::CreateEditor(&config);
}

void Application_Finalize()
{
    ed::DestroyEditor(g_Editor);
}

void Application_Frame()
{
    ed::SetCurrentEditor(g_Editor);

    ed::Begin("###main_editor");

    // Commit all nodes to editor
    for (auto& node : g_Blueprint.GetNodes())
    {
        ed::BeginNode(node->m_Id);

        //
        // General node layout:
        //
        // Everything is aligned to the left. ImGui does not handle layouts, yet.
        //
        // +-----------------------------------------+
        // | Title                                   |
        // | +--------------[ Dummy ]--------------+ |
        // | +-[ Group ]-+             +-[ Group ]-+ |
        // | | o In B    |             |   Out B o | |
        // | | o In A    |             |   Out A o | |
        // | | o In C    |             |           | |
        // | +-----------+             +-----------+ |
        // +-----------------------------------------+

        ImGui::TextUnformatted(node->m_Name.data(), node->m_Name.data() + node->m_Name.size());

        ImGui::Dummy(ImVec2(120.0f, 0.0f)); // For minimum node width

        crude_layout::Grid layout;
        layout.Begin(node->m_Id, 2, 120.0f);
        layout.SetColumnAlignment(0.0f);

        for (auto& pin : node->GetInputPins())
        {
            ed::BeginPin(pin->m_Id, ed::PinKind::Input);
            ed::PinPivotAlignment(ImVec2(0.0f, 0.5f));
            ImGui::TextUnformatted("o");
            if (!pin->m_Name.empty())
            {
                ImGui::SameLine();
                ImGui::TextUnformatted(pin->m_Name.data(), pin->m_Name.data() + pin->m_Name.size());
            }
            ed::EndPin();
            layout.NextRow();
        }

        layout.SetColumnAlignment(1.0f);
        layout.NextColumn();

        for (auto& pin : node->GetOutputPins())
        {
            ed::BeginPin(pin->m_Id, ed::PinKind::Output);
            ed::PinPivotAlignment(ImVec2(1.0f, 0.5f));
            if (!pin->m_Name.empty())
            {
                ImGui::TextUnformatted(pin->m_Name.data(), pin->m_Name.data() + pin->m_Name.size());
                ImGui::SameLine();
            }
            ImGui::TextUnformatted("o");
            ed::EndPin();
            layout.NextRow();
        }

        layout.End();

        ed::EndNode();
    }

    // Commit all links to editor
    for (auto& pin : g_Blueprint.GetPins())
    {
        if (!pin->m_Link)
            continue;

        ed::Link(pin->m_Id, pin->m_Id, pin->m_Link->m_Id);
    }

    ed::End();


}