#include "ImwWindowManager.h"

#include <algorithm>
#include <string>

namespace ImWindow
{
//SFF_BEGIN
ImwWindowManager::DrawWindowAreaAction::DrawWindowAreaAction(ImwPlatformWindow* pWindow, const ImVec2& oRectPos, const ImVec2& oRectSize, const ImColor& oColor)
    : m_oColor(oColor)
{
    m_pWindow = pWindow;
    m_oRectPos = oRectPos;
    m_oRectSize = oRectSize;
}

ImwWindowManager* ImwWindowManager::s_pInstance = 0;

//////////////////////////////////////////////////////////////////////////

ImwWindowManager::Config::Config()
    : m_fDragMarginRatio(0.1f)
    , m_fDragMarginSizeRatio(0.25f)
    , m_oHightlightAreaColor(0.f, 0.5f, 1.f, 0.5f)
    , m_bTabUseImGuiColors(true)
{
}

//////////////////////////////////////////////////////////////////////////

ImwWindowManager::ImwWindowManager()
{
    s_pInstance = this;
    m_pMainPlatformWindow = NULL;
    m_pDragPlatformWindow = NULL;
    m_pCurrentPlatformWindow = NULL;
    m_pDraggedWindow = NULL;
    m_pDragBestContainer = NULL;
    m_bDragOnTab = false;
    m_iDragBestContainerPosition = -1;
    m_oDragPreviewOffset = ImVec2(-20, -10);
    m_MainContext = nullptr;
}

ImwWindowManager::~ImwWindowManager()
{
    Destroy();
    s_pInstance = 0;
}

bool ImwWindowManager::Init()
{
    m_MainContext = ImGui::CreateContext();
    ImGui::SetCurrentContext(m_MainContext);

    ImGuiIO& io = ImGui::GetIO();
    ImGuiStyle& style = ImGui::GetStyle();

    io.Fonts = &m_FontAtlas;
    //io.IniFilename = NULL;


#if defined(_WINDOWS)
    if (auto windir = getenv("WINDIR"))
    {
        std::string fontPath(windir);
        fontPath += "\\Fonts\\segoeui.ttf";

        io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 22);
    }
#endif
        //io.Fonts->AddFontFromFileTTF( "res/DroidSans.ttf", 16 ) || io.Fonts->AddFontDefault();
        //io.Fonts->AddFontFromFileTTF( "res/DroidSans-Bold.ttf", 16 ) || io.Fonts->AddFontDefault();

    style.FrameRounding = 2.f;
    style.WindowRounding = 0.f;
    style.ScrollbarRounding = 0.f;

    m_pMainPlatformWindow = CreatePlatformWindow(true, NULL, false);
    if (NULL != m_pMainPlatformWindow)
    {
        m_pMainPlatformWindow->Show();

        if (CanCreateMultipleWindow())
        {
            m_pDragPlatformWindow = CreatePlatformWindow(false, m_pMainPlatformWindow, true);
        }

        return true;
    }
    return false;
}

bool ImwWindowManager::Run()
{
    PreUpdate();
    InternalRun();
    Update();
    return m_pMainPlatformWindow != NULL;
}

void ImwWindowManager::Destroy()
{
    ImwSafeDelete(m_pMainPlatformWindow);
    ImwSafeDelete(m_pDragPlatformWindow);
    for (ImwList<ImwPlatformWindow*>::iterator it = m_lPlatformWindows.begin(); it != m_lPlatformWindows.end(); ++it)
    {
        ImwSafeDelete(*it);
    }

    if (m_MainContext)
    {
        ImGui::SetCurrentContext(m_MainContext);
        ImGui::Shutdown();
        ImGui::DestroyContext(m_MainContext);
        m_MainContext = nullptr;
    }
}

ImwPlatformWindow* ImwWindowManager::GetMainPlatformWindow()
{
    return m_pMainPlatformWindow;
}

ImwWindowManager::Config& ImwWindowManager::GetConfig()
{
    return m_oConfig;
}

void ImwWindowManager::SetMainTitle(const char* pTitle)
{
    ImwIsSafe(m_pMainPlatformWindow)->SetTitle(pTitle);
}

void ImwWindowManager::UnDock(ImwWindow* pWindow)
{
    DockAction* pAction = new DockAction();
    pAction->m_bFloat = false;
    pAction->m_pWindow = pWindow;
    pAction->m_pWith = NULL;
    pAction->m_eOrientation = E_DOCK_ORIENTATION_CENTER;
    pAction->m_pToPlatformWindow = NULL;
    pAction->m_pToContainer = NULL;
    pAction->m_iPosition = -1;
    m_lDockActions.push_back(pAction);
}

