#include "MessageQueue.hpp"
#include "PlayerState.hpp"
#include "Renderer.hpp"
#include "Turn.hpp"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlgpu3.h"
#include "imguilayouts.hpp"
#include <SDL3/SDL.h>
#include "imgui_internal.h"
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

// Layout filename (relative to working directory)
constexpr const char* LAYOUT_FILENAME = "window_layout.json";

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
            bool newRenderUI = !renderer.renderUI();
            spdlog::debug("before: {}", renderer.renderUI());
            renderer.setRenderUI(newRenderUI);
            // When showing UI, show the cursor and disable relative mode
            if (newRenderUI) {
                SDL_SetWindowRelativeMouseMode(renderer.window(), false);
                SDL_ShowCursor();
            } else {
                // When hiding UI, re-enable relative mode (automatically hides cursor)
                SDL_SetWindowRelativeMouseMode(renderer.window(), true);
            }
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

// Callback to get current window layout (position and size)
WindowLayout getWindowLayoutCallback(const char* windowName)
{
    WindowLayout layout;
    layout.name = windowName;

    ImGuiWindow* window = ImGui::FindWindowByName(windowName);
    if (window) {
        ImVec2 pos = window->Pos;
        ImVec2 size = window->SizeFull;
        layout.posX = pos.x;
        layout.posY = pos.y;
        layout.sizeX = size.x;
        layout.sizeY = size.y;
        layout.isCollapsed = window->Collapsed;
        layout.isValid = true;
    } else {
        // Window not found, return invalid layout
        layout.isValid = false;
    }

    return layout;
}

// Apply a single window's layout (position and size)
// Uses ImGuiCond_Always to apply explicitly when user loads a layout
void applyWindowLayout(const char* windowName, const WindowLayout& layout)
{
    spdlog::debug("Applying layout for '{}': pos=({},{}), size=({},{}), valid={}",
                  windowName, layout.posX, layout.posY, layout.sizeX, layout.sizeY, layout.isValid);
    ImGui::SetNextWindowPos(ImVec2(layout.posX, layout.posY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(layout.sizeX, layout.sizeY), ImGuiCond_Always);
    if (layout.isCollapsed) {
        ImGui::SetNextWindowCollapsed(true, ImGuiCond_Always);
    }
}

// Apply reset layout (cascading from top-left, auto-sized)
// Uses ImGuiCond_Always to apply explicitly when user resets
void applyResetLayout(const char* windowName, int index)
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 basePos = viewport->Pos;

    // Cascade windows: each 60px offset from previous
    float offset = 60.0f * index;
    ImGui::SetNextWindowPos(ImVec2(basePos.x + offset, basePos.y + offset), ImGuiCond_Always);
    // Size 0,0 means auto-size
    ImGui::SetNextWindowSize(ImVec2(0, 0), ImGuiCond_Always);
}

// Get index for a window name (for cascading reset)
int getWindowIndex(const char* windowName)
{
    static const std::vector<std::string> windowNames = {
        "Demo", "player", "map", "network", "renderer", "settings"
    };

    for (int i = 0; i < static_cast<int>(windowNames.size()); ++i) {
        if (windowNames[i] == windowName) {
            return i;
        }
    }
    return 0;
}

