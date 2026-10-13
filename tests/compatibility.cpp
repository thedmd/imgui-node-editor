//------------------------------------------------------------------------------
// Compatibility test: drives one editor through dragging, linking, selection,
// deletion, shortcuts, the context menu, zoom and a settings round trip, with
// every Dear ImGui the editor supports, 1.80 and later. The test suite needs
// Dear ImGui 1.89 for its input. Fails on a failed check, an IM_ASSERT, or an
// ID conflict ImGui reports.
//------------------------------------------------------------------------------
# define IMGUI_DEFINE_MATH_OPERATORS
# include <imgui.h>
# include <imgui_internal.h>
# include <imgui_node_editor.h>
# include <cstdio>
# include <cstdlib>
# include <cstdint>
# include <cstdarg>
# include <cstring>
# include <cmath>
# include <string>
# include <vector>
# include <map>

namespace ed = ax::NodeEditor;

static std::map<std::string, int> g_Asserts;
void NodeEditorTestAssert(const char* expr, const char* file, int line)
{
    const char* base = file;
    for (const char* p = file; *p; ++p)
        if (*p == '\\' || *p == '/')
            base = p + 1;
    char buf[1024];
    snprintf(buf, sizeof(buf), "%s(%d): %s", base, line, expr);
    if (g_Asserts[buf]++ == 0)
        printf("  ASSERT %s\n", buf);
}

static int g_Fail = 0, g_Pass = 0;
static void Check(bool ok, const char* what, const char* detailFmt = "", ...)
{
    char detail[512] = {};
    va_list args;
    va_start(args, detailFmt);
    vsnprintf(detail, sizeof(detail), detailFmt, args);
    va_end(args);
    printf("  %s %s %s\n", ok ? "PASS" : "FAIL", what, detail);
    (ok ? g_Pass : g_Fail)++;
}

//------------------------------------------------------------------------------
// Input helpers (legacy IO before 1.87, event queue after)
static void MouseMove(ImVec2 p)
{
# if IMGUI_VERSION_NUM >= 18700
    ImGui::GetIO().AddMousePosEvent(p.x, p.y);
# else
    ImGui::GetIO().MousePos = p;
# endif
}
static void MouseButton(int b, bool down)
{
# if IMGUI_VERSION_NUM >= 18700
    ImGui::GetIO().AddMouseButtonEvent(b, down);
# else
    ImGui::GetIO().MouseDown[b] = down;
# endif
}
static void MouseWheel(float w)
{
# if IMGUI_VERSION_NUM >= 18700
    ImGui::GetIO().AddMouseWheelEvent(0.0f, w);
# else
    ImGui::GetIO().MouseWheel += w;
# endif
}
static void Key(ImGuiKey key, bool down)
{
# if IMGUI_VERSION_NUM >= 18700
    ImGui::GetIO().AddKeyEvent(key, down);
# else
    ImGui::GetIO().KeysDown[ImGui::GetIO().KeyMap[key]] = down;
# endif
}
static void Ctrl(bool down)
{
# if IMGUI_VERSION_NUM >= 18900
    ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, down);
# elif IMGUI_VERSION_NUM >= 18700
    ImGui::GetIO().AddKeyEvent(ImGuiKey_ModCtrl, down);
# else
    ImGui::GetIO().KeyCtrl = down;
# endif
}

# if IMGUI_VERSION_NUM >= 19200
static void ProcessTextures(ImDrawData* dd)
{
    if (!dd->Textures)
        return;
    for (ImTextureData* tex : *dd->Textures)
    {
        if (tex->Status == ImTextureStatus_WantCreate)
        {
            tex->SetTexID((ImTextureID)(intptr_t)1);
            tex->SetStatus(ImTextureStatus_OK);
        }
        else if (tex->Status == ImTextureStatus_WantUpdates)
            tex->SetStatus(ImTextureStatus_OK);
        else if (tex->Status == ImTextureStatus_WantDestroy && tex->UnusedFrames > 0)
        {
            tex->SetTexID(ImTextureID_Invalid);
            tex->SetStatus(ImTextureStatus_Destroyed);
        }
    }
}
# endif

