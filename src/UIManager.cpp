#include "UIManager.hpp"
#include <SDL3/SDL_scancode.h>
#include <algorithm>

// ── Stack manipulation ────────────────────────────────────────────────

void UIManager::push(UIEntry entry)
{
    std::lock_guard lock(m_stackMutex);
    m_stack.push_back(std::move(entry));
}

void UIManager::pop()
{
    std::lock_guard lock(m_stackMutex);
    if (!m_stack.empty())
        m_stack.pop_back();
}

void UIManager::clear()
{
    std::lock_guard lock(m_stackMutex);
    m_stack.clear();
}

// ── Stack queries ─────────────────────────────────────────────────────

const UIManager::UIEntry* UIManager::top() const
{
    std::lock_guard lock(m_stackMutex);
    if (m_stack.empty())
        return nullptr;
    return &m_stack.back();
}

bool UIManager::empty() const
{
    std::lock_guard lock(m_stackMutex);
    return m_stack.empty();
}

bool UIManager::hasOverlay(std::string_view type) const
{
    std::lock_guard lock(m_stackMutex);
    for (const auto& entry : m_stack) {
        if (entry.type == UIEntry::Overlay && entry.tag == type)
            return true;
    }
    return false;
}

bool UIManager::hasMenu(std::string_view tag) const
{
    std::lock_guard lock(m_stackMutex);
    for (const auto& entry : m_stack) {
        if (entry.type == UIEntry::Menu && entry.tag == tag)
            return true;
    }
    return false;
}

bool UIManager::shouldBlockGameInput() const
{
    std::lock_guard lock(m_stackMutex);
    return !m_stack.empty();
}

bool UIManager::shouldForwardKeysToServer() const
{
    std::lock_guard lock(m_stackMutex);
    return !m_stack.empty();
}

bool UIManager::isMenuActive() const
{
    std::lock_guard lock(m_stackMutex);
    if (m_stack.empty())
        return false;
    return m_stack.back().type == UIEntry::Menu;
}

// ── Menu navigation keys ──────────────────────────────────────────────

// Tags where '-' key has special meaning (e.g. unwield in inventory).
// In these menus, '-' is forwarded to the server instead of being consumed
// as a page-up navigation key.
bool UIManager::menuHasCustomDash(const std::string& tag)
{
    return tag == "inventory" || tag == "stash"
        || tag == "actions" || tag == "macros"
        || tag == "macro_mapping" || tag == "use_item";
}

bool UIManager::handleMenuNavigationKey(int sdlKeycode, bool /*shift*/)
{
    std::lock_guard lock(m_stackMutex);
    if (m_stack.empty() || m_stack.back().type != UIEntry::Menu)
        return false;

    auto& data = m_stack.back().data;
    int flags = data.value("flags", 0);
    bool arrowsSelect = (flags & 0x40000) != 0; // ARROWS_SELECT
    int totalItems = data.value("total_items", 0);

    switch (sdlKeycode) {
        case SDL_SCANCODE_UP:
            if (arrowsSelect && totalItems > 0) {
                int hover = data.value("last_hovered", -1);
                if (hover <= 0)
                    hover = totalItems - 1;
                else
                    hover = hover - 1;
                data["last_hovered"] = hover;
            }
            // Always consumed (line scroll otherwise)
            return true;

        case SDL_SCANCODE_DOWN:
            if (arrowsSelect && totalItems > 0) {
                int hover = data.value("last_hovered", -1);
                if (hover < 0 || hover >= totalItems - 1)
                    hover = 0;
                else
                    hover = hover + 1;
                data["last_hovered"] = hover;
            }
            return true;

        case SDL_SCANCODE_PAGEUP:
            // Page scroll up — always consumed
            return true;

        case SDL_SCANCODE_PAGEDOWN:
            // Page scroll down — always consumed
            return true;

        case SDL_SCANCODE_HOME:
            // Jump to first — always consumed
            return true;

        case SDL_SCANCODE_END:
            // Jump to last — always consumed
            return true;

        case SDL_SCANCODE_SPACE:
            // Page down — always consumed when menu active
            return true;

        case SDL_SCANCODE_MINUS:
            // Page up UNLESS this menu uses '-' as a custom key binding
            return !menuHasCustomDash(data.value("tag", ""));

        default:
            return false;
    }
}

