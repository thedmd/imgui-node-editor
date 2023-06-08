#ifndef EDCONFIG_H
#define EDCONFIG_H

#include <string>

struct CustomNodeId
{
    std::string name;

    CustomNodeId(const CustomNodeId &id) = default;
    CustomNodeId &operator=(const CustomNodeId &id) = default;

    bool operator<(const CustomNodeId &id) const;
    bool operator==(const CustomNodeId &id) const;

    std::string AsString() const;
    static CustomNodeId FromString(const char *str, const char *end);
    bool IsValid() const;

    static const CustomNodeId Invalid;
};

struct CustomPinId
{
    enum class Direction {
        In,
        Out
    };
    std::string nodeName;
    Direction direction;
    int pinIndex;

    CustomPinId(const CustomPinId &id) = default;
    CustomPinId &operator=(const CustomPinId &id) = default;

    bool operator<(const CustomPinId &id) const;
    bool operator==(const CustomPinId &id) const;

    std::string AsString() const;
    static CustomPinId FromString(const char *str, const char *end);
    bool IsValid() const;

    static const CustomPinId Invalid;
};

struct CustomLinkId
{
    CustomPinId in, out;

    CustomLinkId(const CustomLinkId &id) = default;
    CustomLinkId &operator=(const CustomLinkId &id) = default;

    bool operator<(const CustomLinkId &id) const;
    bool operator==(const CustomLinkId &id) const;

    std::string AsString() const;
    static CustomLinkId FromString(const char *str, const char *end);
    bool IsValid() const;

    static const CustomLinkId Invalid;
};

#define IMGUI_NODE_EDITOR_CUSTOM_NODEID CustomNodeId
#define IMGUI_NODE_EDITOR_CUSTOM_PINID CustomPinId
#define IMGUI_NODE_EDITOR_CUSTOM_LINKID CustomLinkId

#endif