void ImwWindowManager::Dock(ImwWindow* pWindow, EDockOrientation eOrientation, float fRatio, ImwPlatformWindow* pToPlatformWindow)
{
    DockAction* pAction = new DockAction();
    pAction->m_bFloat = false;
    pAction->m_pWindow = pWindow;
    pAction->m_pWith = NULL;
    pAction->m_eOrientation = eOrientation;
    pAction->m_pToPlatformWindow = (pToPlatformWindow != NULL) ? pToPlatformWindow : m_pMainPlatformWindow;
    pAction->m_pToContainer = NULL;
    pAction->m_iPosition = -1;
    pAction->m_fRatio = fRatio;
    m_lDockActions.push_back(pAction);
}

void ImwWindowManager::DockTo(ImwWindow* pWindow, EDockOrientation eOrientation, float fRatio, ImwContainer* pContainer, int iPosition)
{
    IM_ASSERT(NULL != pContainer);
    if (NULL != pContainer)
    {
        DockAction* pAction = new DockAction();
        pAction->m_bFloat = false;
        pAction->m_pWindow = pWindow;
        pAction->m_pWith = NULL;
        pAction->m_eOrientation = eOrientation;
        pAction->m_pToPlatformWindow = NULL;
        pAction->m_pToContainer = pContainer;
        pAction->m_iPosition = iPosition;
        pAction->m_fRatio = fRatio;
        m_lDockActions.push_back(pAction);
    }
}

void ImwWindowManager::DockWith(ImwWindow* pWindow, ImwWindow* pWithWindow, EDockOrientation eOrientation, float fRatio)
{
    DockAction* pAction = new DockAction();
    pAction->m_bFloat = false;
    pAction->m_pWindow = pWindow;
    pAction->m_pWith = pWithWindow;
    pAction->m_eOrientation = eOrientation;
    pAction->m_fRatio = fRatio;
    m_lDockActions.push_back(pAction);
}

void ImwWindowManager::Float(ImwWindow* pWindow, const ImVec2& oPosition, const ImVec2& oSize)
{
    DockAction* pAction = new DockAction();
    pAction->m_bFloat = true;
    pAction->m_pWindow = pWindow;
    pAction->m_oPosition = oPosition;
    pAction->m_oSize = oSize;
    m_lDockActions.push_back(pAction);
}

const ImwWindowList& ImwWindowManager::GetWindowList() const
{
    return m_lWindows;
}

ImwPlatformWindow* ImwWindowManager::GetCurrentPlatformWindow()
{
    return m_pCurrentPlatformWindow;
}

ImwPlatformWindow* ImwWindowManager::GetWindowParent(ImwWindow* pWindow)
{
    ImwContainer* pContainer = m_pMainPlatformWindow->HasWindow(pWindow);
    if (NULL != pContainer)
    {
        return m_pMainPlatformWindow;
    }

    for (ImwList<ImwPlatformWindow*>::iterator it = m_lPlatformWindows.begin(); it != m_lPlatformWindows.end(); ++it)
    {
        pContainer = (*it)->HasWindow(pWindow);
        if (NULL != pContainer)
        {
            return *it;
        }
    }
    IM_ASSERT(false);
    return NULL;
}

bool ImwWindowManager::CanCreateMultipleWindow()
{
    return false;
}

ImwPlatformWindow* ImwWindowManager::CreatePlatformWindow(bool bMain, ImwPlatformWindow* /*pParent*/, bool bDragWindow)
{
    if (bMain)
    {
        return (ImWindow::ImwPlatformWindow*)new ImwPlatformWindow(bMain, bDragWindow, CanCreateMultipleWindow());
    }
    return NULL;
}

void ImwWindowManager::InternalRun()
{

}

ImVec2 ImwWindowManager::GetCursorPos()
{
    return ImGui::GetIO().MousePos;
}

bool ImwWindowManager::IsLeftClickDown()
{
    return ImGui::GetIO().MouseDown[0];
}

void ImwWindowManager::PreUpdate()
{
    ImwIsSafe(m_pMainPlatformWindow)->PreUpdate();

    for (ImwList<ImwPlatformWindow*>::iterator it = m_lPlatformWindows.begin(); it != m_lPlatformWindows.end(); ++it)
    {
        (*it)->PreUpdate();
    }
}

