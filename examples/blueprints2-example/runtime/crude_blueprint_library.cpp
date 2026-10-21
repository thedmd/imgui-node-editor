# include "crude_blueprint_library.h"
# include <algorithm>
# include <cmath>
# include <compare>
# include <cstdint>
# include <limits>
# include <memory>
# include <random>
# include <string>


namespace crude_blueprint { namespace {

struct StartNode final : NodeType
{
    StartNode(): NodeType("crude.Start", "Start", "Events") {}

    void Declare(NodeDeclaration& declaration, const NodeConfig&, const Bindings&) const override
    {
        declaration.FlowOutput("then");
    }

    Result<Exit> Execute(ExecContext&, std::string_view) const override
    {
        return Exit::To("then");
    }

    std::string EventName(const NodeConfig&) const override
    {
        return "Start";
    }
};

struct PrintNode final : NodeType
{
    PrintNode(): NodeType("crude.Print", "Print", "Debugging") {}

    void Declare(NodeDeclaration& declaration, const NodeConfig&, const Bindings&) const override
    {
        declaration.FlowInput("enter");
        declaration.Input("text", "Text", StringType());
        declaration.FlowOutput("then");
    }

    Result<Exit> Execute(ExecContext& context, std::string_view) const override
    {
        auto text = context.Get<std::string>("text");
        if (!text)
            return text.GetError();
        context.Log(text.GetValue());
        return Exit::To("then");
    }
};

// Of whatever type links or the user decide. A value of another type, kept while the type was
// undecided, gives way to the type's default.
struct ConstantNode final : NodeType
{
    ConstantNode(): NodeType("crude.Constant", "Constant", "Values") {}

    void Declare(NodeDeclaration& declaration, const NodeConfig&, const Bindings&) const override
    {
        auto T = declaration.Variable("T");
        declaration.Output("value", "Value", T);
    }

    Result<void> Evaluate(EvalContext& context) const override
    {
        auto type  = context.TypeOf("value");
        auto value = context.Config().m_Data.find("value");
        if (value != context.Config().m_Data.end() && value->second.GetType() == type)
            context.Set("value", value->second);
        else
            context.Set("value", type->m_Head->Default(*type));
        return {};
    }
};

// Integers that leave Int32's range are an error; strings are joined.
struct AddNode final : NodeType
{
    AddNode(): NodeType("crude.Add", "Add", "Math") {}

    void Declare(NodeDeclaration& declaration, const NodeConfig&, const Bindings&) const override
    {
        auto T     = declaration.Variable("T", { &Int32ValueType(), &FloatValueType(), &StringValueType() });
        auto count = declaration.Group("in", 2, 16);
        for (int i = 0; i < count; ++i)
            declaration.Input("in." + std::to_string(i), "", T);
        declaration.Output("result", "Result", T);
    }

    Result<void> Evaluate(EvalContext& context) const override
    {
        auto inputs = context.Inputs("in");
        if (!inputs)
            return inputs.GetError();
        auto type = context.TypeOf("result");
        if (type == &Int32Type())
        {
            int64_t sum = 0;
            for (auto& input : inputs.GetValue())
                sum += input.AsInt32();
            if (sum < std::numeric_limits<int32_t>::min() || sum > std::numeric_limits<int32_t>::max())
                return Error(BlueprintError::OutOfRange, "the sum, " + std::to_string(sum) + ", is outside Int32's range");
            context.Set("result", Value::Int32(static_cast<int32_t>(sum)));
        }
        else if (type == &FloatType())
        {
            float sum = 0.0f;
            for (auto& input : inputs.GetValue())
                sum += input.AsFloat();
            context.Set("result", Value::Float(sum));
        }
        else
        {
            std::string text;
            for (auto& input : inputs.GetValue())
                text += input.AsString();
            context.Set("result", Value::String(std::move(text)));
        }
        return {};
    }
};

// Of Int32s or Floats.
TypeVariable Numbers(NodeDeclaration& declaration)
{
    return declaration.Variable("T", { &Int32ValueType(), &FloatValueType() });
}

// Integers that leave Int32's range are an error.
Result<Value> Int32Result(int64_t value, std::string_view what)
{
    if (value < std::numeric_limits<int32_t>::min() || value > std::numeric_limits<int32_t>::max())
        return Error(BlueprintError::OutOfRange, std::string(what) + ", " + std::to_string(value) + ", is outside Int32's range");
    return Value::Int32(static_cast<int32_t>(value));
}

// a and b into a result, all of T.
struct BinaryNumbersNode : NodeType
{
    using NodeType::NodeType;

