#include "Renderer.hpp"
#include "GameMap.hpp"
#include "imguilayouts.hpp"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlgpu3.h"
#include <SDL3/SDL_gpu.h>
#include <SDL3_image/SDL_image.h>
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

glm::mat4 Camera::toSkyViewProjection() const
{
    float cosPhi = std::cos(phi);
    glm::vec3 lookDir = glm::normalize(glm::vec3(
        cosPhi * std::cos(-theta),
        std::sin(phi),
        cosPhi * std::sin(-theta)));

    glm::mat4 lookAt = glm::lookAt(pos, pos + lookDir, glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 perspective = glm::perspective(fov, aspectRatio, 0.1f, 100.0f);
    // Strip translation: convert to 3x3 (rotation only) and back to 4x4.
    // This keeps the skybox centered on the camera regardless of position.
    return perspective * glm::mat4(glm::mat3(lookAt));
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

    // Skybox must be created after depth texture and before models
    m_skybox = std::make_unique<Skybox>(m_GPUDevice, m_window);

    m_mapCubeModel = std::make_unique<MapDisplacedBufferedModel>(
        m_GPUDevice, m_window, std::make_unique<Model>("cube1.obj"),
        ShaderParameters { std::string_view("position_color_shifted.vert"), 0, 1, 0, 0 },
        ShaderParameters { std::string_view("lit.frag"), 1, 2, 0, 0 },
        std::string_view("resources/cube1_diffuse.png"));

    // Monster models are lazily created per OBJ file via getOrCreateMonsterModel().
    // No upfront creation — the model cache is populated on first monster data push.

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
    const bool shouldRenderUI = m_renderUI || anyWindowsPinned();
    ImDrawData* drawData = shouldRenderUI ? ImGui::GetDrawData() : nullptr;
    // TODO determine even without UI, for now just defaults to false so need menu open to skip rendering...
    const bool isMinimized = shouldRenderUI ? (drawData->DisplaySize.x <= 0.0f || drawData->DisplaySize.y <= 0.0f) : false;

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
            pushMonsterToGPU(map, commandBuffer);
            map.setDidRender(true);
        }

        if (shouldRenderUI)
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

        // Pass 1: 3D scene — render map cubes with depth testing
        SDL_GPURenderPass* scenePass = SDL_BeginGPURenderPass(commandBuffer, &targetInfo, 1, &depthTargetInfo);

        SDL_PushGPUVertexUniformData(commandBuffer, 0, glm::value_ptr(cameraView), sizeof(cameraView));

        // Push light/camera data as fragment uniform
        LightUniforms lightData = {
            .lightPos = glm::vec4(camera.pos, 1.0f),
            .cameraPos = glm::vec4(camera.pos, 1.0f),
        };
        SDL_PushGPUFragmentUniformData(commandBuffer, 0, &lightData, sizeof(lightData));

        // Push target cell highlight position as fragment uniform slot 1
        SDL_PushGPUFragmentUniformData(commandBuffer, 1, &m_targetHighlightPos, sizeof(m_targetHighlightPos));

        m_mapCubeModel->draw(scenePass);

        // Push player 2D position for monster look-at rotation (vertex uniform slot 1)
        glm::vec4 playerPos2D(camera.pos.x, camera.pos.z, 0.0f, 0.0f);
        SDL_PushGPUVertexUniformData(commandBuffer, 1, glm::value_ptr(playerPos2D), sizeof(playerPos2D));

        // Draw monsters on top (depth-tested, same pass) — one draw per model type
        for (auto& [file, model] : m_monsterModelCache) {
            model->draw(scenePass);
        }

        // Draw skybox LAST — uses xyww trick (depth=1.0) with LESS_OR_EQUAL test.
        // Only fragments where no scene geometry was drawn will be shaded.
        // View matrix has translation stripped so skybox stays centered on camera.
        // Must push uniform on commandBuffer (not renderPass) before the draw call.
        glm::mat4 skyViewProj = camera.toSkyViewProjection();
        SDL_PushGPUVertexUniformData(commandBuffer, 0, glm::value_ptr(skyViewProj), sizeof(skyViewProj));
        m_skybox->draw(scenePass, skyViewProj);

        SDL_EndGPURenderPass(scenePass);

        // Pass 2: ImGui — render on top, no depth buffer (which otherwise covers the window contents)
        if (shouldRenderUI) {
            SDL_GPUColorTargetInfo uiTargetInfo = {
                .texture = swapchainTexture,
                .load_op = SDL_GPU_LOADOP_LOAD,
                .store_op = SDL_GPU_STOREOP_STORE,
            };
            SDL_GPURenderPass* uiPass = SDL_BeginGPURenderPass(commandBuffer, &uiTargetInfo, 1, NULL);
            ImGui_ImplSDLGPU3_RenderDrawData(drawData, commandBuffer, uiPass);
            SDL_EndGPURenderPass(uiPass);
        }
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

void Renderer::pushMonsterToGPU(const GameMap& map, SDL_GPUCommandBuffer* cmdBuf)
{
    const auto& monsterPositions = map.monsterPositions();
    const auto& monsterTable = map.monsterTable();

    // Group monsters by visual type (btype if polymorph/derived, otherwise type).
    // This ensures e.g. a gnoll zombie renders as a gnoll, not a generic zombie.
    std::unordered_map<std::string, std::vector<std::pair<Pos2<int>, const Monster*>>> grouped;

    for (const auto& [pos, monId] : monsterPositions) {
        auto tableIt = monsterTable.find(monId);
        if (tableIt == monsterTable.end())
            continue;

        const Monster& mon = tableIt->second;
        const std::string& modelFile = getModelFileForType(mon);
        grouped[modelFile].push_back({pos, &mon});
    }

    // Push each group to its corresponding model
    for (auto& [modelFile, monsters] : grouped) {
        MonsterBufferedModel* model = getOrCreateMonsterModel(modelFile);
        model->pushMonsterData(monsters, cmdBuf);
    }
}

Renderer::~Renderer()
{
    // shared ptrs automatically deleted with proper funcs
    // TODO causes segfault, maybe since child Model class frees afterwards?
    m_mapCubeModel->release();
    m_skybox.reset();  // release before GPU device is destroyed

    // Release all cached monster models
    for (auto& [file, model] : m_monsterModelCache) {
        model->release();
    }
    m_monsterModelCache.clear();

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
        } else if (type == "vn") {
            glm::vec3 normal;
            ss >> normal.x >> normal.y >> normal.z;
            m_rawNormals.push_back(std::move(normal));
        } else if (type == "f") {
            // Handle OBJ face formats: v, v/t, v/t/n, v//n
            // Supports both triangles (3 verts) and quads (4 verts).
            // Quads are triangulated into two triangles: (0,1,2) and (0,2,3).

            struct ObjIndex {
                int vIdx;   // vertex index, 0-based
                int tIdx;   // texture index, 0-based (0 = absent)
                int nIdx;   // normal index, 0-based (0 = absent)
            };

            // Parse vertex/texture/normal indices from formats like "5", "5/1", "5/1/1", "5//1"
            // vIdx and tIdx are 0-based; nIdx is kept 1-based so 0 = absent sentinel.
            auto parseObjIndex = [](const std::string& s) -> ObjIndex {
                size_t slash1 = s.find('/');
                int vIdx = std::stoi(s.substr(0, slash1)) - 1; // 0-based
                int tIdx = 0;
                int nIdx = 0;  // 0 = absent, 1+ = OBJ normal index
                if (slash1 != std::string::npos) {
                    size_t slash2 = s.find('/', slash1 + 1);
                    std::string texPart = s.substr(slash1 + 1, slash2 - slash1 - 1);
                    if (!texPart.empty()) {
                        tIdx = std::stoi(texPart) - 1;
                    }
                    if (slash2 != std::string::npos) {
                        std::string normPart = s.substr(slash2 + 1);
                        if (!normPart.empty()) {
                            nIdx = std::stoi(normPart); // 1-based, kept as-is
                        }
                    }
                }
                return {vIdx, tIdx, nIdx};
            };

            // Collect all face vertex strings (3 or 4)
            std::vector<std::string> faceVerts;
            std::string v;
            while (ss >> v)
                faceVerts.push_back(v);

            if (faceVerts.size() < 3) continue; // malformed

            // Emit triangle 0-1-2
            auto i0 = parseObjIndex(faceVerts[0]);
            auto i1 = parseObjIndex(faceVerts[1]);
            auto i2 = parseObjIndex(faceVerts[2]);
            m_faces.push_back({
                {static_cast<Uint16>(i0.vIdx), static_cast<Uint16>(i1.vIdx), static_cast<Uint16>(i2.vIdx)},
                {static_cast<Uint16>(i0.tIdx), static_cast<Uint16>(i1.tIdx), static_cast<Uint16>(i2.tIdx)},
                {static_cast<Uint16>(i0.nIdx), static_cast<Uint16>(i1.nIdx), static_cast<Uint16>(i2.nIdx)}
            });

            // If quad, emit second triangle 0-2-3
            if (faceVerts.size() >= 4) {
                auto i3 = parseObjIndex(faceVerts[3]);
                m_faces.push_back({
                    {static_cast<Uint16>(i0.vIdx), static_cast<Uint16>(i2.vIdx), static_cast<Uint16>(i3.vIdx)},
                    {static_cast<Uint16>(i0.tIdx), static_cast<Uint16>(i2.tIdx), static_cast<Uint16>(i3.tIdx)},
                    {static_cast<Uint16>(i0.nIdx), static_cast<Uint16>(i2.nIdx), static_cast<Uint16>(i3.nIdx)}
                });
            }
        } else {
            continue;
        }
    }

    // Expand indexed geometry to non-indexed, with per-vertex normals.
    // If the OBJ has vertex normals (vn), use them for smooth shading.
    // Otherwise compute flat per-face normals (cross product).
    const bool hasVertexNormals = !m_rawNormals.empty();

    std::vector<glm::vec3> expandedVertices;
    std::vector<glm::vec3> expandedNormals;
    std::vector<glm::vec2> expandedUVs;  // Store expanded UVs
    std::vector<Face> newFaces;

    for (size_t f = 0; f < m_faces.size(); ++f) {
        const auto& face = m_faces[f];
        glm::vec3 v0 = m_vertices[face.vertexIndices[0]];
        glm::vec3 v1 = m_vertices[face.vertexIndices[1]];
        glm::vec3 v2 = m_vertices[face.vertexIndices[2]];

        // Per-corner normals: use OBJ vertex normals if present, else compute flat face normal
        glm::vec3 n0, n1, n2;
        if (hasVertexNormals
            && face.normalIndices[0] > 0 && face.normalIndices[0] <= m_rawNormals.size()
            && face.normalIndices[1] > 0 && face.normalIndices[1] <= m_rawNormals.size()
            && face.normalIndices[2] > 0 && face.normalIndices[2] <= m_rawNormals.size()) {
            n0 = m_rawNormals[face.normalIndices[0] - 1];
            n1 = m_rawNormals[face.normalIndices[1] - 1];
            n2 = m_rawNormals[face.normalIndices[2] - 1];
        } else {
            // Fallback: compute face normal from cross product of edges
            glm::vec3 edge1 = v1 - v0;
            glm::vec3 edge2 = v2 - v0;
            glm::vec3 faceNormal = glm::normalize(glm::cross(edge1, edge2));
            n0 = n1 = n2 = faceNormal;
        }

        // Emit 3 vertices with position, normal, and UV for this triangle
        uint32_t baseIdx = static_cast<uint32_t>(expandedVertices.size());

        // Vertex 0
        expandedVertices.push_back(v0);
        expandedNormals.push_back(n0);
        if (face.textureIndices[0] < m_uvs.size()) {
            expandedUVs.push_back(m_uvs[face.textureIndices[0]]);
        } else {
            // Fallback: generate cube UV based on normal (use n0 for axis decision)
            if (std::abs(n0.z) > 0.9f) {
                expandedUVs.push_back(glm::vec2(v0.x + 0.5f, v0.y + 0.5f));
            } else if (std::abs(n0.x) > 0.9f) {
                expandedUVs.push_back(glm::vec2(v0.z + 0.5f, v0.y + 0.5f));
            } else {
                expandedUVs.push_back(glm::vec2(v0.x + 0.5f, v0.z + 0.5f));
            }
        }

        // Vertex 1
        expandedVertices.push_back(v1);
        expandedNormals.push_back(n1);
        if (face.textureIndices[1] < m_uvs.size()) {
            expandedUVs.push_back(m_uvs[face.textureIndices[1]]);
        } else {
            if (std::abs(n1.z) > 0.9f) {
                expandedUVs.push_back(glm::vec2(v1.x + 0.5f, v1.y + 0.5f));
            } else if (std::abs(n1.x) > 0.9f) {
                expandedUVs.push_back(glm::vec2(v1.z + 0.5f, v1.y + 0.5f));
            } else {
                expandedUVs.push_back(glm::vec2(v1.x + 0.5f, v1.z + 0.5f));
            }
        }

        // Vertex 2
        expandedVertices.push_back(v2);
        expandedNormals.push_back(n2);
        if (face.textureIndices[2] < m_uvs.size()) {
            expandedUVs.push_back(m_uvs[face.textureIndices[2]]);
        } else {
            if (std::abs(n2.z) > 0.9f) {
                expandedUVs.push_back(glm::vec2(v2.x + 0.5f, v2.y + 0.5f));
            } else if (std::abs(n2.x) > 0.9f) {
                expandedUVs.push_back(glm::vec2(v2.z + 0.5f, v2.y + 0.5f));
            } else {
                expandedUVs.push_back(glm::vec2(v2.x + 0.5f, v2.z + 0.5f));
            }
        }

        // New face references the expanded indices
        newFaces.push_back({
            { static_cast<Uint16>(baseIdx), static_cast<Uint16>(baseIdx + 1), static_cast<Uint16>(baseIdx + 2) },
            { 0, 0, 0 }  // no longer used but keep for compatibility
        });
    }

    m_vertices = std::move(expandedVertices);
    m_normals = std::move(expandedNormals);
    m_uvs = std::move(expandedUVs);  // Store expanded UVs
    m_faces = std::move(newFaces);
}

