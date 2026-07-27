# UI Response Pipeline — Server-Driven Architecture

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

## 2. How the JS Client Works

### 2.1 Two Separate UI Message Systems

The server has two distinct message families for UI:

| Message | Purpose | Handled by |
|---------|---------|------------|
| `menu`, `close_menu`, `update_menu`, `close_all_menus` | Interactive selection menus (quaff, drop, inventory, spell list) | `menu.js` |
| `ui-push`, `ui-pop`, `ui-state` | Informational overlays (describe-item, newgame-choice, progress bars, --more--) | `ui-layouts.js` |

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

The client's `open_menu()` renders this as an interactive HTML list.  When the
player presses a letter (e.g. `a`), the client sends `{"msg":"input","text":"a"}`
to the server.  The server processes the selection and sends `close_menu`.

Menu tags seen in the crawl source:
- `"use_item"` — quaff, read, evoke, etc. (created by `UseItemMenu`)
- `"inventory"` — drop, wield, wear, etc. (created by `InvMenu`)
- `"actions"` — ability/action selection
- `"macros"` — macro mapping

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
`{"msg":"key","keycode":27}` to the server.  The server pops the overlay and
sends back `{"msg":"ui-pop"}`.  The client calls `hide_popup()` which removes
the topmost popup from `#ui-stack`.

Known `ui-push` types: `"describe-item"`, `"describe-generic"`, 
`"describe-monster"`, `"describe-spell"`, `"describe-god"`,
`"newgame-choice"`, `"progress-bar"`, `"formatted-scroller"`,
`"describe-feature-wide"`, `"game-over"`, `"version"`, `"seed-selection"`,
`"msgwin-get-line"`, `"describe-cards"`, `"newgame-random-combo"`.

### 2.4 The UI Stack (`ui.js`)

```
┌─────────────────────┐
│  #ui-stack (DOM)    │
│  ┌─────────────────┐│  ← topmost (active)
│  │ describe-item   ││
│  ├─────────────────┤│
│  │ use_item menu   ││
│  ├─────────────────┤│
│  │ newgame-choice  ││
│  └─────────────────┘│  ← bottom (inactive, hidden)
└─────────────────────┘
```

- `show_popup(id, centred, generation_id)`: Appends a new wrapper `<div>` to
  `#ui-stack`.  The new popup goes on TOP of any existing ones.
- `hide_popup(show_below)`: Removes the topmost wrapper.  If `show_below` is
  true, the previously underlying popup becomes visible.
- `top_popup()`: Returns the topmost (last) child of `#ui-stack`.
- ESC handling: `popup_keydown_handler` doesn't directly close popups.  Instead,
  `ui_hotkey_handler` processes the ESC key by sending it to the server (via
  `comm.send_message("key", {keycode: 27})`).  The server responds with
  `ui-pop` or `close_menu`, which triggers `hide_popup()`.

### 2.5 Input Mode

The server sends `{"msg":"input_mode","mode":<n>}` to indicate what kind of
input it expects:

| Mode | Constant | Meaning |
|------|----------|---------|
| 0 | NORMAL | Regular gameplay |
| 1 | COMMAND | Targeting / command mode |
| 2 | TARGET | Target selection |
| 5 | MORE | --more-- prompt |
| 7 | PROMPT | Text prompt (stat gain, scroll reading) |
| 8 | YESNO | Yes/no confirmation |

### 2.6 Key Interaction Flow for Quaffing (JS Client)

```
1. Player presses 'q'
2. Client sends {"msg":"input","text":"q"} to server
3. Server enters quaff mode, sends {"msg":"menu","tag":"use_item",...}
4. Client's open_menu() renders the potion list as HTML
5. Player presses 'a' (letter of a potion)
6. Client sends {"msg":"input","text":"a"} to server
7. Server processes quaff, sends {"msg":"close_menu"} + {"msg":"player",...}
8. Client closes the menu
```

## 3. Current State of Our Client

### 3.1 What We Do (Anti-Patterns)

