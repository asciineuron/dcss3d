# Sprite System Design v2

## 1. Problem & Goals

We need a general-purpose sprite animation system for the 3D game client. Sprites are
textured quads that play named animation clips — sequences of frames from a texture atlas.
They can be positioned in screen-space (first-person weapon, HUD effects) or world-space
(traveling projectiles, area effects).

The system must handle:
- **First-person weapon display** — equipped weapon shown in lower screen, idle/swing
  animations, changing based on equipment
- **Attack/action animations** — discrete one-shot animations triggered by player actions
  (weapon swing, spell cast, potion quaff, level-up)
- **World-space effects** — (future) traveling projectiles, area auras positioned on the
  game map, with billboarding and depth testing
- **Transform modifiers** — running animations can have their scale, rotation, color, or
  alpha modified externally (shrinking projectile, rotating zap, fading aura)

## 2. Core Abstractions

### 2.1 AnimationClip

An `AnimationClip` is a named, reusable sequence of frames. It is pure data — no GPU
state, no rendering logic. Think of it as a "template" for what to display.

```cpp
struct AnimationFrame {
    float u, v;       // UV top-left in atlas texture (normalized 0–1)
    float w, h;       // UV extent
    uint32_t durationMs; // how long this frame is shown
};

struct AnimationClip {
    std::string name;              // lookup key, e.g. "weapon_sword_idle"
    std::vector<AnimationFrame> frames;
    bool loop = true;              // true = repeat, false = play-once then hold last frame
    uint32_t totalDurationMs;      // cached sum of frame durations (derived)
};
```

**Key property**: An `AnimationClip` does NOT know about position, scale, or rendering.
It only defines "what frames exist and how long each lasts."

### 2.2 SpriteAtlas

A `SpriteAtlas` is a GPU texture plus a catalog of named `AnimationClip`s. Multiple
animations share the same texture for efficient batching.

```cpp
class SpriteAtlas {
public:
    // Load a texture + animation definitions from a JSON descriptor.
    // JSON format: { "texture": "weapons.png", "animations": { ... } }
    explicit SpriteAtlas(SDL_GPUDevice*, const std::string& jsonPath);

    const AnimationClip* findAnimation(std::string_view name) const;
    SDL_GPUTexture* texture() const;
    SDL_GPUSampler* sampler() const;

    size_t animationCount() const;
    bool hasAnimation(std::string_view name) const;

private:
    SDL_GPUTexture* m_texture;
    SDL_GPUSampler* m_sampler;
    std::unordered_map<std::string, AnimationClip> m_clips;
};
```

**Atlas descriptor JSON format:**
```json
{
  "texture": "weapons_atlas.png",
  "animations": {
    "weapon_sword_idle": {
      "loop": true,
      "frames": [
        { "u": 0.0, "v": 0.0, "w": 0.25, "h": 0.5, "duration_ms": 500 },
        { "u": 0.25, "v": 0.0, "w": 0.25, "h": 0.5, "duration_ms": 500 }
      ]
    },
    "weapon_sword_swing": {
      "loop": false,
      "frames": [
        { "u": 0.0, "v": 0.5, "w": 0.2, "h": 0.5, "duration_ms": 40 },
        { "u": 0.2, "v": 0.5, "w": 0.2, "h": 0.5, "duration_ms": 40 },
        { "u": 0.4, "v": 0.5, "w": 0.2, "h": 0.5, "duration_ms": 40 },
        { "u": 0.6, "v": 0.5, "w": 0.2, "h": 0.5, "duration_ms": 40 },
        { "u": 0.8, "v": 0.5, "w": 0.2, "h": 0.5, "duration_ms": 40 }
      ]
    },
    "spell_magic_dart": {
      "loop": false,
      "frames": [
        { "u": 0.0, "v": 0.75, "w": 0.125, "h": 0.25, "duration_ms": 60 },
        { "u": 0.125, "v": 0.75, "w": 0.125, "h": 0.25, "duration_ms": 60 },
        { "u": 0.25, "v": 0.75, "w": 0.125, "h": 0.25, "duration_ms": 60 }
      ]
    }
  }
}
```

