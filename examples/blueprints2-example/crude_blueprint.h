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

namespace crude_json {
struct value;
} // crude_json

namespace crude_blueprint {

using nonstd::span;
using nonstd::make_span;
using nonstd::string_view;
using nonstd::variant;
using nonstd::get;
using nonstd::monostate;
using std::string;
using std::vector;
using std::shared_ptr;
using std::unique_ptr;
using std::make_shared;

struct Node;
struct Context;
struct Blueprint;


enum class PinType { Void, Flow, Bool, Int32, Float, String };
using PinValue = variant<monostate, Node*, bool, int32_t, float, string>;



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

    template <typename T>
    T GetValueAs(const Context& context) const;

    PinValue GetValue(const Context& context) const;

    PinType GetType() const;

    virtual bool Load(const crude_json::value& value);
    virtual void Save(crude_json::value& value) const;

    uint32_t    m_Id   = 0;
    Node*       m_Node = nullptr;
    PinType     m_Type = PinType::Void;
    string_view m_Name;
    Pin*        m_Link = nullptr;

protected:
    virtual PinValue GetValue() const;
};

template <typename T>
T crude_blueprint::Pin::GetValueAs(const Context& context) const
{
    return get<T>(GetValue(context));
}



//
// -------[ Concrete Pin Types ]-------
//

struct AnyPin final : Pin
{
    AnyPin(Node* node, string_view name = ""): Pin(node, PinType::Void, name) {}
};

struct FlowPin final : Pin
{
    FlowPin(): FlowPin(nullptr) {}
    FlowPin(Node* node): Pin(node, PinType::Flow) {}
    FlowPin(Node* node, string_view name): Pin(node, PinType::Flow, name) {}
};

struct BoolPin final : Pin
{
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

    bool Load(const crude_json::value& value) override;
    void Save(crude_json::value& value) const override;

    bool m_Value = false;

protected:
    PinValue GetValue() const override { return m_Value; }
};

struct Int32Pin final : Pin
{
    Int32Pin(Node* node, int32_t value = 0): Pin(node, PinType::Int32), m_Value(value) {}
    Int32Pin(Node* node, string_view name, int32_t value = 0): Pin(node, PinType::Int32, name), m_Value(value) {}

    bool Load(const crude_json::value& value) override;
    void Save(crude_json::value& value) const override;

    int32_t m_Value = 0;

protected:
    PinValue GetValue() const override { return m_Value; }
};

struct FloatPin final : Pin
{
    FloatPin(Node* node, float value = 0.0f): Pin(node, PinType::Float), m_Value(value) {}
    FloatPin(Node* node, string_view name, float value = 0.0f): Pin(node, PinType::Float, name), m_Value(value) {}

    bool Load(const crude_json::value& value) override;
    void Save(crude_json::value& value) const override;

    float m_Value = 0.0f;

protected:
    PinValue GetValue() const override { return m_Value; }
};

struct StringPin final : Pin
{
    StringPin(Node* node, string value = ""): Pin(node, PinType::String), m_Value(value) {}
    StringPin(Node* node, string_view name, string value = ""): Pin(node, PinType::String, name), m_Value(value) {}

    bool Load(const crude_json::value& value) override;
    void Save(crude_json::value& value) const override;

    string m_Value;

protected:
    PinValue GetValue() const override { return m_Value; }
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

    virtual void Reset()
    {
    }

    virtual FlowPin Execute(Context& context, FlowPin& entryPoint)
    {
        return {};
    }

    virtual PinValue EvaluatePin(const Context& context, const Pin& pin)
    {
        return pin.GetValue(context);
    }

    virtual NodeTypeInfo GetTypeInfo() const { return {}; }

    virtual span<Pin*> GetInputPins() { return {}; }
    virtual span<Pin*> GetOutputPins() { return {}; }

    virtual bool Load(const crude_json::value& value);
    virtual void Save(crude_json::value& value) const;

    uint32_t    m_Id;
    string_view m_Name;
    Blueprint*  m_Blueprint;
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


struct Context
{
    PinValue GetPinValue(const Pin& pin) const;

    void Start(FlowPin& entryPoint);

    StepResult Step();

    void PushReturnPoint(FlowPin& entryPoint);

    StepResult Execute(FlowPin& entryPoint);

    Node* CurrentNode();
    const Node* CurrentNode() const;

private:
    vector<FlowPin> m_Queue;
    Node* m_CurrentNode = nullptr;
};



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



//
// -------[ Generic Node Types ]-------
//

struct ConstBoolNode final : Node
{
    CRUDE_BP_NODE(ConstBoolNode)

    ConstBoolNode(Blueprint& blueprint): Node(blueprint, "Const Bool") {}

    span<Pin*> GetOutputPins() override { return m_OutputPins; }

