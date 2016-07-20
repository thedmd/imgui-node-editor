#pragma once

#include "imgui/imgui.h"

namespace ax {
namespace NodeEditor {

struct Context;

enum class IconType { Flow, Circle, Square, Grid, RoundSquare };

bool Icon(const char* id, const ImVec2& size, IconType type, bool filled, const ImVec4& color = ImVec4(1, 1, 1, 1));

void Spring(float weight = 1.0f);

void SetCurrentEditor(Context* ctx);
Context* GetCurrentEditor();
Context* CreateEditor();
void DestroyEditor(Context* ctx);

void Begin(const char* id);
void End();

void BeginNode(int id);
void EndNode();

void BeginHeader();
void EndHeader();

void BeginInput(int id);
void EndInput();

void BeginOutput(int id);
void EndOutput();

void Link(int id, int startNodeId, int endNodeId, const ImVec4& color = ImVec4(1, 1, 1, 1));

} // namespace node_editor
} // namespace ax