1. **Key interception**: We intercept `q` in the event loop, show our own ImGui
   quaff menu, and only send `q` + letter to the server when the player selects
   a potion.  The server never knows we showed a menu — it receives `q` and the
   letter in quick succession.

2. **Client-side mode management**: `WindowManager::Mode` is a flat enum
   (`Login`, `Normal`, `Overlay`, `Equipment`, `QuitConfirm`, `QuaffMenu`).
   Only ONE mode can be active.  This can't represent stacked windows
   (description on top of quaff menu).

3. **ESC overloading**: ESC does different things depending on mode:
   - Description showing → dismiss description
   - QuaffMenu → cancel quaff
   - QuitConfirm → cancel quit
   - Equipment → return to Normal
   - Normal → (moved to server, was toggleOverlay)

4. **Missing message handlers**: We don't handle `"menu"` or `"close_menu"`
   messages at all.  We only handle `"ui-push"` for `"newgame-choice"`.

### 3.2 What We Already Handle Correctly

- `"ui-push"` with `type: "newgame-choice"` → Character select window
- `"ui-state"` with `type: "describe-item"` → Description window (after fix)
- `"input_mode"` → InputModeTracker
- Description request flow uses `inv_item_describe` (correct per JS client)

## 4. Target Architecture

### 4.1 Design Principles

1. **Server is the single source of truth for UI state.**  We never decide
   on our own to show/hide windows — we react to server messages.

2. **Keys go to the server.**  We don't intercept keys to manage client-side
   menus.  The server sends us instructions on what to render.

3. **UI is a stack.**  Multiple windows can be open simultaneously, with the
   topmost receiving input.  ESC always pops the topmost (via server round-trip).

4. **Thin rendering layer.**  Our ImGui windows just render data we already
   have (`PlayerData::inv`, `DescriptionManager`) or data from the server
   message itself.

### 4.2 New `UIManager` Class

Replaces the window-management responsibilities currently scattered across
`WindowManager`, `main.cpp`, and `imguilayouts`.

```cpp
class UIManager : public MessageHandler {
public:
    // Stack entry for one open UI element
    struct UIEntry {
        enum Type {
            Menu,          // interactive selection (quaff, drop, inventory)
            Overlay,       // informational (describe, progress bar, --more--)
            Modal,         // client-side modal (quit confirm, character select)
        };
        Type type;
        std::string tag;           // "use_item", "describe-item", etc.
        json data;                 // the full server message
        bool blocksGameInput = true;
    };

    // Push/pop from the stack
    void push(UIEntry entry);
    void pop();
    void clear();

    // Query stack state
    const UIEntry* top() const;
    bool empty() const;
    bool hasTag(std::string_view tag) const;

    // Whether game input (WASD, mouse) should be processed
    bool shouldProcessGameInput() const;

    // MessageHandler: processes "menu", "close_menu", "close_all_menus",
    // "ui-push", "ui-pop", "ui-stack"
    void handleMessage(const json& message) override;

    // Render all active UI elements (called from displayAllWindows)
    void render(const Player& player, NetworkManager& net,
                SpriteManager& sprites, std::vector<SpriteHandle>& effects,
                DescriptionManager& desc, SDL_Window* window);

private:
    std::vector<UIEntry> m_stack;

    // Per-tag render functions
    void renderMenu_useItem(const Player& player);
    void renderOverlay_describeItem();
    void renderOverlay_newgameChoice(NetworkManager& net);
    void renderOverlay_morePrompt();
    void renderModal_quitConfirm(SDL_Window* window);
    // ... etc
};
```

### 4.3 WindowManager Simplification

`WindowManager` currently manages both:
1. Game-level modes (Login, Normal)
2. UI popup modes (QuitConfirm, QuaffMenu, Equipment, Overlay)

After migration, `WindowManager` only handles:
1. **Connection state**: `isLoggedIn()`, `isGameConnected()`
2. **Overlay toggle**: F1 to show/hide debug overlay (NOT tied to ESC)
3. **Equipment toggle**: E to show/hide equipment window
4. **Quit confirmation**: Client-side modal, still needed since `Shift+Q` isn't
   a server-driven menu
