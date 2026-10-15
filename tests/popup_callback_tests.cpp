//------------------------------------------------------------------------------
// Popups begun inside a canvas without Suspend()/Resume(), placed on screen by
// ImGuiContext::BeginWindowCallback / EndWindowCallback.
//------------------------------------------------------------------------------
# include "harness.h"
# include <cmath>

namespace {

enum class Site { BetweenNodes, InNode };

const char* c_ViewState = R"({"view":{"scroll":{"x":-150.0,"y":-90.0},"zoom":1.5}})";

ImVec2 s_AnchorScreen;

void Popup(int frame)
{
    ImGui::Button("button");
    const ImVec2 anchor = ImGui::GetItemRectMin(); // canvas space
    s_AnchorScreen = ed::CanvasToScreen(anchor);
    if (frame == 1)
        ImGui::OpenPopup("popup");
    ImGui::SetNextWindowPos(anchor);
    if (ImGui::BeginPopup("popup"))
    {
        ImGui::Text("Popup");
        ImGui::EndPopup();
    }
}

void CheckSite(Site site)
{
# if !defined(IMGUI_HAS_BEGIN_END_WINDOW_CALLBACKS)
    (void)site;
    test::Skip("needs ImGuiContext::BeginWindowCallback / EndWindowCallback");
# else
    test::Storage storage;
    storage.Global = c_ViewState;
    test::Session session(&storage);

    const int assertsBefore = test::AssertCount();
    for (int i = 0; i < 4; ++i)
        session.Frame([&](int frame)
        {
            if (frame == 0)
                ed::SetNodePosition(1, ImVec2(0.0f, 0.0f));
            if (site == Site::BetweenNodes)
                Popup(frame);
            ed::BeginNode(1);
            ImGui::Text("Node A");
            if (site == Site::InNode)
                Popup(frame);
            ed::EndNode();
        });
    const int asserts = test::AssertCount() - assertsBefore;

    ImVec2 popupPos(-1.0f, -1.0f);
    for (ImGuiWindow* window : GImGui->Windows)
        if ((window->Flags & ImGuiWindowFlags_Popup) && window->Active)
            popupPos = window->Pos;

    test::Check(asserts == 0, "no IM_ASSERT failures: %d", asserts);
    // Dear ImGui truncates window positions to whole pixels.
    test::Check(popupPos.x == std::floor(s_AnchorScreen.x) && popupPos.y == std::floor(s_AnchorScreen.y),
        "popup placed on screen at its anchor (%g, %g): (%g, %g)", s_AnchorScreen.x, s_AnchorScreen.y, popupPos.x, popupPos.y);
# endif
}

} // namespace

TEST_CASE(PopupCallbackBetweenNodes, "popup_callback.between_nodes", "#242", test::TestFlags_None)
{
    CheckSite(Site::BetweenNodes);
}

TEST_CASE(PopupCallbackInNode, "popup_callback.in_node", "#242", test::TestFlags_None)
{
    CheckSite(Site::InNode);
}

//------------------------------------------------------------------------------
// The widgets of issues #242 and #322, clicked inside a node at zoom 2.
//------------------------------------------------------------------------------
namespace {

const char* c_Zoom2 = R"({"view":{"scroll":{"x":0.0,"y":0.0},"zoom":2.0}})";

struct ScreenRect { ImVec2 Min, Max; };

ScreenRect LastItemOnScreen()
{
    return { ed::CanvasToScreen(ImGui::GetItemRectMin()), ed::CanvasToScreen(ImGui::GetItemRectMax()) };
}

ImGuiWindow* ActiveWindowWithFlags(ImGuiWindowFlags flags)
{
    for (ImGuiWindow* window : GImGui->Windows)
        if ((window->Flags & flags) == flags && window->Active)
            return window;
    return nullptr;
}

} // namespace

TEST_CASE(PopupCallbackComboLowInWindow, "popup_callback.combo_low_in_window", "#322", test::TestFlags_None)
{
# if !defined(IMGUI_HAS_BEGIN_END_WINDOW_CALLBACKS)
    test::Skip("needs ImGuiContext::BeginWindowCallback / EndWindowCallback");
# else
    test::Storage storage;
    storage.Global = c_Zoom2;
    test::Session session(&storage);

    int        item = 0;
    ScreenRect combo;
    session.DefaultContent = [&](int frame)
    {
        if (frame == 0)
            ed::SetNodePosition(1, ImVec2(100.0f, 230.0f));
        ed::BeginNode(1);
        ImGui::Text("Node");
        ImGui::SetNextItemWidth(100.0f);
        if (ImGui::BeginCombo("##combo", item == 0 ? "First" : "Second"))
        {
            if (ImGui::Selectable("First"))  item = 0;
            if (ImGui::Selectable("Second")) item = 1;
            ImGui::EndCombo();
        }
        else
            combo = LastItemOnScreen();
        ed::EndNode();
    };

    const int assertsBefore = test::AssertCount();
    session.Run(3);
    test::Check(combo.Max.y > session.HostContext.DisplaySize.y * 0.6f, "the combo is low in the window: bottom at %g", combo.Max.y);
    session.Click((combo.Min + combo.Max) * 0.5f);
    session.Run(2);

    ImGuiWindow* popup = ActiveWindowWithFlags(ImGuiWindowFlags_Popup);
    test::Check(popup != nullptr, "the combo's popup is open");
    if (popup)
        test::Check(popup->Pos.x == std::floor(combo.Min.x) && popup->Pos.y == std::floor(combo.Max.y),
            "the popup opens below its combo, with no gap (#322): at (%g, %g), the combo's bottom left at (%g, %g)",
            popup->Pos.x, popup->Pos.y, combo.Min.x, combo.Max.y);
    test::Check(test::AssertCount() == assertsBefore, "no IM_ASSERT failures: %d", test::AssertCount() - assertsBefore);
# endif
}

