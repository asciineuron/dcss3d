#pragma once

#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string_view>

using json = nlohmann::json;

class Turn {
public:
    virtual ~Turn() = default;
    virtual json asMessage() const = 0;
};

enum Direction {
    None = 0,
    North = 1 << 0,
    East = 1 << 1,
    South = 1 << 2,
    West = 1 << 3,
    NorthEast = (1 << 0) + (1 << 1),
    SouthEast = (1 << 1) + (1 << 2),
    SouthWest = (1 << 2) + (1 << 3),
    NorthWest = (1 << 0) + (1 << 3),
    Here = 1 << 4, // '.' key, wait
    DirectionSize
};

extern const char* directionToString[DirectionSize];

class MoveTurn : public Turn {
public:
    MoveTurn(Direction);
    json asMessage() const override;

private:
    Direction m_direction;
};
