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

bool Link(int id, int startPinId, int endPinId, const ImVec4& color = ImVec4(1, 1, 1, 1), float thickness = 1.0f);

bool BeginCreate(const ImVec4& color = ImVec4(1, 1, 1, 1), float thickness = 1.0f);
void EndCreate();

bool QueryLink(int* startId, int* endId);
bool QueryLink(int* startId, int* endId, const ImVec4& color, float thickness = 1.0f);
bool QueryNode(int* pinId);
bool QueryNode(int* pinId, const ImVec4& color, float thickness = 1.0f);
bool AcceptItem();
bool AcceptItem(const ImVec4& color, float thickness = 1.0f);
void RejectItem();
void RejectItem(const ImVec4& color, float thickness = 1.0f);

bool DestroyLink();
int GetDestroyedLinkId();

void SetNodePosition(int nodeId, const ImVec2& screenPosition);

//------------------------------------------------------------------------------
} // namespace Editor
} // namespace ax