# include "blueprint_editor_document.h"
# include "blueprint_editor_utilities.h"
# include "crude_logger.h"
# include <stdarg.h>

namespace blueprint_editor {

static string SaveReasonFlagsToString(ed::SaveReasonFlags flags, string_view separator = ", ")
{
    ImGuiTextBuffer builder;

    auto testFlag = [flags](ed::SaveReasonFlags flag)
    {
        return (flags & flag) == flag;
    };

    if (testFlag(ed::SaveReasonFlags::Navigation))
        builder.appendf("Navigation%" PRI_sv, FMT_sv(separator));
    if (testFlag(ed::SaveReasonFlags::Position))
        builder.appendf("Position%" PRI_sv, FMT_sv(separator));
    if (testFlag(ed::SaveReasonFlags::Size))
        builder.appendf("Size%" PRI_sv, FMT_sv(separator));
    if (testFlag(ed::SaveReasonFlags::Selection))
        builder.appendf("Selection%" PRI_sv, FMT_sv(separator));
    if (testFlag(ed::SaveReasonFlags::User))
        builder.appendf("User%" PRI_sv, FMT_sv(separator));

    if (builder.empty())
        return "None";
    else
        return string(builder.c_str(), builder.size() - separator.size());
}

} // namespace blueprint_editor {





crude_json::value blueprint_editor::Document::DocumentState::Serialize() const
{
    crude_json::value result;
    auto& nodesValue = result["nodes"];
    for (auto& entry : m_NodeStateMap)
        nodesValue[std::to_string(entry.first)] = entry.second;
    result["selection"] = m_SelectionState;
    result["blueprint"] = m_BlueprintState;
    return result;
}

//# define IMGUI_DEFINE_MATH_OPERATORS
//# include <imgui_internal.h>

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

    if (!nodesValue.is_object())
        return false;

    state.m_SelectionState = selectionValue;
    state.m_BlueprintState = dataValue;

    auto& nodesObject = nodesValue.get<crude_json::object>();
    for (auto& entry : nodesObject)
    {
        auto nodeId = static_cast<uint32_t>(strtoul(entry.first.c_str(), nullptr, 10)); // C sends its regards
        state.m_NodeStateMap[nodeId] = entry.second;
    }

    result = std::move(state);

    return true;
}





blueprint_editor::Document::UndoTransaction::UndoTransaction(Document& document)
    : m_Document(&document)
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

    m_ParentTransaction = m_Document->m_CurrentTransaction;
    m_Document->m_CurrentTransaction = this->shared_from_this();

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

void blueprint_editor::Document::UndoTransaction::Commit()
{
    if (!m_HasBegan && m_IsDone)
        return;

    if (m_ParentTransaction)
    {
        m_ParentTransaction->m_Actions.append(m_Actions.c_str());

        IM_ASSERT(this == m_Document->m_CurrentTransaction.get());

        m_Document->m_CurrentTransaction = m_ParentTransaction;
    }
    else if (!m_Actions.empty())
    {
        auto name = string_view(m_Actions.c_str(), m_Actions.size() - 1);

        LOGV("[UndoTransaction] Commit: %" PRI_sv, FMT_sv(name));

        m_State.m_Name = name.to_string();

        m_Document->m_Undo.emplace_back(std::move(m_State));
        m_Document->m_Redo.clear();

        m_Document->m_CurrentTransaction = nullptr;
    }

    m_IsDone = true;
}

void blueprint_editor::Document::UndoTransaction::Discard()
{
    if (!m_HasBegan)
        return;

    m_IsDone = true;
}





void blueprint_editor::Document::OnSaveBegin()
{
    m_SaveTransaction = BeginUndoTransaction();
}

bool blueprint_editor::Document::OnSaveNodeState(uint32_t nodeId, const crude_json::value& value, ed::SaveReasonFlags reason)
{
    m_DocumentState.m_NodeStateMap[nodeId] = value;

    if (reason != ed::SaveReasonFlags::Size)
    {
        auto node = m_Blueprint.FindNode(nodeId);

        ImGuiTextBuffer buffer;
        buffer.appendf("%" PRI_node " %s", FMT_node(node), SaveReasonFlagsToString(reason).c_str());
        m_SaveTransaction->AddAction(buffer.c_str());
    }

    return true;
}