BufferedModel::BufferedModel(SDL_GPUDevice* gpu, SDL_Window* window, std::unique_ptr<Model> model,
    ShaderParameters vertex, ShaderParameters fragment, std::string_view textureFilename)
    : m_model { std::move(model) }
    , m_GPUDevice { gpu }
    , m_textureFilename { textureFilename }
    // Vertex data: position (16 bytes) + normal (16 bytes) + UV (16 bytes) = 48 bytes per vertex
    , m_vertexBufSize { static_cast<Uint32>(48 * (m_model->vertices().size())) }
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

    // Interleaved vertex format for Metal: position (16 bytes) + normal (16 bytes) + UV (16 bytes) per vertex
    // NOTE: Metal's float3 vertex attribute has 16-byte alignment, not 12!
    // UV (float2) also needs 16-byte alignment in Metal, so we pad it.
    // Total stride = 48 bytes per vertex.
    struct VertexPNU {
        float pos[3];     // offset 0, GPU reads 16 bytes (12 + 4 pad)
        float _pad1[1];    // offset 12, padding to align normal to 16
        float normal[3];   // offset 16, GPU reads 16 bytes (12 + 4 pad)
        float _pad2[1];   // offset 28, padding to align UV to 16
        float uv[2];       // offset 32, GPU reads 16 bytes (8 + 8 pad)
        float _pad3[2];   // offset 40, padding to make total 48 bytes
    };
    static_assert(sizeof(VertexPNU) == 48, "VertexPNU must be 48 bytes for Metal alignment");
    VertexPNU* vertexTransfer = (VertexPNU*)SDL_MapGPUTransferBuffer(m_GPUDevice, tempVertexIndexTransfer, false);
    for (size_t i = 0; i < m_model->vertices().size(); i++) {
        vertexTransfer[i].pos[0] = m_model->vertices()[i].x;
        vertexTransfer[i].pos[1] = m_model->vertices()[i].y;
        vertexTransfer[i].pos[2] = m_model->vertices()[i].z;
        vertexTransfer[i]._pad1[0] = 0.0f;
        vertexTransfer[i].normal[0] = m_model->normals()[i].x;
        vertexTransfer[i].normal[1] = m_model->normals()[i].y;
        vertexTransfer[i].normal[2] = m_model->normals()[i].z;
        vertexTransfer[i]._pad2[0] = 0.0f;
        // Use UV from model - Model::loadObj() is responsible for proper UV generation
        if (i < m_model->uvs().size()) {
            vertexTransfer[i].uv[0] = m_model->uvs()[i].x;
            vertexTransfer[i].uv[1] = m_model->uvs()[i].y;
        } else {
            // Fallback - shouldn't happen if Model::loadObj() is correct
            vertexTransfer[i].uv[0] = 0.0f;
            vertexTransfer[i].uv[1] = 0.0f;
        }
        vertexTransfer[i]._pad3[0] = 0.0f;
        vertexTransfer[i]._pad3[1] = 0.0f;
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

    // Load texture if a texture filename was provided
    if (!m_textureFilename.empty()) {
        SDL_GPUCommandBuffer* texCmdBuf = SDL_AcquireGPUCommandBuffer(m_GPUDevice);
        loadTexture(texCmdBuf, m_textureFilename);
    }
}

