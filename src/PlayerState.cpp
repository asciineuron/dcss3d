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

    // reset to default position TODO incorrect, needs to be at incoming side of new tile?
    // TODO move to updatePosition?
    // m_camera.pos = { 0.5f, 0.f, -0.5f };
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

    // wraparound into [0, 2pi] range:
    m_camera.theta = wrap(m_camera.theta, 0, 2 * pi);
    m_camera.phi = wrap(m_camera.phi, 0, 2 * pi);
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

    float dx = m_velY * std::cos(m_camera.theta) + m_velX * std::cos(pi / 2. - m_camera.theta);
    float dy = -m_velY * std::cos(pi / 2. - m_camera.theta) + m_velX * std::cos(m_camera.theta);

    float x_disp = gameTime.dt() * dx;
    float y_disp = gameTime.dt() * dy;

    // this or flip y sign?
    glm::vec2 testPosition = { m_camera.pos[0] + x_disp, m_camera.pos[2] + y_disp };
    // TODO: removing this causes freeze, character keeps moving in the initial direction, can't stop/change
    if (gameMap.wouldCollide(testPosition) && !skipCollisionCheck)
        return nullptr;

    // spdlog::debug("test position: {} {}", testPosition[0], testPosition[1]);
    // spdlog::debug("camera position: {} {}", m_camera.pos[0], m_camera.pos[2]);
    // spdlog::debug("floor test position: {} {}", std::floor(testPosition[0]), std::floor(testPosition[1]));
    // spdlog::debug("floor camera position: {} {}", std::floor(m_camera.pos[0]), std::floor(m_camera.pos[2]));

    // check if generated a move
    Direction moveDir = None;
    // crossed int threshold:
    // TODO: check these most likely wrong...
    if (std::floor(testPosition[0]) - std::floor(m_camera.pos[0]) > 0.5) // should be ~ 1.0
        moveDir = (Direction)(moveDir | East);
    if (std::floor(testPosition[0]) - std::floor(m_camera.pos[0]) < -0.5) // should be ~ -1.0
        moveDir = (Direction)(moveDir | West);
    if (std::floor(testPosition[1]) - std::floor(m_camera.pos[2]) > 0.5)
        moveDir = (Direction)(moveDir | North);
    if (std::floor(testPosition[1]) - std::floor(m_camera.pos[2]) < -0.5)
        moveDir = (Direction)(moveDir | South);

    // update position:
    // auto newPos = newCellPosition(testPosition, moveDir);
    // m_camera.pos[0] = newPos[0];
    // m_camera.pos[2] = newPos[1];
    m_camera.pos[0] = testPosition[0];
    m_camera.pos[2] = testPosition[1];

    // make move turn
    if (moveDir != None) {
        spdlog::debug("move direction: {}", directionToString[moveDir]);
        return std::make_unique<MoveTurn>(moveDir);
    } else {
        return nullptr;
    }
}
