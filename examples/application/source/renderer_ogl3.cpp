# include "renderer.h"

# if RENDERER(IMGUI_OGL3)

# include "platform.h"
# include <algorithm>
# include <cstdint> // std::intptr_t

# if PLATFORM(WINDOWS)
#     define NOMINMAX
#     define WIN32_LEAN_AND_MEAN
#     include <windows.h>
# endif

# include "imgui_impl_opengl3.h"

# if defined(IMGUI_IMPL_OPENGL_LOADER_GL3W)
#     include <GL/gl3w.h>            // Initialize with gl3wInit()
# elif defined(IMGUI_IMPL_OPENGL_LOADER_GLEW)
#     include <GL/glew.h>            // Initialize with glewInit()
# elif defined(IMGUI_IMPL_OPENGL_LOADER_GLAD)
#     include <glad/glad.h>          // Initialize with gladLoadGL()
# elif defined(IMGUI_IMPL_OPENGL_LOADER_GLAD2)
#     include <glad/gl.h>            // Initialize with gladLoadGL(...) or gladLoaderLoadGL()
# elif defined(IMGUI_IMPL_OPENGL_LOADER_GLBINDING2)
#     define GLFW_INCLUDE_NONE       // GLFW including OpenGL headers causes ambiguity or multiple definition errors.
#     include <glbinding/Binding.h>  // Initialize with glbinding::Binding::initialize()
#     include <glbinding/gl/gl.h>
using namespace gl;
# elif defined(IMGUI_IMPL_OPENGL_LOADER_GLBINDING3)
# define GLFW_INCLUDE_NONE       // GLFW including OpenGL headers causes ambiguity or multiple definition errors.
#     include <glbinding/glbinding.h>// Initialize with glbinding::initialize()
#     include <glbinding/gl/gl.h>
using namespace gl;
# elif defined(IMGUI_IMPL_OPENGL_LOADER_CUSTOM)
#     include IMGUI_IMPL_OPENGL_LOADER_CUSTOM
# else
#     include "imgui_impl_opengl3_loader.h"
# endif

struct ImTexture
{
    GLuint TextureID = 0;
    int    Width     = 0;
    int    Height    = 0;
};

struct RendererOpenGL3 final
    : Renderer
{
    bool Create(Platform& platform) override;
    void Destroy() override;
    void NewFrame() override;
    void RenderDrawData(ImDrawData* drawData) override;
    void Clear(const ImVec4& color) override;
    void Present() override;
    void Resize(int width, int height) override;

    ImVector<ImTexture>::iterator FindTexture(ImTextureID texture);
    ImTextureID CreateTexture(const void* data, int width, int height) override;
    void        DestroyTexture(ImTextureID texture) override;
    int         GetTextureWidth(ImTextureID texture) override;
    int         GetTextureHeight(ImTextureID texture) override;

    Platform*               m_Platform = nullptr;
    ImVector<ImTexture>     m_Textures;
};

std::unique_ptr<Renderer> CreateRenderer()
{
    return std::make_unique<RendererOpenGL3>();
}

bool RendererOpenGL3::Create(Platform& platform)
{
    m_Platform = &platform;

    // Technically we should initialize OpenGL context here,
    // but for now we relay on one created by GLFW3

#if defined(IMGUI_IMPL_OPENGL_LOADER_GL3W)
    bool err = gl3wInit() != 0;
#elif defined(IMGUI_IMPL_OPENGL_LOADER_GLEW)
    bool err = glewInit() != GLEW_OK;
#elif defined(IMGUI_IMPL_OPENGL_LOADER_GLAD)
    bool err = gladLoadGL() == 0;
#elif defined(IMGUI_IMPL_OPENGL_LOADER_GLAD2)
    bool err = gladLoadGL(glfwGetProcAddress) == 0; // glad2 recommend using the windowing library loader instead of the (optionally) bundled one.
#elif defined(IMGUI_IMPL_OPENGL_LOADER_GLBINDING2)
    bool err = false;
    glbinding::Binding::initialize();
#elif defined(IMGUI_IMPL_OPENGL_LOADER_GLBINDING3)
    bool err = false;
    glbinding::initialize([](const char* name) { return (glbinding::ProcAddress)glfwGetProcAddress(name); });
#else
    bool err = false; // If you use IMGUI_IMPL_OPENGL_LOADER_CUSTOM, your loader is likely to requires some form of initialization.
#endif
    if (err)
        return false;

# if PLATFORM(MACOS)
    const char* glslVersion = "#version 150";
# else
    const char* glslVersion = "#version 130";
# endif

    if (!ImGui_ImplOpenGL3_Init(glslVersion))
        return false;

    m_Platform->SetRenderer(this);

    return true;
}

void RendererOpenGL3::Destroy()
{
    if (!m_Platform)
        return;

    m_Platform->SetRenderer(nullptr);

    ImGui_ImplOpenGL3_Shutdown();
}

void RendererOpenGL3::NewFrame()
{
    ImGui_ImplOpenGL3_NewFrame();
}

void RendererOpenGL3::RenderDrawData(ImDrawData* drawData)
{
    ImGui_ImplOpenGL3_RenderDrawData(drawData);
}

void RendererOpenGL3::Clear(const ImVec4& color)
{
    glClearColor(color.x, color.y, color.z, color.w);
    glClear(GL_COLOR_BUFFER_BIT);
}

void RendererOpenGL3::Present()
{
}

void RendererOpenGL3::Resize(int width, int height)
{
    glViewport(0, 0, width, height);
}

ImTextureID RendererOpenGL3::CreateTexture(const void* data, int width, int height)
{
    m_Textures.resize(m_Textures.size() + 1);
    ImTexture& texture = m_Textures.back();

    // Upload texture to graphics system
    GLint last_texture = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture);
    glGenTextures(1, &texture.TextureID);
    glBindTexture(GL_TEXTURE_2D, texture.TextureID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glBindTexture(GL_TEXTURE_2D, last_texture);

    texture.Width  = width;
    texture.Height = height;

    return reinterpret_cast<ImTextureID>(static_cast<std::intptr_t>(texture.TextureID));
}

ImVector<ImTexture>::iterator RendererOpenGL3::FindTexture(ImTextureID texture)
{
    auto textureID = static_cast<GLuint>(reinterpret_cast<std::intptr_t>(texture));

    return std::find_if(m_Textures.begin(), m_Textures.end(), [textureID](ImTexture& texture)
    {
        return texture.TextureID == textureID;
    });
}

void RendererOpenGL3::DestroyTexture(ImTextureID texture)
{
    auto textureIt = FindTexture(texture);
    if (textureIt == m_Textures.end())
        return;

    glDeleteTextures(1, &textureIt->TextureID);

    m_Textures.erase(textureIt);
}

int RendererOpenGL3::GetTextureWidth(ImTextureID texture)
{
    auto textureIt = FindTexture(texture);
    if (textureIt != m_Textures.end())
        return textureIt->Width;
    return 0;
}

int RendererOpenGL3::GetTextureHeight(ImTextureID texture)
{
    auto textureIt = FindTexture(texture);
    if (textureIt != m_Textures.end())
        return textureIt->Height;
    return 0;
}

# endif // RENDERER(IMGUI_OGL3)