void BufferedModel::loadTexture(SDL_GPUCommandBuffer* cmdBuf, const std::string& filename)
{
    // Full path to texture file
    std::string fullPath = std::format("{}{}", SDL_GetBasePath(), filename);
    spdlog::info("Loading texture: {}", fullPath);

    // Use SDL3_image's GPU texture loading - it handles everything for us!
    // This creates a texture with format SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM
    int texWidth = 0;
    int texHeight = 0;

    // We need a copy pass for the upload
    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmdBuf);
    m_texture = IMG_LoadGPUTexture(m_GPUDevice, copyPass, fullPath.c_str(), &texWidth, &texHeight);
    SDL_EndGPUCopyPass(copyPass);

    if (!m_texture) {
        spdlog::error("Failed to load texture {}: {}", fullPath, SDL_GetError());
        return;
    }

    spdlog::info("Loaded texture {}x{}", texWidth, texHeight);

    // Create a sampler for the texture
    SDL_GPUSamplerCreateInfo samplerInfo = {
        .min_filter = SDL_GPU_FILTER_LINEAR,
        .mag_filter = SDL_GPU_FILTER_LINEAR,
        .mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
        .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
        .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
        .address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
        .mip_lod_bias = 0.0f,
        .max_anisotropy = 1.0f,
        .compare_op = SDL_GPU_COMPAREOP_INVALID,
        .min_lod = -1000.0f,
        .max_lod = 1000.0f,
        .enable_anisotropy = false,
        .enable_compare = false,
    };
    m_sampler = SDL_CreateGPUSampler(m_GPUDevice, &samplerInfo);
    if (!m_sampler) {
        spdlog::error("Failed to create sampler: {}", SDL_GetError());
    }

    // Submit the command buffer
    SDL_SubmitGPUCommandBuffer(cmdBuf);
}

