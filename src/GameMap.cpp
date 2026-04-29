#include "GameMap.hpp"
#include "Turn.hpp"
#include "mdspan/mdspan.hpp"
#include <iostream>
#include <spdlog/spdlog.h>
#include <string>
#include <unordered_set>

// --- Monster ----------------------------------------------------------------

void Monster::merge(const json& monJson)
{
    // Always extract id if present (required for identity)
    if (auto it = monJson.find("id"); it != monJson.end())
        m_id = it->get<uint32_t>();

    if (auto it = monJson.find("type"); it != monJson.end())
        m_type = it->get<int>();

    if (auto it = monJson.find("att"); it != monJson.end())
        m_att = it->get<int>();

    if (auto it = monJson.find("threat"); it != monJson.end())
        m_threat = it->get<int>();

    if (auto it = monJson.find("name"); it != monJson.end())
        m_name = it->get<std::string>();

    if (auto it = monJson.find("plural"); it != monJson.end())
        m_plural = it->get<std::string>();

    if (auto it = monJson.find("btype"); it != monJson.end()) {
        m_btype = it->get<int>();
        m_hasBtype = true;
    }

    if (auto it = monJson.find("clientid"); it != monJson.end()) {
        m_clientid = it->get<uint32_t>();
        m_hasClientid = true;
    }

    // typedata sub-object
    if (auto td = monJson.find("typedata"); td != monJson.end() && td->is_object()) {
        if (auto avghp = td->find("avghp"); avghp != td->end())
            m_typedataAvghp = avghp->get<int>();
        if (auto noExp = td->find("no_exp"); noExp != td->end())
            m_typedataNoExp = noExp->get<bool>();
        m_hasTypedata = true;
    }
}

// --- GameMap ----------------------------------------------------------------

glm::vec4 mapTypeToColor(MapType type)
{
    using enum MapType;
    switch (type) {
    case Wall:
        return { 0.5f, 0.5f, 0.0f, 1.0f };
        break;
    case Floor:
        return { 0.0f, 0.5f, 0.0f, 1.0f };
        break;
    case Unexplored:
        return { 0.5f, 0.5f, 0.5f, 1.0f };
        break;
    case Other:
        return { 0.0f, 0.5f, 0.5f, 1.0f };
        break;
    default:
        throw std::logic_error("invalid MapType specified");
        break;
    }
}

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

    // Shift tile map
    MapData nextMap;
    for (const auto& [pos, tile] : m_map) {
        nextMap[{pos.x + dx, pos.y + dy}] = tile;
    }
    m_map = std::move(nextMap);

    // Shift monster positions (monster table is position-independent, no change needed)
    MonsterPosMap nextMonsters;
    for (const auto& [pos, id] : m_monsters) {
        nextMonsters[{pos.x + dx, pos.y + dy}] = id;
    }
    m_monsters = std::move(nextMonsters);

    updateBounds();
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

GameMap::Bounds GameMap::getBounds() const
{
    return m_bounds;
}

void GameMap::updateBounds()
{
    int x_min = 0, x_max = 0, y_min = 0, y_max = 0;

    for (const auto& loc : m_map) {
        if (loc.first.x < x_min) x_min = loc.first.x;
        if (loc.first.x > x_max) x_max = loc.first.x;
        if (loc.first.y < y_min) y_min = loc.first.y;
        if (loc.first.y > y_max) y_max = loc.first.y;
    }
    m_bounds = { x_min, x_max, y_min, y_max };
}

std::optional<MapType> GameMap::getTileAt(int x, int y) const
{
    auto it = m_map.find({ x, y });
    if (it != m_map.end()) {
        return it->second.type();
    }
    return std::nullopt;
}

std::optional<std::reference_wrapper<const Monster>> GameMap::getMonsterAt(int x, int y) const
{
    auto posIt = m_monsters.find({ x, y });
    if (posIt == m_monsters.end())
        return std::nullopt;

    auto tableIt = m_monsterTable.find(posIt->second);
    if (tableIt == m_monsterTable.end())
        return std::nullopt;

    return std::cref(tableIt->second);
}

void GameMap::cleanMonsterTable()
{
    // Build a set of all referenced monster IDs from m_monsters
    std::unordered_set<uint32_t> referencedIds;
    for (const auto& [pos, id] : m_monsters) {
        referencedIds.insert(id);
    }

    // Remove any table entry not present in the set
    for (auto it = m_monsterTable.begin(); it != m_monsterTable.end(); ) {
        if (!referencedIds.contains(it->first)) {
            spdlog::debug("Cleaning unreferenced monster id={}", it->first);
            it = m_monsterTable.erase(it);
        } else {
            ++it;
        }
    }
}

void GameMap::updateMap(const json& message)
{
    // Prevent a second "clear" map (e.g. from spectator_joined's
    // _send_everything() after we've already received the real map)
    // from wiping out existing map data.
    if (message.contains("clear") && message["clear"]) {
        if (!m_map.empty()) {
            spdlog::debug("Skipping clear map — already have {} tiles", m_map.size());
            return;
        }
        m_map.clear();
        m_monsters.clear();
        m_monsterTable.clear();
    }

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

        // --- Monster handling ---
        auto monIt = cell.find("mon");
        if (monIt == cell.end()) {
            // No "mon" field at all — monster state unchanged, leave as-is
            continue;
        }

        if (monIt->is_null()) {
            // "mon": null — monster removed from this cell
            m_monsters.erase(pos);
            continue;
        }

        // "mon" is an object — partial or full update
        const json& monJson = *monIt;
        uint32_t monId = 0;
        if (auto idIt = monJson.find("id"); idIt != monJson.end()) {
            monId = idIt->get<uint32_t>();
        }

        if (monId == 0) {
            // Monster without an ID — nothing to track
            spdlog::debug("Monster at ({},{}) has no id, skipping", pos.x, pos.y);
            continue;
        }

        // Check if we already have a monster at this position
        // (the JS client uses old_mon to seed new entries; we do the lookup from m_monsters)
        uint32_t oldMonId = 0;
        if (auto oldPosIt = m_monsters.find(pos); oldPosIt != m_monsters.end()) {
            oldMonId = oldPosIt->second;
        }

        // Look up existing monster in global table by the incoming ID
        auto tableIt = m_monsterTable.find(monId);
        if (tableIt == m_monsterTable.end()) {
            // New monster ID — create entry
            // If there was a previous monster at this cell with a different ID,
            // use its data as fallback for any missing fields (mirrors JS merge_objects(old_mon, mon))
            Monster newMon;
            if (oldMonId != 0 && oldMonId != monId) {
                auto oldTableIt = m_monsterTable.find(oldMonId);
                if (oldTableIt != m_monsterTable.end()) {
                    newMon = oldTableIt->second; // copy old data as fallback
                }
            }
            newMon.merge(monJson); // overwrite with incoming fields
            m_monsterTable[monId] = newMon;
        } else {
            // Existing monster — merge partial update
            tableIt->second.merge(monJson);
        }

        // Remove any old position that references the same monster ID
        // (a monster can only be at one position at a time)
        for (auto it = m_monsters.begin(); it != m_monsters.end(); ) {
            if (it->second == monId && !(it->first == pos)) {
                it = m_monsters.erase(it);
            } else {
                ++it;
            }
        }

        // Update position → monster ID mapping
        m_monsters[pos] = monId;
    }

    // Clean up IDs no longer referenced by any cell
    cleanMonsterTable();

    updateBounds();
    m_didRender = false;
}
