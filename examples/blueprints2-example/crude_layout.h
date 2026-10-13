# pragma once
# include <imgui.h>

namespace crude_layout {

// Very crude implementation of grid layout.
//
// It was made to fit the need of laying out blueprint
// node, may not work in more general cases.
struct Grid
{
    void Begin(const char* id, int columns, float width = -1.0f);
    void Begin(ImU32 id, int columns, float width = -1.0f);
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
    float m_MinimumWidth = -1.0f;

    ImVec2 m_CursorPos;

    ImGuiStorage* m_Storage = nullptr;

    float m_ColumnAlignment = 0.0f;
    float m_MaximumColumnWidthAcc = -1.0f;
};

} // namespace crude_layout