void BufferedModel::release()
{
    if (!m_hasReleased) {
        SDL_ReleaseGPUGraphicsPipeline(m_GPUDevice, m_pipeline);
        SDL_ReleaseGPUBuffer(m_GPUDevice, m_vertexBuffer);
        SDL_ReleaseGPUBuffer(m_GPUDevice, m_indexBuffer);
        SDL_ReleaseGPUBuffer(m_GPUDevice, m_drawBuffer);
        SDL_ReleaseGPUTransferBuffer(m_GPUDevice, m_drawTransferBuf);
        if (m_texture) {
            SDL_ReleaseGPUTexture(m_GPUDevice, m_texture);
            m_texture = nullptr;
        }
        if (m_sampler) {
            SDL_ReleaseGPUSampler(m_GPUDevice, m_sampler);
            m_sampler = nullptr;
        }
    }
    m_hasReleased = true;
}

BufferedModel::~BufferedModel()
{
    release();
}

MapDisplacedBufferedModel::MapDisplacedBufferedModel(SDL_GPUDevice* gpu, SDL_Window* window, std::unique_ptr<Model> model,
    ShaderParameters vertex, ShaderParameters fragment, std::string_view textureFilename)
    : BufferedModel(gpu, window, std::move(model), vertex, fragment, textureFilename)
{
    SDL_GPUBufferCreateInfo mapBufferCreateInfo = {
        .usage = SDL_GPU_BUFFERUSAGE_VERTEX,  // Must be VERTEX for slot 1 (Metal restriction)
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
        mappedDisplacementColor[idx].padding = 0.0f;
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
// --- Monster Model Cache ----------------------------------------------------

const std::string& Renderer::getModelFileForType(const Monster& mon)
{
    static const std::string s_defaultModel = "monkey.obj";

    // Name → OBJ model file mapping (version-agnostic, always available).
    // Add entries here as new models become available.
    static const std::unordered_map<std::string, std::string> s_nameModelMap = {
        { "bat",       "icosphere.obj" },
        { "fire bat",  "icosphere.obj" },
        // Examples for future models:
        // { "rat",     "rat.obj"     },
        // { "goblin",  "goblin.obj"  },
        // { "kobold",  "kobold.obj"  },
        // { "orc",     "orc.obj"     },
    };

    spdlog::debug("getModelFileForType: name='{}' type={} btype={}",
                  mon.name(), mon.type(), mon.btype());

    auto it = s_nameModelMap.find(mon.name());
    if (it != s_nameModelMap.end())
        return it->second;
    return s_defaultModel;
}

MonsterBufferedModel* Renderer::getOrCreateMonsterModel(const std::string& modelFile)
{
    auto it = m_monsterModelCache.find(modelFile);
    if (it != m_monsterModelCache.end())
        return it->second.get();

    // Lazily create the model — standalone monster vertex shader with look-at rotation.
    // Uses position_monster.vert (no sentinel hack) and solid.frag (no texture).
    auto model = std::make_unique<MonsterBufferedModel>(
        m_GPUDevice, m_window, std::make_unique<Model>(modelFile),
        ShaderParameters { std::string_view("position_monster.vert"), 0, 2, 0, 0 },
        ShaderParameters { std::string_view("solid.frag"), 0, 1, 0, 0 });

    MonsterBufferedModel* ptr = model.get();
    m_monsterModelCache[modelFile] = std::move(model);
    spdlog::info("Created monster model from '{}'", modelFile);
    return ptr;
}

// --- MonsterBufferedModel ---

MonsterBufferedModel::MonsterBufferedModel(SDL_GPUDevice* gpu, SDL_Window* window, std::unique_ptr<Model> model,
    ShaderParameters vertex, ShaderParameters fragment, std::string_view textureFilename)
    : BufferedModel(gpu, window, std::move(model), vertex, fragment, textureFilename)
{
    SDL_GPUBufferCreateInfo instanceBufferCreateInfo = {
        .usage = SDL_GPU_BUFFERUSAGE_VERTEX,
        .size = (Uint32)(s_maxMonsterInstances * sizeof(DisplacementColorInfo))
    };
    m_monsterInstanceBuffer = SDL_CreateGPUBuffer(m_GPUDevice, &instanceBufferCreateInfo);

    SDL_GPUTransferBufferCreateInfo transferCreateInfo = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = (Uint32)(s_maxMonsterInstances * sizeof(DisplacementColorInfo))
    };
    m_monsterTransferBuf = SDL_CreateGPUTransferBuffer(m_GPUDevice, &transferCreateInfo);
}

void MonsterBufferedModel::pushMonsterData(
    const std::vector<std::pair<Pos2<int>, const Monster*>>& monsters,
    SDL_GPUCommandBuffer* cmdBuf)
{
    // Map per-instance data: shift + color for each monster in the filtered list
    DisplacementColorInfo* mapped = (DisplacementColorInfo*)SDL_MapGPUTransferBuffer(
        m_GPUDevice, m_monsterTransferBuf, true);

    Uint32 idx = 0;
    for (const auto& [pos, monPtr] : monsters) {
        if (!monPtr || idx >= s_maxMonsterInstances)
            break;

        const Monster& mon = *monPtr;
        glm::vec2 renderCoords2D = mapCoordToRender(pos);

        mapped[idx].shiftX = renderCoords2D.x;
        mapped[idx].shiftY = renderCoords2D.y;
        mapped[idx].tileType = 999.0f;  // sentinel: don't sink, it's a monster
        mapped[idx].padding = 0.0f;
        // Color by attitude: 0=hostile(red), 1=neutral(yellow), 3=good_neutral(green), 4=friendly(blue), 5=marionette(purple)
        switch (mon.att()) {
            case 0: mapped[idx].color = glm::vec4(1.0f, 0.2f, 0.2f, 1.0f); break; // hostile — red
            case 1: mapped[idx].color = glm::vec4(1.0f, 1.0f, 0.2f, 1.0f); break; // neutral — yellow
            case 2: mapped[idx].color = glm::vec4(0.8f, 0.8f, 0.2f, 1.0f); break; // strict_neutral — olive
            case 3: mapped[idx].color = glm::vec4(0.2f, 0.8f, 0.2f, 1.0f); break; // good_neutral — green
            case 4: mapped[idx].color = glm::vec4(0.3f, 0.5f, 1.0f, 1.0f); break; // friendly — blue
            case 5: mapped[idx].color = glm::vec4(0.7f, 0.3f, 0.9f, 1.0f); break; // marionette — purple
            default: mapped[idx].color = glm::vec4(0.5f, 0.5f, 0.5f, 1.0f); break; // unknown — gray
        }

        ++idx;
    }
    SDL_UnmapGPUTransferBuffer(m_GPUDevice, m_monsterTransferBuf);

    Uint32 monsterCount = idx;

    // Update indirect draw command with correct instance count
    SDL_GPUIndexedIndirectDrawCommand* drawTransfer = (SDL_GPUIndexedIndirectDrawCommand*)SDL_MapGPUTransferBuffer(
        m_GPUDevice, m_drawTransferBuf, true);
    drawTransfer[0] = (SDL_GPUIndexedIndirectDrawCommand) {
        .num_indices = static_cast<Uint32>(3 * m_model->faces().size()),
        .num_instances = monsterCount,
        .first_index = 0,
        .vertex_offset = 0,
        .first_instance = 0
    };
    SDL_UnmapGPUTransferBuffer(m_GPUDevice, m_drawTransferBuf);

    // Upload instance data + draw command
    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmdBuf);

    SDL_GPUTransferBufferLocation instanceLocation = {
        .transfer_buffer = m_monsterTransferBuf, .offset = 0
    };
    SDL_GPUBufferRegion instanceRegion = {
        .buffer = m_monsterInstanceBuffer, .offset = 0,
        .size = s_maxMonsterInstances * sizeof(DisplacementColorInfo)
    };
    SDL_UploadToGPUBuffer(copyPass, &instanceLocation, &instanceRegion, true);

    SDL_GPUTransferBufferLocation drawLocation = {
        .transfer_buffer = m_drawTransferBuf, .offset = 0
    };
    SDL_GPUBufferRegion drawRegion = {
        .buffer = m_drawBuffer, .offset = 0, .size = m_drawBufSize
    };
    SDL_UploadToGPUBuffer(copyPass, &drawLocation, &drawRegion, true);

    SDL_EndGPUCopyPass(copyPass);
}

