# include "crude_layout.h"
# define IMGUI_DEFINE_MATH_OPERATORS
# include <imgui_internal.h>

void crude_layout::Grid::Begin(const char* id, int columns, float width)
{
    Begin(ImGui::GetID(id), columns, width);
}

void crude_layout::Grid::Begin(ImU32 id, int columns, float width)
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
    m_MinimumWidth = width;

    EnterCell(0, 0);
}

void crude_layout::Grid::NextColumn()
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

void crude_layout::Grid::NextRow()
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

void crude_layout::Grid::EnterCell(int column, int row)
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

void crude_layout::Grid::LeaveCell()
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

void crude_layout::Grid::SetColumnAlignment(float alignment)
{
    alignment = ImClamp(alignment, 0.0f, 1.0f);
    m_ColumnAlignment = alignment;
}

void crude_layout::Grid::End()
{
    LeaveCell();

    float totalWidth = 0.0f;
    for (int i = 0; i < m_Columns; ++i)
    {
        ImGui::PushID(ColumnSeed(i));
        auto currentMaxCellWidth = m_Storage->GetFloat(ImGui::GetID("MaximumColumnWidthAcc"), -1.0f);
        totalWidth += currentMaxCellWidth;
        m_Storage->SetFloat(ImGui::GetID("MaximumColumnWidth"), currentMaxCellWidth);
        ImGui::PopID();
    }

    if (totalWidth < m_MinimumWidth)
    {
        auto spaceToDivide = m_MinimumWidth - totalWidth;
        auto spacePerColumn = ImCeil(spaceToDivide / m_Columns);

        for (int i = 0; i < m_Columns; ++i)
        {
            ImGui::PushID(ColumnSeed(i));
            auto columnWidth = m_Storage->GetFloat(ImGui::GetID("MaximumColumnWidth"), -1.0f);
            columnWidth += spacePerColumn;
            m_Storage->SetFloat(ImGui::GetID("MaximumColumnWidth"), columnWidth);
            ImGui::PopID();

            spaceToDivide -= spacePerColumn;
            if (spaceToDivide < 0)
                spacePerColumn += spaceToDivide;
        }
    }

    ImGui::PopID();
}