void ImwWindowManager::Update()
{
    UpdatePlatformwWindowActions();
    UpdateDockActions();
    UpdateOrphans();

    while (m_lToDestroyWindows.begin() != m_lToDestroyWindows.end())
    {
        ImwWindow* pWindow = *m_lToDestroyWindows.begin();

        m_lToDestroyWindows.remove(pWindow);
        m_lOrphanWindows.remove(pWindow);
        m_lWindows.remove(pWindow);

        InternalUnDock(pWindow);

        delete pWindow;
    }

    while (m_lToDestroyStatusBars.begin() != m_lToDestroyStatusBars.end())
    {
        ImwStatusBar* pStatusBar = *m_lToDestroyStatusBars.begin();

        m_lToDestroyStatusBars.remove(pStatusBar);
        delete pStatusBar;
    }

    while (m_lToDestroyMenus.begin() != m_lToDestroyMenus.end())
    {
        ImwMenu* pMenu = *m_lToDestroyMenus.begin();

        m_lToDestroyMenus.remove(pMenu);
        delete pMenu;
    }

    while (m_lToDestroyToolBars.begin() != m_lToDestroyToolBars.end())
    {
        ImwToolBar* pToolBar = *m_lToDestroyToolBars.begin();

        m_lToDestroyToolBars.remove(pToolBar);
        delete pToolBar;
    }

    while (m_lToDestroyPlatformWindows.begin() != m_lToDestroyPlatformWindows.end())
    {
        ImwPlatformWindow* pPlatformWindow = *m_lToDestroyPlatformWindows.begin();
        m_lToDestroyPlatformWindows.remove(pPlatformWindow);
        m_lPlatformWindows.remove(pPlatformWindow);
        delete pPlatformWindow;
    }

    UpdateDragWindow();

    m_pCurrentPlatformWindow = m_pMainPlatformWindow;
    ImwIsSafe(m_pMainPlatformWindow)->Paint();

    for (ImwList<ImwPlatformWindow*>::iterator it = m_lPlatformWindows.begin(); it != m_lPlatformWindows.end(); ++it)
    {
        m_pCurrentPlatformWindow = (*it);
        (*it)->Paint();
    }

    m_pCurrentPlatformWindow = NULL;


}

void ImwWindowManager::UpdatePlatformwWindowActions()
{
    while (m_lPlatformWindowActions.begin() != m_lPlatformWindowActions.end())
    {
        PlatformWindowAction* pAction = *m_lPlatformWindowActions.begin();

        IM_ASSERT((pAction->m_iFlags & E_PLATFORM_WINDOW_ACTION_SHOW & E_PLATFORM_WINDOW_ACTION_HIDE) == 0); // Can't show and hide

        if (pAction->m_iFlags & E_PLATFORM_WINDOW_ACTION_DESTROY)
        {
            //pAction->m_pPlatformWindow->Show();
            //todo destroy
            bool bFound = false;
            if (m_pMainPlatformWindow == pAction->m_pPlatformWindow)
            {
                while (m_lPlatformWindows.begin() != m_lPlatformWindows.end())
                {
                    delete *m_lPlatformWindows.begin();
                    m_lPlatformWindows.erase(m_lPlatformWindows.begin());
                }
                delete m_pMainPlatformWindow;
                m_pMainPlatformWindow = NULL;
                bFound = true;
            }
            else
            {
                for (ImwList<ImwPlatformWindow*>::iterator it = m_lPlatformWindows.begin(); it != m_lPlatformWindows.end(); ++it)
                {
                    if (*it == pAction->m_pPlatformWindow)
                    {
                        delete *it;
                        m_lPlatformWindows.erase(it);
                        bFound = true;
                        break;
                    }
                }
            }

            if (!bFound)
            {
                IM_ASSERT(false); // ImwPlatformWindow not found, maybe already closed
            }
        }
        if (pAction->m_iFlags & E_PLATFORM_WINDOW_ACTION_SHOW)
        {
            pAction->m_pPlatformWindow->Show();
        }
        if (pAction->m_iFlags & E_PLATFORM_WINDOW_ACTION_HIDE)
        {
            pAction->m_pPlatformWindow->Hide();
        }
        if (pAction->m_iFlags & E_PLATFORM_WINDOW_ACTION_SET_POSITION)
        {
            pAction->m_pPlatformWindow->SetPosition((int)pAction->m_oPosition.x, (int)pAction->m_oPosition.y);
        }
        if (pAction->m_iFlags & E_PLATFORM_WINDOW_ACTION_SET_SIZE)
        {
            pAction->m_pPlatformWindow->SetSize((int)pAction->m_oSize.x, (int)pAction->m_oSize.y);
        }

        delete *m_lPlatformWindowActions.begin();
        m_lPlatformWindowActions.erase(m_lPlatformWindowActions.begin());
    }
}

