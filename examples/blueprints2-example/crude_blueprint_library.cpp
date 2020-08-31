# include "crude_blueprint_library.h"



//
// -------[ Nodes ]-------
//

void crude_blueprint::AddNode::SetType(PinType type)
{
    m_Blueprint->ForgetPin(m_A.get());
    m_Blueprint->ForgetPin(m_B.get());
    m_Blueprint->ForgetPin(m_Result.get());

    m_Type = type;

    m_A.reset(CreatePin(type, "A"));
    m_B.reset(CreatePin(type, "B"));
    m_Result.reset(CreatePin(type, "Result"));

    m_InputPins[0] = m_A.get();
    m_InputPins[1] = m_B.get();
    m_OutputPins[0] = m_Result.get();
}

