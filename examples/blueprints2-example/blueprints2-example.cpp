# include <application.h>
# define IMGUI_DEFINE_MATH_OPERATORS
# include <imgui_internal.h>

# include "crude_blueprint.h"
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

struct CrudeGridLayout
{
    void Begin(const char* id, int columns);
    void Begin(ImU32 id, int columns);
    void NextColumn();
    void NextRow();
    void SetColumnAlignment(float alignment);
    void End();

private:
    int Seed(int column, int row) const { return column + row * m_Columns; }
    int Seed() const { return Seed(m_Column, m_Row); }
    int ColumnSeed(int column) const { return Seed(column, -1); }
    int ColumnSeed() const { return ColumnSeed(m_Column); }

    void EnterCell(int column, int row);
    void LeaveCell();

    int m_Columns = 1;
    int m_Row = 0;
    int m_Column = 0;

    ImVec2 m_CursorPos;

    ImGuiStorage* m_Storage = nullptr;

    float m_ColumnAlignment = 0.0f;
    float m_MaximumColumnWidthAcc = -1.0f;
};

void CrudeGridLayout::Begin(const char* id, int columns)
{
    Begin(ImGui::GetID(id), columns);
}

void CrudeGridLayout::Begin(ImU32 id, int columns)
{
    m_CursorPos = ImGui::GetCursorScreenPos();

    ImGui::PushID(id);
    m_Columns = ImMax(1, columns);
    m_Storage = ImGui::GetStateStorage();

    for (int i = 0; i < columns; ++i)
    {
        ImGui::PushID(ColumnSeed());
        m_Storage->SetFloat(ImGui::GetID("MaximumColumnWidthAcc"), -1.0f);
        ImGui::PopID();
    }

    m_ColumnAlignment = 0.0f;

    EnterCell(0, 0);
}

void CrudeGridLayout::NextColumn()
{
    LeaveCell();

    int nextColumn = m_Column + 1;
    int nextRow    = 0;
    if (nextColumn > m_Columns)
    {
        nextColumn -= m_Columns;
        nextRow    += 1;
    }

    auto cursorPos = m_CursorPos;
    for (int i = 0; i < nextColumn; ++i)
    {
        ImGui::PushID(ColumnSeed(i));
        auto maximumColumnWidth = m_Storage->GetFloat(ImGui::GetID("MaximumColumnWidth"), -1.0f);
        ImGui::PopID();

        if (maximumColumnWidth > 0.0f)
            cursorPos.x += maximumColumnWidth + ImGui::GetStyle().ItemSpacing.x;
    }

    ImGui::SetCursorScreenPos(cursorPos);

    EnterCell(nextColumn, nextRow);
}

void CrudeGridLayout::NextRow()
{
    LeaveCell();

    auto cursorPos = ImGui::GetCursorScreenPos();
    cursorPos.x = m_CursorPos.x;
    for (int i = 0; i < m_Column; ++i)
    {
        ImGui::PushID(ColumnSeed(i));
        auto maximumColumnWidth = m_Storage->GetFloat(ImGui::GetID("MaximumColumnWidth"), -1.0f);
        ImGui::PopID();

        if (maximumColumnWidth > 0.0f)
            cursorPos.x += maximumColumnWidth + ImGui::GetStyle().ItemSpacing.x;
    }

    ImGui::SetCursorScreenPos(cursorPos);

    EnterCell(m_Column, m_Row + 1);
}

void CrudeGridLayout::EnterCell(int column, int row)
{
    m_Column = column;
    m_Row    = row;

    ImGui::PushID(ColumnSeed());
    m_MaximumColumnWidthAcc = m_Storage->GetFloat(ImGui::GetID("MaximumColumnWidthAcc"), -1.0f);
    auto maximumColumnWidth = m_Storage->GetFloat(ImGui::GetID("MaximumColumnWidth"), -1.0f);
    ImGui::PopID();

    ImGui::PushID(Seed());
    auto lastCellWidth = m_Storage->GetFloat(ImGui::GetID("LastCellWidth"), -1.0f);

    if (maximumColumnWidth >= 0.0f && lastCellWidth >= 0.0f)
    {
        auto freeSpace = maximumColumnWidth - lastCellWidth;

        auto offset = ImFloor(m_ColumnAlignment * freeSpace);

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
        ImGui::Dummy(ImVec2(offset, 0.0f));
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::PopStyleVar();
    }

    ImGui::BeginGroup();
}

void CrudeGridLayout::LeaveCell()
{
    ImGui::EndGroup();

    auto itemSize = ImGui::GetItemRectSize();
    m_Storage->SetFloat(ImGui::GetID("LastCellWidth"), itemSize.x);
    ImGui::PopID();

    m_MaximumColumnWidthAcc = ImMax(m_MaximumColumnWidthAcc, itemSize.x);
    ImGui::PushID(ColumnSeed());
    m_Storage->SetFloat(ImGui::GetID("MaximumColumnWidthAcc"), m_MaximumColumnWidthAcc);
    ImGui::PopID();
}

void CrudeGridLayout::SetColumnAlignment(float alignment)
{
    alignment = ImClamp(alignment, 0.0f, 1.0f);
    m_ColumnAlignment = alignment;
}

void CrudeGridLayout::End()
{
    LeaveCell();

    for (int i = 0; i < m_Columns; ++i)
    {
        ImGui::PushID(ColumnSeed(i));
        auto currentMaxCellWidth = m_Storage->GetFloat(ImGui::GetID("MaximumColumnWidthAcc"), -1.0f);
        m_Storage->SetFloat(ImGui::GetID("MaximumColumnWidth"), currentMaxCellWidth);
        ImGui::PopID();
    }

    ImGui::PopID();
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
        // | +-[ Group ]-+ +-[ Group ]-+             |
        // | | o In A    | | Out A o   |             |
        // | | o In B    | | Out B o   |             |
        // | | o In C    | |           |             |
        // | +-----------+ +-----------+             |
        // +-----------------------------------------+

        ImGui::TextUnformatted(node->m_Name.data(), node->m_Name.data() + node->m_Name.size());

        ImGui::Dummy(ImVec2(120.0f, 0.0f)); // For minimum node width

        CrudeGridLayout layout;
        layout.Begin(node->m_Id, 2);
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

        layout.NextColumn();
        layout.SetColumnAlignment(1.0f);

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