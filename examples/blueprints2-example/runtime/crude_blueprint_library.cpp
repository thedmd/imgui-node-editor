# include "crude_blueprint_library.h"
# include <algorithm>
# include <cmath>
# include <compare>
# include <cstdint>
# include <limits>
# include <memory>
# include <optional>
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

// A or B: only the one it picks is computed.
struct SelectNode final : NodeType
{
    SelectNode(): NodeType("crude.Select", "Select", "Values") {}

    void Declare(NodeDeclaration& declaration, const NodeConfig&, const Bindings&) const override
    {
        auto T = declaration.Variable("T");
        declaration.Input("pickA", "Pick A", BoolType());
        declaration.Input("a", "A", T);
        declaration.Input("b", "B", T);
        declaration.Output("result", "Result", T);
    }

    Result<void> Evaluate(EvalContext& context) const override
    {
        auto pickA = context.Get<bool>("pickA");
        if (!pickA)
            return pickA.GetError();
        auto value = context.Get(pickA.GetValue() ? "a" : "b");
        if (!value)
            return value.GetError();
        context.Set("result", value.GetValue());
        return {};
    }
};

// As the cast to String writes a value.
struct ToStringNode final : NodeType
{
    ToStringNode(): NodeType("crude.ToString", "To String", "Text") {}

    void Declare(NodeDeclaration& declaration, const NodeConfig&, const Bindings&) const override
    {
        auto T = declaration.Variable("T");
        declaration.Input("value", "Value", T);
        declaration.Output("text", "Text", StringType());
    }

    Result<void> Evaluate(EvalContext& context) const override
    {
        auto value = context.Get("value");
        if (!value)
            return value.GetError();
        context.Set("text", Value::String(value.GetValue().GetType()->m_Head->Format(value.GetValue())));
        return {};
    }
};

// Of a String, its characters, counted as UTF-8 code points; of an array, its items.
struct LengthNode final : NodeType
{
    LengthNode(): NodeType("crude.Length", "Length", "Text") {}

    void Declare(NodeDeclaration& declaration, const NodeConfig&, const Bindings&) const override
    {
        auto T = declaration.Variable("T", { &StringValueType(), &ArrayValueType() });
        declaration.Input("value", "Value", T);
        declaration.Output("length", "Length", Int32Type());
    }

    Result<void> Evaluate(EvalContext& context) const override
    {
        auto value = context.Get("value");
        if (!value)
            return value.GetError();
        auto& read = value.GetValue();
        size_t length = 0;
        if (read.GetType() == &StringType())
            length = static_cast<size_t>(std::count_if(read.AsString().begin(), read.AsString().end(), [](char c) { return (static_cast<unsigned char>(c) & 0xC0) != 0x80; }));
        else
            length = read.AsArray().size();
        auto result = Int32Result(static_cast<int64_t>(length), "the length");
        if (!result)
            return result.GetError();
        context.Set("length", result.GetValue());
        return {};
    }
};

TypeTerm ArrayOf(TypeVariable item)
{
    return TypeTerm(ArrayValueType(), { item });
}

std::string Items(size_t count)
{
    return std::to_string(count) + (count == 1 ? " item" : " items");
}

// An index outside the array is an error.
Result<size_t> ItemIndex(EvalContext& context, size_t size)
{
    auto index = context.Get<int32_t>("index");
    if (!index)
        return index.GetError();
    if (index.GetValue() < 0 || static_cast<size_t>(index.GetValue()) >= size)
        return Error(BlueprintError::OutOfRange, "index " + std::to_string(index.GetValue()) + " is outside the array of " + Items(size));
    return static_cast<size_t>(index.GetValue());
}

struct MakeArrayNode final : NodeType
{
    MakeArrayNode(): NodeType("crude.MakeArray", "Make Array", "Arrays") {}

    void Declare(NodeDeclaration& declaration, const NodeConfig&, const Bindings&) const override
    {
        auto T     = declaration.Variable("T");
        auto count = declaration.Group("in", 1, 16);
        for (int i = 0; i < count; ++i)
            declaration.Input("in." + std::to_string(i), "", T);
        declaration.Output("array", "Array", ArrayOf(T));
    }

