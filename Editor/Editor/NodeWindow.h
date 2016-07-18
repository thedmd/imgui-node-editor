
#pragma once

#include "ImWindow/ImwWindow.h"

class NodeWindow:
    public ImWindow::ImwWindow
{
public:
    NodeWindow();

    virtual void OnGui();
};
