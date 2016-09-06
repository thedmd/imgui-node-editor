#pragma once
#include "ImGuiInterop.h"
namespace ax { using namespace ImGuiInterop; }
#include "Types.h"
#include "EditorApi.h"
#define PICOJSON_USE_LOCALE 0
#include "picojson.h"
#include <vector>

namespace ax {
namespace Editor {
namespace Detail {

using std::vector;
using std::string;


//------------------------------------------------------------------------------
void Log(const char* fmt, ...);


//------------------------------------------------------------------------------
enum class ObjectType
{
    Node, Pin
};

using ax::Editor::PinKind;
using ax::Editor::StyleColor;
using ax::Editor::StyleVar;
using ax::Editor::SaveReasonFlags;

struct Context;

struct Node;
struct Pin;
struct Link;

struct Object
{
    enum DrawFlags
    {
        None     = 0,
        Hovered  = 1,
        Selected = 2
    };

    inline friend DrawFlags operator|(DrawFlags lhs, DrawFlags rhs)  { return static_cast<DrawFlags>(static_cast<int>(lhs) | static_cast<int>(rhs)); }
    inline friend DrawFlags operator&(DrawFlags lhs, DrawFlags rhs)  { return static_cast<DrawFlags>(static_cast<int>(lhs) & static_cast<int>(rhs)); }
    inline friend DrawFlags& operator|=(DrawFlags& lhs, DrawFlags rhs) { lhs = lhs | rhs; return lhs; }
    inline friend DrawFlags& operator&=(DrawFlags& lhs, DrawFlags rhs) { lhs = lhs & rhs; return lhs; }

    Context* const Editor;

    int     ID;
    bool    IsLive;

    Object(Context* editor, int id): Editor(editor), ID(id), IsLive(true) {}
    virtual ~Object() = default;

    bool IsVisible() const
    {
        if (!IsLive)
            return false;

        const auto bounds = GetBounds();

        return ImGui::IsRectVisible(to_imvec(bounds.top_left()), to_imvec(bounds.bottom_right()));
    }

    virtual void Draw(ImDrawList* drawList, DrawFlags flags = None) = 0;

    virtual bool AcceptDrag() { return false; }
    virtual void UpdateDrag(const ax::point& offset) { }
    virtual bool EndDrag() { return false; }

    virtual bool IsDraggable() { bool result = AcceptDrag(); EndDrag(); return result; }

    virtual bool TestHit(const ImVec2& point, float extraThickness = 0.0f) const
    {
        if (!IsLive)
            return false;

        auto bounds = GetBounds();
        if (extraThickness > 0)
            bounds.expand(extraThickness);

        return bounds.contains(to_pointf(point));
    }

    virtual bool TestHit(const ax::rectf& rect, bool allowIntersect = true) const
    {
        if (!IsLive)
            return false;

        const auto bounds = GetBounds();
        return !bounds.is_empty() && (allowIntersect ? bounds.intersects(rect) : rect.contains(bounds));
    }

    virtual ax::rectf GetBounds() const = 0;

    virtual Node*  AsNode()  { return nullptr; }
    virtual Pin*   AsPin()   { return nullptr; }
    virtual Link*  AsLink()  { return nullptr; }
};

struct Pin final: Object
{
    PinKind Kind;
    Node*   Node;
    rect    Bounds;
    rectf   Pivot;
    Pin*    PreviousPin;
    ImU32   Color;
    ImU32   BorderColor;
    float   BorderWidth;
    float   Rounding;
    int     Corners;
    ImVec2  Dir;
    float   Strength;
    float   Radius;
    float   ArrowSize;
    float   ArrowWidth;

    Pin(Context* editor, int id, PinKind kind):
        Object(editor, id), Kind(kind), Node(nullptr), Bounds(), PreviousPin(nullptr),
        Color(IM_COL32_WHITE), BorderColor(IM_COL32_BLACK), BorderWidth(0), Rounding(0),
        Corners(0), Dir(0, 0), Strength(0), Radius(0), ArrowSize(0), ArrowWidth(0)
    {
    }

    virtual void Draw(ImDrawList* drawList, DrawFlags flags = None) override final;

    ImVec2 GetClosestPoint(const ImVec2& p) const;
    line_f GetClosestLine(const Pin* pin) const;

