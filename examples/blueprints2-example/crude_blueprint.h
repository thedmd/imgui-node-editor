// Simple blueprint implementation
//
// Simple mean - no fancy code unless necessary. It could be
// simpler if I had more time.
//
# pragma once
# define span_FEATURE_MAKE_SPAN 1
# include "nonstd/span.hpp" // span<>, make_span
# include "nonstd/string_view.hpp" // span<>, make_span
# include "nonstd/variant.hpp" // variant
# include <stddef.h> // size_t
# include <stdint.h> // intX_t, uintX_t
# include <string>
# include <vector>
# include <memory>
# include <map>

namespace crude_json {
struct value;
} // crude_json

namespace crude_blueprint {

using nonstd::span;
using nonstd::make_span;
using nonstd::string_view;
using nonstd::variant;
using nonstd::get;
using nonstd::get_if;
using nonstd::monostate;
using std::string;
using std::vector;
using std::map;
using std::shared_ptr;
using std::unique_ptr;
using std::make_shared;
using std::make_unique;

struct Pin;
struct FlowPin;
struct Node;
struct Context;
struct Blueprint;


enum class PinType: int32_t { Void = -1, Any, Flow, Bool, Int32, Float, String };

const char* PinTypeToString(PinType pinType);

struct PinValue
{
    using ValueType = variant<monostate, FlowPin*, bool, int32_t, float, string>;

    PinValue() = default;
    PinValue(const PinValue&) = default;
    PinValue(PinValue&&) = default;
    PinValue& operator=(const PinValue&) = default;
    PinValue& operator=(PinValue&&) = default;

    PinValue(FlowPin* pin): m_Value(pin) {}
    PinValue(bool value): m_Value(value) {}
    PinValue(int32_t value): m_Value(value) {}
    PinValue(float value): m_Value(value) {}
    PinValue(string&& value): m_Value(std::move(value)) {}
    PinValue(const string& value): m_Value(value) {}
    PinValue(const char* value): m_Value(string(value)) {}

    PinType GetType() const { return static_cast<PinType>(m_Value.index()); }

    template <typename T>
    T& As()
    {
        return get<T>(m_Value);
    }

    template <typename T>
    const T& As() const
    {
        return get<T>(m_Value);
    }

private:
    ValueType m_Value;
};



//
// -------[ Id Generator ]-------
//

struct IdGenerator
{
    uint32_t GenerateId();

    void SetState(uint32_t state);
    uint32_t State() const;

private:
    uint32_t m_State = 1;
};



//
// -------[ Generic Pin ]-------
//

struct LinkQueryResult
{
    LinkQueryResult(bool result, string_view reason = "")
        : m_Result(result)
        , m_Reason(reason.to_string())
    {
    }

    explicit operator bool() const { return m_Result; }

    bool Result() const { return m_Result; }
    const string& Reason() const { return m_Reason; }

private:
    bool    m_Result = false;
    string  m_Reason;
};

struct Pin
{
    Pin(Node* node, PinType type, string_view name = "");
    virtual ~Pin();

    virtual bool     SetValueType(PinType type) { return m_Type == type; } // By default, type of held value cannot be changed
    virtual PinType  GetValueType() const;                                 // Returns type of held value (may be different from GetType() for Any pin)
    virtual bool     SetValue(PinValue value) { return false; }            // Sets new value to be held by the pin (not all allow data to be modified)
    virtual PinValue GetValue() const;                                     // Returns value held by this pin

    PinType GetType() const; // Returns type of this pin (which may differ from the type of held value for AnyPin)

    // Links can be created from receivers to providers but not other way around.
    // This API deal with only this type of connection.
    // To find what receivers are connected to this node use
    // Blueprint::FindPinsLinkedTo() function.
    LinkQueryResult CanLinkTo(const Pin& pin) const; // Check if link between this and other pin can be created
    bool LinkTo(const Pin& pin);                     // Tries to create link to specified pin as a provider
    void Unlink();                                   // Breaks link from this receiver to provider
    bool IsLinked() const;                           // Returns true if this pin is linked to provider
    const Pin* GetLink() const;                      // Returns provider pin

