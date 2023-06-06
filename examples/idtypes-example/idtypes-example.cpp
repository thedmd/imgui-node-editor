# include <imgui.h>
# include <imgui_node_editor.h>
# include <application.h>
# include <vector>
# include <unordered_map>

#include "edconfig.h"

namespace ed = ax::NodeEditor;

struct Pin
{
};

struct Node
{
    std::vector<Pin> InPins;
    std::vector<Pin> OutPins;
};

bool CustomNodeId::operator<(const CustomNodeId &id) const
{
    return name < id.name;
}

bool CustomNodeId::operator==(const CustomNodeId &id) const
{
    return name == id.name;
}

std::string CustomNodeId::AsString() const
{
    return name;
}

CustomNodeId CustomNodeId::FromString(const char *str, const char *end)
{
    return CustomNodeId{ std::string(str, end) };
}

bool CustomNodeId::IsValid() const
{
    return !name.empty();
}

const CustomNodeId CustomNodeId::Invalid = {};

bool CustomPinId::operator<(const CustomPinId &id) const
{
    if (nodeName < id.nodeName) return true;
    else if (nodeName > id.nodeName) return false;

    if (int(direction) < int(id.direction)) return true;
    else if (int(direction) > int(id.direction)) return false;

    return pinIndex < id.pinIndex;
}

bool CustomPinId::operator==(const CustomPinId &id) const
{
    return nodeName == id.nodeName && direction == id.direction && pinIndex == id.pinIndex;
}

std::string CustomPinId::AsString() const
{
    return nodeName + ";" + std::to_string(int(direction)) + ";" + std::to_string(pinIndex);
}

CustomPinId CustomPinId::FromString(const char *str, const char *end)
{
    std::string string(str, end);
    auto sep1 = string.find(';');

    if (sep1 == 0 || sep1 == std::string::npos) {
        return Invalid;
    }

    auto sep2 = string.find(';', sep1 + 1);
    if (sep2 == sep1 + 1 || sep2 == std::string::npos) {
        return Invalid;
    }

    char *e;
    auto nodeName = std::string(string.data(), sep1);
    int dir = std::strtol(string.data() + sep1 + 1, &e, 10);
    if (dir > 1) {
        return Invalid;
    }

    int pin = std::strtol(string.data() + sep2 + 1, &e, 10);
    return CustomPinId{ nodeName, CustomPinId::Direction(dir), pin };
}

bool CustomPinId::IsValid() const
{
    return !nodeName.empty();
}

const CustomPinId CustomPinId::Invalid = {};


bool CustomLinkId::operator<(const CustomLinkId &id) const
{
    if (in < id.in) return true;
    if (in == id.in) return out < id.out;
    return false;
}

bool CustomLinkId::operator==(const CustomLinkId &id) const
{
    return in == id.in && out == id.out;
}

std::string CustomLinkId::AsString() const
{
    return in.AsString() + "->" + out.AsString();
}

CustomLinkId CustomLinkId::FromString(const char *str, const char *end)
{
    std::string string(str, end);
    auto sep = string.find("->");
    if (sep == 0 || sep == std::string::npos) {
        return Invalid;
    }

    auto in = CustomPinId::FromString(str, str + sep);
    auto out = CustomPinId::FromString(str + sep + 2, end);
    return { in, out };
}

bool CustomLinkId::IsValid() const
{
    return in.IsValid() && out.IsValid();
}

const CustomLinkId CustomLinkId::Invalid = {};

struct Example:
    public Application
{
    using Application::Application;

    void OnStart() override
    {
        ed::Config config;
        config.SettingsFile = "IdTypes.json";
        m_Context = ed::CreateEditor(&config);

        m_Nodes.insert({ "foo", Node{ { {}, {} }, { {} } } });
        m_Nodes.insert({ "bar", Node{ { {} }, { {}, {}, {} } } });
    }

    void OnStop() override
    {
        ed::DestroyEditor(m_Context);
    }

    void OnFrame(float deltaTime) override
    {
        auto& io = ImGui::GetIO();

        ImGui::Text("FPS: %.2f (%.2gms)", io.Framerate, io.Framerate ? 1000.0f / io.Framerate : 0.0f);

        ImGui::Separator();

        ed::SetCurrentEditor(m_Context);
        ed::Begin("My Editor", ImVec2(0.0, 0.0f));
        // Start drawing nodes.
        for (auto &n: m_Nodes) {
            ed::BeginNode(CustomNodeId{ n.first });
                ImGui::TextUnformatted(n.first.c_str());

                ImGui::BeginGroup();
                for (int i = 0; i < n.second.InPins.size(); ++i) {
                    ed::BeginPin(CustomPinId{ n.first, CustomPinId::Direction::In, i }, ed::PinKind::Input);
                        ImGui::Text("-> In");
                    ed::EndPin();
                }
                ImGui::EndGroup();

                ImGui::SameLine();

                ImGui::BeginGroup();
                for (int i = 0; i < n.second.OutPins.size(); ++i) {
                    ed::BeginPin(CustomPinId{ n.first, CustomPinId::Direction::Out, i }, ed::PinKind::Output);
                        ImGui::Text("Out ->");
                    ed::EndPin();
                }
                ImGui::EndGroup();

            ed::EndNode();
        }
        ed::End();
        ed::SetCurrentEditor(nullptr);

	    //ImGui::ShowMetricsWindow();
    }

    ed::EditorContext* m_Context = nullptr;
    std::unordered_map<std::string, Node> m_Nodes;
};

int Main(int argc, char** argv)
{
    Example exampe("IdTypes", argc, argv);

    if (exampe.Create())
        return exampe.Run();

    return 0;
}
