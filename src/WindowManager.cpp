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

bool WindowManager::shouldRenderUI() const
{
    // In any mode except Normal, we render ImGui.
    // In Normal mode, pinned windows may still request rendering.
    return m_mode != Mode::Normal;
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
    case Mode::Overlay:
        // All windows visible in overlay mode
        return true;
    case Mode::Equipment:
        // Only the equipment window is visible
        return (windowName != nullptr) && (std::string(windowName) == "equipment");
    case Mode::Normal:
        // No windows visible (pins handled separately by caller)
        return false;
    }
    return false;
}

bool WindowManager::handleKeyEvent(SDL_Scancode scancode, SDL_Window* window)
{
    switch (scancode) {
    case SDL_SCANCODE_ESCAPE:
        if (m_mode == Mode::Equipment) {
            setMode(Mode::Normal);
        } else {
            toggleOverlay();
        }
        break;
    case SDL_SCANCODE_E:
        toggleEquipment();
        break;
    default:
        return false;
    }

    // Sync mouse mode to the new WindowManager state
    if (window) {
        if (shouldUseRelativeMouse()) {
            SDL_SetWindowRelativeMouseMode(window, true);
        } else {
            SDL_SetWindowRelativeMouseMode(window, false);
            SDL_ShowCursor();
        }
    }
    return true;
}
