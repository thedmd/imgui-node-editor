# include "Application.h"
# include "NodeEditor.h"

namespace ed = ax::NodeEditor;

static ed::EditorContext* g_Context = nullptr;

void Application_Initialize()
{
    ed::Config config;
    config.SettingsFile = "Simple.json";
    g_Context = ed::CreateEditor(&config);
}

void Application_Finalize()
{
    ed::DestroyEditor(g_Context);
}

void Application_Frame()
{
    ed::SetCurrentEditor(g_Context);

    ed::Begin("My Editor");

    int uniqueId = 1;

    // Start drawing nodes.
    ed::BeginNode(uniqueId++);
        ImGui::Text("Node A");
        ed::BeginPin(uniqueId++, ed::PinKind::Input);
            ImGui::Text("-> In");
        ed::EndPin();
        ImGui::SameLine();
        ed::BeginPin(uniqueId++, ed::PinKind::Output);
            ImGui::Text("Out ->");
        ed::EndPin();
    ed::EndNode();

    ed::End();
}

