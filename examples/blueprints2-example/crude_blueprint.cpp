# include "crude_blueprint.h"
# include "crude_blueprint_library.h"
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

crude_blueprint::Pin::Pin(Node* node, PinType type, string_view name)
    : m_Id(node ? node->m_Blueprint->MakePinId(this) : 0)
    , m_Node(node)
    , m_Type(type)
    , m_Name(name)
{
}

crude_blueprint::PinType crude_blueprint::Pin::GetType() const
{
    return m_Type;
}

crude_blueprint::LinkQueryResult crude_blueprint::Pin::CanLinkTo(const Pin& pin) const
{
    auto result = m_Node->AcceptLink(*this, pin);
    if (!result)
        return result;

    auto result2 = pin.m_Node->AcceptLink(*this, pin);
    if (!result2)
        return result2;

    if (result.Reason().empty())
        return result2;

    return result;
}

bool crude_blueprint::Pin::LinkTo(const Pin& pin)
{
    if (!CanLinkTo(pin))
        return false;

    if (m_Link)
        Unlink();

    m_Link = &pin;

    m_Node->WasLinked(*this, pin);
    pin.m_Node->WasLinked(*this, pin);

    return true;
}

void crude_blueprint::Pin::Unlink()
{
    if (m_Link == nullptr)
        return;

    auto link = m_Link;

    m_Link = nullptr;

    m_Node->WasUnlinked(*this, *link);
    link->m_Node->WasUnlinked(*this, *link);
}

bool crude_blueprint::Pin::IsLinked() const
{
    return m_Link != nullptr;
}

const crude_blueprint::Pin* crude_blueprint::Pin::GetLink() const
{
    return m_Link;
}

bool crude_blueprint::Pin::IsInput() const
{
    for (auto pin : m_Node->GetInputPins())
        if (pin->m_Id == m_Id)
            return true;

    return false;
}

bool crude_blueprint::Pin::IsOutput() const
{
    for (auto pin : m_Node->GetOutputPins())
        if (pin->m_Id == m_Id)
            return true;

    return false;
}

bool crude_blueprint::Pin::IsProvider() const
{
    auto outputToInput = (GetValueType() != PinType::Flow);

    auto pins = outputToInput ? m_Node->GetOutputPins() : m_Node->GetInputPins();

    for (auto pin : pins)
        if (pin->m_Id == m_Id)
            return true;

    return false;
}

