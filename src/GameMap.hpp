#pragma once

#include "MessageQueue.hpp"
#include "Turn.hpp"
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <cstdint>
#include <unordered_set>

enum class MapType {
    // Currently visible types — rendered in 3D
    Wall,         // MF_WALL (2)
    Floor,        // MF_FLOOR (1)
    Door,         // MF_DOOR (5)
    Item,         // MF_ITEM (6)
    Water,        // MF_WATER (16), MF_DEEP_WATER (22)
    Lava,         // MF_LAVA (17)
    Other,        // MF_FEATURE (15), etc.

    // Non-visible types — not rendered in 3D, shown in overlay only
    WallMemory,   // MF_MAP_WALL (4)
    FloorMemory,  // MF_MAP_FLOOR (3)
    Unexplored,   // MF_UNSEEN (0), MF_EXPLORE_HORIZON (26)
};

// Tile data from the server's "t" field.  Mirrors the JS client's tile data
// handling — visibility is determined from t.bg flags, not from mf alone.
struct TileData {
    uint64_t bg = 0;
    bool hasBg = false;

    // Background flag constants (from enums.js bg_flags)
    static constexpr uint64_t UNSEEN    = 0x00040000;
    static constexpr uint64_t MM_UNSEEN = 0x00020000;

    bool isVisible() const {
        return hasBg && !(bg & UNSEEN) && !(bg & MM_UNSEEN);
    }
};

// also monster items etc. see 'dungeon features'
// TODO contain both base floor type and wall? e.g. if we clear a wall need to know floor type underneath
class Tile {
public:
    Tile(MapType type = MapType::Other)
        : m_type(type)
    {
    }

    MapType type() const { return m_type; };
    TileData& tileData() { return m_tileData; }
    const TileData& tileData() const { return m_tileData; }
    bool isVisible() const { return m_tileData.isVisible(); }

private:
    MapType m_type;
    TileData m_tileData;
};

// Represents a monster/creature on the map, storing parsed JSON data from the server.
// Mirrors the `mon` object in webtiles map cell updates (see MONSTER_DATA_API.md).
class Monster {
public:
    Monster() = default;

    // Merge in a partial update from the server JSON `mon` object.
    // Only overwrites fields that are present in the incoming JSON.
    // If this is a brand-new monster and fields are missing, sensible defaults are used.
    void merge(const json& monJson);

    // -- Accessors --
    uint32_t id() const { return m_id; }
    void setId(uint32_t id) { m_id = id; }

    int type() const { return m_type; }
    int att() const { return m_att; }
    int threat() const { return m_threat; }
    const std::string& name() const { return m_name; }
    const std::string& plural() const { return m_plural; }
    int btype() const { return m_btype; }
    bool hasBtype() const { return m_hasBtype; }
    int typedataAvghp() const { return m_typedataAvghp; }
    bool typedataNoExp() const { return m_typedataNoExp; }
    bool hasTypedata() const { return m_hasTypedata; }
    uint32_t clientid() const { return m_clientid; }
    bool hasClientid() const { return m_hasClientid; }

private:
    uint32_t m_id {};
    int m_type {};
    int m_att {};         // mon_attitude_type (0=hostile, 1=neutral, 2=strict_neutral, 3=good_neutral, 4=friendly, 5=marionette)
    int m_threat {};      // mon_threat_level_type (0=trivial, 1=easy, 2=tough, 3=nasty, 4=undefined)
    std::string m_name;
    std::string m_plural;
    int m_btype {};
    bool m_hasBtype {};
    int m_typedataAvghp {};
    bool m_typedataNoExp {};
    bool m_hasTypedata {};
    uint32_t m_clientid {};
    bool m_hasClientid {};
};

template <typename T>
struct Pos2 {
    T x;
    T y;

    // bool operator==(const Pos2& other) const { return x == other.x && y == other.y; }
    auto operator<=>(const Pos2& other) const = default;
};
// TODO: ^replace with std tuple int to simplify, otherwise need to define custom hash function for unordered_map
// nope, even std tuple doesn't define one...

// either 1 define keyhash and keyequal structs
// or 2 define const == operator and specialize std::hash for type, 2 is much better

namespace std {
template <>
struct hash<Pos2<int>> {
    std::size_t operator()(const Pos2<int>& p) const
    {
        return std::hash<int> {}(p.x) ^ std::hash<int> {}(p.y);
    }
};
};

// int or float?
class GameMap : public MessageHandler {
public:
    // TODO: could sort by Pos2 distance and use std::map, near-to-far
    using MapData = std::unordered_map<Pos2<int>, Tile>;

    // Monster storage:
    //   m_monsters:    position → monster ID (which monster is at which cell)
    //   m_monsterTable: monster ID → full Monster data (the global table)
    using MonsterPosMap = std::unordered_map<Pos2<int>, uint32_t>;
    using MonsterTable = std::unordered_map<uint32_t, Monster>;

    void handleMessage(const json& message) override;

    void shift(Direction moveDir);

    bool wouldCollide(const glm::vec2&) const;

    const MapData& map() const { return m_map; }

    // Monster accessors
    const MonsterPosMap& monsterPositions() const { return m_monsters; }
    const MonsterTable& monsterTable() const { return m_monsterTable; }
    std::optional<std::reference_wrapper<const Monster>> getMonsterAt(int x, int y) const;

    // check if rendered since last handleMessage() potential update:
    bool didRender() const { return m_didRender; };
    void setDidRender(bool didRender) { m_didRender = didRender; };

    struct Bounds { int x_min, x_max, y_min, y_max; };
    Bounds getBounds() const;
    std::optional<MapType> getTileAt(int x, int y) const;
    bool isVisibleAt(int x, int y) const;

    friend std::ostream& operator<<(std::ostream&, const GameMap&);
    std::string asciiView() const;
    std::string dumpString() const;  // returns compact grid dump as string
    void dump() const;  // logs dumpString() at info level

private:
    MapData m_map;
    MonsterPosMap m_monsters;       // position → monster ID
    MonsterTable m_monsterTable;     // monster ID → Monster data
    bool m_didRender {};
    Bounds m_bounds { 0, 0, 0, 0 };
    void updateMap(const json& message); // call from handleMessage()
    void updateBounds();
    void cleanMonsterTable();        // remove IDs not referenced by any cell in m_monsters
};

glm::vec4 mapTypeToColor(MapType type);

// takes map index and converts to its *center* in render coords
inline glm::vec2 mapCoordToRender(const Pos2<int>& coord)
{
    return { static_cast<float>(coord.x) + 0.5f,
        static_cast<float>(coord.y) - 0.5f };
}