    Result<void> Evaluate(EvalContext& context) const override
    {
        auto items = context.Inputs("in");
        if (!items)
            return items.GetError();
        context.Set("array", Value::Array(*context.TypeOf("array"), std::move(items).GetValue()));
        return {};
    }
};

// An item for each output of its group; an array too short for them is an error.
struct BreakArrayNode final : NodeType
{
    BreakArrayNode(): NodeType("crude.BreakArray", "Break Array", "Arrays") {}

    void Declare(NodeDeclaration& declaration, const NodeConfig&, const Bindings&) const override
    {
        auto T = declaration.Variable("T");
        declaration.Input("array", "Array", ArrayOf(T));
        auto count = declaration.Group("out", 1, 16);
        for (int i = 0; i < count; ++i)
            declaration.Output("out." + std::to_string(i), std::to_string(i), T);
    }

    Result<void> Evaluate(EvalContext& context) const override
    {
        auto array = context.Get("array");
        if (!array)
            return array.GetError();
        auto items = array.GetValue().AsArray();
        for (size_t i = 0; context.HasPin("out." + std::to_string(i)); ++i)
        {
            if (i >= items.size())
                return Error(BlueprintError::OutOfRange, "the array has " + Items(items.size()) + ", not the " + std::to_string(i + 1) + " it breaks into");
            context.Set("out." + std::to_string(i), items[i]);
        }
        return {};
    }
};

struct GetNode final : NodeType
{
    GetNode(): NodeType("crude.Get", "Get", "Arrays") {}

    void Declare(NodeDeclaration& declaration, const NodeConfig&, const Bindings&) const override
    {
        auto T = declaration.Variable("T");
        declaration.Input("array", "Array", ArrayOf(T));
        declaration.Input("index", "Index", Int32Type());
        declaration.Output("item", "Item", T);
    }

    Result<void> Evaluate(EvalContext& context) const override
    {
        auto array = context.Get("array");
        if (!array)
            return array.GetError();
        auto items = array.GetValue().AsArray();
        auto index = ItemIndex(context, items.size());
        if (!index)
            return index.GetError();
        context.Set("item", items[index.GetValue()]);
        return {};
    }
};

// A copy of the array, with the item at the index.
struct SetNode final : NodeType
{
    SetNode(): NodeType("crude.Set", "Set", "Arrays") {}

    void Declare(NodeDeclaration& declaration, const NodeConfig&, const Bindings&) const override
    {
        auto T = declaration.Variable("T");
        declaration.Input("array", "Array", ArrayOf(T));
        declaration.Input("index", "Index", Int32Type());
        declaration.Input("item", "Item", T);
        declaration.Output("result", "Result", ArrayOf(T));
    }

    Result<void> Evaluate(EvalContext& context) const override
    {
        auto array = context.Get("array");
        if (!array)
            return array.GetError();
        auto items = std::vector<Value>(array.GetValue().AsArray().begin(), array.GetValue().AsArray().end());
        auto index = ItemIndex(context, items.size());
        if (!index)
            return index.GetError();
        auto item = context.Get("item");
        if (!item)
            return item.GetError();
        items[index.GetValue()] = std::move(item).GetValue();
        context.Set("result", Value::Array(*context.TypeOf("result"), std::move(items)));
        return {};
    }
};

// A copy of the array, with the item after its last.
struct AppendNode final : NodeType
{
    AppendNode(): NodeType("crude.Append", "Append", "Arrays") {}

    void Declare(NodeDeclaration& declaration, const NodeConfig&, const Bindings&) const override
    {
        auto T = declaration.Variable("T");
        declaration.Input("array", "Array", ArrayOf(T));
        declaration.Input("item", "Item", T);
        declaration.Output("result", "Result", ArrayOf(T));
    }

    Result<void> Evaluate(EvalContext& context) const override
    {
        auto array = context.Get("array");
        if (!array)
            return array.GetError();
        auto item = context.Get("item");
        if (!item)
            return item.GetError();
        auto items = std::vector<Value>(array.GetValue().AsArray().begin(), array.GetValue().AsArray().end());
        items.push_back(std::move(item).GetValue());
        context.Set("result", Value::Array(*context.TypeOf("result"), std::move(items)));
        return {};
    }
};

