//------------------------------------------------------------------------------
// VERSION 0.9.1
//
// LICENSE
//   This software is dual-licensed to the public domain and under the following
//   license: you are granted a perpetual, irrevocable license to copy, modify,
//   publish, and distribute this file as you see fit.
//
// CREDITS
//   Written by Michal Cichon
//------------------------------------------------------------------------------
# ifndef __IMGUI_NODE_EDITOR_H__
# define __IMGUI_NODE_EDITOR_H__
# pragma once


//------------------------------------------------------------------------------
# include <imgui.h>
# include <cstdint> // std::uintXX_t
# include <utility> // std::move


//------------------------------------------------------------------------------
# define IMGUI_NODE_EDITOR_VERSION      "0.9.4"
# define IMGUI_NODE_EDITOR_VERSION_NUM  000904


//------------------------------------------------------------------------------
#ifndef IMGUI_NODE_EDITOR_API
#define IMGUI_NODE_EDITOR_API
#endif


//------------------------------------------------------------------------------
namespace ax {
namespace NodeEditor {


//------------------------------------------------------------------------------
struct NodeId;
struct LinkId;
struct PinId;


//------------------------------------------------------------------------------
enum class PinKind
{
    Input,
    Output
};

enum class FlowDirection
{
    Forward,
    Backward
};

enum class CanvasSizeMode
{
    FitVerticalView,        // Previous view will be scaled to fit new view on Y axis
    FitHorizontalView,      // Previous view will be scaled to fit new view on X axis
    CenterOnly,             // Previous view will be centered on new view
};


//------------------------------------------------------------------------------
enum class SaveReasonFlags: uint32_t
{
    None       = 0x00000000,
    Navigation = 0x00000001,
    Position   = 0x00000002,
    Size       = 0x00000004,
    Selection  = 0x00000008,
    AddNode    = 0x00000010,
    RemoveNode = 0x00000020,
    User       = 0x00000040
};

inline SaveReasonFlags operator |(SaveReasonFlags lhs, SaveReasonFlags rhs) { return static_cast<SaveReasonFlags>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs)); }
inline SaveReasonFlags operator &(SaveReasonFlags lhs, SaveReasonFlags rhs) { return static_cast<SaveReasonFlags>(static_cast<uint32_t>(lhs) & static_cast<uint32_t>(rhs)); }

using ConfigSaveSettings     = bool   (*)(const char* data, size_t size, SaveReasonFlags reason, void* userPointer);
using ConfigLoadSettings     = size_t (*)(char* data, void* userPointer);

using ConfigSaveNodeSettings = bool   (*)(NodeId nodeId, const char* data, size_t size, SaveReasonFlags reason, void* userPointer);
using ConfigLoadNodeSettings = size_t (*)(NodeId nodeId, char* data, void* userPointer);

using ConfigSession          = void   (*)(void* userPointer);

struct Config
{
    using CanvasSizeModeAlias = ax::NodeEditor::CanvasSizeMode;

    const char*             SettingsFile;
    ConfigSession           BeginSaveSession;
    ConfigSession           EndSaveSession;
    ConfigSaveSettings      SaveSettings;
    ConfigLoadSettings      LoadSettings;
    ConfigSaveNodeSettings  SaveNodeSettings;
    ConfigLoadNodeSettings  LoadNodeSettings;
    void*                   UserPointer;
    ImVector<float>         CustomZoomLevels;
    CanvasSizeModeAlias     CanvasSizeMode;
    int                     DragButtonIndex;        // Mouse button index drag action will react to (0-left, 1-right, 2-middle)
    int                     SelectButtonIndex;      // Mouse button index select action will react to (0-left, 1-right, 2-middle)
    int                     NavigateButtonIndex;    // Mouse button index navigate action will react to (0-left, 1-right, 2-middle)
    int                     ContextMenuButtonIndex; // Mouse button index context menu action will react to (0-left, 1-right, 2-middle)
    bool                    EnableSmoothZoom;
    float                   SmoothZoomPower;

