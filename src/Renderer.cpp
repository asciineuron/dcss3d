#include "Renderer.hpp"
#include "GameMap.hpp"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlgpu3.h"
#include <SDL3/SDL_gpu.h>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <spdlog/spdlog.h>
#include <stdexcept>

namespace fs = std::filesystem;

glm::mat4 Camera::toViewProjection() const
{
    float cosPhi = std::cos(phi);
    glm::vec3 lookDir = glm::normalize(glm::vec3(
        cosPhi * std::cos(-theta),
        std::sin(phi),
        cosPhi * std::sin(-theta)));

    glm::mat4 lookAt = glm::lookAt(pos, pos + lookDir, glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 perspective = glm::perspective(fov, aspectRatio, 0.1f, 100.0f);
    return perspective * lookAt;
}

SDL_GPUShader* loadShader(SDL_GPUDevice* device, const ShaderParameters& parameters)
{
    SDL_GPUShaderStage stage;
    if (parameters.filename.contains(".vert")) {
        stage = SDL_GPU_SHADERSTAGE_VERTEX;
    } else if (parameters.filename.contains(".frag")) {
        stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    } else {
        throw std::runtime_error(std::format("invalid shader extension for {}", parameters.filename));
    }

    SDL_GPUShaderFormat backendFormats = SDL_GetGPUShaderFormats(device);
    SDL_GPUShaderFormat format = SDL_GPU_SHADERFORMAT_INVALID;
    const char* entrypoint;
    std::string_view extension;
    if (backendFormats & SDL_GPU_SHADERFORMAT_SPIRV) {
        format = SDL_GPU_SHADERFORMAT_SPIRV;
        extension = ".spv";
        entrypoint = "main";
    } else if (backendFormats & SDL_GPU_SHADERFORMAT_MSL) {
        format = SDL_GPU_SHADERFORMAT_MSL;
        extension = ".metal";
        entrypoint = "main_0";
    } else if (backendFormats & SDL_GPU_SHADERFORMAT_DXIL) {
        format = SDL_GPU_SHADERFORMAT_DXIL;
        extension = ".dxil";
        entrypoint = "main";
    } else {
        throw std::runtime_error("unrecognized backend shader format");
    }

    const std::string shaderPath = std::format("{}shaders/{}{}", SDL_GetBasePath(), parameters.filename, extension);

    auto codeLen = fs::file_size(shaderPath);
    std::ifstream shaderFile(shaderPath);
    std::string code;
    code.resize_and_overwrite(codeLen, [&](char* buf, std::size_t len) { shaderFile.read(buf, len); return shaderFile.gcount(); });

    SDL_GPUShaderCreateInfo shaderInfo = {
        .code_size = codeLen,
        .code = (Uint8*)code.c_str(),
        .entrypoint = entrypoint,
        .format = format,
        .stage = stage,
        .num_samplers = parameters.samplerCount,
        .num_storage_textures = parameters.storageTextureCount,
        .num_storage_buffers = parameters.storageBufferCount,
        .num_uniform_buffers = parameters.uniformBufferCount,
    };
    SDL_GPUShader* shader = SDL_CreateGPUShader(device, &shaderInfo);
    if (!shader) {
        throw std::runtime_error("failed to create shader");
    }
    return shader;
}

Renderer::Renderer()
    : m_window { nullptr }
    , m_GPUDevice { nullptr }
    , m_windowID { 0 }
    , m_windowHeight { 0 }
    , m_windowWidth { 0 }
    , m_renderCount { 0 }
    , m_renderUI { false }
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        throw std::runtime_error(std::format("SDL_Init failure: {}", SDL_GetError()));
    }

    float displayScale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    SDL_WindowFlags windowFlags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;

    m_window = SDL_CreateWindow("dcss3d", std::floor(s_winW * displayScale), std::floor(s_winH * displayScale), windowFlags);
    if (!m_window)
        throw std::runtime_error(std::format("SDL_CreateWindow failed: {}", SDL_GetError()));

    m_windowID = SDL_GetWindowID(m_window);

    if (!SDL_GetWindowSizeInPixels(m_window, &m_windowWidth, &m_windowHeight))
        throw std::runtime_error(std::format("SDL_GetWindowSizeInPixels error: {}", SDL_GetError()));

    m_GPUDevice = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL, true, nullptr);
    if (!m_GPUDevice)
        throw std::runtime_error(std::format("SDL_CreateGPUDevice error: {}", SDL_GetError()));

    if (!SDL_ClaimWindowForGPUDevice(m_GPUDevice, m_window))
        throw std::runtime_error(std::format("SDL_ClaimWindowForGPUDevice error: {}", SDL_GetError()));

    if (!SDL_SetGPUSwapchainParameters(m_GPUDevice, m_window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, SDL_GPU_PRESENTMODE_VSYNC))
        throw std::runtime_error(std::format("SDL_SetGPUSwapchainParameters error: {}", SDL_GetError()));

    createDepthTexture();

    m_mapCubeModel = std::make_unique<MapDisplacedBufferedModel>(
        m_GPUDevice, m_window, std::make_unique<Model>("cube1.obj"),
        ShaderParameters { std::string_view("position_color_shifted.vert"), 0, 1, 0, 0 },
        ShaderParameters { std::string_view("lit.frag"), 0, 1, 0, 0 });

    // imgui:
    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad; // Enable Gamepad Controls

    // Setup Platform/Renderer backends
    ImGui_ImplSDL3_InitForSDLGPU(m_window);
    ImGui_ImplSDLGPU3_InitInfo initInfo = {};
    initInfo.Device = m_GPUDevice;
    initInfo.ColorTargetFormat = SDL_GetGPUSwapchainTextureFormat(m_GPUDevice, m_window);
    initInfo.PresentMode = SDL_GPU_PRESENTMODE_VSYNC;
    // initInfo.PresentMode = SDL_GPU_PRESENTMODE_IMMEDIATE;
    ImGui_ImplSDLGPU3_Init(&initInfo);
}
// TODO stuck acquiring swapchain texture...

