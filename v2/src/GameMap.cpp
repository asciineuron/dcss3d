#include "GameMap.hpp"
#include <iostream>
#include <spdlog/spdlog.h>

// use for future trial Pos2, don't need to do direction checking, just proximity
bool GameMap::wouldCollide(const glm::vec2& testLoc) const
{
    // constexpr float collisionTolerance = 0.1f;
    for (const auto& [mapCoord, tile] : m_map) {
        if (tile.type() != MapType::Wall)
            continue;
        glm::vec2 renderCoords = mapCoordToRender(mapCoord);
        if (std::abs(renderCoords.x - testLoc.x) < 0.5f) {
            spdlog::debug("collision x!");
            return true;
        }
        if (std::abs(renderCoords.y - testLoc.y) < 0.5f) {
            spdlog::debug("collision y!");
            return true;
        }
    }
    return false;
}

void GameMap::handleMessage(const json& message)
{
    if (message["msg"] != "map")
        return;
    updateMap(message);
}

MapType mapTypeFromMF(int mf)
{
    using enum MapType;
    switch (mf) {
    case 1:
        return Floor;
        break;
    case 2:
        return Wall;
        break;
    case 26:
        return Unexplored;
        break;
    default:
        std::cerr << "Warning, received unknown map type: " << mf << std::endl;
        return Other;
    }
}

std::ostream&
operator<<(std::ostream& os, const GameMap& gameMap)
{
    for (const auto& loc : gameMap.map()) {
        os << "(" << loc.first.x << "," << loc.first.y << "): " << static_cast<int>(loc.second.type()) << "\n";
    }
    return os;
}

void GameMap::updateMap(const json& message)
{
    if (message.contains("clear") && message["clear"])
        m_map.clear();
    int curX;
    Pos2<int> pos;
    MapType type;
    for (auto& cell : message["cells"]) {
        if (auto x = cell.find("x"); x != cell.end()) {
            curX = x.value();
        } else {
            ++curX;
        }
        pos.x = curX;
        if (auto y = cell.find("y"); y != cell.end())
            pos.y = y.value();
        if (auto mf = cell.find("mf"); mf != cell.end())
            type = mapTypeFromMF(mf.value());

        m_map[pos] = Tile(type);
    }

    m_didRender = false;

    // std::cerr << "new map data: \n"
    //           << (*this) << std::endl;
}