    void Declare(NodeDeclaration& declaration, const NodeConfig&, const Bindings&) const override
    {
        auto T = Numbers(declaration);
        declaration.Input("a", "A", T);
        declaration.Input("b", "B", T);
        declaration.Output("result", "Result", T);
    }

    Result<void> Evaluate(EvalContext& context) const override
    {
        auto a = context.Get("a");
        if (!a)
            return a.GetError();
        auto b = context.Get("b");
        if (!b)
            return b.GetError();
        auto result = context.TypeOf("result") == &Int32Type()
            ? Int32s(a.GetValue().AsInt32(), b.GetValue().AsInt32())
            : Value::Float(Floats(a.GetValue().AsFloat(), b.GetValue().AsFloat()));
        if (!result)
            return result.GetError();
        context.Set("result", result.GetValue());
        return {};
    }

    virtual Result<Value> Int32s(int32_t a, int32_t b) const = 0;
    virtual float         Floats(float a, float b) const = 0;
};

struct SubtractNode final : BinaryNumbersNode
{
    SubtractNode(): BinaryNumbersNode("crude.Subtract", "Subtract", "Math") {}

    Result<Value> Int32s(int32_t a, int32_t b) const override { return Int32Result(int64_t{ a } - b, "the difference"); }
    float         Floats(float a, float b) const override     { return a - b; }
};

struct DivideNode final : BinaryNumbersNode
{
    DivideNode(): BinaryNumbersNode("crude.Divide", "Divide", "Math") {}

    // Toward zero, as C++ divides.
    Result<Value> Int32s(int32_t a, int32_t b) const override
    {
        if (b == 0)
            return Error(BlueprintError::OutOfRange, "division by zero");
        return Int32Result(int64_t{ a } / b, "the quotient");
    }
    float Floats(float a, float b) const override { return a / b; }
};

// With the dividend's sign, as C++'s % and fmod() have it.
struct ModuloNode final : BinaryNumbersNode
{
    ModuloNode(): BinaryNumbersNode("crude.Modulo", "Modulo", "Math") {}

    Result<Value> Int32s(int32_t a, int32_t b) const override
    {
        if (b == 0)
            return Error(BlueprintError::OutOfRange, "modulo by zero");
        return Value::Int32(static_cast<int32_t>(int64_t{ a } % b));
    }
    float Floats(float a, float b) const override { return std::fmod(a, b); }
};

// A value into a result, both of T.
struct UnaryNumbersNode : NodeType
{
    using NodeType::NodeType;

    void Declare(NodeDeclaration& declaration, const NodeConfig&, const Bindings&) const override
    {
        auto T = Numbers(declaration);
        declaration.Input("value", "Value", T);
        declaration.Output("result", "Result", T);
    }

    Result<void> Evaluate(EvalContext& context) const override
    {
        auto value = context.Get("value");
        if (!value)
            return value.GetError();
        auto result = context.TypeOf("result") == &Int32Type()
            ? Int32Result(Int32s(value.GetValue().AsInt32()), "the result")
            : Value::Float(Floats(value.GetValue().AsFloat()));
        if (!result)
            return result.GetError();
        context.Set("result", result.GetValue());
        return {};
    }

    virtual int64_t Int32s(int32_t value) const = 0;
    virtual float   Floats(float value) const = 0;
};

struct NegateNode final : UnaryNumbersNode
{
    NegateNode(): UnaryNumbersNode("crude.Negate", "Negate", "Math") {}

    int64_t Int32s(int32_t value) const override { return -int64_t{ value }; }
    float   Floats(float value) const override   { return -value; }
};

struct AbsNode final : UnaryNumbersNode
{
    AbsNode(): UnaryNumbersNode("crude.Abs", "Abs", "Math") {}

    int64_t Int32s(int32_t value) const override { return value < 0 ? -int64_t{ value } : int64_t{ value }; }
    float   Floats(float value) const override   { return std::fabs(value); }
};

// Of a group of inputs, in.0 and on, into a result, all of T.
struct VariadicNumbersNode : NodeType
{
    using NodeType::NodeType;