    BoolPin m_Bool = { this };

    Pin* m_OutputPins[1] = { &m_Bool };
};

struct ConstInt32Node final : Node
{
    CRUDE_BP_NODE(ConstInt32Node)

    ConstInt32Node(Blueprint& blueprint): Node(blueprint, "Const Int32") {}

    span<Pin*> GetOutputPins() override { return m_OutputPins; }

    Int32Pin m_Int32 = { this };

    Pin* m_OutputPins[1] = { &m_Int32 };
};

struct ConstFloatNode final : Node
{
    CRUDE_BP_NODE(ConstFloatNode)

    ConstFloatNode(Blueprint& blueprint): Node(blueprint, "Const Float") {}

    span<Pin*> GetOutputPins() override { return m_OutputPins; }

    FloatPin m_Float = { this };

    Pin* m_OutputPins[1] = { &m_Float };
};

struct ConstStringNode final : Node
{
    CRUDE_BP_NODE(ConstStringNode)

    ConstStringNode(Blueprint& blueprint): Node(blueprint, "Const String") {}

    span<Pin*> GetOutputPins() override { return m_OutputPins; }

    StringPin m_String = { this };

    Pin* m_OutputPins[1] = { &m_String };
};

struct BranchNode final : Node
{
    CRUDE_BP_NODE(BranchNode)

    BranchNode(Blueprint& blueprint): Node(blueprint, "Branch") {}

    FlowPin Execute(Context& context, FlowPin& entryPoint) override
    {
        auto value = m_Condition.GetValueAs<bool>(context);
        if (value)
            return m_True;
        else
            return m_False;
    }

    span<Pin*> GetInputPins() override { return m_InputPins; }
    span<Pin*> GetOutputPins() override { return m_OutputPins; }

    FlowPin m_Enter     = { this };
    BoolPin m_Condition = { this, "Condition" };
    FlowPin m_True      = { this, "True" };
    FlowPin m_False     = { this, "False" };

    Pin* m_InputPins[2] = { &m_Enter, &m_Condition };
    Pin* m_OutputPins[2] = { &m_True, &m_False };
};

struct DoNNode final : Node
{
    CRUDE_BP_NODE(DoNNode)

    DoNNode(Blueprint& blueprint): Node(blueprint, "Do N") {}

    FlowPin Execute(Context& context, FlowPin& entryPoint) override
    {
        if (entryPoint.m_Id == m_Reset.m_Id)
        {
            m_Counter.m_Value = 0;
            return {};
        }

        auto n = m_N.GetValueAs<int32_t>(context);
        if (m_Counter.m_Value >= n)
            return {};

        ++m_Counter.m_Value;

        context.PushReturnPoint(entryPoint);

        return m_Exit;
    }

    void Reset() override
    {
        m_Counter.m_Value = 0;
    }

    span<Pin*> GetInputPins() override { return m_InputPins; }
    span<Pin*> GetOutputPins() override { return m_OutputPins; }

    FlowPin  m_Enter   = { this, "Enter" };
    FlowPin  m_Reset   = { this, "Reset" };
    Int32Pin m_N       = { this, "N" };
    FlowPin  m_Exit    = { this, "Exit" };
    Int32Pin m_Counter = { this, "Counter" };

    Pin* m_InputPins[3] = { &m_Enter, &m_N, &m_Reset };
    Pin* m_OutputPins[2] = { &m_Exit, &m_Counter };
};

struct FlipFlopNode final : Node
{
    CRUDE_BP_NODE(FlipFlopNode)

    FlipFlopNode(Blueprint& blueprint): Node(blueprint, "Flip Flop") {}

    void Reset() override
    {
        m_IsA.m_Value = false;
    }

    FlowPin Execute(Context& context, FlowPin& entryPoint) override
    {
        m_IsA.m_Value = !m_IsA.m_Value;
        if (m_IsA.m_Value)
            return m_A;
        else
            return m_B;
    }

    span<Pin*> GetInputPins() override { return m_InputPins; }
    span<Pin*> GetOutputPins() override { return m_OutputPins; }

    FlowPin m_Enter = { this, "Enter" };
    FlowPin m_A     = { this, "A" };
    FlowPin m_B     = { this, "B" };
    BoolPin m_IsA   = { this, "Is A" };

    Pin* m_InputPins[1] = { &m_Enter };
    Pin* m_OutputPins[3] = { &m_A, &m_B, &m_IsA };
};

struct ToStringNode final : Node
{
    CRUDE_BP_NODE(ToStringNode)

    ToStringNode(Blueprint& blueprint): Node(blueprint, "To String") {}