void ImwWindowManager::UpdateDockActions()
{
    while (m_lDockActions.begin() != m_lDockActions.end())
    {
        DockAction* pAction = *m_lDockActions.begin();

        InternalUnDock(pAction->m_pWindow);

        if (pAction->m_bFloat)
        {
            InternalFloat(pAction->m_pWindow, pAction->m_oPosition, pAction->m_oSize);
        }
        else
        {
            if (NULL != pAction->m_pWith)
            {
                InternalDockWith(pAction->m_pWindow, pAction->m_pWith, pAction->m_eOrientation, pAction->m_fRatio);
            }
            else if (NULL != pAction->m_pToContainer)
            {
                InternalDockTo(pAction->m_pWindow, pAction->m_eOrientation, pAction->m_fRatio, pAction->m_pToContainer, pAction->m_iPosition);
            }
            else if (NULL != pAction->m_pToPlatformWindow)
            {
                InternalDock(pAction->m_pWindow, pAction->m_eOrientation, pAction->m_fRatio, pAction->m_pToPlatformWindow);
            }
        }

        m_lOrphanWindows.remove(pAction->m_pWindow);

        delete pAction;
        m_lDockActions.erase(m_lDockActions.begin());
    }
}

void ImwWindowManager::UpdateOrphans()
{
    while (m_lOrphanWindows.begin() != m_lOrphanWindows.end())
    {
        if (m_pMainPlatformWindow->m_pContainer->IsEmpty())
        {
            InternalDock(*m_lOrphanWindows.begin(), E_DOCK_ORIENTATION_CENTER, 0.5f, m_pMainPlatformWindow);
        }
        else if (CanCreateMultipleWindow())
        {
            ImVec2 oSize = ImVec2(300, 300);
            ImVec2 oPos = m_pMainPlatformWindow->GetPosition();
            ImVec2 oMainSize = m_pMainPlatformWindow->GetSize();
            oPos.x += (oMainSize.x - oSize.x) / 2;
            oPos.y += (oMainSize.y - oSize.y) / 2;
            InternalFloat(*m_lOrphanWindows.begin(), oPos, oSize);
        }
        else
        {
            m_pMainPlatformWindow->m_pContainer->DockToBest(*m_lOrphanWindows.begin());
        }
        m_lOrphanWindows.erase(m_lOrphanWindows.begin());
    }
}

void ImwWindowManager::Paint(ImwPlatformWindow* pWindow)
{
    float fTop = 0.f;
    float fBottom = 0.f;
    if (pWindow->IsMain())
    {
        if (!m_lMenus.empty())
        {
            ImGui::BeginMainMenuBar();
            for (ImwMenuList::iterator it = m_lMenus.begin(), itEnd = m_lMenus.end(); it != itEnd; ++it)
            {
                (*it)->OnMenu();
            }
            fTop = ImGui::GetWindowHeight();
            ImGui::EndMainMenuBar();
        }
        if (m_lStatusBars.size() > 0)
        {
            fBottom = 25.f;
        }
    }

    ImGui::SetNextWindowPos(ImVec2(0, fTop), ImGuiSetCond_Always);
    ImGui::SetNextWindowSize(ImVec2(pWindow->GetSize().x, pWindow->GetSize().y - fTop - fBottom), ImGuiSetCond_Always);
    int iFlags = ImGuiWindowFlags_NoTitleBar
        | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoScrollWithMouse
        | ImGuiWindowFlags_NoBringToFrontOnFocus;

    if (NULL != m_pDraggedWindow)
    {
        iFlags += ImGuiWindowFlags_NoInputs;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(5, 5));
    //ImGui::PushStyleColor(ImGuiCol_WindowBg, ImColor(40,40,40,0));
    //ImGui::PushStyleColor(ImGuiCol_ChildWindowBg, ImColor(59, 59, 59, 255));
    ImGui::Begin("Main", NULL, iFlags);

    if (NULL != m_pDraggedWindow)
    {
        ImGuiID oId = ImGui::GetID("##DraggedWindow");
        ImGui::PushID(oId);
        ImGui::PopID();
        ImGui::SetActiveID(oId, ImGui::GetCurrentWindow());
    }

    if (!m_lToolBars.empty())
    {
        for (ImwToolBarList::iterator it = m_lToolBars.begin(), itEnd = m_lToolBars.end(); it != itEnd; ++it)
        {
            (*it)->OnToolBar();
        }
        ImGui::Separator();
    }
    pWindow->PaintContainer();
    ImGui::End();

    //ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(1);

    ImGui::Begin("##Overlay", NULL, ImVec2(0, 0), 0.f, ImGuiWindowFlags_Tooltip | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);
    ImDrawList* pDrawList = ImGui::GetWindowDrawList();
    for (ImwList<DrawWindowAreaAction>::iterator it = m_lDrawWindowAreas.begin(); it != m_lDrawWindowAreas.end(); )
    {
        DrawWindowAreaAction& oAction = *it;
        //if (pWindow->HasWindow(oAction.m_pWindow))
        if (pWindow == oAction.m_pWindow)
        {
            ImVec2 oPosA = oAction.m_oRectPos;
            ImVec2 oPosB = oAction.m_oRectSize;
            oPosB.x += oPosA.x;
            oPosB.y += oPosA.y;

            pDrawList->PushClipRectFullScreen();
            //pDrawList->AddLine(ImGui::CalcItemRectClosestPoint(ImGui::GetIO().MousePos, true, -2.0f), ImGui::GetIO().MousePos, ImColor(ImGui::GetStyle().Colors[ImGuiCol_Button]), 4.0f);
            pDrawList->AddRectFilled(oPosA, oPosB, oAction.m_oColor);
            pDrawList->PopClipRect();
            ImwList<DrawWindowAreaAction>::iterator toRemove = it;
            ++it;
            m_lDrawWindowAreas.erase(toRemove);
        }
        else
        {
            ++it;
        }
    }
    ImGui::End();

    if (pWindow->IsMain() && m_lStatusBars.size() > 0)
    {
        ImGui::SetNextWindowPos(ImVec2(0, pWindow->GetSize().y - fBottom), ImGuiSetCond_Always);
        ImGui::SetNextWindowSize(ImVec2(pWindow->GetSize().x, fBottom), ImGuiSetCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(5, 5));
        ImGui::Begin("##StatusBar", NULL, ImVec2(0, 0), 1.f, iFlags);

        ImGui::Columns((int)m_lStatusBars.size());
        for (ImwStatusBarList::iterator it = m_lStatusBars.begin(); it != m_lStatusBars.end(); ++it)
        {
            (*it)->OnStatusBar();
            ImGui::NextColumn();
        }
        ImGui::Columns(1);

        ImGui::End();
        ImGui::PopStyleVar();
    }

    ImGui::Render();
}

