//------------------------------------------------------------------------------
// Headless test harness: a Dear ImGui context without a renderer, scripted
// input, draw data validation and geometry capture.
//------------------------------------------------------------------------------
# pragma once
# ifndef IMGUI_DEFINE_MATH_OPERATORS
#     define IMGUI_DEFINE_MATH_OPERATORS
# endif
# include <imgui.h>
# include <imgui_internal.h>
# include <imgui_node_editor.h>
# include <cstdint>
# include <functional>
# include <map>
# include <string>
# include <vector>

namespace ed = ax::NodeEditor;

namespace test {


//------------------------------------------------------------------------------
// Registration. CMake reads the TEST_CASE lines to register one CTest test per
// case, so every TEST_CASE must stay on a single line with literal arguments.
enum TestFlags
{
    TestFlags_None           = 0,
    TestFlags_KnownFailure   = 1 << 0, // Covers an open finding. CTest expects the test to fail.
    TestFlags_NeedsSanitizer = 1 << 1, // Detects memory errors only when built with AddressSanitizer.
};

struct TestCase
{
    const char* Name;
    const char* Finding;
    int         Flags;
    void      (*Function)();
};

bool                         Register(const TestCase& testCase);
const std::vector<TestCase>& Registry();

# define TEST_CASE(function, name, finding, flags)                                                   \
    static void function();                                                                        \
    static const bool function##Registered = test::Register({ name, finding, flags, &function });  \
    static void function()


//------------------------------------------------------------------------------
// Results of the running test.
void Check(bool condition, const char* format, ...);
void Fail(const char* format, ...);
void Skip(const char* reason);
bool IsSkipped();
void ExpectAsserts(int count);
int  ExpectedAsserts();
int  AssertCount();
int  CheckFailures();
void ResetResults();

// While a ThrowOnAssert exists, a failed IM_ASSERT throws AssertFailure. For an
// assert that guards code which would crash past it.
struct AssertFailure {};

struct ThrowOnAssert
{
    ThrowOnAssert();
    ~ThrowOnAssert();

    ThrowOnAssert(const ThrowOnAssert&) = delete;
    ThrowOnAssert& operator=(const ThrowOnAssert&) = delete;
};


//------------------------------------------------------------------------------
// Headless Dear ImGui context. Plays the role of a platform and renderer backend:
// accepts every texture request, keeps the clipboard in memory and validates the
// draw data of every frame.
struct Host
{
    explicit Host(ImVec2 displaySize = ImVec2(1280.0f, 720.0f));
    ~Host();

    Host(const Host&) = delete;
    Host& operator=(const Host&) = delete;

    void NewFrame();
    void Render();

    ImGuiContext* Context      = nullptr;
    ImVec2        DisplaySize;
    std::string   Clipboard;
    long long     Vertices       = 0;
    int           BadVertices    = 0;
    int           BadCommands    = 0;
    int           StrayCallbacks = 0;
};


//------------------------------------------------------------------------------
// Input, queued for the next frame.
void MouseMove(ImVec2 position);
void MouseButton(int button, bool down);
void MouseWheel(float delta);
void Key(ImGuiKey key, bool down);
void Ctrl(bool down);


//------------------------------------------------------------------------------
// Rendered output flattened to one entry per index, so comparisons don't depend
// on how commands are split.
struct Geometry
{
    std::vector<ImVec2> Positions;
    std::vector<ImU32>  Colors;
    std::vector<ImVec4> ClipRects;
    int                 Callbacks = 0; // left in the draw list for the renderer to call
};

struct GeometryDiff
{
    size_t SizeA = 0, SizeB = 0;
    size_t Positions = 0, Colors = 0, ClipRects = 0;

    bool Identical() const { return SizeA == SizeB && Positions == 0 && Colors == 0 && ClipRects == 0; }
};

Geometry     Capture(const ImDrawList* drawList);
GeometryDiff Compare(const Geometry& a, const Geometry& b);
std::string  Describe(const GeometryDiff& diff);
long         FirstIndexOf(const Geometry& geometry, ImU32 color);


//------------------------------------------------------------------------------
// Application side storage behind the editor's settings callbacks.
struct Storage
{
    std::string                      Global;
    int                              GlobalSaves = 0;
    std::map<uintptr_t, std::string> Nodes;
    std::vector<std::string>         NodeSaveLog;
};

ed::Config MakeConfig(Storage* storage);


//------------------------------------------------------------------------------
// One editor in a host window. Content runs between ed::Begin() and ed::End().
struct Session
{
    using Content = std::function<void(int frame)>;

    explicit Session(Storage* storage, ImVec2 displaySize = ImVec2(1280.0f, 720.0f));
    Session(const ed::Config& config, ImVec2 displaySize = ImVec2(1280.0f, 720.0f));
    ~Session();

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    void Frame(const Content& content);
    void Frame();
    void Run(int frames);

    void Click(ImVec2 position, int button = 0);
    void Drag(ImVec2 from, ImVec2 to, int button = 0, int steps = 10);
    void PressKey(ImGuiKey key, bool ctrl = false);

    // Runs 'function' with this editor set as the current one.
    template <typename F>
    auto Use(F&& function) -> decltype(function())
    {
        struct Scope
        {
            Scope(ed::EditorContext* editor) { ed::SetCurrentEditor(editor); }
            ~Scope() { ed::SetCurrentEditor(nullptr); }
        } scope(Editor);
        return function();
    }

    Host                  HostContext;
    ed::EditorContext*    Editor       = nullptr;
    ImDrawList*           DrawList     = nullptr; // host window draw list of the last frame
    int                   FrameIndex   = 0;
    Content               DefaultContent;

    bool                  PlaceHost     = true; // false for a docked host, which SetNextWindowPos() would undock
    ImVec2                HostPos       = ImVec2(60.0f, 40.0f);
    ImVec2                HostSize      = ImVec2(1000.0f, 640.0f);
    ImGuiWindowFlags      HostFlags     = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
    bool                  HostCollapsed = false;
    bool                  BareHost      = false; // transparent host without padding, rounding or border, as in issue #282
    ImVec2                EditorSize    = ImVec2(0.0f, 0.0f);
    std::function<void()> BeforeHost;   // runs before the host window
    std::function<void()> BeforeEditor; // host window content emitted before ed::Begin()
    std::function<void()> AfterEditor;  // host window content emitted after ed::End()
    std::function<void()> AfterHost;    // runs after the host window
};

} // namespace test