    void Declare(NodeDeclaration& declaration, const NodeConfig&, const Bindings&) const override
    {
        auto T     = Numbers(declaration);
        auto count = declaration.Group("in", 2, 16);
        for (int i = 0; i < count; ++i)
            declaration.Input("in." + std::to_string(i), "", T);
        declaration.Output("result", "Result", T);
    }

    Result<void> Evaluate(EvalContext& context) const override
    {
        auto inputs = context.Inputs("in");
        if (!inputs)
            return inputs.GetError();
        auto& values = inputs.GetValue();
        if (context.TypeOf("result") == &Int32Type())
        {
            int64_t result = values[0].AsInt32();
            for (size_t i = 1; i < values.size(); ++i)
            {
                auto next = Int32Result(Int32s(result, values[i].AsInt32()), "the result");
                if (!next)
                    return next.GetError();
                result = next.GetValue().AsInt32();
            }
            context.Set("result", Value::Int32(static_cast<int32_t>(result)));
        }
        else
        {
            auto result = values[0].AsFloat();
            for (size_t i = 1; i < values.size(); ++i)
                result = Floats(result, values[i].AsFloat());
            context.Set("result", Value::Float(result));
        }
        return {};
    }

    virtual int64_t Int32s(int64_t a, int32_t b) const = 0;
    virtual float   Floats(float a, float b) const = 0;
};

struct MultiplyNode final : VariadicNumbersNode
{
    MultiplyNode(): VariadicNumbersNode("crude.Multiply", "Multiply", "Math") {}

    int64_t Int32s(int64_t a, int32_t b) const override { return a * b; }
    float   Floats(float a, float b) const override     { return a * b; }
};

struct MinNode final : VariadicNumbersNode
{
    MinNode(): VariadicNumbersNode("crude.Min", "Min", "Math") {}

    int64_t Int32s(int64_t a, int32_t b) const override { return std::min<int64_t>(a, b); }
    float   Floats(float a, float b) const override     { return std::min(a, b); }
};

struct MaxNode final : VariadicNumbersNode
{
    MaxNode(): VariadicNumbersNode("crude.Max", "Max", "Math") {}

    int64_t Int32s(int64_t a, int32_t b) const override { return std::max<int64_t>(a, b); }
    float   Floats(float a, float b) const override     { return std::max(a, b); }
};

// A min above the max is an error.
struct ClampNode final : NodeType
{
    ClampNode(): NodeType("crude.Clamp", "Clamp", "Math") {}

    void Declare(NodeDeclaration& declaration, const NodeConfig&, const Bindings&) const override
    {
        auto T = Numbers(declaration);
        declaration.Input("value", "Value", T);
        declaration.Input("min", "Min", T);
        declaration.Input("max", "Max", T);
        declaration.Output("result", "Result", T);
    }

    Result<void> Evaluate(EvalContext& context) const override
    {
        Value values[3];
        const char* keys[] = { "value", "min", "max" };
        for (int i = 0; i < 3; ++i)
        {
            auto value = context.Get(keys[i]);
            if (!value)
                return value.GetError();
            values[i] = value.GetValue();
        }
        auto& [value, low, high] = values;
        if (context.TypeOf("result") == &Int32Type())
        {
            if (low.AsInt32() > high.AsInt32())
                return Error(BlueprintError::OutOfRange, "the min, " + std::to_string(low.AsInt32()) + ", is above the max, " + std::to_string(high.AsInt32()));
            context.Set("result", Value::Int32(std::clamp(value.AsInt32(), low.AsInt32(), high.AsInt32())));
        }
        else
        {
            if (!(low.AsFloat() <= high.AsFloat()))
                return Error(BlueprintError::OutOfRange, "the min, " + FloatValueType().Format(low) + ", is above the max, " + FloatValueType().Format(high));
            context.Set("result", Value::Float(std::clamp(value.AsFloat(), low.AsFloat(), high.AsFloat())));
        }
        return {};
    }
};

// From 0 up to 1, and the same within a step: pure outputs are computed once a step.
struct RandomNode final : NodeType
{
    RandomNode(): NodeType("crude.Random", "Random", "Math") {}

    void Declare(NodeDeclaration& declaration, const NodeConfig&, const Bindings&) const override
    {
        declaration.Output("value", "Value", FloatType());
    }

