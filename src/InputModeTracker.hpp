#pragma once

#include "MessageQueue.hpp"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// Tracks the server's input_mode, which indicates what kind of input the
// game is expecting.  Mirrors the JS client's `set_input_mode()` in game.js.
//
// Modes:
//   NORMAL  (0) — regular gameplay
//   COMMAND (1) — targeting / command mode
//   TARGET  (2) — target selection
//   MORE    (5) — --more-- prompt
//   PROMPT  (7) — text prompt (stat gain, scroll reading, etc.)
//   YESNO   (8) — yes/no confirmation
class InputModeTracker : public MessageHandler {
public:
    // Well-known mouse_mode values from crawl's mouse_control
    enum Mode : int {
        NORMAL = 0,
        COMMAND = 1,
        TARGET = 2,
        MORE = 5,
        PROMPT = 7,
        YESNO = 8,
    };

    InputModeTracker() = default;

    void handleMessage(const json& message) override;

    int currentMode() const { return m_mode; }

    // Returns true when the game is in a mode that accepts normal gameplay
    // input (WASD movement, mouse attacks).  Returns false during prompts,
    // --more-- waits, and yes/no confirmations — these require raw text or
    // specific key responses instead.
    bool isGameplayMode() const;

private:
    int m_mode = NORMAL;
};
