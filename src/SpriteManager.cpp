#include "SpriteManager.hpp"
#include "ShaderUtil.hpp"

#include <SDL3_image/SDL_image.h>
#include <fstream>
#include <glm/glm.hpp>
#include <spdlog/spdlog.h>
#include <stdexcept>

// ============================================================
// AnimationClip
// ============================================================

uint32_t AnimationClip::totalDurationMs() const
{
    uint32_t total = 0;
    for (const auto& frame : frames) {
        total += frame.durationMs;
    }
    return total;
}

size_t AnimationClip::getFrameIndex(uint32_t elapsedMs) const
{
    if (frames.empty()) {
        return 0;
    }

    if (loop) {
        uint32_t total = totalDurationMs();
        if (total == 0) {
            return 0;
        }
        uint32_t t = elapsedMs % total;
        uint32_t accum = 0;
        for (size_t i = 0; i < frames.size(); ++i) {
            accum += frames[i].durationMs;
            if (t < accum) {
                return i;
            }
        }
        return frames.size() - 1; // shouldn't happen, but safe fallback
    } else {
        uint32_t accum = 0;
        for (size_t i = 0; i < frames.size(); ++i) {
            accum += frames[i].durationMs;
            if (elapsedMs < accum) {
                return i;
            }
        }
        return frames.size() - 1; // past end: clamp to last frame
    }
}

bool AnimationClip::isComplete(uint32_t elapsedMs) const
{
    if (loop) {
        return false;
    }
    return elapsedMs >= totalDurationMs();
}

// ============================================================
// SpriteAtlas
// ============================================================

SpriteAtlas::SpriteAtlas(const json& descriptor)
{
    if (auto tex = descriptor.find("texture"); tex != descriptor.end()) {
        m_textureFilename = tex->get<std::string>();
    }

    if (auto anims = descriptor.find("animations");
        anims != descriptor.end() && anims->is_object()) {
        for (const auto& [name, animJson] : anims->items()) {
            AnimationClip clip;
            clip.name = name;

            if (auto loop = animJson.find("loop"); loop != animJson.end()) {
                clip.loop = loop->get<bool>();
            } // else default true

            if (auto frames = animJson.find("frames");
                frames != animJson.end() && frames->is_array()) {
                for (const auto& f : *frames) {
                    AnimationFrame frame;
                    frame.u = f.value("u", 0.0f);
                    frame.v = f.value("v", 0.0f);
                    frame.w = f.value("w", 1.0f);
                    frame.h = f.value("h", 1.0f);
                    frame.durationMs = f.value("duration_ms", 100u);
                    clip.frames.push_back(frame);
                }
            }

            m_clips[name] = std::move(clip);
        }
    }
}

const AnimationClip* SpriteAtlas::findAnimation(std::string_view name) const
{
    // Convert string_view to string for unordered_map lookup
    std::string key(name);
    auto it = m_clips.find(key);
    if (it != m_clips.end()) {
        return &it->second;
    }
    return nullptr;
}

bool SpriteAtlas::hasAnimation(std::string_view name) const
{
    return findAnimation(name) != nullptr;
}

// ============================================================
// SpriteInstance
// ============================================================

SpriteInstance::SpriteInstance(const AnimationClip* clip, SpriteSpace space,
                               const SpriteTransform& transform)
    : m_clip(clip)
    , m_space(space)
    , m_transform(transform)
{
}

bool SpriteInstance::tick(float dt)
{
    m_elapsed += dt;

    uint32_t elapsedMs = static_cast<uint32_t>(m_elapsed * 1000.0f);
    recalcFrame();

    // Check for completion (non-looping only)
    if (!m_complete && m_clip && !m_clip->loop) {
        m_complete = m_clip->isComplete(elapsedMs);
    }

    return m_complete;
}

void SpriteInstance::recalcFrame()
{
    if (!m_clip || m_clip->frames.empty()) {
        m_currentFrame = 0;
        return;
    }
    uint32_t elapsedMs = static_cast<uint32_t>(m_elapsed * 1000.0f);
    m_currentFrame = m_clip->getFrameIndex(elapsedMs);
}

