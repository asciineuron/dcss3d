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
    case Wall:        return { 0.5f, 0.5f, 0.0f, 1.0f };
    case Floor:       return { 0.0f, 0.5f, 0.0f, 1.0f };
    case Door:        return { 0.3f, 0.15f, 0.0f, 1.0f };  // dark brown
    case OpenDoor:     return { 0.3f, 0.15f, 0.0f, 1.0f };
    case Item:        return { 0.8f, 0.8f, 0.3f, 1.0f };  // bright yellow
    case WallMemory:  return { 0.3f, 0.3f, 0.0f, 1.0f };
    case FloorMemory: return { 0.0f, 0.25f, 0.0f, 1.0f };
    case Unexplored:  return { 0.5f, 0.5f, 0.5f, 1.0f };
    case Water:       return { 0.0f, 0.3f, 0.8f, 1.0f };
    case Lava:        return { 0.8f, 0.3f, 0.0f, 1.0f };
    case StairUp:     return { 0.9f, 0.9f, 0.95f, 1.0f }; // bright white with blue tint
    case StairDown:   return { 1.0f, 0.4f, 0.6f, 1.0f };  // pink
    case StairBranch: return { 0.8f, 0.2f, 0.9f, 1.0f };  // bright magenta
    case Other:       return { 0.0f, 0.5f, 0.5f, 1.0f };
    }
    return { 0.4f, 0.1f, 0.6f, 0.7f }; // fallback
}

