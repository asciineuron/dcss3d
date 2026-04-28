#include "ShaderUtil.hpp"
#include "Skybox.hpp"
#include <SDL3_image/SDL_image.h>
#include <spdlog/spdlog.h>
#include <array>

// Unit cube centered at origin. 8 vertices, 36 indices (12 triangles).
// We hardcode this since the skybox only needs positions (no normals/UVs).
static constexpr std::array<float, 24> s_cubeVertices = {
    // positions (x, y, z)
    -0.5f, -0.5f, -0.5f, // 0
     0.5f, -0.5f, -0.5f, // 1
     0.5f,  0.5f, -0.5f, // 2
    -0.5f,  0.5f, -0.5f, // 3
    -0.5f, -0.5f,  0.5f, // 4
     0.5f, -0.5f,  0.5f, // 5
     0.5f,  0.5f,  0.5f, // 6
    -0.5f,  0.5f,  0.5f, // 7
};

// Triangle indices for the 6 cube faces (36 total).
// Winding order doesn't matter since we disable culling for the skybox.
static constexpr std::array<Uint16, 36> s_cubeIndices = {
    // -Z face (front)
    0, 3, 2, 2, 1, 0,
    // +Z face (back)
    5, 6, 7, 7, 4, 5,
    // -X face (left)
    4, 7, 3, 3, 0, 4,
    // +X face (right)
    1, 2, 6, 6, 5, 1,
    // -Y face (bottom)
    4, 0, 1, 1, 5, 4,
    // +Y face (top)
    3, 7, 6, 6, 2, 3,
};

// Helper: load a compiled shader file (same logic as Renderer.cpp)
SDL_GPUGraphicsPipeline* Skybox::createSkyboxPipeline(SDL_Window* window)
{
    SDL_GPUShader* vertexShader = loadShader(m_GPUDevice,
        ShaderParameters { "skybox_vertex.vert", 0, 1, 0, 0 });
    SDL_GPUShader* fragShader = loadShader(m_GPUDevice,
        ShaderParameters { "skybox_fragment.frag", 1, 0, 0, 0 });

    // Position-only vertex input (float3, 12 bytes, but Metal requires 16-byte alignment)
    SDL_GPUVertexAttribute vertexAttributes[] = {
        { .location = 0,
          .buffer_slot = 0,
          .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
          .offset = 0 },
    };
    SDL_GPUVertexBufferDescription vertexBufferDescriptions[] = {
        { .slot = 0,
          .pitch = 16,   // float3 padded to 16 bytes for Metal alignment
          .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
          .instance_step_rate = 0 },
    };
    SDL_GPUVertexInputState vertexInputState = {
        .vertex_buffer_descriptions = vertexBufferDescriptions,
        .num_vertex_buffers = 1,
        .vertex_attributes = vertexAttributes,
        .num_vertex_attributes = 1,
    };

    SDL_GPUColorTargetDescription colorTargetDescriptions[] = {
        { .format = SDL_GetGPUSwapchainTextureFormat(m_GPUDevice, window) }
    };

    SDL_GPURasterizerState rasterizerState = {
        .fill_mode = SDL_GPU_FILLMODE_FILL,
        .cull_mode = SDL_GPU_CULLMODE_NONE,   // no culling — camera is inside the cube
        .front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE,
        .depth_bias_constant_factor = 0.0f,
        .depth_bias_clamp = 0.0f,
        .depth_bias_slope_factor = 0.0f,
        .enable_depth_bias = false,
        .enable_depth_clip = true,
    };

    // Use LESS_OR_EQUAL for the xyww optimization:
    // Skybox vertices have z=w → after divide, depth=1.0 → only passes where
    // no scene geometry was drawn (since scene depths are < 1.0).
    SDL_GPUDepthStencilState depthStencilState = {
        .compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL,
        .back_stencil_state = {},
        .front_stencil_state = {},
        .compare_mask = 0xFF,
        .write_mask = 0,
        .enable_depth_test = true,
        .enable_depth_write = false,  // don't write depth — skybox is "at infinity"
        .enable_stencil_test = false,
    };

    SDL_GPUMultisampleState multisampleState = {
        .sample_count = SDL_GPU_SAMPLECOUNT_1,
        .sample_mask = 0,
        .enable_mask = false,
        .enable_alpha_to_coverage = false,
    };

    SDL_GPUGraphicsPipelineCreateInfo pipelineInfo = {
        .vertex_shader = vertexShader,
        .fragment_shader = fragShader,
        .vertex_input_state = vertexInputState,
        .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .rasterizer_state = rasterizerState,
        .multisample_state = multisampleState,
        .depth_stencil_state = depthStencilState,
        .target_info = {
            .color_target_descriptions = colorTargetDescriptions,
            .num_color_targets = 1,
            .depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D16_UNORM,
            .has_depth_stencil_target = true,
        }
    };

    SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(m_GPUDevice, &pipelineInfo);

    SDL_ReleaseGPUShader(m_GPUDevice, vertexShader);
    SDL_ReleaseGPUShader(m_GPUDevice, fragShader);

    if (!pipeline) {
        throw std::runtime_error(std::format("failed to create skybox pipeline: {}", SDL_GetError()));
    }
    return pipeline;
}

