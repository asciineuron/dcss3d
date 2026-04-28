# Work Notes

## Current Status: Monster Data Parsing & Storage Complete

Monster data parsing and storage has been implemented on the `feature/monster-data` branch. The `GameMap` class now stores both tile data (`m_map`) and monster data (`m_monsters` + `m_monsterTable`), following the same pattern as the crawl webtiles JavaScript client's `merge_monster()` logic.

## Recent Work

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
- `src/GameMap.hpp`: `Monster` class, `m_monsters` + `m_monsterTable` members, `getMonsterAt()`
- `src/GameMap.cpp`: `Monster::merge()`, updated `updateMap()`, `cleanMonsterTable()`, updated `shift()`
- `src/imguilayouts.cpp`: Updated `displayMap()` with monster markers and list
- `tests/test_game_map.cpp`: 12 new monster test cases
- `CMakeLists.txt`: Added source files + dependencies to test target

## Architecture Notes

### Monster Data Flow
1. Server sends `map` message with `cells[]` containing optional `mon` field per cell
2. `GameMap::updateMap()` processes each cell:
   - `mon` object → merge into `m_monsterTable[id]`, set `m_monsters[pos] = id`
   - `mon: null` → erase from `m_monsters[pos]`
   - No `mon` field → leave unchanged
3. After all cells, `cleanMonsterTable()` removes any IDs with zero cell references
4. On `clear`, both structures are wiped

### Design Decisions
- **Global table approach**: Mirrors the JS client's `monster_table` keyed by `id`. Necessary because partial updates reference monsters by ID, not by position.
- **Single-position enforcement**: When a monster ID appears at a new position, any old position referencing that ID is removed. This matches server behavior where a monster can only be at one position.
- **No reference counting**: Unlike the JS client which uses `refs` for GC, we simply sweep for unreferenced IDs after each batch. Simpler, same result.
- **Fallback from old cell data**: When a new monster ID appears at a cell that previously had a different monster, we copy the old monster's fields as fallback for any missing fields in the partial update (mirrors JS `merge_objects(old_mon, mon)`).

### Future Work
- 3D rendering of monsters (deferred to followup ticket)
- Parse tile flags (`fg`) for damage level, poison, behavior, flying, etc.
- Monster icon overlays (berserk, summoned, hasted, etc.)
- Integration with mcache/sprite lookup for visual representation