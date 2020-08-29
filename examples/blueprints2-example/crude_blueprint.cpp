# include "crude_blueprint.h"
# include <crude_json.h>
# include <algorithm>
# include <map>
# include <stdio.h>
//# include <stdlib.h>
//# include <inttypes.h>


namespace crude_blueprint { namespace detail {

//inline string ToString(uint32_t value)
//{
//    // buffer size is:
//    //   + "4294967295" - longest value possible
//    //   + terminating zero
//    //   + extra zero for platform differences in snprintf()
//    char buffer[12] = {};
//    snprintf(buffer, 11, "%" PRIu32, value);
//    return buffer;
//}

template <typename T>
inline bool GetPtrTo(const crude_json::value& value, string_view key, const T*& result)
{
    auto keyString = key.to_string();
    if (!value.contains(keyString))
        return false;

    auto& valueObject = value[keyString];
    auto valuePtr = valueObject.get_ptr<T>();
    if (valuePtr == nullptr)
        return false;

    result = valuePtr;

    return true;
};

template <typename T, typename V>
inline bool GetTo(const crude_json::value& value, string_view key, V& result)
{
    const T* valuePtr = nullptr;
    if (!GetPtrTo(value, key, valuePtr))
        return false;

    result = static_cast<V>(*valuePtr);

    return true;
};


} } // namespace crude_blueprint { namespace detail {




//
// -------[ IdGenerator ]-------
//

uint32_t crude_blueprint::IdGenerator::GenerateId()
{
    return m_State++;
}

void crude_blueprint::IdGenerator::SetState(uint32_t state)
{
    m_State = state;
}

uint32_t crude_blueprint::IdGenerator::State() const
{
    return m_State;
}



//
// -------[ Pin ]-------
//

crude_blueprint::Pin::Pin(Node* node, PinType type)
    : Pin(node, type, "")
{
}

crude_blueprint::Pin::Pin(Node* node, PinType type, string_view name)
    : m_Id(node ? node->m_Blueprint->MakePinId(this) : 0)
    , m_Node(node)
    , m_Type(type)
    , m_Name(name)
{
}

crude_blueprint::PinValue crude_blueprint::Pin::GetValue() const
{
    if (m_Link)
        return m_Link->GetValue();
    else
        return GetImmediateValue();
}

crude_blueprint::PinType crude_blueprint::Pin::GetType() const
{
    if (m_Link)
        return m_Link->GetType();
    else
        return m_Type;
}

bool crude_blueprint::Pin::Load(const crude_json::value& value)
{
    if (!detail::GetTo<crude_json::number>(value, "id", m_Id)) // required
        return false;

    uint32_t linkId;
    if (detail::GetTo<crude_json::number>(value, "link", linkId)) // optional
    {
        static_assert(sizeof(linkId) <= sizeof(void*), "Pin ID is expected to fit into the pointer.");
        // HACK: We store raw ID here, Blueprint::Load will expand it to valid pointer.
        m_Link = reinterpret_cast<Pin*>(static_cast<uintptr_t>(linkId));
    }

    return true;
}

void crude_blueprint::Pin::Save(crude_json::value& value) const
{
    value["id"] = crude_json::number(m_Id); // required
    if (!m_Name.empty())
        value["name"] = m_Name.to_string();  // optional, to make data readable for humans
    if (m_Link)
        value["link"] = crude_json::number(m_Link->m_Id);
}

crude_blueprint::PinValue crude_blueprint::Pin::GetImmediateValue() const
{
    return PinValue{};
}



//
// -------[ Pins Serialization ]-------
//

bool crude_blueprint::BoolPin::Load(const crude_json::value& value)
{
    if (!Pin::Load(value))
        return false;

    if (!detail::GetTo<bool>(value, "value", m_Value)) // required
        return false;

    return true;
}

void crude_blueprint::BoolPin::Save(crude_json::value& value) const
{
    Pin::Save(value);

    value["value"] = m_Value; // required
}

bool crude_blueprint::Int32Pin::Load(const crude_json::value& value)
{
    if (!Pin::Load(value))
        return false;

    if (!detail::GetTo<crude_json::number>(value, "value", m_Value)) // required
        return false;

    return true;
}

