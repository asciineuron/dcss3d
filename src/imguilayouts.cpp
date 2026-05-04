#include "imguilayouts.hpp"
#include "MessageQueue.hpp"
#include "Turn.hpp"
#include "WindowManager.hpp"
#include "debug.hpp"
#include "imgui.h"
#include "imgui_stdlib.h"
#include <algorithm>
#include <spdlog/spdlog.h>
#include <format>
#include <fstream>
#include <mutex>
#include <ranges>
#include <sstream>
#include <unordered_map>

namespace {
// Action to perform this frame
enum class LayoutAction {
    None,
    Reset,
    Load
};
LayoutAction g_pendingAction = LayoutAction::None;

// Saved layout data
std::vector<WindowLayout> g_savedLayout;
const char* g_pendingLayoutFilename = nullptr;

// Callback to get current window layout
WindowLayoutCallback g_windowLayoutCallback = nullptr;

// Pin state: window name -> is pinned.
// Map and player windows start pinned so they're visible in Normal mode.
std::unordered_map<std::string, bool> g_pinState = {
    {"map", true},
    {"player", true},
};

// Strip DCSS color tags like <brown>, <lightgreen>, <w> from a string
std::string stripColorTags(const std::string& str)
{
    std::string out;
    out.reserve(str.size());
    bool inTag = false;
    for (char c : str) {
        if (c == '<') {
            inTag = true;
        } else if (c == '>') {
            inTag = false;
        } else if (!inTag) {
            out.push_back(c);
        }
    }
    return out;
}
} // namespace

// --- Pin state management ---

bool isWindowPinned(const char* windowName)
{
    auto it = g_pinState.find(windowName);
    return it != g_pinState.end() && it->second;
}

void toggleWindowPin(const char* windowName)
{
    g_pinState[windowName] = !isWindowPinned(windowName);
    spdlog::debug("Window '{}' pinned: {}", windowName, g_pinState[windowName]);
}

bool anyWindowsPinned()
{
    for (const auto& [name, pinned] : g_pinState) {
        if (pinned) return true;
    }
    return false;
}

bool PinButton(const char* windowName)
{
    bool isPinned = isWindowPinned(windowName);

    ImVec4 pinColor = isPinned ? ImVec4(0.3f, 0.6f, 1.0f, 1.0f) : ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, pinColor);

    bool clicked = ImGui::SmallButton(isPinned ? "*" : "o");

    ImGui::PopStyleColor();

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip(isPinned ? "Unpin window" : "Pin window (stays visible when overlay hidden)");
    }

    if (clicked) {
        toggleWindowPin(windowName);
        return !isPinned;
    }
    return isPinned;
}

// --- Window display orchestrator ---

// Apply a single window's layout (position and size) — used by displayAllWindows
static void applyWindowLayout(const char* windowName, const WindowLayout& layout)
{
    spdlog::debug("Applying layout for '{}': pos=({},{}), size=({},{}), valid={}",
                  windowName, layout.posX, layout.posY, layout.sizeX, layout.sizeY, layout.isValid);
    ImGui::SetNextWindowPos(ImVec2(layout.posX, layout.posY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(layout.sizeX, layout.sizeY), ImGuiCond_Always);
    if (layout.isCollapsed) {
        ImGui::SetNextWindowCollapsed(true, ImGuiCond_Always);
    }
}

// Apply reset layout (cascading from top-left, auto-sized)
static void applyResetLayout(const char* windowName, int index)
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 basePos = viewport->Pos;
    float offset = 60.0f * index;
    ImGui::SetNextWindowPos(ImVec2(basePos.x + offset, basePos.y + offset), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(0, 0), ImGuiCond_Always);
}

