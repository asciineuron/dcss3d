#pragma once

#include <SDL3/SDL_scancode.h>
#include <nlohmann/json.hpp>
#include <unordered_map>

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
    Direction getDirection() const { return m_direction; }

private:
    Direction m_direction;
};

const char defaultGameID[] = "Dungeon Crawl Stone Soup 0.34.0";
json playMessage(std::string_view gameID = defaultGameID);
json loginMessage(std::string_view username, std::string_view password);

// just sends a string as "input" msg,
// same underlying category as MoveTurn but for non movement input e.g. '.' or enter
// since 3d doesn't input wasd movement directly
class InputTurn : public Turn {
public:
    InputTurn(SDL_Scancode input);
    json asMessage() const override;

private:
    SDL_Scancode m_input;
    static const std::unordered_map<SDL_Scancode, const char*> scancodeToText;
};

// Sends a force-attack (swing weapon) in a direction.
// Uses the webtiles "key" protocol with CONTROL(keycode) to trigger
// CMD_ATTACK_* on the server — the character swings without moving.
class AttackTurn : public Turn {
public:
    AttackTurn(Direction);
    json asMessage() const override;

private:
    Direction m_direction;
};
