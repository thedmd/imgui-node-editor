//------------------------------------------------------------------------------
// Editor settings kept in imgui.ini by the settings handler.
//------------------------------------------------------------------------------
# include "harness.h"
# include <cstdio>
# include <fstream>
# include <sstream>
# include <string>

namespace {

// Settings with node 1 at (x, y), and the view scrolled to (scrollX, scrollY).
std::string State(int x, int y, int scrollX = 0, int scrollY = 0)
{
    char buffer[256];
    snprintf(buffer, sizeof(buffer), R"({"nodes":{"node:1":{"location":{"x":%d,"y":%d}}},"view":{"scroll":{"x":%d,"y":%d},"zoom":1}})", x, y, scrollX, scrollY);
    return buffer;
}

std::string Section(const char* name, const std::string& state)
{
    return std::string("[NodeEditor][") + name + "]\nData=" + state + "\n";
}

ed::Config NamedConfig(const char* name)
{
    ed::Config config;
    config.SettingsName = name;
    return config;
}

void OneNode(int)
{
    ed::BeginNode(1);
    ImGui::TextUnformatted("Node");
    ed::EndNode();
}

ImVec2 NodePosition(test::Session& session)
{
    return session.Use([] { return ed::GetNodePosition(1); });
}

// Where the canvas origin is on screen, which a pan moves.
ImVec2 ViewOrigin(test::Session& session)
{
    return session.Use([] { return ed::CanvasToScreen(ImVec2(0.0f, 0.0f)); });
}

void LoadIni(test::Session& session, const std::string& ini)
{
    ImGui::SetCurrentContext(session.HostContext.Context);
    ImGui::LoadIniSettingsFromMemory(ini.c_str(), ini.size());
}

// An .ini file in the working directory, removed with the test.
struct IniFile
{
    explicit IniFile(const char* name): Path(std::string(name) + ".ini") { std::remove(Path.c_str()); }
    ~IniFile() { std::remove(Path.c_str()); }

    void Write(const std::string& text) const { std::ofstream(Path) << text; }

    std::string Read() const
    {
        std::ifstream file(Path);
        std::stringstream text;
        text << file.rdbuf();
        return text.str();
    }

    std::string Path;
};

} // namespace

TEST_CASE(KeepsOtherSections, "settings_handler.keeps_sections_of_editors_not_created", "API-9", test::TestFlags_None)
{
    const auto sectionB = Section("B", State(200, 20));
    const auto sectionC = Section("C", State(300, 30));
    IniFile ini("settings_handler_keeps_sections");
    ini.Write(Section("A", State(100, 10)) + "\n" + sectionB + "\n" + sectionC);

    test::Session session(NamedConfig("A"));
    session.HostContext.Context->IO.IniFilename   = ini.Path.c_str();
    session.HostContext.Context->IO.IniSavingRate = 0.1f;
    ed::InstallSettingsHandler(session.HostContext.Context);
    session.DefaultContent = OneNode;
    session.Run(3);

    const auto position = NodePosition(session);
    test::Check(position.x == 100.0f && position.y == 10.0f, "A starts from its section: node at (%g, %g)", position.x, position.y);

    // As a moved window does, makes ImGui save the file.
    ImGui::MarkIniSettingsDirty();
    session.Run(10);

    const auto file = ini.Read();
    test::Check(file.find(sectionB) != std::string::npos && file.find(sectionC) != std::string::npos, "sections of B and C kept unchanged:\n%s", file.c_str());
    test::Check(file.find("[NodeEditor][A]\nData={") != std::string::npos, "A saved:\n%s", file.c_str());
}