void Renderer::doRender(GameMap& map, const Camera& camera)
{
    // do all rendering-related updates that don't require a command buffer pass first
    ImDrawData* drawData = m_renderUI ? ImGui::GetDrawData() : nullptr;
    // TODO determine even without UI, for now just defaults to false so need menu open to skip rendering...
    const bool isMinimized = m_renderUI ? (drawData->DisplaySize.x <= 0.0f || drawData->DisplaySize.y <= 0.0f) : false;

    // TODO instead have MapDisplacedBufferedModel do all the binding itself? and just call its render func which does this?
    glm::mat4 cameraView = camera.toViewProjection();

    // Debug: check if map is empty
    if (!map.didRender() && map.map().empty()) {
        spdlog::debug("Map is empty, nothing to render yet");
    }

    SDL_GPUCommandBuffer* commandBuffer = SDL_AcquireGPUCommandBuffer(m_GPUDevice);

    SDL_GPUTexture* swapchainTexture = NULL;
    // spdlog::debug("Acquiring swapchain texture...");
    SDL_WaitAndAcquireGPUSwapchainTexture(commandBuffer, m_window, &swapchainTexture, NULL, NULL);
    // spdlog::debug("Swapchain texture acquired: {}", swapchainTexture ? "yes" : "no");

    if (swapchainTexture && !isMinimized) {
        // Check for window resize and recreate depth texture if needed
        int newWidth, newHeight;
        SDL_GetWindowSizeInPixels(m_window, &newWidth, &newHeight);
        if (newWidth != m_windowWidth || newHeight != m_windowHeight) {
            m_windowWidth = newWidth;
            m_windowHeight = newHeight;
            releaseDepthTexture();
            createDepthTexture();
        }

        // upload all data via copy passes
        if (!map.didRender()) {
            pushMapToGPU(map, commandBuffer);
            map.setDidRender(true);
        }

        if (m_renderUI)
            ImGui_ImplSDLGPU3_PrepareDrawData(drawData, commandBuffer);

        // do actual rendering
        SDL_GPUColorTargetInfo targetInfo = { 0 };
        targetInfo.texture = swapchainTexture;
        targetInfo.clear_color = (SDL_FColor) { 0.0f, 0.0f, 0.0f, 1.0f };
        targetInfo.load_op = SDL_GPU_LOADOP_CLEAR;
        targetInfo.store_op = SDL_GPU_STOREOP_STORE;

        SDL_GPUDepthStencilTargetInfo depthTargetInfo = {
            .texture = m_depthTexture,
            .clear_depth = 1.0f,
            .load_op = SDL_GPU_LOADOP_CLEAR,
            .store_op = SDL_GPU_STOREOP_DONT_CARE,
            .stencil_load_op = SDL_GPU_LOADOP_DONT_CARE,
            .stencil_store_op = SDL_GPU_STOREOP_DONT_CARE,
            .cycle = true,
            .clear_stencil = 0,
            .mip_level = 0,
            .layer = 0,
        };

        SDL_GPURenderPass* renderPass = SDL_BeginGPURenderPass(commandBuffer, &targetInfo, 1, &depthTargetInfo);

        SDL_PushGPUVertexUniformData(commandBuffer, 0, glm::value_ptr(cameraView), sizeof(cameraView));

        // Push light/camera data as fragment uniform
        LightUniforms lightData = {
            .lightPos = glm::vec4(camera.pos, 1.0f),
            .cameraPos = glm::vec4(camera.pos, 1.0f),
        };
        SDL_PushGPUFragmentUniformData(commandBuffer, 0, &lightData, sizeof(lightData));

        m_mapCubeModel->draw(renderPass);

        if (m_renderUI)
            ImGui_ImplSDLGPU3_RenderDrawData(drawData, commandBuffer, renderPass);

        SDL_EndGPURenderPass(renderPass);
    }

    SDL_SubmitGPUCommandBuffer(commandBuffer);

    ++m_renderCount;
}