// The types a map may take its keys of.
TypeVariable Keys(NodeDeclaration& declaration)
{
    return declaration.Variable("K", { &Int32ValueType(), &StringValueType() });
}

TypeTerm MapOf(TypeVariable key, TypeVariable value)
{
    return TypeTerm(MapValueType(), { key, value });
}

// Of a group of entries, key.0 and value.0 and on; of equal keys, the last entry's stays.
struct MakeMapNode final : NodeType
{
    MakeMapNode(): NodeType("crude.MakeMap", "Make Map", "Maps") {}

    void Declare(NodeDeclaration& declaration, const NodeConfig&, const Bindings&) const override
    {
        auto K     = Keys(declaration);
        auto V     = declaration.Variable("V");
        auto count = declaration.Group("entry", 1, 16, { "key", "value" });
        for (int i = 0; i < count; ++i)
        {
            declaration.Input("key." + std::to_string(i), "Key " + std::to_string(i), K);
            declaration.Input("value." + std::to_string(i), "Value " + std::to_string(i), V);
        }
        declaration.Output("map", "Map", MapOf(K, V));
    }

    Result<void> Evaluate(EvalContext& context) const override
    {
        auto keys = context.Inputs("key");
        if (!keys)
            return keys.GetError();
        auto values = context.Inputs("value");
        if (!values)
            return values.GetError();
        std::vector<std::pair<Value, Value>> entries;
        for (size_t i = 0; i < keys.GetValue().size(); ++i)
            entries.emplace_back(keys.GetValue()[i], values.GetValue()[i]);
        context.Set("map", Value::Map(*context.TypeOf("map"), std::move(entries)));
        return {};
    }
};

// The map and the key, which each node of a map reads first.
struct MapAndKey
{
    Value m_Map;
    Value m_Key;
};

Result<MapAndKey> ReadMapAndKey(EvalContext& context)
{
    auto map = context.Get("map");
    if (!map)
        return map.GetError();
    auto key = context.Get("key");
    if (!key)
        return key.GetError();
    return MapAndKey{ std::move(map).GetValue(), std::move(key).GetValue() };
}

// The value of the key, or its type's default when there's none.
struct FindNode final : NodeType
{
    FindNode(): NodeType("crude.Find", "Find", "Maps") {}

    void Declare(NodeDeclaration& declaration, const NodeConfig&, const Bindings&) const override
    {
        auto K = Keys(declaration);
        auto V = declaration.Variable("V");
        declaration.Input("map", "Map", MapOf(K, V));
        declaration.Input("key", "Key", K);
        declaration.Output("value", "Value", V);
        declaration.Output("found", "Found", BoolType());
    }

    Result<void> Evaluate(EvalContext& context) const override
    {
        auto read = ReadMapAndKey(context);
        if (!read)
            return read.GetError();
        auto  found = read.GetValue().m_Map.Find(read.GetValue().m_Key);
        auto& type  = *context.TypeOf("value");
        context.Set("value", found ? *found : type.m_Head->Default(type));
        context.Set("found", Value::Bool(found != nullptr));
        return {};
    }
};

struct ContainsNode final : NodeType
{
    ContainsNode(): NodeType("crude.Contains", "Contains", "Maps") {}

    void Declare(NodeDeclaration& declaration, const NodeConfig&, const Bindings&) const override
    {
        auto K = Keys(declaration);
        auto V = declaration.Variable("V");
        declaration.Input("map", "Map", MapOf(K, V));
        declaration.Input("key", "Key", K);
        declaration.Output("result", "Result", BoolType());
    }

    Result<void> Evaluate(EvalContext& context) const override
    {
        auto read = ReadMapAndKey(context);
        if (!read)
            return read.GetError();
        context.Set("result", Value::Bool(read.GetValue().m_Map.Find(read.GetValue().m_Key) != nullptr));
        return {};
    }
};

// A copy of the map, with the value at the key, in place of any there.
struct InsertNode final : NodeType
{
    InsertNode(): NodeType("crude.Insert", "Insert", "Maps") {}

