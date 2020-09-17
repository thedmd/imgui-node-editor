# include "crude_blueprint_utilities.h"
# define IMGUI_DEFINE_MATH_OPERATORS
# include <imgui_internal.h>
# include <algorithm>
# include <time.h>

ImEx::IconType crude_blueprint_utilities::PinTypeToIconType(PinType pinType)
{
    switch (pinType)
    {
        case PinType::Void:     return ImEx::IconType::Circle;
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

bool crude_blueprint_utilities::DrawPinValue(const PinValue& value)
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

bool crude_blueprint_utilities::EditPinValue(Pin& pin)
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

void crude_blueprint_utilities::DrawPinValueWithEditor(Pin& pin)
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



const crude_blueprint_utilities::vector<crude_blueprint_utilities::Node*> crude_blueprint_utilities::GetSelectedNodes(Blueprint& blueprint)
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

const crude_blueprint_utilities::vector<crude_blueprint_utilities::Pin*> crude_blueprint_utilities::GetSelectedLinks(Blueprint& blueprint)
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




void crude_blueprint_utilities::OverlayLogger::Log(LogLevel level, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    ImGuiTextBuffer textBuffer;
    textBuffer.appendfv(format, args);
    va_end(args);

    auto timeStamp = time(nullptr);

    char formattedTimeStamp[100] = {};
    strftime(formattedTimeStamp, sizeof(formattedTimeStamp), "%H:%M:%S", localtime(&timeStamp));

    char levelSymbol = ' ';
    switch (level)
    {
        case LogLevel::Verbose: levelSymbol = 'V'; break;
        case LogLevel::Info:    levelSymbol = 'I'; break;
        case LogLevel::Warning: levelSymbol = 'W'; break;
        case LogLevel::Error:   levelSymbol = 'E'; break;
    }

    auto formattedMessage = string("[") + formattedTimeStamp + "] [" + levelSymbol + "] " + textBuffer.c_str();

    auto entry = Entry{ level, timeStamp, std::move(formattedMessage) };
    entry.m_ColorRanges = ParseMessage(entry.m_Buffer.c_str());
    m_Entries.push_back(std::move(entry));
}

void crude_blueprint_utilities::OverlayLogger::Update(float dt)
{
    if (m_HoldTimer)
        return;

    for (auto& entry : m_Entries)
    {
        if (entry.m_IsPinned)
            continue;

        entry.m_Timer += dt;
    }

    auto lastIt = std::remove_if(m_Entries.begin(), m_Entries.end(), [this](const Entry& entry)
    {
        return entry.m_Timer >= m_MessageLifeDuration;
    });

    m_Entries.erase(lastIt, m_Entries.end());
}

void crude_blueprint_utilities::OverlayLogger::Draw(const ImVec2& a, const ImVec2& b)
{
    const auto canvasMin = a + ImVec2(m_Padding, m_Padding);
    const auto canvasMax = b - ImVec2(m_Padding, m_Padding);
    const auto canvasSize = canvasMax - canvasMin;

    auto drawList = ImGui::GetWindowDrawList();
    //drawList->AddRect(a, b, IM_COL32(255, 255, 0, 255));

    m_HoldTimer = false;

    if (canvasSize.x < 1.0f || canvasSize.y < 1.0f)
        return;

    const auto clipRect = ImVec4(canvasMin.x, canvasMin.y, canvasMax.x, canvasMax.y);

    //drawList->AddRect(canvasMin, canvasMax, IM_COL32(255, 0, 0, 255));

    float outlineSize = m_OutlineSize;

    int entryIndex = 0;
    auto cursor = ImVec2(canvasMin.x, canvasMax.y);
    for (auto it = m_Entries.rbegin(), itEnd = m_Entries.rend(); it != itEnd; ++it, ++entryIndex)
    {
        auto& entry = *it;
        auto message = entry.m_Buffer.data();
        auto messageEnd = entry.m_Buffer.data() + entry.m_Buffer.size();

        auto outlineOffset = ImCeil(outlineSize);

        auto itemSize = ImGui::CalcTextSize(message, messageEnd, false, canvasSize.x);
        itemSize.x += outlineOffset * 2.0f;
        itemSize.y += outlineOffset * 2.0f;

        cursor.y -= itemSize.y;

        auto isHovered = ImGui::IsMouseHoveringRect(cursor, cursor + itemSize, true);

        auto textPosition = cursor + ImVec2(outlineOffset, outlineOffset);

        auto alpha = 1.0f;
        if (entry.m_Timer > m_MessagePresentationDuration && m_MessageFadeOutDuration > 0.0f)
        {
            alpha = ImMin(1.0f, (entry.m_Timer - m_MessagePresentationDuration) / m_MessageFadeOutDuration);
            alpha = 1.0f - alpha * alpha;
        }


        auto textColor    = ImColor(1.0f, 1.0f, 1.0f, alpha);
        auto outlineColor = ImColor(0.0f, 0.0f, 0.0f, alpha / 2.0f);

        switch (entry.m_Level)
        {
            case LogLevel::Verbose: textColor = m_LogVerboseColor; break;
            case LogLevel::Info:    textColor = m_LogInfoColor;    break;
            case LogLevel::Warning: textColor = m_LogWarningColor; break;
            case LogLevel::Error:   textColor = m_LogErrorColor;   break;
        }

        textColor.Value.w = alpha;

        if (isHovered || entry.m_IsPinned)
            drawList->AddRectFilled(cursor, cursor + itemSize, isHovered ? m_HighlightFill : m_PinFill);

        if (outlineSize > 0.0f)
        {
            // Shadow `outlineSize`. AddText() does not support subpixel rendering,
            // to avoid artifact apply rounded up offset.
            float outlineSize = outlineOffset;

            drawList->AddText(nullptr, 0.0f, textPosition + ImVec2(+outlineSize, +outlineSize), outlineColor, message, messageEnd, canvasSize.x, &clipRect);
            drawList->AddText(nullptr, 0.0f, textPosition + ImVec2(+outlineSize, -outlineSize), outlineColor, message, messageEnd, canvasSize.x, &clipRect);
            drawList->AddText(nullptr, 0.0f, textPosition + ImVec2(-outlineSize, -outlineSize), outlineColor, message, messageEnd, canvasSize.x, &clipRect);
            drawList->AddText(nullptr, 0.0f, textPosition + ImVec2(-outlineSize, +outlineSize), outlineColor, message, messageEnd, canvasSize.x, &clipRect);
            drawList->AddText(nullptr, 0.0f, textPosition + ImVec2(+outlineSize, 0.0f), outlineColor, message, messageEnd, canvasSize.x, &clipRect);
            drawList->AddText(nullptr, 0.0f, textPosition + ImVec2(-outlineSize, 0.0f), outlineColor, message, messageEnd, canvasSize.x, &clipRect);
            drawList->AddText(nullptr, 0.0f, textPosition + ImVec2(0.0f, +outlineSize), outlineColor, message, messageEnd, canvasSize.x, &clipRect);
            drawList->AddText(nullptr, 0.0f, textPosition + ImVec2(0.0f, -outlineSize), outlineColor, message, messageEnd, canvasSize.x, &clipRect);
        }

        for (int i = 0; i < 2; ++i)
        {
            auto stringStart = drawList->VtxBuffer.Size;
            drawList->AddText(nullptr, 0.0f, textPosition, textColor, message, messageEnd, canvasSize.x, &clipRect);
            for (auto& range : entry.m_ColorRanges)
               TintVertices(drawList, stringStart, range.m_Color, alpha, range.m_Start, range.m_Size);
        }

        if (isHovered)
            m_HoldTimer = true;

        if (isHovered || entry.m_IsPinned)
            drawList->AddRect(cursor, cursor + itemSize, isHovered ? m_HighlightBorder : m_PinBorder);

        if (entry.m_IsPinned || isHovered)
        {
            ImGui::SetItemAllowOverlap();
            ImGui::SetCursorScreenPos(cursor);
            ImGui::PushID(entryIndex);
            if (ImGui::InvisibleButton("##pin", itemSize))
            {
                entry.m_IsPinned = !entry.m_IsPinned;
                entry.m_Timer    = 0.0f;
            }
            ImGui::PopID();

            if (ImGui::IsItemHovered(ImGuiHoveredFlags_None))
                ImGui::SetTooltip("Click to %s message", entry.m_IsPinned ? "pin" : "unpin");
        }

        if (cursor.y < canvasMin.y)
            break;
    }
}

void crude_blueprint_utilities::OverlayLogger::TintVertices(ImDrawList* drawList, int firstVertexIndex, ImColor color, float alpha, int rangeStart, int rangeSize)
{
    const int c_VertexPerCharacter = 4;

    color.Value.w = alpha;
    auto packedColor = static_cast<ImU32>(color);

    auto vertexData    = drawList->VtxBuffer.Data;
    auto vertexDataEnd = vertexData + drawList->VtxBuffer.Size;

    auto data    = vertexData + firstVertexIndex + rangeStart * c_VertexPerCharacter;
    auto dataEnd = data + rangeSize * c_VertexPerCharacter;

    if (data >= vertexDataEnd)
        return;

    if (dataEnd >= vertexDataEnd)
        dataEnd = vertexDataEnd;

    while (data < dataEnd)
    {
        data->col = packedColor;
        ++data;
    }
}

crude_blueprint::vector<crude_blueprint_utilities::OverlayLogger::Range> crude_blueprint_utilities::OverlayLogger::ParseMessage(string_view message) const
{
    vector<Range> result;

    const auto c_HeaderSize = 15;

    // Color timestamp '[00:00:00] [x] '
    result.push_back({  0, 10, m_LogSymbolColor });
    result.push_back({  1,  2, m_LogTimeColor });
    result.push_back({  4,  2, m_LogTimeColor });
    result.push_back({  7,  2, m_LogTimeColor });
    result.push_back({ 11,  1, m_LogSymbolColor });
    result.push_back({ 13,  1, m_LogSymbolColor });

    auto find_first_of = [message](const char* s, size_t offset = 0) -> size_t
    {
        auto result = message.find_first_of(s, offset);
        if (result != string_view::npos)
            return result;

        if (offset == message.size())
            return message.size();

        if (offset > message.size())
            return string_view::npos;

        if (!strchr(s, message.back()))
            return string_view::npos;

        return message.size();
    };

    auto find_first_not_of = [message](const char* s, size_t offset = 0) -> size_t
    {
        auto result = message.find_first_not_of(s, offset);
        if (result != string_view::npos)
            return result;

        if (offset == message.size())
            return message.size();

        if (offset > message.size())
            return string_view::npos;

        if (!strchr(s, message.back()))
            return string_view::npos;

        return message.size();
    };

    auto isWhitespace = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };

    auto symbols = "[]():;!*+-=<>{}\"'";

    size_t searchStart = c_HeaderSize;
    while (true)
    {
        auto index = find_first_of(symbols, searchStart);
        if (index == string_view::npos)
            break;

        result.push_back({ static_cast<int>(index), 1, m_LogSymbolColor });

        searchStart = index + 1;
    }

    searchStart = c_HeaderSize;
    while (true)
    {
        auto tagStart = find_first_of("[", searchStart);
        if (tagStart == string_view::npos)
            break;

        auto tagEnd = find_first_of("]", tagStart + 1);
        if (tagEnd == string_view::npos)
            break;

        result.push_back({ static_cast<int>(tagStart) + 1, static_cast<int>(tagEnd - tagStart) - 1, m_LogTagColor });

        searchStart = tagEnd + 2;
    }

    searchStart = c_HeaderSize;
    while (true)
    {
        auto stringStart = find_first_of("\"'", searchStart);
        if (stringStart == string_view::npos)
            break;

        char search[2] = " ";
        search[0] = message[stringStart];

        auto stringEnd = find_first_of(search, stringStart + 1);
        if (stringEnd == string_view::npos)
            break;

        result.push_back({ static_cast<int>(stringStart), static_cast<int>(stringEnd - stringStart) + 1, m_LogStringColor });

        searchStart = stringEnd + 2;
    }

    searchStart = c_HeaderSize;
    while (true)
    {
        auto numberStart = find_first_of("0123456789.", searchStart);
        if (numberStart == string_view::npos)
            break;

        auto numberEnd = find_first_not_of("0123456789.", numberStart + 1);
        if (numberEnd == string_view::npos)
            break;

        if (numberStart > 0 && !isWhitespace(message[numberStart - 1]) && !strchr(symbols, message[numberStart - 1]))
        {
            searchStart = numberEnd + 1;
            continue;
        }

        auto dotCount = std::count_if(message.begin() + numberStart, message.begin() + numberEnd, [](auto c) { return c == '.'; });
        if (dotCount > 1 || (dotCount == 1 && message[numberEnd - 1] == '.'))
        {
            searchStart = numberEnd + 1;
            continue;
        }

        result.push_back({ static_cast<int>(numberStart), static_cast<int>(numberEnd - numberStart), m_LogNumberColor });

        searchStart = numberEnd + 1;
    }

    for (auto& range : result)
    {
        auto spacesBefore = std::count_if(message.begin(),                 message.begin() + range.m_Start,                isWhitespace);
        auto spacesInside = std::count_if(message.begin() + range.m_Start, message.begin() + range.m_Start + range.m_Size, isWhitespace);
        range.m_Start -= static_cast<int>(spacesBefore);
        range.m_Size  -= static_cast<int>(spacesInside);
    }

    result.erase(std::remove_if(result.begin(), result.end(), [](const auto& range)
    {
        return range.m_Size <= 0;
    }), result.end());

    return result;
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

    m_SortedNodes.clear();
}

bool crude_blueprint_utilities::CreateNodeDialog::Show(Blueprint& blueprint)
{
    if (!ImGui::IsPopupOpen("##create_node"))
        return false;

    auto storage = ImGui::GetStateStorage();
    auto fromPin = reinterpret_cast<Pin*>(storage->GetVoidPtr(ImGui::GetID("##create_node_pin")));

    if (!ImGui::BeginPopup("##create_node"))
        return false;

    auto popupPosition = ImGui::GetMousePosOnOpeningCurrentPopup();

    auto nodeRegistry = blueprint.GetNodeRegistry();

    if (m_SortedNodes.empty())
    {
        auto types = nodeRegistry->GetTypes();

        m_SortedNodes.assign(types.begin(), types.end());
        std::sort(m_SortedNodes.begin(), m_SortedNodes.end(), [](const auto& lhs, const auto& rhs)
        {
            return std::lexicographical_compare(
                lhs->m_DisplayName.begin(), lhs->m_DisplayName.end(),
                rhs->m_DisplayName.begin(), rhs->m_DisplayName.end());
        });
    }

    bool wasNodeCreated = false;

    for (auto nodeTypeInfo : m_SortedNodes)
    {
        bool selected = false;
        if (ImGui::Selectable(nodeTypeInfo->m_DisplayName.to_string().c_str(), &selected))
        {
            auto node = blueprint.CreateNode(nodeTypeInfo->m_Id);

            auto nodePosition = ax::NodeEditor::ScreenToCanvas(popupPosition);

            ax::NodeEditor::SetNodePosition(node->m_Id, nodePosition);

            ax::NodeEditor::SelectNode(node->m_Id);

            m_CreatedNode = node;
            m_CreatedLinks.clear();

            if (fromPin)
                CreateLinkToFirstMatchingPin(*node, *fromPin);

            wasNodeCreated = true;
        }
    }

    ImGui::EndPopup();

    return wasNodeCreated;
}

bool crude_blueprint_utilities::CreateNodeDialog::CreateLinkToFirstMatchingPin(Node& node, Pin& fromPin)
{
    for (auto nodePin : node.GetInputPins())
    {
        if (nodePin->LinkTo(fromPin))
        {
            m_CreatedLinks.push_back(nodePin);
            return true;
        }

        if (fromPin.LinkTo(*nodePin))
        {
            m_CreatedLinks.push_back(&fromPin);
            return true;
        }
    }

    for (auto nodePin : node.GetOutputPins())
    {
        if (nodePin->LinkTo(fromPin))
        {
            m_CreatedLinks.push_back(nodePin);
            return true;
        }

        if (fromPin.LinkTo(*nodePin))
        {
            m_CreatedLinks.push_back(&fromPin);
            return true;
        }
    }

    return false;
}




void crude_blueprint_utilities::NodeContextMenu::Open(Node* node /*= nullptr*/)
{
    ImGui::GetStateStorage()->SetVoidPtr(ImGui::GetID("##node-context-menu-node"), node);
    ImGui::OpenPopup("##node-context-menu");
}

void crude_blueprint_utilities::NodeContextMenu::Show(Blueprint& blueprint)
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




void crude_blueprint_utilities::PinContextMenu::Open(Pin* pin /*= nullptr*/)
{
    ImGui::GetStateStorage()->SetVoidPtr(ImGui::GetID("##pin-context-menu-pin"), pin);
    ImGui::OpenPopup("##pin-context-menu");
}

void crude_blueprint_utilities::PinContextMenu::Show(Blueprint& blueprint)
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




void crude_blueprint_utilities::LinkContextMenu::Open(Pin* pin /*= nullptr*/)
{
    ImGui::GetStateStorage()->SetVoidPtr(ImGui::GetID("##link-context-menu-pin"), pin);
    ImGui::OpenPopup("##link-context-menu");
}

void crude_blueprint_utilities::LinkContextMenu::Show(Blueprint& blueprint)
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