# if IMGUI_VERSION_NUM >= 19120
template <typename T> static auto ConflictId(T& g, int)  -> decltype(g.DebugDrawIdConflictsId) { return g.DebugDrawIdConflictsId; }
template <typename T> static auto ConflictId(T& g, long) -> decltype(g.DebugDrawIdConflicts)   { return g.DebugDrawIdConflicts; }
# endif
static int g_IdConflictFrames = 0;
static std::map<unsigned, int> g_IdConflicts;
static void CheckIdConflict(int frame)
{
# if IMGUI_VERSION_NUM >= 19120
    ImGuiID id = ConflictId(*GImGui, 0);
    if (id != 0)
    {
        ++g_IdConflictFrames;
        if (g_IdConflicts[id]++ == 0)
            printf("  IDCONFLICT id=0x%08X first seen at frame %d\n", id, frame);
    }
# else
    (void)frame;
# endif
}

//------------------------------------------------------------------------------
struct App
{
    struct Node { int id, inPin, outPin; ImVec2 pos; bool alive; };
    struct LinkInfo { ed::LinkId id; ed::PinId a, b; };

    ed::EditorContext*      ctx = nullptr;
    std::vector<Node>       nodes;
    std::vector<LinkInfo>   links;
    int                     nextLinkId = 1000;
    int                     frame = 0;
    bool                    setPositions = true;

    std::map<int, ImVec2>   pinScreen;   // pin id -> screen-space center
    std::map<int, ImVec2>   titleScreen; // node id -> screen-space center of title text
    std::map<int, ImVec2>   nodePos;     // node id -> canvas position

    int  created = 0, deletedNodes = 0, deletedLinks = 0;
    bool bgMenu = false, copy = false, paste = false, cut = false, dup = false, createNode = false;
    float zoom = 1.0f;
    int  selectedCount = 0;
    long long vertices = 0;
    int  badVertices = 0, badCmds = 0;
};

static std::string g_Saved;
static int g_SaveCount = 0;
static bool SaveCb(const char* data, size_t size, ed::SaveReasonFlags, void*)
{
    g_Saved.assign(data, size);
    ++g_SaveCount;
    return true;
}
static std::string g_ToLoad;
static size_t LoadCb(char* data, void*)
{
    if (data)
        memcpy(data, g_ToLoad.data(), g_ToLoad.size());
    return g_ToLoad.size();
}

static void ValidateDrawData(App& app)
{
    ImDrawData* dd = ImGui::GetDrawData();
    if (!dd)
        return;
# if IMGUI_VERSION_NUM >= 18980
    for (int n = 0; n < dd->CmdLists.Size; ++n)
# else
    for (int n = 0; n < dd->CmdListsCount; ++n)
# endif
    {
        const ImDrawList* dl = dd->CmdLists[n];
        app.vertices += dl->VtxBuffer.Size;
        for (const ImDrawVert& v : dl->VtxBuffer)
            if (!std::isfinite(v.pos.x) || !std::isfinite(v.pos.y))
                ++app.badVertices;
        for (const ImDrawCmd& cmd : dl->CmdBuffer)
        {
            if (cmd.UserCallback)
                continue;
            if ((int)(cmd.IdxOffset + cmd.ElemCount) > dl->IdxBuffer.Size)
                ++app.badCmds;
            for (unsigned i = 0; i < cmd.ElemCount && (int)(cmd.IdxOffset + i) < dl->IdxBuffer.Size; ++i)
                if ((int)(cmd.VtxOffset + dl->IdxBuffer[cmd.IdxOffset + i]) >= dl->VtxBuffer.Size)
                {
                    ++app.badCmds;
                    break;
                }
        }
    }
}

