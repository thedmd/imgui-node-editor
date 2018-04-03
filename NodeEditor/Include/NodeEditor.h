//------------------------------------------------------------------------------
// LICENSE
//   This software is dual-licensed to the public domain and under the following
//   license: you are granted a perpetual, irrevocable license to copy, modify,
//   publish, and distribute this file as you see fit.
//
// CREDITS
//   Written by Michal Cichon
//------------------------------------------------------------------------------
# pragma once


//------------------------------------------------------------------------------
# include "imgui.h"


//------------------------------------------------------------------------------
namespace ax {
namespace NodeEditor {


//------------------------------------------------------------------------------
enum class SaveReasonFlags: int
{
    None       = 0x00000000,
    Navigation = 0x00000001,
    Position   = 0x00000002,
    Size       = 0x00000004,
    Selection  = 0x00000008,
    User       = 0x00000010
};

inline SaveReasonFlags operator |(SaveReasonFlags lhs, SaveReasonFlags rhs) { return static_cast<SaveReasonFlags>(static_cast<int>(lhs) | static_cast<int>(rhs)); }
inline SaveReasonFlags operator &(SaveReasonFlags lhs, SaveReasonFlags rhs) { return static_cast<SaveReasonFlags>(static_cast<int>(lhs) & static_cast<int>(rhs)); }

typedef bool        (*ConfigSaveSettings)(const char* data, size_t size, SaveReasonFlags reason, void* userPointer);
typedef size_t      (*ConfigLoadSettings)(char* data, void* userPointer);

typedef bool        (*ConfigSaveNodeSettings)(int nodeId, const char* data, size_t size, SaveReasonFlags reason, void* userPointer);
typedef size_t      (*ConfigLoadNodeSettings)(int nodeId, char* data, void* userPointer);

typedef void        (*ConfigSession)(void* userPointer);

struct Config
{
    const char*             SettingsFile;
    ConfigSession           BeginSaveSession;
    ConfigSession           EndSaveSession;
    ConfigSaveSettings      SaveSettings;
    ConfigLoadSettings      LoadSettings;
    ConfigSaveNodeSettings  SaveNodeSettings;
    ConfigLoadNodeSettings  LoadNodeSettings;
    void*                   UserPointer;

    Config():
        SettingsFile("NodeEditor.json"),
        BeginSaveSession(nullptr),
        EndSaveSession(nullptr),
        SaveSettings(nullptr),
        LoadSettings(nullptr),
        SaveNodeSettings(nullptr),
        LoadNodeSettings(nullptr),
        UserPointer(nullptr)
    {
    }
};


//------------------------------------------------------------------------------
enum class PinKind
{
    Input,
    Output
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
};

struct Style
{
    ImVec4  NodePadding;
    float   NodeRounding;
    float   NodeBorderWidth;
    float   HoveredNodeBorderWidth;
    float   SelectedNodeBorderWidth;
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
    ImVec4  Colors[StyleColor_Count];

