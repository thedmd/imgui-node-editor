# include "application.h"
# include "setup.h"
# include "platform.h"
# include "renderer.h"
# include "stb_image.h"
# include <imgui_internal.h>
# if PLATFORM(WINDOWS)
#     define NOMINMAX
#     define WIN32_LEAN_AND_MEAN
#     include <windows.h>
# elif PLATFORM(MACOS)
#     include <mach-o/dyld.h>
#     include <climits>
#     include <cstdlib>
# else
#     include <unistd.h>
# endif

// Where the data and the settings of an example are: next to its executable, or in
// the Resources of its macOS bundle. Empty when the executable can't be found.
static std::string GetBaseDirectory()
{
    std::string executable;
# if PLATFORM(WINDOWS)
    wchar_t path[MAX_PATH];
    const auto length = static_cast<int>(GetModuleFileNameW(nullptr, path, MAX_PATH));
    if (length == 0 || length == MAX_PATH)
        return std::string();
    executable.resize(WideCharToMultiByte(CP_UTF8, 0, path, length, nullptr, 0, nullptr, nullptr));
    WideCharToMultiByte(CP_UTF8, 0, path, length, &executable[0], static_cast<int>(executable.size()), nullptr, nullptr);
# elif PLATFORM(MACOS)
    char path[PATH_MAX], resolved[PATH_MAX];
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) != 0 || !realpath(path, resolved))
        return std::string();
    executable = resolved;
# else
    char path[4096];
    const auto length = readlink("/proc/self/exe", path, sizeof(path));
    if (length <= 0 || length == static_cast<ssize_t>(sizeof(path)))
        return std::string();
    executable.assign(path, static_cast<size_t>(length));
# endif

    auto directory = executable.substr(0, executable.find_last_of("/\\") + 1);
# if PLATFORM(MACOS)
    const std::string bundle = "/Contents/MacOS/";
    if (directory.size() >= bundle.size() && directory.compare(directory.size() - bundle.size(), bundle.size(), bundle) == 0)
        directory.replace(directory.size() - bundle.size(), bundle.size(), "/Contents/Resources/");
# endif
    return directory;
}

// Relative paths are relative to the base directory, not to the working directory.
static std::string ResolvePath(const std::string& path)
{
    static const auto base = GetBaseDirectory();
    const auto isAbsolute = !path.empty() && (path[0] == '/' || path[0] == '\\' || (path.size() > 1 && path[1] == ':'));
    return isAbsolute ? path : base + path;
}

Application::Application(const char* name)
    : Application(name, 0, nullptr)
{
}

Application::Application(const char* name, int argc, char** argv)
    : m_Name(name)
    , m_Platform(CreatePlatform(*this))
    , m_Renderer(CreateRenderer())
{
    m_Platform->ApplicationStart(argc, argv);
}

Application::~Application()
{
    m_Renderer->Destroy();

    m_Platform->DestroyMainWindow();

    m_Platform->ApplicationStop();

    if (m_Context)
    {
        ImGui::DestroyContext(m_Context);
        m_Context= nullptr;
    }
}

bool Application::Create(int width /*= -1*/, int height /*= -1*/)
{
    m_Context = ImGui::CreateContext();
    ImGui::SetCurrentContext(m_Context);

    if (!m_Platform->OpenMainWindow(m_Name.c_str(), width, height))
        return false;

    if (!m_Renderer->Create(*m_Platform))
        return false;

    m_IniFilename = ResolvePath(m_Name + ".ini");

    ImGuiIO& io = ImGui::GetIO();
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // Enable Keyboard Controls
    io.IniFilename = m_IniFilename.c_str();
    io.LogFilename = nullptr;

    ImGui::StyleColorsDark();

    LoadFonts();

    m_Platform->AcknowledgeWindowScaleChanged();
    m_Platform->AcknowledgeFramebufferScaleChanged();

    OnStart();

    Frame();

    return true;
}

int Application::Run()
{
    m_Platform->ShowMainWindow();

    while (m_Platform->ProcessMainWindowEvents())
    {
        if (!m_Platform->IsMainWindowVisible())
            continue;

        Frame();
    }

    OnStop();

    return 0;
}

void Application::LoadFonts()
{
    ImGuiIO& io = ImGui::GetIO();

    ImFontConfig config;
    config.PixelSnapH = true;

    m_DefaultFont = io.Fonts->AddFontFromFileTTF(ResolvePath("data/Play-Regular.ttf").c_str(), 18.0f, &config);
    m_HeaderFont  = io.Fonts->AddFontFromFileTTF(ResolvePath("data/Cuprum-Bold.ttf").c_str(),  20.0f, &config);
}

void Application::Frame()
{
    auto& io = ImGui::GetIO();

    if (m_Platform->HasWindowScaleChanged())
        m_Platform->AcknowledgeWindowScaleChanged();

    if (m_Platform->HasFramebufferScaleChanged())
        m_Platform->AcknowledgeFramebufferScaleChanged();

    const float windowScale      = m_Platform->GetWindowScale();
    const float framebufferScale = m_Platform->GetFramebufferScale();

    m_Platform->NewFrame();

    io.DisplaySize.x *= windowScale;
    io.DisplaySize.y *= windowScale;

    io.DisplayFramebufferScale.x = framebufferScale;
    io.DisplayFramebufferScale.y = framebufferScale;

    m_Renderer->NewFrame();

    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    const auto windowBorderSize = ImGui::GetStyle().WindowBorderSize;
    const auto windowRounding   = ImGui::GetStyle().WindowRounding;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::Begin("Content", nullptr, GetWindowFlags());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, windowBorderSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, windowRounding);

    OnFrame(io.DeltaTime);

    ImGui::PopStyleVar(2);
    ImGui::End();
    ImGui::PopStyleVar(2);

    // Rendering
    m_Renderer->Clear(ImColor(32, 32, 32, 255));
    ImGui::Render();

    m_Renderer->RenderDrawData(ImGui::GetDrawData());

    m_Platform->FinishFrame();
}

void Application::SetTitle(const char* title)
{
    m_Platform->SetMainWindowTitle(title);
}

bool Application::Close()
{
    return m_Platform->CloseMainWindow();
}

void Application::Quit()
{
    m_Platform->Quit();
}

const std::string& Application::GetName() const
{
    return m_Name;
}

ImFont* Application::DefaultFont() const
{
    return m_DefaultFont;
}

ImFont* Application::HeaderFont() const
{
    return m_HeaderFont;
}

ImTextureID Application::LoadTexture(const char* path)
{
    // ImFileOpen() takes UTF-8 paths on Windows too, unlike stbi_load().
    auto file = ImFileOpen(ResolvePath(path).c_str(), "rb");
    if (!file)
        return ImTextureID{};

    int width = 0, height = 0, component = 0;
    auto data = stbi_load_from_file(file, &width, &height, &component, 4);
    ImFileClose(file);
    if (!data)
        return ImTextureID{};

    auto texture = CreateTexture(data, width, height);
    stbi_image_free(data);
    return texture;
}

ImTextureID Application::CreateTexture(const void* data, int width, int height)
{
    return m_Renderer->CreateTexture(data, width, height);
}

void Application::DestroyTexture(ImTextureID texture)
{
    m_Renderer->DestroyTexture(texture);
}

int Application::GetTextureWidth(ImTextureID texture)
{
    return m_Renderer->GetTextureWidth(texture);
}

int Application::GetTextureHeight(ImTextureID texture)
{
    return m_Renderer->GetTextureHeight(texture);
}

ImGuiWindowFlags Application::GetWindowFlags() const
{
    return
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus;
}
