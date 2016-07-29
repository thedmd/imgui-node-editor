#pragma once
#include "Types.h"
#include "ImGuiInterop.h"
#define PICOJSON_USE_LOCALE 0
#include "picojson.h"
#include <vector>

namespace ax {
namespace Editor {

using namespace ImGuiInterop;
using std::vector;
using std::string;


//------------------------------------------------------------------------------
void Log(const char* fmt, ...);


//------------------------------------------------------------------------------
struct Node
{
    int     ID;
    rect    Bounds;

    Node(int id):
        ID(id),
        Bounds(to_point(ImGui::GetCursorScreenPos()), size())
    {
    }

};

enum class NodeStage
{
    Invalid,
    Begin,
    Header,
    Content,
    Input,
    Output,
    End
};

struct NodeSettings
{
    int   ID;
    point Location;

    NodeSettings(int id): ID(id) {}
};

struct Settings
{
    string  Path;
    bool    Dirty;

    vector<NodeSettings> Nodes;

    Settings(): Path("NodeEditor.json"), Dirty(false) {}
};

struct Context
{
    Context();
    ~Context();

    void Begin(const char* id);
    void End();

    void BeginNode(int id);
    void EndNode();

    void BeginHeader();
    void EndHeader();

    void BeginInput(int id);
    void EndInput();

    void BeginOutput(int id);
    void EndOutput();

private:
    Node* FindNode(int id);
    Node* CreateNode(int id);
    void DestroyNode(Node* node);
    void SetCurrentNode(Node* node, bool isNew = false);
    void SetActiveNode(Node* node);
    bool SetNodeStage(NodeStage stage);

    NodeSettings* FindNodeSettings(int id);
    NodeSettings* AddNodeSettings(int id);
    void          LoadSettings();
    void          SaveSettings();
    void          MarkSettingsDirty();

    vector<Node*>   Nodes;
    Node*           CurrentNode;
    NodeStage       CurrentNodeStage;
    Node*           ActiveNode;
    ImVec2          DragOffset;

    rect            NodeRect;
    rect            HeaderRect;
    rect            ContentRect;

    bool            IsInitialized;
    ImTextureID     HeaderTextureID;
    Settings        Settings;
};

} // namespace node_editor
} // namespace ax