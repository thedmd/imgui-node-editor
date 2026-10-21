# include "crude_blueprint.h"


//
// -------[ Errors ]-------
//

crude_blueprint::Error::Error(ErrorCategory category, uint16_t code, std::string_view message)
    : m_Category(category)
    , m_Code(code)
{
    if (!message.empty())
        m_Message = MakeRef<const StringBox>(std::string(message));
}
