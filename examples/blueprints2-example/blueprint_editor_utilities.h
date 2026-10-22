#pragma once
#include <imgui.h>
#include <imgui_node_editor.h>
#include "crude_blueprint.h"
#include "blueprint_editor_icons.h"
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
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
// "T, a type parameter of Lerp; allows Int32, Float" for a function's type parameter; empty for
// another type.
std::string TypeParameterText(const Blueprint& blueprint, const Type* type);
std::string AllowedText(const VariableDeclaration& parameter); // "Int32, Float", or "any type"
// What a node is, by the color of its header's band: an event red, a node that may suspend its
// fiber teal, a function's entry and returns violet, its calls blue, Print and Assert amber, other
// pure nodes green, and the rest slate.
ImVec4   HeaderColor(const Blueprint& blueprint, const Node& node);

// What an input without a link reads: the value the user typed, the default its node declares,
// or its type's. None while its type is undecided.
Value InputValue(const Graph& graph, const Pin& pin);

// While a pin's type is undecided, the value it keeps: one typed into an input without an active
// link, or a Constant's. None when nothing is kept.
Value KeptValue(const Graph& graph, const Pin& pin);

// A Constant holds its value in its config.
bool  IsConstant(const Node& node);
Value ConstantValue(const Node& node, const Pin& pin);
// A Custom Event's config holds its event's name, and the names of its parameters.
bool  IsCustomEvent(const Node& node);
// A Constant, and a variable's Get, show as a row of their own, without a header; a Get's row names
// its variable.
bool             IsCompact(const Node& node);
std::string_view GetterVariable(const Node& node); // empty for a node that isn't a variable's Get

// A node's look behind its content, on its outline: the top-left and bottom-right corners cut at
// 45°, and a compact node's other two as well. Over a darker header row, a header's band runs from
// the top-left cut past its title and ends in a 45° slant, or spans a node too narrow for that; a
// compact node has a stripe of its type's color along its left edge instead. The border is stroked
// inside the outline, and the selection's ring outside it, past a gap.
struct NodeLook
{
    ImU32 m_Band        = 0;
    float m_BandBottom  = 0.0f; // a header's
    float m_TitleRight  = 0.0f; // the band's slant starts past it
    ImU32 m_Stripe      = 0;    // a compact node's
    ImU32 m_Border      = 0;
    float m_BorderWidth = 1.0f;
    bool  m_Compact     = false;
    bool  m_Selected    = false;
};
void   DrawNodeLook(ImDrawList* drawList, ImVec2 min, ImVec2 max, const NodeLook& look);
float  BandEnd(float titleRight, float bandHeight); // where the band's slant starts, at its top
ImVec2 CutCenter(bool compact);                    // the middle of a node's top-left cut, from its top-left
ImVec4 NodePadding(const NodeLook& look);          // the editor's, a compact node's from inside its stripe

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

// The graph a node or a pin is in, and the node, of all the blueprint's graphs.
const Graph* GraphOf(const Blueprint& blueprint, Id node);
const Graph* GraphOfPin(const Blueprint& blueprint, Id pin);
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

// Edits of a function's signature, each one undo step, and none when nothing changes. One the
// blueprint refuses is logged. Pins are an input's or an output's, by key.
void ChangeSignature(Document& document, Id graph, std::string_view what, const std::function<void(Signature&)>& change);
void AddSignaturePin(Document& document, Id graph, bool input); // of a key of its own, an Int32
void RenameSignaturePin(Document& document, Id graph, bool input, std::string key, std::string name);
void RetypeSignaturePin(Document& document, Id graph, bool input, std::string key, TypeTerm type);
void RemoveSignaturePin(Document& document, Id graph, bool input, std::string key);
void AddTypeParameter(Document& document, Id graph); // named T, U, V..., the first free
void ToggleAllowedType(Document& document, Id graph, size_t index, const ValueType* head);
// A type parameter that a pin's type has can't go.
void RemoveTypeParameter(Document& document, Id graph, int index);
const SignaturePin* TypeParameterUser(const Signature& signature, int index); // the first pin whose type has it

