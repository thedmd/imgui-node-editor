#pragma once
#include <imgui.h>
#include <imgui_node_editor.h>
#include "crude_blueprint.h"
#include "imgui_extras.h"
#include <inttypes.h>

# define PRI_sv     "%*s"
# define LOG_sv(sv) static_cast<int>((sv).size()), (sv).data()

# define PRI_pin_fmt      "Pin %" PRIu32 "%s" PRI_sv "%s"
# define PRI_node_fmt     "Node %" PRIu32 "%s" PRI_sv "%s"
# define LOG_pin(pin)     (pin)->m_Id, (pin)->m_Name.empty() ? "" : " \"", LOG_sv((pin)->m_Name), (pin)->m_Name.empty() ? "" : "\""
# define LOG_node(node)   (node)->m_Id, (node)->GetName().empty() ? "" : " \"", LOG_sv((node)->GetName()), (node)->GetName().empty() ? "" : "\""


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

    void AddKeyword(string_view keyword);
    void RemoveKeyword(string_view keyword);

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
        string          m_Text;
        float           m_Timer     = 0.0f;
        bool            m_IsPinned  = false;
        vector<Range>   m_ColorRanges;
    };

    ImColor GetLevelColor(LogLevel level) const;

    void TintVertices(ImDrawList* drawList, int firstVertexIndex, ImColor color, float alpha, int rangeStart, int rangeSize);

    vector<Range> ParseMessage(LogLevel level, string_view message) const;


    float           m_OutlineSize                 = 0.5f;
    float           m_Padding                     = 10.0f;
    float           m_MessagePresentationDuration = 5.0f;
    float           m_MessageFadeOutDuration      = 0.5f;
    float           m_MessageLifeDuration         = m_MessagePresentationDuration + m_MessageFadeOutDuration;
    bool            m_HoldTimer                   = false;
    vector<Entry>   m_Entries;
    vector<string>  m_Keywords;
    ImColor         m_HighlightBorder             = ImColor(  5, 130, 255, 128);
    ImColor         m_HighlightFill               = ImColor(  5, 130, 255,  64);
    ImColor         m_PinBorder                   = ImColor(255, 176,  50,   0);
    ImColor         m_PinFill                     = ImColor(  0,  75, 150, 128);

    ImColor         m_LogTimeColor                = ImColor(150, 209,   0, 255);
    ImColor         m_LogSymbolColor              = ImColor(192, 192, 192, 255);
    ImColor         m_LogStringColor              = ImColor(255, 174, 133, 255);
    ImColor         m_LogTagColor                 = ImColor(255, 214, 143, 255);
    ImColor         m_LogKeywordColor             = ImColor(255, 255, 255, 255);
    ImColor         m_LogTextColor                = ImColor(192, 192, 192, 255);
    ImColor         m_LogOutlineColor             = ImColor(  0,   0,   0, 255);
    ImColor         m_LogNumberColor              = ImColor(255, 255, 128, 255);
    ImColor         m_LogVerboseColor             = ImColor(128, 255, 128, 255);
    ImColor         m_LogWarningColor             = ImColor(255, 255, 192, 255);
    ImColor         m_LogErrorColor               = ImColor(255, 152, 152, 255);
    ImColor         m_LogInfoColor                = ImColor(138, 197, 255, 255);
    ImColor         m_LogAssertColor              = ImColor(255,  61,  68, 255);
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