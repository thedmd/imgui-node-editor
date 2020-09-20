# include "platform.h"
# include "setup.h"

# if BACKEND(IMGUI_GLFW)

# include "application.h"
# include "renderer.h"

# include <GLFW/glfw3.h>

# if PLATFORM(WINDOWS)
#     define GLFW_EXPOSE_NATIVE_WIN32
#     include <GLFW/glfw3native.h>
# endif

# include <imgui.h>
# include "imgui_impl_glfw.h"

struct PlatformGLFW final
    : Platform
{
    static PlatformGLFW* s_Instance;

    PlatformGLFW(Application& application);

    bool ApplicationStart(int argc, char** argv) override;
    void ApplicationStop() override;
    bool OpenMainWindow(const char* title, int width, int height) override;
    bool CloseMainWindow() override;
    void* GetMainWindowHandle() const override;
    void SetMainWindowTitle(const char* title) override;
    void ShowMainWindow() override;
    bool ProcessMainWindowEvents() override;
    bool IsMainWindowVisible() const override;
    void SetRenderer(Renderer* renderer) override;
    void NewFrame() override;
    void FinishFrame() override;
    void Quit() override;

    void UpdatePixelDensity();

    Application&    m_Application;
    GLFWwindow*     m_Window = nullptr;
    bool            m_QuitRequested = false;
    bool            m_IsMinimized = false;
    bool            m_WasMinimized = false;
    Renderer*       m_Renderer = nullptr;
};

std::unique_ptr<Platform> CreatePlatform(Application& application)
{
    return std::make_unique<PlatformGLFW>(application);
}

PlatformGLFW::PlatformGLFW(Application& application)
    : m_Application(application)
{
}

bool PlatformGLFW::ApplicationStart(int argc, char** argv)
{
    if (!glfwInit())
        return false;

    return true;
}

void PlatformGLFW::ApplicationStop()
{
    glfwTerminate();
}

bool PlatformGLFW::OpenMainWindow(const char* title, int width, int height)
{
    if (m_Window)
        return false;

    glfwWindowHint(GLFW_VISIBLE, 0);

    using InitializerType = bool (*)(GLFWwindow* window, bool install_callbacks);

    InitializerType initializer = nullptr;

# if RENDERER(IMGUI_OGL3)
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#    if PLATFORM(MACOS)
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#        ifdef GLFW_COCOA_RETINA_FRAMEBUFFER
    glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GL_TRUE);
#        endif
#        ifdef GLFW_COCOA_GRAPHICS_SWITCHING
    glfwWindowHint(GLFW_COCOA_GRAPHICS_SWITCHING, GL_TRUE);
#        endif
#    endif
    initializer = &ImGui_ImplGlfw_InitForOpenGL;
# else
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    initializer = &ImGui_ImplGlfw_InitForNone;
# endif
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GL_TRUE);

    width  = width  < 0 ? 1440 : width;
    height = height < 0 ?  800 : height;

    m_Window = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!m_Window)
        return false;

    if (!initializer || !initializer(m_Window, true))
    {
        glfwDestroyWindow(m_Window);
        m_Window = nullptr;
        return false;
    }

    glfwSetWindowUserPointer(m_Window, this);

    glfwSetWindowCloseCallback(m_Window, [](GLFWwindow* window)
    {
        auto self = reinterpret_cast<PlatformGLFW*>(glfwGetWindowUserPointer(window));
        if (!self->m_QuitRequested)
            self->CloseMainWindow();
    });

    glfwSetWindowIconifyCallback(m_Window, [](GLFWwindow* window, int iconified)
    {
        auto self = reinterpret_cast<PlatformGLFW*>(glfwGetWindowUserPointer(window));
        if (iconified)
        {
            self->m_IsMinimized = true;
            self->m_WasMinimized = true;
        }
        else
        {
            self->m_IsMinimized = false;
        }
    });

    auto onFramebuferSizeChanged = [](GLFWwindow* window, int width, int height)
    {
        auto self = reinterpret_cast<PlatformGLFW*>(glfwGetWindowUserPointer(window));
        if (self->m_Renderer)
        {
            self->m_Renderer->Resize(width, height);
            self->UpdatePixelDensity();
        }
    };

    glfwSetFramebufferSizeCallback(m_Window, onFramebuferSizeChanged);

    auto onWindowContentScaleChanged = [](GLFWwindow* window, float xscale, float yscale)
    {
        auto self = reinterpret_cast<PlatformGLFW*>(glfwGetWindowUserPointer(window));
        self->UpdatePixelDensity();
    };

    glfwSetWindowContentScaleCallback(m_Window, onWindowContentScaleChanged);

    UpdatePixelDensity();

    glfwMakeContextCurrent(m_Window);

    glfwSwapInterval(1); // Enable vsync

    return true;
}

bool PlatformGLFW::CloseMainWindow()
{
    if (m_Window == nullptr)
        return true;

    auto canClose = m_Application.CanClose();

    glfwSetWindowShouldClose(m_Window, canClose ? 1 : 0);

    return canClose;
}

void* PlatformGLFW::GetMainWindowHandle() const
{
# if PLATFORM(WINDOWS)
    return m_Window ? glfwGetWin32Window(m_Window) : nullptr;
# else
    return nullptr;
# endif
}

void PlatformGLFW::SetMainWindowTitle(const char* title)
{
    glfwSetWindowTitle(m_Window, title);
}

void PlatformGLFW::ShowMainWindow()
{
    if (m_Window == nullptr)
        return;

    glfwShowWindow(m_Window);
}

bool PlatformGLFW::ProcessMainWindowEvents()
{
    if (m_Window == nullptr)
        return false;

    if (m_IsMinimized)
        glfwWaitEvents();
    else
        glfwPollEvents();

    if (m_QuitRequested || glfwWindowShouldClose(m_Window))
    {
        ImGui_ImplGlfw_Shutdown();

        glfwDestroyWindow(m_Window);

        return false;
    }

    return true;
}

bool PlatformGLFW::IsMainWindowVisible() const
{
    if (m_Window == nullptr)
        return false;

    if (m_IsMinimized)
        return false;

    return true;
}

void PlatformGLFW::SetRenderer(Renderer* renderer)
{
    m_Renderer = renderer;
}

void PlatformGLFW::NewFrame()
{
    ImGui_ImplGlfw_NewFrame();

    if (m_WasMinimized)
    {
        ImGui::GetIO().DeltaTime = 0.1e-6f;
        m_WasMinimized = false;
    }
}

void PlatformGLFW::FinishFrame()
{
    if (m_Renderer)
        m_Renderer->Present();

    glfwSwapBuffers(m_Window);
}

void PlatformGLFW::Quit()
{
    m_QuitRequested = true;

    glfwPostEmptyEvent();
}

void PlatformGLFW::UpdatePixelDensity()
{
    float xscale, yscale;
    glfwGetWindowContentScale(m_Window, &xscale, &yscale);
    float scale = xscale > yscale ? xscale : yscale;

# if PLATFORM(WINDOWS)
    float windowScale      = scale;
    float framebufferScale = scale;
# else
    float windowScale      = 1.0f;
    float framebufferScale = scale;
# endif

    SetWindowScale(windowScale); // this is how windows is scaled, not window content

    SetFramebufferScale(framebufferScale);
}

# endif // BACKEND(IMGUI_GLFW)