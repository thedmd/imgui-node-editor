# include "crude_blueprint_library.h"
# include <crude_json.h>

# ifdef _WIN32
extern "C" __declspec(dllimport) void __stdcall OutputDebugStringA(const char* string);
# else
#     include <stdio.h>
# endif

crude_blueprint::PrintFunction crude_blueprint::PrintNode::s_PrintFunction = nullptr;

crude_blueprint::FlowPin crude_blueprint::PrintNode::Execute(Context& context, FlowPin& entryPoint)
{
    auto value = context.GetPinValue<string>(m_String);

    if (s_PrintFunction)
    {
        s_PrintFunction(*this, value);
    }
    else
    {
# ifdef _WIN32
        OutputDebugStringA("PrintNode: ");
        OutputDebugStringA(value.c_str());
        OutputDebugStringA("\n");
# else
        printf("PrintNode: %s\n", value.c_str());
# endif
    }

    return m_Exit;
}

bool crude_blueprint::ToStringNode::Load(const crude_json::value& value)
{
    if (!Node::Load(value))
        return false;

    if (!value.contains("type"))
        return false;

    auto& typeValue = value["type"];
    if (!typeValue.is_string())
        return false;

    PinType type;
    if (!PinTypeFromString(typeValue.get<crude_json::string>().c_str(), type))
        return false;

    SetType(type);

    return true;
}

void crude_blueprint::ToStringNode::Save(crude_json::value& value) const
{
    Node::Save(value);

    value["type"] = PinTypeToString(m_Value.GetValueType());
}

bool crude_blueprint::AddNode::Load(const crude_json::value& value)
{
    if (!Node::Load(value))
        return false;

    if (!value.contains("type"))
        return false;

    auto& typeValue = value["type"];
    if (!typeValue.is_string())
        return false;

    PinType type;
    if (!PinTypeFromString(typeValue.get<crude_json::string>().c_str(), type))
        return false;

    SetType(type);

    return true;
}

void crude_blueprint::AddNode::Save(crude_json::value& value) const
{
    Node::Save(value);

    value["type"] = PinTypeToString(m_Type);
}