    virtual ax::rectf GetBounds() const override final { return static_cast<rectf>(Bounds); }

    virtual Pin* AsPin() override final { return this; }
};

enum class NodeType
{
    Node,
    Group
};

struct Node final: Object
{
    NodeType Type;
    rect     Bounds;
    int      Channel;
    Pin*     LastPin;
    point    DragStart;

    ImU32    Color;
    ImU32    BorderColor;
    float    BorderWidth;
    float    Rounding;

    rect     GroupBounds;

    Node(Context* editor, int id):
        Object(editor, id),
        Type(NodeType::Node),
        Bounds(),
        Channel(0),
        LastPin(nullptr),
        DragStart(),
        Color(IM_COL32_WHITE),
        BorderColor(IM_COL32_BLACK),
        BorderWidth(0),
        Rounding(0),
        GroupBounds()
    {
    }

    bool AcceptDrag() override final;
    void UpdateDrag(const ax::point& offset) override final;
    bool EndDrag() override final; // return true, when changed

    virtual void Draw(ImDrawList* drawList, DrawFlags flags = None) override final;
    void DrawBorder(ImDrawList* drawList, ImU32 color, float thickness = 1.0f);

    void GetGroupedNodes(std::vector<Node*>& result, bool append = false);

    virtual ax::rectf GetBounds() const override final { return static_cast<rectf>(Bounds); }

    virtual Node* AsNode() override final { return this; }
};

struct Link final: Object
{
    Pin*   StartPin;
    Pin*   EndPin;
    ImU32  Color;
    float  Thickness;
    ImVec2 Start;
    ImVec2 End;

    Link(Context* editor, int id):
        Object(editor, id), StartPin(nullptr), EndPin(nullptr), Color(IM_COL32_WHITE), Thickness(1.0f)
    {
    }

    virtual void Draw(ImDrawList* drawList, DrawFlags flags = None) override final;
    void Draw(ImDrawList* drawList, ImU32 color, float extraThickness = 0.0f) const;

    void UpdateEndpoints();

    cubic_bezier_t GetCurve() const;

    virtual bool TestHit(const ImVec2& point, float extraThickness = 0.0f) const override final;
    virtual bool TestHit(const ax::rectf& rect, bool allowIntersect = true) const override final;

    virtual ax::rectf GetBounds() const override final;

    virtual Link* AsLink() override final { return this; }
};

struct NodeSettings
{
    int    ID;
    ImVec2 Location;
    bool   WasUsed;

    NodeSettings(int id): ID(id), WasUsed(false) {}
};

struct Settings
{
    bool                 Dirty;
    SaveReasonFlags      Reason;

    vector<NodeSettings> Nodes;
    ImVec2               ViewScroll;
    float                ViewZoom;

    Settings(): Dirty(false), Reason(SaveReasonFlags::None), ViewScroll(0, 0), ViewZoom(1.0f) {}

    NodeSettings* AddNode(int id);
    NodeSettings* FindNode(int id);

    std::string Serialize();

    static bool Parse(const char* data, const char* dataEnd, Settings& settings);
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
    bool    BackgroundHot;
    bool    BackgroundActive;
    bool    BackgroundClicked;

    Control(Object* hotObject, Object* activeObject, Object* clickedObject,
        bool backgroundHot, bool backgroundActive, bool backgroundClicked):
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
        BackgroundHot(backgroundHot),
        BackgroundActive(backgroundActive),
        BackgroundClicked(backgroundClicked)
    {
        if (hotObject)
        {
            HotNode  = hotObject->AsNode();
            HotPin   = hotObject->AsPin();
            HotLink  = hotObject->AsLink();

            if (HotPin)
                HotNode = HotPin->Node;
        }

        if (activeObject)
        {
            ActiveNode  = activeObject->AsNode();
            ActivePin   = activeObject->AsPin();
            ActiveLink  = activeObject->AsLink();
        }

        if (clickedObject)
        {
            ClickedNode  = clickedObject->AsNode();
            ClickedPin   = clickedObject->AsPin();
            ClickedLink  = clickedObject->AsLink();
        }
    }
};

// Spaces:
//   Canvas - where objects are
//   Client - where objects are drawn
//   Screen - global screen space
struct Canvas
{
    ImVec2 WindowScreenPos;
    ImVec2 WindowScreenSize;
    ImVec2 ClientOrigin;
    ImVec2 ClientSize;
    ImVec2 Zoom;
    ImVec2 InvZoom;

