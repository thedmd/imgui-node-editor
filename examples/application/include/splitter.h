# pragma once

// A bar between two panes in the current window. Dragging it moves size from one pane to the other.
bool Splitter(bool split_vertically, float thickness, float* size1, float* size2, float min_size1, float min_size2, float splitter_long_axis_size = -1.0f);