static void Frame(App& app)
{
    ImGuiIO& io = ImGui::GetIO();
    io.DeltaTime   = 1.0f / 60.0f;
    io.DisplaySize = ImVec2(1280, 720);

    ImGui::NewFrame();
    CheckIdConflict(app.frame);
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("Host", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

    ed::SetCurrentEditor(app.ctx);
    ed::Begin("Editor", ImVec2(0, 0));

    for (auto& n : app.nodes)
    {
        if (!n.alive)
            continue;
        if (app.setPositions && app.frame == 0)
            ed::SetNodePosition(n.id, n.pos);
        ed::BeginNode(n.id);
        ImGui::Text("Node %d", n.id);
        app.titleScreen[n.id] = ed::CanvasToScreen((ImGui::GetItemRectMin() + ImGui::GetItemRectMax()) * 0.5f);
        ed::BeginPin(n.inPin, ed::PinKind::Input);
        ImGui::Text("-> In");
        ed::EndPin();
        app.pinScreen[n.inPin] = ed::CanvasToScreen((ImGui::GetItemRectMin() + ImGui::GetItemRectMax()) * 0.5f);
        ImGui::SameLine();
        ed::BeginPin(n.outPin, ed::PinKind::Output);
        ImGui::Text("Out ->");
        ed::EndPin();
        app.pinScreen[n.outPin] = ed::CanvasToScreen((ImGui::GetItemRectMin() + ImGui::GetItemRectMax()) * 0.5f);
        ed::EndNode();
        app.nodePos[n.id] = ed::GetNodePosition(n.id);
    }

    for (auto& l : app.links)
        ed::Link(l.id, l.a, l.b);

    if (ed::BeginCreate())
    {
        ed::PinId s, e;
        if (ed::QueryNewLink(&s, &e) && s && e && ed::AcceptNewItem())
        {
            app.links.push_back({ ed::LinkId(app.nextLinkId++), s, e });
            ++app.created;
            ed::Link(app.links.back().id, s, e);
        }
    }
    ed::EndCreate();

    if (ed::BeginDelete())
    {
        ed::NodeId nid;
        while (ed::QueryDeletedNode(&nid))
            if (ed::AcceptDeletedItem())
            {
                for (auto& n : app.nodes)
                    if (ed::NodeId(n.id) == nid)
                        n.alive = false;
                ++app.deletedNodes;
            }
        ed::LinkId lid;
        while (ed::QueryDeletedLink(&lid))
            if (ed::AcceptDeletedItem())
            {
                for (size_t i = 0; i < app.links.size(); ++i)
                    if (app.links[i].id == lid)
                    {
                        app.links.erase(app.links.begin() + i);
                        break;
                    }
                ++app.deletedLinks;
            }
    }
    ed::EndDelete();

    if (ed::BeginShortcut())
    {
        if (ed::AcceptCopy())         app.copy = true;
        if (ed::AcceptPaste())        app.paste = true;
        if (ed::AcceptCut())          app.cut = true;
        if (ed::AcceptDuplicate())    app.dup = true;
        if (ed::AcceptCreateNode())   app.createNode = true;
    }
    ed::EndShortcut();

    ed::Suspend();
    if (ed::ShowBackgroundContextMenu())
    {
        app.bgMenu = true;
        ImGui::OpenPopup("bg");
    }
    if (ImGui::BeginPopup("bg"))
    {
        ImGui::Text("menu");
        ImGui::EndPopup();
    }
    ed::Resume();

    app.zoom = ed::GetCurrentZoom();
    app.selectedCount = ed::GetSelectedObjectCount();

    ed::End();
    ed::SetCurrentEditor(nullptr);
    ImGui::End();
    ImGui::Render();
# if IMGUI_VERSION_NUM >= 19200
    ProcessTextures(ImGui::GetDrawData());
# endif
    ValidateDrawData(app);
    ++app.frame;
}

static void Run(App& app, int n)
{
    for (int i = 0; i < n; ++i)
        Frame(app);
}

static void Drag(App& app, ImVec2 from, ImVec2 to, int button = 0, int steps = 10)
{
    MouseMove(from);         Run(app, 2);
    MouseButton(button, true); Run(app, 2);
    for (int i = 1; i <= steps; ++i)
    {
        MouseMove(from + (to - from) * (float(i) / steps));
        Run(app, 1);
    }
    Run(app, 1);
    MouseButton(button, false); Run(app, 3);
}

static void Click(App& app, ImVec2 at, int button = 0)
{
    MouseMove(at);              Run(app, 2);
    MouseButton(button, true);  Run(app, 2);
    MouseButton(button, false); Run(app, 2);
}

static void PressKey(App& app, ImGuiKey key, bool ctrl = false)
{
    if (ctrl) { Ctrl(true); Run(app, 1); }
    Key(key, true);  Run(app, 1);
    Key(key, false); Run(app, 1);
    if (ctrl) { Ctrl(false); Run(app, 1); }
    Run(app, 1);
}

static ed::EditorContext* MakeEditor()
{
    ed::Config config;
    config.SettingsFile = nullptr;
    config.SaveSettings = SaveCb;
    config.LoadSettings = LoadCb;
    return ed::CreateEditor(&config);
}

int main()
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("ImGui %s (%d)\n", IMGUI_VERSION, IMGUI_VERSION_NUM);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigMacOSXBehaviors = false; // on Apple platforms ImGui would swap the Ctrl the test sends for Cmd
    io.DisplaySize = ImVec2(1280, 720);
# if IMGUI_VERSION_NUM >= 19200
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
# else
    unsigned char* pixels = nullptr;
    int w = 0, h = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    io.Fonts->SetTexID((ImTextureID)(intptr_t)1);
# endif
# if IMGUI_VERSION_NUM < 18700
    io.KeyMap[ImGuiKey_Delete] = 0x2E;
    io.KeyMap[ImGuiKey_Space]  = 0x20;
    io.KeyMap[ImGuiKey_C]      = 'C';
    io.KeyMap[ImGuiKey_V]      = 'V';
    io.KeyMap[ImGuiKey_X]      = 'X';
    io.KeyMap[ImGuiKey_A]      = 'A';
# endif

    App app;
    app.nodes = { { 1, 11, 12, ImVec2(100, 100), true }, { 2, 21, 22, ImVec2(500, 150), true }, { 3, 31, 32, ImVec2(300, 400), true } };
    app.ctx = MakeEditor();

    printf("[settle]\n");
    Run(app, 5);
    Check(app.badVertices == 0 && app.badCmds == 0, "draw data valid after settle", "(vtx=%lld badVtx=%d badCmd=%d)", app.vertices, app.badVertices, app.badCmds);
    Check(fabsf(app.nodePos[1].x - 100) < 0.5f && fabsf(app.nodePos[1].y - 100) < 0.5f, "SetNodePosition applied", "(%.1f,%.1f)", app.nodePos[1].x, app.nodePos[1].y);

    printf("[drag node 1 by (60,40)]\n");
    ImVec2 before = app.nodePos[1];
    Drag(app, app.titleScreen[1], app.titleScreen[1] + ImVec2(60, 40));
    ImVec2 after = app.nodePos[1];
    // Drag snaps pin pivots to the grid unless Alt is held, so allow one grid cell of deviation.
    Check(fabsf(after.x - before.x - 60) <= 16.0f && fabsf(after.y - before.y - 40) <= 16.0f, "node dragged (grid snapped)", "(%.1f,%.1f)->(%.1f,%.1f)", before.x, before.y, after.x, after.y);
    Check(g_SaveCount > 0, "settings saved after drag", "(saves=%d)", g_SaveCount);

    printf("[create link 1.out -> 2.in]\n");
    Drag(app, app.pinScreen[12], app.pinScreen[21]);
    Check(app.created == 1, "link created", "(created=%d)", app.created);
    if (!app.links.empty())
        Check(app.links[0].a == ed::PinId(12) && app.links[0].b == ed::PinId(21), "link start/end pins", "(start=%d end=%d)", (int)app.links[0].a.Get(), (int)app.links[0].b.Get());

    printf("[select node 2, delete]\n");
    Click(app, ImVec2(1100, 650));
    Check(app.selectedCount == 0, "background click clears selection", "(sel=%d)", app.selectedCount);
    Click(app, app.titleScreen[2]);
    Check(app.selectedCount == 1, "click selects node", "(sel=%d)", app.selectedCount);
    PressKey(app, ImGuiKey_Delete);
    Run(app, 3);
    Check(app.deletedNodes == 1, "Delete key deletes selected node", "(deletedNodes=%d)", app.deletedNodes);
    Check(app.deletedLinks == 1, "dependent link deleted with node", "(deletedLinks=%d links=%d)", app.deletedLinks, (int)app.links.size());

    printf("[shortcuts]\n");
    Click(app, app.titleScreen[1]);
    PressKey(app, ImGuiKey_C, true);
    Check(app.copy, "Ctrl+C -> AcceptCopy");
    PressKey(app, ImGuiKey_V, true);
    Check(app.paste, "Ctrl+V -> AcceptPaste");
    PressKey(app, ImGuiKey_X, true);
    Check(app.cut, "Ctrl+X -> AcceptCut");
    MouseMove(ImVec2(1100, 650)); Run(app, 2);
    PressKey(app, ImGuiKey_Space);
    Check(app.createNode, "Space -> AcceptCreateNode");

    printf("[context menu]\n");
    Click(app, ImVec2(1000, 600), 1);
    Check(app.bgMenu, "right click on background -> ShowBackgroundContextMenu");
    Click(app, ImVec2(1200, 700)); // dismiss popup

    printf("[zoom]\n");
    MouseMove(ImVec2(640, 360)); Run(app, 2);
    MouseWheel(1.0f); Run(app, 30);
    // GetCurrentZoom() returns the inverse scale: zooming in makes it smaller.
    Check(app.zoom < 1.0f, "mouse wheel zooms in", "(GetCurrentZoom=%.3f)", app.zoom);
    MouseWheel(-1.0f); Run(app, 30);
    Check(fabsf(app.zoom - 1.0f) < 0.01f, "mouse wheel zooms back out", "(zoom=%.3f)", app.zoom);

    printf("[settings round-trip]\n");
    ImVec2 savedPos = app.nodePos[1];
    printf("  saved settings (%d saves): %.300s\n", g_SaveCount, g_Saved.c_str());
    Run(app, 2);
    ed::DestroyEditor(app.ctx);
    g_ToLoad = g_Saved;
    app.ctx = MakeEditor();
    app.setPositions = false;
    app.frame = 0;
    Run(app, 5);
    Check(fabsf(app.nodePos[1].x - savedPos.x) < 0.5f && fabsf(app.nodePos[1].y - savedPos.y) < 0.5f, "node position restored from settings", "(%.1f,%.1f) vs (%.1f,%.1f)", app.nodePos[1].x, app.nodePos[1].y, savedPos.x, savedPos.y);

    Check(app.badVertices == 0 && app.badCmds == 0, "draw data valid overall", "(vtx=%lld badVtx=%d badCmd=%d)", app.vertices, app.badVertices, app.badCmds);

    ed::DestroyEditor(app.ctx);
    ImGui::DestroyContext();

    int assertSites = (int)g_Asserts.size(), assertHits = 0;
    for (auto& a : g_Asserts)
        assertHits += a.second;
    printf("RESULT pass=%d fail=%d assert_sites=%d assert_hits=%d id_conflict_frames=%d\n", g_Pass, g_Fail, assertSites, assertHits, g_IdConflictFrames);
    return (g_Fail || assertHits || g_IdConflictFrames) ? 1 : 0;
}
