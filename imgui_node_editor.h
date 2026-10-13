//------------------------------------------------------------------------------
// LICENSE
//   This software is dual-licensed to the public domain and under the following
//   license: you are granted a perpetual, irrevocable license to copy, modify,
//   publish, and distribute this file as you see fit.
//
// CREDITS
//   Written by Michal Cichon
//------------------------------------------------------------------------------
# ifndef IMGUI_NODE_EDITOR_H
# define IMGUI_NODE_EDITOR_H
# pragma once


//------------------------------------------------------------------------------
# include <imgui.h>
# include <cstdint> // std::uintXX_t
# include <utility> // std::move


//------------------------------------------------------------------------------
// docs/API.md has the rules that span several functions: the order of calls in a
// frame, IDs, the create and delete protocols, input, persistence and coordinates.
# define IMGUI_NODE_EDITOR_VERSION      "0.10.0"
# define IMGUI_NODE_EDITOR_VERSION_NUM  1000


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
struct NodeState;
struct ViewState;
struct Edit;


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

enum class EditType
{
    Move,                   // nodes dragged
    Resize,                 // a group resized
    Select,                 // the selection changed
    Navigate                // the view moved or zoomed, with Config::ReportNavigationEdits
};


//------------------------------------------------------------------------------
enum class SaveReasonFlags: std::uint32_t
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

inline SaveReasonFlags operator |(SaveReasonFlags lhs, SaveReasonFlags rhs) { return static_cast<SaveReasonFlags>(static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs)); }
inline SaveReasonFlags operator &(SaveReasonFlags lhs, SaveReasonFlags rhs) { return static_cast<SaveReasonFlags>(static_cast<std::uint32_t>(lhs) & static_cast<std::uint32_t>(rhs)); }

enum class SettingsContentFlags: std::uint32_t
{
    None      = 0x00000000,
    Nodes     = 0x00000001,
    Selection = 0x00000002,
    View      = 0x00000004,
    All       = 0x00000007
};

inline SettingsContentFlags operator |(SettingsContentFlags lhs, SettingsContentFlags rhs) { return static_cast<SettingsContentFlags>(static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs)); }
inline SettingsContentFlags operator &(SettingsContentFlags lhs, SettingsContentFlags rhs) { return static_cast<SettingsContentFlags>(static_cast<std::uint32_t>(lhs) & static_cast<std::uint32_t>(rhs)); }

struct Config
{
    using CanvasSizeModeAlias = ax::NodeEditor::CanvasSizeMode;

    const char*             SettingsName;           // Section [NodeEditor][SettingsName] in imgui.ini, see InstallSettingsHandler(); copied by CreateEditor(); nullptr keeps the editor out of imgui.ini
    SettingsContentFlags    SettingsContent;        // What the settings hold: node layout, the selection and the view; leave Nodes out when the application keeps node layout itself
    void*                   UserPointer;            // The application's; the editor doesn't use it
    ImVector<float>         CustomZoomLevels;
    CanvasSizeModeAlias     CanvasSizeMode;
    int                     DragButtonIndex;        // Mouse button index drag action will react to (0-left, 1-right, 2-middle)
    int                     SelectButtonIndex;      // Mouse button index select action will react to (0-left, 1-right, 2-middle)
    int                     NavigateButtonIndex;    // Mouse button index navigate action will react to (0-left, 1-right, 2-middle)
    int                     ContextMenuButtonIndex; // Mouse button index context menu action will react to (0-left, 1-right, 2-middle)
    bool                    EnableSmoothZoom;
    float                   SmoothZoomPower;
    bool                    ReportNavigationEdits;  // Edits report navigation too, once the view settles

