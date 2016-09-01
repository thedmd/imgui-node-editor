#pragma once
#include "EditorApi.h"
#include "Types.h"


//------------------------------------------------------------------------------
namespace ax {
namespace Editor {
namespace Utilities {


//------------------------------------------------------------------------------
struct BlueprintNodeBuilder
{
    BlueprintNodeBuilder(ImTextureID texture = nullptr, int textureWidth = 0, int textureHeight = 0);

    void Begin(int id);
    void End();

    void Header(const ImVec4& color = ImVec4(1, 1, 1, 1));
    void EndHeader();

    void Input(int id);
    void EndInput();

    void Output(int id);
    void EndOutput();

private:
    enum class Stage
    {
        Invalid,
        Begin,
        Header,
        Content,
        Input,
        Output,
        End
    };

    bool SetStage(Stage stage);

    void Pin(int id, ax::Editor::PinKind kind);
    void EndPin();

    ImTextureID HeaderTextureId;
    int         HeaderTextureWidth;
    int         HeaderTextureHeight;
    int         CurrentNodeId;
    Stage       CurrentStage;
    ImU32       HeaderColor;
    rectf       NodeRect;
    rectf       HeaderRect;
    rectf       ContentRect;
};



//------------------------------------------------------------------------------
} // namespace Utilities
} // namespace Editor
} // namespace ax