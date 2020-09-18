# include <application.h>
# define IMGUI_DEFINE_MATH_OPERATORS
# include <imgui_internal.h>
# include <tinyfiledialogs.h>
# include <inttypes.h>

# include "crude_blueprint.h"
# include "crude_blueprint_library.h"
# include "crude_blueprint_utilities.h"
# include "crude_layout.h"
# include "imgui_extras.h"
# include "imgui_node_editor.h"

# include "crude_json.h"

// TODO:
//  - figure out how to present possible action (ex. Alt + click on link)
//  -

//#include <float.h>
//unsigned int fp_control_state = _controlfp(_EM_INEXACT, _MCW_EM);

# include <functional>
# include <type_traits>
# include <map>

namespace ed = ax::NodeEditor;

using namespace crude_blueprint;
using namespace crude_blueprint_utilities;

using std::function;

enum class EventHandle : uint64_t { Invalid };

template <typename... Args>
struct Event
{
    using Delegate = function<void(Args...)>;

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

    void Invoke(Args&&... args)
    {
        vector<Delegate> delegates;
        delegates.reserve(m_Delegates.size());
        for (auto& entry : m_Delegates)
            delegates.push_back(entry.second);

        for (auto& delegate : delegates)
            delegate(args...);
    }

    EventHandle operator += (Delegate delegate)       { return Add(std::move(delegate)); }
    bool        operator -= (EventHandle eventHandle) { return Remove(eventHandle);      }
    void        operator () (Args&&... args)          { Invoke(std::forward<Args>(args)...); }

private:
    using EventHandleType = std::underlying_type_t<EventHandle>;

    map<EventHandle, Delegate> m_Delegates;
    EventHandleType            m_LastHandleId = 0;
};

static ed::EditorContext* g_Editor = nullptr;

# define LOGV(...)      g_OverlayLogger.Log(LogLevel::Verbose, __VA_ARGS__)
# define LOGI(...)      g_OverlayLogger.Log(LogLevel::Info, __VA_ARGS__)
# define LOGW(...)      g_OverlayLogger.Log(LogLevel::Warning, __VA_ARGS__)
# define LOGE(...)      g_OverlayLogger.Log(LogLevel::Error, __VA_ARGS__)
static OverlayLogger    g_OverlayLogger;



struct Action
{
    using OnChangeEvent     = Event<Action*>;
    using OnTriggeredEvent  = Event<>;

    Action() = default;
    Action(string_view name, OnTriggeredEvent::Delegate delegate = {});

    void SetName(string_view name);
    const string& GetName() const;

    void SetEnabled(bool set);
    bool IsEnabled() const;

    void Execute();

    OnChangeEvent       OnChange;
    OnTriggeredEvent    OnTriggered;

private:
    string  m_Name;
    bool    m_IsEnabled = true;
};

Action::Action(string_view name, OnTriggeredEvent::Delegate delegate)
    : m_Name(name.to_string())
{
    if (delegate)
        OnTriggered += std::move(delegate);
}

void Action::SetName(string_view name)
{
    if (m_Name == name)
        return;

    m_Name = name.to_string();

    OnChange(this);
}

const crude_blueprint::string& Action::GetName() const
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

struct Document
{
    void SetPath(string_view path)
    {
        m_Path = path.to_string();

        auto lastSeparator = m_Path.find_last_of("\\/");
        if (lastSeparator != string::npos)
            m_Name = m_Path.substr(lastSeparator + 1);
        else
            m_Name = path.to_string();
    }

    bool Load(string_view path)
    {
        auto loadResult = crude_json::value::load(path.to_string());
        if (!loadResult.second)
            return false;

        auto& value = loadResult.first;

        if (!value.is_object())
            return false;

        if (!value.contains("state") || !value.contains("data"))
            return false;

        auto& stateValue = value["state"];
        auto&  dataValue = value["data"];

        if (!stateValue.is_object())
            return false;

        if (!m_Blueprint.Load(dataValue))
            return false;

        m_EditorState = stateValue;

        return true;
    }

    bool Save(string_view path)
    {
        crude_json::value result;
        result["state"] = m_EditorState;
        m_Blueprint.Save(result["data"]);

        return result.save(path.to_string(), 4);
    }

    string              m_Path;
    string              m_Name;
    bool                m_IsModified = false;
    vector<string>      m_Undo;
    crude_json::value   m_EditorState;

    Blueprint           m_Blueprint;
};

static void Application_UpdateTitle();

static bool File_IsOpen();
static bool File_IsModified();
static void File_MarkModified();
static void File_New();
static bool File_OpenEx(string_view path, string* error = nullptr);
static bool File_Open();
static bool File_Close();
static bool File_SaveAsEx(string_view path);
static bool File_SaveAs();
static bool File_Save();
static void File_Exit();