    Config()
        : SettingsFile("NodeEditor.json")
        , BeginSaveSession(nullptr)
        , EndSaveSession(nullptr)
        , SaveSettings(nullptr)
        , LoadSettings(nullptr)
        , SaveNodeSettings(nullptr)
        , LoadNodeSettings(nullptr)
        , UserPointer(nullptr)
        , CustomZoomLevels()
        , CanvasSizeMode(CanvasSizeModeAlias::FitVerticalView)
        , DragButtonIndex(0)
        , SelectButtonIndex(0)
        , NavigateButtonIndex(1)
        , ContextMenuButtonIndex(1)
        , EnableSmoothZoom(false)
# ifdef __APPLE__
        , SmoothZoomPower(1.1f)
# else
        , SmoothZoomPower(1.3f)
# endif
    {
    }
};


//------------------------------------------------------------------------------
enum StyleColor
{
    StyleColor_Bg,
    StyleColor_Grid,
    StyleColor_NodeBg,
    StyleColor_NodeBorder,
    StyleColor_HovNodeBorder,
    StyleColor_SelNodeBorder,
    StyleColor_NodeSelRect,
    StyleColor_NodeSelRectBorder,
    StyleColor_HovLinkBorder,
    StyleColor_SelLinkBorder,
    StyleColor_HighlightLinkBorder,
    StyleColor_LinkSelRect,
    StyleColor_LinkSelRectBorder,
    StyleColor_PinRect,
    StyleColor_PinRectBorder,
    StyleColor_Flow,
    StyleColor_FlowMarker,
    StyleColor_GroupBg,
    StyleColor_GroupBorder,

    StyleColor_Count
};

enum StyleVar
{
    StyleVar_NodePadding,
    StyleVar_NodeRounding,
    StyleVar_NodeBorderWidth,
    StyleVar_HoveredNodeBorderWidth,
    StyleVar_SelectedNodeBorderWidth,
    StyleVar_PinRounding,
    StyleVar_PinBorderWidth,
    StyleVar_LinkStrength,
    StyleVar_SourceDirection,
    StyleVar_TargetDirection,
    StyleVar_ScrollDuration,
    StyleVar_FlowMarkerDistance,
    StyleVar_FlowSpeed,
    StyleVar_FlowDuration,
    StyleVar_PivotAlignment,
    StyleVar_PivotSize,
    StyleVar_PivotScale,
    StyleVar_PinCorners,
    StyleVar_PinRadius,
    StyleVar_PinArrowSize,
    StyleVar_PinArrowWidth,
    StyleVar_GroupRounding,
    StyleVar_GroupBorderWidth,
    StyleVar_HighlightConnectedLinks,
    StyleVar_SnapLinkToPinDir,
    StyleVar_HoveredNodeBorderOffset,
    StyleVar_SelectedNodeBorderOffset,

    StyleVar_Count
};

struct Style
{
    ImVec4  NodePadding;
    float   NodeRounding;
    float   NodeBorderWidth;
    float   HoveredNodeBorderWidth;
    float   HoverNodeBorderOffset;
    float   SelectedNodeBorderWidth;
    float   SelectedNodeBorderOffset;
    float   PinRounding;
    float   PinBorderWidth;
    float   LinkStrength;
    ImVec2  SourceDirection;
    ImVec2  TargetDirection;
    float   ScrollDuration;
    float   FlowMarkerDistance;
    float   FlowSpeed;
    float   FlowDuration;
    ImVec2  PivotAlignment;
    ImVec2  PivotSize;
    ImVec2  PivotScale;
    float   PinCorners;
    float   PinRadius;
    float   PinArrowSize;
    float   PinArrowWidth;
    float   GroupRounding;
    float   GroupBorderWidth;
    float   HighlightConnectedLinks;
    float   SnapLinkToPinDir; // when true link will start on the line defined by pin direction
    ImVec4  Colors[StyleColor_Count];

