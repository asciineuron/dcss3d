#include "PlayerState.hpp"
#include "GameMap.hpp"
#include "Turn.hpp"
#include "debug.hpp"
#include <numbers>
#include <spdlog/spdlog.h>

using namespace std::numbers;

void Player::handleMessage(const json& message)
{
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

    spdlog::debug("velocity: velX={:.3f}, velY={:.3f}, theta={:.3f} -> dx={:.3f}, dy={:.3f}",
        m_velX, m_velY, m_camera.theta, dx, dy);

    // this or flip y sign?
    glm::vec2 testPosition = { m_camera.pos[0] + x_disp, m_camera.pos[2] + y_disp };

    // Check if would collide BEFORE updating position
    bool wouldCollideNow = gameMap.wouldCollide(testPosition);

    spdlog::debug("testPosition: ({:.3f}, {:.3f}), camera: ({:.3f}, {:.3f}), collide: {}",
        testPosition[0], testPosition[1], m_camera.pos[0], m_camera.pos[2], wouldCollideNow);

    // Store old position for move detection
    glm::vec2 oldPosition = { m_camera.pos[0], m_camera.pos[2] };

    // Only update position if no collision
    if (!wouldCollideNow) {
        m_camera.pos[0] = testPosition[0];
        m_camera.pos[2] = testPosition[1];
        spdlog::debug("position updated to ({:.3f}, {:.3f})", m_camera.pos[0], m_camera.pos[2]);
    } else {
        spdlog::debug("collision prevented, position unchanged");
        // No position change since blocked by wall, no turn needed
        return nullptr;
    }

    // Check if we crossed a tile boundary (compare new vs old position)
    Direction moveDir = None;
    float x_diff = std::floor(m_camera.pos[0]) - std::floor(oldPosition[0]);
    float y_diff = std::floor(m_camera.pos[2]) - std::floor(oldPosition[1]);

    spdlog::debug("move detection: x_diff={:.3f}, y_diff={:.3f}", x_diff, y_diff);

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
