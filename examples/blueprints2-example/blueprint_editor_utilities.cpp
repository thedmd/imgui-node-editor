# define IMGUI_DEFINE_MATH_OPERATORS
# include "blueprint_editor_utilities.h"
# include "blueprint_editor_document.h"
# include "crude_logger.h"
# include <imgui_internal.h>
# include "imgui_extras.h"
# include <algorithm>
# include <cmath>
# include <cstdint>
# include <cstdio>
# include <optional>
# include <tuple>
# include <utility>

blueprint_editor_utilities::IconType blueprint_editor_utilities::PinIcon(const Pin& pin)
{
    if (pin.m_Flow)
        return IconType::Flow;
    if (!pin.m_DecidedType)
        return IconType::Diamond;
    auto head = pin.m_DecidedType->m_Head;
    if (IsA<StructType>(head))
        return IconType::RoundSquare;
    if (head == &ArrayValueType())
        return IconType::Grid;
    if (head == &MapValueType())
        return IconType::Square;
    return IconType::Circle;
}

ImVec4 blueprint_editor_utilities::PinColor(const Pin& pin)
{
    if (pin.m_Flow)
        return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    if (!pin.m_DecidedType)
        return ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
    return TypeColor(pin.m_DecidedType);
}

ImVec4 blueprint_editor_utilities::HeaderColor(const Blueprint& blueprint, const Node& node)
{
    auto rgb = [](uint32_t color) { return ImVec4(static_cast<float>((color >> 16) & 0xFF) / 255.0f, static_cast<float>((color >> 8) & 0xFF) / 255.0f, static_cast<float>(color & 0xFF) / 255.0f, 1.0f); };
    auto& type = *node.m_Type;
    if (!type.EventName(node.m_Config).empty())
        return rgb(0xC43B3B);
    if (blueprint.MaySuspend(type))
        return rgb(0x1F8A8A);
    if (blueprint.FunctionOf(type))
        return type.m_Name.starts_with("call:") ? rgb(0x3568B8) : rgb(0x6E4FB3);
    if (type.m_Name == "crude.Print" || type.m_Name == "crude.Assert")
        return rgb(0xA66A1E);
    if (std::none_of(node.m_Pins.begin(), node.m_Pins.end(), [](const Pin& pin) { return pin.m_Flow; }))
        return rgb(0x3C8A52);
    return rgb(0x5A6270);
}

// Arrays take their items' colors, and maps their values'. Other types without a color of their
// own take one from their name, so each keeps it.
ImVec4 blueprint_editor_utilities::TypeColor(const Type* type)
{
    if (!type)
        return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    auto head = type->m_Head;
    if (head == &ArrayValueType())
        return TypeColor(type->m_Args[0]);
    if (head == &MapValueType())
        return TypeColor(type->m_Args[1]);
    if (head == &BoolValueType())
        return ImVec4(220 / 255.0f,  48 / 255.0f,  48 / 255.0f, 1.0f);
    if (head == &Int32ValueType())
        return ImVec4( 68 / 255.0f, 201 / 255.0f, 156 / 255.0f, 1.0f);
    if (head == &FloatValueType())
        return ImVec4(147 / 255.0f, 226 / 255.0f,  74 / 255.0f, 1.0f);
    if (head == &StringValueType())
        return ImVec4(124 / 255.0f,  21 / 255.0f, 153 / 255.0f, 1.0f);

    // A function's type parameter by its own name, which stays when the function is renamed.
    std::string_view name = head->m_Name;
    if (Cast<ParameterType>(head))
        name = name.substr(name.rfind('.') + 1);
    uint32_t hash = 2166136261u;
    for (auto c : name)
        hash = (hash ^ static_cast<uint8_t>(c)) * 16777619u;
    float r, g, b;
    ImGui::ColorConvertHSVtoRGB(static_cast<float>(hash % 360) / 360.0f, 0.6f, 0.9f, r, g, b);
    return ImVec4(r, g, b, 1.0f);
}

std::string blueprint_editor_utilities::TypeParameterText(const Blueprint& blueprint, const Type* type)
{
    if (!type || !Cast<ParameterType>(type->m_Head))
        return {};
    for (auto& graph : blueprint.Graphs())
    {
        auto function = blueprint.FindFunction(graph->GetId());
        for (size_t i = 0; function && i < function->m_Parameters.size(); ++i)
        {
            if (function->m_Parameters[i] != type->m_Head)
                continue;
            auto& parameter = function->m_Signature.m_Parameters[i];
            return parameter.m_Name + ", a type parameter of " + function->m_Name + "; allows " + AllowedText(parameter);
        }
    }
    return {};
}

std::string blueprint_editor_utilities::AllowedText(const VariableDeclaration& parameter)
{
    std::string text;
    for (auto head : parameter.m_Allowed)
        text += (text.empty() ? "" : ", ") + head->m_Name;
    return text.empty() ? "any type" : text;
}

blueprint_editor_utilities::Value blueprint_editor_utilities::InputValue(const Graph& graph, const Pin& pin)
{
    auto type = pin.m_DecidedType;
    if (!type)
        return {};
    if (auto typed = graph.FindValue(pin.m_Id); typed && typed->GetType() == type)
        return *typed;
    if (pin.m_Default.IsValid() && pin.m_Default.GetType() == type)
        return pin.m_Default;
    return type->m_Head->Default(*type);
}

blueprint_editor_utilities::Value blueprint_editor_utilities::KeptValue(const Graph& graph, const Pin& pin)
{
    if (pin.m_Flow || pin.m_DecidedType)
        return {};
    if (pin.m_Direction == Direction::Output)
    {
        auto node  = graph.NodeOf(pin.m_Id);
        auto value = node->m_Config.m_Data.find("value");
        return IsConstant(*node) && value != node->m_Config.m_Data.end() ? value->second : Value();
    }
    for (auto& link : graph.Links())
        if (link.m_Active && link.m_To == pin.m_Id)
            return {};
    auto value = graph.FindValue(pin.m_Id);
    return value ? *value : Value();
}

bool blueprint_editor_utilities::IsConstant(const Node& node)
{
    return node.m_Type->m_Name == "crude.Constant";
}

bool blueprint_editor_utilities::IsCustomEvent(const Node& node)
{
    return node.m_Type->m_Name == "crude.CustomEvent";
}

bool blueprint_editor_utilities::IsReroute(const Node& node)
{
    return node.m_Type->IsReroute();
}

float blueprint_editor_utilities::RerouteSize()
{
    return ImGui::GetTextLineHeight() * 1.5f;
}

ImVec2 blueprint_editor_utilities::RerouteDirection(ImVec2 center, std::span<const ImVec2> sources, std::span<const ImVec2> targets)
{
    auto unitToward = [center](std::span<const ImVec2> points, float sign)
    {
        ImVec2 sum;
        for (auto& point : points)
            sum += point;
        const auto way    = (sum / static_cast<float>(points.size()) - center) * sign;
        const auto length = ImSqrt(way.x * way.x + way.y * way.y);
        return length > 0.0f ? way / length : ImVec2();
    };
    ImVec2 way;
    if (!sources.empty())
        way += unitToward(sources, -1.0f);
    if (!targets.empty())
        way += unitToward(targets, 1.0f);
    if (ImFabs(way.x) >= ImFabs(way.y))
        return ImVec2(way.x < 0.0f ? -1.0f : 1.0f, 0.0f);
    return ImVec2(0.0f, way.y < 0.0f ? -1.0f : 1.0f);
}

std::vector<blueprint_editor_utilities::Id> blueprint_editor_utilities::FlowChain(const Graph& graph, Id link)
{
    std::vector<Id> chain;
    for (auto found = graph.FindLink(link); found && chain.size() <= graph.Links().size();)
    {
        chain.push_back(found->m_Id);
        auto target = graph.NodeOf(found->m_To);
        auto pin    = graph.FindPin(found->m_To);
        auto out    = target && pin && pin->m_Flow && IsReroute(*target) ? target->FindPin("out") : nullptr;
        found = nullptr;
        for (auto& next : graph.Links())
            if (out && next.m_From == out->m_Id && next.m_Active)
                found = &next;
    }
    return chain;
}

std::string blueprint_editor_utilities::OrphanName(const Node& node, const OrphanPin& orphan)
{
    if (!orphan.m_Name.empty())
        return orphan.m_Name;
    if (auto named = node.m_Config.m_Names.find(orphan.m_Key); named != node.m_Config.m_Names.end())
        return named->second;
    if (auto dot = orphan.m_Key.rfind('.'); dot != std::string::npos)
        if (auto whole = node.FindPin(std::string_view(orphan.m_Key).substr(0, dot)); whole && !whole->m_Name.empty())
            return whole->m_Name + " " + orphan.m_Key.substr(dot + 1);
    return orphan.m_Key;
}

const blueprint_editor_utilities::Node* blueprint_editor_utilities::EndNode(const Graph& graph, Id pin)
{
    auto node = graph.NodeOf(pin);
    return node ? node : graph.NodeOfOrphan(pin);
}

std::string blueprint_editor_utilities::EndText(const Graph& graph, Id pin)
{
    if (auto found = graph.FindPin(pin))
        return graph.NodeOf(pin)->Name() + "." + found->m_Key;
    auto node   = graph.NodeOfOrphan(pin);
    auto orphan = node ? node->FindOrphan(pin) : nullptr;
    return orphan ? node->Name() + "." + orphan->m_Key : "pin " + std::to_string(pin);
}

void blueprint_editor_utilities::GoToEnd(const Graph& graph, Id pin)
{
    if (auto node = EndNode(graph, pin))
    {
        ax::NodeEditor::SelectNode(node->m_Id);
        ax::NodeEditor::NavigateToSelection();
    }
}

bool blueprint_editor_utilities::IsComment(const Node& node)
{
    return node.m_Type->m_Name == "crude.Comment";
}

std::string blueprint_editor_utilities::CommentText(const Node& node)
{
    auto text = node.m_Config.m_Data.find("text");
    if (text == node.m_Config.m_Data.end() || text->second.GetType() != &StringType() || text->second.AsString().empty())
        return "Comment";
    return std::string(text->second.AsString());
}

bool blueprint_editor_utilities::IsCompact(const Node& node)
{
    return IsConstant(node) || !GetterVariable(node).empty() || !node.m_Type->m_CompactTitle.empty();
}

std::string_view blueprint_editor_utilities::GetterVariable(const Node& node)
{
    constexpr std::string_view prefix = "get:";
    const std::string_view name = node.m_Type->m_Name;
    return name.starts_with(prefix) ? name.substr(prefix.size()) : std::string_view();
}

namespace blueprint_editor_utilities { namespace {

constexpr float c_Cut        = 10.0f;
constexpr float c_CompactCut = 8.0f;
constexpr float c_BandPad    = 16.0f; // between a title and the band's slant
constexpr float c_Stripe     = 4.0f;  // a compact node's, along its left edge

// Clockwise from the top-left cut. Grown all around, a cut grows by (2 - sqrt(2)) times as much,
// which keeps its edge at 45°.
void OutlinePath(ImDrawList* drawList, ImVec2 a, ImVec2 b, bool compact, float grow = 0.0f)
{
    const auto cut = (compact ? c_CompactCut : c_Cut) + 0.5857864f * grow;
    a -= ImVec2(grow, grow);
    b += ImVec2(grow, grow);
    drawList->PathLineTo(ImVec2(a.x, a.y + cut));
    drawList->PathLineTo(ImVec2(a.x + cut, a.y));
    if (compact)
    {
        drawList->PathLineTo(ImVec2(b.x - cut, a.y));
        drawList->PathLineTo(ImVec2(b.x, a.y + cut));
    }
    else
        drawList->PathLineTo(ImVec2(b.x, a.y));
    drawList->PathLineTo(ImVec2(b.x, b.y - cut));
    drawList->PathLineTo(ImVec2(b.x - cut, b.y));
    if (compact)
    {
        drawList->PathLineTo(ImVec2(a.x + cut, b.y));
        drawList->PathLineTo(ImVec2(a.x, b.y - cut));
    }
    else
        drawList->PathLineTo(ImVec2(a.x, b.y));
}

// The border, stroked inside the outline, and the selection's ring outside it, past a gap.
void StrokeOutline(ImDrawList* drawList, ImVec2 min, ImVec2 max, const NodeLook& look)
{
    OutlinePath(drawList, min, max, look.m_Compact);
    drawList->PathStroke(look.m_Border, look.m_BorderWidth, ImDrawFlags_Closed | ImDrawFlags_StrokeInside);
    if (look.m_Selected)
    {
        // 2 px wide, 2 px out: the inside stroke of the outline grown by both.
        OutlinePath(drawList, min, max, look.m_Compact, 4.0f);
        drawList->PathStroke(IM_COL32(255, 255, 255, 220), 2.0f, ImDrawFlags_Closed | ImDrawFlags_StrokeInside);
    }
}

} } // namespace blueprint_editor_utilities { namespace {

void blueprint_editor_utilities::DrawNodeLook(ImDrawList* drawList, ImVec2 min, ImVec2 max, const NodeLook& look)
{
    OutlinePath(drawList, min, max, look.m_Compact);
    drawList->PathFillConvex(IM_COL32(32, 33, 38, 255));
    if (look.m_Compact)
    {
        drawList->PathLineTo(ImVec2(min.x, min.y + c_CompactCut));
        drawList->PathLineTo(ImVec2(min.x + c_Stripe, min.y + c_CompactCut - c_Stripe));
        drawList->PathLineTo(ImVec2(min.x + c_Stripe, max.y - c_CompactCut + c_Stripe));
        drawList->PathLineTo(ImVec2(min.x, max.y - c_CompactCut));
        drawList->PathFillConvex(look.m_Stripe);
    }
    else
    {
        const auto bottom = look.m_BandBottom;
        auto top = [&]
        {
            drawList->PathLineTo(ImVec2(min.x, min.y + c_Cut));
            drawList->PathLineTo(ImVec2(min.x + c_Cut, min.y));
        };
        top();
        drawList->PathLineTo(ImVec2(max.x, min.y));
        drawList->PathLineTo(ImVec2(max.x, bottom));
        drawList->PathLineTo(ImVec2(min.x, bottom));
        drawList->PathFillConvex(IM_COL32(42, 44, 50, 255));

        // The slant meets the band's bottom where it meets its top, less the band's height.
        const auto end = BandEnd(look.m_TitleRight, bottom - min.y);
        const auto low = end - (bottom - min.y);
        top();
        if (end <= max.x)
        {
            drawList->PathLineTo(ImVec2(end, min.y));
            drawList->PathLineTo(ImVec2(low, bottom));
        }
        else if (low < max.x)
        {
            drawList->PathLineTo(ImVec2(max.x, min.y));
            drawList->PathLineTo(ImVec2(max.x, min.y + end - max.x));
            drawList->PathLineTo(ImVec2(low, bottom));
        }
        else
        {
            drawList->PathLineTo(ImVec2(max.x, min.y));
            drawList->PathLineTo(ImVec2(max.x, bottom));
        }
        drawList->PathLineTo(ImVec2(min.x, bottom));
        drawList->PathFillConvex(look.m_Band);
        drawList->AddLineH(min.x, max.x, bottom, IM_COL32(0, 0, 0, 90));
    }

    StrokeOutline(drawList, min, max, look);
}

