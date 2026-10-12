# include "application.h"
# include "setup.h"
# include "platform.h"
# include "renderer.h"
# include "stb_image.h"

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

    if (!m_Platform->OpenMainWindow("Application", width, height))
        return false;

    if (!m_Renderer->Create(*m_Platform))
        return false;

    m_IniFilename = m_Name + ".ini";

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

    m_DefaultFont = io.Fonts->AddFontFromFileTTF("data/Play-Regular.ttf", 18.0f, &config);
    m_HeaderFont  = io.Fonts->AddFontFromFileTTF("data/Cuprum-Bold.ttf",  20.0f, &config);
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

    // Manually scale the draw data, because ImGui backends are not yet
    // consistent in handling FramebufferScale
    auto drawData = ImGui::GetDrawData();

    drawData->DisplaySize.x *= framebufferScale;
    drawData->DisplaySize.y *= framebufferScale;

    for (int i = 0; i < drawData->CmdListsCount; i++)
    {
        auto& cmdList = drawData->CmdLists[i];
        for (auto& vtx : cmdList->VtxBuffer)
        {
            vtx.pos.x *= framebufferScale;
            vtx.pos.y *= framebufferScale;
        }
    }

    drawData->ScaleClipRects(drawData->FramebufferScale);

    drawData->FramebufferScale.x = 1.0f;
    drawData->FramebufferScale.y = 1.0f;

    m_Renderer->RenderDrawData(drawData);

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
    int width = 0, height = 0, component = 0;
    if (auto data = stbi_load(path, &width, &height, &component, 4))
    {
        auto texture = CreateTexture(data, width, height);
        stbi_image_free(data);
        return texture;
    }
    else
        return ImTextureID{};
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