void MonsterBufferedModel::draw(SDL_GPURenderPass* renderPass)
{
    SDL_BindGPUGraphicsPipeline(renderPass, m_pipeline);

    // Slot 0: per-vertex position + normal + UV
    SDL_GPUBufferBinding vertexBufferBinding = { .buffer = m_vertexBuffer, .offset = 0 };
    SDL_BindGPUVertexBuffers(renderPass, 0, &vertexBufferBinding, 1);

    // Slot 1: per-instance data (shift + color)
    SDL_GPUBufferBinding instanceBufferBinding = { .buffer = m_monsterInstanceBuffer, .offset = 0 };
    SDL_BindGPUVertexBuffers(renderPass, 1, &instanceBufferBinding, 1);

    // Index buffer
    SDL_GPUBufferBinding indexBufferBinding = { .buffer = m_indexBuffer, .offset = 0 };
    SDL_BindGPUIndexBuffer(renderPass, &indexBufferBinding, SDL_GPU_INDEXELEMENTSIZE_16BIT);

    // Texture sampler
    if (m_texture && m_sampler) {
        SDL_GPUTextureSamplerBinding texSamplerBinding = {
            .texture = m_texture,
            .sampler = m_sampler
        };
        SDL_BindGPUFragmentSamplers(renderPass, 0, &texSamplerBinding, 1);
    }

    SDL_DrawGPUIndexedPrimitivesIndirect(renderPass, m_drawBuffer, 0, 1);
}

