# define IMGUI_DEFINE_MATH_OPERATORS
# include "blueprint_screen.h"
# include <crude_json.h>
# include <imgui_internal.h>
# include <algorithm>
# include <cfloat>
# include <initializer_list>
# include <string_view>
# include <utility>

namespace blueprint_screen { namespace {

struct ScreenBox final : Box
{
    CRUDE_BP_RTTI(ScreenBox, Box)

    explicit ScreenBox(std::shared_ptr<Screen> screen): m_Screen(std::move(screen)) {}

    const std::shared_ptr<Screen> m_Screen;
};

// A screen a run opened. It's none by default, which is all a file holds of it.
struct ScreenType final : ValueType
{
    ScreenType(): ValueType("example.Screen") {}

    static Screen* Of(const Value& value)
    {
        auto box = Cast<ScreenBox>(value.AsBox());
        return box ? box->m_Screen.get() : nullptr;
    }

    Value Make(std::shared_ptr<Screen> screen) const
    {
        return Value::Boxed(AsType(), MakeRef<const ScreenBox>(std::move(screen)));
    }

    Value Default(const Type& type) const override
    {
        return Value::Boxed(type, {});
    }

    Result<Value> Load(const Type& type, const crude_json::value& json) const override
    {
        if (!json.is_null())
            return Error(BlueprintError::WrongType, "a file holds no screen");
        return Default(type);
    }

    crude_json::value Save(const Value&) const override
    {
        return {};
    }

    std::string Format(const Value& value) const override
    {
        auto screen = Of(value);
        return screen ? screen->m_Title + " (" + std::to_string(screen->m_Width) + " x " + std::to_string(screen->m_Height) + ")" : "no screen";
    }

    bool Equal(const Value& a, const Value& b) const override
    {
        return Of(a) == Of(b);
    }
};

ImVec2 VectorOf(const Value& value)
{
    auto fields = value.AsStruct();
    return ImVec2(fields[0].AsFloat(), fields[1].AsFloat());
}

ImVec4 ColorOf(const Value& value)
{
    auto fields = value.AsStruct();
    return ImVec4(fields[0].AsFloat(), fields[1].AsFloat(), fields[2].AsFloat(), fields[3].AsFloat());
}

Value Vector(const StructType& vector2, float x, float y)
{
    return Value::Struct(vector2.AsType(), { Value::Float(x), Value::Float(y) });
}

// The inputs of the keys, in their order, or the first one's error.
Result<std::vector<Value>> Read(ExecContext& context, std::initializer_list<std::string_view> keys)
{
    std::vector<Value> values;
    for (auto key : keys)
    {
        auto value = context.Get(key);
        if (!value)
            return value.GetError();
        values.push_back(std::move(value).GetValue());
    }
    return values;
}

Result<void> Add(Screen& screen, Shape shape)
{
    if (screen.m_Shapes.size() >= Screen::c_MaxShapes)
        return Error(BlueprintError::OutOfRange, screen.m_Title + " has the " + std::to_string(Screen::c_MaxShapes) + " shapes a screen holds, drawn since it was cleared");
    screen.m_Shapes.push_back(std::move(shape));
    return {};
}

struct OpenScreenNode final : NodeType
{
    explicit OpenScreenNode(const ScreenType& screen): NodeType("example.OpenScreen", "Open Screen", "Screen"), m_ScreenType(screen) {}

    void Declare(NodeDeclaration& declaration, const NodeConfig&, const Bindings&) const override
    {
        declaration.Output("screen", "Screen", m_ScreenType.AsType());
    }

    Result<Exit> Execute(ExecContext& context, std::string_view) const override
    {
        auto host = context.FindService<ScreenHost>();
        if (!host)
            return Error(BlueprintError::Unsupported, "no host shows screens here");
        auto title  = context.Get(m_Title);
        auto width  = context.Get(m_Width);
        auto height = context.Get(m_Height);
        if (!title || !width || !height)
            return !title ? title.GetError() : !width ? width.GetError() : height.GetError();
        const auto fits = [](int32_t size) { return size >= 1 && size <= 4096; };
        if (!fits(width.GetValue()) || !fits(height.GetValue()))
            return Error(BlueprintError::OutOfRange, "a screen is from 1 to 4096 wide and high, not " + std::to_string(width.GetValue()) + " x " + std::to_string(height.GetValue()));
        context.Set("screen", m_ScreenType.Make(host->Open(std::move(title).GetValue(), width.GetValue(), height.GetValue())));
        return Exit::To(m_Then);
    }

