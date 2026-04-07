#include "Turn.hpp"
#include <SDL3/SDL_scancode.h>
#include <spdlog/spdlog.h>

const char* directionToString[DirectionSize] = {
    "None",
    "North",
    "East",
    "South",
    "West",
    "NorthEast",
    "SouthEast",
    "SouthWest",
    "NorthWest",
    "Here"
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
    case NorthWest:
        dir_val = "7";
        break;
    case NorthEast:
        dir_val = "9";
        break;
    case SouthWest:
        dir_val = "1";
        break;
    case SouthEast:
        dir_val = "3";
        break;
    case Here:
        dir_val = ".";
        break;
    default:
        throw std::logic_error("invalid direction specified");
    }

    json message;
    message["msg"] = "input";
    message["text"] = dir_val;

    spdlog::debug("move as message: {}", message.dump());
    return message;
}

json playMessage(std::string_view gameID)
{
    return { { "msg", "play" }, { "game_id", gameID } };
}

json loginMessage(std::string_view username, std::string_view password)
{
    return { { "msg", "login" }, { "username", username }, { "password", password } };
}

InputTurn::InputTurn(SDL_Scancode input)
    : m_input { input }
{
}

json InputTurn::asMessage() const
{
    return { { "msg", "input" }, { "text", scancodeToText.at(m_input) } };
}

const std::unordered_map<SDL_Scancode, const char*> InputTurn::scancodeToText = {
    { SDL_SCANCODE_RETURN, ":" },
    { SDL_SCANCODE_KP_GREATER, ">" }, // are the <> scancodes correct?
    { SDL_SCANCODE_KP_LESS, "<" }
};
