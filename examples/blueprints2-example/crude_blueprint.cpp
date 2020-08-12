# include "crude_blueprint.h"
# include <algorithm>


//
// -------[ IdGenerator ]-------
//

uint32_t crude_blueprint::IdGenerator::GenerateId()
{
    return (*m_State)++;
}

void crude_blueprint::IdGenerator::SetState(uint32_t state)
{
    *m_State = state;
}

uint32_t crude_blueprint::IdGenerator::State() const
{
    return *m_State;
}



//
// -------[ Pin ]-------
//

crude_blueprint::Pin::Pin(Node* node, PinType type)
    : Pin(node, type, "")
{
}

crude_blueprint::Pin::Pin(Node* node, PinType type, string_view name)
    : m_Id(node ? node->MakeUniquePinId() : 0)
    , m_Node(node)
    , m_Type(type)
    , m_Name(name)
{
}

crude_blueprint::PinValue crude_blueprint::Pin::GetValue(const Context& context) const
{
    if (m_Link)
        return context.GetPinValue(*m_Link);
    else
        return GetValue();
}

crude_blueprint::PinType crude_blueprint::Pin::GetType() const
{
    if (m_Link)
        return m_Link->GetType();
    else
        return m_Type;
}

crude_blueprint::string_view crude_blueprint::Pin::GetName() const
{
    return m_Name;
}

crude_blueprint::PinValue crude_blueprint::Pin::GetValue() const
{
    return PinValue{};
}



//
// -------[ Node ]-------
//

crude_blueprint::Node::Node(IdGenerator& idGenerator, string_view name)
    : m_Generator(idGenerator)
    , m_Id(m_Generator.GenerateId())
    , m_Name(name)
{
}

uint32_t crude_blueprint::Node::MakeUniquePinId()
{
    return m_Generator.GenerateId();
}



//
// -------[ Context ]-------
//

crude_blueprint::PinValue crude_blueprint::Context::GetPinValue(const Pin& pin) const
{
    return pin.m_Node->EvaluatePin(*this, pin);
}

void crude_blueprint::Context::Start(FlowPin& entryPoint)
{
    m_Queue.resize(0);
    m_Queue.push_back(entryPoint);
}

crude_blueprint::StepResult crude_blueprint::Context::Step()
{
    if (m_Queue.empty())
        return StepResult::Done;

    auto entryPoint = m_Queue.back();
    m_Queue.pop_back();

    if (entryPoint.m_Type != PinType::Flow)
        return StepResult::Error;

    auto next = entryPoint.m_Node->Execute(*this, entryPoint);
    if (next.m_Link && next.m_Link->m_Type == PinType::Flow)
        m_Queue.push_back(*static_cast<FlowPin*>(next.m_Link));

    return StepResult::Success;
}

void crude_blueprint::Context::PushReturnPoint(FlowPin& entryPoint)
{
    m_Queue.push_back(entryPoint);
}

crude_blueprint::StepResult crude_blueprint::Context::Execute(FlowPin& entryPoint)
{
    Start(entryPoint);

    crude_blueprint::StepResult result = StepResult::Done;
    while (true)
    {
        result = Step();
        if (result != StepResult::Success)
            break;
    }

    return result;
}



//
// -------[ NodeRegistry ]-------
//

crude_blueprint::NodeRegistry::NodeRegistry()
{
    RebuildTypes();
}

uint32_t crude_blueprint::NodeRegistry::RegisterNodeType(string_view name, NodeTypeInfo::Factory factory)
{
    auto id = detail::fnv_1a_hash(name.data(), name.size());

    auto it = std::find_if(m_CustomNodes.begin(), m_CustomNodes.end(), [id](const NodeTypeInfo& typeInfo)
    {
        return typeInfo.m_Id == id;
    });

    if (it != m_CustomNodes.end())
        m_CustomNodes.erase(it);

    NodeTypeInfo typeInfo;
    typeInfo.m_Id       = id;
    typeInfo.m_Name     = name.to_string();
    typeInfo.m_Factory  = factory;

    m_CustomNodes.push_back(std::move(typeInfo));

    RebuildTypes();

    return id;
}

void crude_blueprint::NodeRegistry::UnregisterNodeType(string_view name)
{
    auto it = std::find_if(m_CustomNodes.begin(), m_CustomNodes.end(), [name](const NodeTypeInfo& typeInfo)
    {
        return typeInfo.m_Name == name;
    });

    if (it == m_CustomNodes.end())
        return;

    m_CustomNodes.erase(it);

    RebuildTypes();
}

