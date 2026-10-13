# include "blueprint_editor_document.h"
# include "blueprint_editor_utilities.h"
# include "crude_logger.h"
# include <cmath>
# include <stdarg.h>
# include <stdlib.h>

namespace blueprint_editor {

static bool ParseId(const crude_json::value& value, uintptr_t& id)
{
    if (!value.is_number())
        return false;

    const auto number = value.get<crude_json::number>();
    if (!(number >= 1.0 && number <= 4294967295.0) || number != static_cast<double>(static_cast<uint32_t>(number)))
        return false;

    id = static_cast<uintptr_t>(number);
    return true;
}

template <typename Id>
static bool ParseIds(const crude_json::value& value, vector<Id>& result)
{
    if (!value.is_array())
        return false;

    for (auto& item : value.get<crude_json::array>())
    {
        uintptr_t id = 0;
        if (!ParseId(item, id))
            return false;
        result.push_back(Id(id));
    }

    return true;
}

static bool ParseVector(const crude_json::value& value, ImVec2& result)
{
    if (!value.is_object() || !value["x"].is_number() || !value["y"].is_number())
        return false;

    result = ImVec2(static_cast<float>(value["x"].get<crude_json::number>()), static_cast<float>(value["y"].get<crude_json::number>()));
    return std::isfinite(result.x) && std::isfinite(result.y);
}

} // namespace blueprint_editor {





// Nodes are kept in the text FormatNodeState() writes, which needs the document's editor current.
crude_json::value blueprint_editor::Document::DocumentState::Serialize() const
{
    crude_json::value nodes(crude_json::type_t::object);
    for (auto& state : m_NodesState)
        nodes[std::to_string(state.Id.Get())] = crude_json::value::parse(ed::FormatNodeState(state));

    crude_json::value selection(crude_json::type_t::object);
    selection["nodes"] = crude_json::value(crude_json::type_t::array);
    for (auto id : m_SelectedNodes)
        selection["nodes"].push_back(id.Get());
    selection["links"] = crude_json::value(crude_json::type_t::array);
    for (auto id : m_SelectedLinks)
        selection["links"].push_back(id.Get());

    crude_json::value result;
    result["nodes"]     = std::move(nodes);
    result["selection"] = std::move(selection);
    result["blueprint"] = m_BlueprintState;
    return result;
}

bool blueprint_editor::Document::DocumentState::Deserialize(const crude_json::value& value, DocumentState& result)
{
    DocumentState state;

    if (!value.is_object())
        return false;

    if (!value.contains("nodes") || !value.contains("selection") || !value.contains("blueprint"))
        return false;

    auto& nodesValue = value["nodes"];
    auto& selectionValue = value["selection"];
    auto& dataValue = value["blueprint"];

    if (!nodesValue.is_object() || !selectionValue.is_object())
        return false;

    for (auto& node : nodesValue.get<crude_json::object>())
    {
        char* end = nullptr;
        const auto id = strtoul(node.first.c_str(), &end, 10);
        if (end == node.first.c_str() || *end != '\0' || id == 0)
            return false;

        ed::NodeState nodeState;
        nodeState.Id = static_cast<uintptr_t>(id);
        const auto nodeText = node.second.dump();
        if (!ed::ParseNodeState(nodeText.c_str(), nodeText.size(), &nodeState))
            return false;

        state.m_NodesState.push_back(nodeState);
    }

    if (!ParseIds(selectionValue["nodes"], state.m_SelectedNodes) || !ParseIds(selectionValue["links"], state.m_SelectedLinks))
        return false;

    state.m_BlueprintState = dataValue;

    result = std::move(state);

    return true;
}





blueprint_editor::Document::UndoTransaction::UndoTransaction(Document& document, string_view name)
    : m_Name(name.to_string())
    , m_Document(&document)
{
}

blueprint_editor::Document::UndoTransaction::~UndoTransaction()
{
    Commit();
}

void blueprint_editor::Document::UndoTransaction::Begin(string_view name /*= ""*/)
{
    if (m_HasBegan)
        return;

    if (m_Document->m_MasterTransaction)
    {
        m_MasterTransaction = m_Document->m_MasterTransaction->shared_from_this();
    }
    else
    {
        // Spawn master transaction which commit on destruction
        m_MasterTransaction = m_Document->GetDeferredUndoTransaction(m_Name);
        m_Document->m_MasterTransaction = m_MasterTransaction.get();
        m_MasterTransaction->Begin();
        m_MasterTransaction->m_MasterTransaction = nullptr;
    }

    m_HasBegan = true;

    m_State.m_State = m_Document->m_DocumentState;

    if (!name.empty())
        AddAction(name);
}