void ImwWindowManager::StartDragWindow(ImwWindow* pWindow, ImVec2 oOffset)
{
    if (NULL == m_pDraggedWindow)
    {
        m_pDraggedWindow = pWindow;
        m_oDragPreviewOffset = oOffset;

        if (NULL != m_pDragPlatformWindow)
        {
            PlatformWindowAction* pAction = new PlatformWindowAction();
            pAction->m_pPlatformWindow = m_pDragPlatformWindow;
            pAction->m_iFlags = E_PLATFORM_WINDOW_ACTION_SHOW | E_PLATFORM_WINDOW_ACTION_SET_POSITION | E_PLATFORM_WINDOW_ACTION_SET_SIZE;
            ImVec2 oCursorPos = GetCursorPos();
            pAction->m_oPosition = ImVec2(oCursorPos.x + m_oDragPreviewOffset.x, oCursorPos.y + m_oDragPreviewOffset.y);
            pAction->m_oSize = ImVec2(pWindow->GetLastSize().x, pWindow->GetLastSize().y);
            m_lPlatformWindowActions.push_back(pAction);
            Dock(pWindow, E_DOCK_ORIENTATION_CENTER, 0.5f, m_pDragPlatformWindow);
            ImGui::GetIO().MouseDown[0] = true;
        }
        else
        {
            UnDock(pWindow);
        }
    }
}

void ImwWindowManager::StopDragWindow()
{
    if (NULL != m_pDragPlatformWindow)
    {
        PlatformWindowAction* pAction = new PlatformWindowAction();
        pAction->m_pPlatformWindow = m_pDragPlatformWindow;
        pAction->m_iFlags = E_PLATFORM_WINDOW_ACTION_HIDE;
        m_pDragPlatformWindow->Hide();
        m_lPlatformWindowActions.push_back(pAction);
    }
    m_pDraggedWindow = NULL;
}

