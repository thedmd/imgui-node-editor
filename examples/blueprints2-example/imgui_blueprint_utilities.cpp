# include "imgui_blueprint_utilities.h"
# define IMGUI_DEFINE_MATH_OPERATORS
# include <imgui_internal.h>
# include <imgui_node_editor.h>

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

ImVec4 crude_blueprint_utilities::PinTypeToIconColor(PinType pinType)
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
    switch (static_cast<PinType>(value.index()))
    {
        case PinType::Any:
            return false;
        case PinType::Flow:
            return false;
        case PinType::Bool:
            if (get<bool>(value))
                ImGui::TextUnformatted("true");
            else
                ImGui::TextUnformatted("false");
            return true;
        case PinType::Int32:
            ImGui::Text("%d", get<int32_t>(value));
            return true;
        case PinType::Float:
            ImGui::Text("%f", get<float>(value));
            return true;
        case PinType::String:
            ImGui::Text("%s", get<string>(value).c_str());
            return true;
    }

    return false;
}

//bool crude_blueprint_utilities::DrawPinValue(const Pin& pin)
//{
//    return DrawPinValue(pin.GetValue());
//}

//bool crude_blueprint_utilities::DrawPinImmediateValue(const Pin& pin)
//{
//    return DrawPinValue(pin.GetImmediateValue());
//}

bool crude_blueprint_utilities::EditPinImmediateValue(Pin& pin)
{
    ImEx::ScopedItemWidth scopedItemWidth{120};

    switch (pin.m_Type)
    {
        case PinType::Any:
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
    m_Blueprint->GetContext().SetMonitor(this);
}

crude_blueprint_utilities::DebugOverlay::~DebugOverlay()
{
    m_Blueprint->GetContext().SetMonitor(nullptr);
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
    auto flowPin = get_if<FlowPin*>(&flowPinValue);

    const auto isCurrentFlowPin = flowPin && (*flowPin) && (*flowPin)->m_Id == pin.m_Id;

    if (nullptr == m_CurrentNode || (/*!pin.m_Link &&*/ !isCurrentFlowPin))
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

    if (pin.m_Link)
        ax::NodeEditor::Flow(pin.m_Link->m_Id);
}