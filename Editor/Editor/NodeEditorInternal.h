#pragma once

#include "NodeEditor.h"
#include "imgui/imgui_internal.h"
#include "types.h"
#include "types.inl"
#include <vector>

namespace ax {
namespace NodeEditor {

static inline ImVec2 operator+(const ImVec2& lhs, const ImVec2& rhs) { return ImVec2(lhs.x+rhs.x, lhs.y+rhs.y); }
static inline ImVec2 operator-(const ImVec2& lhs, const ImVec2& rhs) { return ImVec2(lhs.x-rhs.x, lhs.y-rhs.y); }
static inline ImVec2 operator*(const ImVec2& lhs, float rhs)         { return ImVec2(lhs.x * rhs,   lhs.y * rhs); }
static inline ImVec2 operator*(float lhs,         const ImVec2& rhs) { return ImVec2(lhs   * rhs.x, lhs   * rhs.y); }

static inline int    roundi(float value)           { return static_cast<int>(value); }
static inline point  to_point(const ImVec2& value) { return point(roundi(value.x), roundi(value.y)); }
static inline size   to_size (const ImVec2& value) { return size (roundi(value.x), roundi(value.y)); }
static inline ImVec2 to_imvec(const point& value)  { return ImVec2(static_cast<float>(value.x), static_cast<float>(value.y)); }
static inline ImVec2 to_imvec(const pointf& value) { return ImVec2(value.x, value.y); }
static inline ImVec2 to_imvec(const size& value)   { return ImVec2(static_cast<float>(value.w), static_cast<float>(value.h)); }

extern Context* s_Editor;

struct Node
{
    int     ID;
    rect    Bounds;

    Node(int id):
        ID(id),
        Bounds(to_point(ImGui::GetCursorScreenPos()), size())
    {
    }
};

enum class NodeStage
{
    Invalid,
    Begin,
    Header,
    Input,
    Output,
    End
};

struct Context
{
    std::vector<Node*>  Nodes;
    Node*               CurrentNode;
    bool                CurrentNodeIsNew;
    NodeStage           CurrentNodeStage;
    Node*               ActiveNode;
    ImVec2              DragOffset;

    Context();
    ~Context();

    Node* FindNode(int id);
    Node* CreateNode(int id);
    void DestroyNode(Node* node);
    void SetCurrentNode(Node* node, bool isNew = false);
    void SetActiveNode(Node* node);
    bool SetNodeStage(NodeStage stage);
};

namespace Draw {
void Icon(ImDrawList* drawList, rect rect, IconType type, bool filled, ImU32 color);
} // namespace Draw







} // namespace node_editor
} // namespace ax