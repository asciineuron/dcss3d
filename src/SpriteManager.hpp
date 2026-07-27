#pragma once

#include <SDL3/SDL_gpu.h>
#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

using json = nlohmann::json;

// Transparent hash functor that enables heterogeneous lookup in
// std::unordered_map<std::string, ...> with std::string_view keys.
struct TransparentStringHash {
    using is_transparent = void; // enable find() with string_view
    size_t operator()(std::string_view sv) const noexcept {
        return std::hash<std::string_view>{}(sv);
    }
};

// ============================================================
// AnimationClip — pure data: a named sequence of frames
// ============================================================

struct AnimationFrame {
    float u, v;            // UV top-left in atlas texture (normalized 0–1)
    float w, h;            // UV extent
    uint32_t durationMs;   // how long this frame is shown
};

struct AnimationClip {
    std::string name;
    std::vector<AnimationFrame> frames;
    bool loop = true;

    // Total duration in milliseconds (sum of all frame durations).
    uint32_t totalDurationMs() const;

    // Which frame is active at a given elapsed time (in milliseconds).
    // For looping clips, wraps around. For non-looping, clamps at last frame.
    size_t getFrameIndex(uint32_t elapsedMs) const;

    // Whether the animation has finished (only relevant for non-looping clips).
    bool isComplete(uint32_t elapsedMs) const;
};

// ============================================================
// SpriteTransform — spatial properties of a sprite instance
// ============================================================

enum class SpriteSpace {
    Screen,  // NDC coordinates, orthographic projection, no depth test
    World    // (future) 3D world coordinates, perspective, depth-tested
};

struct SpriteTransform {
    float posX = 0.0f;
    float posY = 0.0f;
    float posZ = 0.0f;
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    float rotation = 0.0f;   // radians
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
};

// ============================================================
// GpuSpriteData — packed struct for GPU upload (must match shader)
// ============================================================

// Packed to match Metal vertex attribute alignment.
// The vertex shader sees this as 4 float4 attributes at offsets 0, 16, 32, 48.
struct GpuSpriteData {
    float posX, posY, posZ, rotation;   // offset 0:  float4 in shader
    float scaleX, scaleY, texU, texV;   // offset 16: float4 in shader
    float texW, texH, r, g;             // offset 32: float4 in shader
    float b, a, _pad1, _pad2;           // offset 48: float4 in shader
};
static_assert(sizeof(GpuSpriteData) == 64,
    "GpuSpriteData must be 64 bytes (4 x float4)");
// Verify exact field offsets against the hardcoded vertex-attribute offsets
// in the sprite pipeline.  If these fire, the shader will read garbage.
static_assert(offsetof(GpuSpriteData, posX) == 0, "posX must be at offset 0");
static_assert(offsetof(GpuSpriteData, scaleX) == 16, "scaleX must be at offset 16");
static_assert(offsetof(GpuSpriteData, texW) == 32, "texW must be at offset 32");
static_assert(offsetof(GpuSpriteData, b) == 48, "b must be at offset 48");

// ============================================================
// SpriteAtlas — GPU texture + catalog of named AnimationClips
// ============================================================

class SpriteAtlas {
public:
    // Construct from a JSON descriptor. Does NOT load GPU texture — that is
    // done separately by SpriteManager when it calls loadTexture().
    // JSON format:
    // {
    //   "texture": "weapons_atlas.png",
    //   "animations": {
    //     "name": {
    //       "loop": true,       // optional, default true
    //       "frames": [
    //         {"u": 0.0, "v": 0.0, "w": 0.5, "h": 1.0, "duration_ms": 200}
    //       ]
    //     }
    //   }
    // }
    explicit SpriteAtlas(const json& descriptor);

    // Lookup an animation by name. Returns nullptr if not found.
    const AnimationClip* findAnimation(std::string_view name) const;

    // Query methods.
    size_t animationCount() const { return m_clips.size(); }
    bool hasAnimation(std::string_view name) const;

    // GPU resources (set by SpriteManager during loading).
    const std::string& textureFilename() const { return m_textureFilename; }
    SDL_GPUTexture* texture() const { return m_texture; }
    SDL_GPUSampler* sampler() const { return m_sampler; }
    void setTexture(SDL_GPUTexture* tex) { m_texture = tex; }
    void setSampler(SDL_GPUSampler* samp) { m_sampler = samp; }

private:
    std::string m_textureFilename;
    // Transparent hash/equality enables find() with string_view without allocation.
    std::unordered_map<std::string, AnimationClip,
        TransparentStringHash, std::equal_to<>> m_clips;
    SDL_GPUTexture* m_texture = nullptr;
    SDL_GPUSampler* m_sampler = nullptr;
};

// ============================================================
// SpriteInstance — one active sprite being rendered
// ============================================================

using SpriteHandle = uint32_t;
constexpr SpriteHandle INVALID_SPRITE = 0;

class SpriteInstance {
public:
    SpriteInstance(const AnimationClip* clip, SpriteSpace space,
                   const SpriteTransform& transform);

    // Advance playback by dt seconds. Returns true if a play-once clip
    // completed this tick (caller can react by restarting idle, etc.).
    bool tick(float dt);

    // Current frame's UV + transform packed into GPU-ready format.
    GpuSpriteData gpuData() const;

    // Mutators for external systems.
    void setTransform(const SpriteTransform& t) { m_transform = t; }
    void setClip(const AnimationClip* clip, bool restart = true);
    void setSpace(SpriteSpace space) { m_space = space; }

