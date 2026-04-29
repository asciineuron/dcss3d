# Work Notes

## Current Status: Keyboard Menu System (on `feature/keyboard-menus`)

Implemented WindowManager and equipment window. Awaiting playtest.

### Keyboard Menus (uncommitted)
- **WindowManager**: New singleton class managing window visibility modes (Normal, Overlay, Equipment)
  - `Normal`: No imgui windows, game active, WASD + mouse camera
  - `Overlay`: All windows visible (Escape toggle), game input paused
  - `Equipment`: Only equipment window visible (`e` key toggle), game input paused
- **Equipment Window**: Extracted from player window's "Inventory Details" child into standalone `displayEquipment()` window
  - Shows equipped summary (weapon, offhand, quiver, armor) and full inventory list
  - Removed "Inventory Details" toggle button from player window
- **`e` key**: Toggles equipment mode — shows only equipment window, pauses game
- **Escape key**: Exits equipment mode (returns to Normal), or toggles overlay mode
- **12 unit tests** in `tests/test_window_manager.cpp` (all pass)
- Layout save/load includes new "equipment" window

**Key Files:**
- `src/WindowManager.hpp` / `src/WindowManager.cpp`: Mode management singleton
- `src/imguilayouts.cpp`: `displayEquipment()`, updated `displayAllWindows()` gating
- `src/imguilayouts.hpp`: `displayEquipment()` declaration
- `src/main.cpp`: `e`/Escape key handling, mouse mode sync, game input gating via WindowManager
- `src/Renderer.cpp`: `doRender()` uses `WindowManager::shouldRenderUI()`
- `tests/test_window_manager.cpp`: 12 unit tests
- `CMakeLists.txt`: Added new source/test files

## Previous Status: Audio System (on `feature/audio-system`)

AudioManager with footfall + map_update sounds. Awaiting code review.

### Audio System (uncommitted)
- Created `AudioManager` subclass of `MessageHandler`
- `handleMessage()` triggers `"map_update"` sound on `"msg": "map"` server messages
- `onPlayerAction(const Turn&)` plays `"footfall"` for `MoveTurn`
- Integrated at all 4 `networkManager.sendMessage()` call sites in `main.cpp`
- Registered in handlerConfig for `"map"` messages
- Uses SDL3 audio: `SDL_OpenAudioDeviceStream` + `SDL_PutAudioStreamData` + `SDL_LoadWAV`
- Sound files: `resources/sounds/footfall.wav` (percussive step) + `resources/sounds/map_update.wav` (shimmer ping)
- WAVs cached by name after first load
- 7 unit tests (all pass), 19 assertions
- Testable via `triggeredSounds()` / `clearTriggeredSounds()`

**Key Files:**
- `src/AudioManager.hpp` / `src/AudioManager.cpp`: Audio manager implementation
- `src/main.cpp`: Integration at sendMessage call sites + handlerConfig registration
- `tests/test_audio_manager.cpp`: Unit tests
- `resources/sounds/footfall.wav` + `resources/sounds/map_update.wav`: Sound assets

## Recent Work

### Map Display on Reconnect Fix (2026-04-28)
- **Problem**: After login+play, map required 2-3 space bar presses to appear. Each press advanced the game turn.
- **Root cause**: The server's `add_watcher` for the primary player calls `send_message("spectator_joined")` to trigger `_send_everything()` → `_send_map(true)`, but it happens *before* the process connection opens, so `self.conn.open` is false and the message is silently dropped. Player data works because `_send_player()` has its own `_state_ever_synced` guard; the map has no equivalent.
- **Fix**: `main.cpp` sends `{"msg":"spectator_joined"}` after receiving `"game_started"`, when the connection is guaranteed open.

### Async Python Relay Rewrite
- Replaced sync `socketserver` + `select.poll()` with `asyncio` + `websockets.asyncio`
- Eliminates polling timeout workaround — asyncio event loop handles all I/O
- C++ side unchanged; still connects via sync Unix socket
- Two concurrent tasks: `cpp_to_dcss` (read C++ → forward to websocket) and `dcss_to_cpp` (read websocket → forward to C++)
- Reconnect support and file-based test mode preserved

