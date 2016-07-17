
#if defined(_DEBUG)
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif
#include <stdlib.h>

#include "Backend/ImwWindowManagerDX11.h"
#include "ImGui/imgui.h"

#include "NodeWindow.h"

//#define CONSOLE

#ifndef CONSOLE
#include <Windows.h>
#endif

#ifdef _WIN32
#define ImwNewline "\r\n"
#else
#define ImwNewline "\n"
#endif

using namespace ImWindow;

class MyImwWindow3 : public ImwWindow, ImwMenu
{
public:
	MyImwWindow3(const char* pTitle = "MyImwWindow3")
	{
		SetTitle(pTitle);
	}
	virtual void OnGui()
	{
		ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);

		if (ImGui::Button("Close"))
		{
			Destroy();
		}
	}

	virtual void OnMenu()
	{
		if (ImGui::BeginMenu("MyImwWindow3 menu"))
		{
			if (ImGui::MenuItem("Close it"))
			{
				Destroy();
			}
			ImGui::EndMenu();
		}
	}
};

class MyImwWindow2 : public ImwWindow, ImwMenu
{
public:
	MyImwWindow2(const char* pTitle = "MyImwWindow2")
	{
		SetTitle(pTitle);
	}
	virtual void OnGui()
	{
		static float f = 0.0f;
		ImGui::SliderFloat("float", &f, 0.0f, 1.0f);
		/*ImGui::ColorEdit3("clear color", (float*)&clear_col);
		if (ImGui::Button("Test Window")) show_test_window ^= 1;
		if (ImGui::Button("Another Window")) show_another_window ^= 1;*/
		ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
	}

	virtual void OnMenu()
	{
		/*if (ImGui::BeginMenu("Mon menu"))
		{
			ImGui::EndMenu();
		}*/
	}
};

class MyImwWindow : public ImwWindow
{
public:
	MyImwWindow(const char* pTitle = "MyImwWindow")
	{
		SetTitle(pTitle);
	}
	virtual void OnGui()
	{
		ImGui::Text("Hello, world!");

		ImGui::Checkbox("Use ImGui colors", &(ImwWindowManager::GetInstance()->GetConfig().m_bTabUseImGuiColors));
		if (ImGui::Button("Create"))
		{
			new MyImwWindow3();
		}
	}
};

class StyleEditorWindow : public ImwWindow
{
public:
	StyleEditorWindow()
	{
		SetTitle("Style Editor");
	}
	virtual void OnGui()
	{
		ImGuiStyle* ref = NULL;

		ImGuiStyle& style = ImGui::GetStyle();

		const ImGuiStyle def; // Default style
		if (ImGui::Button("Revert Style"))
			style =  def;
		if (ref)
		{
			ImGui::SameLine();
			if (ImGui::Button("Save Style"))
				*ref = style;
		}

		ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.55f);

		if (ImGui::TreeNode("Rendering"))
		{
			ImGui::Checkbox("Anti-aliased lines", &style.AntiAliasedLines);
			ImGui::Checkbox("Anti-aliased shapes", &style.AntiAliasedShapes);
			ImGui::PushItemWidth(100);
			ImGui::DragFloat("Curve Tessellation Tolerance", &style.CurveTessellationTol, 0.02f, 0.10f, FLT_MAX, NULL, 2.0f);
			if (style.CurveTessellationTol < 0.0f) style.CurveTessellationTol = 0.10f;
			ImGui::PopItemWidth();
			ImGui::TreePop();
		}

		if (ImGui::TreeNode("Sizes"))
		{
			ImGui::SliderFloat("Alpha", &style.Alpha, 0.20f, 1.0f, "%.2f");                 // Not exposing zero here so user doesn't "lose" the UI. But application code could have a toggle to switch between zero and non-zero.
			ImGui::SliderFloat2("WindowPadding", (float*)&style.WindowPadding, 0.0f, 20.0f, "%.0f");
			ImGui::SliderFloat("WindowRounding", &style.WindowRounding, 0.0f, 16.0f, "%.0f");
			ImGui::SliderFloat("ChildWindowRounding", &style.ChildWindowRounding, 0.0f, 16.0f, "%.0f");
			ImGui::SliderFloat2("FramePadding", (float*)&style.FramePadding, 0.0f, 20.0f, "%.0f");
			ImGui::SliderFloat("FrameRounding", &style.FrameRounding, 0.0f, 16.0f, "%.0f");
			ImGui::SliderFloat2("ItemSpacing", (float*)&style.ItemSpacing, 0.0f, 20.0f, "%.0f");
			ImGui::SliderFloat2("ItemInnerSpacing", (float*)&style.ItemInnerSpacing, 0.0f, 20.0f, "%.0f");
			ImGui::SliderFloat2("TouchExtraPadding", (float*)&style.TouchExtraPadding, 0.0f, 10.0f, "%.0f");
			ImGui::SliderFloat("IndentSpacing", &style.IndentSpacing, 0.0f, 30.0f, "%.0f");
			ImGui::SliderFloat("ScrollbarWidth", &style.ScrollbarSize, 1.0f, 20.0f, "%.0f");
			ImGui::SliderFloat("ScrollbarRounding", &style.ScrollbarRounding, 0.0f, 16.0f, "%.0f");
			ImGui::SliderFloat("GrabMinSize", &style.GrabMinSize, 1.0f, 20.0f, "%.0f");
			ImGui::SliderFloat("GrabRounding", &style.GrabRounding, 0.0f, 16.0f, "%.0f");
			ImGui::TreePop();
		}