void crude_blueprint::NodeRegistry::RebuildTypes()
{
    m_Types.resize(0);
    m_Types.reserve(m_CustomNodes.size() + std::distance(std::begin(m_BuildInNodes), std::end(m_BuildInNodes)));

    for (auto& typeInfo : m_CustomNodes)
        m_Types.push_back(&typeInfo);

    for (auto& typeInfo : m_BuildInNodes)
        m_Types.push_back(&typeInfo);

    std::stable_sort(m_Types.begin(), m_Types.end(), [](const NodeTypeInfo* lhs, const NodeTypeInfo* rhs) { return lhs->m_Id < rhs->m_Id; });
    m_Types.erase(std::unique(m_Types.begin(), m_Types.end()), m_Types.end());
}

crude_blueprint::Node* crude_blueprint::NodeRegistry::Create(uint32_t id, IdGenerator& generator)
{
    for (auto& nodeInfo : m_Types)
    {
        if (nodeInfo->m_Id != id)
            continue;

        return nodeInfo->m_Factory(generator);
    }

    return nullptr;
}

crude_blueprint::Node* crude_blueprint::NodeRegistry::Create(string_view name, IdGenerator& generator)
{
    for (auto& nodeInfo : m_Types)
    {
        if (nodeInfo->m_Name != name)
            continue;

        return nodeInfo->m_Factory(generator);
    }

    return nullptr;
}

crude_blueprint::span<const crude_blueprint::NodeTypeInfo* const> crude_blueprint::NodeRegistry::GetTypes() const
{
    const NodeTypeInfo* const* begin = m_Types.data();
    const NodeTypeInfo* const* end   = m_Types.data() + m_Types.size();
    return make_span(begin, end);
}



//
// -------[ Blueprint ]-------
//

crude_blueprint::Blueprint::Blueprint(shared_ptr<NodeRegistry> nodeRegistry)
    : m_NodeRegistry(std::move(nodeRegistry))
{
}

crude_blueprint::Blueprint::~Blueprint()
{
    for (auto node : m_Nodes)
        delete node;
}

crude_blueprint::Node* crude_blueprint::Blueprint::CreateNode(string_view nodeName)
{
    if (!m_NodeRegistry)
        return nullptr;

    auto node = m_NodeRegistry->Create(nodeName, m_Generator);
    if (!node)
        return nullptr;

    m_Nodes.push_back(node);

    return node;
}

void crude_blueprint::Blueprint::DeleteNode(Node* node)
{
    auto nodeIt = std::find(m_Nodes.begin(), m_Nodes.end(), node);
    if (nodeIt == m_Nodes.end())
        return;

    delete *nodeIt;

    m_Nodes.erase(nodeIt);
}

crude_blueprint::span<crude_blueprint::Node*> crude_blueprint::Blueprint::GetNodes()
{
    return m_Nodes;
}

crude_blueprint::span<const crude_blueprint::Node* const> crude_blueprint::Blueprint::GetNodes() const
{
    const Node* const* begin = m_Nodes.data();
    const Node* const* end   = m_Nodes.data() + m_Nodes.size();
    return make_span(begin, end);
}

crude_blueprint::shared_ptr<crude_blueprint::NodeRegistry> crude_blueprint::Blueprint::GetNodeRegistry() const
{
    return m_NodeRegistry;
}

void crude_blueprint::Blueprint::Start(EntryPointNode& entryPointNode)
{
    auto nodeIt = std::find(m_Nodes.begin(), m_Nodes.end(), static_cast<Node*>(&entryPointNode));
    if (nodeIt == m_Nodes.end())
        return;

    m_Context.Start(entryPointNode.m_Exit);
}

crude_blueprint::StepResult crude_blueprint::Blueprint::Step()
{
    return m_Context.Step();
}

crude_blueprint::StepResult crude_blueprint::Blueprint::Execute(EntryPointNode& entryPointNode)
{
    auto nodeIt = std::find(m_Nodes.begin(), m_Nodes.end(), static_cast<Node*>(&entryPointNode));
    if (nodeIt == m_Nodes.end())
        return StepResult::Error;

    return m_Context.Execute(entryPointNode.m_Exit);
}
