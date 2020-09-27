# include "imgui_extras.h"
# define IMGUI_DEFINE_MATH_OPERATORS
# include <imgui_internal.h>
# include <algorithm>
# include <stdio.h>
# include <stdlib.h>
# include <errno.h>




void ImEx::Debug_DrawItemRect(const ImVec4& col)
{
    auto drawList = ImGui::GetWindowDrawList();
    auto itemMin = ImGui::GetItemRectMin();
    auto itemMax = ImGui::GetItemRectMax();
    drawList->AddRect(itemMin, itemMax, ImColor(col));
}




ImEx::ScopedItemWidth::ScopedItemWidth(float width)
{
    ImGui::PushItemWidth(width);
}

ImEx::ScopedItemWidth::~ScopedItemWidth()
{
    Release();
}

void ImEx::ScopedItemWidth::Release()
{
    if (m_IsDone)
        return;

    ImGui::PopItemWidth();

    m_IsDone = true;
}




ImEx::ScopedDisableItem::ScopedDisableItem(bool disable, float disabledAlpha)
    : m_Disable(disable)
{
    if (!m_Disable)
        return;

    auto wasDisabled = (ImGui::GetCurrentWindow()->DC.ItemFlags & ImGuiItemFlags_Disabled) == ImGuiItemFlags_Disabled;

    ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);

    auto& stale = ImGui::GetStyle();
    m_LastAlpha = stale.Alpha;

    // Don't override alpha if we're already in disabled context.
    if (!wasDisabled)
        stale.Alpha = disabledAlpha;
}

ImEx::ScopedDisableItem::~ScopedDisableItem()
{
    Release();
}

void ImEx::ScopedDisableItem::Release()
{
    if (!m_Disable)
        return;

    auto& stale = ImGui::GetStyle();
    stale.Alpha = m_LastAlpha;

    ImGui::PopItemFlag();

    m_Disable = false;
}




ImEx::ScopedSuspendLayout::ScopedSuspendLayout()
{
    m_Window = ImGui::GetCurrentWindow();
    m_CursorPos = m_Window->DC.CursorPos;
    m_CursorPosPrevLine = m_Window->DC.CursorPosPrevLine;
    m_CursorMaxPos = m_Window->DC.CursorMaxPos;
    m_CurrLineSize = m_Window->DC.CurrLineSize;
    m_PrevLineSize = m_Window->DC.PrevLineSize;
    m_CurrLineTextBaseOffset = m_Window->DC.CurrLineTextBaseOffset;
    m_PrevLineTextBaseOffset = m_Window->DC.PrevLineTextBaseOffset;
}

ImEx::ScopedSuspendLayout::~ScopedSuspendLayout()
{
    Release();
}

void ImEx::ScopedSuspendLayout::Release()
{
    if (m_Window == nullptr)
        return;

    m_Window->DC.CursorPos = m_CursorPos;
    m_Window->DC.CursorPosPrevLine = m_CursorPosPrevLine;
    m_Window->DC.CursorMaxPos = m_CursorMaxPos;
    m_Window->DC.CurrLineSize = m_CurrLineSize;
    m_Window->DC.PrevLineSize = m_PrevLineSize;
    m_Window->DC.CurrLineTextBaseOffset = m_CurrLineTextBaseOffset;
    m_Window->DC.PrevLineTextBaseOffset = m_PrevLineTextBaseOffset;

    m_Window = nullptr;
}




ImEx::ItemBackgroundRenderer::ItemBackgroundRenderer(OnDrawCallback onDrawBackground)
    : m_OnDrawBackground(std::move(onDrawBackground))
{
    m_DrawList = ImGui::GetWindowDrawList();
    m_Splitter.Split(m_DrawList, 2);
    m_Splitter.SetCurrentChannel(m_DrawList, 1);
}

ImEx::ItemBackgroundRenderer::~ItemBackgroundRenderer()
{
    Commit();
}

