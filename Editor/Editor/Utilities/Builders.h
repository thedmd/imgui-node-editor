#pragma once
#include "../NodeEditor.h"
#include "../Common/Types.h"


//------------------------------------------------------------------------------
namespace ax {
namespace NodeEditor {
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

    void Pin(int id, ax::NodeEditor::PinKind kind);
    void EndPin();

    ImTextureID HeaderTextureId;
    int         HeaderTextureWidth;
    int         HeaderTextureHeight;
    int         CurrentNodeId;
    Stage       CurrentStage;
    ImU32       HeaderColor;
    rect        NodeRect;
    rect        HeaderRect;
    rect        ContentRect;
};



//------------------------------------------------------------------------------
} // namespace Utilities
} // namespace Editor
} // namespace ax