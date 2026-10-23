# pragma once
# include <imgui.h>
# include <imgui_node_editor.h>
# include <crude_json.h>
# include "crude_blueprint.h"
# include <cstdint>
# include <map>
# include <memory>
# include <string>
# include <string_view>
# include <vector>

namespace blueprint_editor {

namespace ed = ax::NodeEditor;

// A blueprint, the layout of its nodes, and the steps undo takes back. One graph shows at a time,
// in an editor of its own, which is the current one while the document works on its layout.
struct Document
{
    // What undo restores: the blueprint, and the layout and selection of its nodes.
    struct DocumentState
    {
        std::vector<ed::NodeState> m_NodesState;
        std::vector<ed::NodeId>    m_SelectedNodes;
        std::vector<ed::LinkId>    m_SelectedLinks;
        crude_json::value          m_BlueprintState;

        crude_json::value Serialize() const;

        static bool Deserialize(const crude_json::value& value, DocumentState& result);
    };

    // Kept with the document, outside undo: what each graph's editor showed.
    struct NavigationState
    {
        std::map<crude_blueprint::Id, ed::ViewState> m_Views;
    };

    struct UndoState
    {
        std::string   m_Name;
        DocumentState m_State;
    };

    struct UndoTransaction
        : std::enable_shared_from_this<UndoTransaction>
    {
        UndoTransaction(Document& document, std::string_view name);
        UndoTransaction(UndoTransaction&&) = delete;
        UndoTransaction(const UndoTransaction&) = delete;
        ~UndoTransaction();

        UndoTransaction& operator=(UndoTransaction&&) = delete;
        UndoTransaction& operator=(const UndoTransaction&) = delete;

        void Begin(std::string_view name = "");
        void AddAction(std::string_view name);
        void AddAction(const char* format, ...) IM_FMTARGS(2);
        void Commit(std::string_view name = "");
        void Discard();

        const Document* GetDocument() const { return m_Document; }

    private:
        std::string                      m_Name;
        Document*                        m_Document = nullptr;
        UndoState                        m_State;
        ImGuiTextBuffer                  m_Actions;
        bool                             m_HasBegan = false;
        bool                             m_IsDone   = false;
        std::shared_ptr<UndoTransaction> m_MasterTransaction;
    };

    // A new document has an empty graph.
    Document(std::shared_ptr<const crude_blueprint::Registry> registry, std::shared_ptr<crude_blueprint::TypeTable> types);

    [[nodiscard]] std::shared_ptr<UndoTransaction> BeginUndoTransaction(std::string_view name, std::string_view action = "");
    [[nodiscard]] std::shared_ptr<UndoTransaction> GetDeferredUndoTransaction(std::string_view name);

    void SetPath(std::string_view path);

    crude_json::value Serialize() const;

    // Into this document; a value it can't read leaves the document as it was.
    bool Deserialize(const crude_json::value& value);

    bool Load(std::string_view path);
    bool Save(std::string_view path) const;

    bool Undo();
    bool Redo();

    // The layout of the graph that shows comes from its editor; the other graphs' is as they were
    // last shown, or as the state applied last left them.
    DocumentState BuildDocumentState() const;
    bool          ApplyState(const DocumentState& state); // false when its blueprint doesn't load
    void          ApplyState(const NavigationState& state);

    // Into the current editor, once it's the graph's that shows: its layout, selection and view as
    // the state has them; the blueprint stays as it is.
    void OnMakeCurrent();
    void CaptureView();

          crude_blueprint::Blueprint& GetBlueprint()       { return *m_Blueprint; }
    const crude_blueprint::Blueprint& GetBlueprint() const { return *m_Blueprint; }
    // The graph that shows: the one chosen, else the first.
          crude_blueprint::Graph&     GetGraph();
    const crude_blueprint::Graph&     GetGraph() const;

    std::string            m_Path;
    std::string            m_Name;
    bool                   m_IsModified = false;
    uint64_t               m_Revision   = 0; // up with each change to the blueprint, by an edit or undo
    crude_blueprint::Id    m_ShownGraph = 0;
    std::vector<UndoState> m_Undo;
    std::vector<UndoState> m_Redo;

    DocumentState   m_DocumentState;
    NavigationState m_NavigationState;

    UndoTransaction* m_MasterTransaction = nullptr;

private:
    std::unique_ptr<crude_blueprint::Blueprint> LoadBlueprint(const crude_json::value& state) const;
    void                                        ApplyLayout(); // the state's, of the graph that shows

    std::shared_ptr<const crude_blueprint::Registry> m_Registry;
    std::shared_ptr<crude_blueprint::TypeTable>      m_Types;
    std::unique_ptr<crude_blueprint::Blueprint>      m_Blueprint;
};

// What the example's documents are made of: the built-in value types and casts, the library, and
// the example's own structs, Vector2, Point and Color, each with its Make and Break. A color, white
// by default, shows as a swatch, and its editor has a picker.
std::shared_ptr<const crude_blueprint::Registry> ExampleRegistry();

// Start runs For Loop from 0 to 4, and its body prints each index plus 10, through a cast from
// Int32 to String; then Print says "Done". Needs the document's editor current, for the layout.
void CreateExampleBlueprint(Document& document);

} // namespace blueprint_editor