// ── Message dispatch ──────────────────────────────────────────────────

void UIManager::handleMessage(const json& message)
{
    // All handler methods are called via std::async, so they can run
    // concurrently with other handlers and with the main thread.
    // Protect all m_stack access with the mutex.
    const std::string msgType = message.value("msg", "");

    if (msgType == "menu") {
        UIEntry entry;
        entry.type = UIEntry::Menu;
        entry.tag = message.value("tag", "");
        entry.data = message;
        push(std::move(entry));
    } else if (msgType == "close_menu") {
        pop();
    } else if (msgType == "close_all_menus") {
        clear();
    } else if (msgType == "ui-push") {
        UIEntry entry;
        entry.type = UIEntry::Overlay;
        entry.tag = message.value("type", "");
        entry.data = message;
        push(std::move(entry));
    } else if (msgType == "ui-pop") {
        pop();
    } else if (msgType == "update_menu") {
        std::lock_guard lock(m_stackMutex);
        if (m_stack.empty()) return;
        for (const auto& [key, value] : message.items()) {
            if (key != "msg")
                m_stack.back().data[key] = value;
        }
    } else if (msgType == "update_menu_items") {
        std::lock_guard lock(m_stackMutex);
        if (m_stack.empty() || m_stack.back().type != UIEntry::Menu) return;

        auto& data = m_stack.back().data;
        int total = data.value("total_items", 0);
        int chunkStart = message.value("chunk_start", 0);
        const auto& newItems = message["items"];

        if (!data.contains("items")) {
            data["items"] = json::array();
        }
        auto& items = data["items"];
        if (total > 0) {
            while (items.size() < static_cast<size_t>(total)) {
                items.push_back(json::object());
            }
        }
        for (size_t i = 0; i < newItems.size(); ++i) {
            size_t idx = static_cast<size_t>(chunkStart) + i;
            if (idx < items.size()) {
                items[idx] = newItems[i];
            }
        }
    } else if (msgType == "ui-state") {
        std::lock_guard lock(m_stackMutex);
        if (m_stack.empty() || m_stack.back().type != UIEntry::Overlay) return;

        auto& data = m_stack.back().data;
        int msgGen = message.value("generation_id", -1);
        int topGen = data.value("generation_id", -1);
        if (msgGen >= 0 && topGen >= 0 && msgGen != topGen) return;

        for (const auto& [key, value] : message.items()) {
            if (key != "msg" && key != "type")
                data[key] = value;
        }
    } else if (msgType == "ui-stack") {
        clear();
        if (message.contains("items")) {
            for (const auto& item : message["items"]) {
                handleMessage(item);
            }
        }
    } else if (msgType == "title_prompt") {
        std::lock_guard lock(m_stackMutex);
        if (m_stack.empty() || m_stack.back().type != UIEntry::Menu) return;

        for (const auto& [key, value] : message.items()) {
            if (key != "msg")
                m_stack.back().data[key] = value;
        }
    } else if (msgType == "menu_scroll") {
        std::lock_guard lock(m_stackMutex);
        if (m_stack.empty() || m_stack.back().type != UIEntry::Menu) return;

        auto& data = m_stack.back().data;
        if (message.contains("first"))
            data["first"] = message["first"];
        if (message.contains("last"))
            data["last"] = message["last"];
        if (message.contains("hover"))
            data["last_hovered"] = message["hover"];
    } else if (msgType == "ui-scroller-scroll") {
        std::lock_guard lock(m_stackMutex);
        if (m_stack.empty() || m_stack.back().type != UIEntry::Overlay) return;

        auto& data = m_stack.back().data;
        if (message.contains("scroll"))
            data["scroll"] = message["scroll"];
        if (message.contains("first"))
            data["first"] = message["first"];
        if (message.contains("last"))
            data["last"] = message["last"];
    } else if (msgType == "ui_cutoff") {
        std::lock_guard lock(m_stackMutex);
        if (m_stack.empty()) return;
        int cutoff = message.value("cutoff", 0);
        for (size_t i = 0; i < m_stack.size(); ++i) {
            int distFromBottom = static_cast<int>(m_stack.size() - 1 - i);
            m_stack[i].data["hidden"] = (distFromBottom > cutoff);
        }
    }
}
