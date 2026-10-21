//------------------------------------------------------------------------------
// blueprints2-example's runtime tests: node types made of functions, and a
// blueprint of one graph to edit.
//------------------------------------------------------------------------------
# pragma once
# include "harness.h"
# include "crude_blueprint.h"
# include <functional>
# include <memory>
# include <string>
# include <string_view>

namespace blueprints2_tests {

using namespace crude_blueprint;

// A node type that declares its pins with a function.
struct TestNodeType final : NodeType
{
    using Declaration = std::function<void(NodeDeclaration&, const NodeConfig&, const Bindings&)>;

    TestNodeType(std::string title, Declaration declare)
        : NodeType("test." + title, title)
        , m_Declare(std::move(declare))
    {
    }

    void Declare(NodeDeclaration& declaration, const NodeConfig& config, const Bindings& bindings) const override
    {
        m_Declare(declaration, config, bindings);
    }

    const Declaration m_Declare;
};

// A blueprint with one graph. Its helpers fail the test when a step it needs isn't done.
struct BlueprintFixture
{
    explicit BlueprintFixture(std::shared_ptr<const Registry> registry)
        : m_Blueprint(std::move(registry), std::make_shared<TypeTable>())
        , m_Graph(m_Blueprint.AddGraph("Main"))
    {
    }

    Id Add(const char* type, NodeConfig config = {})
    {
        auto node = m_Graph.AddNode(std::string("test.") + type, std::move(config));
        if (!node)
            test::Check(false, "%s is added: %s", type, std::string(node.GetError().Message()).c_str());
        return node.ValueOr(0);
    }

    Id Pin(Id node, std::string_view key) const
    {
        auto found = m_Graph.FindNode(node);
        auto pin   = found ? found->FindPin(key) : nullptr;
        if (!pin)
            test::Check(false, "node %d has a pin %s", static_cast<int>(node), std::string(key).c_str());
        return pin ? pin->m_Id : 0;
    }

    std::string Type(Id node, std::string_view key) const
    {
        return m_Graph.TypeText(Pin(node, key));
    }

    Result<Id> Connect(Id from, std::string_view output, Id to, std::string_view input, LinkMode mode = LinkMode::Plain)
    {
        return m_Graph.Connect(Pin(from, output), Pin(to, input), mode);
    }

    Id Link(Id from, std::string_view output, Id to, std::string_view input, LinkMode mode = LinkMode::Plain)
    {
        auto link = Connect(from, output, to, input, mode);
        if (!link)
            test::Check(false, "%s.%s -> %s.%s is made: %s", Name(from).c_str(), std::string(output).c_str(), Name(to).c_str(), std::string(input).c_str(), std::string(link.GetError().Message()).c_str());
        return link.ValueOr(0);
    }

    void Unlink(Id link)
    {
        if (auto result = m_Graph.Disconnect(link); !result)
            test::Check(false, "link %d is removed: %s", static_cast<int>(link), std::string(result.GetError().Message()).c_str());
    }

    void Remove(Id node)
    {
        if (auto result = m_Graph.RemoveNode(node); !result)
            test::Check(false, "node %d is removed: %s", static_cast<int>(node), std::string(result.GetError().Message()).c_str());
    }

    size_t PinCount(Id node) const
    {
        auto found = m_Graph.FindNode(node);
        return found ? found->m_Pins.size() : 0;
    }

    std::string Name(Id node) const
    {
        auto found = m_Graph.FindNode(node);
        return found ? found->Name() : "?";
    }

    std::string Keys(Id node) const
    {
        std::string keys;
        if (auto found = m_Graph.FindNode(node))
            for (auto& pin : found->m_Pins)
                keys += (keys.empty() ? "" : " ") + pin.m_Key;
        return keys;
    }

    Blueprint m_Blueprint;
    Graph&    m_Graph;
};

template <typename T>
std::string Message(const Result<T>& result)
{
    return result ? std::string() : std::string(result.GetError().Message());
}

inline NodeConfig Chosen(const char* variable, const Type& type)
{
    NodeConfig config;
    config.m_Types[variable] = &type;
    return config;
}

inline bool IsActive(const Graph& graph, const Result<Id>& link)
{
    auto found = link ? graph.FindLink(link.GetValue()) : nullptr;
    return found && found->m_Active;
}

inline std::string Reason(const Graph& graph, const Result<Id>& link)
{
    auto found = link ? graph.FindLink(link.GetValue()) : nullptr;
    return found ? std::string(found->m_Reason.Message()) : "no link";
}

} // namespace blueprints2_tests
