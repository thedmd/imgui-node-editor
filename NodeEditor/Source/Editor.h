//------------------------------------------------------------------------------
// LICENSE
//   This software is dual-licensed to the public domain and under the following
//   license: you are granted a perpetual, irrevocable license to copy, modify,
//   publish, and distribute this file as you see fit.
//
// CREDITS
//   Written by Michal Cichon
//------------------------------------------------------------------------------
# pragma once


//------------------------------------------------------------------------------
# include "Shared/Interop.h"
# include "Shared/Math2D.h"
# include "NodeEditor.h"
# define PICOJSON_USE_LOCALE 0
# include "picojson.h"
# include <vector>


//------------------------------------------------------------------------------
namespace ax {
namespace NodeEditor {
namespace Detail {


//------------------------------------------------------------------------------
namespace ed = ax::NodeEditor::Detail;
namespace json = picojson;


//------------------------------------------------------------------------------
using std::vector;
using std::string;


//------------------------------------------------------------------------------
void Log(const char* fmt, ...);


//------------------------------------------------------------------------------
enum class ObjectType
{
    None, Node, Link, Pin
};

using ax::NodeEditor::PinKind;
using ax::NodeEditor::StyleColor;
using ax::NodeEditor::StyleVar;
using ax::NodeEditor::SaveReasonFlags;

using ax::NodeEditor::NodeId;
using ax::NodeEditor::PinId;
using ax::NodeEditor::LinkId;

struct ObjectId: Details::SafePointerType<ObjectId>
{
    using SafePointerType::SafePointerType;

    ObjectId():                  SafePointerType(),                     m_Type(ObjectType::None)   {}
    ObjectId(PinId  pinId):      SafePointerType(pinId.AsPointer()),    m_Type(ObjectType::Pin)    {}
    ObjectId(NodeId nodeId):     SafePointerType(nodeId.AsPointer()),   m_Type(ObjectType::Node)   {}
    ObjectId(LinkId linkId):     SafePointerType(linkId.AsPointer()),   m_Type(ObjectType::Link)   {}

    ObjectType Type() const { return m_Type; }

    PinId    AsPinId()    const { IM_ASSERT(IsPinId());    return PinId(AsPointer());    }
    NodeId   AsNodeId()   const { IM_ASSERT(IsNodeId());   return NodeId(AsPointer());   }
    LinkId   AsLinkId()   const { IM_ASSERT(IsLinkId());   return LinkId(AsPointer());   }

    bool IsPinId()    const { return m_Type == ObjectType::Pin;    }
    bool IsNodeId()   const { return m_Type == ObjectType::Node;   }
    bool IsLinkId()   const { return m_Type == ObjectType::Link;   }

    friend bool operator==(const ObjectId& lhs, const ObjectId& rhs)
    {
        return lhs.Type() == rhs.Type()
            && lhs.AsPointer() == rhs.AsPointer();
    }

    friend bool operator!=(const ObjectId& lhs, const ObjectId& rhs)
    {
        return !(lhs == rhs);
    }

private:
    ObjectType m_Type;
};

struct EditorContext;

struct Node;
struct Pin;
struct Link;

template <typename T, typename Id = typename T::IdType>
struct ObjectWrapper
{
    Id   m_ID;
    T*   m_Object;

          T* operator->()        { return m_Object; }
    const T* operator->() const  { return m_Object; }

    operator T*()             { return m_Object; }
    operator const T*() const { return m_Object; }

    bool operator<(const ObjectWrapper& rhs) const
    {
        return m_ID.AsPointer() < rhs.m_ID.AsPointer();
    }
};

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

    EditorContext* const Editor;

    bool    m_IsLive;

    Object(EditorContext* editor): Editor(editor), m_IsLive(true) {}
    virtual ~Object() = default;

    virtual ObjectId ID() = 0;

    bool IsVisible() const
    {
        if (!m_IsLive)
            return false;

        const auto bounds = GetBounds();

        return ImGui::IsRectVisible(to_imvec(bounds.top_left()), to_imvec(bounds.bottom_right()));
    }

    virtual void Reset() { m_IsLive = false; }

    virtual void Draw(ImDrawList* drawList, DrawFlags flags = None) = 0;

    virtual bool AcceptDrag() { return false; }
    virtual void UpdateDrag(const ax::point& offset) { }
    virtual bool EndDrag() { return false; }
    virtual ax::point DragStartLocation() { return (ax::point)GetBounds().location; }