void blueprint_editor_utilities::DrawCommentLook(ImDrawList* drawList, ImVec2 min, ImVec2 max, float headerBottom, const NodeLook& look)
{
    OutlinePath(drawList, min, max, false);
    drawList->PathFillConvex(IM_COL32(255, 255, 255, 18));
    drawList->PathLineTo(ImVec2(min.x, min.y + c_Cut));
    drawList->PathLineTo(ImVec2(min.x + c_Cut, min.y));
    drawList->PathLineTo(ImVec2(max.x, min.y));
    drawList->PathLineTo(ImVec2(max.x, headerBottom));
    drawList->PathLineTo(ImVec2(min.x, headerBottom));
    drawList->PathFillConvex(IM_COL32(255, 255, 255, 22));
    StrokeOutline(drawList, min, max, look);
}

float blueprint_editor_utilities::BandEnd(float titleRight, float bandHeight)
{
    return titleRight + c_BandPad + bandHeight;
}

ImVec2 blueprint_editor_utilities::CutCenter(bool compact)
{
    const auto cut = compact ? c_CompactCut : c_Cut;
    return ImVec2(cut, cut) * 0.5f;
}

ImVec4 blueprint_editor_utilities::NodePadding(const NodeLook& look)
{
    auto padding = ax::NodeEditor::GetStyle().NodePadding;
    if (look.m_Compact && look.m_Stripe != 0)
        padding.x += c_Stripe;
    return padding;
}

blueprint_editor_utilities::Value blueprint_editor_utilities::ConstantValue(const Node& node, const Pin& pin)
{
    auto type = pin.m_DecidedType;
    if (!type)
        return {};
    auto value = node.m_Config.m_Data.find("value");
    if (value != node.m_Config.m_Data.end() && value->second.GetType() == type)
        return value->second;
    return type->m_Head->Default(*type);
}

bool blueprint_editor_utilities::HasLinks(const Graph& graph, Id pin)
{
    for (auto& link : graph.Links())
        if (link.m_From == pin || link.m_To == pin)
            return true;
    return false;
}

bool blueprint_editor_utilities::HasValueEditor(const Graph& graph, const Pin& pin)
{
    if (pin.m_Flow || !pin.m_DecidedType)
        return false;
    if (pin.m_Direction == Direction::Output)
        return IsConstant(*graph.NodeOf(pin.m_Id));
    for (auto& link : graph.Links())
        if (link.m_Active && link.m_To == pin.m_Id)
            return false;
    return true;
}

const blueprint_editor_utilities::VariableDeclaration* blueprint_editor_utilities::PinVariable(const Graph& graph, const Pin& pin)
{
    if (pin.m_Flow || pin.m_Type.m_Head || pin.m_Type.m_Variable < 0)
        return nullptr;
    return &graph.NodeOf(pin.m_Id)->m_Variables[static_cast<size_t>(pin.m_Type.m_Variable)];
}

const blueprint_editor_utilities::GroupDeclaration* blueprint_editor_utilities::FindGroup(const Node& node, std::string_view key, int* index)
{
    for (auto& group : node.m_Groups)
    {
        if (auto at = GroupIndex(group, key); at >= 0)
        {
            if (index)
                *index = at;
            return &group;
        }
    }
    return nullptr;
}

std::string blueprint_editor_utilities::SplitOf(const Node& node, std::string_view key)
{
    std::string_view found;
    for (auto& split : node.m_Config.m_Split)
        if (key.size() > split.size() && key.starts_with(split) && key[split.size()] == '.' && split.size() > found.size())
            found = split;
    return std::string(found);
}

void blueprint_editor_utilities::SplitStructPin(Document& document, Id node, std::string key, bool split)
{
    auto& graph = document.GetGraph();
    auto  found = graph.FindNode(node);
    if (!found)
        return;

    const auto name = found->Name();
    auto transaction = document.BeginUndoTransaction(split ? "Split Pin" : "Recombine Pin");
    if (auto done = graph.SplitPin(node, key, split); !done)
    {
        auto message = done.GetError().Message();
        LOGW("%.*s", static_cast<int>(message.size()), message.data());
        transaction->Discard();
        return;
    }
    transaction->AddAction("%s: %s %s", name.c_str(), key.c_str(), split ? "split" : "recombined");
}

blueprint_editor_utilities::SignatureNode blueprint_editor_utilities::SignatureNodeOf(const Blueprint& blueprint, const Node& node)
{
    auto& name = node.m_Type->m_Name;
    if (!name.starts_with("entry:") && !name.starts_with("return:"))
        return {};
    return { blueprint.FunctionOf(*node.m_Type), name.starts_with("entry:") };
}

void blueprint_editor_utilities::ChangeSignature(Document& document, Id graph, std::string_view what, const std::function<void(Signature&)>& change)
{
    auto function = document.GetBlueprint().FindFunction(graph);
    if (!function)
        return;
    auto signature = function->m_Signature;
    change(signature);
    if (signature == function->m_Signature)
        return;
    const auto name = function->m_Name;
    auto transaction = document.BeginUndoTransaction(what);
    if (auto set = document.GetBlueprint().SetSignature(graph, std::move(signature)); !set)
    {
        auto message = set.GetError().Message();
        LOGW("[Signature] %s: %.*s", name.c_str(), static_cast<int>(message.size()), message.data());
        transaction->Discard();
        return;
    }
    transaction->AddAction("%s: %.*s", name.c_str(), static_cast<int>(what.size()), what.data());
}

void blueprint_editor_utilities::AddSignaturePin(Document& document, Id graph, bool input)
{
    ChangeSignature(document, graph, input ? "Add Input" : "Add Output", [input](Signature& signature)
    {
        auto taken = [&signature](const std::string& key)
        {
            auto has = [&key](const SignaturePin& pin) { return pin.m_Key == key; };
            return key == "enter" || key == "then" || std::any_of(signature.m_Inputs.begin(), signature.m_Inputs.end(), has) || std::any_of(signature.m_Outputs.begin(), signature.m_Outputs.end(), has);
        };
        const std::string base = input ? "in" : "out", name = input ? "In" : "Out";
        int number = 0;
        while (taken(number ? base + std::to_string(number) : base))
            ++number;
        auto& pins = input ? signature.m_Inputs : signature.m_Outputs;
        pins.push_back({ number ? base + std::to_string(number) : base, number ? name + " " + std::to_string(number) : name, Int32Type() });
    });
}

void blueprint_editor_utilities::RenameSignaturePin(Document& document, Id graph, bool input, std::string key, std::string name)
{
    ChangeSignature(document, graph, "Rename", [input, &key, &name](Signature& changed)
    {
        for (auto& changing : input ? changed.m_Inputs : changed.m_Outputs)
            if (changing.m_Key == key)
                changing.m_Name = name;
    });
}

void blueprint_editor_utilities::RetypeSignaturePin(Document& document, Id graph, bool input, std::string key, TypeTerm type)
{
    ChangeSignature(document, graph, "Change Type", [input, &key, &type](Signature& changed)
    {
        for (auto& changing : input ? changed.m_Inputs : changed.m_Outputs)
            if (changing.m_Key == key)
                changing.m_Type = type;
    });
}

blueprint_editor_utilities::PinGrowth blueprint_editor_utilities::GrowthOf(const Blueprint& blueprint, const Node& node)
{
    PinGrowth growth;
    if (!node.m_Groups.empty())
    {
        auto& group = node.m_Groups.front();
        growth.m_Direction = group.m_Direction;
        growth.m_Group     = group.m_Name;
        growth.m_Enabled   = GroupCount(group, node.m_Config) < group.m_Max;
    }
    else if (auto signature = SignatureNodeOf(blueprint, node); signature.m_Function)
    {
        growth.m_Direction = signature.m_Entry ? Direction::Output : Direction::Input;
        growth.m_Function  = signature.m_Function->m_Graph;
        growth.m_Enabled   = true;
    }
    return growth;
}

void blueprint_editor_utilities::GrowPins(Document& document, Id node, const PinGrowth& growth)
{
    if (!growth.m_Group.empty())
        AddGroupPin(document, node, growth.m_Group);
    else if (growth.m_Function)
        AddSignaturePin(document, growth.m_Function, growth.m_Direction == Direction::Output);
}

blueprint_editor_utilities::PinRemoval blueprint_editor_utilities::RemovalOf(const Blueprint& blueprint, const Node& node, const Pin& pin)
{
    PinRemoval removal;
    int index = -1;
    if (auto group = FindGroup(node, pin.m_Key, &index))
    {
        if (GroupCount(*group, node.m_Config) > group->m_Min)
        {
            removal.m_Group = group->m_Name;
            removal.m_Index = index;
        }
        return removal;
    }
    auto signature = SignatureNodeOf(blueprint, node);
    if (!signature.m_Function)
        return removal;
    auto& pins = signature.m_Entry ? signature.m_Function->m_Signature.m_Inputs : signature.m_Function->m_Signature.m_Outputs;
    if (std::any_of(pins.begin(), pins.end(), [&pin](const SignaturePin& other) { return other.m_Key == pin.m_Key; }))
    {
        removal.m_Function = signature.m_Function->m_Graph;
        removal.m_Input    = signature.m_Entry;
        removal.m_Key      = pin.m_Key;
    }
    return removal;
}

void blueprint_editor_utilities::RemovePin(Document& document, Id node, const PinRemoval& removal)
{
    if (removal.m_Index >= 0)
        RemoveGroupPin(document, node, removal.m_Group, removal.m_Index);
    else if (removal.m_Function)
        RemoveSignaturePin(document, removal.m_Function, removal.m_Input, removal.m_Key);
}

void blueprint_editor_utilities::RemoveSignaturePin(Document& document, Id graph, bool input, std::string key)
{
    ChangeSignature(document, graph, input ? "Remove Input" : "Remove Output", [input, &key](Signature& changed)
    {
        std::erase_if(input ? changed.m_Inputs : changed.m_Outputs, [&key](const SignaturePin& pin) { return pin.m_Key == key; });
    });
}

void blueprint_editor_utilities::AddTypeParameter(Document& document, Id graph)
{
    ChangeSignature(document, graph, "Add Type Parameter", [](Signature& changed)
    {
        for (auto letter : std::string("TUVWXYZABCDEFGHIJKLMNOPQRS"))
        {
            auto name = std::string(1, letter);
            if (std::none_of(changed.m_Parameters.begin(), changed.m_Parameters.end(), [&name](const VariableDeclaration& other) { return other.m_Name == name; }))
                return changed.m_Parameters.push_back({ name, {} });
        }
    });
}

void blueprint_editor_utilities::ToggleAllowedType(Document& document, Id graph, size_t index, const ValueType* head)
{
    auto function = document.GetBlueprint().FindFunction(graph);
    if (!function || index >= function->m_Signature.m_Parameters.size())
        return;
    auto& set = function->m_Signature.m_Parameters[index].m_Allowed;
    const auto allowed = std::find(set.begin(), set.end(), head) != set.end();
    ChangeSignature(document, graph, allowed ? "Disallow Type" : "Allow Type", [index, head, allowed](Signature& changed)
    {
        auto& changing = changed.m_Parameters[index].m_Allowed;
        if (allowed)
            std::erase(changing, head);
        else
            changing.push_back(head);
    });
}

std::vector<blueprint_editor_utilities::SignatureTypeChoice> blueprint_editor_utilities::SignatureTypeChoices(const Registry& registry, const Signature& signature)
{
    std::vector<SignatureTypeChoice> choices;
    for (auto array : { false, true })
    {
        auto wrap = [array](TypeTerm term) { return array ? TypeTerm(ArrayValueType(), { std::move(term) }) : term; };
        for (auto head : registry.ValueTypes())
            if (head->m_Arity == 0)
                choices.push_back({ head->m_Name, wrap(head->AsType()), array });
        for (size_t i = 0; i < signature.m_Parameters.size(); ++i)
            choices.push_back({ signature.m_Parameters[i].m_Name, wrap(TypeVariable{ static_cast<int>(i) }), array });
    }
    return choices;
}

namespace blueprint_editor_utilities { namespace {

bool HasVariable(const TypeTerm& term, int variable)
{
    if (!term.m_Head)
        return term.m_Variable == variable;
    return std::any_of(term.m_Args.begin(), term.m_Args.end(), [variable](const TypeTerm& arg) { return HasVariable(arg, variable); });
}

// The variables after one that went move down.
void Renumber(TypeTerm& term, int removed)
{
    if (!term.m_Head && term.m_Variable > removed)
        --term.m_Variable;
    for (auto& arg : term.m_Args)
        Renumber(arg, removed);
}

// Null while a variable of the term has no type.
const Type* TypeWith(TypeTable& table, const TypeTerm& term, std::span<const Type* const> variables)
{
    if (!term.m_Head)
        return term.m_Variable >= 0 && static_cast<size_t>(term.m_Variable) < variables.size() ? variables[static_cast<size_t>(term.m_Variable)] : nullptr;
    std::vector<const Type*> args;
    for (auto& arg : term.m_Args)
    {
        auto type = TypeWith(table, arg, variables);
        if (!type)
            return nullptr;
        args.push_back(type);
    }
    return table.Get(*term.m_Head, args);
}

} } // namespace blueprint_editor_utilities { namespace {

const crude_blueprint::SignaturePin* blueprint_editor_utilities::TypeParameterUser(const Signature& signature, int index)
{
    for (auto pins : { &signature.m_Inputs, &signature.m_Outputs })
        for (auto& pin : *pins)
            if (HasVariable(pin.m_Type, index))
                return &pin;
    return nullptr;
}

void blueprint_editor_utilities::RemoveTypeParameter(Document& document, Id graph, int index)
{
    auto function = document.GetBlueprint().FindFunction(graph);
    if (!function || index < 0 || static_cast<size_t>(index) >= function->m_Signature.m_Parameters.size())
        return;
    auto& signature = function->m_Signature;
    if (auto user = TypeParameterUser(signature, index))
    {
        LOGW("[Signature] %s: %s is %s's type", function->m_Name.c_str(), signature.m_Parameters[index].m_Name.c_str(), user->m_Key.c_str());
        return;
    }
    ChangeSignature(document, graph, "Remove Type Parameter", [index](Signature& changed)
    {
        changed.m_Parameters.erase(changed.m_Parameters.begin() + index);
        for (auto pins : { &changed.m_Inputs, &changed.m_Outputs })
            for (auto& pin : *pins)
                Renumber(pin.m_Type, index);
    });
}

void blueprint_editor_utilities::RenameFunction(Document& document, Id graph, std::string name)
{
    auto function = document.GetBlueprint().FindFunction(graph);
    if (!function || function->m_Name == name)
        return;
    const auto old = function->m_Name;
    auto transaction = document.BeginUndoTransaction("Rename Function");
    if (auto renamed = document.GetBlueprint().RenameFunction(graph, name); !renamed)
    {
        auto message = renamed.GetError().Message();
        LOGW("[Function] %s: %.*s", old.c_str(), static_cast<int>(message.size()), message.data());
        transaction->Discard();
        return;
    }
    transaction->AddAction("%s renamed %s", old.c_str(), name.c_str());
}

void blueprint_editor_utilities::RenameTypeParameter(Document& document, Id graph, size_t index, std::string name)
{
    auto function = document.GetBlueprint().FindFunction(graph);
    if (!function || index >= function->m_Signature.m_Parameters.size() || function->m_Signature.m_Parameters[index].m_Name == name)
        return;
    const auto owner = function->m_Name;
    const auto old   = function->m_Signature.m_Parameters[index].m_Name;
    auto transaction = document.BeginUndoTransaction("Rename Type Parameter");
    if (auto renamed = document.GetBlueprint().RenameTypeParameter(graph, index, name); !renamed)
    {
        auto message = renamed.GetError().Message();
        LOGW("[Signature] %s: %.*s", owner.c_str(), static_cast<int>(message.size()), message.data());
        transaction->Discard();
        return;
    }
    transaction->AddAction("%s.%s renamed %s", owner.c_str(), old.c_str(), name.c_str());
}

