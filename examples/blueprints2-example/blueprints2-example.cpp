# include <application.h>
# define IMGUI_DEFINE_MATH_OPERATORS
# include <imgui_internal.h>

# include "crude_blueprint.h"
# include "crude_layout.h"
# include "utilities/widgets.h"
# include "imgui_node_editor.h"

# include "crude_json.h"

namespace ed = ax::NodeEditor;

using namespace crude_blueprint;

static ed::EditorContext* g_Editor = nullptr;
static Blueprint g_Blueprint;

struct ScopedItemWidth
{
    ScopedItemWidth(float width)
    {
        ImGui::PushItemWidth(width);
    }

    ~ScopedItemWidth()
    {
        ImGui::PopItemWidth();
    }
};

struct ScopedItemFlag
{
    ScopedItemFlag(ImGuiItemFlags flag)
        : m_Flag(flag)
    {
        ImGui::PushItemFlag(m_Flag, true);
    }

    ~ScopedItemFlag()
    {
        ImGui::PushItemFlag(m_Flag, false);
    }

private:
    ImGuiItemFlags m_Flag = ImGuiItemFlags_None;
};

static void Debug_DrawItemRect(const ImVec4& col = ImVec4(1.0f, 0.0f, 0.0f, 1.0f))
{
    auto drawList = ImGui::GetWindowDrawList();
    auto itemMin = ImGui::GetItemRectMin();
    auto itemMax = ImGui::GetItemRectMax();
    drawList->AddRect(itemMin, itemMax, ImColor(col));
}

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

static ax::Widgets::IconType PinTypeToIconType(PinType pinType)
{
    switch (pinType)
    {
        case PinType::Void:     return ax::Widgets::IconType::Circle;
        case PinType::Flow:     return ax::Widgets::IconType::Flow;
        case PinType::Bool:     return ax::Widgets::IconType::Circle;
        case PinType::Int32:    return ax::Widgets::IconType::Circle;
        case PinType::Float:    return ax::Widgets::IconType::Circle;
        case PinType::String:   return ax::Widgets::IconType::Circle;
    }

    return ax::Widgets::IconType::Circle;
}

static ImVec4 PinTypeToIconColor(PinType pinType)
{
    switch (pinType)
    {
        case PinType::Void:     return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        case PinType::Flow:     return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        case PinType::Bool:     return ImVec4(220 / 255.0f,  48 / 255.0f,  48 / 255.0f, 1.0f);
        case PinType::Int32:    return ImVec4( 68 / 255.0f, 201 / 255.0f, 156 / 255.0f, 1.0f);
        case PinType::Float:    return ImVec4(147 / 255.0f, 226 / 255.0f,  74 / 255.0f, 1.0f);
        case PinType::String:   return ImVec4(124 / 255.0f,  21 / 255.0f, 153 / 255.0f, 1.0f);
    }

    return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
}

static bool DrawPinImmediateValue(const Pin& pin)
{
    switch (pin.m_Type)
    {
        case PinType::Void:
            return false;
        case PinType::Flow:
            return false;
        case PinType::Bool:
            if (static_cast<const BoolPin&>(pin).m_Value)
                ImGui::TextUnformatted("true");
            else
                ImGui::TextUnformatted("false");
            return true;
        case PinType::Int32:
            ImGui::Text("%d", static_cast<const Int32Pin&>(pin).m_Value);
            return true;
        case PinType::Float:
            ImGui::Text("%f", static_cast<const FloatPin&>(pin).m_Value);
            return true;
        case PinType::String:
            ImGui::Text("%s", static_cast<const StringPin&>(pin).m_Value.c_str());
            return true;
    }

    return false;
}

