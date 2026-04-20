#include "GameMap.hpp"
#include "Turn.hpp"
#include "mdspan/mdspan.hpp"
#include <iostream>
#include <spdlog/spdlog.h>
#include <string>

// use for future trial Pos2, don't need to do direction checking, just proximity
bool GameMap::wouldCollide(const glm::vec2& testLoc) const
{
    for (const auto& [mapCoord, tile] : m_map) {
        if (tile.type() != MapType::Wall)
            continue;
        glm::vec2 renderCoords = mapCoordToRender(mapCoord);
        if (std::abs(renderCoords.x - testLoc.x) < 0.5f &&
            std::abs(renderCoords.y - testLoc.y) < 0.5f) {
            spdlog::debug("collision at ({:.3f}, {:.3f})", renderCoords.x, renderCoords.y);
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

void GameMap::shift(Direction moveDir)
{
    if (moveDir == None || moveDir == Here)
        return;

    int dx = 0;
    int dy = 0;

    if (moveDir & North) dy += 1;
    if (moveDir & South) dy -= 1;
    if (moveDir & East)  dx -= 1;
    if (moveDir & West)  dx += 1;

    if (dx == 0 && dy == 0)
        return;

    MapData nextMap;
    for (const auto& [pos, tile] : m_map) {
        nextMap[{pos.x + dx, pos.y + dy}] = tile;
    }
    m_map = std::move(nextMap);
    m_didRender = false;
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

std::string GameMap::asciiView() const
{
    // TODO: natively store the map this way instead? or provide a view, no need to loop over so many times each frame...

    // algorithm to loop over, find min,max x,y => construct rectangle, fill relevant entry with number, appears more graphical
    int x_min, x_max, y_min, y_max;
    x_min = x_max = y_min = y_max = 0;

    for (const auto& loc : m_map) {
        if (loc.first.x < x_min)
            x_min = loc.first.x;
        if (loc.first.x > x_max)
            x_max = loc.first.x;
        if (loc.first.y < y_min)
            y_min = loc.first.y;
        if (loc.first.y > y_max)
            y_max = loc.first.y;
    }

    int x_size = x_max - x_min;
    int y_size = y_max - y_min;

    std::vector<uint32_t> mapAsMatrix;
    mapAsMatrix.resize((x_size + 1) * (y_size + 1), static_cast<uint32_t>(MapType::Other));
    auto matrixmdspan = Kokkos::mdspan(mapAsMatrix.data(), y_size + 1, x_size + 1);

    for (const auto& loc : m_map) {
        matrixmdspan[loc.first.y - y_min, loc.first.x - x_min] = static_cast<uint32_t>(loc.second.type());
    }

    std::string asciified;
    for (int i = 0; i < matrixmdspan.extent(0); i++) {
        for (int j = 0; j < matrixmdspan.extent(1); j++)
            asciified += std::to_string(matrixmdspan[i, j]);
        asciified += "\n";
    }

    return asciified;
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
