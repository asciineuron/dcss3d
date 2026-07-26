# DCSS Webtiles Monster Data API — Complete Reference

## Overview

Monster data flows from the C++ game engine to the webtiles client via WebSocket JSON messages. The system uses two complementary mechanisms:
1. **Per-cell monster data** embedded in map cell updates (`mon` field)
2. **Global monster table** on the client side, keyed by unique `id`, with reference counting

---

## 1. Transport — How Monster Data is Sent

### 1.1 Map Cell Updates

Monster data is transmitted as part of a `"map"` message containing an array of cell updates. Each cell update is a position-indexed object:

```json
{
  "msg": "map",
  "clear": true,
  "cells": [
    { "x": 10, "y": 15, "mon": { "id": 42, "att": 0, "threat": 2 }, ... },
    { "x": 11, "y": 15, "mon": null, ... }
  ]
}
```

Key points:
- `"mon"` is a **partial update** — only changed fields are sent
- `"mon": null` means the monster was removed from that cell
- The client merges partial updates into its global `monster_table` via `merge_monster()`

### 1.2 Incremental Updates

The C++ side (`TilesFramework::_send_monster`) compares the current monster with the previous state (from `m_current_map_knowledge`) and only sends fields that changed. The `force_full` flag (set on initial map load or when no previous state exists) forces all fields to be sent.

---

## 2. Monster Object Structure (JSON)

The `mon` object in a cell update contains the following fields:

### 2.1 Core Fields