const uint64_t Renderer::renderCount() const
{
    return m_renderCount;
}

// delegate to the MapDisplacedBufferedModel?
void Renderer::pushMapToGPU(const GameMap& map, SDL_GPUCommandBuffer* cmdBuf)
{
    m_mapCubeModel->pushMapData(map, cmdBuf);
}

Renderer::~Renderer()
{
    // shared ptrs automatically deleted with proper funcs
    // TODO causes segfault, maybe since child Model class frees afterwards?
    m_mapCubeModel->release();

    releaseDepthTexture();

    ImGui_ImplSDL3_Shutdown();
    ImGui_ImplSDLGPU3_Shutdown();
    ImGui::DestroyContext();

    SDL_ReleaseWindowFromGPUDevice(m_GPUDevice, m_window);
    SDL_DestroyGPUDevice(m_GPUDevice);
    SDL_DestroyWindow(m_window);

    SDL_Quit();
}

void Renderer::createDepthTexture()
{
    SDL_GPUTextureCreateInfo depthCreateInfo = {
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_D16_UNORM,
        .usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET,
        .width = static_cast<Uint32>(m_windowWidth),
        .height = static_cast<Uint32>(m_windowHeight),
        .layer_count_or_depth = 1,
        .num_levels = 1,
        .sample_count = SDL_GPU_SAMPLECOUNT_1,
        .props = 0,
    };
    m_depthTexture = SDL_CreateGPUTexture(m_GPUDevice, &depthCreateInfo);
    if (!m_depthTexture)
        throw std::runtime_error(std::format("SDL_CreateGPUTexture (depth) error: {}", SDL_GetError()));
}

void Renderer::releaseDepthTexture()
{
    if (m_depthTexture) {
        SDL_ReleaseGPUTexture(m_GPUDevice, m_depthTexture);
        m_depthTexture = nullptr;
    }
}

Model::Model(std::string_view filename)
    : m_resourcePath(std::format("{}{}", SDL_GetBasePath(), "resources"))
{
    loadObj(filename);
}