    bool IsInput() const;  // Pin is on input side
    bool IsOutput() const; // Pin is on output side

    bool IsProvider() const; // Pin can provide data
    bool IsReceiver() const; // Pin can receive data

    virtual bool Load(const crude_json::value& value);
    virtual void Save(crude_json::value& value) const;

    uint32_t    m_Id   = 0;
    Node*       m_Node = nullptr;
    PinType     m_Type = PinType::Void;
    string_view m_Name;
    const Pin*  m_Link = nullptr;
};



//
// -------[ Concrete Pin Types ]-------
//

// AnyPin can morph into any other data pin while creating a link
struct AnyPin final : Pin
{
    static constexpr auto TypeId = PinType::Any;

    AnyPin(Node* node, string_view name = ""): Pin(node, PinType::Any, name) {}

    bool     SetValueType(PinType type) override;
    PinType  GetValueType() const override { return m_InnerPin ? m_InnerPin->GetValueType() : Pin::GetValueType(); }
    bool     SetValue(PinValue value) override;
    PinValue GetValue() const override { return m_InnerPin ? m_InnerPin->GetValue() : Pin::GetValue(); }

    bool Load(const crude_json::value& value) override;
    void Save(crude_json::value& value) const override;

    unique_ptr<Pin> m_InnerPin;
};

// FlowPin represent execution flow
struct FlowPin final : Pin
{
    static constexpr auto TypeId = PinType::Flow;

    FlowPin(): FlowPin(nullptr) {}
    FlowPin(Node* node): Pin(node, PinType::Flow) {}
    FlowPin(Node* node, string_view name): Pin(node, PinType::Flow, name) {}

    PinValue GetValue() const override { return const_cast<FlowPin*>(this); }
};

// Boolean type pin
struct BoolPin final : Pin
{
    static constexpr auto TypeId = PinType::Bool;

    BoolPin(Node* node, bool value = false)
        : Pin(node, PinType::Bool)
        , m_Value(value)
    {
    }

    // C++ implicitly convert literals to bool, this will intercept
    // such calls an do the right thing.
    template <size_t N>
    BoolPin(Node* node, const char (&name)[N], bool value = false)
        : Pin(node, PinType::Bool, name)
        , m_Value(value)
    {
    }

    BoolPin(Node* node, string_view name, bool value = false)
        : Pin(node, PinType::Bool, name)
        , m_Value(value)
    {
    }

    bool SetValue(PinValue value) override
    {
        if (value.GetType() != TypeId)
            return false;

        m_Value = value.As<bool>();

        return true;
    }

    PinValue GetValue() const override { return m_Value; }

    bool Load(const crude_json::value& value) override;
    void Save(crude_json::value& value) const override;

    bool m_Value = false;
};

// Integer type pin
struct Int32Pin final : Pin
{
    static constexpr auto TypeId = PinType::Int32;

    Int32Pin(Node* node, int32_t value = 0): Pin(node, PinType::Int32), m_Value(value) {}
    Int32Pin(Node* node, string_view name, int32_t value = 0): Pin(node, PinType::Int32, name), m_Value(value) {}

    bool SetValue(PinValue value) override
    {
        if (value.GetType() != TypeId)
            return false;

        m_Value = value.As<int32_t>();

        return true;
    }

    PinValue GetValue() const override { return m_Value; }

    bool Load(const crude_json::value& value) override;
    void Save(crude_json::value& value) const override;

    int32_t m_Value = 0;
};

// Floating point type pin
struct FloatPin final : Pin
{
    static constexpr auto TypeId = PinType::Float;

    FloatPin(Node* node, float value = 0.0f): Pin(node, PinType::Float), m_Value(value) {}
    FloatPin(Node* node, string_view name, float value = 0.0f): Pin(node, PinType::Float, name), m_Value(value) {}

