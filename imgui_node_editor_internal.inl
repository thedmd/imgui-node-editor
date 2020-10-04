//------------------------------------------------------------------------------
// VERSION 0.9.1
//
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

inline ImVec2 ImGui_GetMouseClickPos(ImGuiMouseButton buttonIndex)
{
    if (ImGui::IsMouseDown(buttonIndex))
        return ImGui::GetIO().MouseClickedPos[buttonIndex];
    else
        return ImGui::GetMousePos();
}


//------------------------------------------------------------------------------
# define CHECK(condition, message) if (condition) {} else { if (error) { if (!error->empty()) *error += '\n'; *error += string() + message; } return false; }
# define CHECK_TYPE(value, _type) CHECK(value.type() == _type, "Expected \"" + ToString(_type) + "\" but got \"" + ToString(value.type()) + "\".")
# define CHECK_KEY(value, key)    CHECK(value.contains(key), "Required key \"" + key + "\" is missing.")
# define PARSE_IMPL(value, out, valueName) CHECK(Parse(value, out, error), "Failed to parse " + valueName + ".")
# define PARSE(value, out) CHECK(Parse(value, out, error), "Failed to parse value.")
# define CHECK_AND_PARSE(value, key, out) CHECK_KEY(value, key); PARSE_IMPL(value[key], out, "\"" + key + "\"")
# define CHECK_AND_PARSE_OPT(value, key, out) if (value.contains(key)) { CHECK_AND_PARSE(value, key, out); } else {}

inline bool Serialization::Parse(const string& str, ObjectId& result, string* error)
{
    (void)error;

    auto separator = str.find_first_of(':');
    auto idStart   = str.c_str() + ((separator != std::string::npos) ? separator + 1 : 0);
    auto id        = reinterpret_cast<void*>(strtoull(idStart, nullptr, 10));
    if (str.compare(0, separator, "node") == 0)
        result = ObjectId(NodeId(id));
    else if (str.compare(0, separator, "link") == 0)
        result = ObjectId(LinkId(id));
    else if (str.compare(0, separator, "pin") == 0)
        result = ObjectId(PinId(id));
    else
        // fallback to old format
        result = ObjectId(NodeId(id)); //return ObjectId();

    return true;
}

inline bool Serialization::Parse(const string& str, NodeId& result, string* error)
{
    ObjectId objectId;
    if (!Parse(str, objectId, error))
        return false;

    CHECK(objectId.IsNodeId(), "\"" + str + "\" is not a NodeId.");

    result = objectId.AsNodeId();

    return true;
}

inline bool Serialization::Parse(const string& str, json::value& result, string* error)
{
    result = json::value::parse(str);
    CHECK(result.is_discarded(), "Failed to parse json");

    return true;
}

inline bool Serialization::Parse(const json::value& v, float& result, string* error)
{
    CHECK_TYPE(v, json::type_t::number);

    result = static_cast<float>(v.get<double>());

    return true;
}

inline bool Serialization::Parse(const json::value& v, ImVec2& result, string* error)
{
    CHECK_TYPE(v, json::type_t::object);

    ImVec2 value;

    CHECK_AND_PARSE(v, "x", value.x);
    CHECK_AND_PARSE(v, "y", value.y);

    result = value;

    return true;
}

inline bool Serialization::Parse(const json::value& v, ImRect& result, string* error)
{
    CHECK_TYPE(v, json::type_t::object);

    ImRect value;

    CHECK_AND_PARSE(v, "min", value.Min);
    CHECK_AND_PARSE(v, "max", value.Max);

    result = value;

    return true;
}

inline bool Serialization::Parse(const json::value& v, NodeState& result, string* error)
{
    CHECK_TYPE(v, json::type_t::object);

    NodeState state;

    CHECK_AND_PARSE    (v, "location",   state.m_Location);
    CHECK_AND_PARSE_OPT(v, "group_size", state.m_GroupSize);

    result = std::move(state);

    return true;
}

inline bool Serialization::Parse(const json::value& v, NodesState& result, string* error)
{
    CHECK_TYPE(v, json::type_t::object);

    NodesState state;

    PARSE(v, state.m_Nodes);

    result = std::move(state);

    return true;
}

inline bool Serialization::Parse(const json::value& v, SelectionState& result, string* error)
{
    CHECK_TYPE(v, json::type_t::array);

    SelectionState state;

    PARSE(v, state.m_Selection);

    result = std::move(state);

    return true;
}

inline bool Serialization::Parse(const json::value& v, ViewState& result, string* error)
{
    CHECK_TYPE(v, json::type_t::object);

    ViewState state;

    CHECK_AND_PARSE    (v, "scroll",       state.m_ViewScroll);
    CHECK_AND_PARSE    (v, "zoom",         state.m_ViewZoom);
    CHECK_AND_PARSE_OPT(v, "visible_rect", state.m_VisibleRect);

    result = state;

    return true;
}

inline bool Serialization::Parse(const json::value& v, EditorState& result, string* error)
{
    CHECK_TYPE(v, json::type_t::object);

    EditorState state;

    CHECK_AND_PARSE(v, "nodes",     state.m_NodesState);
    CHECK_AND_PARSE(v, "selection", state.m_SelectionState);
    CHECK_AND_PARSE(v, "view",      state.m_ViewState);

    result = state;

    return true;
}