| Field | Type | Description |
|-------|------|-------------|
| `id` | `uint32` | Unique client-side monster ID. Persistent across moves. Used to track the same monster as it moves between cells. |
| `att` | `int` | Attitude (see [§3.1](#31-attitude-mon_attitude_type)) |
| `name` | `string` | Full name including proper name if applicable (e.g., "Kikubaa Kocho") |
| `plural` | `string` | Pluralised name (e.g., "imps") |
| `type` | `int` | Monster type enum index (e.g., `MONS_IMP`) |
| `typedata` | `object` | Type metadata (see [§2.2](#22-typedata-object)) |
| `btype` | `int` | Base monster type (for polymorphed/derived monsters, e.g., zombie base) |
| `threat` | `int` | Threat level (see [§3.2](#32-threat-level-mon_threat_level_type)) |
| `clientid` | `uint32` | Same as `id`; sent for named monsters as a tiebreaker for display sorting |

### 2.2 Typedata Object

Sent only when `type` changes (or on `force_full`):

```json
{
  "typedata": {
    "avghp": 14,
    "no_exp": false
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `avghp` | `int` | Average hit points for this monster type (from `mons_avg_hp()`) |
| `no_exp` | `bool` | `true` if killing this monster type gives no XP (from `mons_class_gives_xp()`) |

---

## 3. Enumerations

### 3.1 Attitude (`mon_attitude_type`)

| Value | Constant | Meaning |
|-------|----------|---------|
| 0 | `ATT_HOSTILE` | Hostile — will attack the player |
| 1 | `ATT_NEUTRAL` | Neutral — may attack or not |
| 2 | `ATT_OLD_STRICT_NEUTRAL` | (v34 only) Neutral, won't attack player |
| 3 | `ATT_GOOD_NEUTRAL` | Neutral but won't attack friendlies |
| 4 | `ATT_FRIENDLY` | Friendly — created friendly or tamed |
| 5 | `ATT_MARIONETTE` | Aphotic Marionette (transient, aligned with friendly) |

### 3.2 Threat Level (`mon_threat_level_type`)

| Value | Constant | Meaning |
|-------|----------|---------|
| 0 | `MTHRT_TRIVIAL` | Trivial threat |
| 1 | `MTHRT_EASY` | Easy threat |
| 2 | `MTHRT_TOUGH` | Tough threat |
| 3 | `MTHRT_NASTY` | Nasty threat |
| 4 | `MTHRT_UNDEF` | Undefined/unusual |

### 3.3 Damage Level (`mon_dam_level_type`)

This is encoded in the **foreground tile flags** (not in the `mon` object):

| Bits (in fg flags) | Level | Meaning |
|---|---|---|
| `MDAM_LIGHT` | Lightly damaged |
| `MDAM_MOD` | Moderately damaged |
| `MDAM_HEAVY` | Heavily damaged |
| `MDAM_SEV` | Severely damaged |
| `MDAM_ADEAD` | Almost dead |

---

## 4. Foreground Tile Flags (Monster-Related)

Monster visual state is encoded in the `fg` (foreground) tile flags of the cell, **not** in the `mon` object. These are packed into a 64-bit integer (represented as `[low32, high32]` array in JSON).

### 4.1 Attitude Flags (exclusive, 3 bits: `0x00030000`)

| Value | Flag | Meaning |
|-------|------|---------|
| `0x00010000` | `PET` | Friendly/pet |
| `0x00020000` | `GD_NEUTRAL` | Good neutral |
| `0x00030000` | `NEUTRAL` | Neutral |

### 4.2 Behaviour Flags (exclusive, 4 bits: `0x00700000`)

| Value | Flag | Meaning |
|-------|------|---------|
| `0x00100000` | `STAB` | Will stab (aware, hostile) |
| `0x00200000` | `MAY_STAB` | May stab (unaware but hostile) |
| `0x00300000` | `FLEEING` | Fleeing |
| `0x00400000` | `PARALYSED` | Paralysed |

### 4.3 Status Flags (individual bits)

| Bit | Flag | Meaning |
|-----|------|---------|
| `0x00040000` | `S_UNDER` | Something under the monster |
| `0x00080000` | `FLYING` | Flying/airborne |
| `0x00800000` | `NET` | Caught in a net |
| `0x01000000` | `WEB` | Caught in a web |

### 4.4 Poison Level (exclusive, 2 bits high: `[0, 0x18000000]`)

| Value | Flag | Meaning |
|-------|------|---------|
| `[0, 0x08000000]` | `POISON` | Poisoned |
| `[0, 0x10000000]` | `MORE_POISON` | More poisoned |
| `[0, 0x18000000]` | `MAX_POISON` | Max poisoned |

### 4.5 Threat Level (exclusive, 3 bits high: `[0, 0x60000000 \| highbit]`)

| Value | Flag | Meaning |
|-------|------|---------|
| `[0, 0x20000000]` | `TRIVIAL` | Trivial threat |
| `[0, 0x40000000]` | `EASY` | Easy threat |
| `[0, 0x60000000]` | `TOUGH` | Tough threat |
| `[0, highbit]` | `NASTY` | Nasty threat |
| `[0, 0x60000000 \| highbit]` | `UNUSUAL` | Unusual/undefined |

### 4.6 Ghost Flag

| Bit | Flag | Meaning |
|-----|------|---------|
| `[0, 0x00100000]` | `GHOST` | This is a ghost |

### 4.7 Damage Level (3 bits: `[0x40000000 \| highbit, 0x01]`)

| Value | Flag | Meaning |
|-------|------|---------|
| `[0x40000000, 0x00]` | `MDAM_LIGHT` | Lightly damaged |
| `[highbit, 0x00]` | `MDAM_MOD` | Moderately damaged |
| `[0x40000000 \| highbit, 0x00]` | `MDAM_HEAVY` | Heavily damaged |
| `[0x00000000, 0x01]` | `MDAM_SEV` | Severely damaged |
| `[0x40000000 \| highbit, 0x01]` | `MDAM_ADEAD` | Almost dead |

### 4.8 Demon Tier (3 bits high: `[0, 0x0E]`)

| Value | Flag | Meaning |
|-------|------|---------|
| `[0, 0x02]` | `DEMON_5` | Tier 5 demon |
| `[0, 0x04]` | `DEMON_4` | Tier 4 demon |
| `[0, 0x06]` | `DEMON_3` | Tier 3 demon |
| `[0, 0x08]` | `DEMON_2` | Tier 2 demon |
| `[0, 0x0E]` | `DEMON_1` | Tier 1 demon |

### 4.9 Foreground Flag Mask

The actual tile index is in the lower 16 bits: `fg.flags.mask = 0x0000FFFF`

---

## 5. Client-Side Monster Management

### 5.1 Global Monster Table (`monster_list.js`)

The client maintains a global `monster_table` object keyed by `id`:

```javascript
monster_table = {
  42: {
    id: 42,
    att: 0,
    name: "Kikubaa Kocho",
    plural: "Kikubaa Kocho",
    type: 123,
    typedata: { avghp: 100, no_exp: false },
    threat: 3,
    clientid: 42,
    refs: 1  // reference count
  },
  ...
};
```

### 5.2 Merge Process (`map_knowledge.merge_monster()`)

When a cell update arrives with `mon`:

1. **Decrement** the old monster's `refs` (if it had one)
2. **Lookup** the incoming monster by `id` in `monster_table`
3. If found: **merge** new fields into the existing entry
4. If not found: **create** new entry (copying from old cell monster if available)
5. **Increment** `refs` on the resulting entry
6. Store the merged monster in `entry.mon` on the cell
7. After processing all cells, **clean** entries with `refs === 0`

### 5.3 Monster List Display (`monster_list.js`)

The monster list pane:
- Collects all monsters from the current map knowledge
- Sorts by threat level, then name
- Shows name, attitude icon, threat indicator, and damage level
- Updates on every display cycle via `monster_list.update()`

---

## 6. Map Cell Structure (Complete)

A cell in the `map_knowledge` has this structure:

```javascript
{
  x: 10,
  y: 15,
  dirty: false,
  f: 12,           // dungeon feature enum
  mf: 3,           // minimap feature enum
  g: " ",          // glyph character
  col: 0x77,       // colour (highlight << 4 | macro_colour)
  flc: 0,          // flash colour
  fla: 0,          // flash alpha
  mon: { ... },    // merged monster data (or null)
  t: {             // tile rendering data
    fg: [0x1234, 0],    // foreground tile index + flags
    bg: [0x5678, 0],    // background tile index + flags
    cloud: [0, 0],      // cloud tile index + flags
    base: 0,            // base tile for items/corpses
    icons: [],          // icon overlay indices
    flv: { f: 0, s: 0 }, // flavour (floor, special)
    bloody: false,
    old_blood: false,
    silenced: false,
    halo: 0,
    highlighted_summoner: false,
    sanctuary: false,
    blasphemy: false,
    has_bfb_corpse: false,
    liquefied: false,
    orb_glow: 0,
    quad_glow: false,
    disjunct: false,
    mangrove_water: false,
    awakened_forest: false,
    blood_rotation: 0,
    travel_trail: 0,
    trans: false,       // transparency (for ghosts/submerged)
    // ... doll/mcache if applicable
  }
}
```

---

## 7. C++ Source Reference

### 7.1 Key Files

| File | Purpose |
|------|---------|
| `source/tileweb.cc` | `_send_monster()`, `_send_cell()`, `send_mcache()`, `send_doll()` |
| `source/mon-info.h` | `monster_info` and `monster_info_base` structures |
| `source/mon-util.h` | `mon_threat_level_type`, monster utility functions |
| `source/mon-attitude-type.h` | `mon_attitude_type` enum |
| `source/monster.h` | `monster` class (full game entity) |
| `source/map-cell.h` | `map_cell` structure |
| `source/tilemcache.cc` | Monster cache (mcache) system for complex tile rendering |
| `source/webserver/game_data/static/map_knowledge.js` | Client-side map knowledge and monster merge |
| `source/webserver/game_data/static/monster_list.js` | Client-side monster list pane |
| `source/webserver/game_data/static/enums.js` | Client-side enums and flag definitions |
| `source/webserver/game_data/static/cell_renderer.js` | Cell rendering including monster tiles |

### 7.2 `_send_monster()` Logic (tileweb.cc:1931)

```cpp
void TilesFramework::_send_monster(const coord_def &gc, const monster_info* m,
                                   map<uint32_t, coord_def>& new_monster_locs,
                                   bool force_full)
{
    json_open_object("mon");
    if (m->client_id)
    {
        json_write_int("id", m->client_id);
        json_treat_as_empty();
        new_monster_locs[m->client_id] = gc;
    }

    // Compare with last known state
    const monster_info* last = ...;
    if (last == nullptr)
        force_full = true;

    if (force_full || (last->full_name() != m->full_name()))
        json_write_string("name", m->full_name());

    if (force_full || (last->pluralised_name() != m->pluralised_name()))
        json_write_string("plural", m->pluralised_name());

    if (force_full || last->type != m->type)
    {
        json_write_int("type", m->type);
        json_open_object("typedata");
        json_write_int("avghp", mons_avg_hp(m->type));
        if (!mons_class_gives_xp(m->type))
            json_write_bool("no_exp", true);
        json_close_object();
    }

    if (force_full || last->attitude != m->attitude)
        json_write_int("att", m->attitude);

    if (force_full || last->base_type != m->base_type)
        json_write_int("btype", m->base_type);

    if (force_full || last->threat != m->threat)
        json_write_int("threat", m->threat);

    if (m->is_named())
        json_write_int("clientid", m->client_id);

    json_close_object(true);  // erase_if_empty = true
}
```

### 7.3 `monster_info` Structure (mon-info.h:315)

The `monster_info` struct extends `monster_info_base` and contains all data available for client serialization. Key fields:

| Field | Type | Description |
|-------|------|-------------|
| `pos` | `coord_def` | Position on the grid |
| `mb` | `FixedBitVector<NUM_MB_FLAGS>` | Monster boolean flags (150+ flags) |
| `mname` | `string` | Proper name (if any) |
| `type` | `monster_type` | Monster species enum |
| `base_type` | `monster_type` | Base type for derived monsters |
| `attitude` | `mon_attitude_type` | Attitude toward player |
| `threat` | `mon_threat_level_type` | Threat level |
| `dam` | `mon_dam_level_type` | Damage level |
| `hd` | `int` | Hit dice |
| `ac` | `int` | Armour class |
| `ev` | `int` | Evasion |
| `mr` | `int` | Magic resistance |
| `mresists` | `resists_t` | Resistances bitfield |
| `spells` | `monster_spells` | Spellbook |
| `props` | `CrawlHashTable` | Arbitrary properties |
| `client_id` | `mid_t` | Client-side unique ID |
| `inv[]` | `unique_ptr<item_def>` | Equipment (visible slots) |

Note: **Most of these fields are NOT sent to the client.** Only the fields in `_send_monster()` are transmitted. The rest are available for local tiles rendering.

---

## 8. Monster Flags (`monster_info_flags`)

The `mb` bitvector in `monster_info` has 150+ flags. These are **NOT sent directly** in the `mon` JSON object but are used to compute the **tile flags** and **icon overlays** sent via the `t` (tile) data. Key flags include:

| Flag | Purpose |
|------|---------|
| `MB_STABBABLE` | Monster can be backstabbed |
| `MB_BERSERK` | Berserk state |
| `MB_SLEEPING` | Sleeping |
| `MB_UNAWARE` | Unaware of player |
| `MB_WANDERING` | Wandering |
| `MB_HASTED` | Hasted |
| `MB_SLOWED` | Slowed |
| `MB_FLEEING` | Fleeing |
| `MB_CONFUSED` | Confused |
| `MB_INVISIBLE` | Invisible |
| `MB_POISONED` / `MB_MORE_POISONED` / `MB_MAX_POISONED` | Poison levels |
| `MB_SUMMONED` | Summoned creature |
| `MB_BURNING` | On fire |
| `MB_PARALYSED` | Paralysed |
| `MB_PETRIFIED` / `MB_PETRIFYING` | Petrified / petrifying others |
| `MB_FRENZIED` | Frenzied |
| `MB_SHAPESHIFTER` | Shapeshifter |
| `MB_TWO_WEAPONS` | Wielding two weapons |
| `MB_RANGED_ATTACK` | Has ranged attack |
| `MB_MINION` | Minion (Gozag) |
| `MB_UNREWARDING` | Unrewarding kill |
| `MB_VAMPIRE_THRALL` | Vampire thrall |
| ... | 100+ more flags |

---

## 9. Cell Update Protocol Details

### 9.1 Map Message Format

```json
{
  "msg": "map",
  "clear": true,              // optional: clear entire map
  "player_on_level": true,     // optional: player is on this level
  "vgrdc": { "x": 40, "y": 35 }, // optional: view grid center
  "cells": [
    // Each cell is a partial update. x/y can be omitted
    // if cells are in sequential grid order.
    { "x": 10, "y": 15, "f": 12, "g": " ", "col": 119, "mon": { "id": 42, "att": 0 }, "t": { "fg": [1234, 0], "bg": [5678, 0] } },
    { "f": 12, "g": " ", "col": 119, "mon": null, "t": { "fg": [0, 0] } },
    ...
  ]
}
```

### 9.2 Coordinate Compression

Cells in the `cells` array can omit `x`/`y` if they follow sequential grid order (row-major). The client tracks `merge_last_x` / `merge_last_y` and increments `x` for each cell without explicit coordinates.

### 9.3 Empty Object Erasure

The `json_close_object(true)` call in `_send_monster()` causes the JSON object to be **erased entirely** if no fields were written (all values matched the previous state). This means a cell update might have no `mon` field at all if the monster's state didn't change.

---

## 10. Summary for Custom Client Development

### What you receive per monster:
- **`mon.id`** — unique persistent ID (use this to track monsters)
- **`mon.att`** — attitude (0=hostile, 1=neutral, 3=good-neutral, 4=friendly, 5=marionette)
- **`mon.name`** — full display name (use this for UI)
- **`mon.plural`** — plural name
- **`mon.type`** — monster type enum index (use for tile lookups, mcache, typedata)
- **`mon.typedata.avghp`** — average HP
- **`mon.typedata.no_exp`** — no XP flag
- **`mon.btype`** — base type (for zombies, draconians, etc.)
- **`mon.threat`** — threat level (0-4)
- **`mon.clientid`** — same as id (for named monsters)

### Understanding `name` vs `type` vs `btype`

| Field | Purpose | Example |
|-------|---------|---------|
| `name` | **Display name** for UI | "Gnoll Sergeant" |
| `type` | **Monster type enum** for tile/mcache lookup | `123` (the `MONS_*` index) |
| `btype` | **Base type** for derived/polymorphed monsters | `118` (MONS_GNOLL) |

**Key insight:** You almost never need to map `type` → `name` yourself. The server sends `name` alongside `type` in every monster update. Use `name` for all display purposes.

**When to use `type`:**
- Looking up sprites/tiles via the `mcache` system
- Accessing `typedata` (avghp, no_exp)
- Determining monster category (e.g., check if `type >= MONS_DRACONIAN && type <= MONS_LAST_DRACONIAN`)

**When to use `btype`:**
- A zombie has `type=MONS_ZOMBIE` but `btype` tells you what it was before dying
- Draconian spawns have a base colour but a specialized type

```json
// Example monster update
{
  "id": 42,
  "type": 123,           // Gnoll Sergeant
  "btype": 118,          // Base type: MONS_GNOLL
  "name": "Gnoll Sergeant",
  "att": 0,              // hostile
  "threat": 2            // tough
}
```

### What is NOT in `mon` but in the cell's `t` (tile) data:
- **Damage level** (lightly/moderately/heavily/severely/almost dead) — in `fg` flags
- **Poison level** — in `fg` flags
- **Behaviour** (stabbing/fleeing/paralysed) — in `fg` flags
- **Net/web** status — in `fg` flags
- **Flying** — in `fg` flags
- **Ghost** indicator — in `fg` flags
- **Demon tier** — in `fg` flags
- **Icon overlays** — in `t.icons` array (berserk, summoned, hasted, etc.)

### Client-side integration:
1. On `"map"` message, merge each cell's `mon` into your monster table by `id`
2. Use `merge_monster()` pattern: decrement old refs, merge fields, increment new refs
3. Clean entries with `refs === 0` after each update batch
4. Parse `fg` flags from cell `t` data for visual state (threat, damage, poison, etc.)
5. Use `enums.prepare_fg_flags()` to decode the packed flag integers

---

## 11. Version Negotiation

### The `version` Message

The server sends a standalone `version` message after connection (before the first `map`):

```json
{
  "msg": "version",
  "text": "Dungeon Crawl Stone Soup 0.34.2"
}
```

- **Format:** `{"msg":"version","text":"..."}`
- **Timing:** Sent early in connection, before `game_client` and game data
- **`text` field:** Human-readable version string (game title + version)

### Why Version Matters for Your Client

Since you're reverse-engineering without being in-sync with the crawl repo, version checking is critical:

1. **Monster type enum values shift between versions** — `MONS_IGUANA` is 4 in v34, but the count/position of entries changes in v35+
2. **Message formats may change** — new fields added, old ones removed
3. **Tile/texture mappings change** — `mcache` indices shift

### Recommended Version Handling

```javascript
const SUPPORTED_PREFIXES = [
  "Dungeon Crawl Stone Soup 0.34",
  "Dungeon Crawl Stone Soup 0.35"
];

function handle_version(data) {
  const version_str = data.text; // e.g., "Dungeon Crawl Stone Soup 0.34.2"
  
  const is_supported = SUPPORTED_PREFIXES.some(prefix => 
    version_str.startsWith(prefix)
  );
  
  if (!is_supported) {
    console.error(`Unsupported version: ${version_str}`);
    // Gracefully exit or show incompatibility warning
    show_version_mismatch(version_str);
    return false;
  }
  
  // Store for later use (tile lookups, enum mappings, etc.)
  game_version = version_str;
  return true;
}
```

### Version Detection Checklist

| Version String Prefix | Notes |
|----------------------|-------|
| `0.34.x` | TAG_MAJOR_VERSION = 34 |
| `0.35.x` | TAG_MAJOR_VERSION = 35 |

The version string format is: `Dungeon Crawl Stone Soup <major>.<minor>.<patch>`

Check the major version to determine which monster enum definition to use for tile lookups.

---

## 12. Monster Type Reference

The full monster type enum is defined in `source/monster-type.h`. Values range from 0 (`MONS_PROGRAM_BUG`) to `NUM_MONSTERS - 1`. Key categories:

| Range | Category |
|-------|----------|
| 0-150 | Animals (lizards, mammals, insects) |
| 100-200 | Arthropods (spiders, beetles, moths) |
| 150-350 | Humanoids (goblins, orcs, elves, ogres, trolls, giants) |
| 350-420 | Demons |
| 420-460 | Elementals and spiritual beings |
| 460-490 | Abyssals |
| 490-580 | Undead |
| 580-610 | Holy beings |
| 610+ | Unique monsters |

**Special sentinel values (not real monsters):**
- `MONS_NO_MONSTER = 1000`
- `RANDOM_MONSTER = 2000+`
- `WANDERING_MONSTER = 3500`