    Canvas();
    Canvas(ImVec2 position, ImVec2 size, ImVec2 scale, ImVec2 origin);

    ax::rectf GetVisibleBounds();

    ImVec2 FromScreen(ImVec2 point);
    ImVec2 ToScreen(ImVec2 point);
    ImVec2 FromClient(ImVec2 point);
    ImVec2 ToClient(ImVec2 point);
};

struct ScrollAction;
struct SizeAction;
struct DragAction;
struct SelectAction;
struct CreateItemAction;
struct DeleteItemsAction;
struct ContextMenuAction;

struct AnimationController;
struct FlowAnimationController;

struct Animation
{
    enum State
    {
        Playing,
        Stopped
    };

    Context*        Editor;
    State           State;
    float           Time;
    float           Duration;

    Animation(Context* editor);
    virtual ~Animation();

    void Play(float duration);
    void Stop();
    void Finish();
    void Update();

    bool IsPlaying() const { return State == Playing; }

    float GetProgress() const { return Time / Duration; }

protected:
    virtual void OnPlay() {}
    virtual void OnFinish() {}
    virtual void OnStop() {}

    virtual void OnUpdate(float progress) {}
};

struct ScrollAnimation final: Animation
{
    ScrollAction& Action;
    ImVec2        Start;
    float         StartZoom;
    ImVec2        Target;
    float         TargetZoom;

    ScrollAnimation(Context* editor, ScrollAction& scrollAction);

    void NavigateTo(const ImVec2& target, float targetZoom, float duration);

private:
    void OnUpdate(float progress) override final;
    void OnStop() override final;
    void OnFinish() override final;
};

struct FlowAnimation final: Animation
{
    FlowAnimationController* Controller;
    Link* Link;
    float Speed;
    float MarkerDistance;
    float Offset;

    FlowAnimation(FlowAnimationController* controller);

    void Flow(ax::Editor::Detail::Link* link, float markerDistance, float speed, float duration);

    void Draw(ImDrawList* drawList);

private:
    struct CurvePoint
    {
        float  Distance;
        ImVec2 Point;
    };

    ImVec2 LastStart;
    ImVec2 LastEnd;
    float  PathLength;
    vector<CurvePoint> Path;

    bool IsLinkValid() const;
    bool IsPathValid() const;
    void UpdatePath();
    void ClearPath();

    ImVec2 SamplePath(float distance);

    void OnUpdate(float progress) override final;
    void OnStop() override final;
};

struct AnimationController
{
    Context* Editor;

    AnimationController(Context* editor):
        Editor(editor)
    {
    }

    virtual ~AnimationController()
    {
    }

    virtual void Draw(ImDrawList* drawList)
    {
    }
};

struct FlowAnimationController final : AnimationController
{
    FlowAnimationController(Context* editor);
    virtual ~FlowAnimationController();

    void Flow(Link* link);

    virtual void Draw(ImDrawList* drawList) override final;

    void Release(FlowAnimation* animation);

private:
    FlowAnimation* GetOrCreate(Link* link);

    vector<FlowAnimation*> Animations;
    vector<FlowAnimation*> FreePool;
};

struct EditorAction
{
    enum AcceptResult { False, True, Possible };

    EditorAction(Context* editor): Editor(editor) {}
    virtual ~EditorAction() {}

    virtual const char* GetName() const = 0;

    virtual AcceptResult Accept(const Control& control) = 0;
    virtual bool Process(const Control& control) = 0;

    virtual ImGuiMouseCursor GetCursor() { return ImGuiMouseCursor_Arrow; }

    virtual void ShowMetrics() {}

    virtual ScrollAction*      AsScroll()      { return nullptr; }
    virtual SizeAction*        AsSize()        { return nullptr; }
    virtual DragAction*        AsDrag()        { return nullptr; }
    virtual SelectAction*      AsSelect()      { return nullptr; }
    virtual CreateItemAction*  AsCreateItem()  { return nullptr; }
    virtual DeleteItemsAction* AsDeleteItems() { return nullptr; }
    virtual ContextMenuAction* AsContextMenu() { return nullptr; }

    Context* Editor;
};

struct ScrollAction final: EditorAction
{
    enum class NavigationReason
    {
        Unknown,
        MouseZoom,
        Selection,
        Object,
        Content
    };