    const ScreenType&           m_ScreenType;
    const FlowPin               m_Enter  = FlowInput("enter");
    const InputPin<std::string> m_Title  = Input<std::string>("title", "Title", { .m_Default = Value::String("Screen") });
    const InputPin<int32_t>     m_Width  = Input<int32_t>("width", "Width", { .m_Default = Value::Int32(480) });
    const InputPin<int32_t>     m_Height = Input<int32_t>("height", "Height", { .m_Default = Value::Int32(320) });
    const FlowPin               m_Then   = FlowOutput("then");
};

// Changes the screen its input gives, which it needs, then goes on.
struct ScreenNode : NodeType
{
    ScreenNode(std::string name, std::string title, const ScreenType& screen, const StructType& color, const StructType& vector2)
        : NodeType(std::move(name), std::move(title), "Screen")
        , m_ScreenType(screen)
        , m_Color(color)
        , m_Vector2(vector2)
    {
    }

    void Declare(NodeDeclaration& declaration, const NodeConfig&, const Bindings&) const override
    {
        declaration.Input("screen", "Screen", m_ScreenType.AsType(), { .m_Default = {}, .m_Requirement = Requirement::Required });
        DeclareInputs(declaration);
    }

    Result<Exit> Execute(ExecContext& context, std::string_view) const override
    {
        auto value = context.Get("screen");
        if (!value)
            return value.GetError();
        auto screen = ScreenType::Of(value.GetValue());
        if (!screen)
            return Error(BlueprintError::NotFound, "the screen isn't open: Open Screen opens one");
        if (auto changed = Change(context, *screen); !changed)
            return changed.GetError();
        return Exit::To(m_Then);
    }

    virtual void         DeclareInputs(NodeDeclaration& declaration) const = 0;
    virtual Result<void> Change(ExecContext& context, Screen& screen) const = 0;

    const ScreenType& m_ScreenType;
    const StructType& m_Color;
    const StructType& m_Vector2;
    const FlowPin     m_Enter = FlowInput("enter");
    const FlowPin     m_Then  = FlowOutput("then");
};

// Fills the screen with the color, and what was drawn on it goes.
struct ClearScreenNode final : ScreenNode
{
    ClearScreenNode(const ScreenType& screen, const StructType& color, const StructType& vector2): ScreenNode("example.ClearScreen", "Clear Screen", screen, color, vector2) {}

    void DeclareInputs(NodeDeclaration& declaration) const override
    {
        declaration.Input("color", "Color", m_Color.AsType(), { .m_Default = Value::Struct(m_Color.AsType(), { Value::Float(0.0f), Value::Float(0.0f), Value::Float(0.0f), Value::Float(1.0f) }) });
    }

    Result<void> Change(ExecContext& context, Screen& screen) const override
    {
        auto color = context.Get("color");
        if (!color)
            return color.GetError();
        screen.m_Background = ColorOf(color.GetValue());
        screen.m_Shapes.clear();
        return {};
    }
};

struct FillRectNode final : ScreenNode
{
    FillRectNode(const ScreenType& screen, const StructType& color, const StructType& vector2): ScreenNode("example.FillRect", "Fill Rect", screen, color, vector2) {}

    void DeclareInputs(NodeDeclaration& declaration) const override
    {
        declaration.Input("position", "Position", m_Vector2.AsType());
        declaration.Input("size", "Size", m_Vector2.AsType(), { .m_Default = Vector(m_Vector2, 20.0f, 20.0f) });
        declaration.Input("color", "Color", m_Color.AsType());
    }

