//------------------------------------------------------------------------------
// Headless test harness: a Dear ImGui context without a renderer, scripted
// input, draw data validation and geometry capture.
//------------------------------------------------------------------------------
# include "harness.h"
# include <cfloat>
# include <cmath>
# include <cstdarg>
# include <cstdio>

namespace test {


//------------------------------------------------------------------------------
static std::vector<TestCase>& MutableRegistry()
{
    static std::vector<TestCase> registry;
    return registry;
}

bool Register(const TestCase& testCase)
{
    MutableRegistry().push_back(testCase);
    return true;
}

const std::vector<TestCase>& Registry()
{
    return MutableRegistry();
}


//------------------------------------------------------------------------------
static int  s_AssertCount     = 0;
static int  s_ExpectedAsserts = 0;
static int  s_CheckFailures   = 0;
static bool s_Skipped         = false;
static int  s_ThrowOnAssert   = 0;

static void Report(bool condition, const char* format, va_list args)
{
    char message[1024];
    vsnprintf(message, sizeof(message), format, args);
    printf("    %s %s\n", condition ? "PASS" : "FAIL", message);
    if (!condition)
        ++s_CheckFailures;
}

void Check(bool condition, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    Report(condition, format, args);
    va_end(args);
}

void Fail(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    Report(false, format, args);
    va_end(args);
}

void Skip(const char* reason)
{
    printf("    SKIP %s\n", reason);
    s_Skipped = true;
}

bool IsSkipped()             { return s_Skipped; }
void ExpectAsserts(int count) { s_ExpectedAsserts = count; }
int  ExpectedAsserts()       { return s_ExpectedAsserts; }
int  AssertCount()           { return s_AssertCount; }
int  CheckFailures()         { return s_CheckFailures; }

void ResetResults()
{
    s_AssertCount     = 0;
    s_ExpectedAsserts = 0;
    s_CheckFailures   = 0;
    s_Skipped         = false;
}

ThrowOnAssert::ThrowOnAssert()  { ++s_ThrowOnAssert; }
ThrowOnAssert::~ThrowOnAssert() { --s_ThrowOnAssert; }

} // namespace test

void NodeEditorTestAssert(const char* expression, const char* file, int line)
{
    const char* name = file;
    for (const char* p = file; *p; ++p)
        if (*p == '\\' || *p == '/')
            name = p + 1;

    if (++test::s_AssertCount <= 8)
        printf("    ASSERT %s(%d): %s\n", name, line, expression);

    if (test::s_ThrowOnAssert > 0)
        throw test::AssertFailure();
}