void blueprint_editor_utilities::RemoveFunction(Document& document, Id graph)
{
    auto& blueprint = document.GetBlueprint();
    auto  function  = blueprint.FindFunction(graph);
    if (!function)
        return;
    const auto name  = function->m_Name;
    const auto call  = blueprint.FindNodeType("call:" + name);
    auto transaction = document.BeginUndoTransaction("Delete Function");
    auto calls       = 0;
    for (auto& other : blueprint.Graphs())
    {
        if (other->GetId() == graph)
            continue;
        std::vector<Id> found;
        for (auto& node : other->Nodes())
            if (node.m_Type == call)
                found.push_back(node.m_Id);
        for (auto node : found)
            calls += other->RemoveNode(node).IsOk() ? 1 : 0;
    }
    if (auto removed = blueprint.RemoveFunction(graph); !removed)
    {
        auto message = removed.GetError().Message();
        LOGW("[Function] %s: %.*s", name.c_str(), static_cast<int>(message.size()), message.data());
        transaction->Discard();
        return;
    }
    transaction->AddAction("%s deleted, with %d call%s", name.c_str(), calls, calls != 1 ? "s" : "");
}

void blueprint_editor_utilities::RenameEvent(Document& document, Id node, std::string name)
{
    auto& graph = document.GetGraph();
    auto  found = graph.FindNode(node);
    if (!found || found->m_Type->EventName(found->m_Config) == name)
        return;
    auto config = found->m_Config;
    config.m_Data["event"] = Value::String(name);
    const auto old = found->m_Type->EventName(found->m_Config);
    auto transaction = document.BeginUndoTransaction("Rename Event");
    if (auto set = graph.SetConfig(node, std::move(config)); !set)
    {
        auto message = set.GetError().Message();
        LOGW("[Event] %s: %.*s", old.c_str(), static_cast<int>(message.size()), message.data());
        transaction->Discard();
        return;
    }
    transaction->AddAction("%s renamed %s", old.c_str(), name.c_str());
}

void blueprint_editor_utilities::SetCommentText(Document& document, Id node, std::string text)
{
    auto& graph = document.GetGraph();
    auto  found = graph.FindNode(node);
    if (!found || CommentText(*found) == text)
        return;
    auto config = found->m_Config;
    config.m_Data["text"] = Value::String(text);
    auto transaction = document.BeginUndoTransaction("Edit Comment");
    if (auto set = graph.SetConfig(node, std::move(config)); !set)
    {
        auto message = set.GetError().Message();
        LOGW("[Comment] %s: %.*s", found->Name().c_str(), static_cast<int>(message.size()), message.data());
        transaction->Discard();
        return;
    }
    transaction->AddAction("%s says \"%s\"", graph.FindNode(node)->Name().c_str(), text.c_str());
}

namespace blueprint_editor_utilities { namespace {

// How a link would be made again: a disabled one is kept so.
LinkMode ModeOf(const Link& link)
{
    return !link.m_Cast.empty() ? LinkMode::Cast : link.m_Forced > 0 ? LinkMode::Force : link.m_Active ? LinkMode::Plain : LinkMode::Keep;
}

// A link made again in its mode, or kept, disabled, when it no longer fits.
void Reconnect(Graph& graph, Id from, Id to, LinkMode mode)
{
    if (graph.Connect(from, to, mode))
        return;
    if (auto kept = graph.Connect(from, to, LinkMode::Keep); !kept)
    {
        auto message = kept.GetError().Message();
        auto pin     = graph.FindPin(from);
        LOGW("[Reroute] %s not linked: %.*s", pin ? DescribePin(graph, *pin).c_str() : "?", static_cast<int>(message.size()), message.data());
    }
}

} } // namespace blueprint_editor_utilities { namespace {

blueprint_editor_utilities::Id blueprint_editor_utilities::InsertReroute(Document& document, Id link, ImVec2 center)
{
    auto& graph = document.GetGraph();
    auto  found = graph.FindLink(link);
    auto  from  = found ? graph.FindPin(found->m_From) : nullptr;
    if (!from)
        return 0;
    const auto source = found->m_From, target = found->m_To;
    const auto mode   = ModeOf(*found);
    auto transaction = document.BeginUndoTransaction("Insert Reroute");
    auto reroute     = graph.AddNode(from->m_Flow ? "crude.FlowReroute" : "crude.Reroute");
    if (!reroute)
    {
        transaction->Discard();
        return 0;
    }
    auto node = graph.FindNode(reroute.GetValue());
    const auto in = node->FindPin("in")->m_Id, out = node->FindPin("out")->m_Id;
    if (!graph.Disconnect(link))
    {
        transaction->Discard();
        return 0;
    }
    Reconnect(graph, source, in, LinkMode::Plain);
    Reconnect(graph, out, target, mode);
    ax::NodeEditor::SetNodePosition(reroute.GetValue(), center - ImVec2(RerouteSize(), RerouteSize()) * 0.5f);
    transaction->AddAction("%s put into a link", graph.FindNode(reroute.GetValue())->Name().c_str());
    return reroute.GetValue();
}

void blueprint_editor_utilities::DissolveReroute(Graph& graph, Id reroute)
{
    auto node = graph.FindNode(reroute);
    auto in   = node && IsReroute(*node) ? node->FindPin("in") : nullptr;
    auto out  = node && IsReroute(*node) ? node->FindPin("out") : nullptr;
    if (!in || !out)
        return;
    struct End { Id m_Pin; LinkMode m_Mode; };
    std::vector<End> sources, targets;
    for (auto& link : graph.Links())
    {
        if (link.m_To == in->m_Id)
            sources.push_back({ link.m_From, ModeOf(link) });
        if (link.m_From == out->m_Id)
            targets.push_back({ link.m_To, ModeOf(link) });
    }
    if (!graph.RemoveNode(reroute))
        return;

    // A cast or a force on either side holds for the link joined, as does a link kept disabled.
    for (auto& source : sources)
        for (auto& target : targets)
        {
            auto either = [&](LinkMode mode) { return source.m_Mode == mode || target.m_Mode == mode; };
            const auto mode = either(LinkMode::Cast) ? LinkMode::Cast : either(LinkMode::Force) ? LinkMode::Force : either(LinkMode::Keep) ? LinkMode::Keep : LinkMode::Plain;
            Reconnect(graph, source.m_Pin, target.m_Pin, mode);
        }
}

void blueprint_editor_utilities::RemoveReroute(Document& document, Id reroute)
{
    auto node = document.GetGraph().FindNode(reroute);
    if (!node || !IsReroute(*node))
        return;
    const auto name = node->Name();
    auto transaction = document.BeginUndoTransaction("Remove Reroute");
    if (!document.GetGraph().RemoveNode(reroute))
    {
        transaction->Discard();
        return;
    }
    transaction->AddAction("%s removed with its links", name.c_str());
}

std::string blueprint_editor_utilities::SplitAgainKey(const Node& node, const OrphanPin& orphan)
{
    const auto dot = orphan.m_Key.rfind('.');
    if (dot == std::string::npos)
        return {};
    auto whole = node.FindPin(std::string_view(orphan.m_Key).substr(0, dot));
    return whole && SplittableStruct(*whole) ? whole->m_Key : std::string();
}

void blueprint_editor_utilities::MoveOrphanLinks(Document& document, Id orphan, Id pin)
{
    auto& graph  = document.GetGraph();
    auto  target = graph.FindPin(pin);
    if (!graph.NodeOfOrphan(orphan) || !target)
        return;

    struct Moved { Id m_Link; Id m_Other; LinkMode m_Mode; };
    std::vector<Moved> moved;
    for (auto& link : graph.Links())
        if (link.m_From == orphan || link.m_To == orphan)
            moved.push_back({ link.m_Id, link.m_From == orphan ? link.m_To : link.m_From, !link.m_Cast.empty() ? LinkMode::Cast : link.m_Forced > 0 ? LinkMode::Force : LinkMode::Plain });
    const auto output = target->m_Direction == Direction::Output;
    const auto from   = EndText(graph, orphan), onto = EndText(graph, pin);
    auto transaction = document.BeginUndoTransaction("Move Links");
    for (auto& link : moved)
        if (graph.Disconnect(link.m_Link))
            Reconnect(graph, output ? pin : link.m_Other, output ? link.m_Other : pin, link.m_Mode);
    transaction->AddAction("%s's links moved to %s", from.c_str(), onto.c_str());
}

void blueprint_editor_utilities::RemoveOrphans(Document& document, Id node)
{
    auto& graph    = document.GetGraph();
    auto  orphaned = [&graph, node](Id pin) { auto owner = graph.NodeOfOrphan(pin); return owner && (!node || owner->m_Id == node); };
    std::vector<Id> links;
    for (auto& link : graph.Links())
        if (orphaned(link.m_From) || orphaned(link.m_To))
            links.push_back(link.m_Id);
    if (links.empty())
        return;
    auto transaction = document.BeginUndoTransaction("Remove Orphaned Pins");
    for (auto link : links)
        if (auto broken = graph.Disconnect(link); !broken)
        {
            auto message = broken.GetError().Message();
            LOGW("%.*s", static_cast<int>(message.size()), message.data());
        }
    transaction->AddAction("%d link%s of pins gone broken", static_cast<int>(links.size()), links.size() != 1 ? "s" : "");
}

void blueprint_editor_utilities::RenamePin(Document& document, Id pin, std::string name)
{
    auto& graph = document.GetGraph();
    auto  found = graph.FindPin(pin);
    auto  node  = found ? graph.NodeOf(pin) : nullptr;
    if (!node || found->m_Name == name)
        return;
    auto config = node->m_Config;
    if (name.empty())
        config.m_Names.erase(found->m_Key);
    else
        config.m_Names[found->m_Key] = name;
    if (config == node->m_Config)
        return;
    const auto old = found->m_Name;
    auto transaction = document.BeginUndoTransaction("Rename Pin");
    if (auto set = graph.SetConfig(node->m_Id, std::move(config)); !set)
    {
        auto message = set.GetError().Message();
        LOGW("[Pin] %s: %.*s", old.c_str(), static_cast<int>(message.size()), message.data());
        transaction->Discard();
        return;
    }
    transaction->AddAction("%s renamed %s", old.c_str(), graph.FindPin(pin)->m_Name.c_str());
}

void blueprint_editor_utilities::AddVariable(Document& document)
{
    auto&       blueprint = document.GetBlueprint();
    std::string name;
    for (int i = 1; name.empty() || blueprint.FindVariable(name); ++i)
        name = "Variable" + std::to_string(i);
    auto transaction = document.BeginUndoTransaction("Add Variable");
    if (auto added = blueprint.AddVariable({ name, Value::Int32(0) }); !added)
    {
        auto message = added.GetError().Message();
        LOGW("[Variable] %.*s", static_cast<int>(message.size()), message.data());
        transaction->Discard();
        return;
    }
    transaction->AddAction("%s added", name.c_str());
}

void blueprint_editor_utilities::ChangeVariable(Document& document, std::string name, Variable changed, std::string_view what)
{
    auto transaction = document.BeginUndoTransaction(what);
    if (auto set = document.GetBlueprint().SetVariable(name, std::move(changed)); !set)
    {
        auto message = set.GetError().Message();
        LOGW("[Variable] %s: %.*s", name.c_str(), static_cast<int>(message.size()), message.data());
        transaction->Discard();
        return;
    }
    transaction->AddAction("%s: %.*s", name.c_str(), static_cast<int>(what.size()), what.data());
}

void blueprint_editor_utilities::RemoveVariable(Document& document, std::string name)
{
    auto& blueprint = document.GetBlueprint();
    auto  get       = blueprint.FindNodeType("get:" + name), set = blueprint.FindNodeType("set:" + name);
    auto  transaction = document.BeginUndoTransaction("Remove Variable");
    auto  removedNodes = 0;
    for (auto& graph : blueprint.Graphs())
    {
        std::vector<Id> uses;
        for (auto& node : graph->Nodes())
            if (node.m_Type == get || node.m_Type == set)
                uses.push_back(node.m_Id);
        for (auto node : uses)
            removedNodes += graph->RemoveNode(node).IsOk() ? 1 : 0;
    }
    if (auto removed = blueprint.RemoveVariable(name); !removed)
    {
        auto message = removed.GetError().Message();
        LOGW("[Variable] %s: %.*s", name.c_str(), static_cast<int>(message.size()), message.data());
        transaction->Discard();
        return;
    }
    transaction->AddAction("%s removed, with %d node%s", name.c_str(), removedNodes, removedNodes != 1 ? "s" : "");
}

const blueprint_editor_utilities::Graph* blueprint_editor_utilities::GraphOf(const Blueprint& blueprint, Id node)
{
    for (auto& graph : blueprint.Graphs())
        if (graph->FindNode(node))
            return graph.get();
    return nullptr;
}

const blueprint_editor_utilities::Graph* blueprint_editor_utilities::GraphOfPin(const Blueprint& blueprint, Id pin)
{
    for (auto& graph : blueprint.Graphs())
        if (graph->FindPin(pin))
            return graph.get();
    return nullptr;
}

const blueprint_editor_utilities::Node* blueprint_editor_utilities::FindNode(const Blueprint& blueprint, Id node)
{
    auto graph = GraphOf(blueprint, node);
    return graph ? graph->FindNode(node) : nullptr;
}

std::string blueprint_editor_utilities::DescribePin(const Graph& graph, const Pin& pin)
{
    return graph.NodeOf(pin.m_Id)->Name() + "." + pin.m_Key + " (" + (pin.m_Flow ? std::string("flow") : graph.TypeText(pin.m_Id)) + ")";
}

void blueprint_editor_utilities::CreateLink(Document& document, Id from, Id to, LinkMode mode)
{
    auto& graph = document.GetGraph();
    auto  start = graph.FindPin(from);
    auto  end   = graph.FindPin(to);
    if (!start || !end)
        return;

    auto name = DescribePin(graph, *start) + " -> " + DescribePin(graph, *end);
    if (mode == LinkMode::Cast)
        name += ", cast";
    else if (mode == LinkMode::Force)
        name += ", forced";
    auto transaction = document.BeginUndoTransaction("Create Link");
    if (auto made = graph.Connect(from, to, mode); !made)
    {
        auto message = made.GetError().Message();
        LOGW("%.*s", static_cast<int>(message.size()), message.data());
        transaction->Discard();
        return;
    }
    LOGV("[CreateLink] %s", name.c_str());
    transaction->AddAction("%s", name.c_str());
}

void blueprint_editor_utilities::AddGroupPin(Document& document, Id node, std::string group)
{
    auto& graph = document.GetGraph();
    auto  found = graph.FindNode(node);
    if (!found)
        return;

    const auto name = found->Name();
    auto transaction = document.BeginUndoTransaction("Add Pin");
    if (auto added = graph.AddGroupPin(node, group); !added)
    {
        auto message = added.GetError().Message();
        LOGW("%.*s", static_cast<int>(message.size()), message.data());
        transaction->Discard();
        return;
    }
    transaction->AddAction("%s: %s pin added", name.c_str(), group.c_str());
}

