#include "InputModeTracker.hpp"

void InputModeTracker::handleMessage(const json& message)
{
    if (message.value("msg", "") != "input_mode")
        return;

    if (auto mode = message.find("mode"); mode != message.end() && mode->is_number())
        m_mode = mode->get<int>();
}

bool InputModeTracker::isGameplayMode() const
{
    // MORE (--more--), PROMPT (stat gain etc.), and YESNO block normal
    // gameplay input.  All other modes allow it.
    return m_mode != MORE && m_mode != PROMPT && m_mode != YESNO;
}