void displayAllWindows(const Player& player, const GameMap& map,
                       NetworkManager& networkManager, Renderer& renderer,
                       const char* layoutFilename)
{
    WindowManager& wm = WindowManager::instance();

    // Quit confirmation popup lifecycle.
    // OpenPopup on entry, CloseCurrentPopup on exit, BeginPopupModal only while active.
    {
        static WindowManager::Mode s_lastMode = WindowManager::Mode::Normal;

        // Entering QuitConfirm: open the popup
        if (wm.getMode() == WindowManager::Mode::QuitConfirm
            && s_lastMode != WindowManager::Mode::QuitConfirm) {
            ImGui::OpenPopup("Quit Game?");
        }

        // Leaving QuitConfirm: close the popup (handles Escape closing it via ImGui)
        if (s_lastMode == WindowManager::Mode::QuitConfirm
            && wm.getMode() != WindowManager::Mode::QuitConfirm) {
            ImGui::CloseCurrentPopup();
        }

        s_lastMode = wm.getMode();
    }

    const bool inQuitConfirm = (wm.getMode() == WindowManager::Mode::QuitConfirm);
    if (inQuitConfirm) {
        if (ImGui::BeginPopupModal("Quit Game?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Are you sure you want to quit?");
            ImGui::Spacing();
            if (ImGui::Button("Yes, Quit", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
                wm.confirmQuit();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
                wm.cancelQuitConfirm(renderer.window());
            }
            ImGui::EndPopup();
        }
        return;  // block other windows
    }

    // Character select takes priority during Login phase
    if (const auto* choice = wm.getCharacterSelectData()) {
        characterSelectWindow(*choice, networkManager);
        // Still show network window for status
        networkMenu(networkManager);
        return;
    }

    const bool shouldReset = windowLayoutNeedsReset();
    const WindowLayout* pendingLayout = getPendingLayout();
    const size_t pendingLayoutCount = getPendingLayoutCount();

    if (pendingLayout && pendingLayoutCount > 0) {
        spdlog::debug("Pending layout load: {} windows", pendingLayoutCount);
    }

    // Helper lambda to find and apply layout for a window
    auto applyLayoutForWindow = [](const char* windowName, const WindowLayout* layout, size_t count) {
        for (size_t i = 0; i < count; ++i) {
            if (layout[i].name == windowName && layout[i].isValid) {
                spdlog::debug("  Applying saved layout for '{}'", windowName);
                applyWindowLayout(windowName, layout[i]);
                break;
            }
        }
    };

    const bool normalMode = (wm.getMode() == WindowManager::Mode::Normal);

    // Helper macro for window visibility gating:
    // Visible if WindowManager says so, OR if we're in Normal mode and it's pinned.
    auto showWindow = [&](const char* name) -> bool {
        return wm.isVisible(name) || (normalMode && isWindowPinned(name));
    };

    // Player window (index 0)
    if (showWindow("player")) {
        if (shouldReset) {
            applyResetLayout("player", 0);
        } else if (pendingLayout && pendingLayoutCount > 0) {
            applyLayoutForWindow("player", pendingLayout, pendingLayoutCount);
        }
        displayPlayer(player);
    }

    // Map window (index 1)
    if (showWindow("map")) {
        if (shouldReset) {
            applyResetLayout("map", 1);
        } else if (pendingLayout && pendingLayoutCount > 0) {
            applyLayoutForWindow("map", pendingLayout, pendingLayoutCount);
        }
        displayMap(map, player);
    }

    // Network window (index 2)
    if (showWindow("network")) {
        if (shouldReset) {
            applyResetLayout("network", 2);
        } else if (pendingLayout && pendingLayoutCount > 0) {
            applyLayoutForWindow("network", pendingLayout, pendingLayoutCount);
        }
        networkMenu(networkManager);
    }

    // Renderer window (index 3)
    if (showWindow("renderer")) {
        if (shouldReset) {
            applyResetLayout("renderer", 3);
        } else if (pendingLayout && pendingLayoutCount > 0) {
            applyLayoutForWindow("renderer", pendingLayout, pendingLayoutCount);
        }
        renderMenu(renderer);
    }

    // Settings window (index 4)
    if (showWindow("settings")) {
        if (shouldReset) {
            applyResetLayout("settings", 4);
        } else if (pendingLayout && pendingLayoutCount > 0) {
            applyLayoutForWindow("settings", pendingLayout, pendingLayoutCount);
        }
        settingsMenu(layoutFilename);
    }

    // Equipment window (index 5)
    if (showWindow("equipment")) {
        if (shouldReset) {
            applyResetLayout("equipment", 5);
        } else if (pendingLayout && pendingLayoutCount > 0) {
            applyLayoutForWindow("equipment", pendingLayout, pendingLayoutCount);
        }
        displayEquipment(player);
    }

    if (shouldReset || (pendingLayout && pendingLayoutCount > 0)) {
        clearPendingLayoutAction();
    }
}

void registerWindowForReset(const char* windowName)
{
    // Prevent duplicate registrations
    // (We don't track registered names anymore, just register at startup)
}

void setWindowLayoutCallback(WindowLayoutCallback callback)
{
    g_windowLayoutCallback = callback;
}

bool windowLayoutNeedsReset()
{
    return g_pendingAction == LayoutAction::Reset;
}

const WindowLayout* getPendingLayout()
{
    return (g_pendingAction == LayoutAction::Load && !g_savedLayout.empty()) ? g_savedLayout.data() : nullptr;
}

size_t getPendingLayoutCount()
{
    return (g_pendingAction == LayoutAction::Load) ? g_savedLayout.size() : 0;
}

const char* getPendingLayoutName()
{
    if (g_pendingAction != LayoutAction::Load || g_savedLayout.empty()) return nullptr;
    return g_savedLayout[0].name.c_str();
}

void clearPendingLayoutAction()
{
    g_pendingAction = LayoutAction::None;
}

void resetWindowLayout()
{
    g_savedLayout.clear();
    g_pendingAction = LayoutAction::Reset;
}

bool hasSavedLayout(const char* filename)
{
    std::ifstream file(filename);
    return file.good();
}

bool saveWindowLayout(const char* filename)
{
    if (!g_windowLayoutCallback) {
        spdlog::error("Window layout callback not set, cannot save");
        return false;
    }

    // Get all window names and their current layouts
    const char* windowNames[] = {
        "player", "map", "network", "renderer", "settings", "equipment"
    };

    std::ostringstream json;
    json << "{\n";
    json << "  \"windows\": [\n";

    bool first = true;
    for (const char* name : windowNames) {
        WindowLayout layout = g_windowLayoutCallback(name);
        
        if (!first) json << ",\n";
        first = false;
        
        json << "    {\n";
        json << "      \"name\": \"" << layout.name << "\",\n";
        json << "      \"posX\": " << layout.posX << ",\n";
        json << "      \"posY\": " << layout.posY << ",\n";
        json << "      \"sizeX\": " << layout.sizeX << ",\n";
        json << "      \"sizeY\": " << layout.sizeY << ",\n";
        json << "      \"isCollapsed\": " << (layout.isCollapsed ? "true" : "false") << "\n";
        json << "    }";
    }

    json << "\n  ]\n";
    json << "}\n";

    std::ofstream file(filename);
    if (!file.is_open()) {
        spdlog::error("Failed to open file for saving layout: {}", filename);
        return false;
    }

    file << json.str();
    file.close();

    spdlog::info("Window layout saved to: {}", filename);
    return true;
}

bool loadWindowLayout(const char* filename)
{
    std::ifstream file(filename);
    if (!file.is_open()) {
        spdlog::error("Failed to open layout file: {}", filename);
        return false;
    }

    // Parse JSON manually (simple parser for our format)
    g_savedLayout.clear();

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    file.close();

    // Simple JSON parsing - look for window entries
    // Format: "name": "xxx", "posX": x.x, "posY": y.y, "sizeX": w.w, "sizeY": h.h, "isCollapsed": true/false
    
    size_t pos = 0;
    while (true) {
        // Find next "name": "
        size_t nameStart = content.find("\"name\": \"", pos);
        if (nameStart == std::string::npos) break;
        
        WindowLayout layout;
        
        // Extract name
        size_t nameQuote = nameStart + 9;
        size_t nameEnd = content.find("\"", nameQuote);
        layout.name = content.substr(nameQuote, nameEnd - nameQuote);
        
        // Extract posX
        size_t posXStart = content.find("\"posX\":", nameEnd);
        if (posXStart == std::string::npos) break;
        posXStart += 7;
        while (isspace(content[posXStart])) posXStart++;
        layout.posX = std::stof(content.substr(posXStart));
        
        // Extract posY
        size_t posYStart = content.find("\"posY\":", posXStart);
        posYStart += 7;
        while (isspace(content[posYStart])) posYStart++;
        layout.posY = std::stof(content.substr(posYStart));
        
        // Extract sizeX
        size_t sizeXStart = content.find("\"sizeX\":", posYStart);
        sizeXStart += 8;
        while (isspace(content[sizeXStart])) sizeXStart++;
        layout.sizeX = std::stof(content.substr(sizeXStart));
        
        // Extract sizeY
        size_t sizeYStart = content.find("\"sizeY\":", sizeXStart);
        sizeYStart += 8;
        while (isspace(content[sizeYStart])) sizeYStart++;
        layout.sizeY = std::stof(content.substr(sizeYStart));
        
        // Extract isCollapsed
        size_t collapsedStart = content.find("\"isCollapsed\":", sizeYStart);
        collapsedStart += 14;
        while (isspace(content[collapsedStart])) collapsedStart++;
        layout.isCollapsed = (content.substr(collapsedStart, 4) == "true");
        
        layout.isValid = true;
        g_savedLayout.push_back(layout);
        
        pos = collapsedStart + 4;
    }

    if (g_savedLayout.empty()) {
        spdlog::error("Failed to parse layout file: {}", filename);
        return false;
    }

    g_pendingAction = LayoutAction::Load;
    g_pendingLayoutFilename = filename;
    
    spdlog::info("Window layout loaded from: {} ({} windows)", filename, g_savedLayout.size());
    return true;
}

void settingsMenu(const char* layoutFilename)
{
    ImGui::Begin("settings");
    PinButton("settings");
    ImGui::Spacing();

    ImGui::SeparatorText("Window Layout");
    ImGui::Spacing();

    // Save Layout button
    if (ImGui::Button("Save Layout")) {
        if (saveWindowLayout(layoutFilename)) {
            // Success - could show notification
        }
    }
    ImGui::SameLine();

    // Load Layout button
    bool hasLayout = hasSavedLayout(layoutFilename);
    if (ImGui::Button("Load Layout", ImVec2(100, 0))) {
        loadWindowLayout(layoutFilename);
    }
    ImGui::SameLine();
    if (!hasLayout) {
        ImGui::TextDisabled("(no saved layout)");
    }

    // Reset Layout button
    ImGui::Spacing();
    if (ImGui::Button("Reset Layout to Defaults")) {
        resetWindowLayout();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(cascading positions, auto-sizes)");

    // Show current layout filename
    ImGui::Spacing();
    ImGui::Text("Layout file: %s", layoutFilename);

    ImGui::End();
}

void displayPlayer(const Player& player)
{
    ImGui::Begin("player");
    PinButton("player");
    ImGui::Spacing();

    const auto& d = player.data();

    // --- Player identity ---
    if (!d.name.empty()) {
        ImGui::Text("%s the %s %s", d.name.c_str(), d.species.c_str(), d.title.c_str());
        if (!d.god.empty())
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Worshipper of %s", d.god.c_str());
        ImGui::Separator();
    } else {
        ImGui::TextDisabled("Waiting for player data...");
        ImGui::End();
        return;
    }

    // --- Vital stats: HP & MP bars ---
    ImGui::Text("HP: %d/%d", d.hp, d.hp_max);
    ImGui::SameLine();
    {
        float hpFrac = d.hp_max > 0 ? std::clamp(static_cast<float>(d.hp) / d.hp_max, 0.0f, 1.0f) : 0.0f;
        ImVec4 hpColor = hpFrac > 0.5f ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f)
                        : hpFrac > 0.25f ? ImVec4(1.0f, 1.0f, 0.0f, 1.0f)
                        : ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
        ImGui::ProgressBar(hpFrac, ImVec2(-1, 0), "");
        // Color the bar (workaround: draw over it with colored rect)
        ImVec2 barMin = ImGui::GetItemRectMin();
        ImVec2 barMax = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddRectFilled(barMin,
            ImVec2(barMin.x + (barMax.x - barMin.x) * hpFrac, barMax.y),
            ImColor(hpColor.x, hpColor.y, hpColor.z, 0.6f));
    }

    ImGui::Text("MP: %d/%d", d.mp, d.mp_max);
    ImGui::SameLine();
    {
        float mpFrac = d.mp_max > 0 ? std::clamp(static_cast<float>(d.mp) / d.mp_max, 0.0f, 1.0f) : 0.0f;
        ImGui::ProgressBar(mpFrac, ImVec2(-1, 0), "");
        ImVec2 barMin = ImGui::GetItemRectMin();
        ImVec2 barMax = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddRectFilled(barMin,
            ImVec2(barMin.x + (barMax.x - barMin.x) * mpFrac, barMax.y),
            ImColor(0.3f, 0.3f, 1.0f, 0.6f));
    }

    ImGui::Separator();

    // --- Defenses ---
    ImGui::Text("AC: %-4d  EV: %-4d  SH: %-4d", d.ac, d.ev, d.sh);
    ImGui::Text("XL: %-4d  Progress: %d%%", d.xl, d.progress);
    ImGui::Text("Gold: %d", d.gold);

    // --- Equipment ---
    ImGui::Separator();
    ImGui::Text("Equipment:");
    {
        bool anyShown = false;

        // Weapon
        if (d.weapon_index >= 0) {
            auto wit = d.inv.find(d.weapon_index);
            char wletter = static_cast<char>(d.weapon_index < 26 ? 'a' + d.weapon_index : 'A' + d.weapon_index - 26);
            if (wit != d.inv.end() && !wit->second.name.empty())
                ImGui::Text("%c) %s", wletter, wit->second.name.c_str());
            else
                ImGui::Text("%c) (weapon)", wletter);
            anyShown = true;
        } else if (!d.unarmed_attack.empty()) {
            ImGui::TextDisabled("%s", d.unarmed_attack.c_str());
            anyShown = true;
        }

        // Offhand (only shown if dual-wielding)
        if (d.offhand_weapon && d.offhand_index >= 0) {
            auto oit = d.inv.find(d.offhand_index);
            char oletter = static_cast<char>(d.offhand_index < 26 ? 'a' + d.offhand_index : 'A' + d.offhand_index - 26);
            if (oit != d.inv.end() && !oit->second.name.empty())
                ImGui::Text("%c) %s", oletter, oit->second.name.c_str());
            else
                ImGui::Text("%c) (offhand)", oletter);
            anyShown = true;
        }

        // Quiver
        if (!d.quiver_desc.empty()) {
            std::string stripped = stripColorTags(d.quiver_desc);
            ImGui::Text("Q: %s", stripped.c_str());
            anyShown = true;
        } else if (d.quiver_item >= 0) {
            auto qit = d.inv.find(d.quiver_item);
            char qletter = static_cast<char>(d.quiver_item < 26 ? 'a' + d.quiver_item : 'A' + d.quiver_item - 26);
            if (qit != d.inv.end() && !qit->second.name.empty())
                ImGui::Text("%c) %s", qletter, qit->second.name.c_str());
            anyShown = true;
        }

        if (!anyShown)
            ImGui::TextDisabled("(no equipment data yet)");
    }

    ImGui::Separator();

    // --- Location ---
    ImGui::Text("Location: %s:%d", d.place.c_str(), d.depth);
    ImGui::Text("Position: (%d, %d)", d.pos_x, d.pos_y);

    ImGui::Separator();

    // --- Attributes ---
    ImGui::Text("Attributes:");
    ImGui::Text("Str: %d (%d)  Int: %d (%d)  Dex: %d (%d)",
                 d.str, d.str_max, d.intel, d.intel_max, d.dex, d.dex_max);
    if (d.piety_rank > 0)
        ImGui::Text("Piety: %.*s*****", d.piety_rank, "**********");
    if (d.penance)
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "(Penance)");

    // --- Status effects ---
    if (!d.status.empty()) {
        ImGui::Separator();
        ImGui::Text("Status:");
        for (const auto& s : d.status) {
            ImGui::SameLine();
            ImGui::Text("[%s]", s.c_str());
        }
    }

    // --- Inventory grid (52 slots: a-z, A-Z, 4 rows of 13) ---
    ImGui::Separator();
    ImGui::Text("Inventory:");

    // Draw 4 rows x 13 columns grid
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 13; ++col) {
            int slotIdx = row * 13 + col;  // 0..51

            // Map to letter: 0-25 → a-z, 26-51 → A-Z
            char letter;
            if (slotIdx < 26)
                letter = 'a' + static_cast<char>(slotIdx);
            else
                letter = 'A' + static_cast<char>(slotIdx - 26);

            bool hasItem = d.inv.contains(slotIdx);
            const InventoryItem* item = hasItem ? &d.inv.at(slotIdx) : nullptr;
            bool isEmpty = !hasItem || item->name.empty();

            if (isEmpty) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
                ImGui::Text("%c", letter);
                ImGui::PopStyleColor();
            } else {
                // Show occupied slot in gold/bright color
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.3f, 1.0f));
                ImGui::Text("%c", letter);
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                    ImGui::BeginTooltip();
                    ImGui::Text("%c - %s", letter, item->name.c_str());
                    if (item->count > 1)
                        ImGui::Text("  x%d", item->count);
                    ImGui::EndTooltip();
                }
            }

            if (col < 12)
                ImGui::SameLine();
        }
    }

    // --- Camera debug (collapsible) ---
    if (ImGui::CollapsingHeader("Camera (debug)")) {
        ImGui::TextWrapped("phi=%.3f, theta=%.3f", player.camera().phi, player.camera().theta);
        ImGui::TextWrapped("pos: (%.2f, %.2f, %.2f)",
                           player.camera().pos[0], player.camera().pos[1], player.camera().pos[2]);
    }

    ImGui::End();
}

