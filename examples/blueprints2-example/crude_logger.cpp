# include "crude_logger.h"
# define IMGUI_DEFINE_MATH_OPERATORS
# include <imgui_internal.h>


crude_logger::OverlayLogger* crude_logger::OverlayLogger::s_Instance = nullptr;

void crude_logger::OverlayLogger::SetCurrent(OverlayLogger* instance)
{
    s_Instance = instance;
}

crude_logger::OverlayLogger* crude_logger::OverlayLogger::GetCurrent()
{
    return s_Instance;
}

void crude_logger::OverlayLogger::Log(LogLevel level, const char* format, ...)
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

    Entry entry;
    entry.m_Level     = level;
    entry.m_Timestamp = timeStamp;
    entry.m_Text      = std::move(formattedMessage);

    // Parse log message to get ranges for text coloring
    entry.m_ColorRanges = ParseMessage(entry.m_Level, entry.m_Text.c_str());

    m_Entries.push_back(std::move(entry));
}

void crude_logger::OverlayLogger::Update(float dt)
{
    if (m_HoldTimer)
        return;

    dt = ImMin(dt, 1.0f); // clamp dt, to prevent logs from suddenly disappearing while returning from debugger

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

void crude_logger::OverlayLogger::Draw(const ImVec2& a, const ImVec2& b)
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

    // Draw messages from the last to first
    int entryIndex = 0;
    auto cursor = ImVec2(canvasMin.x, canvasMax.y);
    for (auto it = m_Entries.rbegin(), itEnd = m_Entries.rend(); it != itEnd; ++it, ++entryIndex)
    {
        auto& entry = *it;
        auto message = entry.m_Text.data();
        auto messageEnd = entry.m_Text.data() + entry.m_Text.size();

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
            alpha = 1.0f - alpha * alpha; // Make fade out non-linear
        }

        auto textColor    = m_LogTextColor;
        auto outlineColor = m_LogOutlineColor;

           textColor.Value.w = alpha;
        outlineColor.Value.w = alpha / 2.0f;

        if (isHovered || entry.m_IsPinned)
            drawList->AddRectFilled(cursor, cursor + itemSize, isHovered ? m_HighlightFill : m_PinFill);

        if (outlineSize > 0.0f)
        {
            // Shadow `outlineSize`. AddText() does not support subpixel rendering,
            // to avoid artifact apply rounded up offset.
            float outlineSize = outlineOffset;

            // Poor man outline, just stack same text around place when normal text would be
            drawList->AddText(nullptr, 0.0f, textPosition + ImVec2(+outlineSize, +outlineSize), outlineColor, message, messageEnd, canvasSize.x, &clipRect);
            drawList->AddText(nullptr, 0.0f, textPosition + ImVec2(+outlineSize, -outlineSize), outlineColor, message, messageEnd, canvasSize.x, &clipRect);
            drawList->AddText(nullptr, 0.0f, textPosition + ImVec2(-outlineSize, -outlineSize), outlineColor, message, messageEnd, canvasSize.x, &clipRect);
            drawList->AddText(nullptr, 0.0f, textPosition + ImVec2(-outlineSize, +outlineSize), outlineColor, message, messageEnd, canvasSize.x, &clipRect);
            drawList->AddText(nullptr, 0.0f, textPosition + ImVec2(+outlineSize, 0.0f), outlineColor, message, messageEnd, canvasSize.x, &clipRect);
            drawList->AddText(nullptr, 0.0f, textPosition + ImVec2(-outlineSize, 0.0f), outlineColor, message, messageEnd, canvasSize.x, &clipRect);
            drawList->AddText(nullptr, 0.0f, textPosition + ImVec2(0.0f, +outlineSize), outlineColor, message, messageEnd, canvasSize.x, &clipRect);
            drawList->AddText(nullptr, 0.0f, textPosition + ImVec2(0.0f, -outlineSize), outlineColor, message, messageEnd, canvasSize.x, &clipRect);
        }

        for (int i = 0; i < 2; ++i) // Draw text twice to make it look sharper
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
                ImGui::SetTooltip("Click to %s message", entry.m_IsPinned ? "unpin" : "pin");
        }

        if (cursor.y < canvasMin.y)
            break;
    }
}

void crude_logger::OverlayLogger::AddKeyword(string_view keyword)
{
    m_Keywords.push_back(keyword.to_string());
}

void crude_logger::OverlayLogger::RemoveKeyword(string_view keyword)
{
    auto it = std::find(m_Keywords.begin(), m_Keywords.end(), keyword);
    if (it != m_Keywords.end())
        m_Keywords.erase(it);
}

ImColor crude_logger::OverlayLogger::GetLevelColor(LogLevel level) const
{
    switch (level)
    {
        case LogLevel::Verbose: return m_LogVerboseColor; break;
        case LogLevel::Info:    return m_LogInfoColor;    break;
        case LogLevel::Warning: return m_LogWarningColor; break;
        case LogLevel::Error:   return m_LogErrorColor;   break;
    }

    return ImColor();
}