void blueprint_editor::Document::UndoTransaction::AddAction(string_view name)
{
    if (!m_HasBegan || name.empty())
        return;

    m_Actions.appendf("%" PRI_sv "\n", FMT_sv(name));
}

void blueprint_editor::Document::UndoTransaction::AddAction(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    ImGuiTextBuffer buffer;
    buffer.appendfv(format, args);
    va_end(args);

    AddAction(string_view(buffer.Buf.Data, buffer.Buf.Size));
}

void blueprint_editor::Document::UndoTransaction::Commit(string_view name)
{
    if (!m_HasBegan || m_IsDone)
        return;

    if (m_MasterTransaction)
    {
        if (!name.empty())
        {
            m_MasterTransaction->m_Actions.append(name.data(), name.data() + name.size());
            m_MasterTransaction->m_Actions.append("\n");
        }
        else
        {
            m_MasterTransaction->m_Actions.append(m_Actions.c_str());
        }

        IM_ASSERT(m_MasterTransaction->m_IsDone == false);
    }
    else
    {
        IM_ASSERT(m_Document->m_MasterTransaction == this);

        if (!m_Actions.empty())
        {
            if (name.empty())
                name = string_view(m_Actions.c_str(), m_Actions.size() - 1);

            LOGV("[UndoTransaction] Commit: %" PRI_sv, FMT_sv(name));

            m_State.m_Name = name.to_string();

            m_Document->m_Undo.emplace_back(std::move(m_State));
            m_Document->m_Redo.clear();

            m_Document->m_DocumentState = m_Document->BuildDocumentState();
            m_Document->m_IsModified    = true;
        }

        m_Document->m_MasterTransaction = nullptr;
    }

    m_IsDone = true;
}

void blueprint_editor::Document::UndoTransaction::Discard()
{
    if (!m_HasBegan || m_IsDone)
        return;

    if (!m_MasterTransaction)
    {
        IM_ASSERT(m_Document->m_MasterTransaction == this);

        m_Document->m_MasterTransaction = nullptr;
    }

    m_IsDone = true;
}





blueprint_editor::shared_ptr<blueprint_editor::Document::UndoTransaction> blueprint_editor::Document::BeginUndoTransaction(string_view name, string_view action /*= ""*/)
{
    auto transaction = std::make_shared<UndoTransaction>(*this, name);
    transaction->Begin(action);
    return transaction;
}

blueprint_editor::shared_ptr<blueprint_editor::Document::UndoTransaction> blueprint_editor::Document::GetDeferredUndoTransaction(string_view name)
{
    return std::make_shared<UndoTransaction>(*this, name);
}

void blueprint_editor::Document::SetPath(string_view path)
{
    m_Path = path.to_string();

    auto lastSeparator = m_Path.find_last_of("\\/");
    if (lastSeparator != string::npos)
        m_Name = m_Path.substr(lastSeparator + 1);
    else
        m_Name = path.to_string();
}

crude_json::value blueprint_editor::Document::Serialize() const
{
    // The blueprint as it is, which may have changed since the last undo step.
    crude_json::value result;
    result["document"] = BuildDocumentState().Serialize();

    auto& view = result["view"];
    if (m_NavigationState.m_HasViewState)
    {
        auto& state = m_NavigationState.m_ViewState;
        view["min"]["x"] = state.VisibleMin.x;
        view["min"]["y"] = state.VisibleMin.y;
        view["max"]["x"] = state.VisibleMax.x;
        view["max"]["y"] = state.VisibleMax.y;
    }

    return result;
}

bool blueprint_editor::Document::Deserialize(const crude_json::value& value, Document& result)
{
    if (!value.is_object())
        return false;

    if (!value.contains("document"))
        return false;

    auto& documentValue = value["document"];
    auto& viewValue = value["view"];

    Document document;
    if (!DocumentState::Deserialize(documentValue, document.m_DocumentState))
        return false;

    auto& view = document.m_NavigationState.m_ViewState;
    document.m_NavigationState.m_HasViewState = ParseVector(viewValue["min"], view.VisibleMin) && ParseVector(viewValue["max"], view.VisibleMax);

    if (!document.m_Blueprint.Load(document.m_DocumentState.m_BlueprintState))
        return false;

    result = std::move(document);

    return true;
}

