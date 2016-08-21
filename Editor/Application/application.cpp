#include "application.h"
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <utility>
#include "Editor/Types.h"
#include "Editor/Types.inl"
#include "Editor/ImGuiInterop.h"
#include "Editor/Widgets.h"
#include "Editor/EditorApi.h"
#include "imgui_impl_dx11.h"

namespace ed = ax::Editor;

using namespace ax;
using namespace ax::ImGuiInterop;

using ax::Widgets::IconType;

static ed::Context* m_Editor = nullptr;

const int   PortIconSize = 16;
const float JoinBezierStrength = 50;

extern "C" __declspec(dllimport) short __stdcall GetAsyncKeyState(int vkey);
extern "C" bool Debug_KeyPress(int vkey)
{
    static std::map<int, bool> state;
    auto lastState = state[vkey];
    state[vkey] = (GetAsyncKeyState(vkey) & 0x8000) != 0;
    if (state[vkey] && !lastState)
        return true;
    else
        return false;
}

enum class PinType
{
    Flow,
    Bool,
    Int,
    Float,
    Object,
    Function,
};

enum class PinKind
{
    Input,
    Output
};

struct Node;

struct Pin
{
    int         ID;
    Node*       Node;
    std::string Name;
    PinType     Type;
    PinKind     Kind;

    Pin(int id, const char* name, PinType type):
        ID(id), Node(nullptr), Name(name), Type(type), Kind(PinKind::Input)
    {
    }
};

struct Node
{
    int ID;
    std::string Name;
    std::vector<Pin> Inputs;
    std::vector<Pin> Outputs;
    ImColor Color;

    Node(int id, const char* name, ImColor color = ImColor(255, 255, 255)):
        ID(id), Name(name), Color(color)
    {
    }
};

struct Link
{
    int ID;

    int StartPinID;
    int EndPinID;

    ImColor Color;

    Link(int id, int startPinId, int endPinId):
        ID(id), StartPinID(startPinId), EndPinID(endPinId), Color(255, 255, 255)
    {
    }
};

static std::vector<Node>    s_Nodes;
static std::vector<Link>    s_Links;

static int s_NextId = 1;
static int GetNextId()
{
    return s_NextId++;
}

static Pin* FindPin(int id)
{
    if (id <= 0)
        return nullptr;

    for (auto& node : s_Nodes)
    {
        for (auto& pin : node.Inputs)
            if (pin.ID == id)
                return &pin;

        for (auto& pin : node.Outputs)
            if (pin.ID == id)
                return &pin;
    }

    return nullptr;
}

static bool IsPinLinked(int id)
{
    if (id <= 0)
        return false;

    for (auto& link : s_Links)
        if (link.StartPinID == id || link.EndPinID == id)
            return true;

    return false;
}

static bool CanCreateLink(Pin* a, Pin* b)
{
    if (!a || !b || a == b || a->Kind == b->Kind || a->Type != b->Type || a->Node == b->Node)
        return false;

    return true;
}

static void DrawItemRect(ImColor color, float expand = 0.0f)
{
    ImGui::GetWindowDrawList()->AddRect(
        ImGui::GetItemRectMin() - ImVec2(expand, expand),
        ImGui::GetItemRectMax() + ImVec2(expand, expand),
        color);
};

static void FillItemRect(ImColor color, float expand = 0.0f, float rounding = 0.0f)
{
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImGui::GetItemRectMin() - ImVec2(expand, expand),
        ImGui::GetItemRectMax() + ImVec2(expand, expand),
        color, rounding);
};

static void BuildNode(Node* node)
{
    for (auto& input : node->Inputs)
    {
        input.Node = node;
        input.Kind = PinKind::Input;
    }

    for (auto& output : node->Outputs)
    {
        output.Node = node;
        output.Kind = PinKind::Output;
    }
}

static Node* SpawnInputActionNode()
{
    s_Nodes.emplace_back(GetNextId(), "InputAction Fire", ImColor(255, 128, 128));
    s_Nodes.back().Outputs.emplace_back(GetNextId(), "Pressed", PinType::Flow);
    s_Nodes.back().Outputs.emplace_back(GetNextId(), "Released", PinType::Flow);

    BuildNode(&s_Nodes.back());

    return &s_Nodes.back();
}

