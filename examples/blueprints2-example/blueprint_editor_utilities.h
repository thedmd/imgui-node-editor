#pragma once
#include <imgui.h>
#include <imgui_node_editor.h>
#include "crude_blueprint.h"
#include "blueprint_editor_icons.h"
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
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

// The split pin a key is a field of, the innermost: "box.min" of "box.min.x"; empty for none.
std::string SplitOf(const Node& node, std::string_view key);

// As the log and tooltips name it: "Add#7.in.0 (Int32)".
std::string DescribePin(const Graph& graph, const Pin& pin);

// The graph a node is in, and the node, of all the blueprint's graphs.
const Graph* GraphOf(const Blueprint& blueprint, Id node);
const Node*  FindNode(const Blueprint& blueprint, Id node);

// Edits of one undo step each. One the graph refuses is logged, and makes no step.
void CreateLink(Document& document, Id from, Id to, LinkMode mode = LinkMode::Plain);
void AddGroupPin(Document& document, Id node, std::string group);
void RemoveGroupPin(Document& document, Id node, std::string group, int index);
void SplitStructPin(Document& document, Id node, std::string key, bool split);

// A function's entry, which gives its inputs, or a return, which takes its outputs; no function
// for other nodes.
struct SignatureNode
{
    const Function* m_Function = nullptr;
    bool            m_Entry    = false;
};
SignatureNode SignatureNodeOf(const Blueprint& blueprint, const Node& node);

// Edits of a function's signature, each one undo step. One the blueprint refuses is logged.
void ChangeSignature(Document& document, Id graph, std::string_view what, const std::function<void(Signature&)>& change);
void AddSignaturePin(Document& document, Id graph, bool input); // of a key of its own, an Int32
// A type parameter that a pin's type has can't go.
void RemoveTypeParameter(Document& document, Id graph, int index);
void RenameFunction(Document& document, Id graph, std::string name);
void RenameEvent(Document& document, Id node, std::string name); // a Custom Event's

// Edits of the blueprint's variables, one undo step each. One the blueprint refuses is logged.
void AddVariable(Document& document); // an Int32, named Variable1, or of the first number free
void ChangeVariable(Document& document, std::string name, Variable changed, std::string_view what);
void RemoveVariable(Document& document, std::string name); // with the nodes that use it

ImU32 ProblemColor(Severity severity);
bool  ProblemPills(int errors, int warnings); // a red disc and a yellow triangle, with their counts; true when clicked
void  ProblemSign(ImDrawList* drawList, ImVec2 center, float radius, Severity severity);

// After the title of a node that may suspend its fiber: a clock, filled while a fiber waits there,
// then what it waits for. True when hovered.
bool LatentMark(std::string_view waiting);

bool DrawValue(const Value& value);        // Draws a value; false for none.
bool EditValue(Value& value);              // Shows an editor for a value; true once the edit is done.
bool DrawValueWithEditor(Id id, Value& value); // Draws a value, or its editor once clicked; true when an edit changed it.

// A popup of a name's field, which opens while 'open' is set, its field focused, and clears it.
// Enter gives the name typed. Escape closes it, and so does losing what the name is of.
std::optional<std::string> NamePopup(const char* id, bool& open, std::span<char> name, bool exists);

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
// began. Prints go to the log. A step the document takes goes on in the run when it changed only
// values, and stops it otherwise. Breakpoints stay for the runs that follow. The run's clock moves
// on only while it runs.
struct Debugger
{
    // A blueprint with an error doesn't start: its diagnostics are logged. The event's parameters
    // get their types' defaults.
    bool Start(const Document& document, bool running, std::string_view event = "Start");
    void Continue(); // a paused run runs on
    void Step();     // a fiber that waits for its time skips the wait
    void Stop();
    // Each frame: a run steps for a few milliseconds, so the window stays responsive, and pauses
    // before a node with a breakpoint.
    void Update(const Document& document);
    void Advance(double seconds); // a running run's clock, by a frame's time
    void Signal(std::string_view signal);

    void SetBreakpoint(Id node, bool enabled);
    bool HasBreakpoint(Id node) const { return m_Breakpoints.contains(node); }
    void ClearBreakpoints();

    // The pins whose values the debugger lists, in the order they were added.
    void                SetWatched(Id pin, bool watched);
    bool                IsWatched(Id pin) const;
    std::span<const Id> Watched() const { return m_Watched; }

