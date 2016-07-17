
#ifndef __IMW_CONTAINER_H__
#define __IMW_CONTAINER_H__

#include "ImwConfig.h"
#include "ImwWindow.h"

namespace ImWindow
{
	class ImwPlatformWindow;
//SFF_BEGIN
	class IMGUI_API ImwContainer
	{
		friend class ImwPlatformWindow;
	public:

		void							Dock(ImwWindow* pWindow, EDockOrientation eOrientation = E_DOCK_ORIENTATION_CENTER, float fRatio = 0.5f, int iPosition = -1);
		bool							UnDock(ImwWindow* pWindow);
		void							DockToBest(ImwWindow* pWindow);

		bool							IsEmpty() const;
		bool							IsSplit() const;
		bool							HasWindowTabbed() const;
		ImwContainer*					HasWindow(const ImwWindow* pWindow);
		ImwPlatformWindow*				GetPlatformWindowParent() const;
		ImwContainer*					GetBestDocking(const ImVec2 oCursorPosInContainer, EDockOrientation& oOutOrientation, ImVec2& oOutAreaPos, ImVec2& oOutAreaSize, bool& bOutOnTabArea, int& iOutPosition, bool bLargeCheck);
		bool							HasUnclosableWindow() const;
	protected:
										ImwContainer(ImwContainer* pParent);
										ImwContainer(ImwPlatformWindow* pParent);
										~ImwContainer();

		void							CreateSplits();

		void							Paint();

		bool							Tab(const ImwWindow* pWindow, bool bFocused, float fStartLinePos, float fEndLinePos, float fMaxSize = -1.f);
		void							DrawTab(const char* pText, bool bFocused, ImVec2 oPos, float fStartLinePos, float fEndLinePos, float fMaxSize = -1.f, ImVec2* pSizeOut = NULL);
		float							GetTabWidth(const char* pText, float fMaxSize, ImVec2* pOutTextSize = NULL);
		float							GetTabAreaWidth() const;

		ImwContainer*					m_pParent;
		ImwPlatformWindow*				m_pParentWindow;
		ImwWindowList					m_lWindows;
		ImwContainer*					m_pSplits[2];

		float							m_fSplitRatio;
		bool							m_bVerticalSplit;
		int								m_iActiveWindow;

		bool							m_bIsDrag;

		ImVec2							m_oLastPosition;
		ImVec2							m_oLastSize;

		static const float				c_fTabHeight;
	};
//SFF_END
}

#endif // __IMW_CONTAINER_H__