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

void WindowManager::enterQuaffMenu(SDL_Window* window)
{
    if (m_mode == Mode::Normal) {
        m_previousMode = m_mode;
        setMode(Mode::QuaffMenu);
        syncMouseMode(window, shouldUseRelativeMouse());
    }
}

void WindowManager::cancelQuaffMenu(SDL_Window* window)
{
    if (m_mode == Mode::QuaffMenu) {
        setMode(m_previousMode);
        syncMouseMode(window, shouldUseRelativeMouse());
        // Flush accumulated relative mouse motion from absolute-mode
        // movement so the camera doesn't jump on the next frame.
        if (shouldUseRelativeMouse()) {
            SDL_GetRelativeMouseState(nullptr, nullptr);
        }
    }
}

bool WindowManager::shouldRenderUI() const
{
    // Render ImGui in any mode except Normal (and even then, for pinned windows).
    // QuitConfirm and QuaffMenu always need their modal popups.
    return m_mode != Mode::Normal || m_mode == Mode::QuitConfirm
        || m_mode == Mode::QuaffMenu;
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
    case Mode::QuaffMenu:
        // Only the quaff modal is shown
        return false;
    }
    return false;
}

bool WindowManager::isGameConnected() const
{
    // In QuitConfirm, use the mode we'll return to on cancel.
    // This keeps the skybox visible when quitting from an active game
    // but hidden when quitting from the pre-game Login screen.
    Mode effectiveMode = (m_mode == Mode::QuitConfirm || m_mode == Mode::QuaffMenu)
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
    // The player can press Escape to toggle the overlay on.
    if (m_mode == Mode::Login && msgType == "game_started") {
        m_characterSelectData.reset();
        setMode(Mode::Normal);
        return;
    }

    // Character selection complete / game producing map data.
    // Clear the selection UI so only normal game windows are shown.
    if (msgType == "map") {
        m_characterSelectData.reset();
        return;
    }

    // Capture newgame-choice ui-push for the character select window.
    if (msgType == "ui-push" && message.value("type", "") == "newgame-choice") {
        auto choice = parseNewgameChoice(message);
        if (choice.isValid) {
            setCharacterSelectData(choice);
        }
    }
}

void WindowManager::setCharacterSelectData(const NewgameChoice& data)
{
    m_characterSelectData = data;
}

const NewgameChoice* WindowManager::getCharacterSelectData() const
{
    return m_characterSelectData.has_value() ? &*m_characterSelectData : nullptr;
}

void WindowManager::clearCharacterSelectData()
{
    m_characterSelectData.reset();
}

bool WindowManager::handleKeyEvent(SDL_Scancode scancode, SDL_Window* window)
{
    // In Login mode, mode-toggle keys are consumed but do nothing.
    if (m_mode == Mode::Login) {
        switch (scancode) {
        case SDL_SCANCODE_ESCAPE:
        case SDL_SCANCODE_E:
        case SDL_SCANCODE_F1:
            return true; // consumed, no-op
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

    // In QuaffMenu mode, Escape cancels the quaff menu.
    if (m_mode == Mode::QuaffMenu) {
        if (scancode == SDL_SCANCODE_ESCAPE) {
            cancelQuaffMenu(window);
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