GpuSpriteData SpriteInstance::gpuData() const
{
    GpuSpriteData data = {};

    data.posX = m_transform.posX;
    data.posY = m_transform.posY;
    data.posZ = m_transform.posZ;
    data.rotation = m_transform.rotation;
    data.scaleX = m_transform.scaleX;
    data.scaleY = m_transform.scaleY;
    data.r = m_transform.r;
    data.g = m_transform.g;
    data.b = m_transform.b;
    data.a = m_transform.a;

    // UV from current frame
    if (m_clip && m_currentFrame < m_clip->frames.size()) {
        const auto& frame = m_clip->frames[m_currentFrame];
        data.texU = frame.u;
        data.texV = frame.v;
        data.texW = frame.w;
        data.texH = frame.h;
    }

    return data;
}

void SpriteInstance::setClip(const AnimationClip* clip, bool restart)
{
    m_clip = clip;
    m_complete = false;
    if (restart) {
        m_elapsed = 0.0f;
        m_currentFrame = 0;
    } else {
        recalcFrame();
    }
}

// ============================================================
// SpriteManager
// ============================================================

SpriteManager::SpriteManager(SDL_GPUDevice* device, SDL_Window* window)
    : m_device(device)
    , m_window(window)
{
    m_instances.reserve(kInitialCapacity);
    m_instanceActive.reserve(kInitialCapacity);
    m_gpuData.reserve(kInitialCapacity);
}

SpriteManager::~SpriteManager()
{
    if (!m_device) return;

    if (m_screenPipeline)
        SDL_ReleaseGPUGraphicsPipeline(m_device, m_screenPipeline);
    if (m_quadVertexBuffer)
        SDL_ReleaseGPUBuffer(m_device, m_quadVertexBuffer);
    if (m_quadIndexBuffer)
        SDL_ReleaseGPUBuffer(m_device, m_quadIndexBuffer);
    if (m_spriteBuffer)
        SDL_ReleaseGPUBuffer(m_device, m_spriteBuffer);
    if (m_spriteTransferBuf)
        SDL_ReleaseGPUTransferBuffer(m_device, m_spriteTransferBuf);
    if (m_drawBuffer)
        SDL_ReleaseGPUBuffer(m_device, m_drawBuffer);
    if (m_drawTransferBuf)
        SDL_ReleaseGPUTransferBuffer(m_device, m_drawTransferBuf);

    // Release atlas textures and samplers
    for (auto& atlas : m_atlases) {
        if (atlas->texture())
            SDL_ReleaseGPUTexture(m_device, atlas->texture());
        if (atlas->sampler())
            SDL_ReleaseGPUSampler(m_device, atlas->sampler());
    }
}

void SpriteManager::loadAtlas(const std::string& jsonPath)
{
    spdlog::info("SpriteManager: loading atlas '{}'", jsonPath);

    std::ifstream inFile(jsonPath);
    if (!inFile) {
        throw std::runtime_error(
            std::format("SpriteManager: failed to open atlas file '{}'", jsonPath));
    }

    json desc;
    inFile >> desc;

    addAtlas(desc);

    // Load the texture if we have a GPU device
    if (!m_device) {
        return;
    }

    SpriteAtlas* atlas = m_atlases.back().get();
    const std::string& texFile = atlas->textureFilename();
    if (texFile.empty()) {
        return;
    }

    // Resolve path: relative to base path
    std::string fullPath = std::format("{}{}", SDL_GetBasePath(), texFile);
    spdlog::info("SpriteManager: loading texture '{}'", fullPath);

    SDL_GPUCommandBuffer* cmdBuf = SDL_AcquireGPUCommandBuffer(m_device);
    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmdBuf);

    int texW, texH;
    SDL_GPUTexture* texture = IMG_LoadGPUTexture(
        m_device, copyPass, fullPath.c_str(), &texW, &texH);

    SDL_EndGPUCopyPass(copyPass);
    SDL_SubmitGPUCommandBuffer(cmdBuf);

    if (!texture) {
        spdlog::error("SpriteManager: failed to load texture '{}': {}",
            fullPath, SDL_GetError());
        return;
    }

    spdlog::info("SpriteManager: texture loaded {}x{}", texW, texH);

    // Create sampler — NEAREST for pixel-art style
    SDL_GPUSamplerCreateInfo samplerInfo = {
        .min_filter = SDL_GPU_FILTER_NEAREST,
        .mag_filter = SDL_GPU_FILTER_NEAREST,
        .mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
        .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
    };
    SDL_GPUSampler* sampler = SDL_CreateGPUSampler(m_device, &samplerInfo);
    if (!sampler) {
        spdlog::error("SpriteManager: failed to create sampler: {}",
            SDL_GetError());
        SDL_ReleaseGPUTexture(m_device, texture);
        return;
    }

    atlas->setTexture(texture);
    atlas->setSampler(sampler);
}

