# UI Response Pipeline — Server-Driven Architecture (Revised)

## 1. Problem

Our client currently intercepts keypresses (e.g. `q` for quaff) and manages UI
state (modes, popups) on the client side.  The JS webtiles client does the
opposite: **all UI state is driven by the server**.  The client sends raw
keypresses and reacts to server messages that tell it what to show/hide.

This architectural drift causes recurring bugs:
- ESC has too many conflicting behaviors (dismiss description, cancel quaff,
  toggle overlay)
- WindowManager's flat `Mode` enum can't represent stacked windows (description
  on top of quaff menu)
- We have special-case handlers scattered across main.cpp, WindowManager, and
  imguilayouts
- Features like item description require hacky workarounds because we bypass
  the server's menu system
- The client-side quaff menu can get out of sync with server state

## 2. How the JS Client Works (Complete Reference)

### 2.1 Two Separate UI Message Systems

The server has two distinct message families for UI:

| Message | Purpose | Handled by |
|---------|---------|------------|
| `menu`, `close_menu`, `update_menu`, `update_menu_items`, `close_all_menus`, `menu_scroll`, `title_prompt` | Interactive selection menus (quaff, drop, inventory, spell list, actions) | `menu.js` |
| `ui-push`, `ui-pop`, `ui-state`, `ui-stack`, `ui-scroller-scroll` | Informational overlays (describe-item, newgame-choice, progress bars, --more--, formatted scrollers) | `ui-layouts.js` |

Both share a common UI stack (`#ui-stack` DOM element) managed by `ui.js`.

### 2.2 Menu Messages (`menu.js`)

When the player presses `q`, the client sends `{"msg":"input","text":"q"}` to
the server.  The server enters an inventory-prompt mode and sends back:

```json
{
  "msg": "menu",
  "tag": "use_item",
  "flags": 5,
  "title": "Quaff which item?",
  "items": [
    {"hotkeys": [97], "text": "a - a potion of healing"},
    {"hotkeys": [99], "text": "c - a potion of might"}
  ],
  "more": "--press Space or Enter to continue--",
  "total_items": 2,
  "chunk_start": 0
}
```

The client's `open_menu()` renders this as an interactive HTML list. When the
player presses a letter (e.g. `a`), the client sends `{"msg":"input","text":"a"}`
to the server. The server processes the selection and sends `close_menu`.

**Menu tags** seen in the crawl source:
- `"use_item"` — quaff, read, evoke, etc. (created by `UseItemMenu`)
- `"inventory"` — drop, wield, wear, etc. (created by `InvMenu`)
- `"actions"` — ability/action selection
- `"macros"` — macro mapping
- `"macro_mapping"` — key rebinding
- `"ability"` — ability selection
- `"spell"` — spell selection
- `"travel"` — travel/waypoint selection

**Complete menu message family:**

| Message | When sent | What it does |
|---------|-----------|--------------|
| `menu` | Server enters a menu mode | Open a new top-level menu (pushes onto stack) |
| `close_menu` | Player exits menu | Pop the topmost menu from stack |
| `close_all_menus` | Level change, game reset | Clear the entire menu stack |
| `update_menu` | Menu metadata changes (e.g. title, total_items) | Update title, flags, total count |
| `update_menu_items` | Large menus scroll — server sends item chunks | Replace items in `[chunk_start, chunk_start+len)` |
| `menu_scroll` | Spectator scroll sync | Set scroll position from server |
| `title_prompt` | Menu enters search/filter mode | Show text input in menu title bar |

**Menu flags** (from `menu.h`, exposed in `enums.js` as `menu_flag`):

| Flag | Value | Meaning |
|------|-------|---------|
| `NOSELECT` | 0x0001 | Items are not selectable |
| `SINGLESELECT` | 0x0002 | Single item selection (e.g. inventory) |
| `MULTISELECT` | 0x0004 | Multi-select (e.g. drop many) |
| `ANYPRINTABLE` | 0x0010 | Any printable key selects |
| `SELECT_BY_PAGE` | 0x0020 | Page-up/down select |
| `WRAP` | 0x0080 | Wrap cursor around |
| `ALLOW_FILTER` | 0x0100 | Allow regex filter (? key) |
| `START_AT_END` | 0x1000 | Scroll to bottom initially |
| `PRESELECTED` | 0x2000 | Item is pre-selected |
| `ARROWS_SELECT` | 0x40000 | Arrow keys move selection cursor |

### 2.3 UI Push/Pop Messages (`ui-layouts.js`)

Overlays that aren't interactive selection menus use `ui-push`/`ui-pop`:

```json
{
  "msg": "ui-push",
  "type": "describe-item",
  "title": "d - a potion of might.",
  "body": "<lightgrey>\n\nA potion which greatly increases...SPELLSET_PLACEHOLDER",
  "spellset": [],
  "ui-centred": false,
  "generation_id": 4
}
```

When the player presses ESC while a UI overlay is showing, the client sends
`{"msg":"key","keycode":27}` to the server. The server pops the overlay and
sends back `{"msg":"ui-pop"}`. The client calls `hide_popup()` which removes
the topmost popup from `#ui-stack`.

**Known `ui-push` types** and their handler functions:

| type | Handler | Purpose |
|------|---------|---------|
| `"describe-item"` | `describe_item()` | Item description with spellset |
| `"describe-generic"` | `describe_generic()` | Generic monster/feature description |
| `"describe-monster"` | `describe_monster()` | Monster description with panes |
| `"describe-spell"` | `describe_spell()` | Spell description |
| `"describe-god"` | `describe_god()` | God description with panes |
| `"describe-cards"` | `describe_cards()` | Nemelex card description |
| `"describe-feature-wide"` | `describe_feature_wide()` | Multi-feature description |
| `"newgame-choice"` | `newgame_choice()` | Species/background/weapon selection grid |
| `"newgame-random-combo"` | `newgame_random_combo()` | Random character confirmation |
| `"progress-bar"` | `progress_bar()` | Loading/saving progress bar |
| `"formatted-scroller"` | `formatted_scroller()` | Scrolling text (help, ? screens, etc.) |
| `"game-over"` | `game_over()` | Death/victory screen |
| `"version"` | `version()` | Version info display |
| `"seed-selection"` | `seed_selection()` | Game seed input |
| `"msgwin-get-line"` | `msgwin_get_line()` | Single line text prompt |

**`ui-state` message**: Updates an existing overlay's state (progress bar
percentage, god description pane switch, formatted scroller scroll position).

**`ui-scroller-scroll`**: Server-initiated scroll sync for overlays.

### 2.4 The UI Stack (`ui.js`)

```
┌─────────────────────┐
│  #ui-stack (DOM)    │
│  ┌─────────────────┐│  ← topmost (active, visible)
│  │ describe-item   ││
│  ├─────────────────┤│
│  │ use_item menu   ││  ← hidden while describe-item is on top
│  ├─────────────────┤│
│  │ newgame-choice  ││
│  └─────────────────┘│  ← bottom (inactive, hidden)
└─────────────────────┘
```

- `show_popup(elem, centred, generation_id)`: Wraps element in a `.ui-popup`
  div, appends to `#ui-stack`, fades in. The new popup goes on TOP.
