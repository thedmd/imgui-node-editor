# include "blueprint_editor_utilities.h"
# include "blueprint_editor_document.h"
# include "crude_logger.h"
# define IMGUI_DEFINE_MATH_OPERATORS
# include <imgui_internal.h>
# include "imgui_extras.h"
# include <algorithm>
# include <time.h>

blueprint_editor_utilities::IconType blueprint_editor_utilities::PinTypeToIconType(PinType pinType)
{
    switch (pinType)
    {
        case PinType::Void:     return IconType::Circle;
        case PinType::Any:      return IconType::Diamond;
        case PinType::Flow:     return IconType::Flow;
        case PinType::Bool:     return IconType::Circle;
        case PinType::Int32:    return IconType::Circle;
        case PinType::Float:    return IconType::Circle;
        case PinType::String:   return IconType::Circle;
    }

    return IconType::Circle;
}

ImVec4 blueprint_editor_utilities::PinTypeToColor(PinType pinType)
{
    switch (pinType)
    {
        case PinType::Void:     return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        case PinType::Any:      return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        case PinType::Flow:     return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        case PinType::Bool:     return ImVec4(220 / 255.0f,  48 / 255.0f,  48 / 255.0f, 1.0f);
        case PinType::Int32:    return ImVec4( 68 / 255.0f, 201 / 255.0f, 156 / 255.0f, 1.0f);
        case PinType::Float:    return ImVec4(147 / 255.0f, 226 / 255.0f,  74 / 255.0f, 1.0f);
        case PinType::String:   return ImVec4(124 / 255.0f,  21 / 255.0f, 153 / 255.0f, 1.0f);
    }

    return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
}

