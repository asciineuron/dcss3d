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

## 5. Implementation Plan

### Phase 1: UIManager Foundation

#### 1.1 Create `src/UIManager.hpp`
- `UIManager` class with `UIEntry` struct, `UIStack` as `std::vector<UIEntry>`
- `push()`, `pop()`, `clear()`, `top()`, `empty()` methods
- `hasOverlay()`, `hasMenu()`, `shouldBlockGameInput()`, `shouldForwardKeysToServer()`, `isMenuActive()`
- `handleMenuNavigationKey()` for arrow keys, pgup/pgdn, home/end
- `handleMessage()` override for `MessageHandler`
- `render()` method with NetworkManager parameter for sending keys

#### 1.2 Create `src/UIManager.cpp`
- Implement stack push/pop/clear
- Implement all query methods
- Implement menu navigation key handler:
  - Up/Down arrows → cycle hover (when ARROWS_SELECT flag set) or scroll line
  - PgUp/PgDn → page scroll
  - Home/End → jump to top/bottom
  - Space/+ → page down
  - - → page up (unless menu uses - as custom key)
- Implement `handleMessage()` dispatch:
  ```
  "menu"           → push(Menu, tag, data)
  "close_menu"     → pop()
  "close_all_menus"→ clear()
  "update_menu"    → merge metadata into top menu (title, flags, total_items)
  "update_menu_items" → merge items chunk into top menu
  "menu_scroll"    → update scroll position
  "title_prompt"   → enter search/filter mode in top menu
  "ui-push"        → push(Overlay, type, data)
  "ui-pop"         → pop()
  "ui-state"       → update top overlay state
  "ui-stack"       → set entire stack (spectator sync)
  ```

#### 1.3 Wire UIManager into main.cpp
- Add `UIManager uiManager;` instance
- Register for messages: `"menu"`, `"close_menu"`, `"close_all_menus"`,
  `"update_menu"`, `"update_menu_items"`, `"menu_scroll"`, `"title_prompt"`,
  `"ui-push"`, `"ui-pop"`, `"ui-state"`, `"ui-stack"`
- Remove `WindowManager` and `DescriptionManager` from `"ui-push"` handler
  (UIManager takes over)

#### 1.4 Unit tests: `tests/test_ui_manager.cpp`
- Test stack push/pop/clear
- Test hasOverlay/hasMenu queries
- Test shouldBlockGameInput / shouldForwardKeysToServer
- Test menu navigation key handling
- Test update_menu_items chunk merging
- Test close_all_menus clearing
- Test ui-push/pop round-trip

### Phase 2: Event Loop Restructure

#### 2.1 Rewrite event loop in main.cpp
- Implement the new 8-step priority order from section 4.5
- Remove all quaff-specific code paths:
  - Remove `quaffDescribePending` variable
  - Remove the enterQuaffMenu handler
  - Remove the quaff letter selection handler
  - Remove the quaff animation optimistic trigger
- Remove description-specific ESC handler (step 1 in current code)
- Move description dismiss to be server-driven
- Implement key forwarding path (step 6) for non-gameplay modes
- Keep Shift+Q quit confirm and F1/E toggles in WindowManager

#### 2.2 Update process_key() for q forwarding
- Remove the `SDK_SCANCODE_Q` case from `process_key()` (it had no-op comment)
- q should flow through the normal key handling path

#### 2.3 Update prompt mode handling
- Current code already forwards keys in prompt mode (step in current event loop)
- Ensure this works with the new priority order
- Key forwarding should cover MORE, PROMPT, YESNO modes plus any UI stack mode

### Phase 3: Menu Rendering

#### 3.1 Implement `UIManager::renderMenu()`
- Read the top `UIEntry` from the stack
- Dispatch on `tag` to per-tag render functions:
  - `"use_item"` → `renderMenu_useItem()`
  - `"inventory"` → `renderMenu_inventory()`
  - `"actions"` → `renderMenu_actions()`
  - `"macros"` / `"macro_mapping"` → `renderMenu_macros()`
  - `"ability"` → `renderMenu_ability()`
  - `"spell"` → `renderMenu_spell()`
  - Default → `renderMenu_generic()`

#### 3.2 Implement `renderMenu_useItem()` (quaff/read/evoke)
- Render an ImGui popup modal titled with the menu's `title`
- List items from the menu's `items` array:
  ```
  for (auto& item : menuData["items"]) {
      std::string text = item["text"];  // server-formatted text
      // Strip color tags for plain text display
      // Show hotkey letters
  }
  ```