// What a signature's pin may be of: each value type of no type arguments, each type parameter, and
// an array of each.
struct SignatureTypeChoice
{
    std::string m_Name;  // the value type's or the parameter's
    TypeTerm    m_Type;
    bool        m_Array = false;
};
std::vector<SignatureTypeChoice> SignatureTypeChoices(const Registry& registry, const Signature& signature);
void RenameFunction(Document& document, Id graph, std::string name);
void RemoveFunction(Document& document, Id graph); // with the calls of it in other graphs
void RenameTypeParameter(Document& document, Id graph, size_t index, std::string name);
void RenameEvent(Document& document, Id node, std::string name); // a Custom Event's
void RenamePin(Document& document, Id pin, std::string name);    // in its node's config; an empty name gives back the declared one

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

bool SquareButton(const char* label);      // As wide as it's high, a line's height: for a sign such as + or -.
bool GroupButton(const char* id, bool plus, bool enabled); // A node's + or -, a line's height, drawn a shade off the node's body.
std::string ValueText(const Value& value); // "true", "12", "1.5", a string as it is, else as its type formats it
bool DrawValue(const Value& value);        // Draws a value; false for none.
// An editor of a value. A number's - and + change it, the editor staying; Enter ends the edit.
// A field ends its edit when it stops being active, keeping what was typed, but for a click on a
// number's - or +, which steps it. 'focus' makes the field active.
enum class ValueEdit { None, Changed, Done };
ValueEdit EditValue(Value& value, bool focus = false);

// Draws a value, or its editor once clicked, its field active; true when an edit changed it. The
// editor closes when its edit ends, and on a click elsewhere.
bool DrawValueWithEditor(Id id, Value& value);

// A popup of a name's field, which opens while 'open' is set, its field focused, and clears it.
// Enter gives the name typed, unless a problem with it shows under the field. Escape closes it,
// and so does losing what the name is of.
std::optional<std::string> NamePopup(const char* id, bool& open, std::span<char> name, bool exists, std::string_view problem = {});

// Before a frame's widgets: a click outside the open popups closes them, so that it also reaches
// what it's over. ImGui would close them after the widgets, which ignore the click while a popup is
// open. A click over a popup, and any while a modal is open, stays ImGui's.
void ClosePopupsOnClickOutside();

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

// A run of a document's blueprint: a context of its compiled program, and the fiber its Start or
// Call began. Prints go to the log, and so do a call's results. A step the document takes goes on in the run when it changed only
// values, and stops it otherwise. Breakpoints stay for the runs that follow. The run's clock moves
// on only while it runs.
struct Debugger
{
    // A blueprint with an error doesn't start: its diagnostics are logged. Without arguments, the
    // event's parameters, or the function's inputs, get their types' defaults, and a generic
    // function doesn't run.
    bool Start(const Document& document, bool running, std::string_view event = "Start", std::span<const Value> arguments = {});
    bool Call(const Document& document, bool running, std::string_view function, std::span<const Value> arguments = {});
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

    bool                     Begin(const Document& document, bool running, std::string_view name, bool call, std::span<const Value> arguments);
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
    void Open(Id fromPin = 0); // in the editor, suspended
    void Show(Document& document);

    Id     GetCreatedNode() const { return m_CreatedNode; }
    bool   IsOpen() const         { return m_Open; }    // as it last showed
    Id     GetFromPin() const     { return m_FromPin; }
    ImVec2 GetDropPoint() const   { return m_DropPoint; } // on the canvas, where it opened

private:
    void Update(Document& document);
    bool ShowType(const NewNodeFit& entry, bool selected);
    void Create(Document& document, const NewNodeFit& entry, ImVec2 position);