    Result<void> Evaluate(EvalContext& context) const override
    {
        auto& engine = context.State<std::mt19937>();
        context.Set("value", Value::Float(std::uniform_real_distribution<float>(0.0f, 1.0f)(engine)));
        return {};
    }
};

// a and b of any type T, by their type's own equality.
struct EqualityNode final : NodeType
{
    EqualityNode(std::string name, std::string title, bool equal): NodeType(std::move(name), std::move(title), "Logic"), m_Equal(equal) {}

    void Declare(NodeDeclaration& declaration, const NodeConfig&, const Bindings&) const override
    {
        auto T = declaration.Variable("T");
        declaration.Input("a", "A", T);
        declaration.Input("b", "B", T);
        declaration.Output("result", "Result", BoolType());
    }

    Result<void> Evaluate(EvalContext& context) const override
    {
        auto a = context.Get("a");
        if (!a)
            return a.GetError();
        auto b = context.Get("b");
        if (!b)
            return b.GetError();
        auto& value = a.GetValue();
        context.Set("result", Value::Bool(value.GetType()->m_Head->Equal(value, b.GetValue()) == m_Equal));
        return {};
    }

    const bool m_Equal;
};

// a and b in order: Int32s and Floats as numbers, Strings by their bytes. NaN is in no order.
struct OrderNode final : NodeType
{
    using Holds = bool (*)(std::partial_ordering order);

    OrderNode(std::string name, std::string title, Holds holds): NodeType(std::move(name), std::move(title), "Logic"), m_Holds(holds) {}

    void Declare(NodeDeclaration& declaration, const NodeConfig&, const Bindings&) const override
    {
        auto T = declaration.Variable("T", { &Int32ValueType(), &FloatValueType(), &StringValueType() });
        declaration.Input("a", "A", T);
        declaration.Input("b", "B", T);
        declaration.Output("result", "Result", BoolType());
    }

    Result<void> Evaluate(EvalContext& context) const override
    {
        auto a = context.Get("a");
        if (!a)
            return a.GetError();
        auto b = context.Get("b");
        if (!b)
            return b.GetError();
        auto& x = a.GetValue();
        auto& y = b.GetValue();
        auto  type  = x.GetType();
        auto  order = type == &Int32Type() ? std::partial_ordering(x.AsInt32() <=> y.AsInt32())
                    : type == &FloatType() ? x.AsFloat() <=> y.AsFloat()
                    : std::partial_ordering(x.AsString().compare(y.AsString()) <=> 0);
        context.Set("result", Value::Bool(m_Holds(order)));
        return {};
    }

    const Holds m_Holds;
};

// Of a group of Bools, read in order until one decides the result: false for And, true for Or.
struct ConnectiveNode final : NodeType
{
    ConnectiveNode(std::string name, std::string title, bool decider): NodeType(std::move(name), std::move(title), "Logic"), m_Decider(decider) {}

    void Declare(NodeDeclaration& declaration, const NodeConfig&, const Bindings&) const override
    {
        auto count = declaration.Group("in", 2, 16);
        for (int i = 0; i < count; ++i)
            declaration.Input("in." + std::to_string(i), "", BoolType());
        declaration.Output("result", "Result", BoolType());
    }

    Result<void> Evaluate(EvalContext& context) const override
    {
        for (int i = 0; context.TypeOf("in." + std::to_string(i)); ++i)
        {
            auto value = context.Get<bool>("in." + std::to_string(i));
            if (!value)
                return value.GetError();
            if (value.GetValue() == m_Decider)
            {
                context.Set("result", Value::Bool(m_Decider));
                return {};
            }
        }
        context.Set("result", Value::Bool(!m_Decider));
        return {};
    }

    const bool m_Decider;
};

struct NotNode final : NodeType
{
    NotNode(): NodeType("crude.Not", "Not", "Logic") {}

    void Declare(NodeDeclaration& declaration, const NodeConfig&, const Bindings&) const override
    {
        declaration.Input("value", "Value", BoolType());
        declaration.Output("result", "Result", BoolType());
    }

    Result<void> Evaluate(EvalContext& context) const override
    {
        auto value = context.Get<bool>("value");
        if (!value)
            return value.GetError();
        context.Set("result", Value::Bool(!value.GetValue()));
        return {};
    }
};

struct XorNode final : NodeType
{
    XorNode(): NodeType("crude.Xor", "Xor", "Logic") {}

    void Declare(NodeDeclaration& declaration, const NodeConfig&, const Bindings&) const override
    {
        declaration.Input("a", "A", BoolType());
        declaration.Input("b", "B", BoolType());
        declaration.Output("result", "Result", BoolType());
    }

