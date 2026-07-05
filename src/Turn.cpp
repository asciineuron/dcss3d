#include "Turn.hpp"
#include "AudioManager.hpp"
#include <SDL3/SDL_scancode.h>
#include <spdlog/spdlog.h>

// Array indexed by Direction enum values (which are non-sequential bitmask values).
// Ensure each enum value maps to the correct string.
const char* directionToString[DirectionSize] = {
    /*  0 */ "None",
    /*  1 */ "North",
    /*  2 */ "East",
    /*  3 */ "NorthEast",
    /*  4 */ "South",
    /*  5 */ nullptr,
    /*  6 */ "SouthEast",
    /*  7 */ nullptr,
    /*  8 */ "West",
    /*  9 */ "NorthWest",
    /* 10 */ nullptr,
    /* 11 */ nullptr,
    /* 12 */ "SouthWest",
    /* 13 */ nullptr,
    /* 14 */ nullptr,
    /* 15 */ nullptr,
    /* 16 */ "Here"
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
        dir_val = "6";
        break;
    case South:
        dir_val = "2";
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

void MoveTurn::playSound(AudioManager& audio) const
{
    audio.triggerSound("footfall");
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

TextTurn::TextTurn(std::string text)
    : m_text { std::move(text) }
{
}

json TextTurn::asMessage() const
{
    return { { "msg", "input" }, { "text", m_text } };
}

AttackTurn::AttackTurn(Direction direction)
    : m_direction { direction }
{
}

void AttackTurn::triggerAnimation(SpriteManager& sm) const
{
    sm.doSwing();
}

json AttackTurn::asMessage() const
{
    // Map compass direction to the corresponding vim key for Ctrl-attack.
    // crawl's CONTROL macro: CONTROL('K') = 'K' - 'A' + 1, etc.
    // These trigger CMD_ATTACK_* commands — swing weapon without moving.
    // Direction → vim key → CONTROL value:
    //   N=K(11)  S=J(10)  E=L(12)  W=H(8)
    //   NE=U(21) SE=N(14) SW=B(2)  NW=Y(25)
    int ctrlKeycode = 0;
    using enum Direction;
    switch (m_direction) {
    case North:
        ctrlKeycode = 'K' - 'A' + 1;
        break;
    case South:
        ctrlKeycode = 'J' - 'A' + 1;
        break;
    case East:
        ctrlKeycode = 'L' - 'A' + 1;
        break;
    case West:
        ctrlKeycode = 'H' - 'A' + 1;
        break;
    case NorthEast:
        ctrlKeycode = 'U' - 'A' + 1;
        break;
    case SouthEast:
        ctrlKeycode = 'N' - 'A' + 1;
        break;
    case SouthWest:
        ctrlKeycode = 'B' - 'A' + 1;
        break;
    case NorthWest:
        ctrlKeycode = 'Y' - 'A' + 1;
        break;
    default:
        throw std::logic_error("invalid attack direction");
    }

    json message;
    message["msg"] = "key";
    message["keycode"] = ctrlKeycode;

    return message;
}
