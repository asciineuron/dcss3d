# Issue Resolution Report: Map Displacement and Collision Fix

## Problem Statement
When the player moved a full tile and new map data was received from the server, the existing 3D map cells were not adjusted. Since the server sends map data relative to the player's new position, the old cells remained in their previous absolute coordinates, causing the map to appear displaced. Additionally, players were immediately getting stuck upon map load due to faulty collision logic.

## Root Cause
1. **Missing Map Shift**: There was no mechanism to shift the `GameMap` in the opposite direction of a player's `MoveTurn` to maintain relative positioning.
2. **Incorrect Collision Logic**: `GameMap::wouldCollide` was using an `OR` condition for the X and Y axis checks. This meant a collision was triggered if *any* wall shared either the same X or Y coordinate as the player, regardless of the other axis.
3. **Incorrect Tile Dimensions**: `Player::handleMessage` was shifting the camera by `2.0f` units, but tiles were actually `1.0f` unit wide.
4. **Inverted Axis Logic**: Movement detection for North/South in `Player::updatePosition` and the corresponding shift in `GameMap::shift` were inverted.

## Impact
- The game map visually shifted incorrectly upon moving between tiles.
- The player would become "stuck" and unable to move as soon as the map was populated with walls.

## Resolution
- **Implemented Map Shifting**: Added `GameMap::shift(Direction moveDir)` which translates all existing tiles in the `unordered_map` in the opposite direction of the player's movement.
- **Integrated Shift into Main Loop**: Modified `main.cpp` to invoke `map.shift()` whenever a `MoveTurn` is successfully generated and sent to the server.
- **Fixed Collision Detection**: Updated `GameMap::wouldCollide` to use a proper AABB intersection check (`abs(dx) < 0.5 && abs(dy) < 0.5`).
- **Corrected North/South Logic**: Fixed the sign of Y-axis movement detection and shifting to align with the coordinate system (North = Y decrease).
- **Updated Camera Adjustment**: Changed the shift value in `Player::handleMessage` from `2.0f` to `1.0f` to match current tile sizes.

## Verification Results
- **Compilation**: Verified that the project compiles without errors.
- **Playtesting**: The user confirmed that these changes fixed the displacement and the "stuck" behavior, and movement is now working as expected.