- Uses menu items array, NOT `PlayerData::inv`
- The menu's `flags` field determines behavior (e.g. ALLOW_FILTER shows ? hint)
- Hotkeys come from `item["hotkeys"]` array (array of keycodes)
- Non-selectable items (level < 2) shown as headers

#### 3.3 Implement `renderMenu_inventory()` (drop/wield/wear)
- Same structure as use_item but with different flags
- May have MULTISELECT for drop-many

#### 3.4 Implement other menu renderers
- `renderMenu_generic()`: Lists items with hotkeys, suitable for actions/macros/etc.
- Start with generic renderer for uncommon menu types; specialize later

#### 3.5 Handle menu scrolling for large menus
- Track `total_items` vs chunk size
- Implement `update_menu_items` merging into the menu data
- Show --more-- indicator when applicable
- Support page-up/page-down navigation

#### 3.6 Handle title_prompt (search/filter in menus)
- When `title_prompt` arrives, show an ImGui input text in the menu title
- Send typed characters to the server
- Enter sends the accumulated text + Enter
- ESC cancels (sent to server)

### Phase 4: Overlay Rendering

#### 4.1 Implement `UIManager::renderOverlay()`
- Dispatch on `type` to per-type render functions, matching ui-layouts.js handlers:
  - `"describe-item"` → `renderOverlay_describeItem()`
  - `"describe-generic"` → `renderOverlay_describeGeneric()`
  - `"describe-monster"` → `renderOverlay_describeMonster()`
  - `"describe-spell"` → `renderOverlay_describeSpell()`
  - `"describe-god"` → `renderOverlay_describeGod()`
  - `"newgame-choice"` → `renderOverlay_newgameChoice()` (move from WindowManager)
  - `"progress-bar"` → `renderOverlay_progressBar()`
  - `"formatted-scroller"` → `renderOverlay_formattedScroller()` (help, ? screens)
  - `"game-over"` → `renderOverlay_gameOver()`
  - `"version"` → `renderOverlay_version()`
  - `"seed-selection"` → `renderOverlay_seedSelection()`
  - `"msgwin-get-line"` → `renderOverlay_msgwinGetLine()`
  - Default → show unhandled type notice

#### 4.2 Implement `renderOverlay_describeItem()`
- Body text: strip DCSS color tags, convert `\n\n` to paragraph breaks
- Handle `SPELLSET_PLACEHOLDER` → replace with formatted spell list
- Show `actions` text with clickable hotkeys
- Uses data from the `ui-push` message (stored in UIEntry.data), NOT from DescriptionManager
- DescriptionManager can cache the parsed text for convenience but is not authoritative

#### 4.3 Move newgame-choice from WindowManager to UIManager
- `characterSelectWindow()` currently called from `displayAllWindows()` based on `WindowManager::getCharacterSelectData()`
- New: `UIManager` pushes an Overlay entry when `ui-push` newgame-choice arrives
- `render()` calls `characterSelectWindow()` when top of stack is newgame-choice
- WindowManager no longer holds `m_characterSelectData`

#### 4.4 Implement formatted_scroller (help, ? screens)
- This is the most complex overlay type
- Server sends text with color tags; we strip and display
- Scrolling sends `formatted_scroller_scroll` message back to server
- Highlight text support for search results

#### 4.5 Implement other overlay types
- `progress-bar`: Simple progress bar with title and status text
- `describe-generic`: Title + body text
- `describe-monster`: Multi-pane (description + quote), with ! to switch panes
- `describe-god`: Multi-pane (description, powers, wrath, extra)
- `describe-spell`: Spell info with can_mem action
- `game-over`: Death/victory text
- `version`: Version info
- `seed-selection`: Text input for seed
- `msgwin-get-line`: Single text prompt

### Phase 5: WindowManager Cleanup

#### 5.1 Simplify WindowManager
- Remove `Mode::QuaffMenu`, `Mode::QuitConfirm` (quit confirm stays as client-side)
- Remove `enterQuaffMenu()`, `cancelQuaffMenu()`
- Remove `m_characterSelectData` and related methods
- Remove `ui-push` handling from `handleMessage()` (moves to UIManager)

#### 5.2 Update handleKeyEvent
- Remove QuaffMenu ESC handling
- Remove QuitConfirm ESC handling (keep quit confirm but handled in event loop step 1)
- Keep F1 (overlay toggle) and E (equipment toggle)

#### 5.3 Update shouldRenderUI and related methods
- Remove QuaffMenu-specific logic
- Keep Overlay and Equipment window visibility logic

### Phase 6: DescriptionManager Cleanup