// Check if the test location would collide with a blocking map element.
// Blocks on: Wall tiles, monsters, and feature tiles (plants, etc.).
bool GameMap::wouldCollide(const glm::vec2& testLoc) const
{
    // Convert render-space test location to game coordinates.
    // Inverse of mapCoordToRender: gameX = floor(renderX), gameY = floor(renderZ + 1).
    int cellX = static_cast<int>(std::floor(testLoc.x));
    int cellY = static_cast<int>(std::floor(testLoc.y + 1.0f));

    // Check tile at that cell
    auto tileIt = m_map.find({cellX, cellY});
    if (tileIt != m_map.end()) {
        MapType type = tileIt->second.type();
        bool isBlocked =
            type == MapType::Wall || type == MapType::WallMemory ||
            type == MapType::Door || type == MapType::Other;
        if (isBlocked) {
            spdlog::debug("collision at ({}, {}): type={}", cellX, cellY, static_cast<int>(type));
            return true;
        }
    }

    // Check for monster at that cell
    if (m_monsters.contains({cellX, cellY})) {
        spdlog::debug("collision at ({}, {}): monster present", cellX, cellY);
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

    // Shift object positions
    ObjectMap nextObjects;
    for (const auto& [pos, info] : m_objects) {
        nextObjects[{pos.x + dx, pos.y + dy}] = info;
    }
    m_objects = std::move(nextObjects);

    updateBounds();
    m_didRender = false;
}

// Map crawl's map_feature enum (map-feature.h) to our MapType.
// Reference: MF_UNSEEN=0, MF_FLOOR=1, MF_WALL=2, MF_MAP_FLOOR=3,
// MF_MAP_WALL=4, MF_DOOR=5, MF_ITEM=6, MF_MONS_*=7-11,
// MF_STAIR_*=12-14, MF_FEATURE=15, MF_WATER=16, MF_LAVA=17,
// MF_TRAP=18, MF_EXCL_*=19-20, MF_PLAYER=21, MF_DEEP_WATER=22,
// MF_PORTAL=23, MF_TRANSPORTER*=24-25, MF_EXPLORE_HORIZON=26.
MapType mapTypeFromMF(int mf)
{
    // TODO: fix case  6: return Item; case 15: return Other; to handle correctly
    using enum MapType;
    switch (mf) {
    case  0: return Unexplored;   // MF_UNSEEN
    case  1: return Floor;         // MF_FLOOR
    case  2: return Wall;          // MF_WALL
    case  3: return FloorMemory;   // MF_MAP_FLOOR
    case  4: return WallMemory;    // MF_MAP_WALL
    case  5: return Door;          // MF_DOOR
    case  6: return Floor;         // MF_ITEM — 3D model on top
    case  7: return Floor;         // MF_MONS_FRIENDLY
    case  8: return Floor;         // MF_MONS_PEACEFUL
    case  9: return Floor;         // MF_MONS_NEUTRAL
    case 10: return Floor;         // MF_MONS_HOSTILE
    case 11: return Floor;         // MF_MONS_NO_EXP
    case 12: return StairUp;       // MF_STAIR_UP
    case 13: return StairDown;     // MF_STAIR_DOWN
    case 14: return StairBranch;   // MF_STAIR_BRANCH
    case 15: return Floor;         // MF_FEATURE — 3D model on top
    case 16: return Water;         // MF_WATER
    case 17: return Lava;          // MF_LAVA
    case 18: return Other;         // MF_TRAP
    case 19: return Other;         // MF_EXCL_ROOT
    case 20: return Other;         // MF_EXCL
    case 21: return Floor;         // MF_PLAYER
    case 22: return Water;         // MF_DEEP_WATER
    case 23: return Other;         // MF_PORTAL
    case 24: return Other;         // MF_TRANSPORTER
    case 25: return Other;         // MF_TRANSPORTER_LANDING
    case 26: return Unexplored;    // MF_EXPLORE_HORIZON
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

std::string GameMap::dumpString() const
{
    if (m_map.empty()) return "(empty)\n";

    Bounds b = m_bounds;
    int w = b.x_max - b.x_min + 1;
    int h = b.y_max - b.y_min + 1;

    std::vector<char> grid(w * h, ' ');
    for (const auto& [pos, tile] : m_map) {
        int gx = pos.x - b.x_min;
        int gy = pos.y - b.y_min;
        bool vis = tile.isVisible();
        char c = '?';
        switch (tile.type()) {
        case MapType::Wall:        c = vis ? '#' : '='; break;
        case MapType::Floor:       c = vis ? '.' : ':'; break;
        case MapType::Door:        c = '+'; break;
        case MapType::OpenDoor:     c = '\''; break;
        case MapType::Item:        c = vis ? '!' : 'i'; break;
        case MapType::WallMemory:  c = '='; break;
        case MapType::FloorMemory: c = ':'; break;
        case MapType::Unexplored:  c = 'U'; break;
        case MapType::Water:       c = vis ? '~' : 'w'; break;
        case MapType::Lava:        c = vis ? 'L' : 'l'; break;
        case MapType::StairUp:     c = '<'; break;
        case MapType::StairDown:   c = '>'; break;
        case MapType::StairBranch: c = 'B'; break;
        case MapType::Other:       c = vis ? 'O' : 'o'; break;
        }
        if (m_monsters.contains(pos)) c = 'M';
        else if (m_objects.contains(pos)) c = 'O';
        grid[gy * w + gx] = c;
    }

    std::string monSummary;
    for (const auto& [pos, monId] : m_monsters) {
        auto it = m_monsterTable.find(monId);
        const char* name = (it != m_monsterTable.end()) ? it->second.name().c_str() : "?";
        monSummary += std::format("  id={} '{}' at ({},{})\n", monId, name, pos.x, pos.y);
    }

    std::string header;
    for (int x = b.x_min; x <= b.x_max; ++x) {
        if (x % 5 == 0 || x == b.x_min)
            header += std::format("{:>3}", x);
        else
            header += "   ";
    }

    std::string out;
    out += std::format("Map {}x{} [{}..{}, {}..{}]:\n", w, h, b.x_min, b.x_max, b.y_min, b.y_max);
    out += std::format("   {}\n", header);
    for (int y = 0; y < h; ++y) {
        std::string row;
        for (int x = 0; x < w; ++x)
            row += std::format("  {}", grid[y * w + x]);
        out += std::format("{:2}: {}\n", b.y_min + y, row);
    }
    if (!monSummary.empty())
        out += "Monsters:\n" + monSummary;
    return out;
}

void GameMap::dump() const
{
    spdlog::info("{}", dumpString());
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

bool GameMap::isOpenDoorAt(int x, int y) const
{
    auto it = m_map.find({ x, y });
    return it != m_map.end() && it->second.type() == MapType::Door && it->second.tileData().isOpenDoor();
}

void GameMap::setTileType(int x, int y, MapType type)
{
    auto it = m_map.find({ x, y });
    if (it != m_map.end()) {
        TileData saved = it->second.tileData();
        it->second = Tile(type);
        it->second.tileData() = saved;
    }
}

bool GameMap::isVisibleAt(int x, int y) const
{
    auto it = m_map.find({ x, y });
    if (it != m_map.end()) {
        return it->second.isVisible();
    }
    return false;
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
    // Always honour clear: true — matches JS client's handle_map_message()
    // which calls clear_map() unconditionally.  This handles both the initial
    // game load and level changes (descend/ascend stairs).
    if (message.contains("clear") && message["clear"]) {
        m_map.clear();
        m_monsters.clear();
        m_monsterTable.clear();
        m_objects.clear();
    }

    int curX;
    Pos2<int> pos;
    for (auto& cell : message["cells"]) {
        if (auto x = cell.find("x"); x != cell.end()) {
            curX = x.value();
        } else {
            ++curX;
        }
        pos.x = curX;
        if (auto y = cell.find("y"); y != cell.end())
            pos.y = y.value();

        // Mirror JavaScript merge(): for each property in the cell, update the
        // stored entry.  mf determines tile type; t.bg determines visibility.
        if (auto mf = cell.find("mf"); mf != cell.end()) {
            int mfVal = mf.value();
            MapType newType = mapTypeFromMF(mfVal);
            auto it = m_map.find(pos);
            if (it == m_map.end()) {
                m_map[pos] = Tile(newType);
            } else {
                // Preserve existing tile data (visibility), update type only
                TileData saved = it->second.tileData();
                bool wasSeen = it->second.seen();
                it->second = Tile(newType);
                it->second.tileData() = saved;
                it->second.setSeen(wasSeen || newType != MapType::Unexplored);
            }
            // Mark as seen if the cell is known (not UNSEEN, not EXPLORE_HORIZON)
            if (newType != MapType::Unexplored) {
                m_map[pos].setSeen(true);
            }

            // Track inanimate objects: MF_ITEM (6) and MF_FEATURE (15)
            // are rendered as 3D models on top of the floor tile.
            if (mfVal == 6 || mfVal == 15) {
                m_objects[pos] = { mfVal };
                spdlog::debug("object added at ({},{}) mf={}", pos.x, pos.y, mfVal);
            } else {
                if (m_objects.erase(pos))
                    spdlog::debug("object removed at ({},{}) mf={}", pos.x, pos.y, mfVal);
            }
        } else if (!m_map.contains(pos)) {
            // Cell is new to us but has no mf — create with Unexplored so the
            // cell exists, but it won't be visible (no tile data yet).
            m_map[pos] = Tile(MapType::Unexplored);
        }

        // Parse t.bg for visibility (mirrors JS merge of t property)
        if (auto t = cell.find("t"); t != cell.end() && t->is_object()) {
            auto& td = m_map[pos].tileData();
            if (auto bg = t->find("bg"); bg != t->end() && !bg->is_null()) {
                td.hasBg = true;
                if (bg->is_number()) {
                    td.bg = bg->get<uint64_t>();
                } else if (bg->is_array() && bg->size() >= 1) {
                    td.bg = (*bg)[0].get<uint64_t>();
                    if (bg->size() >= 2)
                        td.bg |= static_cast<uint64_t>((*bg)[1].get<uint32_t>()) << 32;
                }
            }
        }

        // Parse dungeon feature (f field) — used for open-door detection etc.
        if (auto f = cell.find("f"); f != cell.end() && f->is_number()) {
            int feat = f->get<int>();
            m_map[pos].tileData().feature = feat;
            // Promote closed Door to OpenDoor when server sends open-door feature
            if (feat == TileData::DNGN_OPEN_DOOR && m_map[pos].type() == MapType::Door) {
                TileData saved = m_map[pos].tileData();
                m_map[pos] = Tile(MapType::OpenDoor);
                m_map[pos].tileData() = saved;
            }
        }

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

        // "mon" is an object — partial or full update.
        // A monster at this cell takes visual precedence over any object.
        m_objects.erase(pos);

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
    dump();
    m_didRender = false;
}
