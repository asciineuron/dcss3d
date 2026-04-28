#include "imguilayouts.hpp"
#include "MessageQueue.hpp"
#include "Turn.hpp"
#include "debug.hpp"
#include "imgui.h"
#include "imgui_stdlib.h"
#include <spdlog/spdlog.h>
#include <format>
#include <fstream>
#include <mutex>
#include <ranges>
#include <sstream>

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
} // namespace

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
    // Window names are defined in main.cpp
    const char* windowNames[] = {
        "Demo", "player", "map", "network", "renderer", "settings"
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

    ImGui::Separator();

    // --- Location ---
    ImGui::Text("Location: %s:%d", d.place.c_str(), d.depth);
    ImGui::Text("Position: (%d, %d)", d.pos_x, d.pos_y);

    ImGui::Separator();

    // --- Attributes ---
    if (ImGui::CollapsingHeader("Attributes")) {
        ImGui::Text("Str: %d (%d)  Int: %d (%d)  Dex: %d (%d)",
                     d.str, d.str_max, d.intel, d.intel_max, d.dex, d.dex_max);
        if (d.piety_rank > 0)
            ImGui::Text("Piety: %.*s*****", d.piety_rank, "**********");
        if (d.penance)
            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "(Penance)");
    }

    // --- Status effects ---
    if (!d.status.empty()) {
        ImGui::Separator();
        ImGui::Text("Status:");
        for (const auto& s : d.status) {
            ImGui::SameLine();
            ImGui::Text("[%s]", s.c_str());
        }
    }

    // --- Inventory ---
    if (!d.inv.empty() && ImGui::CollapsingHeader("Inventory")) {
        for (const auto& [slot, item] : d.inv) {
            char letter = 'a' + static_cast<char>(slot);
            ImGui::Text("%c - %s", letter, item.name.c_str());
            if (item.count > 1)
                ImGui::SameLine(); ImGui::Text(" x%d", item.count);
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

void displayMap(const GameMap& map)
{
    ImGui::Begin("map");

    ImGui::Text("map:");
    
    auto bounds = map.getBounds();
    for (int y = bounds.y_min; y <= bounds.y_max; ++y) {
        for (int x = bounds.x_min; x <= bounds.x_max; ++x) {
            auto typeOpt = map.getTileAt(x, y);
            if (!typeOpt) {
                ImGui::Text(" ");
                ImGui::SameLine();
                continue;
            }

            MapType type = *typeOpt;
            if (x == 0 && y == 0) {
                // Blinking red color for player
                float alpha = (sinf(ImGui::GetTime() * 8.0f) * 0.5f) + 0.5f;
                ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, alpha), "%d", static_cast<int>(type));
            } else {
                glm::vec4 color = mapTypeToColor(type);
                ImGui::TextColored(ImVec4(color.r, color.g, color.b, color.a), "%d", static_cast<int>(type));
            }

            // Monster marker: flashing '*' if a monster is on this cell
            auto monOpt = map.getMonsterAt(x, y);
            if (monOpt) {
                ImGui::SameLine();
                float monAlpha = (sinf(ImGui::GetTime() * 8.0f) * 0.5f) + 0.5f;
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, monAlpha), "*");
            }

            ImGui::SameLine();
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

void networkMenu(NetworkManager& net)
{
    ImGui::Begin("network");

    // Connection status indicator
    if (net.isConnected()) {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Connected");
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Disconnected");
    }

    if (ImGui::Button("Reconnect")) {
        net.reconnect();
    }

    ImGui::Separator();

    if (ImGui::Button("send network login")) {
        net.sendMessage(loginMessage("asciineuron", "password"));
    }

    // TODO combine listbox with input text to give dropdown presets
    static std::string gameID = "dcss-web-trunk"; // defaultGameID;
    ImGui::InputText("game id", &gameID);

    if (ImGui::Button("send network play")) {
        net.sendMessage(playMessage(gameID));
    }

    if (ImGui::Button("send character select")) {
        net.chooseCharacter();
    }

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

    ImGui::Text("%s", std::format("skip collision: {}", skipCollisionCheck).c_str());

    if (ImGui::Button("toggle skip collision"))
        skipCollisionCheck = !skipCollisionCheck;

    ImGui::Text("render count: %llu", renderer.renderCount());

    ImGui::End();
}