bool blueprint_editor::Document::Load(string_view path)
{
    auto loadResult = crude_json::value::load(path.to_string());
    if (!loadResult.second)
        return false;

    Document document;
    if (!Deserialize(loadResult.first, document))
        return false;

    *this = std::move(document);

    return true;
}

bool blueprint_editor::Document::Save(string_view path) const
{
    auto result = Serialize();

    // Only a file the document can be loaded from again replaces the one at 'path'.
    Document check;
    if (!Deserialize(crude_json::value::parse(result.dump()), check))
        return false;

    return result.save(path.to_string());
}

bool blueprint_editor::Document::Undo()
{
    if (m_Undo.empty())
        return false;

    auto state = std::move(m_Undo.back());
    m_Undo.pop_back();

    LOGI("[Document] Undo: %s", state.m_Name.c_str());

    UndoState undoState;
    undoState.m_Name = state.m_Name;
    undoState.m_State = m_DocumentState;

    ApplyState(state.m_State);
    m_IsModified = true;

    m_Redo.push_back(std::move(undoState));

    return true;
}

bool blueprint_editor::Document::Redo()
{
    if (m_Redo.empty())
        return true;

    auto state = std::move(m_Redo.back());
    m_Redo.pop_back();

    LOGI("[Document] Redo: %s", state.m_Name.c_str());

    UndoState undoState;
    undoState.m_Name = state.m_Name;
    undoState.m_State = m_DocumentState;

    ApplyState(state.m_State);
    m_IsModified = true;

    m_Undo.push_back(std::move(undoState));

    return true;
}

blueprint_editor::Document::DocumentState blueprint_editor::Document::BuildDocumentState() const
{
    DocumentState result;
    m_Blueprint.Save(result.m_BlueprintState);

    // The editor still knows the nodes undo took out of the blueprint; the document doesn't.
    vector<ed::NodeState> nodes(static_cast<size_t>(ed::GetNodeStates(nullptr, 0)));
    nodes.resize(static_cast<size_t>(ed::GetNodeStates(nodes.data(), static_cast<int>(nodes.size()))));
    for (auto& node : nodes)
        if (m_Blueprint.FindNode(static_cast<uint32_t>(node.Id.Get())))
            result.m_NodesState.push_back(node);

    const auto selected = static_cast<size_t>(ed::GetSelectedObjectCount());
    result.m_SelectedNodes.resize(selected);
    result.m_SelectedNodes.resize(static_cast<size_t>(ed::GetSelectedNodes(result.m_SelectedNodes.data(), static_cast<int>(selected))));
    result.m_SelectedLinks.resize(selected);
    result.m_SelectedLinks.resize(static_cast<size_t>(ed::GetSelectedLinks(result.m_SelectedLinks.data(), static_cast<int>(selected))));
    return result;
}

void blueprint_editor::Document::ApplyState(const NavigationState& state)
{
    m_NavigationState = state;
    if (m_NavigationState.m_HasViewState)
        ed::SetViewState(m_NavigationState.m_ViewState);
}

void blueprint_editor::Document::ApplyState(const DocumentState& state)
{
    m_Blueprint.Load(state.m_BlueprintState);

    m_DocumentState = state;
    ed::SetNodeStates(m_DocumentState.m_NodesState.data(), static_cast<int>(m_DocumentState.m_NodesState.size()));
    ed::SetSelection(
        m_DocumentState.m_SelectedNodes.data(), static_cast<int>(m_DocumentState.m_SelectedNodes.size()),
        m_DocumentState.m_SelectedLinks.data(), static_cast<int>(m_DocumentState.m_SelectedLinks.size()));
}

void blueprint_editor::Document::OnMakeCurrent()
{
    ApplyState(m_DocumentState);
    ApplyState(m_NavigationState);
}

void blueprint_editor::Document::CaptureView()
{
    m_NavigationState.m_ViewState    = ed::GetViewState();
    m_NavigationState.m_HasViewState = true;
}
