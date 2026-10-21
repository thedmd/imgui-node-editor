# include "crude_blueprint_library.h"
# include <limits>
# include <memory>
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
        std::make_unique<BranchNode>(),
        std::make_unique<ForLoopNode>(),
    };
    for (auto& type : types)
        if (auto result = registry.AddNodeType(std::move(type)); !result)
            return result;
    return {};
}
