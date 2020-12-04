# pragma once
# include <imgui.h>
# include <imgui_node_editor.h>
# include <crude_json.h>
# include "crude_blueprint.h"
# include "nonstd/string_view.hpp" // span<>, make_span
# include <vector>
# include <map>
# include <memory>
# include <string>
# include <stdint.h>

namespace blueprint_editor {

namespace ed = ax::NodeEditor;

using nonstd::string_view;
using std::shared_ptr;
using std::weak_ptr;
using std::map;
using std::vector;
using std::string;

using crude_blueprint::Blueprint;

struct Document
{
    struct DocumentState
    {
        crude_json::value m_NodesState;
        crude_json::value m_SelectionState;
        crude_json::value m_BlueprintState;

        crude_json::value Serialize() const;

        static bool Deserialize(const crude_json::value& value, DocumentState& result);
    };

    struct NavigationState
    {
        crude_json::value                   m_ViewState;
    };

    struct UndoState
    {
        string          m_Name;
        DocumentState   m_State;
    };

    struct UndoTransaction
        : std::enable_shared_from_this<UndoTransaction>
    {
        UndoTransaction(Document& document, string_view name);
        UndoTransaction(UndoTransaction&&) = delete;
        UndoTransaction(const UndoTransaction&) = delete;
        ~UndoTransaction();

        UndoTransaction& operator=(UndoTransaction&&) = delete;
        UndoTransaction& operator=(const UndoTransaction&) = delete;

        void Begin(string_view name = "");
        void AddAction(string_view name);
        void AddAction(const char* format, ...) IM_FMTARGS(2);
        void Commit(string_view name = "");
        void Discard();

        const Document* GetDocument() const { return m_Document; }

    private:
        string                      m_Name;
        Document*                   m_Document = nullptr;
        UndoState                   m_State;
        ImGuiTextBuffer             m_Actions;
        bool                        m_HasBegan = false;
        bool                        m_IsDone = false;
        shared_ptr<UndoTransaction> m_MasterTransaction;
    };

# if !CRUDE_BP_MSVC2015 // [[nodiscard]] is unrecognized attribute
    [[nodiscard]]
# endif
    shared_ptr<UndoTransaction> BeginUndoTransaction(string_view name, string_view action = "");

# if !CRUDE_BP_MSVC2015 // [[nodiscard]] is unrecognized attribute
    [[nodiscard]]
# endif
    shared_ptr<UndoTransaction> GetDeferredUndoTransaction(string_view name);

    void SetPath(string_view path);

    crude_json::value Serialize() const;

    static bool Deserialize(const crude_json::value& value, Document& result);

    bool Load(string_view path);
    bool Save(string_view path) const;

    bool Undo();
    bool Redo();

    DocumentState BuildDocumentState();
    void ApplyState(const DocumentState& state);
    void ApplyState(const NavigationState& state);

    void OnMakeCurrent();

    void OnSaveBegin();
    bool OnSaveNodeState(uint32_t nodeId, const crude_json::value& value, ed::SaveReasonFlags reason);
    bool OnSaveState(const crude_json::value& value, ed::SaveReasonFlags reason);
    void OnSaveEnd();

    crude_json::value OnLoadNodeState(uint32_t nodeId) const;
    crude_json::value OnLoadState() const;

          Blueprint& GetBlueprint()       { return m_Blueprint; }
    const Blueprint& GetBlueprint() const { return m_Blueprint; }

    string                  m_Path;
    string                  m_Name;
    bool                    m_IsModified = false;
    vector<UndoState>       m_Undo;
    vector<UndoState>       m_Redo;

    DocumentState           m_DocumentState;
    NavigationState         m_NavigationState;

    UndoTransaction* m_MasterTransaction = nullptr;

    shared_ptr<UndoTransaction> m_SaveTransaction = nullptr;

    Blueprint               m_Blueprint;
};



} // namespace blueprint_editor {