### 2.3 SpriteInstance

A `SpriteInstance` is one active sprite being rendered. It tracks playback state and
spatial properties.

```cpp
enum class SpriteSpace {
    Screen,  // NDC coordinates, orthographic projection, no depth test
    World    // (future) 3D world coordinates, perspective projection, depth-tested
};

struct SpriteTransform {
    float posX = 0.0f, posY = 0.0f;  // NDC for Screen; world XZ for World
    float posZ = 0.0f;               // depth for Screen; world Y for World
    float scaleX = 1.0f, scaleY = 1.0f;
    float rotation = 0.0f;           // radians
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
};

// GPU-side struct — must match shader layout
struct GpuSpriteData {
    float posX, posY, posZ;
    float rotation;
    float scaleX, scaleY;
    float padding_a, padding_b;  // alignment
    float texU, texV, texW, texH;
    float r, g, b, a;
};

class SpriteInstance {
public:
    SpriteInstance(const AnimationClip* clip, SpriteSpace space,
                   const SpriteTransform& transform);

    // Advance playback by dt seconds. Returns true if a play-once clip
    // completed this tick (for the caller to react).
    bool tick(float dt);

    // Current frame's UV + color packed into GpuSpriteData.
    // Caller reads this each frame to build the GPU upload buffer.
    GpuSpriteData gpuData() const;

    // Mutators for external systems to modify a running sprite.
    void setTransform(const SpriteTransform& t);
    void setClip(const AnimationClip* clip, bool restart = true);
    void setSpace(SpriteSpace space);

    // Queries
    const AnimationClip* clip() const { return m_clip; }
    SpriteSpace space() const { return m_space; }
    const SpriteTransform& transform() const { return m_transform; }
    bool isComplete() const { return m_complete; }
    uint32_t currentFrame() const { return m_currentFrame; }
    float elapsedTime() const { return m_elapsed; }

private:
    const AnimationClip* m_clip;
    SpriteSpace m_space;
    SpriteTransform m_transform;
    float m_elapsed = 0.0f;     // seconds since animation started
    uint32_t m_currentFrame = 0;
    bool m_complete = false;    // true when a non-looping clip finishes

    void recalcFrame();
};
```

### 2.4 SpriteManager

The `SpriteManager` owns atlases and instances, orchestrates per-frame update and
rendering.

```cpp
using SpriteHandle = uint32_t;
constexpr SpriteHandle INVALID_SPRITE = 0;

class SpriteManager {
public:
    SpriteManager(SDL_GPUDevice*, SDL_Window*);

    // --- Asset management ---
    void loadAtlas(const std::string& jsonPath);
    const SpriteAtlas* atlas(size_t index) const;

    // --- Instance lifecycle ---
    // Play a named animation. Returns a handle for later manipulation.
    // If an existing instance is already at this "slot", it is replaced.
    SpriteHandle play(std::string_view animationName,
                      SpriteSpace space,
                      const SpriteTransform& transform);

    // Stop and remove an instance.
    void stop(SpriteHandle handle);

    // Modify a running instance (e.g., shrink a projectile, fade out).
    void setTransform(SpriteHandle handle, const SpriteTransform& t);

    // Swap the animation clip on a running instance without changing
    // its position/transform. Used for weapon idle→swing→idle transitions.
    void setAnimation(SpriteHandle handle, std::string_view animationName);

    // Query whether a play-once animation has finished.
    bool isComplete(SpriteHandle handle) const;

    // --- Per-frame ---
    // Advance all instances. Call once per game tick.
    void update(float dt);

    // Upload sprite data to GPU and draw. Called from the render pass.
    void draw(SDL_GPURenderPass* renderPass, SDL_GPUCommandBuffer* cmdBuf);

    // --- Batching ---
    // Set the maximum number of simultaneous sprite instances.
    void setCapacity(size_t maxSprites);

private:
    SDL_GPUDevice* m_device;
    SDL_Window* m_window;

    std::vector<std::unique_ptr<SpriteAtlas>> m_atlases;
    std::vector<SpriteInstance> m_instances;       // indexed by handle-1
    std::vector<bool> m_instanceActive;
    std::vector<GpuSpriteData> m_gpuData;          // CPU-side staging for upload
    SpriteHandle m_nextHandle = 1;

    // GPU resources for rendering
    SDL_GPUGraphicsPipeline* m_screenPipeline;     // screen-space sprite pipeline
    SDL_GPUBuffer* m_spriteBuffer;                 // GPU storage buffer for GpuSpriteData
    SDL_GPUTransferBuffer* m_spriteTransferBuf;

    void createPipeline();
    void uploadGpuData(SDL_GPUCommandBuffer* cmdBuf);
    SpriteAtlas* findAtlasFor(std::string_view animationName) const;
};
```