    void Declare(NodeDeclaration& declaration, const NodeConfig&, const Bindings&) const override
    {
        auto K = Keys(declaration);
        auto V = declaration.Variable("V");
        declaration.Input("map", "Map", MapOf(K, V));
        declaration.Input("key", "Key", K);
        declaration.Input("value", "Value", V);
        declaration.Output("result", "Result", MapOf(K, V));
    }

    Result<void> Evaluate(EvalContext& context) const override
    {
        auto read = ReadMapAndKey(context);
        if (!read)
            return read.GetError();
        auto value = context.Get("value");
        if (!value)
            return value.GetError();
        auto entries = read.GetValue().m_Map.AsMap();
        std::vector<std::pair<Value, Value>> result(entries.begin(), entries.end());
        result.emplace_back(read.GetValue().m_Key, std::move(value).GetValue());
        context.Set("result", Value::Map(*context.TypeOf("result"), std::move(result)));
        return {};
    }
};

// A copy of the map, without the key.
struct RemoveNode final : NodeType
{
    RemoveNode(): NodeType("crude.Remove", "Remove", "Maps") {}

    void Declare(NodeDeclaration& declaration, const NodeConfig&, const Bindings&) const override
    {
        auto K = Keys(declaration);
        auto V = declaration.Variable("V");
        declaration.Input("map", "Map", MapOf(K, V));
        declaration.Input("key", "Key", K);
        declaration.Output("result", "Result", MapOf(K, V));
    }

    Result<void> Evaluate(EvalContext& context) const override
    {
        auto read = ReadMapAndKey(context);
        if (!read)
            return read.GetError();
        auto& [map, key] = read.GetValue();
        std::vector<std::pair<Value, Value>> result;
        for (auto& entry : map.AsMap())
            if (!(entry.first == key))
                result.push_back(entry);
        context.Set("result", Value::Map(*context.TypeOf("result"), std::move(result)));
        return {};
    }
};

// The keys or the values of a map, in the order of its keys.
struct MapItemsNode final : NodeType
{
    MapItemsNode(std::string name, std::string title, bool keys): NodeType(std::move(name), std::move(title), "Maps"), m_Keys(keys) {}

    void Declare(NodeDeclaration& declaration, const NodeConfig&, const Bindings&) const override
    {
        auto K = Keys(declaration);
        auto V = declaration.Variable("V");
        declaration.Input("map", "Map", MapOf(K, V));
        declaration.Output("items", m_Keys ? "Keys" : "Values", ArrayOf(m_Keys ? K : V));
    }

    Result<void> Evaluate(EvalContext& context) const override
    {
        auto map = context.Get("map");
        if (!map)
            return map.GetError();
        std::vector<Value> items;
        for (auto& [key, value] : map.GetValue().AsMap())
            items.push_back(m_Keys ? key : value);
        context.Set("items", Value::Array(*context.TypeOf("items"), std::move(items)));
        return {};
    }

    const bool m_Keys;
};

// A false condition stops the fiber, with the message.
struct AssertNode final : NodeType
{
    AssertNode(): NodeType("crude.Assert", "Assert", "Debugging") {}

    void Declare(NodeDeclaration& declaration, const NodeConfig&, const Bindings&) const override
    {
        declaration.FlowInput("enter");
        declaration.Input("condition", "Condition", BoolType());
        declaration.Input("message", "Message", StringType());
        declaration.FlowOutput("then");
    }

