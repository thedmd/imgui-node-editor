#pragma once
#include <imgui.h>
#include <imgui_node_editor.h>
#include "crude_blueprint.h"
#include "imgui_extras.h"
#include <inttypes.h>

# define PRI_pin_fmt      "Pin %" PRIu32 "%s%*s%s"
# define PRI_node_fmt     "Node %" PRIu32 "%s%*s%s"
# define LOG_pin(pin)     (pin)->m_Id, (pin)->m_Name.empty() ? "" : " \"", static_cast<int>((pin)->m_Name.size()), (pin)->m_Name.data(), (pin)->m_Name.empty() ? "" : "\""
# define LOG_node(node)   (node)->m_Id, (node)->GetName().empty() ? "" : " \"", static_cast<int>((node)->GetName().size()), (node)->GetName().data(), (node)->GetName().empty() ? "" : "\""

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

enum class LogLevel: int32_t
{
    Verbose,
    Info,
    Warning,
    Error,
};

struct OverlayLogger
{
    void Log(LogLevel level, const char* format, ...) IM_FMTARGS(3);

    void Update(float dt);
    void Draw(const ImVec2& a, const ImVec2& b);

private:
    struct Range
    {
        int     m_Start = 0;
        int     m_Size  = 0;
        ImColor m_Color;
    };

    struct Entry
    {
        LogLevel        m_Level     = LogLevel::Verbose;
        time_t          m_Timestamp = 0;
        string          m_Buffer;
        float           m_Timer     = 0.0f;
        bool            m_IsPinned  = false;
        vector<Range>   m_ColorRanges;
    };

    void TintVertices(ImDrawList* drawList, int firstVertexIndex, ImColor color, float alpha, int rangeStart, int rangeSize);

    vector<Range> ParseMessage(string_view message) const;

    float           m_OutlineSize                 = 0.5f;
    float           m_Padding                     = 10.0f;
    float           m_MessagePresentationDuration = 5.0f;
    float           m_MessageFadeOutDuration      = 0.5f;
    float           m_MessageLifeDuration         = m_MessagePresentationDuration + m_MessageFadeOutDuration;
    bool            m_HoldTimer                   = false;
    vector<Entry>   m_Entries;
    ImColor         m_HighlightBorder             = ImColor(  5, 130, 255, 128);
    ImColor         m_HighlightFill               = ImColor(  5, 130, 255,  64);
    ImColor         m_PinBorder                   = ImColor(255, 176,  50,   0);
    ImColor         m_PinFill                     = ImColor(  0,  75, 150, 128);

    ImColor         m_LogTimeColor                = IM_COL32(0x96, 0xD1, 0x00, 0xFF);
    ImColor         m_LogSymbolColor              = IM_COL32(0xC0, 0xC0, 0xC0, 0xFF);
    ImColor         m_LogStringColor              = IM_COL32(0xFF, 0x80, 0x40, 0xFF);
    ImColor         m_LogTagColor                 = IM_COL32(0xFF, 0xD6, 0x8F, 0xFF);
    ImColor         m_LogNumberColor              = IM_COL32(0xFF, 0xFF, 0x80, 0xFF);
    ImColor         m_LogVerboseColor             = IM_COL32(0x80, 0xFF, 0x80, 0xFF);
    ImColor         m_LogWarningColor             = IM_COL32(0xFF, 0xFF, 0x80, 0xFF);
    ImColor         m_LogErrorColor               = IM_COL32(0xFF, 0x70, 0x4D, 0xFF);
    ImColor         m_LogInfoColor                = IM_COL32(0x8A, 0xC5, 0xFF, 0xFF);
    ImColor         m_LogAssertColor              = IM_COL32(0xFF, 0x3D, 0x44, 0xFF);
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
    bool Show(Blueprint& blueprint);

          Node* GetCreatedNode()       { return m_CreatedNode; }
    const Node* GetCreatedNode() const { return m_CreatedNode; }

    span<      Pin*>       GetCreatedLinks()       { return m_CreatedLinks; }
    span<const Pin* const> GetCreatedLinks() const { return make_span(const_cast<const Pin* const*>(m_CreatedLinks.data()), m_CreatedLinks.size()); }

private:
    bool CreateLinkToFirstMatchingPin(Node& node, Pin& fromPin);

    Node*           m_CreatedNode = nullptr;
    vector<Pin*>    m_CreatedLinks;

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