### 2.5 Data Flow

```
┌──────────────┐     ┌─────────────────┐     ┌──────────────┐
│  game code   │────>│  SpriteManager  │────>│    GPU       │
│ (main.cpp,   │     │                 │     │              │
│  PlayerState,│     │ m_instances[]   │     │ spriteBuffer │
│  Turn)       │     │ m_gpuData[]     │     │ pipeline     │
│              │     │                 │     │ atlasTexture │
│ play()       │     │ update(dt)      │     │              │
│ setAnim()    │     │ draw(pass)      │     │              │
│ setTransform │     │                 │     │              │
│ stop()       │     └─────────────────┘     └──────────────┘
└──────────────┘
```

**Per-frame sequence:**
1. `SpriteManager::update(dt)` — advance all active instances' elapsed time,
   recompute current frame. Mark completed non-looping instances.
2. Game code checks `isComplete()` and reacts (e.g., restart idle animation
   after swing finishes).
3. `SpriteManager::draw(renderPass, cmdBuf)` — for each active instance,
   compute `GpuSpriteData` from current frame + transform, upload to GPU buffer,
   bind pipeline + atlas texture, issue draw call.

## 3. Screen-Space vs World-Space

### 3.1 Current: Screen-Space Only

All sprites are rendered in a **separate render pass** after the 3D scene, using an
orthographic projection (NDC coordinates). No depth testing. Alpha blending enabled.

- Weapon sprite: positioned in lower-center of screen, eg. `(0.5, -0.3)` in NDC
- HUD effects: positioned anywhere on screen

### 3.2 Future: World-Space Migration Path

When we need world-space sprites (traveling projectiles, area auras on map tiles):

1. **Add `SpriteSpace::World`** to the enum.

2. **Split rendering** into two passes:
   - `drawWorld(renderPass)` — called during the 3D scene pass. Uses perspective
     projection, depth testing. Vertex shader billboards quads to face the camera
     (same technique as the existing monster vertex shader `position_monster.vert.slang`).
   - `drawScreen(renderPass)` — called in the screen-space pass (current behavior).

3. **World-space SpriteTransform semantics:**
   - `posX, posZ` = world XZ (map coordinates, same as `mapCoordToRender`)
   - `posY` = world Y (height above floor)
   - `scaleX, scaleY` = world-space size (meters)
   - Perspective projection handles distance-based shrinking automatically

4. **For traveling effects** (e.g., magic dart flying across the map):
   - Create a World-space instance at the caster's position
   - Each frame, lerp its position toward the target
   - Optionally shrink scale over time for a "fading into distance" effect
   - When `isComplete()` or position reaches target, stop it

