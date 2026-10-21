#pragma once
#include <imgui.h>
#include <imgui_node_editor.h>
#include "crude_blueprint.h"
#include "blueprint_editor_icons.h"
#include <chrono>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace blueprint_editor {
struct Document;
} // namespace blueprint_editor {

namespace blueprint_editor_utilities {

using namespace blueprint_editor;
using namespace crude_blueprint;

IconType PinIcon(const Pin& pin);     // by its type's kind; a diamond while undecided
ImVec4   PinColor(const Pin& pin);    // white for flow, grey while undecided, else its type's
ImVec4   TypeColor(const Type* type);

// What an input without a link reads: the value the user typed, the default its node declares,
// or its type's. None while its type is undecided.
Value InputValue(const Graph& graph, const Pin& pin);

// While a pin's type is undecided, the value it keeps: one typed into an input without an active
// link, or a Constant's. None when nothing is kept.
Value KeptValue(const Graph& graph, const Pin& pin);

// A Constant holds its value in its config.
bool  IsConstant(const Node& node);
Value ConstantValue(const Node& node, const Pin& pin);

bool HasLinks(const Graph& graph, Id pin);              // active or disabled
bool HasValueEditor(const Graph& graph, const Pin& pin); // an input without an active link, or a Constant's output; of a decided type

// The node's type variable that is the pin's type, not one inside a type such as Array<T>.
const VariableDeclaration* PinVariable(const Graph& graph, const Pin& pin);

// The node's group a key is of, and the key's index there.
const GroupDeclaration* FindGroup(const Node& node, std::string_view key, int* index = nullptr);

// As the log and tooltips name it: "Add#7.in.0 (Int32)".
std::string DescribePin(const Graph& graph, const Pin& pin);

// Edits of one undo step each. One the graph refuses is logged, and makes no step.
void CreateLink(Document& document, Id from, Id to, LinkMode mode = LinkMode::Plain);
void AddGroupPin(Document& document, Id node, std::string group);
void RemoveGroupPin(Document& document, Id node, std::string group, int index);

ImU32 ProblemColor(Severity severity);
void  ProblemPills(int errors, int warnings); // a red disc and a yellow triangle, with their counts

bool DrawValue(const Value& value);        // Draws a value; false for none.
bool EditValue(Value& value);              // Shows an editor for a value; true once the edit is done.
bool DrawValueWithEditor(Id id, Value& value); // Draws a value, or its editor once clicked; true when an edit changed it.

std::vector<Id> GetSelectedNodes();
std::vector<Id> GetSelectedLinks();

// The example's own interactions on the canvas, declared each frame in priority order, and listed
// in its bottom-right corner, with those that don't apply now dimmed. A press fires through
// ImGui::Shortcut(): only the first active one whose chord is pressed, and only over the canvas
// unless it's global. A gesture is the node editor's or a widget's, and only listed.
struct CanvasActions
{
    explicit CanvasActions(bool overCanvas): m_OverCanvas(overCanvas) {}

    bool Press(const char* name, ImGuiKeyChord chord, bool active, bool global = false);
    void Gesture(const char* name, const char* gesture, bool active);

    void Draw(ImVec2 canvasMin, ImVec2 canvasMax) const;

private:
    struct Entry
    {
        std::string m_Text;
        bool        m_Active = false;
    };

    bool               m_OverCanvas = false;
    bool               m_Fired      = false;
    std::vector<Entry> m_Entries;
};

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

// A run of a document's blueprint: a context of its compiled program, and the fiber its Start
// began. Prints go to the log. Any step the document takes since stops the run.
struct Debugger
{
    // A blueprint with an error doesn't start: its diagnostics are logged.
    bool Start(const Document& document, bool running);
    void Step();
    void Stop();
    // Each frame: a run steps for a few milliseconds, so the window stays responsive.
    void Update(const Document& document);

    bool IsActive() const;  // a fiber that's ready
    bool IsRunning() const { return IsActive() && m_Running; }

    const Context* GetContext() const { return m_Context.get(); }
    Context*       GetContext()       { return m_Context.get(); }
    Id             CurrentNode() const { return m_Current; } // the node the last step ran
    Id             NextNode() const;
    Id             TakenLink() const; // the flow link the last step took

private:
    struct Monitor final : ContextMonitor
    {
        void OnLog(const Context& context, Id node, std::string_view text) override;
    };

    std::unique_ptr<Context> m_Context;
    Monitor                  m_Monitor;
    FiberId                  m_Fiber    = 0;
    Id                       m_Current  = 0;
    uint64_t                 m_Revision = 0;
    bool                     m_Running  = false;
};

// What's wrong with a document's blueprint: its graphs' problems and compiling's, for its revision,
// and the errors of a debugger's run. Each is a node's: a link's is its input's node's.
struct ProblemList
{
    struct Problem
    {
        Diagnostic m_Diagnostic;
        Id         m_Node = 0; // none for one of the graph itself
        bool       m_Run  = false;
    };

    void Update(const Document& document, const Debugger& debugger);

    std::span<const Problem> Problems() const { return m_Problems; }
    int  Count(Severity severity, Id node = 0) const; // of a node, or of all
    bool BlocksRun() const;                           // an error, before running

private:
    uint64_t             m_Revision = UINT64_MAX;
    std::vector<Problem> m_Compiled;
    std::vector<Problem> m_Problems;
};

// While a run is paused: a marker over the node the last step ran and over the one next, values
// beside pins, and the link the step took flowing.
struct DebugOverlay
{
    explicit DebugOverlay(Debugger& debugger);

    void Begin();
    void End();

    void DrawNode(Id node);
    void DrawInputPin(const Pin& pin, bool linked);
    void DrawOutputPin(const Pin& pin);

private:
    Context*           m_Context = nullptr;
    Id                 m_Current = 0;
    Id                 m_Next    = 0;
    Id                 m_Link    = 0;
    ImDrawList*        m_DrawList = nullptr;
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

        bool Accept(ImVec4 color = ImVec4(0.34f, 1.0f, 0.34f, 1.0f));
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

// Dialog for picking new node type. One opened from a pin links the new node to it, through the
// first of the node's pins that fits.
struct CreateNodeDialog
{
    void Open(Id fromPin = 0);
    void Show(Document& document);

    Id GetCreatedNode() const { return m_CreatedNode; }

private:
    Id                           m_FromPin     = 0;
    Id                           m_CreatedNode = 0;
    std::vector<const NodeType*> m_SortedTypes;
};

struct NodeContextMenu
{
    void Open(Id node = 0);
    void Show(Document& document);

private:
    Id m_Node = 0;
};

// For a pin of a type variable, its type: chosen, or reset to what links decide.
struct PinContextMenu
{
    void Open(Id pin = 0);
    void Show(Document& document);

private:
    Id m_Pin = 0;
};

struct LinkContextMenu
{
    void Open(Id link = 0);
    void Show(Document& document);

private:
    Id m_Link = 0;
};

// For a link dropped on a pin it fits only through a cast, or forced: why it doesn't fit as it is,
// and the ways it does.
struct LinkDropMenu
{
    void Open(Id from, Id to, std::string reason, bool cast, std::optional<int> forced);
    void Show(Document& document);

private:
    Id                 m_From = 0;
    Id                 m_To   = 0;
    std::string        m_Reason;
    bool               m_Cast = false;
    std::optional<int> m_Forced; // the links forcing it disables
};

} // namespace blueprint_editor_utilities {