int main(int argc, char* argv[])
{
    spdlog::set_level(spdlog::level::debug);

    pid_t relayPID = runRelayServer();

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

        // Set up window layout callback for save functionality
        setWindowLayoutCallback(getWindowLayoutCallback);

        bool isDone = false;
        while (!isDone) {
            std::vector<json> responses = networkManager.getNewMessages();
            if (!responses.empty())
                processMessages(responseHandlers, responses);

            ImGui_ImplSDLGPU3_NewFrame();
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();

            // Check pending layout actions
            const bool shouldReset = windowLayoutNeedsReset();
            const WindowLayout* pendingLayout = getPendingLayout();
            const char* pendingName = getPendingLayoutName();
            const size_t pendingLayoutCount = getPendingLayoutCount();

            if (pendingLayout && pendingLayoutCount > 0) {
                spdlog::debug("Pending layout load: {} windows", pendingLayoutCount);
            }

            // Helper lambda to find and apply layout for a window
            auto applyLayoutForWindow = [](const char* windowName, const WindowLayout* layout, size_t count) {
                spdlog::debug("applyLayoutForWindow called: name='{}', count={}", windowName, count);
                for (size_t i = 0; i < count; ++i) {
                    spdlog::debug("  Checking[{}]: name='{}', isValid={}", i, layout[i].name, layout[i].isValid);
                    if (layout[i].name == windowName && layout[i].isValid) {
                        spdlog::debug("  Match found, calling applyWindowLayout");
                        applyWindowLayout(windowName, layout[i]);
                        break;
                    }
                }
            };

            // Track which windows we've applied this frame
            // Apply layouts BEFORE Begin() calls

            // Demo window
            if (shouldReset) {
                applyResetLayout("Demo", 0);
            } else if (pendingLayout && pendingLayoutCount > 0) {
                applyLayoutForWindow("Demo", pendingLayout, pendingLayoutCount);
            }

            // player window
            if (shouldReset) {
                applyResetLayout("player", 1);
            } else if (pendingLayout && pendingLayoutCount > 0) {
                applyLayoutForWindow("player", pendingLayout, pendingLayoutCount);
            }
            displayPlayer(player);

            // map window
            if (shouldReset) {
                applyResetLayout("map", 2);
            } else if (pendingLayout && pendingLayoutCount > 0) {
                applyLayoutForWindow("map", pendingLayout, pendingLayoutCount);
            }
            displayMap(map);

            // network window
            if (shouldReset) {
                applyResetLayout("network", 3);
            } else if (pendingLayout && pendingLayoutCount > 0) {
                applyLayoutForWindow("network", pendingLayout, pendingLayoutCount);
            }
            networkMenu(networkManager);

            // renderer window
            if (shouldReset) {
                applyResetLayout("renderer", 4);
            } else if (pendingLayout && pendingLayoutCount > 0) {
                applyLayoutForWindow("renderer", pendingLayout, pendingLayoutCount);
            }
            renderMenu(renderer);

            // settings window
            if (shouldReset) {
                applyResetLayout("settings", 5);
            } else if (pendingLayout && pendingLayoutCount > 0) {
                applyLayoutForWindow("settings", pendingLayout, pendingLayoutCount);
            }
            settingsMenu(LAYOUT_FILENAME);

            if (shouldReset || (pendingLayout && pendingLayoutCount > 0)) {
                clearPendingLayoutAction();
            }

            ImGui::Render();

            renderer.doRender(map, player.camera()); // imgui layout too

            // Process ALL input FIRST (keyboard + mouse) before updating position.
            // This ensures velocity changes are always detected, preventing infinite loops
            // when the player hits a wall and needs to release movement keys.

            // Process keyboard events first
            {
                SDL_Event event;
                while (SDL_PollEvent(&event)) {
                    ImGui_ImplSDL3_ProcessEvent(&event);

                    // Always process keyboard events for Escape key, regardless of ImGui capture state
                    // This ensures we can always toggle the UI overlay even when in relative mouse mode
                    if (event.type == SDL_EVENT_KEY_UP && event.key.scancode == SDL_SCANCODE_ESCAPE) {
                        std::unique_ptr<Turn> turn = processInput(event, renderer, player, isDone);
                        if (turn) {
                            spdlog::debug("generated turn: {}", turn->asMessage().dump());
                            networkManager.sendMessage(turn->asMessage());
                            break;
                        }
                    } else if (!(io.WantCaptureMouse || io.WantCaptureKeyboard)) {
                        std::unique_ptr<Turn> turn = processInput(event, renderer, player, isDone);
                        if (turn) {
                            spdlog::debug("generated turn: {}", turn->asMessage().dump());
                            networkManager.sendMessage(turn->asMessage());
                            break;
                        }
                    }
                }
            }

            // Process mouse input separately from SDL_PollEvent to reduce overhead
            // When overlay is hidden, always allow mouse to control camera
            // When overlay is visible, only allow if ImGui doesn't want mouse
            if (!renderer.renderUI() || !io.WantCaptureMouse) {
                std::unique_ptr<Turn> turn = processMouseInput(player);
                if (turn) {
                    spdlog::debug("generated turn: {}", turn->asMessage().dump());
                    networkManager.sendMessage(turn->asMessage());
                }
            }

            // NOW update position and generate turn (after input is processed)
            gameTime.update();

            std::unique_ptr<Turn> turn;
            turn = player.updatePosition(gameTime, map);
            if (turn) {
                spdlog::debug("generated turn: {}", turn->asMessage().dump());
                networkManager.sendMessage(turn->asMessage());

                if (auto* moveTurn = dynamic_cast<MoveTurn*>(turn.get())) {
                    map.shift(moveTurn->getDirection());
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
