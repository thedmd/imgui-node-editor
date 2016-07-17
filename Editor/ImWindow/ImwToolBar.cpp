
#include "ImwToolBar.h"

#include "ImwWindowManager.h"

namespace ImWindow
{
//SFF_BEGIN
    ImwToolBar::ImwToolBar(int iHorizontalPriority)
    {
        m_iHorizontalPriority = iHorizontalPriority;

        ImwWindowManager::GetInstance()->AddToolBar(this);
    }

    ImwToolBar::ImwToolBar(const ImwToolBar& oToolBar)
    {
        m_iHorizontalPriority = oToolBar.m_iHorizontalPriority;
    }

    ImwToolBar::~ImwToolBar()
    {
        ImwWindowManager::GetInstance()->RemoveToolBar(this);
    }

    void ImwToolBar::Destroy()
    {
        ImwWindowManager::GetInstance()->DestroyToolBar(this);
    }

    int ImwToolBar::GetHorizontalPriority() const
    {
        return m_iHorizontalPriority;
    }
//SFF_END
}