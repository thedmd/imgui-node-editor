//------------------------------------------------------------------------------
// blueprint_screen: the screens blueprints2-example's blueprints draw on, which
// the example shows in windows of their own.
//------------------------------------------------------------------------------
# pragma once
# include "crude_blueprint.h"
# include <imgui.h>
# include <memory>
# include <span>
# include <string>
# include <vector>

namespace blueprint_screen {

using namespace crude_blueprint;

// Where a text is along its line, from the position it's drawn at.
enum class Align : uint8_t { Left, Center, Right };

// What a node drew, in its screen's units: from the top left, x to the right and y down.
struct Shape
{
    enum class Kind : uint8_t { Rect, Circle, Text };

    Kind        m_Kind  = Kind::Rect;
    ImVec2      m_At;              // a rect's top left, a circle's center, where a text is
    ImVec2      m_Size;            // a rect's; a circle's radius in x; a text's height in y
    ImVec4      m_Color;
    std::string m_Text;
    Align       m_Align = Align::Left;
};

// What nodes drew on it since it was cleared, and the picture it shows: what was drawn when its run
// last waited, so a picture shows whole however many steps it took.
struct Screen
{
    static constexpr size_t c_MaxShapes = 65536; // between clears

    std::string        m_Title;
    int                m_Width  = 0;
    int                m_Height = 0;
    ImVec4             m_Background = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    std::vector<Shape> m_Shapes;
    ImVec4             m_ShownBackground = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    std::vector<Shape> m_Shown;
    bool               m_Open = true; // its window
};

// The screens a run's nodes open, each shown in a window of its title. They stay after their run,
// and a run that opens a screen of the same title has it again.
class ScreenHost final : public Service
{
public:
    CRUDE_BP_RTTI(ScreenHost, Service)

    // The screen of the title, cleared to black, of the size, its window open.
    std::shared_ptr<Screen> Open(std::string title, int width, int height);

    // Each screen shows what was drawn on it since it was cleared. For the host to call while no
    // fiber of the run is ready.
    void Present();

    // A window for each open screen, its picture scaled to fit; 'live' shows what was drawn so far,
    // as for a run that's paused.
    void ShowWindows(bool live);

    std::span<const std::shared_ptr<Screen>> Screens() const { return m_Screens; }

private:
    std::vector<std::shared_ptr<Screen>> m_Screens;
};

// example.Screen, a screen a run opened, which a file holds as none, and example.Align; and the
// nodes of the Screen category: Open Screen, Clear Screen, Fill Rect, Fill Circle and Draw Text.
// Their colors are of the struct, their positions and sizes of the vector, and both live as long
// as the registry.
Result<void> RegisterScreen(Registry& registry, const StructType& color, const StructType& vector2);

} // namespace blueprint_screen