    virtual bool IsDraggable() { bool result = AcceptDrag(); EndDrag(); return result; }
    virtual bool IsSelectable() { return false; }

    virtual bool TestHit(const ImVec2& point, float extraThickness = 0.0f) const
    {
        if (!m_IsLive)
            return false;

        auto bounds = GetBounds();
        if (extraThickness > 0)
            bounds.expand(extraThickness);

        return bounds.contains(to_pointf(point));
    }

    virtual bool TestHit(const ax::rectf& rect, bool allowIntersect = true) const
    {
        if (!m_IsLive)
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
    using IdType = PinId;

    PinId   m_ID;
    PinKind m_Kind;
    Node*   m_Node;
    rect    m_Bounds;
    rectf   m_Pivot;
    Pin*    m_PreviousPin;
    ImU32   m_Color;
    ImU32   m_BorderColor;
    float   m_BorderWidth;
    float   m_Rounding;
    int     m_Corners;
    ImVec2  m_Dir;
    float   m_Strength;
    float   m_Radius;
    float   m_ArrowSize;
    float   m_ArrowWidth;
    bool    m_HasConnection;
    bool    m_HadConnection;

    Pin(EditorContext* editor, PinId id, PinKind kind):
        Object(editor), m_ID(id), m_Kind(kind), m_Node(nullptr), m_Bounds(), m_PreviousPin(nullptr),
        m_Color(IM_COL32_WHITE), m_BorderColor(IM_COL32_BLACK), m_BorderWidth(0), m_Rounding(0),
        m_Corners(0), m_Dir(0, 0), m_Strength(0), m_Radius(0), m_ArrowSize(0), m_ArrowWidth(0),
        m_HasConnection(false), m_HadConnection(false)
    {
    }

    virtual ObjectId ID() override { return m_ID; }

    virtual void Reset() override final
    {
        m_HadConnection = m_HasConnection && m_IsLive;
        m_HasConnection = false;

        Object::Reset();
    }

    virtual void Draw(ImDrawList* drawList, DrawFlags flags = None) override final;

    ImVec2 GetClosestPoint(const ImVec2& p) const;
    line_f GetClosestLine(const Pin* pin) const;

    virtual ax::rectf GetBounds() const override final { return static_cast<rectf>(m_Bounds); }

    virtual Pin* AsPin() override final { return this; }
};

enum class NodeType
{
    Node,
    Group
};

struct Node final: Object
{
    using IdType = NodeId;

    NodeId   m_ID;
    NodeType m_Type;
    rect     m_Bounds;
    int      m_Channel;
    Pin*     m_LastPin;
    point    m_DragStart;

    ImU32    m_Color;
    ImU32    m_BorderColor;
    float    m_BorderWidth;
    float    m_Rounding;

    ImU32    m_GroupColor;
    ImU32    m_GroupBorderColor;
    float    m_GroupBorderWidth;
    float    m_GroupRounding;
    rect     m_GroupBounds;

    bool     m_RestoreState;
    bool     m_CenterOnScreen;

    Node(EditorContext* editor, NodeId id):
        Object(editor),
        m_ID(id),
        m_Type(NodeType::Node),
        m_Bounds(),
        m_Channel(0),
        m_LastPin(nullptr),
        m_DragStart(),
        m_Color(IM_COL32_WHITE),
        m_BorderColor(IM_COL32_BLACK),
        m_BorderWidth(0),
        m_Rounding(0),
        m_GroupBounds(),
        m_RestoreState(false),
        m_CenterOnScreen(false)
    {
    }

    virtual ObjectId ID() override { return m_ID; }

    bool AcceptDrag() override final;
    void UpdateDrag(const ax::point& offset) override final;
    bool EndDrag() override final; // return true, when changed
    ax::point DragStartLocation() override final { return m_DragStart; }

    virtual bool IsSelectable() override { return true; }

    virtual void Draw(ImDrawList* drawList, DrawFlags flags = None) override final;
    void DrawBorder(ImDrawList* drawList, ImU32 color, float thickness = 1.0f);

    void GetGroupedNodes(std::vector<Node*>& result, bool append = false);

    void CenterOnScreenInNextFrame() { m_CenterOnScreen = true; }

    virtual ax::rectf GetBounds() const override final { return static_cast<rectf>(m_Bounds); }

