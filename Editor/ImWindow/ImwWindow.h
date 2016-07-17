
#ifndef __IM_WINDOW_H__
#define __IM_WINDOW_H__

#include "ImwConfig.h"

namespace ImWindow
{
//SFF_BEGIN
    enum EDockOrientation
    {
        E_DOCK_ORIENTATION_CENTER,
        //E_DOCK_ORIENTATION_TABBED = E_DOCK_ORIENTATION_CENTER,
        E_DOCK_ORIENTATION_TOP,
        E_DOCK_ORIENTATION_LEFT,
        E_DOCK_ORIENTATION_RIGHT,
        E_DOCK_ORIENTATION_BOTTOM,
    };

#ifdef IMW_INHERITED_BY_IMWWINDOW
    class IMGUI_API ImwWindow : public IMW_INHERITED_BY_IMWWINDOW
#else
    class IMGUI_API ImwWindow
#endif //IMW_INHERITED_BY_IMWWINDOW
    {
        friend class ImwWindowManager;
        friend class ImwContainer;
    protected:
        ImwWindow();
        virtual                 ~ImwWindow();
    public:
        virtual void            OnGui() = 0;
        virtual void            OnContextMenu();

        ImU32                   GetId() const;
        const char*             GetIdStr() const;

        void                    Destroy();

        void                    SetTitle(const char* pTitle);
        const char*             GetTitle() const;

        void                    SetClosable( bool bClosable );
        bool                    IsClosable() const;

        const ImVec2&           GetLastPosition() const;
        const ImVec2&           GetLastSize() const;

    protected:

        char*                   m_pTitle;
        ImU32                   m_iId;
        char                    m_pId[11];
        bool                    m_bClosable;

        ImVec2                  m_oLastPosition;
        ImVec2                  m_oLastSize;

        static int              s_iNextId;
    };

    typedef ImwList<ImwWindow*> ImwWindowList;
//SFF_END
}

#endif // __IM_WINDOW_H__