    bool            IsActive;
    float           Zoom;
    ImVec2          Scroll;

    ScrollAction(Context* editor);

    virtual const char* GetName() const override final { return "Scroll"; }

    virtual AcceptResult Accept(const Control& control) override final;
    virtual bool Process(const Control& control) override final;

    virtual void ShowMetrics() override final;

    virtual ScrollAction* AsScroll() override final { return this; }

    void NavigateTo(const ax::rectf& bounds, bool zoomIn, float duration = -1.0f, NavigationReason reason = NavigationReason::Unknown);
    void StopNavigation();
    void FinishNavigation();

    void SetWindow(ImVec2 position, ImVec2 size);

    Canvas GetCanvas(bool alignToPixels = true);

private:
    ImVec2 WindowScreenPos;
    ImVec2 WindowScreenSize;
    ImVec2 ScrollStart;

    ScrollAnimation  Animation;
    NavigationReason Reason;
    uint64_t         LastSelectionId;
    Object*          LastObject;

    void NavigateTo(const ImVec2& target, float targetZoom, float duration = -1.0f, NavigationReason reason = NavigationReason::Unknown);

    float MatchZoom(int steps, float fallbackZoom);
    int MatchZoomIndex(int direction);

    static const float s_ZoomLevels[];
    static const int   s_ZoomLevelCount;
};

struct SizeAction final: EditorAction
{
    bool IsActive;
    Node* SizedNode;

    SizeAction(Context* editor);

    virtual const char* GetName() const override final { return "Size"; }

    virtual AcceptResult Accept(const Control& control) override final;
    virtual bool Process(const Control& control) override final;

    virtual ImGuiMouseCursor GetCursor() override final { return Cursor; }

    virtual void ShowMetrics() override final;

    virtual SizeAction* AsSize() override final { return this; }

private:
    ax::rect_region GetRegion(Node* node);
    ImGuiMouseCursor ChooseCursor(ax::rect_region region);

    ax::rect         StartBounds;
    ax::rect         StartGroupBounds;
    ax::rect_region  Pivot;
    ImGuiMouseCursor Cursor;
};

struct DragAction final: EditorAction
{
    bool            IsActive;
    Object*         DraggedObject;
    vector<Object*> Objects;

    DragAction(Context* editor);

    virtual const char* GetName() const override final { return "Drag"; }

    virtual AcceptResult Accept(const Control& control) override final;
    virtual bool Process(const Control& control) override final;

    virtual void ShowMetrics() override final;

    virtual DragAction* AsDrag() override final { return this; }
};

struct SelectAction final: EditorAction
{
    bool            IsActive;

    bool            SelectLinkMode;
    ImVec2          StartPoint;
    ImVec2          EndPoint;
    vector<Object*> CandidateObjects;
    vector<Object*> SelectedObjectsAtStart;

    Animation       Animation;

    SelectAction(Context* editor);

    virtual const char* GetName() const override final { return "Select"; }

    virtual AcceptResult Accept(const Control& control) override final;
    virtual bool Process(const Control& control) override final;

    virtual void ShowMetrics() override final;

    virtual SelectAction* AsSelect() override final { return this; }

    void Draw(ImDrawList* drawList);
};

struct ContextMenuAction final: EditorAction
{
    enum Menu { None, Node, Pin, Link, Background };

    Menu CurrentMenu;
    int  ContextId;

    ContextMenuAction(Context* editor);

    virtual const char* GetName() const override final { return "Context Menu"; }

    virtual AcceptResult Accept(const Control& control) override final;
    virtual bool Process(const Control& control) override final;

    virtual void ShowMetrics() override final;

    virtual ContextMenuAction* AsContextMenu() override final { return this; }

    bool ShowNodeContextMenu(int* nodeId);
    bool ShowPinContextMenu(int* pinId);
    bool ShowLinkContextMenu(int* linkId);
    bool ShowBackgroundContextMenu();
};

struct CreateItemAction final : EditorAction
{
    enum Stage
    {
        None,
        Possible,
        Create
    };

    enum Action
    {
        Unknown,
        UserReject,
        UserAccept
    };

    enum Type
    {
        NoItem,
        Node,
        Link
    };

    enum Result
    {
        True,
        False,
        Indeterminate
    };