void ImwWindowManager::UpdateDragWindow()
{
    if (NULL != m_pDraggedWindow)
    {
        ImVec2 oCursorPos = GetCursorPos();

        //Search best dock area
        EDockOrientation eBestDockOrientation;
        ImVec2 oHightlightPos;
        ImVec2 oHightlightSize;
        float fSizeRatio;

        m_pDragBestContainer = GetBestDocking(m_pMainPlatformWindow, oCursorPos, eBestDockOrientation, oHightlightPos, oHightlightSize, fSizeRatio, m_bDragOnTab, m_iDragBestContainerPosition, !CanCreateMultipleWindow());
        if (NULL == m_pDragBestContainer)
        {
            for (ImwList<ImwPlatformWindow*>::iterator it = m_lPlatformWindows.begin(); it != m_lPlatformWindows.end() && NULL == m_pDragBestContainer; ++it)
            {
                m_pDragBestContainer = GetBestDocking(*it, oCursorPos, eBestDockOrientation, oHightlightPos, oHightlightSize, fSizeRatio, m_bDragOnTab, m_iDragBestContainerPosition, false);
            }
        }
        if (m_pDragBestContainer)
        {
            DrawWindowArea(m_pDragBestContainer->GetPlatformWindowParent(), oHightlightPos, oHightlightSize, m_oConfig.m_oHightlightAreaColor);
        }

        if (NULL != m_pDragPlatformWindow)
        {
            if (m_bDragOnTab || m_pDragBestContainer != NULL)
            {
                m_pDragPlatformWindow->Hide();
            }
            else
            {
                m_pDragPlatformWindow->Show();
                m_pCurrentPlatformWindow = m_pDragPlatformWindow;
                m_pDragPlatformWindow->Paint();
                m_pCurrentPlatformWindow = NULL;

                m_pDragPlatformWindow->SetPosition((int)(oCursorPos.x + m_oDragPreviewOffset.x), (int)(oCursorPos.y + m_oDragPreviewOffset.y));
            }
        }

        if (!IsLeftClickDown())
        {
            InternalUnDock(m_pDraggedWindow);
            if (NULL != m_pDragBestContainer)
            {
                InternalDockTo(m_pDraggedWindow, eBestDockOrientation, fSizeRatio, m_pDragBestContainer, m_iDragBestContainerPosition);
            }
            else if (NULL != m_pDragPlatformWindow)
            {
                InternalFloat(m_pDraggedWindow, m_pDragPlatformWindow->GetPosition(), m_pDragPlatformWindow->GetSize());
            }
            else
            {
                InternalDock(m_pDraggedWindow, E_DOCK_ORIENTATION_CENTER, fSizeRatio, m_pMainPlatformWindow);
            }

            StopDragWindow();
        }
    }
}

ImwWindow* ImwWindowManager::GetDraggedWindow() const
{
    return m_pDraggedWindow;
}

ImVec2 ImwWindowManager::GetDragOffset() const
{
    return m_oDragPreviewOffset;
}

ImwContainer* ImwWindowManager::GetDragBestContainer() const
{
    return m_pDragBestContainer;
}

bool ImwWindowManager::GetDragOnTabArea() const
{
    return m_bDragOnTab;
}

int ImwWindowManager::GetDragTabPosition() const
{
    return m_iDragBestContainerPosition;
}


ImwContainer* ImwWindowManager::GetBestDocking(ImwPlatformWindow* pPlatformWindow, const ImVec2 oCursorPos, EDockOrientation& oOutOrientation, ImVec2& oOutAreaPos, ImVec2& oOutAreaSize, float& fOutRatio, bool& bOutOnTabArea, int& iOutPosition, bool bLargeCheck)
{
    ImVec2 oPos = pPlatformWindow->GetPosition();
    ImVec2 oSize = pPlatformWindow->GetSize();
    if (bLargeCheck || (oCursorPos.x >= oPos.x && oCursorPos.x <= (oPos.x + oSize.x) &&
        oCursorPos.y >= oPos.y && oCursorPos.y <= (oPos.y + oSize.y)))
    {
        ImVec2 oRectPos(oCursorPos.x - oPos.x, oCursorPos.y - oPos.y);

        ImwContainer* pBestContainer = pPlatformWindow->GetContainer()->GetBestDocking(oRectPos, oOutOrientation, oOutAreaPos, oOutAreaSize, bOutOnTabArea, iOutPosition, bLargeCheck);
        if (NULL != pBestContainer)
        {
            fOutRatio = 0.5f; //Default value
            return pBestContainer;
        }

        //Left
        if (oRectPos.x <= oSize.x * m_oConfig.m_fDragMarginRatio)
        {
            oOutOrientation = E_DOCK_ORIENTATION_LEFT;
            oOutAreaPos = ImVec2(0, 0);
            oOutAreaSize = ImVec2(oSize.x * m_oConfig.m_fDragMarginSizeRatio, oSize.y);
            bOutOnTabArea = false;
        }
        //Right
        else if (oRectPos.x >= oSize.x * (1.f - m_oConfig.m_fDragMarginRatio))
        {
            oOutOrientation = E_DOCK_ORIENTATION_RIGHT;
            oOutAreaPos = ImVec2(oSize.x * (1.f - m_oConfig.m_fDragMarginSizeRatio), 0.f);
            oOutAreaSize = ImVec2(oSize.x * m_oConfig.m_fDragMarginSizeRatio, oSize.y);
            bOutOnTabArea = false;
        }
        //Top
        else if (oRectPos.y <= oSize.y * m_oConfig.m_fDragMarginRatio)
        {
            oOutOrientation = E_DOCK_ORIENTATION_TOP;
            oOutAreaPos = ImVec2(0, 0);
            oOutAreaSize = ImVec2(oSize.x, oSize.y * m_oConfig.m_fDragMarginSizeRatio);
            bOutOnTabArea = false;
        }
        //Bottom
        else if (oRectPos.y >= oSize.y * (1.f - m_oConfig.m_fDragMarginRatio))
        {
            oOutOrientation = E_DOCK_ORIENTATION_BOTTOM;
            oOutAreaPos = ImVec2(0.f, oSize.y * (1.f - m_oConfig.m_fDragMarginSizeRatio));
            oOutAreaSize = ImVec2(oSize.x, oSize.y * m_oConfig.m_fDragMarginSizeRatio);
            bOutOnTabArea = false;
        }
        else
        {
            oOutOrientation = E_DOCK_ORIENTATION_CENTER;
            oOutAreaPos = ImVec2(0, 0);
            oOutAreaSize = ImVec2(oSize.x, oSize.y);
            bOutOnTabArea = false;
            //IM_ASSERT(false); //Best dock orientation not found
            return NULL;
        }
        fOutRatio = GetConfig().m_fDragMarginSizeRatio;
        return pPlatformWindow->GetContainer();
    }
    oOutOrientation = E_DOCK_ORIENTATION_CENTER;
    oOutAreaPos = ImVec2(0, 0);
    oOutAreaSize = ImVec2(oSize.x, oSize.y);
    bOutOnTabArea = false;
    return NULL;
}

