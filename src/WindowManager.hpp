#pragma once

#include "MessageQueue.hpp"
#include <SDL3/SDL_scancode.h>

struct SDL_Window;

// Centralized window visibility and game input mode management.
// Controls which imgui windows are visible and whether game input
// (camera, movement, attacks) should be processed.

class WindowManager : public MessageHandler {
public:
    enum class Mode {
        Login,       // Only network window, no skybox/game (waiting for login_success)
        Normal,      // No imgui windows, game active (relative mouse)
        Overlay,     // All windows visible, game input paused (absolute mouse)
        Equipment,   // Only equipment window, game input paused (absolute mouse)
    };

    static WindowManager& instance();

    Mode getMode() const;
    void setMode(Mode mode);

    // Convenience toggles
    void toggleOverlay();     // Normal <-> Overlay
    void toggleEquipment();   // Normal <-> Equipment

    // Whether any imgui UI should be rendered this frame
    bool shouldRenderUI() const;

    // Whether game input (movement, attacks, camera) should be processed
    bool shouldProcessGameInput() const;

    // Whether the mouse should be in relative mode (game camera control)
    bool shouldUseRelativeMouse() const;

    // Per-window visibility query
    bool isVisible(const char* windowName) const;

    // Handle a key-up event for mode-toggle keys (Escape, E).
    // Syncs mouse mode to the new WindowManager state.
    // Returns true if the event was consumed.
    bool handleKeyEvent(SDL_Scancode scancode, SDL_Window* window);

    // Whether the player has logged into a game session.
    // False only in Login mode — gates skybox rendering.
    bool isGameConnected() const;

    // MessageHandler: responds to "login_success" by transitioning Login -> Overlay.
    void handleMessage(const json& message) override;

private:
    WindowManager() = default;
    Mode m_mode = Mode::Login;
};