bool blueprint_editor::Document::OnSaveState(const crude_json::value& value, ed::SaveReasonFlags reason)
{
    if ((reason & ed::SaveReasonFlags::Selection) == ed::SaveReasonFlags::Selection)
    {
        m_DocumentState.m_SelectionState = ed::GetState(ed::StateType::Selection);
    }

    if ((reason & ed::SaveReasonFlags::Navigation) == ed::SaveReasonFlags::Navigation)
    {
        m_NavigationState.m_ViewState = ed::GetState(ed::StateType::View);
    }

    return true;
}

void blueprint_editor::Document::OnSaveEnd()
{
    m_Blueprint.Save(m_DocumentState.m_BlueprintState);

    if (m_DocumentState.m_SelectionState.is_null())
        m_DocumentState.m_SelectionState = ed::GetState(ed::StateType::Selection);

    m_SaveTransaction = nullptr;
}

crude_json::value blueprint_editor::Document::OnLoadNodeState(uint32_t nodeId) const
{
    return {};
}

crude_json::value blueprint_editor::Document::OnLoadState() const
{
    return {};
}

blueprint_editor::shared_ptr<blueprint_editor::Document::UndoTransaction> blueprint_editor::Document::BeginUndoTransaction(string_view name /*= ""*/)
{
    auto transaction = std::make_shared<UndoTransaction>(*this);
    transaction->Begin(name);
    return transaction;
}

blueprint_editor::shared_ptr<blueprint_editor::Document::UndoTransaction> blueprint_editor::Document::GetDeferredUndoTransaction()
{
    return std::make_shared<UndoTransaction>(*this);
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
    crude_json::value result;
    result["document"] = m_DocumentState.Serialize();
    result["view"] = m_NavigationState.m_ViewState;
    return result;
}

bool blueprint_editor::Document::Deserialize(const crude_json::value& value, Document& result)
{
    if (!value.is_object())
        return false;

    if (!value.contains("document") || !value.contains("view"))
        return false;

    auto& documentValue = value["document"];
    auto& viewValue = value["view"];

    Document document;
    if (!DocumentState::Deserialize(documentValue, document.m_DocumentState))
        return false;

    document.m_NavigationState.m_ViewState = viewValue;

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
    return result.save(path.to_string());
}

bool blueprint_editor::Document::Undo()
{
    if (m_Undo.empty())
        return false;

    auto state = std::move(m_Undo.back());
    m_Undo.pop_back();

    LOGI("[Document] Undo \"%s\"", state.m_Name.c_str());

    UndoState undoState;
    undoState.m_Name = state.m_Name;
    undoState.m_State = m_DocumentState;

    ApplyState(state.m_State);

    m_Redo.push_back(std::move(undoState));

    return true;
}

bool blueprint_editor::Document::Redo()
{
    if (m_Redo.empty())
        return true;

    auto state = std::move(m_Redo.back());
    m_Redo.pop_back();

    LOGI("[Document] Redo \"%s\"", state.m_Name.c_str());

    UndoState undoState;
    undoState.m_Name = state.m_Name;
    undoState.m_State = m_DocumentState;

    ApplyState(state.m_State);

    m_Undo.push_back(std::move(undoState));

    return true;
}

void blueprint_editor::Document::ApplyState(const NavigationState& state)
{
    m_NavigationState = state;
    ed::ApplyState(ed::StateType::View, m_NavigationState.m_ViewState);
}

void blueprint_editor::Document::ApplyState(const DocumentState& state)
{
    m_Blueprint.Load(state.m_BlueprintState);

    m_DocumentState = state;
    ed::ApplyState(ed::StateType::Selection, m_DocumentState.m_SelectionState);
    for (auto& entry : m_DocumentState.m_NodeStateMap)
        ed::ApplyState(ed::StateType::Node, entry.first, entry.second);
}

void blueprint_editor::Document::OnMakeCurrent()
{
    ApplyState(m_DocumentState);
    ApplyState(m_NavigationState);
}