void Model::loadObj(std::string_view filename)
{
    m_name = filename;
    std::string fullFilename = std::format("{}/{}", m_resourcePath, filename);

    std::ifstream inFile(fullFilename);
    if (!inFile)
        throw std::runtime_error(std::format("Failed to open file {}", fullFilename));

    std::string line;
    while (std::getline(inFile, line)) {
        std::stringstream ss(line);
        std::string type;
        ss >> type;
        if (type == "v") {
            glm::vec3 vertex;
            ss >> vertex.x >> vertex.y >> vertex.z;
            m_vertices.push_back(std::move(vertex));
        } else if (type == "vt") {
            glm::vec2 uv;
            ss >> uv.x >> uv.y;
            m_uvs.push_back(std::move(uv));
        } else if (type == "f") {
            // TODO: handle 3 vs 6 form variants?
            Face face;
            ss >> face.vertexIndices[0] >> face.vertexIndices[1] >> face.vertexIndices[2];
            // 0-based indexing:
            --(face.vertexIndices[0]);
            --(face.vertexIndices[1]);
            --(face.vertexIndices[2]);
            m_faces.push_back(std::move(face));
        } else {
            continue;
        }
    }

    // Compute per-face flat normals and expand indexed geometry to non-indexed.
    // Each triangle gets its own 3 vertices with the face's normal.
    // This duplicates shared vertices (e.g., cube corners appear 3 times) but
    // gives correct per-face flat shading for lighting.
    std::vector<glm::vec3> expandedVertices;
    std::vector<glm::vec3> expandedNormals;
    std::vector<Face> newFaces;

    for (size_t f = 0; f < m_faces.size(); ++f) {
        const auto& face = m_faces[f];
        // Compute face normal from first 3 vertices (cross product of edges)
        glm::vec3 v0 = m_vertices[face.vertexIndices[0]];
        glm::vec3 v1 = m_vertices[face.vertexIndices[1]];
        glm::vec3 v2 = m_vertices[face.vertexIndices[2]];
        glm::vec3 edge1 = v1 - v0;
        glm::vec3 edge2 = v2 - v0;
        glm::vec3 normal = glm::normalize(glm::cross(edge1, edge2));

        // Emit 3 vertices with position and normal for this triangle
        uint32_t baseIdx = static_cast<uint32_t>(expandedVertices.size());
        expandedVertices.push_back(v0);
        expandedNormals.push_back(normal);
        expandedVertices.push_back(v1);
        expandedNormals.push_back(normal);
        expandedVertices.push_back(v2);
        expandedNormals.push_back(normal);

        // New face references the expanded indices
        newFaces.push_back({
            { static_cast<Uint16>(baseIdx), static_cast<Uint16>(baseIdx + 1), static_cast<Uint16>(baseIdx + 2) },
            { 0, 0, 0 }  // no texture indices
        });
    }

    m_vertices = std::move(expandedVertices);
    m_normals = std::move(expandedNormals);
    m_faces = std::move(newFaces);
}

