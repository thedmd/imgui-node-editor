#include "application.h"
#include <string>
#include <vector>
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

static void DrawItemRect(ImColor color, float expand = 0.0f)
{
    ImGui::GetWindowDrawList()->AddRect(
        ImGui::GetItemRectMin() - ImVec2(expand, expand),
        ImGui::GetItemRectMax() + ImVec2(expand, expand),
        color);
};

static void FillItemRect(ImColor color, float expand = 0.0f)
{
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImGui::GetItemRectMin() - ImVec2(expand, expand),
        ImGui::GetItemRectMax() + ImVec2(expand, expand),
        color);
};

void Application_Initialize()
{
    m_Editor = ed::CreateEditor();

    s_Nodes.emplace_back(GetNextId(), "InputAction Fire", ImColor(255, 128, 128));
    s_Nodes.back().Outputs.emplace_back(GetNextId(), "Pressed", PinType::Flow);
    s_Nodes.back().Outputs.emplace_back(GetNextId(), "Released", PinType::Flow);

    s_Nodes.emplace_back(GetNextId(), "Branch");
    s_Nodes.back().Inputs.emplace_back(GetNextId(), "", PinType::Flow);
    s_Nodes.back().Inputs.emplace_back(GetNextId(), "Condition", PinType::Bool);
    s_Nodes.back().Outputs.emplace_back(GetNextId(), "True", PinType::Flow);
    s_Nodes.back().Outputs.emplace_back(GetNextId(), "False", PinType::Flow);

    s_Nodes.emplace_back(GetNextId(), "Do N");
    s_Nodes.back().Inputs.emplace_back(GetNextId(), "Enter", PinType::Flow);
    s_Nodes.back().Inputs.emplace_back(GetNextId(), "N", PinType::Int);
    s_Nodes.back().Inputs.emplace_back(GetNextId(), "Reset", PinType::Flow);
    s_Nodes.back().Outputs.emplace_back(GetNextId(), "Exit", PinType::Flow);
    s_Nodes.back().Outputs.emplace_back(GetNextId(), "Counter", PinType::Int);

    s_Nodes.emplace_back(GetNextId(), "OutputAction");
    s_Nodes.back().Inputs.emplace_back(GetNextId(), "Sample", PinType::Float);
    s_Nodes.back().Outputs.emplace_back(GetNextId(), "Condition", PinType::Bool);

    s_Nodes.emplace_back(GetNextId(), "Set Timer", ImColor(128, 195, 248));
    s_Nodes.back().Inputs.emplace_back(GetNextId(), "", PinType::Flow);
    s_Nodes.back().Inputs.emplace_back(GetNextId(), "Object", PinType::Object);
    s_Nodes.back().Inputs.emplace_back(GetNextId(), "Function Name", PinType::Function);
    s_Nodes.back().Inputs.emplace_back(GetNextId(), "Time", PinType::Float);
    s_Nodes.back().Inputs.emplace_back(GetNextId(), "Looping", PinType::Bool);
    s_Nodes.back().Outputs.emplace_back(GetNextId(), "", PinType::Flow);


    //     s_Nodes.emplace_back(GetNextId(), "Single Line Trace by Channel", point(600, 30));
    //     s_Nodes.back().Inputs.emplace_back(GetNextId(), "", PinType::Flow);
    //     s_Nodes.back().Inputs.emplace_back(GetNextId(), "Start", PinType::Flow);
    //     s_Nodes.back().Inputs.emplace_back(GetNextId(), "End", PinType::Int);
    //     s_Nodes.back().Inputs.emplace_back(GetNextId(), "Trace Channel", PinType::Float);
    //     s_Nodes.back().Inputs.emplace_back(GetNextId(), "Trace Complex", PinType::Bool);
    //     s_Nodes.back().Inputs.emplace_back(GetNextId(), "Actors to Ignore", PinType::Int);
    //     s_Nodes.back().Inputs.emplace_back(GetNextId(), "Draw Debug Type", PinType::Bool);
    //     s_Nodes.back().Inputs.emplace_back(GetNextId(), "Ignore Self", PinType::Bool);
    //     s_Nodes.back().Outputs.emplace_back(GetNextId(), "", PinType::Flow);
    //     s_Nodes.back().Outputs.emplace_back(GetNextId(), "Out Hit", PinType::Float);
    //     s_Nodes.back().Outputs.emplace_back(GetNextId(), "Return Value", PinType::Bool);

    for (auto& node : s_Nodes)
    {
        for (auto& input : node.Inputs)
        {
            input.Node = &node;
            input.Kind = PinKind::Input;
        }

        for (auto& output : node.Outputs)
        {
            output.Node = &node;
            output.Kind = PinKind::Output;
        }
    }
}