namespace blueprint_editor_utilities { namespace {

const GroupDeclaration* GroupNamed(const Node& node, std::string_view name)
{
    auto found = std::find_if(node.m_Groups.begin(), node.m_Groups.end(), [name](const GroupDeclaration& group) { return group.m_Name == name; });
    return found != node.m_Groups.end() ? &*found : nullptr;
}

// The key of the first pin the group's next index has.
std::string NextGroupKey(const Node& node, const GroupDeclaration& group)
{
    return group.m_Prefixes.front() + "." + std::to_string(GroupCount(group, node.m_Config));
}

} } // namespace blueprint_editor_utilities { namespace {

blueprint_editor_utilities::Result<void> blueprint_editor_utilities::CanLinkNewGroupPin(const Graph& graph, Id node, const std::string& group, Id pin)
{
    auto found       = graph.FindNode(node);
    auto declaration = found ? GroupNamed(*found, group) : nullptr;
    if (!declaration)
        return Error(BlueprintError::NotFound, "the node has no " + group + " pins to add");
    const auto key = NextGroupKey(*found, *declaration);
    auto trial = graph;
    if (auto added = trial.AddGroupPin(node, group); !added)
        return added.GetError();
    auto fresh = trial.FindNode(node)->FindPin(key);
    auto other = trial.FindPin(pin);
    if (!fresh || !other)
        return Error(BlueprintError::NoPin, "the pin to add has nothing to link to");
    return other->m_Direction == Direction::Output ? trial.CanConnect(pin, fresh->m_Id) : trial.CanConnect(fresh->m_Id, pin);
}

void blueprint_editor_utilities::AddGroupPinLinked(Document& document, Id node, std::string group, Id pin)
{
    auto& graph       = document.GetGraph();
    auto  found       = graph.FindNode(node);
    auto  other       = graph.FindPin(pin);
    auto  declaration = found ? GroupNamed(*found, group) : nullptr;
    if (!declaration || !other)
        return;

    const auto key    = NextGroupKey(*found, *declaration);
    const auto name   = found->Name();
    const auto linked = DescribePin(graph, *other);
    const auto output = other->m_Direction == Direction::Output;
    auto transaction = document.BeginUndoTransaction("Add Pin and Link");
    auto refused     = [&transaction](const Error& error)
    {
        auto message = error.Message();
        LOGW("%.*s", static_cast<int>(message.size()), message.data());
        transaction->Discard();
    };
    if (auto added = graph.AddGroupPin(node, group); !added)
        return refused(added.GetError());
    auto fresh = graph.FindNode(node)->FindPin(key);
    if (!fresh)
        return refused(Error(BlueprintError::NoPin, "the pin added isn't " + key));
    if (auto made = output ? graph.Connect(pin, fresh->m_Id) : graph.Connect(fresh->m_Id, pin); !made)
        return refused(made.GetError());
    transaction->AddAction("%s: %s added, linked to %s", name.c_str(), key.c_str(), linked.c_str());
}

void blueprint_editor_utilities::RemoveGroupPin(Document& document, Id node, std::string group, int index)
{
    auto& graph = document.GetGraph();
    auto  found = graph.FindNode(node);
    if (!found)
        return;

    const auto name = found->Name();
    auto transaction = document.BeginUndoTransaction("Remove Pin");
    if (auto removed = graph.RemoveGroupPin(node, group, index); !removed)
    {
        auto message = removed.GetError().Message();
        LOGW("%.*s", static_cast<int>(message.size()), message.data());
        transaction->Discard();
        return;
    }
    transaction->AddAction("%s: %s.%d removed", name.c_str(), group.c_str(), index);
}

ImU32 blueprint_editor_utilities::ProblemColor(Severity severity)
{
    switch (severity)
    {
        case Severity::Error:   return IM_COL32(255,  88,  88, 255);
        case Severity::Warning: return IM_COL32(255, 204,  77, 255);
        case Severity::Info:    break;
    }
    return IM_COL32(170, 170, 170, 255);
}

void blueprint_editor_utilities::ProblemSign(ImDrawList* drawList, ImVec2 center, float radius, Severity severity)
{
    if (severity == Severity::Error)
        drawList->AddCircleFilled(center, radius, ProblemColor(severity));
    else
        drawList->AddTriangleFilled(center + ImVec2(0.0f, -radius), center + ImVec2(radius, radius * 0.8f), center + ImVec2(-radius, radius * 0.8f), ProblemColor(severity));
}

bool blueprint_editor_utilities::ProblemPills(int errors, int warnings)
{
    auto drawList = ImGui::GetWindowDrawList();
    auto size     = ImGui::GetTextLineHeight();
    auto sign = [drawList, size](int count, Severity severity)
    {
        auto at = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(size, size));
        ProblemSign(drawList, at + ImVec2(size, size) * 0.5f, size * 0.32f, severity);
        ImGui::SameLine(0.0f, 2.0f);
        ImGui::Text("%d", count);
    };

    ImGui::BeginGroup();
    if (errors > 0)
        sign(errors, Severity::Error);
    if (errors > 0 && warnings > 0)
        ImGui::SameLine();
    if (warnings > 0)
        sign(warnings, Severity::Warning);
    ImGui::EndGroup();

    const auto pillsMin  = ImGui::GetItemRectMin();
    const auto pillsSize = ImGui::GetItemRectSize();
    ImGui::SetCursorScreenPos(pillsMin);
    return ImGui::InvisibleButton("##problems", pillsSize);
}

bool blueprint_editor_utilities::LatentMark(std::string_view waiting)
{
    auto drawList = ImGui::GetWindowDrawList();
    auto size     = ImGui::GetTextLineHeight();
    auto center   = ImGui::GetCursorScreenPos() + ImVec2(size, size) * 0.5f;
    auto radius   = size * 0.4f;
    auto face     = ImGui::GetColorU32(waiting.empty() ? ImGuiCol_Text : ImGuiCol_NavCursor);
    auto hands    = waiting.empty() ? face : ImGui::GetColorU32(ImGuiCol_WindowBg);

    ImGui::BeginGroup();
    ImGui::Dummy(ImVec2(size, size));
    if (waiting.empty())
        drawList->AddCircle(center, radius, face, 0, 1.5f);
    else
        drawList->AddCircleFilled(center, radius, face);
    drawList->AddLine(center, center + ImVec2(0.0f, -radius * 0.65f), hands, 1.5f);
    drawList->AddLine(center, center + ImVec2(radius * 0.5f, 0.0f), hands, 1.5f);
    if (!waiting.empty())
    {
        ImGui::SameLine(0.0f, 4.0f);
        ImGui::TextUnformatted(waiting.data(), waiting.data() + waiting.size());
    }
    ImGui::EndGroup();
    return ImGui::IsItemHovered();
}

bool blueprint_editor_utilities::SquareButton(const char* label)
{
    const auto size = ImGui::GetTextLineHeight();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
    const auto pressed = ImGui::Button(label, ImVec2(size, size));
    ImGui::PopStyleVar();
    return pressed;
}

void blueprint_editor_utilities::DrawSign(ImDrawList* drawList, ImVec2 min, ImVec2 max, bool plus, bool enabled, bool hot, float squareAlpha, float signAlpha)
{
    const auto rect  = ImRect(min, max);
    auto       faded = [](ImU32 color, float alpha) { return (color & ~IM_COL32_A_MASK) | (static_cast<ImU32>(((color & IM_COL32_A_MASK) >> IM_COL32_A_SHIFT) * alpha + 0.5f) << IM_COL32_A_SHIFT); };
    const auto fill    = !enabled ? IM_COL32(37, 38, 43, 255) : hot ? IM_COL32(62, 64, 71, 255) : IM_COL32(40, 41, 47, 255);
    const auto outline = IM_COL32(255, 255, 255, 60);
    const auto ink     = enabled ? IM_COL32(228, 230, 235, 255) : IM_COL32(100, 102, 107, 255);
    const auto center  = rect.GetCenter();
    const auto arm     = rect.GetHeight() * 0.3f;
    drawList->AddRectFilled(rect.Min, rect.Max, faded(fill, squareAlpha), 3.0f);
    drawList->AddRect(rect.Min, rect.Max, faded(outline, squareAlpha), 3.0f);
    drawList->AddLine(ImVec2(center.x - arm, center.y), ImVec2(center.x + arm, center.y), faded(ink, signAlpha), 1.5f);
    if (plus)
        drawList->AddLine(ImVec2(center.x, center.y - arm), ImVec2(center.x, center.y + arm), faded(ink, signAlpha), 1.5f);
}

bool blueprint_editor_utilities::SignButton(const char* id, ImVec2 min, ImVec2 max, bool plus, bool lit, float faint, bool* hovered)
{
    // EndGroup() takes in the last item's rectangle (Dear ImGui #7543).
    const auto last   = GImGui->LastItemData;
    const auto rect   = ImRect(min, max);
    const auto itemId = ImGui::GetID(id);
    bool hover = false, held = false, pressed = false;
    if (ImGui::ItemAdd(rect, itemId))
    {
        pressed = ImGui::ButtonBehavior(rect, itemId, &hover, &held);
        DrawSign(ImGui::GetWindowDrawList(), min, max, plus, true, hover || held, 1.0f, lit || hover ? 1.0f : faint);
    }
    GImGui->LastItemData = last;
    if (hovered)
        *hovered = hover;
    return pressed;
}

std::string blueprint_editor_utilities::ValueText(const Value& value)
{
    auto type = value.GetType();
    if (!type)
        return {};
    if (type == &BoolType())
        return value.AsBool() ? "true" : "false";
    if (type == &Int32Type())
        return std::to_string(value.AsInt32());
    if (type == &FloatType())
    {
        char text[32];
        std::snprintf(text, sizeof(text), "%g", value.AsFloat());
        return text;
    }
    return type == &StringType() ? std::string(value.AsString()) : type->m_Head->Format(value);
}

bool blueprint_editor_utilities::DrawValue(const Value& value)
{
    if (!value.GetType())
        return false;
    auto text = ValueText(value);
    ImGui::TextUnformatted(text.data(), text.data() + text.size());
    return true;
}

// Files can't hold NaN or an infinity, so a Float editor takes only finite numbers.
blueprint_editor_utilities::ValueEdit blueprint_editor_utilities::EditValue(Value& value, bool focus)
{
    ImEx::ScopedItemWidth scopedItemWidth{120};

    // A number's field, then its - and +, laid out as ImGui::InputScalar() lays out its own, which
    // applies what was typed in it when it stops being active.
    auto number = [focus](ImGuiDataType type, void* data, const char* format) -> ValueEdit
    {
        const auto spacing = ImGui::GetStyle().ItemInnerSpacing.x;
        const auto button  = ImGui::GetFrameHeight();
        ImGui::SetNextItemWidth(ImMax(1.0f, ImGui::CalcItemWidth() - (button + spacing) * 2.0f));
        if (focus)
            ImGui::SetKeyboardFocusHere();
        ImGui::InputScalar("##editor", type, data, nullptr, nullptr, format);
        const auto stepsMin = ImVec2(ImGui::GetItemRectMax().x, ImGui::GetItemRectMin().y);
        const auto stepsMax = ImGui::GetItemRectMax() + ImVec2((button + spacing) * 2.0f, 0.0f);
        const auto typed    = ImGui::IsItemDeactivated();
        const auto ended    = typed && !(ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsMouseHoveringRect(stepsMin, stepsMax));
        auto step = 0;
        ImGui::SameLine(0.0f, spacing);
        if (ImGui::Button("-", ImVec2(button, button)))
            step = -1;
        ImGui::SameLine(0.0f, spacing);
        if (ImGui::Button("+", ImVec2(button, button)))
            step = 1;
        if (ended)
            return ValueEdit::Done;
        if (step == 0)
            return typed ? ValueEdit::Changed : ValueEdit::None;
        if (type == ImGuiDataType_S32)
        {
            auto& integer = *static_cast<int32_t*>(data);
            if ((step < 0 && integer == INT32_MIN) || (step > 0 && integer == INT32_MAX))
                return ValueEdit::None;
            integer += step;
        }
        else
            *static_cast<float*>(data) += static_cast<float>(step);
        return ValueEdit::Changed;
    };

    auto type = value.GetType();
    if (type == &BoolType())
    {
        value = Value::Bool(!value.AsBool());
        return ValueEdit::Done;
    }
    if (type == &Int32Type())
    {
        auto integer = value.AsInt32();
        auto edit    = number(ImGuiDataType_S32, &integer, "%d");
        if (edit != ValueEdit::None)
            value = Value::Int32(integer);
        return edit;
    }
    if (type == &FloatType())
    {
        auto real = value.AsFloat();
        auto edit = number(ImGuiDataType_Float, &real, "%.3f");
        if (edit == ValueEdit::None || !std::isfinite(real))
            return ValueEdit::None;
        value = Value::Float(real);
        return edit;
    }
    if (type == &StringType())
    {
        auto text = std::string(value.AsString());
        auto resize = [](ImGuiInputTextCallbackData* data) -> int
        {
            if (data->EventFlag == ImGuiInputTextFlags_CallbackResize)
            {
                auto& buffer = *static_cast<std::string*>(data->UserData);
                IM_ASSERT(buffer.data() == data->Buf);
                buffer.resize(static_cast<size_t>(data->BufSize)); // NB: On resizing calls, generally data->BufSize == data->BufTextLen + 1
                data->Buf = buffer.data();
            }
            return 0;
        };
        if (focus)
            ImGui::SetKeyboardFocusHere();
        ImGui::InputText("##editor", text.data(), text.size() + 1, ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_CallbackResize, resize, &text);
        if (!ImGui::IsItemDeactivated())
            return ValueEdit::None;
        text.resize(strlen(text.c_str()));
        value = Value::String(std::move(text));
        return ValueEdit::Done;
    }
    if (auto structType = type ? Cast<StructType>(type->m_Head) : nullptr)
    {
        // A field on each line, after its name, in its type's editor, but for a Bool's checkbox and
        // the text of a value with no editor. The struct's edit ends where a field's does and none
        // of its fields is active then, so going on to another field, or a number's - or +, keeps
        // it going.
        const auto declared = structType->Fields();
        auto       fields   = std::vector<Value>(value.AsStruct().begin(), value.AsStruct().end());
        auto       names    = 0.0f;
        for (auto& field : declared)
            names = ImMax(names, ImGui::CalcTextSize(field.m_Name.c_str()).x);
        auto changed = false, ended = false;
        ImGui::BeginGroup();
        for (size_t i = 0; i < fields.size() && i < declared.size(); ++i)
        {
            ImGui::PushID(static_cast<int>(i));
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(declared[i].m_Name.c_str());
            ImGui::SameLine(names + ImGui::GetStyle().ItemSpacing.x);
            const auto before = fields[i];
            const auto head   = fields[i].GetType() ? fields[i].GetType()->m_Head : nullptr;
            if (head == &BoolValueType())
            {
                auto checked = fields[i].AsBool();
                if (ImGui::Checkbox("##field", &checked))
                    fields[i] = Value::Bool(checked);
            }
            else if (head == &Int32ValueType() || head == &FloatValueType() || head == &StringValueType() || IsA<StructType>(head))
                ended = EditValue(fields[i], focus && i == 0) == ValueEdit::Done || ended;
            else
            {
                ImGui::BeginDisabled();
                DrawValue(fields[i]);
                ImGui::EndDisabled();
            }
            changed = changed || !(fields[i] == before);
            ImGui::PopID();
        }
        ImGui::EndGroup();
        const auto done = ended && !ImGui::IsItemActive();
        if (changed)
            value = Value::Struct(*type, std::move(fields));
        return done ? ValueEdit::Done : changed ? ValueEdit::Changed : ValueEdit::None;
    }

    // Values of other types have no editor yet.
    return ValueEdit::Done;
}