void MonsterBufferedModel::release()
{
    if (m_hasReleased)
        return;

    // Release our instance buffers first
    if (m_monsterInstanceBuffer) {
        SDL_ReleaseGPUBuffer(m_GPUDevice, m_monsterInstanceBuffer);
        m_monsterInstanceBuffer = nullptr;
    }
    if (m_monsterTransferBuf) {
        SDL_ReleaseGPUTransferBuffer(m_GPUDevice, m_monsterTransferBuf);
        m_monsterTransferBuf = nullptr;
    }

    // Then release parent (pipeline, vertex, index, draw buffers)
    BufferedModel::release();

    m_hasReleased = true;
}

MonsterBufferedModel::~MonsterBufferedModel()
{
    release();
}


SDL_GPUGraphicsPipeline* BufferedModel::createGraphicsPipelineWithShaders(SDL_Window* window, ShaderParameters vertex, ShaderParameters fragment)
{
    SDL_GPUShader* vertexShader = loadShader(m_GPUDevice, vertex);
    SDL_GPUShader* fragShader = loadShader(m_GPUDevice, fragment);

    // Vertex format: position (16 bytes) + normal (16 bytes) + UV (16 bytes) = 48 bytes stride
    // NOTE: Metal's float3 has 16-byte alignment in vertex attribute arrays
    SDL_GPUVertexAttribute vertexAttributes[] = {
        { .location = 0,
            .buffer_slot = 0,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
            .offset = 0 },                       // position at offset 0
        { .location = 1,
            .buffer_slot = 0,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
            .offset = 16 },                      // normal at offset 16 (float3 = 16 bytes in Metal)
        { .location = 2,
            .buffer_slot = 1,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
            .offset = 0 },                       // tile shift at offset 0
        { .location = 3,
            .buffer_slot = 1,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,
            .offset = 16 },                      // tile color at offset 16
        { .location = 4,
            .buffer_slot = 0,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
            .offset = 32 },                      // UV at offset 32
    };
    SDL_GPUVertexBufferDescription vertexBufferDescriptions[] = {
        { .slot = 0,
            .pitch = 48,                         // stride: position(16) + normal(16) + UV(16) = 48 bytes
            .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
            .instance_step_rate = 0 },
        { .slot = 1,
            // stride matches DisplacementColorInfo: float(4)+float(4)+float(4)+float(4)+vec4(16) = 32 bytes
            .pitch = 32,
            .input_rate = SDL_GPU_VERTEXINPUTRATE_INSTANCE,
            .instance_step_rate = 0 }
    };
    // TODO add more vertex buffers here, one for map, one for each actor etc?
    SDL_GPUVertexInputState vertexInputState = {
        .vertex_buffer_descriptions = vertexBufferDescriptions,
        .num_vertex_buffers = 2,
        .vertex_attributes = vertexAttributes,
        .num_vertex_attributes = 5,             // position + normal + shift + color + UV
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
        .enable_depth_write = true,
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

    // Bind texture sampler if we have a texture
    if (m_texture && m_sampler) {
        SDL_GPUTextureSamplerBinding texSamplerBinding = {
            .texture = m_texture,
            .sampler = m_sampler
        };
        SDL_BindGPUFragmentSamplers(renderPass, 0, &texSamplerBinding, 1);
    }

    SDL_DrawGPUIndexedPrimitivesIndirect(renderPass, m_drawBuffer, 0, 1);
}

void MapDisplacedBufferedModel::draw(SDL_GPURenderPass* renderPass)
{
    SDL_BindGPUGraphicsPipeline(renderPass, m_pipeline);

    // Bind vertex buffers:
    // Slot 0: per-vertex position + normal + UV (48 bytes per vertex)
    SDL_GPUBufferBinding vertexBufferBinding = { .buffer = m_vertexBuffer, .offset = 0 };
    SDL_BindGPUVertexBuffers(renderPass, 0, &vertexBufferBinding, 1);

    // Slot 1: per-instance data (shift + color)
    SDL_GPUBufferBinding instanceBufferBinding = { .buffer = m_mapDataBuffer, .offset = 0 };
    SDL_BindGPUVertexBuffers(renderPass, 1, &instanceBufferBinding, 1);

    // Bind index buffer
    SDL_GPUBufferBinding indexBufferBinding = { .buffer = m_indexBuffer, .offset = 0 };
    SDL_BindGPUIndexBuffer(renderPass, &indexBufferBinding, SDL_GPU_INDEXELEMENTSIZE_16BIT);

    // Bind texture sampler if we have a texture
    if (m_texture && m_sampler) {
        SDL_GPUTextureSamplerBinding texSamplerBinding = {
            .texture = m_texture,
            .sampler = m_sampler
        };
        SDL_BindGPUFragmentSamplers(renderPass, 0, &texSamplerBinding, 1);
    }

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
