#pragma once
#include "MessageQueue.hpp"
#include "Renderer.hpp"
#include "Turn.hpp"

// TODO: every game render, coordinates are shifted back to 0,
// so don't need to handle in player since gets reset each time...
// But also affects camera.....

// TODO where/how to handle this now?
class GameTime : public MessageHandler {
public:
    const double dt() const { return m_dt; }

    void update()
    {
        m_lastTick = m_curTick;
        m_curTick = SDL_GetTicks();
        m_dt = (m_curTick - m_lastTick) / 1000.0;
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
    double m_dt {};
};

// primarily responsible for controlling the camera
class Player : public MessageHandler {
public:
    void handleMessage(const json& message) override;

    const Camera& camera() const { return m_camera; };

    // rotate camera:
    void updateView(const float mouse_dx, const float mouse_dy); 
    // check collisions here, displace camera:
    std::unique_ptr<Turn> updatePosition(const GameTime&, const GameMap&); 

    void setVelX(float vel_x) { m_velX = vel_x; }
    void setVelY(float vel_y) { m_velY = vel_y; }

private:
    Camera m_camera; // TODO these shift too each render, should watch for map update? can then reset DO THIS IN HANDLE_MESSAGE
    float m_velX {};
    float m_velY {};

    static constexpr double s_mouseSensitivity = 0.005;
    static constexpr double s_baseVelocity = 1.5;
};
