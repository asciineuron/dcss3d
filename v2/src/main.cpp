#include "MessageQueue.hpp"
#include "PlayerState.hpp"
#include "Renderer.hpp"
#include "Turn.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>

std::unique_ptr<Turn> process_key(SDL_Scancode key, Player& player, bool& isDone)
{
    // TODO prevent diagonal speedup

    float velocity = 1.0f;
    if (key & SDL_SCANCODE_LSHIFT)
        velocity *= 2.0f;

    switch (key) {
    case SDL_SCANCODE_W:
        player.setVelY(velocity);
        break;
    case SDL_SCANCODE_S:
        player.setVelY(velocity);
        break;
    case SDL_SCANCODE_A:
        player.setVelX(-velocity);
        break;
    case SDL_SCANCODE_D:
        player.setVelX(velocity);
        break;
    case SDL_SCANCODE_SPACE:
        return std::make_unique<MoveTurn>(Here);
    default:
        break;
    }
    return nullptr;
}

std::unique_ptr<Turn> processInput(const SDL_Event& event, Renderer& renderer, Player& player, bool& isDone)
{
    switch (event.type) {
    case SDL_EVENT_QUIT:
        isDone = true;
        break;
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        isDone = true;
        break;
    case SDL_EVENT_KEY_UP:
    case SDL_EVENT_KEY_DOWN:
        return process_key(event.key.scancode, player, isDone);
    case SDL_EVENT_WINDOW_RESIZED:
        break;
    case SDL_EVENT_WINDOW_MOUSE_ENTER:
        if (!SDL_SetWindowRelativeMouseMode(renderer.window(), true)) {
            throw std::runtime_error(std::format("SDL_SetWindowRelativeMouseMode error: {}", SDL_GetError()));
        }
    default:
        break;
    }
    return nullptr;
}

std::unique_ptr<Turn> processMouseInput(Player& player)
{
    float mouse_dx, mouse_dy;
    SDL_MouseButtonFlags mouseState = SDL_GetRelativeMouseState(&mouse_dx, &mouse_dy);

    player.updateView(mouse_dx, mouse_dy);

    if (mouseState & SDL_BUTTON_LMASK) {
        // generate attack turn
        // TODO: how to interact with imgui? and suppress if UI overlay active
        std::cerr << "left mouse clicked\n";
    }
    return nullptr;
}

int main(int argc, char* argv[])
{
    bool didFail = false;
    try {
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            throw std::runtime_error(std::format("SDL_Init failure: {}", SDL_GetError()));
        }

        auto player = std::make_shared<Player>();
        auto gameTime = std::make_shared<GameTime>();
        auto map = std::make_shared<GameMap>();
        Renderer renderer;
        NetworkManager networkManager;

        GameResponseQueue responseQueue;
        responseQueue.addHandler(std::vector<std::string> { "map" }, player); // reset camera
        responseQueue.addHandler(std::vector<std::string> { "map" }, gameTime); // increment turn
        responseQueue.addHandler(std::vector<std::string> { "map" }, map); // update map data
        // could add e.g. imgui overlaarrat to receive data as well, or logger

        bool isDone = false;
        while (!isDone) {
            std::vector<json> responses = networkManager.getNewMessages();
            if (!responses.empty())
                responseQueue.processMessages(responses);

            renderer.doRender(*map, player->camera());

            gameTime->update();

            std::unique_ptr<Turn> turn;
            turn = player->updatePosition(*gameTime, *map);
            if (turn) {
                networkManager.sendMessage(turn->asMessage());
                continue; // re-render before handling potentially turn-generating input
            }

            // process mouse input separately from SDL_PollEvent to reduce overhead:
            turn = processMouseInput(*player);
            if (turn) {
                networkManager.sendMessage(turn->asMessage());
                continue;
            }

            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                turn = processInput(event, renderer, *player, isDone);
                if (turn) {
                    networkManager.sendMessage(turn->asMessage());
                    break;
                }
            }
        }
    } catch (const std::runtime_error& e) {
        std::cerr << "Received unhandled exception: " << e.what() << "\n";
        didFail = true;
    }

    SDL_Quit();
    return didFail ? EXIT_FAILURE : EXIT_SUCCESS;
}
