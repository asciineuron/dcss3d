#include "PlayerState.hpp"
#include "GameMap.hpp"
#include "Turn.hpp"
#include "debug.hpp"
#include <numbers>
#include <spdlog/spdlog.h>

using namespace std::numbers;

void Player::handleMessage(const json& message)
{
    if (message["msg"] == "player") {
        handlePlayerMessage(message);
        return;
    }

    if (message["msg"] != "map")
        return;

    // When the server sends a new map, the player's position is reset to the "incoming side"
    // of the new tile (e.g., entering from south means y=-1.5 instead of y=0.5).
    // We need to shift the camera in the opposite direction to maintain visual continuity.
    if (m_lastMoveDirection != None) {
        spdlog::debug("Adjusting camera for map update, last move: {}",
                      directionToString[m_lastMoveDirection]);

        // Shift camera opposite to the movement direction
        // Remember: camera pos[0]=X (sideways), pos[2]=Z (forward/backward)
        // Game map: X=sideways, Y=forward (mapped to camera Z)
        switch (m_lastMoveDirection) {
            case North:
                // Moving north (y decreases by 1), shift camera south (y increases)
                m_camera.pos[2] += 1.0f;
                break;
            case South:
                // Moving south (y increases by 1), shift camera north (y decreases)
                m_camera.pos[2] -= 1.0f;
                break;
            case East:
                // Moving east (x increases by 1), shift camera west (x decreases)
                m_camera.pos[0] -= 1.0f;
                break;
            case West:
                // Moving west (x decreases by 1), shift camera east (x increases)
                m_camera.pos[0] += 1.0f;
                break;
            case NorthEast:
                m_camera.pos[0] -= 1.0f;
                m_camera.pos[2] += 1.0f;
                break;
            case SouthEast:
                m_camera.pos[0] -= 1.0f;
                m_camera.pos[2] -= 1.0f;
                break;
            case SouthWest:
                m_camera.pos[0] += 1.0f;
                m_camera.pos[2] -= 1.0f;
                break;
            case NorthWest:
                m_camera.pos[0] += 1.0f;
                m_camera.pos[2] += 1.0f;
                break;
            case Here:
            case None:
            case DirectionSize:
                // No directional movement, no camera adjustment needed
                break;
        }

        spdlog::debug("Camera adjusted to ({:.3f}, {:.3f}, {:.3f})",
                      m_camera.pos[0], m_camera.pos[1], m_camera.pos[2]);

        // Clear the last move direction since we've applied the adjustment
        m_lastMoveDirection = None;
    }
}