static bool EditPinImmediateValue(Pin& pin)
{
    ScopedItemWidth scopedItemWidth{120};

    switch (pin.m_Type)
    {
        case PinType::Void:
            return false;
        case PinType::Flow:
            return false;
        case PinType::Bool:
            if (ImGui::Checkbox("##editor", &static_cast<BoolPin&>(pin).m_Value))
                return false;
            return true;
        case PinType::Int32:
            if (ImGui::InputInt("##editor", &static_cast<Int32Pin&>(pin).m_Value, 1, 100, ImGuiInputTextFlags_EnterReturnsTrue))
                return false;
            return true;
        case PinType::Float:
            if (ImGui::InputFloat("##editor", &static_cast<FloatPin&>(pin).m_Value, 1, 100, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue))
                return false;
            return true;
        case PinType::String:
            auto& stringValue = static_cast<StringPin&>(pin).m_Value;
            if (ImGui::InputText("##editor", (char*)stringValue.data(), stringValue.size() + 1, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackResize, [](ImGuiInputTextCallbackData *data) -> int
            {
                if (data->EventFlag == ImGuiInputTextFlags_CallbackResize)
                {
                    auto& stringValue = *static_cast<string*>(data->UserData);
                    ImVector<char>* my_str = (ImVector<char>*)data->UserData;
                    IM_ASSERT(stringValue.data() == data->Buf);
                    stringValue.resize(data->BufSize); // NB: On resizing calls, generally data->BufSize == data->BufTextLen + 1
                    data->Buf = (char*)stringValue.data();
                }
                return 0;
            }, &stringValue))
                return false;
            return true;
    }

    return false;
}

struct PinValueBackgroundDrawer
{
    PinValueBackgroundDrawer()
    {
        m_DrawList = ImGui::GetWindowDrawList();
        m_Splitter.Split(m_DrawList, 2);
        m_Splitter.SetCurrentChannel(m_DrawList, 1);
    }

    ~PinValueBackgroundDrawer()
    {
        if (m_Splitter._Current == 0)
            return;

        m_Splitter.SetCurrentChannel(m_DrawList, 0);

        auto itemMin = ImGui::GetItemRectMin();
        auto itemMax = ImGui::GetItemRectMax();
        auto margin = ImGui::GetStyle().ItemSpacing * 0.25f;
        margin.x = ImCeil(margin.x);
        margin.y = ImCeil(margin.y);
        itemMin -= margin;
        itemMax += margin;

        m_DrawList->AddRectFilled(itemMin, itemMax,
            ImGui::GetColorU32(ImGuiCol_CheckMark, 0.25f), 4.0f);

        m_DrawList->AddRect(itemMin, itemMax,
            ImGui::GetColorU32(ImGuiCol_Text, 0.25f), 4.0f);

        m_Splitter.Merge(m_DrawList);
    }

    void Discard()
    {
        if (m_Splitter._Current == 1)
            m_Splitter.Merge(m_DrawList);
    }

private:
    ImDrawList* m_DrawList = nullptr;
    ImDrawListSplitter m_Splitter;
};

static void EditOrDrawPinValue(Pin& pin)
{
    auto storage = ImGui::GetStateStorage();
    auto activePinId = storage->GetInt(ImGui::GetID("PinValueEditor_ActivePinId"), false);

    if (activePinId == pin.m_Id)
    {
        if (!EditPinImmediateValue(pin))
        {
            ed::EnableShortcuts(true);
            activePinId = 0;
        }
    }
    else
    {
        // Draw pin value
        PinValueBackgroundDrawer bg;
        if (!DrawPinImmediateValue(pin))
        {
            bg.Discard();
            return;
        }

        // Draw invisible button over pin value which triggers an editor if clicked
        auto itemMin = ImGui::GetItemRectMin();
        auto itemMax = ImGui::GetItemRectMax();

        ImGui::SetCursorScreenPos(itemMin);

        if (ImGui::InvisibleButton("###pin_value_editor", itemMax - itemMin))
        {
            activePinId = pin.m_Id;
            ed::EnableShortcuts(false);
        }
    }

    storage->SetInt(ImGui::GetID("PinValueEditor_ActivePinId"), activePinId);
}

static EntryPointNode* FindEntryPointNode()
{
    for (auto& node : g_Blueprint.GetNodes())
    {
        if (node->GetTypeInfo().m_Id == EntryPointNode::GetStaticTypeInfo().m_Id)
        {
            return static_cast<EntryPointNode*>(node);
        }
    }

    return nullptr;
}

