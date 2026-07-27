#include "AudioManager.hpp"
#include "DescriptionManager.hpp"
#include "InputModeTracker.hpp"
#include "MessageLog.hpp"
#include "MessageQueue.hpp"
#include "PlayerState.hpp"
#include "Renderer.hpp"
#include "SpriteManager.hpp"
#include "Turn.hpp"
#include "UIManager.hpp"
#include "WindowManager.hpp"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlgpu3.h"
#include "imgui_internal.h"
#include "imguilayouts.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_scancode.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <signal.h>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <string>
#include <unordered_map>
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

// Map a weapon name (from inventory) to a sprite animation clip name.
// Returns "weapon_default_idle" for unknown weapons or when unarmed.
static std::string weaponToClipName(const PlayerData& data, bool swing)
{
    if (data.weapon_index == -1) {
        // Unarmed
        return swing ? "weapon_default_swing" : "weapon_default_idle";
    }

    auto it = data.inv.find(data.weapon_index);
    if (it == data.inv.end()) {
        return swing ? "weapon_default_swing" : "weapon_default_idle";
    }

    const std::string& name = it->second.name;

    // Map known weapon names to animation prefixes.
    // Expand this table as sprite assets are created.
    static const std::unordered_map<std::string, std::string> prefixMap = {
        {"dagger", "weapon_default"},
        {"short sword", "weapon_default"},
        {"long sword", "weapon_default"},
        {"hand axe", "weapon_default"},
        {"mace", "weapon_default"},
        {"club", "weapon_default"},
        {"spear", "weapon_default"},
    };

    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    for (const auto& [key, prefix] : prefixMap) {
        if (lower.find(key) != std::string::npos) {
            return prefix + (swing ? "_swing" : "_idle");
        }
    }

    return swing ? "weapon_default_swing" : "weapon_default_idle";
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
        SpriteManager spriteManager(renderer.gpu_device(), renderer.window());

        // Load sprite atlas (texture is loaded asynchronously by loadAtlas).
        spriteManager.loadAtlas(
            std::format("{}resources/weapons_atlas.json", SDL_GetBasePath()));

        DescriptionManager descriptionManager;
        UIManager uiManager;

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
            { "menu", { uiManager } },
            { "close_menu", { uiManager } },
            { "close_all_menus", { uiManager } },
            { "update_menu", { uiManager } },
            { "update_menu_items", { uiManager } },
            { "menu_scroll", { uiManager } },
            { "title_prompt", { uiManager } },
            { "ui-push", { descriptionManager, uiManager } },
            { "ui-pop", { descriptionManager, uiManager } },
            { "ui-state", { uiManager } },
            { "ui-stack", { uiManager } },
            { "ui-scroller-scroll", { uiManager } },
            { "ui_cutoff", { uiManager } },
            { "input_mode", { inputModeTracker } }
        };

        // Set up window layout callback for save functionality
        setWindowLayoutCallback(getWindowLayoutCallback);

        bool isDone = false;

        // --- Sprite/weapon state ---
        SpriteHandle weaponHandle = INVALID_SPRITE;
        std::string currentWeaponName; // tracks weapon_index for change detection
        bool weaponSwinging = false;

        // --- Effect tracking ---
        std::vector<SpriteHandle> activeEffects;
        int lastPlayerXl = 0;

        uint64_t lastSpriteTick = SDL_GetTicks();

        while (!isDone && !WindowManager::instance().isQuitConfirmed()) {
            std::vector<json> responses = networkManager.getNewMessages();
            if (!responses.empty()) {
                spdlog::debug("frame: got {} messages", responses.size());
                processMessages(responseHandlers, responses);
                spdlog::debug("frame: messages processed");
            }

            // Sync mouse mode BEFORE ImGui: use WindowManager's policy for
            // when to use relative (gameplay) vs absolute (menus/overlay/login).
            // When UIManager has entries on the stack, force absolute mode so
            // menus are clickable.  Also flush accumulated relative motion when
            // entering relative mode to prevent camera jump after closing menus.
            {
                SDL_Window* win = renderer.window();
                bool wantRelative = WindowManager::instance().shouldUseRelativeMouse()
                    && !uiManager.shouldBlockGameInput();
                if (win) {
                    SDL_SetWindowRelativeMouseMode(win, wantRelative);
                    if (!wantRelative) {
                        SDL_ShowCursor();
                    }
                }
                // Flush accumulated motion to avoid camera jump.
                if (wantRelative) {
                    SDL_GetRelativeMouseState(nullptr, nullptr);
                }
            }

            ImGui_ImplSDLGPU3_NewFrame();
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();

            // Display all ImGui windows — layout management, pin gating, and content
            displayAllWindows(player, map, networkManager, renderer, messageLog,
                              LAYOUT_FILENAME, &descriptionManager, &uiManager);

            // Clear ImGui capture state only during normal gameplay (no UI
            // open, no menus, no overlay mode).  This lets WASD/mouse pass
            // through to the game.  Keep capture active when UIManager has
            // entries or when WindowManager is in a non-gameplay mode.
            if (WindowManager::instance().getMode() == WindowManager::Mode::Normal
                && !uiManager.shouldBlockGameInput()
                && inputModeTracker.isGameplayMode()) {
                io.WantCaptureKeyboard = false;
                io.WantCaptureMouse = false;
            }

            ImGui::Render();
            spdlog::debug("frame: after ImGui::Render");

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

            renderer.doRender(map, player.camera(), { player.data().pos_x, player.data().pos_y },
                              &spriteManager);
            spdlog::debug("frame: after doRender");

            // Process ALL input FIRST (keyboard + mouse) before updating position.
            // This ensures velocity changes are always detected, preventing infinite loops
            // when the player hits a wall and needs to release movement keys.

            // Process keyboard events first — new server-driven architecture.
            // Priority order matches the JS client's layered key handling.
            {
                WindowManager& wm = WindowManager::instance();
                SDL_Event event;
                while (SDL_PollEvent(&event)) {
                    // ── 1. Client-side modals (quit confirm) ────────
                    if (wm.getMode() == WindowManager::Mode::QuitConfirm) {
                        if (event.type == SDL_EVENT_KEY_UP) {
                            if (event.key.scancode == SDL_SCANCODE_ESCAPE) {
                                wm.cancelQuitConfirm(renderer.window());
                            }
                            continue; // block all keyboard input
                        }
                        // Let mouse events through to ImGui for button clicks
                    }

                    // ── 2. ESC → send to server (always when logged in) ──
                    //      Matches JS client which sends on keydown.
                    if (event.type == SDL_EVENT_KEY_DOWN
                        && event.key.scancode == SDL_SCANCODE_ESCAPE
                        && wm.isLoggedIn()) {
                        json escMsg = { { "msg", "key" }, { "keycode", 27 } };
                        networkManager.sendMessage(escMsg);
                        continue;
                    }

                    // ── 3. Menu active + navigation key → handle locally ──
                    if (uiManager.isMenuActive()
                        && (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP)
                        && uiManager.handleMenuNavigationKey(
                            event.key.scancode,
                            event.key.mod & SDL_KMOD_SHIFT)) {
                        continue;
                    }

                    // ── 4. Mode-toggle keys (F1 overlay, E equipment) ──
                    if (event.type == SDL_EVENT_KEY_UP
                        && wm.handleKeyEvent(event.key.scancode, renderer.window())) {
                        continue;
                    }

                    // ── 5. Ctrl+Q quit ────────────────────────────
                    //      Client-side quit confirmation (doesn't conflict
                    //      with DCSS gameplay keys).  Shift+Q is forwarded
                    //      to the server as the quiver command.
                    if (event.key.scancode == SDL_SCANCODE_Q
                        && (event.key.mod & SDL_KMOD_CTRL)) {
                        if (event.type == SDL_EVENT_KEY_UP) {
                            wm.enterQuitConfirm(renderer.window());
                        }
                        continue;
                    }

                    // ── 6. Key forwarding to server ──────────────────
                    //      Forward keys when UI stack has entries or
                    //      server is in prompt/more/yesno mode.
                    bool inForwardingMode = uiManager.shouldForwardKeysToServer()
                                         || !inputModeTracker.isGameplayMode();
                    if (inForwardingMode
                        && event.type == SDL_EVENT_KEY_DOWN
                        && wm.isLoggedIn()) {
                        char c = scancodeToChar(event.key.scancode, event.key.mod);
                        if (c != '\0') {
                            if (c == 27)      // ESC handled by step 2
                                ;
                            else if (c == 13) // Enter
                                networkManager.sendMessage({{"msg","key"},{"keycode",13}});
                            else if (c == 9)  // Tab
                                networkManager.sendMessage({{"msg","key"},{"keycode",9}});
                            else
                                networkManager.sendMessage({{"msg","input"},{"text",std::string(1,c)}});
                            continue;
                        }
                    }

                    // ── 7. Game input (WASD, mouse) ──────────────────
                    //      Only when stack empty AND gameplay mode.
                    bool isGameplay = inputModeTracker.isGameplayMode();
                    bool canGameInput = !uiManager.shouldBlockGameInput()
                                     && isGameplay
                                     && wm.shouldProcessGameInput()
                                     && !(io.WantCaptureMouse || io.WantCaptureKeyboard);
                    // Track whether processInput consumed this event.
                    // Movement keys (WASD, Space, Enter, <, >) are handled
                    // even if they don't generate a turn, and must not
                    // fall through to the forwarding step.
                    bool gameInputHandled = false;
                    if (canGameInput) {
                        std::unique_ptr<Turn> turn = processInput(event, renderer, player, isDone);
                        if (turn) {
                            spdlog::debug("generated turn: {}", turn->asMessage().dump());
                            turn->playSound(audioManager);
                            networkManager.sendMessage(turn->asMessage());
                            break;
                        }
                        // Mark game keys as handled even when they only
                        // set velocity (WASD up/down, shift+, shift+.).
                        if (event.type == SDL_EVENT_KEY_DOWN
                            || event.type == SDL_EVENT_KEY_UP) {
                            switch (event.key.scancode) {
                            case SDL_SCANCODE_W: case SDL_SCANCODE_A:
                            case SDL_SCANCODE_S: case SDL_SCANCODE_D:
                            case SDL_SCANCODE_SPACE:
                            case SDL_SCANCODE_RETURN:
                            case SDL_SCANCODE_PERIOD:
                            case SDL_SCANCODE_COMMA:
                            case SDL_SCANCODE_KP_GREATER:
                            case SDL_SCANCODE_KP_LESS:
                                gameInputHandled = true;
                                break;
                            default: break;
                            }
                        }
                    }

                    // ── 7.5 Fallback key forwarding ─────────────────
                    //      Forward unhandled keys (q, i, d, r, p, etc.)
                    //      to the server.  Skips keys consumed by step 7.
                    if (!gameInputHandled
                        && wm.isLoggedIn()
                        && wm.getMode() == WindowManager::Mode::Normal
                        && event.type == SDL_EVENT_KEY_DOWN) {
                        char c = scancodeToChar(event.key.scancode, event.key.mod);
                        if (c != '\0') {
                            if (c == 27 || c == 13 || c == ' ') {
                                // already handled by steps above
                            } else {
                                networkManager.sendMessage(
                                    {{"msg","input"},{"text",std::string(1,c)}});
                                spdlog::debug("forwarded key '{}' to server", c);
                            }
                            continue;
                        }
                    }

                    // ── 8. ImGui processing (always last) ────────────
                    //      Always process events when UIManager has UI
                    //      open (menus need clicks/drags), or when the
                    //      overlay/equipment mode is active.
                    if (wm.shouldRenderUI() || uiManager.shouldBlockGameInput()) {
                        ImGui_ImplSDL3_ProcessEvent(&event);
                    }
                }
            }

            // Process mouse input separately from SDL_PollEvent to reduce overhead.
            // Mouse only controls camera when the UI stack is empty and we're in
            // gameplay mode.  When menus or overlays are showing, mouse is for UI.
            if (!uiManager.shouldBlockGameInput()
                && WindowManager::instance().shouldProcessGameInput()
                && inputModeTracker.isGameplayMode()) {
                std::unique_ptr<Turn> turn = processMouseInput(player);
                if (turn) {
                    spdlog::debug("generated turn: {}", turn->asMessage().dump());
                    turn->playSound(audioManager);
                    networkManager.sendMessage(turn->asMessage());

                    // Trigger weapon swing animation
                    if (dynamic_cast<AttackTurn*>(turn.get())
                        && weaponHandle != INVALID_SPRITE
                        && !weaponSwinging) {
                        std::string swingClip = weaponToClipName(
                            player.data(), true);
                        spriteManager.setAnimation(weaponHandle, swingClip);
                        weaponSwinging = true;
                        spdlog::debug("Swing animation '{}' triggered",
                            swingClip);
                    }
                }
            }

            spdlog::debug("frame: after mouse input");

            // NOW update position and generate turn (after input is processed).
            // Only advance when no UI is blocking and the server expects gameplay.
            if (!uiManager.shouldBlockGameInput()
                && WindowManager::instance().shouldProcessGameInput()
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

            // --- Weapon sprite: create when logged in, destroy on logout ---
            if (WindowManager::instance().isLoggedIn()
                && weaponHandle == INVALID_SPRITE) {
                SpriteTransform t;
                t.posX = 0.0f;
                t.posY = -0.5f;
                t.scaleX = 0.4f;
                t.scaleY = 0.4f;
                std::string clipName = weaponToClipName(player.data(), false);
                weaponHandle = spriteManager.play(
                    clipName, SpriteSpace::Screen, t);
                currentWeaponName = clipName;
                spdlog::info("Weapon sprite '{}' created, handle={}",
                    clipName, weaponHandle);
            }
            if (!WindowManager::instance().isLoggedIn()
                && weaponHandle != INVALID_SPRITE) {
                spriteManager.stop(weaponHandle);
                weaponHandle = INVALID_SPRITE;
                weaponSwinging = false;
                spdlog::info("Weapon sprite destroyed");
            }

            // --- Detect weapon changes ---
            if (weaponHandle != INVALID_SPRITE && !weaponSwinging) {
                std::string idleClip = weaponToClipName(player.data(), false);
                if (idleClip != currentWeaponName) {
                    spriteManager.setAnimation(weaponHandle, idleClip);
                    currentWeaponName = idleClip;
                    spdlog::info("Weapon changed to '{}'", idleClip);
                }
            }

            // --- After a swing completes, return to idle ---
            if (weaponSwinging && weaponHandle != INVALID_SPRITE
                && spriteManager.isComplete(weaponHandle)) {
                std::string idleClip = weaponToClipName(player.data(), false);
                spriteManager.setAnimation(weaponHandle, idleClip);
                currentWeaponName = idleClip;
                weaponSwinging = false;
                spdlog::debug("Swing complete, back to '{}'", idleClip);
            }

            // --- Clean up completed effects ---
            for (auto it = activeEffects.begin(); it != activeEffects.end();) {
                if (spriteManager.isComplete(*it)) {
                    spriteManager.stop(*it);
                    it = activeEffects.erase(it);
                } else {
                    ++it;
                }
            }

            // --- Detect level-up and play effect ---
            if (player.data().xl > lastPlayerXl && lastPlayerXl > 0) {
                SpriteTransform et;
                et.posX = 0.0f;
                et.posY = 0.0f;
                et.scaleX = 0.6f;
                et.scaleY = 0.6f;
                SpriteHandle effectHandle = spriteManager.play(
                    "level_up", SpriteSpace::Screen, et);
                if (effectHandle != INVALID_SPRITE) {
                    activeEffects.push_back(effectHandle);
                    spdlog::info("Level-up effect triggered (XL {} -> {})",
                        lastPlayerXl, player.data().xl);
                }
            }
            lastPlayerXl = player.data().xl;

            spdlog::debug("frame: after position update");

            // --- Sprite system update (use own frame timer, not gameTime) ---
            {
                uint64_t now = SDL_GetTicks();
                float spriteDt = (now - lastSpriteTick) / 1000.0f;
                lastSpriteTick = now;
                if (spriteDt > 0.1f) spriteDt = 0.1f;
                spriteManager.update(spriteDt);
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
