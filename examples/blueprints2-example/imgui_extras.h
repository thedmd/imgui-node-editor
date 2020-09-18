#pragma once
#include <imgui.h>
# define IMGUI_DEFINE_MATH_OPERATORS
# include <imgui_internal.h>
# include <functional>
# include <string>
# include <vector>
# include <memory>
# include <map>

namespace ImEx {

enum class IconType: ImU32 { Flow, FlowDown, Circle, Square, Grid, RoundSquare, Diamond };

// Draws an icon into specified draw list.
void DrawIcon(ImDrawList* drawList, const ImVec2& a, const ImVec2& b, IconType type, bool filled, ImU32 color, ImU32 innerColor);

// Icon widget
void Icon(const ImVec2& size, IconType type, bool filled, const ImVec4& color = ImVec4(1, 1, 1, 1), const ImVec4& innerColor = ImVec4(0, 0, 0, 0));

// Drawn an rectangle around last ImGui widget.
void Debug_DrawItemRect(const ImVec4& col = ImVec4(1.0f, 0.0f, 0.0f, 1.0f));


struct ScopedItemWidth
{
    ScopedItemWidth(float width);
    ~ScopedItemWidth();

    void Release();

private:
    bool m_IsDone = false;
};


struct ScopedDisableItem
{
    ScopedDisableItem(bool disable, float disabledAlpha = 0.5f);
    ~ScopedDisableItem();

    void Release();

private:
    bool    m_Disable       = false;
    float   m_LastAlpha     = 1.0f;
};


struct ScopedSuspendLayout
{
    ScopedSuspendLayout();
    ~ScopedSuspendLayout();

    void Release();

private:
    ImGuiWindow* m_Window = nullptr;
    ImVec2 m_CursorPos;
    ImVec2 m_CursorPosPrevLine;
    ImVec2 m_CursorMaxPos;
    ImVec2 m_CurrLineSize;
    ImVec2 m_PrevLineSize;
    float  m_CurrLineTextBaseOffset;
    float  m_PrevLineTextBaseOffset;
};


template <typename Settings>
struct StorageHandler
{
    using Storage = std::map<std::string, std::unique_ptr<Settings>>;

    ImGuiSettingsHandler MakeHandler(const char* const typeName)
    {
        ImGuiSettingsHandler handler;
        handler.TypeName = typeName;
        handler.TypeHash = ImHashStr(typeName);
        handler.UserData = this;
        handler.ReadOpenFn = [](ImGuiContext* ctx, ImGuiSettingsHandler* handler, const char* name) -> void*
        {
            auto storage = reinterpret_cast<StorageHandler*>(handler->UserData);
            return storage->DoReadOpen(ctx, name);
        };
        handler.ReadLineFn = [](ImGuiContext* ctx, ImGuiSettingsHandler* handler, void* entry, const char* line) -> void
        {
            auto storage = reinterpret_cast<StorageHandler*>(handler->UserData);
            storage->DoReadLine(ctx, reinterpret_cast<Settings*>(entry), line);
        };
        handler.WriteAllFn = [](ImGuiContext* ctx, ImGuiSettingsHandler* handler, ImGuiTextBuffer* out_buf)
        {
            auto storage = reinterpret_cast<StorageHandler*>(handler->UserData);
            storage->DoWriteAll(ctx, out_buf);
        };
        return handler;
    }

    const Settings* Find(const char* name) const
    {
        auto it = m_Settings.find(name);
        if (it == m_Settings.end())
            return nullptr;

        return it->second.get();
    }

    Settings* Find(const char* name)
    {
        return const_cast<Settings*>(const_cast<const StorageHandler*>(this)->Find(name));
    }

    Settings* FindOrCreate(const char* name)
    {
        auto settings = Find(name);
        if (settings == nullptr)
        {
            auto it = m_Settings.emplace(name, std::make_unique<Settings>());
            settings = it.first->second.get();
        }
        return settings;
    }

    std::function<void(ImGuiContext*, Settings*, const char*)>              ReadOpen;
    std::function<void(ImGuiContext*, Settings*, const char*)>              ReadLine;
    std::function<void(ImGuiContext*, ImGuiTextBuffer*, const Storage&)>    WriteAll;

private:
    Settings* DoReadOpen(ImGuiContext* ctx, const char* name)
    {
        auto settings = FindOrCreate(name);
        if (ReadOpen)
            ReadOpen(ctx, settings, name);
        return settings;
    }

    void DoReadLine(ImGuiContext* ctx, Settings* entry, const char* line)
    {
        if (ReadLine)
            ReadLine(ctx, entry, line);
    }

    void DoWriteAll(ImGuiContext* ctx, ImGuiTextBuffer* out_buf)
    {
        if (WriteAll)
            WriteAll(ctx, out_buf, m_Settings);
    }

    std::map<std::string, std::unique_ptr<Settings>> m_Settings;
};

struct MostRecentlyUsedList
{
    static void Install(ImGuiContext* context);

    MostRecentlyUsedList(const char* id, int capacity = 10);

    void Add(const std::string& item);
    void Add(const char* item);
    void Clear();

    const std::vector<std::string>& GetList() const;

    int Size() const;

private:
    struct Settings
    {
        std::vector<std::string> m_List;
    };

    void PullFromStorage();
    void PushToStorage();

    std::string                 m_ID;
    int                         m_Capacity = 10;
    std::vector<std::string>&   m_List;

    static ImEx::StorageHandler<Settings> s_Storage;
};



} // namespace ImEx