    Result<void> Evaluate(EvalContext& context) const override
    {
        auto a = context.Get<bool>("a");
        if (!a)
            return a.GetError();
        auto b = context.Get<bool>("b");
        if (!b)
            return b.GetError();
        context.Set("result", Value::Bool(a.GetValue() != b.GetValue()));
        return {};
    }
};

struct BranchNode final : NodeType
{
    BranchNode(): NodeType("crude.Branch", "Branch", "Flow") {}

    void Declare(NodeDeclaration& declaration, const NodeConfig&, const Bindings&) const override
    {
        declaration.FlowInput("enter");
        declaration.Input("condition", "Condition", BoolType());
        declaration.FlowOutput("true", "True");
        declaration.FlowOutput("false", "False");
    }

    Result<Exit> Execute(ExecContext& context, std::string_view) const override
    {
        auto condition = context.Get<bool>("condition");
        if (!condition)
            return condition.GetError();
        return Exit::To(condition.GetValue() ? "true" : "false");
    }
};

// Unreal's: from the first index to the last, inclusive, reading the last again before each round.
struct ForLoopNode final : NodeType
{
    ForLoopNode(): NodeType("crude.ForLoop", "For Loop", "Flow") {}

    void Declare(NodeDeclaration& declaration, const NodeConfig&, const Bindings&) const override
    {
        declaration.FlowInput("enter");
        declaration.Input("first", "First Index", Int32Type());
        declaration.Input("last", "Last Index", Int32Type(), { .m_Default = Value::Int32(10) });
        declaration.FlowOutput("body", "Loop Body");
        declaration.Output("index", "Index", Int32Type());
        declaration.FlowOutput("completed", "Completed");
    }

    Result<Exit> Execute(ExecContext& context, std::string_view) const override
    {
        // Wider than Int32, so a last index of its maximum ends the loop.
        auto& index = context.State<int64_t>();
        if (context.IsAgain())
            ++index;
        else
        {
            auto first = context.Get<int32_t>("first");
            if (!first)
                return first.GetError();
            index = first.GetValue();
        }
        auto last = context.Get<int32_t>("last");
        if (!last)
            return last.GetError();
        if (index > last.GetValue())
            return Exit::To("completed");
        context.Set("index", Value::Int32(static_cast<int32_t>(index)));
        return Exit::Loop("body");
    }
};

} } // namespace crude_blueprint { namespace {

crude_blueprint::Result<void> crude_blueprint::RegisterLibrary(Registry& registry)
{
    std::unique_ptr<NodeType> types[] =
    {
        std::make_unique<StartNode>(),
        std::make_unique<PrintNode>(),
        std::make_unique<ConstantNode>(),
        std::make_unique<AddNode>(),
        std::make_unique<SubtractNode>(),
        std::make_unique<MultiplyNode>(),
        std::make_unique<DivideNode>(),
        std::make_unique<ModuloNode>(),
        std::make_unique<NegateNode>(),
        std::make_unique<AbsNode>(),
        std::make_unique<MinNode>(),
        std::make_unique<MaxNode>(),
        std::make_unique<ClampNode>(),
        std::make_unique<RandomNode>(),
        std::make_unique<EqualityNode>("crude.Equal", "Equal", true),
        std::make_unique<EqualityNode>("crude.NotEqual", "Not Equal", false),
        std::make_unique<OrderNode>("crude.Less", "Less", [](std::partial_ordering order) { return order < 0; }),
        std::make_unique<OrderNode>("crude.LessOrEqual", "Less Or Equal", [](std::partial_ordering order) { return order <= 0; }),
        std::make_unique<OrderNode>("crude.Greater", "Greater", [](std::partial_ordering order) { return order > 0; }),
        std::make_unique<OrderNode>("crude.GreaterOrEqual", "Greater Or Equal", [](std::partial_ordering order) { return order >= 0; }),
        std::make_unique<ConnectiveNode>("crude.And", "And", false),
        std::make_unique<ConnectiveNode>("crude.Or", "Or", true),
        std::make_unique<NotNode>(),
        std::make_unique<XorNode>(),
        std::make_unique<BranchNode>(),
        std::make_unique<ForLoopNode>(),
    };
    for (auto& type : types)
        if (auto result = registry.AddNodeType(std::move(type)); !result)
            return result;
    return {};
}