    Result<void> Change(ExecContext& context, Screen& screen) const override
    {
        auto inputs = Read(context, { "position", "size", "color" });
        if (!inputs)
            return inputs.GetError();
        auto& values = inputs.GetValue();
        auto  size   = VectorOf(values[1]);
        if (size.x < 0.0f || size.y < 0.0f)
            return Error(BlueprintError::OutOfRange, "a rect's size is negative: " + values[1].GetType()->m_Head->Format(values[1]));
        return Add(screen, { Shape::Kind::Rect, VectorOf(values[0]), size, ColorOf(values[2]), {}, Align::Left });
    }
};

struct FillCircleNode final : ScreenNode
{
    FillCircleNode(const ScreenType& screen, const StructType& color, const StructType& vector2): ScreenNode("example.FillCircle", "Fill Circle", screen, color, vector2) {}

    void DeclareInputs(NodeDeclaration& declaration) const override
    {
        declaration.Input("center", "Center", m_Vector2.AsType());
        declaration.Input("radius", "Radius", FloatType(), { .m_Default = Value::Float(10.0f) });
        declaration.Input("color", "Color", m_Color.AsType());
    }

    Result<void> Change(ExecContext& context, Screen& screen) const override
    {
        auto inputs = Read(context, { "center", "radius", "color" });
        if (!inputs)
            return inputs.GetError();
        auto& values = inputs.GetValue();
        auto  radius = values[1].AsFloat();
        if (radius < 0.0f)
            return Error(BlueprintError::OutOfRange, "a circle's radius is negative: " + FloatType().m_Head->Format(values[1]));
        return Add(screen, { Shape::Kind::Circle, VectorOf(values[0]), ImVec2(radius, radius), ColorOf(values[2]), {}, Align::Left });
    }
};

// A line of text, its top at the position, and along its line as the alignment says.
struct DrawTextNode final : ScreenNode
{
    DrawTextNode(const ScreenType& screen, const StructType& color, const StructType& vector2, const EnumType& align): ScreenNode("example.DrawText", "Draw Text", screen, color, vector2), m_Align(align) {}

    void DeclareInputs(NodeDeclaration& declaration) const override
    {
        declaration.Input("position", "Position", m_Vector2.AsType());
        declaration.Input("text", "Text", StringType(), { .m_Default = Value::String("Text") });
        declaration.Input("size", "Size", FloatType(), { .m_Default = Value::Float(20.0f) });
        declaration.Input("color", "Color", m_Color.AsType());
        declaration.Input("align", "Align", m_Align.AsType());
    }

    Result<void> Change(ExecContext& context, Screen& screen) const override
    {
        auto inputs = Read(context, { "position", "text", "size", "color", "align" });
        if (!inputs)
            return inputs.GetError();
        auto& values = inputs.GetValue();
        auto  size   = values[2].AsFloat();
        if (!(size > 0.0f))
            return Error(BlueprintError::OutOfRange, "a text's size is above 0, not " + FloatType().m_Head->Format(values[2]));
        return Add(screen, { Shape::Kind::Text, VectorOf(values[0]), ImVec2(0.0f, size), ColorOf(values[3]), std::string(values[1].AsString()), static_cast<Align>(values[4].AsEnum()) });
    }

    const EnumType& m_Align;
};

bool OfFloats(const StructType& type, size_t count)
{
    auto fields = type.Fields();
    return fields.size() == count && std::all_of(fields.begin(), fields.end(), [](const StructField& field) { return field.m_Type == &FloatType(); });
}

} } // namespace blueprint_screen { namespace {

std::shared_ptr<blueprint_screen::Screen> blueprint_screen::ScreenHost::Open(std::string title, int width, int height)
{
    auto found = std::find_if(m_Screens.begin(), m_Screens.end(), [&title](const std::shared_ptr<Screen>& screen) { return screen->m_Title == title; });
    auto screen = found != m_Screens.end() ? *found : m_Screens.emplace_back(std::make_shared<Screen>());
    screen->m_Title           = std::move(title);
    screen->m_Width           = width;
    screen->m_Height          = height;
    screen->m_Background      = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    screen->m_ShownBackground = screen->m_Background;
    screen->m_Shapes.clear();
    screen->m_Shown.clear();
    screen->m_Open = true;
    return screen;
}