void ImwWindowManager::AddWindow(ImwWindow* pWindow)
{
    m_lWindows.push_back(pWindow);

    m_lOrphanWindows.push_back(pWindow);
}

void ImwWindowManager::RemoveWindow(ImwWindow* pWindow)
{
    m_lWindows.remove(pWindow);
    m_lOrphanWindows.remove(pWindow);
}

void ImwWindowManager::DestroyWindow(ImwWindow* pWindow)
{
    if (NULL != pWindow && std::find(m_lToDestroyWindows.begin(), m_lToDestroyWindows.end(), pWindow) == m_lToDestroyWindows.end())
    {
        m_lToDestroyWindows.push_back(pWindow);
    }
}

void ImwWindowManager::AddStatusBar(ImwStatusBar* pStatusBar)
{
    ImwStatusBarList::iterator it = m_lStatusBars.begin(), itEnd = m_lStatusBars.end();
    for (; it != itEnd; ++it)
    {
        if (pStatusBar->GetHorizontalPriority() <= (*it)->GetHorizontalPriority())
            break;
    }
    m_lStatusBars.insert(it, pStatusBar);
}

void ImwWindowManager::RemoveStatusBar(ImwStatusBar* pStatusBar)
{
    m_lStatusBars.remove(pStatusBar);
}

void ImwWindowManager::DestroyStatusBar(ImwStatusBar* pStatusBar)
{
    if (NULL != pStatusBar && std::find(m_lToDestroyStatusBars.begin(), m_lToDestroyStatusBars.end(), pStatusBar) == m_lToDestroyStatusBars.end())
    {
        m_lToDestroyStatusBars.push_back(pStatusBar);
    }
}

void ImwWindowManager::AddMenu(ImwMenu* pMenu)
{
    ImwMenuList::iterator it = m_lMenus.begin(), itEnd = m_lMenus.end();
    for (; it != itEnd; ++it)
    {
        if (pMenu->GetHorizontalPriority() <= (*it)->GetHorizontalPriority())
            break;
    }
    m_lMenus.insert(it, pMenu);
}

void ImwWindowManager::RemoveMenu(ImwMenu* pMenu)
{
    m_lMenus.remove(pMenu);
}

void ImwWindowManager::DestroyMenu(ImwMenu* pMenu)
{
    if (NULL != pMenu && std::find(m_lToDestroyMenus.begin(), m_lToDestroyMenus.end(), pMenu) == m_lToDestroyMenus.end())
    {
        m_lToDestroyMenus.push_back(pMenu);
    }
}

void ImwWindowManager::AddToolBar(ImwToolBar* pToolBar)
{
    ImwToolBarList::iterator it = m_lToolBars.begin(), itEnd = m_lToolBars.end();
    for (; it != itEnd; ++it)
    {
        if (pToolBar->GetHorizontalPriority() <= (*it)->GetHorizontalPriority())
            break;
    }
    m_lToolBars.insert(it, pToolBar);
}