void displayMap(const GameMap& map, const Player& player)
{
    ImGui::Begin("map");
    PinButton("map");
    ImGui::Spacing();

    ImGui::Text("map:");
    ImGui::SameLine();
    if (ImGui::SmallButton("Export")) {
        auto t = std::time(nullptr);
        auto* tm = std::localtime(&t);
        char buf[64];
        std::strftime(buf, sizeof(buf), "map_dump_%Y%m%d_%H%M%S.txt", tm);
        std::ofstream out(buf);
        if (out) {
            // GameMap internal data
            out << "=== GameMap dump ===\n";
            out << map.dumpString();
            out << "\nPlayer at (" << player.data().pos_x << "," << player.data().pos_y << ")\n";
            out << "Look: " << directionToString[player.getFacingDirection()] << "\n";

            // Overlay rendered output (exactly what the player sees)
            out << "\n=== Overlay rendered grid (TileType ints) ===\n";
            auto b = map.getBounds();
            // Column header
            out << "   ";
            for (int x = b.x_min; x <= b.x_max; ++x)
                out << std::format("{:>2}", x % 10);
            out << "\n";
            for (int y = b.y_min; y <= b.y_max; ++y) {
                out << std::format("{:2}: ", y);
                for (int x = b.x_min; x <= b.x_max; ++x) {
                    auto typeOpt = map.getTileAt(x, y);
                    if (!typeOpt) {
                        out << "  ";
                    } else {
                        auto monOpt = map.getMonsterAt(x, y);
                        if (x == player.data().pos_x && y == player.data().pos_y)
                            out << std::format("{:>2}", std::format("{}@", static_cast<int>(*typeOpt)));
                        else if (monOpt)
                            out << std::format("{:>2}", std::format("{}*", static_cast<int>(*typeOpt)));
                        else
                            out << std::format("{:2d}", static_cast<int>(*typeOpt));
                    }
                }
                out << "\n";
            }
            // Monster list as shown in overlay
            out << "\nMonster list from overlay:\n";
            for (const auto& [pos, monId] : map.monsterPositions()) {
                auto it = map.monsterTable().find(monId);
                const char* name = (it != map.monsterTable().end()) ? it->second.name().c_str() : "?";
                out << std::format("  ({},{}) [id={}] '{}'\n", pos.x, pos.y, monId, name);
            }
            spdlog::info("Exported map dump to '{}'", buf);
        }
    }
    ImGui::Text("look: %s", directionToString[player.getFacingDirection()]);
    
    auto bounds = map.getBounds();
    for (int y = bounds.y_min; y <= bounds.y_max; ++y) {
        for (int x = bounds.x_min; x <= bounds.x_max; ++x) {
            auto typeOpt = map.getTileAt(x, y);
            bool isPlayer = (x == player.data().pos_x && y == player.data().pos_y);

            // Single-character glyph per cell (same scheme as GameMap::dumpString).
            char glyph = ' ';
            glm::vec4 color(1, 1, 1, 1);
            if (typeOpt) {
                MapType type = *typeOpt;
                bool vis = map.isVisibleAt(x, y);
                switch (type) {
                case MapType::Wall:        glyph = vis ? '#' : '='; color = vis ? glm::vec4(0.5f,0.5f,0.0f,1) : glm::vec4(0.3f,0.3f,0.0f,1); break;
                case MapType::Floor:       glyph = vis ? '.' : ':'; color = vis ? glm::vec4(0.0f,0.5f,0.0f,1) : glm::vec4(0.0f,0.25f,0.0f,1); break;
                case MapType::Door:        glyph = '+'; color = glm::vec4(0.3f,0.15f,0.0f,1); break;
                case MapType::OpenDoor:     glyph = '\''; color = glm::vec4(0.3f,0.15f,0.0f,1); break;
                case MapType::Item:        glyph = vis ? '!' : 'i'; color = glm::vec4(0.8f,0.8f,0.3f,1); break;
                case MapType::Water:       glyph = vis ? '~' : 'w'; color = glm::vec4(0.0f,0.3f,0.8f,1); break;
                case MapType::Lava:        glyph = vis ? 'L' : 'l'; color = glm::vec4(0.8f,0.3f,0.0f,1); break;
                case MapType::Other:       glyph = vis ? 'O' : 'o'; color = glm::vec4(0.0f,0.5f,0.5f,1); break;
                case MapType::WallMemory:  glyph = '='; color = glm::vec4(0.3f,0.3f,0.0f,1); break;
                case MapType::FloorMemory: glyph = ':'; color = glm::vec4(0.0f,0.25f,0.0f,1); break;
                case MapType::Unexplored:  glyph = 'U'; color = glm::vec4(0.5f,0.5f,0.5f,1); break;
                }
            }

            // Monster glyph overrides tile glyph
            auto monOpt = map.getMonsterAt(x, y);
            if (monOpt) {
                glyph = 'M';
                float monAlpha = (sinf(ImGui::GetTime() * 8.0f) * 0.5f) + 0.5f;
                color = glm::vec4(1.0f, 0.5f, 0.0f, monAlpha);
            }

            // Player glyph overrides everything
            if (isPlayer) {
                glyph = '@';
                float alpha = (sinf(ImGui::GetTime() * 8.0f) * 0.5f) + 0.5f;
                color = glm::vec4(1.0f, 0.0f, 0.0f, alpha);
            }

            ImGui::TextColored(ImVec4(color.r, color.g, color.b, color.a), "%c", glyph);

            if (isPlayer) {
                // Draw small red arrow centered on the player marker
                ImVec2 itemMin = ImGui::GetItemRectMin();
                ImVec2 itemMax = ImGui::GetItemRectMax();
                float cx = (itemMin.x + itemMax.x) * 0.5f;
                float cy = (itemMin.y + itemMax.y) * 0.5f;
                float theta = player.camera().theta;
                float dirX = cosf(theta);
                float dirY = -sinf(theta);
                float arrowLen = 14.0f;
                float tipX = cx + dirX * arrowLen;
                float tipY = cy + dirY * arrowLen;
                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImU32 arrowCol = IM_COL32(255, 0, 0, 255);
                dl->AddLine(ImVec2(cx, cy), ImVec2(tipX, tipY), arrowCol, 1.5f);
                float headSize = 3.5f;
                float perpX = -dirY * headSize;
                float perpY = dirX * headSize;
                dl->AddTriangleFilled(
                    ImVec2(tipX + dirX * headSize * 0.6f, tipY + dirY * headSize * 0.6f),
                    ImVec2(tipX + perpX, tipY + perpY),
                    ImVec2(tipX - perpX, tipY - perpY),
                    arrowCol);
            }

            ImGui::SameLine(0.0f, 0.0f);
        }
        ImGui::NewLine();
    }

    // Monster list below the map grid
    ImGui::Spacing();
    ImGui::SeparatorText("Monsters on map:");
    const auto& monsterTable = map.monsterTable();
    const auto& monsterPositions = map.monsterPositions();
    if (monsterPositions.empty()) {
        ImGui::TextDisabled("(no monsters)");
    } else {
        // Show each monster: position, name, type, attitude, threat
        for (const auto& [pos, monId] : monsterPositions) {
            auto tableIt = monsterTable.find(monId);
            if (tableIt == monsterTable.end())
                continue;
            const Monster& mon = tableIt->second;

            ImGui::Text("(%d,%d) [id=%u]", pos.x, pos.y, monId);
            ImGui::SameLine();
            if (!mon.name().empty())
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), " %s", mon.name().c_str());
            ImGui::SameLine();
            ImGui::Text(" type=%d att=%d threat=%d", mon.type(), mon.att(), mon.threat());
        }
    }

    ImGui::End();
}

