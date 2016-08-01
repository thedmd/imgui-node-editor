#pragma once
#include "imgui/imgui.h"

//------------------------------------------------------------------------------
namespace ax {
namespace Editor {


//------------------------------------------------------------------------------
struct Context;


//------------------------------------------------------------------------------
void SetCurrentEditor(Context* ctx);
Context* GetCurrentEditor();
Context* CreateEditor();
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

bool CreateLink(int* startId, int* endId, const ImColor& color = ImColor(255, 255, 255), float thickness = 1.0f);
void RejectLink(const ImVec4& color = ImVec4(1, 1, 1, 1), float thickness = 1.0f);
bool AcceptLink(const ImVec4& color = ImVec4(1, 1, 1, 1), float thickness = 1.0f);

bool DestroyLink();
int GetDestroyedLinkId();

bool Link(int id, int startPinId, int endPinId, const ImVec4& color = ImVec4(1, 1, 1, 1), float thickness = 1.0f);


//------------------------------------------------------------------------------
} // namespace Editor
} // namespace ax