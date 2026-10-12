# include "platform.h"
# include "setup.h"

# if BACKEND(IMGUI_WIN32)

# include "application.h"
# include "renderer.h"

# define NOMINMAX
# define WIN32_LEAN_AND_MEAN
# include <windows.h>
# include <tchar.h>
# include <string>

# include <imgui.h>
# include <imgui_internal.h>
# include "imgui_impl_win32.h"

# if defined(_UNICODE)
std::wstring Utf8ToNative(const std::string& str)
{
    int size = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), nullptr, 0);
    std::wstring result(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), (wchar_t*)result.data(), size);
    return result;
}
# else
std::string Utf8ToNative(const std::string& str)
{
    return str;
}
# endif

IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

struct PlatformWin32 final
    : Platform
{
    static PlatformWin32* s_Instance;

    PlatformWin32(Application& application);

    bool ApplicationStart(int argc, char** argv) override;
    void ApplicationStop() override;
    bool OpenMainWindow(const char* title, int width, int height) override;
    bool CloseMainWindow() override;
    void* GetMainWindowHandle() const override;
    void SetMainWindowTitle(const char* title) override;
    void ShowMainWindow() override;
    bool ProcessMainWindowEvents() override;
    bool IsMainWindowVisible() const override;
    void SetRenderer(Renderer* renderer) override;
    void NewFrame() override;
    void FinishFrame() override;
    void Quit() override;

    //void SetDpiScale(float dpiScale);

    LRESULT WinProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    Application&    m_Application;
    WNDCLASSEX      m_WindowClass = {};
    HWND            m_MainWindowHandle = nullptr;
    bool            m_IsMinimized = false;
    bool            m_WasMinimized = false;
    bool            m_CanCloseResult = false;
    Renderer*       m_Renderer = nullptr;
    ImU32           m_LastEventId = 0;
};

std::unique_ptr<Platform> CreatePlatform(Application& application)
{
    return std::make_unique<PlatformWin32>(application);
}

PlatformWin32* PlatformWin32::s_Instance = nullptr;

PlatformWin32::PlatformWin32(Application& application)
    : m_Application(application)
{
}

bool PlatformWin32::ApplicationStart(int argc, char** argv)
{
    if (s_Instance)
        return false;

    s_Instance = this;

    ImGui_ImplWin32_EnableDpiAwareness();

    auto winProc = [](HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) -> LRESULT
    {
        return s_Instance->WinProc(hWnd, msg, wParam, lParam);
    };

    m_WindowClass =
    {
        sizeof(WNDCLASSEX),
        CS_CLASSDC,
        winProc,
        0L,
        0L,
        GetModuleHandle(nullptr),
        LoadIcon(GetModuleHandle(nullptr),
        IDI_APPLICATION),
        LoadCursor(nullptr, IDC_ARROW),
        nullptr,
        nullptr,
        _T("imgui-node-editor-application"),
        LoadIcon(GetModuleHandle(nullptr),
        IDI_APPLICATION)
    };

    if (!RegisterClassEx(&m_WindowClass))
    {
        s_Instance = nullptr;
        return false;
    }

    return true;
}

void PlatformWin32::ApplicationStop()
{
    if (!s_Instance)
        return;

    UnregisterClass(m_WindowClass.lpszClassName, m_WindowClass.hInstance);

    s_Instance = nullptr;
}


bool PlatformWin32::OpenMainWindow(const char* title, int width, int height)
{
    if (m_MainWindowHandle)
        return false;

    const auto windowStyle = WS_OVERLAPPEDWINDOW;
    const auto windowStyleEx = WS_EX_OVERLAPPEDWINDOW;

    RECT rect;
    rect.left = 0;
    rect.top = 0;
    rect.right = width;
    rect.bottom = height;
    AdjustWindowRectEx(&rect, windowStyle, false, windowStyleEx);

    auto windowWidth = rect.right - rect.left;
    auto windowHeight = rect.bottom - rect.top;

    m_MainWindowHandle = CreateWindowEx(
        WS_EX_OVERLAPPEDWINDOW,
        m_WindowClass.lpszClassName,
        Utf8ToNative(title).c_str(),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        width < 0 ? CW_USEDEFAULT : windowWidth,
        height < 0 ? CW_USEDEFAULT : windowHeight,
        nullptr, nullptr, m_WindowClass.hInstance, nullptr);

    if (!m_MainWindowHandle)
        return false;

# if RENDERER(IMGUI_OGL3)
    if (!ImGui_ImplWin32_InitForOpenGL(m_MainWindowHandle))
# else
    if (!ImGui_ImplWin32_Init(m_MainWindowHandle))
# endif
    {
        DestroyWindow(m_MainWindowHandle);
        m_MainWindowHandle = nullptr;
        return false;
    }

    const auto windowScale = ImGui_ImplWin32_GetDpiScaleForHwnd(m_MainWindowHandle);

    RECT clientRect;
    GetClientRect(m_MainWindowHandle, &clientRect);

    POINT origin = { clientRect.left, clientRect.top };
    ClientToScreen(m_MainWindowHandle, &origin);

    // move rect by origin
    clientRect.right  += origin.x - clientRect.left;
    clientRect.bottom += origin.y - clientRect.top;
    clientRect.left    = origin.x;
    clientRect.top     = origin.y;

    if (width >= 0)
        clientRect.right = static_cast<int>((clientRect.right - clientRect.left) * windowScale) + clientRect.left;
    if (height >= 0)
        clientRect.bottom = static_cast<int>((clientRect.bottom - clientRect.top) * windowScale) + clientRect.top;

    RECT windowRect = clientRect;
    AdjustWindowRectEx(&windowRect, windowStyle, false, windowStyleEx);

    SetWindowPos(m_MainWindowHandle, nullptr,
        windowRect.left, windowRect.top,
        windowRect.right - windowRect.left,
        windowRect.bottom - windowRect.top,
        SWP_NOZORDER | SWP_NOACTIVATE);

    SetWindowScale(1.0f / windowScale);
    SetFramebufferScale(windowScale);

    return true;
}

