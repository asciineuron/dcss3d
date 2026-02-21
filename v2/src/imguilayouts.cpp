#include "imguilayouts.hpp"
#include "imgui.h"
#include "debug.hpp"
#include <format>
#include <sstream>

void displayPlayer(const Player& player)
{
    ImGui::Begin("player");

    // TODO 2-18 found camera bug! phi circles 2pi very fast/small mouse movement, meaning the sensitivity is just too high
    ImGui::Text("%s", std::format("Camera: phi={}, theta={}", player.camera().phi, player.camera().theta).c_str());
    ImGui::Text("%s", std::format("Camera position: {}, {}, {}", player.camera().pos[0], player.camera().pos[1], player.camera().pos[2]).c_str());
    ImGui::End();
}

void displayMap(const GameMap& map)
{
    ImGui::Begin("map");

    std::stringstream mapstr;
    mapstr << map;
    ImGui::Text("map:\n%s", mapstr.str().c_str());

    ImGui::End();
}

void requestNetworkMapUpdate(GameMap& map)
{
    // get request and convert to json get and toss until first map packet
    json response;
    map.handleMessage(response);
}

void toggleCollision()
{
    ImGui::Begin("collision");

    ImGui::Text("%s", std::format("skip collision: {}", skipCollisionCheck).c_str());

    if (ImGui::Button("toggle skip collision"))
        skipCollisionCheck = !skipCollisionCheck;

    ImGui::End();
}