BufferedModel::BufferedModel(SDL_GPUDevice* gpu, SDL_Window* window, std::unique_ptr<Model> model,
    ShaderParameters vertex, ShaderParameters fragment)
    : m_model { std::move(model) }
    , m_GPUDevice { gpu }
    // Vertex data: position (3 floats) + normal (3 floats) = 6 floats per vertex
    , m_vertexBufSize { static_cast<Uint32>(sizeof(glm::vec3) * 2 * (m_model->vertices().size())) }
    , m_indexBufSize { static_cast<Uint16>(sizeof(Uint16) * 3 * m_model->faces().size()) }
    , m_drawBufSize { sizeof(SDL_GPUIndexedIndirectDrawCommand) * 1 }
{
    m_pipeline = createGraphicsPipelineWithShaders(window, vertex, fragment);

    // set up buffers next:
    SDL_GPUBufferCreateInfo vertexInfo = {
        .usage = SDL_GPU_BUFFERUSAGE_VERTEX,
        .size = m_vertexBufSize
    };
    SDL_GPUBufferCreateInfo indexInfo = {
        .usage = SDL_GPU_BUFFERUSAGE_INDEX,
        .size = m_indexBufSize
    };
    SDL_GPUBufferCreateInfo drawInfo = {
        .usage = SDL_GPU_BUFFERUSAGE_INDIRECT,
        .size = m_drawBufSize
    };
    SDL_GPUTransferBufferCreateInfo drawTransferInfo = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = (Uint32)sizeof(SDL_GPUIndexedIndirectDrawCommand)
    };
    m_vertexBuffer = SDL_CreateGPUBuffer(m_GPUDevice, &vertexInfo);
    m_indexBuffer = SDL_CreateGPUBuffer(m_GPUDevice, &indexInfo);
    m_drawBuffer = SDL_CreateGPUBuffer(m_GPUDevice, &drawInfo);
    m_drawTransferBuf = SDL_CreateGPUTransferBuffer(m_GPUDevice, &drawTransferInfo);
    // then map model data to gpu:
    uploadModel();
}

void BufferedModel::uploadModel()
{
    SDL_GPUTransferBufferCreateInfo tempVertexIndexTransferInfo = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = m_vertexBufSize + m_indexBufSize
    };
    SDL_GPUTransferBuffer* tempVertexIndexTransfer = SDL_CreateGPUTransferBuffer(
        m_GPUDevice, &tempVertexIndexTransferInfo);

    // Interleaved vertex format: position (12 bytes) + normal (12 bytes) per vertex
    struct VertexPN {
        glm::vec3 pos;
        glm::vec3 normal;
    };
    VertexPN* vertexTransfer = (VertexPN*)SDL_MapGPUTransferBuffer(m_GPUDevice, tempVertexIndexTransfer, false);
    for (size_t i = 0; i < m_model->vertices().size(); i++) {
        vertexTransfer[i].pos = m_model->vertices()[i];
        vertexTransfer[i].normal = m_model->normals()[i];
    }

    // Index buffer: triangle i starts at index 3*i (vertex i*3, i*3+1, i*3+2)
    // Since expanded vertices are non-indexed (each face has unique consecutive vertices)
    Uint16* indexTransfer = (Uint16*)&vertexTransfer[m_model->vertices().size()];
    for (size_t i = 0; i < m_model->faces().size(); i++) {
        indexTransfer[3 * i] = static_cast<Uint16>(3 * i);
        indexTransfer[3 * i + 1] = static_cast<Uint16>(3 * i + 1);
        indexTransfer[3 * i + 2] = static_cast<Uint16>(3 * i + 2);
    }
    SDL_UnmapGPUTransferBuffer(m_GPUDevice, tempVertexIndexTransfer);

    SDL_GPUIndexedIndirectDrawCommand* drawTransfer = (SDL_GPUIndexedIndirectDrawCommand*)SDL_MapGPUTransferBuffer(
        m_GPUDevice, m_drawTransferBuf, true);
    drawTransfer[0] = (SDL_GPUIndexedIndirectDrawCommand) {
        .num_indices = (Uint32)(3 * m_model->faces().size()),
        .num_instances = 1,
        .first_index = 0,
        .vertex_offset = 0,
        .first_instance = 0
    };
    SDL_UnmapGPUTransferBuffer(m_GPUDevice, m_drawTransferBuf);

    // then actually upload the mapping to gpu:
    SDL_GPUCommandBuffer* cmdBuf = SDL_AcquireGPUCommandBuffer(m_GPUDevice);
    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmdBuf);

    SDL_GPUTransferBufferLocation tempVertexIndexTransferLocation = { .transfer_buffer = tempVertexIndexTransfer, .offset = 0 };
    SDL_GPUBufferRegion tempVertexIndexTransferRegion = { .buffer = m_vertexBuffer, .offset = 0, .size = m_vertexBufSize };
    SDL_UploadToGPUBuffer(
        copyPass,
        &tempVertexIndexTransferLocation,
        &tempVertexIndexTransferRegion,
        false);

    SDL_GPUTransferBufferLocation drawTransferLocation = { .transfer_buffer = tempVertexIndexTransfer, .offset = m_vertexBufSize };
    SDL_GPUBufferRegion drawTransferRegion = { .buffer = m_indexBuffer, .offset = 0, .size = m_indexBufSize };
    SDL_UploadToGPUBuffer(
        copyPass,
        &drawTransferLocation,
        &drawTransferRegion,
        false);

    SDL_GPUTransferBufferLocation drawBufferLocation = { .transfer_buffer = m_drawTransferBuf, .offset = 0 };
    SDL_GPUBufferRegion drawBufferRegion = { .buffer = m_drawBuffer, .offset = 0, .size = m_drawBufSize };
    SDL_UploadToGPUBuffer(copyPass,
        &drawBufferLocation,
        &drawBufferRegion,
        false);

    SDL_EndGPUCopyPass(copyPass);
    SDL_SubmitGPUCommandBuffer(cmdBuf);
    SDL_ReleaseGPUTransferBuffer(m_GPUDevice, tempVertexIndexTransfer); // not used again
}