#### 6.1 Simplify DescriptionManager
- Remove `MessageHandler` interface (no longer receives messages directly)
- Remove `requestDescription()` method (server-driven flow handles this)
- Keep `setDescription()`, `hasDescription()`, `itemName()`, `description()`, `dismiss()`, `stripColorTags()`
- UIManager calls `setDescription()` when a `ui-push` describe-item is processed

#### 6.2 Update description rendering
- `descriptionWindow()` is called by `UIManager::render()` when top of stack is describe-item
- Data flows: server `ui-push` → `UIManager::handleMessage` → `UIManager::push` → `UIManager::render` → `descriptionWindow()`

### Phase 7: Render Integration

#### 7.1 Update `displayAllWindows()`
- Add `UIManager&` parameter (or make it globally accessible)
- Call `uiManager.render()` after existing window rendering
- Remove inline quaff menu popup lifecycle management
- Remove inline description window popup lifecycle management
- Remove newgame-choice from `displayAllWindows` (moved to UIManager)

#### 7.2 Update main.cpp render section
- Pass `uiManager` to `displayAllWindows()`
- Remove standalone quaff menu / description rendering calls

### Phase 8: Polish and Edge Cases

#### 8.1 Handle `close_all_menus` on level changes
- Server sends `close_all_menus` on level transitions and game resets
- `UIManager::clear()` empties the entire stack with no animation

#### 8.2 Handle `ui-stack` for future spectator support
- Server sends full stack state for spectators to sync
- `UIManager` stores the full stack from the message

#### 8.3 Handle `ui-state` updates
- Update top overlay with new state (progress, scroll, pane switches)
- Check generation_id to ensure update applies to current popup version

#### 8.4 Handle `ui-scroller-scroll` for scroll sync
- Server-initiated scroll position for formatted scrollers
- Apply scroll position to the active scroller overlay

#### 8.5 Handle `ui_cutoff` message
- Server sends cutoff index; all popups behind that index should be hidden
- Used in some UI transitions

#### 8.6 Quaff animation timing
- **Decision: Trigger on server confirmation, not optimistically.**
- When `close_menu` arrives after a quaff, check if a potion was consumed
  (compare old vs new PlayerData::inv potion counts).
- If consumed, trigger the quaff animation.
- This avoids phantom animations when server rejects the quaff.
- Track last known potion count in `Player` or a small helper.

#### 8.7 Mouse click on menu items
- Support clicking on menu items to select them
- Send the item's hotkey to the server on click
- Consistent with JS client's `item_click_handler`

#### 8.8 --more-- display
- When a menu has `more` text, display it at the bottom of the menu
- The JS client shows `--press Space or Enter to continue--` etc.
- Render this as a non-interactive text line in the ImGui popup

## 6. Files to Create/Modify

| File | Change |
|------|--------|
| `src/UIManager.hpp` | **NEW** — UIManager class, UIEntry struct, UIStack |
| `src/UIManager.cpp` | **NEW** — Stack management, message handling, rendering, menu navigation |
| `tests/test_ui_manager.cpp` | **NEW** — Unit tests for stack logic, message handling, navigation |
| `src/WindowManager.hpp` | Simplify Mode enum, remove quaff/description/character-select methods |
| `src/WindowManager.cpp` | Remove quaff methods, ui-push handling; simplify handleKeyEvent |
| `src/DescriptionManager.hpp` | Remove MessageHandler, remove requestDescription; become data holder |
| `src/DescriptionManager.cpp` | Remove handleMessage, requestDescription; keep stripColorTags |
| `src/main.cpp` | Restructure event loop; remove quaff/description special cases; wire UIManager |
| `src/imguilayouts.hpp` | Add UIManager& parameter to displayAllWindows; keep window display functions |
| `src/imguilayouts.cpp` | Remove quaff popup lifecycle, description lifecycle; delegate to UIManager |
| `CMakeLists.txt` | Add UIManager.cpp, test_ui_manager.cpp |

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

## 8. Risks and Mitigations

1. **Risk**: Server sends menu messages our renderer doesn't handle yet
   **Mitigation**: Generic menu renderer handles unknown tags; add specific
   renderers incrementally.

2. **Risk**: Key forwarding might send keys the server doesn't expect
   **Mitigation**: This matches the JS client's behavior exactly. The server is
   designed for this pattern. If a key is irrelevant in the current mode, the
   server ignores it.

3. **Risk**: `update_menu_items` chunk handling for very large menus
   **Mitigation**: Handle chunk merging in UIManager; render from merged items
   array. Track total_items vs received items.

4. **Risk**: Character select (newgame-choice) is currently handled by
   WindowManager and is complex
   **Mitigation**: Move the rendering function to UIManager but keep the
   existing `characterSelectWindow()` and `parseNewgameChoice()` code. The
   only change is who owns calling it.