void Player::handlePlayerMessage(const json& message)
{
    // Mirror upstream JS handle_player_message() in player.js:543-593

    // 1. Merge inventory items (like JS: $.extend(player.inv[i], data.inv[i]))
    if (auto inv = message.find("inv"); inv != message.end() && inv->is_object()) {
        for (const auto& [key, itemJson] : inv->items()) {
            int slot = std::stoi(key);  // JS uses string keys like "0", "1", ...
            auto& item = m_data.inv[slot];
            item.slot = slot;
            if (auto n = itemJson.find("name"); n != itemJson.end())
                item.name = n->get<std::string>();
            if (auto c = itemJson.find("quantity"); c != itemJson.end())
                item.count = c->get<int>();
            if (auto i = itemJson.find("idx"); i != itemJson.end())
                item.idx = i->get<int>();
            if (auto bt = itemJson.find("base_type"); bt != itemJson.end())
                item.base_type = bt->get<int>();
        }
    }

    // 2. Handle time_delta (JS: calculates based on last_time)
    if (auto t = message.find("time"); t != message.end()) {
        int time = t->get<int>();
        if (m_lastTime != 0)
            m_data.time_delta = time - m_lastTime;
        m_lastTime = time;
        m_data.time = time;
    }

    // 3. Extend remaining fields onto player data (JS: $.extend(player, data))
    // Only update fields present in the message (partial updates are common)
    auto setIf = [&]<typename T>(const char* key, T& field) {
        if (auto it = message.find(key); it != message.end())
            field = it->get<T>();
    };

    setIf("name", m_data.name);
    setIf("god", m_data.god);
    setIf("title", m_data.title);
    setIf("species", m_data.species);
    setIf("hp", m_data.hp);
    setIf("hp_max", m_data.hp_max);
    setIf("real_hp_max", m_data.real_hp_max);
    setIf("poison_survival", m_data.poison_survival);
    setIf("mp", m_data.mp);
    setIf("mp_max", m_data.mp_max);
    setIf("dd_real_mp_max", m_data.dd_real_mp_max);
    setIf("ac", m_data.ac);
    setIf("ev", m_data.ev);
    setIf("sh", m_data.sh);
    setIf("xl", m_data.xl);
    setIf("progress", m_data.progress);
    setIf("gold", m_data.gold);
    setIf("str", m_data.str);
    setIf("int", m_data.intel);
    setIf("dex", m_data.dex);
    setIf("str_max", m_data.str_max);
    setIf("int_max", m_data.intel_max);
    setIf("dex_max", m_data.dex_max);
    setIf("piety_rank", m_data.piety_rank);
    // penance: server sends as integer 0/1, not bool
    if (auto it = message.find("penance"); it != message.end() && it->is_number())
        m_data.penance = it->get<int>() != 0;
    setIf("wizard", m_data.wizard);
    setIf("explore", m_data.explore);
    setIf("depth", m_data.depth);
    setIf("place", m_data.place);
    setIf("contam", m_data.contam);
    setIf("noise", m_data.noise);
    setIf("adjusted_noise", m_data.adjusted_noise);
    setIf("weapon_index", m_data.weapon_index);
    setIf("offhand_index", m_data.offhand_index);
    // offhand_weapon: server sends as integer 0/1, not bool
    if (auto it = message.find("offhand_weapon"); it != message.end() && it->is_number())
        m_data.offhand_weapon = it->get<int>() != 0;
    setIf("quiver_item", m_data.quiver_item);
    setIf("quiver_desc", m_data.quiver_desc);
    setIf("unarmed_attack", m_data.unarmed_attack);
    setIf("unarmed_attack_colour", m_data.unarmed_attack_colour);

    // status: array of strings
    if (auto status = message.find("status"); status != message.end() && status->is_array()) {
        m_data.status.clear();
        for (const auto& s : *status)
            m_data.status.push_back(s.get<std::string>());
    }

    // pos: object {x, y}
    if (auto pos = message.find("pos"); pos != message.end() && pos->is_object()) {
        if (auto x = pos->find("x"); x != pos->end())
            m_data.pos_x = x->get<int>();
        if (auto y = pos->find("y"); y != pos->end())
            m_data.pos_y = y->get<int>();
    }

    spdlog::debug("Player updated: {} XL{} HP:{}/{} MP:{}/{} AC:{} EV:{} SH:{} @ ({},{}) in {}:{}",
                  m_data.name, m_data.xl, m_data.hp, m_data.hp_max,
                  m_data.mp, m_data.mp_max, m_data.ac, m_data.ev, m_data.sh,
                  m_data.pos_x, m_data.pos_y, m_data.place, m_data.depth);
    spdlog::debug("  Equipment: w_idx={} off_idx={} off_wpn={} q_item={} q_desc='{}' unarmed='{}'",
                  m_data.weapon_index, m_data.offhand_index, m_data.offhand_weapon,
                  m_data.quiver_item, m_data.quiver_desc, m_data.unarmed_attack);
}

Direction Player::getFacingDirection() const
{
    // theta ranges [0, 2π). Divide into 8 π/4 sectors, offset by π/8
    // so sectors are centered on compass directions.
    // Camera look: theta=0→+X(East), pi/2→-Z(North), pi→-X(West), 3pi/2→+Z(South)
    const float sector = pi_v<float> / 8.0f;  // π/8
    float t = m_camera.theta;

    using enum Direction;
    if (t < sector || t >= 15.0f * sector)
        return East;
    if (t < 3.0f * sector)
        return NorthEast;
    if (t < 5.0f * sector)
        return North;
    if (t < 7.0f * sector)
        return NorthWest;
    if (t < 9.0f * sector)
        return West;
    if (t < 11.0f * sector)
        return SouthWest;
    if (t < 13.0f * sector)
        return South;
    return SouthEast;
}

Pos2<int> Player::getTargetCell() const
{
    Direction dir = getFacingDirection();
    int dx = 0, dy = 0;
    // Game coordinate offsets: North = -y, South = +y, East = +x, West = -x
    // (same convention as GameMap::shift)
    if (dir & North) dy -= 1;
    if (dir & South) dy += 1;
    if (dir & East)  dx += 1;
    if (dir & West)  dx -= 1;
    return { m_data.pos_x + dx, m_data.pos_y + dy };
}

static float wrap(float x, float min, float max)
{
    if (min > max)
        return wrap(x, max, min);
    return (x >= 0 ? min : max) + fmod(x, max - min);
}

