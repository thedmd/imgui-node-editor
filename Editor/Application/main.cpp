#if defined(_DEBUG)
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif
#include "imgui/imgui.h"
#include "imgui_impl_dx11.h"
#include "Support/scope_guard.h"
#include "Application/application.h"
#include <d3d11.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <tchar.h>
#include <string>

// Data
static ID3D11Device*            g_pd3dDevice = nullptr;
static ID3D11DeviceContext*     g_pd3dDeviceContext = nullptr;
static IDXGISwapChain*          g_pSwapChain = nullptr;
static ID3D11RenderTargetView*  g_mainRenderTargetView = nullptr;

static void CreateRenderTarget()
{
    DXGI_SWAP_CHAIN_DESC sd;
    g_pSwapChain->GetDesc(&sd);

    // Create the render target
    ID3D11Texture2D* pBackBuffer;
    D3D11_RENDER_TARGET_VIEW_DESC render_target_view_desc;
    ZeroMemory(&render_target_view_desc, sizeof(render_target_view_desc));
    render_target_view_desc.Format = sd.BufferDesc.Format;
    render_target_view_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    g_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, &render_target_view_desc, &g_mainRenderTargetView);
    g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
    pBackBuffer->Release();
}

static void CleanupRenderTarget()
{
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

static HRESULT CreateDeviceD3D(HWND hWnd)
{
    // Setup swap chain
    DXGI_SWAP_CHAIN_DESC sd;
    {
        ZeroMemory(&sd, sizeof(sd));
        sd.BufferCount = 2;
        sd.BufferDesc.Width = 0;
        sd.BufferDesc.Height = 0;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferDesc.RefreshRate.Numerator = 60;
        sd.BufferDesc.RefreshRate.Denominator = 1;
        sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = hWnd;
        sd.SampleDesc.Count = 1;
        sd.SampleDesc.Quality = 0;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    }

    UINT createDeviceFlags = 0;
    //createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[1] = { D3D_FEATURE_LEVEL_11_0, };
    if (D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 1, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext) != S_OK)
        return E_FAIL;

    CreateRenderTarget();

    return S_OK;
}

static void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

extern LRESULT ImGui_ImplDX11_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT WINAPI ImGui_WinProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplDX11_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
        case WM_SIZE:
            if (g_pd3dDevice != nullptr && wParam != SIZE_MINIMIZED)
            {
                ImGui_ImplDX11_InvalidateDeviceObjects();
                CleanupRenderTarget();
                g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
                CreateRenderTarget();
                ImGui_ImplDX11_CreateDeviceObjects();
            }
            return 0;
        case WM_SYSCOMMAND:
            if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
                return 0;
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