static Node* SpawnBranchNode()
{
    s_Nodes.emplace_back(GetNextId(), "Branch");
    s_Nodes.back().Inputs.emplace_back(GetNextId(), "", PinType::Flow);
    s_Nodes.back().Inputs.emplace_back(GetNextId(), "Condition", PinType::Bool);
    s_Nodes.back().Outputs.emplace_back(GetNextId(), "True", PinType::Flow);
    s_Nodes.back().Outputs.emplace_back(GetNextId(), "False", PinType::Flow);

    BuildNode(&s_Nodes.back());

    return &s_Nodes.back();
}

static Node* SpawnDoNNode()
{
    s_Nodes.emplace_back(GetNextId(), "Do N");
    s_Nodes.back().Inputs.emplace_back(GetNextId(), "Enter", PinType::Flow);
    s_Nodes.back().Inputs.emplace_back(GetNextId(), "N", PinType::Int);
    s_Nodes.back().Inputs.emplace_back(GetNextId(), "Reset", PinType::Flow);
    s_Nodes.back().Outputs.emplace_back(GetNextId(), "Exit", PinType::Flow);
    s_Nodes.back().Outputs.emplace_back(GetNextId(), "Counter", PinType::Int);

    BuildNode(&s_Nodes.back());

    return &s_Nodes.back();
}

static Node* SpawnOutputActionNode()
{
    s_Nodes.emplace_back(GetNextId(), "OutputAction");
    s_Nodes.back().Inputs.emplace_back(GetNextId(), "Sample", PinType::Float);
    s_Nodes.back().Outputs.emplace_back(GetNextId(), "Condition", PinType::Bool);

    BuildNode(&s_Nodes.back());

    return &s_Nodes.back();
}

static Node* SpawnSetTimerNode()
{
    s_Nodes.emplace_back(GetNextId(), "Set Timer", ImColor(128, 195, 248));
    s_Nodes.back().Inputs.emplace_back(GetNextId(), "", PinType::Flow);
    s_Nodes.back().Inputs.emplace_back(GetNextId(), "Object", PinType::Object);
    s_Nodes.back().Inputs.emplace_back(GetNextId(), "Function Name", PinType::Function);
    s_Nodes.back().Inputs.emplace_back(GetNextId(), "Time", PinType::Float);
    s_Nodes.back().Inputs.emplace_back(GetNextId(), "Looping", PinType::Bool);
    s_Nodes.back().Outputs.emplace_back(GetNextId(), "", PinType::Flow);

    BuildNode(&s_Nodes.back());

    return &s_Nodes.back();
}

static Node* SpawnTraceByChannelNode()
{
    s_Nodes.emplace_back(GetNextId(), "Single Line Trace by Channel", ImColor(255, 128, 64));
    s_Nodes.back().Inputs.emplace_back(GetNextId(), "", PinType::Flow);
    s_Nodes.back().Inputs.emplace_back(GetNextId(), "Start", PinType::Flow);
    s_Nodes.back().Inputs.emplace_back(GetNextId(), "End", PinType::Int);
    s_Nodes.back().Inputs.emplace_back(GetNextId(), "Trace Channel", PinType::Float);
    s_Nodes.back().Inputs.emplace_back(GetNextId(), "Trace Complex", PinType::Bool);
    s_Nodes.back().Inputs.emplace_back(GetNextId(), "Actors to Ignore", PinType::Int);
    s_Nodes.back().Inputs.emplace_back(GetNextId(), "Draw Debug Type", PinType::Bool);
    s_Nodes.back().Inputs.emplace_back(GetNextId(), "Ignore Self", PinType::Bool);
    s_Nodes.back().Outputs.emplace_back(GetNextId(), "", PinType::Flow);
    s_Nodes.back().Outputs.emplace_back(GetNextId(), "Out Hit", PinType::Float);
    s_Nodes.back().Outputs.emplace_back(GetNextId(), "Return Value", PinType::Bool);

    BuildNode(&s_Nodes.back());

    return &s_Nodes.back();
}

void Application_Initialize()
{
    m_Editor = ed::CreateEditor();

    SpawnInputActionNode();
    SpawnBranchNode();
    SpawnDoNNode();
    SpawnOutputActionNode();
    SpawnSetTimerNode();
}