    virtual Node* AsNode() override final { return this; }
};

struct Link final: Object
{
    using IdType = LinkId;

    LinkId m_ID;
    Pin*   m_StartPin;
    Pin*   m_EndPin;
    ImU32  m_Color;
    float  m_Thickness;
    ImVec2 m_Start;
    ImVec2 m_End;

    Link(EditorContext* editor, LinkId id):
        Object(editor), m_ID(id), m_StartPin(nullptr), m_EndPin(nullptr), m_Color(IM_COL32_WHITE), m_Thickness(1.0f)
    {
    }

    virtual ObjectId ID() override { return m_ID; }

    virtual bool IsSelectable() override { return true; }

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
    NodeId m_ID;
    ImVec2 m_Location;
    ImVec2 m_Size;
    ImVec2 m_GroupSize;
    bool   m_WasUsed;

    bool            m_Saved;
    bool            m_IsDirty;
    SaveReasonFlags m_DirtyReason;

    NodeSettings(NodeId id): m_ID(id), m_Location(0, 0), m_Size(0, 0), m_GroupSize(0, 0), m_WasUsed(false), m_Saved(false), m_IsDirty(false), m_DirtyReason(SaveReasonFlags::None) {}

    void ClearDirty();
    void MakeDirty(SaveReasonFlags reason);

    json::object Serialize();

    static bool Parse(const std::string& string, NodeSettings& settings) { return Parse(string.data(), string.data() + string.size(), settings); }
    static bool Parse(const char* data, const char* dataEnd, NodeSettings& settings);
    static bool Parse(const json::value& data, NodeSettings& result);
};

struct Settings
{
    bool                 m_IsDirty;
    SaveReasonFlags      m_DirtyReason;

    vector<NodeSettings> m_Nodes;
    vector<ObjectId>     m_Selection;
    ImVec2               m_ViewScroll;
    float                m_ViewZoom;

    Settings(): m_IsDirty(false), m_DirtyReason(SaveReasonFlags::None), m_ViewScroll(0, 0), m_ViewZoom(1.0f) {}

    NodeSettings* AddNode(NodeId id);
    NodeSettings* FindNode(NodeId id);

    void ClearDirty(Node* node = nullptr);
    void MakeDirty(SaveReasonFlags reason, Node* node = nullptr);

    std::string Serialize();

    static bool Parse(const std::string& string, Settings& settings) { return Parse(string.data(), string.data() + string.size(), settings); }
    static bool Parse(const char* data, const char* dataEnd, Settings& settings);
};

struct Control
{
    Object* HotObject;
    Object* ActiveObject;
    Object* ClickedObject;
    Object* DoubleClickedObject;
    Node*   HotNode;
    Node*   ActiveNode;
    Node*   ClickedNode;
    Node*   DoubleClickedNode;
    Pin*    HotPin;
    Pin*    ActivePin;
    Pin*    ClickedPin;
    Pin*    DoubleClickedPin;
    Link*   HotLink;
    Link*   ActiveLink;
    Link*   ClickedLink;
    Link*   DoubleClickedLink;
    bool    BackgroundHot;
    bool    BackgroundActive;
    bool    BackgroundClicked;
    bool    BackgroundDoubleClicked;