5. **Mouse mode sync**: `shouldUseRelativeMouse()`, `syncMouseMode()`

The `Mode` enum simplifies to:
```cpp
enum class Mode {
    Login,      // pre-authentication
    Playing,    // normal gameplay (UI may be stacked on top)
};
```

All popup management (QuaffMenu, description, --more--) moves to `UIManager`.

### 4.4 Event Loop Changes

The event loop in `main.cpp` simplifies significantly:

```cpp
while (SDL_PollEvent(&event)) {
    // 1. Client-side modals (quit confirm)
    if (quit confirm active) { handle quit keys; continue; }

    // 2. Send ESC to server if UI is stacked
    if (ESC && !uiManager.empty()) {
        send ESC to server; continue;
    }

    // 3. Mode-toggle keys (F1 overlay, E equipment)
    if (WindowManager::handleKeyEvent(...)) continue;

    // 4. Shift+Q quit
    if (Shift+Q) { enterQuitConfirm(); continue; }

    // 5. Game input (only when no UI blocks it)
    if (uiManager.shouldProcessGameInput()) {
        // process WASD, mouse, etc.
    }

    // 6. Prompt input (only when no UI blocks it)
    if (uiManager.shouldProcessGameInput() && !isGameplay) {
        // send text characters to server
    }

    // 7. ImGui processing (last, for window interaction)
    ImGui_ImplSDL3_ProcessEvent(&event);
}
```

Key simplifications:
- No `q` key interception — it goes through game input to the server
- No quaff letter handling — the server's menu drives the UI
- ESC is simply sent to the server when UI is open
- `QuaffMenu` mode disappears entirely

### 4.5 Quaff Flow (After Migration)

```
1. Player presses 'q'
2. Event loop: game input → send {"msg":"input","text":"q"} to server
3. Server sends {"msg":"menu","tag":"use_item","title":"Quaff which item?",...}
4. processMessages → UIManager::handleMessage("menu")
5. UIManager pushes UIEntry{Menu, "use_item", ...} onto stack
6. Next frame: UIManager::render() sees "use_item" tag, calls quaffMenu()
7. ImGui quaff popup renders (same window as now, using PlayerData::inv)
8. Player presses 'a' → game input sends {"msg":"input","text":"a"}
9. Server processes quaff, sends {"msg":"close_menu"} + player update
10. UIManager pops the menu, quaff window closes
```

No client-side mode needed. The server drives everything.

## 5. Implementation Plan

### Phase 1: UIManager Foundation

1. **Create `src/UIManager.hpp/cpp`** with the `UIManager` class and `UIStack`
   data structure.
2. **Register UIManager for `"menu"`, `"close_menu"`, `"close_all_menus"`,
   `"ui-push"`, `"ui-pop"`** messages in main.cpp.
3. **Implement stack push/pop** with proper game-input blocking logic.
4. **Unit tests**: Test stack push/pop, input blocking, tag lookup.

### Phase 2: Migrate Menu Handling

5. **Handle `"menu"` with `tag: "use_item"`**: When this menu arrives, push a
   `UIEntry{Menu, "use_item"}`.  The existing `quaffMenu()` ImGui function
   renders it.
6. **Stop intercepting `q`**: Remove the `q` key handler from the event loop.
   `q` goes through game input to the server.
7. **Update quaff letter handling**: When the user presses a letter during a
   `use_item` menu, send just the letter (not `q` + letter) to the server.
   The quaff animation triggers when the server responds with a player update
   showing decreased potion count (or we can trigger optimistically).
8. **Handle `"close_menu"`**: Pop the menu from the stack.

### Phase 3: Migrate Overlay Handling

9. **Handle `"ui-push"` with `type: "describe-item"`**: Push a UIEntry for
   the description.  Render `descriptionWindow()`.
10. **Handle `"ui-push"` with `type: "newgame-choice"`**: Already handled by
    WindowManager; move to UIManager.
11. **Handle `"ui-pop"`**: Pop the topmost overlay.

### Phase 4: Simplify WindowManager