bool blueprint_editor_utilities::DrawValueWithEditor(Id id, Value& value)
{
    auto storage  = ImGui::GetStateStorage();
    auto focusKey = ImGui::GetID("PinValueEditor_Focus");
    auto activeId = static_cast<Id>(storage->GetInt(ImGui::GetID("PinValueEditor_ActivePinId"), 0));

    bool changed = false;
    if (activeId == id)
    {
        const auto before = value;
        ImGui::BeginGroup();
        const auto edit = EditValue(value, storage->GetBool(focusKey));
        ImGui::EndGroup();
        storage->SetBool(focusKey, false);
        const auto clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right);
        if (edit == ValueEdit::Done || (clicked && !ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)))
            activeId = 0;
        changed = edit != ValueEdit::None && !(value == before);
    }
    else
    {
        // The value's box is half as wide as a line is high at least, the value centered in it, so
        // that an empty or a short value takes a click as well; a click on the box opens the editor.
        PinValueBackgroundRenderer bg;
        if (!value.GetType())
        {
            bg.Discard();
            return false;
        }
        const auto text   = ValueText(value);
        const auto width  = ImGui::CalcTextSize(text.data(), text.data() + text.size()).x;
        const auto boxMin = ImGui::GetCursorScreenPos();
        const auto box    = ImVec2(ImMax(width, ImGui::GetTextLineHeight() * 0.5f), ImGui::GetTextLineHeight());
        ImGui::SetCursorScreenPos(boxMin + ImVec2(ImFloor((box.x - width) * 0.5f), 0.0f));
        ImGui::TextUnformatted(text.data(), text.data() + text.size());
        ImGui::SetCursorScreenPos(boxMin);
        if (ImGui::InvisibleButton("###pin_value_editor", box))
        {
            activeId = id;
            storage->SetBool(focusKey, true);
        }
    }

    storage->SetInt(ImGui::GetID("PinValueEditor_ActivePinId"), static_cast<int>(activeId));

    return changed;
}



bool blueprint_editor_utilities::CanvasActions::Press(const char* name, ImGuiKeyChord chord, bool active, bool global)
{
    constexpr std::string_view mouseLeft = "MouseLeft";
    auto gesture = std::string(ImGui::GetKeyChordName(chord));
    if (auto mouse = gesture.find(mouseLeft); mouse != std::string::npos)
        gesture.replace(mouse, mouseLeft.size(), "Click");
    m_Entries.push_back({ std::string(name) + ": " + gesture, active });
    if (!active || m_Fired || !(m_OverCanvas || global))
        return false;
    m_Fired = ImGui::Shortcut(chord, ImGuiInputFlags_RouteGlobal);
    return m_Fired;
}

void blueprint_editor_utilities::CanvasActions::Gesture(const char* name, const char* gesture, bool active)
{
    m_Entries.push_back({ std::string(name) + ": " + gesture, active });
}

void blueprint_editor_utilities::CanvasActions::Draw(ImVec2 canvasMin, ImVec2 canvasMax, bool inactive) const
{
    ImGui::PushFont(nullptr, ImGui::GetFontSize() * 0.75f);
    auto&      style = ImGui::GetStyle();
    const auto line  = ImGui::GetTextLineHeightWithSpacing();

    // In columns as tall as the canvas holds, the last one on the right, each entry right-aligned
    // in its column, and the whole list in the canvas's bottom-right corner.
    std::vector<const Entry*> shown;
    for (auto& entry : m_Entries)
        if (inactive || entry.m_Active)
            shown.push_back(&entry);
    const auto rows    = static_cast<size_t>(ImMax(1.0f, (canvasMax.y - canvasMin.y - style.ItemSpacing.y) / line));
    const auto columns = (shown.size() + rows - 1) / rows;
    std::vector<float> rights(columns, 0.0f); // each column's width, then its right edge
    for (size_t i = 0; i < shown.size(); ++i)
        rights[i / rows] = ImMax(rights[i / rows], ImGui::CalcTextSize(shown[i]->m_Text.c_str()).x);
    auto right = canvasMax.x - style.ItemSpacing.x;
    for (size_t column = columns; column-- > 0;)
        right -= std::exchange(rights[column], right) + style.ItemSpacing.x * 2.0f;
    const auto top = ImMax(canvasMin.y, canvasMax.y - static_cast<float>(ImMin(rows, shown.size())) * line - style.ItemSpacing.y);

    ImGui::PushClipRect(canvasMin, canvasMax, true);
    for (size_t i = 0; i < shown.size(); ++i)
    {
        auto width = ImGui::CalcTextSize(shown[i]->m_Text.c_str()).x;
        ImGui::SetCursorScreenPos(ImVec2(rights[i / rows] - width, top + static_cast<float>(i % rows) * line));
        ImGui::BeginDisabled(!shown[i]->m_Active);
        ImGui::TextUnformatted(shown[i]->m_Text.c_str());
        ImGui::EndDisabled();
    }
    ImGui::PopClipRect();
    ImGui::PopFont();
}



void blueprint_editor_utilities::ClosePopupsOnClickOutside()
{
    auto& g = *ImGui::GetCurrentContext();
    if (g.OpenPopupStack.empty() || ImGui::GetTopMostPopupModal())
        return;
    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        return;
    auto hovered = g.HoveredWindow;
    if (!hovered || (hovered->RootWindow->Flags & ImGuiWindowFlags_Popup))
        return;
    ImGui::FocusWindow(hovered);
}

std::vector<blueprint_editor_utilities::Id> blueprint_editor_utilities::GetSelectedNodes()
{
    std::vector<ax::NodeEditor::NodeId> nodeIds(static_cast<size_t>(ax::NodeEditor::GetSelectedNodes(nullptr, 0)));
    nodeIds.resize(static_cast<size_t>(ax::NodeEditor::GetSelectedNodes(nodeIds.data(), static_cast<int>(nodeIds.size()))));

    std::vector<Id> result;
    for (auto nodeId : nodeIds)
        result.push_back(static_cast<Id>(nodeId.Get()));
    return result;
}

std::vector<blueprint_editor_utilities::Id> blueprint_editor_utilities::GetSelectedLinks()
{
    std::vector<ax::NodeEditor::LinkId> linkIds(static_cast<size_t>(ax::NodeEditor::GetSelectedLinks(nullptr, 0)));
    linkIds.resize(static_cast<size_t>(ax::NodeEditor::GetSelectedLinks(linkIds.data(), static_cast<int>(linkIds.size()))));

    std::vector<Id> result;
    for (auto linkId : linkIds)
        result.push_back(static_cast<Id>(linkId.Get()));
    return result;
}

blueprint_editor_utilities::PinValueBackgroundRenderer::PinValueBackgroundRenderer(const ImVec4 color, float alpha /*= 0.25f*/)
{
    m_DrawList = ImGui::GetWindowDrawList();
    m_Splitter.Split(m_DrawList, 2);
    m_Splitter.SetCurrentChannel(m_DrawList, 1);
    m_Color = color;
    m_Alpha = alpha;
}

blueprint_editor_utilities::PinValueBackgroundRenderer::PinValueBackgroundRenderer()
    : PinValueBackgroundRenderer(ImGui::GetStyleColorVec4(ImGuiCol_CheckMark))
{
}

blueprint_editor_utilities::PinValueBackgroundRenderer::~PinValueBackgroundRenderer()
{
    Commit();
}

void blueprint_editor_utilities::PinValueBackgroundRenderer::Commit()
{
    if (m_Splitter._Current == 0)
        return;

    m_Splitter.SetCurrentChannel(m_DrawList, 0);

    auto itemMin = ImGui::GetItemRectMin();
    auto itemMax = ImGui::GetItemRectMax();
    auto margin = ImGui::GetStyle().ItemSpacing * 0.25f;
    margin.x = ImCeil(margin.x);
    margin.y = ImCeil(margin.y);
    itemMin -= margin;
    itemMax += margin;

    auto color = m_Color;
    color.w *= m_Alpha;
    m_DrawList->AddRectFilled(itemMin, itemMax,
        ImGui::GetColorU32(color), 4.0f);

    m_DrawList->AddRect(itemMin, itemMax,
        ImGui::GetColorU32(ImGuiCol_Text, m_Alpha), 4.0f);

    m_Splitter.Merge(m_DrawList);
}

void blueprint_editor_utilities::PinValueBackgroundRenderer::Discard()
{
    if (m_Splitter._Current == 1)
        m_Splitter.Merge(m_DrawList);
}



void blueprint_editor_utilities::Debugger::Monitor::OnLog(const Context& context, Id node, std::string_view text)
{
    auto index = context.GetProgram().FindNode(node);
    auto name  = index >= 0 ? context.GetProgram().Nodes()[static_cast<size_t>(index)].m_Name : std::string("?");
    LOGI("[Print] %s: \"%.*s\"", name.c_str(), static_cast<int>(text.size()), text.data());
}

bool blueprint_editor_utilities::Debugger::Start(const Document& document, bool running, std::string_view event, std::span<const Value> arguments)
{
    return Begin(document, running, event, false, arguments);
}

bool blueprint_editor_utilities::Debugger::Call(const Document& document, bool running, std::string_view function, std::span<const Value> arguments)
{
    return Begin(document, running, function, true, arguments);
}

bool blueprint_editor_utilities::Debugger::Begin(const Document& document, bool running, std::string_view name, bool call, std::span<const Value> arguments)
{
    m_Context.reset();
    m_Running  = false;
    m_Reported = false;

    auto compiled = Compile(document.GetBlueprint());
    for (auto& diagnostic : compiled.m_Diagnostics)
    {
        auto message = diagnostic.m_Error.Message();
        if (diagnostic.m_Severity == Severity::Error)
            LOGE("%.*s", static_cast<int>(message.size()), message.data());
        else
            LOGW("%.*s", static_cast<int>(message.size()), message.data());
    }
    if (!compiled.m_Program)
    {
        LOGE("Execution: the blueprint has errors, and doesn't run");
        return false;
    }

    m_Context = std::make_unique<Context>(compiled.m_Program);
    m_Context->SetMonitor(&m_Monitor);
    for (auto node : m_Breakpoints)
        m_Context->SetBreakpoint(node, true);
    auto& program = *compiled.m_Program;
    auto  node    = call ? -1 : program.FindEvent(name);
    for (auto& function : program.Functions())
    {
        if (!call || function.m_Name != name)
            continue;
        if (!function.m_Parameters.empty() && arguments.empty())
        {
            LOGE("Execution: %s is generic, and runs only with arguments, which give it types", function.m_Name.c_str());
            m_Context.reset();
            return false;
        }
        node = function.m_Entry;
    }
    std::vector<Value> defaults;
    if (node >= 0 && arguments.empty())
        for (auto& pin : program.Nodes()[static_cast<size_t>(node)].m_Pins)
            if (!pin.m_Flow && pin.m_Direction == Direction::Output)
                defaults.push_back(pin.m_Type->m_Head->Default(*pin.m_Type));
    if (arguments.empty())
        arguments = defaults;
    auto fiber = call ? m_Context->Call(name, arguments) : m_Context->Start(name, arguments);
    if (!fiber)
    {
        auto message = fiber.GetError().Message();
        LOGE("Execution: %.*s", static_cast<int>(message.size()), message.data());
        m_Context.reset();
        return false;
    }

    // "Run" for the Start event, "Run Spawn("Bob", 3)" for another, "Call Echo(0)" for a function.
    auto what = !call && name == "Start" ? std::string() : " " + std::string(name);
    for (size_t i = 0; i < arguments.size(); ++i)
    {
        auto text = ValueText(arguments[i]);
        what += (i ? ", " : "(") + (arguments[i].GetType() == &StringType() ? "\"" + text + "\"" : text) + (i + 1 < arguments.size() ? "" : ")");
    }
    LOGI("Execution: %s%s", call ? "Call" : running ? "Run" : "Start", what.c_str());
    m_Fiber    = fiber.GetValue();
    m_Revision = document.m_Revision;
    m_Running  = running;
    return true;
}

void blueprint_editor_utilities::Debugger::Continue()
{
    if (!IsActive())
        return;
    LOGI("Execution: Continue");
    m_Running = true;
}

void blueprint_editor_utilities::Debugger::Step()
{
    if (!IsActive())
        return;
    if (auto waiting = Waiting())
    {
        if (waiting->m_Kind == Wake::Kind::Signal)
        {
            LOGI("Execution: waits for %s", WakeText(*waiting, 0.0).c_str());
            return;
        }
        m_Context->AdvanceTo(waiting->m_Time);
    }
    if (m_Context->Step(m_Fiber))
        Report();
}

void blueprint_editor_utilities::Debugger::Stop()
{
    if (IsActive())
        LOGI("Execution: Stop");
    m_Context.reset();
    m_Running = false;
}

void blueprint_editor_utilities::Debugger::Update(const Document& document)
{
    if (m_Context && document.m_Revision != m_Revision)
    {
        m_Revision = document.m_Revision;
        auto compiled = IsActive() ? Compile(document.GetBlueprint()) : CompileResult();
        if (!compiled.m_Program || !m_Context->Patch(compiled.m_Program))
        {
            if (IsActive())
                LOGI("Execution: an edit stopped it");
            m_Context.reset();
            m_Running = false;
            return;
        }
    }
    if (!IsRunning())
        return;

    if (m_Context->Run(std::chrono::milliseconds(5)) == RunState::Breakpoint)
    {
        m_Running = false;
        auto index = m_Context->GetProgram().FindNode(NextNode());
        auto name  = index >= 0 ? m_Context->GetProgram().Nodes()[static_cast<size_t>(index)].m_Name : std::string("?");
        LOGI("Execution: Paused at %s's breakpoint", name.c_str());
    }
    Report();
}

void blueprint_editor_utilities::Debugger::Advance(double seconds)
{
    if (IsRunning())
        m_Context->Advance(seconds);
}

void blueprint_editor_utilities::Debugger::Signal(std::string_view signal)
{
    if (!IsActive())
        return;
    LOGI("Execution: %s sent", WakeText(Wake::OnSignal(std::string(signal)), 0.0).c_str());
    m_Context->Signal(signal);
}

void blueprint_editor_utilities::Debugger::SetBreakpoint(Id node, bool enabled)
{
    if (enabled)
        m_Breakpoints.insert(node);
    else
        m_Breakpoints.erase(node);
    if (m_Context)
        m_Context->SetBreakpoint(node, enabled);
}

void blueprint_editor_utilities::Debugger::SetWatched(Id pin, bool watched)
{
    if (watched == IsWatched(pin))
        return;
    if (watched)
        m_Watched.push_back(pin);
    else
        std::erase(m_Watched, pin);
}

bool blueprint_editor_utilities::Debugger::IsWatched(Id pin) const
{
    return std::find(m_Watched.begin(), m_Watched.end(), pin) != m_Watched.end();
}

void blueprint_editor_utilities::Debugger::ClearBreakpoints()
{
    if (m_Context)
        for (auto node : m_Breakpoints)
            m_Context->SetBreakpoint(node, false);
    m_Breakpoints.clear();
}

bool blueprint_editor_utilities::Debugger::IsActive() const
{
    auto fiber = Fiber();
    return fiber && (fiber->m_State == FiberState::Ready || fiber->m_State == FiberState::Suspended);
}

std::optional<blueprint_editor_utilities::Wake> blueprint_editor_utilities::Debugger::Waiting() const
{
    auto fiber = Fiber();
    if (!fiber || fiber->m_State != FiberState::Suspended)
        return std::nullopt;
    return fiber->m_Wake;
}

