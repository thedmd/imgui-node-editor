
#ifndef __IM_WINDOW_MANAGER_DX11_H__
#define __IM_WINDOW_MANAGER_DX11_H__

#include "ImWindow/ImwConfig.h"

#include "ImWindow/ImwWindowManager.h"

namespace ImWindow
{
	class ImwWindowManagerDX11 : public ImwWindowManager
	{
	public:
		ImwWindowManagerDX11();
		virtual							~ImwWindowManagerDX11();
	protected:
		virtual bool					CanCreateMultipleWindow() { return true; }
		virtual ImwPlatformWindow*		CreatePlatformWindow(bool bMain, ImwPlatformWindow* pParent, bool bDragWindow);

		virtual void					InternalRun();
		virtual ImVec2					GetCursorPos();
		virtual bool					IsLeftClickDown();
	};
}

#endif //__IM_WINDOW_MANAGER_DX11_H__