void crude_logger::OverlayLogger::TintVertices(ImDrawList* drawList, int firstVertexIndex, ImColor color, float alpha, int rangeStart, int rangeSize)
{
    // We tint data vertices emitted by ImDrawList::AddText().
    //
    // We work under assumption that each character is built from
    // four vertices.

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

crude_logger::vector<crude_logger::OverlayLogger::Range> crude_logger::OverlayLogger::ParseMessage(LogLevel level, string_view message) const
{
    // Totally not optimized message parsing. It identifies numbers,
    // strings, tags, keywords and symbols.

    vector<Range> result;

    auto levelColor = GetLevelColor(level);

    // Color timestamp '[00:00:00] [x] ', this is present in all messages.
    result.push_back({  0, 10, m_LogSymbolColor });
    result.push_back({  1,  2, m_LogTimeColor });
    result.push_back({  4,  2, m_LogTimeColor });
    result.push_back({  7,  2, m_LogTimeColor });
    result.push_back({ 11,  1, m_LogSymbolColor });
    result.push_back({ 12,  1, levelColor });
    result.push_back({ 13,  1, m_LogSymbolColor });

    // Color rest of the message to level color.
    result.push_back({ 15, static_cast<int>(message.size()) - 15, levelColor });

    // More usefull variant of find_first_of().
    // If end of the string is reached and last character is
    // in searched set we return string size not npos.
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

    // More usefull variant of find_first_not_of().
    // Behavior is modified in same way as for find_first_of().
    auto find_first_not_of = [message](const char* s, size_t offset = 0) -> size_t
    {
        auto result = message.find_first_not_of(s, offset);
        if (result != string_view::npos)
            return result;

        if (offset == message.size())
            return message.size();

        if (offset > message.size())
            return string_view::npos;

        if (strchr(s, message.back()))
            return string_view::npos;

        return message.size();
    };

    auto isWhitespace = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };

    auto symbolsWithWhitespace = " \t\n\r[]():;!*+-=<>{}\"'";
    auto symbols               = symbolsWithWhitespace + 4;

    // Start search after timestamp, there is no need to parse it over and over again.
    const auto c_HeaderSize = 15;

    // Seek for symbols
    size_t searchStart = c_HeaderSize;
    while (true)
    {
        auto index = find_first_of(symbols, searchStart);
        if (index == string_view::npos)
            break;

        result.push_back({ static_cast<int>(index), 1, m_LogSymbolColor });

        searchStart = index + 1;
    }

    // Seek for tags, text in brackets
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

    // Seek for tokens, matches standalone words separated by symbols
    searchStart = c_HeaderSize;
    while (true)
    {
        auto tokenStart = find_first_not_of(symbolsWithWhitespace, searchStart);
        if (tokenStart == string_view::npos)
            break;

        auto tokenEnd = find_first_of(symbolsWithWhitespace, tokenStart + 1);
        if (tokenEnd == string_view::npos)
            tokenEnd = message.size();

        auto token = message.substr(tokenStart, tokenEnd - tokenStart);

        if (std::find(m_Keywords.begin(), m_Keywords.end(), token) != m_Keywords.end())
        {
            result.push_back({ static_cast<int>(tokenStart), static_cast<int>(tokenEnd - tokenStart), m_LogKeywordColor });
        }

        searchStart = tokenEnd + 1;
    }

    // Seek for strings, matches "strings" and 'strings'
    searchStart = c_HeaderSize;
    while (true)
    {
        auto stringStart = find_first_of("\"'", searchStart);
        if (stringStart >= message.size())
            break;

        char search[2] = " ";
        search[0] = message[stringStart];

        auto stringEnd = find_first_of(search, stringStart + 1);
        if (stringEnd == string_view::npos)
            break;

        result.push_back({ static_cast<int>(stringStart), static_cast<int>(stringEnd - stringStart) + 1, m_LogStringColor });

        searchStart = stringEnd + 2;
    }

    // Seek for decimal numbers
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

    // Now... ImDrawList::AddText() do not render whitespaces,
    // our ranges are build on original string. To properly
    // match generated vertex date we need to shift and shrink
    // ranges.
    for (auto& range : result)
    {
        auto spacesBefore = std::count_if(message.begin(),                 message.begin() + range.m_Start,                isWhitespace);
        auto spacesInside = std::count_if(message.begin() + range.m_Start, message.begin() + range.m_Start + range.m_Size, isWhitespace);
        range.m_Start -= static_cast<int>(spacesBefore);
        range.m_Size  -= static_cast<int>(spacesInside);
    }

    // Drop all empty ranges.
    result.erase(std::remove_if(result.begin(), result.end(), [](const auto& range)
    {
        return range.m_Size <= 0;
    }), result.end());

    return result;
}
