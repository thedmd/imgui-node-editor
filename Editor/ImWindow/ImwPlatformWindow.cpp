
#include "ImwPlatformWindow.h"

#include "ImwWindowManager.h"

namespace ImWindow
{
//SFF_BEGIN
    ImwPlatformWindow::ImwPlatformWindow(bool bMain, bool bIsDragWindow, bool bCreateState)
    {
        m_bMain = bMain;
        m_bIsDragWindow = bIsDragWindow;
        m_pContainer = new ImwContainer(this);
        m_pState = NULL;
        m_pPreviousState = NULL;

        if (bCreateState)
        {
            m_pState = ImGui::CreateContext();
            ImGui::SetCurrentContext(m_pState);
            auto& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.Fonts = ImwWindowManager::GetInstance()->GetFontAtlas();

            m_pState->Style = ImwWindowManager::GetInstance()->GetStyle();
//
//             auto& style = ImGui::GetStyle();
//             style.FrameRounding = 2.f;
//             style.WindowRounding = 0.f;
//             style.ScrollbarRounding = 0.f;
        }
    }

    ImwPlatformWindow::~ImwPlatformWindow()
    {
        ImwSafeDelete(m_pContainer);

        SetState();
        ImGui::GetIO().Fonts = NULL;
        ImGui::Shutdown();
        RestoreState();
        ImGui::DestroyContext(m_pState);
    }

    bool ImwPlatformWindow::Init(ImwPlatformWindow* /*pParent*/)
    {
        return true;
    }

    const ImVec2 c_oVec2_0 = ImVec2(0,0);
    const ImVec2& ImwPlatformWindow::GetPosition() const
    {
        return c_oVec2_0;
    }

    const ImVec2& ImwPlatformWindow::GetSize() const
    {
        return ImGui::GetIO().DisplaySize;
    }

    void ImwPlatformWindow::Show()
    {
    }

    void ImwPlatformWindow::Hide()
    {
    }

    void ImwPlatformWindow::SetSize(int /*iWidth*/, int /*iHeight*/)
    {
    }

    void ImwPlatformWindow::SetPosition(int /*iX*/, int /*iY*/)
    {
    }

    void ImwPlatformWindow::SetTitle(const char* /*pTtile*/)
    {
    }

    void ImwPlatformWindow::OnClose()
    {
        ImwWindowManager::GetInstance()->OnClosePlatformWindow(this);
    }

    static bool s_bStatePush = false;

    bool ImwPlatformWindow::IsStateSet()
    {
        return s_bStatePush;
    }

    void ImwPlatformWindow::SetState()
    {
        IM_ASSERT(s_bStatePush == false);
        s_bStatePush = true;
        if (m_pState != NULL)
        {
            m_pPreviousState = ImGui::GetCurrentContext();
            ImGui::SetCurrentContext(m_pState);
        }
    }

    void ImwPlatformWindow::RestoreState()
    {
        IM_ASSERT(s_bStatePush == true);
        s_bStatePush = false;
        if (m_pState != NULL)
            ImGui::SetCurrentContext(m_pPreviousState);
    }

    void ImwPlatformWindow::OnLoseFocus()
    {
        if (NULL != m_pState)
            m_pState->SetNextWindowPosCond = m_pState->SetNextWindowSizeCond = m_pState->SetNextWindowContentSizeCond = m_pState->SetNextWindowCollapsedCond = m_pState->SetNextWindowFocus = 0;
    }

    void ImwPlatformWindow::PreUpdate()
    {
    }

    void ImwPlatformWindow::Destroy()
    {
    }

    void ImwPlatformWindow::StartDrag()
    {
    }

    void ImwPlatformWindow::StopDrag()
    {
    }

    bool ImwPlatformWindow::IsDraging()
    {
        return false;
    }

    void ImwPlatformWindow::Paint()
    {
        ImwWindowManager::GetInstance()->Paint(this);
    }

    bool ImwPlatformWindow::IsMain()
    {
        return m_bMain;
    }

    void ImwPlatformWindow::Dock(ImwWindow* pWindow)
    {
        m_pContainer->Dock(pWindow);
    }

    bool ImwPlatformWindow::UnDock(ImwWindow* pWindow)
    {
        return m_pContainer->UnDock(pWindow);
    }

    ImwContainer* ImwPlatformWindow::GetContainer()
    {
        return m_pContainer;
    }

    ImwContainer* ImwPlatformWindow::HasWindow(ImwWindow* pWindow)
    {
        return m_pContainer->HasWindow(pWindow);
    }

    void ImwPlatformWindow::PaintContainer()
    {
        m_pContainer->Paint();
    }
//SFF_END
}