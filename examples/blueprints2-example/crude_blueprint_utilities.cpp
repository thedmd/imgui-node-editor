# include "crude_blueprint_utilities.h"
# define IMGUI_DEFINE_MATH_OPERATORS
# include <imgui_internal.h>

ImEx::IconType crude_blueprint_utilities::PinTypeToIconType(PinType pinType)
{
    switch (pinType)
    {
        case PinType::Any:      return ImEx::IconType::Diamond;
        case PinType::Flow:     return ImEx::IconType::Flow;
        case PinType::Bool:     return ImEx::IconType::Circle;
        case PinType::Int32:    return ImEx::IconType::Circle;
        case PinType::Float:    return ImEx::IconType::Circle;
        case PinType::String:   return ImEx::IconType::Circle;
    }

    return ImEx::IconType::Circle;
}

ImVec4 crude_blueprint_utilities::PinTypeToColor(PinType pinType)
{
    switch (pinType)
    {
        case PinType::Any:      return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        case PinType::Flow:     return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        case PinType::Bool:     return ImVec4(220 / 255.0f,  48 / 255.0f,  48 / 255.0f, 1.0f);
        case PinType::Int32:    return ImVec4( 68 / 255.0f, 201 / 255.0f, 156 / 255.0f, 1.0f);
        case PinType::Float:    return ImVec4(147 / 255.0f, 226 / 255.0f,  74 / 255.0f, 1.0f);
        case PinType::String:   return ImVec4(124 / 255.0f,  21 / 255.0f, 153 / 255.0f, 1.0f);
    }

    return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
}

bool crude_blueprint_utilities::DrawPinValue(const PinValue& value)
{
    switch (value.GetType())
    {
        case PinType::Any:
            return false;
        case PinType::Flow:
            return false;
        case PinType::Bool:
            if (value.As<bool>())
                ImGui::TextUnformatted("true");
            else
                ImGui::TextUnformatted("false");
            return true;
        case PinType::Int32:
            ImGui::Text("%d", value.As<int32_t>());
            return true;
        case PinType::Float:
            ImGui::Text("%g", value.As<float>());
            return true;
        case PinType::String:
            ImGui::Text("%s", value.As<string>().c_str());
            return true;
    }

    return false;
}

bool crude_blueprint_utilities::EditPinImmediateValue(Pin& pin)
{
    ImEx::ScopedItemWidth scopedItemWidth{120};

    auto pinValue = pin.GetValue();

    switch (pinValue.GetType())
    {
        case PinType::Any:
            return false;
        case PinType::Flow:
            return false;
        case PinType::Bool:
            pin.SetValue(!pinValue.As<bool>());
            return false;
        case PinType::Int32:
            {
                auto value = pinValue.As<int32_t>();
                if (ImGui::InputInt("##editor", &value, 1, 100, ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    pin.SetValue(value);
                    return false;
                }
            }
            return true;
        case PinType::Float:
            {
                auto value = pinValue.As<float>();
                if (ImGui::InputFloat("##editor", &value, 1, 100, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    pin.SetValue(value);
                    return false;
                }
            }
            return true;
        case PinType::String:
            {
                auto value = pinValue.As<string>();
                if (ImGui::InputText("##editor", (char*)value.data(), value.size() + 1, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackResize, [](ImGuiInputTextCallbackData* data) -> int
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
                }, &value))
                {
                    pin.SetValue(value);
                    return false;
                }
            }
            return true;
    }

    return false;
}

void crude_blueprint_utilities::EditOrDrawPinValue(Pin& pin)
{
    auto storage = ImGui::GetStateStorage();
    auto activePinId = storage->GetInt(ImGui::GetID("PinValueEditor_ActivePinId"), false);

    if (activePinId == pin.m_Id)
    {
        if (!EditPinImmediateValue(pin))
        {
            ax::NodeEditor::EnableShortcuts(true);
            activePinId = 0;
        }
    }
    else
    {
        // Draw pin value
        PinValueBackgroundRenderer bg;
        if (!DrawPinValue(pin.GetValue()))
        {
            bg.Discard();
            return;
        }

        // Draw invisible button over pin value which triggers an editor if clicked
        auto itemMin = ImGui::GetItemRectMin();
        auto itemMax = ImGui::GetItemRectMax();
        auto itemSize = itemMax - itemMin;
        itemSize.x = ImMax(itemSize.x, 1.0f);
        itemSize.y = ImMax(itemSize.y, 1.0f);

        ImGui::SetCursorScreenPos(itemMin);

        if (ImGui::InvisibleButton("###pin_value_editor", itemSize))
        {
            activePinId = pin.m_Id;
            ax::NodeEditor::EnableShortcuts(false);
        }
    }

    storage->SetInt(ImGui::GetID("PinValueEditor_ActivePinId"), activePinId);
}

crude_blueprint_utilities::PinValueBackgroundRenderer::PinValueBackgroundRenderer(const ImVec4 color, float alpha /*= 0.25f*/)
{
    m_DrawList = ImGui::GetWindowDrawList();
    m_Splitter.Split(m_DrawList, 2);
    m_Splitter.SetCurrentChannel(m_DrawList, 1);
    m_Color = color;
    m_Alpha = alpha;
}

crude_blueprint_utilities::PinValueBackgroundRenderer::PinValueBackgroundRenderer()
    : PinValueBackgroundRenderer(ImGui::GetStyleColorVec4(ImGuiCol_CheckMark))
{
}

crude_blueprint_utilities::PinValueBackgroundRenderer::~PinValueBackgroundRenderer()
{
    Commit();
}

void crude_blueprint_utilities::PinValueBackgroundRenderer::Commit()
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

    auto color = m_Color;
    color.w *= m_Alpha;
    m_DrawList->AddRectFilled(itemMin, itemMax,
        ImGui::GetColorU32(color), 4.0f);

    m_DrawList->AddRect(itemMin, itemMax,
        ImGui::GetColorU32(ImGuiCol_Text, m_Alpha), 4.0f);

    m_Splitter.Merge(m_DrawList);
}