    bool SetValue(PinValue value) override
    {
        if (value.GetType() != TypeId)
            return false;

        m_Value = value.As<float>();

        return true;
    }

    PinValue GetValue() const override { return m_Value; }

    bool Load(const crude_json::value& value) override;
    void Save(crude_json::value& value) const override;

    float m_Value = 0.0f;
};

// String type pin
struct StringPin final : Pin
{
    static constexpr auto TypeId = PinType::String;

    StringPin(Node* node, string value = ""): Pin(node, PinType::String), m_Value(value) {}
    StringPin(Node* node, string_view name, string value = ""): Pin(node, PinType::String, name), m_Value(value) {}

    bool SetValue(PinValue value) override
    {
        if (value.GetType() != TypeId)
            return false;

        m_Value = value.As<string>();

        return true;
    }

    PinValue GetValue() const override { return m_Value; }

    bool Load(const crude_json::value& value) override;
    void Save(crude_json::value& value) const override;

    string m_Value;
};



//
// -------[ Generic Node Type ]-------
//

struct NodeTypeInfo
{
    using Factory = Node*(*)(Blueprint& blueprint);

    uint32_t    m_Id;
    string_view m_Name;
    string_view m_DisplayName;
    Factory     m_Factory;
};

// Base for all nodes
//
// Node is a function with can have multiple entry points and multiple exit points.
// Execution of the node can behave differently depending of entry point passed to Execute(),
// also may lead to different exit point depending on node internal state.
//
// Execution to work required node to have one or more Flow pins. Otherwise it cannot be linked with
// other nodes. Implementation of execution logic is optional since node may not have any Flow
// pin. In such case node can do its thing in EvaluatePin() function and do something more
// than simply return value held by a pin.
//
// Reset(), Execute() and EvaluatePin() operate on provided execution context. All input
// pins should be evaluated using `context.GetPinValue<>(pin)` instead of accessed directly
// by calling `pin.GetValue()`. Later is valid only for output pins, former is valid
// for input pins.
// Not sticking to these rules will lead to bugs. One node can be executed in multiple context
// exactly like a class method can be run on different instances on the object in C++. Blueprint
// is a like a class not an instance. Using `pin.GetValue()` on input pins is similar to
// having static members in class.
//
// Output pins values can be updated only in Execute() and Reset() by
// `context.SetPinValue(pin, value)`. Other nodes will read it using `context.GetPinValue<>(pin)`
// call.
//
// EvaluatePin() wlays calculate value each time it is called. Execution context nor node itself
// is changed in the process. This is like calling method marked as a `const` in C++.
//
// Nodes can create or destroy pins at will. It is advised to not do that while executing
// a blueprint. That would be analogous to self-modifying code (which is neat trick, but
// a hell to debug).
//
struct Node
{
    Node(Blueprint& blueprint);
    virtual ~Node() = default;

    template <typename T>
    unique_ptr<T> CreatePin(string_view name = "");
    unique_ptr<Pin> CreatePin(PinType pinType, string_view name = "");

    virtual void Reset(Context& context) // Reset state of the node before execution. Allows to set initial state for the specified execution context.
    {
    }

    virtual FlowPin Execute(Context& context, FlowPin& entryPoint) // Executes node logic from specified entry point. Returns exit point (flow pin on output side) or nothing.
    {
        return {};
    }

    virtual PinValue EvaluatePin(const Context& context, const Pin& pin) const // Evaluates pin in node in specified execution context.
    {
        return pin.GetValue();
    }

    virtual NodeTypeInfo GetTypeInfo() const { return {}; }

    virtual string_view GetName() const;

    virtual LinkQueryResult AcceptLink(const Pin& receiver, const Pin& provider) const; // Checks if node accept link between these two pins. There node can filter out unsupported link types.
    virtual void WasLinked(const Pin& receiver, const Pin& provider); // Notifies node that link involving one of its pins has been made.
    virtual void WasUnlinked(const Pin& receiver, const Pin& provider); // Notifies node that link involving one of its pins has been broken.

