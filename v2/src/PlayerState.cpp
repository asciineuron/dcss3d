#include "PlayerState.hpp"
#include "Turn.hpp"
#include "GameMap.hpp"
#include <numbers>

using namespace std::numbers;

void Player::handleMessage(const json& message)
{
    if (message["msg"] != "map")
        return;

    // reset to default position
    m_camera.pos = { 0.5f, 0.f, -0.5f };
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
    m_camera.phi -= mouse_dy * s_baseVelocity;

    // wraparound into [0, 2pi] range:
    m_camera.theta = wrap(m_camera.theta, 0, 2 * pi);
    m_camera.phi = wrap(m_camera.phi, 0, 2 * pi);
}

std::unique_ptr<Turn> Player::updatePosition(const GameTime& gameTime, const GameMap& gameMap)
{

    float dx = m_velY * std::cos(m_camera.theta) + m_velX * std::cos(pi / 2. - m_camera.theta);
    float dy = -m_velY * std::cos(pi / 2. - m_camera.theta) + m_velX * std::cos(m_camera.theta);

    float x_disp = gameTime.dt() * dx;
    float y_disp = gameTime.dt() * dy;

    // this or flip y sign?
    glm::vec2 testPosition = { m_camera.pos[0] + x_disp, m_camera.pos[1] + y_disp };
    if (gameMap.wouldCollide(testPosition))
        return nullptr;

    // check if generated a move
    Direction moveDir = None;
    // crossed int threshold:
    // TODO: check these most likely wrong...
    if (std::floor(testPosition[0]) - std::floor(m_camera.pos[0]) > 0.)
        moveDir = (Direction)(moveDir | East);
    if (std::floor(testPosition[0]) - std::floor(m_camera.pos[0]) < 0.)
        moveDir = (Direction)(moveDir | West);
    if (std::floor(testPosition[1]) - std::floor(m_camera.pos[1]) > 0.)
        moveDir = (Direction)(moveDir | North);
    if (std::floor(testPosition[1]) - std::floor(m_camera.pos[1]) < 0.)
        moveDir = (Direction)(moveDir | South);

    // update position:
    m_camera.pos[0] = testPosition[0];
    // or pos[2] ?
    m_camera.pos[1] = testPosition[1];

    // make move turn
    if (moveDir != None) {
        return std::make_unique<MoveTurn>(moveDir);
    } else {
        return nullptr;
    }
}
