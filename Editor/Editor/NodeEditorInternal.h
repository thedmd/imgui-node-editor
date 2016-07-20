#pragma once

#include "NodeEditor.h"
#include "imgui/imgui_internal.h"
#include "types.h"
#include "types.inl"
#define PICOJSON_USE_LOCALE 0
#include "picojson.h"
#include <vector>

namespace ax {
namespace NodeEditor {

void Log(const char* fmt, ...);

using std::vector;
using std::string;

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
static inline rect   get_item_bounds()             { return rect(to_point(ImGui::GetItemRectMin()), to_point(ImGui::GetItemRectMax())); }

extern Context* s_Editor;

//------------------------------------------------------------------------------
// Layouts with springs
enum class LayoutType
{
    Horizontal,
    Vertical
};

enum class LayoutItemType
{
    Widget, Spring
};

struct LayoutItem
{
    LayoutItemType  Type;
    size            Size;
    float           SpringWeight;
    int             SpringSize;

    LayoutItem(LayoutItemType type);
};

struct Layout
{
    const LayoutType    Type;
    vector<LayoutItem>  Items;
    size                Size;

    int                 CurrentItem;
    bool                Editing;
    bool                Modified;

    Layout(LayoutType type);

    void Begin();
    void AddSeparator(float weight = 1.0f);
    void End();

    bool Build(const size& size);

    int ItemCount() const;
    void MakeDirty();

private:
    void BeginWidget();
    void EndWidget();
    //void PushWidget(const size& size);
    void PushSpring(float weight);

    void BeginGroup();
    void EndGroup();

    void CenterWidget();
};


//------------------------------------------------------------------------------
struct Node
{
    int     ID;
    rect    Bounds;

    rect    ContentBounds;
    Layout  HeaderLayout;

    Node(int id);

    void Layout(const rect& bounds);
};

enum class NodeStage
{
    Invalid,
    Begin,
    Header,
    Content,
    Input,
    Output,
    End
};

struct NodeSettings
{
    int   ID;
    point Location;

    NodeSettings(int id): ID(id) {}
};

struct Settings
{
    string  Path;
    bool    Dirty;

    vector<NodeSettings> Nodes;

    Settings(): Path("NodeEditor.json"), Dirty(false) {}
};

struct Context
{
    vector<Node*>   Nodes;
    Node*           CurrentNode;
    bool            CurrentNodeIsNew;
    NodeStage       CurrentNodeStage;
    Node*           ActiveNode;
    ImVec2          DragOffset;

    Layout*         CurrentLayout;

    bool            IsInitialized;
    Settings        Settings;

    Context();
    ~Context();

    Node* FindNode(int id);
    Node* CreateNode(int id);
    void DestroyNode(Node* node);
    void SetCurrentNode(Node* node, bool isNew = false);
    void SetActiveNode(Node* node);
    bool SetNodeStage(NodeStage stage);
    void SetCurrentLayout(Layout* layout);

    NodeSettings* FindNodeSettings(int id);
    NodeSettings* AddNodeSettings(int id);
    void          LoadSettings();
    void          SaveSettings();
    void          MarkSettingsDirty();
};

namespace Draw {
void Icon(ImDrawList* drawList, rect rect, IconType type, bool filled, ImU32 color);
} // namespace Draw








} // namespace node_editor
} // namespace ax