void displayEquipment(const Player& player)
{
    ImGui::Begin("equipment");
    PinButton("equipment");
    ImGui::Spacing();

    const auto& d = player.data();

    if (d.inv.empty()) {
        ImGui::TextDisabled("(inventory not yet received)");
        ImGui::End();
        return;
    }

    // --- Equipped summary ---
    ImGui::SeparatorText("Equipped");
    bool hasEquip = false;

    // Wielded weapon
    if (d.weapon_index >= 0) {
        auto wit = d.inv.find(d.weapon_index);
        if (wit != d.inv.end() && !wit->second.name.empty()) {
            char wl = static_cast<char>(d.weapon_index < 26 ? 'a' + d.weapon_index : 'A' + d.weapon_index - 26);
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "%c) %s (weapon)", wl, wit->second.name.c_str());
            hasEquip = true;
        }
    } else if (!d.unarmed_attack.empty()) {
        ImGui::TextDisabled("%s", d.unarmed_attack.c_str());
        hasEquip = true;
    }

    // Offhand (dual-wield)
    if (d.offhand_weapon && d.offhand_index >= 0) {
        auto oit = d.inv.find(d.offhand_index);
        if (oit != d.inv.end() && !oit->second.name.empty()) {
            char ol = static_cast<char>(d.offhand_index < 26 ? 'a' + d.offhand_index : 'A' + d.offhand_index - 26);
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "%c) %s (offhand)", ol, oit->second.name.c_str());
            hasEquip = true;
        }
    }

    // Quivered
    if (!d.quiver_desc.empty()) {
        std::string stripped = stripColorTags(d.quiver_desc);
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Q: %s", stripped.c_str());
        hasEquip = true;
    } else if (d.quiver_item >= 0) {
        auto qit = d.inv.find(d.quiver_item);
        if (qit != d.inv.end() && !qit->second.name.empty()) {
            char ql = static_cast<char>(d.quiver_item < 26 ? 'a' + d.quiver_item : 'A' + d.quiver_item - 26);
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "%c) %s (quivered)", ql, qit->second.name.c_str());
            hasEquip = true;
        }
    }

    // Armour (base_type=2 items not already shown)
    for (const auto& [slot, item] : d.inv) {
        if (item.name.empty()) continue;
        if (item.base_type != 2) continue;
        if (slot == d.weapon_index || slot == d.offhand_index || slot == d.quiver_item) continue;
        char al = static_cast<char>(slot < 26 ? 'a' + slot : 'A' + slot - 26);
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%c) %s (worn)", al, item.name.c_str());
        hasEquip = true;
    }

    if (!hasEquip)
        ImGui::TextDisabled("(no equipment)");

    ImGui::Spacing();
    ImGui::SeparatorText("Full Inventory");

    // Full inventory list
    ImGui::BeginChild("EquipInvList", ImVec2(0, 0), ImGuiChildFlags_Borders);
    for (const auto& [slot, item] : d.inv) {
        if (item.name.empty()) continue;
        char letter = static_cast<char>(slot < 26 ? 'a' + slot : 'A' + slot - 26);
        ImGui::Text("%c - %s", letter, item.name.c_str());
        if (item.count > 1) {
            ImGui::SameLine();
            ImGui::Text("x%d", item.count);
        }
    }
    ImGui::EndChild();

    ImGui::End();
}