inline bool Serialization::Parse(const json::value& v, ObjectId& result, string* error)
{
    CHECK_TYPE(v, json::type_t::string);

    auto& str = v.get<std::string>();

    return Parse(str, result, error);
}

template <typename T>
inline bool Serialization::Parse(const json::value& v, vector<T>& result, string* error)
{
    CHECK_TYPE(v, json::type_t::array);

    const auto& array = v.get<json::array>();

    vector<T> paresed;

    paresed.reserve(array.size());
    for (auto& value : array)
    {
        T item;
        PARSE(value, item);
        paresed.emplace_back(std::move(item));
    }

    result = std::move(paresed);

    return true;
}

template <typename K, typename V>
inline bool Serialization::Parse(const json::value& v, map<K, V>& result, string* error)
{
    CHECK_TYPE(v, json::type_t::object);

    const auto& object = v.get<json::object>();

    map<K, V> paresed;

    for (auto& entry : object)
    {
        K key;
        PARSE(entry.first, key);

        V value;
        PARSE_IMPL(entry.second, value, entry.first);

        paresed[std::move(key)] = std::move(value);
    }

    result = std::move(paresed);

    return true;
}

inline string Serialization::ToString(const json::value& value)
{
    return value.dump();
}

inline string Serialization::ToString(const json::type_t& type)
{
    switch (type)
    {
        case json::type_t::null:        return "null";
        case json::type_t::object:      return "object";
        case json::type_t::array:       return "array";
        case json::type_t::string:      return "string";
        case json::type_t::boolean:     return "boolean";
        case json::type_t::number:      return "number";
        case json::type_t::discarded:   return "discarded";
    }

    return "";
}

inline string Serialization::ToString(const ObjectId& objectId)
{
    auto value = std::to_string(reinterpret_cast<uintptr_t>(objectId.AsPointer()));
    switch (objectId.Type())
    {
        default:
        case NodeEditor::Detail::ObjectType::None: return value;
        case NodeEditor::Detail::ObjectType::Node: return "node:" + value;
        case NodeEditor::Detail::ObjectType::Link: return "link:" + value;
        case NodeEditor::Detail::ObjectType::Pin:  return "pin:"  + value;
    }
    return value;
}

inline string Serialization::ToString(const NodeId& nodeId)
{
    return ToString(ObjectId(nodeId));
}

inline json::value Serialization::ToJson(const string& value)
{
    json::value result;
    if (!Parse(value, result))
        return json::value(json::type_t::discarded);
    return result;
}

inline json::value Serialization::ToJson(const ImVec2& value)
{
    json::value result;
    result["x"] = value.x;
    result["y"] = value.y;
    return result;
}

inline json::value Serialization::ToJson(const ImRect& value)
{
    json::value result;
    result["min"] = ToJson(value.Min);
    result["max"] = ToJson(value.Max);
    return result;
}

inline json::value Serialization::ToJson(const NodeState& value)
{
    json::value result;

    result["location"] = ToJson(value.m_Location);
    if (value.m_GroupSize.x > 0 || value.m_GroupSize.y > 0)
        result["group_size"] = ToJson(value.m_GroupSize);

    return result;
}

inline json::value Serialization::ToJson(const NodesState& value)
{
    return ToJson(value.m_Nodes);
}


inline json::value Serialization::ToJson(const SelectionState& value)
{
    return ToJson(value.m_Selection);
}

inline json::value Serialization::ToJson(const ViewState& value)
{
    json::value result;

    result["scroll"]       = ToJson(value.m_ViewScroll);
    result["zoom"]         = value.m_ViewZoom;
    result["visible_rect"] = ToJson(value.m_VisibleRect);

    return result;
}

inline json::value Serialization::ToJson(const EditorState& value)
{
    json::value result;

    result["nodes"]     = ToJson(value.m_NodesState);
    result["selection"] = ToJson(value.m_SelectionState);
    result["view"]      = ToJson(value.m_ViewState);

    return result;
}

inline json::value Serialization::ToJson(const ObjectId& value)
{
    return ToString(value);
}

template <typename T>
inline json::value Serialization::ToJson(const vector<T>& value)
{
    json::array array;
    array.reserve(value.size());

    for (auto& v : value)
        array.emplace_back(ToJson(v));

    return array;
}

template <typename K, typename V>
inline json::value Serialization::ToJson(const map<K, V>& value)
{
    json::object object;

    for (auto& v : value)
        object[ToString(v.first)] = ToJson(v.second);

    return object;
}

# undef CHECK
# undef CHECK_TYPE
# undef CHECK_KEY
# undef PARSE_IMPL
# undef PARSE
# undef CHECK_AND_PARSE
# undef CHECK_AND_PARSE_OPT


//------------------------------------------------------------------------------
} // namespace Detail
} // namespace Editor
} // namespace ax


//------------------------------------------------------------------------------
# endif // __IMGUI_NODE_EDITOR_INTERNAL_INL__
