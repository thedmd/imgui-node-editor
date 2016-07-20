#include "NodeEditorInternal.h"

namespace ed = ax::NodeEditor;

ed::Node::Node(int id):
    ID(id),
    Bounds(to_point(ImGui::GetCursorScreenPos()), size()),
    HeaderLayout(LayoutType::Horizontal)
{
}

void ed::Node::Layout(const ax::rect& bounds)
{
    // Adjust header to content size
    HeaderLayout.Build(ContentBounds.size);



}