TEST_CASE(LateEditorLoadsSection, "settings_handler.editor_created_later_loads_its_section", "API-9", test::TestFlags_None)
{
    test::Session session(NamedConfig("A"));
    session.EditorSize = ImVec2(400.0f, 300.0f);
    ed::InstallSettingsHandler(session.HostContext.Context);
    LoadIni(session, Section("A", State(100, 10)) + Section("B", State(200, 20)));

    const auto configB = NamedConfig("B");
    ed::EditorContext* editorB = nullptr;
    session.DefaultContent = OneNode;
    session.AfterEditor = [&]
    {
        if (!editorB)
            return;
        ed::SetCurrentEditor(editorB);
        ed::Begin("B", ImVec2(400.0f, 300.0f));
        OneNode(0);
        ed::End();
        ed::SetCurrentEditor(nullptr);
    };

    session.Run(100);
    editorB = ed::CreateEditor(&configB);
    session.Run(3);

    ed::SetCurrentEditor(editorB);
    const auto position = ed::GetNodePosition(1);
    ed::SetCurrentEditor(nullptr);
    ed::DestroyEditor(editorB);
    editorB = nullptr;

    test::Check(position.x == 200.0f && position.y == 20.0f, "B, created after 100 frames, starts from its section: node at (%g, %g)", position.x, position.y);
}

TEST_CASE(ClearStateBeforeFirstFrame, "settings_handler.clear_state_before_the_first_frame_wins", "CORE-7", test::TestFlags_None)
{
    // The section loads in the first Begin(), after ClearState().
    test::Session session(NamedConfig("A"));
    ed::InstallSettingsHandler(session.HostContext.Context);
    LoadIni(session, Section("A", State(100, 10)));

    session.Use([] { ed::ClearState(); });
    session.DefaultContent = OneNode;
    session.Run(3);

    const auto position = NodePosition(session);
    test::Check(position.x == 0.0f && position.y == 0.0f, "node 1 starts at the origin, not from the section: (%g, %g)", position.x, position.y);
}

TEST_CASE(RecreatedEditorGetsLatestState, "settings_handler.recreated_editor_gets_latest_state", "API-9", test::TestFlags_None)
{
    const auto config = NamedConfig("A");
    test::Session session(config);
    ed::InstallSettingsHandler(session.HostContext.Context);
    LoadIni(session, Section("A", State(100, 10)));
    session.DefaultContent = OneNode;
    session.Run(3);

    // Pans with the right mouse button, over the background.
    const auto loaded = ViewOrigin(session);
    session.Drag(ImVec2(700.0f, 400.0f), ImVec2(600.0f, 350.0f), 1);
    const auto panned = ViewOrigin(session);
    test::Check(panned.x != loaded.x || panned.y != loaded.y, "view panned: origin at (%g, %g), was (%g, %g)", panned.x, panned.y, loaded.x, loaded.y);

    ed::DestroyEditor(session.Editor);
    session.Editor = ed::CreateEditor(&config);
    session.Run(3);

    const auto restored = ViewOrigin(session);
    test::Check(restored.x == panned.x && restored.y == panned.y, "editor created again has the panned view: origin at (%g, %g), panned to (%g, %g)", restored.x, restored.y, panned.x, panned.y);
}

TEST_CASE(MemorySettingsTakePrecedence, "settings_handler.settings_from_memory_take_precedence", "API-9", test::TestFlags_None)
{
    test::Session session(NamedConfig("A"));
    ed::InstallSettingsHandler(session.HostContext.Context);
    LoadIni(session, Section("A", State(100, 10)));
    session.Use([] { ed::LoadSettingsFromMemory(State(700, 70).c_str()); });
    session.DefaultContent = OneNode;
    session.Run(3);

    const auto position = NodePosition(session);
    const std::string ini = ImGui::SaveIniSettingsToMemory();
    test::Check(position.x == 700.0f && position.y == 70.0f, "settings loaded before the first Begin() win over the section: node at (%g, %g)", position.x, position.y);
    test::Check(ini.find(R"("node:1":{"location":{"x":700,"y":70}})") != std::string::npos, "and the editor still saves to its section:\n%s", ini.c_str());
}