void ImEx::ItemBackgroundRenderer::Commit()
{
    if (m_Splitter._Current == 0)
        return;

    m_Splitter.SetCurrentChannel(m_DrawList, 0);

    if (m_OnDrawBackground)
        m_OnDrawBackground(m_DrawList);

    m_Splitter.Merge(m_DrawList);
}

void ImEx::ItemBackgroundRenderer::Discard()
{
    if (m_Splitter._Current == 1)
        m_Splitter.Merge(m_DrawList);
}




ImEx::StorageHandler<ImEx::MostRecentlyUsedList::Settings> ImEx::MostRecentlyUsedList::s_Storage;

void ImEx::MostRecentlyUsedList::Install(ImGuiContext* context)
{
    context->SettingsHandlers.push_back(s_Storage.MakeHandler("MostRecentlyUsedList"));

    s_Storage.ReadLine = [](ImGuiContext*, Settings* entry, const char* line)
    {
        const char* lineEnd = line + strlen(line);

        auto parseListEntry = [lineEnd](const char* line, int& index) -> const char*
        {
            char* indexEnd = nullptr;
            errno = 0;
            index = strtol(line, &indexEnd, 10);
            if (errno == ERANGE)
                return nullptr;
            if (indexEnd >= lineEnd)
                return nullptr;
            if (*indexEnd != '=')
                return nullptr;
            return indexEnd + 1;
        };


        int index = 0;
        if (auto path = parseListEntry(line, index))
        {
            if (static_cast<int>(entry->m_List.size()) <= index)
                entry->m_List.resize(index + 1);
            entry->m_List[index] = path;
        }
    };

    s_Storage.WriteAll = [](ImGuiContext*, ImGuiTextBuffer* out_buf, const ImEx::StorageHandler<Settings>::Storage& storage)
    {
        for (auto& entry : storage)
        {
            out_buf->appendf("[%s][%s]\n", "MostRecentlyUsedList", entry.first.c_str());
            int index = 0;
            for (auto& value : entry.second->m_List)
                out_buf->appendf("%d=%s\n", index++, value.c_str());
            out_buf->append("\n");
        }
    };
}

ImEx::MostRecentlyUsedList::MostRecentlyUsedList(const char* id, int capacity /*= 10*/)
    : m_ID(id)
    , m_Capacity(capacity)
    , m_List(s_Storage.FindOrCreate(id)->m_List)
{
}

void ImEx::MostRecentlyUsedList::Add(const std::string& item)
{
    Add(item.c_str());
}

void ImEx::MostRecentlyUsedList::Add(const char* item)
{
    auto itemIt = std::find(m_List.begin(), m_List.end(), item);
    if (itemIt != m_List.end())
    {
        // Item is already on the list. Rotate list to move it to the
        // first place.
        std::rotate(m_List.begin(), itemIt, itemIt + 1);
    }
    else
    {
        // Push new item to the back, rotate list to move it to the front,
        // pop back last element if we're over capacity.
        m_List.push_back(item);
        std::rotate(m_List.begin(), m_List.end() - 1, m_List.end());
        if (static_cast<int>(m_List.size()) > m_Capacity)
            m_List.pop_back();
    }

    PushToStorage();

    ImGui::MarkIniSettingsDirty();
}

void ImEx::MostRecentlyUsedList::Clear()
{
    if (m_List.empty())
        return;

    m_List.resize(0);

    PushToStorage();

    ImGui::MarkIniSettingsDirty();
}

const std::vector<std::string>& ImEx::MostRecentlyUsedList::GetList() const
{
    return m_List;
}

int ImEx::MostRecentlyUsedList::Size() const
{
    return static_cast<int>(m_List.size());
}

void ImEx::MostRecentlyUsedList::PullFromStorage()
{
    if (auto settings = s_Storage.Find(m_ID.c_str()))
        m_List = settings->m_List;
}

void ImEx::MostRecentlyUsedList::PushToStorage()
{
    auto settings = s_Storage.FindOrCreate(m_ID.c_str());
    settings->m_List = m_List;
}
