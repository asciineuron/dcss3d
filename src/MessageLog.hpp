#pragma once

#include "MessageQueue.hpp"
#include <deque>
#include <mutex>
#include <string>
#include <vector>

// Stores the most recent game message log lines received from the server.
// Mirrors the JS client's message log handling in messages.js.
// Lines are displayed in an ImGui window via displayMessageLog().
class MessageLog : public MessageHandler {
public:
    MessageLog() = default;

    void handleMessage(const json& message) override;

    // Thread-safe access to recent lines (newest last).
    // Returns a copy of the current buffer.
    std::vector<std::string> getLines() const;

    // Returns a counter that increments whenever new lines are added.
    // Display code can use this to detect when to auto-scroll.
    uint64_t lineCount() const { return m_lineCount; }

    // Maximum number of lines to keep (ring-buffer style).
    static constexpr size_t s_maxLines = 200;

private:
    std::deque<std::string> m_lines;
    mutable std::mutex m_mutex;
    uint64_t m_lineCount = 0;
};
