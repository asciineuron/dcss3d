#include "AudioManager.hpp"
#include "InputModeTracker.hpp"
#include "MessageLog.hpp"
#include "MessageQueue.hpp"
#include "PlayerState.hpp"
#include "Renderer.hpp"
#include "Turn.hpp"
#include "WindowManager.hpp"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlgpu3.h"
#include "imgui_internal.h"
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

// Layout filename (relative to working directory)
constexpr const char* LAYOUT_FILENAME = "window_layout.json";

// Convert an SDL scancode + modifiers to a character.
// Returns '\0' if the scancode doesn't produce a printable character.
// Assumes US keyboard layout (standard for crawl).
// Preserves case: Shift+A → 'A', A → 'a'.
static char scancodeToChar(SDL_Scancode sc, SDL_Keymod mod)
{
    bool shift = (mod & SDL_KMOD_SHIFT);
    switch (sc) {
    case SDL_SCANCODE_A:
        return shift ? 'A' : 'a';
    case SDL_SCANCODE_B:
        return shift ? 'B' : 'b';
    case SDL_SCANCODE_C:
        return shift ? 'C' : 'c';
    case SDL_SCANCODE_D:
        return shift ? 'D' : 'd';
    case SDL_SCANCODE_E:
        return shift ? 'E' : 'e';
    case SDL_SCANCODE_F:
        return shift ? 'F' : 'f';
    case SDL_SCANCODE_G:
        return shift ? 'G' : 'g';
    case SDL_SCANCODE_H:
        return shift ? 'H' : 'h';
    case SDL_SCANCODE_I:
        return shift ? 'I' : 'i';
    case SDL_SCANCODE_J:
        return shift ? 'J' : 'j';
    case SDL_SCANCODE_K:
        return shift ? 'K' : 'k';
    case SDL_SCANCODE_L:
        return shift ? 'L' : 'l';
    case SDL_SCANCODE_M:
        return shift ? 'M' : 'm';
    case SDL_SCANCODE_N:
        return shift ? 'N' : 'n';
    case SDL_SCANCODE_O:
        return shift ? 'O' : 'o';
    case SDL_SCANCODE_P:
        return shift ? 'P' : 'p';
    case SDL_SCANCODE_Q:
        return shift ? 'Q' : 'q';
    case SDL_SCANCODE_R:
        return shift ? 'R' : 'r';
    case SDL_SCANCODE_S:
        return shift ? 'S' : 's';
    case SDL_SCANCODE_T:
        return shift ? 'T' : 't';
    case SDL_SCANCODE_U:
        return shift ? 'U' : 'u';
    case SDL_SCANCODE_V:
        return shift ? 'V' : 'v';
    case SDL_SCANCODE_W:
        return shift ? 'W' : 'w';
    case SDL_SCANCODE_X:
        return shift ? 'X' : 'x';
    case SDL_SCANCODE_Y:
        return shift ? 'Y' : 'y';
    case SDL_SCANCODE_Z:
        return shift ? 'Z' : 'z';
    case SDL_SCANCODE_1:
        return shift ? '!' : '1';
    case SDL_SCANCODE_2:
        return shift ? '@' : '2';
    case SDL_SCANCODE_3:
        return shift ? '#' : '3';
    case SDL_SCANCODE_4:
        return shift ? '$' : '4';
    case SDL_SCANCODE_5:
        return shift ? '%' : '5';
    case SDL_SCANCODE_6:
        return shift ? '^' : '6';
    case SDL_SCANCODE_7:
        return shift ? '&' : '7';
    case SDL_SCANCODE_8:
        return shift ? '*' : '8';
    case SDL_SCANCODE_9:
        return shift ? '(' : '9';
    case SDL_SCANCODE_0:
        return shift ? ')' : '0';
    case SDL_SCANCODE_SPACE:
        return ' ';
    case SDL_SCANCODE_PERIOD:
        return shift ? '>' : '.';
    case SDL_SCANCODE_COMMA:
        return shift ? '<' : ',';
    case SDL_SCANCODE_SLASH:
        return shift ? '?' : '/';
    case SDL_SCANCODE_MINUS:
        return shift ? '_' : '-';
    case SDL_SCANCODE_EQUALS:
        return shift ? '+' : '=';
    case SDL_SCANCODE_SEMICOLON:
        return shift ? ':' : ';';
    case SDL_SCANCODE_APOSTROPHE:
        return shift ? '"' : '\'';
    case SDL_SCANCODE_GRAVE:
        return shift ? '~' : '`';
    case SDL_SCANCODE_LEFTBRACKET:
        return shift ? '{' : '[';
    case SDL_SCANCODE_RIGHTBRACKET:
        return shift ? '}' : ']';
    case SDL_SCANCODE_BACKSLASH:
        return shift ? '|' : '\\';
    case SDL_SCANCODE_ESCAPE:
        return 27; // Escape keycode
    case SDL_SCANCODE_RETURN:
        return 13; // Enter keycode
    case SDL_SCANCODE_TAB:
        return 9; // Tab
    default:
        return '\0';
    }
}