5. **For area effects** (e.g., poison cloud at a map cell):
   - Create a World-space instance at the cell's render coordinates
   - Optionally set `posY` slightly above the floor
   - Play a looping or one-shot animation
   - Scale determines the area size

### 3.3 Shader Strategy

We need two vertex shaders (or one with a branch/constant):
- **Screen-space**: simple NDC transform, no billboarding
- **World-space**: billboard toward camera, apply viewproj matrix, depth test

Both share the same fragment shader (texture sample with alpha blending).

For the initial implementation, only the screen-space shader is needed.

## 4. How Game Code Uses the Sprite System

### 4.1 Weapon Display

```cpp
// In Player or main.cpp, after receiving player data:

// When equipment changes:
void onWeaponChanged(const std::string& weaponName) {
    std::string clipName = weaponToClipName(weaponName); // "weapon_sword_idle"
    if (m_weaponHandle != INVALID_SPRITE) {
        m_spriteManager->setAnimation(m_weaponHandle, clipName);
    } else {
        SpriteTransform t;
        t.posX = 0.5f; t.posY = -0.35f;  // lower-center of screen
        t.scaleX = 0.4f; t.scaleY = 0.4f;
        m_weaponHandle = m_spriteManager->play(clipName, SpriteSpace::Screen, t);
    }
}

// When attack is triggered:
void onAttack() {
    std::string swingName = weaponToSwingClipName(m_currentWeapon);
    m_spriteManager->setAnimation(m_weaponHandle, swingName);
}

// In the main loop, after updating:
if (m_weaponHandle != INVALID_SPRITE && m_spriteManager->isComplete(m_weaponHandle)) {
    // Swing finished — go back to idle
    std::string idleName = weaponToClipName(m_currentWeapon);
    m_spriteManager->setAnimation(m_weaponHandle, idleName);
}
```

### 4.2 One-Shot Effects

```cpp
// Spell cast:
void onSpellCast(const std::string& spellName) {
    SpriteTransform t;
    t.posX = 0.5f; t.posY = 0.0f;
    t.scaleX = 0.6f; t.scaleY = 0.6f;
    SpriteHandle h = m_spriteManager->play("spell_" + spellName, SpriteSpace::Screen, t);
    m_effectHandles.push_back(h); // track for cleanup
}

// In main loop:
for (auto it = m_effectHandles.begin(); it != m_effectHandles.end(); ) {
    if (m_spriteManager->isComplete(*it)) {
        m_spriteManager->stop(*it);
        it = m_effectHandles.erase(it);
    } else {
        ++it;
    }
}
```

### 4.3 Future: World-Space Projectile

```cpp
void onProjectileFired(glm::vec3 from, glm::vec3 to, const std::string& type) {
    SpriteTransform t;
    t.posX = from.x; t.posY = from.y; t.posZ = from.z;
    t.scaleX = 0.2f; t.scaleY = 0.2f;
    SpriteHandle h = m_spriteManager->play("proj_" + type, SpriteSpace::World, t);
    // Store handle + trajectory, update position each frame
}
```

## 5. Weapon/Animation Name Mapping

The system is animation-driven, not weapon-driven. The mapping from game state to
animation name is a concern of the game code, not the sprite system.

```cpp
// In a utility function or lookup table:
std::string weaponToClipName(const PlayerData& data) {
    if (data.weapon_index == -1) {
        // Unarmed — use unarmed_attack field
        if (data.unarmed_attack.find("Claw") != std::string::npos)
            return "weapon_claw_idle";
        return "weapon_unarmed_idle";
    }

    auto it = data.inv.find(data.weapon_index);
    if (it == data.inv.end())
        return "weapon_unarmed_idle";

    const std::string& name = it->second.name;
    // Map known weapon names to animation prefixes
    static const std::unordered_map<std::string, std::string> prefixMap = {
        {"dagger", "weapon_dagger"},
        {"short sword", "weapon_shortsword"},
        {"long sword", "weapon_longsword"},
        {"hand axe", "weapon_axe"},
        {"mace", "weapon_mace"},
        {"club", "weapon_club"},
        {"spear", "weapon_spear"},
        // ... etc
    };

    // Case-insensitive prefix match
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    for (const auto& [key, prefix] : prefixMap) {
        if (lower.find(key) != std::string::npos)
            return prefix + "_idle";
    }
    return "weapon_default_idle";
}
```

