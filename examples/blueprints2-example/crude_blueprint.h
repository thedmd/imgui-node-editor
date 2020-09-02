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


enum class PinType { Any, Flow, Bool, Int32, Float, String };

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

//using PinValue = variant<monostate, FlowPin*, bool, int32_t, float, string>;



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

struct Pin
{
    Pin(Node* node, PinType type);
    Pin(Node* node, PinType type, string_view name);
    virtual ~Pin() = default;

    virtual bool SetValueType(PinType type) { return false; }
    virtual PinType GetValueType() const;
    virtual bool SetValue(PinValue value) { return false; }
    virtual PinValue GetValue() const;

    PinType GetType() const;

    bool CanLinkTo(const Pin& pin) const;
    bool LinkTo(const Pin& pin);
    void Unlink();

    virtual bool Load(const crude_json::value& value);
    virtual void Save(crude_json::value& value) const;

    uint32_t    m_Id   = 0;
    Node*       m_Node = nullptr;
    PinType     m_Type = PinType::Any;
    string_view m_Name;
    const Pin*  m_Link = nullptr;
};



//
// -------[ Concrete Pin Types ]-------
//

struct AnyPin final : Pin
{
    static constexpr auto TypeId = PinType::Any;

    AnyPin(Node* node, string_view name = ""): Pin(node, PinType::Any, name) {}

    bool SetValueType(PinType type) override;
    PinType GetValueType() const override { return m_InnerPin ? m_InnerPin->GetValueType() : Pin::GetValueType(); }
    bool SetValue(PinValue value) override;
    PinValue GetValue() const override { return m_InnerPin ? m_InnerPin->GetValue() : Pin::GetValue(); }

    bool Load(const crude_json::value& value) override;
    void Save(crude_json::value& value) const override;

    unique_ptr<Pin> m_InnerPin;
};

struct FlowPin final : Pin
{
    static constexpr auto TypeId = PinType::Flow;

    FlowPin(): FlowPin(nullptr) {}
    FlowPin(Node* node): Pin(node, PinType::Flow) {}
    FlowPin(Node* node, string_view name): Pin(node, PinType::Flow, name) {}

    PinValue GetValue() const override { return const_cast<FlowPin*>(this); }
};

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
    Factory     m_Factory;
};

struct Node
{
    Node(Blueprint& blueprint, string_view name);
    virtual ~Node() = default;

    template <typename T>
    T* CreatePin(string_view name = "")
    {
        if (auto pin = CreatePin(T::TypeId, name))
            return static_cast<T*>(pin);
        else
            return nullptr;
    }

    Pin* CreatePin(PinType pinType, string_view name = "");

    virtual void Reset(Context& context)
    {
    }

    virtual FlowPin Execute(Context& context, FlowPin& entryPoint)
    {
        return {};
    }

    virtual PinValue EvaluatePin(const Context& context, const Pin& pin) const
    {
        return pin.GetValue();
    }

    virtual NodeTypeInfo GetTypeInfo() const { return {}; }

    virtual bool AcceptLink(const Pin& target, const Pin& source) const;
    virtual void WasLinked(const Pin& target, const Pin& source);
    virtual void WasUnlinked(const Pin& target, const Pin& source);

    virtual span<Pin*> GetInputPins() { return {}; }
    virtual span<Pin*> GetOutputPins() { return {}; }

    virtual bool Load(const crude_json::value& value);
    virtual void Save(crude_json::value& value) const;

    uint32_t    m_Id;
    string_view m_Name;
    Blueprint*  m_Blueprint;

protected:
};



//
// -------[ Execution Context ]-------
//

enum class StepResult
{
    Success,
    Done,
    Error
};


struct PinValuePair
{
    uint32_t m_PinId;
    PinValue m_Value;
};

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

    void Reset();

    StepResult Start(FlowPin& entryPoint);
    StepResult Step();

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
# define CRUDE_BP_NODE(type) \
    static ::crude_blueprint::NodeTypeInfo GetStaticTypeInfo() \
    { \
        return \
        { \
            ::crude_blueprint::detail::fnv_1a_hash(#type), \
            #type, \
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

    StepResult Execute(EntryPointNode& entryPointNode);

    Node* CurrentNode();
    const Node* CurrentNode() const;

    Node* NextNode();
    const Node* NextNode() const;

    FlowPin CurrentFlowPin() const;

    StepResult LastStepResult() const;

    bool Load(const crude_json::value& value);
    void Save(crude_json::value& value) const;

    bool Load(string_view path);
    bool Save(string_view path) const;

    uint32_t MakeNodeId(Node* node);
    uint32_t MakePinId(Pin* pin);

    bool IsPinLinked(const Pin* pin) const;

private:
    void ResetState();

    shared_ptr<NodeRegistry>    m_NodeRegistry;
    IdGenerator                 m_Generator;
    vector<Node*>               m_Nodes;
    vector<Pin*>                m_Pins;
    Context                     m_Context;
};

} // namespace crude_blueprint {

