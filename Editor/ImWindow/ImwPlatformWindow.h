
#ifndef __IM_PLATFORM_WINDOW_H__
#define __IM_PLATFORM_WINDOW_H__

#include "ImwConfig.h"

#include "ImwContainer.h"

namespace ImWindow
{
//SFF_BEGIN
	class IMGUI_API ImwPlatformWindow
	{
		friend class ImwWindowManager;
	public:
											ImwPlatformWindow(bool bMainWindow, bool bIsDragWindow, bool bCreateState);
		virtual								~ImwPlatformWindow();

		virtual bool						Init(ImwPlatformWindow* pParent);

		virtual const ImVec2&				GetPosition() const;
		virtual const ImVec2&				GetSize() const;

		virtual void						Show();
		virtual void						Hide();
		virtual void						SetSize(int iWidth, int iHeight);
		virtual void						SetPosition(int iX, int iY);
		virtual void						SetTitle(const char* pTtile);

		bool								IsMain();

		void								Dock(ImwWindow* pWindow);
		bool								UnDock(ImwWindow* pWindow);

		ImwContainer*						GetContainer();
		ImwContainer*						HasWindow(ImwWindow* pWindow);
		bool								IsStateSet();
	protected:
		void								SetState();
		void								RestoreState();
		void								OnLoseFocus();
		virtual void						PreUpdate();
		virtual void						Paint();
		virtual void						Destroy();
		virtual void						StartDrag();
		virtual void						StopDrag();
		virtual bool						IsDraging();

		void								PaintContainer();
		void								OnClose();

		bool								m_bMain;
		bool								m_bIsDragWindow;
		ImwContainer*						m_pContainer;
		void*								m_pState;
		void*								m_pPreviousState;
	};

	typedef ImwList<ImwPlatformWindow*> ImwPlatformWindowList;
//SFF_END
}

#endif // __IM_PLATFORM_WINDOW_H__