void ImwWindowManager::RemoveToolBar(ImwToolBar* pToolBar)
{
    m_lToolBars.remove(pToolBar);
}

void ImwWindowManager::DestroyToolBar(ImwToolBar* pToolBar)
{
    if (NULL != pToolBar && std::find(m_lToDestroyToolBars.begin(), m_lToDestroyToolBars.end(), pToolBar) == m_lToDestroyToolBars.end())
    {
        m_lToDestroyToolBars.push_back(pToolBar);
    }
}

void ImwWindowManager::InternalDock(ImwWindow* pWindow, EDockOrientation eOrientation, float fRatio, ImwPlatformWindow* pToPlatformWindow)
{
    pToPlatformWindow->m_pContainer->Dock(pWindow, eOrientation, fRatio);
}

void ImwWindowManager::InternalDockTo(ImwWindow* pWindow, EDockOrientation eOrientation, float fRatio, ImwContainer* pToContainer, int iPosition)
{
    pToContainer->Dock(pWindow, eOrientation, fRatio, iPosition);
}

void ImwWindowManager::InternalDockWith(ImwWindow* pWindow, ImwWindow* pWithWindow, EDockOrientation eOrientation, float fRatio)
{
    ImwContainer* pContainer = m_pMainPlatformWindow->HasWindow(pWithWindow);
    if (NULL != pContainer)
    {
        pContainer->Dock(pWindow, eOrientation, fRatio);
    }

    for (ImwList<ImwPlatformWindow*>::iterator it = m_lPlatformWindows.begin(); it != m_lPlatformWindows.end(); ++it)
    {
        pContainer = (*it)->HasWindow(pWithWindow);
        if (NULL != pContainer)
        {
            pContainer->Dock(pWindow, eOrientation, fRatio);
            break;
        }
    }
}

void ImwWindowManager::InternalFloat(ImwWindow* pWindow, ImVec2 oPosition, ImVec2 oSize)
{
    ImwPlatformWindow* pPlatformWindow = CreatePlatformWindow(false, m_pMainPlatformWindow, false);
    if (NULL != pPlatformWindow)
    {
        m_lPlatformWindows.push_back(pPlatformWindow);

        const ImVec2 oVec2_1 = ImVec2(1, 1);
        const ImVec2 oVec2_N1 = ImVec2(-1, -1);

        if (oSize.x == oVec2_1.x && oSize.y == oVec2_1.y)
        {
            oSize = pWindow->GetLastSize();
        }
        if (oPosition.x == oVec2_N1.x && oPosition.y == oVec2_N1.y)
        {
            oPosition = GetCursorPos();
            oPosition.x -= 20;
            oPosition.x -= 10;
        }
        pPlatformWindow->Dock(pWindow);
        pPlatformWindow->SetSize((int)oSize.x, (int)oSize.y);
        pPlatformWindow->SetPosition((int)oPosition.x, (int)oPosition.y);
        pPlatformWindow->Show();
    }
}

void ImwWindowManager::InternalUnDock(ImwWindow* pWindow)
{
    if (m_pMainPlatformWindow->UnDock(pWindow))
    {
        return;
    }

    for (ImwList<ImwPlatformWindow*>::iterator it = m_lPlatformWindows.begin(); it != m_lPlatformWindows.end(); ++it)
    {
        if ((*it)->UnDock(pWindow))
        {
            //Destroy empty platform window if not main window
            if (!(*it)->IsMain() && (*it)->GetContainer()->IsEmpty())
            {
                m_lToDestroyPlatformWindows.push_back(*it);
            }
            return;
        }
    }

    if (NULL != m_pDragPlatformWindow)
    {
        m_pDragPlatformWindow->UnDock(pWindow);
    }
}

void ImwWindowManager::OnClosePlatformWindow(ImwPlatformWindow* pWindow)
{
    if (NULL != pWindow && !pWindow->m_pContainer->HasUnclosableWindow())
    {
        PlatformWindowAction* pAction = new PlatformWindowAction();
        pAction->m_iFlags = E_PLATFORM_WINDOW_ACTION_DESTROY;
        pAction->m_pPlatformWindow = pWindow;
        m_lPlatformWindowActions.push_back(pAction);
    }
}

void ImwWindowManager::DrawWindowArea(ImwPlatformWindow* pWindow, const ImVec2& oPos, const ImVec2& oSize, const ImColor& oColor)
{
    m_lDrawWindowAreas.push_back(DrawWindowAreaAction(pWindow, oPos, oSize, oColor));
}

// Static
ImwWindowManager* ImwWindowManager::GetInstance()
{
    return s_pInstance;
}
//SFF_END
}