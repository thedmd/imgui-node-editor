#include "NodeEditorInternal.h"
#include <algorithm>

namespace ed = ax::NodeEditor;

ed::LayoutItem::LayoutItem(LayoutItemType type):
    Type(type),
    SpringWeight(1.0f),
    SpringSize(0)
{
}

ed::Layout::Layout(LayoutType type):
    Type(type), CurrentItem(0), Editing(false), Modified(false)
{
}

void ed::Layout::Begin()
{
    assert(false == Editing);
    Editing = true;

    // Whole layout group
    BeginGroup();

    CurrentItem = 0;

    // First item group
    BeginWidget();
}

void ed::Layout::AddSeparator(float weight/* = 1.0f*/)
{
    assert(Editing);

    EndWidget();
    //ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImColor(255, 255, 0, 180));

    PushSpring(std::max(0.0f, weight));

    // Start next item
    if (Type == LayoutType::Horizontal)
        ImGui::SameLine();

    BeginWidget();
}

void ed::Layout::End()
{
    assert(Editing);

    EndWidget();

    // Finish layout group
    EndGroup();
    ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImColor(0, 255, 0, 64));

    auto measuredBounds = get_item_bounds();
    if (Size != measuredBounds.size)
    {
        Size = measuredBounds.size;
        MakeDirty();
    }

    Editing = false;
}

int ed::Layout::ItemCount() const
{
    return static_cast<int>(Items.size());
}

void ed::Layout::MakeDirty()
{
    Modified = true;
}

void ed::Layout::BeginWidget()
{
    BeginGroup();

    if (CurrentItem < ItemCount() && Items[CurrentItem].Type == LayoutItemType::Widget)
    {
        auto& item = Items[CurrentItem];

        const auto isHorizontal = (Type == LayoutType::Horizontal);

        const auto freeSpace = isHorizontal ? (Size.h - item.Size.h) : (Size.w - item.Size.w);
        if (freeSpace != 0)
        {
            const auto align = freeSpace / 2;

            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
            if (isHorizontal)
            {
                ImGui::GetWindowDrawList()->AddRectFilled(
                    ImGui::GetCursorScreenPos() + ImVec2(0, static_cast<float>(align)),
                    ImGui::GetCursorScreenPos() + ImVec2(0, static_cast<float>(align)) + to_imvec(item.Size),
                    ImColor(255, 255, 0, 128));

//                 auto cursor = ImGui::GetCursorScreenPos();
//                 cursor.y += align;
//                 ImGui::SetCursorScreenPos(cursor);

                //ImGui::NewLine();
                //ImGui::Spacing();
                //ImGui::SameLine();

                //ImGui::SameLine();
                //ImGui::Dummy(ImVec2(0, static_cast<float>(align)));
            }
            else
            {
                //ImGui::SameLine();
                //ImGui::Dummy(ImVec2(static_cast<float>(align), 0));
            }
            ImGui::PopStyleVar();
        }
    }

    BeginGroup();
}

void ed::Layout::EndWidget()
{
    EndGroup();

    //ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImColor(255, 255, 0, 128));

    auto size = to_size(ImGui::GetItemRectSize());

    EndGroup();

    if (CurrentItem == ItemCount())
    {
        Items.push_back(LayoutItem(LayoutItemType::Widget));

        MakeDirty(); // increasing number of items
    }

    auto& item = Items[CurrentItem];
    if (item.Size != size)
    {
        item.Size = size;

        MakeDirty(); // bounds changed
    }

    ++CurrentItem;
}

void ed::Layout::PushSpring(float weight)
{
    if (CurrentItem == ItemCount())
    {
        Items.push_back(LayoutItem(LayoutItemType::Spring));

        MakeDirty(); // increasing number of items
    }

    auto& item = Items[CurrentItem];
    if (item.SpringWeight != weight)
    {
        item.SpringWeight = weight;

        MakeDirty(); // spring weight changed
    }

    if (item.SpringSize > 0)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        if (Type == LayoutType::Horizontal)
        {
            ImGui::SameLine();
            //ImGui::GetWindowDrawList()->AddRectFilled(
            //    ImGui::GetCursorScreenPos(),
            //    ImGui::GetCursorScreenPos() + ImVec2(static_cast<float>(item.SpringSize), 100),
            //    ImColor(255, 0, 0, 64));
            ImGui::Dummy(ImVec2(static_cast<float>(item.SpringSize), 0));
        }
        else
        {
            //ImGui::GetWindowDrawList()->AddRectFilled(
            //    ImGui::GetCursorScreenPos(),
            //    ImGui::GetCursorScreenPos() + ImVec2(100, static_cast<float>(item.SpringSize)),
            //    ImColor(255, 0, 0, 64));
            ImGui::Dummy(ImVec2(0, static_cast<float>(item.SpringSize)));
        }
        ImGui::PopStyleVar();
    }

    ++CurrentItem;
}

void ed::Layout::CenterWidget()
{
}

void ed::Layout::BeginGroup()
{
    //if (Type == LayoutType::Horizontal)
    //    ImGui::SameLine();
    //ImGui::Dummy(ImVec2(0, 0));
    //if (Type == LayoutType::Horizontal)
    //    ImGui::SameLine();

    ImGui::BeginGroup();
    //ImGui::Dummy(ImVec2(0, 0));
    //if (Type == LayoutType::Horizontal)
    //    ImGui::SameLine();
}

void ed::Layout::EndGroup()
{
    //if (Type == LayoutType::Horizontal)
    //    ImGui::SameLine();
    //ImGui::Dummy(ImVec2(0, 0));
    //ImGui::GetCurrentWindow()->DC.GroupStack.back().AdvanceCursor = false;
    ImGui::EndGroup();
}

bool ed::Layout::Build(const ax::size& size)
{
    assert(!Editing);

    const auto isHorizontal = (Type == LayoutType::Horizontal);

    // Horizontal layout span horizontally to input bounds
    auto newSize = ax::size(
         isHorizontal ? size.w : Size.w,
        !isHorizontal ? size.h : Size.h);

    if (newSize != Size)
        MakeDirty();

    if (!Modified)
        return false;

    Size = newSize;

    // There is a free space, so springs can be expanded
    auto totalSize   = 0;
    auto totalWeight = 0.0f;
    auto widgetCount = 0;
    for (auto& item : Items)
    {
        if (item.Type == LayoutItemType::Widget)
        {
            totalSize += isHorizontal ? item.Size.w : item.Size.h;
            ++widgetCount;
        }
        else
            totalWeight += item.SpringWeight;
    }

    if (widgetCount == 0)
        return false;

    auto widgetPadding = ImGui::GetStyle().ItemSpacing;
    totalSize += static_cast<int>((isHorizontal ? widgetPadding.x : widgetPadding.y) * (widgetCount - 1));

    auto freeSpace = (isHorizontal ? size.w : size.h) - totalSize;

    // Distribute free space
    if (freeSpace != 0 && totalWeight > 0)
    {
        float startWeight = 0.0f;
        int spanStart = 0;
        for (auto& item : Items)
        {
            if (item.Type == LayoutItemType::Spring)
            {
                float nextWeight = startWeight + item.SpringWeight;

                auto spanEnd = static_cast<int>(freeSpace * nextWeight  / totalWeight + 0.5f);

                if (spanEnd > spanStart)
                    item.SpringSize = spanEnd - spanStart;
                else
                    item.SpringSize = 0;

                spanStart   = spanEnd;
                startWeight = nextWeight;

                totalSize += item.SpringSize;
            }
            else
                totalSize += item.Size.w;
        }
    }

    Modified = false;

    return true;
}