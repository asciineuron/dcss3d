# DCSS Webtiles API Documentation

Comprehensive guide for building a new web client for Dungeon Crawl Stone Soup using the Webtiles WebSocket API.

## Table of Contents

1. [Overview](#overview)
2. [Connection Flow](#connection-flow)
3. [Authentication](#authentication)
4. [Message Protocol](#message-protocol)
5. [Client → Server Messages](#client--server-messages)
6. [Server → Client Messages](#server--client-messages)
7. [Game Data Structures](#game-data-structures)
8. [Map Rendering](#map-rendering)
9. [Tile System](#tile-system)
10. [State Management](#state-management)
11. [Code References](#code-references)

---

## Overview

The Webtiles server exposes a WebSocket API at `/socket` (or `/wss/` for HTTPS) that provides:
- Real-time game state updates
- Bidirectional communication with the running DCSS game process
- Spectator mode for watching other players
- Lobby and chat functionality

**Base URL**: `ws://host:port/socket` or `wss://host:port/socket`

**Server Code**: `webtiles/ws_handler.py`, `webtiles/process_handler.py`

---

## Connection Flow

```
┌─────────────┐     WebSocket      ┌─────────────────────┐     Unix Socket     ┌─────────────┐
│  Web Client │ ◄────────────────► │  Webtiles Server    │ ◄─────────────────► │ DCSS Game   │
└─────────────┘                    └─────────────────────┘                     └─────────────┘
     |                                    |                                          |
     |  JSON over WebSocket               |  JSON over Unix Socket                   |
     |  (deflate compressed)              |  (deflate compressed)                    |
     └────────────────────────────────────┴─────────────────────────────────────────┘
```

### Connection Steps

1. **Connect** to WebSocket endpoint
2. **Authenticate** (register/login or use cookie)
3. **Start game** (`play` message) or **watch** another player
4. **Receive** game state updates continuously
5. **Send** input commands during gameplay

---

## Authentication

### Registration

```json
{
  "msg": "register",
  "username": "newuser",
  "password": "securepassword",
  "email": "user@example.com"
}
```

**Response**: `login_success` (on success) or `register_fail` (on error)

### Login

```json
{
  "msg": "login",
  "username": "user",
  "password": "password"
}
```

**Response**: `login_success` or `login_fail`

### Token Login (Persistent Sessions)

1. After login, request a cookie:
```json
{ "msg": "set_login_cookie" }
```

2. Receive cookie:
```json
{ "msg": "login_cookie", "cookie": "user%20123456789", "expires": 3600 }
```

3. On subsequent connections:
```json
{ "msg": "token_login", "cookie": "user%20123456789" }
```

### Logout / Forget Cookie

```json
{ "msg": "forget_login_cookie", "cookie": "user%20123456789" }
```

**Code**: `webtiles/auth.py`, `webtiles/ws_handler.py:941-975`

---

## Message Protocol

All messages are JSON objects with a `msg` field indicating the message type. Additional fields carry the payload.

**Format**:
```json
{
  "msg": "<message_type>",
  "field1": "value1",
  "field2": "value2"
}
```

### Compression

Messages are compressed using WebSocket deflate-frame extension. The server supports:
- `deflate-frame` extension (preferred)
- `no-compression` subprotocol (fallback)
- Old WebSocket protocol 76 (no compression)

---

## Client → Server Messages

### Authentication & Account

| Message | Parameters | Description |
|---------|------------|-------------|
| `login` | `username`, `password` | Login with credentials |
| `token_login` | `cookie` | Login with persistent cookie |
| `set_login_cookie` | - | Request persistent login cookie |
| `forget_login_cookie` | `cookie` | Invalidate login cookie |
| `register` | `username`, `password`, `email` | Create new account |
| `forgot_password` | `email` | Request password reset email |
| `reset_password` | `token`, `password` | Complete password reset |
| `start_change_password` | - | Begin password change flow |
| `change_password` | `cur_password`, `new_password` | Change password |
| `start_change_email` | - | Begin email change flow |
| `change_email` | `email` | Change email address |

### Game Control

| Message | Parameters | Description |
|---------|------------|-------------|
| `play` | `game_id` | Start a new game (e.g., "crawl") |
| `go_lobby` | - | Return to lobby after game ends |
| `go_admin` | - | Go to admin interface |
| `watch` | `username` | Watch another player's game |

### Chat & Communication

| Message | Parameters | Description |
|---------|------------|-------------|
| `chat_msg` | `text` | Send chat message |

### Configuration

| Message | Parameters | Description |
|---------|------------|-------------|
| `get_rc` | `game_id` | Get player's rc file |
| `set_rc` | `game_id`, `rc` | Set player's rc file |

### Keepalive

| Message | Parameters | Description |
|---------|------------|-------------|
| `pong` | - | Respond to server ping |

**Code**: `webtiles/ws_handler.py:303-322` (message_handlers dict)

---

## Server → Client Messages

### Authentication Responses

| Message | Fields | Description |
|---------|--------|-------------|
| `login_success` | `username` | Login successful |
| `login_fail` | `reason?` | Login failed |
| `register_fail` | `reason` | Registration failed |
| `login_cookie` | `cookie`, `expires` | Persistent cookie issued |
| `login_required` | `game` | Must login to access game |
| `auth_error` | `reason` | Authentication error |
| `set_account_hold` | - | Account is restricted |

### Game State

| Message | Fields | Description |
|---------|--------|-------------|
| `game_started` | - | Game process started |
| `game_ended` | `reason`, `message`, `dump` | Game ended (saved, dead, won, etc.) |
| `game_client` | `version`, `content` | Game client HTML/JS |
| `go_lobby` | - | Return to lobby |
| `watching_started` | `username` | Spectator mode active |

### Lobby

| Message | Fields | Description |
|---------|--------|-------------|
| `lobby_clear` | - | Clear lobby list |
| `lobby_entry` | `id`, `name`, `game_id`, `v`, `vlong`, `race`, `cls`, `xl`, `title`, `place`, `god`, `turn`, `dur`, `idle_time`, `viewers` | Player entered lobby |
| `lobby_remove` | `id`, `reason`, `message`, `dump` | Player left lobby |
| `lobby_complete` | - | Lobby list complete |

### Chat & Notifications

| Message | Fields | Description |
|---------|--------|-------------|
| `chat` | `name`, `text` | Chat message |
| `server_announcement` | `text` | Server-wide announcement |
| `super_hide_chat` | - | Hide chat window |
| `toggle_chat` | - | Toggle chat visibility |

### Game Data Messages (Core Gameplay)

| Message | Fields | Description |
|---------|--------|-------------|
| `map` | `clear`, `player_on_level`, `vgrdc`, `cells` | Map update |
| `player` | (see [Player Stats](#player-stats)) | Player state update |
| `msgs` | `lines` | Message log update |
| `txt` | `id`, `lines` | Text area update |
| `cursor` | `x`, `y`, `type` | Cursor position |
| `options` | `watcher`, `options` | Game options |
| `ui-push` | `state` | Push UI state |
| `ui-pop` | - | Pop UI state |
| `ui-stack` | `stack` | Set UI stack |
| `ui-state` | `state` | Set UI state |
| `ui-scroller-scroll` | `id`, `scroll` | Scroller position |
| `ui_cutoff` | `id`, `cutoff` | UI cutoff |
| `input_mode` | `mode` | Input mode change |
| `version` | `version` | Game version |
| `layout` | `layout` | Layout change |
| `delay` | `ms` | Delay messages |

### Other

| Message | Fields | Description |
|---------|--------|-------------|
| `ping` | - | Keepalive ping |
| `html` | `id`, `content` | HTML content |
| `set_game_links` | `content` | Game selection links |
| `dump` | `url` | Dump file available |
| `close` | `reason` | Connection closing |
| `admin_log` | `text` | Admin log message |
| `admin_pw_reset_done` | `email_body`, `username`, `email`, `error?` | Admin password reset result |

---

## Game Data Structures

### Map Data (`msg: "map"`)

The map is sent as incremental updates. Each cell contains tile, monster, and field data.

```json
{
  "msg": "map",
  "clear": true,           // true = reset entire map first
  "player_on_level": true, // player is on this level
  "vgrdc": {"x": 10, "y": 15}, // view center
  "cells": [
    {
      "x": 10,
      "y": 15,
      "t": {                // Tile data
        "fg": [85, 0],      // Foreground tile [main_idx, player_idx]
        "bg": [0, 0],       // Background tile [floor_idx, wall_idx]
        "flags": {...},     // Tile flags (bitfield)
        "doll": [...],      // Doll parts for complex sprites
        "mcache": [...],    // Monster cache mapping
        "left_overlap": 0,  // Overlap with left cell
        "sy": 0             // Shift Y
      },
      "mon": {              // Monster (if present)
        "id": 123,
        "att": 0,           // Animation frame
        "refs": 1           // Reference count
      },
      "g": " ",             // Glyph (ASCII fallback)
      "col": 7,             // Color index
      "trans": false,       // Transparency flag
      "fg": {...},          // Foreground flags
      "bg": {...},          // Background flags  
      "cloud": {...},       // Cloud effect flags
      "icons": [],          // Icon overlays
      "flv": {              // Field values
        "f": 0,             // Field type
        "s": 0              // Field strength
      }
    }
  ]
}
```

#### Incremental Update Rules

Cells in the `cells` array are sent incrementally:
- If a cell has `x` and `y`, use those coordinates
- If missing, use `last_x + 1` and `last_y`
- This allows compact transmission of adjacent cells

**Code**: `game_data/static/map_knowledge.js:133-177` (merge function)

#### Map State Management

Your client must maintain:
1. **Full map grid** - Store all known cells
2. **Bounds tracking** - Track visible/explored area
3. **Dirty cells** - Track which cells need re-rendering
4. **Monster table** - Global monster registry by ID

```javascript
// Recommended state structure
const mapState = {
  cells: {},              // Key: "x,y", Value: cell data
  bounds: { left, top, right, bottom },
  dirty: [],              // Array of {x, y} to re-render
  monsters: {},           // Key: monster_id, Value: monster data
  playerOnLevel: false,
  viewCenter: {x, y}
};
```

**Code**: `game_data/static/map_knowledge.js`

### Player Stats (`msg: "player"`)

```json
{
  "msg": "player",
  "name": "PlayerName",
  "god": "Beogh",
  "title": "Fighter",
  "species": "Human",
  "hp": 45,
  "hp_max": 60,
  "real_hp_max": 60,
  "poison_survival": 0,
  "mp": 10,
  "mp_max": 25,
  "dd_real_mp_max": 25,
  "ac": 12,
  "ev": 8,
  "sh": 5,
  "xl": 5,
  "progress": 15,
  "time": 12345,
  "time_delta": 123,
  "gold": 500,
  "str": 16,
  "int": 10,
  "dex": 13,
  "str_max": 16,
  "int_max": 10,
  "dex_max": 13,
  "piety_rank": 3,
  "penance": false,
  "status": [],           // Status effects array
  "inv": {                // Inventory
    "a": {"name": "Dagger", "count": 1, ...},
    "b": {"name": "Potion", "count": 3, ...}
  },
  "weapon_index": -1,
  "offhand_index": -1,
  "quiver_item": -1,
  "unarmed_attack": "",
  "pos": {"x": 10, "y": 15},
  "wizard": 0,
  "explore": 0,
  "depth": 3,
  "place": "Dungeon",
  "contam": 0,
  "noise": 0,
  "adjusted_noise": 0
}
```

**Code**: `game_data/static/player.js:543-593`

### Message Log (`msg: "msgs"`)

```json
{
  "msg": "msgs",
  "lines": [
    "You hit the goblin for 5 damage.",
    "The goblin dies.",
    "You gain experience."
  ]
}
```

### Text Areas (`msg: "txt"`)

```json
{
  "msg": "txt",
  "id": "description",    // Area ID: description, info, etc.
  "lines": {
    "0": "A goblin stands here.",
    "1": "",
    "2": "It looks hostile."
  }
}
```

### Cursor (`msg: "cursor"`)

```json
{
  "msg": "cursor",
  "x": 10,
  "y": 15,
  "type": 0              // 0=mouse, 1=tutorial, 2=map
}
```

---

## Map Rendering

### Tile Coordinate System

- Grid coordinates: (x, y) starting from (0, 0) at top-left
- Viewport: Typically 80x24 characters (configurable)
- Map bounds can extend beyond viewport

### Cell Rendering Pipeline

```
1. Receive map message
   ↓
2. If clear=true, reset map state
   ↓
3. For each cell in cells array:
   - Merge with existing cell data
   - Mark cell as dirty
   - Update bounds if needed
   ↓
4. For each dirty cell:
   - Get tile indices (fg, bg)
   - Get flags (visibility, fog, etc.)
   - Draw background tile
   - Draw foreground tile
   - Draw monster (if present)
   - Apply effects (clouds, fields, icons)
   ↓
5. Clear dirty list
```

### Visibility Flags

Background flags determine cell visibility:
- `UNSEEN` - Never seen (black)
- `MM_UNSEEN` - Minimap unseen
- `FOG` - In fog of war (dimmed)
- `SEEN` - Currently visible

**Code**: `game_data/static/enums.js:100-150` (flag definitions)

### Overlap Handling

Some tiles overlap adjacent cells:
- `left_overlap` - Overlaps left cell
- `sy` - Shift Y (negative = overlaps cell above)

When rendering, check overlapping cells and render in correct order.

**Code**: `game_data/static/display.js:75-95`

---

## Tile System

### Tile Index Format

Tiles use multi-index arrays: `[main_index, player_index]`

- `main_index`: Index into main.png (features, objects, etc.)
- `player_index`: Index into player.png (player sprites)
- `floor_index`: Index into floor.png
- `wall_index`: Index into wall.png
- `feat_index`: Index into feat.png

### Texture Files

| File | Purpose |
|------|---------|
| `main.png` | Features, objects, items |
| `player.png` | Player character sprites |
| `floor.png` | Floor tiles |
| `wall.png` | Wall tiles |
| `feat.png` | Feature tiles |
| `gui.png` | GUI elements |
| `icons.png` | Icon overlays |

**Code**: `game_data/static/enums.js:32-39` (texture constants)

### Doll System

Complex sprites use "dolls" - arrays of tile parts:

```json
{
  "doll": [
    {"idx": 100, "x": 0, "y": 0},
    {"idx": 101, "x": 1, "y": 0},
    {"idx": 102, "x": 0, "y": 1}
  ]
}
```

Each doll part has:
- `idx` - Tile index
- `x`, `y` - Offset from base position

### Monster Cache (`mcache`)

Maps monster animation frames to doll parts:

```json
{
  "mcache": [
    [monster_frame_idx, doll_part_idx, x_offset, y_offset]
  ]
}
```

**Code**: `game_data/static/cell_renderer.js:320-345`

### Field Values (`flv`)

Fields represent environmental effects:

```json
{
  "flv": {
    "f": 1,    // Field type (fire, ice, poison, etc.)
    "s": 50    // Field strength (0-100)
  }
}
```

Field types are defined in the game's enums.

---

## State Management

### Required Client State

```javascript
const gameState = {
  // Authentication
  username: null,
  isLoggedIn: false,
  
  // Game connection
  isPlaying: false,
  isSpectating: false,
  watchedUsername: null,
  gameVersion: null,
  
  // Map
  map: {
    cells: {},           // Key: "x,y"
    bounds: null,        // {left, top, right, bottom}
    dirty: [],           // Cells to re-render
    monsters: {},        // Key: monster_id
    playerOnLevel: false,
    viewCenter: {x: 0, y: 0}
  },
  
  // Player
  player: {
    name: "",
    hp: 0, hp_max: 0,
    mp: 0, mp_max: 0,
    xl: 0,
    pos: {x: 0, y: 0},
    // ... (all player fields)
  },
  
  // UI
  layout: "tiles",
  uiState: "NORMAL",
  uiStack: [],
  
  // Messages
  messageLog: [],
  textAreas: {}          // Key: area_id, Value: lines
};
```

### Message Handler Registration

```javascript
const messageHandlers = {
  // Authentication
  "login_success": handleLoginSuccess,
  "login_fail": handleLoginFail,
  "login_cookie": handleLoginCookie,
  
  // Game control
  "game_started": handleGameStarted,
  "game_ended": handleGameEnded,
  "watching_started": handleWatchingStarted,
  
  // Lobby
  "lobby_clear": handleLobbyClear,
  "lobby_entry": handleLobbyEntry,
  "lobby_remove": handleLobbyRemove,
  "lobby_complete": handleLobbyComplete,
  
  // Game data
  "map": handleMapUpdate,
  "player": handlePlayerUpdate,
  "msgs": handleMessageUpdate,
  "txt": handleTextUpdate,
  "cursor": handleCursorUpdate,
  "options": handleOptions,
  
  // UI
  "ui-push": handleUiPush,
  "ui-pop": handleUiPop,
  "ui-stack": handleUiStack,
  "ui-state": handleUiState,
  "layout": handleLayout,
  "version": handleVersion,
  
  // Chat
  "chat": handleChatMessage,
  "server_announcement": handleAnnouncement,
  
  // Control
  "ping": handlePing,
  "go_lobby": handleGoLobby,
  "close": handleClose
};
```

### Input Handling

Send player input as raw bytes to the game:

```javascript
function sendInput(keyCode, modifiers = {}) {
  const input = {
    "msg": "input",
    "key": keyCode,
    "shift": modifiers.shift || false,
    "ctrl": modifiers.ctrl || false,
    "alt": modifiers.alt || false
  };
  socket.send(JSON.stringify(input));
}
```

**Note**: Input format may vary by game version. Check `game_data/static/key_conversion.js` for key mappings.

---

## Code References

### Server-Side (Python)

| File | Purpose | Key Lines |
|------|---------|-----------|
| `webtiles/ws_handler.py` | WebSocket handler, message routing | 303-322 (handlers), 941-1250 (message handlers) |
| `webtiles/process_handler.py` | Game process management | 192-202 (handle_process_message), 1096-1190 (socket messages) |
| `webtiles/terminal.py` | Game subprocess, PTY handling | Full file |
| `webtiles/connection.py` | Unix socket to game | Full file |
| `webtiles/auth.py` | Authentication, cookies | Full file |
| `webtiles/userdb.py` | User database API | Full file |
| `webtiles/config.py` | Server configuration | Full file |
| `webtiles/status.py` | HTTP status endpoints | Full file |
| `webtiles/game_data_handler.py` | Game data file serving | Full file |

### Client-Side (JavaScript)

| File | Purpose | Key Lines |
|------|---------|-----------|
| `game_data/static/display.js` | Map rendering | 61-100 (handle_map_message) |
| `game_data/static/map_knowledge.js` | Map state management | Full file |
| `game_data/static/cell_renderer.js` | Cell drawing | 220-400 |
| `game_data/static/player.js` | Player state | 543-593 (handlers) |
| `game_data/static/enums.js` | Constants, flags | Full file |
| `game_data/static/messages.js` | Message log | 148-150 (handlers) |
| `game_data/static/text.js` | Text areas | 58-60 (handlers) |
| `game_data/static/view_data.js` | Cursor, view | 86-88 (handlers) |
| `game_data/static/ui-layouts.js` | UI state | 1064-1072 (handlers) |
| `game_data/static/game.js` | Game state | 498-506 (handlers) |
| `game_data/static/menu.js` | Menu handling | 861+ (handlers) |
| `static/scripts/client.js` | Main client | Full file |
| `static/scripts/comm.js` | Message protocol | Full file |
| `static/scripts/key_conversion.js` | Key mappings | Full file |

### HTTP Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/status/lobby/` | GET | JSON array of active games |
| `/status/version/` | GET | Server version info |
| `/gamedata/<version>/<path>` | GET | Version-specific game data |

**Code**: `webtiles/status.py`, `webtiles/server.py:224-232`

---

## Implementation Checklist

### Phase 1: Connection & Authentication
- [ ] WebSocket connection to `/socket`
- [ ] Handle compression (deflate-frame)
- [ ] Implement login/register
- [ ] Implement token login with cookies
- [ ] Handle login responses

### Phase 2: Game Connection
- [ ] Send `play` message to start game
- [ ] Handle `game_started` / `game_ended`
- [ ] Implement `go_lobby` flow
- [ ] Implement `watch` for spectating

### Phase 3: Map Rendering
- [ ] Parse `map` messages
- [ ] Maintain map state (cells, bounds, dirty)
- [ ] Implement tile loading (PNG textures)
- [ ] Render background tiles
- [ ] Render foreground tiles
- [ ] Handle doll sprites
- [ ] Render monsters
- [ ] Handle visibility flags

### Phase 4: Player & UI
- [ ] Parse `player` messages
- [ ] Display player stats
- [ ] Parse `msgs` for message log
- [ ] Parse `txt` for text areas
- [ ] Handle `cursor` positioning
- [ ] Handle UI state messages

### Phase 5: Input & Interaction
- [ ] Keyboard input capture
- [ ] Send input to game
- [ ] Handle `options` message
- [ ] Implement chat (`chat_msg`)

### Phase 6: Polish
- [ ] Lobby display
- [ ] Chat display
- [ ] Error handling
- [ ] Reconnection logic
- [ ] Performance optimization

---

## Notes & Gotchas

1. **Message Queueing**: Server may queue messages and send `flush_messages` control message. Process queued messages only after flush.

2. **Large Map Messages**: Full map messages (`clear: true`) can be 100KB+. The server optimizes by sending these only once to new spectators.

3. **Incremental Updates**: Map cells use incremental coordinates. Track `last_x`, `last_y` when parsing.

4. **Monster References**: Monsters use reference counting. Clean up monsters with `refs: 0`.

5. **Tile Overlaps**: Check `left_overlap` and `sy` for tiles that extend into adjacent cells.

6. **Version Compatibility**: Different DCSS versions may have different message formats. Check `version` message.

7. **Compression**: Enable WebSocket deflate. Messages are compressed on the wire.

8. **Idle Timeouts**: Server has idle timeouts. Send `pong` in response to `ping`.

---

## Testing

### Local Development

1. Start DCSS with webtiles: `make WEBTILES=y`
2. Run server: `python3 webserver/server.py`
3. Connect: `ws://localhost:8080/socket`

### Debug Mode

Set in `config.py`:
```python
autologin = "testuser"  # Auto-login username
development_mode = True  # Enable debug
no_cache = True  # Disable caching
```

---

## External Resources

- DCSS Source: https://github.com/crawl/crawl
- Webtiles Server: This codebase (`webtiles/` directory)
- Tornado WebSocket: https://www.tornadoweb.org/en/stable/websocket.html

---

*Generated for building a new DCSS webtiles client. Last updated: 2026-04-16*