void crude_blueprint_utilities::PinValueBackgroundRenderer::Discard()
{
    if (m_Splitter._Current == 1)
        m_Splitter.Merge(m_DrawList);
}

crude_blueprint_utilities::DebugOverlay::DebugOverlay(Blueprint& blueprint)
    : m_Blueprint(&blueprint)
{
    m_Blueprint->SetContextMonitor(this);
}

crude_blueprint_utilities::DebugOverlay::~DebugOverlay()
{
    m_Blueprint->SetContextMonitor(nullptr);
}

void crude_blueprint_utilities::DebugOverlay::Begin()
{
    m_CurrentNode = m_Blueprint->CurrentNode();
    m_NextNode = m_Blueprint->NextNode();
    m_CurrentFlowPin = m_Blueprint->CurrentFlowPin();

    if (nullptr == m_CurrentNode)
        return;

    // Use splitter to draw pin values on top of nodes
    m_DrawList = ImGui::GetWindowDrawList();
    m_Splitter.Split(m_DrawList, 2);
}

void crude_blueprint_utilities::DebugOverlay::End()
{
    if (nullptr == m_CurrentNode)
        return;

    m_Splitter.Merge(m_DrawList);
}

void crude_blueprint_utilities::DebugOverlay::DrawNode(const Node& node)
{
    if (nullptr == m_CurrentNode)
        return;

    auto isCurrentNode = (m_CurrentNode->m_Id == node.m_Id);
    auto isNextNode = m_NextNode ? (m_NextNode->m_Id == node.m_Id) : false;
    if (!isCurrentNode && !isNextNode)
        return;

    // Draw to layer over nodes
    m_Splitter.SetCurrentChannel(m_DrawList, 1);

    // Save current layout to avoid conflicts with node measurements
    ImEx::ScopedSuspendLayout suspendLayout;

    // Put cursor on the left side of the Pin
    auto itemRectMin = ImGui::GetItemRectMin();
    auto itemRectMax = ImGui::GetItemRectMax();

    // itemRectMin -= ImVec2(4.0f, 4.0f);
    // itemRectMax += ImVec2(4.0f, 4.0f);
    auto pivot = ImVec2((itemRectMin.x + itemRectMax.x) * 0.5f, itemRectMin.y);
    auto markerMin = pivot - ImVec2(40.0f, 35.0f);
    auto markerMax = pivot + ImVec2(40.0f, 0.0f);

    auto color = ImGui::GetStyleColorVec4(isCurrentNode ? ImGuiCol_PlotHistogram : ImGuiCol_NavHighlight);

    ImEx::DrawIcon(m_DrawList, markerMin, markerMax, ImEx::IconType::FlowDown, true, ImColor(color), 0);

    // Switch back to normal layer
    m_Splitter.SetCurrentChannel(m_DrawList, 0);
}