The `_idle` and `_swing` suffixes are convention. The atlas JSON defines the actual
clips. If a weapon doesn't have a swing animation, it falls back to a default swing.

## 6. Integration with Existing Code

### 6.1 What Changes

| File | Change |
|------|--------|
| `src/SpriteManager.hpp` | **NEW** — SpriteManager, SpriteInstance, SpriteAtlas, AnimationClip, SpriteTransform, GpuSpriteData |
| `src/SpriteManager.cpp` | **NEW** — Full implementation |
| `src/Renderer.hpp` | Add `SpriteManager` member, remove old `SpriteModel`/`Texture` stubs, remove `m_spriteModelCache` |
| `src/Renderer.cpp` | Add sprite render pass call in `doRender()` |
| `src/Turn.hpp` | Remove `SpriteManager` class and `SpriteModel` forward decl (moved to SpriteManager.hpp). Keep `AttackTurn::triggerAnimation()` declaration, keep `SpecialEffect` enum. |
| `src/PlayerState.hpp` | Already has `setSpriteManager()`. Add comment about weapon→animation mapping. |
| `src/PlayerState.cpp` | In `handlePlayerMessage()`, detect weapon_index change, notify via callback. |
| `src/main.cpp` | Create `SpriteManager`, load atlases, wire per-frame update/draw, handle weapon changes, handle attack triggers. |
| `shaders/sprite.vert.slang` | **NEW** — Screen-space sprite vertex shader (PullSpriteBatch-style) |
| `shaders/sprite.frag.slang` | **NEW** — Texture sample + alpha fragment shader |
| `resources/weapons_atlas.png` | **NEW** — Placeholder sprite sheet |
| `resources/weapons_atlas.json` | **NEW** — Animation clip definitions |
| `CMakeLists.txt` | Add new source files and shaders |

### 6.2 Render Pipeline Integration

```
doRender():
  1. Acquire command buffer + swapchain texture
  2. pushMapToGPU / pushMonsterToGPU / pushObjectsToGPU (copy passes)
  3. ImGui PrepareDrawData
  4. ┌─ 3D Scene Pass (depth-tested) ─────────────────┐
     │  Draw map cubes, monsters, objects, skybox       │
     │  (future: SpriteManager::drawWorld() here)       │
     └─────────────────────────────────────────────────┘
  5. ┌─ Sprite Pass (no depth, orthographic) ──────────┐  ← NEW
     │  SpriteManager::drawScreen(renderPass, cmdBuf)   │
     │  Bind screen-space pipeline, upload sprite data   │
     │  Draw N instances * 6 vertices                    │
     └─────────────────────────────────────────────────┘
  6. ┌─ ImGui Pass (no depth) ─────────────────────────┐
     │  ImGui_ImplSDLGPU3_RenderDrawData                │
     └─────────────────────────────────────────────────┘
  7. Submit command buffer
```

### 6.3 Main Loop Integration

```cpp
// In main.cpp, before the while loop:
SpriteManager spriteManager(renderer.gpu_device(), renderer.window());
spriteManager.loadAtlas("resources/weapons_atlas.json");
player.setSpriteManager(&spriteManager);

// In the while loop, after game state updates and before doRender:
spriteManager.update(gameTime.dt());
// Check weapon swing completion, restart idle, etc.
// Check effect completion, cleanup, etc.

// doRender now takes SpriteManager&:
renderer.doRender(map, player.camera(), {player.data().pos_x, player.data().pos_y},
                  spriteManager);
```

