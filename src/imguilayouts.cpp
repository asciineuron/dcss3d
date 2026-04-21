#include "imguilayouts.hpp"
#include "MessageQueue.hpp"
#include "Turn.hpp"
#include "debug.hpp"
#include "imgui.h"
#include "imgui_stdlib.h"
#define GLM_ENABLE_EXPERIMENTAL // for glm::to_string()
#include "glm/gtx/string_cast.hpp"
#include <format>
#include <mutex>
#include <ranges>
#include <sstream>

void displayPlayer(const Player& player)
{
    ImGui::Begin("player");

    // TODO 2-18 found camera bug! phi circles 2pi very fast/small mouse movement, meaning the sensitivity is just too high
    ImGui::TextWrapped("%s", std::format("Camera: phi={}, theta={}", player.camera().phi, player.camera().theta).c_str());
    ImGui::TextWrapped("%s", std::format("Camera position: {}, {}, {}", player.camera().pos[0], player.camera().pos[1], player.camera().pos[2]).c_str());

    ImGui::TextWrapped("Camera view matrix %s", glm::to_string(player.camera().toViewProjection()).c_str());

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
            ImGui::SameLine();
        }
        ImGui::NewLine();
    }

    ImGui::End();
}

void networkMenu(NetworkManager& net)
{
    ImGui::Begin("network");

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
