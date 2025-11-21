#include "GameMap.hpp"

// use for future trial Pos2, don't need to do direction checking, just proximity
bool GameMap::wouldCollide(const glm::vec2& testLoc) const
{
    // constexpr float collisionTolerance = 0.1f;
    for (const auto& [mapCoord, tile] : m_map) {
        if (tile.type() != MapType::Wall)
            continue;
        glm::vec2 renderCoords = mapCoordToRender(mapCoord);
        if (std::abs(renderCoords.x - testLoc.x) < 0.5f)
            return true;
        if (std::abs(renderCoords.y - testLoc.y) < 0.5f)
            return true;
    }
    return false;
}

void GameMap::handleMessage(const json& message)
{
    if (message["msg"] != "map")
        return;
    updateMap(message);
}

void GameMap::updateMap(const json& message)
{
    if (message.contains("clear") && message["clear"])
        m_map.clear();
    for (auto& cell : message["cells"]) {
        if (auto x = cell.find("x"); x != cell.end()) {

        }
        if (auto y = cell.find("y"); y != cell.end()) {
            
        }
    }
}
