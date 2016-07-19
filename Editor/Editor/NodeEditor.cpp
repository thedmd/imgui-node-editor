#include "NodeEditorInternal.h"

namespace ed = ax::NodeEditor;

namespace ax {
namespace NodeEditor {
Context* s_Editor = nullptr;
} // namespace node_editor
} // namespace ax

bool ed::Icon(const char* id, const ImVec2& size, IconType type, bool filled, const ImVec4& color/* = ImVec4(1, 1, 1, 1)*/)
{
    if (!ImGui::IsRectVisible(size))
        return false;

    auto cursorPos = ImGui::GetCursorScreenPos();
    auto iconRect  = rect(to_point(cursorPos), to_size(size));

    auto drawList  = ImGui::GetWindowDrawList();
    Draw::Icon(drawList, iconRect, type, filled, ImColor(color));

    return ImGui::InvisibleButton(id, size);
}

ax::NodeEditor::Context* ax::NodeEditor::CreateEditor()
{
    return new Context();
}

void ax::NodeEditor::DestroyEditor(Context* ctx)
{
    if (GetCurrentEditor() == ctx)
        SetCurrentEditor(nullptr);

    delete ctx;
}

void ax::NodeEditor::SetCurrentEditor(Context* ctx)
{
    s_Editor = ctx;
}

ax::NodeEditor::Context* ax::NodeEditor::GetCurrentEditor()
{
    return s_Editor;
}

ed::Node* ed::FindNode(int id)
{
    return nullptr;
}

ed::Node* ed::CreateNode(int id)
{
    return nullptr;
}

void ed::DestroyNode(Node* node)
{
}

void ax::NodeEditor::Begin(const char* id)
{
    ImGui::BeginChild(id, ImVec2(0, 0), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove);
}

void ax::NodeEditor::End()
{
    ImGui::EndChild();
}

void ax::NodeEditor::BeginNode(int id)
{
}

void ax::NodeEditor::EndNode()
{
}


void ax::NodeEditor::BeginHeader()
{
}

void ax::NodeEditor::EndHeader()
{
}

void ax::NodeEditor::BeginInput(int id)
{
}

void ax::NodeEditor::EndInput()
{
}

void ax::NodeEditor::BeginOutput(int id)
{
}

void ax::NodeEditor::EndOutput()
{
}





