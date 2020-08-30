#pragma once
#include <imgui.h>
#include "crude_blueprint.h"
#include "imgui_extras.h"

namespace crude_blueprint_utilities {

using namespace crude_blueprint;

ImEx::IconType PinTypeToIconType(PinType pinType);
ImVec4 PinTypeToIconColor(PinType pinType);
bool DrawPinValue(const PinValue& value);
bool EditPinImmediateValue(Pin& pin);
void EditOrDrawPinValue(Pin& pin);

struct PinValueBackgroundRenderer
{
    PinValueBackgroundRenderer();
    PinValueBackgroundRenderer(const ImVec4 color, float alpha = 0.25f);
    ~PinValueBackgroundRenderer();

    void Commit();
    void Discard();

private:
    ImDrawList* m_DrawList = nullptr;
    ImDrawListSplitter m_Splitter;
    ImVec4 m_Color;
    float m_Alpha = 1.0f;
};

struct DebugOverlay:
    private ContextMonitor
{
    DebugOverlay(Blueprint& blueprint);
    ~DebugOverlay();

    void Begin();
    void End();

    void DrawNode(const Node& node);
    void DrawInputPin(const Pin& pin);
    void DrawOutputPin(const Pin& pin);

private:
    void OnEvaluatePin(const Context& context, const Pin& pin) override;

    Blueprint* m_Blueprint = nullptr;
    const Node* m_CurrentNode = nullptr;
    const Node* m_NextNode = nullptr;
    FlowPin m_CurrentFlowPin;
    ImDrawList* m_DrawList = nullptr;
    ImDrawListSplitter m_Splitter;
};

} // namespace crude_blueprint_utilities {