static ImFont* ImGui_LoadFont(ImGuiIO& io, const char* name, float size, const ImVec2& displayOffset = ImVec2(0, 0))
{
    auto windir = getenv("WINDIR");
    if (!windir)
        return nullptr;

    auto path = std::string(windir) + "\\Fonts\\" + name;
    auto font = io.Fonts->AddFontFromFileTTF(path.c_str(), size);
    if (font)
        font->DisplayOffset = displayOffset;

    return font;
}

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nShowCmd)
{
    const auto c_ClassName  = _T("Node Editor Class");
    const auto c_WindowName = _T("Node Editor");

# if defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
# endif
    // Create application window
    const auto wc = WNDCLASSEX{ sizeof(WNDCLASSEX), CS_CLASSDC, ImGui_WinProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr, c_ClassName, nullptr };
    RegisterClassEx(&wc); AX_SCOPE_EXIT { UnregisterClass(c_ClassName, wc.hInstance) ; };

    auto hwnd = CreateWindow(c_ClassName, c_WindowName, WS_OVERLAPPEDWINDOW, 100, 100, 1440, 800, nullptr, nullptr, wc.hInstance, nullptr);

    // Initialize Direct3D
    AX_SCOPE_EXIT{ CleanupDeviceD3D(); };
    if (CreateDeviceD3D(hwnd) < 0)
        return 1;

    // Setup ImGui binding
    ImGui_ImplDX11_Init(hwnd, g_pd3dDevice, g_pd3dDeviceContext);
    AX_SCOPE_EXIT{ ImGui_ImplDX11_Shutdown(); };

    // Load Fonts
    ImGuiIO& io = ImGui::GetIO();
    ImGui_LoadFont(io, "segoeui.ttf", 16 * 96.0f / 72.0f, ImVec2(0, -1));

    ImGuiStyle& style = ImGui::GetStyle();
    style.FrameRounding     = 2.0f;
    style.WindowRounding    = 0.0f;
    style.ScrollbarRounding = 0.0f;

    ImVec4 backgroundColor = ImColor(32, 32, 32, 255);//style.Colors[ImGuiCol_TitleBg];

    Application_Initialize();
    AX_SCOPE_EXIT { Application_Finalize(); };

    auto frame = [&]()
    {
        ImGui_ImplDX11_NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Content", nullptr, ImVec2(0, 0), 0.0f,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBringToFrontOnFocus);

        Application_Frame();

        ImGui::End();


        //ImGui::ShowMetricsWindow();

        //static bool show = true;
        //ImGui::ShowTestWindow(&show);


        // Rendering
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, (float*)&backgroundColor);
        ImGui::Render();
        g_pSwapChain->Present(0, 0);
    };

    frame();

    ShowWindow(hwnd, SW_SHOWMAXIMIZED);
    UpdateWindow(hwnd);

    // Main loop
    MSG msg = {};
    while (msg.message != WM_QUIT)
    {
        if (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            if (msg.message == WM_KEYDOWN && (msg.wParam == VK_ESCAPE))
                PostQuitMessage(0);

            TranslateMessage(&msg);
            DispatchMessage(&msg);
            continue;
        }

        frame();
    }

    return 0;
}

