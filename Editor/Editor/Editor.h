#pragma once
#include "Types.h"
#include "ImGuiInterop.h"
#define PICOJSON_USE_LOCALE 0
#include "picojson.h"
#include <vector>

namespace ax {
namespace Editor {
namespace Detail {

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

enum class LinkCreateStage
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

struct Control
{
    Object* HotObject;
    Object* ActiveObject;
    Object* ClickedObject;
    Node*   HotNode;
    Node*   ActiveNode;
    Node*   ClickedNode;
    Pin*    HotPin;
    Pin*    ActivePin;
    Pin*    ClickedPin;
    Link*   HotLink;
    Link*   ActiveLink;
    Link*   ClickedLink;
    bool    WasBackgroundClicked;

    Control(Object* hotObject, Object* activeObject, Object* clickedObject, bool wasBackgroundClicked):
        HotObject(hotObject),
        ActiveObject(activeObject),
        ClickedObject(clickedObject),
        HotNode(nullptr),
        ActiveNode(nullptr),
        ClickedNode(nullptr),
        HotPin(nullptr),
        ActivePin(nullptr),
        ClickedPin(nullptr),
        HotLink(nullptr),
        ActiveLink(nullptr),
        ClickedLink(nullptr),
        WasBackgroundClicked(wasBackgroundClicked)
    {
        if (hotObject)
        {
            HotNode = hotObject->AsNode();
            HotPin  = hotObject->AsPin();
            HotLink = hotObject->AsLink();

            if (HotPin)
                HotNode = HotPin->Node;
        }

        if (activeObject)
        {
            ActiveNode = activeObject->AsNode();
            ActivePin  = activeObject->AsPin();
            ActiveLink = activeObject->AsLink();
        }

        if (clickedObject)
        {
            ClickedNode = clickedObject->AsNode();
            ClickedPin  = clickedObject->AsPin();
            ClickedLink = clickedObject->AsLink();
        }
    }
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

    bool DestroyLink();
    int GetDestroyedLinkId();

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

    //void SetHotObject(Object* object);
    //void SetActiveObject(Object* object);

    void ClearSelection();
    void AddSelectedObject(Object* object);
    void RemoveSelectedObject(Object* object);
    void SetSelectedObject(Object* object);
    bool IsSelected(Object* object);
    bool IsAnyNodeSelected();
    bool IsAnyLinkSelected();

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

    Link* FindLinkAt(const point& p);
    Control ComputeControl();

    vector<Node*>   Nodes;
    vector<Pin*>    Pins;
    vector<Link*>   Links;

    //Object*         HotObject;
    //Object*         ActiveObject;
    Object*         SelectedObject;
    vector<Object*> SelectedObjects;

    Link*           LastActiveLink;

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
    LinkCreateStage LinkCreateStage;
    ImU32           LinkColor;
    float           LinkThickness;
    Pin*            LinkStart;
    Pin*            LinkEnd;

    // Link deleting
    vector<Link*>   DeletedLinks;

    bool            IsInitialized;
    ImTextureID     HeaderTextureID;
    Settings        Settings;
};

} // namespace Detail
} // namespace Editor
} // namespace ax