void BufferedModel::release()
{
    if (!m_hasReleased) {
        SDL_ReleaseGPUGraphicsPipeline(m_GPUDevice, m_pipeline);
        SDL_ReleaseGPUBuffer(m_GPUDevice, m_vertexBuffer);
        SDL_ReleaseGPUBuffer(m_GPUDevice, m_indexBuffer);
        SDL_ReleaseGPUBuffer(m_GPUDevice, m_drawBuffer);
        SDL_ReleaseGPUTransferBuffer(m_GPUDevice, m_drawTransferBuf);
    }
    m_hasReleased = true;
}

BufferedModel::~BufferedModel()
{
    release();
}

MapDisplacedBufferedModel::MapDisplacedBufferedModel(SDL_GPUDevice* gpu, SDL_Window* window, std::unique_ptr<Model> model,
    ShaderParameters vertex, ShaderParameters fragment)
    : BufferedModel(gpu, window, std::move(model), vertex, fragment)
{
    SDL_GPUBufferCreateInfo mapBufferCreateInfo = {
        .usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
        .size = (Uint32)(s_maxRenderCopies * sizeof(DisplacementColorInfo))
    };
    m_mapDataBuffer = SDL_CreateGPUBuffer(m_GPUDevice, &mapBufferCreateInfo);

    SDL_GPUTransferBufferCreateInfo mapTransferCreateInfo = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = (Uint32)(s_maxRenderCopies * sizeof(DisplacementColorInfo))
    };
    m_dataTransferBuf = SDL_CreateGPUTransferBuffer(m_GPUDevice, &mapTransferCreateInfo);
}

