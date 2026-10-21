# include "blueprint_editor_document.h"
# include "crude_blueprint_library.h"
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
static bool ParseIds(const crude_json::value& value, std::vector<Id>& result)
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

static bool HasNode(const crude_blueprint::Blueprint& blueprint, uintptr_t id)
{
    for (auto& graph : blueprint.Graphs())
        if (graph->FindNode(static_cast<crude_blueprint::Id>(id)))
            return true;
    return false;
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





blueprint_editor::Document::UndoTransaction::UndoTransaction(Document& document, std::string_view name)
    : m_Name(name)
    , m_Document(&document)
{
}

blueprint_editor::Document::UndoTransaction::~UndoTransaction()
{
    Commit();
}

void blueprint_editor::Document::UndoTransaction::Begin(std::string_view name /*= ""*/)
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

void blueprint_editor::Document::UndoTransaction::AddAction(std::string_view name)
{
    if (!m_HasBegan || name.empty())
        return;

    m_Actions.appendf("%.*s\n", static_cast<int>(name.size()), name.data());
}

void blueprint_editor::Document::UndoTransaction::AddAction(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    ImGuiTextBuffer buffer;
    buffer.appendfv(format, args);
    va_end(args);

    AddAction(std::string_view(buffer.c_str(), static_cast<size_t>(buffer.size())));
}

void blueprint_editor::Document::UndoTransaction::Commit(std::string_view name)
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
                name = std::string_view(m_Actions.c_str(), static_cast<size_t>(m_Actions.size()) - 1);

            LOGV("[UndoTransaction] Commit: %.*s", static_cast<int>(name.size()), name.data());

            m_State.m_Name = std::string(name);

            m_Document->m_Undo.emplace_back(std::move(m_State));
            m_Document->m_Redo.clear();

            auto blueprint = m_Document->m_DocumentState.m_BlueprintState.dump();
            m_Document->m_DocumentState = m_Document->BuildDocumentState();
            m_Document->m_IsModified    = true;
            if (m_Document->m_DocumentState.m_BlueprintState.dump() != blueprint)
                ++m_Document->m_Revision;
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





blueprint_editor::Document::Document(std::shared_ptr<const crude_blueprint::Registry> registry, std::shared_ptr<crude_blueprint::TypeTable> types)
    : m_Registry(std::move(registry))
    , m_Types(std::move(types))
    , m_Blueprint(std::make_unique<crude_blueprint::Blueprint>(m_Registry, m_Types))
{
    m_Blueprint->AddGraph("Event Graph");
}

std::shared_ptr<blueprint_editor::Document::UndoTransaction> blueprint_editor::Document::BeginUndoTransaction(std::string_view name, std::string_view action /*= ""*/)
{
    auto transaction = std::make_shared<UndoTransaction>(*this, name);
    transaction->Begin(action);
    return transaction;
}

std::shared_ptr<blueprint_editor::Document::UndoTransaction> blueprint_editor::Document::GetDeferredUndoTransaction(std::string_view name)
{
    return std::make_shared<UndoTransaction>(*this, name);
}

void blueprint_editor::Document::SetPath(std::string_view path)
{
    m_Path = std::string(path);

    auto lastSeparator = m_Path.find_last_of("\\/");
    if (lastSeparator != std::string::npos)
        m_Name = m_Path.substr(lastSeparator + 1);
    else
        m_Name = m_Path;
}

crude_json::value blueprint_editor::Document::Serialize() const
{
    // The blueprint as it is, which may have changed since the last undo step.
    crude_json::value result;
    result["document"] = BuildDocumentState().Serialize();

    // "view" is the first graph's, as documents of one graph have it.
    auto save = [](const ed::ViewState& state)
    {
        crude_json::value view;
        view["min"]["x"] = state.VisibleMin.x;
        view["min"]["y"] = state.VisibleMin.y;
        view["max"]["x"] = state.VisibleMax.x;
        view["max"]["y"] = state.VisibleMax.y;
        return view;
    };
    const auto first = m_Blueprint->Graphs().front()->GetId();
    for (auto& [graph, state] : m_NavigationState.m_Views)
    {
        if (graph == first)
            result["view"] = save(state);
        else if (m_Blueprint->FindGraph(graph))
            result["views"][std::to_string(graph)] = save(state);
    }

    return result;
}

bool blueprint_editor::Document::Deserialize(const crude_json::value& value)
{
    if (!value.is_object() || !value.contains("document"))
        return false;

    DocumentState state;
    if (!DocumentState::Deserialize(value["document"], state))
        return false;

    auto blueprint = LoadBlueprint(state.m_BlueprintState);
    if (!blueprint)
        return false;

    NavigationState navigation;
    auto load = [&navigation](crude_blueprint::Id graph, const crude_json::value& view)
    {
        ed::ViewState state;
        if (ParseVector(view["min"], state.VisibleMin) && ParseVector(view["max"], state.VisibleMax))
            navigation.m_Views[graph] = state;
    };
    if (value.contains("view"))
        load(blueprint->Graphs().front()->GetId(), value["view"]);
    if (auto views = value.find("views"); views && views->is_object())
    {
        for (auto& [key, view] : views->get<crude_json::object>())
        {
            uintptr_t graph = 0;
            if (ParseId(crude_json::value(std::strtod(key.c_str(), nullptr)), graph) && blueprint->FindGraph(static_cast<crude_blueprint::Id>(graph)))
                load(static_cast<crude_blueprint::Id>(graph), view);
        }
    }

    m_Blueprint       = std::move(blueprint);
    m_DocumentState   = std::move(state);
    m_NavigationState = navigation;
    m_ShownGraph      = 0;
    m_Undo.clear();
    m_Redo.clear();
    m_IsModified = false;
    ++m_Revision;
    return true;
}

bool blueprint_editor::Document::Load(std::string_view path)
{
    auto loadResult = crude_json::value::load(std::string(path));
    if (!loadResult.second)
        return false;

    return Deserialize(loadResult.first);
}

bool blueprint_editor::Document::Save(std::string_view path) const
{
    auto result = Serialize();

    // Only a file the document can be loaded from again replaces the one at 'path'.
    Document check(m_Registry, std::make_shared<crude_blueprint::TypeTable>());
    if (!check.Deserialize(crude_json::value::parse(result.dump())))
        return false;

    return result.save(std::string(path));
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
    result.m_BlueprintState = crude_blueprint::Save(*m_Blueprint);

    // The editor still knows the nodes undo took out of the blueprint; the document doesn't.
    auto& shown = GetGraph();
    std::vector<ed::NodeState> nodes(static_cast<size_t>(ed::GetNodeStates(nullptr, 0)));
    nodes.resize(static_cast<size_t>(ed::GetNodeStates(nodes.data(), static_cast<int>(nodes.size()))));
    for (auto& node : nodes)
        if (shown.FindNode(static_cast<crude_blueprint::Id>(node.Id.Get())))
            result.m_NodesState.push_back(node);
    for (auto& node : m_DocumentState.m_NodesState)
        if (!shown.FindNode(static_cast<crude_blueprint::Id>(node.Id.Get())) && HasNode(*m_Blueprint, node.Id.Get()))
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
    if (auto view = m_NavigationState.m_Views.find(GetGraph().GetId()); view != m_NavigationState.m_Views.end())
        ed::SetViewState(view->second);
}

bool blueprint_editor::Document::ApplyState(const DocumentState& state)
{
    if (state.m_BlueprintState.dump() != crude_blueprint::Save(*m_Blueprint).dump())
    {
        auto blueprint = LoadBlueprint(state.m_BlueprintState);
        if (!blueprint)
            return false;
        m_Blueprint = std::move(blueprint);
        ++m_Revision;
    }

    m_DocumentState = state;
    ApplyLayout();
    return true;
}

void blueprint_editor::Document::ApplyLayout()
{
    auto& shown = GetGraph();
    std::vector<ed::NodeState> nodes;
    for (auto& node : m_DocumentState.m_NodesState)
        if (shown.FindNode(static_cast<crude_blueprint::Id>(node.Id.Get())))
            nodes.push_back(node);
    std::vector<ed::NodeId> selectedNodes;
    std::vector<ed::LinkId> selectedLinks;
    for (auto node : m_DocumentState.m_SelectedNodes)
        if (shown.FindNode(static_cast<crude_blueprint::Id>(node.Get())))
            selectedNodes.push_back(node);
    for (auto link : m_DocumentState.m_SelectedLinks)
        if (shown.FindLink(static_cast<crude_blueprint::Id>(link.Get())))
            selectedLinks.push_back(link);
    ed::SetNodeStates(nodes.data(), static_cast<int>(nodes.size()));
    ed::SetSelection(selectedNodes.data(), static_cast<int>(selectedNodes.size()), selectedLinks.data(), static_cast<int>(selectedLinks.size()));
}

void blueprint_editor::Document::OnMakeCurrent()
{
    ApplyLayout();
    ApplyState(m_NavigationState);
}

void blueprint_editor::Document::CaptureView()
{
    m_NavigationState.m_Views[GetGraph().GetId()] = ed::GetViewState();
}

crude_blueprint::Graph& blueprint_editor::Document::GetGraph()
{
    auto graph = m_Blueprint->FindGraph(m_ShownGraph);
    return graph ? *graph : *m_Blueprint->Graphs().front();
}

const crude_blueprint::Graph& blueprint_editor::Document::GetGraph() const
{
    auto graph = m_Blueprint->FindGraph(m_ShownGraph);
    return graph ? *graph : *m_Blueprint->Graphs().front();
}

// A blueprint that doesn't load is logged with the reason, and a document of no graph doesn't
// load either: the editor shows the first.
std::unique_ptr<crude_blueprint::Blueprint> blueprint_editor::Document::LoadBlueprint(const crude_json::value& state) const
{
    auto blueprint = std::make_unique<crude_blueprint::Blueprint>(m_Registry, m_Types);
    if (auto loaded = crude_blueprint::Load(*blueprint, state); !loaded)
    {
        LOGE("The blueprint doesn't load: %.*s", static_cast<int>(loaded.GetError().Message().size()), loaded.GetError().Message().data());
        return nullptr;
    }
    if (blueprint->Graphs().empty())
    {
        LOGE("The blueprint has no graph");
        return nullptr;
    }
    return blueprint;
}

std::shared_ptr<const crude_blueprint::Registry> blueprint_editor::ExampleRegistry()
{
    using namespace crude_blueprint;

    static const std::shared_ptr<const Registry> registry = []
    {
        auto made    = std::make_shared<Registry>();
        auto vector2 = std::make_unique<StructType>("example.Vector2", std::vector<StructField>{ { "x", &FloatType() }, { "y", &FloatType() } });
        auto& type   = *vector2;
        [[maybe_unused]] auto builtins = RegisterBuiltins(*made);
        [[maybe_unused]] auto library  = RegisterLibrary(*made);
        [[maybe_unused]] auto added    = made->AddValueType(std::move(vector2));
        [[maybe_unused]] auto nodes    = RegisterStructNodes(*made, type);
        IM_ASSERT(builtins && library && added && nodes);
        return made;
    }();
    return registry;
}

void blueprint_editor::CreateExampleBlueprint(Document& document)
{
    using namespace crude_blueprint;

    auto& graph = document.GetGraph();
    auto add = [&graph](const char* type, ImVec2 position, NodeConfig config = {})
    {
        auto node = graph.AddNode(type, std::move(config));
        IM_ASSERT(node);
        ed::SetNodePosition(node.ValueOr(0), position);
        return node.ValueOr(0);
    };
    auto pin = [&graph](Id node, std::string_view key)
    {
        auto found = graph.FindNode(node) ? graph.FindNode(node)->FindPin(key) : nullptr;
        IM_ASSERT(found);
        return found ? found->m_Id : 0;
    };
    auto link = [&graph, &pin](Id from, std::string_view output, Id to, std::string_view input, LinkMode mode = LinkMode::Plain)
    {
        [[maybe_unused]] auto made = graph.Connect(pin(from, output), pin(to, input), mode);
        IM_ASSERT(made);
    };
    auto constant = [](Value value)
    {
        NodeConfig config;
        config.m_Types["T"]     = value.GetType();
        config.m_Data["value"] = std::move(value);
        return config;
    };

    auto start = add("crude.Start",    ImVec2( 20,  80));
    auto loop  = add("crude.ForLoop",  ImVec2(200,  60));
    auto print = add("crude.Print",    ImVec2(760,  40));
    auto sum   = add("crude.Add",      ImVec2(480, 300));
    auto ten   = add("crude.Constant", ImVec2(260, 380), constant(Value::Int32(10)));
    auto done  = add("crude.Print",    ImVec2(760, 180));

    link(start, "then", loop, "enter");
    link(loop, "body", print, "enter");
    link(loop, "index", sum, "in.0");
    link(ten, "value", sum, "in.1");
    link(sum, "result", print, "text", LinkMode::Cast);
    link(loop, "completed", done, "enter");
    [[maybe_unused]] auto last = graph.SetValue(pin(loop, "last"), Value::Int32(4));
    [[maybe_unused]] auto text = graph.SetValue(pin(done, "text"), Value::String("Done"));
    IM_ASSERT(last && text);

    // The state the first undo step goes back to.
    document.m_DocumentState = document.BuildDocumentState();
}
