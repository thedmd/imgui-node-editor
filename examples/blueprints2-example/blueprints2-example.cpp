# define IMGUI_DEFINE_MATH_OPERATORS
# include <application.h>
# include <imgui_internal.h>
# include <tinyfiledialogs.h>

# include "crude_logger.h"
# include "crude_blueprint.h"
# include "blueprint_editor_document.h"
# include "blueprint_editor_utilities.h"
# include "blueprint_editor_icons.h"
# include "crude_layout.h"
# include "imgui_extras.h"
# include "imgui_node_editor.h"

# include "crude_json.h"

# include <algorithm>
# include <cstdio>
# include <functional>
# include <map>
# include <memory>
# include <optional>
# include <string>
# include <string_view>
# include <type_traits>
# include <unordered_set>
# include <utility>
# include <vector>

namespace ed = ax::NodeEditor;

using namespace crude_blueprint;
using namespace blueprint_editor;
using namespace blueprint_editor_utilities;


static ImEx::MostRecentlyUsedList Application_GetMostRecentlyOpenFileList()
{
    return ImEx::MostRecentlyUsedList("MostRecentlyOpenList");
}

static std::shared_ptr<TypeTable> ExampleTypes()
{
    static const auto types = std::make_shared<TypeTable>();
    return types;
}

static bool HasStart(const Blueprint& blueprint)
{
    for (auto& graph : blueprint.Graphs())
        for (auto& node : graph->Nodes())
            if (node.m_Type->EventName(node.m_Config) == "Start")
                return true;
    return false;
}

static std::string Text(std::string_view text)
{
    return std::string(text);
}



enum class EventHandle : uint64_t { Invalid };

template <typename... Args>
struct Event
{
    using Delegate = std::function<void(Args...)>;

    EventHandle Add(Delegate delegate)
    {
        auto eventHandle = static_cast<EventHandle>(++m_LastHandleId);
        m_Delegates[eventHandle] = std::move(delegate);
        return eventHandle;
    }

    bool Remove(EventHandle eventHandle)
    {
        return m_Delegates.erase(eventHandle) > 0;
    }

    void Clear()
    {
        m_Delegates.clear();
    }

    template <typename... CallArgs>
    void Invoke(CallArgs&&... args)
    {
        std::vector<Delegate> delegates;
        delegates.reserve(m_Delegates.size());
        for (auto& entry : m_Delegates)
            delegates.push_back(entry.second);

        for (auto& delegate : delegates)
            delegate(args...);
    }

    EventHandle operator += (Delegate delegate)       { return Add(std::move(delegate)); }
    bool        operator -= (EventHandle eventHandle) { return Remove(eventHandle);      }
    template <typename... CallArgs>
    void        operator () (CallArgs&&... args)      { Invoke(std::forward<CallArgs>(args)...); }

private:
    using EventHandleType = std::underlying_type_t<EventHandle>;

    std::map<EventHandle, Delegate> m_Delegates;
    EventHandleType                 m_LastHandleId = 0;
};





struct Action
{
    using OnChangeEvent     = Event<Action*>;
    using OnTriggeredEvent  = Event<>;

    Action() = default;
    Action(std::string_view name, OnTriggeredEvent::Delegate delegate = {});

    void SetName(std::string_view name);
    const std::string& GetName() const;

    void SetEnabled(bool set);
    bool IsEnabled() const;

    void Execute();

    OnChangeEvent       OnChange;
    OnTriggeredEvent    OnTriggered;

private:
    std::string m_Name;
    bool        m_IsEnabled = true;
};

Action::Action(std::string_view name, OnTriggeredEvent::Delegate delegate)
    : m_Name(name)
{
    if (delegate)
        OnTriggered += std::move(delegate);
}

void Action::SetName(std::string_view name)
{
    if (m_Name == name)
        return;

    m_Name = std::string(name);

    OnChange(this);
}

const std::string& Action::GetName() const
{
    return m_Name;
}

void Action::SetEnabled(bool set)
{
    if (m_IsEnabled == set)
        return;

    m_IsEnabled = set;

    OnChange(this);
}

bool Action::IsEnabled() const
{
    return m_IsEnabled;
}

void Action::Execute()
{
    LOGV("Action: %s", m_Name.c_str());
    OnTriggered();
}



