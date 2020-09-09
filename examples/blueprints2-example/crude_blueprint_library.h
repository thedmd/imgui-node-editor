// Blueprint node library
//
// Some nodes implements ones found in UE4 documentation.
//
# pragma once
# include "crude_blueprint.h"

namespace crude_blueprint {

// Very basic nodes which serve as a constant data source.
//
// They're equivalent to setting data directly in input nodes
// with one exception. Constant node can feed multiple inputs.

struct ConstBoolNode final : Node
{
    CRUDE_BP_NODE(ConstBoolNode, "Const Bool")

    ConstBoolNode(Blueprint& blueprint): Node(blueprint) {}

    span<Pin*> GetOutputPins() override { return m_OutputPins; }

    BoolPin m_Bool = { this };

    Pin* m_OutputPins[1] = { &m_Bool };
};

struct ConstInt32Node final : Node
{
    CRUDE_BP_NODE(ConstInt32Node, "Const Int32")

    ConstInt32Node(Blueprint& blueprint): Node(blueprint) {}

    span<Pin*> GetOutputPins() override { return m_OutputPins; }

    Int32Pin m_Int32 = { this };

    Pin* m_OutputPins[1] = { &m_Int32 };
};

struct ConstFloatNode final : Node
{
    CRUDE_BP_NODE(ConstFloatNode, "Const Float")

    ConstFloatNode(Blueprint& blueprint): Node(blueprint) {}

    span<Pin*> GetOutputPins() override { return m_OutputPins; }

    FloatPin m_Float = { this };

    Pin* m_OutputPins[1] = { &m_Float };
};

struct ConstStringNode final : Node
{
    CRUDE_BP_NODE(ConstStringNode, "Const String")

    ConstStringNode(Blueprint& blueprint): Node(blueprint) {}

    span<Pin*> GetOutputPins() override { return m_OutputPins; }

    StringPin m_String = { this };

    Pin* m_OutputPins[1] = { &m_String };
};


// Flow Control nodes that mimics UE4 nodes
//
// See: https://docs.unrealengine.com/en-US/Engine/Blueprints/UserGuide/FlowControl/index.html

struct BranchNode final : Node
{
    CRUDE_BP_NODE(BranchNode, "Branch")

    BranchNode(Blueprint& blueprint): Node(blueprint) {}