bool PlatformWin32::CloseMainWindow()
{
    if (m_MainWindowHandle == nullptr)
        return true;

    SendMessage(m_MainWindowHandle, WM_CLOSE, 0, 0);

    return m_CanCloseResult;
}

void* PlatformWin32::GetMainWindowHandle() const
{
    return m_MainWindowHandle;
}

void PlatformWin32::SetMainWindowTitle(const char* title)
{
    SetWindowText(m_MainWindowHandle, Utf8ToNative(title).c_str());
}

void PlatformWin32::ShowMainWindow()
{
    if (m_MainWindowHandle == nullptr)
        return;

    //ShowWindow(m_MainWindowHandle, SW_SHOWMAXIMIZED);
    ShowWindow(m_MainWindowHandle, SW_SHOW);
    UpdateWindow(m_MainWindowHandle);
}

bool PlatformWin32::ProcessMainWindowEvents()
{
    if (m_MainWindowHandle == nullptr)
        return false;

    MSG msg = {};
    while (true)
    {
        // While minimized, wait for messages. GetMessage() returns 0 for WM_QUIT.
        if (!m_IsMinimized)
        {
            if (!PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
                break;
        }
        else if (GetMessage(&msg, nullptr, 0U, 0U) <= 0)
            return false;

        if (msg.message == WM_KEYDOWN && (msg.wParam == VK_ESCAPE))
            PostQuitMessage(0);

        if (msg.message == WM_QUIT)
            return false;

        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return true;
}

bool PlatformWin32::IsMainWindowVisible() const
{
    if (m_MainWindowHandle == nullptr)
        return false;

    if (m_IsMinimized)
        return false;

    return true;
}

void PlatformWin32::SetRenderer(Renderer* renderer)
{
    m_Renderer = renderer;
}

void PlatformWin32::NewFrame()
{
    auto& io = ImGui::GetIO();
    auto& ctx = *ImGui::GetCurrentContext();

    ImGui_ImplWin32_NewFrame();
    auto inputEventCountAfterUpdate = ctx.InputEventsQueue.Size;

    auto windowScale = GetWindowScale();

    for (auto& event : ctx.InputEventsQueue)
    {
        if (event.EventId <= m_LastEventId)
            continue;

        m_LastEventId = event.EventId;

        if (event.Type == ImGuiInputEventType_MousePos)
        {
            if (event.MousePos.PosX > -FLT_MAX && event.MousePos.PosY > -FLT_MAX)
            {
                event.MousePos.PosX *= windowScale;
                event.MousePos.PosY *= windowScale;
            }
        }
    }

    if (m_WasMinimized)
    {
        ImGui::GetIO().DeltaTime = 0.1e-6f;
        m_WasMinimized = false;
    }
}

void PlatformWin32::FinishFrame()
{
    if (m_Renderer)
        m_Renderer->Present();
}

void PlatformWin32::Quit()
{
    PostQuitMessage(0);
}

LRESULT PlatformWin32::WinProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return 1;

    switch (msg)
    {
        case WM_CLOSE:
            m_CanCloseResult = m_Application.CanClose();
            if (m_CanCloseResult)
            {
                ImGui_ImplWin32_Shutdown();
                DestroyWindow(hWnd);
            }
            return 0;

        case WM_SIZE:
            if (wParam == SIZE_MINIMIZED)
            {
                m_IsMinimized  = true;
                m_WasMinimized = true;
            }
            else if (wParam == SIZE_RESTORED && m_IsMinimized)
            {
                m_IsMinimized = false;
            }

            if (m_Renderer != nullptr && wParam != SIZE_MINIMIZED)
                m_Renderer->Resize(static_cast<int>(LOWORD(lParam)), static_cast<int>(HIWORD(lParam)));
            return 0;

        case WM_SYSCOMMAND:
            if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
                return 0;
            break;

        case WM_DPICHANGED:
        {
            RECT* const prcNewWindow = (RECT*)lParam;
            SetWindowPos(hWnd,
                NULL,
                prcNewWindow ->left,
                prcNewWindow ->top,
                prcNewWindow->right - prcNewWindow->left,
                prcNewWindow->bottom - prcNewWindow->top,
                SWP_NOZORDER | SWP_NOACTIVATE);

            const auto windowScale = ImGui_ImplWin32_GetDpiScaleForHwnd(hWnd);
            SetWindowScale(1.0f / windowScale);
            SetFramebufferScale(windowScale);
            return 0;
        }

        case WM_DESTROY:
            m_MainWindowHandle = nullptr;
            m_IsMinimized      = false;
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProc(hWnd, msg, wParam, lParam);
}

# endif // BACKEND(IMGUI_WIN32)