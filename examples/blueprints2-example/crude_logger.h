# pragma once
# include <imgui.h>
# include "nonstd/string_view.hpp" // string_view
# include <vector>
# include <string>
# include <time.h>
# include <stdint.h>

namespace crude_logger {

using nonstd::string_view;
using std::vector;
using std::string;

enum class LogLevel: int32_t
{
    Verbose,
    Info,
    Warning,
    Error,
};

# define LOGV(...)      ::crude_logger::OverlayLogger::GetCurrent()->Log(::crude_logger::LogLevel::Verbose,  __VA_ARGS__)
# define LOGI(...)      ::crude_logger::OverlayLogger::GetCurrent()->Log(::crude_logger::LogLevel::Info,     __VA_ARGS__)
# define LOGW(...)      ::crude_logger::OverlayLogger::GetCurrent()->Log(::crude_logger::LogLevel::Warning,  __VA_ARGS__)
# define LOGE(...)      ::crude_logger::OverlayLogger::GetCurrent()->Log(::crude_logger::LogLevel::Error,    __VA_ARGS__)


struct OverlayLogger
{
    static void SetCurrent(OverlayLogger* instance);
    static OverlayLogger* GetCurrent();

    void Log(LogLevel level, const char* format, ...) IM_FMTARGS(3);

    void Update(float dt);
    void Draw(const ImVec2& a, const ImVec2& b);

    void AddKeyword(string_view keyword);
    void RemoveKeyword(string_view keyword);

private:
    struct Range
    {
        int     m_Start = 0;
        int     m_Size  = 0;
        ImColor m_Color;

# if CRUDE_BP_MSVC2015 // No aggregate initialization
        Range() = default;
        Range(int start, int size, ImColor color)
            : m_Start(start)
            , m_Size(size)
            , m_Color(color)
        {
        }
# endif
    };

    struct Entry
    {
        LogLevel        m_Level     = LogLevel::Verbose;
        time_t          m_Timestamp = 0;
        string          m_Text;
        float           m_Timer     = 0.0f;
        bool            m_IsPinned  = false;
        vector<Range>   m_ColorRanges;
    };

    ImColor GetLevelColor(LogLevel level) const;

    void TintVertices(ImDrawList* drawList, int firstVertexIndex, ImColor color, float alpha, int rangeStart, int rangeSize);

    vector<Range> ParseMessage(LogLevel level, string_view message) const;

    float           m_OutlineSize                 = 0.5f;
    float           m_Padding                     = 10.0f;
    float           m_MessagePresentationDuration = 5.0f;
    float           m_MessageFadeOutDuration      = 0.5f;
    float           m_MessageLifeDuration         = m_MessagePresentationDuration + m_MessageFadeOutDuration;
    bool            m_HoldTimer                   = false;
    vector<Entry>   m_Entries;
    vector<string>  m_Keywords;
    ImColor         m_HighlightBorder             = ImColor(  5, 130, 255, 128);
    ImColor         m_HighlightFill               = ImColor(  5, 130, 255,  64);
    ImColor         m_PinBorder                   = ImColor(255, 176,  50,   0);
    ImColor         m_PinFill                     = ImColor(  0,  75, 150, 128);

    ImColor         m_LogTimeColor                = ImColor(150, 209,   0, 255);
    ImColor         m_LogSymbolColor              = ImColor(192, 192, 192, 255);
    ImColor         m_LogStringColor              = ImColor(255, 174, 133, 255);
    ImColor         m_LogTagColor                 = ImColor(255, 214, 143, 255);
    ImColor         m_LogKeywordColor             = ImColor(255, 255, 255, 255);
    ImColor         m_LogTextColor                = ImColor(192, 192, 192, 255);
    ImColor         m_LogOutlineColor             = ImColor(  0,   0,   0, 255);
    ImColor         m_LogNumberColor              = ImColor(255, 255, 128, 255);
    ImColor         m_LogVerboseColor             = ImColor(128, 255, 128, 255);
    ImColor         m_LogWarningColor             = ImColor(255, 255, 192, 255);
    ImColor         m_LogErrorColor               = ImColor(255, 152, 152, 255);
    ImColor         m_LogInfoColor                = ImColor(138, 197, 255, 255);
    ImColor         m_LogAssertColor              = ImColor(255,  61,  68, 255);

    static OverlayLogger* s_Instance;
};

} // namespace crude_logger {