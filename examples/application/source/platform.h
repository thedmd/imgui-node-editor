# pragma once
# include "setup.h"
# include <memory>

struct Application;
struct Renderer;

struct Platform
{
    virtual ~Platform() {};

    virtual bool ApplicationStart(int argc, char** argv) = 0;
    virtual void ApplicationStop() = 0;

    virtual bool OpenMainWindow(const char* title, int width, int height) = 0;
    virtual bool CloseMainWindow() = 0;
    virtual void* GetMainWindowHandle() const = 0;
    virtual void SetMainWindowTitle(const char* title) = 0;
    virtual void ShowMainWindow() = 0;
    virtual bool ProcessMainWindowEvents() = 0;
    virtual bool IsMainWindowVisible() const = 0;

    virtual void SetRenderer(Renderer* renderer) = 0;

    virtual void NewFrame() = 0;
    virtual void FinishFrame() = 0;

    virtual void Quit() = 0;

    bool  HasWindowScaleChanged() const { return m_WindowScaleChanged; }
    void  AcknowledgeWindowScaleChanged() { m_WindowScaleChanged = false; }
    float GetWindowScale() const { return m_WindowScale; }
    void  SetWindowScale(float windowScale)
    {
        if (windowScale == m_WindowScale)
            return;
        m_WindowScale = windowScale;
        m_WindowScaleChanged = true;
    }

    bool  HasFramebufferScaleChanged() const { return m_FramebufferScaleChanged; }
    void  AcknowledgeFramebufferScaleChanged() { m_FramebufferScaleChanged = false; }
    float GetFramebufferScale() const { return m_FramebufferScale; }
    void  SetFramebufferScale(float framebufferScale)
    {
        if (framebufferScale == m_FramebufferScale)
            return;
        m_FramebufferScale = framebufferScale;
        m_FramebufferScaleChanged = true;
    }


private:
    bool    m_WindowScaleChanged = false;
    float   m_WindowScale = 1.0f;
    bool    m_FramebufferScaleChanged = false;
    float   m_FramebufferScale = 1.0f;
};

std::unique_ptr<Platform> CreatePlatform(Application& application);