void SpriteManager::addAtlas(const json& descriptor)
{
    m_atlases.push_back(std::make_unique<SpriteAtlas>(descriptor));
    spdlog::debug("SpriteManager: added atlas '{}' with {} animations",
        m_atlases.back()->textureFilename(),
        m_atlases.back()->animationCount());
}

const SpriteAtlas* SpriteManager::atlas(size_t index) const
{
    if (index < m_atlases.size()) {
        return m_atlases[index].get();
    }
    return nullptr;
}

SpriteAtlas* SpriteManager::findAtlasFor(std::string_view animationName) const
{
    for (const auto& atlas : m_atlases) {
        if (atlas->hasAnimation(animationName)) {
            return atlas.get();
        }
    }
    return nullptr;
}

SpriteInstance* SpriteManager::instanceForHandle(SpriteHandle handle)
{
    if (handle == INVALID_SPRITE || handle > m_instances.size()) {
        return nullptr;
    }
    size_t idx = handle - 1;
    if (!m_instanceActive[idx]) {
        return nullptr;
    }
    return &m_instances[idx];
}

const SpriteInstance* SpriteManager::instanceForHandle(SpriteHandle handle) const
{
    if (handle == INVALID_SPRITE || handle > m_instances.size()) {
        return nullptr;
    }
    size_t idx = handle - 1;
    if (!m_instanceActive[idx]) {
        return nullptr;
    }
    return &m_instances[idx];
}

SpriteHandle SpriteManager::play(std::string_view animationName,
                                  SpriteSpace space,
                                  const SpriteTransform& transform)
{
    SpriteAtlas* atlas = findAtlasFor(animationName);
    if (!atlas) {
        spdlog::warn("SpriteManager::play: animation '{}' not found in any atlas",
            animationName);
        return INVALID_SPRITE;
    }

    const AnimationClip* clip = atlas->findAnimation(animationName);
    if (!clip) {
        return INVALID_SPRITE;
    }

    // Monotonically increasing handles — simplifies lifetime tracking.
    // Old handles are never reused.
    SpriteHandle handle = m_nextHandle++;
    m_instances.emplace_back(clip, space, transform);
    m_instanceActive.push_back(true);
    m_gpuData.push_back({});

    spdlog::debug("SpriteManager::play: '{}' -> handle {}", animationName, handle);
    return handle;
}

void SpriteManager::stop(SpriteHandle handle)
{
    SpriteInstance* inst = instanceForHandle(handle);
    if (!inst) {
        return;
    }

    size_t idx = handle - 1;
    m_instanceActive[idx] = false;
    spdlog::debug("SpriteManager::stop: handle {}", handle);
}

void SpriteManager::setTransform(SpriteHandle handle, const SpriteTransform& t)
{
    SpriteInstance* inst = instanceForHandle(handle);
    if (inst) {
        inst->setTransform(t);
    }
}

void SpriteManager::setAnimation(SpriteHandle handle, std::string_view animationName)
{
    SpriteInstance* inst = instanceForHandle(handle);
    if (!inst) {
        return;
    }

    SpriteAtlas* atlas = findAtlasFor(animationName);
    if (!atlas) {
        spdlog::warn("SpriteManager::setAnimation: '{}' not found", animationName);
        return;
    }

    const AnimationClip* clip = atlas->findAnimation(animationName);
    if (!clip) {
        return;
    }

    inst->setClip(clip, true); // restart with new clip
    spdlog::debug("SpriteManager::setAnimation: handle {} -> '{}'", handle,
        animationName);
}

void SpriteManager::setSpace(SpriteHandle handle, SpriteSpace space)
{
    SpriteInstance* inst = instanceForHandle(handle);
    if (inst) {
        inst->setSpace(space);
    }
}

bool SpriteManager::isComplete(SpriteHandle handle) const
{
    const SpriteInstance* inst = instanceForHandle(handle);
    if (!inst) {
        return true; // stopped instances are "complete"
    }
    return inst->isComplete();
}

GpuSpriteData SpriteManager::gpuData(SpriteHandle handle) const
{
    const SpriteInstance* inst = instanceForHandle(handle);
    if (inst) {
        return inst->gpuData();
    }
    return {};
}

size_t SpriteManager::activeCount() const
{
    size_t count = 0;
    for (bool active : m_instanceActive) {
        if (active) count++;
    }
    return count;
}

