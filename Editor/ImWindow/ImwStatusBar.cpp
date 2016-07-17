
#include "ImwStatusBar.h"

#include "ImwWindowManager.h"

namespace ImWindow
{
//SFF_BEGIN
    ImwStatusBar::ImwStatusBar(int iHorizontalPriority)
    {
        m_iHorizontalPriority = iHorizontalPriority;

        ImwWindowManager::GetInstance()->AddStatusBar(this);
    }

    ImwStatusBar::ImwStatusBar(const ImwStatusBar& oStatusBar)
    {
        m_iHorizontalPriority = oStatusBar.m_iHorizontalPriority;
    }

    ImwStatusBar::~ImwStatusBar()
    {
        ImwWindowManager::GetInstance()->RemoveStatusBar(this);
    }

    void ImwStatusBar::OnStatusBar()
    {
    }

    int ImwStatusBar::GetHorizontalPriority() const
    {
        return m_iHorizontalPriority;
    }
//SFF_END
}