std::unique_ptr<Turn> process_key(SDL_KeyboardEvent key, Player& player, Renderer& renderer, bool& isDone)
{
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
        // Handled in event loop (always-active) — unreachable here
        break;
    case SDL_SCANCODE_ESCAPE:
        // Escape handled directly in event loop via WindowManager
        break;
    case SDL_SCANCODE_RETURN:
        return std::make_unique<InputTurn>(SDL_SCANCODE_RETURN);
        break;
    // Descend stairs: Shift+. (>) or keypad >
    case SDL_SCANCODE_PERIOD:
        if ((key.mod & SDL_KMOD_SHIFT) && key.type == SDL_EVENT_KEY_DOWN)
            return std::make_unique<TextTurn>(">");
        break;
    // Ascend stairs: Shift+, (<) or keypad <
    case SDL_SCANCODE_COMMA:
        if ((key.mod & SDL_KMOD_SHIFT) && key.type == SDL_EVENT_KEY_DOWN)
            return std::make_unique<TextTurn>("<");
        break;
    case SDL_SCANCODE_KP_GREATER:
        if (key.type == SDL_EVENT_KEY_DOWN)
            return std::make_unique<TextTurn>(">");
        break;
    case SDL_SCANCODE_KP_LESS:
        if (key.type == SDL_EVENT_KEY_DOWN)
            return std::make_unique<TextTurn>("<");
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

    // Debounce: only fire on rising edge (button just pressed)
    static bool s_lastLeftPressed = false;
    bool leftPressed = (mouseState & SDL_BUTTON_LMASK) != 0;

    if (leftPressed && !s_lastLeftPressed) {
        Direction dir = player.getFacingDirection();
        spdlog::debug("left mouse clicked, attacking direction: {}", directionToString[dir]);
        s_lastLeftPressed = true;
        return std::make_unique<AttackTurn>(dir);
    }

    s_lastLeftPressed = leftPressed;
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

int main(int argc, char* argv[])
{
    spdlog::set_level(spdlog::level::debug);

    pid_t relayPID = runRelayServer();

    bool didFail = false;
    try {
        Player player;
        GameTime gameTime;
        GameMap map;
        InputModeTracker inputModeTracker;
        MessageLog messageLog;
        Renderer renderer; // imgui + SDL setup too
        NetworkManager networkManager(std::format("{}dcss3d.sock", SDL_GetBasePath()));
        AudioManager audioManager;
        player.setAudioManager(&audioManager);
        ImGuiIO& io = ImGui::GetIO();
        // could add e.g. logger

        handlerConfig responseHandlers = {
            { "map", { player, gameTime, map, audioManager, WindowManager::instance() } },
            { "player", { player } },
            { "msgs", { messageLog } },
            { "game_ended", { audioManager } },
            { "game_started", { WindowManager::instance() } },
            { "login_success", { WindowManager::instance() } },
            { "ui-push", { WindowManager::instance() } },
            { "input_mode", { inputModeTracker } }
        };

        // Set up window layout callback for save functionality
        setWindowLayoutCallback(getWindowLayoutCallback);

        bool isDone = false;
        while (!isDone && !WindowManager::instance().isQuitConfirmed()) {
            std::vector<json> responses = networkManager.getNewMessages();
            if (!responses.empty()) {
                processMessages(responseHandlers, responses);
            }

            ImGui_ImplSDLGPU3_NewFrame();
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();

            // Display all ImGui windows — layout management, pin gating, and content
            displayAllWindows(player, map, networkManager, renderer, messageLog, LAYOUT_FILENAME);

            // In Normal mode, pinned windows are read-only overlays.
            // Clear any ImGui capture state so game input is not blocked —
            // but only during gameplay mode.  During prompts (stat gain etc.)
            // we keep ImGui capture so the user can scroll the log window.
            if (WindowManager::instance().getMode() == WindowManager::Mode::Normal
                && inputModeTracker.isGameplayMode()) {
                io.WantCaptureKeyboard = false;
                io.WantCaptureMouse = false;
            }

            ImGui::Render();

            // Compute target cell highlight position before rendering
            {
                Pos2<int> targetCell = player.getTargetCell();
                auto tileOpt = map.getTileAt(targetCell.x, targetCell.y);
                if (tileOpt.has_value()) {
                    glm::vec2 renderCoords = mapCoordToRender(targetCell);
                    // renderCoords: x=worldX, y=worldZ; Y(height) is 0 for cube base
                    renderer.setTargetHighlight(glm::vec4(renderCoords.x, 0.0f, renderCoords.y, 1.0f));
                } else {
                    renderer.setTargetHighlight(glm::vec4(0.0f, 0.0f, 0.0f, 0.0f)); // disabled
                }
            }

            renderer.doRender(map, player.camera(), { player.data().pos_x, player.data().pos_y });

            // Process ALL input FIRST (keyboard + mouse) before updating position.
            // This ensures velocity changes are always detected, preventing infinite loops
            // when the player hits a wall and needs to release movement keys.

            // Process keyboard events first
            {
                SDL_Event event;
                while (SDL_PollEvent(&event)) {
                    // When UI is not active (Normal mode), skip ALL events to ImGui.
                    // Pinned windows are read-only overlays — no hover, no clicks, no keyboard.
                    if (WindowManager::instance().shouldRenderUI()) {
                        ImGui_ImplSDL3_ProcessEvent(&event);
                    }

                    // Mode-toggle keys (Escape, E) — always handled, even when ImGui captures input
                    if (event.type == SDL_EVENT_KEY_UP
                        && WindowManager::instance().handleKeyEvent(event.key.scancode, renderer.window())) {
                        continue;
                    }

                    // Quit key (Q) — always handled regardless of mode
                    if (event.type == SDL_EVENT_KEY_UP && event.key.scancode == SDL_SCANCODE_Q) {
                        WindowManager::instance().enterQuitConfirm(renderer.window());
                        continue;
                    }

                    // Game input processing: only when WindowManager allows it,
                    // InputModeTracker says we're in gameplay mode, and ImGui
                    // is not capturing input.
                    bool isGameplay = inputModeTracker.isGameplayMode();
                    if (WindowManager::instance().shouldProcessGameInput()
                        && isGameplay
                        && !(io.WantCaptureMouse || io.WantCaptureKeyboard)) {
                        std::unique_ptr<Turn> turn = processInput(event, renderer, player, isDone);
                        if (turn) {
                            spdlog::debug("generated turn: {}", turn->asMessage().dump());
                            turn->playSound(audioManager);
                            networkManager.sendMessage(turn->asMessage());
                            break;
                        }
                    }

                    // Prompt mode (stat gain, --more--, yes/no): forward keyboard
                    // input as text.  Matches JS client's handle_keypress during
                    // non-gameplay input modes.
                    if (WindowManager::instance().shouldProcessGameInput()
                        && !isGameplay
                        && !(io.WantCaptureMouse || io.WantCaptureKeyboard)
                        && (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP)) {
                        SDL_KeyboardEvent& key = event.key;
                        if (key.type == SDL_EVENT_KEY_DOWN) {
                            char c = scancodeToChar(key.scancode, key.mod);
                            if (c != '\0') {
                                std::unique_ptr<Turn> turn = std::make_unique<TextTurn>(std::string(1, c));
                                spdlog::debug("prompt mode input: '{}' (0x{:02x})", c, (unsigned char)c);
                                networkManager.sendMessage(turn->asMessage());
                                break;
                            }
                        }
                    }
                }
            }

            // Process mouse input separately from SDL_PollEvent to reduce overhead.
            // When UI is showing (Overlay/Equipment mode), mouse never controls camera.
            // When UI is hidden (Normal mode, even with pinned overlay windows visible),
            // mouse always controls camera — pinned windows are read-only.
            if (WindowManager::instance().shouldProcessGameInput()
                && inputModeTracker.isGameplayMode()) {
                std::unique_ptr<Turn> turn = processMouseInput(player);
                if (turn) {
                    spdlog::debug("generated turn: {}", turn->asMessage().dump());
                    turn->playSound(audioManager);
                    networkManager.sendMessage(turn->asMessage());
                }
            }

            // NOW update position and generate turn (after input is processed)
            // Only process game state updates in Normal mode, and only when
            // the server is in gameplay input mode.
            if (WindowManager::instance().shouldProcessGameInput()
                && inputModeTracker.isGameplayMode()) {
                gameTime.update();

                std::unique_ptr<Turn> turn;
                turn = player.updatePosition(gameTime, map);
                if (turn) {
                    if (auto* moveTurn = dynamic_cast<MoveTurn*>(turn.get())) {
                        spdlog::debug("sending movement: dir={} msg={}",
                            directionToString[moveTurn->getDirection()],
                            turn->asMessage().dump());
                    } else {
                        spdlog::debug("sending turn: {}", turn->asMessage().dump());
                    }
                    turn->playSound(audioManager);
                    networkManager.sendMessage(turn->asMessage());
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
