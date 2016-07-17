#include "ImwWindowManagerDX11.h"
#include "ImwPlatformWindowDX11.h"

#include "imgui_impl_dx11.h"

using namespace ImWindow;

ImwWindowManagerDX11::ImwWindowManagerDX11()
{
	ImwPlatformWindowDX11::InitDX11();
}

ImwWindowManagerDX11::~ImwWindowManagerDX11()
{
	ImwPlatformWindowDX11::ShutdownDX11();
	//ImGui_ImplDX11_Shutdown();
}

void ImwWindowManagerDX11::InternalRun()
{
	PreUpdate();
	/*MSG msg;
	if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}*/
	Sleep(16);
}

ImwPlatformWindow* ImwWindowManagerDX11::CreatePlatformWindow(bool bMain, ImwPlatformWindow* pParent, bool bDragWindow)
{
	IM_ASSERT(m_pCurrentPlatformWindow == NULL);
	ImwPlatformWindowDX11* pWindow = new ImwPlatformWindowDX11(bMain, bDragWindow, CanCreateMultipleWindow());
	if (pWindow->Init(pParent))
	{
		return (ImwPlatformWindow*)pWindow;
	}
	else
	{
		delete pWindow;
		return NULL;
	}
}

ImVec2 ImwWindowManagerDX11::GetCursorPos()
{
	POINT oPoint;
	::GetCursorPos(&oPoint);
	return ImVec2(static_cast<float>(oPoint.x), static_cast<float>(oPoint.y));
}

bool ImwWindowManagerDX11::IsLeftClickDown()
{
	return (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
}