struct BlueprintEditorExample
    : Application
{
    using Application::Application;

    // Each graph of a document has its own editor, made when the graph first shows, so nothing of
    // one shows in another. The document keeps the layout; the editors stay out of imgui.ini.
    ed::EditorContext* EditorFor(Id graph)
    {
        auto& editor = m_Editors[graph];
        if (!editor)
        {
            ed::Config config;
            config.SettingsName = nullptr;
            editor = ed::CreateEditor(&config);
        }
        return editor;
    }

    void DestroyEditors()
    {
        for (auto& [graph, editor] : m_Editors)
            ed::DestroyEditor(editor);
        m_Editors.clear();
        m_History.clear();
        m_HistoryIndex = 0;
        m_TabRequest   = 0;
        m_PendingGraph = 0;
        m_PendingNode  = 0;
        m_FollowedNode = 0;
    }

    // The document's editor current, for the graph it shows, which is the first of navigation.
    void MakeDocumentCurrent()
    {
        ed::SetCurrentEditor(EditorFor(m_Document->GetGraph().GetId()));
        m_History      = { m_Document->GetGraph().GetId() };
        m_HistoryIndex = 0;
    }

    // Before the editor begins, the shown graph's editor current. When the function's graph showed,
    // the first graph shows. Then the function's graph's editor goes.
    void DeleteFunction(Id graph)
    {
        auto& document = *m_Document;
        const auto shown = document.GetGraph().GetId() == graph;
        RemoveFunction(document, graph);
        if (document.GetBlueprint().FindGraph(graph))
            return;
        if (shown)
        {
            document.m_ShownGraph = document.GetBlueprint().Graphs().front()->GetId();
            ed::SetCurrentEditor(EditorFor(document.m_ShownGraph));
            document.OnMakeCurrent();
            m_TabRequest = document.m_ShownGraph;
        }
        if (auto editor = m_Editors.find(graph); editor != m_Editors.end())
        {
            ed::DestroyEditor(editor->second);
            m_Editors.erase(editor);
        }
    }

    // Before the editor begins: the graph's editor becomes current, with the graph's layout and view.
    void ShowGraph(Id graph, bool remember = true)
    {
        auto& document = *m_Document;
        if (graph == document.GetGraph().GetId() || !document.GetBlueprint().FindGraph(graph))
            return;
        document.CaptureView();
        document.m_ShownGraph = graph;
        ed::SetCurrentEditor(EditorFor(graph));
        document.OnMakeCurrent();
        m_TabRequest = graph;
        if (remember)
        {
            m_History.resize(m_HistoryIndex + 1);
            m_History.push_back(graph);
            m_HistoryIndex = m_History.size() - 1;
        }
    }

    // The edits the user completed in a frame make one undo step. Navigation isn't reported.
    void RecordEdits(Document& document)
    {
        if (!ed::BeginEdits())
            return;

        auto transaction = document.BeginUndoTransaction("Edit");
        auto& graph = document.GetGraph();

        ed::Edit edit;
        while (ed::QueryEdit(&edit))
        {
            auto node = edit.NodeCount > 0 ? graph.FindNode(static_cast<Id>(edit.NodesAfter[0].Id.Get())) : nullptr;
            switch (edit.Type)
            {
                case ed::EditType::Move:
                    if (edit.NodeCount == 1 && node)
                        transaction->AddAction("Move %s", node->Name().c_str());
                    else
                        transaction->AddAction("Move %d nodes", edit.NodeCount);
                    break;

                case ed::EditType::Resize:
                    if (node)
                        transaction->AddAction("Resize %s", node->Name().c_str());
                    else
                        transaction->AddAction("Resize");
                    break;

                case ed::EditType::Select:
                    if (edit.SelectionAfter.NodeCount > 0)
                        transaction->AddAction("Select %d nodes", edit.SelectionAfter.NodeCount);
                    else if (edit.SelectionAfter.LinkCount > 0)
                        transaction->AddAction("Select %d links", edit.SelectionAfter.LinkCount);
                    else
                        transaction->AddAction("Clear selection");
                    break;

                case ed::EditType::Navigate:
                    break;
            }
        }

        ed::EndEdits();
    }

    void CreateExampleDocument()
    {
        m_Document = std::make_unique<Document>(ExampleRegistry(), ExampleTypes());
        MakeDocumentCurrent();
        CreateExampleBlueprint(*m_Document);
    }

    void OnStart() override
    {
        crude_logger::OverlayLogger::SetCurrent(&m_OverlayLogger);

        m_OverlayLogger.AddKeyword("Node");
        m_OverlayLogger.AddKeyword("Pin");
        m_OverlayLogger.AddKeyword("Link");
        m_OverlayLogger.AddKeyword("CreateNodeDialog");
        m_OverlayLogger.AddKeyword("NodeContextMenu");
        m_OverlayLogger.AddKeyword("PinContextMenu");
        m_OverlayLogger.AddKeyword("LinkContextMenu");
        m_OverlayLogger.AddKeyword("Print");

        ImEx::MostRecentlyUsedList::Install(ImGui::GetCurrentContext());

        CreateExampleDocument();

        for (auto type : ExampleRegistry()->NodeTypes())
            m_OverlayLogger.AddKeyword(type->m_Title);

        ed::SetCurrentEditor(nullptr);
    }

    void OnStop() override
    {
        DestroyEditors();
    }

    void OnFrame(float deltaTime) override
    {
        ClosePopupsOnClickOutside();
        ed::SetCurrentEditor(m_Document ? EditorFor(m_Document->GetGraph().GetId()) : nullptr);
        if (auto graph = std::exchange(m_PendingDelete, 0); graph && m_Document)
            DeleteFunction(graph);

        if (m_Document)
        {
            m_Debugger.Advance(deltaTime);
            m_Debugger.Update(*m_Document);
            m_Problems.Update(*m_Document, m_Debugger);

            // A paused run shows the graph of the node it runs next.
            auto next = m_Debugger.IsActive() && !m_Debugger.IsRunning() ? m_Debugger.NextNode() : 0;
            if (next != m_FollowedNode)
            {
                m_FollowedNode = next;
                if (auto graph = GraphOf(m_Document->GetBlueprint(), next); graph && graph->GetId() != m_Document->GetGraph().GetId())
                    m_PendingGraph = graph->GetId();
            }
            if (auto graph = std::exchange(m_PendingGraph, 0))
                ShowGraph(graph, std::exchange(m_PendingRemember, true));
            if (auto node = std::exchange(m_PendingNode, 0); node && m_Document->GetGraph().FindNode(node))
                GoToNode(node);

            // A run's error opens its node's bubble, once in each run.
            auto running = false;
            for (auto& problem : m_Problems.Problems())
            {
                if (!problem.m_Run)
                    continue;
                running = true;
                if (problem.m_Node && m_RunBubbles.insert(problem.m_Node).second)
                    m_Bubbles.insert(problem.m_Node);
            }
            if (!running)
                m_RunBubbles.clear();
        }
        UpdateActions();
        UpdateTitle();

        ShowMainMenu();
        ShowToolbar();

        ImGui::Separator();

        ImVec2 canvasMin, canvasMax;
        if (m_Document)
        {
            ShowGraphTabs(*m_Document);

            auto& style  = ImGui::GetStyle();
            auto  strip  = m_ShowPanels ? ImGui::GetTextLineHeightWithSpacing() * 5.0f + ImGui::GetFrameHeightWithSpacing() + style.WindowPadding.y * 2.0f : 0.0f;
            auto  canvas = ImGui::GetContentRegionAvail().y - (m_ShowPanels ? strip + style.ItemSpacing.y : 0.0f);
            ed::Begin("###main_editor", ImVec2(0.0f, ImMax(canvas, 1.0f)));

            CommitBlueprintNodes(*m_Document);

            HandleCreateAction(*m_Document);
            HandleDestroyAction(*m_Document);
            HandleContextMenuAction();

            ShowDialogs(*m_Document);

            ShowInfoTooltip(*m_Document);

            ed::End();
            canvasMin = ImGui::GetItemRectMin();
            canvasMax = ImGui::GetItemRectMax();

            ShowBreakpoints(*m_Document, canvasMin, canvasMax);
            ShowPendingLink(*m_Document, canvasMin, canvasMax);
            ShowRunOverlay(*m_Document, canvasMin, canvasMax);
            ShowBubbles(*m_Document, canvasMin, canvasMax);
            DeclareActions(*m_Document, canvasMin, canvasMax);
            if (m_ShowPanels)
                ShowPanels(*m_Document, ImVec2(canvasMin.x, canvasMax.y + style.ItemSpacing.y), ImVec2(canvasMax.x - canvasMin.x, strip));

            RecordEdits(*m_Document);
        }
        else
        {
            ImGui::Dummy(ImGui::GetContentRegionAvail());
            canvasMin = ImGui::GetItemRectMin();
            canvasMax = ImGui::GetItemRectMax();
        }

        m_OverlayLogger.Update(ImGui::GetIO().DeltaTime);
        m_OverlayLogger.Draw(canvasMin, canvasMax);

        ed::SetCurrentEditor(nullptr);
    }

    ImGuiWindowFlags GetWindowFlags() const override
    {
        auto flags = Application::GetWindowFlags();
        flags |= ImGuiWindowFlags_MenuBar;
        flags &= ~ImGuiWindowFlags_NoSavedSettings;
        return flags;
    }

    bool CanClose() override
    {
        return File_Close();
    }

private:
    void UpdateTitle()
    {
        std::string title = GetName();

        if (File_IsOpen())
        {
            title += " - ";
            title += m_Document->m_Name.c_str();

            if (File_IsModified())
                title += "*";
        }

        if (title == m_Title)
            return;

        m_Title = title;
        SetTitle(title.c_str());
    }

    void UpdateActions()
    {
        auto hasDocument = File_IsOpen();
        auto hasUndo     = hasDocument && !m_Document->m_Undo.empty();
        auto hasRedo     = hasDocument && !m_Document->m_Redo.empty();

        m_File_Close.SetEnabled(hasDocument);
        m_File_SaveAs.SetEnabled(hasDocument);
        m_File_Save.SetEnabled(hasDocument);

        m_Edit_Undo.SetEnabled(hasUndo);
        m_Edit_Redo.SetEnabled(hasRedo);
        m_Edit_Cut.SetEnabled(hasDocument && false);
        m_Edit_Copy.SetEnabled(hasDocument && false);
        m_Edit_Paste.SetEnabled(hasDocument && false);
        m_Edit_Duplicate.SetEnabled(hasDocument && false);
        m_Edit_Delete.SetEnabled(hasDocument && false);
        m_Edit_SelectAll.SetEnabled(hasDocument && false);

        m_View_NavigateBackward.SetEnabled(hasDocument && m_HistoryIndex > 0);
        m_View_NavigateForward.SetEnabled(hasDocument && m_HistoryIndex + 1 < m_History.size());
        m_Blueprint_AddFunction.SetEnabled(hasDocument);
        m_Blueprint_AddVariable.SetEnabled(hasDocument);

        // An error blocks a run, never an edit or saving.
        auto canStart = hasDocument && HasStart(m_Document->GetBlueprint()) && !m_Problems.BlocksRun();

        m_Blueprint_Start.SetEnabled(canStart);
        m_Blueprint_Step.SetEnabled(m_Debugger.IsActive() ? !m_Debugger.IsRunning() : canStart);
        m_Blueprint_Stop.SetEnabled(m_Debugger.IsActive());
        m_Blueprint_Run.SetEnabled(canStart && !m_Debugger.IsRunning());
    }

    void ShowMainMenu()
    {
        auto menuAction = [](Action& action)
        {
            if (ImGui::MenuItem(action.GetName().c_str(), nullptr, nullptr, action.IsEnabled()))
            {
                action.Execute();
            }
        };

        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                auto mostRecentlyOpenFiles = Application_GetMostRecentlyOpenFileList();

                menuAction(m_File_New);
                ImGui::Separator();
                menuAction(m_File_Open);
                if (ImGui::BeginMenu("Open Recent", !mostRecentlyOpenFiles.GetList().empty()))
                {
                    // Opening a file moves it to the front of the list.
                    std::string clickedPath;
                    for (auto& entry : mostRecentlyOpenFiles.GetList())
                        if (ImGui::MenuItem(entry.c_str()))
                            clickedPath = entry;
                    ImGui::Separator();
                    if (ImGui::MenuItem("Clear Recently Opened"))
                        mostRecentlyOpenFiles.Clear();
                    ImGui::EndMenu();

                    if (!clickedPath.empty())
                        File_Open(clickedPath.c_str());
                }
                ImGui::Separator();
                menuAction(m_File_SaveAs);
                menuAction(m_File_Save);
                ImGui::Separator();
                menuAction(m_File_Close);
                ImGui::Separator();
                menuAction(m_File_Exit);

                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Edit"))
            {
                menuAction(m_Edit_Undo);
                menuAction(m_Edit_Redo);
                ImGui::Separator();
                menuAction(m_Edit_Cut);
                menuAction(m_Edit_Copy);
                menuAction(m_Edit_Paste);
                menuAction(m_Edit_Duplicate);
                menuAction(m_Edit_Delete);
                ImGui::Separator();
                menuAction(m_Edit_SelectAll);

                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("View"))
            {
                menuAction(m_View_NavigateBackward);
                menuAction(m_View_NavigateForward);
                ImGui::Separator();
                ImGui::MenuItem("Show Actions", nullptr, &m_ShowActions);
                ImGui::MenuItem("Show Panels", nullptr, &m_ShowPanels);
                if (ImGui::MenuItem("Show or Hide All Problems", "Ctrl+Shift+D", false, File_IsOpen() && !m_Problems.Problems().empty()))
                    ToggleBubbles();

                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Blueprint"))
            {
                menuAction(m_Blueprint_Start);
                menuAction(m_Blueprint_Step);
                menuAction(m_Blueprint_Stop);
                ImGui::Separator();
                menuAction(m_Blueprint_Run);
                ImGui::Separator();
                menuAction(m_Blueprint_AddFunction);
                menuAction(m_Blueprint_AddVariable);

                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }
    }

    void ShowToolbar()
    {
        auto toolbarAction = [](Action& action)
        {
            ImEx::ScopedDisableItem disableAction(!action.IsEnabled());

            if (ImGui::Button(action.GetName().c_str()))
            {
                action.Execute();
            }
        };

        toolbarAction(m_File_Open);
        ImGui::SameLine();
        toolbarAction(m_File_Save);
        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();
        toolbarAction(m_Edit_Undo);
        ImGui::SameLine();
        toolbarAction(m_Edit_Redo);
        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();
        toolbarAction(m_Blueprint_Start);
        ImGui::SameLine();
        toolbarAction(m_Blueprint_Step);
        ImGui::SameLine();
        toolbarAction(m_Blueprint_Stop);
        ImGui::SameLine();
        toolbarAction(m_Blueprint_Run);

        if (!m_Document)
            return;

        auto errors = m_Problems.Count(Severity::Error);

        ImGui::SameLine();
        if (errors > 0)
            ImGui::Text("Status: %d error%s", errors, errors != 1 ? "s" : "");
        else if (m_Debugger.IsRunning())
            ImGui::TextUnformatted(m_Debugger.Waiting() ? "Status: suspended" : "Status: running");
        else if (m_Debugger.IsActive())
            ImGui::TextUnformatted(m_Debugger.IsAtBreakpoint() ? "Status: paused at a breakpoint" : "Status: paused");
        else
            ImGui::TextUnformatted("Status: -");

        if (m_Debugger.IsActive())
        {
            auto& blueprint = m_Document->GetBlueprint();
            auto  current   = FindNode(blueprint, m_Debugger.CurrentNode());
            auto  next      = FindNode(blueprint, m_Debugger.NextNode());
            ImGui::SameLine(); ImGui::Spacing();
            ImGui::SameLine(); ImGui::Text("Current: %s", current ? current->Name().c_str() : "-");
            ImGui::SameLine(); ImGui::Spacing();
            ImGui::SameLine(); ImGui::Text("Next: %s", next ? next->Name().c_str() : "-");
            if (auto waiting = m_Debugger.Waiting())
            {
                ImGui::SameLine(); ImGui::Spacing();
                ImGui::SameLine(); ImGui::Text("Waits: %s", WakeText(*waiting, m_Debugger.GetContext()->Time()).c_str());
            }
        }
    }

    // Iterate over the graph's nodes and commit them to node editor.
    void CommitBlueprintNodes(Document& document)
    {
        const auto iconSize = ImVec2(ImGui::GetTextLineHeight(), ImGui::GetTextLineHeight());

        auto& graph = document.GetGraph();

        // A pin with an active link has a filled icon, and an input with one, no value. An input
        // whose link is disabled has its icon slashed, and one linked through a cast has it split:
        // the source's color on the left, its own on the right.
        std::unordered_set<Id> linked, disabled;
        std::unordered_map<Id, ImVec4> castFrom;
        for (auto& link : graph.Links())
        {
            if (!link.m_Active)
            {
                disabled.insert(link.m_To);
                continue;
            }
            linked.insert(link.m_From);
            linked.insert(link.m_To);
            if (!link.m_Cast.empty())
                castFrom[link.m_To] = PinColor(*graph.FindPin(link.m_From));
        }

        // An edit made with a node's widgets, done once the nodes are drawn.
        std::function<void()> edit;
        m_PinIcons.clear();

        // Where the run's fibers wait: at their nodes, and in the calls they're in.
        std::unordered_map<Id, std::string> waits;
        if (auto context = m_Debugger.GetContext())
        {
            for (auto& fiber : context->Fibers())
            {
                if (fiber.m_State != FiberState::Suspended)
                    continue;
                auto text = WakeText(fiber.m_Wake, context->Time());
                for (auto call : fiber.m_Calls)
                    waits[call] = text;
                waits[fiber.m_Node] = text;
            }
        }

        // A paused run's next node has a blue border, and the node its last step ran an orange one.
        // The example draws the borders, over the header bands, on the nodes' outlines.
        const auto current = m_Debugger.IsActive() ? m_Debugger.CurrentNode() : 0;
        const auto next    = m_Debugger.IsActive() ? m_Debugger.NextNode() : 0;
        ed::PushStyleVar(ed::StyleVar_NodeBorderWidth, 0.0f);
        for (auto& node : graph.Nodes())
        {
            const auto errors   = m_Problems.Count(Severity::Error, node.m_Id);
            const auto warnings = m_Problems.Count(Severity::Warning, node.m_Id);
            const auto stepped  = node.m_Id == next || node.m_Id == current;
            auto borderColor = ImGui::ColorConvertFloat4ToU32(ed::GetStyle().Colors[ed::StyleColor_NodeBorder]);
            auto borderWidth = 1.0f;
            if (stepped)
            {
                borderColor = ImGui::GetColorU32(node.m_Id == next ? ImGuiCol_NavCursor : ImGuiCol_PlotHistogram);
                borderWidth = 3.0f;
            }
            else if (errors > 0)
                borderColor = ProblemColor(Severity::Error);

            ed::BeginNode(node.m_Id);

            // General node layout:
            //
            // +-----------------------------------+
            // | Title                             |
            // | +-----------[ Dummy ]-----------+ |
            // | +---------------+   +-----------+ |
            // | | o Pin         |   |   Out B o | |
            // | | o Pin <Value> |   |   Out A o | |
            // | | o Pin         |   |           | |
            // | +---------------+   +-----------+ |
            // +-----------------------------------+

            // An event's node is titled by the event's name.
            const auto event = node.m_Type->EventName(node.m_Config);
            auto title = event.empty() ? node.m_Type->m_Title : event;
            if (auto owner = SignatureNodeOf(document.GetBlueprint(), node); owner.m_Function && owner.m_Entry && !owner.m_Function->m_Signature.m_Parameters.empty())
            {
                // A generic function's entry names its type parameters.
                auto& parameters = owner.m_Function->m_Signature.m_Parameters;
                for (size_t i = 0; i < parameters.size(); ++i)
                    title += (i ? ", " : " <") + parameters[i].m_Name;
                title += ">";
            }

            // A band of a tint of what the node is, white one fainter.
            auto tint = HeaderColor(document.GetBlueprint(), node);
            tint.w    = tint.x + tint.y + tint.z >= 3.0f ? 0.25f : 0.55f;
            auto band = ImGui::GetColorU32(tint);
            auto bandBottom = 0.0f;
            {
                ImGui::BeginGroup();
                ImGui::PushFont(HeaderFont(), HeaderFont()->LegacySize);
                ImGui::TextUnformatted(title.data(), title.data() + title.size());
                ImGui::PopFont();
                if (errors > 0 || warnings > 0)
                {
                    ImGui::SameLine();
                    if (ProblemPills(errors, warnings) && !m_Bubbles.erase(node.m_Id))
                        m_Bubbles.insert(node.m_Id);
                }
                auto wait = waits.find(node.m_Id);
                if (wait != waits.end() || document.GetBlueprint().MaySuspend(*node.m_Type))
                {
                    ImGui::SameLine();
                    if (LatentMark(wait != waits.end() ? wait->second : std::string()))
                    {
                        ed::Suspend();
                        ImGui::SetTooltip("May suspend its fiber");
                        ed::Resume();
                    }
                }
                ImGui::EndGroup();
                bandBottom = ImGui::GetItemRectMax().y + ImGui::GetStyle().ItemSpacing.y;

                ImGui::Spacing();
            }

            ImGui::Dummy(ImVec2(100.0f, 0.0f)); // For minimum node width

            crude_layout::Grid layout;
            layout.Begin(node.m_Id, 2, 100.0f);
            layout.SetColumnAlignment(0.0f);

            // A group's [+] and [-] follow its last pin, or end the inputs while it has none.
            std::map<const GroupDeclaration*, const Pin*> lastOfGroup;
            for (auto& pin : node.m_Pins)
                if (auto group = FindGroup(node, pin.m_Key))
                    lastOfGroup[group] = &pin;
            auto groupButtons = [&](const GroupDeclaration& group)
            {
                const auto count = GroupCount(group, node.m_Config);
                ImGui::Spacing();
                ImGui::PushID(group.m_Name.c_str());
                ImGui::BeginDisabled(count >= group.m_Max);
                if (SquareButton("+"))
                    edit = [&document, id = node.m_Id, name = group.m_Name] { AddGroupPin(document, id, name); };
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::BeginDisabled(count <= group.m_Min);
                if (SquareButton("-"))
                    edit = [&document, id = node.m_Id, name = group.m_Name, index = count - 1] { RemoveGroupPin(document, id, name, index); };
                ImGui::EndDisabled();
                ImGui::PopID();
                layout.NextRow();
            };
            auto groupButtonsAfter = [&](const Pin& pin)
            {
                for (auto& [group, last] : lastOfGroup)
                    if (last == &pin)
                        groupButtons(*group);
            };

            // Draw column with input pins.
            for (auto& pin : node.m_Pins)
            {
                if (pin.m_Direction != Direction::Input)
                    continue;

                // Add a bit of spacing to separate pins and make value not cramped
                ImGui::Spacing();

                // Input pin layout:
                //
                //     +-[1]---+-[2]------+-[3]----------+
                //     |       |          |              |
                //    [X] Icon | Pin Name | Value/Editor |
                //     |       |          |              |
                //     +-------+----------+--------------+

                ed::BeginPin(pin.m_Id, ed::PinKind::Input);
                // [X] - Tell editor to put pivot point in the middle of
                //       the left side of the pin. This is the point
                //       where link will be hooked to.
                //
                //       By default pivot is in pin center point which
                //       does not look good for blueprint nodes.
                ed::PinPivotAlignment(ImVec2(0.0f, 0.5f));

                // [1] - Icon
                const auto isLinked = linked.count(pin.m_Id) > 0;
                if (auto cast = castFrom.find(pin.m_Id); cast != castFrom.end())
                {
                    auto iconMin = ImGui::GetCursorScreenPos();
                    DrawSplitIcon(ImGui::GetWindowDrawList(), iconMin, iconMin + iconSize, PinIcon(pin), ImColor(cast->second), ImColor(PinColor(pin)));
                    ImGui::Dummy(iconSize);
                }
                else
                    Icon(iconSize, PinIcon(pin), isLinked, PinColor(pin));
                m_PinIcons[pin.m_Id] = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
                if (disabled.contains(pin.m_Id))
                {
                    auto iconMin = ImGui::GetItemRectMin(), iconMax = ImGui::GetItemRectMax();
                    ImGui::GetWindowDrawList()->AddLine(ImVec2(iconMin.x + 1.0f, iconMax.y - 1.0f), ImVec2(iconMax.x - 1.0f, iconMin.y + 1.0f), IM_COL32(255, 96, 96, 255), 2.0f);
                }

                // [2] - Show pin name if it has one
                if (!pin.m_Name.empty())
                {
                    ImGui::SameLine();
                    ImGui::TextUnformatted(pin.m_Name.data(), pin.m_Name.data() + pin.m_Name.size());
                }

                // [3] - Show value/editor when pin is not linked to anything
                if (HasValueEditor(graph, pin))
                {
                    ImGui::SameLine();
                    auto value = InputValue(graph, pin);
                    if (DrawValueWithEditor(pin.m_Id, value))
                        edit = [&document, id = pin.m_Id, value] { SetPinValue(document, id, value); };
                }
                else if (auto kept = KeptValue(graph, pin); kept.IsValid())
                {
                    ImGui::SameLine();
                    ImGui::BeginDisabled();
                    DrawValue(kept);
                    ImGui::EndDisabled();
                }

                ed::EndPin();

                layout.NextRow();
                groupButtonsAfter(pin);
            }
            for (auto& group : node.m_Groups)
                if (!lastOfGroup.contains(&group))
                    groupButtons(group);

            // A function's return adds its outputs, and its entry its inputs, with [+] at their end.
            const auto signature    = SignatureNodeOf(document.GetBlueprint(), node);
            auto       addPinButton = [&](bool input)
            {
                ImGui::Spacing();
                if (SquareButton("+"))
                    edit = [&document, graph = signature.m_Function->m_Graph, input] { AddSignaturePin(document, graph, input); };
                if (ImGui::IsItemHovered())
                {
                    ed::Suspend();
                    ImGui::SetTooltip(input ? "Add Input" : "Add Output");
                    ed::Resume();
                }
                layout.NextRow();
            };
            if (signature.m_Function && !signature.m_Entry)
                addPinButton(false);

            layout.SetColumnAlignment(1.0f);
            layout.NextColumn();

            // Draw column with output pins.
            for (auto& pin : node.m_Pins)
            {
                if (pin.m_Direction != Direction::Output)
                    continue;

                // Add a bit of spacing to separate pins and make value not cramped
                ImGui::Spacing();

                // Output pin layout:
                //
                //    +-[1]------+-[2]---+
                //    |          |       |
                //    | Pin Name | Icon [X]
                //    |          |       |
                //    +----------+-------+

                // A Constant shows its value, and an editor once clicked, before its pin.
                if (HasValueEditor(graph, pin))
                {
                    auto value = ConstantValue(node, pin);
                    if (DrawValueWithEditor(pin.m_Id, value))
                        edit = [&document, id = pin.m_Id, value] { SetPinValue(document, id, value); };
                    ImGui::SameLine();
                }
                else if (auto kept = KeptValue(graph, pin); kept.IsValid())
                {
                    ImGui::BeginDisabled();
                    DrawValue(kept);
                    ImGui::EndDisabled();
                    ImGui::SameLine();
                }

                ed::BeginPin(pin.m_Id, ed::PinKind::Output);

                // [X] - Tell editor to put pivot point in the middle of
                //       the right side of the pin. This is the point
                //       where link will be hooked to.
                //
                //       By default pivot is in pin center point which
                //       does not look good for blueprint nodes.
                ed::PinPivotAlignment(ImVec2(1.0f, 0.5f));

                // [1] - Show pin name if it has one
                if (!pin.m_Name.empty())
                {
                    ImGui::TextUnformatted(pin.m_Name.data(), pin.m_Name.data() + pin.m_Name.size());
                    ImGui::SameLine();
                }

                // [2] - Show icon
                Icon(iconSize, PinIcon(pin), linked.count(pin.m_Id) > 0, PinColor(pin));
                m_PinIcons[pin.m_Id] = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

                ed::EndPin();

                layout.NextRow();
                groupButtonsAfter(pin);
            }
            if (signature.m_Function && signature.m_Entry)
                addPinButton(true);

            layout.End();

            ed::EndNode();

            if (auto drawList = ed::GetNodeBackgroundDrawList(node.m_Id))
            {
                const auto min      = ed::GetNodePosition(node.m_Id);
                const auto max      = min + ed::GetNodeSize(node.m_Id);
                const auto rounding = ed::GetStyle().NodeRounding;
                drawList->AddRectFilled(min, ImVec2(max.x, bandBottom), band, rounding, ImDrawFlags_RoundTop);
                drawList->AddRect(min, max, borderColor, rounding, borderWidth);
            }
        }
        ed::PopStyleVar();

        // While a run goes on, a flow link it took in its last steps is thicker, the more so the more
        // recently, but for the one it took last, which flows. A run that ended keeps its context, for
        // its values, not its trace.
        std::unordered_map<Id, uint64_t> taken;
        if (auto context = m_Debugger.IsActive() ? m_Debugger.GetContext() : nullptr)
            for (auto& entry : context->Trace())
                taken[entry.m_Link] = entry.m_Step;
        auto takenWidth = [this, &taken](Id link)
        {
            const float c_Steps = 16.0f;
            auto it = taken.find(link);
            if (it == taken.end() || (m_Debugger.IsActive() && link == m_Debugger.TakenLink()))
                return 0.0f;
            return 2.0f * ImMax(0.0f, 1.0f - static_cast<float>(m_Debugger.StepCount() - it->second) / c_Steps);
        };

        // Commit all links to editor. A disabled link shows, grey, until its problem is resolved;
        // one whose pin is gone has nothing to show between.
        for (auto& link : graph.Links())
        {
            auto from = graph.FindPin(link.m_From);
            if (!from || !graph.FindPin(link.m_To))
                continue;
            if (link.m_Active)
                ed::Link(link.m_Id, link.m_From, link.m_To, PinColor(*from), (from->m_Flow ? 2.5f : 1.5f) + takenWidth(link.m_Id));
            else
                ed::Link(link.m_Id, link.m_From, link.m_To, ImVec4(0.5f, 0.5f, 0.5f, 0.4f), 1.0f);
        }

        FlowTakenLink(m_Debugger);

        if (edit)
            edit();
    }

    // While the Create Node dialog is open for a link dragged from a pin, the link, from the pin to
    // where it was dropped, curved as the editor curves links.
    void ShowPendingLink(const Document& document, ImVec2 canvasMin, ImVec2 canvasMax)
    {
        const auto from = m_CreateNodeDialog.IsOpen() ? m_CreateNodeDialog.GetFromPin() : 0;
        const auto pin  = from ? document.GetGraph().FindPin(from) : nullptr;
        const auto icon = m_PinIcons.find(from);
        if (!pin || icon == m_PinIcons.end())
            return;
        const auto scale     = ed::CanvasToScreen(ImVec2(1.0f, 0.0f)).x - ed::CanvasToScreen(ImVec2(0.0f, 0.0f)).x;
        const auto output    = pin->m_Direction == Direction::Output;
        const auto start     = ed::CanvasToScreen(ImVec2(output ? icon->second.Max.x : icon->second.Min.x, icon->second.GetCenter().y));
        const auto end       = ed::CanvasToScreen(m_CreateNodeDialog.GetDropPoint());
        const auto direction = ImVec2(output ? 1.0f : -1.0f, 0.0f) * ed::GetStyle().LinkStrength * scale;
        auto drawList = ImGui::GetWindowDrawList();
        drawList->PushClipRect(canvasMin, canvasMax, true);
        drawList->AddBezierCubic(start, start + direction, end - direction, end, ImColor(PinColor(*pin)), (pin->m_Flow ? 2.5f : 1.5f) * scale);
        drawList->PopClipRect();
    }

    // A red disc over the top-left corner of each node with a breakpoint.
    void ShowBreakpoints(const Document& document, ImVec2 canvasMin, ImVec2 canvasMax)
    {
        const auto radius = ImGui::GetTextLineHeight() * 0.35f;
        auto drawList = ImGui::GetWindowDrawList();
        drawList->PushClipRect(canvasMin, canvasMax, true);
        for (auto& node : document.GetGraph().Nodes())
        {
            if (!m_Debugger.HasBreakpoint(node.m_Id))
                continue;
            const auto center = ed::CanvasToScreen(ed::GetNodePosition(node.m_Id)) + ImVec2(radius, radius) * 0.5f;
            drawList->AddCircleFilled(center, radius, IM_COL32(220, 48, 48, 255));
            drawList->AddCircle(center, radius, IM_COL32(32, 32, 32, 255), 0, 1.5f);
        }
        drawList->PopClipRect();
    }

    // While a run is paused, a chevron above the node its last step ran, and a blue one above the
    // node it runs next; beside the pins, what the run holds for them: values of written outputs and
    // of linked inputs, and the flow along the link it took. Drawn to the canvas's scale.
    void ShowRunOverlay(Document& document, ImVec2 canvasMin, ImVec2 canvasMax)
    {
        if (!m_Debugger.IsActive() || m_Debugger.IsRunning())
            return;
        auto&      graph    = document.GetGraph();
        auto       drawList = ImGui::GetWindowDrawList();
        const auto scale    = ed::CanvasToScreen(ImVec2(1.0f, 0.0f)).x - ed::CanvasToScreen(ImVec2(0.0f, 0.0f)).x;
        const auto current  = m_Debugger.CurrentNode(), next = m_Debugger.NextNode();
        const auto ran      = ImGui::GetColorU32(ImGuiCol_PlotHistogram), runs = ImGui::GetColorU32(ImGuiCol_NavCursor);
        drawList->PushClipRect(canvasMin, canvasMax, true);

        for (auto [node, color] : { std::pair(current, ran), std::pair(next, runs) })
        {
            if (!node || !graph.FindNode(node))
                continue;
            const auto position = ed::GetNodePosition(node);
            const auto pivot    = ed::CanvasToScreen(position + ImVec2(ed::GetNodeSize(node).x * 0.5f, 0.0f));
            DrawIcon(drawList, pivot - ImVec2(40.0f, 35.0f) * scale, pivot + ImVec2(40.0f, 0.0f) * scale, IconType::FlowDown, true, color, 0);
        }

        auto       font     = ImGui::GetFont();
        const auto fontSize = ImGui::GetFontSize() * scale;
        const auto padding  = ImVec2(4.0f, 1.0f) * scale;
        const auto gap      = ImGui::GetStyle().ItemSpacing.x * 1.75f * scale;
        const auto taken    = graph.FindLink(m_Debugger.TakenLink());
        auto pill = [&](const Pin& pin, ImU32 color, const std::string& text)
        {
            auto icon = m_PinIcons.find(pin.m_Id);
            if (icon == m_PinIcons.end())
                return;
            const auto min   = ed::CanvasToScreen(icon->second.Min), max = ed::CanvasToScreen(icon->second.Max);
            const auto inner = text.empty() ? ImVec2(fontSize, fontSize) : font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text.c_str());
            const auto size  = inner + padding * 2.0f;
            const auto y     = (min.y + max.y - size.y) * 0.5f;
            const auto a     = ImVec2(pin.m_Direction == Direction::Input ? min.x - gap - size.x : max.x + gap, y);
            drawList->AddRectFilled(a, a + size, (color & ~IM_COL32_A_MASK) | IM_COL32(0, 0, 0, 128), 4.0f * scale);
            drawList->AddRect(a, a + size, ImGui::GetColorU32(ImGuiCol_Text, 0.5f), 4.0f * scale);
            if (text.empty())
                DrawIcon(drawList, a + padding, a + padding + inner, IconType::Flow, true, color, 0);
            else
                drawList->AddText(font, fontSize, a + padding, ImGui::GetColorU32(ImGuiCol_Text), text.c_str());
        };
        for (auto& node : graph.Nodes())
        {
            const auto color = node.m_Id == current ? ran : runs;
            for (auto& pin : node.m_Pins)
            {
                if (pin.m_Flow)
                {
                    if (taken && (taken->m_From == pin.m_Id || taken->m_To == pin.m_Id))
                        pill(pin, color, {});
                    continue;
                }
                if (pin.m_Direction == Direction::Input && !std::any_of(graph.Links().begin(), graph.Links().end(), [&pin](const Link& link) { return link.m_Active && link.m_To == pin.m_Id; }))
                    continue;
                auto watch = m_Debugger.Evaluate(document, pin.m_Id);
                if (!watch || !watch.GetValue().m_Step)
                    continue;
                auto text = ValueText(watch.GetValue().m_Value);
                if (text.size() > 32)
                    text = text.substr(0, 29) + "...";
                pill(pin, color, text.empty() ? std::string("\"\"") : text);
            }
        }
        drawList->PopClipRect();
    }

    // Above each node whose bubble is open, its problems.
    void ShowBubbles(const Document& document, ImVec2 canvasMin, ImVec2 canvasMax)
    {
        const auto wrap     = 420.0f;
        const auto line     = ImGui::GetTextLineHeight();
        const auto padding  = ImGui::GetStyle().FramePadding;
        const auto spacing  = ImGui::GetStyle().ItemSpacing.y * 0.5f;
        const auto tail     = 8.0f;
        const auto fill     = IM_COL32(28, 28, 32, 240);
        const auto rounding = 4.0f;

        auto drawList = ImGui::GetWindowDrawList();
        drawList->PushClipRect(canvasMin, canvasMax, true);
        for (auto& node : document.GetGraph().Nodes())
        {
            if (!m_Bubbles.contains(node.m_Id))
                continue;

            std::vector<std::pair<const ProblemList::Problem*, std::string>> lines;
            auto size = ImVec2(0.0f, -spacing);
            for (auto& problem : m_Problems.Problems())
            {
                if (problem.m_Node != node.m_Id)
                    continue;
                auto& text = lines.emplace_back(&problem, ProblemList::Text(document.GetBlueprint(), problem)).second;
                auto  textSize = ImGui::CalcTextSize(text.c_str(), nullptr, false, wrap);
                size.x  = ImMax(size.x, line + textSize.x);
                size.y += textSize.y + spacing;
            }
            if (lines.empty())
                continue;

            const auto nodeMin   = ed::CanvasToScreen(ed::GetNodePosition(node.m_Id));
            const auto bubbleMax = ImVec2(nodeMin.x + size.x + padding.x * 2.0f, nodeMin.y - tail);
            const auto bubbleMin = ImVec2(nodeMin.x, bubbleMax.y - size.y - padding.y * 2.0f);
            drawList->AddRectFilled(bubbleMin, bubbleMax, fill, rounding);
            drawList->AddRect(bubbleMin, bubbleMax, ImGui::GetColorU32(ImGuiCol_Border), rounding);
            drawList->AddTriangleFilled(ImVec2(nodeMin.x + 10.0f, bubbleMax.y), ImVec2(nodeMin.x + 10.0f + tail * 2.0f, bubbleMax.y), ImVec2(nodeMin.x + 10.0f, nodeMin.y), fill);

            auto cursor = bubbleMin + padding;
            for (auto& [problem, text] : lines)
            {
                ProblemSign(drawList, cursor + ImVec2(line, line) * 0.5f, line * 0.3f, problem->m_Diagnostic.m_Severity);
                drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(), cursor + ImVec2(line, 0.0f), ImGui::GetColorU32(ImGuiCol_Text), text.c_str(), nullptr, wrap);
                cursor.y += ImGui::CalcTextSize(text.c_str(), nullptr, false, wrap).y + spacing;
            }
        }
        drawList->PopClipRect();
    }

    // Tabs for the problems, the variables, the signature of a function whose graph shows, and the
    // watches and the fibers of a run.
    void ShowPanels(Document& document, ImVec2 position, ImVec2 size)
    {
        ImGui::SetCursorScreenPos(position);
        if (ImGui::BeginChild("##panels", size, ImGuiChildFlags_Borders) && ImGui::BeginTabBar("##panels"))
        {
            auto tab = [this](const char* name, std::string title, auto&& show)
            {
                auto label = title + "###" + name;
                if (!ImGui::BeginTabItem(label.c_str(), nullptr, m_PanelRequest == name ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None))
                    return;
                if (ImGui::BeginChild("##list"))
                    show();
                ImGui::EndChild();
                ImGui::EndTabItem();
            };
            auto counted = [](const char* name, size_t count) { return std::string(name) + (count ? " (" + std::to_string(count) + ")" : std::string()); };
            auto fibers  = m_Debugger.GetContext() ? m_Debugger.GetContext()->Fibers() : std::vector<FiberInfo>();
            tab("Problems", counted("Problems", m_Problems.Problems().size()), [&] { ShowProblems(document); });
            tab("Variables", counted("Variables", document.GetBlueprint().Variables().size()), [&] { ShowVariables(document); });
            if (auto function = document.GetBlueprint().FindFunction(document.GetGraph().GetId()))
                tab("Signature", "Signature: " + function->m_Name, [&] { ShowSignature(document, *function); });
            tab("Watches", counted("Watches", m_Debugger.Watched().size()), [&] { ShowWatches(document); });
            tab("Fibers", counted("Fibers", fibers.size()), [&] { ShowFibers(document, fibers); });
            m_PanelRequest.clear();
            ImGui::EndTabBar();
        }
        ImGui::EndChild();
    }

    // The panels show, the one of the name chosen.
    void RequestPanel(const char* name)
    {
        m_PanelRequest = name;
        m_ShowPanels   = true;
    }

    // A row for each problem; choosing one goes to it.
    void ShowProblems(const Document& document)
    {
        auto& graph    = document.GetGraph();
        auto  problems = m_Problems.Problems();
        auto  line     = ImGui::GetTextLineHeight();
        if (problems.empty())
            ImGui::TextDisabled("No problems");
        for (size_t i = 0; i < problems.size(); ++i)
        {
            auto& problem = problems[i];
            auto  node    = graph.FindNode(problem.m_Node);
            auto  at      = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(line, line));
            ProblemSign(ImGui::GetWindowDrawList(), at + ImVec2(line, line) * 0.5f, line * 0.3f, problem.m_Diagnostic.m_Severity);
            ImGui::SameLine();

            auto label = ProblemList::Text(document.GetBlueprint(), problem) + (problem.m_Run ? ", in the run" : "");
            if (node && !label.starts_with(node->Name()))
                label = node->Name() + ": " + label;
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Selectable(label.c_str(), m_ProblemIndex == i))
                GoToProblem(i);
            ImGui::PopID();
        }
    }

    // A row for each watched pin, of any graph: its value, and the step that wrote it. Choosing one
    // goes to its node; one of a graph that isn't shown is named with the graph's name.
    void ShowWatches(Document& document)
    {
        auto watched = m_Debugger.Watched();
        if (watched.empty())
        {
            ImGui::TextDisabled("No watches: Ctrl+Click a pin, or use its menu");
            return;
        }
        if (!ImGui::BeginTable("##watches", 4, ImGuiTableFlags_SizingFixedFit))
            return;
        ImGui::TableSetupColumn("Pin");
        ImGui::TableSetupColumn("Value");
        ImGui::TableSetupColumn("Written");
        ImGui::TableSetupColumn("Remove");
        std::optional<Id> removed;
        for (auto id : watched)
        {
            ImGui::PushID(static_cast<int>(id));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            auto graph = GraphOfPin(document.GetBlueprint(), id);
            if (auto pin = graph ? graph->FindPin(id) : nullptr)
            {
                auto node  = graph->NodeOf(id);
                auto label = node->Name() + "." + pin->m_Key;
                if (graph != &document.GetGraph())
                    label = graph->GetName() + ": " + label;
                if (ImGui::Selectable(label.c_str()))
                    GoToNode(node->m_Id);
                ImGui::TableNextColumn();
                auto watch = m_Debugger.Evaluate(document, id);
                if (!watch)
                    ImGui::TextDisabled("%s", Text(watch.GetError().Message()).c_str());
                else
                {
                    DrawValue(watch.GetValue().m_Value);
                    ImGui::TableNextColumn();
                    if (pin->m_Direction == Direction::Output || HasLinks(*graph, id))
                        ImGui::TextDisabled("%s", WhenWritten(watch.GetValue(), m_Debugger.StepCount()).c_str());
                }
            }
            else
                ImGui::TextDisabled("pin %u, gone", static_cast<unsigned>(id));
            ImGui::TableSetColumnIndex(3);
            if (SquareButton("x"))
                removed = id;
            ImGui::PopID();
        }
        ImGui::EndTable();
        if (removed)
            m_Debugger.SetWatched(*removed, false);
    }

    // A row for each of the blueprint's variables: its name, type and default, which edits change,
    // and its value in the run.
    void ShowVariables(Document& document)
    {
        auto& blueprint = document.GetBlueprint();
        auto  variables = blueprint.Variables();
        if (SquareButton("+"))
            m_Blueprint_AddVariable.Execute();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Add Variable");
        if (variables.empty())
        {
            ImGui::SameLine();
            ImGui::TextDisabled("No variables");
            return;
        }
        if (!ImGui::BeginTable("##variables", 5, ImGuiTableFlags_SizingFixedFit))
            return;

        // The change is made once the table is drawn.
        std::function<void()> change;
        for (size_t i = 0; i < variables.size(); ++i)
        {
            auto& variable = *variables[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            char name[64];
            std::snprintf(name, sizeof(name), "%s", variable.m_Name.c_str());
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
            if (ImGui::InputText("##name", name, sizeof(name), ImGuiInputTextFlags_EnterReturnsTrue))
                change = [&document, old = variable.m_Name, renamed = Variable{ name, variable.m_Default }] { ChangeVariable(document, old, renamed, "Rename Variable"); };

            ImGui::TableNextColumn();
            auto type = variable.m_Default.GetType();
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0f);
            if (ImGui::BeginCombo("##type", type->m_Name.c_str()))
            {
                for (auto head : blueprint.GetRegistry().ValueTypes())
                    if (head->m_Arity == 0 && ImGui::Selectable(head->m_Name.c_str(), &head->AsType() == type) && &head->AsType() != type)
                        change = [&document, retyped = Variable{ variable.m_Name, head->Default(head->AsType()) }] { ChangeVariable(document, retyped.m_Name, retyped, "Change Variable Type"); };
                ImGui::EndCombo();
            }

            ImGui::TableNextColumn();
            auto value = variable.m_Default;
            if (DrawValueWithEditor(static_cast<Id>(i + 1), value))
                change = [&document, given = Variable{ variable.m_Name, value }] { ChangeVariable(document, given.m_Name, given, "Change Variable Default"); };

            ImGui::TableNextColumn();
            auto current = m_Debugger.GetContext() ? m_Debugger.GetContext()->GetVariable(variable.m_Name) : Result<Value>(BlueprintError::NotFound);
            if (!current || !DrawValue(current.GetValue()))
                ImGui::TextDisabled("-");

            ImGui::TableNextColumn();
            if (SquareButton("x"))
                change = [&document, removed = variable.m_Name] { RemoveVariable(document, removed); };
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Remove, with the nodes that use it");
            ImGui::PopID();
        }
        ImGui::EndTable();
        if (change)
            change();
    }

    // The function's name, whether it's pure, and a row for each of its type parameters, inputs and
    // outputs: a name, which Enter commits, and a type, or the types a parameter allows as chips that
    // toggle. A parameter a pin's type has can't go.
    void ShowSignature(Document& document, const Function& function)
    {
        auto&       blueprint = document.GetBlueprint();
        const auto  graph     = function.m_Graph;
        const auto& signature = function.m_Signature;
        const auto  field     = ImGui::GetFontSize() * 8.0f;

        // The change is made once the panel is drawn.
        std::function<void()> change;
        auto nameField = [field](const std::string& name) -> std::optional<std::string>
        {
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), "%s", name.c_str());
            ImGui::SetNextItemWidth(field);
            if (!ImGui::InputText("##name", buffer, sizeof(buffer), ImGuiInputTextFlags_EnterReturnsTrue))
                return std::nullopt;
            return std::string(buffer);
        };

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Name");
        ImGui::SameLine();
        if (auto renamed = nameField(function.m_Name))
            change = [&document, graph, renamed = *renamed] { RenameFunction(document, graph, renamed); };
        ImGui::SameLine();
        auto pure = signature.m_Pure;
        if (ImGui::Checkbox("Pure", &pure))
            change = [&document, graph, pure] { ChangeSignature(document, graph, pure ? "Make Pure" : "Make Impure", [pure](Signature& changed) { changed.m_Pure = pure; }); };

        if (!ImGui::BeginTable("##signature", 3, ImGuiTableFlags_SizingFixedFit))
            return;
        auto section = [](const char* title, const char* add)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", title);
            ImGui::TableSetColumnIndex(2);
            ImGui::PushID(title);
            auto added = SquareButton("+");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", add);
            ImGui::PopID();
            return added;
        };
        auto removeButton = [](const char* blocked)
        {
            ImGui::BeginDisabled(blocked != nullptr);
            auto removed = SquareButton("x");
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", blocked ? blocked : "Remove");
            return removed;
        };

        if (section("Type Parameters", "Add Type Parameter"))
            change = [&document, graph] { AddTypeParameter(document, graph); };
        for (size_t i = 0; i < signature.m_Parameters.size(); ++i)
        {
            auto& parameter = signature.m_Parameters[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (auto renamed = nameField(parameter.m_Name))
                change = [&document, graph, i, renamed = *renamed] { RenameTypeParameter(document, graph, i, renamed); };

            ImGui::TableNextColumn();
            auto first = true;
            for (auto head : blueprint.GetRegistry().ValueTypes())
            {
                if (!std::exchange(first, false))
                    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
                auto allowed = std::find(parameter.m_Allowed.begin(), parameter.m_Allowed.end(), head) != parameter.m_Allowed.end();
                if (!allowed)
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                if (ImGui::Button(head->m_Name.c_str()))
                    change = [&document, graph, i, head] { ToggleAllowedType(document, graph, i, head); };
                if (!allowed)
                    ImGui::PopStyleColor();
            }
            if (parameter.m_Allowed.empty())
            {
                ImGui::SameLine();
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("any type, while none is chosen");
            }

            ImGui::TableNextColumn();
            auto user    = TypeParameterUser(signature, static_cast<int>(i));
            auto blocked = user ? user->m_Name + "'s type has it" : std::string();
            if (removeButton(user ? blocked.c_str() : nullptr))
                change = [&document, graph, i] { RemoveTypeParameter(document, graph, static_cast<int>(i)); };
            ImGui::PopID();
        }

        const auto choices = SignatureTypeChoices(blueprint.GetRegistry(), signature);
        for (auto input : { true, false })
        {
            ImGui::PushID(input ? "inputs" : "outputs");
            if (section(input ? "Inputs" : "Outputs", input ? "Add Input" : "Add Output"))
                change = [&document, graph, input] { AddSignaturePin(document, graph, input); };
            for (auto& pin : input ? signature.m_Inputs : signature.m_Outputs)
            {
                ImGui::PushID(pin.m_Key.c_str());
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (auto renamed = nameField(pin.m_Name))
                    change = [&document, graph, input, key = pin.m_Key, renamed = *renamed] { RenameSignaturePin(document, graph, input, key, renamed); };

                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(field);
                if (ImGui::BeginCombo("##type", TermText(pin.m_Type, signature.m_Parameters).c_str()))
                {
                    for (auto& choice : choices)
                        if (ImGui::Selectable(TermText(choice.m_Type, signature.m_Parameters).c_str(), choice.m_Type == pin.m_Type))
                            change = [&document, graph, input, key = pin.m_Key, type = choice.m_Type] { RetypeSignaturePin(document, graph, input, key, type); };
                    ImGui::EndCombo();
                }

                ImGui::TableNextColumn();
                if (removeButton(nullptr))
                    change = [&document, graph, input, key = pin.m_Key] { RemoveSignaturePin(document, graph, input, key); };
                ImGui::PopID();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
        if (change)
            change();
    }

    // A row for each fiber of the run: its state, the node it's at, and while it's suspended, what it
    // waits for. Choosing the node goes to it, and a signal it waits for can be sent.
    void ShowFibers(const Document& document, std::span<const FiberInfo> fibers)
    {
        if (fibers.empty())
        {
            ImGui::TextDisabled("No run");
            return;
        }
        if (!ImGui::BeginTable("##fibers", 4, ImGuiTableFlags_SizingFixedFit))
            return;
        for (auto& fiber : fibers)
        {
            const char* state = "";
            switch (fiber.m_State)
            {
                case FiberState::Ready:     state = fiber.m_AtBreakpoint ? "paused at a breakpoint" : m_Debugger.IsRunning() ? "running" : "paused"; break;
                case FiberState::Suspended: state = "suspended"; break;
                case FiberState::Done:      state = "done";      break;
                case FiberState::Failed:    state = "failed";    break;
                case FiberState::Stopped:   state = "stopped";   break;
            }
            ImGui::PushID(static_cast<int>(fiber.m_Id));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("#%u", static_cast<unsigned>(fiber.m_Id));
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(state);
            ImGui::TableNextColumn();
            if (auto node = FindNode(document.GetBlueprint(), fiber.m_Node); node && ImGui::Selectable(node->Name().c_str()))
                GoToNode(node->m_Id);
            ImGui::TableNextColumn();
            if (fiber.m_State == FiberState::Suspended)
            {
                ImGui::TextUnformatted(WakeText(fiber.m_Wake, m_Debugger.GetContext()->Time()).c_str());
                if (fiber.m_Wake.m_Kind == Wake::Kind::Signal)
                {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Send"))
                        m_Debugger.Signal(fiber.m_Wake.m_Signal);
                }
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    // Selects the problem's node, and brings it into view, in its graph.
    void GoToProblem(size_t index)
    {
        auto problems = m_Problems.Problems();
        if (index >= problems.size())
            return;
        m_ProblemIndex = index;
        if (auto node = problems[index].m_Node)
            GoToNode(node);
    }

    // The function a call node calls.
    static const Function* CalledFunction(const Document& document, const Node* node)
    {
        if (!node || !node->m_Type->m_Name.starts_with("call:"))
            return nullptr;
        return document.GetBlueprint().FunctionOf(*node->m_Type);
    }

    // A tab for each graph, a function's with "()", and one to add a function. A function's tab,
    // double-clicked or from its menu, renames it, and its menu deletes it.
    void ShowGraphTabs(Document& document)
    {
        if (!ImGui::BeginTabBar("##graphs", ImGuiTabBarFlags_FittingPolicyScroll))
            return;
        for (auto& graph : document.GetBlueprint().Graphs())
        {
            auto id       = graph->GetId();
            auto function = document.GetBlueprint().FindFunction(id) != nullptr;
            auto label    = graph->GetName() + (function ? "()" : "") + "###" + std::to_string(id);
            auto shown    = ImGui::BeginTabItem(label.c_str(), nullptr, m_TabRequest == id ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None);
            auto rename   = function && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
            if (function && ImGui::BeginPopupContextItem())
            {
                rename = ImGui::MenuItem("Rename...");
                if (ImGui::MenuItem("Delete"))
                    m_PendingDelete = id;
                ImGui::EndPopup();
            }
            if (rename)
            {
                m_RenamedFunction = id;
                m_RenameFocus     = true;
                std::snprintf(m_RenameText, sizeof(m_RenameText), "%s", graph->GetName().c_str());
            }
            if (!shown)
                continue;
            if (m_TabRequest == id)
                m_TabRequest = 0;
            else if (!m_TabRequest && id != document.GetGraph().GetId())
                m_PendingGraph = id;
            ImGui::EndTabItem();
        }
        if (ImGui::TabItemButton("+", ImGuiTabItemFlags_Trailing))
            m_Blueprint_AddFunction.Execute();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Add Function");
        ImGui::EndTabBar();
        ShowRenameFunction(document);
    }

    void ShowRenameFunction(Document& document)
    {
        if (auto name = NamePopup("##rename-function", m_RenameFocus, m_RenameText, document.GetBlueprint().FindFunction(m_RenamedFunction) != nullptr))
            RenameFunction(document, m_RenamedFunction, *name);
    }

    // Selects the node, and brings it into view, in its graph.
    void GoToNode(Id node)
    {
        if (auto graph = GraphOf(m_Document->GetBlueprint(), node); graph && graph->GetId() != m_Document->GetGraph().GetId())
        {
            m_PendingGraph = graph->GetId();
            m_PendingNode  = node;
            return;
        }
        ed::SelectNode(node);
        ed::NavigateToSelection();
    }

    // Each node's bubble opens, unless all are open already: then all close.
    void ToggleBubbles()
    {
        std::unordered_set<Id> nodes;
        for (auto& problem : m_Problems.Problems())
            if (problem.m_Node)
                nodes.insert(problem.m_Node);
        const auto allOpen = std::all_of(nodes.begin(), nodes.end(), [this](Id node) { return m_Bubbles.contains(node); });
        if (allOpen)
            m_Bubbles.clear();
        else
            m_Bubbles.insert(nodes.begin(), nodes.end());
    }

    // An input keeps its value in the graph, and a Constant the value of its output in its config.
    static void SetPinValue(Document& document, Id pinId, Value value)
    {
        auto& graph = document.GetGraph();
        auto  pin   = graph.FindPin(pinId);
        if (!pin)
            return;

        const auto   name = graph.NodeOf(pinId)->Name() + "." + pin->m_Key;
        auto         transaction = document.BeginUndoTransaction("Edit Value");
        Result<void> set;
        if (pin->m_Direction == Direction::Input)
            set = graph.SetValue(pinId, std::move(value));
        else
        {
            auto node   = graph.NodeOf(pinId);
            auto config = node->m_Config;
            config.m_Data["value"] = std::move(value);
            set = graph.SetConfig(node->m_Id, std::move(config));
        }

        if (!set)
        {
            LOGW("%s", Text(set.GetError().Message()).c_str());
            transaction->Discard();
        }
        else
            transaction->AddAction("%s changed", name.c_str());
    }

    // While a link is dragged, whether it fits is asked once for each pair of pins.
    void HandleCreateAction(Document& document)
    {
        auto& graph = document.GetGraph();

        ItemBuilder itemBuilder;

        if (!itemBuilder)
            return;

        if (auto linkBuilder = itemBuilder.QueryNewLink())
        {
            auto startPin = graph.FindPin(static_cast<Id>(linkBuilder->m_StartPinId.Get()));
            auto endPin   = graph.FindPin(static_cast<Id>(linkBuilder->m_EndPinId.Get()));
            if (!startPin || !endPin)
            {
                linkBuilder->Reject();
                return;
            }

            // Editor return pins in order draw by the user. A link goes from an output to an input.
            if (startPin->m_Direction == Direction::Input)
                std::swap(startPin, endPin);

            const auto from = startPin->m_Id, to = endPin->m_Id;
            if (!m_LinkQuery || m_LinkQuery->m_From != from || m_LinkQuery->m_To != to || m_LinkQuery->m_Revision != document.m_Revision)
            {
                m_LinkQuery = LinkQuery{ from, to, document.m_Revision, graph.CanConnect(from, to) };
                if (!m_LinkQuery->m_Fits)
                {
                    m_LinkQuery->m_Cast = graph.CanConnect(from, to, LinkMode::Cast).IsOk();
                    if (auto forced = graph.ForceDisables(from, to))
                        m_LinkQuery->m_Forced = static_cast<int>(forced.GetValue().size());
                }
            }
            auto& query = *m_LinkQuery;

            const auto ends = "From: " + DescribePin(graph, *startPin) + "\nTo: " + DescribePin(graph, *endPin);
            if (query.m_Fits)
            {
                ed::Suspend();
                ImGui::SetTooltip("Valid Link\n%s", ends.c_str());
                ed::Resume();

                if (linkBuilder->Accept())
                    CreateLink(document, from, to);
            }
            else if (query.m_Cast || query.m_Forced)
            {
                const auto reason = Text(query.m_Fits.GetError().Message());
                ed::Suspend();
                ImGui::SetTooltip("Link With %s\n%s\n%s", query.m_Cast ? (query.m_Forced ? "Cast, or Forced" : "Cast") : "Force", reason.c_str(), ends.c_str());
                ed::Resume();

                if (linkBuilder->Accept(ImVec4(1.0f, 0.6f, 0.2f, 1.0f)))
                {
                    if (query.m_Cast && !query.m_Forced)
                        CreateLink(document, from, to, LinkMode::Cast);
                    else
                    {
                        ed::Suspend();
                        m_LinkDropMenu.Open(from, to, reason, query.m_Cast, query.m_Forced);
                        ed::Resume();
                    }
                }
            }
            else
            {
                ed::Suspend();
                ImGui::SetTooltip("Invalid Link: %s", Text(query.m_Fits.GetError().Message()).c_str());
                ed::Resume();

                linkBuilder->Reject();
            }
        }
        else if (auto nodeBuilder = itemBuilder.QueryNewNode())
        {
            // Arguably creation of node is simpler than a link.
            ed::Suspend();
            ImGui::SetTooltip("Create Node...");
            ed::Resume();

            // Node builder accept return true when user release mouse button.
            // When this happen we request CreateNodeDialog to open.
            if (nodeBuilder->Accept())
            {
                ed::Suspend();
                LOGV("[HandleCreateAction] Open CreateNodeDialog");
                m_CreateNodeDialog.Open(static_cast<Id>(nodeBuilder->m_PinId.Get()));
                ed::Resume();
            }
        }
    }

    // A node goes with its links; links go first, so none is asked about twice.
    void HandleDestroyAction(Document& document)
    {
        auto& graph = document.GetGraph();

        ItemDeleter itemDeleter;

        if (!itemDeleter)
            return;

        auto deferredTransaction = document.GetDeferredUndoTransaction("Destroy Action");

        std::vector<Id> nodesToDelete;
        while (auto nodeDeleter = itemDeleter.QueryDeletedNode())
        {
            deferredTransaction->Begin("Delete Item");
            if (nodeDeleter->Accept(true))
                nodesToDelete.push_back(static_cast<Id>(nodeDeleter->m_NodeId.Get()));
        }

        int brokenLinkCount = 0;
        while (auto linkDeleter = itemDeleter.QueryDeleteLink())
        {
            deferredTransaction->Begin("Delete Item");
            if (linkDeleter->Accept() && graph.Disconnect(static_cast<Id>(linkDeleter->m_LinkId.Get())))
                ++brokenLinkCount;
        }

        for (auto node : nodesToDelete)
        {
            auto name = graph.FindNode(node) ? graph.FindNode(node)->Name() : std::string("?");
            if (graph.RemoveNode(node))
                LOGV("[HandleDestroyAction] %s", name.c_str());
        }

        if (!nodesToDelete.empty() || brokenLinkCount)
        {
            LOGV("[HandleDestroyAction] %d node%s deleted, %d link%s broken",
                static_cast<int>(nodesToDelete.size()), nodesToDelete.size() != 1 ? "s" : "",
                brokenLinkCount, brokenLinkCount != 1 ? "s" : "");
        }
    }

    void HandleContextMenuAction()
    {
        auto createNode = ed::ShowBackgroundContextMenu();
        if (ed::BeginShortcut())
        {
            createNode = ed::AcceptCreateNode() || createNode;
            ed::EndShortcut();
        }
        if (createNode)
        {
            ed::Suspend();
            LOGV("[HandleContextMenuAction] Open CreateNodeDialog");
            m_CreateNodeDialog.Open();
            ed::Resume();
        }

        ed::NodeId contextNodeId;
        if (ed::ShowNodeContextMenu(&contextNodeId))
        {
            ed::Suspend();
            m_NodeContextMenu.Open(static_cast<Id>(contextNodeId.Get()));
            ed::Resume();
        }

        ed::PinId contextPinId;
        if (ed::ShowPinContextMenu(&contextPinId))
        {
            ed::Suspend();
            m_PinContextMenu.Open(static_cast<Id>(contextPinId.Get()));
            ed::Resume();
        }

        ed::LinkId contextLinkId;
        if (ed::ShowLinkContextMenu(&contextLinkId))
        {
            ed::Suspend();
            m_LinkContextMenu.Open(static_cast<Id>(contextLinkId.Get()));
            ed::Resume();
        }
    }

    // After the editor's own, whose hovered items answer for this frame. Dialogs open in the
    // editor, the next frame.
    void DeclareActions(Document& document, ImVec2 canvasMin, ImVec2 canvasMax)
    {
        auto& graph = document.GetGraph();
        auto  pin   = graph.FindPin(static_cast<Id>(ed::GetHoveredPin().Get()));
        auto  link  = graph.FindLink(static_cast<Id>(ed::GetHoveredLink().Get()));
        auto  node  = graph.FindNode(static_cast<Id>(ed::GetHoveredNode().Get()));

        const auto overCanvas = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && ImGui::IsMouseHoveringRect(canvasMin, canvasMax);
        const auto background = overCanvas && !pin && !link && !node;
        const auto typing     = ImGui::GetIO().WantTextInput;
        const auto pinLinked  = pin && HasLinks(graph, pin->m_Id);
        const auto pinValue   = pin && HasValueEditor(graph, *pin);
        const auto pinTyped   = pin && PinVariable(graph, *pin);
        const auto pinSplits  = pin && (SplittableStruct(*pin) || !SplitOf(*graph.NodeOf(pin->m_Id), pin->m_Key).empty());

        auto owner   = node ? node : pin ? graph.NodeOf(pin->m_Id) : nullptr;
        auto canGrow = false, canShrink = false;
        if (owner)
        {
            for (auto& group : owner->m_Groups)
            {
                const auto count = GroupCount(group, owner->m_Config);
                canGrow   = canGrow || count < group.m_Max;
                canShrink = canShrink || count > group.m_Min;
            }
        }

        CanvasActions actions(overCanvas);
        actions.Gesture("Create Link", "Drag Pin to Pin", pin != nullptr);
        actions.Gesture("Create Linked Node", "Drag Pin to Canvas", pin != nullptr);
        if (actions.Press("Break Pin's Links", ImGuiMod_Alt | ImGuiKey_MouseLeft, pinLinked))
            ed::BreakLinks(ed::PinId(pin->m_Id));
        if (actions.Press("Watch Pin", ImGuiMod_Ctrl | ImGuiKey_MouseLeft, pin && !pin->m_Flow))
            m_Debugger.SetWatched(pin->m_Id, !m_Debugger.IsWatched(pin->m_Id));
        actions.Gesture("Break Link", "Alt+Click", link != nullptr);
        actions.Gesture("Delete Selection", "Delete", ed::GetSelectedObjectCount() > 0);
        actions.Gesture("Edit Value", "Click", pinValue);
        actions.Gesture("Show Node's Problems", "Click Count", owner && (m_Problems.Count(Severity::Error, owner->m_Id) > 0 || m_Problems.Count(Severity::Warning, owner->m_Id) > 0));
        actions.Gesture("Add Pin", "Click +", canGrow);
        actions.Gesture("Remove Pin", "Click -", canShrink);
        actions.Gesture("Choose Type", "Right-Click", pinTyped);
        actions.Gesture("Split or Recombine", "Double-Click", pinSplits);
        actions.Gesture("Open Function", "Double-Click", !pin && CalledFunction(document, node));
        actions.Gesture("Edit Signature", "Double-Click", !pin && node && SignatureNodeOf(document.GetBlueprint(), *node).m_Function);
        actions.Gesture("Create Node", "Space", background && !typing);
        actions.Gesture("Create Node", "Right-Click", background);
        if (actions.Press("Show or Hide All Problems", ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_D, !m_Problems.Problems().empty()))
            ToggleBubbles();
        const auto problems = m_Problems.Problems().size();
        const auto current  = m_ProblemIndex < problems;
        if (actions.Press("Next Problem", ImGuiKey_F8, problems > 0, true))
            GoToProblem(current ? (m_ProblemIndex + 1) % problems : 0);
        if (actions.Press("Previous Problem", ImGuiMod_Shift | ImGuiKey_F8, problems > 0, true))
            GoToProblem(current ? (m_ProblemIndex + problems - 1) % problems : problems - 1);
        if (actions.Press("Run", ImGuiKey_F5, m_Blueprint_Run.IsEnabled(), true))
            m_Blueprint_Run.Execute();
        if (actions.Press("Step", ImGuiKey_F10, m_Blueprint_Step.IsEnabled(), true))
            m_Blueprint_Step.Execute();
        if (actions.Press("Stop", ImGuiMod_Shift | ImGuiKey_F5, m_Blueprint_Stop.IsEnabled(), true))
            m_Blueprint_Stop.Execute();
        if (actions.Press("Navigate Backward", ImGuiMod_Alt | ImGuiKey_LeftArrow, m_View_NavigateBackward.IsEnabled(), true))
            m_View_NavigateBackward.Execute();
        if (actions.Press("Navigate Forward", ImGuiMod_Alt | ImGuiKey_RightArrow, m_View_NavigateForward.IsEnabled(), true))
            m_View_NavigateForward.Execute();
        auto selected = GetSelectedNodes();
        if (actions.Press("Toggle Breakpoint", ImGuiKey_F9, owner || !selected.empty(), true))
        {
            auto nodes   = owner ? std::vector<Id>{ owner->m_Id } : selected;
            auto enabled = !m_Debugger.HasBreakpoint(nodes.front());
            for (auto node : nodes)
                m_Debugger.SetBreakpoint(node, enabled);
        }

        if (m_ShowActions)
            actions.Draw(canvasMin, canvasMax);

        auto doubleClicked = graph.FindNode(static_cast<Id>(ed::GetDoubleClickedNode().Get()));
        if (auto function = CalledFunction(document, doubleClicked))
            m_PendingGraph = function->m_Graph;
        else if (doubleClicked && SignatureNodeOf(document.GetBlueprint(), *doubleClicked).m_Function)
            RequestPanel("Signature");
        if (auto clicked = graph.FindPin(static_cast<Id>(ed::GetDoubleClickedPin().Get())))
        {
            const auto owner = graph.NodeOf(clicked->m_Id)->m_Id;
            if (SplittableStruct(*clicked))
                SplitStructPin(document, owner, clicked->m_Key, true);
            else if (auto splitOf = SplitOf(*graph.NodeOf(clicked->m_Id), clicked->m_Key); !splitOf.empty())
                SplitStructPin(document, owner, splitOf, false);
        }
    }

    void ShowDialogs(Document& document)
    {
        ed::Suspend();

        m_CreateNodeDialog.Show(document);
        m_NodeContextMenu.Show(document, m_Debugger);
        if (m_NodeContextMenu.TakeSignatureRequest())
            RequestPanel("Signature");
        m_PinContextMenu.Show(document, m_Debugger);
        m_LinkContextMenu.Show(document);
        m_LinkDropMenu.Show(document);

        ed::Resume();
    }

    void ShowInfoTooltip(Document& document)
    {
        if (!ed::IsActive())
            return;

        auto& graph = document.GetGraph();
        auto hoveredPin  = graph.FindPin(static_cast<Id>(ed::GetHoveredPin().Get()));
        auto hoveredNode = graph.FindNode(static_cast<Id>(ed::GetHoveredNode().Get()));
        if (!hoveredNode && hoveredPin)
            hoveredNode = graph.NodeOf(hoveredPin->m_Id);

        auto pinTooltip = [this, &document, &graph](const char* label, const Pin& pin, bool showNode)
        {
            ImGui::TextUnformatted(label);
            ImGui::Bullet(); ImGui::Text("ID: %u", static_cast<unsigned>(pin.m_Id));
            ImGui::Bullet(); ImGui::Text("Key: %s", pin.m_Key.c_str());
            if (!pin.m_Name.empty())
            {
                ImGui::Bullet(); ImGui::Text("Name: %s", pin.m_Name.c_str());
            }
            if (showNode)
            {
                ImGui::Bullet(); ImGui::Text("Node: %s", graph.NodeOf(pin.m_Id)->Name().c_str());
            }
            auto parameter = pin.m_Flow ? std::string() : TypeParameterText(document.GetBlueprint(), pin.m_DecidedType);
            ImGui::Bullet(); ImGui::Text("Type: %s", pin.m_Flow ? "flow" : !parameter.empty() ? parameter.c_str() : graph.TypeText(pin.m_Id).c_str());
            for (auto& link : graph.Links())
            {
                if (link.m_To != pin.m_Id || !link.m_Active || link.m_Cast.empty())
                    continue;
                auto arrow = link.m_Cast.find("->");
                ImGui::Bullet(); ImGui::Text("Cast: %.*s -> %s", static_cast<int>(arrow), link.m_Cast.c_str(), link.m_Cast.c_str() + arrow + 2);
            }
            ImGui::Bullet(); ImGui::Text("%s%s", pin.m_Direction == Direction::Input ? "input" : "output",
                pin.m_Requirement == Requirement::Required ? ", required" : pin.m_Requirement == Requirement::Recommended ? ", recommended" : "");
            if (pin.m_Flow)
                return;

            // A constant, typed in or declared, is the pin's own: no step wrote it.
            ImGui::Bullet(); ImGui::TextUnformatted("Value:"); ImGui::SameLine();
            auto watch = m_Debugger.Evaluate(document, pin.m_Id);
            if (!watch)
            {
                ImGui::TextDisabled("%s", Text(watch.GetError().Message()).c_str());
                return;
            }
            DrawValue(watch.GetValue().m_Value);
            const auto constant = pin.m_Direction == Direction::Input && !HasLinks(graph, pin.m_Id);
            if (auto when = constant ? std::string() : WhenWritten(watch.GetValue(), m_Debugger.StepCount()); !when.empty())
            {
                ImGui::SameLine();
                ImGui::TextDisabled("(%s)", when.c_str());
            }
        };

        auto diagnostics = [this, &document, &graph](Id node, Id pin)
        {
            for (auto& problem : m_Problems.Problems())
            {
                auto& diagnostic = problem.m_Diagnostic;
                auto  link = diagnostic.m_Link ? graph.FindLink(diagnostic.m_Link) : nullptr;
                auto  here = pin ? diagnostic.m_Pin == pin || (link && link->m_To == pin) : problem.m_Node == node;
                if (here)
                    ImGui::TextColored(ImColor(ProblemColor(diagnostic.m_Severity)), "%s", ProblemList::Text(document.GetBlueprint(), problem).c_str());
            }
        };

        if (hoveredNode)
        {
            ed::Suspend();
            ImGui::BeginTooltip();

            ImGui::Text("Node: %s", hoveredNode->Name().c_str());
            ImGui::Text("Type: %s", hoveredNode->m_Type->m_Name.c_str());

            if (hoveredPin)
            {
                ImGui::Separator();
                pinTooltip("Pin", *hoveredPin, false);
            }

            diagnostics(hoveredPin ? 0 : hoveredNode->m_Id, hoveredPin ? hoveredPin->m_Id : 0);

            ImGui::EndTooltip();
            ed::Resume();
        }
        else if (auto hoveredLink = graph.FindLink(static_cast<Id>(ed::GetHoveredLink().Get())))
        {
            auto startPin = graph.FindPin(hoveredLink->m_From);
            auto endPin   = graph.FindPin(hoveredLink->m_To);
            if (!startPin || !endPin)
                return;

            ed::Suspend();
            ImGui::BeginTooltip();

            ImGui::Text("Link ID: %u", static_cast<unsigned>(hoveredLink->m_Id));
            if (!hoveredLink->m_Cast.empty())
                ImGui::Text("Cast: %s", hoveredLink->m_Cast.c_str());
            if (hoveredLink->m_Forced)
                ImGui::TextUnformatted("Forced");
            if (!hoveredLink->m_Active)
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Disabled: %s", Text(hoveredLink->m_Reason.Message()).c_str());
            ImGui::Separator();
            pinTooltip("Start Pin:", *startPin, true);
            ImGui::Separator();
            pinTooltip("End Pin:", *endPin, true);

            ImGui::EndTooltip();
            ed::Resume();
        }
    }

    bool File_IsOpen()
    {
        return m_Document != nullptr;
    }

    bool File_IsModified()
    {
        return m_Document->m_IsModified;
    }

    void File_New()
    {
        if (!File_Close())
            return;

        LOGI("[File] New");

        m_Document = std::make_unique<Document>(ExampleRegistry(), ExampleTypes());
        MakeDocumentCurrent();
        m_Document->m_DocumentState = m_Document->BuildDocumentState();
    }

    bool File_Open(std::string_view path, std::string* error = nullptr)
    {
        auto document = std::make_unique<Document>(ExampleRegistry(), ExampleTypes());
        if (!document->Load(path))
        {
            auto message = "Failed to load blueprint from file \"" + std::string(path) + "\".";
            if (error)
                *error = message;
            else
                LOGE("%s", message.c_str());
            return false;
        }

        if (!File_Close())
            return false;

        LOGI("[File] Open \"%s\"", std::string(path).c_str());

        auto mostRecentlyOpenFiles = Application_GetMostRecentlyOpenFileList();
        mostRecentlyOpenFiles.Add(std::string(path));

        document->SetPath(path);

        m_Document = std::move(document);
        MakeDocumentCurrent();

        UpdateTitle();

        m_Document->OnMakeCurrent();

        return true;
    }

    // On Linux and macOS the dialogs build shell command lines from their arguments, and
    // a shell expands '$' and '`' in them, so text from files stays out: the error message
    // is fixed, and a dialog starts in the document's directory only when its name has none
    // of those characters.
    static std::string DialogDirectory(std::string_view documentPath)
    {
        auto separator = documentPath.find_last_of("\\/");
        if (separator == std::string_view::npos)
            return {};

        auto directory = std::string(documentPath.substr(0, separator + 1));
# if !defined(_WIN32)
        if (directory.find_first_of("$`\"'\\") != std::string::npos)
            return {};
# endif
        return directory;
    }

    bool File_Open()
    {
        const char* filterPatterns[1] = { "*.bp" };
        auto directory = m_Document ? DialogDirectory(m_Document->m_Path) : std::string();
        auto path = tinyfd_openFileDialog(
            "Open Blueprint...",
            directory.empty() ? nullptr : directory.c_str(),
            1, filterPatterns,
            "Blueprint Files (*.bp)", 0);
        if (!path)
            return false;

        std::string error;
        if (!File_Open(path, &error) && !error.empty())
        {
            tinyfd_messageBox(
                (std::string(GetName()) + " - Open Blueprint...").c_str(),
                "The blueprint could not be loaded. The log says which file failed.",
                "ok", "error", 1);

            LOGE("%s", error.c_str());

            return false;
        }

        return true;
    }

    bool File_Close()
    {
        if (!File_IsOpen())
            return true;

        if (File_IsModified())
        {
            auto dialogResult = tinyfd_messageBox(
                GetName().c_str(),
                "Do you want to save changes to this blueprint before closing?",
                "yesnocancel", "question", 1);
            if (dialogResult == 1) // yes
            {
                if (!File_Save())
                    return false;
            }
            else if (dialogResult == 0) // cancel
            {
                return false;
            }
        }

        LOGI("[File] Close");

        m_Debugger.Stop();
        m_Debugger.ClearBreakpoints();
        m_Problems = {};
        m_Bubbles.clear();
        m_RunBubbles.clear();
        m_ProblemIndex = SIZE_MAX;
        m_Document = nullptr;

        ed::SetCurrentEditor(nullptr);
        DestroyEditors();

        UpdateTitle();

        return true;
    }

    bool File_SaveAsEx(std::string_view path)
    {
        if (!File_IsOpen())
            return true;

        // Closing the window saves outside OnFrame(), where no editor is current.
        auto previousEditor = ed::GetCurrentEditor();
        ed::SetCurrentEditor(EditorFor(m_Document->GetGraph().GetId()));
        m_Document->CaptureView();
        const auto saved = m_Document->Save(path);
        ed::SetCurrentEditor(previousEditor);

        if (!saved)
        {
            LOGE("Failed to save blueprint to file \"%s\".", std::string(path).c_str());
            return false;
        }

        m_Document->m_IsModified = false;

        LOGI("[File] Save \"%s\".", std::string(path).c_str());

        return true;
    }

    bool File_SaveAs()
    {
        const char* filterPatterns[1] = { "*.bp" };
        auto directory = DialogDirectory(m_Document->m_Path);
        auto path = tinyfd_saveFileDialog(
            "Save Blueprint...",
            directory.empty() ? nullptr : directory.c_str(),
            1, filterPatterns,
            "Blueprint Files (*.bp)");
        if (!path)
            return false;

        if (!File_SaveAsEx(path))
            return false;

        m_Document->SetPath(path);

        UpdateTitle();

        return true;
    }

    bool File_Save()
    {
        if (!m_Document->m_Path.empty())
            return File_SaveAsEx(m_Document->m_Path);
        else
            return File_SaveAs();
    }

    void File_Exit()
    {
        if (!Close())
            return;

        LOGI("[File] Quit");

        Quit();
    }

    void Edit_Undo()
    {
        m_Document->Undo();
    }

    void Edit_Redo()
    {
        m_Document->Redo();
    }

    void Edit_Cut()
    {
    }

    void Edit_Copy()
    {
    }

    void Edit_Paste()
    {
    }

    void Edit_Duplicate()
    {
    }

    void Edit_Delete()
    {
    }

    void Edit_SelectAll()
    {
    }

    void View_NavigateBackward()
    {
        if (m_HistoryIndex == 0)
            return;
        m_PendingGraph    = m_History[--m_HistoryIndex];
        m_PendingRemember = false;
    }

    void View_NavigateForward()
    {
        if (m_HistoryIndex + 1 >= m_History.size())
            return;
        m_PendingGraph    = m_History[++m_HistoryIndex];
        m_PendingRemember = false;
    }

    // A function of a free name, with its entry linked to a return, shown in its tab: one undo step.
    void Blueprint_AddFunction()
    {
        auto& blueprint = m_Document->GetBlueprint();
        std::string name;
        for (int i = 1; name.empty(); ++i)
        {
            name = "Function" + std::to_string(i);
            for (auto& graph : blueprint.Graphs())
                if (graph->GetName() == name)
                    name.clear();
        }

        auto transaction = m_Document->BeginUndoTransaction("Add Function");
        auto added = blueprint.AddFunction(name, {});
        if (!added)
        {
            auto message = added.GetError().Message();
            LOGE("[AddFunction] %.*s", static_cast<int>(message.size()), message.data());
            transaction->Discard();
            return;
        }
        auto& body  = *added.GetValue();
        auto  entry = body.AddNode("entry:" + name), ret = body.AddNode("return:" + name);
        auto  pin   = [&body](const Result<Id>& node, const char* key) { return body.FindNode(node.ValueOr(0))->FindPin(key)->m_Id; };
        if (!entry || !ret || !body.Connect(pin(entry, "then"), pin(ret, "enter")))
            LOGE("[AddFunction] %s has no entry linked to a return", name.c_str());

        ShowGraph(body.GetId());
        if (entry && ret)
        {
            ed::SetNodePosition(entry.GetValue(), ImVec2(40.0f, 40.0f));
            ed::SetNodePosition(ret.GetValue(), ImVec2(360.0f, 40.0f));
        }
        LOGV("[AddFunction] %s", name.c_str());
        transaction->AddAction("%s added", name.c_str());
    }

    void Blueprint_Start()
    {
        m_Debugger.Start(*m_Document, false);
    }

    // Without a run, a step starts one, paused, and takes its first step.
    void Blueprint_Step()
    {
        if (!m_Debugger.IsActive() && !m_Debugger.Start(*m_Document, false))
            return;
        m_Debugger.Step();
    }

    void Blueprint_Stop()
    {
        m_Debugger.Stop();
    }

    void Blueprint_Run()
    {
        if (m_Debugger.IsActive())
            m_Debugger.Continue();
        else
            m_Debugger.Start(*m_Document, true);
    }

    struct LinkQuery
    {
        Id                 m_From     = 0;
        Id                 m_To       = 0;
        uint64_t           m_Revision = 0;
        Result<void>       m_Fits;
        bool               m_Cast   = false;
        std::optional<int> m_Forced = {}; // the links forcing it disables
    };

    crude_logger::OverlayLogger m_OverlayLogger;
    std::string                 m_Title;

    std::unique_ptr<Document> m_Document;
    std::map<Id, ed::EditorContext*> m_Editors;
    std::vector<Id>           m_History;               // the graphs shown, for navigating back and forward
    size_t                    m_HistoryIndex    = 0;
    Id                        m_TabRequest      = 0;     // a graph shown by other means than its tab, whose tab to select
    Id                        m_PendingGraph    = 0;     // to show once the frame begins, before the editor does
    bool                      m_PendingRemember = true;
    Id                        m_PendingNode     = 0;     // to go to once its graph shows
    Id                        m_PendingDelete   = 0;     // a function's graph, to delete as the next frame begins
    Id                        m_FollowedNode    = 0;     // the run's next node, as its graph last showed for it
    std::unordered_map<Id, ImRect> m_PinIcons;           // where the shown graph's pins' icons are, as its nodes last drew
    Id                        m_RenamedFunction = 0;     // its graph's ID
    bool                      m_RenameFocus     = false; // the rename popup opens, its name focused
    char                      m_RenameText[64]  = {};
    Debugger                  m_Debugger;
    ProblemList               m_Problems;
    std::unordered_set<Id>    m_Bubbles;    // nodes whose problems show
    std::unordered_set<Id>    m_RunBubbles; // nodes whose bubble a run's error opened
    std::optional<LinkQuery>  m_LinkQuery;
    bool                      m_ShowActions  = true;
    bool                      m_ShowPanels   = true;
    std::string               m_PanelRequest;          // the panel to choose as the panels show next
    size_t                    m_ProblemIndex = SIZE_MAX; // the problem chosen last

    CreateNodeDialog m_CreateNodeDialog;
    NodeContextMenu  m_NodeContextMenu;
    PinContextMenu   m_PinContextMenu;
    LinkContextMenu  m_LinkContextMenu;
    LinkDropMenu     m_LinkDropMenu;

    Action m_File_New       = { "New",          [this] { File_New();    } };
    Action m_File_Open       = { "Open...",      [this] { File_Open();   } };
    Action m_File_SaveAs     = { "Save As...",   [this] { File_SaveAs(); } };
    Action m_File_Save       = { "Save",         [this] { File_Save();   } };
    Action m_File_Close      = { "Close",        [this] { File_Close();  } };
    Action m_File_Exit       = { "Exit",         [this] { File_Exit();   } };

    Action m_Edit_Undo       = { "Undo",         [this] { Edit_Undo();      } };
    Action m_Edit_Redo       = { "Redo",         [this] { Edit_Redo();      } };
    Action m_Edit_Cut        = { "Cut",          [this] { Edit_Cut();       } };
    Action m_Edit_Copy       = { "Copy",         [this] { Edit_Copy();      } };
    Action m_Edit_Paste      = { "Paste",        [this] { Edit_Paste();     } };
    Action m_Edit_Duplicate  = { "Duplicate",    [this] { Edit_Duplicate(); } };
    Action m_Edit_Delete     = { "Delete",       [this] { Edit_Delete();    } };
    Action m_Edit_SelectAll  = { "Select All",   [this] { Edit_SelectAll(); } };

    Action m_View_NavigateBackward = { "Navigate Backward", [this] { View_NavigateBackward(); } };
    Action m_View_NavigateForward  = { "Navigate Forward",  [this] { View_NavigateForward();  } };

    Action m_Blueprint_Start = { "Start",        [this] { Blueprint_Start(); } };
    Action m_Blueprint_Step  = { "Step",         [this] { Blueprint_Step();  } };
    Action m_Blueprint_Stop  = { "Stop",         [this] { Blueprint_Stop();  } };
    Action m_Blueprint_Run   = { "Run",          [this] { Blueprint_Run();   } };
    Action m_Blueprint_AddFunction = { "Add Function", [this] { Blueprint_AddFunction(); } };
    Action m_Blueprint_AddVariable = { "Add Variable", [this] { AddVariable(*m_Document); } };
};


int Main(int argc, char** argv)
{
    BlueprintEditorExample example("Blueprints2", argc, argv);

    if (example.Create())
        return example.Run();

    return 0;
}