    FlowPin Execute(Context& context, FlowPin& entryPoint) override
    {
        auto value = context.GetPinValue<bool>(m_Condition);
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
    CRUDE_BP_NODE(DoNNode, "Do N")

    DoNNode(Blueprint& blueprint): Node(blueprint) {}

    void Reset(Context& context) override
    {
        context.SetPinValue(m_Counter, 0);
    }

    FlowPin Execute(Context& context, FlowPin& entryPoint) override
    {
        if (entryPoint.m_Id == m_Reset.m_Id)
        {
            context.SetPinValue(m_Counter, 0);
            return {};
        }

        auto n = context.GetPinValue<int32_t>(m_N);
        auto c = context.GetPinValue<int32_t>(m_Counter);
        if (c >= n)
            return {};

        context.SetPinValue(m_Counter, c + 1);

        context.PushReturnPoint(entryPoint);

        return m_Exit;
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

struct DoOnceNode final : Node
{
    CRUDE_BP_NODE(DoOnceNode, "Do Once")

    DoOnceNode(Blueprint& blueprint): Node(blueprint) {}

    void Reset(Context& context) override
    {
        context.SetPinValue(m_IsClosed, -1);
    }

    FlowPin Execute(Context& context, FlowPin& entryPoint) override
    {
        if (entryPoint.m_Id == m_Reset.m_Id)
        {
            context.SetPinValue(m_IsClosed, -1);
            return {};
        }

        auto isClosed = context.GetPinValue<int32_t>(m_IsClosed);
        if (isClosed < 0)
        {
            isClosed = context.GetPinValue<bool>(m_StartClosed) ? 1 : 0;
            context.SetPinValue(m_IsClosed, isClosed);
        }

        if (isClosed)
            return {};

        isClosed = 1;

        context.SetPinValue(m_IsClosed, isClosed);

        return m_Completed;
    }

    span<Pin*> GetInputPins() override { return m_InputPins; }
    span<Pin*> GetOutputPins() override { return m_OutputPins; }

    FlowPin  m_Enter       = { this, "Enter" };
    FlowPin  m_Reset       = { this, "Reset" };
    BoolPin  m_StartClosed = { this, "Start Closed" };
    FlowPin  m_Completed   = { this, "Completed" };
    Int32Pin m_IsClosed    = { this }; // private pin, to hold node state

    Pin* m_InputPins[3] = { &m_Enter, &m_Reset, &m_StartClosed };
    Pin* m_OutputPins[1] = { &m_Completed };
};

struct FlipFlopNode final : Node
{
    CRUDE_BP_NODE(FlipFlopNode, "Flip Flop")

    FlipFlopNode(Blueprint& blueprint): Node(blueprint) {}

    void Reset(Context& context) override
    {
        context.SetPinValue(m_IsA, false);
    }

    FlowPin Execute(Context& context, FlowPin& entryPoint) override
    {
        auto isA = !context.GetPinValue<bool>(m_IsA);
        context.SetPinValue(m_IsA, isA);
        if (isA)
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

// In UE4 one iteration is done per frame. Here whole loop is executed at once.
struct ForLoopNode final : Node
{
    CRUDE_BP_NODE(ForLoopNode, "For Loop")

    ForLoopNode(Blueprint& blueprint): Node(blueprint) {}

    void Reset(Context& context) override
    {
        context.SetPinValue(m_IsCounting, false);
    }

    FlowPin Execute(Context& context, FlowPin& entryPoint) override
    {
        auto isCounting = context.GetPinValue<bool>(m_IsCounting);
        if (!isCounting)
        {
            auto firstIndex = context.GetPinValue<int32_t>(m_FirstIndex);
            context.SetPinValue(m_Index, firstIndex);
        }

        auto index     = context.GetPinValue<int32_t>(m_Index);
        auto lastIndex = context.GetPinValue<int32_t>(m_LastIndex);
        if (index <= lastIndex)
        {
            context.SetPinValue(m_Index, index + 1);

            context.PushReturnPoint(entryPoint);

            return m_LoopBody;
        }

        context.SetPinValue(m_IsCounting, false);

        return m_Completed;
    }

    span<Pin*> GetInputPins() override { return m_InputPins; }
    span<Pin*> GetOutputPins() override { return m_OutputPins; }

    FlowPin  m_Enter      = { this, "" };
    Int32Pin m_FirstIndex = { this, "First Index" };
    Int32Pin m_LastIndex  = { this, "Last Index" };
    FlowPin  m_LoopBody   = { this, "Loop Body" };
    Int32Pin m_Index      = { this, "Index" };
    FlowPin  m_Completed  = { this, "Completed" };
    BoolPin  m_IsCounting = { this }; // private pin for internal state

    Pin* m_InputPins[3] = { &m_Enter, &m_FirstIndex, &m_LastIndex };
    Pin* m_OutputPins[3] = { &m_LoopBody, &m_Index, &m_Completed };
};

// ForLoopWithBreakNode - This node does not make sense here, since
// it require frame based execution model.

struct GateNode final : Node
{
    CRUDE_BP_NODE(GateNode, "Gate")

    GateNode(Blueprint& blueprint): Node(blueprint) {}

    void Reset(Context& context) override
    {
        context.SetPinValue(m_IsClosed, -1);
    }

    FlowPin Execute(Context& context, FlowPin& entryPoint) override
    {
        auto isClosed = context.GetPinValue<int32_t>(m_IsClosed);
        if (isClosed < 0)
        {
            isClosed = context.GetPinValue<bool>(m_StartClosed) ? 1 : 0;
            context.SetPinValue(m_IsClosed, isClosed);
        }

        if (entryPoint.m_Id == m_Enter.m_Id)
        {
            if (isClosed == 0)
                return m_Exit;
        }
        else if (entryPoint.m_Id == m_Open.m_Id)
            context.SetPinValue(m_IsClosed, 0);
        else if (entryPoint.m_Id == m_Close.m_Id)
            context.SetPinValue(m_IsClosed, 1);
        else if (entryPoint.m_Id == m_Toggle.m_Id)
            context.SetPinValue(m_IsClosed, isClosed ? 0 : 1);

        return {};
    }

    span<Pin*> GetInputPins() override { return m_InputPins; }
    span<Pin*> GetOutputPins() override { return m_OutputPins; }

    FlowPin  m_Enter       = { this, "Enter" };
    FlowPin  m_Open        = { this, "Open" };
    FlowPin  m_Close       = { this, "Close" };
    FlowPin  m_Toggle      = { this, "Toggle" };
    BoolPin  m_StartClosed = { this, "Start Closed" };
    FlowPin  m_Exit        = { this, "Exit" };
    Int32Pin m_IsClosed    = { this }; // private pin, to hold node state

    Pin* m_InputPins[5] = { &m_Enter, &m_Open, &m_Close, &m_Toggle, &m_StartClosed };
    Pin* m_OutputPins[1] = { &m_Exit };
};

struct ToStringNode final : Node
{
    CRUDE_BP_NODE(ToStringNode, "To String")

    ToStringNode(Blueprint& blueprint): Node(blueprint)
    {
        SetType(PinType::Any);
    }

    FlowPin Execute(Context& context, FlowPin& entryPoint) override
    {
        auto value = context.GetPinValue(m_Value);

        string result;
        switch (value.GetType())
        {
            case PinType::Any:    break;
            case PinType::Flow:   break;
            case PinType::Bool:   result = value.As<bool>() ? "true" : "false"; break;
            case PinType::Int32:  result = std::to_string(value.As<int32_t>()); break;
            case PinType::Float:  result = std::to_string(value.As<float>()); break;
            case PinType::String: result = value.As<string>(); break;
        }

        context.SetPinValue(m_String, std::move(result));

        return m_Exit;
    }

    string_view GetName() const override
    {
        return m_Name;
    }

    void SetType(PinType type)
    {
        if (type != PinType::Any)
            m_Name = string(PinTypeToString(type)) + " To String";
        else
            m_Name = "To String";

        m_Value.SetValueType(type);
    }

    void WasLinked(const Pin& receiver, const Pin& provider) override
    {
        if (receiver.m_Id == m_Value.m_Id)
            SetType(provider.GetValueType());
    }

    void WasUnlinked(const Pin& receiver, const Pin& provider) override
    {
        if (receiver.m_Id == m_Value.m_Id)
            SetType(PinType::Any);
    }

    span<Pin*> GetInputPins() override { return m_InputPins; }
    span<Pin*> GetOutputPins() override { return m_OutputPins; }

    string m_Name;

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
    CRUDE_BP_NODE(PrintNode, "Print")

    PrintNode(Blueprint& blueprint): Node(blueprint) {}

    FlowPin Execute(Context& context, FlowPin& entryPoint) override
    {
        auto value = context.GetPinValue<string>(m_String);

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
    CRUDE_BP_NODE(EntryPointNode, "Start")

    EntryPointNode(Blueprint& blueprint): Node(blueprint) {}

    FlowPin Execute(Context& context, FlowPin& entryPoint) override
    {
        return m_Exit;
    }

    span<Pin*> GetOutputPins() override { return m_OutputPins; }

    FlowPin m_Exit = { this };

    Pin* m_OutputPins[1] = { &m_Exit };
};

struct AddNode final : Node
{
    CRUDE_BP_NODE(AddNode, "Add (const)")

    AddNode(Blueprint& blueprint): Node(blueprint)
    {
        SetType(PinType::Any);
    }

    PinValue EvaluatePin(const Context& context, const Pin& pin) const override
    {
        if (pin.m_Id == m_Result.m_Id)
        {
            auto aValue = context.GetPinValue(m_A);
            auto bValue = context.GetPinValue(m_B);

            if (aValue.GetType() != m_Type ||
                bValue.GetType() != m_Type)
            {
                return {}; // Error: Node values must be of same type
            }

            switch (m_Type)
            {
                case PinType::Int32:
                    return aValue.As<int32_t>() + bValue.As<int32_t>();
                case PinType::Float:
                    return aValue.As<float>() + bValue.As<float>();
                case PinType::String:
                    return aValue.As<string>() + bValue.As<string>();
            }

            return {}; // Error: Unsupported type
        }
        else
            return Node::EvaluatePin(context, pin);
    }

    string_view GetName() const override
    {
        return m_Name;
    }

    LinkQueryResult AcceptLink(const Pin& receiver, const Pin& provider) const override
    {
        auto result = Node::AcceptLink(receiver, provider);
        if (!result)
            return result;

        // Accept connection of any type
        if (m_Type != PinType::Any)
        {
            if (receiver.m_Node == this && receiver.GetValueType() != m_Type)
                return { false, "Receiver must match type of the node" };

            if (provider.m_Node == this && provider.GetValueType() != m_Type)
                return { false, "Provider must match type of the node" };
        }
        else if (m_Type == PinType::Any)
        {
            auto candidateType = PinType::Void;
            if (receiver.m_Node != this)
                candidateType = receiver.GetValueType();
            else if (provider.m_Node != this)
                candidateType = provider.GetValueType();

            switch (candidateType)
            {
                case PinType::Any:
                case PinType::Int32:
                case PinType::Float:
                case PinType::String:
                    return { true, "Other pins will convert to this pin type" };

                default:
                    return { false, "Node do not support pin of this type" };
            }
        }

        return {true};
    }

    void WasLinked(const Pin& receiver, const Pin& provider) override
    {
        if (m_Type != PinType::Any)
            return;

        if (receiver.m_Id == m_A.m_Id || receiver.m_Id == m_B.m_Id)
            SetType(provider.GetValueType());
        else if (provider.m_Id == m_Result.m_Id)
            SetType(receiver.GetValueType());
    }

    void WasUnlinked(const Pin& receiver, const Pin& provider) override
    {
    }

    void SetType(PinType type)
    {
        if (type != PinType::Any)
            m_Name = string("Add ") + PinTypeToString(type) + " (const)";
        else
            m_Name = "Add (const)";

        m_Type = type;

        m_A.SetValueType(type);
        m_B.SetValueType(type);
        m_Result.SetValueType(type);
    }

    span<Pin*> GetInputPins() override { return m_InputPins; }
    span<Pin*> GetOutputPins() override { return m_OutputPins; }

    string m_Name;

    PinType m_Type = PinType::Any;

    AnyPin m_A = { this, "A" };
    AnyPin m_B = { this, "B" };
    AnyPin m_Result = { this, "Result" };

    Pin* m_InputPins[2] = { &m_A, &m_B };
    Pin* m_OutputPins[1] = { &m_Result };
};

} // namespace crude_blueprint {