    Style()
    {
        NodePadding              = ImVec4(8, 8, 8, 8);
        NodeRounding             = 12.0f;
        NodeBorderWidth          = 1.5f;
        HoveredNodeBorderWidth   = 3.5f;
        HoverNodeBorderOffset    = 0.0f;
        SelectedNodeBorderWidth  = 3.5f;
        SelectedNodeBorderOffset = 0.0f;
        PinRounding              = 4.0f;
        PinBorderWidth           = 0.0f;
        LinkStrength             = 100.0f;
        SourceDirection          = ImVec2(1.0f, 0.0f);
        TargetDirection          = ImVec2(-1.0f, 0.0f);
        ScrollDuration           = 0.35f;
        FlowMarkerDistance       = 30.0f;
        FlowSpeed                = 150.0f;
        FlowDuration             = 2.0f;
        PivotAlignment           = ImVec2(0.5f, 0.5f);
        PivotSize                = ImVec2(0.0f, 0.0f);
        PivotScale               = ImVec2(1, 1);
#if IMGUI_VERSION_NUM > 18101
        PinCorners               = ImDrawFlags_RoundCornersAll;
#else
        PinCorners               = ImDrawCornerFlags_All;
#endif
        PinRadius                = 0.0f;
        PinArrowSize             = 0.0f;
        PinArrowWidth            = 0.0f;
        GroupRounding            = 6.0f;
        GroupBorderWidth         = 1.0f;
        HighlightConnectedLinks  = 0.0f;
        SnapLinkToPinDir         = 0.0f;

        Colors[StyleColor_Bg]                 = ImColor( 60,  60,  70, 200);
        Colors[StyleColor_Grid]               = ImColor(120, 120, 120,  40);
        Colors[StyleColor_NodeBg]             = ImColor( 32,  32,  32, 200);
        Colors[StyleColor_NodeBorder]         = ImColor(255, 255, 255,  96);
        Colors[StyleColor_HovNodeBorder]      = ImColor( 50, 176, 255, 255);
        Colors[StyleColor_SelNodeBorder]      = ImColor(255, 176,  50, 255);
        Colors[StyleColor_NodeSelRect]        = ImColor(  5, 130, 255,  64);
        Colors[StyleColor_NodeSelRectBorder]  = ImColor(  5, 130, 255, 128);
        Colors[StyleColor_HovLinkBorder]      = ImColor( 50, 176, 255, 255);
        Colors[StyleColor_SelLinkBorder]      = ImColor(255, 176,  50, 255);
        Colors[StyleColor_HighlightLinkBorder]= ImColor(204, 105,   0, 255);
        Colors[StyleColor_LinkSelRect]        = ImColor(  5, 130, 255,  64);
        Colors[StyleColor_LinkSelRectBorder]  = ImColor(  5, 130, 255, 128);
        Colors[StyleColor_PinRect]            = ImColor( 60, 180, 255, 100);
        Colors[StyleColor_PinRectBorder]      = ImColor( 60, 180, 255, 128);
        Colors[StyleColor_Flow]               = ImColor(255, 128,  64, 255);
        Colors[StyleColor_FlowMarker]         = ImColor(255, 128,  64, 255);
        Colors[StyleColor_GroupBg]            = ImColor(  0,   0,   0, 160);
        Colors[StyleColor_GroupBorder]        = ImColor(255, 255, 255,  32);
    }
};


//------------------------------------------------------------------------------
struct EditorContext;


//------------------------------------------------------------------------------
IMGUI_NODE_EDITOR_API void SetCurrentEditor(EditorContext* ctx);
IMGUI_NODE_EDITOR_API EditorContext* GetCurrentEditor();
IMGUI_NODE_EDITOR_API EditorContext* CreateEditor(const Config* config = nullptr);
IMGUI_NODE_EDITOR_API void DestroyEditor(EditorContext* ctx);
IMGUI_NODE_EDITOR_API const Config& GetConfig(EditorContext* ctx = nullptr);

IMGUI_NODE_EDITOR_API Style& GetStyle();
IMGUI_NODE_EDITOR_API const char* GetStyleColorName(StyleColor colorIndex);

IMGUI_NODE_EDITOR_API void PushStyleColor(StyleColor colorIndex, const ImVec4& color);
IMGUI_NODE_EDITOR_API void PopStyleColor(int count = 1);

IMGUI_NODE_EDITOR_API void PushStyleVar(StyleVar varIndex, float value);
IMGUI_NODE_EDITOR_API void PushStyleVar(StyleVar varIndex, const ImVec2& value);
IMGUI_NODE_EDITOR_API void PushStyleVar(StyleVar varIndex, const ImVec4& value);
IMGUI_NODE_EDITOR_API void PopStyleVar(int count = 1);

