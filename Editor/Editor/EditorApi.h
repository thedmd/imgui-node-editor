#pragma once
#include "imgui/imgui.h"

//------------------------------------------------------------------------------
namespace ax {
namespace Editor {


//------------------------------------------------------------------------------
typedef void        (*ConfigSaveSettings)(const char* data, void* userPointer);
typedef size_t      (*ConfigLoadSettings)(char* data, void* userPointer);

struct Config
{
    const char*             SettingsFile;
    ConfigSaveSettings      SaveSettings;
    ConfigLoadSettings      LoadSettings;
    void*                   UserPointer;

    Config():
        SettingsFile("NodeEditor.json"),
        SaveSettings(nullptr),
        LoadSettings(nullptr),
        UserPointer(nullptr)
    {
    }
};


//------------------------------------------------------------------------------
enum class PinKind
{
    Target,
    Source
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
    StyleColor_HovPinRect,
    StyleColor_HovPinRectBorder,

    StyleColor_Count
};

struct Style
{
    ImVec2  NodePadding;
    float   NodeRounding;
    float   NodeBorderWidth;
    float   HoveredNodeBorderWidth;
    float   SelectedNodeBorderWidth;
    float   HoveredPinRounding;
    float   HoveredPinBorderWidth;
    float   LinkStrength;
    ImVec4  Colors[StyleColor_Count];

    Style()
    {
        NodePadding             = ImVec2(8, 8);
        NodeRounding            = 12.0f;
        NodeBorderWidth         = 1.5f;
        HoveredNodeBorderWidth  = 3.5f;
        SelectedNodeBorderWidth = 3.5f;
        HoveredPinRounding      = 4.0f;
        HoveredPinBorderWidth   = 0.0f;
        LinkStrength            = 100.0f;

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
        Colors[StyleColor_HovPinRect]         = ImColor( 60, 180, 255, 100);
        Colors[StyleColor_HovPinRectBorder]   = ImColor( 60, 180, 255, 128);

    }
};


//------------------------------------------------------------------------------
struct Context;


//------------------------------------------------------------------------------
void SetCurrentEditor(Context* ctx);
Context* GetCurrentEditor();
Context* CreateEditor(const Config* config = nullptr);
void DestroyEditor(Context* ctx);

Style& GetStyle();
const char* GetStyleColorName(StyleColor colorIndex);

void Begin(const char* id, const ImVec2& size = ImVec2(0, 0));
void End();

void BeginNode2(int id);
void NodeHeader2(const ImVec4& color = ImVec4(1, 1, 1, 1));
void BeginPin2(int id, PinKind kind, const ImVec2& pivot = ImVec2(0.5f, 0.5f));
void EndPin2();
void EndNode2();

void BeginNode(int id);
void EndNode();

void BeginHeader(const ImVec4& color = ImVec4(1, 1, 1, 1));
void EndHeader();

void BeginInput(int id);
void EndInput();

void BeginOutput(int id);
void EndOutput();

bool Link(int id, int startPinId, int endPinId, const ImVec4& color = ImVec4(1, 1, 1, 1), float thickness = 1.0f);

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
bool QueryDeletedLink(int* linkId);
bool QueryDeletedNode(int* nodeId);
bool AcceptDeletedItem();
void RejectDeletedItem();
void EndDelete();

void SetNodePosition(int nodeId, const ImVec2& editorPosition);
ImVec2 GetNodePosition(int nodeId);

void Suspend();
void Resume();

bool HasSelectionChanged();
int  GetSelectedObjectCount();
int  GetSelectedNodes(int* nodes, int size);
int  GetSelectedLinks(int* links, int size);
void ClearSelection();
void SelectNode(int nodeId, bool append = false);
void SelectLink(int linkId, bool append = false);
void DeselectNode(int nodeId);
void DeselectLink(int linkId);

//------------------------------------------------------------------------------
} // namespace Editor
} // namespace ax