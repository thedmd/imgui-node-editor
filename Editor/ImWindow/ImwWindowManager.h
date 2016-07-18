#ifndef __IM_WINDOW_MANAGER_H__
#define __IM_WINDOW_MANAGER_H__

#include "ImwConfig.h"
#include "ImwWindow.h"
#include "ImwMenu.h"
#include "ImwPlatformWindow.h"
#include "ImwStatusBar.h"
#include "ImwToolbar.h"

namespace ImWindow
{
//SFF_BEGIN
    class IMGUI_API ImwWindowManager
    {
        friend class ImwWindow;
        friend class ImwMenu;
        friend class ImwStatusBar;
        friend class ImwToolBar;
        friend class ImwPlatformWindow;
        friend class ImwContainer;

        enum EPlatformWindowAction
        {
            E_PLATFORM_WINDOW_ACTION_DESTROY        = 1,
            E_PLATFORM_WINDOW_ACTION_SHOW           = 2,
            E_PLATFORM_WINDOW_ACTION_HIDE           = 4,
            E_PLATFORM_WINDOW_ACTION_SET_POSITION   = 8,
            E_PLATFORM_WINDOW_ACTION_SET_SIZE       = 16,
        };

        struct PlatformWindowAction
        {
            ImwPlatformWindow*      m_pPlatformWindow;
            unsigned int            m_iFlags;
            ImVec2                  m_oPosition;
            ImVec2                  m_oSize;
        };

        struct DockAction
        {
            ImwWindow*              m_pWindow;
            // Is Dock or Float
            bool                    m_bFloat;
            //For Docking
            ImwWindow*              m_pWith;
            EDockOrientation        m_eOrientation;
            ImwPlatformWindow*      m_pToPlatformWindow;
            ImwContainer*           m_pToContainer;
            int                     m_iPosition;
            float                   m_fRatio;
            //For Floating
            ImVec2                  m_oPosition;
            ImVec2                  m_oSize;
        };

        struct DrawWindowAreaAction
        {
            DrawWindowAreaAction( ImwPlatformWindow* pWindow, const ImVec2& oRectPos, const ImVec2& oRectSize, const ImColor& oColor );
            ImwPlatformWindow*      m_pWindow;
            ImVec2                  m_oRectPos;
            ImVec2                  m_oRectSize;
            ImColor                 m_oColor;
        };
    public:
        struct Config
        {
            Config();
            float                   m_fDragMarginRatio;
            float                   m_fDragMarginSizeRatio;
            ImColor                 m_oHightlightAreaColor;
            bool                    m_bTabUseImGuiColors;
        };
    public:
        ImwWindowManager();
        virtual                             ~ImwWindowManager();

        bool                                Init();
        bool                                Run();
        void                                Destroy();

        ImwPlatformWindow*                  GetMainPlatformWindow();
        Config&                             GetConfig();

        void                                SetMainTitle(const char* pTitle);

        void                                Dock(ImwWindow* pWindow, EDockOrientation eOrientation = E_DOCK_ORIENTATION_CENTER, float fRatio = 0.5f, ImwPlatformWindow* pToPlatformWindow = NULL);
        void                                DockTo(ImwWindow* pWindow, EDockOrientation eOrientation = E_DOCK_ORIENTATION_CENTER, float fRatio = 0.5f, ImwContainer* pContainer = NULL, int iPosition = -1);
        void                                DockWith(ImwWindow* pWindow, ImwWindow* pWithWindow, EDockOrientation eOrientation = E_DOCK_ORIENTATION_CENTER, float fRatio = 0.5f);
        void                                Float(ImwWindow* pWindow, const ImVec2& oPosition = ImVec2(-1, -1), const ImVec2& oSize = ImVec2(-1, -1));

        const ImwWindowList&                GetWindowList() const;
        ImwPlatformWindow*                  GetCurrentPlatformWindow();
        ImwPlatformWindow*                  GetWindowParent(ImwWindow* pWindow);

        ImFontAtlas*                        GetFontAtlas() { return &m_FontAtlas; }
        ImGuiStyle*                         GetStyle() { return &m_Style; }

    protected:
        //To override for use multi window mode
        virtual bool                        CanCreateMultipleWindow();
        virtual ImwPlatformWindow*          CreatePlatformWindow(bool bMain, ImwPlatformWindow* pParent, bool bDragWindow);
        virtual void                        InternalRun();
        virtual ImVec2                      GetCursorPos();
        virtual bool                        IsLeftClickDown();