    Config()
        : SettingsName("NodeEditor")
        , SettingsContent(SettingsContentFlags::All)
        , UserPointer(nullptr)
        , CustomZoomLevels()
        , CanvasSizeMode(CanvasSizeModeAlias::FitVerticalView)
        , DragButtonIndex(0)
        , SelectButtonIndex(0)
        , NavigateButtonIndex(1)
        , ContextMenuButtonIndex(1)
        , EnableSmoothZoom(true)
# ifdef __APPLE__
        , SmoothZoomPower(1.1f)
# else
        , SmoothZoomPower(1.3f)
# endif
        , ReportNavigationEdits(false)
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

// Flags and booleans are floats, the type style variables push, and HoverNodeBorderOffset
// is pushed as StyleVar_HoveredNodeBorderOffset; both are kept for compatibility.
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
    float   PinCorners;               // ImDrawFlags
    float   PinRadius;
    float   PinArrowSize;
    float   PinArrowWidth;
    float   GroupRounding;
    float   GroupBorderWidth;
    float   HighlightConnectedLinks;  // a boolean: non-zero highlights the links of a selected node
    float   SnapLinkToPinDir;         // a boolean: non-zero starts links on the line defined by pin direction
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
        PinCorners               = 0.0f; // ImDrawFlags without corner flags round all corners
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

// Keeps the state of editors with a Config::SettingsName in imgui.ini, next to
// ImGui's windows. Call once per ImGui context, before ImGui reads .ini data:
// before the first NewFrame() when io.IniFilename is set, otherwise before
// LoadIniSettingsFromMemory(). An editor created at any time starts from the
// latest state of its section. nullptr: the current ImGui context.
IMGUI_NODE_EDITOR_API void InstallSettingsHandler(ImGuiContext* context = nullptr);

// Settings the application stores itself, with or without the settings handler.
// The editor doesn't save on its own: once WantSaveSettings() is true, store
// SaveSettingsToMemory() and call ClearWantSaveSettings().
IMGUI_NODE_EDITOR_API void            LoadSettingsFromMemory(const char* data, size_t size = 0); // before the first Begin(), takes precedence over imgui.ini; later, replaces the state it holds; size 0: zero-terminated
IMGUI_NODE_EDITOR_API const char*     SaveSettingsToMemory(size_t* outSize = nullptr);           // zero-terminated, valid until the next call
IMGUI_NODE_EDITOR_API bool            WantSaveSettings();                                        // settings changed, and io.IniSavingRate elapsed since
IMGUI_NODE_EDITOR_API void            ClearWantSaveSettings();
IMGUI_NODE_EDITOR_API SaveReasonFlags GetSettingsChangeReason();                                 // what changed since the last ClearWantSaveSettings()

IMGUI_NODE_EDITOR_API Style& GetStyle();
IMGUI_NODE_EDITOR_API const char* GetStyleColorName(StyleColor colorIndex);

IMGUI_NODE_EDITOR_API void PushStyleColor(StyleColor colorIndex, const ImVec4& color);
IMGUI_NODE_EDITOR_API void PopStyleColor(int count = 1);

IMGUI_NODE_EDITOR_API void PushStyleVar(StyleVar varIndex, float value);
IMGUI_NODE_EDITOR_API void PushStyleVar(StyleVar varIndex, const ImVec2& value);
IMGUI_NODE_EDITOR_API void PushStyleVar(StyleVar varIndex, const ImVec4& value);
IMGUI_NODE_EDITOR_API void PopStyleVar(int count = 1);

// Call inside a visible host window. While the host skips items (collapsed,
// a hidden dock tab, a child window out of view) the editor measures nothing
// and changes no state, so calling it then is harmless.
IMGUI_NODE_EDITOR_API void Begin(const char* id, const ImVec2& size = ImVec2(0, 0));
IMGUI_NODE_EDITOR_API void End();

// Node, pin and link IDs are separate kinds, and 0 isn't valid for any of them.
// Each node pushes an ImGui ID scope, so widgets in different nodes may share
// labels. A pin ID can be submitted once per frame.
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

IMGUI_NODE_EDITOR_API bool BeginGroupHint(NodeId nodeId); // outside of BeginNode()/EndNode()
IMGUI_NODE_EDITOR_API ImVec2 GetGroupMin();
IMGUI_NODE_EDITOR_API ImVec2 GetGroupMax();
IMGUI_NODE_EDITOR_API ImDrawList* GetHintForegroundDrawList();
IMGUI_NODE_EDITOR_API ImDrawList* GetHintBackgroundDrawList();
IMGUI_NODE_EDITOR_API void EndGroupHint();

// TODO: Add a way to manage node background channels
IMGUI_NODE_EDITOR_API ImDrawList* GetNodeBackgroundDrawList(NodeId nodeId); // available after EndNode(); nullptr outside Begin()/End(), and for a node not submitted in this frame

IMGUI_NODE_EDITOR_API bool Link(LinkId id, PinId startPinId, PinId endPinId, const ImVec4& color = ImVec4(1, 1, 1, 1), float thickness = 1.0f);

IMGUI_NODE_EDITOR_API void Flow(LinkId linkId, FlowDirection direction = FlowDirection::Forward);

// Call EndCreate(), EndDelete() and EndShortcut() only when the matching Begin
// function returned true, as with Dear ImGui's BeginXXX()/EndXXX() pairs.
IMGUI_NODE_EDITOR_API bool BeginCreate(const ImVec4& color = ImVec4(1, 1, 1, 1), float thickness = 1.0f);
// startId is the pin the drag started from and endId the one it is over, of either kind.
IMGUI_NODE_EDITOR_API bool QueryNewLink(PinId* startId, PinId* endId);
IMGUI_NODE_EDITOR_API bool QueryNewLink(PinId* startId, PinId* endId, const ImVec4& color, float thickness = 1.0f);
IMGUI_NODE_EDITOR_API bool QueryNewNode(PinId* pinId);
IMGUI_NODE_EDITOR_API bool QueryNewNode(PinId* pinId, const ImVec4& color, float thickness = 1.0f);
IMGUI_NODE_EDITOR_API bool AcceptNewItem();
IMGUI_NODE_EDITOR_API bool AcceptNewItem(const ImVec4& color, float thickness = 1.0f);
IMGUI_NODE_EDITOR_API void RejectNewItem();
IMGUI_NODE_EDITOR_API void RejectNewItem(const ImVec4& color, float thickness = 1.0f);
IMGUI_NODE_EDITOR_API void EndCreate();

// Links attached to a node deleted with its dependencies are deleted with it.
// QueryDeletedLink() reports each of them, with the pins it connected: in the
// same interaction when queried after the node, otherwise in one more
// interaction in the next frame. They are already deleted: AcceptDeletedItem()
// only acknowledges them, and RejectDeletedItem() asserts.
IMGUI_NODE_EDITOR_API bool BeginDelete();
IMGUI_NODE_EDITOR_API bool QueryDeletedLink(LinkId* linkId, PinId* startId = nullptr, PinId* endId = nullptr);
IMGUI_NODE_EDITOR_API bool QueryDeletedNode(NodeId* nodeId);
IMGUI_NODE_EDITOR_API bool AcceptDeletedItem(bool deleteDependencies = true);
IMGUI_NODE_EDITOR_API void RejectDeletedItem();
IMGUI_NODE_EDITOR_API void EndDelete();

// Nodes are kept inside the canvas border, 250000 from the origin.
IMGUI_NODE_EDITOR_API void SetNodePosition(NodeId nodeId, const ImVec2& editorPosition);
IMGUI_NODE_EDITOR_API void SetGroupSize(NodeId nodeId, const ImVec2& size);
IMGUI_NODE_EDITOR_API ImVec2 GetNodePosition(NodeId nodeId);
IMGUI_NODE_EDITOR_API ImVec2 GetNodeSize(NodeId nodeId);
IMGUI_NODE_EDITOR_API void CenterNodeOnScreen(NodeId nodeId);
IMGUI_NODE_EDITOR_API void SetNodeZPosition(NodeId nodeId, float z); // Sets node z position, nodes with higher value are drawn over nodes with lower value; saved with the node's settings
IMGUI_NODE_EDITOR_API float GetNodeZPosition(NodeId nodeId); // Returns node z position, defaults is 0.0f

// Leaves the editor's canvas space, inside a node too. Begin popups, tooltips and
// child windows between Suspend() and Resume(): the canvas doesn't transform them.
IMGUI_NODE_EDITOR_API void Suspend();
IMGUI_NODE_EDITOR_API void Resume();
IMGUI_NODE_EDITOR_API bool IsSuspended();

IMGUI_NODE_EDITOR_API bool IsFocused(); // Whether the window the editor is in had focus in Begin()
IMGUI_NODE_EDITOR_API bool IsActive();  // Same as IsFocused()

IMGUI_NODE_EDITOR_API bool HasSelectionChanged(); // Selection changed in the last frame, also when asked between Begin() and End()
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

// Forgets the node, its pins, the links to them, its saved state and its selection.
// A node submitted later with the same ID starts as a new one. Call it outside
// Begin()/End().
// Returns false if the editor doesn't know the node.
IMGUI_NODE_EDITOR_API bool ForgetNode(NodeId nodeId);

// Forgets all nodes, pins and links with their saved state, the selection, the view
// and the edits not read yet, as for a new document. Call it outside Begin()/End().
IMGUI_NODE_EDITOR_API void ClearState();

// Typed state, for applications that keep node layout in their documents. The Set
// functions return whether anything changed, and stop a drag or resize of the nodes
// they change, putting those nodes back where the user found them.
IMGUI_NODE_EDITOR_API bool      GetNodeState(NodeId nodeId, NodeState* state);  // false for a node the editor doesn't know
IMGUI_NODE_EDITOR_API int       GetNodeStates(NodeState* states, int capacity); // known nodes by ID, also those only loaded; returns how many it wrote, or all of them for nullptr
IMGUI_NODE_EDITOR_API bool      SetNodeState(const NodeState& state);           // false for state that isn't finite, and for a node being deleted; keeps the node inside the canvas border; a node the editor doesn't know starts with the state when submitted
IMGUI_NODE_EDITOR_API bool      SetNodeStates(const NodeState* states, int count);
IMGUI_NODE_EDITOR_API ViewState GetViewState();                                 // before the first visible frame, the view the editor will restore
IMGUI_NODE_EDITOR_API bool      SetViewState(const ViewState& state, float duration = 0.0f); // keeps the zoom within the zoom levels
IMGUI_NODE_EDITOR_API bool      SetSelection(const NodeId* nodes, int nodeCount, const LinkId* links, int linkCount); // skips unknown objects and objects being deleted; links only without nodes; before the first frame, also nodes not created yet

// A node's state as text, in the format 0.9's Config::SaveNodeSettings gave, for
// documents that keep node layout as text. The text has no node ID.
IMGUI_NODE_EDITOR_API const char* FormatNodeState(const NodeState& state, size_t* outSize = nullptr); // zero-terminated, valid until the next call
IMGUI_NODE_EDITOR_API bool        ParseNodeState(const char* data, size_t size, NodeState* state);   // also what 0.9 wrote; leaves state->Id alone; false for text that doesn't parse, changing nothing; size 0: zero-terminated

// Edits the user completed in the last frame, in order: a drag or resize once it ends,
// a selection change, and navigation once the view settles. Changes the application
// makes aren't reported. Call EndEdits() only when BeginEdits() returned true, and
// before End() when inside Begin()/End(). The edit's data stays valid until then.
IMGUI_NODE_EDITOR_API bool BeginEdits();
IMGUI_NODE_EDITOR_API bool QueryEdit(Edit* edit);
IMGUI_NODE_EDITOR_API void EndEdits();

// These see the links submitted so far: between Begin() and End(), call them after
// the frame's links. PinHadAnyLinks() answers for the previous frame.
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

// The shortcuts don't fire while another ImGui item, such as a text field, is
// active. With io.ConfigMacOSXBehaviors, Cmd takes Ctrl's place in them and in
// Ctrl+click selection.
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

// Zoom factor of the view: 2 at 200%, as in Config::CustomZoomLevels.
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

IMGUI_NODE_EDITOR_API bool GetLinkPins(LinkId linkId, PinId* startPinId, PinId* endPinId); // pass nullptr if particular pin do not interest you; false for an unknown link and a link whose pins were deleted

IMGUI_NODE_EDITOR_API bool PinHadAnyLinks(PinId pinId); // Whether the pin had any link connected in the previous frame

IMGUI_NODE_EDITOR_API ImVec2 GetScreenSize();
IMGUI_NODE_EDITOR_API ImVec2 ScreenToCanvas(const ImVec2& pos);
IMGUI_NODE_EDITOR_API ImVec2 CanvasToScreen(const ImVec2& pos);

IMGUI_NODE_EDITOR_API int GetNodeCount();                                // Returns number of submitted nodes since Begin() call
IMGUI_NODE_EDITOR_API int GetOrderedNodeIds(NodeId* nodes, int size);    // Fills an array with node id's in order they're drawn; up to 'size` elements are set. Returns actual size of filled id's, or the number of nodes when 'nodes' is null. Between Begin() and End(), only nodes submitted so far.







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

