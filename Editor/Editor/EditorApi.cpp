#include "Editor.h"
#include "EditorApi.h"


//------------------------------------------------------------------------------
static ax::Editor::Detail::Context* s_Editor = nullptr;


//------------------------------------------------------------------------------
ax::Editor::Context* ax::Editor::CreateEditor()
{
    return reinterpret_cast<ax::Editor::Context*>(new ax::Editor::Detail::Context());
}

void ax::Editor::DestroyEditor(Context* ctx)
{
    if (GetCurrentEditor() == ctx)
        SetCurrentEditor(nullptr);

    auto editor = reinterpret_cast<ax::Editor::Detail::Context*>(ctx);

    delete editor;
}

void ax::Editor::SetCurrentEditor(Context* ctx)
{
    s_Editor = reinterpret_cast<ax::Editor::Detail::Context*>(ctx);
}

ax::Editor::Context* ax::Editor::GetCurrentEditor()
{
    return reinterpret_cast<ax::Editor::Context*>(s_Editor);
}

void ax::Editor::Begin(const char* id)              { s_Editor->Begin(id);                   }
void ax::Editor::End()                              { s_Editor->End();                       }
void ax::Editor::BeginNode(int id)                  { s_Editor->BeginNode(id);               }
void ax::Editor::EndNode()                          { s_Editor->EndNode();                   }
void ax::Editor::BeginHeader(const ImVec4& color)   { s_Editor->BeginHeader(ImColor(color)); }
void ax::Editor::EndHeader()                        { s_Editor->EndHeader();                 }
void ax::Editor::BeginInput(int id)                 { s_Editor->BeginInput(id);              }
void ax::Editor::EndInput()                         { s_Editor->EndInput();                  }
void ax::Editor::BeginOutput(int id)                { s_Editor->BeginOutput(id);             }
void ax::Editor::EndOutput()                        { s_Editor->EndOutput();                 }

bool ax::Editor::CreateLink(int* startId, int* endId, const ImColor& color, float thickness)
{
    return s_Editor->CreateLink(startId, endId, color, thickness);
}

void ax::Editor::RejectLink(const ImVec4& color, float thickness)
{
    s_Editor->RejectLink(ImColor(color), thickness);
}

bool ax::Editor::AcceptLink(const ImVec4& color, float thickness)
{
    return s_Editor->AcceptLink(ImColor(color), thickness);
}

bool ax::Editor::DestroyLink()
{
    return s_Editor->DestroyLink();
}

int ax::Editor::GetDestroyedLinkId()
{
    return s_Editor->GetDestroyedLinkId();
}

bool ax::Editor::Link(int id, int startPinId, int endPinId, const ImVec4& color/* = ImVec4(1, 1, 1, 1)*/, float thickness/* = 1.0f*/)
{
    return s_Editor->DoLink(id, startPinId, endPinId, ImColor(color), thickness);
}




