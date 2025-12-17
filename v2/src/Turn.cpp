#include "Turn.hpp"
#include <iostream>

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