    Result<Exit> Execute(ExecContext& context, std::string_view) const override
    {
        auto condition = context.Get<bool>("condition");
        if (!condition)
            return condition.GetError();
        if (condition.GetValue())
            return Exit::To("then");
        auto message = context.Get<std::string>("message");
        if (!message)
            return message.GetError();
        return Error(LibraryError::AssertFailed, message.GetValue().empty() ? std::string("an assertion failed") : "assertion failed: " + message.GetValue());
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

// Each output of its group in turn, once the branch from the one before has ended.
struct SequenceNode final : NodeType
{
    SequenceNode(): NodeType("crude.Sequence", "Sequence", "Flow") {}

    void Declare(NodeDeclaration& declaration, const NodeConfig&, const Bindings&) const override
    {
        declaration.FlowInput("enter");
        auto count = declaration.Group("then", 2, 16);
        for (int i = 0; i < count; ++i)
            declaration.FlowOutput("then." + std::to_string(i), "Then " + std::to_string(i));
    }

    Result<Exit> Execute(ExecContext& context, std::string_view) const override
    {
        auto& next = context.State<int>();
        if (!context.IsAgain())
            next = 0;
        auto key = "then." + std::to_string(next);
        if (!context.HasPin(key))
            return Exit::None();
        ++next;
        return Exit::Loop(std::move(key));
    }
};

// The case of the selection's index, or the default for one without a case.
struct SwitchOnIntNode final : NodeType
{
    SwitchOnIntNode(): NodeType("crude.SwitchOnInt", "Switch on Int", "Flow") {}

    void Declare(NodeDeclaration& declaration, const NodeConfig&, const Bindings&) const override
    {
        declaration.FlowInput("enter");
        declaration.Input("selection", "Selection", Int32Type());
        auto count = declaration.Group("case", 1, 16);
        for (int i = 0; i < count; ++i)
            declaration.FlowOutput("case." + std::to_string(i), std::to_string(i));
        declaration.FlowOutput("default", "Default");
    }

    Result<Exit> Execute(ExecContext& context, std::string_view) const override
    {
        auto selection = context.Get<int32_t>("selection");
        if (!selection)
            return selection.GetError();
        auto key = "case." + std::to_string(selection.GetValue());
        return Exit::To(selection.GetValue() >= 0 && context.HasPin(key) ? std::move(key) : std::string("default"));
    }
};

// Of an array it reads again before each round, so its items may change while it runs.
struct ForEachNode final : NodeType
{
    ForEachNode(): NodeType("crude.ForEach", "For Each", "Flow") {}

    void Declare(NodeDeclaration& declaration, const NodeConfig&, const Bindings&) const override
    {
        auto T = declaration.Variable("T");
        declaration.FlowInput("enter");
        declaration.Input("array", "Array", TypeTerm(ArrayValueType(), { T }));
        declaration.FlowOutput("body", "Loop Body");
        declaration.Output("element", "Element", T);
        declaration.Output("index", "Index", Int32Type());
        declaration.FlowOutput("completed", "Completed");
    }

    Result<Exit> Execute(ExecContext& context, std::string_view) const override
    {
        auto& index = context.State<size_t>();
        if (context.IsAgain())
            ++index;
        else
            index = 0;
        auto array = context.Get("array");
        if (!array)
            return array.GetError();
        auto items = array.GetValue().AsArray();
        if (index >= items.size())
            return Exit::To("completed");
        context.Set("element", items[index]);
        context.Set("index", Value::Int32(static_cast<int32_t>(index)));
        return Exit::Loop("body");
    }
};

// Of a map it reads again before each round, in the order of its keys.
struct ForEachEntryNode final : NodeType
{
    ForEachEntryNode(): NodeType("crude.ForEachEntry", "For Each Entry", "Flow") {}

    void Declare(NodeDeclaration& declaration, const NodeConfig&, const Bindings&) const override
    {
        auto K = Keys(declaration);
        auto V = declaration.Variable("V");
        declaration.FlowInput("enter");
        declaration.Input("map", "Map", MapOf(K, V));
        declaration.FlowOutput("body", "Loop Body");
        declaration.Output("key", "Key", K);
        declaration.Output("value", "Value", V);
        declaration.FlowOutput("completed", "Completed");
    }

    Result<Exit> Execute(ExecContext& context, std::string_view) const override
    {
        auto& index = context.State<size_t>();
        if (context.IsAgain())
            ++index;
        else
            index = 0;
        auto map = context.Get("map");
        if (!map)
            return map.GetError();
        auto entries = map.GetValue().AsMap();
        if (index >= entries.size())
            return Exit::To("completed");
        context.Set("key", entries[index].first);
        context.Set("value", entries[index].second);
        return Exit::Loop("body");
    }
};

// Reads its condition again before each round.
struct WhileLoopNode final : NodeType
{
    WhileLoopNode(): NodeType("crude.WhileLoop", "While Loop", "Flow") {}

    void Declare(NodeDeclaration& declaration, const NodeConfig&, const Bindings&) const override
    {
        declaration.FlowInput("enter");
        declaration.Input("condition", "Condition", BoolType());
        declaration.FlowOutput("body", "Loop Body");
        declaration.FlowOutput("completed", "Completed");
    }

    Result<Exit> Execute(ExecContext& context, std::string_view) const override
    {
        auto condition = context.Get<bool>("condition");
        if (!condition)
            return condition.GetError();
        return condition.GetValue() ? Exit::Loop("body") : Exit::To("completed");
    }
};

// Goes on once, until it's reset.
struct DoOnceNode final : NodeType
{
    DoOnceNode(): NodeType("crude.DoOnce", "Do Once", "Flow") {}

    void Declare(NodeDeclaration& declaration, const NodeConfig&, const Bindings&) const override
    {
        declaration.FlowInput("enter");
        declaration.FlowInput("reset", "Reset");
        declaration.FlowOutput("completed", "Completed");
    }

    Result<Exit> Execute(ExecContext& context, std::string_view entry) const override
    {
        auto& done = context.State<bool>();
        if (entry == "reset")
        {
            done = false;
            return Exit::None();
        }
        if (done)
            return Exit::None();
        done = true;
        return Exit::To("completed");
    }
};

// Unreal's: goes on N times, counting them from 1, until it's reset.
struct DoNNode final : NodeType
{
    DoNNode(): NodeType("crude.DoN", "Do N", "Flow") {}

    void Declare(NodeDeclaration& declaration, const NodeConfig&, const Bindings&) const override
    {
        declaration.FlowInput("enter");
        declaration.Input("n", "N", Int32Type(), { .m_Default = Value::Int32(1) });
        declaration.FlowInput("reset", "Reset");
        declaration.FlowOutput("exit", "Exit");
        declaration.Output("counter", "Counter", Int32Type());
    }

    Result<Exit> Execute(ExecContext& context, std::string_view entry) const override
    {
        auto& counter = context.State<int32_t>();
        if (entry == "reset")
        {
            counter = 0;
            return Exit::None();
        }
        auto n = context.Get<int32_t>("n");
        if (!n)
            return n.GetError();
        if (counter >= n.GetValue())
            return Exit::None();
        context.Set("counter", Value::Int32(++counter));
        return Exit::To("exit");
    }
};

// Lets the flow through while it's open; it starts as Start Closed says, on the first entry.
struct GateNode final : NodeType
{
    GateNode(): NodeType("crude.Gate", "Gate", "Flow") {}

    void Declare(NodeDeclaration& declaration, const NodeConfig&, const Bindings&) const override
    {
        declaration.FlowInput("enter");
        declaration.FlowInput("open", "Open");
        declaration.FlowInput("close", "Close");
        declaration.FlowInput("toggle", "Toggle");
        declaration.Input("startClosed", "Start Closed", BoolType(), { .m_Default = Value::Bool(true) });
        declaration.FlowOutput("exit", "Exit");
    }

    Result<Exit> Execute(ExecContext& context, std::string_view entry) const override
    {
        auto& open = context.State<std::optional<bool>>();
        if (!open)
        {
            auto closed = context.Get<bool>("startClosed");
            if (!closed)
                return closed.GetError();
            open = !closed.GetValue();
        }
        if (entry == "open")
            open = true;
        else if (entry == "close")
            open = false;
        else if (entry == "toggle")
            open = !*open;
        else if (*open)
            return Exit::To("exit");
        return Exit::None();
    }
};

// A, then B, then A again.
struct FlipFlopNode final : NodeType
{
    FlipFlopNode(): NodeType("crude.FlipFlop", "Flip Flop", "Flow") {}

    void Declare(NodeDeclaration& declaration, const NodeConfig&, const Bindings&) const override
    {
        declaration.FlowInput("enter");
        declaration.FlowOutput("a", "A");
        declaration.FlowOutput("b", "B");
        declaration.Output("isA", "Is A", BoolType());
    }

    Result<Exit> Execute(ExecContext& context, std::string_view) const override
    {
        auto& isA = context.State<bool>();
        isA = !isA;
        context.Set("isA", Value::Bool(isA));
        return Exit::To(isA ? "a" : "b");
    }
};

// A struct's name without its namespace: "Vector2" of "example.Vector2".
std::string ShortName(const StructType& type)
{
    auto dot = type.m_Name.find_last_of('.');
    return dot == std::string::npos ? type.m_Name : type.m_Name.substr(dot + 1);
}

// The struct of its fields.
struct MakeStructNode final : NodeType
{
    explicit MakeStructNode(const StructType& type): NodeType("make:" + type.m_Name, "Make " + ShortName(type), "Structs"), m_Struct(type) {}

    void Declare(NodeDeclaration& declaration, const NodeConfig&, const Bindings&) const override
    {
        for (auto& field : m_Struct.Fields())
            declaration.Input(field.m_Name, field.m_Name, *field.m_Type);
        declaration.Output("struct", ShortName(m_Struct), m_Struct.AsType());
    }

    Result<void> Evaluate(EvalContext& context) const override
    {
        std::vector<Value> fields;
        for (auto& field : m_Struct.Fields())
        {
            auto value = context.Get(field.m_Name);
            if (!value)
                return value.GetError();
            fields.push_back(std::move(value).GetValue());
        }
        context.Set("struct", Value::Struct(m_Struct.AsType(), std::move(fields)));
        return {};
    }

    const StructType& m_Struct;
};

// The fields of the struct.
struct BreakStructNode final : NodeType
{
    explicit BreakStructNode(const StructType& type): NodeType("break:" + type.m_Name, "Break " + ShortName(type), "Structs"), m_Struct(type) {}

    void Declare(NodeDeclaration& declaration, const NodeConfig&, const Bindings&) const override
    {
        declaration.Input("struct", ShortName(m_Struct), m_Struct.AsType());
        for (auto& field : m_Struct.Fields())
            declaration.Output(field.m_Name, field.m_Name, *field.m_Type);
    }

    Result<void> Evaluate(EvalContext& context) const override
    {
        auto value = context.Get("struct");
        if (!value)
            return value.GetError();
        auto fields = value.GetValue().AsStruct();
        for (size_t i = 0; i < m_Struct.Fields().size(); ++i)
            context.Set(m_Struct.Fields()[i].m_Name, fields[i]);
        return {};
    }

    const StructType& m_Struct;
};

} } // namespace crude_blueprint { namespace {