void Player::updateView(const float mouse_dx, const float mouse_dy)
{
    m_camera.theta -= mouse_dx * s_mouseSensitivity;
    m_camera.phi -= mouse_dy * s_mouseSensitivity;

    // wraparound yaw into [0, 2pi] range:
    m_camera.theta = wrap(m_camera.theta, 0, 2 * pi);
    // clamp pitch to avoid flipping:
    m_camera.phi = std::clamp(m_camera.phi, -pi_v<float> / 2.f + 0.01f, pi_v<float> / 2.f - 0.01f);
}

/**
 * Adjusts position to be relative new cell. If player moves in direction, the map is
 * updated relative the new position, so the position must be given relative to that
 * tile (e.g enter from below, y position flips from top of old tile to bottom of new tile)
 */
glm::vec2 newCellPosition(glm::vec2 testPosition, Direction moveDir)
{
    // remember cube width is 2.0 total +- 1.0 extent
    // TODO check order/sign correct i.e. y reversed
    switch (moveDir) {
    case None:
        break;
    case Here:
        break;
    case North:
        testPosition.y -= 2.0;
        break;
    case East:
        testPosition.x += 2.0;
        break;
    case South:
        testPosition.y += 2.0;
        break;
    case West:
        testPosition.x -= 2.0;
        break;
    case NorthWest:
        testPosition.x -= 2.0;
        testPosition.y -= 2.0;
        break;
    case NorthEast:
        testPosition.x += 2.0;
        testPosition.y -= 2.0;
        break;
    case SouthWest:
        testPosition.x -= 2.0;
        testPosition.y += 2.0;
        break;
    case SouthEast:
        testPosition.x += 2.0;
        testPosition.y += 2.0;
        break;
    default:
        throw std::logic_error("invalid direction specified");
    }
    return testPosition;
}

std::unique_ptr<Turn> Player::updatePosition(GameTime& gameTime, const GameMap& gameMap)
{

    // Calculate world-space movement from camera-relative velocity
    // m_velX/m_velY are camera-relative (forward/backward, left/right)
    // theta is camera yaw angle
    float dx = m_velY * std::cos(m_camera.theta) + m_velX * std::cos(pi / 2. - m_camera.theta);
    float dy = -m_velY * std::cos(pi / 2. - m_camera.theta) + m_velX * std::cos(m_camera.theta);

    float x_disp = gameTime.dt() * dx;
    float y_disp = gameTime.dt() * dy;

    // spdlog::debug("velocity: velX={:.3f}, velY={:.3f}, theta={:.3f} -> dx={:.3f}, dy={:.3f}",
    //     m_velX, m_velY, m_camera.theta, dx, dy);

    // this or flip y sign?
    glm::vec2 testPosition = { m_camera.pos[0] + x_disp, m_camera.pos[2] + y_disp };

    // Check if would collide BEFORE updating position
    bool wouldCollideNow = gameMap.wouldCollide(testPosition);

    // spdlog::debug("testPosition: ({:.3f}, {:.3f}), camera: ({:.3f}, {:.3f}), collide: {}",
    //     testPosition[0], testPosition[1], m_camera.pos[0], m_camera.pos[2], wouldCollideNow);

    // Store old position for move detection
    glm::vec2 oldPosition = { m_camera.pos[0], m_camera.pos[2] };

    // Only update position if no collision
    if (!wouldCollideNow) {
        m_camera.pos[0] = testPosition[0];
        m_camera.pos[2] = testPosition[1];
        // spdlog::debug("position updated to ({:.3f}, {:.3f})", m_camera.pos[0], m_camera.pos[2]);
    } else {
        spdlog::debug("collision prevented, position unchanged");
        // No position change since blocked by wall, no turn needed
        return nullptr;
    }

    // Check if we crossed a tile boundary (compare new vs old position)
    Direction moveDir = None;
    float x_diff = std::floor(m_camera.pos[0]) - std::floor(oldPosition[0]);
    float y_diff = std::floor(m_camera.pos[2]) - std::floor(oldPosition[1]);

    // spdlog::debug("move detection: x_diff={:.3f}, y_diff={:.3f}", x_diff, y_diff);

    if (x_diff > 0.5f)
        moveDir = (Direction)(moveDir | East);
    if (x_diff < -0.5f)
        moveDir = (Direction)(moveDir | West);
    if (y_diff > 0.5f)
        moveDir = (Direction)(moveDir | South);
    if (y_diff < -0.5f)
        moveDir = (Direction)(moveDir | North);

    // make move turn
    if (moveDir != None) {
        spdlog::debug("move direction: {}", directionToString[moveDir]);
        m_lastMoveDirection = moveDir; // Track for camera adjustment on map update
        return std::make_unique<MoveTurn>(moveDir);
    } else {
        return nullptr;
    }
}
