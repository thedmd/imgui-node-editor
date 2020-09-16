#pragma once
#include <imgui.h>
# define IMGUI_DEFINE_MATH_OPERATORS
# include <imgui_internal.h>

namespace ImEx {

enum class IconType: ImU32 { Flow, FlowDown, Circle, Square, Grid, RoundSquare, Diamond };

// Draws an icon into specified draw list.
void DrawIcon(ImDrawList* drawList, const ImVec2& a, const ImVec2& b, IconType type, bool filled, ImU32 color, ImU32 innerColor);

// Icon widget
void Icon(const ImVec2& size, IconType type, bool filled, const ImVec4& color = ImVec4(1, 1, 1, 1), const ImVec4& innerColor = ImVec4(0, 0, 0, 0));

// Drawn an rectangle around last ImGui widget.
void Debug_DrawItemRect(const ImVec4& col = ImVec4(1.0f, 0.0f, 0.0f, 1.0f));


struct ScopedItemWidth
{
    ScopedItemWidth(float width);
    ~ScopedItemWidth();

    void Release();

private:
    bool m_IsDone = false;
};


struct ScopedDisableItem
{
    ScopedDisableItem(bool disable, float disabledAlpha = 0.5f);
    ~ScopedDisableItem();

    void Release();

private:
    bool    m_Disable       = false;
    float   m_LastAlpha     = 1.0f;
};


struct ScopedSuspendLayout
{
    ScopedSuspendLayout();
    ~ScopedSuspendLayout();

    void Release();

private:
    ImGuiWindow* m_Window = nullptr;
    ImVec2 m_CursorPos;
    ImVec2 m_CursorPosPrevLine;
    ImVec2 m_CursorMaxPos;
    ImVec2 m_CurrLineSize;
    ImVec2 m_PrevLineSize;
    float  m_CurrLineTextBaseOffset;
    float  m_PrevLineTextBaseOffset;
};


} // namespace ImEx