void Application_Finalize()
{
    if (m_Editor)
    {
        ed::DestroyEditor(m_Editor);
        m_Editor = nullptr;
    }
}
extern "C" __declspec(dllimport) void __stdcall Sleep(unsigned int);

void Application_Frame()
{
    auto& io = ImGui::GetIO();

    ImGui::Text("FPS: %.2f (%.2gms)", io.Framerate, io.Framerate ? 1000.0f / io.Framerate : 0.0f);
    //ImGui::Spacing();

    auto iconSize2 = size(24, 24);

    ed::SetCurrentEditor(m_Editor);

    auto& style = ImGui::GetStyle();

    auto getIconColor = [](PinType type)
    {
        switch (type)
        {
            default:
            case PinType::Flow:     return ImColor(255, 255, 255);
            case PinType::Bool:     return ImColor(139,   0,   0);
            case PinType::Int:      return ImColor( 68, 201, 156);
            case PinType::Float:    return ImColor(147, 226,  74);
            case PinType::Object:   return ImColor( 51, 150, 215);
            case PinType::Function: return ImColor(218,   0, 183);
        }
    };

    auto drawPinIcon = [iconSize2, &getIconColor](const Pin& pin, bool connected, int alpha)
    {
        IconType iconType;
        ImColor  color = getIconColor(pin.Type);
        color.Value.w = alpha / 255.0f;
        switch (pin.Type)
        {
            case PinType::Flow:     iconType = IconType::Flow;   break;
            case PinType::Bool:     iconType = IconType::Circle; break;
            case PinType::Int:      iconType = IconType::Circle; break;
            case PinType::Float:    iconType = IconType::Circle; break;
            case PinType::Object:   iconType = IconType::Circle; break;
            case PinType::Function: iconType = IconType::Circle; break;
            default:
                return;
        }

        ax::Widgets::Icon(to_imvec(iconSize2), iconType, connected, color, ImColor(32, 32, 32, alpha));
        //ImGui::Dummy(to_imvec(iconSize2));
    };

    static bool createNewNode = false;
    static Pin* newNodeLinkPin = nullptr;
    static Pin* newLinkPin = nullptr;

    ed::Begin("Node editor");
    {
        auto cursorTopLeft = ImGui::GetCursorScreenPos();

        for (auto& node : s_Nodes)
        {
            ed::BeginNode(node.ID);
                ed::BeginHeader(node.Color);
                    ImGui::Spring(0);
//                     auto headerTextSize = ImGui::CalcTextSize(node.Name.c_str());
//                     ImGui::GetWindowDrawList()->AddRectFilled(
//                         ImGui::GetCursorScreenPos() - ImVec2(style.ItemInnerSpacing.x, 0),
//                         ImGui::GetCursorScreenPos() + ImVec2(style.ItemInnerSpacing.x, 0) + headerTextSize,
//                         ImColor(0, 0, 0, 64), headerTextSize.y / 3.0f);
                    ImGui::TextUnformatted(node.Name.c_str());
                    ImGui::Spring(1);
                    //ImGui::Text("%d", orderIndex);
                    //ImGui::Spring(0,0);
                    ImGui::Dummy(ImVec2(0, 28));
                    ImGui::Spring(0);
                ed::EndHeader();

                for (auto& input : node.Inputs)
                {
                    auto alpha = 255;
                    if (newLinkPin && !CanCreateLink(newLinkPin, &input) && &input != newLinkPin)
                        alpha = 48;

                    ed::BeginInput(input.ID);
                    alpha = (int)(alpha * ImGui::GetStyle().Alpha);
                    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha / 255.0f);
                    drawPinIcon(input, IsPinLinked(input.ID), alpha);
                    ImGui::Spring(0);
                    if (!input.Name.empty())
                    {
                        ImGui::TextUnformatted(input.Name.c_str());
                        ImGui::Spring(0);
                    }
                    if (input.Type == PinType::Bool)
                    {
//                         ImGui::Button("Hello");
//                         ImGui::Spring(0);
                    }
                    ImGui::PopStyleVar();
                    ed::EndInput();
                }

                for (auto& output : node.Outputs)
                {
                    auto alpha = 255;
                    if (newLinkPin && !CanCreateLink(newLinkPin, &output) && &output != newLinkPin)
                        alpha = 48;

                    alpha = (int)(alpha * ImGui::GetStyle().Alpha);
                    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha / 255.0f);
                    ed::BeginOutput(output.ID);
                    if (!output.Name.empty())
                    {
                        ImGui::Spring(0);
                        ImGui::TextUnformatted(output.Name.c_str());
                    }
                    ImGui::Spring(0);
                    drawPinIcon(output, IsPinLinked(output.ID), alpha);
                    ImGui::PopStyleVar();
                    ed::EndOutput();
                }

            ed::EndNode();
        }

        for (auto& link : s_Links)
            ed::Link(link.ID, link.StartPinID, link.EndPinID, link.Color, 2.0f);

        if (!createNewNode)
        {
            if (ed::BeginCreate(ImColor(255, 255, 255), 2.0f))
            {
                auto showLabel = [](const char* label, ImColor color)
                {
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetTextLineHeight());
                    auto size = ImGui::CalcTextSize(label);

                    auto padding = ImGui::GetStyle().FramePadding;
                    auto spacing = ImGui::GetStyle().ItemSpacing;

                    ImGui::SetCursorPos(ImGui::GetCursorPos() + ImVec2(spacing.x, -spacing.y));

                    auto rectMin = ImGui::GetCursorScreenPos() - padding;
                    auto rectMax = ImGui::GetCursorScreenPos() + size + padding;

                    auto drawList = ImGui::GetWindowDrawList();
                    drawList->AddRectFilled(rectMin, rectMax, color, size.y * 0.15f);
                    ImGui::TextUnformatted(label);
                };

                int startPinId = 0, endPinId = 0;
                if (ed::QueryNewLink(&startPinId, &endPinId))
                {
                    auto startPin = FindPin(startPinId);
                    auto endPin   = FindPin(endPinId);

                    newLinkPin = startPin ? startPin : endPin;

                    if (startPin->Kind == PinKind::Input)
                    {
                        std::swap(startPin, endPin);
                        std::swap(startPinId, endPinId);
                    }

                    if (startPin && endPin)
                    {
                        if (endPin == startPin)
                        {
                            ed::RejectNewItem(ImColor(255, 0, 0), 2.0f);
                        }
                        else if (endPin->Kind == startPin->Kind)
                        {
                            showLabel("x Incompatible Pin Kind", ImColor(45, 32, 32, 180));
                            ed::RejectNewItem(ImColor(255, 0, 0), 2.0f);
                        }
                        else if (endPin->Node == startPin->Node)
                        {
                            showLabel("x Cannot connect to self", ImColor(45, 32, 32, 180));
                            ed::RejectNewItem(ImColor(255, 0, 0), 1.0f);
                        }
                        else if (endPin->Type != startPin->Type)
                        {
                            showLabel("x Incompatible Pin Type", ImColor(45, 32, 32, 180));
                            ed::RejectNewItem(ImColor(255, 128, 128), 1.0f);
                        }
                        else
                        {
                            showLabel("+ Create Link", ImColor(32, 45, 32, 180));
                            if (ed::AcceptNewItem(ImColor(128, 255, 128), 4.0f))
                            {
                                s_Links.emplace_back(Link(GetNextId(), startPinId, endPinId));
                                s_Links.back().Color = getIconColor(startPin->Type);
                            }
                        }
                    }
                }

                int pinId = 0;
                if (ed::QueryNewNode(&pinId))
                {
                    newLinkPin = FindPin(pinId);
                    if (newLinkPin)
                        showLabel("+ Create Node", ImColor(32, 45, 32, 180));

                    if (ed::AcceptNewItem())
                    {
                        createNewNode  = true;
                        newNodeLinkPin = FindPin(pinId);
                        newLinkPin = nullptr;
                        ImGui::OpenPopup("Create New Node");
                    }
                }

            }
            else
                newLinkPin = nullptr;
            ed::EndCreate();

            if (ed::BeginDelete())
            {
                int linkId = 0;
                while (ed::QueryDeletedLink(&linkId))
                {
                    if (ed::AcceptDeletedItem())
                    {
                        auto id = std::find_if(s_Links.begin(), s_Links.end(), [linkId](auto& link) { return link.ID == linkId; });
                        if (id != s_Links.end())
                            s_Links.erase(id);
                    }
                }

                int nodeId = 0;
                while (ed::QueryDeletedNode(&nodeId))
                {
                    if (ed::AcceptDeletedItem())
                    {
                        auto id = std::find_if(s_Nodes.begin(), s_Nodes.end(), [nodeId](auto& node) { return node.ID == nodeId; });
                        if (id != s_Nodes.end())
                            s_Nodes.erase(id);
                    }
                }
            }
            ed::EndDelete();
        }

        ImGui::SetCursorScreenPos(cursorTopLeft);
    }

    ed::Suspend();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
    if (ImGui::BeginPopup("Create New Node"))
    {
        auto newNodePostion = ImGui::GetMousePosOnOpeningCurrentPopup();
        //ImGui::SetCursorScreenPos(ImGui::GetMousePosOnOpeningCurrentPopup());

        //auto drawList = ImGui::GetWindowDrawList();
        //drawList->AddCircleFilled(ImGui::GetMousePosOnOpeningCurrentPopup(), 10.0f, 0xFFFF00FF);

        Node* node = nullptr;
        if (ImGui::MenuItem("Input Action"))
            node = SpawnInputActionNode();
        if (ImGui::MenuItem("Output Action"))
            node = SpawnOutputActionNode();
        if (ImGui::MenuItem("Branch"))
            node = SpawnBranchNode();
        if (ImGui::MenuItem("Do N"))
            node = SpawnDoNNode();
        if (ImGui::MenuItem("Set Timer"))
            node = SpawnSetTimerNode();
        if (ImGui::MenuItem("Trace by Channel"))
            node = SpawnTraceByChannelNode();

        if (node)
        {
            createNewNode = false;

            ed::SetNodePosition(node->ID, newNodePostion);

            if (auto startPin = newNodeLinkPin)
            {
                auto& pins = startPin->Kind == PinKind::Input ? node->Outputs : node->Inputs;

                for (auto& pin : pins)
                {
                    if (CanCreateLink(startPin, &pin))
                    {
                        auto endPin = &pin;
                        if (startPin->Kind == PinKind::Input)
                            std::swap(startPin, endPin);

                        s_Links.emplace_back(Link(GetNextId(), startPin->ID, endPin->ID));
                        s_Links.back().Color = getIconColor(startPin->Type);

                        break;
                    }
                }
            }
        }

        ImGui::EndPopup();
    }
    else
        createNewNode = false;
    ImGui::PopStyleVar();
    ed::Resume();


