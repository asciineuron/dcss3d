#include "WindowManager.hpp"
#include <SDL3/SDL.h>
#include <spdlog/spdlog.h>

WindowManager& WindowManager::instance()
{
    static WindowManager s_instance;
    return s_instance;
}

WindowManager::Mode WindowManager::getMode() const
{
    return m_mode;
}

void WindowManager::setMode(Mode mode)
{
    if (m_mode != mode) {
        spdlog::debug("WindowManager: mode {} -> {}", static_cast<int>(m_mode), static_cast<int>(mode));
        m_mode = mode;
    }
}

void WindowManager::toggleOverlay()
{
    if (m_mode == Mode::Overlay) {
        setMode(Mode::Normal);
    } else {
        setMode(Mode::Overlay);
    }
}

void WindowManager::toggleEquipment()
{
    if (m_mode == Mode::Equipment) {
        setMode(Mode::Normal);
    } else {
        setMode(Mode::Equipment);
    }
}

static void syncMouseMode(SDL_Window* window, bool useRelative)
{
    if (!window)
        return;
    if (useRelative) {
        SDL_SetWindowRelativeMouseMode(window, true);
    } else {
        SDL_SetWindowRelativeMouseMode(window, false);
        SDL_ShowCursor();
    }
}

void WindowManager::enterQuitConfirm(SDL_Window* window)
{
    if (m_mode != Mode::QuitConfirm) {
        m_previousMode = m_mode;
        setMode(Mode::QuitConfirm);
        syncMouseMode(window, false); // absolute mouse for button clicks
    }
}

void WindowManager::cancelQuitConfirm(SDL_Window* window)
{
    if (m_mode == Mode::QuitConfirm) {
        setMode(m_previousMode);
        syncMouseMode(window, shouldUseRelativeMouse());
    }
}

void WindowManager::confirmQuit()
{
    m_quitConfirmed = true;
}

bool WindowManager::isQuitConfirmed() const
{
    return m_quitConfirmed;
}

bool WindowManager::shouldRenderUI() const
{
    // Render ImGui in any mode except Normal.
    // QuitConfirm always needs its modal popup.
    return m_mode != Mode::Normal || m_mode == Mode::QuitConfirm;
}

bool WindowManager::shouldProcessGameInput() const
{
    // Only process game input in Normal mode
    return m_mode == Mode::Normal;
}

bool WindowManager::shouldUseRelativeMouse() const
{
    // Relative mouse only in Normal mode (game camera control)
    return m_mode == Mode::Normal;
}

bool WindowManager::isVisible(const char* windowName) const
{
    switch (m_mode) {
    case Mode::Login:
        // Only the network window is visible before login
        return (windowName != nullptr) && (std::string(windowName) == "network");
    case Mode::Overlay:
        // All windows visible in overlay mode
        return true;
    case Mode::Equipment:
        // Only the equipment window is visible
        return (windowName != nullptr) && (std::string(windowName) == "equipment");
    case Mode::Normal:
        // No windows visible (pins handled separately by caller)
        return false;
    case Mode::QuitConfirm:
        // Only the quit confirmation modal is shown — no regular windows
        return false;
    }
    return false;
}

bool WindowManager::isGameConnected() const
{
    // In QuitConfirm, use the mode we'll return to on cancel.
    // This keeps the skybox visible when quitting from an active game
    // but hidden when quitting from the pre-game Login screen.
    Mode effectiveMode = (m_mode == Mode::QuitConfirm)
        ? m_previousMode : m_mode;
    return effectiveMode != Mode::Login;
}

bool WindowManager::isLoggedIn() const
{
    return m_isLoggedIn;
}

void WindowManager::handleMessage(const json& message)
{
    auto msg = message.find("msg");
    if (msg == message.end())
        return;

    const std::string msgType = msg->get<std::string>();

    // Track login state
    if (msgType == "login_success") {
        m_isLoggedIn = true;
        return;
    }

    // Game process started — transition to Normal (playable); skybox becomes visible.
    if (m_mode == Mode::Login && msgType == "game_started") {
        setMode(Mode::Normal);
        return;
    }
}

bool WindowManager::handleKeyEvent(SDL_Scancode scancode, SDL_Window* window)
{
    // In Login mode, F1 toggles overlay so the user can access
    // the Network window.  ESC and E are still no-ops.
    if (m_mode == Mode::Login) {
        switch (scancode) {
        case SDL_SCANCODE_ESCAPE:
        case SDL_SCANCODE_E:
            return true; // consumed, no-op
        case SDL_SCANCODE_F1:
            toggleOverlay();
            syncMouseMode(window, shouldUseRelativeMouse());
            return true;
        default:
            return false;
        }
    }

    // In QuitConfirm mode, Escape cancels the quit dialog.
    if (m_mode == Mode::QuitConfirm) {
        if (scancode == SDL_SCANCODE_ESCAPE) {
            cancelQuitConfirm(window);
            return true;
        }
        return false;
    }

    // Normal / Overlay / Equipment mode
    switch (scancode) {
    case SDL_SCANCODE_ESCAPE:
        // ESC: close equipment if open, otherwise NOT handled here.
        // In Normal mode, ESC is sent to the server (see main event loop).
        if (m_mode == Mode::Equipment) {
            setMode(Mode::Normal);
            syncMouseMode(window, shouldUseRelativeMouse());
            return true;
        }
        return false; // let main loop send ESC to server
    case SDL_SCANCODE_E:
        toggleEquipment();
        break;
    case SDL_SCANCODE_F1:
        // F1 toggles the debug overlay (moved from ESC to avoid conflict)
        toggleOverlay();
        break;
    default:
        return false;
    }

    syncMouseMode(window, shouldUseRelativeMouse());
    return true;
}