void SpriteManager::update(float dt)
{
    for (size_t i = 0; i < m_instances.size(); ++i) {
        if (m_instanceActive[i]) {
            m_instances[i].tick(dt);
            // gpuData will be computed on demand in draw()
        }
    }
}

void SpriteManager::uploadGpuData(SDL_GPUCommandBuffer* cmdBuf)
{
    if (!m_device || !cmdBuf) {
        return;
    }

    size_t count = activeCount();
    if (count == 0) {
        return;
    }

    ensureGpuResources();

    // --- Build per-instance GPU data from active sprites ---
    m_gpuData.clear();
    for (size_t i = 0; i < m_instances.size(); ++i) {
        if (m_instanceActive[i]) {
            m_gpuData.push_back(m_instances[i].gpuData());
        }
    }

    Uint32 instanceCount = static_cast<Uint32>(m_gpuData.size());
    if (instanceCount == 0) {
        return;
    }

    // --- Map transfer buffers ---
    GpuSpriteData* mapped = static_cast<GpuSpriteData*>(
        SDL_MapGPUTransferBuffer(m_device, m_spriteTransferBuf, true));
    SDL_memcpy(mapped, m_gpuData.data(),
        instanceCount * sizeof(GpuSpriteData));
    SDL_UnmapGPUTransferBuffer(m_device, m_spriteTransferBuf);

    SDL_GPUIndexedIndirectDrawCommand* drawCmd =
        static_cast<SDL_GPUIndexedIndirectDrawCommand*>(
            SDL_MapGPUTransferBuffer(m_device, m_drawTransferBuf, true));
    drawCmd[0] = {
        .num_indices = 6,
        .num_instances = instanceCount,
        .first_index = 0,
        .vertex_offset = 0,
        .first_instance = 0
    };
    SDL_UnmapGPUTransferBuffer(m_device, m_drawTransferBuf);

    // --- Upload via copy pass ---
    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmdBuf);

    SDL_GPUTransferBufferLocation instanceLoc = {
        .transfer_buffer = m_spriteTransferBuf, .offset = 0
    };
    SDL_GPUBufferRegion instanceRegion = {
        .buffer = m_spriteBuffer, .offset = 0,
        .size = static_cast<Uint32>(instanceCount * sizeof(GpuSpriteData))
    };
    SDL_UploadToGPUBuffer(copyPass, &instanceLoc, &instanceRegion, true);

    SDL_GPUTransferBufferLocation drawLoc = {
        .transfer_buffer = m_drawTransferBuf, .offset = 0
    };
    SDL_GPUBufferRegion drawRegion = {
        .buffer = m_drawBuffer, .offset = 0,
        .size = sizeof(SDL_GPUIndexedIndirectDrawCommand)
    };
    SDL_UploadToGPUBuffer(copyPass, &drawLoc, &drawRegion, true);

    SDL_EndGPUCopyPass(copyPass);

    m_instanceCount = instanceCount;
}

void SpriteManager::draw(SDL_GPURenderPass* renderPass)
{
    if (!m_device || !renderPass || m_instanceCount == 0) {
        return;
    }

    // Find the atlas for the first active sprite.
    SDL_GPUTexture* atlasTexture = nullptr;
    SDL_GPUSampler* atlasSampler = nullptr;
    for (size_t i = 0; i < m_instances.size(); ++i) {
        if (m_instanceActive[i]) {
            for (const auto& atlas : m_atlases) {
                if (atlas->hasAnimation(m_instances[i].clip()->name)) {
                    atlasTexture = atlas->texture();
                    atlasSampler = atlas->sampler();
                    break;
                }
            }
            break;
        }
    }

    if (!atlasTexture || !atlasSampler) {
        spdlog::warn("SpriteManager::draw: no atlas texture/sampler");
        return;
    }

    SDL_BindGPUGraphicsPipeline(renderPass, m_screenPipeline);

    SDL_GPUBufferBinding vb0 = { .buffer = m_quadVertexBuffer, .offset = 0 };
    SDL_BindGPUVertexBuffers(renderPass, 0, &vb0, 1);

    SDL_GPUBufferBinding vb1 = { .buffer = m_spriteBuffer, .offset = 0 };
    SDL_BindGPUVertexBuffers(renderPass, 1, &vb1, 1);

    SDL_GPUBufferBinding ib = { .buffer = m_quadIndexBuffer, .offset = 0 };
    SDL_BindGPUIndexBuffer(renderPass, &ib, SDL_GPU_INDEXELEMENTSIZE_16BIT);

    SDL_GPUTextureSamplerBinding texBind = {
        .texture = atlasTexture,
        .sampler = atlasSampler
    };
    SDL_BindGPUFragmentSamplers(renderPass, 0, &texBind, 1);

    SDL_DrawGPUIndexedPrimitivesIndirect(renderPass, m_drawBuffer, 0, 1);
}