    bool      InActive;
    Stage     NextStage;

    Stage     CurrentStage;
    Type      ItemType;
    Action    UserAction;
    ImU32     LinkColor;
    float     LinkThickness;
    Pin*      LinkStart;
    Pin*      LinkEnd;

    bool      IsActive;
    Pin*      DraggedPin;


    CreateItemAction(Context* editor);

    virtual const char* GetName() const override final { return "Create Item"; }

    virtual AcceptResult Accept(const Control& control) override final;
    virtual bool Process(const Control& control) override final;

    virtual void ShowMetrics() override final;

    virtual CreateItemAction* AsCreateItem() override final { return this; }

    void SetStyle(ImU32 color, float thickness);

    bool Begin();
    void End();

    Result RejectItem();
    Result AcceptItem();

    Result QueryLink(int* startId, int* endId);
    Result QueryNode(int* pinId);

private:
    void DragStart(Pin* startPin);
    void DragEnd();
    void DropPin(Pin* endPin);
    void DropNode();
    void DropNothing();
};

struct DeleteItemsAction final: EditorAction
{
    bool    IsActive;
    bool    InInteraction;

    DeleteItemsAction(Context* editor);

    virtual const char* GetName() const override final { return "Delete Items"; }

    virtual AcceptResult Accept(const Control& control) override final;
    virtual bool Process(const Control& control) override final;

    virtual void ShowMetrics() override final;

    virtual DeleteItemsAction* AsDeleteItems() override final { return this; }

    bool Begin();
    void End();

    bool QueryLink(int* linkId);
    bool QueryNode(int* nodeId);

    bool AcceptItem();
    void RejectItem();

private:
    enum IteratorType { Unknown, Link, Node };
    enum UserAction { Undetermined, Accepted, Rejected };

    bool QueryItem(int* itemId, IteratorType itemType);
    void RemoveItem();

    IteratorType    CurrentItemType;
    UserAction      UserAction;
    vector<Object*> CandidateObjects;
    int             CandidateItemIndex;
};

struct NodeBuilder
{
    Context* const Editor;

    Node* CurrentNode;
    Pin*  CurrentPin;

    rect   NodeRect;

    rect   PivotRect;
    ImVec2 PivotAlignment;
    ImVec2 PivotSize;
    ImVec2 PivotScale;
    bool   ResolvePinRect;
    bool   ResolvePivot;

    rect   GroupBounds;
    bool   IsGroup;

    NodeBuilder(Context* editor);

    void Begin(int nodeId);
    void End();

    void BeginPin(int pinId, PinKind kind);
    void EndPin();

    void PinRect(const ImVec2& a, const ImVec2& b);
    void PinPivotRect(const ImVec2& a, const ImVec2& b);
    void PinPivotSize(const ImVec2& size);
    void PinPivotScale(const ImVec2& scale);
    void PinPivotAlignment(const ImVec2& alignment);

    void Group(const ImVec2& size);

    ImDrawList* GetBackgroundDrawList() const;
    ImDrawList* GetBackgroundDrawList(Node* node) const;
};

struct Style: ax::Editor::Style
{
    void PushColor(StyleColor colorIndex, const ImVec4& color);
    void PopColor(int count = 1);

    void PushVar(StyleVar varIndex, float value);
    void PushVar(StyleVar varIndex, const ImVec2& value);
    void PushVar(StyleVar varIndex, const ImVec4& value);
    void PopVar(int count = 1);

    const char* GetColorName(StyleColor colorIndex) const;

private:
    struct ColorModifier
    {
        StyleColor  Index;
        ImVec4      Value;
    };

    struct VarModifier
    {
        StyleVar Index;
        ImVec4   Value;
    };

    float* GetVarFloatAddr(StyleVar idx);
    ImVec2* GetVarVec2Addr(StyleVar idx);
    ImVec4* GetVarVec4Addr(StyleVar idx);

    vector<ColorModifier>   ColorStack;
    vector<VarModifier>     VarStack;
};

struct Context
{
    Context(const Config* config = nullptr);
    ~Context();

    Style& GetStyle() { return Style; }

    void Begin(const char* id, const ImVec2& size = ImVec2(0, 0));
    void End();

    bool DoLink(int id, int startPinId, int endPinId, ImU32 color, float thickness);