    bool IsActive() const; // a fiber that's ready or suspended
    bool IsRunning() const { return IsActive() && m_Running; }
    bool IsAtBreakpoint() const;
    std::optional<Wake> Waiting() const; // what the fiber waits for, while it's suspended

    // A pin's value: the run's while there is one, else as a run that hasn't started computes it,
    // until the document changes. An error for a blueprint with errors.
    Result<Watch> Evaluate(const Document& document, Id pin);

    const Context* GetContext() const { return m_Context.get(); }
    Context*       GetContext()       { return m_Context.get(); }
    uint64_t       StepCount() const  { return m_Context ? m_Context->StepCount() : 0; }
    Id             CurrentNode() const; // the node the last step ran
    Id             NextNode() const;
    Id             TakenLink() const;   // the flow link the last step took

private:
    struct Monitor final : ContextMonitor
    {
        void OnLog(const Context& context, Id node, std::string_view text) override;
    };

    std::optional<FiberInfo> Fiber() const;
    void                     Report(); // how the fiber ended, once

    std::unique_ptr<Context> m_Context;
    Monitor                  m_Monitor;
    std::set<Id>             m_Breakpoints;
    std::vector<Id>          m_Watched;
    FiberId                  m_Fiber    = 0;
    uint64_t                 m_Revision = 0;
    bool                     m_Running  = false;
    bool                     m_Reported = false;
    std::unique_ptr<Context> m_Preview;
    uint64_t                 m_PreviewRevision = UINT64_MAX;
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

    // As the editor shows it: a link's names its input pin, "text: a cast needs both types decided".
    static std::string Text(const Blueprint& blueprint, const Problem& problem);

    std::span<const Problem> Problems() const { return m_Problems; }
    int  Count(Severity severity, Id node = 0) const; // of a node, or of all
    bool BlocksRun() const;                           // an error, before running

private:
    uint64_t             m_Revision = UINT64_MAX;
    std::vector<Problem> m_Compiled;
    std::vector<Problem> m_Problems;
};

// While a run is paused, the link its last step took flows. After the links are submitted.
void FlowTakenLink(const Debugger& debugger);

// When a run wrote a value, as the debugger shows it: "this step", "step 12", "not run yet"; empty
// for one computed before any step.
std::string WhenWritten(const Watch& watch, uint64_t step);

// What a fiber waits for, as the debugger shows it: 1.5 s left, or signal "go".
std::string WakeText(const Wake& wake, double now);

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

// Dialog for picking a new node's type, searched for or by category. One opened from a pin lists
// only the types whose new node takes a link with it, those that need a cast apart, and links the
// new node to it.
struct CreateNodeDialog
{
    void Open(Id fromPin = 0);
    void Show(Document& document);

    Id GetCreatedNode() const { return m_CreatedNode; }

private:
    void Update(Document& document);
    bool ShowType(const NewNodeFit& entry);
    void Create(Document& document, const NewNodeFit& entry, ImVec2 position);

    Id                      m_FromPin     = 0;
    Id                      m_CreatedNode = 0;
    std::optional<uint64_t> m_Revision;   // of the document the entries are of
    bool                    m_Focus       = false;
    char                    m_Search[64]  = {};
    std::vector<NewNodeFit> m_Entries;    // by category and title; those that need a cast last, by title
};

// For a node, or for the selection when the node is in it. A function's entry has its function's
// type parameters, and whether it's pure. An event's node runs the event, and a Custom Event's
// renames it.
struct NodeContextMenu
{
    void Open(Id node = 0);
    void Show(Document& document, Debugger& debugger);

private:
    void ShowRename(Document& document);

    Id   m_Node          = 0;
    Id   m_RenamedEvent  = 0; // the Custom Event whose name the rename popup edits
    bool m_Renaming      = false;
    char m_EventName[64] = {};
};

// For a pin of a type variable, its type: chosen, or reset to what links decide.
// For a pin of a function's entry or return, the input or output of its signature it is: its name,
// its type, and whether it's there.
struct PinContextMenu
{
    void Open(Id pin = 0);
    void Show(Document& document, Debugger& debugger);

private:
    void ShowSignatureItems(Document& document, const Node& node, const Pin& pin);
    void ShowRename(Document& document);

    Id   m_Pin       = 0;
    Id   m_Renamed   = 0;  // the pin whose name the rename popup edits
    bool m_Focus     = false;
    char m_Name[64]  = {};
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