bool blueprint_editor_utilities::DrawPinValue(const PinValue& value)
{
    switch (value.GetType())
    {
        case PinType::Void:
            return false;
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

bool blueprint_editor_utilities::EditPinValue(Pin& pin)
{
    ImEx::ScopedItemWidth scopedItemWidth{120};

    auto pinValue = pin.GetValue();

    switch (pinValue.GetType())
    {
        case PinType::Void:
            return true;
        case PinType::Any:
            return true;
        case PinType::Flow:
            return true;
        case PinType::Bool:
            pin.SetValue(!pinValue.As<bool>());
            return true;
        case PinType::Int32:
            {
                auto value = pinValue.As<int32_t>();
                if (ImGui::InputInt("##editor", &value, 1, 100, ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    pin.SetValue(value);
                    return true;
                }
            }
            return false;
        case PinType::Float:
            {
                auto value = pinValue.As<float>();
                if (ImGui::InputFloat("##editor", &value, 1, 100, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    pin.SetValue(value);
                    return true;
                }
            }
            return false;
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
                    value.resize(strlen(value.c_str()));
                    pin.SetValue(value);
                    return true;
                }
            }
            return false;
    }

    return true;
}

void blueprint_editor_utilities::DrawPinValueWithEditor(Pin& pin)
{
    auto storage = ImGui::GetStateStorage();
    auto activePinId = storage->GetInt(ImGui::GetID("PinValueEditor_ActivePinId"), false);

    if (activePinId == pin.m_Id)
    {
        if (EditPinValue(pin))
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



const blueprint_editor_utilities::vector<blueprint_editor_utilities::Node*> blueprint_editor_utilities::GetSelectedNodes(Blueprint& blueprint)
{
    auto selectedObjects = ax::NodeEditor::GetSelectedNodes(nullptr, 0);

    vector<ax::NodeEditor::NodeId> nodeIds;
    nodeIds.resize(selectedObjects);
    nodeIds.resize(ax::NodeEditor::GetSelectedNodes(nodeIds.data(), selectedObjects));

    vector<Node*> result;
    result.reserve(nodeIds.size());
    for (auto nodeId : nodeIds)
    {
        auto node = blueprint.FindNode(static_cast<uint32_t>(nodeId.Get()));
        IM_ASSERT(node != nullptr);
        result.push_back(node);
    }

    return result;
}

const blueprint_editor_utilities::vector<blueprint_editor_utilities::Pin*> blueprint_editor_utilities::GetSelectedLinks(Blueprint& blueprint)
{
    auto selectedObjects = ax::NodeEditor::GetSelectedLinks(nullptr, 0);

    vector<ax::NodeEditor::LinkId> linkIds;
    linkIds.resize(selectedObjects);
    linkIds.resize(ax::NodeEditor::GetSelectedLinks(linkIds.data(), selectedObjects));

    vector<Pin*> result;
    result.reserve(linkIds.size());
    for (auto linkId : linkIds)
    {
        auto node = blueprint.FindPin(static_cast<uint32_t>(linkId.Get()));
        IM_ASSERT(node != nullptr);
        result.push_back(node);
    }

    return result;
}

blueprint_editor_utilities::PinValueBackgroundRenderer::PinValueBackgroundRenderer(const ImVec4 color, float alpha /*= 0.25f*/)
{
    m_DrawList = ImGui::GetWindowDrawList();
    m_Splitter.Split(m_DrawList, 2);
    m_Splitter.SetCurrentChannel(m_DrawList, 1);
    m_Color = color;
    m_Alpha = alpha;
}

blueprint_editor_utilities::PinValueBackgroundRenderer::PinValueBackgroundRenderer()
    : PinValueBackgroundRenderer(ImGui::GetStyleColorVec4(ImGuiCol_CheckMark))
{
}

blueprint_editor_utilities::PinValueBackgroundRenderer::~PinValueBackgroundRenderer()
{
    Commit();
}

void blueprint_editor_utilities::PinValueBackgroundRenderer::Commit()
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

void blueprint_editor_utilities::PinValueBackgroundRenderer::Discard()
{
    if (m_Splitter._Current == 1)
        m_Splitter.Merge(m_DrawList);
}

blueprint_editor_utilities::DebugOverlay::DebugOverlay(Blueprint& blueprint)
    : m_Blueprint(&blueprint)
{
    m_Blueprint->SetContextMonitor(this);
}

blueprint_editor_utilities::DebugOverlay::~DebugOverlay()
{
    m_Blueprint->SetContextMonitor(nullptr);
}

void blueprint_editor_utilities::DebugOverlay::Begin()
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

void blueprint_editor_utilities::DebugOverlay::End()
{
    if (nullptr == m_CurrentNode)
        return;

    m_Splitter.Merge(m_DrawList);
}

void blueprint_editor_utilities::DebugOverlay::DrawNode(const Node& node)
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

    DrawIcon(m_DrawList, markerMin, markerMax, IconType::FlowDown, true, ImColor(color), 0);

    // Switch back to normal layer
    m_Splitter.SetCurrentChannel(m_DrawList, 0);
}

void blueprint_editor_utilities::DebugOverlay::DrawInputPin(const Pin& pin)
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
            Icon(iconSize, IconType::Flow, true, color);
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

void blueprint_editor_utilities::DebugOverlay::DrawOutputPin(const Pin& pin)
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
            Icon(iconSize, IconType::Flow, true, color);
        }
        else
            bg.Discard();
    }
    bg.Commit();

    // Switch back to normal layer
    m_Splitter.SetCurrentChannel(m_DrawList, 0);
}

void blueprint_editor_utilities::DebugOverlay::OnEvaluatePin(const Context& context, const Pin& pin)
{
    auto nodeEditor = ax::NodeEditor::GetCurrentEditor();
    if (!nodeEditor)
        return;

    //if (pin.m_Link)
        ax::NodeEditor::Flow(pin.m_Id);
}




blueprint_editor_utilities::ItemBuilder::ItemBuilder()
    : m_InCreate(ax::NodeEditor::BeginCreate(ImGui::GetStyleColorVec4(ImGuiCol_NavHighlight)))
{

}