void crude_blueprint::Int32Pin::Save(crude_json::value& value) const
{
    Pin::Save(value);

    value["value"] = crude_json::number(m_Value); // required
}

bool crude_blueprint::FloatPin::Load(const crude_json::value& value)
{
    if (!Pin::Load(value))
        return false;

    if (!detail::GetTo<crude_json::number>(value, "value", m_Value)) // required
        return false;

    return true;
}

void crude_blueprint::FloatPin::Save(crude_json::value& value) const
{
    Pin::Save(value);

    value["value"] = crude_json::number(m_Value); // required
}

void crude_blueprint::StringPin::Save(crude_json::value& value) const
{
    Pin::Save(value);

    value["value"] = m_Value; // required
}

bool crude_blueprint::StringPin::Load(const crude_json::value& value)
{
    if (!Pin::Load(value))
        return false;

    if (!detail::GetTo<crude_json::string>(value, "value", m_Value)) // required
        return false;

    return true;
}



//
// -------[ Node ]-------
//

crude_blueprint::Node::Node(Blueprint& blueprint, string_view name)
    : m_Id(blueprint.MakeNodeId(this))
    , m_Name(name)
    , m_Blueprint(&blueprint)
{
}

bool crude_blueprint::Node::Load(const crude_json::value& value)
{
    if (!value.is_object())
        return false;

    if (!detail::GetTo<crude_json::number>(value, "id", m_Id)) // required
        return false;

    const crude_json::array* inputPinsArray = nullptr;
    if (detail::GetPtrTo(value, "input_pins", inputPinsArray)) // optional
    {
        auto pins = GetInputPins();

        if (pins.size() != inputPinsArray->size())
            return false;

        auto pin = pins.data();
        for (auto& pinValue : *inputPinsArray)
        {
            if (!(*pin)->Load(pinValue))
                return false;

            ++pin;
        }
    }

    const crude_json::array* outputPinsArray = nullptr;
    if (detail::GetPtrTo(value, "output_pins", outputPinsArray)) // optional
    {
        auto pins = GetOutputPins();

        if (pins.size() != outputPinsArray->size())
            return false;

        auto pin = pins.data();
        for (auto& pinValue : *outputPinsArray)
        {
            if (!(*pin)->Load(pinValue))
                return false;

            ++pin;
        }
    }

    return true;
}

void crude_blueprint::Node::Save(crude_json::value& value) const
{
    value["id"] = crude_json::number(m_Id); // required
    value["name"] = m_Name.to_string(); // optional, to make data readable for humans

    auto& inputPinsValue = value["input_pins"]; // optional
    for (auto& pin : const_cast<Node*>(this)->GetInputPins())
    {
        crude_json::value pinValue;
        pin->Save(pinValue);
        inputPinsValue.push_back(pinValue);
    }
    if (inputPinsValue.is_null())
        value.erase("input_pins");

    auto& outputPinsValue = value["output_pins"]; // optional
    for (auto& pin : const_cast<Node*>(this)->GetOutputPins())
    {
        crude_json::value pinValue;
        pin->Save(pinValue);
        outputPinsValue.push_back(pinValue);
    }
    if (outputPinsValue.is_null())
        value.erase("output_pins");
}



//
// -------[ Context ]-------
//

void crude_blueprint::Context::Start(FlowPin& entryPoint)
{
    m_Queue.resize(0);
    m_Queue.push_back(entryPoint);
    m_CurrentNode = entryPoint.m_Node;
    m_CurrentFlowPin = entryPoint;
    m_StepCount = 0;
    if (m_CurrentNode == nullptr || m_CurrentFlowPin.m_Id == 0)
        SetStepResult(StepResult::Error);

    SetStepResult(StepResult::Success);
}

