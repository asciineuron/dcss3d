#include "UIManager.hpp"
#include "CharacterSelect.hpp"
#include "DescriptionManager.hpp"
#include "MessageQueue.hpp"
#include "PlayerState.hpp"
#include "imguilayouts.hpp"
#include "WindowManager.hpp"
#include "imgui.h"
#include "imgui_stdlib.h"
#include <SDL3/SDL.h>
#include <spdlog/spdlog.h>

namespace {

// DCSS sends title as either a plain string or an object {"text":"..."}.
// Extract the display text regardless of format.
static std::string extractTitle(const json& data, const std::string& key,
                                const std::string& fallback = "")
{
    if (!data.contains(key))
        return fallback;
    const auto& val = data[key];
    if (val.is_string())
        return val.get<std::string>();
    if (val.is_object() && val.contains("text") && val["text"].is_string())
        return val["text"].get<std::string>();
    return fallback;
}

// Color tag stripping is provided by DescriptionManager::DescriptionManager::stripColorTags().

// ── Per-tag menu renderers ───────────────────────────────────────────

void renderMenu_generic(const UIManager::UIEntry& entry)
{
    const auto& data = entry.data;
    std::string title = extractTitle(data, "title", entry.tag);
    std::string cleanTitle = DescriptionManager::stripColorTags(title);
    std::string popupTitle = std::format("{}##menu_{}", cleanTitle, entry.tag);

    ImGui::SetNextWindowSize(ImVec2(380, 400), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImVec2(200, 150), ImGuiCond_Appearing);
    ImGui::PushTextWrapPos(360.0f);
    if (ImGui::Begin(popupTitle.c_str(), nullptr,
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {

        if (!cleanTitle.empty()) {
            ImGui::TextUnformatted(cleanTitle.c_str());
            ImGui::Separator();
        }

        if (data.contains("items") && data["items"].is_array()) {
            const auto& items = data["items"];
            for (size_t i = 0; i < items.size(); ++i) {
                const auto& item = items[i];
                int level = item.value("level", 2);
                std::string text = DescriptionManager::stripColorTags(item.value("text", ""));

                std::string hotkeyStr;
                if (item.contains("hotkeys") && item["hotkeys"].is_array()) {
                    for (const auto& hk : item["hotkeys"]) {
                        int kc = hk.get<int>();
                        if (kc >= 32 && kc < 127)
                            hotkeyStr += static_cast<char>(kc);
                    }
                }

                if (level < 2) {
                    ImGui::TextDisabled("%s", text.c_str());
                } else {
                    std::string label;
                    if (!hotkeyStr.empty())
                        label = std::format("[{}] {}", hotkeyStr, text);
                    else
                        label = text;

                    if (ImGui::Selectable(label.c_str())) {
                        if (!hotkeyStr.empty()) {
                            spdlog::debug("Menu click: sending keycode {}",
                                static_cast<int>(hotkeyStr[0]));
                        }
                    }
                }
            }
        }

        if (data.contains("more") && !data["more"].get<std::string>().empty()) {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextDisabled("%s", DescriptionManager::stripColorTags(data["more"].get<std::string>()).c_str());
        }

        if (data.contains("prompt") && data["prompt"].is_string()) {
            ImGui::Spacing();
            ImGui::Text("%s", data["prompt"].get<std::string>().c_str());
            static char filterBuf[256] = {};
            ImGui::SetKeyboardFocusHere();
            if (ImGui::InputText("##menu_filter", filterBuf, sizeof(filterBuf),
                    ImGuiInputTextFlags_EnterReturnsTrue)) {
                // Send filter input — caller handles networking
            }
        }

        ImGui::End();
    }
    ImGui::PopTextWrapPos();
}

// Inventory-style menu (drop, wield, inventory tag).
// Items with level < 2 are section headers; others are clickable.
void renderMenu_inventory(const UIManager::UIEntry& entry, NetworkManager& net)
{
    const auto& data = entry.data;
    std::string title = extractTitle(data, "title", "Inventory");
    std::string cleanTitle = DescriptionManager::stripColorTags(title);
    std::string popupTitle = std::format("{}##menu_{}", cleanTitle, entry.tag);

    ImGui::SetNextWindowSize(ImVec2(380, 400), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImVec2(200, 150), ImGuiCond_Appearing);
    ImGui::PushTextWrapPos(360.0f);
    if (ImGui::Begin(popupTitle.c_str(), nullptr,
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {

        if (!cleanTitle.empty()) {
            ImGui::TextUnformatted(cleanTitle.c_str());
            ImGui::Separator();
        }

        if (data.contains("items") && data["items"].is_array()) {
            const auto& items = data["items"];
            for (size_t i = 0; i < items.size(); ++i) {
                const auto& item = items[i];
                int level = item.value("level", 2);
                std::string text = DescriptionManager::stripColorTags(item.value("text", ""));

                std::string hotkeyStr;
                if (item.contains("hotkeys") && item["hotkeys"].is_array()) {
                    for (const auto& hk : item["hotkeys"]) {
                        int kc = hk.get<int>();
                        if (kc >= 32 && kc < 127)
                            hotkeyStr += static_cast<char>(kc);
                    }
                }

                if (level < 2) {
                    ImGui::TextDisabled("%s", text.c_str());
                } else {
                    std::string label;
                    if (!hotkeyStr.empty())
                        label = std::format("[{}] {}", hotkeyStr, text);
                    else
                        label = text;

                    if (ImGui::Selectable(label.c_str())) {
                        if (!hotkeyStr.empty()) {
                            int hotkey = item["hotkeys"][0].get<int>();
                            if (hotkey >= 32 && hotkey < 127) {
                                json msg = {{"msg","input"},
                                    {"text",std::string(1,static_cast<char>(hotkey))}};
                                net.sendMessage(msg);
                                spdlog::debug("Menu click: sent input '{}'",
                                    static_cast<char>(hotkey));
                            }
                        }
                    }
                }
            }
        }

        if (data.contains("more") && !data["more"].get<std::string>().empty()) {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextDisabled("%s", DescriptionManager::stripColorTags(data["more"].get<std::string>()).c_str());
        }

        ImGui::End();
    }
    ImGui::PopTextWrapPos();
}

// use_item menu — potions, scrolls, evocables.
// Supports click-to-select and displays hotkey letters.
void renderMenu_useItem(const UIManager::UIEntry& entry, NetworkManager& net)
{
    const auto& data = entry.data;
    std::string title = extractTitle(data, "title", "Use which item?");
    std::string cleanTitle = DescriptionManager::stripColorTags(title);
    std::string popupTitle = std::format("{}##menu_{}", cleanTitle, entry.tag);

    ImGui::SetNextWindowSize(ImVec2(380, 400), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImVec2(200, 150), ImGuiCond_Appearing);
    ImGui::PushTextWrapPos(360.0f);
    if (ImGui::Begin(popupTitle.c_str(), nullptr,
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {

        if (!cleanTitle.empty()) {
            ImGui::TextUnformatted(cleanTitle.c_str());
            ImGui::Separator();
        }

        if (data.contains("items") && data["items"].is_array()) {
            const auto& items = data["items"];
            for (size_t i = 0; i < items.size(); ++i) {
                const auto& item = items[i];
                int level = item.value("level", 2);
                std::string text = DescriptionManager::stripColorTags(item.value("text", ""));

                std::string hotkeyStr;
                if (item.contains("hotkeys") && item["hotkeys"].is_array()) {
                    for (const auto& hk : item["hotkeys"]) {
                        int kc = hk.get<int>();
                        if (kc >= 32 && kc < 127)
                            hotkeyStr += static_cast<char>(kc);
                    }
                }

                if (level < 2) {
                    ImGui::TextDisabled("%s", text.c_str());
                } else {
                    std::string label;
                    if (!hotkeyStr.empty())
                        label = std::format("[{}] {}", hotkeyStr, text);
                    else
                        label = text;

                    if (ImGui::Selectable(label.c_str())) {
                        if (!hotkeyStr.empty()) {
                            int hotkey = item["hotkeys"][0].get<int>();
                            if (hotkey >= 32 && hotkey < 127) {
                                json msg = {{"msg","input"},
                                    {"text",std::string(1,static_cast<char>(hotkey))}};
                                net.sendMessage(msg);
                                spdlog::debug("use_item click: sent input '{}'",
                                    static_cast<char>(hotkey));
                            }
                        }
                    }
                }
            }
        }

        if (data.contains("more") && !data["more"].get<std::string>().empty()) {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextDisabled("%s", DescriptionManager::stripColorTags(data["more"].get<std::string>()).c_str());
        }

        ImGui::End();
    }
    ImGui::PopTextWrapPos();
}

} // namespace

// ── Public render entry point ───────────────────────────────────────────

void UIManager::render(const Player& player, NetworkManager& net,
                       SDL_Window* /*window*/, DescriptionManager& descMgr)
{
    // Lock while we snapshot the top entry to render.  Handlers run on
    // async threads and can modify m_stack concurrently.
    std::lock_guard lock(m_stackMutex);
    if (m_stack.empty()) return;

    const auto& top = m_stack.back();
    spdlog::debug("UIManager::render: stack size={}, top type={}, tag={}",
        m_stack.size(), static_cast<int>(top.type), top.tag);

    if (top.type == UIEntry::Menu) {
        const std::string& tag = top.tag;
        if (tag == "use_item") {
            renderMenu_useItem(top, net);
        } else if (tag == "inventory") {
            renderMenu_inventory(top, net);
        } else {
            renderMenu_generic(top);
        }

    } else if (top.type == UIEntry::Overlay) {
        const std::string& type = top.tag;

        if (type == "describe-item") {
            if (descMgr.hasDescription()) {
                std::string title = descMgr.itemName();
                if (title.empty()) title = "Item Description";
                ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_Appearing);
                ImGui::SetNextWindowPos(ImVec2(250, 100), ImGuiCond_Appearing);
                if (ImGui::Begin(title.c_str(), nullptr,
                        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 40.0f);
                    ImGui::TextUnformatted(descMgr.description().c_str());
                    ImGui::PopTextWrapPos();
                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Text("[ESC to close]");
                }
                ImGui::End();
            }
        } else if (type == "newgame-choice") {
            auto choice = parseNewgameChoice(top.data);
            if (choice.isValid) {
                characterSelectWindow(choice, net);
            }
        } else if (type == "progress-bar") {
            std::string title = extractTitle(top.data, "title", "Progress");
            ImGui::SetNextWindowSize(ImVec2(350, 120), ImGuiCond_Appearing);
            ImGui::SetNextWindowPos(ImVec2(250, 200), ImGuiCond_Appearing);
            if (ImGui::Begin(title.c_str(), nullptr,
                    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
                std::string status = top.data.value("status", "Working...");
                ImGui::TextUnformatted(status.c_str());
                if (top.data.contains("bar_text") && top.data["bar_text"].is_string()) {
                    ImGui::Spacing();
                    ImGui::TextUnformatted(
                        top.data["bar_text"].get<std::string>().c_str());
                }
            }
            ImGui::End();
        } else if (type == "game-over") {
            std::string popupId = std::format("Game Over##overlay_{}", type);
            ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_Appearing);
            ImGui::SetNextWindowPos(ImVec2(250, 150), ImGuiCond_Appearing);
            if (ImGui::Begin(popupId.c_str(), nullptr,
                    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
                std::string gameOverTitle = extractTitle(top.data, "title");
                if (!gameOverTitle.empty()) {
                    ImGui::TextUnformatted(gameOverTitle.c_str());
                    ImGui::Separator();
                }
                if (top.data.contains("body") && top.data["body"].is_string()) {
                    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 40.0f);
                    ImGui::TextUnformatted(
                        top.data["body"].get<std::string>().c_str());
                    ImGui::PopTextWrapPos();
                }
            }
            ImGui::End();
        } else {
            // Generic fallback for all other overlay types.
            std::string popupId = std::format("{}##overlay_{}", type, type);
            ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_Appearing);
            ImGui::SetNextWindowPos(ImVec2(250, 150), ImGuiCond_Appearing);
            if (ImGui::Begin(popupId.c_str(), nullptr,
                    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("Overlay: %s", type.c_str());
                std::string genericTitle = extractTitle(top.data, "title");
                if (!genericTitle.empty()) {
                    ImGui::Separator();
                    ImGui::TextUnformatted(genericTitle.c_str());
                }
                if (top.data.contains("body")) {
                    ImGui::Spacing();
                    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 40.0f);
                    ImGui::TextUnformatted(top.data["body"].get<std::string>().c_str());
                    ImGui::PopTextWrapPos();
                }
            }
            ImGui::End();
        }
    }
}