    Style()
    {
        NodePadding             = ImVec4(8, 8, 8, 8);
        NodeRounding            = 12.0f;
        NodeBorderWidth         = 1.5f;
        HoveredNodeBorderWidth  = 3.5f;
        SelectedNodeBorderWidth = 3.5f;
        PinRounding             = 4.0f;
        PinBorderWidth          = 0.0f;
        LinkStrength            = 100.0f;
        SourceDirection         = ImVec2(1.0f, 0.0f);
        TargetDirection         = ImVec2(-1.0f, 0.0f);
        ScrollDuration          = 0.35f;
        FlowMarkerDistance      = 30.0f;
        FlowSpeed               = 150.0f;
        FlowDuration            = 2.0f;
        PivotAlignment          = ImVec2(0.5f, 0.5f);
        PivotSize               = ImVec2(-1, -1);
        PivotScale              = ImVec2(1, 1);
        PinCorners              = 15;
        PinRadius               = 0.0f;
        PinArrowSize            = 0.0f;
        PinArrowWidth           = 0.0f;
        GroupRounding           = 6.0f;
        GroupBorderWidth        = 1.0f;

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
void SetCurrentEditor(EditorContext* ctx);
EditorContext* GetCurrentEditor();
EditorContext* CreateEditor(const Config* config = nullptr);
void DestroyEditor(EditorContext* ctx);

Style& GetStyle();
const char* GetStyleColorName(StyleColor colorIndex);

void PushStyleColor(StyleColor colorIndex, const ImVec4& color);
void PopStyleColor(int count = 1);

void PushStyleVar(StyleVar varIndex, float value);
void PushStyleVar(StyleVar varIndex, const ImVec2& value);
void PushStyleVar(StyleVar varIndex, const ImVec4& value);
void PopStyleVar(int count = 1);

void Begin(const char* id, const ImVec2& size = ImVec2(0, 0));
void End();

void BeginNode(int id);
void BeginPin(int id, PinKind kind);
void PinRect(const ImVec2& a, const ImVec2& b);
void PinPivotRect(const ImVec2& a, const ImVec2& b);
void PinPivotSize(const ImVec2& size);
void PinPivotScale(const ImVec2& scale);
void PinPivotAlignment(const ImVec2& alignment);
void EndPin();
void Group(const ImVec2& size);
void EndNode();

bool BeginGroupHint(int nodeId);
ImVec2 GetGroupMin();
ImVec2 GetGroupMax();
ImDrawList* GetHintForegroundDrawList();
ImDrawList* GetHintBackgroundDrawList();
void EndGroupHint();

// TODO: Add a way to manage node background channels
ImDrawList* GetNodeBackgroundDrawList(int nodeId);

bool Link(int id, int startPinId, int endPinId, const ImVec4& color = ImVec4(1, 1, 1, 1), float thickness = 1.0f);

void Flow(int linkId);

bool BeginCreate(const ImVec4& color = ImVec4(1, 1, 1, 1), float thickness = 1.0f);
bool QueryNewLink(int* startId, int* endId);
bool QueryNewLink(int* startId, int* endId, const ImVec4& color, float thickness = 1.0f);
bool QueryNewNode(int* pinId);
bool QueryNewNode(int* pinId, const ImVec4& color, float thickness = 1.0f);
bool AcceptNewItem();
bool AcceptNewItem(const ImVec4& color, float thickness = 1.0f);
void RejectNewItem();
void RejectNewItem(const ImVec4& color, float thickness = 1.0f);
void EndCreate();

bool BeginDelete();
bool QueryDeletedLink(int* linkId, int* startId = nullptr, int* endId = nullptr);
bool QueryDeletedNode(int* nodeId);
bool AcceptDeletedItem();
void RejectDeletedItem();
void EndDelete();

void SetNodePosition(int nodeId, const ImVec2& editorPosition);
ImVec2 GetNodePosition(int nodeId);
ImVec2 GetNodeSize(int nodeId);
void CenterNodeOnScreen(int nodeId);

void RestoreNodeState(int nodeId);

void Suspend();
void Resume();
bool IsSuspended();

bool IsActive();

bool HasSelectionChanged();
int  GetSelectedObjectCount();
int  GetSelectedNodes(int* nodes, int size);
int  GetSelectedLinks(int* links, int size);
void ClearSelection();
void SelectNode(int nodeId, bool append = false);
void SelectLink(int linkId, bool append = false);
void DeselectNode(int nodeId);
void DeselectLink(int linkId);

bool DeleteNode(int nodeId);
bool DeleteLink(int linkId);

void NavigateToContent(float duration = -1);
void NavigateToSelection(bool zoomIn = false, float duration = -1);

bool ShowNodeContextMenu(int* nodeId);
bool ShowPinContextMenu(int* pinId);
bool ShowLinkContextMenu(int* linkId);
bool ShowBackgroundContextMenu();

void EnableShortcuts(bool enable);
bool AreShortcutsEnabled();

bool BeginShortcut();
bool AcceptCut();
bool AcceptCopy();
bool AcceptPaste();
bool AcceptDuplicate();
bool AcceptCreateNode();
int  GetActionContextSize();
int  GetActionContextNodes(int* nodes, int size);
int  GetActionContextLinks(int* links, int size);
void EndShortcut();

float GetCurrentZoom();

int GetDoubleClickedNode();
int GetDoubleClickedPin();
int GetDoubleClickedLink();
bool IsBackgroundClicked();
bool IsBackgroundDoubleClicked();

bool PinHadAnyLinks(int pinId);

ImVec2 GetScreenSize();
ImVec2 ScreenToCanvas(const ImVec2& pos);
ImVec2 CanvasToScreen(const ImVec2& pos);

//------------------------------------------------------------------------------
} // namespace Editor
} // namespace ax
