#pragma once

#include "ImWindow/ImwWindow.h"
#include "EditorApi.h"

class NodeWindow:
    public ImWindow::ImwWindow
{
public:
    NodeWindow();
    ~NodeWindow();

    virtual void OnGui();


private:
    ax::Editor::Context* m_Editor;
};