TEST_CASE(InstallAfterFirstFrame, "settings_handler.install_after_first_frame_without_ini_file", "API-9", test::TestFlags_None)
{
    test::Session session(NamedConfig("A"));
    session.HostContext.NewFrame();
    ImGui::EndFrame();

    // Without io.IniFilename, the first NewFrame() read nothing.
    ed::InstallSettingsHandler(session.HostContext.Context);
    LoadIni(session, Section("A", State(100, 10)));
    session.DefaultContent = OneNode;
    session.Run(3);

    const auto position = NodePosition(session);
    test::Check(position.x == 100.0f && position.y == 10.0f, "A starts from its section: node at (%g, %g)", position.x, position.y);
}

TEST_CASE(Misuse, "settings_handler.misuse_asserts", "API-9", test::TestFlags_None)
{
    test::ExpectAsserts(3);
    {
        test::Host host;
        ImGui::SetCurrentContext(host.Context);
        ImGui::LoadIniSettingsFromMemory("[Window][Debug##Default]\nPos=60,60\n");
        ed::InstallSettingsHandler();
        test::Check(test::AssertCount() == 1, "installing after ImGui read .ini data asserts");
    }
    {
        test::Host host;
        ed::InstallSettingsHandler(host.Context);
        ed::InstallSettingsHandler(host.Context);
        test::Check(test::AssertCount() == 2, "installing twice asserts");
    }
    {
        test::Session session(NamedConfig("A"));
        session.EditorSize = ImVec2(400.0f, 300.0f);
        ed::InstallSettingsHandler(session.HostContext.Context);

        const auto config = NamedConfig("A");
        auto second = ed::CreateEditor(&config);
        session.AfterEditor = [&]
        {
            ed::SetCurrentEditor(second);
            ed::Begin("Second", ImVec2(400.0f, 300.0f));
            ed::End();
            ed::SetCurrentEditor(nullptr);
        };
        session.Run(2);
        session.AfterEditor = nullptr;
        ed::DestroyEditor(second);
        test::Check(test::AssertCount() == 3, "two live editors with the same name assert");
    }
}

TEST_CASE(ManualIniRoundTrip, "settings_handler.manual_ini_round_trip", "API-9", test::TestFlags_None)
{
    std::string data;
    ImVec2 view;
    {
        test::Session session(NamedConfig("A"));
        auto& io = session.HostContext.Context->IO;
        io.IniSavingRate = 1.0f;
        ed::InstallSettingsHandler(session.HostContext.Context);
        session.DefaultContent = OneNode;
        session.Run(90);
        io.WantSaveIniSettings = false;

        session.Frame([](int frame) { ed::SetNodePosition(1, ImVec2(500.0f, 300.0f)); OneNode(frame); });
        session.Run(50);
        const bool early = io.WantSaveIniSettings;
        session.Run(20);
        test::Check(!early && io.WantSaveIniSettings, "a change raises io.WantSaveIniSettings after io.IniSavingRate: %d after 50 frames, %d after 70", early, io.WantSaveIniSettings);

        data = ImGui::SaveIniSettingsToMemory();
        view = ViewOrigin(session);
    }

    test::Session session(NamedConfig("A"));
    ed::InstallSettingsHandler(session.HostContext.Context);
    LoadIni(session, data);
    session.DefaultContent = OneNode;
    session.Run(3);

    const auto position = NodePosition(session);
    const auto origin   = ViewOrigin(session);
    test::Check(position.x == 500.0f && position.y == 300.0f, "a fresh context starts from the saved data: node at (%g, %g)", position.x, position.y);
    test::Check(origin.x == view.x && origin.y == view.y, "and with the saved view: origin at (%g, %g), saved (%g, %g)", origin.x, origin.y, view.x, view.y);
}