crude_blueprint::Result<void> crude_blueprint::RegisterStructNodes(Registry& registry, const StructType& type)
{
    for (auto& field : type.Fields())
        if (field.m_Name == "struct")
            return Error(BlueprintError::Duplicate, type.m_Name + "'s field struct has the key of the struct's pin");
    if (auto made = registry.AddNodeType(std::make_unique<MakeStructNode>(type)); !made)
        return made;
    return registry.AddNodeType(std::make_unique<BreakStructNode>(type));
}

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
        std::make_unique<SelectNode>(),
        std::make_unique<ToStringNode>(),
        std::make_unique<LengthNode>(),
        std::make_unique<MakeArrayNode>(),
        std::make_unique<BreakArrayNode>(),
        std::make_unique<GetNode>(),
        std::make_unique<SetNode>(),
        std::make_unique<AppendNode>(),
        std::make_unique<MakeMapNode>(),
        std::make_unique<FindNode>(),
        std::make_unique<ContainsNode>(),
        std::make_unique<InsertNode>(),
        std::make_unique<RemoveNode>(),
        std::make_unique<MapItemsNode>("crude.Keys", "Keys", true),
        std::make_unique<MapItemsNode>("crude.Values", "Values", false),
        std::make_unique<AssertNode>(),
        std::make_unique<BranchNode>(),
        std::make_unique<ForLoopNode>(),
        std::make_unique<SequenceNode>(),
        std::make_unique<SwitchOnIntNode>(),
        std::make_unique<ForEachNode>(),
        std::make_unique<ForEachEntryNode>(),
        std::make_unique<WhileLoopNode>(),
        std::make_unique<DoOnceNode>(),
        std::make_unique<DoNNode>(),
        std::make_unique<GateNode>(),
        std::make_unique<FlipFlopNode>(),
    };
    for (auto& type : types)
        if (auto result = registry.AddNodeType(std::move(type)); !result)
            return result;
    return {};
}