void SpriteManager::ensureGpuResources()
{
    if (!m_device || m_screenPipeline) {
        return; // already created
    }

    // --- Unit quad vertex buffer (slot 0) ---
    // 4 corners: (0,0), (1,0), (0,1), (1,1)
    float quadVerts[] = {
        0.0f, 0.0f,
        1.0f, 0.0f,
        0.0f, 1.0f,
        1.0f, 1.0f
    };

    SDL_GPUBufferCreateInfo quadVbInfo = {
        .usage = SDL_GPU_BUFFERUSAGE_VERTEX,
        .size = sizeof(quadVerts)
    };
    m_quadVertexBuffer = SDL_CreateGPUBuffer(m_device, &quadVbInfo);

    SDL_GPUTransferBufferCreateInfo quadTransferInfo = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = sizeof(quadVerts)
    };
    SDL_GPUTransferBuffer* quadTransfer =
        SDL_CreateGPUTransferBuffer(m_device, &quadTransferInfo);

    float* vtxData = static_cast<float*>(
        SDL_MapGPUTransferBuffer(m_device, quadTransfer, false));
    SDL_memcpy(vtxData, quadVerts, sizeof(quadVerts));
    SDL_UnmapGPUTransferBuffer(m_device, quadTransfer);

    // --- Index buffer: 2 triangles = 6 indices ---
    uint16_t indices[] = { 0, 1, 2, 2, 1, 3 };

    SDL_GPUBufferCreateInfo quadIbInfo = {
        .usage = SDL_GPU_BUFFERUSAGE_INDEX,
        .size = sizeof(indices)
    };
    m_quadIndexBuffer = SDL_CreateGPUBuffer(m_device, &quadIbInfo);

    SDL_GPUTransferBufferCreateInfo indexTransferInfo = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = sizeof(indices)
    };
    SDL_GPUTransferBuffer* indexTransfer =
        SDL_CreateGPUTransferBuffer(m_device, &indexTransferInfo);

    uint16_t* idxData = static_cast<uint16_t*>(
        SDL_MapGPUTransferBuffer(m_device, indexTransfer, false));
    SDL_memcpy(idxData, indices, sizeof(indices));
    SDL_UnmapGPUTransferBuffer(m_device, indexTransfer);

    // --- Instance buffer (up to kMaxSprites GpuSpriteData entries) ---
    SDL_GPUBufferCreateInfo spriteBufInfo = {
        .usage = SDL_GPU_BUFFERUSAGE_VERTEX,
        .size = static_cast<Uint32>(kMaxSprites * sizeof(GpuSpriteData))
    };
    m_spriteBuffer = SDL_CreateGPUBuffer(m_device, &spriteBufInfo);

    SDL_GPUTransferBufferCreateInfo spriteTransInfo = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = static_cast<Uint32>(kMaxSprites * sizeof(GpuSpriteData))
    };
    m_spriteTransferBuf = SDL_CreateGPUTransferBuffer(m_device, &spriteTransInfo);

    // --- Indirect draw buffer ---
    SDL_GPUBufferCreateInfo drawBufInfo = {
        .usage = SDL_GPU_BUFFERUSAGE_INDIRECT,
        .size = sizeof(SDL_GPUIndexedIndirectDrawCommand)
    };
    m_drawBuffer = SDL_CreateGPUBuffer(m_device, &drawBufInfo);

    SDL_GPUTransferBufferCreateInfo drawTransInfo = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = sizeof(SDL_GPUIndexedIndirectDrawCommand)
    };
    m_drawTransferBuf = SDL_CreateGPUTransferBuffer(m_device, &drawTransInfo);

    // --- Upload static vertex/index data ---
    SDL_GPUCommandBuffer* uploadCmd = SDL_AcquireGPUCommandBuffer(m_device);
    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(uploadCmd);

    SDL_GPUTransferBufferLocation quadLoc = {
        .transfer_buffer = quadTransfer, .offset = 0
    };
    SDL_GPUBufferRegion quadRegion = {
        .buffer = m_quadVertexBuffer, .offset = 0, .size = sizeof(quadVerts)
    };
    SDL_UploadToGPUBuffer(copyPass, &quadLoc, &quadRegion, false);

    SDL_GPUTransferBufferLocation idxLoc = {
        .transfer_buffer = indexTransfer, .offset = 0
    };
    SDL_GPUBufferRegion idxRegion = {
        .buffer = m_quadIndexBuffer, .offset = 0, .size = sizeof(indices)
    };
    SDL_UploadToGPUBuffer(copyPass, &idxLoc, &idxRegion, false);

    SDL_EndGPUCopyPass(copyPass);
    SDL_SubmitGPUCommandBuffer(uploadCmd);

    SDL_ReleaseGPUTransferBuffer(m_device, quadTransfer);
    SDL_ReleaseGPUTransferBuffer(m_device, indexTransfer);

    // --- Create pipeline ---
    createPipeline();
}