		if (ImGui::TreeNode("Colors"))
		{
			static int output_dest = 0;
			static bool output_only_modified = false;
			if (ImGui::Button("Output Colors"))
			{
				if (output_dest == 0)
					ImGui::LogToClipboard();
				else
					ImGui::LogToTTY();
				ImGui::LogText("ImGuiStyle& style = ImGui::GetStyle();" ImwNewline);
				for (int i = 0; i < ImGuiCol_COUNT; i++)
				{
					const ImVec4& col = style.Colors[i];
					const char* name = ImGui::GetStyleColName(i);
					if (!output_only_modified || memcmp(&col, (ref ? &ref->Colors[i] : &def.Colors[i]), sizeof(ImVec4)) != 0)
						ImGui::LogText("style.Colors[ImGuiCol_%s]%*s= ImVec4(%.2ff, %.2ff, %.2ff, %.2ff);" ImwNewline, name, 22 - (int)strlen(name), "", col.x, col.y, col.z, col.w);
				}
				ImGui::LogFinish();
			}
			ImGui::SameLine(); ImGui::PushItemWidth(120); ImGui::Combo("##output_type", &output_dest, "To Clipboard\0To TTY"); ImGui::PopItemWidth();
			ImGui::SameLine(); ImGui::Checkbox("Only Modified Fields", &output_only_modified);

			static ImGuiColorEditMode edit_mode = ImGuiColorEditMode_RGB;
			ImGui::RadioButton("RGB", &edit_mode, ImGuiColorEditMode_RGB);
			ImGui::SameLine();
			ImGui::RadioButton("HSV", &edit_mode, ImGuiColorEditMode_HSV);
			ImGui::SameLine();
			ImGui::RadioButton("HEX", &edit_mode, ImGuiColorEditMode_HEX);
			//ImGui::Text("Tip: Click on colored square to change edit mode.");

			static ImGuiTextFilter filter;
			filter.Draw("Filter colors", 200);

			ImGui::BeginChild("#colors", ImVec2(0, 300), true);
			ImGui::PushItemWidth(-160);
			ImGui::ColorEditMode(edit_mode);
			for (int i = 0; i < ImGuiCol_COUNT; i++)
			{
				const char* name = ImGui::GetStyleColName(i);
				if (!filter.PassFilter(name))
					continue;
				ImGui::PushID(i);
				ImGui::ColorEdit4(name, (float*)&style.Colors[i], true);
				if (memcmp(&style.Colors[i], (ref ? &ref->Colors[i] : &def.Colors[i]), sizeof(ImVec4)) != 0)
				{
					ImGui::SameLine(); if (ImGui::Button("Revert")) style.Colors[i] = ref ? ref->Colors[i] : def.Colors[i];
					if (ref) { ImGui::SameLine(); if (ImGui::Button("Save")) ref->Colors[i] = style.Colors[i]; }
				}
				ImGui::PopID();
			}
			ImGui::PopItemWidth();
			ImGui::EndChild();

			ImGui::TreePop();
		}

		ImGui::PopItemWidth();
	}
};

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nShowCmd)
//int main(int argc, char* argv[])
{
# if defined(_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
# endif

	ImwWindowManagerDX11 oMgr;

	oMgr.Init();

	//oMgr.GetMainPlatformWindow()->SetPos(2000,100);
	oMgr.SetMainTitle("ImWindow Test");

	ImwWindow* pWindow1 = new MyImwWindow();

	ImwWindow* pWindow2 = new MyImwWindow2();

	ImwWindow* pWindow3 = new MyImwWindow2("MyImwWindow2(2)");
	ImwWindow* pWindow4 = new MyImwWindow2("MyImwWindow2(3)");
	ImwWindow* pWindow5 = new MyImwWindow3();
	pWindow5->SetClosable(false);

	//ImwWindow* pStyleEditor = new StyleEditorWindow();

	//ImwWindow* pNodeWindow = new NodeWindow();

	oMgr.Dock(pWindow1);
	oMgr.Dock(pWindow2, E_DOCK_ORIENTATION_LEFT);
	oMgr.DockWith(pWindow3, pWindow2, E_DOCK_ORIENTATION_TOP);
	oMgr.DockWith(pWindow4, pWindow3, E_DOCK_ORIENTATION_LEFT);
	oMgr.DockWith(pWindow5, pWindow4, E_DOCK_ORIENTATION_CENTER);

	//oMgr.Dock(pNodeWindow, E_DOCK_ORIENTATION_LEFT);
	//oMgr.Dock(pStyleEditor, E_DOCK_ORIENTATION_LEFT);

	//oMgr.Dock
	//MyImwWindow* pWindow2 = new MyImwWindow(pWindow1);
	//pWindow2->SetSize(300, 200);
	//pWindow2->Show();*/

	while (oMgr.Run());

	ImGui::Shutdown();

	return 0;
}
