//------------------------------------------------------------------------------
// LICENSE
//   This software is dual-licensed to the public domain and under the following
//   license: you are granted a perpetual, irrevocable license to copy, modify,
//   publish, and distribute this file as you see fit.
//
// CREDITS
//   Written by Michal Cichon
//------------------------------------------------------------------------------
# ifndef __IMGUI_NODE_EDITOR_INTERNAL_INL__
# define __IMGUI_NODE_EDITOR_INTERNAL_INL__
# pragma once


//------------------------------------------------------------------------------
# include "imgui_node_editor_internal.h"


//------------------------------------------------------------------------------
namespace ax {
namespace NodeEditor {
namespace Detail {


//------------------------------------------------------------------------------
//inline ImRect ToRect(const ax::rectf& rect)
//{
//    return ImRect(
//        to_imvec(rect.top_left()),
//        to_imvec(rect.bottom_right())
//    );
//}
//
//inline ImRect ToRect(const ax::rect& rect)
//{
//    return ImRect(
//        to_imvec(rect.top_left()),
//        to_imvec(rect.bottom_right())
//    );
//}

inline ImRect ImGui_GetItemRect()
{
    return ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
}


//------------------------------------------------------------------------------
} // namespace Detail
} // namespace Editor
} // namespace ax


//------------------------------------------------------------------------------
# endif // __IMGUI_NODE_EDITOR_INTERNAL_INL__
