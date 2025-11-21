#pragma once

#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string_view>

using json = nlohmann::json;

class Turn {
public:
    virtual json asMessage() const = 0;
};

// enum class Direction {
//     North,
//     East,
//     South,
//     West,
//     NorthEast,
//     SouthEast,
//     SouthWest,
//     NorthWest,
//     Here, // '.' key, wait
// };
enum Direction {
    None = 0,
    North = 1 << 0,
    East = 1 << 1,
    South = 1 << 2,
    West = 1 << 3,
    NorthEast =  (1 << 0) +  (1 << 1),
    SouthEast = (1 << 1) +  (1 << 2),
    SouthWest = (1 << 2) +  (1 << 3),
    NorthWest = (1 << 0) +  (1 << 3),
    Here = 1 << 4 // '.' key, wait
};

class MoveTurn : public Turn {
public:
    MoveTurn(Direction);
    json asMessage() const override;

private:
    Direction m_direction;
};

MoveTurn::MoveTurn(Direction direction)
    : m_direction { direction }
{
}

json MoveTurn::asMessage() const
{
    // using enum Direction;
    std::string_view dir_val; // string_view ok since binding to string literal?
    switch (m_direction) {
    case North:
        dir_val = "8";
        break;
    case East:
        dir_val = "2";
        break;
    case South:
        dir_val = "6";
        break;
    case West:
        dir_val = "4";
        break;
    default:
        throw std::logic_error("invalid direction specified");
    }

    json message;
    message["msg"] = "input";
    message["text"] = dir_val;

    std::cerr << "move as message: " << message << "\n";
    return message;
}