static void Edit_Undo();
static void Edit_Redo();
static void Edit_Cut();
static void Edit_Copy();
static void Edit_Paste();
static void Edit_Duplicate();
static void Edit_Delete();
static void Edit_SelectAll();

static void Blueprint_Start();
static void Blueprint_Step();
static void Blueprint_Stop();
static void Blueprint_Run();

static unique_ptr<Document> g_Document;
static Blueprint*           g_Blueprint = nullptr;

static CreateNodeDialog g_CreateNodeDailog;
static NodeContextMenu  g_NodeContextMenu;
static PinContextMenu   g_PinContextMenu;
static LinkContextMenu  g_LinkContextMenu;

static Action g_File_New        = { "New",          &File_New    };
static Action g_File_Open       = { "Open...",      &File_Open   };
static Action g_File_Close      = { "Close",        &File_Close  };
static Action g_File_SaveAs     = { "Save As...",   &File_SaveAs };
static Action g_File_Save       = { "Save",         &File_Save   };
static Action g_File_Exit       = { "Exit",         &File_Exit   };

static Action g_Edit_Undo       = { "Undo",         &Edit_Undo      };
static Action g_Edit_Redo       = { "Redo",         &Edit_Redo      };
static Action g_Edit_Cut        = { "Cut",          &Edit_Cut       };
static Action g_Edit_Copy       = { "Copy",         &Edit_Copy      };
static Action g_Edit_Paste      = { "Paste",        &Edit_Paste     };
static Action g_Edit_Duplicate  = { "Duplicate",    &Edit_Duplicate };
static Action g_Edit_Delete     = { "Delete",       &Edit_Delete    };
static Action g_Edit_SelectAll  = { "Select All",   &Edit_SelectAll };

static Action g_Blueprint_Start = { "Start",        &Blueprint_Start };
static Action g_Blueprint_Step  = { "Step",         &Blueprint_Step  };
static Action g_Blueprint_Stop  = { "Stop",         &Blueprint_Stop  };
static Action g_Blueprint_Run   = { "Run",          &Blueprint_Run   };


static EntryPointNode* FindEntryPointNode();

static void Application_UpdateTitle()
{
    string title = Application_GetName();

    if (File_IsOpen())
    {
        title += " - ";
        title += g_Document->m_Name.c_str();

        if (File_IsModified())
            title += "*";
    }

    Application_SetTitle(title.c_str());
}

static bool File_IsOpen()
{
    return g_Document != nullptr;
}

static bool File_IsModified()
{
    return g_Document->m_IsModified;
}

static void File_MarkModified()
{
    if (g_Document->m_IsModified)
        return;

    g_Document->m_IsModified = true;

    Application_UpdateTitle();
}

static void File_New()
{
    if (!File_Close())
        return;

    LOGI("[File] New");

    g_Document = make_unique<Document>();
    g_Blueprint = &g_Document->m_Blueprint;
}

static bool File_OpenEx(string_view path, string* error)
{
    auto document = make_unique<Document>();
    if (!document->Load(path))
    {
        if (error)
        {
            ImGuiTextBuffer buffer;
            buffer.appendf("Failed to load blueprint from file \"%*s\".", static_cast<int>(path.size()), path.data());
            *error = buffer.c_str();
        }
        else
            LOGE("Failed to load blueprint from file \"%*s\".", static_cast<int>(path.size()), path.data());
        return false;
    }

    if (!File_Close())
        return false;

    LOGI("[File] Open \"%*s\"", static_cast<int>(path.size()), path.data());

    document->SetPath(path);

    g_Document = std::move(document);
    g_Blueprint = &g_Document->m_Blueprint;

    Application_UpdateTitle();

    ed::RestoreState();

    return true;
}

static bool File_Open()
{
    const char* filterPatterns[1] = { "*.bp" };
    auto path = tinyfd_openFileDialog(
        "Open Blueprint...",
        g_Document ? g_Document->m_Path.c_str() : nullptr,
        1, filterPatterns,
        "Blueprint Files (*.bp)", 0);
    if (!path)
        return false;

    string error;
    if (!File_OpenEx(path, &error) && !error.empty())
    {
        tinyfd_messageBox(
            (string(Application_GetName()) + " - Open Blueprint...").c_str(),
            error.c_str(),
            "ok", "error", 1);

        LOGE("%s", error.c_str());

        return false;
    }

    return true;
}

