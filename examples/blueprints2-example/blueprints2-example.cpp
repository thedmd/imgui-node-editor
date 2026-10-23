# define IMGUI_DEFINE_MATH_OPERATORS
# include <application.h>
# include <imgui_internal.h>
# include <splitter.h>
# include <IconsFontAwesome7.h>
# include <nfd.hpp>

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
    Action(const char* icon, std::string_view name, OnTriggeredEvent::Delegate delegate = {});
    Action(const char* icon, std::string_view name, ImGuiKeyChord shortcut, OnTriggeredEvent::Delegate delegate);

    void SetName(std::string_view name);
    const std::string& GetName() const;

    const char*   GetIcon() const;     // Font Awesome's, before the name in menus
    ImGuiKeyChord GetShortcut() const; // shown wherever the action is; ImGuiKey_None for none

    void SetEnabled(bool set);
    bool IsEnabled() const;

    void Execute();

    OnChangeEvent       OnChange;
    OnTriggeredEvent    OnTriggered;

private:
    const char*   m_Icon = nullptr;
    std::string   m_Name;
    ImGuiKeyChord m_Shortcut  = ImGuiKey_None;
    bool          m_IsEnabled = true;
};

Action::Action(const char* icon, std::string_view name, OnTriggeredEvent::Delegate delegate)
    : Action(icon, name, ImGuiKey_None, std::move(delegate))
{
}

Action::Action(const char* icon, std::string_view name, ImGuiKeyChord shortcut, OnTriggeredEvent::Delegate delegate)
    : m_Icon(icon)
    , m_Name(name)
    , m_Shortcut(shortcut)
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

const char* Action::GetIcon() const
{
    return m_Icon;
}