//     auto p0 = pointf(400,  300);
//     auto p1 = pointf(1200, 200);
//     auto p2 = pointf(1600,  600);
//     auto p3 = pointf(900,  700);
//
//     auto a0 = pointf(600, 200);
//     auto a1 = to_pointf(ImGui::GetMousePos());//pointf(200, 500);
//
//     auto drawList = ImGui::GetWindowDrawList();
//     drawList->PushClipRectFullScreen();
//
//     drawList->AddLine(to_imvec(p0), to_imvec(p1), IM_COL32(255, 0, 255, 255), 1.0f);
//     drawList->AddLine(to_imvec(p1), to_imvec(p2), IM_COL32(255, 0, 255, 255), 1.0f);
//     drawList->AddLine(to_imvec(p2), to_imvec(p3), IM_COL32(255, 0, 255, 255), 1.0f);
//     drawList->AddBezierCurve(to_imvec(p0), to_imvec(p1), to_imvec(p2), to_imvec(p3), IM_COL32(255, 255, 255, 255), 4.0f);
//
//     drawList->AddLine(to_imvec(a0), to_imvec(a1), IM_COL32(255, 255, 0, 255), 1.0f);
//
//     pointf roots[3];
//     auto count = bezier_line_intersect(p0, p1, p2, p3, a0, a1, roots);
//     for (int i = 0; i < count; ++i)
//     {
//         auto p = roots[i];
//         drawList->AddCircleFilled(to_imvec(p), 5.0f, IM_COL32(255, 255, 0, 255));
//     }
//
//     auto bounds = bezier_bounding_rect(p0, p1, p2, p3);
//     drawList->AddRect(
//         to_imvec(bounds.top_left()),
//         to_imvec(bounds.bottom_right()),
//         IM_COL32(0, 0, 255, 255), 0.0f, 15, 2.0f);
//
//     drawList->PopClipRect();


    ed::End();

    //Sleep(16);
}