IMGUI_NODE_EDITOR_API void Begin(const char* id, const ImVec2& size = ImVec2(0, 0));
IMGUI_NODE_EDITOR_API void End();

IMGUI_NODE_EDITOR_API void BeginNode(NodeId id);
IMGUI_NODE_EDITOR_API void BeginPin(PinId id, PinKind kind);
IMGUI_NODE_EDITOR_API void PinRect(const ImVec2& a, const ImVec2& b);
IMGUI_NODE_EDITOR_API void PinPivotRect(const ImVec2& a, const ImVec2& b);
IMGUI_NODE_EDITOR_API void PinPivotSize(const ImVec2& size);
IMGUI_NODE_EDITOR_API void PinPivotScale(const ImVec2& scale);
IMGUI_NODE_EDITOR_API void PinPivotAlignment(const ImVec2& alignment);
IMGUI_NODE_EDITOR_API void EndPin();
IMGUI_NODE_EDITOR_API void Group(const ImVec2& size);
IMGUI_NODE_EDITOR_API void EndNode();

IMGUI_NODE_EDITOR_API bool BeginGroupHint(NodeId nodeId);
IMGUI_NODE_EDITOR_API ImVec2 GetGroupMin();
IMGUI_NODE_EDITOR_API ImVec2 GetGroupMax();
IMGUI_NODE_EDITOR_API ImDrawList* GetHintForegroundDrawList();
IMGUI_NODE_EDITOR_API ImDrawList* GetHintBackgroundDrawList();
IMGUI_NODE_EDITOR_API void EndGroupHint();

// TODO: Add a way to manage node background channels
IMGUI_NODE_EDITOR_API ImDrawList* GetNodeBackgroundDrawList(NodeId nodeId);

IMGUI_NODE_EDITOR_API bool Link(LinkId id, PinId startPinId, PinId endPinId, const ImVec4& color = ImVec4(1, 1, 1, 1), float thickness = 1.0f);

IMGUI_NODE_EDITOR_API void Flow(LinkId linkId, FlowDirection direction = FlowDirection::Forward);

IMGUI_NODE_EDITOR_API bool BeginCreate(const ImVec4& color = ImVec4(1, 1, 1, 1), float thickness = 1.0f);
IMGUI_NODE_EDITOR_API bool QueryNewLink(PinId* startId, PinId* endId);
IMGUI_NODE_EDITOR_API bool QueryNewLink(PinId* startId, PinId* endId, const ImVec4& color, float thickness = 1.0f);
IMGUI_NODE_EDITOR_API bool QueryNewNode(PinId* pinId);
IMGUI_NODE_EDITOR_API bool QueryNewNode(PinId* pinId, const ImVec4& color, float thickness = 1.0f);
IMGUI_NODE_EDITOR_API bool AcceptNewItem();
IMGUI_NODE_EDITOR_API bool AcceptNewItem(const ImVec4& color, float thickness = 1.0f);
IMGUI_NODE_EDITOR_API void RejectNewItem();
IMGUI_NODE_EDITOR_API void RejectNewItem(const ImVec4& color, float thickness = 1.0f);
IMGUI_NODE_EDITOR_API void EndCreate();

IMGUI_NODE_EDITOR_API bool BeginDelete();
IMGUI_NODE_EDITOR_API bool QueryDeletedLink(LinkId* linkId, PinId* startId = nullptr, PinId* endId = nullptr);
IMGUI_NODE_EDITOR_API bool QueryDeletedNode(NodeId* nodeId);
IMGUI_NODE_EDITOR_API bool AcceptDeletedItem(bool deleteDependencies = true);
IMGUI_NODE_EDITOR_API void RejectDeletedItem();
IMGUI_NODE_EDITOR_API void EndDelete();