- `hide_popup(show_below)`: Removes the topmost wrapper. If `show_below` is
  true, fades in the previously underlying popup.
- `top_popup()`: Returns the topmost (last) child of `#ui-stack`.
- `hide_all_popups()`: Removes every popup from the stack.
- Generation IDs track which version of a popup is current (prevents stale
  `ui-state` updates from applying to the wrong popup).

### 2.5 Input Mode

The server sends `{"msg":"input_mode","mode":<n>}` to indicate what kind of
input it expects:

| Mode | Constant | Meaning |
|------|----------|---------|
| 0 | NORMAL | Regular gameplay |
| 1 | COMMAND | Targeting / command mode |
| 2 | TARGET | Target selection |
| 3 | TARGET_DIR | Directional target |
| 4 | TARGET_PATH | Path target |
| 5 | MORE | --more-- prompt |
| 6 | MACRO | Macro recording |
| 7 | PROMPT | Text prompt (stat gain, scroll reading) |
| 8 | YESNO | Yes/no confirmation |

### 2.6 Complete Key Handling Flow (JS Client)

The JS client has a layered key handling architecture:

```
Browser key event (keydown/keypress)
    │
    ▼
1. client.js: handle_keydown / handle_keypress
    │
    ├── retrigger_event → "game_keydown" / "game_keypress" events
    │       │
    │       ├── menu.js: menu_keydown_handler     (arrows, pgup/dn, home/end)
    │       ├── menu.js: menu_keypress_handler    (space, -, +, <, >)
    │       ├── ui.js: ui_key_handler             (forwards to top popup)
    │       └── If any handler calls preventDefault(), stop here
    │
    ├── If watching: handle ESC/F12 locally, stop
    │
    ├── Ctrl/Shift/Alt combos: send {msg:"key", keycode:N}
    │
    └── Unmodified keys: send {msg:"key", keycode:N}
            OR (for keypress): send {msg:"input", text:"c"}
```

Key insight: **All non-navigation keys go to the server.** The client does NOT
decide which keys are "game input" vs "menu input." It sends everything and
the server decides what to do.

The only keys handled locally are:
- Arrow keys, PgUp, PgDn, Home, End (menu navigation)
- Tab (focus management in popups)
- Shift+arrow keys (scroll line)
- Space, -, +, <, > in menu context (page scrolling)
- F12 (chat toggle)
- ESC in spectator mode (return to lobby)

