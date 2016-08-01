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
enum class ObjectType
{
    Node, Pin
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

enum class PinType
{
    Input, Output
};

enum class LinkStage
{
    None,
    Possible,
    Edit,
    Reject,
    Accept,
    Created
};

struct Node;
struct Pin;
struct Link;

struct Object
{
    int     ID;
    bool    IsLive;

    Object(int id): ID(id), IsLive(true) {}
    virtual ~Object() = default;

    virtual Node* AsNode() { return nullptr; }
    virtual Pin*  AsPin()  { return nullptr; }
    virtual Link* AsLink() { return nullptr; }
};

struct Pin final: Object
{
    PinType Type;
    Node*   Node;
    rect    Bounds;
    point   DragPoint;
    Pin*    PreviousPin;

    Pin(int id, PinType type):
        Object(id), Type(type), Node(nullptr), Bounds(), PreviousPin(nullptr)
    {
    }

    virtual Pin* AsPin() override final { return this; }
};

struct Node final: Object
{
    rect    Bounds;
    int     Channel;
    Pin*    LastPin;
    point   DragStart;

    Node(int id):
        Object(id),
        Bounds(to_point(ImGui::GetCursorScreenPos()), size()),
        Channel(0),
        LastPin(nullptr),
        DragStart()
    {
    }

    virtual Node* AsNode() override final { return this; }
};

struct Link final: Object
{
    Pin*  StartPin;
    Pin*  EndPin;
    ImU32 Color;
    float Thickness;

    Link(int id):
        Object(id), StartPin(nullptr), EndPin(nullptr), Color(IM_COL32_WHITE), Thickness(1.0f)
    {
    }

    virtual Link* AsLink() override final { return this; }
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

    void BeginHeader(ImU32 color);
    void EndHeader();

    void BeginInput(int id);
    void EndInput();

    void BeginOutput(int id);
    void EndOutput();

    bool CreateLink(int* startId, int* endId, ImU32 color, float thickness);
    void RejectLink(ImU32 color, float thickness);
    bool AcceptLink(ImU32 color, float thickness);

    bool DoLink(int id, int startPinId, int endPinId, ImU32 color, float thickness);

private:
    Pin* CreatePin(int id, PinType type);
    Node* CreateNode(int id);
    Link* CreateLink(int id);
    void DestroyObject(Node* node);
    Object* FindObject(int id);

    Node* FindNode(int id);
    Pin* FindPin(int id);
    Link* FindLink(int id);

    void SetHotObject(Object* object);
    void SetActiveObject(Object* object);

    void ClearSelection();
    void AddSelectedObject(Object* object);
    void RemoveSelectedObject(Object* object);
    void SetSelectedObject(Object* object);
    bool IsSelected(Object* object);

    void SetCurrentNode(Node* node);
    void SetCurrentPin(Pin* pin);

    bool SetNodeStage(NodeStage stage);

    Pin* GetPin(int id, PinType type);
    void BeginPin(int id, PinType type);
    void EndPin();

    Link* GetLink(int id);

    NodeSettings* FindNodeSettings(int id);
    NodeSettings* AddNodeSettings(int id);
    void          LoadSettings();
    void          SaveSettings();
    void          MarkSettingsDirty();

    vector<Node*>   Nodes;
    vector<Pin*>    Pins;
    vector<Link*>   Links;

    Object*         HotObject;
    Object*         ActiveObject;
    Object*         SelectedObject;
    vector<Object*> SelectedObjects;

    Pin*            CurrentPin;
    Node*           CurrentNode;

    ImVec2          DragOffset;
    Node*           DraggedNode;
    Pin*            DraggedPin;

    // Node building
    NodeStage       NodeBuildStage;
    ImU32           HeaderColor;
    rect            NodeRect;
    rect            HeaderRect;
    rect            ContentRect;

    // Link creating
    LinkStage       LinkStage;
    ImU32           LinkColor;
    float           LinkThickness;
    Pin*            LinkStart;
    Pin*            LinkEnd;

    bool            IsInitialized;
    ImTextureID     HeaderTextureID;
    Settings        Settings;
};

} // namespace node_editor
} // namespace ax