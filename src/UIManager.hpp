#pragma once

#include "MessageQueue.hpp"
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

class NetworkManager;
class Player;
struct SDL_Window;
class DescriptionManager;

// Server-driven UI stack manager.
//
// UIManager maintains a stack of UI entries (menus and overlays) that mirrors
// the JS client's #ui-stack.  The server is the single source of truth for UI
// state — we never decide on our own to show/hide windows.  Keys go to the
// server except for menu navigation keys (arrows, pgup/pgdn, etc.) which are
// handled locally via handleMenuNavigationKey().
//
// This header intentionally does NOT include any imgui or SDL headers so that
// the state management can be compiled into the test executable without those
// dependencies.  The render() method is declared here but defined in the
// separate UIManagerRender.cpp translation unit.
class UIManager : public MessageHandler {
public:
    // One entry in the UI stack — either an interactive selection menu or an
    // informational overlay.
    struct UIEntry {
        enum Type {
            Menu,    // interactive selection (quaff, drop, inventory, actions)
            Overlay, // informational (describe-item, progress-bar, --more--)
        };

        Type type;
        std::string tag;  // for Menu: the "tag" field (e.g. "use_item")
                          // for Overlay: the "type" field (e.g. "describe-item")
        json data;        // the full server message
    };

    UIManager() = default;

    // Stack manipulation — these are called by handleMessage() dispatch and
    // are also available for direct use in tests.
    void push(UIEntry entry);
    void pop();
    void clear();  // close_all_menus semantics

    // Stack queries
    const UIEntry* top() const;
    bool empty() const;

    // Search the entire stack for a specific overlay type or menu tag
    bool hasOverlay(std::string_view type) const;
    bool hasMenu(std::string_view tag) const;

    // Whether game input (WASD, mouse) should be blocked.
    // True when any UI is on the stack.
    bool shouldBlockGameInput() const;

    // Whether the event loop should forward all keypresses to the server.
    // True when a menu or overlay is the top of stack (server needs keys).
    bool shouldForwardKeysToServer() const;

    // Whether the top of stack is a menu (for local menu navigation).
    bool isMenuActive() const;

    // Navigation key handling for active menus.
    // Returns true if the navigation key was consumed (locally handled).
    // Handles: arrows (with ARROWS_SELECT flag), pgup/pgdn, home, end,
    // space, and minus (custom-dash permitting).
    bool handleMenuNavigationKey(int sdlKeycode, bool shift);

    // MessageHandler: processes all UI-related server messages.
    void handleMessage(const json& message) override;

    // Render all active UI elements (defined in UIManagerRender.cpp).
    // Only renders the topmost stack entry.  Underlying entries are hidden.
    void render(const Player& player, NetworkManager& net,
                SDL_Window* window, DescriptionManager& descMgr);

private:
    std::vector<UIEntry> m_stack;
    mutable std::mutex m_stackMutex; // protects m_stack (handlers run via std::async)

    // Returns true if the given menu tag uses '-' as a custom key binding
    // (e.g. unwield in inventory).  In these menus, '-' is NOT consumed by
    // local navigation — it is forwarded to the server.
    static bool menuHasCustomDash(const std::string& tag);
};
