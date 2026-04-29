#pragma once
#include "MessageQueue.hpp"
#include "Renderer.hpp"
#include "Turn.hpp"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// TODO: every game render, coordinates are shifted back to 0,
// so don't need to handle in player since gets reset each time...
// But also affects camera.....

// TODO where/how to handle this now?

// Forward declare Direction enum from Turn.hpp
enum Direction;

// --- PlayerData: mirrors upstream webtiles JS player object ---
// See: game_data/static/player.js (game_init.player handler)
// Fields match the server's "player" message JSON keys 1:1.

struct InventoryItem {
    std::string name;
    int count = 0;
    int slot = -1;    // letter index ('a' = 0, 'b' = 1, ...)
    int idx = -1;     // item type index
    int base_type = -1; // 0=weapon, 1=missile, 2=armour, 100=empty
};

struct PlayerData {
    std::string name;
    std::string god;
    std::string title;
    std::string species;

    int hp = 0;
    int hp_max = 0;
    int real_hp_max = 0;
    int poison_survival = 0;

    int mp = 0;
    int mp_max = 0;
    int dd_real_mp_max = 0;

    int ac = 0;
    int ev = 0;
    int sh = 0;

    int xl = 0;
    int progress = 0;  // % progress to next XL

    int time = 0;
    int time_delta = 0;

    int gold = 0;

    int str = 0;
    int intel = 0;     // 'int' is a keyword
    int dex = 0;
    int str_max = 0;
    int intel_max = 0;
    int dex_max = 0;

    int piety_rank = 0;
    bool penance = false;

    std::vector<std::string> status;   // active status effects
    std::unordered_map<int, InventoryItem> inv;  // key = slot letter index

    int weapon_index = -1;
    int offhand_index = -1;
    bool offhand_weapon = false;  // true if dual-wielding (Coglin)
    int quiver_item = -1;
    std::string quiver_desc;      // formatted quiver description
    std::string unarmed_attack;
    int unarmed_attack_colour = 7; // default light grey

    int pos_x = 0;
    int pos_y = 0;

    int wizard = 0;
    int explore = 0;

    int depth = 0;
    std::string place;

    int contam = 0;
    int noise = 0;
    int adjusted_noise = 0;
};
class GameTime : public MessageHandler {
public:
    /**
     * Seconds since the last render frame.
     */
    double dt()
    {
        m_curTick = SDL_GetTicks();
        double dt = (m_curTick - m_lastTick) / 1000.0;
        return dt;
    }

    /**
     * Reset timestamp.
     */
    void update()
    {
        m_lastTick = m_curTick;
    }

    void handleMessage(const json& message) override
    {
        // assume map send iff turn updated
        if (message["msg"] != "map") {
            ++m_gameTurn;
        }
    }

private:
    uint64_t m_curTick {};
    uint64_t m_lastTick {};
    uint64_t m_gameTurn {};
};

// primarily responsible for controlling the camera
class Player : public MessageHandler {
public:
    void handleMessage(const json& message) override;

    const Camera& camera() const { return m_camera; };
    const PlayerData& data() const { return m_data; };

    // rotate camera:
    void updateView(const float mouse_dx, const float mouse_dy);
    // check collisions here, displace camera:
    std::unique_ptr<Turn> updatePosition(GameTime&, const GameMap&);

    float velX() const { return m_velX; }
    float velY() const { return m_velY; }
    void setVelX(float vel_x) { m_velX = vel_x; }
    void setVelY(float vel_y) { m_velY = vel_y; }
    void scaleVel(float scale) { m_velX *= scale; m_velY *= scale; }

    // Convert camera yaw to one of 8 compass directions (N, NE, E, SE, S, SW, W, NW).
    Direction getFacingDirection() const;

    // Return the game-map cell adjacent to the player in the facing direction.
    Pos2<int> getTargetCell() const;

private:
    void handlePlayerMessage(const json& message);

    Camera m_camera; // TODO these shift too each render, should watch for map update? can then reset DO THIS IN HANDLE_MESSAGE
    float m_velX {};
    float m_velY {};
    Direction m_lastMoveDirection { None }; // Track last move for camera adjustment on map update
    PlayerData m_data;
    int m_lastTime = 0; // for time_delta calculation

    static constexpr float s_mouseSensitivity = 0.005;
};
