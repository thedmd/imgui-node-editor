
#include "ImwMenu.h"

#include "ImwWindowManager.h"

namespace ImWindow
{
//SFF_BEGIN
    ImwMenu::ImwMenu(int iHorizontalPriority)
    {
        m_iHorizontalPriority = iHorizontalPriority;

        ImwWindowManager::GetInstance()->AddMenu(this);
    }

    ImwMenu::ImwMenu(const ImwMenu& oStatusBar)
    {
        m_iHorizontalPriority = oStatusBar.m_iHorizontalPriority;
    }

    ImwMenu::~ImwMenu()
    {
        ImwWindowManager::GetInstance()->RemoveMenu(this);
    }

    int ImwMenu::GetHorizontalPriority() const
    {
        return m_iHorizontalPriority;
    }
//SFF_END
}