void networkMenu(NetworkManager& net)
{
    ImGui::Begin("network");
    PinButton("network");
    ImGui::Spacing();

    WindowManager& wm = WindowManager::instance();

    // Connection status indicator
    if (net.isConnected()) {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Connected");
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Disconnected");
    }

    // Login status
    if (wm.isLoggedIn()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), " | Logged In");
    }

    // Game status
    if (wm.isGameConnected()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), " | Game Running");
    }

    if (ImGui::Button("Reconnect")) {
        net.reconnect();
    }

    ImGui::Separator();

    // Login button — always available (needed first)
    if (ImGui::Button("send network login")) {
        net.sendMessage(loginMessage("asciineuron", "password"));
    }

    // Game start — only enabled after login, before game is running
    const bool canStartGame = wm.isLoggedIn() && !wm.isGameConnected();

    if (!canStartGame)
        ImGui::BeginDisabled();

    // TODO combine listbox with input text to give dropdown presets
    static std::string gameID = "dcss-web-trunk"; // defaultGameID;
    ImGui::InputText("game id", &gameID);

    if (ImGui::Button("Create Character / Resume Game")) {
        net.sendMessage(playMessage(gameID));
    }

    if (!canStartGame)
        ImGui::EndDisabled();

    // log of network messages:
    if (ImGui::CollapsingHeader("Message History")) {
        if (ImGui::TreeNode("Server Received Messages")) {
            const bool serverHistoryVisible = ImGui::BeginChild("Message Log##1");
            if (serverHistoryVisible) {
                {
                    std::scoped_lock lock(net.messageMutex);
                    // iterate in reverse:
                    for (const auto& message : std::views::reverse(net.responseHistory())) {
                        ImGui::TextWrapped("%s", message.c_str());
                    }
                }
            }
            ImGui::EndChild();

            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Client Sent Messages")) {
            // TODO not sure if needs ##2 or can reuse ##1 since separate child:
            const bool clientHistoryVisible = ImGui::BeginChild("Message Log##2");
            if (clientHistoryVisible) {
                for (const auto& message : std::views::reverse((net.sendHistory()))) {
                    ImGui::TextWrapped("%s", message.c_str());
                }
            }
            ImGui::EndChild();

            ImGui::TreePop();
        }
    }

    ImGui::End();
}