blueprint_editor_utilities::ItemBuilder::~ItemBuilder()
{
    ax::NodeEditor::EndCreate();
}

blueprint_editor_utilities::ItemBuilder::operator bool() const
{
    return m_InCreate;
}

blueprint_editor_utilities::ItemBuilder::NodeBuilder* blueprint_editor_utilities::ItemBuilder::QueryNewNode()
{
    if (m_InCreate && ax::NodeEditor::QueryNewNode(&m_NodeBuilder.m_PinId))
        return &m_NodeBuilder;
    else
        return nullptr;
}

blueprint_editor_utilities::ItemBuilder::LinkBuilder* blueprint_editor_utilities::ItemBuilder::QueryNewLink()
{
    if (m_InCreate && ax::NodeEditor::QueryNewLink(&m_LinkBuilder.m_StartPinId, &m_LinkBuilder.m_EndPinId))
        return &m_LinkBuilder;
    else
        return nullptr;
}

bool blueprint_editor_utilities::ItemBuilder::LinkBuilder::Accept()
{
    return ax::NodeEditor::AcceptNewItem(ImVec4(0.34f, 1.0f, 0.34f, 1.0f), 3.0f);
}

void blueprint_editor_utilities::ItemBuilder::LinkBuilder::Reject()
{
    ax::NodeEditor::RejectNewItem(ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
}

bool blueprint_editor_utilities::ItemBuilder::NodeBuilder::Accept()
{
    return ax::NodeEditor::AcceptNewItem(ImVec4(0.34f, 1.0f, 0.34f, 1.0f), 3.0f);
}

void blueprint_editor_utilities::ItemBuilder::NodeBuilder::Reject()
{
    ax::NodeEditor::RejectNewItem(ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
}




blueprint_editor_utilities::ItemDeleter::ItemDeleter()
    : m_InDelete(ax::NodeEditor::BeginDelete())
{

}

blueprint_editor_utilities::ItemDeleter::~ItemDeleter()
{
    ax::NodeEditor::EndDelete();
}

blueprint_editor_utilities::ItemDeleter::operator bool() const
{
    return m_InDelete;
}

blueprint_editor_utilities::ItemDeleter::NodeDeleter* blueprint_editor_utilities::ItemDeleter::QueryDeletedNode()
{
    if (m_InDelete && ax::NodeEditor::QueryDeletedNode(&m_NodeDeleter.m_NodeId))
        return &m_NodeDeleter;
    else
        return nullptr;
}

blueprint_editor_utilities::ItemDeleter::LinkDeleter* blueprint_editor_utilities::ItemDeleter::QueryDeleteLink()
{
    if (m_InDelete && ax::NodeEditor::QueryDeletedLink(&m_LinkDeleter.m_LinkId, &m_LinkDeleter.m_StartPinId, &m_LinkDeleter.m_EndPinId))
        return &m_LinkDeleter;
    else
        return nullptr;
}

bool blueprint_editor_utilities::ItemDeleter::LinkDeleter::Accept()
{
    return ax::NodeEditor::AcceptDeletedItem();
}

void blueprint_editor_utilities::ItemDeleter::LinkDeleter::Reject()
{
    ax::NodeEditor::RejectDeletedItem();
}

bool blueprint_editor_utilities::ItemDeleter::NodeDeleter::Accept(bool deleteLinks /*= true*/)
{
    return ax::NodeEditor::AcceptDeletedItem(deleteLinks);
}

void blueprint_editor_utilities::ItemDeleter::NodeDeleter::Reject()
{
    ax::NodeEditor::RejectDeletedItem();
}




void blueprint_editor_utilities::CreateNodeDialog::Open(Pin* fromPin)
{
    auto storage = ImGui::GetStateStorage();
    storage->SetVoidPtr(ImGui::GetID("##create_node_pin"), fromPin);
    ImGui::OpenPopup("##create_node");

    m_SortedNodes.clear();
}

void blueprint_editor_utilities::CreateNodeDialog::Show(Document& document)
{
    if (!ImGui::IsPopupOpen("##create_node"))
        return;

    auto storage = ImGui::GetStateStorage();
    auto fromPin = reinterpret_cast<Pin*>(storage->GetVoidPtr(ImGui::GetID("##create_node_pin")));

    if (!ImGui::BeginPopup("##create_node"))
        return;

    auto popupPosition = ImGui::GetMousePosOnOpeningCurrentPopup();

    if (m_SortedNodes.empty())
    {
        auto nodeRegistry = document.GetBlueprint().GetNodeRegistry();

        auto types = nodeRegistry->GetTypes();

        m_SortedNodes.assign(types.begin(), types.end());
        std::sort(m_SortedNodes.begin(), m_SortedNodes.end(), [](const auto& lhs, const auto& rhs)
        {
            return std::lexicographical_compare(
                lhs->m_DisplayName.begin(), lhs->m_DisplayName.end(),
                rhs->m_DisplayName.begin(), rhs->m_DisplayName.end());
        });
    }

    for (auto nodeTypeInfo : m_SortedNodes)
    {
        bool selected = false;
        if (ImGui::Selectable(nodeTypeInfo->m_DisplayName.to_string().c_str(), &selected))
        {
            auto transaction = document.BeginUndoTransaction("CreateNodeDialog");

            auto& blueprint = document.GetBlueprint();

            auto node = blueprint.CreateNode(nodeTypeInfo->m_Id);

            LOGV("[CreateNodeDailog] %" PRI_node " created", FMT_node(node));

            auto nodePosition = ax::NodeEditor::ScreenToCanvas(popupPosition);

            ax::NodeEditor::SetNodePosition(node->m_Id, nodePosition);

            ax::NodeEditor::SelectNode(node->m_Id);

            m_CreatedNode = node;
            m_CreatedLinks.clear();

            if (fromPin)
            {
                auto newLinks = CreateLinkToFirstMatchingPin(*node, *fromPin);

                for (auto startPin : newLinks)
                    LOGV("[CreateNodeDailog] %" PRI_pin "  linked with %" PRI_pin, FMT_pin(startPin), FMT_pin(startPin->GetLink()));
            }

            transaction->AddAction("%" PRI_node " created", FMT_node(node));
        }
    }

    ImGui::EndPopup();
}

blueprint_editor_utilities::vector<blueprint_editor_utilities::Pin*> blueprint_editor_utilities::CreateNodeDialog::CreateLinkToFirstMatchingPin(Node& node, Pin& fromPin)
{
    for (auto nodePin : node.GetInputPins())
    {
        if (nodePin->LinkTo(fromPin))
            return { nodePin };

        if (fromPin.LinkTo(*nodePin))
            return { &fromPin };
    }

    for (auto nodePin : node.GetOutputPins())
    {
        if (nodePin->LinkTo(fromPin))
            return { nodePin };

        if (fromPin.LinkTo(*nodePin))
            return { &fromPin };
    }

    return {};
}




void blueprint_editor_utilities::NodeContextMenu::Open(Node* node /*= nullptr*/)
{
    ImGui::GetStateStorage()->SetVoidPtr(ImGui::GetID("##node-context-menu-node"), node);
    ImGui::OpenPopup("##node-context-menu");
}

void blueprint_editor_utilities::NodeContextMenu::Show(Blueprint& blueprint)
{
    if (!ImGui::IsPopupOpen("##node-context-menu"))
        return;

    auto storage = ImGui::GetStateStorage();
    auto node = reinterpret_cast<Node*>(storage->GetVoidPtr(ImGui::GetID("##node-context-menu-node")));

    if (ImGui::BeginPopup("##node-context-menu"))
    {
        if (ImGui::MenuItem("Cut", nullptr, nullptr, false))
        {
            // #FIXME: Not implemented
        }

        if (ImGui::MenuItem("Copy", nullptr, nullptr, false))
        {
            // #FIXME: Not implemented
        }

        if (ImGui::MenuItem("Paste", nullptr, nullptr, false))
        {
            // #FIXME: Not implemented
        }

        if (ImGui::MenuItem("Duplicate", nullptr, nullptr, false))
        {
            // #FIXME: Not implemented
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Break Links", nullptr, nullptr, node != nullptr && ax::NodeEditor::HasAnyLinks(ax::NodeEditor::NodeId(node->m_Id))))
        {
            if (node && !ax::NodeEditor::IsNodeSelected(node->m_Id))
            {
                ax::NodeEditor::BreakLinks(ax::NodeEditor::NodeId(node->m_Id));
            }
            else
            {
                auto selectedNodes = GetSelectedNodes(blueprint);
                for (auto selectedNode : selectedNodes)
                    ax::NodeEditor::BreakLinks(ax::NodeEditor::NodeId(selectedNode->m_Id));
            }
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Delete"))
        {
            if (node && !ax::NodeEditor::IsNodeSelected(node->m_Id))
            {
                ax::NodeEditor::DeleteNode(node->m_Id);
            }
            else
            {
                auto selectedNodes = GetSelectedNodes(blueprint);
                for (auto selectedNode : selectedNodes)
                    ax::NodeEditor::DeleteNode(selectedNode->m_Id);
            }
        }

        ImGui::EndPopup();
    }
}




void blueprint_editor_utilities::PinContextMenu::Open(Pin* pin /*= nullptr*/)
{
    ImGui::GetStateStorage()->SetVoidPtr(ImGui::GetID("##pin-context-menu-pin"), pin);
    ImGui::OpenPopup("##pin-context-menu");
}

void blueprint_editor_utilities::PinContextMenu::Show(Blueprint& blueprint)
{
    if (!ImGui::IsPopupOpen("##pin-context-menu"))
        return;

    auto storage = ImGui::GetStateStorage();
    auto pin = reinterpret_cast<Pin*>(storage->GetVoidPtr(ImGui::GetID("##pin-context-menu-pin")));

    if (ImGui::BeginPopup("##pin-context-menu"))
    {
        if (ImGui::MenuItem("Break Links", nullptr, nullptr, pin != nullptr && ax::NodeEditor::HasAnyLinks(ax::NodeEditor::PinId(pin->m_Id))))
        {
            ax::NodeEditor::BreakLinks(ax::NodeEditor::PinId(pin->m_Id));
        }

        ImGui::EndPopup();
    }
}




void blueprint_editor_utilities::LinkContextMenu::Open(Pin* pin /*= nullptr*/)
{
    ImGui::GetStateStorage()->SetVoidPtr(ImGui::GetID("##link-context-menu-pin"), pin);
    ImGui::OpenPopup("##link-context-menu");
}

void blueprint_editor_utilities::LinkContextMenu::Show(Blueprint& blueprint)
{
    if (!ImGui::IsPopupOpen("##link-context-menu"))
        return;

    auto storage = ImGui::GetStateStorage();
    auto pin = reinterpret_cast<Pin*>(storage->GetVoidPtr(ImGui::GetID("##link-context-menu-pin")));

    if (ImGui::BeginPopup("##link-context-menu"))
    {
        if (ImGui::MenuItem("Break"))
        {
            if (pin && !ax::NodeEditor::IsLinkSelected(pin->m_Id))
            {
                ax::NodeEditor::DeleteLink(pin->m_Id);
            }
            else
            {
                auto selectedLinks = GetSelectedLinks(blueprint);
                for (auto selectedLink : selectedLinks)
                    ax::NodeEditor::DeleteLink(selectedLink->m_Id);
            }
        }

        ImGui::EndPopup();
    }
}
