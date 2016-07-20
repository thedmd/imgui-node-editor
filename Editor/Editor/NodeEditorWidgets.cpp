#include "NodeEditorInternal.h"

namespace ed = ax::NodeEditor;

bool ed::Icon(const char* id, const ImVec2& size, IconType type, bool filled, const ImVec4& color/* = ImVec4(1, 1, 1, 1)*/)
{
    if (ImGui::IsRectVisible(size))
    {
        auto cursorPos = ImGui::GetCursorScreenPos();
        auto iconRect = rect(to_point(cursorPos), to_size(size));

        auto drawList = ImGui::GetWindowDrawList();
        Draw::Icon(drawList, iconRect, type, filled, ImColor(color));
    }

    return ImGui::/*Invisible*/Button(id, size);
}