    virtual span<Pin*> GetInputPins() { return {}; } // Returns list of input pins of the node
    virtual span<Pin*> GetOutputPins() { return {}; } // Returns list of output pins of the node

    virtual bool Load(const crude_json::value& value);
    virtual void Save(crude_json::value& value) const;

    uint32_t    m_Id;
    Blueprint*  m_Blueprint;

protected:
};

template <typename T>
inline crude_blueprint::unique_ptr<T> crude_blueprint::Node::CreatePin(string_view name /*= ""*/)
{
    if (auto pin = CreatePin(T::TypeId, name))
        return unique_ptr<T>(static_cast<T*>(pin.release()));
    else
        return nullptr;
}


//
// -------[ Execution Context ]-------
//

enum class StepResult
{
    Success,
    Done,
    Error
};

const char* StepResultToString(StepResult stepResult);

struct ContextMonitor
{
    virtual ~ContextMonitor() {};
    virtual void OnStart(Context& context) {}
    virtual void OnError(Context& context) {}
    virtual void OnDone(Context& context) {}

    virtual void OnPreStep(Context& context) {}
    virtual void OnPostStep(Context& context) {}

    virtual void OnEvaluatePin(const Context& context, const Pin& pin) {}
};

struct Context
{
    void SetContextMonitor(ContextMonitor* monitor);
          ContextMonitor* GetContextMonitor();
    const ContextMonitor* GetContextMonitor() const;

    void ResetState();

    StepResult Start(FlowPin& entryPoint);
    StepResult Step();
    void Stop();

    StepResult Execute(FlowPin& entryPoint);

    Node* CurrentNode();
    const Node* CurrentNode() const;

    Node* NextNode();
    const Node* NextNode() const;

    FlowPin CurrentFlowPin() const;

    StepResult LastStepResult() const;

    uint32_t StepCount() const;

    void PushReturnPoint(FlowPin& entryPoint);

    template <typename T>
    auto GetPinValue(const Pin& pin) const;

    void SetPinValue(const Pin& pin, PinValue value);
    PinValue GetPinValue(const Pin& pin) const;

private:
    StepResult SetStepResult(StepResult result);

