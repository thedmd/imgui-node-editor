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
struct Context;


//------------------------------------------------------------------------------
void SetCurrentEditor(Context* ctx);
Context* GetCurrentEditor();
Context* CreateEditor(const Config* config = nullptr);
void DestroyEditor(Context* ctx);

void Begin(const char* id);
void End();

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

void SetNodePosition(int nodeId, const ImVec2& screenPosition);
ImVec2 GetNodePosition(int nodeId);

void Suspend();
void Resume();

//------------------------------------------------------------------------------
} // namespace Editor
} // namespace ax