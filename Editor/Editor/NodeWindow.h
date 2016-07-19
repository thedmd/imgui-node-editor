
#pragma once

#include "ImWindow/ImwWindow.h"
#include "NodeEditor.h"

class NodeWindow:
    public ImWindow::ImwWindow
{
public:
    NodeWindow();
    ~NodeWindow();

    virtual void OnGui();


private:
    ax::NodeEditor::Context* m_Editor;
};