IMGUI_NODE_EDITOR_API void SetNodePosition(NodeId nodeId, const ImVec2& editorPosition);
IMGUI_NODE_EDITOR_API void SetGroupSize(NodeId nodeId, const ImVec2& size);
IMGUI_NODE_EDITOR_API ImVec2 GetNodePosition(NodeId nodeId);
IMGUI_NODE_EDITOR_API ImVec2 GetNodeSize(NodeId nodeId);
IMGUI_NODE_EDITOR_API void CenterNodeOnScreen(NodeId nodeId);
IMGUI_NODE_EDITOR_API void SetNodeZPosition(NodeId nodeId, float z); // Sets node z position, nodes with higher value are drawn over nodes with lower value
IMGUI_NODE_EDITOR_API float GetNodeZPosition(NodeId nodeId); // Returns node z position, defaults is 0.0f

IMGUI_NODE_EDITOR_API void RestoreNodeState(NodeId nodeId);

IMGUI_NODE_EDITOR_API void Suspend();
IMGUI_NODE_EDITOR_API void Resume();
IMGUI_NODE_EDITOR_API bool IsSuspended();

IMGUI_NODE_EDITOR_API bool IsActive();

IMGUI_NODE_EDITOR_API bool HasSelectionChanged();
IMGUI_NODE_EDITOR_API int  GetSelectedObjectCount();
IMGUI_NODE_EDITOR_API int  GetSelectedNodes(NodeId* nodes, int size);
IMGUI_NODE_EDITOR_API int  GetSelectedLinks(LinkId* links, int size);
IMGUI_NODE_EDITOR_API bool IsNodeSelected(NodeId nodeId);
IMGUI_NODE_EDITOR_API bool IsLinkSelected(LinkId linkId);
IMGUI_NODE_EDITOR_API void ClearSelection();
IMGUI_NODE_EDITOR_API void SelectNode(NodeId nodeId, bool append = false);
IMGUI_NODE_EDITOR_API void SelectLink(LinkId linkId, bool append = false);
IMGUI_NODE_EDITOR_API void DeselectNode(NodeId nodeId);
IMGUI_NODE_EDITOR_API void DeselectLink(LinkId linkId);

IMGUI_NODE_EDITOR_API bool DeleteNode(NodeId nodeId);
IMGUI_NODE_EDITOR_API bool DeleteLink(LinkId linkId);

IMGUI_NODE_EDITOR_API bool HasAnyLinks(NodeId nodeId); // Returns true if node has any link connected
IMGUI_NODE_EDITOR_API bool HasAnyLinks(PinId pinId); // Return true if pin has any link connected
IMGUI_NODE_EDITOR_API int BreakLinks(NodeId nodeId); // Break all links connected to this node
IMGUI_NODE_EDITOR_API int BreakLinks(PinId pinId); // Break all links connected to this pin

IMGUI_NODE_EDITOR_API void NavigateToContent(float duration = -1);
IMGUI_NODE_EDITOR_API void NavigateToSelection(bool zoomIn = false, float duration = -1);

IMGUI_NODE_EDITOR_API bool ShowNodeContextMenu(NodeId* nodeId);
IMGUI_NODE_EDITOR_API bool ShowPinContextMenu(PinId* pinId);
IMGUI_NODE_EDITOR_API bool ShowLinkContextMenu(LinkId* linkId);
IMGUI_NODE_EDITOR_API bool ShowBackgroundContextMenu();

IMGUI_NODE_EDITOR_API void EnableShortcuts(bool enable);
IMGUI_NODE_EDITOR_API bool AreShortcutsEnabled();

IMGUI_NODE_EDITOR_API bool BeginShortcut();
IMGUI_NODE_EDITOR_API bool AcceptCut();
IMGUI_NODE_EDITOR_API bool AcceptCopy();
IMGUI_NODE_EDITOR_API bool AcceptPaste();
IMGUI_NODE_EDITOR_API bool AcceptDuplicate();
IMGUI_NODE_EDITOR_API bool AcceptCreateNode();
IMGUI_NODE_EDITOR_API int  GetActionContextSize();
IMGUI_NODE_EDITOR_API int  GetActionContextNodes(NodeId* nodes, int size);
IMGUI_NODE_EDITOR_API int  GetActionContextLinks(LinkId* links, int size);
IMGUI_NODE_EDITOR_API void EndShortcut();