crude_blueprint::StepResult crude_blueprint::Context::Step()
{
    if (m_LastResult != StepResult::Success)
        return m_LastResult;

    m_CurrentNode = nullptr;
    m_CurrentFlowPin = {};

    if (m_Queue.empty())
        return SetStepResult(StepResult::Done);

    auto entryPoint = m_Queue.back();
    m_Queue.pop_back();

    if (entryPoint.m_Type != PinType::Flow)
        return SetStepResult(StepResult::Error);

    m_CurrentNode = entryPoint.m_Node;

    ++m_StepCount;

    entryPoint.m_Node->m_Blueprint->TouchPin(entryPoint);

    auto next = entryPoint.m_Node->Execute(*this, entryPoint);
    if (next.m_Link && next.m_Link->m_Type == PinType::Flow)
    {
        m_CurrentFlowPin = next;
        m_Queue.push_back(*static_cast<FlowPin*>(next.m_Link));
    }
    else
    {
        if (!m_Queue.empty())
            m_CurrentFlowPin = m_Queue.back();
    }

    return SetStepResult(StepResult::Success);
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

crude_blueprint::Node* crude_blueprint::Context::CurrentNode()
{
    return m_CurrentNode;
}

const crude_blueprint::Node* crude_blueprint::Context::CurrentNode() const
{
    return m_CurrentNode;
}

crude_blueprint::Node* crude_blueprint::Context::NextNode()
{
    return m_CurrentFlowPin.m_Link ? m_CurrentFlowPin.m_Link->m_Node : m_CurrentFlowPin.m_Node;
}

const crude_blueprint::Node* crude_blueprint::Context::NextNode() const
{
    return m_CurrentFlowPin.m_Link ? m_CurrentFlowPin.m_Link->m_Node : m_CurrentFlowPin.m_Node;
}

crude_blueprint::FlowPin crude_blueprint::Context::CurrentFlowPin() const
{
    return m_CurrentFlowPin;
}

crude_blueprint::StepResult crude_blueprint::Context::LastStepResult() const
{
    return m_LastResult;
}

uint32_t crude_blueprint::Context::StepCount() const
{
    return m_StepCount;
}

crude_blueprint::StepResult crude_blueprint::Context::SetStepResult(StepResult result)
{
    m_LastResult = result;
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

crude_blueprint::Node* crude_blueprint::NodeRegistry::Create(uint32_t typeId, Blueprint& blueprint)
{
    for (auto& nodeInfo : m_Types)
    {
        if (nodeInfo->m_Id != typeId)
            continue;

        return nodeInfo->m_Factory(blueprint);
    }

    return nullptr;
}

crude_blueprint::Node* crude_blueprint::NodeRegistry::Create(string_view typeName, Blueprint& blueprint)
{
    for (auto& nodeInfo : m_Types)
    {
        if (nodeInfo->m_Name != typeName)
            continue;

        return nodeInfo->m_Factory(blueprint);
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
    if (!m_NodeRegistry)
        m_NodeRegistry = make_shared<NodeRegistry>();
}

crude_blueprint::Blueprint::Blueprint(const Blueprint& other)
    : m_NodeRegistry(other.m_NodeRegistry)
    , m_Context(other.m_Context)
{
    crude_json::value value;
    other.Save(value);
    Load(value);
}

crude_blueprint::Blueprint::Blueprint(Blueprint&& other)
    : m_NodeRegistry(std::move(other.m_NodeRegistry))
    , m_Generator(std::move(other.m_Generator))
    , m_Nodes(std::move(other.m_Nodes))
    , m_Pins(std::move(other.m_Pins))
    , m_Context(std::move(other.m_Context))
{
}

crude_blueprint::Blueprint::~Blueprint()
{
    Clear();
}


crude_blueprint::Blueprint& crude_blueprint::Blueprint::operator=(const Blueprint& other)
{
    if (this == &other)
        return *this;

    Clear();

    m_NodeRegistry = other.m_NodeRegistry;
    m_Context = other.m_Context;

    crude_json::value value;
    other.Save(value);
    Load(value);

    return *this;
}

crude_blueprint::Blueprint& crude_blueprint::Blueprint::operator=(Blueprint&& other)
{
    if (this == &other)
        return *this;

    m_NodeRegistry = std::move(other.m_NodeRegistry);
    m_Generator    = std::move(other.m_Generator);
    m_Nodes        = std::move(other.m_Nodes);
    m_Pins         = std::move(other.m_Pins);
    m_Context      = std::move(other.m_Context);

    return *this;
}

crude_blueprint::Node* crude_blueprint::Blueprint::CreateNode(uint32_t nodeTypeId)
{
    if (!m_NodeRegistry)
        return nullptr;

    auto node = m_NodeRegistry->Create(nodeTypeId, *this);
    if (!node)
        return nullptr;

    m_Nodes.push_back(node);

    return node;
}

crude_blueprint::Node* crude_blueprint::Blueprint::CreateNode(string_view nodeTypeName)
{
    if (!m_NodeRegistry)
        return nullptr;

    auto node = m_NodeRegistry->Create(nodeTypeName, *this);
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

void crude_blueprint::Blueprint::Clear()
{
    for (auto node : m_Nodes)
        delete node;

    m_Nodes.resize(0);
    m_Pins.resize(0);
    m_Generator = IdGenerator();
    m_Context = Context();
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

crude_blueprint::span<crude_blueprint::Pin*> crude_blueprint::Blueprint::GetPins()
{
    return m_Pins;
}

crude_blueprint::span<const crude_blueprint::Pin* const> crude_blueprint::Blueprint::GetPins() const
{
    const Pin* const* begin = m_Pins.data();
    const Pin* const* end   = m_Pins.data() + m_Pins.size();
    return make_span(begin, end);
}

crude_blueprint::Pin* crude_blueprint::Blueprint::FindPin(uint32_t pinId)
{
    return const_cast<Pin*>(const_cast<const Blueprint*>(this)->FindPin(pinId));
}

const crude_blueprint::Pin* crude_blueprint::Blueprint::FindPin(uint32_t pinId) const
{
    for (auto& pin : m_Pins)
    {
        if (pin->m_Id == pinId)
            return pin;
    }

    return nullptr;
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

    ResetState();

    m_Context.Start(entryPointNode.m_Exit);
}

crude_blueprint::StepResult crude_blueprint::Blueprint::Step()
{
    m_TouchedPinIds.resize(0);

    return m_Context.Step();
}

crude_blueprint::StepResult crude_blueprint::Blueprint::Execute(EntryPointNode& entryPointNode)
{
    auto nodeIt = std::find(m_Nodes.begin(), m_Nodes.end(), static_cast<Node*>(&entryPointNode));
    if (nodeIt == m_Nodes.end())
        return StepResult::Error;

    ResetState();

    return m_Context.Execute(entryPointNode.m_Exit);
}

crude_blueprint::Node* crude_blueprint::Blueprint::CurrentNode()
{
    return m_Context.CurrentNode();
}

const crude_blueprint::Node* crude_blueprint::Blueprint::CurrentNode() const
{
    return m_Context.CurrentNode();
}

crude_blueprint::Node* crude_blueprint::Blueprint::NextNode()
{
    return m_Context.NextNode();
}

const crude_blueprint::Node* crude_blueprint::Blueprint::NextNode() const
{
    return m_Context.NextNode();
}

crude_blueprint::FlowPin crude_blueprint::Blueprint::CurrentFlowPin() const
{
    return m_Context.CurrentFlowPin();
}

crude_blueprint::StepResult crude_blueprint::Blueprint::LastStepResult() const
{
    return m_Context.LastStepResult();
}

bool crude_blueprint::Blueprint::Load(const crude_json::value& value)
{
    if (!value.is_object())
        return false;

    const crude_json::array* nodeArray = nullptr;
    if (!detail::GetPtrTo(value, "nodes", nodeArray)) // required
        return false;

    Blueprint blueprint{m_NodeRegistry};

    IdGenerator generator;
    std::map<uint32_t, Pin*> pinMap;

    for (auto& nodeValue : *nodeArray)
    {
        uint32_t typeId;
        if (!detail::GetTo<crude_json::number>(nodeValue, "type_id", typeId)) // required
            return false;

        auto node = m_NodeRegistry->Create(typeId, blueprint);
        if (!node)
            return false;

        blueprint.m_Nodes.push_back(node);

        if (!node->Load(nodeValue))
            return false;

        // Collect pins for m_Link resolver
        for (auto pin : node->GetInputPins())
            pinMap[pin->m_Id] = pin;
        for (auto pin : node->GetOutputPins())
            pinMap[pin->m_Id] = pin;
    }

    const crude_json::object* stateObject = nullptr;
    if (!detail::GetPtrTo(value, "state", stateObject)) // required
        return false;

    uint32_t generatorState = 0;
    if (!detail::GetTo<crude_json::number>(*stateObject, "generator_state", generatorState)) // required
        return false;

    // HACK: Pin::Load store pin ID in m_Link. Let's resolve ids to valid pointers.
    for (auto& entry : pinMap)
    {
        auto& pin = *entry.second;
        if (pin.m_Link == nullptr)
            continue;

        auto linkedPinId = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pin.m_Link));
        auto linketPinIt = pinMap.find(linkedPinId);
        if (linketPinIt == pinMap.end())
            return false;

        pin.m_Link = linketPinIt->second;
    }

    for (auto& node : blueprint.m_Nodes)
        node->m_Blueprint = this;

    Clear();

    m_Generator.SetState(generatorState);

    m_Nodes.swap(blueprint.m_Nodes);
    m_Pins.swap(blueprint.m_Pins);

    return true;
}

void crude_blueprint::Blueprint::Save(crude_json::value& value) const
{
    auto& nodesValue = value["nodes"]; // required
    nodesValue = crude_json::array();
    for (auto& node : m_Nodes)
    {
        crude_json::value nodeValue;

        nodeValue["type_id"] = crude_json::number(node->GetTypeInfo().m_Id); // required
        nodeValue["type_name"] = node->GetTypeInfo().m_Name.to_string(); // optional, to make data readable for humans

        node->Save(nodeValue);

        nodesValue.push_back(nodeValue);
    }

    auto& stateValue = value["state"]; // required
    stateValue["generator_state"] = crude_json::number(m_Generator.State()); // required
}

bool crude_blueprint::Blueprint::Load(string_view path)
{
    // Modern C++, so beautiful...
    unique_ptr<FILE, void(*)(FILE*)> file{nullptr, [](FILE* file) { if (file) fclose(file); }};
    file.reset(fopen(path.to_string().c_str(), "rb"));

    if (!file)
        return false;

    fseek(file.get(), 0, SEEK_END);
    auto size = static_cast<size_t>(ftell(file.get()));

    string data;
    data.resize(size);
    if (fread(const_cast<char*>(data.data()), size, 1, file.get()) != 1)
        return false;

    auto value = crude_json::value::parse(data);

    return Load(value);
}

bool crude_blueprint::Blueprint::Save(string_view path) const
{
    // Modern C++, so beautiful...
    unique_ptr<FILE, void(*)(FILE*)> file{nullptr, [](FILE* file) { if (file) fclose(file); }};
    file.reset(fopen(path.to_string().c_str(), "wb"));

    if (!file)
        return false;

    crude_json::value value;
    Save(value);

    auto data = value.dump(4);

    if (fwrite(data.data(), data.size(), 1, file.get()) != 1)
        return false;

    return true;
}

uint32_t crude_blueprint::Blueprint::MakeNodeId(Node* node)
{
    (void)node;
    return m_Generator.GenerateId();
}

uint32_t crude_blueprint::Blueprint::MakePinId(Pin* pin)
{
    m_Pins.push_back(pin);

    return m_Generator.GenerateId();
}

bool crude_blueprint::Blueprint::IsPinLinked(const Pin* pin) const
{
    if (pin->m_Link)
        return true;

    for (auto& p : m_Pins)
    {
        if (p->m_Link && p->m_Link->m_Id == pin->m_Id)
            return true;
    }

    return false;
}

void crude_blueprint::Blueprint::ResetState()
{
    for (auto node : m_Nodes)
        node->Reset();
}

void crude_blueprint::Blueprint::TouchPin(const Pin& pin)
{
    m_TouchedPinIds.push_back(pin.m_Id);
}