## 7. TDD Implementation Plan (Red/Green)

### Test File: `tests/test_sprite_manager.cpp`

We test in order of dependency: AnimationClip → SpriteInstance → SpriteAtlas → SpriteManager.

No GPU is needed for these tests — we test the pure logic. GPU rendering is validated
via manual playtesting.

#### Test Suite 1: AnimationClip

| Test | RED (failing) | GREEN (minimal impl) |
|------|---------------|---------------------|
| `clip_total_duration` | Create clip with 3 frames of 100ms each, assert `totalDurationMs == 300` | Compute sum in constructor |
| `clip_frame_at_time_loop` | Loop clip, 3 frames 100ms each. At t=0→frame0, t=150→frame1, t=250→frame2, t=300→frame0, t=550→frame1 | Implement `getFrameIndex(timeMs)` with modulo |
| `clip_frame_at_time_once` | Non-loop clip. At t=0→frame0, t=150→frame1, t=250→frame2, t=300→frame2, t=999→frame2 | Implement with clamp |
| `clip_is_complete_loop` | Loop clip never returns complete=true | Always false for loop |
| `clip_is_complete_once` | Non-loop clip: false at t=0, false at t=150, true at t=300 | `time >= totalDuration` |

#### Test Suite 2: SpriteInstance

| Test | RED | GREEN |
|------|-----|-------|
| `instance_initial_frame` | Create instance with 3-frame clip, assert currentFrame==0 | Initialize m_currentFrame=0, m_elapsed=0 |
| `instance_tick_advances_frame` | Tick 150ms on 100ms frames → frame 1 | `tick()` calls `recalcFrame()` which computes by elapsed time |
| `instance_loop_wraps` | Tick 350ms on 3×100ms loop → frame 0 | Modulo logic in recalcFrame |
| `instance_non_loop_completes` | Tick 350ms on 3×100ms once → tick returns true, isComplete()==true | Check elapsed >= totalDuration |
| `instance_transform_affects_gpu_data` | Set posX=0.5, scaleY=2.0, rotation=1.57, verify gpuData() fields | Straightforward field copying in gpuData() |
| `instance_gpu_data_uv_from_frame` | Clip frame 1 has u=0.25,v=0.5,w=0.2,h=0.5; tick 150ms; gpuData() has those UVs | gpuData() reads from clip->frames[currentFrame] |
| `instance_set_clip_restarts` | Start clip A, tick 200ms, setClip(clip B), assert elapsed==0, currentFrame==0 | setClip with restart=true resets elapsed/frame |
| `instance_set_transform_updates_running` | setTransform with new pos, gpuData() reflects it | setTransform overwrites m_transform |

#### Test Suite 3: SpriteAtlas

| Test | RED | GREEN |
|------|-----|-------|
| `atlas_loads_animations` | Load test JSON, assert atlas.hasAnimation("test_idle") and count==1 | Parse JSON, create clips map |
| `atlas_find_returns_correct_clip` | findAnimation("test_idle")->name == "test_idle" | Lookup in map |
| `atlas_find_missing_returns_null` | findAnimation("nonexistent") == nullptr | Return nullptr for missing |
| `atlas_loop_flag` | Load clip with "loop": true, assert clip->loop==true | Parse loop field |
| `atlas_multiple_animations` | Atlas with 3 animations, count==3 | Parse all entries |

#### Test Suite 4: SpriteManager (CPU logic only)