void Skybox::uploadCubeMesh()
{
    // Create vertex buffer
    SDL_GPUBufferCreateInfo vertexInfo = {
        .usage = SDL_GPU_BUFFERUSAGE_VERTEX,
        .size = static_cast<Uint32>(s_cubeVertices.size() * sizeof(float)),
    };
    m_vertexBuffer = SDL_CreateGPUBuffer(m_GPUDevice, &vertexInfo);
    if (!m_vertexBuffer)
        throw std::runtime_error("failed to create skybox vertex buffer");

    // Create index buffer
    SDL_GPUBufferCreateInfo indexInfo = {
        .usage = SDL_GPU_BUFFERUSAGE_INDEX,
        .size = static_cast<Uint32>(s_cubeIndices.size() * sizeof(Uint16)),
    };
    m_indexBuffer = SDL_CreateGPUBuffer(m_GPUDevice, &indexInfo);
    if (!m_indexBuffer)
        throw std::runtime_error("failed to create skybox index buffer");

    m_vertexCount = static_cast<Uint32>(s_cubeVertices.size() / 3);
    m_indexCount = static_cast<Uint32>(s_cubeIndices.size());

    // Upload vertex + index data using a transfer buffer
    Uint32 vertexDataSize = static_cast<Uint32>(s_cubeVertices.size() * sizeof(float));
    Uint32 indexDataSize = static_cast<Uint32>(s_cubeIndices.size() * sizeof(Uint16));

    // For Metal alignment: float3 positions need 16-byte stride, not 12.
    // We need to pad each vertex from 12 bytes to 16 bytes.
    Uint32 paddedVertexStride = 16;  // 4 floats (x,y,z,pad)
    Uint32 paddedVertexDataSize = m_vertexCount * paddedVertexStride;

    Uint32 totalTransferSize = paddedVertexDataSize + indexDataSize;

    SDL_GPUTransferBufferCreateInfo transferInfo = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = totalTransferSize,
    };
    SDL_GPUTransferBuffer* transferBuf = SDL_CreateGPUTransferBuffer(m_GPUDevice, &transferInfo);
    if (!transferBuf)
        throw std::runtime_error("failed to create skybox transfer buffer");

    // Map and fill with padded vertex data + index data
    Uint8* mapped = static_cast<Uint8*>(SDL_MapGPUTransferBuffer(m_GPUDevice, transferBuf, false));
    for (Uint32 i = 0; i < m_vertexCount; ++i) {
        float* dest = reinterpret_cast<float*>(mapped + i * paddedVertexStride);
        dest[0] = s_cubeVertices[i * 3 + 0];  // x
        dest[1] = s_cubeVertices[i * 3 + 1];  // y
        dest[2] = s_cubeVertices[i * 3 + 2];  // z
        dest[3] = 0.0f;                         // padding
    }
    // Copy index data after vertex data
    std::memcpy(mapped + paddedVertexDataSize, s_cubeIndices.data(), indexDataSize);
    SDL_UnmapGPUTransferBuffer(m_GPUDevice, transferBuf);

    // Upload in a copy pass
    SDL_GPUCommandBuffer* cmdBuf = SDL_AcquireGPUCommandBuffer(m_GPUDevice);
    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmdBuf);

    // Upload vertices
    {
        SDL_GPUTransferBufferLocation src = { .transfer_buffer = transferBuf, .offset = 0 };
        SDL_GPUBufferRegion dst = { .buffer = m_vertexBuffer, .offset = 0, .size = paddedVertexDataSize };
        SDL_UploadToGPUBuffer(copyPass, &src, &dst, false);
    }
    // Upload indices
    {
        SDL_GPUTransferBufferLocation src = { .transfer_buffer = transferBuf, .offset = paddedVertexDataSize };
        SDL_GPUBufferRegion dst = { .buffer = m_indexBuffer, .offset = 0, .size = indexDataSize };
        SDL_UploadToGPUBuffer(copyPass, &src, &dst, false);
    }

    SDL_EndGPUCopyPass(copyPass);
    SDL_SubmitGPUCommandBuffer(cmdBuf);
    SDL_ReleaseGPUTransferBuffer(m_GPUDevice, transferBuf);
}