namespace test {


//------------------------------------------------------------------------------
static bool IsResetRenderState(ImDrawCallback callback)
{
# if IMGUI_VERSION_NUM >= 19280
    return callback == ImGui::GetPlatformIO().DrawCallback_ResetRenderState;
# else
    return callback == ImDrawCallback_ResetRenderState;
# endif
}


//------------------------------------------------------------------------------
# if IMGUI_VERSION_NUM >= 19110
static const char* GetClipboardText(ImGuiContext*)
{
    return static_cast<Host*>(ImGui::GetPlatformIO().Platform_ClipboardUserData)->Clipboard.c_str();
}

static void SetClipboardText(ImGuiContext*, const char* text)
{
    static_cast<Host*>(ImGui::GetPlatformIO().Platform_ClipboardUserData)->Clipboard = text;
}
# else
static const char* GetClipboardText(void* userData)
{
    return static_cast<Host*>(userData)->Clipboard.c_str();
}

static void SetClipboardText(void* userData, const char* text)
{
    static_cast<Host*>(userData)->Clipboard = text;
}
# endif

Host::Host(ImVec2 displaySize)
    : DisplaySize(displaySize)
{
    Context = ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.DisplaySize = DisplaySize;
    io.DeltaTime   = 1.0f / 60.0f;
    // On Apple platforms ImGui swaps Ctrl and Cmd, and turns Ctrl+click into a right click.
    // The tests send the same input everywhere; a test of the macOS behavior turns it on.
    io.ConfigMacOSXBehaviors = false;
# if IMGUI_VERSION_NUM >= 19110
    auto& platformIO = ImGui::GetPlatformIO();
    platformIO.Platform_GetClipboardTextFn = GetClipboardText;
    platformIO.Platform_SetClipboardTextFn = SetClipboardText;
    platformIO.Platform_ClipboardUserData  = this;
# else
    io.GetClipboardTextFn = GetClipboardText;
    io.SetClipboardTextFn = SetClipboardText;
    io.ClipboardUserData  = this;
# endif
# if defined(IMGUI_HAS_TEXTURES)
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
# else
    unsigned char* pixels = nullptr;
    int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    io.Fonts->SetTexID((ImTextureID)(intptr_t)1);
# endif
}

Host::~Host()
{
    if (Context)
        ImGui::DestroyContext(Context);
}

void Host::NewFrame()
{
    ImGui::SetCurrentContext(Context);
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = DisplaySize;
    io.DeltaTime   = 1.0f / 60.0f;
    ImGui::NewFrame();
}

void Host::Render()
{
    ImGui::Render();
    ImDrawData* drawData = ImGui::GetDrawData();

# if defined(IMGUI_HAS_TEXTURES)
    if (drawData->Textures)
    {
        for (ImTextureData* texture : *drawData->Textures)
        {
            if (texture->Status == ImTextureStatus_WantCreate)
            {
                texture->SetTexID((ImTextureID)(texture->UniqueID + 1));
                texture->SetStatus(ImTextureStatus_OK);
            }
            else if (texture->Status == ImTextureStatus_WantUpdates)
            {
                texture->SetStatus(ImTextureStatus_OK);
            }
            else if (texture->Status == ImTextureStatus_WantDestroy && texture->UnusedFrames > 0)
            {
                texture->SetTexID(ImTextureID_Invalid);
                texture->SetStatus(ImTextureStatus_Destroyed);
            }
        }
    }
# endif

# if IMGUI_VERSION_NUM >= 18980
    const int listCount = drawData->CmdLists.Size;
# else
    const int listCount = drawData->CmdListsCount;
# endif
    for (int n = 0; n < listCount; ++n)
    {
        const ImDrawList* drawList = drawData->CmdLists[n];
        Vertices += drawList->VtxBuffer.Size;
        for (const ImDrawVert& vertex : drawList->VtxBuffer)
        {
            if (!std::isfinite(vertex.pos.x) || !std::isfinite(vertex.pos.y))
                ++BadVertices;
        }
        for (const ImDrawCmd& command : drawList->CmdBuffer)
        {
            if (command.UserCallback)
            {
                if (!IsResetRenderState(command.UserCallback))
                {
                    if (StrayCallbacks++ == 0)
                        Fail("draw list '%s' has a callback (%p) the renderer would call", drawList->_OwnerName ? drawList->_OwnerName : "?", (void*)command.UserCallback);
                }
                continue;
            }

            if ((int)(command.IdxOffset + command.ElemCount) > drawList->IdxBuffer.Size)
            {
                if (BadCommands++ == 0)
                    Fail("draw command indexes past the index buffer");
                continue;
            }

            for (unsigned int i = 0; i < command.ElemCount; ++i)
            {
                if ((int)(command.VtxOffset + drawList->IdxBuffer[command.IdxOffset + i]) >= drawList->VtxBuffer.Size)
                {
                    if (BadCommands++ == 0)
                        Fail("draw command indexes past the vertex buffer");
                    break;
                }
            }
        }
    }
    if (BadVertices > 0)
    {
        Fail("%d vertices with non-finite positions", BadVertices);
        BadVertices = 0;
    }
}


//------------------------------------------------------------------------------
void MouseMove(ImVec2 position)         { ImGui::GetIO().AddMousePosEvent(position.x, position.y); }
void MouseButton(int button, bool down) { ImGui::GetIO().AddMouseButtonEvent(button, down); }
void MouseWheel(float delta)            { ImGui::GetIO().AddMouseWheelEvent(0.0f, delta); }
void Key(ImGuiKey key, bool down)       { ImGui::GetIO().AddKeyEvent(key, down); }
void Ctrl(bool down)                    { ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, down); }


//------------------------------------------------------------------------------
Geometry Capture(const ImDrawList* drawList)
{
    Geometry geometry;
    for (const ImDrawCmd& command : drawList->CmdBuffer)
    {
        if (command.UserCallback != nullptr)
        {
            if (!IsResetRenderState(command.UserCallback))
                ++geometry.Callbacks;
            continue;
        }

        for (unsigned int i = 0; i < command.ElemCount; ++i)
        {
            const ImDrawIdx   index  = drawList->IdxBuffer[command.IdxOffset + i];
            const ImDrawVert& vertex = drawList->VtxBuffer[command.VtxOffset + index];
            geometry.Positions.push_back(vertex.pos);
            geometry.Colors.push_back(vertex.col);
            geometry.ClipRects.push_back(command.ClipRect);
        }
    }
    return geometry;
}

static bool Near(float a, float b)
{
    return fabsf(a - b) <= 0.01f;
}

