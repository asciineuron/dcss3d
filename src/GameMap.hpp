#pragma once

#include "MessageQueue.hpp"
#include "Turn.hpp"
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
#include <optional>

enum class MapType {
    Wall,
    Floor,
    Unexplored,
    Other,
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

private:
    MapType m_type;
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

    void handleMessage(const json& message) override;

    void shift(Direction moveDir);

    bool wouldCollide(const glm::vec2&) const;

    const MapData& map() const { return m_map; }

    // check if rendered since last handleMessage() potential update:
    const bool didRender() const { return m_didRender; };
    void setDidRender(bool didRender) { m_didRender = didRender; };

    struct Bounds { int x_min, x_max, y_min, y_max; };
    Bounds getBounds() const;
    std::optional<MapType> getTileAt(int x, int y) const;

    friend std::ostream& operator<<(std::ostream&, const GameMap&);
    std::string asciiView() const;

private:
    MapData m_map;
    bool m_didRender {};
    void updateMap(const json& message); // call from handleMessage()
};

glm::vec4 mapTypeToColor(MapType type);

// takes map index and converts to its *center* in render coords
inline glm::vec2 mapCoordToRender(const Pos2<int>& coord)
{
    return { static_cast<float>(coord.x) + 0.5f,
        static_cast<float>(coord.y) - 0.5f };
}