void blueprint_screen::ScreenHost::Present()
{
    for (auto& screen : m_Screens)
    {
        screen->m_ShownBackground = screen->m_Background;
        screen->m_Shown           = screen->m_Shapes;
    }
}

void blueprint_screen::ScreenHost::ShowWindows(bool live)
{
    for (auto& screen : m_Screens)
    {
        if (!screen->m_Open)
            continue;
        const auto& style   = ImGui::GetStyle();
        const auto  picture = ImVec2(static_cast<float>(screen->m_Width), static_cast<float>(screen->m_Height));
        ImGui::SetNextWindowSize(picture + style.WindowPadding * 2.0f + ImVec2(0.0f, ImGui::GetFrameHeight()), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
        const auto label = screen->m_Title + "###screen:" + screen->m_Title;
        if (ImGui::Begin(label.c_str(), &screen->m_Open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse))
        {
            // The picture as large as the window takes, in the middle of it.
            const auto room  = ImMax(ImGui::GetContentRegionAvail(), ImVec2(1.0f, 1.0f));
            const auto scale = ImMin(room.x / picture.x, room.y / picture.y);
            const auto min   = ImFloor(ImGui::GetCursorScreenPos() + (room - picture * scale) * 0.5f);
            const auto max   = min + picture * scale;
            const auto at    = [min, scale](ImVec2 point) { return min + point * scale; };
            ImGui::InvisibleButton("##picture", room);

            auto  drawList = ImGui::GetWindowDrawList();
            auto& shapes   = live ? screen->m_Shapes : screen->m_Shown;
            drawList->AddRectFilled(min, max, ImGui::ColorConvertFloat4ToU32(live ? screen->m_Background : screen->m_ShownBackground));
            drawList->PushClipRect(min, max, true);
            for (auto& shape : shapes)
            {
                const auto color = ImGui::ColorConvertFloat4ToU32(shape.m_Color);
                switch (shape.m_Kind)
                {
                    case Shape::Kind::Rect:
                        drawList->AddRectFilled(at(shape.m_At), at(shape.m_At + shape.m_Size), color);
                        break;
                    case Shape::Kind::Circle:
                        drawList->AddCircleFilled(at(shape.m_At), shape.m_Size.x * scale, color);
                        break;
                    case Shape::Kind::Text:
                    {
                        auto       font     = ImGui::GetFont();
                        const auto height   = shape.m_Size.y * scale;
                        const auto width    = font->CalcTextSizeA(height, FLT_MAX, 0.0f, shape.m_Text.c_str()).x;
                        auto       position = at(shape.m_At);
                        position.x -= shape.m_Align == Align::Center ? width * 0.5f : shape.m_Align == Align::Right ? width : 0.0f;
                        drawList->AddText(font, height, position, color, shape.m_Text.c_str());
                        break;
                    }
                }
            }
            drawList->PopClipRect();
        }
        ImGui::End();
    }
}

crude_blueprint::Result<void> blueprint_screen::RegisterScreen(Registry& registry, const StructType& color, const StructType& vector2)
{
    if (!OfFloats(color, 4) || !OfFloats(vector2, 2))
        return Error(BlueprintError::WrongType, "screens take colors of four Floats, and vectors of two");
    auto  screen     = std::make_unique<ScreenType>();
    auto  align      = std::make_unique<EnumType>("example.Align", std::vector<std::string>{ "Left", "Center", "Right" });
    auto& screenType = *screen;
    auto& alignType  = *align;
    if (auto added = registry.AddValueType(std::move(screen)); !added)
        return added;
    if (auto added = registry.AddValueType(std::move(align)); !added)
        return added;

    std::unique_ptr<NodeType> types[] =
    {
        std::make_unique<OpenScreenNode>(screenType),
        std::make_unique<ClearScreenNode>(screenType, color, vector2),
        std::make_unique<FillRectNode>(screenType, color, vector2),
        std::make_unique<FillCircleNode>(screenType, color, vector2),
        std::make_unique<DrawTextNode>(screenType, color, vector2, alignType),
    };
    for (auto& type : types)
        if (auto added = registry.AddNodeType(std::move(type)); !added)
            return added;
    return {};
}