static bool File_Close()
{
    if (!File_IsOpen())
        return true;

    if (File_IsModified())
    {
        auto dialogResult = tinyfd_messageBox(
            Application_GetName(),
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

    g_Blueprint = nullptr;
    g_Document = nullptr;

    Application_UpdateTitle();

    return true;
}

static bool File_SaveAsEx(string_view path)
{
    if (!File_IsOpen())
        return true;

    ed::SaveState();

    if (!g_Document->Save(path))
    {
        LOGE("Failed to save blueprint to file \"%*s\".", static_cast<int>(path.size()), path.data());
        return false;
    }

    g_Document->m_IsModified = false;

    LOGI("[File] Save \"%*s\".", static_cast<int>(path.size()), path.data());

    return true;
}

static bool File_SaveAs()
{
    const char* filterPatterns[1] = { "*.bp" };
    auto path = tinyfd_saveFileDialog(
        "Save Blueprint...",
        g_Document->m_Path.c_str(),
        1, filterPatterns,
        "Blueprint Files (*.bp)");
    if (!path)
        return false;

    if (!File_SaveAsEx(path))
        return false;

    g_Document->SetPath(path);

    Application_UpdateTitle();

    return true;
}

static bool File_Save()
{
    if (!g_Document->m_Path.empty())
        return File_SaveAsEx(g_Document->m_Path);
    else
        return File_SaveAs();
}

static void File_Exit()
{
    if (!Application_Close())
        return;

    LOGI("[File] Quit");

    Application_Quit();
}

static void Edit_Undo()
{
}

static void Edit_Redo()
{
}

static void Edit_Cut()
{
}

static void Edit_Copy()
{
}

static void Edit_Paste()
{
}

static void Edit_Duplicate()
{
}

static void Edit_Delete()
{
}

static void Edit_SelectAll()
{
}

static void Blueprint_Start()
{
    auto entryNode = FindEntryPointNode();

    LOGI("Execution: Start");
    g_Blueprint->Start(*entryNode);
}

static void Blueprint_Step()
{
    LOGI("Execution: Step %" PRIu32, g_Blueprint->StepCount());
    auto result = g_Blueprint->Step();
    if (result == StepResult::Done)
        LOGI("Execution: Done (%" PRIu32 " steps)", g_Blueprint->StepCount());
    else if (result == StepResult::Error)
        LOGI("Execution: Failed at step %" PRIu32, g_Blueprint->StepCount());
}

static void Blueprint_Stop()
{
    LOGI("Execution: Stop");
    g_Blueprint->Stop();
}

static void Blueprint_Run()
{
    auto entryNode = FindEntryPointNode();

    LOGI("Execution: Run");
    auto result = g_Blueprint->Execute(*entryNode);
    if (result == StepResult::Done)
        LOGI("Execution: Done (%" PRIu32 " steps)", g_Blueprint->StepCount());
    else if (result == StepResult::Error)
        LOGI("Execution: Failed at step %" PRIu32, g_Blueprint->StepCount());
}



const char* Application_GetName()
{
    return "Blueprints2";
}

void Application_Initialize()
{
    g_ApplicationWindowFlags |= ImGuiWindowFlags_MenuBar;

    using namespace crude_blueprint;

    g_Document = make_unique<Document>();
    g_Blueprint = &g_Document->m_Blueprint;

    g_OverlayLogger.AddKeyword("Node");
    g_OverlayLogger.AddKeyword("Pin");
    g_OverlayLogger.AddKeyword("Link");
    g_OverlayLogger.AddKeyword("CreateNodeDialog");
    g_OverlayLogger.AddKeyword("NodeContextMenu");
    g_OverlayLogger.AddKeyword("PinContextMenu");
    g_OverlayLogger.AddKeyword("LinkContextMenu");

    for (auto nodeTypeInfo : g_Blueprint->GetNodeRegistry()->GetTypes())
        g_OverlayLogger.AddKeyword(nodeTypeInfo->m_Name);

    PrintNode::s_PrintFunction = [](const PrintNode& node, string_view message)
    {
        LOGI("PrintNode(%" PRIu32 "): \"%*s\"", node.m_Id, static_cast<int>(message.size()), message.data());
    };

    auto printNode2Node = g_Blueprint->CreateNode<PrintNode>();
    auto entryPointNode = g_Blueprint->CreateNode<EntryPointNode>();
    auto printNode1Node = g_Blueprint->CreateNode<PrintNode>();
    auto flipFlopNode = g_Blueprint->CreateNode<FlipFlopNode>();
    auto toStringNode = g_Blueprint->CreateNode<ToStringNode>();
    auto doNNode = g_Blueprint->CreateNode<DoNNode>();
    auto addNode = g_Blueprint->CreateNode<AddNode>();

    entryPointNode->m_Exit.LinkTo(doNNode->m_Enter);

    doNNode->m_N.SetValue(5);
    doNNode->m_Exit.LinkTo(flipFlopNode->m_Enter);

    toStringNode->m_Value.LinkTo(addNode->m_Result);

    addNode->m_A.LinkTo(doNNode->m_Counter);
    addNode->m_B.SetValue(3);

    toStringNode->m_Exit.LinkTo(printNode2Node->m_Enter);

    printNode1Node->m_String.SetValue("FlipFlop slot A!");
    printNode2Node->m_String.SetValue("FlipFlop slot B!");
    printNode2Node->m_String.LinkTo(toStringNode->m_String);

    //printNode2Node->m_String.m_Link = &toStringNode.m_Enter;

    flipFlopNode->m_A.LinkTo(printNode1Node->m_Enter);
    flipFlopNode->m_B.LinkTo(toStringNode->m_Enter);


    //auto constBoolNode  = g_Blueprint->CreateNode<ConstBoolNode>();
    //auto toStringNode   = g_Blueprint->CreateNode<ToStringNode>();
    //auto printNodeNode  = g_Blueprint->CreateNode<PrintNode>();

    //crude_json::value value;
    //g_Blueprint->Save(value);
    //auto yyy = value.dump();

    //g_Blueprint->Execute(*entryPointNode);

    //Blueprint b2;

    //b2.Load(value);

    //crude_json::value value2;
    //b2.Save(value2);
    //auto zzz = value2.dump();

    //bool ok = yyy == zzz;

    //auto b3 = b2;

    //g_Blueprint->CreateNode<BranchNode>();
    //g_Blueprint->CreateNode<DoNNode>();
    //g_Blueprint->CreateNode<DoOnceNode>();
    //g_Blueprint->CreateNode<FlipFlopNode>();
    //g_Blueprint->CreateNode<ForLoopNode>();
    //g_Blueprint->CreateNode<GateNode>();
    //g_Blueprint->CreateNode<AddNode>();
    //g_Blueprint->CreateNode<PrintNode>();
    //g_Blueprint->CreateNode<ConstBoolNode>();
    //g_Blueprint->CreateNode<ConstInt32Node>();
    //g_Blueprint->CreateNode<ConstFloatNode>();
    //g_Blueprint->CreateNode<ConstStringNode>();
    //g_Blueprint->CreateNode<ToStringNode>();
    //g_Blueprint->CreateNode<AddNode>();


    ed::Config config;
    //config.SettingsFile = "blueprint2-example.cfg";
    config.SaveSettingsJson = [](const crude_json::value& state, ed::SaveReasonFlags reason, void* userPointer) -> bool
    {
        if (g_Document)
        {
            g_Document->m_EditorState = state;
            return true;
        }
        else
            return false;
    };
    config.LoadSettingsJson = [](void* userPointer) -> crude_json::value
    {
        if (g_Document)
            return g_Document->m_EditorState;
        else
            return {};
    };

    g_Editor = ed::CreateEditor(&config);

    //g_Blueprint->Start(*FindEntryPointNode());
}

void Application_Finalize()
{
    ed::DestroyEditor(g_Editor);
}

static EntryPointNode* FindEntryPointNode()
{
    if (!g_Blueprint)
        return nullptr;

    for (auto& node : g_Blueprint->GetNodes())
    {
        if (node->GetTypeInfo().m_Id == EntryPointNode::GetStaticTypeInfo().m_Id)
        {
            return static_cast<EntryPointNode*>(node);
        }
    }

    return nullptr;
}

static const char* StepResultToString(StepResult stepResult)
{
    switch (stepResult)
    {
        case StepResult::Success:   return "Success";
        case StepResult::Done:      return "Done";
        case StepResult::Error:     return "Error";
        default:                    return "";
    }
}

static void ShowControlPanel()
{
    auto toolbarAction = [](Action& action)
    {
        ImEx::ScopedDisableItem disableAction(!action.IsEnabled());

        if (ImGui::Button(action.GetName().c_str()))
        {
            action.Execute();
        }
    };

    toolbarAction(g_Blueprint_Start);
    ImGui::SameLine();
    toolbarAction(g_Blueprint_Step);
    ImGui::SameLine();
    toolbarAction(g_Blueprint_Stop);
    ImGui::SameLine();
    toolbarAction(g_Blueprint_Run);

    ImGui::SameLine();
    ImGui::Text("Status: %s", g_Blueprint ? StepResultToString(g_Blueprint->LastStepResult()) : "-");

    if (auto currentNode = g_Blueprint ? g_Blueprint->CurrentNode() : nullptr)
    {
        auto nodeName = currentNode->GetName();

        ImGui::SameLine();
        ImGui::Spacing();
        ImGui::SameLine();
        ImGui::Text("Current Node: %*s", static_cast<int>(nodeName.size()), nodeName.data());

        auto nextNode = g_Blueprint->NextNode();
        auto nextNodeName = nextNode ? nextNode->GetName() : "-";
        ImGui::SameLine();
        ImGui::Spacing();
        ImGui::SameLine();
        ImGui::Text("Next Node: %*s", static_cast<int>(nextNodeName.size()), nextNodeName.data());
    }
}

// Iterate over blueprint nodes and commit them to node editor.
static void CommitBlueprintNodes(Blueprint& blueprint, DebugOverlay& debugOverlay)
{
    const auto iconSize = ImVec2(ImGui::GetTextLineHeight(), ImGui::GetTextLineHeight());

    debugOverlay.Begin();

    // Commit all nodes to editor
    for (auto& node : blueprint.GetNodes())
    {
        ed::BeginNode(node->m_Id);

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

        // Show title if node has one.
        auto nodeName = node->GetName();
        if (!nodeName.empty())
        {
            ImGui::PushFont(Application_HeaderFont());
            ImGui::TextUnformatted(nodeName.data(), nodeName.data() + nodeName.size());
            ImGui::PopFont();
        }

        ImGui::Dummy(ImVec2(100.0f, 0.0f)); // For minimum node width

        crude_layout::Grid layout;
        layout.Begin(node->m_Id, 2, 100.0f);
        layout.SetColumnAlignment(0.0f);

        // Draw column with input pins.
        for (auto& pin : node->GetInputPins())
        {
            // Add a bit of spacing to separate pins and make value not cramped
            ImGui::Spacing();

            // Input pin layout:
            //
            //     +-[1]---+-[2]------+-[3]----------+
            //     |       |          |              |
            //    [X] Icon | Pin Name | Value/Editor |
            //     |       |          |              |
            //     +-------+----------+--------------+

            ed::BeginPin(pin->m_Id, ed::PinKind::Input);
            // [X] - Tell editor to put pivot point in the middle of
            //       the left side of the pin. This is the point
            //       where link will be hooked to.
            //
            //       By default pivot is in pin center point which
            //       does not look good for blueprint nodes.
            ed::PinPivotAlignment(ImVec2(0.0f, 0.5f));

            // [1] - Icon
            ImEx::Icon(iconSize,
                PinTypeToIconType(pin->GetType()),
                blueprint.HasPinAnyLink(*pin),
                PinTypeToColor(pin->GetValueType()));

            // [2] - Show pin name if it has one
            if (!pin->m_Name.empty())
            {
                ImGui::SameLine();
                ImGui::TextUnformatted(pin->m_Name.data(), pin->m_Name.data() + pin->m_Name.size());
            }

            // [3] - Show value/editor when pin is not linked to anything
            if (!blueprint.HasPinAnyLink(*pin))
            {
                ImGui::SameLine();
                DrawPinValueWithEditor(*pin);
            }

            ed::EndPin();

            // [Debug Overlay] Show value of the pin if node is currently executed
            debugOverlay.DrawInputPin(*pin);

            layout.NextRow();
        }

        layout.SetColumnAlignment(1.0f);
        layout.NextColumn();

        // Draw column with output pins.
        for (auto& pin : node->GetOutputPins())
        {
            // Add a bit of spacing to separate pins and make value not cramped
            ImGui::Spacing();

            // Output pin layout:
            //
            //    +-[1]------+-[2]---+
            //    |          |       |
            //    | Pin Name | Icon [X]
            //    |          |       |
            //    +----------+-------+

            ed::BeginPin(pin->m_Id, ed::PinKind::Output);

            // [X] - Tell editor to put pivot point in the middle of
            //       the right side of the pin. This is the point
            //       where link will be hooked to.
            //
            //       By default pivot is in pin center point which
            //       does not look good for blueprint nodes.
            ed::PinPivotAlignment(ImVec2(1.0f, 0.5f));

            // [1] - Show pin name if it has one
            if (!pin->m_Name.empty())
            {
                ImGui::TextUnformatted(pin->m_Name.data(), pin->m_Name.data() + pin->m_Name.size());
                ImGui::SameLine();
            }

            // [2] - Show icon
            ImEx::Icon(iconSize,
                PinTypeToIconType(pin->GetType()),
                blueprint.HasPinAnyLink(*pin),
                PinTypeToColor(pin->GetValueType()));

            ed::EndPin();

            // [Debug Overlay] Show value of the pin if node is currently executed
            debugOverlay.DrawOutputPin(*pin);

            layout.NextRow();
        }

        layout.End();

        ed::EndNode();

        // [Debug Overlay] Show cursor over node
        debugOverlay.DrawNode(*node);
    }

    // Commit all links to editor
    for (auto& pin : blueprint.GetPins())
    {
        if (!pin->m_Link)
            continue;

        // To keep things simple, link id is same as pin id.
        ed::Link(pin->m_Id, pin->m_Id, pin->m_Link->m_Id, PinTypeToColor(pin->GetValueType()));
    }

    debugOverlay.End();
}

static void HandleCreateAction(Blueprint& blueprint)
{
    ItemBuilder itemBuilder;

    if (!itemBuilder)
        return;

    if (auto linkBuilder = itemBuilder.QueryNewLink())
    {
        auto startPin = blueprint.FindPin(static_cast<uint32_t>(linkBuilder->m_StartPinId.Get()));
        auto endPin   = blueprint.FindPin(static_cast<uint32_t>(linkBuilder->m_EndPinId.Get()));

        // Editor return pins in order draw by the user. It is up to the
        // user to determine if it is valid. In blueprints we accept only links
        // from receivers to providers. Other graph types may allow bi-directional
        // links between nodes and this ordering make this feature possible.
        if (endPin->IsReceiver() && startPin->IsProvider())
            ImSwap(startPin, endPin);

        if (auto canLinkResult = startPin->CanLinkTo(*endPin))
        {
            ed::Suspend();
            ImGui::BeginTooltip();
            ImGui::Text("Valid Link%s%s",
                canLinkResult.Reason().empty() ? "" : ": ",
                canLinkResult.Reason().empty() ? "" : canLinkResult.Reason().c_str());
            ImGui::Separator();
            ImGui::TextUnformatted("From:");
            ImGui::Bullet(); ImGui::Text(PRI_pin_fmt, LOG_pin(startPin));
            ImGui::Bullet(); ImGui::Text(PRI_node_fmt, LOG_node(startPin->m_Node));
            ImGui::TextUnformatted("To:");
            ImGui::Bullet(); ImGui::Text(PRI_pin_fmt, LOG_pin(endPin));
            ImGui::Bullet(); ImGui::Text(PRI_node_fmt, LOG_node(endPin->m_Node));
            ImGui::EndTooltip();
            ed::Resume();

            if (linkBuilder->Accept())
            {
                if (startPin->LinkTo(*endPin))
                    LOGV("[HandleCreateAction] " PRI_pin_fmt " linked with " PRI_pin_fmt, LOG_pin(startPin), LOG_pin(endPin));
            }
        }
        else
        {
            ed::Suspend();
            ImGui::SetTooltip(
                "Invalid Link: %s",
                canLinkResult.Reason().c_str()
            );
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
            // Get node from which link was pulled (if any). After creating
            // node we will try to make link with first matching pin of the node.
            auto pin = blueprint.FindPin(static_cast<uint32_t>(nodeBuilder->m_PinId.Get()));

            ed::Suspend();
            LOGV("[HandleCreateAction] Open CreateNodeDialog");
            g_CreateNodeDailog.Open(pin);
            ed::Resume();
        }
    }
}

static void HandleDestroyAction(Blueprint& blueprint)
{
    ItemDeleter itemDeleter;

    if (!itemDeleter)
        return;

    vector<Node*> nodesToDelete;
    uint32_t brokenLinkCount = 0;

    // Process all nodes marked for deletion
    while (auto nodeDeleter = itemDeleter.QueryDeletedNode())
    {
        // Remove node, pass 'true' so links attached to node will also be queued for deletion.
        if (nodeDeleter->Accept(true))
        {
            auto node = blueprint.FindNode(static_cast<uint32_t>(nodeDeleter->m_NodeId.Get()));
            if (node != nullptr)
                // Queue nodes for deletion. We need to serve links first to avoid crash.
                nodesToDelete.push_back(node);
        }
    }

    // Process all links marked for deletion
    while (auto linkDeleter = itemDeleter.QueryDeleteLink())
    {
        if (linkDeleter->Accept())
        {
            auto startPin = blueprint.FindPin(static_cast<uint32_t>(linkDeleter->m_StartPinId.Get()));
            if (startPin != nullptr && startPin->IsLinked())
            {
                LOGV("[HandleDestroyAction] " PRI_pin_fmt " unlinked from " PRI_pin_fmt, LOG_pin(startPin), LOG_pin(startPin->GetLink()));
                startPin->Unlink();
                ++brokenLinkCount;
            }
        }
    }

    // After links was removed, now it is safe to delete nodes.
    for (auto node : nodesToDelete)
    {
        LOGV("[HandleDestroyAction] " PRI_node_fmt, LOG_node(node));
        blueprint.DeleteNode(node);
    }

    if (!nodesToDelete.empty() || brokenLinkCount)
    {
        LOGV("[HandleDestroyAction] %" PRIu32 " node%s deleted, %" PRIu32 " link%s broken",
            static_cast<uint32_t>(nodesToDelete.size()), nodesToDelete.size() != 1 ? "s" : "",
            brokenLinkCount, brokenLinkCount != 1 ? "s" : "");
    }
}

static void HandleContextMenuAction(Blueprint& blueprint)
{
    if (ed::ShowBackgroundContextMenu())
    {
        ed::Suspend();
        LOGV("[HandleContextMenuAction] Open CreateNodeDialog");
        g_CreateNodeDailog.Open();
        ed::Resume();
    }

    ed::NodeId contextNodeId;
    if (ed::ShowNodeContextMenu(&contextNodeId))
    {
        auto node = blueprint.FindNode(static_cast<uint32_t>(contextNodeId.Get()));

        ed::Suspend();
        LOGV("[HandleContextMenuAction] Open NodeContextMenu for " PRI_node_fmt, LOG_node(node));
        g_NodeContextMenu.Open(node);
        ed::Resume();
    }

    ed::PinId contextPinId;
    if (ed::ShowPinContextMenu(&contextPinId))
    {
        auto pin = blueprint.FindPin(static_cast<uint32_t>(contextPinId.Get()));

        ed::Suspend();
        LOGV("[HandleContextMenuAction] Open PinContextMenu for " PRI_pin_fmt, LOG_pin(pin));
        g_PinContextMenu.Open(pin);
        ed::Resume();
    }

    ed::LinkId contextLinkId;
    if (ed::ShowLinkContextMenu(&contextLinkId))
    {
        auto pin = blueprint.FindPin(static_cast<uint32_t>(contextLinkId.Get()));

        ed::Suspend();
        LOGV("[HandleContextMenuAction] Open LinkContextMenu for " PRI_pin_fmt, LOG_pin(pin));
        g_LinkContextMenu.Open(pin);
        ed::Resume();
    }
}

static void ShowDialogs(Blueprint& blueprint)
{
    ed::Suspend();

    if (g_CreateNodeDailog.Show(blueprint))
    {
        auto createdNode = g_CreateNodeDailog.GetCreatedNode();
        auto nodeName    = createdNode->GetName();

        LOGV("[CreateNodeDailog] " PRI_node_fmt" created", LOG_node(createdNode));

        for (auto startPin : g_CreateNodeDailog.GetCreatedLinks())
            LOGV("[CreateNodeDailog] " PRI_pin_fmt "  linked with " PRI_pin_fmt, LOG_pin(startPin), LOG_pin(startPin->GetLink()));
    }
    g_NodeContextMenu.Show(blueprint);
    g_PinContextMenu.Show(blueprint);
    g_LinkContextMenu.Show(blueprint);

    ed::Resume();
}

static void ShowInfoTooltip(Blueprint& blueprint)
{
    if (!ed::IsActive())
        return;

    auto hoveredNode = blueprint.FindNode(static_cast<uint32_t>(ed::GetHoveredNode().Get()));
    auto hoveredPin  = blueprint.FindPin(static_cast<uint32_t>(ed::GetHoveredPin().Get()));
    if (!hoveredNode && hoveredPin)
        hoveredNode = hoveredPin->m_Node;

    auto pinTooltip = [](const char* label, const Pin& pin, bool showNode)
    {
        ImGui::TextUnformatted(label);
        ImGui::Bullet(); ImGui::Text("ID: %" PRIu32, pin.m_Id);
        if (!pin.m_Name.empty())
        {
            ImGui::Bullet(); ImGui::Text("Name: %*s", static_cast<int>(pin.m_Name.size()), pin.m_Name.data());
        }
        if (showNode && pin.m_Node)
        {
            auto nodeName = pin.m_Node->GetName();
            ImGui::Bullet(); ImGui::Text("Node: %*s", static_cast<int>(nodeName.size()), nodeName.data());
        }
        ImGui::Bullet(); ImGui::Text("Type: %s", PinTypeToString(pin.GetType()));
        ImGui::Bullet(); ImGui::Text("Value Type: %s", PinTypeToString(pin.GetValueType()));

        string flags;
        if (pin.IsLinked())
            flags += "linked, ";
        if (pin.IsInput())
            flags += "input, ";
        if (pin.IsOutput())
            flags += "output, ";
        if (pin.IsProvider())
            flags += "provider, ";
        if (pin.IsReceiver())
            flags += "receiver, ";
        if (!flags.empty())
            flags = flags.substr(0, flags.size() - 2);
        ImGui::Bullet(); ImGui::Text("Flags: %s", flags.c_str());
    };

    if (hoveredNode)
    {
        auto nodeTypeInfo = hoveredNode->GetTypeInfo();
        auto nodeName = hoveredNode->GetName();

        ed::Suspend();
        ImGui::BeginTooltip();

        ImGui::Text("Node ID: %" PRIu32, hoveredNode->m_Id);
        ImGui::Text("Name: %*s", static_cast<int>(nodeName.size()), nodeName.data());
        ImGui::Separator();
        ImGui::TextUnformatted("Type:");
        ImGui::Bullet(); ImGui::Text("ID: 0x%08" PRIX32, nodeTypeInfo.m_Id);
        ImGui::Bullet(); ImGui::Text("Name: %*s", static_cast<int>(nodeTypeInfo.m_Name.size()), nodeTypeInfo.m_Name.data());

        if (hoveredPin)
        {
            ImGui::Separator();
            pinTooltip("Pin", *hoveredPin, false);
        }

        ImGui::EndTooltip();
        ed::Resume();
    }
    else if (auto hoveredLinkId = ed::GetHoveredLink())
    {
        ed::PinId startPinId, endPinId;
        ed::GetLinkPins(hoveredLinkId, &startPinId, &endPinId);

        auto startPin = blueprint.FindPin(static_cast<uint32_t>(startPinId.Get()));
        auto endPin = blueprint.FindPin(static_cast<uint32_t>(endPinId.Get()));

        ed::Suspend();
        ImGui::BeginTooltip();

        ImGui::Text("Link ID: %" PRIu32, startPin->m_Id);
        ImGui::Text("Type: %s", PinTypeToString(startPin->GetValueType()));
        ImGui::Separator();
        pinTooltip("Start Pin:", *startPin, true);
        ImGui::Separator();
        pinTooltip("End Pin:", *endPin, true);

        ImGui::EndTooltip();
        ed::Resume();
    }
}

static void UpdateActions()
{
    auto hasDocument = File_IsOpen();
    //auto isModified  = hasDocument && File_IsModified();

    g_File_Close.SetEnabled(hasDocument);
    g_File_SaveAs.SetEnabled(hasDocument);
    g_File_Save.SetEnabled(hasDocument);

    g_Edit_Undo.SetEnabled(hasDocument && false);
    g_Edit_Redo.SetEnabled(hasDocument && false);
    g_Edit_Cut.SetEnabled(hasDocument && false);
    g_Edit_Copy.SetEnabled(hasDocument && false);
    g_Edit_Paste.SetEnabled(hasDocument && false);
    g_Edit_Duplicate.SetEnabled(hasDocument && false);
    g_Edit_Delete.SetEnabled(hasDocument && false);
    g_Edit_SelectAll.SetEnabled(hasDocument && false);

    auto entryNode = FindEntryPointNode();

    bool hasBlueprint  = (g_Blueprint != nullptr);
    bool hasEntryPoint = (entryNode != nullptr);
    bool isExecuting   = hasBlueprint && (g_Blueprint->CurrentNode() != nullptr);

    g_Blueprint_Start.SetEnabled(hasBlueprint && hasEntryPoint);
    g_Blueprint_Step.SetEnabled(hasBlueprint && isExecuting);
    g_Blueprint_Stop.SetEnabled(hasBlueprint && isExecuting);
    g_Blueprint_Run.SetEnabled(hasBlueprint && hasEntryPoint);
}

static void ShowMainMenu()
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
            menuAction(g_File_New);
            menuAction(g_File_Open);
            ImGui::Separator();
            menuAction(g_File_Close);
            ImGui::Separator();
            menuAction(g_File_SaveAs);
            menuAction(g_File_Save);
            ImGui::Separator();
            menuAction(g_File_Exit);

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit"))
        {
            menuAction(g_Edit_Undo);
            menuAction(g_Edit_Redo);
            ImGui::Separator();
            menuAction(g_Edit_Cut);
            menuAction(g_Edit_Copy);
            menuAction(g_Edit_Paste);
            menuAction(g_Edit_Duplicate);
            menuAction(g_Edit_Delete);
            ImGui::Separator();
            menuAction(g_Edit_SelectAll);

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Blueprint"))
        {
            menuAction(g_Blueprint_Start);
            menuAction(g_Blueprint_Step);
            menuAction(g_Blueprint_Stop);
            ImGui::Separator();
            menuAction(g_Blueprint_Run);

            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }
}

void Application_Frame()
{
    ed::SetCurrentEditor(g_Editor);

    UpdateActions();

    ShowMainMenu();
    ShowControlPanel();

    ImGui::Separator();

    if (g_Blueprint)
    {
        DebugOverlay debugOverlay(*g_Blueprint);

        ed::Begin("###main_editor");

        CommitBlueprintNodes(*g_Blueprint, debugOverlay);

        HandleCreateAction(*g_Blueprint);
        HandleDestroyAction(*g_Blueprint);
        HandleContextMenuAction(*g_Blueprint);

        ShowDialogs(*g_Blueprint);

        ShowInfoTooltip(*g_Blueprint);

        ed::End();
    }
    else
    {
        ImGui::Dummy(ImGui::GetContentRegionAvail());
    }

    g_OverlayLogger.Update(ImGui::GetIO().DeltaTime);
    g_OverlayLogger.Draw(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

    ed::SetCurrentEditor(nullptr);

    //ImGui::ShowMetricsWindow();
}

bool Application_Close()
{
    return File_Close();
}