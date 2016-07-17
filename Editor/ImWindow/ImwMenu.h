
#ifndef __IM_MENU_H__
#define __IM_MENU_H__

#include "ImwConfig.h"

namespace ImWindow
{
//SFF_BEGIN
    class IMGUI_API ImwMenu
    {
    public:
        ImwMenu(int iHorizontalPriority = 0);
        ImwMenu(const ImwMenu& oStatusBar);
        virtual                     ~ImwMenu();

        virtual void                OnMenu() = 0;

        int                         GetHorizontalPriority() const;
    private:
        int                         m_iHorizontalPriority;
    };
    typedef ImwList<ImwMenu*> ImwMenuList;
//SFF_END
}


#endif // __IM_MENU_H__