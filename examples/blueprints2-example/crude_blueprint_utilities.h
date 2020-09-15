#pragma once
#include <imgui.h>
#include <imgui_node_editor.h>
#include "crude_blueprint.h"
#include "imgui_extras.h"

namespace crude_blueprint_utilities {

using namespace crude_blueprint;

ImEx::IconType PinTypeToIconType(PinType pinType); // Returns icon for corresponding pin type.
ImVec4 PinTypeToColor(PinType pinType); // Returns color for corresponding pin type.
bool DrawPinValue(const PinValue& value); // Draw widget representing pin value.
bool EditPinValue(Pin& pin); // Show editor for pin. Returns true if edit is complete.
void DrawPinValueWithEditor(Pin& pin); // Draw pin value or editor if value is clicked.
const vector<Node*> GetSelectedNodes(Blueprint& blueprint); // Returns selected nodes as a vector.
const vector<Pin*> GetSelectedLinks(Blueprint& blueprint); // Returns selected links as a vector.

// Uses ImDrawListSplitter to draw background under pin value
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

// Show overlay while blueprint is executed presenting
// current state and execution point.
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

// Wrapper over flat API for item construction
struct ItemBuilder
{
    struct NodeBuilder
    {
        ax::NodeEditor::PinId m_PinId = ax::NodeEditor::PinId::Invalid;

        bool Accept();
        void Reject();
    };

    struct LinkBuilder
    {
        ax::NodeEditor::PinId m_StartPinId = ax::NodeEditor::PinId::Invalid;
        ax::NodeEditor::PinId m_EndPinId = ax::NodeEditor::PinId::Invalid;

        bool Accept();
        void Reject();
    };

    ItemBuilder();
    ~ItemBuilder();

    explicit operator bool() const;

    NodeBuilder* QueryNewNode();
    LinkBuilder* QueryNewLink();

private:
    bool m_InCreate = false;
    NodeBuilder m_NodeBuilder;
    LinkBuilder m_LinkBuilder;
};

// Wrapper over flat API for item deletion
struct ItemDeleter
{
    struct NodeDeleter
    {
        ax::NodeEditor::NodeId m_NodeId = ax::NodeEditor::NodeId::Invalid;

        bool Accept(bool deleteLinks = true);
        void Reject();
    };

    struct LinkDeleter
    {
        ax::NodeEditor::LinkId m_LinkId     = ax::NodeEditor::LinkId::Invalid;
        ax::NodeEditor::PinId  m_StartPinId = ax::NodeEditor::PinId::Invalid;
        ax::NodeEditor::PinId  m_EndPinId   = ax::NodeEditor::PinId::Invalid;

        bool Accept();
        void Reject();
    };

    ItemDeleter();
    ~ItemDeleter();

    explicit operator bool() const;

    NodeDeleter* QueryDeletedNode();
    LinkDeleter* QueryDeleteLink();

private:
    bool m_InDelete = false;
    NodeDeleter m_NodeDeleter;
    LinkDeleter m_LinkDeleter;
};

// Dialog for picking new node type
struct CreateNodeDialog
{
    void Open(Pin* fromPin = nullptr);
    void Show(Blueprint& blueprint);

private:
    bool CreateLinkToFirstMatchingPin(Node& node, Pin& fromPin);

    vector<const NodeTypeInfo*> m_SortedNodes;
};

struct NodeContextMenu
{
    void Open(Node* node = nullptr);
    void Show(Blueprint& blueprint);
};

struct PinContextMenu
{
    void Open(Pin* pin = nullptr);
    void Show(Blueprint& blueprint);
};

struct LinkContextMenu
{
    void Open(Pin* pin = nullptr);
    void Show(Blueprint& blueprint);
};

} // namespace crude_blueprint_utilities {