void MapDisplacedBufferedModel::pushMapData(const GameMap& map, SDL_GPUCommandBuffer* cmdBuf)
{
    // displacement, color info (per-instance vertex buffer):
    DisplacementColorInfo* mappedDisplacementColor = (DisplacementColorInfo*)SDL_MapGPUTransferBuffer(
        m_GPUDevice, m_dataTransferBuf, true);
    for (auto idx = 0; const auto& [mapCoord, tile] : map.map()) {
        glm::vec2 renderCoords2D = mapCoordToRender(mapCoord);
        mappedDisplacementColor[idx].shiftX = renderCoords2D.x;
        mappedDisplacementColor[idx].shiftY = renderCoords2D.y;
        mappedDisplacementColor[idx].tileType = static_cast<float>(std::to_underlying(tile.type()));
        mappedDisplacementColor[idx].padding1 = 0.0f;
        mappedDisplacementColor[idx].color = mapTypeToColor(tile.type());
        // TODO: hard limit on # map entries
        if (++idx >= s_maxRenderCopies)
            break;
    }
    SDL_UnmapGPUTransferBuffer(m_GPUDevice, m_dataTransferBuf);

    // draw commands to set the # instances of map tiles:
    SDL_GPUIndexedIndirectDrawCommand* drawTransfer = (SDL_GPUIndexedIndirectDrawCommand*)SDL_MapGPUTransferBuffer(
        m_GPUDevice, m_drawTransferBuf, true);
    drawTransfer[0] = (SDL_GPUIndexedIndirectDrawCommand) {
        .num_indices = static_cast<Uint32>(3 * m_model->faces().size()),
        // NOTE: setting num_instances:
        .num_instances = static_cast<Uint32>(map.map().size()),
        .first_index = 0,
        .vertex_offset = 0,
        .first_instance = 0
    };
    SDL_UnmapGPUTransferBuffer(m_GPUDevice, m_drawTransferBuf);

    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmdBuf);

    SDL_GPUTransferBufferLocation mapBufferLocation = { .transfer_buffer = m_dataTransferBuf, .offset = 0 };
    SDL_GPUBufferRegion mapBufferRegion = { .buffer = m_mapDataBuffer, .offset = 0, .size = s_maxRenderCopies * sizeof(DisplacementColorInfo) };
    SDL_UploadToGPUBuffer(copyPass,
        &mapBufferLocation,
        &mapBufferRegion,
        true);

    SDL_GPUTransferBufferLocation drawBufferLocation = { .transfer_buffer = m_drawTransferBuf, .offset = 0 };
    SDL_GPUBufferRegion drawBufferRegion = { .buffer = m_drawBuffer, .offset = 0, .size = m_drawBufSize };
    SDL_UploadToGPUBuffer(
        copyPass,
        &drawBufferLocation,
        &drawBufferRegion,
        true);

    SDL_EndGPUCopyPass(copyPass);
}

void MapDisplacedBufferedModel::release()
{
    // add release here
    if (!m_hasReleased) {
        SDL_ReleaseGPUBuffer(m_GPUDevice, m_mapDataBuffer);
        SDL_ReleaseGPUTransferBuffer(m_GPUDevice, m_dataTransferBuf);
    }
    m_hasReleased = true;
    BufferedModel::release();
}

MapDisplacedBufferedModel::~MapDisplacedBufferedModel()
{
    release();
}

