
#ifndef __IM_PLATFORM_WINDOW_DX11_H__
#define __IM_PLATFORM_WINDOW_DX11_H__

#include "ImWindow/ImwConfig.h"
#include "ImWindow/ImwPlatformWindow.h"

#include <map>

#include "imgui_impl_dx11.h"

#include <windows.h>

#include <d3d11.h>

namespace ImWindow
{
	class ImwPlatformWindowDX11 : ImwPlatformWindow
	{
		friend class ImwWindowManagerDX11;
	public:
		typedef ImwMap<HWND, ImwPlatformWindowDX11*> InstanceMap;
	public:
											ImwPlatformWindowDX11(bool bMain, bool bIsDragWindow, bool bCreateState);
		virtual								~ImwPlatformWindowDX11();

		virtual bool						Init(ImwPlatformWindow* pMain);

		virtual const ImVec2&				GetPosition() const;
		virtual const ImVec2&				GetSize() const;

		virtual void						Show();
		virtual void						Hide();
		virtual void						SetSize(int iWidth, int iHeight);
		virtual void						SetPosition(int iX, int iY);
		virtual void						SetTitle(const char* pTtile);

		HWND								GetHWnd();
	protected:
		virtual void						PreUpdate();
		virtual void						Paint();
		virtual void						Destroy();

		virtual void						StartDrag();
		virtual void						StopDrag();
		virtual bool						IsDraging();

		LRESULT								OnMessage(UINT message, WPARAM wParam, LPARAM lParam);

		HWND								m_hWnd;

		// Static
	public:
		static int							GetInstanceCount();

	protected:
		static void							InitWndClassEx();
		static LRESULT CALLBACK				ImwPlatformWindowDX11Proc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

		static bool							InitDX11();
		static void							ShutdownDX11();


		static InstanceMap					s_mInstances;
		static bool							s_bClassInitialized;
		static WNDCLASSEX					s_oWndClassEx;
		static IDXGIFactory*				s_pFactory;
		static ID3D11Device*				s_pDevice;
		static ID3D11DeviceContext*			s_pDeviceContext;
		static ImwPlatformWindow*			s_pLastHoveredWindow;
		static INT64						g_Time;
		static INT64						g_TicksPerSecond;

		ImVec2								m_oPosition;
		ImVec2								m_oSize;

		IDXGISwapChain*						m_pSwapChain;
		ID3D11RenderTargetView*				m_pRenderTargetView;

		bool								m_bDrag;
		ImVec2								m_iCursorPosStartDrag;
		ImVec2								m_iWindowPosStartDrag;

        bool                                m_IsCursorCaptured;
        HWND                                m_PreviousCapture;

		HCURSOR								m_hCursorArrow;
		HCURSOR								m_hCursorResizeNS;
		HCURSOR								m_hCursorResizeWE;

	};
}

#endif // __IM_PLATFORM_WINDOW_DX11_H__