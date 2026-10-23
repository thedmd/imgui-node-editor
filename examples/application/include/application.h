# pragma once
# include <imgui.h>
# include <string>
# include <memory>

struct Platform;
struct Renderer;

struct Application
{
    Application(const char* name);
    Application(const char* name, int argc, char** argv);
    ~Application();

    bool Create(int width = -1, int height = -1);

    int Run();

    void SetTitle(const char* title);

    bool Close();
    void Quit();

    const std::string& GetName() const;

    // Where a file of the example is: a relative path is from where its data and settings are.
    std::string ResolvePath(const std::string& path) const;

    ImFont* DefaultFont() const;
    ImFont* HeaderFont() const;

    ImTextureID LoadTexture(const char* path);
    ImTextureID CreateTexture(const void* data, int width, int height);
    void        DestroyTexture(ImTextureID texture);
    int         GetTextureWidth(ImTextureID texture);
    int         GetTextureHeight(ImTextureID texture);

    virtual void OnStart() {}
    virtual void OnStop() {}
    virtual void OnFrame(float deltaTime) { IM_UNUSED(deltaTime); }

    virtual ImGuiWindowFlags GetWindowFlags() const;

    virtual bool CanClose() { return true; }

private:
    void LoadFonts();

    void Frame();

    std::string                 m_Name;
    std::string                 m_IniFilename;
    std::unique_ptr<Platform>   m_Platform;
    std::unique_ptr<Renderer>   m_Renderer;
    ImGuiContext*               m_Context = nullptr;
    ImFont*                     m_DefaultFont = nullptr;
    ImFont*                     m_HeaderFont = nullptr;
    void                      (*m_SetImeData)(ImGuiContext*, ImGuiViewport*, ImGuiPlatformImeData*) = nullptr;
};

int Main(int argc, char** argv);