    Control(Object* hotObject, Object* activeObject, Object* clickedObject, Object* doubleClickedObject,
        bool backgroundHot, bool backgroundActive, bool backgroundClicked, bool backgroundDoubleClicked):
        HotObject(hotObject),
        ActiveObject(activeObject),
        ClickedObject(clickedObject),
        DoubleClickedObject(doubleClickedObject),
        HotNode(nullptr),
        ActiveNode(nullptr),
        ClickedNode(nullptr),
        DoubleClickedNode(nullptr),
        HotPin(nullptr),
        ActivePin(nullptr),
        ClickedPin(nullptr),
        DoubleClickedPin(nullptr),
        HotLink(nullptr),
        ActiveLink(nullptr),
        ClickedLink(nullptr),
        DoubleClickedLink(nullptr),
        BackgroundHot(backgroundHot),
        BackgroundActive(backgroundActive),
        BackgroundClicked(backgroundClicked),
        BackgroundDoubleClicked(backgroundDoubleClicked)
    {
        if (hotObject)
        {
            HotNode  = hotObject->AsNode();
            HotPin   = hotObject->AsPin();
            HotLink  = hotObject->AsLink();

            if (HotPin)
                HotNode = HotPin->m_Node;
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

        if (doubleClickedObject)
        {
            DoubleClickedNode = doubleClickedObject->AsNode();
            DoubleClickedPin  = doubleClickedObject->AsPin();
            DoubleClickedLink = doubleClickedObject->AsLink();
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

    ax::rectf GetVisibleBounds() const;

    ImVec2 FromScreen(ImVec2 point) const;
    ImVec2 ToScreen(ImVec2 point) const;
    ImVec2 FromClient(ImVec2 point) const;
    ImVec2 ToClient(ImVec2 point) const;
};

struct NavigateAction;
struct SizeAction;
struct DragAction;
struct SelectAction;
struct CreateItemAction;
struct DeleteItemsAction;
struct ContextMenuAction;
struct ShortcutAction;

struct AnimationController;
struct FlowAnimationController;

struct Animation
{
    enum State
    {
        Playing,
        Stopped
    };

    EditorContext*  Editor;
    State           m_State;
    float           m_Time;
    float           m_Duration;

    Animation(EditorContext* editor);
    virtual ~Animation();

    void Play(float duration);
    void Stop();
    void Finish();
    void Update();

    bool IsPlaying() const { return m_State == Playing; }

    float GetProgress() const { return m_Time / m_Duration; }

protected:
    virtual void OnPlay() {}
    virtual void OnFinish() {}
    virtual void OnStop() {}

    virtual void OnUpdate(float progress) {}
};

struct NavigateAnimation final: Animation
{
    NavigateAction& Action;
    ImVec2        m_Start;
    float         m_StartZoom;
    ImVec2        m_Target;
    float         m_TargetZoom;

    NavigateAnimation(EditorContext* editor, NavigateAction& scrollAction);

    void NavigateTo(const ImVec2& target, float targetZoom, float duration);

private:
    void OnUpdate(float progress) override final;
    void OnStop() override final;
    void OnFinish() override final;
};

struct FlowAnimation final: Animation
{
    FlowAnimationController* Controller;
    Link* m_Link;
    float m_Speed;
    float m_MarkerDistance;
    float m_Offset;

    FlowAnimation(FlowAnimationController* controller);

    void Flow(Link* link, float markerDistance, float speed, float duration);

    void Draw(ImDrawList* drawList);

private:
    struct CurvePoint
    {
        float  Distance;
        ImVec2 Point;
    };

    ImVec2 m_LastStart;
    ImVec2 m_LastEnd;
    float  m_PathLength;
    vector<CurvePoint> m_Path;

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
    EditorContext* Editor;

    AnimationController(EditorContext* editor):
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
    FlowAnimationController(EditorContext* editor);
    virtual ~FlowAnimationController();

    void Flow(Link* link);

    virtual void Draw(ImDrawList* drawList) override final;

    void Release(FlowAnimation* animation);

private:
    FlowAnimation* GetOrCreate(Link* link);

    vector<FlowAnimation*> m_Animations;
    vector<FlowAnimation*> m_FreePool;
};

struct EditorAction
{
    enum AcceptResult { False, True, Possible };

    EditorAction(EditorContext* editor): Editor(editor) {}
    virtual ~EditorAction() {}

    virtual const char* GetName() const = 0;

    virtual AcceptResult Accept(const Control& control) = 0;
    virtual bool Process(const Control& control) = 0;
    virtual void Reject() {} // celled when Accept return 'Possible' and was rejected

    virtual ImGuiMouseCursor GetCursor() { return ImGuiMouseCursor_Arrow; }

    virtual bool IsDragging() { return false; }

    virtual void ShowMetrics() {}

    virtual NavigateAction*     AsNavigate()     { return nullptr; }
    virtual SizeAction*         AsSize()         { return nullptr; }
    virtual DragAction*         AsDrag()         { return nullptr; }
    virtual SelectAction*       AsSelect()       { return nullptr; }
    virtual CreateItemAction*   AsCreateItem()   { return nullptr; }
    virtual DeleteItemsAction*  AsDeleteItems()  { return nullptr; }
    virtual ContextMenuAction*  AsContextMenu()  { return nullptr; }
    virtual ShortcutAction* AsCutCopyPaste() { return nullptr; }

    EditorContext* Editor;
};

struct NavigateAction final: EditorAction
{
    enum class NavigationReason
    {
        Unknown,
        MouseZoom,
        Selection,
        Object,
        Content,
        Edge
    };

    bool            m_IsActive;
    float           m_Zoom;
    ImVec2          m_Scroll;
    ImVec2          m_ScrollStart;
    ImVec2          m_ScrollDelta;

    NavigateAction(EditorContext* editor);

    virtual const char* GetName() const override final { return "Navigate"; }

    virtual AcceptResult Accept(const Control& control) override final;
    virtual bool Process(const Control& control) override final;

    virtual void ShowMetrics() override final;

    virtual NavigateAction* AsNavigate() override final { return this; }

    void NavigateTo(const ax::rectf& bounds, bool zoomIn, float duration = -1.0f, NavigationReason reason = NavigationReason::Unknown);
    void StopNavigation();
    void FinishNavigation();

    bool MoveOverEdge();
    void StopMoveOverEdge();
    bool IsMovingOverEdge() const { return m_MovingOverEdge; }
    ImVec2 GetMoveOffset() const { return m_MoveOffset; }

    void SetWindow(ImVec2 position, ImVec2 size);

    Canvas GetCanvas(bool alignToPixels = true);

private:
    ImVec2 m_WindowScreenPos;
    ImVec2 m_WindowScreenSize;

    NavigateAnimation  m_Animation;
    NavigationReason   m_Reason;
    uint64_t           m_LastSelectionId;
    Object*            m_LastObject;
    bool               m_MovingOverEdge;
    ImVec2             m_MoveOffset;

    bool HandleZoom(const Control& control);

    void NavigateTo(const ImVec2& target, float targetZoom, float duration = -1.0f, NavigationReason reason = NavigationReason::Unknown);

    float MatchZoom(int steps, float fallbackZoom);
    int MatchZoomIndex(int direction);

    static const float s_ZoomLevels[];
    static const int   s_ZoomLevelCount;
};

struct SizeAction final: EditorAction
{
    bool  m_IsActive;
    bool  m_Clean;
    Node* m_SizedNode;

    SizeAction(EditorContext* editor);

    virtual const char* GetName() const override final { return "Size"; }

    virtual AcceptResult Accept(const Control& control) override final;
    virtual bool Process(const Control& control) override final;

    virtual ImGuiMouseCursor GetCursor() override final { return m_Cursor; }

    virtual void ShowMetrics() override final;

    virtual SizeAction* AsSize() override final { return this; }

    virtual bool IsDragging() override final { return m_IsActive; }

    const ax::rect& GetStartGroupBounds() const { return m_StartGroupBounds; }

private:
    ax::rect_region GetRegion(Node* node);
    ImGuiMouseCursor ChooseCursor(ax::rect_region region);

    ax::rect         m_StartBounds;
    ax::rect         m_StartGroupBounds;
    ax::size         m_LastSize;
    ax::point        m_LastDragOffset;
    bool             m_Stable;
    ax::rect_region  m_Pivot;
    ImGuiMouseCursor m_Cursor;
};

struct DragAction final: EditorAction
{
    bool            m_IsActive;
    bool            m_Clear;
    Object*         m_DraggedObject;
    vector<Object*> m_Objects;

    DragAction(EditorContext* editor);

    virtual const char* GetName() const override final { return "Drag"; }

    virtual AcceptResult Accept(const Control& control) override final;
    virtual bool Process(const Control& control) override final;

    virtual ImGuiMouseCursor GetCursor() override final { return ImGuiMouseCursor_ResizeAll; }

    virtual bool IsDragging() override final { return m_IsActive; }

    virtual void ShowMetrics() override final;

    virtual DragAction* AsDrag() override final { return this; }
};

struct SelectAction final: EditorAction
{
    bool            m_IsActive;

    bool            m_SelectGroups;
    bool            m_SelectLinkMode;
    bool            m_CommitSelection;
    ImVec2          m_StartPoint;
    ImVec2          m_EndPoint;
    vector<Object*> m_CandidateObjects;
    vector<Object*> m_SelectedObjectsAtStart;

    Animation       m_Animation;

    SelectAction(EditorContext* editor);

    virtual const char* GetName() const override final { return "Select"; }

    virtual AcceptResult Accept(const Control& control) override final;
    virtual bool Process(const Control& control) override final;

    virtual void ShowMetrics() override final;

    virtual bool IsDragging() override final { return m_IsActive; }

    virtual SelectAction* AsSelect() override final { return this; }

    void Draw(ImDrawList* drawList);
};

struct ContextMenuAction final: EditorAction
{
    enum Menu { None, Node, Pin, Link, Background };

    Menu m_CandidateMenu;
    Menu m_CurrentMenu;
    ObjectId m_ContextId;

    ContextMenuAction(EditorContext* editor);

    virtual const char* GetName() const override final { return "Context Menu"; }

    virtual AcceptResult Accept(const Control& control) override final;
    virtual bool Process(const Control& control) override final;
    virtual void Reject() override final;

    virtual void ShowMetrics() override final;

    virtual ContextMenuAction* AsContextMenu() override final { return this; }

    bool ShowNodeContextMenu(NodeId* nodeId);
    bool ShowPinContextMenu(PinId* pinId);
    bool ShowLinkContextMenu(LinkId* linkId);
    bool ShowBackgroundContextMenu();
};

struct ShortcutAction final: EditorAction
{
    enum Action { None, Cut, Copy, Paste, Duplicate, CreateNode };

    bool            m_IsActive;
    bool            m_InAction;
    Action          m_CurrentAction;
    vector<Object*> m_Context;

    ShortcutAction(EditorContext* editor);

    virtual const char* GetName() const override final { return "Shortcut"; }

    virtual AcceptResult Accept(const Control& control) override final;
    virtual bool Process(const Control& control) override final;
    virtual void Reject() override final;

    virtual void ShowMetrics() override final;

    virtual ShortcutAction* AsCutCopyPaste() override final { return this; }

    bool Begin();
    void End();

    bool AcceptCut();
    bool AcceptCopy();
    bool AcceptPaste();
    bool AcceptDuplicate();
    bool AcceptCreateNode();
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

    bool      m_InActive;
    Stage     m_NextStage;

    Stage     m_CurrentStage;
    Type      m_ItemType;
    Action    m_UserAction;
    ImU32     m_LinkColor;
    float     m_LinkThickness;
    Pin*      m_LinkStart;
    Pin*      m_LinkEnd;

    bool      m_IsActive;
    Pin*      m_DraggedPin;


    CreateItemAction(EditorContext* editor);

    virtual const char* GetName() const override final { return "Create Item"; }

    virtual AcceptResult Accept(const Control& control) override final;
    virtual bool Process(const Control& control) override final;

    virtual void ShowMetrics() override final;

    virtual bool IsDragging() override final { return m_IsActive; }

    virtual CreateItemAction* AsCreateItem() override final { return this; }

    void SetStyle(ImU32 color, float thickness);

    bool Begin();
    void End();

    Result RejectItem();
    Result AcceptItem();

    Result QueryLink(PinId* startId, PinId* endId);
    Result QueryNode(PinId* pinId);

private:
    bool m_IsInGlobalSpace;

    void DragStart(Pin* startPin);
    void DragEnd();
    void DropPin(Pin* endPin);
    void DropNode();
    void DropNothing();
};

struct DeleteItemsAction final: EditorAction
{
    bool    m_IsActive;
    bool    m_InInteraction;

    DeleteItemsAction(EditorContext* editor);

    virtual const char* GetName() const override final { return "Delete Items"; }

    virtual AcceptResult Accept(const Control& control) override final;
    virtual bool Process(const Control& control) override final;

    virtual void ShowMetrics() override final;

    virtual DeleteItemsAction* AsDeleteItems() override final { return this; }

    bool Add(Object* object);

    bool Begin();
    void End();

    bool QueryLink(LinkId* linkId, PinId* startId = nullptr, PinId* endId = nullptr);
    bool QueryNode(NodeId* nodeId);

    bool AcceptItem();
    void RejectItem();

private:
    enum IteratorType { Unknown, Link, Node };
    enum UserAction { Undetermined, Accepted, Rejected };

    bool QueryItem(ObjectId* itemId, IteratorType itemType);
    void RemoveItem();

    vector<Object*> m_ManuallyDeletedObjects;

    IteratorType    m_CurrentItemType;
    UserAction      m_UserAction;
    vector<Object*> m_CandidateObjects;
    int             m_CandidateItemIndex;
};

struct NodeBuilder
{
    EditorContext* const Editor;

    Node* m_CurrentNode;
    Pin*  m_CurrentPin;

    rect   m_NodeRect;

    rect   m_PivotRect;
    ImVec2 m_PivotAlignment;
    ImVec2 m_PivotSize;
    ImVec2 m_PivotScale;
    bool   m_ResolvePinRect;
    bool   m_ResolvePivot;

    rect   m_GroupBounds;
    bool   m_IsGroup;

    NodeBuilder(EditorContext* editor);

    void Begin(NodeId nodeId);
    void End();

    void BeginPin(PinId pinId, PinKind kind);
    void EndPin();

    void PinRect(const ImVec2& a, const ImVec2& b);
    void PinPivotRect(const ImVec2& a, const ImVec2& b);
    void PinPivotSize(const ImVec2& size);
    void PinPivotScale(const ImVec2& scale);
    void PinPivotAlignment(const ImVec2& alignment);

    void Group(const ImVec2& size);

    ImDrawList* GetUserBackgroundDrawList() const;
    ImDrawList* GetUserBackgroundDrawList(Node* node) const;
};

struct HintBuilder
{
    EditorContext* const Editor;
    bool  m_IsActive;
    Node* m_CurrentNode;

    HintBuilder(EditorContext* editor);

    bool Begin(NodeId nodeId);
    void End();

    ImVec2 GetGroupMin();
    ImVec2 GetGroupMax();

    ImDrawList* GetForegroundDrawList();
    ImDrawList* GetBackgroundDrawList();
};

struct Style: ax::NodeEditor::Style
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

    vector<ColorModifier>   m_ColorStack;
    vector<VarModifier>     m_VarStack;
};

struct Config: ax::NodeEditor::Config
{
    Config(const ax::NodeEditor::Config* config);

    std::string Load();
    std::string LoadNode(NodeId nodeId);

    void BeginSave();
    bool Save(const std::string& data, SaveReasonFlags flags);
    bool SaveNode(NodeId nodeId, const std::string& data, SaveReasonFlags flags);
    void EndSave();
};

struct EditorContext
{
    EditorContext(const ax::NodeEditor::Config* config = nullptr);
    ~EditorContext();

    Style& GetStyle() { return m_Style; }

    void Begin(const char* id, const ImVec2& size = ImVec2(0, 0));
    void End();

    bool DoLink(LinkId id, PinId startPinId, PinId endPinId, ImU32 color, float thickness);


    NodeBuilder& GetNodeBuilder() { return m_NodeBuilder; }
    HintBuilder& GetHintBuilder() { return m_HintBuilder; }

    EditorAction* GetCurrentAction() { return m_CurrentAction; }

    CreateItemAction& GetItemCreator() { return m_CreateItemAction; }
    DeleteItemsAction& GetItemDeleter() { return m_DeleteItemsAction; }
    ContextMenuAction& GetContextMenu() { return m_ContextMenuAction; }
    ShortcutAction& GetShortcut() { return m_ShortcutAction; }

    const Canvas& GetCanvas() const { return m_Canvas; }

    void SetNodePosition(NodeId nodeId, const ImVec2& screenPosition);
    ImVec2 GetNodePosition(NodeId nodeId);
    ImVec2 GetNodeSize(NodeId nodeId);

    void MarkNodeToRestoreState(Node* node);
    void RestoreNodeState(Node* node);

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
    uint64_t GetSelectionId() const { return m_SelectionId; }

    Node* FindNodeAt(const ImVec2& p);
    void FindNodesInRect(const ax::rectf& r, vector<Node*>& result, bool append = false, bool includeIntersecting = true);
    void FindLinksInRect(const ax::rectf& r, vector<Link*>& result, bool append = false);

    void FindLinksForNode(NodeId nodeId, vector<Link*>& result, bool add = false);

    bool PinHadAnyLinks(PinId pinId);

    ImVec2 ToCanvas(ImVec2 point) { return m_Canvas.FromScreen(point); }
    ImVec2 ToScreen(ImVec2 point) { return m_Canvas.ToScreen(point); }

    void NotifyLinkDeleted(Link* link);

    void Suspend();
    void Resume();
    bool IsSuspended();

    bool IsActive();

    void MakeDirty(SaveReasonFlags reason);
    void MakeDirty(SaveReasonFlags reason, Node* node);

    Pin*    CreatePin(PinId id, PinKind kind);
    Node*   CreateNode(NodeId id);
    Link*   CreateLink(LinkId id);

    Node*   FindNode(NodeId id);
    Pin*    FindPin(PinId id);
    Link*   FindLink(LinkId id);
    Object* FindObject(ObjectId id);

    Node*  GetNode(NodeId id);
    Pin*   GetPin(PinId id, PinKind kind);
    Link*  GetLink(LinkId id);

    Link* FindLinkAt(const point& p);

    template <typename T>
    ax::rectf GetBounds(const std::vector<T*>& objects)
    {
        ax::rectf bounds;

        for (auto object : objects)
            if (object->m_IsLive)
                bounds = make_union(bounds, object->GetBounds());

        return bounds;
    }

    template <typename T>
    ax::rectf GetBounds(const std::vector<ObjectWrapper<T>>& objects)
    {
        ax::rectf bounds;

        for (auto object : objects)
            if (object.m_Object->m_IsLive)
                bounds = make_union(bounds, object.m_Object->GetBounds());

        return bounds;
    }

    ax::rectf GetSelectionBounds() { return GetBounds(m_SelectedObjects); }
    ax::rectf GetContentBounds() { return GetBounds(m_Nodes); }

    ImU32 GetColor(StyleColor colorIndex) const;
    ImU32 GetColor(StyleColor colorIndex, float alpha) const;

    void NavigateTo(const rectf& bounds, bool zoomIn = false, float duration = -1) { m_NavigateAction.NavigateTo(bounds, zoomIn, duration); }

    void RegisterAnimation(Animation* animation);
    void UnregisterAnimation(Animation* animation);

    void Flow(Link* link);

    void SetUserContext(bool globalSpace = false);

    void EnableShortcuts(bool enable);
    bool AreShortcutsEnabled();

    NodeId GetDoubleClickedNode()      const { return m_DoubleClickedNode;       }
    PinId  GetDoubleClickedPin()       const { return m_DoubleClickedPin;        }
    LinkId GetDoubleClickedLink()      const { return m_DoubleClickedLink;       }
    bool   IsBackgroundClicked()       const { return m_BackgroundClicked;       }
    bool   IsBackgroundDoubleClicked() const { return m_BackgroundDoubleClicked; }

    ax::point AlignPointToGrid(const ax::point& p) const
    {
        if (!ImGui::GetIO().KeyAlt)
            return p - ax::point(((p.x + 0) % 16) - 0, ((p.y + 0) % 16) - 0);
        else
            return p;
    }

private:
    void LoadSettings();
    void SaveSettings();

    Control BuildControl(bool allowOffscreen);

    void ShowMetrics(const Control& control);

    void CaptureMouse();
    void ReleaseMouse();

    void UpdateAnimations();

    bool                m_IsFirstFrame;
    bool                m_IsWindowActive;

    bool                m_ShortcutsEnabled;

    Style               m_Style;

    vector<ObjectWrapper<Node>> m_Nodes;
    vector<ObjectWrapper<Pin>>  m_Pins;
    vector<ObjectWrapper<Link>> m_Links;

    vector<Object*>     m_SelectedObjects;

    vector<Object*>     m_LastSelectedObjects;
    uint64_t            m_SelectionId;

    Link*               m_LastActiveLink;

    vector<Animation*>  m_LiveAnimations;
    vector<Animation*>  m_LastLiveAnimations;

    ImVec2              m_MousePosBackup;
    ImVec2              m_MousePosPrevBackup;
    ImVec2              m_MouseClickPosBackup[5];

    Canvas              m_Canvas;

    int                 m_SuspendCount;

    NodeBuilder         m_NodeBuilder;
    HintBuilder         m_HintBuilder;

    EditorAction*       m_CurrentAction;
    NavigateAction      m_NavigateAction;
    SizeAction          m_SizeAction;
    DragAction          m_DragAction;
    SelectAction        m_SelectAction;
    ContextMenuAction   m_ContextMenuAction;
    ShortcutAction      m_ShortcutAction;
    CreateItemAction    m_CreateItemAction;
    DeleteItemsAction   m_DeleteItemsAction;

    vector<AnimationController*> m_AnimationControllers;
    FlowAnimationController      m_FlowAnimationController;

    NodeId              m_DoubleClickedNode;
    PinId               m_DoubleClickedPin;
    LinkId              m_DoubleClickedLink;
    bool                m_BackgroundClicked;
    bool                m_BackgroundDoubleClicked;

    bool                m_IsInitialized;
    Settings            m_Settings;

    Config              m_Config;
};

} // namespace Detail
} // namespace Editor
} // namespace ax