void Application_Finalize()
{
    if (m_Editor)
    {
        ed::DestroyEditor(m_Editor);
        m_Editor = nullptr;
    }
}

void Application_Frame()
{
    auto& io = ImGui::GetIO();

    ImGui::Text("FPS: %.2f", io.Framerate);
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

    auto drawPinIcon = [iconSize2, &getIconColor](const Pin& pin, bool connected)
    {
        IconType iconType;
        ImColor  color = getIconColor(pin.Type);
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

        ax::Widgets::Icon(to_imvec(iconSize2), iconType, connected, color, ImColor(32, 32, 32));
    };


    ed::Begin("Node editor");
    {
        auto cursorTopLeft = ImGui::GetCursorScreenPos();

        for (auto& node : s_Nodes)
        {
            ed::BeginNode(node.ID);
                ed::BeginHeader(node.Color);
                    ImGui::Spring(0);
                    ImGui::TextUnformatted(node.Name.c_str());
                    ImGui::Spring(1);
                    //ImGui::Text("%d", orderIndex);
                    //ImGui::Spring(0,0);
                    ImGui::Dummy(ImVec2(0, 28));
                    ImGui::Spring(0);
                ed::EndHeader();

                for (auto& input : node.Inputs)
                {
                    ed::BeginInput(input.ID);
                    drawPinIcon(input, IsPinLinked(input.ID));
                    ImGui::Spring(0);
                    if (!input.Name.empty())
                    {
                        ImGui::TextUnformatted(input.Name.c_str());
                        ImGui::Spring(0);
                    }
                    if (input.Type == PinType::Bool)
                    {
                        ImGui::Button("Hello");
                        ImGui::Spring(0);
                    }
                    ed::EndInput();
                }

                for (auto& output : node.Outputs)
                {
                    ed::BeginOutput(output.ID);
                    if (!output.Name.empty())
                    {
                        ImGui::Spring(0);
                        ImGui::TextUnformatted(output.Name.c_str());
                    }
                    ImGui::Spring(0);
                    drawPinIcon(output, IsPinLinked(output.ID));
                    ed::EndOutput();
                }

            ed::EndNode();
        }

        for (auto& link : s_Links)
            ed::Link(link.ID, link.StartPinID, link.EndPinID, link.Color, 2.0f);

        int startPinId = 0, endPinId = 0;
        if (ed::CreateLink(&startPinId, &endPinId, ImColor(255, 255, 255), 2.0f))
        {
            auto startPin = FindPin(startPinId);
            auto endPin   = FindPin(endPinId);

            if (startPin && endPin)
            {
                if (endPin->Kind == startPin->Kind)
                    ed::RejectLink(ImColor(255, 0, 0), 2.0f);
                else if (endPin->Node == startPin->Node)
                    ed::RejectLink(ImColor(255, 0, 0), 1.0f);
                else if (endPin->Type != startPin->Type)
                    ed::RejectLink(ImColor(255, 128, 128), 1.0f);
                else if (ed::AcceptLink(ImColor(128, 255, 128), 4.0f))
                {
                    s_Links.emplace_back(Link(GetNextId(), startPinId, endPinId));
                    s_Links.back().Color = getIconColor(startPin->Type);
                }
            }
        }

        if (ed::DestroyLink())
        {
            while (int linkId = ed::GetDestroyedLinkId())
            {
                auto id = std::find_if(s_Links.begin(), s_Links.end(), [linkId](auto& link) { return link.ID == linkId; });
                if (id != s_Links.end())
                    s_Links.erase(id);
            }
        }

        ImGui::SetCursorScreenPos(cursorTopLeft);
    }
    ed::End();
}