//#if defined(_DEBUG)
//#define _CRTDBG_MAP_ALLOC
//#include <crtdbg.h>
//#endif
//#include <stdlib.h>
//
//#
//
//#include "Backend/ImwWindowManagerDX11.h"
//#include "ImGui/imgui.h"
//
//#include "Window.h"
//
////#define CONSOLE
//
//#ifndef CONSOLE
//#include <Windows.h>
//#endif
//
//#ifdef _WIN32
//#define ImwNewline "\r\n"
//#else
//#define ImwNewline "\n"
//#endif
//
////using namespace ImWindow;
////
////class MyImwWindow3: public ImwWindow, ImwMenu
////{
////public:
////    MyImwWindow3(const char* pTitle = "MyImwWindow3")
////    {
////        SetTitle(pTitle);
////    }
////    virtual void OnGui()
////    {
////        ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
////
////        if (ImGui::Button("Close"))
////        {
////            Destroy();
////        }
////    }
////
////    virtual void OnMenu()
////    {
////        if (ImGui::BeginMenu("MyImwWindow3 menu"))
////        {
////            if (ImGui::MenuItem("Close it"))
////            {
////                Destroy();
////            }
////            ImGui::EndMenu();
////        }
////    }
////};
////
////class MyImwWindow2: public ImwWindow, ImwMenu
////{
////public:
////    MyImwWindow2(const char* pTitle = "MyImwWindow2")
////    {
////        SetTitle(pTitle);
////    }
////    virtual void OnGui()
////    {
////        static float f = 0.0f;
////        ImGui::SliderFloat("float", &f, 0.0f, 1.0f);
////        /*ImGui::ColorEdit3("clear color", (float*)&clear_col);
////        if (ImGui::Button("Test Window")) show_test_window ^= 1;
////        if (ImGui::Button("Another Window")) show_another_window ^= 1;*/
////        ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
////    }
////
////    virtual void OnMenu()
////    {
////        /*if (ImGui::BeginMenu("Mon menu"))
////        {
////            ImGui::EndMenu();
////        }*/
////    }
////};
////
////class MyImwWindow: public ImwWindow
////{
////public:
////    MyImwWindow(const char* pTitle = "MyImwWindow")
////    {
////        SetTitle(pTitle);
////    }
////    virtual void OnGui()
////    {
////        ImGui::Text("Hello, world!");
////
////        ImGui::Checkbox("Use ImGui colors", &(ImwWindowManager::GetInstance()->GetConfig().m_bTabUseImGuiColors));
////        if (ImGui::Button("Create"))
////        {
////            new MyImwWindow3();
////        }
////    }
////};
////
////class StyleEditorWindow: public ImwWindow
////{
////public:
////    StyleEditorWindow()
////    {
////        SetTitle("Style Editor");
////    }
////    virtual void OnGui()
////    {
////        ImGuiStyle* ref = nullptr;
////
////        ImGuiStyle& style = ImGui::GetStyle();
////
////        const ImGuiStyle def; // Default style
////        if (ImGui::Button("Revert Style"))
////            style = def;
////        if (ref)
////        {
////            ImGui::SameLine();if (ImGui::Button("Save Style"))
////                *ref = style;
////        }
////
////        ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.55f);
////
////        if (ImGui::TreeNode("Rendering"))
////        {
////            ImGui::Checkbox("Anti-aliased lines", &style.AntiAliasedLines);
////            ImGui::Checkbox("Anti-aliased shapes", &style.AntiAliasedShapes);
////            ImGui::PushItemWidth(100);
////            ImGui::DragFloat("Curve Tessellation Tolerance", &style.CurveTessellationTol, 0.02f, 0.10f, FLT_MAX, nullptr, 2.0f);
////            if (style.CurveTessellationTol < 0.0f) style.CurveTessellationTol = 0.10f;
////            ImGui::PopItemWidth();
////            ImGui::TreePop();
////        }
////
////        if (ImGui::TreeNode("Sizes"))
////        {
////            ImGui::SliderFloat("Alpha", &style.Alpha, 0.20f, 1.0f, "%.2f");                 // Not exposing zero here so user doesn't "lose" the UI. But application code could have a toggle to switch between zero and non-zero.
////            ImGui::SliderFloat2("WindowPadding", (float*)&style.WindowPadding, 0.0f, 20.0f, "%.0f");
////            ImGui::SliderFloat("WindowRounding", &style.WindowRounding, 0.0f, 16.0f, "%.0f");
////            ImGui::SliderFloat("ChildWindowRounding", &style.ChildWindowRounding, 0.0f, 16.0f, "%.0f");
////            ImGui::SliderFloat2("FramePadding", (float*)&style.FramePadding, 0.0f, 20.0f, "%.0f");
////            ImGui::SliderFloat("FrameRounding", &style.FrameRounding, 0.0f, 16.0f, "%.0f");
////            ImGui::SliderFloat2("ItemSpacing", (float*)&style.ItemSpacing, 0.0f, 20.0f, "%.0f");
////            ImGui::SliderFloat2("ItemInnerSpacing", (float*)&style.ItemInnerSpacing, 0.0f, 20.0f, "%.0f");
////            ImGui::SliderFloat2("TouchExtraPadding", (float*)&style.TouchExtraPadding, 0.0f, 10.0f, "%.0f");
////            ImGui::SliderFloat("IndentSpacing", &style.IndentSpacing, 0.0f, 30.0f, "%.0f");
////            ImGui::SliderFloat("ScrollbarWidth", &style.ScrollbarSize, 1.0f, 20.0f, "%.0f");
////            ImGui::SliderFloat("ScrollbarRounding", &style.ScrollbarRounding, 0.0f, 16.0f, "%.0f");
////            ImGui::SliderFloat("GrabMinSize", &style.GrabMinSize, 1.0f, 20.0f, "%.0f");
////            ImGui::SliderFloat("GrabRounding", &style.GrabRounding, 0.0f, 16.0f, "%.0f");
////            ImGui::TreePop();
////        }
////
////        if (ImGui::TreeNode("Colors"))
////        {
////            static int output_dest = 0;
////            static bool output_only_modified = false;
////            if (ImGui::Button("Output Colors"))
////            {
////                if (output_dest == 0)
////                    ImGui::LogToClipboard();
////                else
////                    ImGui::LogToTTY();
////                ImGui::LogText("ImGuiStyle& style = ImGui::GetStyle();" ImwNewline);
////                for (int i = 0; i < ImGuiCol_COUNT; i++)
////                {
////                    const ImVec4& col = style.Colors[i];
////                    const char* name = ImGui::GetStyleColName(i);
////                    if (!output_only_modified || memcmp(&col, (ref ? &ref->Colors[i] : &def.Colors[i]), sizeof(ImVec4)) != 0)
////                        ImGui::LogText("style.Colors[ImGuiCol_%s]%*s= ImVec4(%.2ff, %.2ff, %.2ff, %.2ff);" ImwNewline, name, 22 - (int)strlen(name), "", col.x, col.y, col.z, col.w);
////                }
////                ImGui::LogFinish();
////            }
////            ImGui::SameLine(); ImGui::PushItemWidth(120); ImGui::Combo("##output_type", &output_dest, "To Clipboard\0To TTY"); ImGui::PopItemWidth();
////            ImGui::SameLine(); ImGui::Checkbox("Only Modified Fields", &output_only_modified);
////
////            static ImGuiColorEditMode edit_mode = ImGuiColorEditMode_RGB;
////            ImGui::RadioButton("RGB", &edit_mode, ImGuiColorEditMode_RGB);
////            ImGui::SameLine();
////            ImGui::RadioButton("HSV", &edit_mode, ImGuiColorEditMode_HSV);
////            ImGui::SameLine();
////            ImGui::RadioButton("HEX", &edit_mode, ImGuiColorEditMode_HEX);
////            //ImGui::Text("Tip: Click on colored square to change edit mode.");
////
////            static ImGuiTextFilter filter;
////            filter.Draw("Filter colors", 200);
////
////            ImGui::BeginChild("#colors", ImVec2(0, 300), true);
////            ImGui::PushItemWidth(-160);
////            ImGui::ColorEditMode(edit_mode);
////            for (int i = 0; i < ImGuiCol_COUNT; i++)
////            {
////                const char* name = ImGui::GetStyleColName(i);
////                if (!filter.PassFilter(name))
////                    continue;
////                ImGui::PushID(i);
////                ImGui::ColorEdit4(name, (float*)&style.Colors[i], true);
////                if (memcmp(&style.Colors[i], (ref ? &ref->Colors[i] : &def.Colors[i]), sizeof(ImVec4)) != 0)
////                {
////                    ImGui::SameLine(); if (ImGui::Button("Revert")) style.Colors[i] = ref ? ref->Colors[i] : def.Colors[i];
////                    if (ref) { ImGui::SameLine(); if (ImGui::Button("Save")) ref->Colors[i] = style.Colors[i]; }
////                }
////                ImGui::PopID();
////            }
////            ImGui::PopItemWidth();
////            ImGui::EndChild();
////
////            ImGui::TreePop();
////        }
////
////        ImGui::PopItemWidth();
////    }
////};
//
//int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nShowCmd)
////int main(int argc, char* argv[])
//{
//# if defined(_DEBUG)
//    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
//# endif
//
//    using namespace ImWindow;
//
//    ImwWindowManagerDX11 windowManager;
//    if (!windowManager.Init())
//        return 1;
//
//    windowManager.SetMainTitle("ImGui Test");
//
//    auto editor = new NodeWindow();
//    windowManager.Dock(editor);
//
//    while (windowManager.Run())
//        ;
//
//    return 0;
//}