void crude_blueprint_utilities::DebugOverlay::DrawInputPin(const Pin& pin)
{
    auto flowPinValue = m_Blueprint->GetContext().GetPinValue(m_CurrentFlowPin);
    auto flowPin = flowPinValue.GetType() == PinType::Flow ? flowPinValue.As<FlowPin*>() : nullptr;

    const auto isCurrentFlowPin = flowPin && flowPin->m_Id == pin.m_Id;

    if (nullptr == m_CurrentNode || (!pin.m_Link && !isCurrentFlowPin))
        return;

    // Draw to layer over nodes
    m_Splitter.SetCurrentChannel(m_DrawList, 1);

    // Save current layout to avoid conflicts with node measurements
    ImEx::ScopedSuspendLayout suspendLayout;

    // Put cursor on the left side of the Pin
    auto itemRectMin = ImGui::GetItemRectMin();
    auto itemRectMax = ImGui::GetItemRectMax();
    ImGui::SetCursorScreenPos(ImVec2(itemRectMin.x, itemRectMin.y)
        - ImVec2(1.75f * ImGui::GetStyle().ItemSpacing.x, 0.0f));

    // Remember current vertex, later we will patch generated mesh so drawn value
    // will be aligned to the right side of the pin.
    auto vertexStartIdx = m_DrawList->_VtxCurrentOffset + m_DrawList->_VtxCurrentIdx;

    auto isCurrentNode = (m_CurrentNode == pin.m_Node);
    auto color = ImGui::GetStyleColorVec4(isCurrentNode ? ImGuiCol_PlotHistogram : ImGuiCol_NavHighlight);

    // Actual drawing
    PinValueBackgroundRenderer bg(color, 0.5f);
    if (!DrawPinValue(m_Blueprint->GetContext().GetPinValue(pin)))
    {
        if (isCurrentFlowPin)
        {
            auto iconSize = ImVec2(ImGui::GetFontSize(), ImGui::GetFontSize());
            ImEx::Icon(iconSize, ImEx::IconType::Flow, true, color);
        }
        else
            bg.Discard();
    }
    bg.Commit();

    // Move generated mesh to the left
    auto itemWidth = ImGui::GetItemRectSize().x;
    auto vertexEndIdx = m_DrawList->_VtxCurrentOffset + m_DrawList->_VtxCurrentIdx;
    for (auto vertexIdx = vertexStartIdx; vertexIdx < vertexEndIdx; ++vertexIdx)
        m_DrawList->VtxBuffer[vertexIdx].pos.x -= itemWidth;

    // Switch back to normal layer
    m_Splitter.SetCurrentChannel(m_DrawList, 0);
}

void crude_blueprint_utilities::DebugOverlay::DrawOutputPin(const Pin& pin)
{
    if (nullptr == m_CurrentNode)
        return;

    // Draw to layer over nodes
    m_Splitter.SetCurrentChannel(m_DrawList, 1);

    ImEx::ScopedSuspendLayout suspendLayout;

    // Put cursor on the right side of the Pin
    auto itemRectMin = ImGui::GetItemRectMin();
    auto itemRectMax = ImGui::GetItemRectMax();
    ImGui::SetCursorScreenPos(ImVec2(itemRectMax.x, itemRectMin.y)
        + ImVec2(1.75f * ImGui::GetStyle().ItemSpacing.x, 0.0f));

    auto isCurrentNode = m_CurrentNode == pin.m_Node;
    auto color = ImGui::GetStyleColorVec4(isCurrentNode ? ImGuiCol_PlotHistogram : ImGuiCol_NavHighlight);

    // Actual drawing
    PinValueBackgroundRenderer bg(color, 0.5f);
    if (!DrawPinValue(m_Blueprint->GetContext().GetPinValue(pin)))
    {
        if (m_CurrentFlowPin.m_Id == pin.m_Id)
        {
            auto iconSize = ImVec2(ImGui::GetFontSize(), ImGui::GetFontSize());
            ImEx::Icon(iconSize, ImEx::IconType::Flow, true, color);
        }
        else
            bg.Discard();
    }
    bg.Commit();

    // Switch back to normal layer
    m_Splitter.SetCurrentChannel(m_DrawList, 0);
}

void crude_blueprint_utilities::DebugOverlay::OnEvaluatePin(const Context& context, const Pin& pin)
{
    auto nodeEditor = ax::NodeEditor::GetCurrentEditor();
    if (!nodeEditor)
        return;

    //if (pin.m_Link)
        ax::NodeEditor::Flow(pin.m_Id);
}




crude_blueprint_utilities::ItemBuilder::ItemBuilder()
    : m_InCreate(ax::NodeEditor::BeginCreate(ImGui::GetStyleColorVec4(ImGuiCol_NavHighlight)))
{

}

crude_blueprint_utilities::ItemBuilder::~ItemBuilder()
{
    ax::NodeEditor::EndCreate();
}

crude_blueprint_utilities::ItemBuilder::operator bool() const
{
    return m_InCreate;
}

crude_blueprint_utilities::ItemBuilder::NodeBuilder* crude_blueprint_utilities::ItemBuilder::QueryNewNode()
{
    if (m_InCreate && ax::NodeEditor::QueryNewNode(&m_NodeBuilder.m_PinId))
        return &m_NodeBuilder;
    else
        return nullptr;
}

crude_blueprint_utilities::ItemBuilder::LinkBuilder* crude_blueprint_utilities::ItemBuilder::QueryNewLink()
{
    if (m_InCreate && ax::NodeEditor::QueryNewLink(&m_LinkBuilder.m_StartPinId, &m_LinkBuilder.m_EndPinId))
        return &m_LinkBuilder;
    else
        return nullptr;
}