bool blueprint_editor_utilities::Debugger::IsAtBreakpoint() const
{
    auto fiber = Fiber();
    return fiber && fiber->m_AtBreakpoint;
}

blueprint_editor_utilities::Id blueprint_editor_utilities::Debugger::CurrentNode() const
{
    auto fiber = Fiber();
    return fiber ? fiber->m_Ran : 0;
}

blueprint_editor_utilities::Id blueprint_editor_utilities::Debugger::NextNode() const
{
    auto fiber = Fiber();
    return fiber ? fiber->m_Node : 0;
}

blueprint_editor_utilities::Id blueprint_editor_utilities::Debugger::TakenLink() const
{
    auto fiber = Fiber();
    return fiber ? fiber->m_Link : 0;
}

blueprint_editor_utilities::Result<blueprint_editor_utilities::Watch> blueprint_editor_utilities::Debugger::Evaluate(const Document& document, Id pin)
{
    if (m_Context)
        return m_Context->Evaluate(pin);
    if (m_PreviewRevision != document.m_Revision)
    {
        m_PreviewRevision = document.m_Revision;
        auto compiled = Compile(document.GetBlueprint());
        m_Preview = compiled.m_Program ? std::make_unique<Context>(compiled.m_Program) : nullptr;
    }
    if (!m_Preview)
        return Error(BlueprintError::Unsupported, "the blueprint has errors");
    return m_Preview->Evaluate(pin);
}

std::optional<blueprint_editor_utilities::FiberInfo> blueprint_editor_utilities::Debugger::Fiber() const
{
    if (m_Context)
        for (auto& fiber : m_Context->Fibers())
            if (fiber.m_Id == m_Fiber)
                return fiber;
    return std::nullopt;
}

void blueprint_editor_utilities::Debugger::Report()
{
    auto fiber = Fiber();
    if (!fiber || fiber->m_State == FiberState::Ready || fiber->m_State == FiberState::Suspended || m_Reported)
        return;
    m_Reported = true;
    if (fiber->m_State == FiberState::Done)
    {
        std::string results;
        for (auto& result : fiber->m_Results)
            results += (results.empty() ? ", giving " : ", ") + result.GetType()->m_Head->Format(result);
        LOGI("Execution: Done (%llu steps)%s", static_cast<unsigned long long>(m_Context->StepCount()), results.c_str());
    }
    else if (fiber->m_State == FiberState::Failed)
    {
        auto diagnostics = m_Context->Diagnostics();
        auto message = diagnostics.empty() ? std::string_view() : diagnostics.back().m_Error.Message();
        LOGE("Execution: Failed at step %llu: %.*s", static_cast<unsigned long long>(m_Context->StepCount()), static_cast<int>(message.size()), message.data());
    }
}

void blueprint_editor_utilities::ProblemList::Update(const Document& document, const Debugger& debugger)
{
    auto owner = [&document](const Diagnostic& diagnostic) -> Id
    {
        auto graph = document.GetBlueprint().FindGraph(diagnostic.m_Graph);
        if (diagnostic.m_Node || !graph)
            return diagnostic.m_Node;
        auto link = diagnostic.m_Link ? graph->FindLink(diagnostic.m_Link) : nullptr;
        auto node = link ? graph->NodeOf(link->m_To) : diagnostic.m_Pin ? graph->NodeOf(diagnostic.m_Pin) : nullptr;
        return node ? node->m_Id : 0;
    };

    if (m_Revision != document.m_Revision)
    {
        m_Revision = document.m_Revision;
        m_Compiled.clear();
        for (auto& diagnostic : Compile(document.GetBlueprint()).m_Diagnostics)
            m_Compiled.push_back({ diagnostic, owner(diagnostic), false });
    }

    m_Problems = m_Compiled;
    if (auto context = debugger.GetContext())
        for (auto& diagnostic : context->Diagnostics())
            m_Problems.push_back({ diagnostic, owner(diagnostic), true });
}

std::string blueprint_editor_utilities::ProblemList::Text(const Blueprint& blueprint, const Problem& problem)
{
    auto& diagnostic = problem.m_Diagnostic;
    auto  graph      = blueprint.FindGraph(diagnostic.m_Graph);
    auto  link       = graph && diagnostic.m_Link ? graph->FindLink(diagnostic.m_Link) : nullptr;
    auto  input      = link && !diagnostic.m_Error.Is(BlueprintError::NoPin) ? graph->FindPin(link->m_To) : nullptr; // a gone pin's names its ends
    auto  message    = std::string(diagnostic.m_Error.Message());
    return input ? input->m_Key + ": " + message : message;
}

int blueprint_editor_utilities::ProblemList::Count(Severity severity, Id node) const
{
    return static_cast<int>(std::count_if(m_Problems.begin(), m_Problems.end(), [severity, node](const Problem& problem)
    {
        return problem.m_Diagnostic.m_Severity == severity && (!node || problem.m_Node == node);
    }));
}

bool blueprint_editor_utilities::ProblemList::BlocksRun() const
{
    return std::any_of(m_Compiled.begin(), m_Compiled.end(), [](const Problem& problem) { return problem.m_Diagnostic.m_Severity == Severity::Error; });
}

void blueprint_editor_utilities::FlowTakenLink(const Debugger& debugger, const Graph& graph)
{
    if (!debugger.IsActive() || !debugger.TakenLink())
        return;
    ax::NodeEditor::PushStyleVar(ax::NodeEditor::StyleVar_FlowDuration, 0.5f);
    for (auto link : FlowChain(graph, debugger.TakenLink()))
        ax::NodeEditor::Flow(link);
    ax::NodeEditor::PopStyleVar();
}

std::string blueprint_editor_utilities::WhenWritten(const Watch& watch, uint64_t step)
{
    if (!watch.m_Step)
        return "not run yet";
    if (*watch.m_Step == 0)
        return {};
    if (*watch.m_Step == step)
        return "this step";
    return "step " + std::to_string(*watch.m_Step);
}

std::string blueprint_editor_utilities::WakeText(const Wake& wake, double now)
{
    if (wake.m_Kind == Wake::Kind::Signal)
        return std::string("signal ") + '"' + wake.m_Signal + '"';
    char text[32];
    std::snprintf(text, sizeof(text), "%.1f s left", std::max(0.0, wake.m_Time - now));
    return text;
}




blueprint_editor_utilities::ItemBuilder::ItemBuilder()
    : m_InCreate(ax::NodeEditor::BeginCreate(ImGui::GetStyleColorVec4(ImGuiCol_NavCursor)))
{

}

blueprint_editor_utilities::ItemBuilder::~ItemBuilder()
{
    if (m_InCreate)
        ax::NodeEditor::EndCreate();
}

blueprint_editor_utilities::ItemBuilder::operator bool() const
{
    return m_InCreate;
}

blueprint_editor_utilities::ItemBuilder::NodeBuilder* blueprint_editor_utilities::ItemBuilder::QueryNewNode()
{
    if (m_InCreate && ax::NodeEditor::QueryNewNode(&m_NodeBuilder.m_PinId))
        return &m_NodeBuilder;
    else
        return nullptr;
}

blueprint_editor_utilities::ItemBuilder::LinkBuilder* blueprint_editor_utilities::ItemBuilder::QueryNewLink()
{
    if (m_InCreate && ax::NodeEditor::QueryNewLink(&m_LinkBuilder.m_StartPinId, &m_LinkBuilder.m_EndPinId))
        return &m_LinkBuilder;
    else
        return nullptr;
}

bool blueprint_editor_utilities::ItemBuilder::LinkBuilder::Accept(ImVec4 color)
{
    return ax::NodeEditor::AcceptNewItem(color, 3.0f);
}