void SpriteManager::createPipeline()
{
    // Load compiled shaders
    SDL_GPUShader* vertShader = loadShader(m_device,
        { "sprite.vert", 0, 0, 0, 0 });
    SDL_GPUShader* fragShader = loadShader(m_device,
        { "sprite.frag", 1, 0, 0, 0 });

    // Vertex attribute layout:
    // Slot 0 (per-vertex): float2 corner (8 bytes stride)
    // Slot 1 (per-instance): 4 x float4 = 64 bytes stride
    SDL_GPUVertexAttribute vertexAttrs[] = {
        { .location = 0, .buffer_slot = 0,
          .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, .offset = 0 },
        { .location = 1, .buffer_slot = 1,
          .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, .offset = 0 },
        { .location = 2, .buffer_slot = 1,
          .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, .offset = 16 },
        { .location = 3, .buffer_slot = 1,
          .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, .offset = 32 },
        { .location = 4, .buffer_slot = 1,
          .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, .offset = 48 },
    };

    SDL_GPUVertexBufferDescription vbDescs[] = {
        { .slot = 0, .pitch = 8, // float2
          .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
          .instance_step_rate = 0 },
        { .slot = 1, .pitch = 64, // 4 x float4
          .input_rate = SDL_GPU_VERTEXINPUTRATE_INSTANCE,
          .instance_step_rate = 0 },
    };

    SDL_GPUVertexInputState vertexInput = {
        .vertex_buffer_descriptions = vbDescs,
        .num_vertex_buffers = 2,
        .vertex_attributes = vertexAttrs,
        .num_vertex_attributes = 5,
    };

    // Alpha blending — sprites composite over the scene
    SDL_GPUColorTargetBlendState blend = {
        .src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
        .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
        .color_blend_op = SDL_GPU_BLENDOP_ADD,
        .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
        .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
        .alpha_blend_op = SDL_GPU_BLENDOP_ADD,
        .color_write_mask = 0,
        .enable_blend = true,
        .enable_color_write_mask = false,
    };

    SDL_GPUColorTargetDescription colorTarget = {
        .format = SDL_GetGPUSwapchainTextureFormat(m_device, m_window),
        .blend_state = blend,
    };

    // No depth test — sprites draw on top of 3D scene
    SDL_GPURasterizerState rasterizer = {
        .fill_mode = SDL_GPU_FILLMODE_FILL,
        .cull_mode = SDL_GPU_CULLMODE_NONE,
        .front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE,
        .depth_bias_constant_factor = 0.0f,
        .depth_bias_clamp = 0.0f,
        .depth_bias_slope_factor = 0.0f,
        .enable_depth_bias = false,
        .enable_depth_clip = false,
    };

    SDL_GPUGraphicsPipelineCreateInfo pipeInfo = {
        .vertex_shader = vertShader,
        .fragment_shader = fragShader,
        .vertex_input_state = vertexInput,
        .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .rasterizer_state = rasterizer,
        .target_info = {
            .color_target_descriptions = &colorTarget,
            .num_color_targets = 1,
            .has_depth_stencil_target = false,
        },
    };

    m_screenPipeline = SDL_CreateGPUGraphicsPipeline(m_device, &pipeInfo);

    SDL_ReleaseGPUShader(m_device, vertShader);
    SDL_ReleaseGPUShader(m_device, fragShader);

    if (!m_screenPipeline) {
        throw std::runtime_error(
            std::format("SpriteManager: failed to create pipeline: {}",
                SDL_GetError()));
    }

    spdlog::info("SpriteManager: GPU pipeline created");
}
