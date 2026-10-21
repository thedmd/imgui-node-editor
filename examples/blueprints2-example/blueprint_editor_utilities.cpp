# define IMGUI_DEFINE_MATH_OPERATORS
# include "blueprint_editor_utilities.h"
# include "blueprint_editor_document.h"
# include "crude_logger.h"
# include <imgui_internal.h>
# include "imgui_extras.h"
# include <algorithm>
# include <cmath>
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

    uint32_t hash = 2166136261u;
    for (auto c : head->m_Name)
        hash = (hash ^ static_cast<uint8_t>(c)) * 16777619u;
    float r, g, b;
    ImGui::ColorConvertHSVtoRGB(static_cast<float>(hash % 360) / 360.0f, 0.6f, 0.9f, r, g, b);
    return ImVec4(r, g, b, 1.0f);
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

bool blueprint_editor_utilities::DrawValue(const Value& value)
{
    auto type = value.GetType();
    if (!type)
        return false;
    if (type == &BoolType())
        ImGui::TextUnformatted(value.AsBool() ? "true" : "false");
    else if (type == &Int32Type())
        ImGui::Text("%d", value.AsInt32());
    else if (type == &FloatType())
        ImGui::Text("%g", value.AsFloat());
    else
    {
        auto text = type == &StringType() ? std::string(value.AsString()) : type->m_Head->Format(value);
        ImGui::TextUnformatted(text.data(), text.data() + text.size());
    }
    return true;
}

// Files can't hold NaN or an infinity, so a Float editor takes only finite numbers.
bool blueprint_editor_utilities::EditValue(Value& value)
{
    ImEx::ScopedItemWidth scopedItemWidth{120};

    auto type = value.GetType();
    if (type == &BoolType())
    {
        value = Value::Bool(!value.AsBool());
        return true;
    }
    if (type == &Int32Type())
    {
        auto number = value.AsInt32();
        if (!ImGui::InputInt("##editor", &number, 1, 100, ImGuiInputTextFlags_EnterReturnsTrue))
            return false;
        value = Value::Int32(number);
        return true;
    }
    if (type == &FloatType())
    {
        auto number = value.AsFloat();
        if (!ImGui::InputFloat("##editor", &number, 1, 100, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue) || !std::isfinite(number))
            return false;
        value = Value::Float(number);
        return true;
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
        if (!ImGui::InputText("##editor", text.data(), text.size() + 1, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackResize, resize, &text))
            return false;
        text.resize(strlen(text.c_str()));
        value = Value::String(std::move(text));
        return true;
    }

    // Values of other types have no editor yet.
    return true;
}

