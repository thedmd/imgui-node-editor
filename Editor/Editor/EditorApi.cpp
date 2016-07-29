#include "EditorApi.h"
#include "Editor.h"


//------------------------------------------------------------------------------
static ax::Editor::Context* s_Editor = nullptr;


//------------------------------------------------------------------------------
ax::Editor::Context* ax::Editor::CreateEditor()
{
    return new Context();
}

void ax::Editor::DestroyEditor(Context* ctx)
{
    if (GetCurrentEditor() == ctx)
        SetCurrentEditor(nullptr);

    delete ctx;
}

void ax::Editor::SetCurrentEditor(Context* ctx)
{
    s_Editor = ctx;
}

ax::Editor::Context* ax::Editor::GetCurrentEditor()
{
    return s_Editor;
}

void ax::Editor::Begin(const char* id)  { s_Editor->Begin(id);          }
void ax::Editor::End()                  { s_Editor->End();              }
void ax::Editor::BeginNode(int id)      { s_Editor->BeginNode(id);      }
void ax::Editor::EndNode()              { s_Editor->EndNode();          }
void ax::Editor::BeginHeader()          { s_Editor->BeginHeader();      }
void ax::Editor::EndHeader()            { s_Editor->EndHeader();        }
void ax::Editor::BeginInput(int id)     { s_Editor->BeginInput(id);     }
void ax::Editor::EndInput()             { s_Editor->EndInput();         }
void ax::Editor::BeginOutput(int id)    { s_Editor->BeginOutput(id);    }
void ax::Editor::EndOutput()            { s_Editor->EndOutput();        }

void ax::Editor::Link(int id, int startNodeId, int endNodeId, const ImVec4& color/* = ImVec4(1, 1, 1, 1)*/)
{
}