bool crude_blueprint::Pin::IsReceiver() const
{
    auto outputToInput = (GetValueType() != PinType::Flow);

    auto pins = outputToInput ? m_Node->GetInputPins() : m_Node->GetOutputPins();

    for (auto pin : pins)
        if (pin->m_Id == m_Id)
            return true;

    return false;
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

crude_blueprint::PinType crude_blueprint::Pin::GetValueType() const
{
    return m_Type;
}

crude_blueprint::PinValue crude_blueprint::Pin::GetValue() const
{
    return PinValue{};
}



//
// -------[ Pins Serialization ]-------
//

bool crude_blueprint::AnyPin::SetValueType(PinType type)
{
    if (GetValueType() == type)
        return true;

    if (m_InnerPin)
    {
        m_Node->m_Blueprint->ForgetPin(m_InnerPin.get());
        m_InnerPin.reset();
    }

    if (type == PinType::Any)
        return true;

    m_InnerPin = m_Node->CreatePin(type);

    auto links = m_Node->m_Blueprint->FindPinsLinkedTo(*this);
    for (auto link : links)
    {
        link->Unlink();
        if (link->SetValueType(type))
            link->LinkTo(*this);
    }

    return true;
}

bool crude_blueprint::AnyPin::SetValue(PinValue value)
{
    if (!m_InnerPin)
        return false;

    return m_InnerPin->SetValue(std::move(value));
}

bool crude_blueprint::AnyPin::Load(const crude_json::value& value)
{
    if (!Pin::Load(value))
        return false;

    PinType type = PinType::Any;
    if (!detail::GetTo<double>(value, "type", type))
        return false;

    if (type != PinType::Any)
    {
        m_InnerPin = m_Node->CreatePin(type);

        if (!value.contains("inner"))
            return false;

        if (!m_InnerPin->Load(value["inner"]))
            return false;
    }

    return true;
}

void crude_blueprint::AnyPin::Save(crude_json::value& value) const
{
    Pin::Save(value);

    value["type"] = static_cast<double>(GetValueType());
    if (m_InnerPin)
         m_InnerPin->Save(value["inner"]);
}

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

crude_blueprint::unique_ptr<crude_blueprint::Pin> crude_blueprint::Node::CreatePin(PinType pinType, string_view name)
{
    switch (pinType)
    {
        case PinType::Void:     return nullptr;
        case PinType::Any:      return make_unique<AnyPin>(this, name);
        case PinType::Flow:     return make_unique<FlowPin>(this, name);
        case PinType::Bool:     return make_unique<BoolPin>(this, name);
        case PinType::Int32:    return make_unique<Int32Pin>(this, name);
        case PinType::Float:    return make_unique<FloatPin>(this, name);
        case PinType::String:   return make_unique<StringPin>(this, name);
    }

    return nullptr;
}

crude_blueprint::LinkQueryResult crude_blueprint::Node::AcceptLink(const Pin& receiver, const Pin& provider) const
{
    if (receiver.m_Node == provider.m_Node)
        return { false, "Pins of same node cannot be connected"};

    const auto receiverIsFlow = receiver.GetType() == PinType::Flow;
    const auto providerIsFlow = provider.GetType() == PinType::Flow;
    if (receiverIsFlow != providerIsFlow)
        return { false, "Flow pins can be connected only to other Flow pins"};

    if (receiver.IsInput() && provider.IsInput())
        return { false, "Input pins cannot be linked together"};

    if (receiver.IsOutput() && provider.IsOutput())
        return { false, "Output pins cannot be linked together"};

    if (!receiver.IsReceiver())
        return { false, "Receiver pin cannot be used as provider"};

    if (!provider.IsProvider())
        return { false, "Provider pin cannot be used as receiver"};

    if (provider.GetValueType() != receiver.GetValueType() && (provider.GetValueType() != PinType::Any && receiver.GetValueType() != PinType::Any))
        return { false, "Incompatible types"};

    return {true};
}

void crude_blueprint::Node::WasLinked(const Pin& receiver, const Pin& provider)
{
}

void crude_blueprint::Node::WasUnlinked(const Pin& receiver, const Pin& provider)
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


void crude_blueprint::Context::SetContextMonitor(ContextMonitor* monitor)
{
    m_Monitor = monitor;
}

crude_blueprint::ContextMonitor* crude_blueprint::Context::GetContextMonitor()
{
    return m_Monitor;
}

const crude_blueprint::ContextMonitor* crude_blueprint::Context::GetContextMonitor() const
{
    return m_Monitor;
}

void crude_blueprint::Context::Reset()
{
    m_Values.clear();
}

crude_blueprint::StepResult crude_blueprint::Context::Start(FlowPin& entryPoint)
{
    m_Callstack.resize(0);
    m_CurrentNode = entryPoint.m_Node;
    m_CurrentFlowPin = entryPoint;
    m_StepCount = 0;

    if (m_Monitor)
        m_Monitor->OnStart(*this);

    if (m_CurrentNode == nullptr || m_CurrentFlowPin.m_Id == 0)
        return SetStepResult(StepResult::Error);

    return SetStepResult(StepResult::Success);
}

crude_blueprint::StepResult crude_blueprint::Context::Step()
{
    if (m_LastResult != StepResult::Success)
        return m_LastResult;

    auto currentFlowPin = m_CurrentFlowPin;

    m_CurrentNode = nullptr;
    m_CurrentFlowPin = {};

    if (currentFlowPin.m_Id == 0 && m_Callstack.empty())
        return SetStepResult(StepResult::Done);

    auto entryPoint = GetPinValue(currentFlowPin);

    if (entryPoint.GetType() != PinType::Flow)
        return SetStepResult(StepResult::Error);

    auto entryPin = entryPoint.As<FlowPin*>();

    m_CurrentNode = entryPin->m_Node;

    ++m_StepCount;

    if (m_Monitor)
        m_Monitor->OnPreStep(*this);

    auto next = entryPin->m_Node->Execute(*this, *entryPin);

    if (next.m_Link && next.m_Link->m_Type == PinType::Flow)
    {
        m_CurrentFlowPin = next;
    }
    else if (!m_Callstack.empty())
    {
        m_CurrentFlowPin = m_Callstack.back();
        m_Callstack.pop_back();
    }

    if (m_Monitor)
        m_Monitor->OnPostStep(*this);

    return SetStepResult(StepResult::Success);
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

void crude_blueprint::Context::PushReturnPoint(FlowPin& entryPoint)
{
    m_Callstack.push_back(entryPoint);
}

void crude_blueprint::Context::SetPinValue(const Pin& pin, PinValue value)
{
    m_Values[pin.m_Id] = std::move(value);
}

crude_blueprint::PinValue crude_blueprint::Context::GetPinValue(const Pin& pin) const
{
    if (m_Monitor)
        m_Monitor->OnEvaluatePin(*this, pin);

    auto valueIt = m_Values.find(pin.m_Id);
    if (valueIt != m_Values.end())
        return valueIt->second;

    PinValue value;
    if (pin.m_Link)
        value = GetPinValue(*pin.m_Link);
    else if (pin.m_Node)
        value = pin.m_Node->EvaluatePin(*this, pin);
    else
        value = pin.GetValue();

    return std::move(value);
}

crude_blueprint::StepResult crude_blueprint::Context::SetStepResult(StepResult result)
{
    m_LastResult = result;

    if (m_Monitor)
    {
        switch (result)
        {
            case StepResult::Done:
                m_Monitor->OnDone(*this);
                break;

            case StepResult::Error:
                m_Monitor->OnError(*this);
                break;

            default:
                break;
        }
    }

    return result;
}




//
// -------[ NodeRegistry ]-------
//

crude_blueprint::NodeRegistry::NodeRegistry()
    : m_BuildInNodes(
        {
            ConstBoolNode::GetStaticTypeInfo(),
            ConstInt32Node::GetStaticTypeInfo(),
            ConstFloatNode::GetStaticTypeInfo(),
            ConstStringNode::GetStaticTypeInfo(),
            BranchNode::GetStaticTypeInfo(),
            DoNNode::GetStaticTypeInfo(),
            DoOnceNode::GetStaticTypeInfo(),
            FlipFlopNode::GetStaticTypeInfo(),
            ForLoopNode::GetStaticTypeInfo(),
            GateNode::GetStaticTypeInfo(),
            ToStringNode::GetStaticTypeInfo(),
            PrintNode::GetStaticTypeInfo(),
            EntryPointNode::GetStaticTypeInfo(),
            AddNode::GetStaticTypeInfo(),
        })
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

void crude_blueprint::Blueprint::ForgetPin(Pin* pin)
{
    auto pinIt = std::find(m_Pins.begin(), m_Pins.end(), pin);
    if (pinIt == m_Pins.end())
        return;

    m_Pins.erase(pinIt);
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

//crude_blueprint::Context& crude_blueprint::Blueprint::GetContext()
//{
//    return m_Context;
//}

const crude_blueprint::Context& crude_blueprint::Blueprint::GetContext() const
{
    return m_Context;
}

void crude_blueprint::Blueprint::SetContextMonitor(ContextMonitor* monitor)
{
    m_Context.SetContextMonitor(monitor);
}

crude_blueprint::ContextMonitor* crude_blueprint::Blueprint::GetContextMonitor()
{
    return m_Context.GetContextMonitor();
}

const crude_blueprint::ContextMonitor* crude_blueprint::Blueprint::GetContextMonitor() const
{
    return m_Context.GetContextMonitor();
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

bool crude_blueprint::Blueprint::HasPinAnyLink(const Pin& pin) const
{
    if (pin.IsLinked())
        return true;

    for (auto& p : m_Pins)
    {
        auto linkedPin = p->GetLink();

        if (linkedPin && linkedPin->m_Id == pin.m_Id)
            return true;
    }

    return false;
}

crude_blueprint::vector<crude_blueprint::Pin*> crude_blueprint::Blueprint::FindPinsLinkedTo(const Pin& pin) const
{
    vector<Pin*> result;

    for (auto& p : m_Pins)
    {
        auto linkedPin = p->GetLink();

        if (linkedPin && linkedPin->m_Id == pin.m_Id)
            result.push_back(p);
    }

    return result;
}

void crude_blueprint::Blueprint::ResetState()
{
    m_Context.Reset();

    for (auto node : m_Nodes)
        node->Reset(m_Context);
}