    Id                      m_FromPin     = 0;
    Id                      m_CreatedNode = 0;
    std::optional<uint64_t> m_Revision;   // of the document the entries are of
    bool                    m_Focus       = false;
    char                    m_Search[64]  = {};
    std::vector<NewNodeFit> m_Entries;    // by category and title; those that need a cast last, by title
    ImVec2                  m_DropPoint;
    bool                    m_Open        = false;
    int                     m_Selected    = 0;     // of the entries shown, which Up and Down move over and Enter makes
    int                     m_Shown       = 0;     // in the last frame
    bool                    m_Scroll      = false; // to the selected one
};

// What a run of an event or of a function takes, and the arguments for it: a value for each of
// the event's parameters or the function's inputs, and for a generic function, a type for each of
// its type parameters, which its inputs' types follow.
struct RunArguments
{
    std::string                      m_Name;       // the event's, or the function's
    bool                             m_Call = false;
    std::vector<VariableDeclaration> m_Parameters; // the function's type parameters
    std::vector<const Type*>         m_Chosen;     // a type for each
    std::vector<std::string>         m_Names;      // the parameters', or the inputs'
    std::vector<TypeTerm>            m_Terms;      // their types, of m_Parameters by index; none while undecided
    std::vector<Value>               m_Values;

    // An event's node's, or an impure function's entry's, of the graph that shows.
    static std::optional<RunArguments> Of(const Document& document, Id node);
    // The types a type parameter may be given: those it allows that take no type arguments.
    static std::vector<const Type*> Choices(const Registry& registry, const VariableDeclaration& parameter);

    std::string Key() const; // the same for the runs of an event, or of a function
    bool        Asks() const { return !m_Parameters.empty() || !m_Names.empty(); }
    const Type* TypeOf(TypeTable& types, size_t value) const; // null while undecided
    // The choices and values of another run of the same event or function, Settle() fitting them.
    void        Reuse(const RunArguments& other);
    // Each type parameter of a type it may be, the first when it had none, and each value of its
    // type, its type's default when it was of another.
    void        Settle(const Blueprint& blueprint);
    bool        IsReady(TypeTable& types) const; // each value of its decided type
};

// For a node, or for the selection when the node is in it. A function's entry runs its function,
// and edits its type parameters, their names and the types each allows, and whether it's pure; it
// and a return ask for the whole signature to show. An event's node runs the event, and a Custom
// Event's renames it. A run that takes arguments asks for them in a popup, which offers those of
// the last run of the same event or function.
struct NodeContextMenu
{
    void Open(Id node = 0);
    void Show(Document& document, Debugger& debugger);
    bool TakeSignatureRequest() { return std::exchange(m_SignatureRequested, false); }

private:
    void ShowRename(Document& document);
    void ShowRenameParameter(Document& document);
    void ShowArguments(Document& document, Debugger& debugger);

    Id   m_Node          = 0;
    Id   m_RenamedEvent  = 0; // the Custom Event whose name the rename popup edits
    bool m_Renaming      = false;
    char m_EventName[64] = {};

    Id     m_ParameterGraph    = 0; // the function whose type parameter the rename popup edits
    size_t m_Parameter         = 0;
    bool   m_RenamingParameter = false;
    char   m_ParameterName[64] = {};

    bool m_SignatureRequested = false;

    Id                                  m_Run    = 0;     // the node whose run the arguments popup starts
    bool                                m_Asking = false; // the arguments popup opens
    int                                 m_Asked  = 0;     // times it opened
    RunArguments                        m_Arguments;
    std::map<std::string, RunArguments> m_Used;           // by key, of the last run each
};

// For a pin of a type variable, its type: chosen, or reset to what links decide.
// For a pin of a function's entry or return, the input or output of its signature it is: its name,
// its type, and whether it's there. For a Custom Event's parameter, its name.
struct PinContextMenu
{
    void Open(Id pin = 0);
    void Show(Document& document, Debugger& debugger);

private:
    std::function<void()> ShowSignatureItems(Document& document, const Node& node, const Pin& pin); // gives the edit chosen
    void ShowRenameItem(const Pin& pin); // opens the rename popup
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