bool crude_blueprint_utilities::ItemBuilder::LinkBuilder::Accept()
{
    return ax::NodeEditor::AcceptNewItem(ImVec4(0.34f, 1.0f, 0.34f, 1.0f), 3.0f);
}

void crude_blueprint_utilities::ItemBuilder::LinkBuilder::Reject()
{
    ax::NodeEditor::RejectNewItem(ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
}

bool crude_blueprint_utilities::ItemBuilder::NodeBuilder::Accept()
{
    return ax::NodeEditor::AcceptNewItem(ImVec4(0.34f, 1.0f, 0.34f, 1.0f), 3.0f);
}

void crude_blueprint_utilities::ItemBuilder::NodeBuilder::Reject()
{
    ax::NodeEditor::RejectNewItem(ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
}




crude_blueprint_utilities::ItemDeleter::ItemDeleter()
    : m_InDelete(ax::NodeEditor::BeginDelete())
{

}

crude_blueprint_utilities::ItemDeleter::~ItemDeleter()
{
    ax::NodeEditor::EndDelete();
}

crude_blueprint_utilities::ItemDeleter::operator bool() const
{
    return m_InDelete;
}

crude_blueprint_utilities::ItemDeleter::NodeDeleter* crude_blueprint_utilities::ItemDeleter::QueryDeletedNode()
{
    if (m_InDelete && ax::NodeEditor::QueryDeletedNode(&m_NodeDeleter.m_NodeId))
        return &m_NodeDeleter;
    else
        return nullptr;
}

crude_blueprint_utilities::ItemDeleter::LinkDeleter* crude_blueprint_utilities::ItemDeleter::QueryDeleteLink()
{
    if (m_InDelete && ax::NodeEditor::QueryDeletedLink(&m_LinkDeleter.m_LinkId, &m_LinkDeleter.m_StartPinId, &m_LinkDeleter.m_EndPinId))
        return &m_LinkDeleter;
    else
        return nullptr;
}

bool crude_blueprint_utilities::ItemDeleter::LinkDeleter::Accept()
{
    return ax::NodeEditor::AcceptDeletedItem();
}

void crude_blueprint_utilities::ItemDeleter::LinkDeleter::Reject()
{
    ax::NodeEditor::RejectDeletedItem();
}

bool crude_blueprint_utilities::ItemDeleter::NodeDeleter::Accept(bool deleteLinks /*= true*/)
{
    return ax::NodeEditor::AcceptDeletedItem(deleteLinks);
}

void crude_blueprint_utilities::ItemDeleter::NodeDeleter::Reject()
{
    ax::NodeEditor::RejectDeletedItem();
}




void crude_blueprint_utilities::CreateNodeDialog::Open(Pin* fromPin)
{
    auto storage = ImGui::GetStateStorage();
    storage->SetVoidPtr(ImGui::GetID("##create_node_pin"), fromPin);
    ImGui::OpenPopup("##create_node");
}

void crude_blueprint_utilities::CreateNodeDialog::Show(Blueprint& blueprint)
{
    if (!ImGui::IsPopupOpen("##create_node"))
        return;

    auto storage = ImGui::GetStateStorage();
    auto fromPin = reinterpret_cast<Pin*>(storage->GetVoidPtr(ImGui::GetID("##create_node_pin")));

    if (!ImGui::BeginPopup("##create_node"))
        return;

    auto popupPosition = ImGui::GetMousePosOnOpeningCurrentPopup();

    auto nodeRegistry = blueprint.GetNodeRegistry();

    for (auto nodeTypeInfo : nodeRegistry->GetTypes())
    {
        bool selected = false;
        if (ImGui::Selectable(nodeTypeInfo->m_DisplayName.to_string().c_str(), &selected))
        {
            auto node = blueprint.CreateNode(nodeTypeInfo->m_Id);

            auto nodePosition = ax::NodeEditor::ScreenToCanvas(popupPosition);

            ax::NodeEditor::SetNodePosition(node->m_Id, nodePosition);

            ax::NodeEditor::SelectNode(node->m_Id);

            if (fromPin)
                CreateLinkToFirstMatchingPin(*node, *fromPin);
        }
    }

    ImGui::EndPopup();
}

bool crude_blueprint_utilities::CreateNodeDialog::CreateLinkToFirstMatchingPin(Node& node, Pin& fromPin)
{
    for (auto nodePin : node.GetInputPins())
        if (nodePin->LinkTo(fromPin) || fromPin.LinkTo(*nodePin))
            return true;

    for (auto nodePin : node.GetOutputPins())
        if (nodePin->LinkTo(fromPin) || fromPin.LinkTo(*nodePin))
            return true;

    return false;
}