| Test | RED | GREEN |
|------|-----|-------|
| `manager_play_returns_valid_handle` | play("clip", Screen, {}) returns != INVALID_SPRITE | Allocate instance slot |
| `manager_play_twice_returns_different_handles` | Two plays → two distinct handles | Increment handle counter |
| `manager_stop_removes_instance` | play→stop→play should get different handle (slot reused) | Mark slot inactive on stop |
| `manager_update_advances_all` | Play two instances, update(0.2f), both elapsed == 0.2f | Loop over active instances, call tick() |
| `manager_is_complete_non_loop` | Play non-loop clip, update(totalDuration+1), isComplete → true | Delegate to instance |
| `manager_set_animation_swaps_clip` | Play clip A, setAnimation(handle, clip B), instance now uses clip B | Find instance, call setClip() |
| `manager_set_transform_modifies_running` | Play, setTransform(handle, newTransform), gpuData reflects it | Find instance, call setTransform() |
| `manager_active_count` | Play 3, stop 1, only 2 active in update loop | Count active instances |

### Implementation Phases

#### Phase 1: Pure Data Layer (no GPU)
Files: `src/SpriteManager.hpp`, `src/SpriteManager.cpp` (partial)
- `AnimationFrame`, `AnimationClip` structs
- `SpriteTransform` struct
- `GpuSpriteData` struct (must match shader layout)
- `SpriteInstance` class (tick, frame calc, gpuData)
- `SpriteAtlas` class (JSON loading, clip lookup)
- `SpriteManager` class (instance lifecycle, update)
- Tests: All Test Suites 1-4

#### Phase 2: GPU Rendering
Files: `shaders/sprite.vert.slang`, `shaders/sprite.frag.slang`, rest of `SpriteManager.cpp`
- Sprite vertex/fragment shaders (screen-space only)
- `SpriteAtlas::SpriteAtlas(SDL_GPUDevice*, jsonPath)` — texture loading
- `SpriteManager::SpriteManager(SDL_GPUDevice*, SDL_Window*)` — pipeline creation
- `SpriteManager::draw(renderPass, cmdBuf)` — GPU upload + draw calls
- No unit tests (GPU-dependent; validated via playtesting)

#### Phase 3: Render Pass Integration
Files: `src/Renderer.cpp`, `src/Renderer.hpp`
- Add sprite pass between scene pass and ImGui pass
- Pass SpriteManager reference to doRender

#### Phase 4: Game Code Wiring
Files: `src/main.cpp`, `src/PlayerState.cpp`, `src/Turn.hpp`
- Create SpriteManager, load atlases
- Weapon change detection → play appropriate idle clip
- AttackTurn → trigger swing clip
- Per-frame update + completion check → restart idle
- Placeholder sprite art

#### Phase 5: Effects System & World-Space (future)
- `SpriteSpace::World` and world-space shader
- Effect queuing/cleanup pattern in game code
- Projectile trajectory updating

## 8. File Layout

```
src/
  SpriteManager.hpp    — All sprite classes (AnimationClip, SpriteAtlas,
                          SpriteInstance, SpriteTransform, SpriteManager)
  SpriteManager.cpp    — Implementations

shaders/
  sprite.vert.slang    — Screen-space vertex shader (PullSpriteBatch pattern)
  sprite.frag.slang    — Texture sample fragment shader

resources/
  weapons_atlas.png    — Placeholder sprite sheet
  weapons_atlas.json   — Animation clip definitions

tests/
  test_sprite_manager.cpp — Unit tests for CPU-side sprite logic

design_docs/
  sprite_system.md     — This document
```

## 9. Open Questions

1. **Sprite art**: Placeholder art needed for initial development. Simplest: colored
   rectangles with weapon silhouettes. Can be refined later.

2. **Animation completion callback**: Currently polling `isComplete()`. Could add a
   callback/event system for "animation finished" to reduce polling. Defer.

3. **Atlas batching**: Currently one atlas = one texture. If sprites span multiple
   atlases, we need to group draw calls by atlas. The current design handles this:
   `SpriteAtlas::texture()` gives the texture, and `SpriteManager::draw()` can sort
   instances by atlas.

4. **Depth sorting for world-space**: When world-space sprites overlap, we need
   depth testing. The perspective projection + depth buffer handle this naturally.