    vector<FlowPin>         m_Callstack;
    Node*                   m_CurrentNode = nullptr;
    FlowPin                 m_CurrentFlowPin = {};
    StepResult              m_LastResult = StepResult::Done;
    uint32_t                m_StepCount = 0;
    map<uint32_t, PinValue> m_Values;
    ContextMonitor*         m_Monitor = nullptr;
};

template <typename T>
inline auto Context::GetPinValue(const Pin& pin) const
{
    return GetPinValue(pin).As<T>();
}


//
// -------[ Poor-man Type Info ]-------
//

namespace detail {

constexpr inline uint32_t fnv_1a_hash(const char string[], size_t size)
{
    constexpr uint32_t c_offset_basis = 2166136261U;
    constexpr uint32_t c_prime        =   16777619U;

    uint32_t value = c_offset_basis;

    auto   data = string;
    while (size-- > 0)
    {
        value ^= static_cast<uint32_t>(static_cast<uint8_t>(*data++));
        value *= c_prime;
    }

    return value;
}

template <size_t N>
constexpr inline uint32_t fnv_1a_hash(const char (&string)[N])
{
    return fnv_1a_hash(string, N - 1);
}

} // namespace detail {


// Where there is a reflection in C++, there are macros.
// Remind me to check if C++ will solve this until 2030.
//
//                                    thedmd, 2020-08-12
//
// CRUDE_BP_NODE(...) define a functions:
//
//     static NodeTypeInfo GetStaticTypeInfo() { ... }
//
//     NodeTypeInfo GetTypeInfo() const override { ... }
//
# define CRUDE_BP_NODE(type, displayName) \
    static ::crude_blueprint::NodeTypeInfo GetStaticTypeInfo() \
    { \
        return \
        { \
            ::crude_blueprint::detail::fnv_1a_hash(#type), \
            #type, \
            displayName, \
            [](::crude_blueprint::Blueprint& blueprint) -> ::crude_blueprint::Node* { return new type(blueprint); } \
        }; \
    } \
    \
    ::crude_blueprint::NodeTypeInfo GetTypeInfo() const override \
    { \
        return GetStaticTypeInfo(); \
    }

// Look to crude_blueprint_library.h for concrete node examples



//
// -------[ Node Factory ]-------
//

struct NodeRegistry
{
    NodeRegistry();

    uint32_t RegisterNodeType(string_view name, NodeTypeInfo::Factory factory);
    void UnregisterNodeType(string_view name);

    Node* Create(uint32_t typeId, Blueprint& blueprint);
    Node* Create(string_view typeName, Blueprint& blueprint);

    span<const NodeTypeInfo* const> GetTypes() const;

private:
    void RebuildTypes();

    vector<NodeTypeInfo> m_BuildInNodes;

    vector<NodeTypeInfo> m_CustomNodes;

    vector<NodeTypeInfo*> m_Types;
};



//
// -------[ Blueprint Object ]-------
//

struct EntryPointNode;

struct Blueprint
{
    Blueprint(shared_ptr<NodeRegistry> nodeRegistry = nullptr);
    Blueprint(const Blueprint& other);
    Blueprint(Blueprint&& other);
    ~Blueprint();

    Blueprint& operator=(const Blueprint& other);
    Blueprint& operator=(Blueprint&& other);

    template <typename T>
    T* CreateNode()
    {
        if (auto node = CreateNode(T::GetStaticTypeInfo().m_Id))
            return static_cast<T*>(node);
        else
            return nullptr;
    }

    Node* CreateNode(uint32_t nodeTypeId);
    Node* CreateNode(string_view nodeTypeName);
    void DeleteNode(Node* node);

    void ForgetPin(Pin* pin);

    void Clear();

    span<      Node*>       GetNodes();
    span<const Node* const> GetNodes() const;

    span<      Pin*>       GetPins();
    span<const Pin* const> GetPins() const;

          Node* FindNode(uint32_t nodeId);
    const Node* FindNode(uint32_t nodeId) const;

          Pin* FindPin(uint32_t pinId);
    const Pin* FindPin(uint32_t pinId) const;

    shared_ptr<NodeRegistry> GetNodeRegistry() const;

    //      Context& GetContext();
    const Context& GetContext() const;

    void SetContextMonitor(ContextMonitor* monitor);
          ContextMonitor* GetContextMonitor();
    const ContextMonitor* GetContextMonitor() const;

    void Start(EntryPointNode& entryPointNode);

    StepResult Step();

    void Stop();

    StepResult Execute(EntryPointNode& entryPointNode);

    Node* CurrentNode();
    const Node* CurrentNode() const;

    Node* NextNode();
    const Node* NextNode() const;

    FlowPin CurrentFlowPin() const;

    StepResult LastStepResult() const;

    uint32_t StepCount() const;

    bool Load(const crude_json::value& value);
    void Save(crude_json::value& value) const;

    bool Load(string_view path);
    bool Save(string_view path) const;

    uint32_t MakeNodeId(Node* node);
    uint32_t MakePinId(Pin* pin);

    bool HasPinAnyLink(const Pin& pin) const;

    vector<Pin*> FindPinsLinkedTo(const Pin& pin) const;

private:
    void ResetState();

    shared_ptr<NodeRegistry>    m_NodeRegistry;
    IdGenerator                 m_Generator;
    vector<Node*>               m_Nodes;
    vector<Pin*>                m_Pins;
    Context                     m_Context;
};

} // namespace crude_blueprint {