void Skybox::loadCubemap(SDL_GPUCommandBuffer* cmdBuf)
{
    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmdBuf);

    // Load each face as a temporary 2D texture via IMG_LoadGPUTexture,
    // then copy it into the cubemap using SDL_CopyGPUTextureToTexture.
    // We use this approach instead of SDL_UploadToGPUTexture because
    // the Metal backend has issues with per-layer uploads to cube textures.

    // First, load face 0 to determine dimensions and create the cubemap
    std::string path0 = std::format("{}{}", SDL_GetBasePath(), s_faceFilenames[0]);
    spdlog::info("Loading skybox face 0: {}", path0);

    int texWidth = 0, texHeight = 0;
    SDL_GPUTexture* faceTex = IMG_LoadGPUTexture(m_GPUDevice, copyPass, path0.c_str(), &texWidth, &texHeight);
    if (!faceTex) {
        spdlog::error("Failed to load skybox face 0: {}", SDL_GetError());
        SDL_EndGPUCopyPass(copyPass);
        return;
    }

    // Create the cubemap texture
    SDL_GPUTextureCreateInfo texInfo = {
        .type = SDL_GPU_TEXTURETYPE_CUBE,
        .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
        .width = static_cast<Uint32>(texWidth),
        .height = static_cast<Uint32>(texHeight),
        .layer_count_or_depth = 6,
        .num_levels = 1,
        .sample_count = SDL_GPU_SAMPLECOUNT_1,
        .props = 0,
    };
    m_cubemapTexture = SDL_CreateGPUTexture(m_GPUDevice, &texInfo);
    if (!m_cubemapTexture) {
        spdlog::error("Failed to create cubemap texture: {}", SDL_GetError());
        SDL_ReleaseGPUTexture(m_GPUDevice, faceTex);
        SDL_EndGPUCopyPass(copyPass);
        return;
    }

    // Copy face 0 into cubemap layer 0
    {
        SDL_GPUTextureLocation src = {
            .texture = faceTex, .mip_level = 0, .layer = 0,
            .x = 0, .y = 0, .z = 0,
        };
        SDL_GPUTextureLocation dst = {
            .texture = m_cubemapTexture, .mip_level = 0, .layer = 0,
            .x = 0, .y = 0, .z = 0,
        };
        SDL_CopyGPUTextureToTexture(copyPass, &src, &dst,
            static_cast<Uint32>(texWidth), static_cast<Uint32>(texHeight), 1, false);
    }
    SDL_ReleaseGPUTexture(m_GPUDevice, faceTex);

    // Load and copy remaining 5 faces
    for (int i = 1; i < 6; ++i) {
        std::string path = std::format("{}{}", SDL_GetBasePath(), s_faceFilenames[i]);
        spdlog::info("Loading skybox face {}: {}", i, path);

        int w, h;
        faceTex = IMG_LoadGPUTexture(m_GPUDevice, copyPass, path.c_str(), &w, &h);
        if (!faceTex) {
            spdlog::error("Failed to load skybox face {}: {}", i, SDL_GetError());
            continue;
        }

        SDL_GPUTextureLocation src = {
            .texture = faceTex, .mip_level = 0, .layer = 0,
            .x = 0, .y = 0, .z = 0,
        };
        SDL_GPUTextureLocation dst = {
            .texture = m_cubemapTexture, .mip_level = 0,
            .layer = static_cast<Uint32>(i),
            .x = 0, .y = 0, .z = 0,
        };
        SDL_CopyGPUTextureToTexture(copyPass, &src, &dst,
            static_cast<Uint32>(w), static_cast<Uint32>(h), 1, false);

        SDL_ReleaseGPUTexture(m_GPUDevice, faceTex);
    }

    SDL_EndGPUCopyPass(copyPass);

    // Create sampler with CLAMP_TO_EDGE on all axes (prevents cubemap seam artifacts)
    SDL_GPUSamplerCreateInfo samplerInfo = {
        .min_filter = SDL_GPU_FILTER_LINEAR,
        .mag_filter = SDL_GPU_FILTER_LINEAR,
        .mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
        .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .mip_lod_bias = 0.0f,
        .max_anisotropy = 1.0f,
        .compare_op = SDL_GPU_COMPAREOP_INVALID,
        .min_lod = -1000.0f,
        .max_lod = 1000.0f,
        .enable_anisotropy = false,
        .enable_compare = false,
    };
    m_cubemapSampler = SDL_CreateGPUSampler(m_GPUDevice, &samplerInfo);
    if (!m_cubemapSampler) {
        spdlog::error("Failed to create cubemap sampler: {}", SDL_GetError());
        return;
    }

    spdlog::info("Skybox cubemap loaded: {}x{}", texWidth, texHeight);
    m_loaded = true;
}

