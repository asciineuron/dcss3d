#include "MessageQueue.hpp"
#include "PlayerState.hpp"
#include "Renderer.hpp"
#include "Turn.hpp"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlgpu3.h"
#include "imguilayouts.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_scancode.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <signal.h>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

std::unique_ptr<Turn> process_key(SDL_KeyboardEvent key, Player& player, Renderer& renderer, bool& isDone)
{
    // TODO prevent diagonal speedup

    float velocity = 1.0f;
    if (key.type == SDL_EVENT_KEY_UP)
        velocity = 0.0f; // reset movement when key released

    if (key.mod == SDL_KMOD_LSHIFT)
        velocity *= 2.0f;

    switch (key.scancode) {
    case SDL_SCANCODE_W:
        player.setVelY(velocity);
        break;
    case SDL_SCANCODE_S:
        player.setVelY(-velocity);
        break;
    case SDL_SCANCODE_A:
        player.setVelX(-velocity);
        break;
    case SDL_SCANCODE_D:
        player.setVelX(velocity);
        break;
    case SDL_SCANCODE_SPACE:
        return std::make_unique<MoveTurn>(Here);
        break;
    case SDL_SCANCODE_Q:
        // TODO 2-26 now not fully quitting with q, need ctrl-c, probably related to rendering freeze bug
        spdlog::debug("received q key");
        isDone = true;
        break;
    case SDL_SCANCODE_ESCAPE:
        if (key.type == SDL_EVENT_KEY_UP) {
            spdlog::debug("before: {}", renderer.renderUI());
            renderer.setRenderUI(!renderer.renderUI());
            spdlog::debug("after: {}", renderer.renderUI());
        }
        break;
    case SDL_SCANCODE_RETURN:
        return std::make_unique<InputTurn>(SDL_SCANCODE_RETURN);
        break;
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
        return process_key(event.key, player, renderer, isDone);
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
        spdlog::debug("left mouse clicked");
    }
    return nullptr;
}

constexpr uint32_t FRAMERATE_MS = (1.0f / 60.0f) * 1000.0f;

pid_t runRelayServer()
{
    // const std::string scriptPath = std::format("{}dcss_server.py", SDL_GetBasePath());
    const std::string scriptPath = std::format("{}dcss_server_wrapper.sh", SDL_GetBasePath());
    spdlog::debug("script path: '{}'", scriptPath);

    pid_t pid;
    if (!(pid = fork())) {
        // child
        if (execlp(scriptPath.c_str(), scriptPath.c_str(), (char*)NULL) == -1) {
            throw std::runtime_error(std::format("exec error: {}", strerror(errno)));
        }
    }
    return pid;
}

void stopRelayServer(pid_t child)
{
    kill(child, SIGINT);
    waitpid(child, nullptr, 0);
}

int main(int argc, char* argv[])
{
    pid_t relayPID = runRelayServer();

    spdlog::set_level(spdlog::level::debug);

    bool didFail = false;
    try {
        Player player;
        GameTime gameTime;
        GameMap map;
        Renderer renderer; // imgui + SDL setup too
        NetworkManager networkManager(std::format("{}dcss3d.sock", SDL_GetBasePath()));
        ImGuiIO& io = ImGui::GetIO();
        // could add e.g. logger

        handlerConfig responseHandlers = { { "map", { player, gameTime, map } } };

        bool isDone = false;
        while (!isDone) {
            std::vector<json> responses = networkManager.getNewMessages();
            if (!responses.empty())
                processMessages(responseHandlers, responses);

            ImGui_ImplSDLGPU3_NewFrame();
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();

            // imgui functions here
            ImGui::ShowDemoWindow();
            displayPlayer(player);
            displayMap(map);
            networkMenu(networkManager);
            renderMenu(renderer);

            ImGui::Render();

            renderer.doRender(map, player.camera()); // imgui layout too

            gameTime.update();

            std::unique_ptr<Turn> turn;
            turn = player.updatePosition(gameTime, map);
            if (turn) {
                spdlog::debug("generated turn: {}", turn->asMessage().dump());
                networkManager.sendMessage(turn->asMessage());
                continue; // re-render before handling potentially turn-generating input
                // TODO ^ this continue is why infinite loop, each render updatePosition updates the position so never gets to mouse or keyboard input...
            }

            // process mouse input separately from SDL_PollEvent to reduce overhead:
            turn = nullptr;
            if (!io.WantCaptureMouse)
                turn = processMouseInput(player);
            if (turn) {
                spdlog::debug("generated turn: {}", turn->asMessage().dump());
                networkManager.sendMessage(turn->asMessage());
                continue;
            }

            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                ImGui_ImplSDL3_ProcessEvent(&event);
                if (!(io.WantCaptureMouse || io.WantCaptureKeyboard)) {
                    turn = processInput(event, renderer, player, isDone);
                    if (turn) {
                        spdlog::debug("generated turn: {}", turn->asMessage().dump());
                        networkManager.sendMessage(turn->asMessage());
                        break;
                    }
                }
            }

            // wait for 60fps
            // SDL_Delay(FRAMERATE_MS - (1000.0f * gameTime.dt()));
        }
    } catch (const std::runtime_error& e) {
        spdlog::error("Received unhandled exception: {}", e.what());
        didFail = true;
    }

    stopRelayServer(relayPID);

    return didFail ? EXIT_FAILURE : EXIT_SUCCESS;
}
