#pragma once

#include "MessageQueue.hpp"
#include <glm/glm.hpp>

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
    Tile(MapType type)
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
};

// int or float?
class GameMap : public MessageHandler {
public:
    // TODO: could sort by Pos2 distance and use std::map, near-to-far
    using MapData = std::unordered_map<Pos2<int>, Tile>;

    void handleMessage(const json& message) override;

    bool wouldCollide(const glm::vec2&) const;

    const MapData& map() const { return m_map; }

    // check if rendered since last handleMessage() potential update:
    const bool didRender() const { return m_didRender; };
    void setDidRender(bool didRender) { m_didRender = didRender; };

private:
    MapData m_map;
    bool m_didRender {};
    void updateMap(const json& message); // call from handleMessage()
};

// takes map index and converts to its *center* in render coords
glm::vec2 mapCoordToRender(const Pos2<int>& coord)
{
    return { static_cast<float>(coord.x) + 0.5f,
        static_cast<float>(coord.y) - 0.5f };
}