TEST_CASE(PopupCallbackColorPickerInNode, "popup_callback.color_picker_in_node", "#242", test::TestFlags_None)
{
# if !defined(IMGUI_HAS_BEGIN_END_WINDOW_CALLBACKS)
    test::Skip("needs ImGuiContext::BeginWindowCallback / EndWindowCallback");
# else
    test::Storage storage;
    storage.Global = c_Zoom2;
    test::Session session(&storage);

    float      color[4] = { 0.2f, 0.6f, 0.9f, 1.0f };
    ScreenRect button;
    session.DefaultContent = [&](int frame)
    {
        if (frame == 0)
            ed::SetNodePosition(1, ImVec2(100.0f, 100.0f));
        ed::BeginNode(1);
        ImGui::Text("Node");
        ImGui::ColorEdit4("##color", color, ImGuiColorEditFlags_NoInputs);
        if (!ImGui::IsPopupOpen("##color/picker", ImGuiPopupFlags_AnyPopupId))
            button = LastItemOnScreen();
        ed::EndNode();
    };

    const int assertsBefore = test::AssertCount();
    session.Run(3);
    session.Click((button.Min + button.Max) * 0.5f);
    session.Run(2);

    ImGuiWindow* picker = ActiveWindowWithFlags(ImGuiWindowFlags_Popup);
    test::Check(picker != nullptr, "the color picker is open");
    if (picker)
    {
        // ColorEdit4() places it at the button's bottom left, one ItemSpacing.y lower (in canvas units, so twice that on screen)
        const float gap = picker->Pos.y - button.Max.y;
        test::Check(picker->Pos.x == std::floor(button.Min.x) && gap >= 0.0f && gap <= ImGui::GetStyle().ItemSpacing.y * 2.0f + 1.0f,
            "the picker opens under its button, on screen (#242): at (%g, %g), the button's bottom left at (%g, %g)",
            picker->Pos.x, picker->Pos.y, button.Min.x, button.Max.y);
    }
    test::Check(test::AssertCount() == assertsBefore, "no IM_ASSERT failures: %d", test::AssertCount() - assertsBefore);
# endif
}

TEST_CASE(PopupCallbackTooltipInNode, "popup_callback.tooltip_in_node", "#242", test::TestFlags_None)
{
# if !defined(IMGUI_HAS_BEGIN_END_WINDOW_CALLBACKS)
    test::Skip("needs ImGuiContext::BeginWindowCallback / EndWindowCallback");
# else
    test::Storage storage;
    storage.Global = c_Zoom2;
    test::Session session(&storage);

    ScreenRect button;
    session.DefaultContent = [&](int frame)
    {
        if (frame == 0)
            ed::SetNodePosition(1, ImVec2(100.0f, 100.0f));
        ed::BeginNode(1);
        ImGui::Button("Hover me");
        button = LastItemOnScreen();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Tooltip");
        ed::EndNode();
    };

    const int assertsBefore = test::AssertCount();
    session.Run(3);
    const ImVec2 mouse = (button.Min + button.Max) * 0.5f;
    test::MouseMove(mouse);
    session.Run(3);

    ImGuiWindow* tooltip = ActiveWindowWithFlags(ImGuiWindowFlags_Tooltip);
    test::Check(tooltip != nullptr, "the tooltip is shown");
    if (tooltip)
        test::Check(tooltip->Pos.x >= mouse.x && tooltip->Pos.x <= mouse.x + 50.0f && tooltip->Pos.y >= mouse.y && tooltip->Pos.y <= mouse.y + 50.0f,
            "the tooltip is next to the mouse on screen: at (%g, %g), the mouse at (%g, %g)", tooltip->Pos.x, tooltip->Pos.y, mouse.x, mouse.y);
    test::Check(test::AssertCount() == assertsBefore, "no IM_ASSERT failures: %d", test::AssertCount() - assertsBefore);
# endif
}

TEST_CASE(PopupCallbackChildWindowAsserts, "popup_callback.child_window_asserts", "#242", test::TestFlags_None)
{
# if !defined(IMGUI_HAS_BEGIN_END_WINDOW_CALLBACKS)
    test::Skip("needs ImGuiContext::BeginWindowCallback / EndWindowCallback");
# else
    test::Storage storage;
    test::Session session(&storage);

    int childBegins = 0;
    session.DefaultContent = [&](int frame)
    {
        if (frame == 0)
            ed::SetNodePosition(1, ImVec2(100.0f, 100.0f));
        ed::BeginNode(1);
        ImGui::Text("Node");
        ImGui::BeginChild("child", ImVec2(100.0f, 50.0f));
        ++childBegins;
        ImGui::Text("Child");
        ImGui::EndChild();
        ed::EndNode();
    };

    const int assertsBefore = test::AssertCount();
    session.Run(3);
    const int asserts = test::AssertCount() - assertsBefore;
    test::ExpectAsserts(childBegins);
    test::Check(asserts == childBegins, "a child window begun inside a node asserts each time: %d asserts for %d BeginChild()", asserts, childBegins);
# endif
}