SDL_GPUGraphicsPipeline* BufferedModel::createGraphicsPipelineWithShaders(SDL_Window* window, ShaderParameters vertex, ShaderParameters fragment)
{
    SDL_GPUShader* vertexShader = loadShader(m_GPUDevice, vertex);
    SDL_GPUShader* fragShader = loadShader(m_GPUDevice, fragment);

    // Vertex format: position (12 bytes) + normal (12 bytes) = 24 bytes stride
    // Per-instance vertex buffer: tile shift (12 bytes) + color (16 bytes) = 28 bytes
    // Both buffers use VERTEX input rate (no INSTANCE — Metal restriction on vertex buffers)
    SDL_GPUVertexAttribute vertexAttributes[] = {
        { .location = 0,
            .buffer_slot = 0,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
            .offset = 0 },                       // position at offset 0
        { .location = 1,
            .buffer_slot = 0,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
            .offset = sizeof(glm::vec3) },       // normal at offset 12
        { .location = 2,
            .buffer_slot = 1,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
            .offset = 0 },                       // tile shift at offset 0
        { .location = 3,
            .buffer_slot = 1,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,
            .offset = sizeof(glm::vec3) }        // tile color at offset 12
    };
    SDL_GPUVertexBufferDescription vertexBufferDescriptions[] = {
        { .slot = 0,
            .pitch = 2 * sizeof(glm::vec3),    // stride: position + normal
            .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
            .instance_step_rate = 0 },
        { .slot = 1,
            .pitch = sizeof(glm::vec3) + sizeof(glm::vec4),  // stride: shift + color
            .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
            .instance_step_rate = 0 }
    };
    // TODO add more vertex buffers here, one for map, one for each actor etc?
    SDL_GPUVertexInputState vertexInputState = {
        .vertex_buffer_descriptions = vertexBufferDescriptions,
        .num_vertex_buffers = 2,
        .vertex_attributes = vertexAttributes,
        .num_vertex_attributes = 4,             // position + normal + shift + color
    };
    SDL_GPUColorTargetDescription colorTargetDescriptions[] = {
        { .format = SDL_GetGPUSwapchainTextureFormat(
              m_GPUDevice, window) }
    };
    SDL_GPURasterizerState rasterizer_state = {
        .fill_mode = SDL_GPU_FILLMODE_FILL,    // Filled polygons (not wireframe)
        .cull_mode = SDL_GPU_CULLMODE_BACK,    // Cull back faces
        .front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE,
        .depth_bias_constant_factor = 0.0f,
        .depth_bias_clamp = 0.0f,
        .depth_bias_slope_factor = 0.0f,
        .enable_depth_bias = false,
        .enable_depth_clip = true,
    };
    SDL_GPUDepthStencilState depthStencilState = {
        .compare_op = SDL_GPU_COMPAREOP_LESS,  // Pass if new depth < stored depth
        .back_stencil_state = {},
        .front_stencil_state = {},
        .compare_mask = 0,
        .write_mask = 0,
        .enable_depth_test = true,
        .enable_depth_write = false,  // Don't write depth — imgui renders on top without depth write
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
        .rasterizer_state = rasterizer_state,
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

    return pipeline;
}

void BufferedModel::draw(SDL_GPURenderPass* renderPass)
{
    // TODO not tested without map displacement...
    SDL_BindGPUGraphicsPipeline(renderPass, m_pipeline);

    SDL_GPUBufferBinding vertexBufferBinding = { .buffer = m_vertexBuffer, .offset = 0 };
    SDL_BindGPUVertexBuffers(renderPass, 0, &vertexBufferBinding, 1);

    SDL_GPUBufferBinding indexBufferBinding = { .buffer = m_indexBuffer, .offset = 0 };
    SDL_BindGPUIndexBuffer(renderPass, &indexBufferBinding, SDL_GPU_INDEXELEMENTSIZE_16BIT);

    SDL_DrawGPUIndexedPrimitivesIndirect(renderPass, m_drawBuffer, 0, 1);
}

void MapDisplacedBufferedModel::draw(SDL_GPURenderPass* renderPass)
{
    SDL_BindGPUGraphicsPipeline(renderPass, m_pipeline);

    // Bind vertex buffers:
    // Slot 0: per-vertex position + normal (24 bytes per vertex)
    SDL_GPUBufferBinding vertexBufferBinding = { .buffer = m_vertexBuffer, .offset = 0 };
    SDL_BindGPUVertexBuffers(renderPass, 0, &vertexBufferBinding, 1);

    // Slot 1: per-instance data (shift + color)
    SDL_GPUBufferBinding instanceBufferBinding = { .buffer = m_mapDataBuffer, .offset = 0 };
    SDL_BindGPUVertexBuffers(renderPass, 1, &instanceBufferBinding, 1);

    // Bind index buffer
    SDL_GPUBufferBinding indexBufferBinding = { .buffer = m_indexBuffer, .offset = 0 };
    SDL_BindGPUIndexBuffer(renderPass, &indexBufferBinding, SDL_GPU_INDEXELEMENTSIZE_16BIT);

    SDL_DrawGPUIndexedPrimitivesIndirect(renderPass, m_drawBuffer, 0, 1);
}

std::ostream&
operator<<(std::ostream& os, const Model& model)
{
    os << "model: " << model.m_name << "\n";
    os << "vertices: \n";
    for (const auto& vertex : model.m_vertices) {
        os << "( " << vertex.x
           << " " << vertex.y
           << " " << vertex.z << " )\n";
    }
    os << "uvs: \n";
    for (const auto& uv : model.m_uvs) {
        os << "( " << uv.x
           << " " << uv.y << " )\n";
    }
    os << "face vertex indices: \n";
    for (const auto& face : model.m_faces) {
        os << "( " << face.vertexIndices[0]
           << " " << face.vertexIndices[1]
           << " " << face.vertexIndices[2] << " )\n";
    }
    return os;
}