IMGUI_NODE_EDITOR_API float GetCurrentZoom();

IMGUI_NODE_EDITOR_API NodeId GetHoveredNode();
IMGUI_NODE_EDITOR_API PinId GetHoveredPin();
IMGUI_NODE_EDITOR_API LinkId GetHoveredLink();
IMGUI_NODE_EDITOR_API NodeId GetDoubleClickedNode();
IMGUI_NODE_EDITOR_API PinId GetDoubleClickedPin();
IMGUI_NODE_EDITOR_API LinkId GetDoubleClickedLink();
IMGUI_NODE_EDITOR_API bool IsBackgroundClicked();
IMGUI_NODE_EDITOR_API bool IsBackgroundDoubleClicked();
IMGUI_NODE_EDITOR_API ImGuiMouseButton GetBackgroundClickButtonIndex(); // -1 if none
IMGUI_NODE_EDITOR_API ImGuiMouseButton GetBackgroundDoubleClickButtonIndex(); // -1 if none

IMGUI_NODE_EDITOR_API bool GetLinkPins(LinkId linkId, PinId* startPinId, PinId* endPinId); // pass nullptr if particular pin do not interest you

IMGUI_NODE_EDITOR_API bool PinHadAnyLinks(PinId pinId);

IMGUI_NODE_EDITOR_API ImVec2 GetScreenSize();
IMGUI_NODE_EDITOR_API ImVec2 ScreenToCanvas(const ImVec2& pos);
IMGUI_NODE_EDITOR_API ImVec2 CanvasToScreen(const ImVec2& pos);

IMGUI_NODE_EDITOR_API int GetNodeCount();                                // Returns number of submitted nodes since Begin() call
IMGUI_NODE_EDITOR_API int GetOrderedNodeIds(NodeId* nodes, int size);    // Fills an array with node id's in order they're drawn; up to 'size` elements are set. Returns actual size of filled id's.







//------------------------------------------------------------------------------
namespace Details {

template <typename T, typename Tag>
struct SafeType
{
    SafeType(T t)
        : m_Value(std::move(t))
    {
    }

    SafeType(const SafeType&) = default;

    template <typename T2, typename Tag2>
    SafeType(
        const SafeType
        <
            typename std::enable_if<!std::is_same<T, T2>::value, T2>::type,
            typename std::enable_if<!std::is_same<Tag, Tag2>::value, Tag2>::type
        >&) = delete;

    SafeType& operator=(const SafeType&) = default;

    explicit operator T() const { return Get(); }

    T Get() const { return m_Value; }

private:
    T m_Value;
};


template <typename Tag>
struct SafePointerType
    : SafeType<uintptr_t, Tag>
{
    static const Tag Invalid;

    using SafeType<uintptr_t, Tag>::SafeType;

    SafePointerType()
        : SafePointerType(Invalid)
    {
    }

    template <typename T = void> explicit SafePointerType(T* ptr): SafePointerType(reinterpret_cast<uintptr_t>(ptr)) {}
    template <typename T = void> T* AsPointer() const { return reinterpret_cast<T*>(this->Get()); }

    explicit operator bool() const { return *this != Invalid; }
};

template <typename Tag>
const Tag SafePointerType<Tag>::Invalid = { 0 };

template <typename Tag>
inline bool operator==(const SafePointerType<Tag>& lhs, const SafePointerType<Tag>& rhs)
{
    return lhs.Get() == rhs.Get();
}

template <typename Tag>
inline bool operator!=(const SafePointerType<Tag>& lhs, const SafePointerType<Tag>& rhs)
{
    return lhs.Get() != rhs.Get();
}

} // namespace Details

struct NodeId final: Details::SafePointerType<NodeId>
{
    using SafePointerType::SafePointerType;
};

struct LinkId final: Details::SafePointerType<LinkId>
{
    using SafePointerType::SafePointerType;
};

struct PinId final: Details::SafePointerType<PinId>
{
    using SafePointerType::SafePointerType;
};


//------------------------------------------------------------------------------
} // namespace Editor
} // namespace ax


//------------------------------------------------------------------------------
# endif // __IMGUI_NODE_EDITOR_H__
