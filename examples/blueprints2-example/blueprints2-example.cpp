# define IMGUI_DEFINE_MATH_OPERATORS
# include <application.h>
# include <imgui_internal.h>
# include <tinyfiledialogs.h>

# include "crude_logger.h"
# include "crude_blueprint.h"
# include "crude_blueprint_library.h"
# include "blueprint_editor_document.h"
# include "blueprint_editor_utilities.h"
# include "blueprint_editor_icons.h"
# include "crude_layout.h"
# include "imgui_extras.h"
# include "imgui_node_editor.h"

# include "crude_json.h"

# include <algorithm>
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

// What every document uses: the built-in value types and casts, and the library's node types.
static std::shared_ptr<const Registry> ExampleRegistry()
{
    static const std::shared_ptr<const Registry> registry = []
    {
        auto made = std::make_shared<Registry>();
        [[maybe_unused]] auto builtins = RegisterBuiltins(*made);
        [[maybe_unused]] auto library  = RegisterLibrary(*made);
        IM_ASSERT(builtins && library);
        return made;
    }();
    return registry;
}

static std::shared_ptr<TypeTable> ExampleTypes()
{
    static const auto types = std::make_shared<TypeTable>();
    return types;
}

static bool HasStart(const Graph& graph)
{
    for (auto& node : graph.Nodes())
        if (!node.m_Type->EventName(node.m_Config).empty())
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

static ed::EditorContext* m_Editor = nullptr;




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

    // Each document has its own editor, so nothing of one shows in the next. The
    // document keeps the layout; the editor stays out of imgui.ini.
    void CreateDocumentEditor()
    {
        ed::DestroyEditor(m_Editor);

        ed::Config config;
        config.SettingsName = nullptr;
        m_Editor = ed::CreateEditor(&config);
        ed::SetCurrentEditor(m_Editor);
    }

    void DestroyDocumentEditor()
    {
        ed::DestroyEditor(m_Editor);
        m_Editor = nullptr;
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

        CreateDocumentEditor();

        CreateExampleDocument();

        for (auto type : ExampleRegistry()->NodeTypes())
            m_OverlayLogger.AddKeyword(type->m_Title);

        ed::SetCurrentEditor(nullptr);
    }

    void OnStop() override
    {
        DestroyDocumentEditor();
    }

    void OnFrame(float deltaTime) override
    {
        IM_UNUSED(deltaTime);

        ed::SetCurrentEditor(m_Editor);

        if (m_Document)
        {
            m_Debugger.Update(*m_Document);
            m_Problems.Update(*m_Document, m_Debugger);

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
            ed::Begin("###main_editor");

            CommitBlueprintNodes(*m_Document);

            HandleCreateAction(*m_Document);
            HandleDestroyAction(*m_Document);
            HandleContextMenuAction();

            ShowDialogs(*m_Document);

            ShowInfoTooltip(*m_Document);

            ed::End();
            canvasMin = ImGui::GetItemRectMin();
            canvasMax = ImGui::GetItemRectMax();

            ShowBubbles(*m_Document, canvasMin, canvasMax);
            DeclareActions(*m_Document, canvasMin, canvasMax);

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

        m_View_NavigateBackward.SetEnabled(hasDocument && false);
        m_View_NavigateForward.SetEnabled(hasDocument && false);

        // An error blocks a run, never an edit or saving.
        auto canStart = hasDocument && HasStart(m_Document->GetGraph()) && !m_Problems.BlocksRun();

        m_Blueprint_Start.SetEnabled(canStart);
        m_Blueprint_Step.SetEnabled(m_Debugger.IsActive() && !m_Debugger.IsRunning());
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

        auto& graph  = m_Document->GetGraph();
        auto  errors = m_Problems.Count(Severity::Error);

        ImGui::SameLine();
        if (errors > 0)
            ImGui::Text("Status: %d error%s", errors, errors != 1 ? "s" : "");
        else if (m_Debugger.IsRunning())
            ImGui::TextUnformatted("Status: running");
        else if (m_Debugger.IsActive())
            ImGui::TextUnformatted("Status: paused");
        else
            ImGui::TextUnformatted("Status: -");

        if (m_Debugger.IsActive())
        {
            auto current = graph.FindNode(m_Debugger.CurrentNode());
            auto next    = graph.FindNode(m_Debugger.NextNode());
            ImGui::SameLine(); ImGui::Spacing();
            ImGui::SameLine(); ImGui::Text("Current: %s", current ? current->Name().c_str() : "-");
            ImGui::SameLine(); ImGui::Spacing();
            ImGui::SameLine(); ImGui::Text("Next: %s", next ? next->Name().c_str() : "-");
        }
    }

    // Iterate over the graph's nodes and commit them to node editor.
    void CommitBlueprintNodes(Document& document)
    {
        const auto iconSize = ImVec2(ImGui::GetTextLineHeight(), ImGui::GetTextLineHeight());

        auto& graph = document.GetGraph();

        // A pin with an active link has a filled icon, and an input with one, no value. An input
        // whose link is disabled has its icon slashed.
        std::unordered_set<Id> linked, disabled;
        for (auto& link : graph.Links())
        {
            if (!link.m_Active)
            {
                disabled.insert(link.m_To);
                continue;
            }
            linked.insert(link.m_From);
            linked.insert(link.m_To);
        }

        // An edit made with a node's widgets, done once the nodes are drawn.
        std::function<void()> edit;

        DebugOverlay debugOverlay(m_Debugger);
        debugOverlay.Begin();

        for (auto& node : graph.Nodes())
        {
            const auto errors   = m_Problems.Count(Severity::Error, node.m_Id);
            const auto warnings = m_Problems.Count(Severity::Warning, node.m_Id);
            if (errors > 0)
                ed::PushStyleColor(ed::StyleColor_NodeBorder, ImColor(ProblemColor(Severity::Error)));

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

            auto& title = node.m_Type->m_Title;
            {
                auto id = node.m_Id;
                auto headerBackgroundRenderer = ImEx::ItemBackgroundRenderer([id](ImDrawList* drawList)
                {
                    auto border   = ed::GetStyle().NodeBorderWidth;
                    auto rounding = ed::GetStyle().NodeRounding;

                    auto itemMin = ImGui::GetItemRectMin();
                    auto itemMax = ImGui::GetItemRectMax();

                    auto nodeStart = ed::GetNodePosition(id);
                    auto nodeSize  = ed::GetNodeSize(id);

                    itemMin   = nodeStart;
                    itemMin.x = itemMin.x + border - 0.5f;
                    itemMin.y = itemMin.y + border - 0.5f;
                    itemMax.x = nodeStart.x + nodeSize.x - border + 0.5f;
                    itemMax.y = itemMax.y + ImGui::GetStyle().ItemSpacing.y + 0.5f;

                    drawList->AddRectFilled(itemMin, itemMax, IM_COL32(255, 255, 255, 64), rounding, ImDrawFlags_RoundTop);
                });

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
                ImGui::EndGroup();

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
                if (ImGui::SmallButton("+"))
                    edit = [&document, id = node.m_Id, name = group.m_Name] { AddGroupPin(document, id, name); };
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::BeginDisabled(count <= group.m_Min);
                if (ImGui::SmallButton("-"))
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
                Icon(iconSize, PinIcon(pin), isLinked, PinColor(pin));
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

                // [Debug Overlay] Show value of the pin if node is currently executed
                debugOverlay.DrawInputPin(pin, isLinked);

                layout.NextRow();
                groupButtonsAfter(pin);
            }
            for (auto& group : node.m_Groups)
                if (!lastOfGroup.contains(&group))
                    groupButtons(group);

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

                ed::EndPin();

                // [Debug Overlay] Show value of the pin if node is currently executed
                debugOverlay.DrawOutputPin(pin);

                layout.NextRow();
                groupButtonsAfter(pin);
            }

            layout.End();

            ed::EndNode();
            if (errors > 0)
                ed::PopStyleColor();

            // [Debug Overlay] Show cursor over node
            debugOverlay.DrawNode(node.m_Id);
        }

        // Commit all links to editor. A disabled link shows, grey, until its problem is resolved;
        // one whose pin is gone has nothing to show between.
        for (auto& link : graph.Links())
        {
            auto from = graph.FindPin(link.m_From);
            if (!from || !graph.FindPin(link.m_To))
                continue;
            if (link.m_Active)
                ed::Link(link.m_Id, link.m_From, link.m_To, PinColor(*from), from->m_Flow ? 2.5f : 1.5f);
            else
                ed::Link(link.m_Id, link.m_From, link.m_To, ImVec4(0.5f, 0.5f, 0.5f, 0.4f), 1.0f);
        }

        debugOverlay.End();

        if (edit)
            edit();
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
                auto& text = lines.emplace_back(&problem, ProblemList::Text(document.GetGraph(), problem)).second;
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
        actions.Gesture("Break Link", "Alt+Click", link != nullptr);
        actions.Gesture("Delete Selection", "Delete", ed::GetSelectedObjectCount() > 0);
        actions.Gesture("Edit Value", "Click", pinValue);
        actions.Gesture("Show Node's Problems", "Click Count", owner && (m_Problems.Count(Severity::Error, owner->m_Id) > 0 || m_Problems.Count(Severity::Warning, owner->m_Id) > 0));
        actions.Gesture("Add Pin", "Click +", canGrow);
        actions.Gesture("Remove Pin", "Click -", canShrink);
        actions.Gesture("Choose Type", "Right-Click", pinTyped);
        actions.Gesture("Create Node", "Space", background && !typing);
        actions.Gesture("Create Node", "Right-Click", background);
        if (actions.Press("Show or Hide All Problems", ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_D, !m_Problems.Problems().empty()))
            ToggleBubbles();
        if (actions.Press("Run", ImGuiKey_F5, m_Blueprint_Run.IsEnabled(), true))
            m_Blueprint_Run.Execute();
        if (actions.Press("Step", ImGuiKey_F10, m_Blueprint_Step.IsEnabled(), true))
            m_Blueprint_Step.Execute();
        if (actions.Press("Stop", ImGuiMod_Shift | ImGuiKey_F5, m_Blueprint_Stop.IsEnabled(), true))
            m_Blueprint_Stop.Execute();

        if (m_ShowActions)
            actions.Draw(canvasMin, canvasMax);
    }

    void ShowDialogs(Document& document)
    {
        ed::Suspend();

        m_CreateNodeDialog.Show(document);
        m_NodeContextMenu.Show(document);
        m_PinContextMenu.Show(document);
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

        auto pinTooltip = [&graph](const char* label, const Pin& pin, bool showNode)
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
            ImGui::Bullet(); ImGui::Text("Type: %s", pin.m_Flow ? "flow" : graph.TypeText(pin.m_Id).c_str());
            ImGui::Bullet(); ImGui::Text("%s%s", pin.m_Direction == Direction::Input ? "input" : "output",
                pin.m_Requirement == Requirement::Required ? ", required" : pin.m_Requirement == Requirement::Recommended ? ", recommended" : "");
        };

        auto diagnostics = [this, &graph](Id node, Id pin)
        {
            for (auto& problem : m_Problems.Problems())
            {
                auto& diagnostic = problem.m_Diagnostic;
                auto  link = diagnostic.m_Link ? graph.FindLink(diagnostic.m_Link) : nullptr;
                auto  here = pin ? diagnostic.m_Pin == pin || (link && link->m_To == pin) : problem.m_Node == node;
                if (here)
                    ImGui::TextColored(ImColor(ProblemColor(diagnostic.m_Severity)), "%s", ProblemList::Text(graph, problem).c_str());
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

        CreateDocumentEditor();

        m_Document = std::make_unique<Document>(ExampleRegistry(), ExampleTypes());
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

        CreateDocumentEditor();

        m_Document = std::move(document);

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
        m_Problems = {};
        m_Bubbles.clear();
        m_RunBubbles.clear();
        m_Document = nullptr;

        DestroyDocumentEditor();

        UpdateTitle();

        return true;
    }

    bool File_SaveAsEx(std::string_view path)
    {
        if (!File_IsOpen())
            return true;

        // Closing the window saves outside OnFrame(), where no editor is current.
        auto previousEditor = ed::GetCurrentEditor();
        ed::SetCurrentEditor(m_Editor);
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
    }

    void View_NavigateForward()
    {
    }

    void Blueprint_Start()
    {
        m_Debugger.Start(*m_Document, false);
    }

    void Blueprint_Step()
    {
        m_Debugger.Step();
    }

    void Blueprint_Stop()
    {
        m_Debugger.Stop();
    }

    void Blueprint_Run()
    {
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
    Debugger                  m_Debugger;
    ProblemList               m_Problems;
    std::unordered_set<Id>    m_Bubbles;    // nodes whose problems show
    std::unordered_set<Id>    m_RunBubbles; // nodes whose bubble a run's error opened
    std::optional<LinkQuery>  m_LinkQuery;
    bool                      m_ShowActions = true;

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
};


int Main(int argc, char** argv)
{
    BlueprintEditorExample example("Blueprints2", argc, argv);

    if (example.Create())
        return example.Run();

    return 0;
}