    // Queries.
    const AnimationClip* clip() const { return m_clip; }
    SpriteSpace space() const { return m_space; }
    const SpriteTransform& transform() const { return m_transform; }
    bool isComplete() const { return m_complete; }
    size_t currentFrame() const { return m_currentFrame; }
    float elapsedTime() const { return m_elapsed; }

    // Handle assigned by SpriteManager when this instance is created.
    SpriteHandle handle() const { return m_handle; }
    void setHandle(SpriteHandle h) { m_handle = h; }

    // Which atlas owns this instance's clip (set by SpriteManager on creation).
    const class SpriteAtlas* ownerAtlas() const { return m_ownerAtlas; }
    void setOwnerAtlas(const class SpriteAtlas* a) { m_ownerAtlas = a; }

private:
    const AnimationClip* m_clip;
    SpriteSpace m_space;
    SpriteTransform m_transform;
    float m_elapsed = 0.0f;          // seconds since animation started
    size_t m_currentFrame = 0;
    bool m_complete = false;         // true when non-loop clip finishes
    SpriteHandle m_handle = 0;
    const class SpriteAtlas* m_ownerAtlas = nullptr;

    void recalcFrame();
};

// ============================================================
// SpriteManager — owns atlases and instances, orchestrates
//                 per-frame update and rendering
// ============================================================

class SpriteManager {
public:
    // If device/window are null, GPU resources and draw() are no-ops.
    // Useful for testing CPU-side logic without a GPU.
    SpriteManager(SDL_GPUDevice* device, SDL_Window* window);
    ~SpriteManager();

    // --- Asset management ---

    // Load an atlas from a JSON descriptor file.
    void loadAtlas(const std::string& jsonPath);

    // Add an atlas directly from a JSON object (used by tests).
    void addAtlas(const json& descriptor);

    const SpriteAtlas* atlas(size_t index) const;
    size_t atlasCount() const { return m_atlases.size(); }

    // --- Instance lifecycle ---

    // Play a named animation. Returns a handle for later manipulation.
    // Returns INVALID_SPRITE if the animation name is not found in any atlas.
    SpriteHandle play(std::string_view animationName,
                      SpriteSpace space,
                      const SpriteTransform& transform);

    // Stop and remove an instance.
    void stop(SpriteHandle handle);

    // Modify a running instance.
    void setTransform(SpriteHandle handle, const SpriteTransform& t);
    void setAnimation(SpriteHandle handle, std::string_view animationName);
    void setSpace(SpriteHandle handle, SpriteSpace space);

    // Query whether a play-once animation has finished.
    bool isComplete(SpriteHandle handle) const;

    // Get the current GPU data for an instance (for testing).
    GpuSpriteData gpuData(SpriteHandle handle) const;

    // Number of currently active instances.
    size_t activeCount() const;

    // --- Per-frame ---

    // Advance all instances. Call once per game tick.
    void update(float dt);

    // Upload sprite data to GPU via copy pass. Call BEFORE any render pass.
    void uploadGpuData(SDL_GPUCommandBuffer* cmdBuf);

    // Draw all active sprites. Call during the sprite render pass.
    // Must be preceded by uploadGpuData() in the same frame.
    void draw(SDL_GPURenderPass* renderPass);

private:
    SDL_GPUDevice* m_device;
    SDL_Window* m_window;

    std::vector<std::unique_ptr<SpriteAtlas>> m_atlases;

    // --- Instance storage ---
    // m_instances holds all active sprites; inactive slots are tracked in
    // m_freeSlots for reuse.  Handles are monotonically increasing and
    // never reused, but the storage slots are recycled via the free list
    // to prevent unbounded growth over long sessions.
    std::vector<SpriteInstance> m_instances;
    std::vector<bool> m_instanceActive;
    std::vector<size_t> m_freeSlots;       // indices that are available for reuse
    SpriteHandle m_nextHandle = 1;

    // GPU staging: atlas-grouped draw batches, populated by uploadGpuData().
    struct AtlasBatch {
        SDL_GPUTexture* texture = nullptr;
        SDL_GPUSampler* sampler = nullptr;
        Uint32 firstInstance = 0;
        Uint32 instanceCount = 0;
    };
    std::vector<AtlasBatch> m_batches;   // one per atlas with active sprites
    Uint32 m_totalInstances = 0;         // total instances across all batches

    // GPU resources — created lazily in draw().
    SDL_GPUGraphicsPipeline* m_screenPipeline = nullptr;
    SDL_GPUTextureFormat m_pipelineFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
    SDL_GPUBuffer* m_quadVertexBuffer = nullptr;    // unit quad (slot 0)
    SDL_GPUBuffer* m_quadIndexBuffer = nullptr;     // 2 triangles
    SDL_GPUBuffer* m_spriteBuffer = nullptr;        // instance data (slot 1)
    SDL_GPUTransferBuffer* m_spriteTransferBuf = nullptr;
    SDL_GPUBuffer* m_drawBuffer = nullptr;          // indirect draw command
    SDL_GPUTransferBuffer* m_drawTransferBuf = nullptr;

    static constexpr size_t kInitialCapacity = 16;
    static constexpr size_t kMaxSprites = 64;
    static constexpr size_t kMaxAtlasBatches = 8;

    SpriteAtlas* findAtlasFor(std::string_view animationName) const;
    SpriteInstance* instanceForHandle(SpriteHandle handle);
    const SpriteInstance* instanceForHandle(SpriteHandle handle) const;
    size_t instanceIndexForHandle(SpriteHandle handle) const;

    void ensureGpuResources();
    void createPipeline();
};