    FlowPin Execute(Context& context, FlowPin& entryPoint) override
    {
        string result;
        switch (m_Value.GetType())
        {
            case PinType::Void:   break;
            case PinType::Flow:   break;
            case PinType::Bool:   result = m_Value.GetValueAs<bool>(context) ? "true" : "false"; break;
            case PinType::Int32:  result = std::to_string(m_Value.GetValueAs<int32_t>(context)); break;
            case PinType::Float:  result = std::to_string(m_Value.GetValueAs<float>(context)); break;
            case PinType::String: result = m_Value.GetValueAs<string>(context); break;
        }

        m_String.m_Value = std::move(result);

        return m_Exit;
    }

    span<Pin*> GetInputPins() override { return m_InputPins; }
    span<Pin*> GetOutputPins() override { return m_OutputPins; }

    FlowPin   m_Enter  = { this };
    FlowPin   m_Exit   = { this };
    AnyPin    m_Value  = { this, "Value" };
    StringPin m_String = { this, string_view("String") };

    Pin* m_InputPins[2] = { &m_Enter, &m_Value };
    Pin* m_OutputPins[2] = { &m_Exit, &m_String };
};

# ifdef _WIN32
extern "C" __declspec(dllimport) void __stdcall OutputDebugStringA(const char* string);
# else
#     include <stdio.h>
# endif

struct PrintNode final : Node
{
    CRUDE_BP_NODE(PrintNode)

    PrintNode(Blueprint& blueprint): Node(blueprint, "Print") {}

    FlowPin Execute(Context& context, FlowPin& entryPoint) override
    {
        auto value = m_String.GetValueAs<string>(context);

# ifdef _WIN32
        OutputDebugStringA("PrintNode: ");
        OutputDebugStringA(value.c_str());
        OutputDebugStringA("\n");
# else
        printf("PrintNode: %s\n", value.c_str());
# endif

        return m_Exit;
    }

    span<Pin*> GetInputPins() override { return m_InputPins; }
    span<Pin*> GetOutputPins() override { return m_OutputPins; }

    FlowPin   m_Enter  = { this };
    FlowPin   m_Exit   = { this };
    StringPin m_String = { this };

    Pin* m_InputPins[2] = { &m_Enter, &m_String };
    Pin* m_OutputPins[1] = { &m_Exit };
};

struct EntryPointNode final : Node
{
    CRUDE_BP_NODE(EntryPointNode)

    EntryPointNode(Blueprint& blueprint): Node(blueprint, "Start") {}

    FlowPin Execute(Context& context, FlowPin& entryPoint) override
    {
        return m_Exit;
    }

    span<Pin*> GetOutputPins() override { return m_OutputPins; }

    FlowPin m_Exit = { this };

    Pin* m_OutputPins[1] = { &m_Exit };
};



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

    NodeTypeInfo m_BuildInNodes[10] =
    {
        ConstBoolNode::GetStaticTypeInfo(),
        ConstInt32Node::GetStaticTypeInfo(),
        ConstFloatNode::GetStaticTypeInfo(),
        ConstStringNode::GetStaticTypeInfo(),
        BranchNode::GetStaticTypeInfo(),
        DoNNode::GetStaticTypeInfo(),
        FlipFlopNode::GetStaticTypeInfo(),
        ToStringNode::GetStaticTypeInfo(),
        PrintNode::GetStaticTypeInfo(),
        EntryPointNode::GetStaticTypeInfo(),
    };

    vector<NodeTypeInfo> m_CustomNodes;

    vector<NodeTypeInfo*> m_Types;
};



//
// -------[ Blueprint Object ]-------
//

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

    void Clear();

    span<      Node*>       GetNodes();
    span<const Node* const> GetNodes() const;

    span<      Pin*>       GetPins();
    span<const Pin* const> GetPins() const;

          Pin* FindPin(uint32_t pinId);
    const Pin* FindPin(uint32_t pinId) const;

    shared_ptr<NodeRegistry> GetNodeRegistry() const;

    void Start(EntryPointNode& entryPointNode);

    StepResult Step();

    StepResult Execute(EntryPointNode& entryPointNode);

    Node* CurrentNode();
    const Node* CurrentNode() const;

    bool Load(const crude_json::value& value);
    void Save(crude_json::value& value) const;

    bool Load(string_view path);
    bool Save(string_view path) const;

    uint32_t MakeNodeId(Node* node);
    uint32_t MakePinId(Pin* pin);

    bool IsPinLinked(const Pin* pin) const;

    void TouchPin(const Pin& pin);
    span<uint32_t> GetTouchedPinIds() { return m_TouchedPinIds; }

private:
    void ResetState();

    shared_ptr<NodeRegistry>    m_NodeRegistry;
    IdGenerator                 m_Generator;
    vector<Node*>               m_Nodes;
    vector<Pin*>                m_Pins;
    Context                     m_Context;
    vector<uint32_t>            m_TouchedPinIds;
};

} // namespace crude_blueprint {

