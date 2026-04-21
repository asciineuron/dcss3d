# Camera Pitch Clamping Issue

## Analysis
- **Problem**: The camera pitch (`phi`) is not clamped, allowing the player to look "past" straight up or down, causing the camera to flip.
- **Root Cause**: `Player::updateView` uses `wrap(m_camera.phi, 0, 2 * pi)` which allows a full $360^\circ$ rotation.
- **Impact**: Disorienting camera movement (flipping).

## Implementation Plan
1. Modify `src/PlayerState.cpp`:
    - Replace `m_camera.phi = wrap(m_camera.phi, 0, 2 * pi);` with `std::clamp`.
    - Use a range of roughly $[-\pi/2, \pi/2]$ for `phi`.
    - To avoid gimbal lock/flipping at the exact poles, use a small offset (e.g., $0.01$ radians).
    - Keep `m_camera.theta` wrapped as it's the yaw.
2. Verify that the camera no longer flips.

# Socket Connection Race Condition

## Analysis
- **Problem**: Game fails to start because it can't connect to the relay server's Unix domain socket.
- **Root Cause**: `NetworkManager` doesn't handle `ENOENT` (socket file not yet created) and has flawed retry logic.
- **Impact**: Inconsistent startup failure.

## Implementation Plan (Completed)
1. Updated `src/MessageQueue.cpp` to handle `ENOENT` and `ECONNREFUSED`.
2. Fixed retry logic to properly wait for success and avoid throwing on success.
3. Added `SDL_Delay` to the retry loop.

## Verification
- Game now starts consistently without "No such file or directory" error for the socket.