void blueprint_editor_utilities::ItemBuilder::LinkBuilder::Reject()
{
    ax::NodeEditor::RejectNewItem(ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
}

bool blueprint_editor_utilities::ItemBuilder::NodeBuilder::Accept()
{
    return ax::NodeEditor::AcceptNewItem(ImVec4(0.34f, 1.0f, 0.34f, 1.0f), 3.0f);
}

void blueprint_editor_utilities::ItemBuilder::NodeBuilder::Reject()
{
    ax::NodeEditor::RejectNewItem(ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
}




blueprint_editor_utilities::ItemDeleter::ItemDeleter()
    : m_InDelete(ax::NodeEditor::BeginDelete())
{

}

blueprint_editor_utilities::ItemDeleter::~ItemDeleter()
{
    if (m_InDelete)
        ax::NodeEditor::EndDelete();
}

blueprint_editor_utilities::ItemDeleter::operator bool() const
{
    return m_InDelete;
}

blueprint_editor_utilities::ItemDeleter::NodeDeleter* blueprint_editor_utilities::ItemDeleter::QueryDeletedNode()
{
    if (m_InDelete && ax::NodeEditor::QueryDeletedNode(&m_NodeDeleter.m_NodeId))
        return &m_NodeDeleter;
    else
        return nullptr;
}

blueprint_editor_utilities::ItemDeleter::LinkDeleter* blueprint_editor_utilities::ItemDeleter::QueryDeleteLink()
{
    if (m_InDelete && ax::NodeEditor::QueryDeletedLink(&m_LinkDeleter.m_LinkId, &m_LinkDeleter.m_StartPinId, &m_LinkDeleter.m_EndPinId))
        return &m_LinkDeleter;
    else
        return nullptr;
}

bool blueprint_editor_utilities::ItemDeleter::LinkDeleter::Accept()
{
    return ax::NodeEditor::AcceptDeletedItem();
}

void blueprint_editor_utilities::ItemDeleter::LinkDeleter::Reject()
{
    ax::NodeEditor::RejectDeletedItem();
}

bool blueprint_editor_utilities::ItemDeleter::NodeDeleter::Accept(bool deleteLinks /*= true*/)
{
    return ax::NodeEditor::AcceptDeletedItem(deleteLinks);
}

void blueprint_editor_utilities::ItemDeleter::NodeDeleter::Reject()
{
    ax::NodeEditor::RejectDeletedItem();
}




void blueprint_editor_utilities::CreateNodeDialog::Open(Id fromPin)
{
    m_FromPin   = fromPin;
    m_DropPoint = ax::NodeEditor::ScreenToCanvas(ImGui::GetMousePos());
    m_Revision  = std::nullopt;
    m_Focus     = true;
    m_Search[0] = 0;
    m_Selected  = 0;
    ImGui::OpenPopup("##create_node");
}

void blueprint_editor_utilities::CreateNodeDialog::Show(Document& document)
{
    m_Open = ImGui::IsPopupOpen("##create_node");
    if (!m_Open)
        return;

    if (!ImGui::BeginPopup("##create_node"))
        return;

    auto popupPosition = ImGui::GetMousePosOnOpeningCurrentPopup();
    if (m_Revision != document.m_Revision)
        Update(document);

    auto& graph = document.GetGraph();
    const auto width = ImGui::GetFontSize() * 18.0f;
    if (m_Focus)
    {
        ImGui::SetKeyboardFocusHere();
        m_Focus = false;
    }
    ImGui::SetNextItemWidth(width);
    const auto enter = ImGui::InputTextWithHint("##search", "Search", m_Search, sizeof(m_Search), ImGuiInputTextFlags_EnterReturnsTrue);
    if (ImGui::IsItemEdited())
        m_Selected = 0;
    if (auto from = m_FromPin ? graph.FindPin(m_FromPin) : nullptr)
        ImGui::TextDisabled("From %s pin", from->m_Flow ? "flow" : graph.TypeText(from->m_Id).c_str());

    const auto move = (ImGui::IsKeyPressed(ImGuiKey_DownArrow) ? 1 : 0) - (ImGui::IsKeyPressed(ImGuiKey_UpArrow) ? 1 : 0);
    if (move != 0)
    {
        m_Selected = std::clamp(m_Selected + move, 0, std::max(m_Shown - 1, 0));
        m_Scroll   = true;
    }

    // The entries in the order shown, Enter making the selected one.
    const NewNodeFit* chosen = nullptr;
    auto shown = 0;
    auto show  = [&](const NewNodeFit& entry)
    {
        const auto selected = shown++ == m_Selected;
        if (ShowType(entry, selected) || (enter && selected))
            chosen = &entry;
        if (selected && std::exchange(m_Scroll, false))
            ImGui::SetScrollHereY();
    };
    ImGui::BeginChild("##types", ImVec2(width, ImGui::GetTextLineHeightWithSpacing() * 16.0f));
    if (m_Entries.empty())
        ImGui::TextDisabled("No node takes a link with it");
    else if (m_Search[0])
    {
        std::vector<std::pair<int, const NewNodeFit*>> matches;
        for (auto& entry : m_Entries)
        {
            auto& title = entry.m_Type->m_Title;
            if (auto found = ImStristr(title.c_str(), title.c_str() + title.size(), m_Search, nullptr))
                matches.emplace_back((entry.m_Cast.empty() ? 0 : 2) + (found == title.c_str() ? 0 : 1), &entry);
        }
        std::stable_sort(matches.begin(), matches.end(), [](const auto& lhs, const auto& rhs)
        {
            return std::tie(lhs.first, lhs.second->m_Type->m_Title) < std::tie(rhs.first, rhs.second->m_Type->m_Title);
        });

        for (size_t i = 0; i < matches.size(); ++i)
        {
            auto entry = matches[i].second;
            if (!entry->m_Cast.empty() && (i == 0 || matches[i - 1].second->m_Cast.empty()))
                ImGui::SeparatorText("With cast");
            show(*entry);
        }
        if (matches.empty())
            ImGui::TextDisabled("No node matches");
    }
    else
    {
        ImGui::PushID(m_FromPin ? "from_pin" : "all");
        const auto flags = m_FromPin ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None;
        const char* group = nullptr;
        bool open = false;
        for (auto& entry : m_Entries)
        {
            auto label = !entry.m_Cast.empty() ? "With cast" : entry.m_Type->m_Category.empty() ? "Other" : entry.m_Type->m_Category.c_str();
            if (!group || std::string_view(label) != group)
            {
                if (open)
                    ImGui::TreePop();
                group = label;
                open  = ImGui::TreeNodeEx(label, flags);
            }
            if (open)
                show(entry);
        }
        if (open)
            ImGui::TreePop();
        ImGui::PopID();
    }
    ImGui::EndChild();
    m_Shown  = shown;
    m_Scroll = false;

    if (chosen)
    {
        Create(document, *chosen, popupPosition);
        ImGui::CloseCurrentPopup();
    }
    else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}

void blueprint_editor_utilities::CreateNodeDialog::Update(Document& document)
{
    m_Revision = document.m_Revision;
    auto& graph = document.GetGraph();
    if (m_FromPin && !graph.FindPin(m_FromPin))
        m_FromPin = 0;

    // A function's entry and returns go only in its own graph, which has one entry at most, and events
    // only in event graphs.
    auto& blueprint = document.GetBlueprint();
    auto  types     = blueprint.NodeTypes();
    std::erase_if(types, [&blueprint, &graph](const NodeType* type)
    {
        if (blueprint.FindFunction(graph.GetId()) && !type->EventName({}).empty())
            return true;
        auto function = blueprint.FunctionOf(*type);
        if (!function || type->m_Name.starts_with("call:"))
            return false;
        if (function->m_Graph != graph.GetId())
            return true;
        return type->m_Name.starts_with("entry:") && std::any_of(graph.Nodes().begin(), graph.Nodes().end(), [type](const Node& node) { return node.m_Type == type; });
    });
    m_Entries.clear();
    if (m_FromPin)
    {
        if (auto fits = graph.FitNewNodes(m_FromPin, types))
            m_Entries = std::move(fits).GetValue();
    }
    else
    {
        for (auto type : types)
            m_Entries.push_back({ type, {}, {} });
    }
    // For a link dropped on the canvas, a reroute comes first.
    auto order = [dropped = m_FromPin != 0](const NewNodeFit& entry)
    {
        const auto cast = !entry.m_Cast.empty();
        return std::tuple(cast, !(dropped && entry.m_Type->IsReroute()), cast ? std::string_view() : std::string_view(entry.m_Type->m_Category), std::string_view(entry.m_Type->m_Title));
    };
    std::stable_sort(m_Entries.begin(), m_Entries.end(), [&order](const NewNodeFit& lhs, const NewNodeFit& rhs) { return order(lhs) < order(rhs); });
}

bool blueprint_editor_utilities::CreateNodeDialog::ShowType(const NewNodeFit& entry, bool selected)
{
    ImGui::PushID(entry.m_Type);
    const auto chosen = ImGui::Selectable(entry.m_Type->m_Title.c_str(), selected);
    if (ImGui::IsItemHovered() && (!entry.m_Type->m_Category.empty() || !entry.m_Pin.empty()))
    {
        ImGui::BeginTooltip();
        if (!entry.m_Type->m_Category.empty())
            ImGui::TextUnformatted(entry.m_Type->m_Category.c_str());
        if (!entry.m_Pin.empty())
            ImGui::TextDisabled("Links its %s", entry.m_Pin.c_str());
        ImGui::EndTooltip();
    }
    if (auto arrow = entry.m_Cast.find("->"); arrow != std::string::npos)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("(%.*s -> %s)", static_cast<int>(arrow), entry.m_Cast.c_str(), entry.m_Cast.c_str() + arrow + 2);
    }
    ImGui::PopID();
    return chosen;
}

void blueprint_editor_utilities::CreateNodeDialog::Create(Document& document, const NewNodeFit& entry, ImVec2 position)
{
    auto& graph = document.GetGraph();
    auto transaction = document.BeginUndoTransaction("Create Node");
    auto node = graph.AddNode(entry.m_Type->m_Name);
    if (!node)
    {
        auto message = node.GetError().Message();
        LOGE("[CreateNodeDialog] %.*s", static_cast<int>(message.size()), message.data());
        transaction->Discard();
        return;
    }

    ax::NodeEditor::SetNodePosition(node.GetValue(), ax::NodeEditor::ScreenToCanvas(position));
    ax::NodeEditor::SelectNode(node.GetValue());
    m_CreatedNode = node.GetValue();

    auto name = graph.FindNode(m_CreatedNode)->Name();
    auto from = m_FromPin ? graph.FindPin(m_FromPin) : nullptr;
    auto pin  = graph.FindNode(m_CreatedNode)->FindPin(entry.m_Pin);
    if (from && pin)
    {
        const auto output = from->m_Direction == Direction::Output;
        auto link = graph.Connect(output ? from->m_Id : pin->m_Id, output ? pin->m_Id : from->m_Id, entry.m_Cast.empty() ? LinkMode::Plain : LinkMode::Cast);
        if (link)
            LOGV("[CreateNodeDialog] %s linked through its %s", name.c_str(), entry.m_Pin.c_str());
        else
        {
            auto message = link.GetError().Message();
            LOGW("[CreateNodeDialog] %s not linked: %.*s", name.c_str(), static_cast<int>(message.size()), message.data());
        }
    }

    LOGV("[CreateNodeDialog] %s created", name.c_str());
    transaction->AddAction("%s created", name.c_str());
}




void blueprint_editor_utilities::NodeContextMenu::Open(Id node)
{
    m_Node = node;
    ImGui::OpenPopup("##node-context-menu");
}

void blueprint_editor_utilities::NodeContextMenu::EditComment(const Node& comment)
{
    m_EditedComment  = comment.m_Id;
    m_EditingComment = true;
    std::snprintf(m_CommentText, sizeof(m_CommentText), "%s", CommentText(comment).c_str());
}

void blueprint_editor_utilities::NodeContextMenu::ShowCommentText(Document& document)
{
    if (auto text = NamePopup("##comment-text", m_EditingComment, m_CommentText, document.GetGraph().FindNode(m_EditedComment) != nullptr))
        SetCommentText(document, m_EditedComment, *text);
}

void blueprint_editor_utilities::NodeContextMenu::Show(Document& document, Debugger& debugger)
{
    ShowRename(document);
    ShowCommentText(document);
    ShowRenameParameter(document);
    ShowArguments(document, debugger);
    if (!ImGui::IsPopupOpen("##node-context-menu"))
        return;

    auto node = document.GetGraph().FindNode(m_Node);

    if (ImGui::BeginPopup("##node-context-menu"))
    {
        if (node && IsComment(*node) && ImGui::MenuItem("Edit Comment..."))
            EditComment(*node);
        if (node && !node->m_Orphans.empty() && ImGui::MenuItem("Remove Orphaned Pins"))
            RemoveOrphans(document, node->m_Id);
        if (ImGui::MenuItem("Breakpoint", "F9", debugger.HasBreakpoint(m_Node), node && !IsComment(*node) && !IsReroute(*node)))
        {
            const auto enabled = !debugger.HasBreakpoint(m_Node);
            if (!ax::NodeEditor::IsNodeSelected(m_Node))
                debugger.SetBreakpoint(m_Node, enabled);
            else
                for (auto selected : GetSelectedNodes())
                    debugger.SetBreakpoint(selected, enabled);
        }

        auto run     = node ? RunArguments::Of(document, node->m_Id) : std::nullopt;
        auto runItem = [&](const char* label, bool enabled, bool running)
        {
            const auto asks = run && run->Asks();
            if (!ImGui::MenuItem(asks ? (std::string(label) + "...").c_str() : label, nullptr, false, enabled))
                return;
            if (asks)
            {
                m_Run     = node->m_Id;
                m_Running = running;
                m_Asking  = true;
            }
            else if (run->m_Call)
                debugger.Call(document, running, run->m_Name);
            else
                debugger.Start(document, running, run->m_Name);
        };

        if (auto event = node ? node->m_Type->EventName(node->m_Config) : std::string(); !event.empty())
        {
            runItem("Run Event", true, true);
            runItem("Debug Event", true, false);
            if (IsCustomEvent(*node) && ImGui::MenuItem("Rename Event..."))
            {
                m_RenamedEvent = node->m_Id;
                m_Renaming     = true;
                std::snprintf(m_EventName, sizeof(m_EventName), "%s", event.c_str());
            }
        }

        // The change is made once the menu is drawn.
        std::function<void()> change;
        if (node && IsReroute(*node))
        {
            if (ImGui::MenuItem("Dissolve", "Delete"))
                change = [&document, id = node->m_Id, name = node->Name()]
                {
                    auto transaction = document.BeginUndoTransaction("Dissolve Reroute");
                    DissolveReroute(document.GetGraph(), id);
                    transaction->AddAction("%s dissolved", name.c_str());
                };
            if (ImGui::MenuItem("Remove With Links", "Alt+Click"))
                change = [&document, id = node->m_Id] { RemoveReroute(document, id); };
        }
        auto signature = node ? SignatureNodeOf(document.GetBlueprint(), *node) : SignatureNode();
        if (signature.m_Function && signature.m_Entry)
        {
            const auto  graph    = signature.m_Function->m_Graph;
            const auto& declared = signature.m_Function->m_Signature;
            ImGui::Separator();
            runItem("Run Function", run.has_value(), true);
            runItem("Debug Function", run.has_value(), false);
            if (ImGui::MenuItem("Pure", nullptr, declared.m_Pure))
                change = [&document, graph, pure = declared.m_Pure] { ChangeSignature(document, graph, pure ? "Make Impure" : "Make Pure", [](Signature& changed) { changed.m_Pure = !changed.m_Pure; }); };
            if (ImGui::BeginMenu("Type Parameters"))
            {
                for (size_t i = 0; i < declared.m_Parameters.size(); ++i)
                {
                    auto& parameter = declared.m_Parameters[i];
                    if (!ImGui::BeginMenu(parameter.m_Name.c_str()))
                        continue;
                    ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false);
                    for (auto head : document.GetBlueprint().GetRegistry().ValueTypes())
                    {
                        auto allowed = std::find(parameter.m_Allowed.begin(), parameter.m_Allowed.end(), head) != parameter.m_Allowed.end();
                        if (ImGui::MenuItem(head->m_Name.c_str(), nullptr, allowed))
                            change = [&document, graph, i, head] { ToggleAllowedType(document, graph, i, head); };
                    }
                    ImGui::PopItemFlag();
                    ImGui::Separator();
                    ImGui::TextDisabled("Allows %s", parameter.m_Allowed.empty() ? "any type" : "the types checked");
                    if (ImGui::MenuItem("Rename..."))
                    {
                        m_ParameterGraph    = graph;
                        m_Parameter         = i;
                        m_RenamingParameter = true;
                        std::snprintf(m_ParameterName, sizeof(m_ParameterName), "%s", parameter.m_Name.c_str());
                    }
                    if (ImGui::MenuItem("Remove"))
                        change = [&document, graph, i] { RemoveTypeParameter(document, graph, static_cast<int>(i)); };
                    ImGui::EndMenu();
                }
                if (ImGui::MenuItem("Add"))
                    change = [&document, graph] { AddTypeParameter(document, graph); };
                ImGui::EndMenu();
            }
        }
        if (signature.m_Function && !signature.m_Entry)
            ImGui::Separator();
        if (signature.m_Function && ImGui::MenuItem("Signature..."))
            m_SignatureRequested = true;

        ImGui::Separator();

        if (ImGui::MenuItem("Break Links", nullptr, nullptr, node != nullptr && ax::NodeEditor::HasAnyLinks(ax::NodeEditor::NodeId(m_Node))))
        {
            if (!ax::NodeEditor::IsNodeSelected(m_Node))
                ax::NodeEditor::BreakLinks(ax::NodeEditor::NodeId(m_Node));
            else
                for (auto selected : GetSelectedNodes())
                    ax::NodeEditor::BreakLinks(ax::NodeEditor::NodeId(selected));
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Delete", nullptr, nullptr, node != nullptr))
        {
            if (!ax::NodeEditor::IsNodeSelected(m_Node))
                ax::NodeEditor::DeleteNode(m_Node);
            else
                for (auto selected : GetSelectedNodes())
                    ax::NodeEditor::DeleteNode(selected);
        }

        ImGui::EndPopup();

        if (change)
            change();
    }
}




void blueprint_editor_utilities::NodeContextMenu::ShowRename(Document& document)
{
    if (auto name = NamePopup("##rename-event", m_Renaming, m_EventName, document.GetGraph().FindNode(m_RenamedEvent) != nullptr))
        RenameEvent(document, m_RenamedEvent, *name);
}

// The name typed is checked as the blueprint will check it, so the reason it can't be shows at once.
void blueprint_editor_utilities::NodeContextMenu::ShowRenameParameter(Document& document)
{
    auto& blueprint = document.GetBlueprint();
    auto  function  = blueprint.FindFunction(m_ParameterGraph);
    const auto exists = function && m_Parameter < function->m_Signature.m_Parameters.size();
    std::string problem;
    if (exists && ImGui::IsPopupOpen("##rename-parameter") && function->m_Signature.m_Parameters[m_Parameter].m_Name != m_ParameterName)
    {
        auto signature = function->m_Signature;
        signature.m_Parameters[m_Parameter].m_Name = m_ParameterName;
        if (auto check = blueprint.CheckSignature(signature); !check)
            problem = check.GetError().Message();
    }
    if (auto name = NamePopup("##rename-parameter", m_RenamingParameter, m_ParameterName, exists, problem))
        RenameTypeParameter(document, m_ParameterGraph, m_Parameter, *name);
}

void blueprint_editor_utilities::NodeContextMenu::ShowArguments(Document& document, Debugger& debugger)
{
    auto& blueprint = document.GetBlueprint();
    if (std::exchange(m_Asking, false))
    {
        auto asked = RunArguments::Of(document, m_Run);
        if (!asked)
            return;
        if (auto used = m_Used.find(asked->Key()); used != m_Used.end())
            asked->Reuse(used->second);
        asked->Settle(blueprint);
        m_Arguments = std::move(*asked);
        ++m_Asked;
        ImGui::OpenPopup("##run-arguments");
    }
    if (!ImGui::BeginPopup("##run-arguments"))
        return;
    auto current = RunArguments::Of(document, m_Run);
    if (!current || !current->Asks())
    {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    current->Reuse(m_Arguments);
    current->Settle(blueprint);
    m_Arguments = std::move(*current);

    auto& arguments = m_Arguments;
    auto& types     = blueprint.GetTypes();
    ImGui::Text("%s %s", m_Running ? "Run" : "Debug", arguments.m_Name.c_str());
    ImGui::Separator();
    // Of IDs of their own each time, the value editors open closed.
    ImGui::PushID(m_Asked);
    if (ImGui::BeginTable("##arguments", 3, ImGuiTableFlags_SizingFixedFit))
    {
        for (size_t i = 0; i < arguments.m_Parameters.size(); ++i)
        {
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(arguments.m_Parameters[i].m_Name.c_str());
            ImGui::TableNextColumn();
            ImGui::TextDisabled("type");
            ImGui::TableNextColumn();
            auto& chosen = arguments.m_Chosen[i];
            ImGui::SetNextItemWidth(120.0f);
            if (ImGui::BeginCombo("##type", chosen ? chosen->m_Name.c_str() : "none"))
            {
                for (auto choice : RunArguments::Choices(blueprint.GetRegistry(), arguments.m_Parameters[i]))
                    if (ImGui::Selectable(choice->m_Name.c_str(), choice == chosen))
                        chosen = choice;
                ImGui::EndCombo();
            }
            ImGui::PopID();
        }
        for (size_t i = 0; i < arguments.m_Names.size(); ++i)
        {
            ImGui::PushID(static_cast<int>(arguments.m_Parameters.size() + i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(arguments.m_Names[i].c_str());
            ImGui::TableNextColumn();
            auto type = arguments.TypeOf(types, i);
            ImGui::TextDisabled("%s", type ? type->m_Name.c_str() : "undecided");
            ImGui::TableNextColumn();
            if (type && arguments.m_Values[i].GetType() == type)
                DrawValueWithEditor(static_cast<Id>(i + 1), arguments.m_Values[i]);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::PopID();
    ImGui::Separator();
    ImGui::BeginDisabled(!arguments.IsReady(types));
    const auto run = ImGui::Button(m_Running ? "Run" : "Debug");
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        ImGui::CloseCurrentPopup();
    if (run)
    {
        m_Used[arguments.Key()] = arguments;
        ImGui::CloseCurrentPopup();
        if (arguments.m_Call)
            debugger.Call(document, m_Running, arguments.m_Name, arguments.m_Values);
        else
            debugger.Start(document, m_Running, arguments.m_Name, arguments.m_Values);
    }
    ImGui::EndPopup();
}

std::optional<blueprint_editor_utilities::RunArguments> blueprint_editor_utilities::RunArguments::Of(const Document& document, Id id)
{
    auto node = document.GetGraph().FindNode(id);
    if (!node)
        return std::nullopt;
    RunArguments run;
    run.m_Name = node->m_Type->EventName(node->m_Config);
    if (!run.m_Name.empty())
    {
        for (auto& pin : node->m_Pins)
        {
            if (pin.m_Flow || pin.m_Direction != Direction::Output)
                continue;
            run.m_Names.push_back(pin.m_Name);
            run.m_Terms.push_back(pin.m_DecidedType ? TypeTerm(*pin.m_DecidedType) : TypeTerm());
        }
        return run;
    }
    auto signature = SignatureNodeOf(document.GetBlueprint(), *node);
    if (!signature.m_Function || !signature.m_Entry || signature.m_Function->m_Signature.m_Pure)
        return std::nullopt;
    auto& declared = signature.m_Function->m_Signature;
    run.m_Name       = signature.m_Function->m_Name;
    run.m_Call       = true;
    run.m_Parameters = declared.m_Parameters;
    for (auto& input : declared.m_Inputs)
    {
        run.m_Names.push_back(input.m_Name);
        run.m_Terms.push_back(input.m_Type);
    }
    return run;
}

std::vector<const blueprint_editor_utilities::Type*> blueprint_editor_utilities::RunArguments::Choices(const Registry& registry, const VariableDeclaration& parameter)
{
    std::vector<const Type*> choices;
    for (auto head : registry.ValueTypes())
    {
        auto allowed = parameter.m_Allowed.empty() || std::find(parameter.m_Allowed.begin(), parameter.m_Allowed.end(), head) != parameter.m_Allowed.end();
        if (head->m_Arity == 0 && allowed)
            choices.push_back(&head->AsType());
    }
    return choices;
}

std::string blueprint_editor_utilities::RunArguments::Key() const
{
    return (m_Call ? "call:" : "") + m_Name;
}

const blueprint_editor_utilities::Type* blueprint_editor_utilities::RunArguments::TypeOf(TypeTable& types, size_t value) const
{
    return value < m_Terms.size() ? TypeWith(types, m_Terms[value], m_Chosen) : nullptr;
}

void blueprint_editor_utilities::RunArguments::Reuse(const RunArguments& other)
{
    if (other.Key() != Key())
        return;
    m_Chosen = other.m_Chosen;
    m_Values = other.m_Values;
}

void blueprint_editor_utilities::RunArguments::Settle(const Blueprint& blueprint)
{
    m_Chosen.resize(m_Parameters.size(), nullptr);
    for (size_t i = 0; i < m_Parameters.size(); ++i)
    {
        auto choices = Choices(blueprint.GetRegistry(), m_Parameters[i]);
        if (std::find(choices.begin(), choices.end(), m_Chosen[i]) == choices.end())
            m_Chosen[i] = choices.empty() ? nullptr : choices.front();
    }
    m_Values.resize(m_Names.size());
    for (size_t i = 0; i < m_Names.size(); ++i)
        if (auto type = TypeOf(blueprint.GetTypes(), i); type && m_Values[i].GetType() != type)
            m_Values[i] = type->m_Head->Default(*type);
}

bool blueprint_editor_utilities::RunArguments::IsReady(TypeTable& types) const
{
    for (size_t i = 0; i < m_Names.size(); ++i)
        if (auto type = TypeOf(types, i); !type || i >= m_Values.size() || m_Values[i].GetType() != type)
            return false;
    return true;
}

void blueprint_editor_utilities::PinContextMenu::Open(Id pin)
{
    m_Pin = pin;
    ImGui::OpenPopup("##pin-context-menu");
}

void blueprint_editor_utilities::PinContextMenu::Show(Document& document, Debugger& debugger)
{
    ShowRename(document);
    if (!ImGui::IsPopupOpen("##pin-context-menu"))
        return;

    auto& graph = document.GetGraph();
    auto  pin   = graph.FindPin(m_Pin);
    auto  node  = graph.NodeOf(m_Pin);

    if (ImGui::BeginPopup("##pin-context-menu"))
    {
        if (ImGui::MenuItem("Watch", "Ctrl+Click", debugger.IsWatched(m_Pin), pin != nullptr && !pin->m_Flow))
            debugger.SetWatched(m_Pin, !debugger.IsWatched(m_Pin));

        if (ImGui::MenuItem("Break Links", nullptr, nullptr, ax::NodeEditor::HasAnyLinks(ax::NodeEditor::PinId(m_Pin))))
            ax::NodeEditor::BreakLinks(ax::NodeEditor::PinId(m_Pin));

        // An orphan: its struct pin split again, or its links moved to a pin of its node.
        auto orphanNode = pin ? nullptr : graph.NodeOfOrphan(m_Pin);
        auto orphan     = orphanNode ? orphanNode->FindOrphan(m_Pin) : nullptr;
        std::function<void()> mend;
        if (orphan)
        {
            if (auto whole = SplitAgainKey(*orphanNode, *orphan); !whole.empty() && ImGui::MenuItem("Split Again"))
                mend = [&document, node = orphanNode->m_Id, whole] { SplitStructPin(document, node, whole, true); };
            if (ImGui::BeginMenu("Move Links To"))
            {
                for (auto& candidate : orphanNode->m_Pins)
                    if (candidate.m_Direction == orphan->m_Direction && candidate.m_Flow == orphan->m_Flow && ImGui::MenuItem(candidate.m_Key.c_str()))
                        mend = [&document, from = m_Pin, to = candidate.m_Id] { MoveOrphanLinks(document, from, to); };
                ImGui::EndMenu();
            }
        }

        std::vector<Id> others; // the other ends of its links
        for (auto& link : graph.Links())
            if (link.m_From == m_Pin || link.m_To == m_Pin)
                others.push_back(link.m_From == m_Pin ? link.m_To : link.m_From);
        if (ImGui::BeginMenu("Go To", !others.empty()))
        {
            for (auto other : others)
                if (ImGui::MenuItem(EndText(graph, other).c_str()))
                    GoToEnd(graph, other);
            ImGui::EndMenu();
        }

        int  index  = -1;
        auto group  = pin ? FindGroup(*node, pin->m_Key, &index) : nullptr;
        auto remove = group && ImGui::MenuItem("Remove Pin", nullptr, nullptr, GroupCount(*group, node->m_Config) > group->m_Min);

        auto splitOf   = pin ? SplitOf(*node, pin->m_Key) : std::string();
        auto split     = pin && SplittableStruct(*pin) && ImGui::MenuItem("Split Struct Pin");
        auto recombine = !splitOf.empty() && ImGui::MenuItem("Recombine Struct Pin");

        // The choice is set once the menu is drawn.
        auto variable = pin ? PinVariable(graph, *pin) : nullptr;
        std::optional<const Type*> choice; // null: reset
        if (variable)
        {
            auto chosen = node->m_Config.m_Types.find(variable->m_Name);
            ImGui::Separator();
            if (ImGui::BeginMenu("Type"))
            {
                for (auto head : document.GetBlueprint().GetRegistry().ValueTypes())
                {
                    auto allowed = variable->m_Allowed.empty() || std::find(variable->m_Allowed.begin(), variable->m_Allowed.end(), head) != variable->m_Allowed.end();
                    if (head->m_Arity > 0 || !allowed)
                        continue;
                    auto isChosen = chosen != node->m_Config.m_Types.end() && chosen->second == &head->AsType();
                    if (ImGui::MenuItem(head->m_Name.c_str(), nullptr, isChosen))
                        choice = &head->AsType();
                }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Reset Type", nullptr, nullptr, chosen != node->m_Config.m_Types.end()))
                choice = nullptr;
        }

        auto edit = pin && node ? ShowSignatureItems(document, *node, *pin) : std::function<void()>();
        if (pin && node && IsCustomEvent(*node) && !pin->m_Flow)
        {
            ImGui::Separator();
            ShowRenameItem(*pin);
        }

        if (choice)
        {
            auto config = node->m_Config;
            if (*choice)
                config.m_Types[variable->m_Name] = *choice;
            else
                config.m_Types.erase(variable->m_Name);
            const auto what = *choice ? "Choose Type" : "Reset Type";
            const auto id   = node->m_Id;
            auto transaction = document.BeginUndoTransaction(what);
            if (auto set = graph.SetConfig(id, std::move(config)); !set)
            {
                auto message = set.GetError().Message();
                LOGW("[PinContextMenu] %.*s", static_cast<int>(message.size()), message.data());
                transaction->Discard();
            }
            else
                transaction->AddAction("%s: %s", graph.FindNode(id)->Name().c_str(), what);
        }
        else if (remove)
            RemoveGroupPin(document, node->m_Id, group->m_Name, index);
        else if (split || recombine)
            SplitStructPin(document, node->m_Id, split ? pin->m_Key : splitOf, split);
        else if (edit)
            edit();
        else if (mend)
            mend();

        ImGui::EndPopup();
    }
}

std::function<void()> blueprint_editor_utilities::PinContextMenu::ShowSignatureItems(Document& document, const Node& node, const Pin& pin)
{
    auto signature = SignatureNodeOf(document.GetBlueprint(), node);
    if (!signature.m_Function || pin.m_Flow)
        return {};
    const auto  graph    = signature.m_Function->m_Graph;
    const auto  input    = signature.m_Entry;
    const auto& declared = signature.m_Function->m_Signature;
    const auto& pins     = input ? declared.m_Inputs : declared.m_Outputs;
    auto at = std::find_if(pins.begin(), pins.end(), [&pin](const SignaturePin& other) { return other.m_Key == pin.m_Key; });
    if (at == pins.end())
        return {};

    ImGui::Separator();
    ShowRenameItem(pin);

    std::function<void()> edit;
    auto choices = SignatureTypeChoices(document.GetBlueprint().GetRegistry(), declared);
    auto items   = [&](bool array)
    {
        for (auto& choice : choices)
            if (choice.m_Array == array && ImGui::MenuItem(choice.m_Name.c_str(), nullptr, choice.m_Type == at->m_Type))
                edit = [&document, graph, input, key = pin.m_Key, type = choice.m_Type] { RetypeSignaturePin(document, graph, input, key, type); };
    };
    if (ImGui::BeginMenu("Type"))
    {
        items(false);
        if (ImGui::BeginMenu("Array"))
        {
            items(true);
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }

    if (ImGui::MenuItem(input ? "Remove Input" : "Remove Output"))
        edit = [&document, graph, input, key = pin.m_Key] { RemoveSignaturePin(document, graph, input, key); };
    return edit;
}

void blueprint_editor_utilities::PinContextMenu::ShowRenameItem(const Pin& pin)
{
    if (!ImGui::MenuItem("Rename..."))
        return;
    m_Renamed = pin.m_Id;
    m_Focus   = true;
    std::snprintf(m_Name, sizeof(m_Name), "%s", pin.m_Name.c_str());
}

void blueprint_editor_utilities::PinContextMenu::ShowRename(Document& document)
{
    auto& graph = document.GetGraph();
    auto  pin   = graph.FindPin(m_Renamed);
    auto  node  = pin ? graph.NodeOf(m_Renamed) : nullptr;
    auto  owner = node ? SignatureNodeOf(document.GetBlueprint(), *node) : SignatureNode();
    auto  name  = NamePopup("##rename-pin", m_Focus, m_Name, pin != nullptr);
    if (!name)
        return;
    if (!owner.m_Function)
        return RenamePin(document, m_Renamed, *name);
    RenameSignaturePin(document, owner.m_Function->m_Graph, owner.m_Entry, pin->m_Key, *name);
}

std::optional<std::string> blueprint_editor_utilities::NamePopup(const char* id, bool& open, std::span<char> name, bool exists, std::string_view problem)
{
    const auto opening = std::exchange(open, false);
    if (opening)
        ImGui::OpenPopup(id);
    if (!ImGui::BeginPopup(id))
        return std::nullopt;
    if (opening)
        ImGui::SetKeyboardFocusHere();
    const auto done     = ImGui::InputText("##name", name.data(), name.size(), ImGuiInputTextFlags_EnterReturnsTrue);
    const auto accepted = done && problem.empty();
    if (done && !accepted)
        ImGui::SetKeyboardFocusHere(-1);
    if (!problem.empty())
        ImGui::TextColored(ImColor(ProblemColor(Severity::Error)), "%.*s", static_cast<int>(problem.size()), problem.data());
    if (accepted || ImGui::IsKeyPressed(ImGuiKey_Escape, false) || !exists)
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    if (!accepted || !exists)
        return std::nullopt;
    return std::string(name.data());
}




void blueprint_editor_utilities::LinkContextMenu::Open(Id link)
{
    m_Link = link;
    ImGui::OpenPopup("##link-context-menu");
}

void blueprint_editor_utilities::LinkContextMenu::Show(Document& document)
{
    if (!ImGui::IsPopupOpen("##link-context-menu"))
        return;

    auto link = document.GetGraph().FindLink(m_Link);
    std::optional<ImVec2> insert; // where the reroute chosen goes, once the menu is drawn

    if (ImGui::BeginPopup("##link-context-menu"))
    {
        if (link && !link->m_Active)
        {
            auto reason = link->m_Reason.Message();
            ImGui::TextDisabled("Disabled: %.*s", static_cast<int>(reason.size()), reason.data());
            ImGui::Separator();
        }

        if (link)
        {
            auto& graph = document.GetGraph();
            for (auto end : { link->m_From, link->m_To })
                if (ImGui::MenuItem(("Go To " + EndText(graph, end)).c_str()))
                    GoToEnd(graph, end);
            ImGui::Separator();
        }

        if (ImGui::MenuItem("Insert Reroute", "Double-Click"))
            insert = ax::NodeEditor::ScreenToCanvas(ImGui::GetMousePosOnOpeningCurrentPopup());

        if (ImGui::MenuItem("Break"))
        {
            if (!ax::NodeEditor::IsLinkSelected(m_Link))
                ax::NodeEditor::DeleteLink(m_Link);
            else
                for (auto selected : GetSelectedLinks())
                    ax::NodeEditor::DeleteLink(selected);
        }

        ImGui::EndPopup();
    }

    if (insert)
        InsertReroute(document, m_Link, *insert);
}




void blueprint_editor_utilities::LinkDropMenu::Open(Id from, Id to, std::string reason, bool cast, std::optional<int> forced)
{
    m_From   = from;
    m_To     = to;
    m_Reason = std::move(reason);
    m_Cast   = cast;
    m_Forced = forced;
    ImGui::OpenPopup("##link-drop-menu");
}

void blueprint_editor_utilities::LinkDropMenu::Show(Document& document)
{
    if (!ImGui::IsPopupOpen("##link-drop-menu"))
        return;

    if (!ImGui::BeginPopup("##link-drop-menu"))
        return;

    auto& graph = document.GetGraph();
    auto  from  = graph.FindPin(m_From);
    auto  to    = graph.FindPin(m_To);

    // The choice is made once the menu is drawn.
    std::optional<LinkMode> mode;
    if (from && to)
    {
        ImGui::TextDisabled("%s", m_Reason.c_str());
        ImGui::Separator();
        if (m_Cast && from->m_DecidedType && to->m_DecidedType)
        {
            auto label = "Cast: " + from->m_DecidedType->m_Name + " -> " + to->m_DecidedType->m_Name;
            if (ImGui::MenuItem(label.c_str()))
                mode = LinkMode::Cast;
        }
        if (m_Forced)
        {
            auto label = *m_Forced == 0 ? std::string("Force") : "Force (disables " + std::to_string(*m_Forced) + (*m_Forced == 1 ? " link)" : " links)");
            if (ImGui::MenuItem(label.c_str()))
                mode = LinkMode::Force;
        }
    }
    ImGui::MenuItem("Cancel");

    if (mode)
        CreateLink(document, m_From, m_To, *mode);

    ImGui::EndPopup();
}