bool blueprint_editor_utilities::DrawValueWithEditor(Id id, Value& value)
{
    auto storage = ImGui::GetStateStorage();
    auto activeId = static_cast<Id>(storage->GetInt(ImGui::GetID("PinValueEditor_ActivePinId"), 0));

    bool changed = false;
    if (activeId == id)
    {
        const auto before = value;
        if (EditValue(value))
        {
            activeId = 0;
            changed  = !(value == before);
        }
    }
    else
    {
        // Draw pin value
        PinValueBackgroundRenderer bg;
        if (!DrawValue(value))
        {
            bg.Discard();
            return false;
        }

        // Draw invisible button over pin value which triggers an editor if clicked
        auto itemMin = ImGui::GetItemRectMin();
        auto itemMax = ImGui::GetItemRectMax();
        auto itemSize = itemMax - itemMin;
        itemSize.x = ImMax(itemSize.x, 1.0f);
        itemSize.y = ImMax(itemSize.y, 1.0f);

        ImGui::SetCursorScreenPos(itemMin);

        if (ImGui::InvisibleButton("###pin_value_editor", itemSize))
            activeId = id;
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

void blueprint_editor_utilities::CanvasActions::Draw(ImVec2 canvasMin, ImVec2 canvasMax) const
{
    ImGui::PushFont(nullptr, ImGui::GetFontSize() * 0.75f);
    auto& style  = ImGui::GetStyle();
    auto  height = static_cast<float>(m_Entries.size()) * ImGui::GetTextLineHeightWithSpacing();
    auto  y      = ImMax(canvasMin.y, canvasMax.y - height - style.ItemSpacing.y);
    ImGui::PushClipRect(canvasMin, canvasMax, true);
    for (auto& entry : m_Entries)
    {
        auto width = ImGui::CalcTextSize(entry.m_Text.c_str()).x;
        ImGui::SetCursorScreenPos(ImVec2(canvasMax.x - style.ItemSpacing.x - width, y));
        ImGui::BeginDisabled(!entry.m_Active);
        ImGui::TextUnformatted(entry.m_Text.c_str());
        ImGui::EndDisabled();
        y += ImGui::GetTextLineHeightWithSpacing();
    }
    ImGui::PopClipRect();
    ImGui::PopFont();
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

bool blueprint_editor_utilities::Debugger::Start(const Document& document, bool running)
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
    auto fiber = m_Context->Start("Start");
    if (!fiber)
    {
        auto message = fiber.GetError().Message();
        LOGE("Execution: %.*s", static_cast<int>(message.size()), message.data());
        m_Context.reset();
        return false;
    }

    LOGI("Execution: %s", running ? "Run" : "Start");
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

void blueprint_editor_utilities::Debugger::SetBreakpoint(Id node, bool enabled)
{
    if (enabled)
        m_Breakpoints.insert(node);
    else
        m_Breakpoints.erase(node);
    if (m_Context)
        m_Context->SetBreakpoint(node, enabled);
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
    return fiber && fiber->m_State == FiberState::Ready;
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
    if (!fiber || fiber->m_State == FiberState::Ready || m_Reported)
        return;
    m_Reported = true;
    if (fiber->m_State == FiberState::Done)
        LOGI("Execution: Done (%llu steps)", static_cast<unsigned long long>(m_Context->StepCount()));
    else if (fiber->m_State == FiberState::Failed)
    {
        auto diagnostics = m_Context->Diagnostics();
        auto message = diagnostics.empty() ? std::string_view() : diagnostics.back().m_Error.Message();
        LOGE("Execution: Failed at step %llu: %.*s", static_cast<unsigned long long>(m_Context->StepCount()), static_cast<int>(message.size()), message.data());
    }
}

void blueprint_editor_utilities::ProblemList::Update(const Document& document, const Debugger& debugger)
{
    auto& graph = document.GetGraph();
    auto  owner = [&graph](const Diagnostic& diagnostic) -> Id
    {
        if (diagnostic.m_Node)
            return diagnostic.m_Node;
        auto link = diagnostic.m_Link ? graph.FindLink(diagnostic.m_Link) : nullptr;
        auto node = link ? graph.NodeOf(link->m_To) : diagnostic.m_Pin ? graph.NodeOf(diagnostic.m_Pin) : nullptr;
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

std::string blueprint_editor_utilities::ProblemList::Text(const Graph& graph, const Problem& problem)
{
    auto& diagnostic = problem.m_Diagnostic;
    auto  link       = diagnostic.m_Link ? graph.FindLink(diagnostic.m_Link) : nullptr;
    auto  input      = link ? graph.FindPin(link->m_To) : nullptr;
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

blueprint_editor_utilities::DebugOverlay::DebugOverlay(Debugger& debugger)
{
    if (!debugger.IsActive())
        return;
    m_Context = debugger.GetContext();
    m_Current = debugger.CurrentNode();
    m_Next    = debugger.NextNode();
    m_Link    = debugger.TakenLink();
}

void blueprint_editor_utilities::DebugOverlay::Begin()
{
    if (!m_Context)
        return;

    // Use splitter to draw pin values on top of nodes
    m_DrawList = ImGui::GetWindowDrawList();
    m_Splitter.Split(m_DrawList, 2);
}

void blueprint_editor_utilities::DebugOverlay::End()
{
    if (!m_Context)
        return;

    m_Splitter.Merge(m_DrawList);

    // The link the step took fades out once a step leaves it behind.
    if (m_Link)
    {
        ax::NodeEditor::PushStyleVar(ax::NodeEditor::StyleVar_FlowDuration, 0.5f);
        ax::NodeEditor::Flow(m_Link);
        ax::NodeEditor::PopStyleVar();
    }
}

void blueprint_editor_utilities::DebugOverlay::DrawNode(Id node)
{
    if (!m_Context)
        return;

    auto isCurrentNode = m_Current == node;
    auto isNextNode    = m_Next == node;
    if (!isCurrentNode && !isNextNode)
        return;

    // Draw to layer over nodes
    m_Splitter.SetCurrentChannel(m_DrawList, 1);

    // Save current layout to avoid conflicts with node measurements
    ImEx::ScopedSuspendLayout suspendLayout;

    auto itemRectMin = ImGui::GetItemRectMin();
    auto itemRectMax = ImGui::GetItemRectMax();

    auto pivot = ImVec2((itemRectMin.x + itemRectMax.x) * 0.5f, itemRectMin.y);
    auto markerMin = pivot - ImVec2(40.0f, 35.0f);
    auto markerMax = pivot + ImVec2(40.0f, 0.0f);

    auto color = ImGui::GetStyleColorVec4(isCurrentNode ? ImGuiCol_PlotHistogram : ImGuiCol_NavCursor);

    DrawIcon(m_DrawList, markerMin, markerMax, IconType::FlowDown, true, ImColor(color), 0);

    // Switch back to normal layer
    m_Splitter.SetCurrentChannel(m_DrawList, 0);
}

void blueprint_editor_utilities::DebugOverlay::DrawInputPin(const Pin& pin, bool linked)
{
    if (!m_Context || !linked || pin.m_Flow)
        return;
    auto watch = m_Context->Evaluate(pin.m_Id);
    if (!watch)
        return;

    // Draw to layer over nodes
    m_Splitter.SetCurrentChannel(m_DrawList, 1);

    // Save current layout to avoid conflicts with node measurements
    ImEx::ScopedSuspendLayout suspendLayout;

    // Put cursor on the left side of the Pin
    auto itemRectMin = ImGui::GetItemRectMin();
    ImGui::SetCursorScreenPos(ImVec2(itemRectMin.x, itemRectMin.y)
        - ImVec2(1.75f * ImGui::GetStyle().ItemSpacing.x, 0.0f));

    // Remember current vertex, later we will patch generated mesh so drawn value
    // will be aligned to the right side of the pin.
    auto vertexStartIdx = m_DrawList->_CmdHeader.VtxOffset + m_DrawList->_VtxCurrentIdx;

    auto isCurrentNode = m_Current == pin.m_Node;
    auto color = ImGui::GetStyleColorVec4(isCurrentNode ? ImGuiCol_PlotHistogram : ImGuiCol_NavCursor);

    // Actual drawing
    PinValueBackgroundRenderer bg(color, 0.5f);
    if (!DrawValue(watch.GetValue().m_Value))
        bg.Discard();
    bg.Commit();

    // Move generated mesh to the left
    auto itemWidth = ImGui::GetItemRectSize().x;
    auto vertexEndIdx = m_DrawList->_CmdHeader.VtxOffset + m_DrawList->_VtxCurrentIdx;
    for (auto vertexIdx = vertexStartIdx; vertexIdx < vertexEndIdx; ++vertexIdx)
        m_DrawList->VtxBuffer[vertexIdx].pos.x -= itemWidth;

    // Switch back to normal layer
    m_Splitter.SetCurrentChannel(m_DrawList, 0);
}

void blueprint_editor_utilities::DebugOverlay::DrawOutputPin(const Pin& pin)
{
    if (!m_Context || pin.m_Flow)
        return;
    auto watch = m_Context->Evaluate(pin.m_Id);
    if (!watch)
        return;

    // Draw to layer over nodes
    m_Splitter.SetCurrentChannel(m_DrawList, 1);

    ImEx::ScopedSuspendLayout suspendLayout;

    // Put cursor on the right side of the Pin
    auto itemRectMin = ImGui::GetItemRectMin();
    auto itemRectMax = ImGui::GetItemRectMax();
    ImGui::SetCursorScreenPos(ImVec2(itemRectMax.x, itemRectMin.y)
        + ImVec2(1.75f * ImGui::GetStyle().ItemSpacing.x, 0.0f));

    auto isCurrentNode = m_Current == pin.m_Node;
    auto color = ImGui::GetStyleColorVec4(isCurrentNode ? ImGuiCol_PlotHistogram : ImGuiCol_NavCursor);

    // Actual drawing
    PinValueBackgroundRenderer bg(color, 0.5f);
    if (!DrawValue(watch.GetValue().m_Value))
        bg.Discard();
    bg.Commit();

    // Switch back to normal layer
    m_Splitter.SetCurrentChannel(m_DrawList, 0);
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
    m_Revision  = std::nullopt;
    m_Focus     = true;
    m_Search[0] = 0;
    ImGui::OpenPopup("##create_node");
}

void blueprint_editor_utilities::CreateNodeDialog::Show(Document& document)
{
    if (!ImGui::IsPopupOpen("##create_node"))
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
    if (auto from = m_FromPin ? graph.FindPin(m_FromPin) : nullptr)
        ImGui::TextDisabled("From %s pin", from->m_Flow ? "flow" : graph.TypeText(from->m_Id).c_str());

    const NewNodeFit* chosen = nullptr;
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
            if (ShowType(*entry) || (enter && i == 0))
                chosen = entry;
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
            if (open && ShowType(entry))
                chosen = &entry;
        }
        if (open)
            ImGui::TreePop();
        ImGui::PopID();
    }
    ImGui::EndChild();

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

    auto types = document.GetBlueprint().GetRegistry().NodeTypes();
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
    auto order = [](const NewNodeFit& entry)
    {
        const auto cast = !entry.m_Cast.empty();
        return std::tuple(cast, cast ? std::string_view() : std::string_view(entry.m_Type->m_Category), std::string_view(entry.m_Type->m_Title));
    };
    std::stable_sort(m_Entries.begin(), m_Entries.end(), [&order](const NewNodeFit& lhs, const NewNodeFit& rhs) { return order(lhs) < order(rhs); });
}

bool blueprint_editor_utilities::CreateNodeDialog::ShowType(const NewNodeFit& entry)
{
    ImGui::PushID(entry.m_Type);
    const auto chosen = ImGui::Selectable(entry.m_Type->m_Title.c_str());
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

void blueprint_editor_utilities::NodeContextMenu::Show(Document& document, Debugger& debugger)
{
    if (!ImGui::IsPopupOpen("##node-context-menu"))
        return;

    auto node = document.GetGraph().FindNode(m_Node);

    if (ImGui::BeginPopup("##node-context-menu"))
    {
        if (ImGui::MenuItem("Breakpoint", "F9", debugger.HasBreakpoint(m_Node), node != nullptr))
        {
            const auto enabled = !debugger.HasBreakpoint(m_Node);
            if (!ax::NodeEditor::IsNodeSelected(m_Node))
                debugger.SetBreakpoint(m_Node, enabled);
            else
                for (auto selected : GetSelectedNodes())
                    debugger.SetBreakpoint(selected, enabled);
        }

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
    }
}




void blueprint_editor_utilities::PinContextMenu::Open(Id pin)
{
    m_Pin = pin;
    ImGui::OpenPopup("##pin-context-menu");
}

void blueprint_editor_utilities::PinContextMenu::Show(Document& document)
{
    if (!ImGui::IsPopupOpen("##pin-context-menu"))
        return;

    auto& graph = document.GetGraph();
    auto  pin   = graph.FindPin(m_Pin);
    auto  node  = graph.NodeOf(m_Pin);

    if (ImGui::BeginPopup("##pin-context-menu"))
    {
        if (ImGui::MenuItem("Break Links", nullptr, nullptr, pin != nullptr && ax::NodeEditor::HasAnyLinks(ax::NodeEditor::PinId(m_Pin))))
            ax::NodeEditor::BreakLinks(ax::NodeEditor::PinId(m_Pin));

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

        ImGui::EndPopup();
    }
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

    if (ImGui::BeginPopup("##link-context-menu"))
    {
        if (link && !link->m_Active)
        {
            auto reason = link->m_Reason.Message();
            ImGui::TextDisabled("Disabled: %.*s", static_cast<int>(reason.size()), reason.data());
            ImGui::Separator();
        }

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