static void ShowControlPanel()
{
    auto entryNode = FindEntryPointNode();

    ScopedItemFlag disableItemFlag(entryNode == nullptr);
    ImGui::GetStyle().Alpha = entryNode == nullptr ? 0.5f : 1.0f;

    bool doStep = false;
    if (ImGui::Button("Start"))
    {
        g_Blueprint.Start(*entryNode);
        //ed::SelectNode(entryNode->m_Id);
        //ed::NavigateToSelection();
        doStep = true;
    }

    ImGui::SameLine();
    if (doStep || ImGui::Button("Step"))
    {
        g_Blueprint.Step();
        if (auto currentNode = g_Blueprint.CurrentNode())
        {
            ed::SelectNode(currentNode->m_Id);
            //ed::NavigateToSelection();
        }
        else
        {
            ed::ClearSelection();
        }

        for (auto& touchedPinId : g_Blueprint.GetTouchedPinIds())
        {
            ed::Flow(touchedPinId);
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Run"))
    {
        g_Blueprint.Execute(*entryNode);
    }

    ImGui::GetStyle().Alpha = 1.0f;
}

void Application_Frame()
{
    ShowControlPanel();

    ImGui::Separator();

    ed::SetCurrentEditor(g_Editor);

    ed::Begin("###main_editor");

    const auto iconSize = ImVec2(ImGui::GetTextLineHeight(), ImGui::GetTextLineHeight());

    // Commit all nodes to editor
    for (auto& node : g_Blueprint.GetNodes())
    {
        ed::BeginNode(node->m_Id);

        //
        // General node layout:
        //
        // Everything is aligned to the left. ImGui does not handle layouts, yet.
        //
        // +-----------------------------------+
        // | Title                             |
        // | +-----------[ Dummy ]-----------+ |
        // | +-[ Group ]-----+   +-[ Group ]-+ |
        // | | o Pin         |   |   Out B o | |
        // | | o Pin <Value> |   |   Out A o | |
        // | | o Pin         |   |           | |
        // | +---------------+   +-----------+ |
        // +-----------------------------------+

        ImGui::PushFont(Application_HeaderFont());
        ImGui::TextUnformatted(node->m_Name.data(), node->m_Name.data() + node->m_Name.size());
        ImGui::PopFont();

        ImGui::Dummy(ImVec2(100.0f, 0.0f)); // For minimum node width

        crude_layout::Grid layout;
        layout.Begin(node->m_Id, 2, 100.0f);
        layout.SetColumnAlignment(0.0f);

        for (auto& pin : node->GetInputPins())
        {
            ImGui::Spacing(); // Add a bit of spacing to separate pins and make value not cramped
            ed::BeginPin(pin->m_Id, ed::PinKind::Input);
            ed::PinPivotAlignment(ImVec2(0.0f, 0.5f));
            ax::Widgets::Icon(iconSize,
                PinTypeToIconType(pin->m_Type),
                g_Blueprint.IsPinLinked(pin),
                PinTypeToIconColor(pin->m_Link ? pin->m_Link->m_Type : pin->m_Type));
            if (!pin->m_Name.empty())
            {
                ImGui::SameLine();
                ImGui::TextUnformatted(pin->m_Name.data(), pin->m_Name.data() + pin->m_Name.size());
            }
            if (!g_Blueprint.IsPinLinked(pin))
            {
                ImGui::SameLine();
                EditOrDrawPinValue(*pin);
            }
            ed::EndPin();
            layout.NextRow();
        }

        layout.SetColumnAlignment(1.0f);
        layout.NextColumn();

        for (auto& pin : node->GetOutputPins())
        {
            ImGui::Spacing(); // Add a bit of spacing to separate pins and make value not cramped
            ed::BeginPin(pin->m_Id, ed::PinKind::Output);
            ed::PinPivotAlignment(ImVec2(1.0f, 0.5f));
            if (!pin->m_Name.empty())
            {
                ImGui::TextUnformatted(pin->m_Name.data(), pin->m_Name.data() + pin->m_Name.size());
                ImGui::SameLine();
            }
            ax::Widgets::Icon(iconSize,
                PinTypeToIconType(pin->m_Type),
                g_Blueprint.IsPinLinked(pin),
                PinTypeToIconColor(pin->m_Type));
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

        //ed::Link(pin->m_Id, pin->m_Id, pin->m_Link->m_Id, PinTypeToIconColor(pin->m_Link->m_Type));
        ed::Link(pin->m_Link->m_Id, pin->m_Id, pin->m_Link->m_Id, PinTypeToIconColor(pin->m_Link->m_Type));
    }

    ed::End();


}