TEST_CASE(MidSessionLoad, "settings_handler.loading_ini_data_updates_live_editor", "API-9", test::TestFlags_None)
{
    test::Session session(NamedConfig("A"));
    ed::InstallSettingsHandler(session.HostContext.Context);
    session.DefaultContent = OneNode;
    session.Run(5);
    const auto before = ViewOrigin(session);

    LoadIni(session, Section("A", State(640, 320, -100, -50)));
    session.Run(2);

    const auto position = NodePosition(session);
    const auto origin   = ViewOrigin(session);
    test::Check(position.x == 640.0f && position.y == 320.0f, "node moved to the loaded position: (%g, %g)", position.x, position.y);
    test::Check(origin.x == before.x + 100.0f && origin.y == before.y + 50.0f, "view scrolled as loaded: origin at (%g, %g), was (%g, %g)", origin.x, origin.y, before.x, before.y);
}

TEST_CASE(ContextDestroyedFirst, "settings_handler.context_destroyed_before_editor", "API-9", test::TestFlags_None)
{
    IniFile ini("settings_handler_context_destroyed_first");
    test::Session session(NamedConfig("A"));
    session.HostContext.Context->IO.IniFilename = ini.Path.c_str();
    ed::InstallSettingsHandler(session.HostContext.Context);
    session.DefaultContent = OneNode;
    session.Run(3);
    session.Frame([](int frame) { ed::SetNodePosition(1, ImVec2(500.0f, 300.0f)); OneNode(frame); });

    ImGui::DestroyContext(session.HostContext.Context);
    session.HostContext.Context = nullptr;

    const auto file = ini.Read();
    test::Check(file.find(R"("node:1":{"location":{"x":500,"y":300}})") != std::string::npos, "DestroyContext() wrote the latest state:\n%s", file.c_str());

    // The session destroys its editor after the context.
}

TEST_CASE(DestroyUnchangedEditor, "settings_handler.destroying_an_unchanged_editor_saves_nothing", "API-13", test::TestFlags_None)
{
    // DestroyEditor() saved every time, which marked imgui.ini for a write.
    const auto config = NamedConfig("A");
    test::Session session(config);
    session.HostContext.Context->IO.IniSavingRate = 0.1f;
    ed::InstallSettingsHandler(session.HostContext.Context);
    session.DefaultContent = OneNode;
    session.Run(20);

    // Saved as an application does once io.WantSaveIniSettings is set.
    ImGui::SaveIniSettingsToMemory();
    ed::DestroyEditor(session.Editor);
    const bool unchanged = session.HostContext.Context->SettingsDirtyTimer <= 0.0f;

    // A position set after the last frame is saved by the destructor.
    session.Editor = ed::CreateEditor(&config);
    session.Run(20);
    ImGui::SaveIniSettingsToMemory();
    session.Use([] { ed::SetNodePosition(1, ImVec2(500.0f, 300.0f)); });
    ed::DestroyEditor(session.Editor);
    const bool changed = session.HostContext.Context->SettingsDirtyTimer > 0.0f;
    const std::string ini = ImGui::SaveIniSettingsToMemory();
    session.Editor = ed::CreateEditor(&config);

    test::Check(unchanged, "destroying an unchanged editor leaves ImGui's settings clean");
    test::Check(changed && ini.find(R"("node:1":{"location":{"x":500,"y":300}})") != std::string::npos, "destroying a changed one saves the change:\n%s", ini.c_str());
}

TEST_CASE(IdleFramesSaveNothing, "settings_handler.idle_frames_save_nothing", "API-9", test::TestFlags_None)
{
    test::Session session(NamedConfig("A"));
    auto& io = session.HostContext.Context->IO;
    io.IniSavingRate = 0.1f;
    ed::InstallSettingsHandler(session.HostContext.Context);
    session.DefaultContent = OneNode;
    session.Run(20);
    io.WantSaveIniSettings = false;

    int dirtyFrames = 0;
    for (int i = 0; i < 60; ++i)
    {
        session.Frame();
        if (session.HostContext.Context->SettingsDirtyTimer > 0.0f || io.WantSaveIniSettings)
            ++dirtyFrames;
    }
    test::Check(dirtyFrames == 0, "idle frames leave ImGui's settings clean: %d of 60 frames dirty", dirtyFrames);
}