Skybox::Skybox(SDL_GPUDevice* device, SDL_Window* window)
    : m_GPUDevice(device)
    , m_window(window)
{
    spdlog::info("Creating skybox...");

    // Create the pipeline first (needed for rendering)
    m_pipeline = createSkyboxPipeline(window);

    // Upload the cube mesh
    uploadCubeMesh();

    // Load cubemap texture (requires a command buffer for copy pass)
    SDL_GPUCommandBuffer* cmdBuf = SDL_AcquireGPUCommandBuffer(m_GPUDevice);
    loadCubemap(cmdBuf);
    SDL_SubmitGPUCommandBuffer(cmdBuf);

    if (m_loaded) {
        spdlog::info("Skybox created successfully");
    } else {
        spdlog::error("Skybox creation failed — skybox will not render");
    }
}

void Skybox::draw(SDL_GPURenderPass* renderPass, const glm::mat4& viewProjNoTranslation)
{
    if (!m_loaded)
        return;

    SDL_BindGPUGraphicsPipeline(renderPass, m_pipeline);

    // NOTE: viewproj uniform must be pushed on the command buffer BEFORE calling this
    // draw method. The caller (Renderer::doRender) handles this.

    // Bind vertex buffer
    SDL_GPUBufferBinding vertexBinding = { .buffer = m_vertexBuffer, .offset = 0 };
    SDL_BindGPUVertexBuffers(renderPass, 0, &vertexBinding, 1);

    // Bind index buffer
    SDL_GPUBufferBinding indexBinding = { .buffer = m_indexBuffer, .offset = 0 };
    SDL_BindGPUIndexBuffer(renderPass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_16BIT);

    // Bind cubemap texture + sampler
    SDL_GPUTextureSamplerBinding texSamplerBinding = {
        .texture = m_cubemapTexture,
        .sampler = m_cubemapSampler,
    };
    SDL_BindGPUFragmentSamplers(renderPass, 0, &texSamplerBinding, 1);

    // Draw 36 indices (12 triangles)
    SDL_DrawGPUIndexedPrimitives(renderPass, m_indexCount, 1, 0, 0, 0);
}

void Skybox::release()
{
    if (m_released)
        return;

    if (m_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(m_GPUDevice, m_pipeline);
        m_pipeline = nullptr;
    }
    if (m_vertexBuffer) {
        SDL_ReleaseGPUBuffer(m_GPUDevice, m_vertexBuffer);
        m_vertexBuffer = nullptr;
    }
    if (m_indexBuffer) {
        SDL_ReleaseGPUBuffer(m_GPUDevice, m_indexBuffer);
        m_indexBuffer = nullptr;
    }
    if (m_cubemapTexture) {
        SDL_ReleaseGPUTexture(m_GPUDevice, m_cubemapTexture);
        m_cubemapTexture = nullptr;
    }
    if (m_cubemapSampler) {
        SDL_ReleaseGPUSampler(m_GPUDevice, m_cubemapSampler);
        m_cubemapSampler = nullptr;
    }

    m_released = true;
}

Skybox::~Skybox()
{
    release();
}