        void                                AddWindow(ImwWindow* pWindow);
        void                                RemoveWindow(ImwWindow* pWindow);
        void                                DestroyWindow(ImwWindow* pWindow);

        void                                AddStatusBar(ImwStatusBar* pStatusBar);
        void                                RemoveStatusBar(ImwStatusBar* pStatusBar);
        void                                DestroyStatusBar(ImwStatusBar* pStatusBar);

        void                                AddMenu(ImwMenu* pMenu);
        void                                RemoveMenu(ImwMenu* pMenu);
        void                                DestroyMenu(ImwMenu* pWindow);

        void                                AddToolBar(ImwToolBar* pToolBar);
        void                                RemoveToolBar(ImwToolBar* pToolBar);
        void                                DestroyToolBar(ImwToolBar* pToolBar);

        void                                UnDock(ImwWindow* pWindow);
        void                                InternalDock(ImwWindow* pWindow, EDockOrientation eOrientation, float fRatio, ImwPlatformWindow* pToPlatformWindow);
        void                                InternalDockTo(ImwWindow* pWindow, EDockOrientation eOrientation, float fRatio, ImwContainer* pToContainer, int iPosition);
        void                                InternalDockWith(ImwWindow* pWindow, ImwWindow* pWithWindow, EDockOrientation eOrientation, float fRatio);
        void                                InternalFloat(ImwWindow* pWindow, ImVec2 oPosition, ImVec2 oSize);
        void                                InternalUnDock(ImwWindow* pWindow);

        void                                OnClosePlatformWindow(ImwPlatformWindow* pWindow);

        void                                DrawWindowArea( ImwPlatformWindow* pWindow, const ImVec2& oPos, const ImVec2& oSize, const ImColor& oColor );

        void                                PreUpdate();
        void                                Update();
        void                                UpdatePlatformwWindowActions();
        void                                UpdateDockActions();
        void                                UpdateOrphans();

        void                                Paint(ImwPlatformWindow* pWindow);

        void                                StartDragWindow(ImwWindow* pWindow, ImVec2 oOffset);
        void                                StopDragWindow();
        void                                UpdateDragWindow();
        ImwWindow*                          GetDraggedWindow() const;
        ImVec2                              GetDragOffset() const;
        ImwContainer*                       GetDragBestContainer() const;
        bool                                GetDragOnTabArea() const;
        int                                 GetDragTabPosition() const;
        ImwContainer*                       GetBestDocking(ImwPlatformWindow* pPlatformWindow, const ImVec2 oCursorPos, EDockOrientation& oOutOrientation, ImVec2& oOutAreaPos, ImVec2& oOutAreaSize, float& fOutRatio, bool& bOutOnTabArea, int& iOutPosition, bool bLargeCheck);

        Config                              m_oConfig;
        ImwPlatformWindow*                  m_pMainPlatformWindow;
        ImwPlatformWindowList               m_lPlatformWindows;
        ImwPlatformWindow*                  m_pDragPlatformWindow;
        ImwWindowList                       m_lWindows;
        ImwWindowList                       m_lOrphanWindows;
        ImwWindowList                       m_lToDestroyWindows;
        ImwStatusBarList                    m_lStatusBars;
        ImwStatusBarList                    m_lToDestroyStatusBars;
        ImwToolBarList                      m_lToolBars;
        ImwToolBarList                      m_lToDestroyToolBars;
        ImwMenuList                         m_lMenus;
        ImwMenuList                         m_lToDestroyMenus;
        ImwPlatformWindowList               m_lToDestroyPlatformWindows;
        ImwList<PlatformWindowAction*>      m_lPlatformWindowActions;
        ImwList<DockAction*>                m_lDockActions;
        ImwList<DrawWindowAreaAction>       m_lDrawWindowAreas;

        ImwPlatformWindow*                  m_pCurrentPlatformWindow;
        ImwWindow*                          m_pDraggedWindow;
        bool                                m_bDragOnTab;
        ImwContainer*                       m_pDragBestContainer;
        int                                 m_iDragBestContainerPosition;

        ImVec2                              m_oDragPreviewOffset;

        ImGuiContext*                       m_MainContext;
        ImFontAtlas                         m_FontAtlas;
        ImGuiStyle                          m_Style;

        // Static
    public:
        static ImwWindowManager*            GetInstance();
    protected:
        static ImwWindowManager*            s_pInstance;
    };
//SFF_END
}


#endif // __IM_WINDOW_MANAGER_H__