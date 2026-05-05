#include "MessageLog.hpp"
#include <spdlog/spdlog.h>

void MessageLog::handleMessage(const json& message)
{
    if (message.value("msg", "") != "msgs")
        return;

    // Server sends {"msg":"msgs","messages":[{"channel":2,"text":"..."},...]}
    auto messagesIt = message.find("messages");
    if (messagesIt != message.end() && messagesIt->is_array()) {
        std::lock_guard lock(m_mutex);
        for (const auto& msg : *messagesIt) {
            if (msg.is_object()) {
                auto textIt = msg.find("text");
                if (textIt != msg.end() && textIt->is_string()) {
                    m_lines.push_back(textIt->get<std::string>());
                    ++m_lineCount;
                    if (m_lines.size() > s_maxLines)
                        m_lines.pop_front();
                }
            } else if (msg.is_string()) {
                m_lines.push_back(msg.get<std::string>());
                ++m_lineCount;
                if (m_lines.size() > s_maxLines)
                    m_lines.pop_front();
            }
        }
        spdlog::debug("MessageLog: received {} messages, total {} (count {})",
                      messagesIt->size(), m_lines.size(), m_lineCount);
        return;
    }

    // Legacy format: {"msg":"msgs","lines":["text1","text2"]}
    auto linesIt = message.find("lines");
    if (linesIt != message.end() && linesIt->is_array()) {
        std::lock_guard lock(m_mutex);
        for (const auto& line : *linesIt) {
            if (line.is_string()) {
                m_lines.push_back(line.get<std::string>());
                ++m_lineCount;
                if (m_lines.size() > s_maxLines)
                    m_lines.pop_front();
            }
        }
        spdlog::debug("MessageLog: received {} legacy lines, total {} (count {})",
                      linesIt->size(), m_lines.size(), m_lineCount);
    }
}

std::vector<std::string> MessageLog::getLines() const
{
    std::lock_guard lock(m_mutex);
    return {m_lines.begin(), m_lines.end()};
}