12. **Remove `Mode::QuaffMenu`** — no longer needed.
13. **Simplify `handleKeyEvent`** — only handles F1 (overlay toggle), E
    (equipment toggle), and mode-specific ESC for QuitConfirm.
14. **Remove `enterQuaffMenu`/`cancelQuaffMenu`** methods.
15. **UIManager renders quaff menu** based on `"menu"` tag, not WindowManager mode.

### Phase 5: ESC and Input Cleanup

16. **ESC always goes to server when UI is stacked**: Single handler in event
    loop: if `!uiManager.empty()`, send `{"msg":"key","keycode":27}`.
17. **No more ESC special cases**: Description dismiss, quaff cancel, etc. all
    handled by server → UIManager stack pop.
18. **--more-- prompt handling**: Detect `"ui-push"` with MORE tag or
    `input_mode: MORE` and show a visible indicator (the JS client shows
    "--more--" in the message area).

### Phase 6: Generalize for Future Menus

19. **Add render functions for other menu tags**: `"inventory"` (InvMenu),
    `"actions"`, `"macros"`, etc.  Each can use `PlayerData::inv` or the
    menu's items array for rendering.
20. **Handle additional `ui-push` types**: `"progress-bar"`, `"formatted-scroller"`,
    etc. as needed.

## 6. Files to Create/Modify

| File | Change |
|------|--------|
| `src/UIManager.hpp` | **NEW** — UIManager class, UIEntry struct |
| `src/UIManager.cpp` | **NEW** — Stack management, message handling, rendering |
| `tests/test_ui_manager.cpp` | **NEW** — Unit tests for stack logic |
| `src/WindowManager.hpp` | Remove Mode::QuaffMenu, enterQuaffMenu, cancelQuaffMenu; simplify Mode enum |
| `src/WindowManager.cpp` | Remove quaff-specific methods; simplify handleKeyEvent |
| `src/main.cpp` | Remove `q` interception, quaff letter handling, description dismiss; wire UIManager; simplify event loop |
| `src/imguilayouts.hpp/cpp` | Move quaff rendering to UIManager; keep descriptionWindow, quaffMenu as helper functions |
| `CMakeLists.txt` | Add new source files |

## 7. Migration Path (Least Disruption)

The migration can be done incrementally:

1. **Create UIManager alongside existing code** — it starts with an empty stack.
2. **Migrate one message type at a time**: First `"menu"`, then `"ui-push"` overlays.
3. **Remove old code only after UIManager handles it**: Keep `QuaffMenu` mode
   working until UIManager reliably drives the quaff flow.
4. **Cleanup**: Remove deprecated WindowManager methods and event loop handlers.

At each step, the game remains playable because the old and new paths don't
conflict — UIManager only acts on messages it receives, and the old code only
acts on keypresses.

## 8. Open Questions

1. **Animation trigger timing**: Currently we trigger the quaff animation
   optimistically when the player selects a potion.  With server-driven flow,
   the quaff is confirmed when the server sends `close_menu` + player update.
   We can trigger the animation on `close_menu` (server confirmed) or continue
   triggering optimistically on keypress (client-side).  The JS client doesn't
   show quaff animations, so there's no reference behavior.

2. **Menu items vs PlayerData::inv**: The server's `menu` message includes item
   names and hotkeys.  Should we render from the menu data or from our cached
   `PlayerData::inv`?  Both should be in sync (the server just sent the menu
   after receiving our keypress).  Using `PlayerData::inv` is simpler — we
   already have the data structures.  The menu message serves only as a trigger.

3. **Multiple simultaneous menus**: The server can push multiple menus (e.g.,
   a quaff menu on top of an inventory menu).  Our `UIManager` stack handles
   this naturally — each `menu` message pushes a new entry.  Rendering shows
   only the topmost.  ESC pops the topmost.

4. **`close_all_menus`**: The server sends this on level changes or game resets.
   We should clear the entire UI stack.

5. **Spectator mode**: The JS client's `recv_ui_stack` handles initial UI stack
   sync for spectators.  We may need this for future spectator support.