// TODO 2-25: add remaining network functionality, i.e. sending messages
// for connect, ping, choose character etc., rather than handling at the python script level,
// just have be essentially an echo server

void renderMenu(Renderer& renderer)
{
    ImGui::Begin("renderer");
    PinButton("renderer");
    ImGui::Spacing();

    ImGui::Text("%s", std::format("skip collision: {}", skipCollisionCheck).c_str());

    if (ImGui::Button("toggle skip collision"))
        skipCollisionCheck = !skipCollisionCheck;

    ImGui::Text("render count: %llu", renderer.renderCount());

    ImGui::End();
}

// --- Character selection window ---

void characterSelectWindow(const NewgameChoice& choice, NetworkManager& net)
{
    const char* phaseLabel = "Species";
    switch (choice.phase) {
    case CharSelectPhase::Species:    phaseLabel = "Species";    break;
    case CharSelectPhase::Background: phaseLabel = "Background"; break;
    case CharSelectPhase::Weapon:     phaseLabel = "Weapon";     break;
    default: break;
    }

    ImGui::SetNextWindowSize(ImVec2(500, 450), ImGuiCond_FirstUseEver);
    ImGui::Begin(std::format("Choose {}", phaseLabel).c_str());

    // Title (strip DCSS color tags like <brown>, <lightgreen>)
    if (!choice.title.empty()) {
        ImGui::TextWrapped("%s", stripColorTags(choice.title).c_str());
        ImGui::Separator();
    }

    // Main buttons — rendered in row groups by y-coordinate
    if (!choice.mainButtons.empty()) {
        // Group buttons by row (y coordinate), preserve x-order within each row
        int maxY = 0;
        for (const auto& btn : choice.mainButtons)
            maxY = std::max(maxY, btn.y);

        for (int row = 0; row <= maxY; ++row) {
            // Collect buttons in this row, sorted by x
            std::vector<const ChoiceButton*> rowButtons;
            for (const auto& btn : choice.mainButtons)
                if (btn.y == row)
                    rowButtons.push_back(&btn);

            if (rowButtons.empty())
                continue;

            std::sort(rowButtons.begin(), rowButtons.end(),
                      [](const ChoiceButton* a, const ChoiceButton* b) {
                          return a->x < b->x;
                      });

            for (size_t i = 0; i < rowButtons.size(); ++i) {
                const auto& btn = *rowButtons[i];

                // Skip non-interactive grid labels (buttons with empty hotkey)
                if (btn.hotkey == '\0') {
                    ImGui::TextDisabled("%s", stripColorTags(btn.label).c_str());
                    if (i + 1 < rowButtons.size())
                        ImGui::SameLine();
                    continue;
                }

                // Colour by highlight: 0=bad(red), 1=normal(white), 2=good(green)
                if (btn.highlightColour == 2)
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 1.0f, 0.2f, 1.0f));
                else if (btn.highlightColour == 0)
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.3f, 0.3f, 1.0f));
                else
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

                // Button label: strip tags
                std::string btnLabel = stripColorTags(btn.label);
                if (btnLabel.empty())
                    btnLabel = std::format("[{}]", btn.hotkey);

                if (ImGui::Button(btnLabel.c_str())) {
                    spdlog::info("Character select: sending input '{}' for '{}'",
                                 btn.hotkey, btn.label);
                    json inputMsg;
                    inputMsg["msg"] = "input";
                    inputMsg["text"] = std::string(1, btn.hotkey);
                    net.sendMessage(inputMsg);
                }

                ImGui::PopStyleColor();

                // Description tooltip (strip tags)
                if (!btn.description.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                    ImGui::BeginTooltip();
                    ImGui::PushTextWrapPos(400.0f);
                    ImGui::TextWrapped("%s", stripColorTags(btn.description).c_str());
                    ImGui::PopTextWrapPos();
                    ImGui::EndTooltip();
                }

                if (i + 1 < rowButtons.size())
                    ImGui::SameLine();
            }
        }
    }

    // Sub buttons — Recommended, Random, etc.
    if (!choice.subButtons.empty()) {
        ImGui::Spacing();
        ImGui::Separator();
        for (const auto& btn : choice.subButtons) {
            if (btn.hotkey == '\0') continue;

            if (btn.highlightColour == 2)
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.5f, 0.1f, 1.0f));

            std::string label = stripColorTags(btn.label);
            if (ImGui::Button(label.c_str())) {
                spdlog::info("Character select: sending input '{}' for '{}'",
                             btn.hotkey, btn.label);
                json inputMsg;
                inputMsg["msg"] = "input";
                inputMsg["text"] = std::string(1, btn.hotkey);
                net.sendMessage(inputMsg);
            }

            if (btn.highlightColour == 2)
                ImGui::PopStyleColor();

            if (!btn.description.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(400.0f);
                ImGui::TextWrapped("%s", stripColorTags(btn.description).c_str());
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }

            ImGui::SameLine();
        }
        ImGui::NewLine();
    }

    ImGui::End();
}