    SafeType& operator=(const SafeType&) = default;

    explicit operator T() const { return Get(); }

    T Get() const { return m_Value; }

private:
    T m_Value;
};


template <typename Tag>
struct SafePointerType
    : SafeType<std::uintptr_t, Tag>
{
    static const Tag Invalid;

    using SafeType<std::uintptr_t, Tag>::SafeType;

    SafePointerType()
        : SafePointerType(Invalid)
    {
    }

    template <typename T = void> explicit SafePointerType(T* ptr): SafePointerType(reinterpret_cast<std::uintptr_t>(ptr)) {}
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
struct NodeState
{
    NodeId Id;
    ImVec2 Position;          // top left corner, in canvas coordinates
    ImVec2 GroupSize;         // (0, 0) for a node that isn't a group
    float  ZPosition = 0.0f;
};

// The visible part of the canvas, in canvas coordinates. The zoom follows from it
// and the editor's size.
struct ViewState
{
    ImVec2 VisibleMin;
    ImVec2 VisibleMax;
};

struct SelectionState
{
    const NodeId* Nodes     = nullptr;
    int           NodeCount = 0;
    const LinkId* Links     = nullptr;
    int           LinkCount = 0;
};

struct Edit
{
    EditType         Type = EditType::Move;
    const NodeState* NodesBefore = nullptr; // Move and Resize: the nodes the edit changed, in the same order before and after it
    const NodeState* NodesAfter  = nullptr;
    int              NodeCount   = 0;
    SelectionState   SelectionBefore;       // Select
    SelectionState   SelectionAfter;
    ViewState        ViewBefore;            // Navigate
    ViewState        ViewAfter;
};


//------------------------------------------------------------------------------
} // namespace NodeEditor
} // namespace ax


//------------------------------------------------------------------------------
# endif // IMGUI_NODE_EDITOR_H