    NodeBuilder& GetNodeBuilder() { return NodeBuilder; }

    EditorAction* GetCurrentAction() { return CurrentAction; }

    CreateItemAction& GetItemCreator() { return CreateItemAction; }
    DeleteItemsAction& GetItemDeleter() { return DeleteItemsAction; }
    ContextMenuAction& GetContextMenu() { return ContextMenuAction; }

    void SetNodePosition(int nodeId, const ImVec2& screenPosition);
    ImVec2 GetNodePosition(int nodeId);

    void ClearSelection();
    void SelectObject(Object* object);
    void DeselectObject(Object* object);
    void SetSelectedObject(Object* object);
    void ToggleObjectSelection(Object* object);
    bool IsSelected(Object* object);
    const vector<Object*>& GetSelectedObjects();
    bool IsAnyNodeSelected();
    bool IsAnyLinkSelected();
    bool HasSelectionChanged();
    uint64_t GetSelectionId() const { return SelectionId; }

    void FindNodesInRect(const ax::rectf& r, vector<Node*>& result, bool append = false, bool includeIntersecting = true);
    void FindLinksInRect(const ax::rectf& r, vector<Link*>& result, bool append = false);

    void FindLinksForNode(int nodeId, vector<Link*>& result, bool add = false);

    ImVec2 ToCanvas(ImVec2 point) { return Canvas.FromScreen(point); }
    ImVec2 ToScreen(ImVec2 point) { return Canvas.ToScreen(point); }

    void NotifyLinkDeleted(Link* link);

    void Suspend();
    void Resume();

    void MarkSettingsDirty(SaveReasonFlags reason);

    Pin*    CreatePin(int id, PinKind kind);
    Node*   CreateNode(int id);
    Link*   CreateLink(int id);
    Object* FindObject(int id);

    Node*  FindNode(int id);
    Pin*   FindPin(int id);
    Link*  FindLink(int id);

    Node*  GetNode(int id);
    Pin*   GetPin(int id, PinKind kind);
    Link*  GetLink(int id);

    Link* FindLinkAt(const point& p);

    template <typename T>
    ax::rectf GetBounds(const std::vector<T*>& objects)
    {
        ax::rectf bounds;

        for (auto object : objects)
            bounds = make_union(bounds, object->GetBounds());

        return bounds;
    }

    ax::rectf GetSelectionBounds() { return GetBounds(SelectedObjects); }
    ax::rectf GetContentBounds() { return GetBounds(Nodes); }

    ImU32 GetColor(StyleColor colorIndex) const;
    ImU32 GetColor(StyleColor colorIndex, float alpha) const;

    void NavigateTo(const rectf& bounds, bool zoomIn = false, float duration = -1) { ScrollAction.NavigateTo(bounds, zoomIn, duration); }

    void RegisterAnimation(Animation* animation);
    void UnregisterAnimation(Animation* animation);

    void Flow(Link* link);

    void SetUserContext();

private:
    void LoadSettings();
    void SaveSettings();

    Control ComputeControl();

    void ShowMetrics(const Control& control);

    void CaptureMouse();
    void ReleaseMouse();

    void UpdateAnimations();

    Style               Style;

    vector<Node*>       Nodes;
    vector<Pin*>        Pins;
    vector<Link*>       Links;

    vector<Object*>     SelectedObjects;

    vector<Object*>     LastSelectedObjects;
    uint64_t            SelectionId;
    bool                SelectionChanged;

    Link*               LastActiveLink;

    vector<Animation*>  LiveAnimations;
    vector<Animation*>  LastLiveAnimations;

    ImVec2              MousePosBackup;
    ImVec2              MouseClickPosBackup[5];

    Canvas              Canvas;

    bool                IsSuspended;

    NodeBuilder         NodeBuilder;

    EditorAction*       CurrentAction;
    ScrollAction        ScrollAction;
    SizeAction          SizeAction;
    DragAction          DragAction;
    SelectAction        SelectAction;
    ContextMenuAction   ContextMenuAction;
    CreateItemAction    CreateItemAction;
    DeleteItemsAction   DeleteItemsAction;

    vector<AnimationController*> AnimationControllers;
    FlowAnimationController      FlowAnimationController;

    bool                IsInitialized;
    Settings            Settings;

    Config              Config;
};

} // namespace Detail
} // namespace Editor
} // namespace ax