**ESC handling** deserves special attention:
1. `popup_keydown_handler` in ui.js: If something is focused, blur it (don't send to server)
2. If ESC not consumed by blur, `ui_hotkey_handler` runs
3. If no `data-hotkey` element matches, the key falls through to client.js
4. client.js sends `{"msg":"key","keycode":27}` to the server
5. The server processes ESC and responds with `ui-pop` or `close_menu`

### 2.7 Key Interaction Flow for Quaffing (JS Client)

```
1. Player presses 'q'
2. client.js handle_keypress: sends {"msg":"input","text":"q"} to server
3. Server enters quaff mode, sends {"msg":"menu","tag":"use_item",...}
4. menu.js open_menu(): pushes menu onto stack, renders potion list
5. Player presses 'a' (letter of a potion)
6. client.js handle_keypress: sends {"msg":"input","text":"a"} to server
7. Server processes quaff, sends {"msg":"close_menu"} + {"msg":"player",...}
8. menu.js close_menu(): pops the menu, quaff window closes
9. Player update shows decreased potion count
```

No client-side mode. Keys go to the server. The server drives everything.

### 2.8 Description Flow During Quaff (JS Client)

```
1. Player has quaff menu open
2. Player presses '?'  (ALLOW_FILTER flag in use_item menu)
3. menu_keypress_handler: '?' is not a handled character, falls through
4. client.js sends {"msg":"input","text":"?"} to server
5. Server receives '?' in use_item menu → enters filter/describe mode
6. Player presses 'd' (letter of a potion)
7. client.js sends {"msg":"input","text":"d"} to server
8. Server sends {"msg":"ui-push","type":"describe-item",...}
9. ui-layouts.js recv_ui_push(): pushes describe-item overlay ON TOP of the menu
10. Player reads description, presses ESC
11. client.js sends {"msg":"key","keycode":27} to server
12. Server sends {"msg":"ui-pop"}
13. ui-layouts.js recv_ui_pop(): pops description, reveals menu underneath
```

This is fundamentally different from our current approach. We send `q` +
`inv_item_describe` + ESC as a blast, bypassing the server's menu system
entirely. The new architecture must match the JS flow exactly.

## 3. Current State of Our Client

### 3.1 What We Do (Anti-Patterns)

1. **Key interception**: We intercept `q` in the event loop, show our own ImGui
   quaff menu, and only send `q` + letter to the server when the player selects
   a potion. The server never knows we showed a menu.

2. **Client-side mode management**: `WindowManager::Mode` is a flat enum
   (Login, Normal, Overlay, Equipment, QuitConfirm, QuaffMenu).
   Only ONE mode can be active. This can't represent stacked windows.

3. **ESC overloading**: ESC does different things depending on mode:
   - Description showing → dismiss description
   - QuaffMenu → cancel quaff
   - QuitConfirm → cancel quit
   - Equipment → return to Normal
   - Normal → sent to server

4. **Missing message handlers**: We don't handle `"menu"`, `"close_menu"`,
   `"update_menu"`, `"update_menu_items"`, `"close_all_menus"`,
   `"menu_scroll"`, or `"title_prompt"` messages at all.

5. **Hacky description flow**: `DescriptionManager::requestDescription()` sends
   `q` + `inv_item_describe` + ESC as a batch, bypassing the menu system.

### 3.2 What We Already Handle Correctly

- `"ui-push"` with `type: "newgame-choice"` → Character select window
- `"ui-state"` with `type: "describe-item"` → Description data capture
- `"input_mode"` → InputModeTracker
- `"ui-pop"` → DescriptionManager dismiss (partial)

## 4. Target Architecture

### 4.1 Design Principles

1. **Server is the single source of truth for UI state.** We never decide
   on our own to show/hide windows — we react to server messages.

2. **Keys go to the server.** We don't intercept keys to manage client-side
   menus. The only keys we handle locally are navigation keys (arrows, etc.)
   when a menu is active, and client-side modals like quit confirm.

3. **UI is a stack.** Multiple windows can be open simultaneously, with the
   topmost receiving input. ESC always pops the topmost (via server round-trip).

4. **Render from server data.** We render UI windows from the server messages
   themselves (menu items array, ui-push body text), not from cached client
   state like PlayerData::inv. The server is authoritative.

5. **Thin rendering layer.** Our ImGui windows just render data from the
   server message. No client-side logic about what to show.

### 4.2 New `UIManager` Class

Replaces the window-management responsibilities currently scattered across
`WindowManager`, `main.cpp`, `DescriptionManager`, and `imguilayouts`.

```cpp
class UIManager : public MessageHandler {
public:
    // Stack entry for one open UI element
    struct UIEntry {
        enum Type {
            Menu,          // interactive selection (quaff, drop, inventory)
            Overlay,       // informational (describe, progress bar, --more--)
        };

        Type type;
        std::string tag;           // "use_item", "describe-item", etc.
        json data;                 // the full server message (menu or ui-push)
    };

    // Push/pop from the stack
    void push(UIEntry entry);
    void pop();
    void clear();  // close_all_menus semantics

    // Query stack state
    const UIEntry* top() const;
    bool empty() const;
    bool hasOverlay(std::string_view type) const;
    bool hasMenu(std::string_view tag) const;

    // Whether game input (WASD, mouse) should be processed.
    // False when any UI is on the stack.
    bool shouldBlockGameInput() const;

    // Whether the event loop should forward all keypresses to the server.
    // True when a menu or overlay is the top of stack (server needs keys).
    bool shouldForwardKeysToServer() const;

    // Whether the top of stack is a menu (for local menu navigation).
    bool isMenuActive() const;

    // Navigation key handling for active menus (arrow keys, pgup/pgdn).
    // Returns true if the navigation key was consumed.
    bool handleMenuNavigationKey(int sdlKeycode, bool shift);

    // MessageHandler: processes all UI-related messages.
    void handleMessage(const json& message) override;

    // Render all active UI elements (called from displayAllWindows).
    void render(const Player& player, NetworkManager& net,
                SDL_Window* window);

private:
    std::vector<UIEntry> m_stack;

    // Menu scroll state (for update_menu_items chunk handling)
    int m_menuHoverIndex = -1;

    // Per-tag render functions dispatch table
    void renderMenu(const UIEntry& entry, const Player& player);
    void renderOverlay(const UIEntry& entry, NetworkManager& net);
};
```

### 4.3 WindowManager Simplification

`WindowManager` currently manages both game-level modes and UI popup modes.
After migration:

```cpp
enum class Mode {
    Login,      // pre-authentication
    Playing,    // normal gameplay (UIManager may have stacked UI on top)
};

// WindowManager retains only:
// - Connection state: isLoggedIn(), isGameConnected()
// - Overlay toggle: F1 to show/hide debug overlay
// - Equipment toggle: E to show/hide equipment window
// - Quit confirmation: client-side modal (not server-driven)
// - Mouse mode sync: shouldUseRelativeMouse(), syncMouseMode()
// - Login/game state transitions

// Removed from WindowManager:
// - Mode enum: Normal, Overlay, Equipment, QuaffMenu removed
// - enterQuaffMenu / cancelQuaffMenu
// - ESC handling (now in main event loop via UIManager)
// - Description interaction (now in UIManager)
```

### 4.4 DescriptionManager Simplification

`DescriptionManager` becomes a thin data holder. `UIManager` owns the UI stack
and handles all `ui-push`/`ui-pop` messages. `DescriptionManager` just stores
the parsed description text for the renderer to use:

```cpp
class DescriptionManager {
public:
    // Set directly from UIManager when ui-push describe-item arrives
    void setDescription(const std::string& itemName, const std::string& desc);
    bool hasDescription() const;
    const std::string& itemName() const;
    const std::string& description() const;
    void dismiss();
    
    // Static helper for stripping DCSS color tags
    static std::string stripColorTags(const std::string& raw);
private:
    std::string m_itemName;
    std::string m_description;
};
```

No more `requestDescription()` — the server-driven flow handles everything.
No more `MessageHandler` — `UIManager` handles the messages.

### 4.5 Event Loop Changes

The event loop in `main.cpp` restructures around the new priority:

```cpp
while (SDL_PollEvent(&event)) {
    // 1. Client-side modals (quit confirm) — always checked first
    if (quit confirm active) { handle quit keys; continue; }

    // 2. UI stack is non-empty AND key is ESC:
    //    Send ESC to server. Server responds with ui-pop or close_menu.
    if (event is keyup && scancode == ESC && !uiManager.empty()) {
        send {"msg":"key","keycode":27} to server;
        continue;
    }

    // 3. UI stack has an active MENU on top AND key is a navigation key:
    //    Handle locally (arrow keys, pgup/pgdn, home/end).
    //    This matches menu.js menu_keydown_handler + menu_keypress_handler.
    if (event is keydown/keypress && uiManager.isMenuActive()) {
        if (uiManager.handleMenuNavigationKey(scancode, shift)) continue;
        // Not a nav key: fall through to key forwarding
    }

    // 4. Mode-toggle keys (F1 overlay debug, E equipment)
    if (WindowManager::handleKeyEvent(...)) continue;

    // 5. Shift+Q quit
    if (Shift+Q) { enterQuitConfirm(); continue; }

    // 6. KEY FORWARDING TO SERVER
    //    Forward ALL keys to the server when:
    //    a) UI stack is non-empty (server is in menu/prompt mode), OR
    //    b) InputModeTracker says we're in prompt mode (MORE, PROMPT, YESNO)
    //    This matches the JS client: send everything, let the server decide.
    if ((uiManager.shouldForwardKeysToServer() || !inputModeTracker.isGameplayMode())
        && event.type == SDL_EVENT_KEY_DOWN) {
        char c = scancodeToChar(event.key.scancode, event.key.mod);
        if (c != '\0') {
            if (c == 27)  // ESC already handled above
                send {"msg":"key","keycode":27};
            else if (c == 13)  // Enter
                send {"msg":"key","keycode":13};
            else
                send {"msg":"input","text":string(1,c)};
            continue;
        }
    }

    // 7. Game input (WASD, mouse) — only when UI stack is empty AND gameplay mode
    if (uiManager.shouldBlockGameInput() == false
        && inputModeTracker.isGameplayMode()) {
        process game input...
    }

    // 8. ImGui processing (always last)
    if (WindowManager::shouldRenderUI()) {
        ImGui_ImplSDL3_ProcessEvent(&event);
    }
}
```

Key changes from current code:
- No `q` key interception (goes to server via step 6)
- No quaff letter handling (server's menu drives the UI)
- ESC is simply sent to the server when UI is open (step 2)
- `QuaffMenu` mode disappears entirely
- New key forwarding path (step 6) handles all non-gameplay keypresses

### 4.6 Quaff Flow (After Migration)

```
1. Player presses 'q'
2. Event loop step 6: UI stack is empty, in gameplay mode → game input path
3. q is sent as {"msg":"input","text":"q"} to server
4. Server enters quaff mode, sends {"msg":"menu","tag":"use_item","title":"Quaff which item?",...}
5. UIManager::handleMessage("menu"): pushes UIEntry{Menu, "use_item", data} onto stack
6. Next frame: UIManager::render() sees top is "use_item" menu, calls renderMenu()
7. renderMenu renders an ImGui popup from the menu's items array
8. Player presses 'a' → step 6 key forwarding: sends {"msg":"input","text":"a"}
9. Server processes quaff, sends {"msg":"close_menu"} + {"msg":"player",...}
10. UIManager pops the menu, quaff window closes
11. Player update shows decreased potion count
```

### 4.7 Description During Quaff Flow (After Migration)

```
1. Quaff menu is open (UIManager stack: [use_item menu])
2. Player presses '?'
3. Event loop step 6: UI stack non-empty → forward to server
4. Server receives '?' in menu filter mode, enters describe mode
5. Player presses 'd' (letter of a potion in the menu)
6. Event loop step 6: forward 'd' to server
7. Server sends {"msg":"ui-push","type":"describe-item",...}
8. UIManager::handleMessage("ui-push"): pushes UIEntry{Overlay, "describe-item", data}
9. Stack is now: [use_item menu, describe-item overlay]
10. Next frame: render() shows description window on top of quaff menu
11. Player presses ESC → step 2: sends {keycode:27} to server
12. Server sends {"msg":"ui-pop"}
13. UIManager::handleMessage("ui-pop"): pops describe-item overlay
14. Stack is now: [use_item menu], quaff menu visible again
```

## 5. Implementation Plan (Red/Green TDD)

### File Structure

UIManager is split into two translation units so the test executable can
compile the state management without linking imgui:

```
src/UIManager.hpp       — Class declaration, UIEntry struct (no imgui #includes)
src/UIManager.cpp       — Stack ops, handleMessage(), query methods, menu navigation
src/UIManagerRender.cpp — render() + per-tag ImGui render functions (imgui #includes)
```

`CMakeLists.txt` adds `UIManager.cpp` to both `dcss3d_obj` and `dcss3d_tests`,
but `UIManagerRender.cpp` only to `dcss3d_obj`.

---

### Phase 1: UIManager Core State Management (TDD)

**Goal**: UIManager.h/cpp exists and passes all state management tests.
At the end of this phase, `UIManager` is a fully-functional state machine
that can be instantiated, receive messages, manage a stack, and answer
queries — all without any imgui dependency.

#### 1.1 RED: Write `tests/test_ui_manager.cpp` — Stack Operations

```cpp
#include <catch2/catch_all.hpp>
#include "UIManager.hpp"

using json = nlohmann::json;

// ── Stack push/pop ──────────────────────────────────────────────

TEST_CASE("UIManager: empty on construction", "[UIManager]") {
    UIManager mgr;
    REQUIRE(mgr.empty());
    REQUIRE(mgr.top() == nullptr);
    REQUIRE_FALSE(mgr.shouldBlockGameInput());
    REQUIRE_FALSE(mgr.shouldForwardKeysToServer());
    REQUIRE_FALSE(mgr.isMenuActive());
}

TEST_CASE("UIManager: push menu, query state", "[UIManager]") {
    UIManager mgr;
    json menuMsg = {{"msg","menu"},{"tag","use_item"},{"title","Quaff which item?"}};
    mgr.handleMessage(menuMsg);
    REQUIRE_FALSE(mgr.empty());
    REQUIRE(mgr.top() != nullptr);
    REQUIRE(mgr.top()->type == UIManager::UIEntry::Menu);
    REQUIRE(mgr.top()->tag == "use_item");
    REQUIRE(mgr.isMenuActive());
    REQUIRE(mgr.shouldBlockGameInput());
    REQUIRE(mgr.shouldForwardKeysToServer());
}

TEST_CASE("UIManager: push then pop returns to empty", "[UIManager]") {
    UIManager mgr;
    mgr.handleMessage({{"msg","menu"},{"tag","use_item"}});
    mgr.handleMessage({{"msg","close_menu"}});
    REQUIRE(mgr.empty());
    REQUIRE_FALSE(mgr.shouldBlockGameInput());
}

TEST_CASE("UIManager: clear empties stack", "[UIManager]") {
    UIManager mgr;
    mgr.handleMessage({{"msg","menu"},{"tag","use_item"}});
    mgr.handleMessage({{"msg","ui-push"},{"type","describe-item"}});
    REQUIRE_FALSE(mgr.empty());
    mgr.handleMessage({{"msg","close_all_menus"}});
    REQUIRE(mgr.empty());
}

// ── Stacked menu + overlay ──────────────────────────────────────

TEST_CASE("UIManager: overlay stacks on top of menu", "[UIManager]") {
    UIManager mgr;
    mgr.handleMessage({{"msg","menu"},{"tag","use_item"}});
    mgr.handleMessage({{"msg","ui-push"},{"type","describe-item"}});
    // Top is the overlay, menu is underneath
    REQUIRE(mgr.top()->type == UIManager::UIEntry::Overlay);
    REQUIRE(mgr.top()->tag == "describe-item");
    REQUIRE(mgr.isMenuActive() == false);  // top is overlay, not menu
    REQUIRE(mgr.hasOverlay("describe-item"));
    // Pop overlay, menu reappears
    mgr.handleMessage({{"msg","ui-pop"}});
    REQUIRE(mgr.top()->type == UIManager::UIEntry::Menu);
    REQUIRE(mgr.isMenuActive());
}

// ── hasOverlay / hasMenu lookups ─────────────────────────────────

TEST_CASE("UIManager: hasOverlay finds type anywhere in stack", "[UIManager]") {
    UIManager mgr;
    mgr.handleMessage({{"msg","ui-push"},{"type","progress-bar"}});
    REQUIRE(mgr.hasOverlay("progress-bar"));
    REQUIRE_FALSE(mgr.hasOverlay("describe-item"));
}

TEST_CASE("UIManager: hasMenu finds tag anywhere in stack", "[UIManager]") {
    UIManager mgr;
    mgr.handleMessage({{"msg","menu"},{"tag","inventory"}});
    REQUIRE(mgr.hasMenu("inventory"));
    REQUIRE_FALSE(mgr.hasMenu("use_item"));
}
```

#### 1.2 GREEN: Implement `src/UIManager.hpp` + `src/UIManager.cpp`

Implement just enough to pass the stack operation tests:
- `UIEntry` struct with `Type` enum (`Menu`, `Overlay`), `tag` string, `data` json
- `std::vector<UIEntry> m_stack`
- `push()`, `pop()`, `clear()`, `top()`, `empty()`
- `hasOverlay()`, `hasMenu()`, `shouldBlockGameInput()`, `shouldForwardKeysToServer()`, `isMenuActive()`
- `handleMessage()` with dispatch for `"menu"`, `"close_menu"`, `"close_all_menus"`, `"ui-push"`, `"ui-pop"`

#### 1.3 RED: Write tests — Remaining Message Types

```cpp
// ── update_menu metadata merge ───────────────────────────────────

TEST_CASE("UIManager: update_menu merges title and flags", "[UIManager]") {
    UIManager mgr;
    mgr.handleMessage({{"msg","menu"},{"tag","use_item"},
        {"title","Quaff which item?"},{"flags",5},{"total_items",2}});
    mgr.handleMessage({{"msg","update_menu"},
        {"title","Quaff which potion?"},{"total_items",3}});
    // Title updated, flags preserved, total_items updated
    REQUIRE(mgr.top()->data["title"] == "Quaff which potion?");
    REQUIRE(mgr.top()->data["flags"] == 5);
    REQUIRE(mgr.top()->data["total_items"] == 3);
}

// ── update_menu_items chunk merging ──────────────────────────────

TEST_CASE("UIManager: update_menu_items merges chunk at offset", "[UIManager]") {
    UIManager mgr;
    mgr.handleMessage({{"msg","menu"},{"tag","use_item"},
        {"total_items",3},{"chunk_start",0},
        {"items",{{"text","a - potion of healing"},{"text","b - potion of might"}}}});
    // Second chunk covers index 2
    json update = {{"msg","update_menu_items"},{"chunk_start",2},
        {"items",{{"text","c - potion of brilliance"}}}};
    mgr.handleMessage(update);
    auto& items = mgr.top()->data["items"];
    REQUIRE(items.size() == 3);
    REQUIRE(items[2]["text"] == "c - potion of brilliance");
}

// ── ui-state with generation_id ──────────────────────────────────

TEST_CASE("UIManager: ui-state updates top overlay", "[UIManager]") {
    UIManager mgr;
    mgr.handleMessage({{"msg","ui-push"},{"type","progress-bar"},
        {"generation_id",1},{"status","Loading..."}});
    mgr.handleMessage({{"msg","ui-state"},{"type","progress-bar"},
        {"generation_id",1},{"status","Saving..."}});
    REQUIRE(mgr.top()->data["status"] == "Saving...");
}

TEST_CASE("UIManager: ui-state with wrong generation_id is ignored", "[UIManager]") {
    UIManager mgr;
    mgr.handleMessage({{"msg","ui-push"},{"type","progress-bar"},
        {"generation_id",1},{"status","Loading..."}});
    mgr.handleMessage({{"msg","ui-state"},{"type","progress-bar"},
        {"generation_id",99},{"status","Stale update"}});
    // Should NOT update — generation_id mismatch
    REQUIRE(mgr.top()->data["status"] == "Loading...");
}

// ── ui-stack (spectator sync) ────────────────────────────────────

TEST_CASE("UIManager: ui-stack replaces entire stack", "[UIManager]") {
    UIManager mgr;
    json stackMsg = {{"msg","ui-stack"},{"items", json::array({
        {{"msg","ui-push"},{"type","newgame-choice"}},
        {{"msg","menu"},{"tag","use_item"}}
    })}};
    mgr.handleMessage(stackMsg);
    REQUIRE_FALSE(mgr.empty());
    // Two entries pushed in order
    mgr.handleMessage({{"msg","ui-pop"}});
    REQUIRE_FALSE(mgr.empty());
    mgr.handleMessage({{"msg","ui-pop"}});
    REQUIRE(mgr.empty());
}

// ── title_prompt merge ───────────────────────────────────────────

TEST_CASE("UIManager: title_prompt merges into top menu", "[UIManager]") {
    UIManager mgr;
    mgr.handleMessage({{"msg","menu"},{"tag","inventory"}});
    mgr.handleMessage({{"msg","title_prompt"},{"prompt","Search:"}});
    REQUIRE(mgr.top()->data["prompt"] == "Search:");
    REQUIRE(mgr.top()->data.value("raw", false) == false);
}

// ── menu_scroll ──────────────────────────────────────────────────

TEST_CASE("UIManager: menu_scroll updates first/last/hover", "[UIManager]") {
    UIManager mgr;
    mgr.handleMessage({{"msg","menu"},{"tag","use_item"},{"total_items",10}});
    mgr.handleMessage({{"msg","menu_scroll"},{"first",5},{"last",8},{"hover",6}});
    REQUIRE(mgr.top()->data["first"] == 5);
    REQUIRE(mgr.top()->data["last"] == 8);
    REQUIRE(mgr.top()->data["last_hovered"] == 6);
}
```

#### 1.4 GREEN: Implement remaining message handlers

- `"update_menu"`: iterate the message's fields, merge into `m_stack.back().data`
- `"update_menu_items"`: resize `data["items"]` to `total_items`, copy chunk at `chunk_start`
- `"ui-state"`: check `generation_id` matches top overlay; if so, merge data
- `"ui-stack"`: clear stack, then recursively push each item in `msg["items"]`
- `"title_prompt"`: merge prompt data into top menu entry
- `"menu_scroll"`: store first/last/hover into top menu entry

#### 1.5 RED: Write tests — Menu Navigation Keys

```cpp
// ── Menu navigation keys ─────────────────────────────────────────

TEST_CASE("UIManager: handleMenuNavigationKey returns false when not menu", "[UIManager]") {
    UIManager mgr;
    REQUIRE_FALSE(mgr.handleMenuNavigationKey(SDL_SCANCODE_UP, false));
}

TEST_CASE("UIManager: arrow up when ARROWS_SELECT flag", "[UIManager]") {
    UIManager mgr;
    // ARROWS_SELECT = 0x40000
    mgr.handleMessage({{"msg","menu"},{"tag","use_item"},{"flags",0x40000},
        {"total_items",3},
        {"items",{{"text","a - item1","level",2},{"text","b - item2","level",2},
                  {"text","c - item3","level",2}}}});
    // Initial hover is -1; down arrow should select first
    REQUIRE(mgr.handleMenuNavigationKey(SDL_SCANCODE_DOWN, false));
    REQUIRE(mgr.top()->data["last_hovered"] == 0);
    // Another down arrow
    REQUIRE(mgr.handleMenuNavigationKey(SDL_SCANCODE_DOWN, false));
    REQUIRE(mgr.top()->data["last_hovered"] == 1);
    // Up arrow back
    REQUIRE(mgr.handleMenuNavigationKey(SDL_SCANCODE_UP, false));
    REQUIRE(mgr.top()->data["last_hovered"] == 0);
}

TEST_CASE("UIManager: pgup/pgdn are consumed", "[UIManager]") {
    UIManager mgr;
    mgr.handleMessage({{"msg","menu"},{"tag","use_item"},{"flags",0},{"total_items",20}});
    REQUIRE(mgr.handleMenuNavigationKey(SDL_SCANCODE_PAGEUP, false));
    REQUIRE(mgr.handleMenuNavigationKey(SDL_SCANCODE_PAGEDOWN, false));
}

TEST_CASE("UIManager: home/end are consumed", "[UIManager]") {
    UIManager mgr;
    mgr.handleMessage({{"msg","menu"},{"tag","use_item"},{"flags",0},{"total_items",20}});
    REQUIRE(mgr.handleMenuNavigationKey(SDL_SCANCODE_HOME, false));
    REQUIRE(mgr.handleMenuNavigationKey(SDL_SCANCODE_END, false));
}

TEST_CASE("UIManager: space in menu is consumed (page down)", "[UIManager]") {
    UIManager mgr;
    mgr.handleMessage({{"msg","menu"},{"tag","use_item"},{"flags",0},{"total_items",20}});
    REQUIRE(mgr.handleMenuNavigationKey(SDL_SCANCODE_SPACE, false));
}

TEST_CASE("UIManager: minus in menu depends on tag", "[UIManager]") {
    UIManager mgr;
    // use_item uses '-' as custom key (unwield), so we DON'T consume it
    mgr.handleMessage({{"msg","menu"},{"tag","use_item"},{"flags",0},{"total_items",20}});
    REQUIRE_FALSE(mgr.handleMenuNavigationKey(SDL_SCANCODE_MINUS, false));
    // A generic menu without custom dash DOES consume it (page up)
    mgr.handleMessage({{"msg","menu"},{"tag","actions"},{"flags",0},{"total_items",20}});
    REQUIRE_FALSE(mgr.handleMenuNavigationKey(SDL_SCANCODE_MINUS, false));
}

TEST_CASE("UIManager: arrow keys without ARROWS_SELECT do line scroll", "[UIManager]") {
    UIManager mgr;
    mgr.handleMessage({{"msg","menu"},{"tag","inventory"},{"flags",0},{"total_items",20}});
    // Arrow keys should still be consumed (line up/down scroll)
    REQUIRE(mgr.handleMenuNavigationKey(SDL_SCANCODE_UP, false));
    REQUIRE(mgr.handleMenuNavigationKey(SDL_SCANCODE_DOWN, false));
}
```

#### 1.6 GREEN: Implement `handleMenuNavigationKey()`

Tag-to-custom-dash mapping (menus where `-` has special meaning):
```cpp
static bool menuHasCustomDash(const std::string& tag) {
    return tag == "inventory" || tag == "stash"
        || tag == "actions" || tag == "macros"
        || tag == "macro_mapping" || tag == "use_item";
}
```

Navigation logic:
- Up/Down: if ARROWS_SELECT, cycle hover; else scroll line
- PgUp/PgDn: page scroll (always consumed)
- Home/End: jump to bounds (always consumed)
- Space: page down (always consumed when menu active)
- `-`: page up UNLESS `menuHasCustomDash(tag)` → not consumed, falls through to key forwarding

#### 1.7 Compile & Wire into Test Executable

Add to `CMakeLists.txt`:
```cmake
target_sources(dcss3d_tests PRIVATE
    src/UIManager.cpp
    tests/test_ui_manager.cpp
)

target_sources(dcss3d_obj PRIVATE
    src/UIManager.cpp
    # UIManagerRender.cpp added later in Phase 4
)
```

Run `cd build && ctest` — all UIManager tests should pass.

---

### Phase 2: Wire UIManager into the Pipeline

**Goal**: UIManager receives messages through the normal `processMessages`
dispatch, and the old handlers (WindowManager, DescriptionManager) no longer
receive UI messages that belong to UIManager.

#### 2.1 Register UIManager in handlerConfig

In `main.cpp`, add `UIManager uiManager;` and register it:
```cpp
handlerConfig responseHandlers = {
    // ... existing handlers ...
    {"ui-push", {WindowManager::instance(), descriptionManager}},  // REMOVE
    {"ui-pop", {descriptionManager}},                              // REMOVE
    // NEW:
    {"menu", {uiManager}},
    {"close_menu", {uiManager}},
    {"close_all_menus", {uiManager}},
    {"update_menu", {uiManager}},
    {"update_menu_items", {uiManager}},
    {"menu_scroll", {uiManager}},
    {"title_prompt", {uiManager}},
    {"ui-push", {uiManager, descriptionManager}},   // UIManager for stack, DescMgr for text cache
    {"ui-pop", {uiManager}},
    {"ui-state", {uiManager}},
    {"ui-stack", {uiManager}},
};
```

Note: `DescriptionManager` stays registered for `"ui-push"` alongside UIManager
so it can cache parsed description text. This dual registration is temporary —
in Phase 6, DescriptionManager becomes a passive data holder and UIManager
calls it directly.

#### 2.2 UIManager calls DescriptionManager for describe-item

In `UIManager::handleMessage()`, when processing a `"ui-push"` with
`"type":"describe-item"`, after pushing onto the stack, call
`DescriptionManager::instance().setDescription(title, parsedBody)`.

#### 2.3 Verify: make sure nothing breaks

At this point the old code paths still work (quaff interception, etc.),
but UIManager is now also receiving and processing messages. Since the
old code doesn't handle menu/close_menu/ui-push for describe-item in the
same way, there's no conflict. Build and run — game should still function
as before.

---

### Phase 3: Event Loop Restructure

**Goal**: The event loop matches the JS client's key handling priority.
Quaff interception and description special-cases are removed. All key
decisions flow through UIManager state.

#### 3.1 Remove quaff interception code

Delete from `main.cpp`:
- The `quaffDescribePending` variable
- The entire `if (WindowManager::Mode::QuaffMenu)` letter-handling block
  (lines handling '?' and letter selection in quaff mode)
- The `if (scancode == SDL_SCANCODE_Q && !shift)` enterQuaffMenu handler
- The quaff animation optimistic trigger inside the letter handler
- The `SDK_SCANCODE_Q` no-op case in `process_key()`

#### 3.2 Remove description ESC special case

Delete the first ESC handler in the event loop:
```cpp
// REMOVE this block:
if (event.type == SDL_EVENT_KEY_UP
    && event.key.scancode == SDL_SCANCODE_ESCAPE
    && descriptionManager.hasDescription()) {
    descriptionManager.dismiss();
    continue;
}
```

#### 3.3 Implement new event loop priority order

Rewrite the event loop to the 8-step priority from section 4.5:

```cpp
while (SDL_PollEvent(&event)) {
    // 1. Client-side modals (quit confirm)
    if (wm.getMode() == WindowManager::Mode::QuitConfirm) {
        if (event.type == SDL_EVENT_KEY_UP) {
            if (event.key.scancode == SDL_SCANCODE_ESCAPE) {
                wm.cancelQuitConfirm(renderer.window());
            }
        }
        continue;
    }

    // 2. ESC → send to server (always, regardless of UI state).
    //    Matches JS client: server decides what to do with ESC.
    if (event.type == SDL_EVENT_KEY_UP
        && event.key.scancode == SDL_SCANCODE_ESCAPE
        && wm.isLoggedIn()) {
        json escMsg = {{"msg","key"},{"keycode",27}};
        networkManager.sendMessage(escMsg);
        continue;
    }

    // 3. Menu active + navigation key → handle locally
    if (uiManager.isMenuActive()
        && (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP)
        && uiManager.handleMenuNavigationKey(event.key.scancode,
               event.key.mod & SDL_KMOD_SHIFT)) {
        continue;
    }

    // 4. Mode-toggle keys (F1 overlay, E equipment)
    if (event.type == SDL_EVENT_KEY_UP
        && wm.handleKeyEvent(event.key.scancode, renderer.window())) {
        continue;
    }

    // 5. Shift+Q quit
    if (event.type == SDL_EVENT_KEY_UP
        && event.key.scancode == SDL_SCANCODE_Q
        && (event.key.mod & SDL_KMOD_SHIFT)) {
        wm.enterQuitConfirm(renderer.window());
        continue;
    }

    // 6. Key forwarding to server (menu/prompt modes)
    bool inForwardingMode = uiManager.shouldForwardKeysToServer()
                         || !inputModeTracker.isGameplayMode();
    if (inForwardingMode
        && event.type == SDL_EVENT_KEY_DOWN
        && wm.isLoggedIn()) {
        char c = scancodeToChar(event.key.scancode, event.key.mod);
        if (c != '\0') {
            if (c == 27)      // ESC (shouldn't reach here but be safe)
                networkManager.sendMessage({{"msg","key"},{"keycode",27}});
            else if (c == 13) // Enter
                networkManager.sendMessage({{"msg","key"},{"keycode",13}});
            else if (c == 9)  // Tab
                networkManager.sendMessage({{"msg","key"},{"keycode",9}});
            else
                networkManager.sendMessage({{"msg","input"},{"text",std::string(1,c)}});
            continue;
        }
    }

    // 7. Game input (WASD, mouse) — only when stack empty AND gameplay
    bool isGameplay = inputModeTracker.isGameplayMode();
    if (!uiManager.shouldBlockGameInput() && isGameplay
        && !(io.WantCaptureMouse || io.WantCaptureKeyboard)) {
        // ... existing game input processing (processInput, processMouseInput) ...
    }

    // 8. ImGui processing (last)
    if (wm.shouldRenderUI()) {
        ImGui_ImplSDL3_ProcessEvent(&event);
    }
}
```

#### 3.4 Remove old ESC-to-server code

The old Normal-mode ESC path in the current event loop:
```cpp
// REMOVE this block:
if (event.type == SDL_EVENT_KEY_UP
    && event.key.scancode == SDL_SCANCODE_ESCAPE
    && !descriptionManager.hasDescription()
    && WindowManager::instance().getMode() == WindowManager::Mode::Normal
    && WindowManager::instance().isLoggedIn()) {
    json escMsg = { { "msg", "key" }, { "keycode", 27 } };
    networkManager.sendMessage(escMsg);
    continue;
}
```

This is superseded by step 2 in the new event loop (section 3.3), which
sends ESC unconditionally when logged in — matching the JS client exactly.

---

### Phase 4: Menu Rendering (ImGui)

**Goal**: Create `UIManagerRender.cpp` with per-tag ImGui rendering.
The game becomes playable with server-driven menus.

Testing approach: rendering is verified via manual playtesting, not
unit tests (ImGui doesn't have a headless test mode).

#### 4.1 Create `src/UIManagerRender.cpp`

Add to `dcss3d_obj` only (not the test executable):
```cmake
target_sources(dcss3d_obj PRIVATE
    src/UIManagerRender.cpp
)
```

#### 4.2 Implement `UIManager::render()`

```cpp
void UIManager::render(const Player& player, NetworkManager& net,
                       SDL_Window* window, DescriptionManager& descMgr) {
    if (m_stack.empty()) return;
    const auto& top = m_stack.back();
    if (top.type == UIEntry::Menu)
        renderMenu(top, player);
    else
        renderOverlay(top, net, descMgr);
}
```

If the stack has multiple entries (e.g., describe-item on top of use_item),
`render()` only renders the topmost. The underlying entries are hidden.

#### 4.3 Implement `renderMenu()`

Dispatch on tag. For initial implementation, implement:
- `renderMenu_useItem()` — potions/scrolls (matches current quaffMenu but
  renders from `entry.data["items"]` array)
- `renderMenu_inventory()` — drop/wield/wear
- `renderMenu_generic()` — fallback for any unrecognized tag

Each renders an `ImGui::BeginPopupModal()` listing items from the server's
items array, with hotkey letters and item text (color tags stripped).

Non-selectable items (item["level"] < 2) are rendered as section headers.

The `--more--` text from `entry.data["more"]` is shown at the bottom.

#### 4.4 Implement `renderOverlay()` dispatch skeleton

Start with a switch on `type` that for now just shows:
- `"describe-item"` → calls existing `descriptionWindow()`
- `"newgame-choice"` → calls existing `characterSelectWindow()`
- Everything else → `ImGui::Text("Unhandled overlay: %s", type)`

Full overlay rendering is Phase 5.

#### 4.5 Update `displayAllWindows()`

Add `UIManager& uiManager` parameter and call `uiManager.render()` before
the existing window rendering. Remove the inline quaff menu and description
popup lifecycle management (those are now handled by UIManager).

#### 4.6 Playtest

- Connect to a game, press `q` → server sends menu → UIManager pushes →
  render displays quaff popup from menu items
- Press a letter → sent to server → server responds with close_menu →
  menu disappears
- Verify that `?` during quaff works (server-driven, describe-item stacks
  on top)

---

### Phase 5: Overlay Rendering (ImGui)

**Goal**: All `ui-push` overlay types have working renderers.

#### 5.1 Implement all overlay renderers in `UIManagerRender.cpp`

Following the handler dispatch table from section 2.3:

| Priority | Type | Implementation |
|----------|------|----------------|
| 1 | `describe-item` | Existing `descriptionWindow()` with color-tag stripping, SPELLSET_PLACEHOLDER handling, actions text |
| 2 | `describe-generic` | Title + body text in a scroller |
| 3 | `newgame-choice` | Move call from WindowManager; `characterSelectWindow()` reads from `entry.data` |
| 4 | `progress-bar` | Title, bar text, status line |
| 5 | `formatted-scroller` | Body text in scrollable region; arrow keys scroll; sends `formatted_scroller_scroll` on scroll |
| 6 | `game-over` | Title + body text |
| 7-12 | `describe-monster`, `describe-spell`, `describe-god`, `describe-cards`, `describe-feature-wide`, `version` | Basic text display for now; multi-pane support added later |
| 13 | `seed-selection` | Text input field |
| 14 | `msgwin-get-line` | Single prompt text |

#### 5.2 Move newgame-choice from WindowManager to UIManager

- `WindowManager::m_characterSelectData` is removed
- `UIManager::handleMessage("ui-push", "newgame-choice")` pushes an Overlay entry
- `render()` calls `characterSelectWindow()` with data from `entry.data`
- `WindowManager::handleMessage` no longer processes `"ui-push"` or `"map"` for
  character select state

#### 5.3 Playtest all overlay types

---

### Phase 6: WindowManager & DescriptionManager Cleanup

**Goal**: Remove all deprecated code paths. WindowManager is minimal.
DescriptionManager is a passive data holder.

#### 6.1 WindowManager: remove deprecated members

- `Mode::QuaffMenu` enum value
- `enterQuaffMenu()`, `cancelQuaffMenu()`
- `m_characterSelectData`, `setCharacterSelectData()`, `getCharacterSelectData()`,
  `clearCharacterSelectData()`
- `ui-push` / `map` handling in `handleMessage()` (lines processing newgame-choice
  and map-for-char-select-clear)
- `ui-push` registration in handlerConfig in main.cpp

Update `handleKeyEvent()`:
- Remove QuaffMenu ESC case
- Keep F1 and E toggles

#### 6.2 Update existing WindowManager tests

Remove tests for QuaffMenu, quaff entry/cancel, character select data.
Update tests that reference removed Mode values.

#### 6.3 DescriptionManager: remove MessageHandler

- Remove `handleMessage()` override
- Remove `requestDescription()` method
- Remove `MessageHandler` from class declaration
- Keep: `setDescription()`, `hasDescription()`, `itemName()`, `description()`,
  `dismiss()`, `stripColorTags()`

#### 6.4 UIManager manages DescriptionManager directly

In `UIManager::handleMessage()`, when processing `"ui-push"` with
`"type":"describe-item"`:
```cpp
std::string clean = DescriptionManager::stripColorTags(body);
// ... SPELLSET_PLACEHOLDER handling ...
descriptionManager.setDescription(title, clean);
```

Remove `descriptionManager` from `handlerConfig` for `"ui-push"` — UIManager
is the sole handler and it calls DescriptionManager internally.

---

### Phase 7: Polish and Edge Cases

**Goal**: Handle all remaining message types and edge cases.

#### 7.1 Quaff animation on server confirmation

Instead of triggering when the player presses a letter, trigger when
`close_menu` arrives AND the player's potion count decreased.

Track `m_lastPotionCount` in `Player` (or a small helper). On `close_menu`:
```cpp
int newCount = countPotions(player.data().inv);
if (newCount < m_lastPotionCount) {
    triggerQuaffAnimation();
}
m_lastPotionCount = newCount;
```

#### 7.2 Mouse click on menu items

In menu render functions, each item is rendered with `ImGui::Selectable()`
or a clickable region. On click, send the item's hotkey:
```cpp
if (ImGui::Selectable(label)) {
    int hotkey = item["hotkeys"][0];
    net.sendMessage({{"msg","key"},{"keycode",hotkey}});
}
```

#### 7.3 --more-- indicator in menus

When `entry.data.contains("more")`, render the more text at the bottom
of the menu popup as a non-interactive text line.

#### 7.4 `close_all_menus` on level changes

Already implemented in Phase 1. Verify in playtesting that level transitions
clear the UI stack.

#### 7.5 `ui_cutoff` handling

When received, mark entries at or below the cutoff index as hidden.
`render()` skips hidden entries when showing the stack.

#### 7.6 `ui-scroller-scroll` handling

When received and top overlay is a `formatted-scroller`, apply the scroll
position from the message.

---

## 6. Files to Create/Modify

| File | Change |
|------|--------|
| `src/UIManager.hpp` | **NEW** — UIManager class, UIEntry struct (no imgui) |
| `src/UIManager.cpp` | **NEW** — Stack, handleMessage, queries, menu nav keys |
| `src/UIManagerRender.cpp` | **NEW** — render(), all per-tag ImGui render functions |
| `tests/test_ui_manager.cpp` | **NEW** — TDD tests for stack, messages, navigation |
| `src/WindowManager.hpp` | Remove Mode::QuaffMenu, quaff methods, char select state |
| `src/WindowManager.cpp` | Remove quaff/description/char-select code; simplify handleKeyEvent |
| `tests/test_window_manager.cpp` | Remove QuaffMenu/char-select test cases |
| `src/DescriptionManager.hpp` | Remove MessageHandler, requestDescription; become data holder |
| `src/DescriptionManager.cpp` | Remove handleMessage, requestDescription; keep stripColorTags |
| `src/main.cpp` | Restructure event loop; remove quaff/description special cases; wire UIManager |
| `src/imguilayouts.hpp` | Add UIManager& parameter to displayAllWindows |
| `src/imguilayouts.cpp` | Remove quaff/desc lifecycle; delegate to UIManager::render() |
| `CMakeLists.txt` | Add UIManager.cpp, UIManagerRender.cpp, test_ui_manager.cpp |

## 7. Key Design Decisions

### 7.1 Render from server menu items, not PlayerData::inv
**Rationale**: The JS client renders menus from the server's items array. The
server controls which items appear, how they're formatted, and what hotkeys
they have. Using `PlayerData::inv` is a client-side assumption that can drift
from server state. The server's items array is authoritative.

### 7.2 Quaff animation triggered on server confirmation
**Rationale**: Don't trigger animation optimistically when the player presses a
letter. Trigger on `close_menu` arrival, after verifying the potion count
decreased in the player update. This ensures we never show an animation for a
rejected action.

### 7.3 DescriptionManager becomes a data holder, not a message handler
**Rationale**: `UIManager` owns all UI message handling. Having two classes
handle `ui-push` creates ordering problems. `UIManager` processes the message,
pushes onto the stack, and populates `DescriptionManager` with the parsed text.
The render code reads from `DescriptionManager` for convenience but the
authoritative state is in the UIManager stack.

### 7.4 Forward all keys to server when UI is stacked
**Rationale**: The JS client's architecture: keys go to the server unless
locally handled for navigation. We replicate this: when `UIManager` stack is
non-empty, all keypresses (printable characters) go to the server. Only menu
navigation keys are handled locally. This eliminates all special-case key
handling for menus.

### 7.5 No incremental migration for quaff menu
**Rationale**: Doing it all at once avoids maintaining two parallel quaff paths.
The existing client-side quaff flow is fundamentally incompatible with the
server-driven model. Replace it completely in one go.

### 7.6 Separate UIManager state from rendering (two translation units)
**Rationale**: `UIManager.cpp` contains all state management, message handling,
and query logic — no imgui dependency. `UIManagerRender.cpp` contains the
`render()` method and all per-tag ImGui render functions. The test executable
(`dcss3d_tests`) compiles only `UIManager.cpp`, allowing comprehensive unit
tests without linking imgui or SDL_gpu.

### 7.7 Red/green TDD for all state management
**Rationale**: Tests are written first (red), then the minimal implementation
is written to pass (green). This applies to stack operations, message handling,
and navigation key logic. ImGui rendering is verified via manual playtesting
since there's no headless ImGui test mode.

### 7.8 ESC always sent to server when logged in
**Rationale**: The JS client sends ESC to the server regardless of UI state.
The server knows whether to pop a UI element, cancel a mode, or do nothing.
We remove the client-side ESC special cases (dismiss description, cancel quaff,
etc.) and let the server drive all ESC behavior. This matches `client.js`
`handle_keydown` which sends `{"msg":"key","keycode":27}` for unmodified ESC
with no UI-state preconditions.

## 8. Risks and Mitigations

1. **Risk**: Server sends menu messages our renderer doesn't handle yet
   **Mitigation**: Generic menu renderer handles unknown tags; add specific
   renderers incrementally. The state management (Phase 1) handles all message
   types regardless of render support.

2. **Risk**: Key forwarding might send keys the server doesn't expect
   **Mitigation**: This matches the JS client's behavior exactly. The server is
   designed for this pattern. If a key is irrelevant in the current mode, the
   server ignores it.

3. **Risk**: `update_menu_items` chunk handling for very large menus
   **Mitigation**: Handled in Phase 1 with TDD tests for chunk merging.
   Items array is resized to `total_items` and chunks are placed at
   `chunk_start` offsets.

4. **Risk**: Character select (newgame-choice) is currently handled by
   WindowManager and is complex
   **Mitigation**: Move the rendering function to UIManager but keep the
   existing `characterSelectWindow()` and `parseNewgameChoice()` code. The
   only change is who owns calling it.

5. **Risk**: Event loop changes break non-menu gameplay input
   **Mitigation**: The new event loop is structured as a priority cascade.
   Each step only fires when its preconditions are met. Game input (step 7)
   only fires when the UI stack is empty AND `isGameplayMode()` is true.
   This is a strict subset of the current behavior.

6. **Risk**: `UIManager.cpp` includes headers that don't compile in test context
   **Mitigation**: `UIManager.hpp` does NOT include imgui, SDL_gpu, or any
   rendering headers. It only includes `<vector>`, `<string>`, `<string_view>`,
   `nlohmann/json.hpp`, and `MessageQueue.hpp`. The render method is declared
   in the header but defined in `UIManagerRender.cpp` which is NOT compiled
   into the test executable.