GeometryDiff Compare(const Geometry& a, const Geometry& b)
{
    GeometryDiff diff;
    diff.SizeA = a.Positions.size();
    diff.SizeB = b.Positions.size();
    for (size_t i = 0, n = ImMin(diff.SizeA, diff.SizeB); i < n; ++i)
    {
        if (!Near(a.Positions[i].x, b.Positions[i].x) || !Near(a.Positions[i].y, b.Positions[i].y))
            ++diff.Positions;
        if (a.Colors[i] != b.Colors[i])
            ++diff.Colors;
        const auto& ra = a.ClipRects[i];
        const auto& rb = b.ClipRects[i];
        if (!Near(ra.x, rb.x) || !Near(ra.y, rb.y) || !Near(ra.z, rb.z) || !Near(ra.w, rb.w))
            ++diff.ClipRects;
    }
    return diff;
}

std::string Describe(const GeometryDiff& diff)
{
    char text[256];
    if (diff.Identical())
        snprintf(text, sizeof(text), "identical geometry (%d indices)", (int)diff.SizeA);
    else
        snprintf(text, sizeof(text), "indices %d vs %d, positions %d, colors %d and clip rects %d differ",
            (int)diff.SizeA, (int)diff.SizeB, (int)diff.Positions, (int)diff.Colors, (int)diff.ClipRects);
    return text;
}

long FirstIndexOf(const Geometry& geometry, ImU32 color)
{
    for (size_t i = 0; i < geometry.Colors.size(); ++i)
        if (geometry.Colors[i] == color)
            return (long)i;
    return -1;
}


//------------------------------------------------------------------------------
ed::Config MakeConfig(Storage* storage)
{
    ed::Config config;
    config.SmoothZoomPower = 1.3f; // one power on every platform, whose defaults differ
    config.SettingsName = nullptr;
    config.UserPointer  = storage;
    return config;
}


//------------------------------------------------------------------------------
Session::Session(Storage* storage, ImVec2 displaySize)
    : Session(MakeConfig(storage), displaySize)
{
    Settings = storage;
}

Session::Session(const ed::Config& config, ImVec2 displaySize)
    : HostContext(displaySize)
{
    Editor = ed::CreateEditor(&config);
}

Session::~Session()
{
    ImGui::SetCurrentContext(HostContext.Context);
    ed::DestroyEditor(Editor);
}

void Session::Frame(const Content& content)
{
    HostContext.NewFrame();

    if (BeforeHost)
        BeforeHost();

    if (PlaceHost)
    {
        ImGui::SetNextWindowPos(HostPos);
        ImGui::SetNextWindowSize(HostSize);
    }
    if (!(HostFlags & ImGuiWindowFlags_NoTitleBar))
        ImGui::SetNextWindowCollapsed(HostCollapsed);
    if (BareHost)
    {
        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    }
    // Like an application that ignores ImGui::Begin()'s result, which ImGui allows.
    ImGui::Begin("Host", nullptr, HostFlags);
    if (BareHost)
        ImGui::PopStyleVar(3);
    DrawList = ImGui::GetWindowDrawList();

    if (BeforeEditor)
        BeforeEditor();

    ed::SetCurrentEditor(Editor);
    if (Settings && FrameIndex == 0 && !Settings->Global.empty())
        ed::LoadSettingsFromMemory(Settings->Global.c_str(), Settings->Global.size());
    ed::Begin("Editor", EditorSize);
    if (content)
        content(FrameIndex);
    ed::End();
    if (Settings && ed::GetSettingsChangeReason() != ed::SaveReasonFlags::None)
    {
        Settings->Global = ed::SaveSettingsToMemory();
        ++Settings->GlobalSaves;
        ed::ClearWantSaveSettings();
    }
    ed::SetCurrentEditor(nullptr);

    if (AfterEditor)
        AfterEditor();

    ImGui::End();

    if (AfterHost)
        AfterHost();

    HostContext.Render();
    ++FrameIndex;
}

void Session::Frame()
{
    Frame(DefaultContent);
}

void Session::Run(int frames)
{
    for (int i = 0; i < frames; ++i)
        Frame();
}

void Session::Click(ImVec2 position, int button)
{
    MouseMove(position);
    Run(2);
    MouseButton(button, true);
    Run(2);
    MouseButton(button, false);
    Run(2);
}

void Session::Drag(ImVec2 from, ImVec2 to, int button, int steps)
{
    MouseMove(from);
    Run(2);
    MouseButton(button, true);
    Run(2);
    for (int i = 1; i <= steps; ++i)
    {
        MouseMove(from + (to - from) * (float(i) / steps));
        Run(1);
    }
    Run(1);
    MouseButton(button, false);
    Run(3);
}

void Session::PressKey(ImGuiKey key, bool ctrl)
{
    if (ctrl)
    {
        Ctrl(true);
        Run(1);
    }
    Key(key, true);
    Run(1);
    Key(key, false);
    Run(1);
    if (ctrl)
    {
        Ctrl(false);
        Run(1);
    }
    Run(1);
}

} // namespace test