### ImGui Pin & Mouse Fix
- Fixed mouse input: camera no longer moves when ImGui overlay is visible
- Added reusable `PinButton()` per-window toggle — pinned windows stay visible as read-only overlays when overlay is dismissed
- Refactored window display logic from `main.cpp` into `displayAllWindows()` in `imguilayouts.cpp`
- `Renderer::doRender()` now renders ImGui when pinned windows exist even if renderUI is false
- Swapped `||` to `&&` in mouse guard: `!renderer.renderUI() && !io.WantCaptureMouse`

## Recent Work

### Equipment Display & Batch Fix (uncommitted)
- Added `offhand_weapon`, `quiver_desc`, `unarmed_attack_colour` fields to `PlayerData`
- Added equipment section in `displayPlayer()`: weapon, offhand, quiver, fallback message
- Added debug logging for equipment field values
- **FIXED** `parseResponseMessages()` in `MessageQueue.cpp`: `"msgs"` batch path now correctly extracts `.value()` from key-value pairs (was pushing the full pair, wrapping messages in extra object layer and silently dropping them)
- Handles both `"msgs"` as object (integer keys → values) and as array

### Inventory Grid (committed: c4bd2ed)
- Replaced collapsible inventory list with compact 52-slot grid (4×13: a-m, n-z, A-M, N-Z)
- Empty slots dim grey, occupied slots gold with hover tooltip
- "Inventory Details" toggle button opens scrollable detailed list
- Removed unused `glm/gtx/string_cast.hpp` include

### Player State & UI (committed: 31f36cc)
- Added `PlayerData` struct and `InventoryItem` struct in `PlayerState.hpp` mirroring the upstream JS `player` object field-for-field
- Added `Player::handlePlayerMessage()` method that mirrors the JS `handle_player_message()` logic
- Registered `"player"` message type in handler config alongside existing `"map"`
- Rewrote `displayPlayer()` imgui window to show actual game stats:
  - Player identity, HP/MP bars, defenses, location, attributes, status effects
  - Collapsible Camera debug section
- Builds cleanly, all 83 existing assertions pass

### Monster Data (feature/monster-data)
- Added `Monster` class with `merge(const json&)` method for partial updates
- Added `m_monsters` (position→monster ID) and `m_monsterTable` (ID→Monster data) to `GameMap`
- Parse `mon` field from map cell JSON in `GameMap::updateMap()`
- Handle three cases: `mon` object (partial/full update), `mon: null` (removal), no `mon` field (no change)
- Monster ID uniqueness enforced: when a monster moves to a new cell, old position is automatically cleaned up
- `cleanMonsterTable()` removes unreferenced IDs after each message batch
- Monster positions shifted alongside tiles in `GameMap::shift()`
- Map `clear` also clears all monster data
- Debug overlay: `displayMap()` shows flashing `*` at monster positions and text list below grid
- 16 unit tests covering: Monster construction, merge parsing, partial updates, null removal, global table merging, shift, clear

**Key Files:**
- `src/PlayerState.hpp`: `InventoryItem`, `PlayerData` structs; `Player::data()` accessor; `handlePlayerMessage()` declaration
- `src/PlayerState.cpp`: `handlePlayerMessage()` implementation with inventory merge, time_delta calc, field extension, equipment debug logging
- `src/MessageQueue.cpp`: Fixed `parseResponseMessages()` batch path
- `src/imguilayouts.cpp`: Updated `displayPlayer()` with HP/MP bars, stats, equipment, inventory grid, details popup
- `src/main.cpp`: Added `"player"` to handlerConfig

## Architecture Notes

### Player Data Flow
1. Server sends `"player"` messages with partial or full player state
2. `Player::handleMessage()` dispatches to `handlePlayerMessage()` for `"player"` type
3. `handlePlayerMessage()` merges inventory slots, calculates time_delta, extends remaining fields via `setIf` lambda
4. `displayPlayer()` reads `PlayerData` and renders HP/MP bars, stats, attributes, status, equipment, inventory grid

### Batch Message Parsing
- Server sends initial full game state as `{"msgs": {"0": {...}, "1": {...}}}` (object with integer keys)
- `parseResponseMessages()` now correctly extracts values from the key-value pairs
- Subsequent messages arrive individually as `{"msg": "type", ...}`
- Both paths feed into `processMessages()` which dispatches by `message["msg"]` type