ImGuiKeyChord Action::GetShortcut() const
{
    return m_Shortcut;
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
            // The panels' bar covers or uncovers the canvas's bottom, and leaves the graph where it is.
            ed::Config config;
            config.SettingsName   = nullptr;
            config.CanvasSizeMode = ed::CanvasSizeMode::KeepTopLeft;
            editor = ed::CreateEditor(&config);

            // Nodes draw their own fills, borders and rings, on their cut outlines. The editor
            // draws its rings from the style as the canvas ends, so a push wouldn't hide them.
            auto previous = ed::GetCurrentEditor();
            ed::SetCurrentEditor(editor);
            auto& style = ed::GetStyle();
            style.NodeBorderWidth         = 0.0f;
            style.GroupBorderWidth        = 0.0f;
            style.Colors[ed::StyleColor_GroupBg].w = 0.0f;
            style.HoveredNodeBorderWidth  = 0.0f;
            style.SelectedNodeBorderWidth = 0.0f;
            style.Colors[ed::StyleColor_NodeBg].w = 0.0f;
            ed::SetCurrentEditor(previous);
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

        m_HasFileDialogs = NFD::Init() == NFD_OKAY;
        if (!m_HasFileDialogs)
            LOGE("[File] No file dialogs: %s", NFD::GetError());
    }

    void OnStop() override
    {
        if (std::exchange(m_HasFileDialogs, false))
            NFD::Quit();

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
        PressShortcuts();
        UpdateTitle();

        ShowMainMenu();
        ShowToolbar();

        ImGui::Separator();

        ImVec2 canvasMin, canvasMax;
        if (m_Document)
        {
            ShowGraphTabs(*m_Document);

            // The panels keep their height as the window's changes, and the bar above them moves it.
            auto& style  = ImGui::GetStyle();
            auto  canvas = ImGui::GetContentRegionAvail().y;
            auto  panels = 0.0f;
            if (m_ShowPanels)
            {
                const auto least = ImGui::GetFrameHeightWithSpacing() + ImGui::GetTextLineHeightWithSpacing() + style.WindowPadding.y * 2.0f;
                if (m_PanelsHeight <= 0.0f)
                    m_PanelsHeight = least + ImGui::GetTextLineHeightWithSpacing() * 4.0f;
                panels = ImClamp(m_PanelsHeight, least, ImMax(least, canvas - style.ItemSpacing.y - least));
                canvas -= panels + style.ItemSpacing.y;
                Splitter(false, style.ItemSpacing.y, &canvas, &panels, least, least);
                m_PanelsHeight = panels;
            }
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
                ShowPanels(*m_Document, ImVec2(canvasMin.x, canvasMax.y + style.ItemSpacing.y), ImVec2(canvasMax.x - canvasMin.x, panels));

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

        ShowClosePrompt();
        ShowOpenFailure();

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
        return File_Close([this] { Close(); });
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
        const auto hasNodes = hasDocument && ed::GetSelectedNodes(nullptr, 0) > 0;
        m_Edit_Cut.SetEnabled(hasNodes);
        m_Edit_Copy.SetEnabled(hasNodes);
        m_Edit_Paste.SetEnabled(hasDocument);
        m_Edit_Duplicate.SetEnabled(hasNodes);
        m_Edit_Delete.SetEnabled(hasDocument && ed::GetSelectedObjectCount() > 0);
        m_Edit_SelectAll.SetEnabled(hasDocument && !m_Document->GetGraph().Nodes().empty());

        m_View_NavigateBackward.SetEnabled(hasDocument && m_HistoryIndex > 0);
        m_View_NavigateForward.SetEnabled(hasDocument && m_HistoryIndex + 1 < m_History.size());
        m_View_NextProblem.SetEnabled(hasDocument && !m_Problems.Problems().empty());
        m_View_PreviousProblem.SetEnabled(hasDocument && !m_Problems.Problems().empty());
        m_View_ToggleProblems.SetEnabled(hasDocument && !m_Problems.Problems().empty());
        m_Blueprint_AddFunction.SetEnabled(hasDocument);
        const auto inFunction = hasDocument && m_Document->GetBlueprint().FindFunction(m_Document->GetGraph().GetId()) != nullptr;
        m_Blueprint_RenameFunction.SetEnabled(inFunction);
        m_Blueprint_DuplicateFunction.SetEnabled(inFunction);
        m_Blueprint_AddVariable.SetEnabled(hasDocument);

        // An error blocks a run, never an edit or saving.
        auto canStart = hasDocument && HasStart(m_Document->GetBlueprint()) && !m_Problems.BlocksRun();

        m_Blueprint_Start.SetEnabled(canStart);
        m_Blueprint_Step.SetEnabled(m_Debugger.IsActive() ? !m_Debugger.IsRunning() : canStart);
        m_Blueprint_Stop.SetEnabled(m_Debugger.IsActive());
        m_Blueprint_Run.SetEnabled(canStart && !m_Debugger.IsRunning());
        m_Blueprint_ToggleBreakpoint.SetEnabled(hasDocument && !BreakpointNodes().empty());
        m_Blueprint_RemoveOrphans.SetEnabled(hasDocument && std::any_of(m_Document->GetGraph().Nodes().begin(), m_Document->GetGraph().Nodes().end(), [](const Node& node) { return !node.m_Orphans.empty(); }));
    }

    // The keys of the actions in the File and Edit menus; a field typed in keeps its own. The
    // canvas's actions take theirs with their hints, and the editor takes Delete.
    void PressShortcuts()
    {
        for (auto action : { &m_File_New, &m_File_Open, &m_File_SaveAs, &m_File_Save, &m_Edit_Undo, &m_Edit_Redo, &m_Edit_SelectAll })
            if (action->IsEnabled() && ImGui::Shortcut(action->GetShortcut(), ImGuiInputFlags_RouteGlobal))
                action->Execute();
    }

    void ShowMainMenu()
    {
        // A menu's icons stand in a column of their own, before the labels.
        auto menuAction = [](Action& action)
        {
            const auto shortcut = action.GetShortcut() != ImGuiKey_None ? ImGui::GetKeyChordName(action.GetShortcut()) : nullptr;
            if (ImGui::MenuItemEx(action.GetName().c_str(), action.GetIcon(), shortcut, false, action.IsEnabled()))
            {
                action.Execute();
            }
        };
        auto menuToggle = [](const char* icon, const char* label, bool& value, bool enabled = true)
        {
            if (ImGui::MenuItemEx(label, icon, nullptr, value, enabled))
                value = !value;
        };

        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                auto mostRecentlyOpenFiles = Application_GetMostRecentlyOpenFileList();

                menuAction(m_File_New);
                ImGui::Separator();
                menuAction(m_File_Open);
                if (ImGui::BeginMenuEx("Open Recent", ICON_FA_CLOCK_ROTATE_LEFT, !mostRecentlyOpenFiles.GetList().empty()))
                {
                    // Opening a file moves it to the front of the list.
                    std::string clickedPath;
                    for (auto& entry : mostRecentlyOpenFiles.GetList())
                        if (ImGui::MenuItem(entry.c_str()))
                            clickedPath = entry;
                    ImGui::Separator();
                    if (ImGui::MenuItemEx("Clear Recently Opened", ICON_FA_BROOM))
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
                menuAction(m_View_NextProblem);
                menuAction(m_View_PreviousProblem);
                menuAction(m_View_ToggleProblems);
                ImGui::Separator();
                menuToggle(ICON_FA_KEYBOARD, "Show Actions", m_ShowActions);
                menuToggle(ICON_FA_EYE_SLASH, "Show Disabled Actions", m_ShowDisabledActions, m_ShowActions);
                menuToggle(ICON_FA_TABLE_COLUMNS, "Show Panels", m_ShowPanels);

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
                menuAction(m_Blueprint_ToggleBreakpoint);
                menuAction(m_Blueprint_RemoveOrphans);
                ImGui::Separator();
                menuAction(m_Blueprint_AddFunction);
                menuAction(m_Blueprint_RenameFunction);
                menuAction(m_Blueprint_DuplicateFunction);
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
            if (action.GetShortcut() != ImGuiKey_None)
                ImGui::SetItemTooltip("%s", ImGui::GetKeyChordName(action.GetShortcut()));
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

    // A comment shows its text in its header, over an editor group. Zoomed out, the text shows above
    // it as well, at the size of text outside the canvas.
    void ShowComment(const Node& comment, bool hovered)
    {
        NodeLook look;
        look.m_Selected    = ed::IsNodeSelected(comment.m_Id);
        look.m_Border      = hovered ? IM_COL32(255, 255, 255, 80) : IM_COL32(255, 255, 255, 38);
        look.m_BorderWidth = hovered ? 1.5f : 1.0f;

        const auto text = CommentText(comment);
        ed::BeginNode(comment.m_Id);
        ImGui::PushFont(HeaderFont(), HeaderFont()->LegacySize);
        ImGui::TextUnformatted(text.c_str());
        ImGui::PopFont();
        const auto headerBottom = ImGui::GetItemRectMax().y + ImGui::GetStyle().ItemSpacing.y * 0.5f;
        ed::Group(ImVec2(320.0f, 180.0f));
        ed::EndNode();

        if (auto drawList = ed::GetNodeBackgroundDrawList(comment.m_Id))
        {
            const auto min = ed::GetNodePosition(comment.m_Id);
            DrawCommentLook(drawList, min, min + ed::GetNodeSize(comment.m_Id), headerBottom, look);
        }

        if (ed::BeginGroupHint(comment.m_Id))
        {
            ImGui::PushFont(HeaderFont(), HeaderFont()->LegacySize);
            ImGui::SetCursorScreenPos(ed::GetGroupMin() - ImVec2(0.0f, ImGui::GetTextLineHeightWithSpacing() + 4.0f));
            ImGui::TextUnformatted(text.c_str());
            ImGui::PopFont();
        }
        ed::EndGroupHint();
    }

    // A node's "+" is a pin of the editor's alone, of an ID no blueprint gives out: the node's, with
    // its top bit set.
    static constexpr uintptr_t c_GrowTabBit = 0x80000000u;
    static ed::PinId GrowTabId(Id node) { return ed::PinId(c_GrowTabBit | node); }
    static Id        GrowTabNode(ed::PinId pin)
    {
        const auto id = pin.Get();
        return id > c_GrowTabBit && id <= 0xFFFFFFFFu ? static_cast<Id>(id - c_GrowTabBit) : 0;
    }

    // The node whose pins' "+" and "-" show at full: the one the mouse is on, or on a pin or the "+"
    // of, for as long as the mouse stays within a line and a half of its edges, where its "-" are.
    void UpdateControlsNode(const Graph& graph)
    {
        auto hovered = graph.FindNode(static_cast<Id>(ed::GetHoveredNode().Get()));
        if (!hovered)
            hovered = graph.NodeOf(static_cast<Id>(ed::GetHoveredPin().Get()));
        if (!hovered)
            hovered = graph.FindNode(GrowTabNode(ed::GetHoveredPin()));
        if (hovered)
        {
            m_ControlsNode = hovered->m_Id;
            return;
        }
        const auto reach = ImVec2(1.0f, 1.0f) * ImGui::GetTextLineHeight() * 1.5f;
        const auto min   = m_ControlsNode ? ed::GetNodePosition(m_ControlsNode) : ImVec2(FLT_MAX, FLT_MAX);
        if (min.x == FLT_MAX || !graph.FindNode(m_ControlsNode) || !ImRect(min - reach, min + ed::GetNodeSize(m_ControlsNode) + reach).Contains(ImGui::GetMousePos()))
            m_ControlsNode = 0;
    }

    // A node that grows by pins has a "+" on its bottom edge, under the column they're added to:
    // faint until the node is hovered or selected. A click on it adds a pin, and a link dropped on
    // it one linked (LinkToTab()). Each pin the node can lose has a "-" beside it, outside the node,
    // while the node is hovered: faint until the pin or the "-" is. Neither takes room in the node.
    // Called once the node's layout ends, its padding given.
    void ShowPinControls(Document& document, const Node& node, ImVec4 padding, std::function<void()>& edit)
    {
        auto&      blueprint = document.GetBlueprint();
        const auto shown     = node.m_Id == m_ControlsNode;
        const auto line      = ImGui::GetTextLineHeight();
        const auto faint     = 0.35f;
        const auto position  = ed::GetNodePosition(node.m_Id);
        const auto right     = position.x + ed::GetNodeSize(node.m_Id).x;
        const auto content   = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        auto       tooltip   = [](const char* text)
        {
            ed::Suspend();
            ImGui::SetTooltip("%s", text);
            ed::Resume();
        };

        if (auto growth = GrowthOf(blueprint, node))
        {
            // Under the icons of the column, or the node's edge padded while the column has none.
            const auto input = growth.m_Direction == Direction::Input;
            auto       x     = input ? position.x + padding.x + line * 0.5f : right - padding.z - line * 0.5f;
            for (auto& pin : node.m_Pins)
            {
                auto icon = m_PinIcons.find(pin.m_Id);
                if (pin.m_Direction == growth.m_Direction && icon != m_PinIcons.end())
                {
                    x = icon->second.GetCenter().x;
                    break;
                }
            }
            const auto tab    = GrowTabId(node.m_Id);
            const auto center = ImVec2(x, content.Max.y + padding.w);
            const auto rect   = ImRect(center - ImVec2(line, line) * 0.5f, center + ImVec2(line, line) * 0.5f);

            // The tab's own item, empty, goes where the content starts, which keeps the node as it is.
            ImGui::SetCursorScreenPos(content.Min);
            ed::PushStyleVar(input ? ed::StyleVar_TargetDirection : ed::StyleVar_SourceDirection, ImVec2(0.0f, 1.0f));
            ed::BeginPin(tab, input ? ed::PinKind::Input : ed::PinKind::Output);
            ed::PinRect(rect.Min, rect.Max);
            ed::PinPivotRect(center, center);
            ed::EndPin();
            ed::PopStyleVar();

            const auto  hot = ed::GetHoveredPin() == tab;
            const auto& io  = ImGui::GetIO();
            const auto lit = shown || hot || ed::IsNodeSelected(node.m_Id);
            DrawSign(ImGui::GetWindowDrawList(), rect.Min, rect.Max, true, growth.m_Enabled, hot, lit ? 1.0f : faint, lit ? 1.0f : faint);
            if (hot)
                tooltip(!growth.m_Group.empty() ? "Add Pin" : input ? "Add Output" : "Add Input");
            if (hot && growth.m_Enabled && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && rect.Contains(io.MouseClickedPos[ImGuiMouseButton_Left]) && !ImGui::IsMouseDragPastThreshold(ImGuiMouseButton_Left))
                edit = [&document, id = node.m_Id, growth] { GrowPins(document, id, growth); };
        }

        if (!shown)
            return;
        std::unordered_set<int> indices; // an index's first pin has its "-"
        for (auto& pin : node.m_Pins)
        {
            const auto removal = RemovalOf(blueprint, node, pin);
            const auto icon    = m_PinIcons.find(pin.m_Id);
            if (!removal || icon == m_PinIcons.end() || (removal.m_Index >= 0 && !indices.insert(removal.m_Index).second))
                continue;
            const auto gap  = ImGui::GetStyle().ItemInnerSpacing.x;
            const auto x    = pin.m_Direction == Direction::Input ? position.x - gap - line : right + gap;
            const auto y    = icon->second.GetCenter().y - line * 0.5f;
            const auto rect = ImRect(ImVec2(x, y), ImVec2(x + line, y + line));
            ImGui::PushID(static_cast<int>(pin.m_Id));
            auto hovered = false;
            if (SignButton("-", rect.Min, rect.Max, false, ed::GetHoveredPin() == ed::PinId(pin.m_Id), 0.55f, &hovered))
                edit = [&document, id = node.m_Id, removal] { RemovePin(document, id, removal); };
            if (hovered)
                tooltip(!removal.m_Function ? "Remove Pin" : removal.m_Input ? "Remove Input" : "Remove Output");
            ImGui::PopID();
        }
    }

    // A link dragged between a pin and a node's "+" adds a pin to the node's group, linked to the one
    // dragged, in one undo step. A function's entry and return take none this way.
    void LinkToTab(Document& document, ItemBuilder::LinkBuilder& builder, Id node, Id pin)
    {
        auto&      graph  = document.GetGraph();
        auto       found  = graph.FindNode(node);
        const auto growth = found ? GrowthOf(document.GetBlueprint(), *found) : PinGrowth();
        const auto tab    = static_cast<Id>(GrowTabId(node).Get());
        if (!m_LinkQuery || m_LinkQuery->m_From != pin || m_LinkQuery->m_To != tab || m_LinkQuery->m_Revision != document.m_Revision)
        {
            auto fits = growth.m_Group.empty() ? Result<void>(Error(BlueprintError::Unsupported, "a link adds no input or output to a function"))
                      : !growth.m_Enabled      ? Result<void>(Error(BlueprintError::Unsupported, "the node has all the pins it can"))
                                               : CanLinkNewGroupPin(graph, node, growth.m_Group, pin);
            m_LinkQuery = LinkQuery{ pin, tab, document.m_Revision, std::move(fits) };
        }
        ed::Suspend();
        if (m_LinkQuery->m_Fits)
            ImGui::SetTooltip("Add a Pin, Linked");
        else
            ImGui::SetTooltip("Can't Add a Pin: %s", Text(m_LinkQuery->m_Fits.GetError().Message()).c_str());
        ed::Resume();
        if (!m_LinkQuery->m_Fits)
            builder.Reject();
        else if (builder.Accept())
            AddGroupPinLinked(document, node, growth.m_Group, pin);
    }

    // Where a link meets a pin on the canvas, as the editor last placed it: a reroute's center, the
    // center of the pin's icon as the nodes last drew, or the middle of its node's edge on the pin's
    // side; none for a node not placed.
    std::optional<ImVec2> LinkEnd(const Graph& graph, Id pin) const
    {
        auto owner = graph.NodeOf(pin);
        auto found = graph.FindPin(pin);
        const auto position = owner ? ed::GetNodePosition(owner->m_Id) : ImVec2(FLT_MAX, FLT_MAX);
        if (!found || position.x == FLT_MAX)
            return std::nullopt;
        if (IsReroute(*owner))
            return position + ImVec2(RerouteSize(), RerouteSize()) * 0.5f;
        if (auto icon = m_LastPinIcons.find(pin); icon != m_LastPinIcons.end())
            return icon->second.GetCenter();
        const auto size = ed::GetNodeSize(owner->m_Id);
        return position + ImVec2(found->m_Direction == Direction::Output ? size.x : 0.0f, size.y * 0.5f);
    }

    // The side of its icon links meet a pin on: an input's left, an output's right, a reroute's the
    // ones it was last drawn with.
    ImVec2 PinSide(const Pin& pin) const
    {
        auto side = m_PinSides.find(pin.m_Id);
        return side != m_PinSides.end() ? side->second : ImVec2(pin.m_Direction == Direction::Input ? -1.0f : 1.0f, 0.0f);
    }

    // A reroute is a disc of its type's color, filled while linked, with a cast's two colors or a
    // disabled link's slash as a pin's icon has them. Links run through its center the way
    // RerouteDirection() gives: of its square, the quarter they come in on is its input, the one
    // they go out on its output, and the two across the way move it, as Ctrl+drag does anywhere on it.
    void ShowReroute(const Graph& graph, const Node& node, const std::unordered_set<Id>& linked, const std::unordered_set<Id>& disabled, const std::unordered_map<Id, ImVec4>& castFrom)
    {
        auto in  = node.FindPin("in");
        auto out = node.FindPin("out");
        if (!in || !out)
            return;
        const auto size   = RerouteSize();
        const auto radius = size * 0.25f;

        std::vector<ImVec2> sources, targets;
        for (auto& link : graph.Links())
        {
            if (auto end = link.m_To == in->m_Id ? LinkEnd(graph, link.m_From) : std::nullopt)
                sources.push_back(*end);
            if (auto end = link.m_From == out->m_Id ? LinkEnd(graph, link.m_To) : std::nullopt)
                targets.push_back(*end);
        }
        const auto position = ed::GetNodePosition(node.m_Id);
        const auto way      = position.x != FLT_MAX ? RerouteDirection(position + ImVec2(size, size) * 0.5f, sources, targets) : ImVec2(1.0f, 0.0f);
        m_PinSides[in->m_Id]  = -way;
        m_PinSides[out->m_Id] = way;

        ed::PushStyleVar(ed::StyleVar_NodePadding, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ed::PushStyleVar(ed::StyleVar_SourceDirection, way);
        ed::PushStyleVar(ed::StyleVar_TargetDirection, -way);
        ed::BeginNode(node.m_Id);
        ImGui::Dummy(ImVec2(size, size));
        const auto center = ImGui::GetItemRectMin() + ImVec2(size, size) * 0.5f;

        // The editor hit-tests a pin by its rectangle, which a quarter isn't: while the mouse is in a
        // quarter across the way, or Ctrl is down, the pins have none, and the node takes the mouse.
        // While a button is down from an earlier frame, they keep their halves, so a link dragged
        // from one stays alive.
        const auto offset = ImGui::GetMousePos() - center;
        const auto inside = ImFabs(offset.x) <= size * 0.5f && ImFabs(offset.y) <= size * 0.5f;
        const auto across = way.x != 0.0f ? ImFabs(offset.y) >= ImFabs(offset.x) : ImFabs(offset.x) >= ImFabs(offset.y);
        auto held = false;
        for (int button = 0; button < ImGuiMouseButton_COUNT; ++button)
            held = held || (ImGui::IsMouseDown(button) && !ImGui::IsMouseClicked(button));
        const auto grip = !held && ((inside && across) || ImGui::GetIO().KeyCtrl);
        if (grip && inside && ed::GetHoveredNode() == ed::NodeId(node.m_Id))
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);

        // The pins' own items, empty, go in the node's corner, which keeps the node square.
        ImGui::SetCursorScreenPos(ImGui::GetItemRectMin());
        auto half = [&](const Pin& pin, ed::PinKind kind, ImVec2 side)
        {
            const auto extent = size * 0.5f;
            const auto min    = center + ImVec2(side.x > 0.0f ? 0.0f : -extent, side.y > 0.0f ? 0.0f : -extent);
            const auto max    = center + ImVec2(side.x < 0.0f ? 0.0f : extent, side.y < 0.0f ? 0.0f : extent);
            ed::BeginPin(pin.m_Id, kind);
            ed::PinRect(min, grip ? min : max);
            ed::PinPivotRect(center, center);
            ed::EndPin();
            m_PinIcons[pin.m_Id] = ImRect(center - ImVec2(radius, radius), center + ImVec2(radius, radius));
        };
        half(*in, ed::PinKind::Input, -way);
        half(*out, ed::PinKind::Output, way);
        ed::EndNode();
        ed::PopStyleVar(3);

        auto drawList = ed::GetNodeBackgroundDrawList(node.m_Id);
        if (!drawList)
            return;
        const auto hovered  = ed::GetHoveredNode() == ed::NodeId(node.m_Id) || ed::GetHoveredPin() == ed::PinId(in->m_Id) || ed::GetHoveredPin() == ed::PinId(out->m_Id);
        const auto errors   = m_Problems.Count(Severity::Error, node.m_Id), warnings = m_Problems.Count(Severity::Warning, node.m_Id);
        const auto color    = ImGui::GetColorU32(PinColor(*out));
        if (ed::IsNodeSelected(node.m_Id))
            drawList->AddCircle(center, radius + 4.0f, IM_COL32(255, 255, 255, 220), 0, 2.0f);
        else if (errors > 0 || warnings > 0)
            drawList->AddCircle(center, radius + 3.5f, ImGui::GetColorU32(ProblemColor(errors > 0 ? Severity::Error : Severity::Warning)), 0, 2.0f);
        else if (hovered)
            drawList->AddCircle(center, radius + 3.0f, IM_COL32(255, 255, 255, 80), 0, 1.5f);
        if (auto cast = castFrom.find(in->m_Id); cast != castFrom.end())
        {
            const auto inside = ImAtan2(-way.y, -way.x) - IM_PI * 0.5f; // where the half the link comes in on starts
            drawList->PathArcTo(center, radius, inside, inside + IM_PI);
            drawList->PathFillConvex(ImGui::GetColorU32(cast->second));
            drawList->PathArcTo(center, radius, inside + IM_PI, inside + 2.0f * IM_PI);
            drawList->PathFillConvex(color);
        }
        else if (linked.contains(in->m_Id) || linked.contains(out->m_Id))
            drawList->AddCircleFilled(center, radius, color);
        else
            drawList->AddCircle(center, radius, color, 0, 2.0f);
        if (disabled.contains(in->m_Id))
            drawList->AddLine(center + ImVec2(-radius, radius), center + ImVec2(radius, -radius), IM_COL32(255, 96, 96, 255), 2.0f);
    }

    // A comment over the selected nodes, their group inside it a margin away from them: one undo step.
    void CommentSelection(Document& document)
    {
        auto nodes = GetSelectedNodes();
        if (nodes.empty())
            return;
        ImRect bounds(ImVec2(FLT_MAX, FLT_MAX), ImVec2(-FLT_MAX, -FLT_MAX));
        for (auto node : nodes)
        {
            bounds.Add(ed::GetNodePosition(node));
            bounds.Add(ed::GetNodePosition(node) + ed::GetNodeSize(node));
        }

        auto& style       = ImGui::GetStyle();
        auto  padding     = ed::GetStyle().NodePadding;
        auto  margin      = ImVec2(24.0f, 24.0f);
        ImGui::PushFont(HeaderFont(), HeaderFont()->LegacySize);
        auto  header      = ImGui::GetTextLineHeight() + style.ItemSpacing.y;
        ImGui::PopFont();

        auto transaction = document.BeginUndoTransaction("Comment Selection");
        auto comment     = document.GetGraph().AddNode("crude.Comment");
        if (!comment)
        {
            transaction->Discard();
            return;
        }
        ed::SetNodePosition(comment.GetValue(), bounds.Min - margin - ImVec2(padding.x, padding.y + header));
        ed::SetGroupSize(comment.GetValue(), bounds.GetSize() + margin * 2.0f);
        transaction->AddAction("%s around %d nodes", document.GetGraph().FindNode(comment.GetValue())->Name().c_str(), static_cast<int>(nodes.size()));
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
        m_LastPinIcons.swap(m_PinIcons);
        m_PinIcons.clear();
        m_PinSides.clear();

        // Without frame padding above and below, a value's editor keeps the line of the pin's label.
        auto valueEditor = [&edit, &document](Id pin, Value value)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, 0.0f));
            if (DrawValueWithEditor(pin, value))
                edit = [&document, pin, value] { SetPinValue(document, pin, value); };
            ImGui::PopStyleVar();
        };

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

        // A paused run's next node has a blue border, and the node its last step ran an orange one;
        // a node with errors a red one, and a hovered node a brighter one.
        const auto current = m_Debugger.IsActive() ? m_Debugger.CurrentNode() : 0;
        const auto next    = m_Debugger.IsActive() ? m_Debugger.NextNode() : 0;
        const auto hovered = static_cast<Id>(ed::GetHoveredNode().Get());
        UpdateControlsNode(graph);

        // A pin hovered that a disabled link ends at lights the node at the link's other end, and the
        // link.
        std::unordered_set<Id> litNodes, litLinks;
        const auto hoveredPin = static_cast<Id>(ed::GetHoveredPin().Get());
        for (auto& link : graph.Links())
        {
            if (!hoveredPin || link.m_Active || (link.m_From != hoveredPin && link.m_To != hoveredPin))
                continue;
            litLinks.insert(link.m_Id);
            if (auto other = EndNode(graph, link.m_From == hoveredPin ? link.m_To : link.m_From))
                litNodes.insert(other->m_Id);
        }
        for (auto& node : graph.Nodes())
        {
            if (IsComment(node))
            {
                ShowComment(node, node.m_Id == hovered);
                continue;
            }
            if (IsReroute(node))
            {
                ShowReroute(graph, node, linked, disabled, castFrom);
                continue;
            }

            const auto errors   = m_Problems.Count(Severity::Error, node.m_Id);
            const auto warnings = m_Problems.Count(Severity::Warning, node.m_Id);
            const auto stepped  = node.m_Id == next || node.m_Id == current;
            const auto compact  = IsCompact(node);
            NodeLook look;
            look.m_Compact     = compact;
            look.m_Selected    = ed::IsNodeSelected(node.m_Id);
            look.m_Border      = node.m_Id == hovered ? IM_COL32(255, 255, 255, 80) : IM_COL32(255, 255, 255, 38);
            look.m_BorderWidth = node.m_Id == hovered ? 1.5f : 1.0f;
            if (stepped)
            {
                look.m_Border      = ImGui::GetColorU32(node.m_Id == next ? ImGuiCol_NavCursor : ImGuiCol_PlotHistogram);
                look.m_BorderWidth = 2.0f;
            }
            else if (errors > 0)
            {
                look.m_Border      = ProblemColor(Severity::Error);
                look.m_BorderWidth = 1.5f;
            }
            if (litNodes.contains(node.m_Id))
            {
                look.m_Border      = IM_COL32(255, 255, 255, 200);
                look.m_BorderWidth = 2.0f;
            }

            // A Constant's and a variable Get's stripe is of their value's color; an operator has none.
            const auto symbol = std::string_view(node.m_Type->m_CompactTitle);
            if (compact && symbol.empty())
                for (auto& pin : node.m_Pins)
                    if (pin.m_Direction == Direction::Output)
                        look.m_Stripe = ImGui::GetColorU32(PinColor(pin));

            const auto padding = NodePadding(look);
            ed::PushStyleVar(ed::StyleVar_NodePadding, padding);
            ed::BeginNode(node.m_Id);
            ed::PopStyleVar();

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

            // The band, of the color of what the node is, ends past the title, and the problems'
            // pills and the clock of a node that may suspend follow it. The body is as wide as the
            // header at least, so outputs line up at the node's right edge.
            auto bodyWidth = compact ? -1.0f : 100.0f;
            if (!compact)
            {
                look.m_Band = ImGui::GetColorU32(HeaderColor(document.GetBlueprint(), node));
                ImGui::BeginGroup();
                ImGui::PushFont(HeaderFont(), HeaderFont()->LegacySize);
                if (auto icon = NodeIcon(document.GetBlueprint(), node))
                {
                    ImGui::TextUnformatted(icon);
                    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
                }
                ImGui::TextUnformatted(title.data(), title.data() + title.size());
                ImGui::PopFont();
                look.m_TitleRight = ImGui::GetItemRectMax().x;
                const auto bandHeight = ImGui::GetItemRectMax().y + ImGui::GetStyle().ItemSpacing.y - ed::GetNodePosition(node.m_Id).y;
                auto       sameLine   = [&, first = true]() mutable
                {
                    ImGui::SameLine(0.0f, std::exchange(first, false) ? BandEnd(look.m_TitleRight, bandHeight) - look.m_TitleRight : -1.0f);
                };
                if (errors > 0 || warnings > 0)
                {
                    sameLine();
                    if (ProblemPills(errors, warnings) && !m_Bubbles.erase(node.m_Id))
                        m_Bubbles.insert(node.m_Id);
                }
                auto wait = waits.find(node.m_Id);
                if (wait != waits.end() || document.GetBlueprint().MaySuspend(*node.m_Type))
                {
                    sameLine();
                    if (LatentMark(wait != waits.end() ? wait->second : std::string()))
                    {
                        ed::Suspend();
                        ImGui::SetTooltip("May suspend its fiber");
                        ed::Resume();
                    }
                }
                ImGui::EndGroup();
                look.m_BandBottom = ImGui::GetItemRectMax().y + ImGui::GetStyle().ItemSpacing.y;
                bodyWidth = ImMax(bodyWidth, ImMax(ImGui::GetItemRectMax().x, BandEnd(look.m_TitleRight, bandHeight)) - ImGui::GetItemRectMin().x);

                ImGui::Spacing();
                ImGui::Dummy(ImVec2(100.0f, 0.0f)); // For minimum node width
            }

            // An operator's symbol stands between its inputs and outputs. Rows are a line apart, and
            // the columns are centered on one another.
            const auto spacing    = ImGui::GetStyle().ItemSpacing.y;
            const auto lineHeight = ImGui::GetTextLineHeight();
            const auto pitch      = lineHeight + 2.0f * spacing;
            auto       rowsHeight = [&](Direction direction)
            {
                const auto rows = std::count_if(node.m_Pins.begin(), node.m_Pins.end(), [direction](const Pin& pin) { return pin.m_Direction == direction; })
                                + std::count_if(node.m_Orphans.begin(), node.m_Orphans.end(), [direction](const OrphanPin& orphan) { return orphan.m_Direction == direction; });
                return rows > 0 ? static_cast<float>(rows - 1) * pitch + lineHeight : 0.0f;
            };
            const auto glyphs       = std::count_if(symbol.begin(), symbol.end(), [](char c) { return (static_cast<unsigned char>(c) & 0xC0) != 0x80; });
            const auto symbolScale  = glyphs == 1 ? 2.0f : 1.25f; // a sign large, a word less so
            const auto symbolHeight = symbol.empty() ? 0.0f : ImGui::GetTextLineHeight() * symbolScale;
            const auto inputsHeight = rowsHeight(Direction::Input), outputsHeight = rowsHeight(Direction::Output);
            const auto middle       = ImMax(spacing + ImMax(inputsHeight, outputsHeight) * 0.5f, symbolHeight * 0.5f);
            const auto bodyTop      = ImGui::GetCursorScreenPos().y;
            auto       startAt      = [&](float top)
            {
                if (!symbol.empty())
                    ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, bodyTop + ImFloor(top)));
            };

            crude_layout::Grid layout;
            layout.Begin(node.m_Id, symbol.empty() ? 2 : 3, bodyWidth);
            layout.SetColumnAlignment(0.0f);

            // A pin gone that links still name ends its column: red, its icon slashed. A compact
            // node's shows its icon alone, as its pins do.
            auto orphanRow = [&](const OrphanPin& orphan)
            {
                const auto input = orphan.m_Direction == Direction::Input;
                const auto name  = OrphanName(node, orphan);
                const auto color = ImGui::ColorConvertU32ToFloat4(ProblemColor(Severity::Error));
                ImGui::Spacing();
                ed::BeginPin(orphan.m_Id, input ? ed::PinKind::Input : ed::PinKind::Output);
                ed::PinPivotAlignment(ImVec2(input ? 0.0f : 1.0f, 0.5f));
                if (!input && !compact)
                {
                    ImGui::TextColored(color, "%s", name.c_str());
                    ImGui::SameLine();
                }
                Icon(iconSize, orphan.m_Flow ? IconType::Flow : IconType::Circle, false, color);
                const auto iconMin = ImGui::GetItemRectMin(), iconMax = ImGui::GetItemRectMax();
                m_PinIcons[orphan.m_Id] = ImRect(iconMin, iconMax);
                ImGui::GetWindowDrawList()->AddLine(ImVec2(iconMin.x + 1.0f, iconMax.y - 1.0f), ImVec2(iconMax.x - 1.0f, iconMin.y + 1.0f), IM_COL32(255, 96, 96, 255), 2.0f);
                if (input && !compact)
                {
                    ImGui::SameLine();
                    ImGui::TextColored(color, "%s", name.c_str());
                }
                ed::EndPin();
                layout.NextRow();
            };

            // Draw column with input pins.
            startAt(middle - inputsHeight * 0.5f - spacing);
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

                // [2] - Show pin name if it has one, but on a compact node
                if (!pin.m_Name.empty() && !compact)
                {
                    ImGui::SameLine();
                    ImGui::TextUnformatted(pin.m_Name.data(), pin.m_Name.data() + pin.m_Name.size());
                }

                // [3] - Show value/editor when pin is not linked to anything
                if (HasValueEditor(graph, pin))
                {
                    ImGui::SameLine();
                    valueEditor(pin.m_Id, InputValue(graph, pin));
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
            }
            for (auto& orphan : node.m_Orphans)
                if (orphan.m_Direction == Direction::Input)
                    orphanRow(orphan);

            if (!symbol.empty())
            {
                layout.SetColumnAlignment(0.5f);
                layout.NextColumn();
                startAt(middle - symbolHeight * 0.5f);
                ImGui::PushFont(nullptr, ImGui::GetFontSize() * symbolScale);
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                ImGui::TextUnformatted(symbol.data(), symbol.data() + symbol.size());
                ImGui::PopStyleColor();
                ImGui::PopFont();
                layout.NextRow();
            }

            layout.SetColumnAlignment(1.0f);
            layout.NextColumn();
            startAt(middle - outputsHeight * 0.5f - spacing);

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

                // A compact node shows its problems' pills before its value or its variable's name.
                if (compact && (errors > 0 || warnings > 0))
                {
                    if (ProblemPills(errors, warnings) && !m_Bubbles.erase(node.m_Id))
                        m_Bubbles.insert(node.m_Id);
                    ImGui::SameLine();
                }
                // A Constant shows its value, and an editor once clicked, before its pin.
                if (HasValueEditor(graph, pin))
                {
                    valueEditor(pin.m_Id, ConstantValue(node, pin));
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

                // [1] - Show pin name if it has one; a compact node's, its variable's, if any
                const auto label = compact ? GetterVariable(node) : std::string_view(pin.m_Name);
                if (!label.empty())
                {
                    ImGui::TextUnformatted(label.data(), label.data() + label.size());
                    ImGui::SameLine();
                }

                // [2] - Show icon
                Icon(iconSize, PinIcon(pin), linked.count(pin.m_Id) > 0, PinColor(pin));
                m_PinIcons[pin.m_Id] = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

                ed::EndPin();

                layout.NextRow();
            }
            for (auto& orphan : node.m_Orphans)
                if (orphan.m_Direction == Direction::Output)
                    orphanRow(orphan);

            layout.End();
            ShowPinControls(document, node, padding, edit);

            ed::EndNode();

            if (auto drawList = ed::GetNodeBackgroundDrawList(node.m_Id))
            {
                const auto min = ed::GetNodePosition(node.m_Id);
                DrawNodeLook(drawList, min, min + ed::GetNodeSize(node.m_Id), look);
            }
        }

        // While a run goes on, a flow link it took in its last steps is thicker, the more so the more
        // recently, but for the one it took last, which flows. The links past flow reroutes are taken
        // with the link into them. A run that ended keeps its context, for its values, not its trace.
        std::unordered_map<Id, uint64_t> taken;
        if (auto context = m_Debugger.IsActive() ? m_Debugger.GetContext() : nullptr)
            for (auto& entry : context->Trace())
                for (auto link : FlowChain(graph, entry.m_Link))
                    taken[link] = entry.m_Step;
        const auto last = m_Debugger.IsActive() ? FlowChain(graph, m_Debugger.TakenLink()) : std::vector<Id>();
        auto takenWidth = [this, &taken, &last](Id link)
        {
            const float c_Steps = 16.0f;
            auto it = taken.find(link);
            if (it == taken.end() || std::find(last.begin(), last.end(), link) != last.end())
                return 0.0f;
            return 2.0f * ImMax(0.0f, 1.0f - static_cast<float>(m_Debugger.StepCount() - it->second) / c_Steps);
        };

        // Commit all links to editor. A disabled link shows, grey, until its problem is resolved:
        // one whose pin is gone, to the pin's orphan row.
        for (auto& link : graph.Links())
        {
            if (!link.m_Active)
            {
                const auto lit = litLinks.contains(link.m_Id);
                ed::Link(link.m_Id, link.m_From, link.m_To, lit ? ImVec4(0.8f, 0.8f, 0.8f, 0.9f) : ImVec4(0.5f, 0.5f, 0.5f, 0.4f), lit ? 2.5f : 1.0f);
                continue;
            }
            auto from = graph.FindPin(link.m_From);
            ed::Link(link.m_Id, link.m_From, link.m_To, PinColor(*from), (from->m_Flow ? 2.5f : 1.5f) + takenWidth(link.m_Id));
        }

        FlowTakenLink(m_Debugger, graph);

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
        const auto side      = PinSide(*pin);
        const auto start     = ed::CanvasToScreen(icon->second.GetCenter() + side * icon->second.GetSize() * 0.5f);
        const auto end       = ed::CanvasToScreen(m_CreateNodeDialog.GetDropPoint());
        const auto direction = side * ed::GetStyle().LinkStrength * scale;
        auto drawList = ImGui::GetWindowDrawList();
        drawList->PushClipRect(canvasMin, canvasMax, true);
        drawList->AddBezierCubic(start, start + direction, end - direction, end, ImColor(PinColor(*pin)), (pin->m_Flow ? 2.5f : 1.5f) * scale);
        drawList->PopClipRect();
    }

    // A red disc on the top-left cut of each node with a breakpoint, to the canvas's scale.
    void ShowBreakpoints(const Document& document, ImVec2 canvasMin, ImVec2 canvasMax)
    {
        const auto scale    = ed::CanvasToScreen(ImVec2(1.0f, 0.0f)).x - ed::CanvasToScreen(ImVec2(0.0f, 0.0f)).x;
        const auto radius   = 6.0f * scale;
        auto       drawList = ImGui::GetWindowDrawList();
        drawList->PushClipRect(canvasMin, canvasMax, true);
        for (auto& node : document.GetGraph().Nodes())
        {
            if (!m_Debugger.HasBreakpoint(node.m_Id))
                continue;
            const auto center = ed::CanvasToScreen(ed::GetNodePosition(node.m_Id) + CutCenter(IsCompact(node)));
            drawList->AddCircleFilled(center, radius, IM_COL32(220, 48, 48, 255));
            drawList->AddCircle(center, radius, IM_COL32(32, 32, 32, 255), 0, 1.5f * scale);
        }
        drawList->PopClipRect();
    }

    // While a run is paused, a chevron above the node its last step ran, and a blue one above the
    // node it runs next; beside the pins, what the run holds for them: values of written outputs and
    // of linked inputs, and the flow along the link it took, past flow reroutes. Drawn to the
    // canvas's scale.
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
        const auto taken    = FlowChain(graph, m_Debugger.TakenLink());
        const auto from     = taken.empty() ? Id(0) : graph.FindLink(taken.front())->m_From;
        const auto to       = taken.empty() ? Id(0) : graph.FindLink(taken.back())->m_To;
        auto pill = [&](const Pin& pin, ImU32 color, const std::string& text)
        {
            auto icon = m_PinIcons.find(pin.m_Id);
            if (icon == m_PinIcons.end())
                return;
            const auto min   = ed::CanvasToScreen(icon->second.Min), max = ed::CanvasToScreen(icon->second.Max);
            const auto inner = text.empty() ? ImVec2(fontSize, fontSize) : font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text.c_str());
            const auto size  = inner + padding * 2.0f;
            const auto side  = PinSide(pin);
            const auto a     = side.x != 0.0f ? ImVec2(side.x < 0.0f ? min.x - gap - size.x : max.x + gap, (min.y + max.y - size.y) * 0.5f)
                                              : ImVec2((min.x + max.x - size.x) * 0.5f, side.y < 0.0f ? min.y - gap - size.y : max.y + gap);
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
                    if (pin.m_Id == from || pin.m_Id == to)
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

    // Tabs for the problems, the variables, the signature of a function whose graph shows, the
    // watches and the fibers of a run, and the breakpoints.
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
            tab("Breakpoints", counted("Breakpoints", m_Debugger.Breakpoints().size()), [&] { ShowBreakpointList(document); });
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

    // A row for each breakpoint, of any graph: its disc, its node, whether the run is paused there,
    // and a button that removes it. Choosing one goes to its node; one of a graph that isn't shown is
    // named with the graph's name.
    void ShowBreakpointList(const Document& document)
    {
        auto& breakpoints = m_Debugger.Breakpoints();
        if (breakpoints.empty())
        {
            ImGui::TextDisabled("No breakpoints: F9 on a node, or use its menu");
            return;
        }
        if (ImGui::Button("Remove All"))
        {
            m_Debugger.ClearBreakpoints();
            return;
        }
        if (!ImGui::BeginTable("##breakpoints", 3, ImGuiTableFlags_SizingFixedFit))
            return;
        ImGui::TableSetupColumn("Node");
        ImGui::TableSetupColumn("Run");
        ImGui::TableSetupColumn("Remove");
        const auto line = ImGui::GetTextLineHeight();
        std::optional<Id> removed;
        for (auto id : breakpoints)
        {
            ImGui::PushID(static_cast<int>(id));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const auto at = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(line, line));
            ImGui::GetWindowDrawList()->AddCircleFilled(at + ImVec2(line, line) * 0.5f, line * 0.3f, IM_COL32(220, 48, 48, 255));
            ImGui::SameLine();
            if (auto graph = GraphOf(document.GetBlueprint(), id))
            {
                auto label = graph->FindNode(id)->Name();
                if (graph != &document.GetGraph())
                    label = graph->GetName() + ": " + label;
                if (ImGui::Selectable(label.c_str()))
                    GoToNode(id);
            }
            else
                ImGui::TextDisabled("node %u, gone", static_cast<unsigned>(id));
            ImGui::TableNextColumn();
            if (m_Debugger.IsAtBreakpoint() && m_Debugger.NextNode() == id)
                ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_NavCursor), "paused here");
            ImGui::TableNextColumn();
            if (SquareButton("x"))
                removed = id;
            ImGui::PopID();
        }
        ImGui::EndTable();
        if (removed)
            m_Debugger.SetBreakpoint(*removed, false);
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

        std::vector<std::string> typeNames;
        for (auto head : blueprint.GetRegistry().ValueTypes())
            if (head->m_Arity == 0)
                typeNames.push_back(head->m_Name);
        for (auto variable : variables)
            typeNames.push_back(variable->m_Default.GetType()->m_Name);
        const auto typeWidth = ComboWidth(typeNames);

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
            ImGui::SetNextItemWidth(typeWidth);
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
        std::vector<std::string> typeTexts;
        for (auto& choice : choices)
            typeTexts.push_back(TermText(choice.m_Type, signature.m_Parameters));
        for (auto pins : { &signature.m_Inputs, &signature.m_Outputs })
            for (auto& pin : *pins)
                typeTexts.push_back(TermText(pin.m_Type, signature.m_Parameters));
        const auto typeWidth = ComboWidth(typeTexts);
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
                ImGui::SetNextItemWidth(typeWidth);
                if (ImGui::BeginCombo("##type", TermText(pin.m_Type, signature.m_Parameters).c_str()))
                {
                    for (size_t i = 0; i < choices.size(); ++i)
                        if (ImGui::Selectable(typeTexts[i].c_str(), choices[i].m_Type == pin.m_Type))
                            change = [&document, graph, input, key = pin.m_Key, type = choices[i].m_Type] { RetypeSignaturePin(document, graph, input, key, type); };
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

    // Selects the problem's node, and brings it into view, in its graph: a link's problem's, both
    // nodes at the link's ends, in the graph shown.
    void GoToProblem(size_t index)
    {
        auto problems = m_Problems.Problems();
        if (index >= problems.size())
            return;
        m_ProblemIndex = index;
        auto& diagnostic = problems[index].m_Diagnostic;
        auto& graph      = m_Document->GetGraph();
        auto  link       = diagnostic.m_Link && diagnostic.m_Graph == graph.GetId() ? graph.FindLink(diagnostic.m_Link) : nullptr;
        auto  from       = link ? EndNode(graph, link->m_From) : nullptr;
        auto  to         = link ? EndNode(graph, link->m_To) : nullptr;
        if (from && to)
        {
            ed::SelectNode(from->m_Id);
            ed::SelectNode(to->m_Id, true);
            ed::NavigateToSelection();
        }
        else if (auto node = problems[index].m_Node)
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
    // double-clicked or from its menu, renames it, and its menu duplicates and deletes it.
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
                rename = ImGui::MenuItem("Rename...", id == document.GetGraph().GetId() ? "F2" : nullptr);
                if (ImGui::MenuItem("Duplicate..."))
                    StartDuplicateFunction(id);
                if (ImGui::MenuItem("Delete"))
                    m_PendingDelete = id;
                ImGui::EndPopup();
            }
            if (rename)
                StartRenameFunction(id);
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
        ShowDuplicateFunction(document);
    }

    void StartRenameFunction(Id graph)
    {
        auto found = m_Document->GetBlueprint().FindGraph(graph);
        if (!found || !m_Document->GetBlueprint().FindFunction(graph))
            return;
        m_RenamedFunction = graph;
        m_RenameFocus     = true;
        std::snprintf(m_RenameText, sizeof(m_RenameText), "%s", found->GetName().c_str());
    }

    void ShowRenameFunction(Document& document)
    {
        if (auto name = NamePopup("##rename-function", m_RenameFocus, m_RenameText, document.GetBlueprint().FindFunction(m_RenamedFunction) != nullptr))
            RenameFunction(document, m_RenamedFunction, *name);
    }

    // The name offered is the function's, numbered as no function is.
    void StartDuplicateFunction(Id graph)
    {
        auto& blueprint = m_Document->GetBlueprint();
        auto  found     = blueprint.FindGraph(graph);
        auto  function  = blueprint.FindFunction(graph);
        if (!found || !function)
            return;
        std::string name;
        for (int i = 2; i < 1000; ++i)
        {
            name = found->GetName() + std::to_string(i);
            if (blueprint.CheckFunction(name, function->m_Signature))
                break;
        }
        m_DuplicatedFunction = graph;
        m_DuplicateFocus     = true;
        std::snprintf(m_DuplicateText, sizeof(m_DuplicateText), "%s", name.c_str());
    }

    // The name typed is checked as the blueprint will check it, so the reason it can't be shows at once.
    void ShowDuplicateFunction(Document& document)
    {
        auto& blueprint = document.GetBlueprint();
        auto  function  = blueprint.FindFunction(m_DuplicatedFunction);
        std::string problem;
        if (function && ImGui::IsPopupOpen("##duplicate-function"))
            if (auto check = blueprint.CheckFunction(m_DuplicateText, function->m_Signature); !check)
                problem = check.GetError().Message();
        if (auto name = NamePopup("##duplicate-function", m_DuplicateFocus, m_DuplicateText, function != nullptr, problem))
            DuplicateFunction(m_DuplicatedFunction, *name);
    }

    // A copy of the function under the name, shown as one undo step: its nodes where the function's
    // are, and its view the function's.
    void DuplicateFunction(Id graph, const std::string& name)
    {
        auto& document  = *m_Document;
        auto& blueprint = document.GetBlueprint();
        auto  source    = blueprint.FindGraph(graph);
        if (!source)
            return;
        std::vector<Id> nodes;
        for (auto& node : source->Nodes())
            nodes.push_back(node.m_Id);
        const auto old    = source->GetName();
        const auto layout = document.BuildDocumentState().m_NodesState;
        document.CaptureView();
        const auto views = document.m_NavigationState.m_Views;

        auto transaction = document.BeginUndoTransaction("Duplicate Function");
        auto copy        = blueprint.DuplicateFunction(graph, name);
        if (!copy)
        {
            auto message = copy.GetError().Message();
            LOGW("[Function] %s: %.*s", old.c_str(), static_cast<int>(message.size()), message.data());
            transaction->Discard();
            return;
        }
        auto& duplicate = *copy.GetValue();
        if (auto view = views.find(graph); view != views.end())
            document.m_NavigationState.m_Views[duplicate.GetId()] = view->second;
        ShowGraph(duplicate.GetId());
        for (size_t i = 0; i < nodes.size() && i < duplicate.Nodes().size(); ++i)
        {
            auto state = std::find_if(layout.begin(), layout.end(), [node = nodes[i]](const ed::NodeState& state) { return state.Id.Get() == node; });
            if (state == layout.end())
                continue;
            auto copied = *state;
            copied.Id   = duplicate.Nodes()[i].m_Id;
            ed::SetNodeState(copied);
        }
        transaction->AddAction("%s duplicated as %s", old.c_str(), name.c_str());
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
            const auto startTab = GrowTabNode(linkBuilder->m_StartPinId), endTab = GrowTabNode(linkBuilder->m_EndPinId);
            if (startTab || endTab)
            {
                LinkToTab(document, *linkBuilder, startTab ? startTab : endTab, static_cast<Id>((startTab ? linkBuilder->m_EndPinId : linkBuilder->m_StartPinId).Get()));
                return;
            }

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
            // A link dragged from a "+" makes no node.
            if (GrowTabNode(nodeBuilder->m_PinId))
            {
                nodeBuilder->Reject();
                return;
            }

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

        // A reroute's links stay to be joined past it, once the other nodes are gone.
        std::vector<Id> nodesToDelete, reroutesToDissolve;
        while (auto nodeDeleter = itemDeleter.QueryDeletedNode())
        {
            deferredTransaction->Begin("Delete Item");
            const auto id       = static_cast<Id>(nodeDeleter->m_NodeId.Get());
            const auto found    = graph.FindNode(id);
            const auto dissolve = found && IsReroute(*found);
            if (nodeDeleter->Accept(!dissolve))
                (dissolve ? reroutesToDissolve : nodesToDelete).push_back(id);
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
        for (auto reroute : reroutesToDissolve)
            DissolveReroute(graph, reroute);
        nodesToDelete.insert(nodesToDelete.end(), reroutesToDissolve.begin(), reroutesToDissolve.end());

        if (!nodesToDelete.empty() || brokenLinkCount)
        {
            LOGV("[HandleDestroyAction] %d node%s deleted, %d link%s broken",
                static_cast<int>(nodesToDelete.size()), nodesToDelete.size() != 1 ? "s" : "",
                brokenLinkCount, brokenLinkCount != 1 ? "s" : "");
        }
    }

    void HandleContextMenuAction()
    {
        // Ctrl+X, C and D take the nodes the editor's shortcut names; Ctrl+V pastes where the mouse
        // points, or in the middle of the view while it points elsewhere.
        auto createNode = ed::ShowBackgroundContextMenu();
        auto cut = false, copy = false, paste = false, duplicate = false;
        std::vector<Id> context;
        if (ed::BeginShortcut())
        {
            createNode = ed::AcceptCreateNode() || createNode;
            cut        = ed::AcceptCut();
            copy       = ed::AcceptCopy();
            paste      = ed::AcceptPaste();
            duplicate  = ed::AcceptDuplicate();
            std::vector<ed::NodeId> nodes(static_cast<size_t>(ed::GetActionContextNodes(nullptr, 0)));
            nodes.resize(static_cast<size_t>(ed::GetActionContextNodes(nodes.data(), static_cast<int>(nodes.size()))));
            for (auto node : nodes)
                context.push_back(static_cast<Id>(node.Get()));
            ed::EndShortcut();
        }
        if (cut)
            CutToClipboard(context);
        if (copy)
            CopyToClipboard(context);
        if (duplicate)
            Duplicate(context);
        if (paste)
        {
            const auto view  = ed::GetViewState();
            const auto mouse = ed::ScreenToCanvas(ImGui::GetMousePos());
            const auto over  = mouse.x >= view.VisibleMin.x && mouse.y >= view.VisibleMin.y && mouse.x <= view.VisibleMax.x && mouse.y <= view.VisibleMax.y;
            PasteFromClipboard(over ? mouse : (view.VisibleMin + view.VisibleMax) * 0.5f);
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
            if (auto tab = GrowTabNode(contextPinId))
                m_NodeContextMenu.Open(tab);
            else
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
        const auto orphan     = pin ? nullptr : graph.NodeOfOrphan(static_cast<Id>(ed::GetHoveredPin().Get()));
        const auto pinLinked  = (pin && HasLinks(graph, pin->m_Id)) || orphan;
        const auto pinValue   = pin && HasValueEditor(graph, *pin);
        const auto pinTyped   = pin && PinVariable(graph, *pin);
        const auto pinSplits  = pin && (SplittableStruct(*pin) || !SplitOf(*graph.NodeOf(pin->m_Id), pin->m_Key).empty());

        auto owner = node ? node : pin ? graph.NodeOf(pin->m_Id) : orphan;
        if (auto tab = GrowTabNode(ed::GetHoveredPin()); !owner && tab)
            owner = graph.FindNode(tab);
        auto&      blueprint = document.GetBlueprint();
        const auto reroute   = owner && IsReroute(*owner) ? owner : nullptr;
        const auto canGrow   = owner && GrowthOf(blueprint, *owner).m_Enabled;
        const auto canShrink = owner && std::any_of(owner->m_Pins.begin(), owner->m_Pins.end(), [&blueprint, owner](const Pin& candidate) { return static_cast<bool>(RemovalOf(blueprint, *owner, candidate)); });
        const auto removal   = pin ? RemovalOf(blueprint, *graph.NodeOf(pin->m_Id), *pin) : PinRemoval();

        CanvasActions actions(overCanvas);
        actions.Gesture("Create Link", "Drag Pin to Pin", pin != nullptr);
        actions.Gesture("Create Linked Node", "Drag Pin to Canvas", pin != nullptr);
        actions.Gesture("Insert Reroute", "Double-Click", link != nullptr);
        actions.Gesture("Dissolve Reroute", "Delete", reroute != nullptr);
        if (actions.Press("Remove Reroute", ImGuiMod_Alt | ImGuiKey_MouseLeft, reroute != nullptr))
        {
            RemoveReroute(document, reroute->m_Id);
            return;
        }
        if (actions.Press("Break Pin's Links", ImGuiMod_Alt | ImGuiKey_MouseLeft, pinLinked && !reroute))
            ed::BreakLinks(ed::GetHoveredPin());
        if (actions.Press("Remove Pin", ImGuiMod_Alt | ImGuiKey_MouseLeft, removal && !pinLinked))
        {
            RemovePin(document, graph.NodeOf(pin->m_Id)->m_Id, removal);
            return;
        }
        if (actions.Press("Watch Pin", ImGuiMod_Ctrl | ImGuiKey_MouseLeft, pin && !pin->m_Flow))
            m_Debugger.SetWatched(pin->m_Id, !m_Debugger.IsWatched(pin->m_Id));
        actions.Gesture("Edit Value", "Click", pinValue);
        actions.Gesture("Show Node's Problems", "Click Count", owner && (m_Problems.Count(Severity::Error, owner->m_Id) > 0 || m_Problems.Count(Severity::Warning, owner->m_Id) > 0));
        actions.Gesture("Add Pin", "Click +, or Drop a Link on It", canGrow);
        actions.Gesture("Remove Pin", "Click - Beside It", canShrink);
        actions.Gesture("Choose Type", "Right-Click", pinTyped);
        actions.Gesture("Split or Recombine", "Double-Click", pinSplits);
        actions.Gesture("Open Function", "Double-Click", !pin && CalledFunction(document, node));
        actions.Gesture("Edit Signature", "Double-Click", !pin && node && SignatureNodeOf(document.GetBlueprint(), *node).m_Function);

        // The node editor's own actions, its groups being comments, but for Create Link and the
        // context menus, which the example lists as it carries them out.
        for (auto action : { ed::Action::Center, ed::Action::Frame, ed::Action::Pan, ed::Action::Zoom, ed::Action::Select, ed::Action::ToggleSelection, ed::Action::ClearSelection,
                             ed::Action::SelectNodes, ed::Action::AddToSelection, ed::Action::SelectGroups, ed::Action::SelectLinks, ed::Action::Move, ed::Action::MoveWithoutSnapping,
                             ed::Action::MoveGroupAlone, ed::Action::ResizeGroup, ed::Action::DeleteSelection, ed::Action::DeleteLink, ed::Action::Cut, ed::Action::Copy,
                             ed::Action::Paste, ed::Action::Duplicate, ed::Action::CreateNode })
        {
            constexpr std::string_view group = "Group";
            const auto info = ed::GetActionInfo(action);
            auto       name = std::string(action == ed::Action::DeleteLink ? "Break Link" : info.Name);
            if (auto at = name.find(group); at != std::string::npos)
                name.replace(at, group.size(), "Comment");
            actions.Gesture(name.c_str(), info.Trigger, info.Available);
        }
        actions.Gesture("Create Node", "Right-Click", background);
        actions.Gesture("Edit Comment", "Double-Click", node && IsComment(*node));
        if (actions.Press("Comment Selection", ImGuiKey_C, ed::GetSelectedObjectCount() > 0 && !typing))
            CommentSelection(document);
        for (auto action : { &m_View_ToggleProblems, &m_View_NextProblem, &m_View_PreviousProblem, &m_Blueprint_Run, &m_Blueprint_Step, &m_Blueprint_Stop,
                             &m_View_NavigateBackward, &m_View_NavigateForward, &m_Blueprint_ToggleBreakpoint })
            if (actions.Press(action->GetName().c_str(), action->GetShortcut(), action->IsEnabled(), true))
                action->Execute();
        if (actions.Press("Rename Function", m_Blueprint_RenameFunction.GetShortcut(), m_Blueprint_RenameFunction.IsEnabled(), true))
            m_Blueprint_RenameFunction.Execute();

        if (m_ShowActions)
            actions.Draw(canvasMin, canvasMax, m_ShowDisabledActions);

        auto doubleClicked = graph.FindNode(static_cast<Id>(ed::GetDoubleClickedNode().Get()));
        if (doubleClicked && IsComment(*doubleClicked))
            m_NodeContextMenu.EditComment(*doubleClicked);
        else if (auto function = CalledFunction(document, doubleClicked))
            m_PendingGraph = function->m_Graph;
        else if (doubleClicked && SignatureNodeOf(document.GetBlueprint(), *doubleClicked).m_Function)
            RequestPanel("Signature");
        if (auto clicked = graph.FindLink(static_cast<Id>(ed::GetDoubleClickedLink().Get())))
        {
            InsertReroute(document, clicked->m_Id, ed::ScreenToCanvas(ImGui::GetMousePos()));
            return;
        }
        // A pin a disabled link ends at, or an orphan, double-clicked goes to the link's other end,
        // unless it's a struct pin, which splits, or a field, which recombines.
        const auto clickedId     = static_cast<Id>(ed::GetDoubleClickedPin().Get());
        const auto disabledOther = [&graph, clickedId]() -> Id
        {
            for (auto& link : graph.Links())
                if (!link.m_Active && (link.m_From == clickedId || link.m_To == clickedId))
                    return link.m_From == clickedId ? link.m_To : link.m_From;
            return 0;
        };
        if (auto clicked = graph.FindPin(clickedId))
        {
            const auto owner = graph.NodeOf(clicked->m_Id)->m_Id;
            if (SplittableStruct(*clicked))
                SplitStructPin(document, owner, clicked->m_Key, true);
            else if (auto splitOf = SplitOf(*graph.NodeOf(clicked->m_Id), clicked->m_Key); !splitOf.empty())
                SplitStructPin(document, owner, splitOf, false);
            else if (auto other = disabledOther())
                GoToEnd(graph, other);
        }
        else if (auto other = clickedId && graph.NodeOfOrphan(clickedId) ? disabledOther() : Id(0))
            GoToEnd(graph, other);
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
        const auto hoveredId = static_cast<Id>(ed::GetHoveredPin().Get());
        auto hoveredPin  = graph.FindPin(hoveredId);
        auto hoveredNode = graph.FindNode(static_cast<Id>(ed::GetHoveredNode().Get()));
        if (!hoveredNode && hoveredPin)
            hoveredNode = graph.NodeOf(hoveredPin->m_Id);
        auto hoveredOrphan = hoveredPin ? nullptr : graph.NodeOfOrphan(hoveredId);
        if (!hoveredNode && hoveredOrphan)
            hoveredNode = hoveredOrphan;
        if (hoveredNode && IsComment(*hoveredNode))
            return;

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
            else if (auto orphan = hoveredOrphan ? hoveredOrphan->FindOrphan(hoveredId) : nullptr)
            {
                ImGui::Separator();
                ImGui::TextUnformatted("Pin, gone: links name it still");
                ImGui::Bullet(); ImGui::Text("ID: %u", static_cast<unsigned>(orphan->m_Id));
                ImGui::Bullet(); ImGui::Text("Key: %s", orphan->m_Key.c_str());
                ImGui::Bullet(); ImGui::Text("Name: %s", OrphanName(*hoveredOrphan, *orphan).c_str());
                ImGui::Bullet(); ImGui::TextUnformatted(orphan->m_Direction == Direction::Input ? "input" : "output");
            }

            const auto pinId = hoveredPin || hoveredOrphan ? hoveredId : 0;
            diagnostics(pinId ? 0 : hoveredNode->m_Id, pinId);

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
        if (!File_Close([this] { File_New(); }))
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

        if (!File_Close([this, opened = std::string(path)] { File_Open(opened); }))
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

    // The dialogs start in the directory of the document's file, and show blueprints alone. A
    // dialog that failed tells the log why; one the user cancelled doesn't.
    static std::string DialogDirectory(std::string_view documentPath)
    {
        auto separator = documentPath.find_last_of("\\/");
        if (separator == std::string_view::npos)
            return {};

        return std::string(documentPath.substr(0, separator + 1));
    }

    static constexpr nfdu8filteritem_t c_BlueprintFiles[] = { { "Blueprint Files", "bp" } };

    static bool DialogPicked(nfdresult_t result)
    {
        if (result == NFD_ERROR)
            LOGE("[File] The file dialog failed: %s", NFD::GetError());
        return result == NFD_OKAY;
    }

    bool File_Open()
    {
        auto directory = m_Document ? DialogDirectory(m_Document->m_Path) : std::string();
        NFD::UniquePathU8 path;
        if (!DialogPicked(NFD::OpenDialog(path, c_BlueprintFiles, 1, directory.empty() ? nullptr : directory.c_str())))
            return false;

        std::string error;
        if (!File_Open(path.get(), &error) && !error.empty())
        {
            LOGE("%s", error.c_str());
            m_OpenFailure = error;

            return false;
        }

        return true;
    }

    // Closes the document now, but for one with changes not saved: a popup asks whether to save
    // them, and once the document closed, 'then' goes on with what closing it was for.
    bool File_Close(std::function<void()> then = {})
    {
        if (!File_IsOpen())
            return true;

        if (File_IsModified())
        {
            m_CloseRequest = std::move(then);
            return false;
        }

        CloseDocument();

        return true;
    }

    void CloseDocument()
    {
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
    }

    void ShowClosePrompt()
    {
        const auto title = "Save Changes?###ClosePrompt";
        if (m_CloseRequest && !ImGui::IsPopupOpen(title))
            ImGui::OpenPopup(title);
        if (!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return;

        ImGui::TextUnformatted("Do you want to save changes to this blueprint before closing it?");
        ImGui::Spacing();
        auto close = [this](bool save)
        {
            auto then = std::exchange(m_CloseRequest, std::nullopt);
            ImGui::CloseCurrentPopup();
            if (!m_Document || (save && !File_Save()))
                return;
            CloseDocument();
            if (then && *then)
                (*then)();
        };
        if (ImGui::Button("Save"))
            close(true);
        ImGui::SameLine();
        if (ImGui::Button("Don't Save"))
            close(false);
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            m_CloseRequest.reset();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    void ShowOpenFailure()
    {
        const auto title = "Open Blueprint###OpenFailure";
        if (!m_OpenFailure.empty() && !ImGui::IsPopupOpen(title))
            ImGui::OpenPopup(title);
        if (!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return;

        ImGui::TextUnformatted(m_OpenFailure.c_str());
        ImGui::Spacing();
        if (ImGui::Button("OK") || ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            m_OpenFailure.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
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

        Application_GetMostRecentlyOpenFileList().Add(std::string(path));

        return true;
    }

    bool File_SaveAs()
    {
        auto directory = DialogDirectory(m_Document->m_Path);
        auto name      = m_Document->m_Path.substr(directory.size());
        NFD::UniquePathU8 path;
        if (!DialogPicked(NFD::SaveDialog(path, c_BlueprintFiles, 1, directory.empty() ? nullptr : directory.c_str(), name.empty() ? nullptr : name.c_str())))
            return false;

        if (!File_SaveAsEx(path.get()))
            return false;

        m_Document->SetPath(path.get());

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

    // The Edit menu's take the selection with the content of its comments, as the editor's Ctrl+X,
    // C and D do, and paste in the middle of the view.
    void Edit_Cut()
    {
        CutToClipboard(SelectedNodesWithContent(m_Document->GetGraph()));
    }

    void Edit_Copy()
    {
        CopyToClipboard(SelectedNodesWithContent(m_Document->GetGraph()));
    }

    void Edit_Paste()
    {
        const auto view = ed::GetViewState();
        PasteFromClipboard((view.VisibleMin + view.VisibleMax) * 0.5f);
    }

    void Edit_Duplicate()
    {
        Duplicate(SelectedNodesWithContent(m_Document->GetGraph()));
    }

    void CopyToClipboard(std::span<const Id> nodes)
    {
        ImGui::SetClipboardText(CopyNodes(*m_Document, nodes).c_str());
    }

    void CutToClipboard(std::span<const Id> nodes)
    {
        CopyToClipboard(nodes);
        for (auto node : nodes)
            ed::DeleteNode(node);
    }

    void PasteFromClipboard(ImVec2 center)
    {
        const auto text   = ImGui::GetClipboardText();
        const auto pasted = PasteNodes(*m_Document, text ? text : "", center, "Paste");
        if (!pasted)
        {
            const auto message = pasted.GetError().Message();
            LOGW("[Edit] Paste: %.*s", static_cast<int>(message.size()), message.data());
        }
    }

    void Duplicate(std::span<const Id> nodes)
    {
        const auto duplicated = DuplicateNodes(*m_Document, nodes, ImVec2(32.0f, 32.0f));
        if (!duplicated)
        {
            const auto message = duplicated.GetError().Message();
            LOGW("[Edit] Duplicate: %.*s", static_cast<int>(message.size()), message.data());
        }
    }

    // As the Delete key deletes them.
    void Edit_Delete()
    {
        for (auto node : GetSelectedNodes())
            ed::DeleteNode(node);
        for (auto link : GetSelectedLinks())
            ed::DeleteLink(link);
    }

    void Edit_SelectAll()
    {
        for (auto& node : m_Document->GetGraph().Nodes())
            ed::SelectNode(node.m_Id, true);
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

    // From the problem chosen last, or from the first or the last one.
    void View_GoToProblem(bool next)
    {
        const auto problems = m_Problems.Problems().size();
        if (problems == 0)
            return;
        if (m_ProblemIndex >= problems)
            GoToProblem(next ? 0 : problems - 1);
        else
            GoToProblem(next ? (m_ProblemIndex + 1) % problems : (m_ProblemIndex + problems - 1) % problems);
    }

    // The node under the mouse, or the one of a pin under it, or else the selected ones.
    std::vector<Id> BreakpointNodes()
    {
        auto& graph = m_Document->GetGraph();
        auto  node  = graph.FindNode(static_cast<Id>(ed::GetHoveredNode().Get()));
        if (auto pin = graph.FindPin(static_cast<Id>(ed::GetHoveredPin().Get())); !node && pin)
            node = graph.NodeOf(pin->m_Id);
        if (node && (IsComment(*node) || IsReroute(*node)))
            node = nullptr;
        auto nodes = node ? std::vector<Id>{ node->m_Id } : GetSelectedNodes();
        std::erase_if(nodes, [&graph](Id id) { auto found = graph.FindNode(id); return !found || IsComment(*found) || IsReroute(*found); });
        return nodes;
    }

    // On for all of them, unless the first has one: then off.
    void Blueprint_ToggleBreakpoint()
    {
        auto nodes = BreakpointNodes();
        if (nodes.empty())
            return;
        const auto enabled = !m_Debugger.HasBreakpoint(nodes.front());
        for (auto node : nodes)
            m_Debugger.SetBreakpoint(node, enabled);
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
    Id                             m_ControlsNode = 0;   // the node whose pins' + and - show at full
    std::unordered_map<Id, ImRect> m_LastPinIcons;       // the frame before's
    std::unordered_map<Id, ImVec2> m_PinSides;           // sides links meet pins on other than their kind's: reroutes'
    Id                        m_RenamedFunction = 0;     // its graph's ID
    bool                      m_RenameFocus     = false; // the rename popup opens, its name focused
    char                      m_RenameText[64]  = {};
    Id                        m_DuplicatedFunction = 0;     // its graph's ID
    bool                      m_DuplicateFocus     = false; // the duplicate's name popup opens, its name focused
    char                      m_DuplicateText[64]  = {};
    Debugger                  m_Debugger;
    ProblemList               m_Problems;
    std::unordered_set<Id>    m_Bubbles;    // nodes whose problems show
    std::unordered_set<Id>    m_RunBubbles; // nodes whose bubble a run's error opened
    std::optional<LinkQuery>  m_LinkQuery;
    std::optional<std::function<void()>> m_CloseRequest; // the close the popup asks about, and what follows it
    std::string               m_OpenFailure;             // shown in a popup
    bool                      m_HasFileDialogs = false;
    bool                      m_ShowActions         = true;
    bool                      m_ShowDisabledActions = false;
    bool                      m_ShowPanels          = true;
    float                     m_PanelsHeight = 0.0f;   // as the bar above the panels left it; 0 until they first show
    std::string               m_PanelRequest;          // the panel to choose as the panels show next
    size_t                    m_ProblemIndex = SIZE_MAX; // the problem chosen last

    CreateNodeDialog m_CreateNodeDialog;
    NodeContextMenu  m_NodeContextMenu;
    PinContextMenu   m_PinContextMenu;
    LinkContextMenu  m_LinkContextMenu;
    LinkDropMenu     m_LinkDropMenu;

    Action m_File_New        = { ICON_FA_FILE,        "New",          ImGuiMod_Ctrl | ImGuiKey_N,                  [this] { File_New();    } };
    Action m_File_Open       = { ICON_FA_FOLDER_OPEN, "Open...",      ImGuiMod_Ctrl | ImGuiKey_O,                  [this] { File_Open();   } };
    Action m_File_SaveAs     = { ICON_FA_FILE_EXPORT, "Save As...",   ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S, [this] { File_SaveAs(); } };
    Action m_File_Save       = { ICON_FA_FLOPPY_DISK, "Save",         ImGuiMod_Ctrl | ImGuiKey_S,                  [this] { File_Save();   } };
    Action m_File_Close      = { ICON_FA_XMARK,       "Close",        [this] { File_Close();  } };
    Action m_File_Exit       = { ICON_FA_POWER_OFF,   "Exit",         [this] { File_Exit();   } };

    Action m_Edit_Undo       = { ICON_FA_ROTATE_LEFT,  "Undo",         ImGuiMod_Ctrl | ImGuiKey_Z, [this] { Edit_Undo();      } };
    Action m_Edit_Redo       = { ICON_FA_ROTATE_RIGHT, "Redo",         ImGuiMod_Ctrl | ImGuiKey_Y, [this] { Edit_Redo();      } };
    Action m_Edit_Cut        = { ICON_FA_SCISSORS,     "Cut",          ImGuiMod_Ctrl | ImGuiKey_X, [this] { Edit_Cut();       } };
    Action m_Edit_Copy       = { ICON_FA_COPY,         "Copy",         ImGuiMod_Ctrl | ImGuiKey_C, [this] { Edit_Copy();      } };
    Action m_Edit_Paste      = { ICON_FA_PASTE,        "Paste",        ImGuiMod_Ctrl | ImGuiKey_V, [this] { Edit_Paste();     } };
    Action m_Edit_Duplicate  = { ICON_FA_CLONE,        "Duplicate",    ImGuiMod_Ctrl | ImGuiKey_D, [this] { Edit_Duplicate(); } };
    Action m_Edit_Delete     = { ICON_FA_TRASH,        "Delete",       ImGuiKey_Delete,            [this] { Edit_Delete();    } };
    Action m_Edit_SelectAll  = { ICON_FA_OBJECT_GROUP, "Select All",   ImGuiMod_Ctrl | ImGuiKey_A, [this] { Edit_SelectAll(); } };

    Action m_View_NavigateBackward = { ICON_FA_ARROW_LEFT,           "Navigate Backward",         ImGuiMod_Alt | ImGuiKey_LeftArrow,           [this] { View_NavigateBackward(); } };
    Action m_View_NavigateForward  = { ICON_FA_ARROW_RIGHT,          "Navigate Forward",          ImGuiMod_Alt | ImGuiKey_RightArrow,          [this] { View_NavigateForward();  } };
    Action m_View_NextProblem      = { ICON_FA_ARROW_DOWN,           "Next Problem",              ImGuiKey_F8,                                 [this] { View_GoToProblem(true);  } };
    Action m_View_PreviousProblem  = { ICON_FA_ARROW_UP,             "Previous Problem",          ImGuiMod_Shift | ImGuiKey_F8,                [this] { View_GoToProblem(false); } };
    Action m_View_ToggleProblems   = { ICON_FA_TRIANGLE_EXCLAMATION, "Show or Hide All Problems", ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_D, [this] { ToggleBubbles();          } };

    Action m_Blueprint_Start = { ICON_FA_CIRCLE_PLAY,  "Start",        [this] { Blueprint_Start(); } };
    Action m_Blueprint_Step  = { ICON_FA_FORWARD_STEP, "Step",         ImGuiKey_F10,                 [this] { Blueprint_Step();  } };
    Action m_Blueprint_Stop  = { ICON_FA_STOP,         "Stop",         ImGuiMod_Shift | ImGuiKey_F5, [this] { Blueprint_Stop();  } };
    Action m_Blueprint_Run   = { ICON_FA_PLAY,         "Run",          ImGuiKey_F5,                  [this] { Blueprint_Run();   } };
    Action m_Blueprint_ToggleBreakpoint = { ICON_FA_CIRCLE_DOT, "Toggle Breakpoint", ImGuiKey_F9, [this] { Blueprint_ToggleBreakpoint(); } };
    Action m_Blueprint_AddFunction = { ICON_FA_FLORIN_SIGN, "Add Function", [this] { Blueprint_AddFunction(); } };
    Action m_Blueprint_RenameFunction = { ICON_FA_I_CURSOR, "Rename Function...", ImGuiKey_F2, [this] { StartRenameFunction(m_Document->GetGraph().GetId()); } };
    Action m_Blueprint_DuplicateFunction = { ICON_FA_CLONE, "Duplicate Function...", [this] { StartDuplicateFunction(m_Document->GetGraph().GetId()); } };
    Action m_Blueprint_RemoveOrphans = { ICON_FA_LINK_SLASH, "Remove All Orphaned Pins", [this] { RemoveOrphans(*m_Document, 0); } };
    Action m_Blueprint_AddVariable = { ICON_FA_DATABASE, "Add Variable", [this] { AddVariable(*m_Document); } };
};


int Main(int argc, char** argv)
{
    BlueprintEditorExample example("Blueprints2", argc, argv);

    if (example.Create())
        return example.Run();

    return 0;
}
