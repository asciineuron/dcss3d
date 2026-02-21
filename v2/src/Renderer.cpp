#include "Renderer.hpp"
#include "GameMap.hpp"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlgpu3.h"
#include <filesystem>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <stdexcept>

namespace fs = std::filesystem;

glm::vec4 mapTypeToColor(MapType type)
{
    using enum MapType;
    switch (type) {
    case Wall:
        return { 0.5f, 0.5f, 0.0f, 1.0f };
        break;
    case Floor:
        return { 0.0f, 0.5f, 0.0f, 1.0f };
        break;
    case Unexplored:
        return { 0.5f, 0.5f, 0.5f, 1.0f };
        break;
    case Other:
        return { 0.0f, 0.5f, 0.5f, 1.0f };
        break;
    default:
        throw std::logic_error("invalid MapType specified");
        break;
    }
}

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
        extension = ".msl";
        entrypoint = "main0";
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

    if (!SDL_GetWindowSize(m_window, &m_windowWidth, &m_windowWidth))
        throw std::runtime_error(std::format("SDL_GetWindowSize error: {}", SDL_GetError()));

    m_GPUDevice = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL, true, nullptr);
    if (!m_GPUDevice)
        throw std::runtime_error(std::format("SDL_CreateGPUDevice error: {}", SDL_GetError()));

    if (!SDL_ClaimWindowForGPUDevice(m_GPUDevice, m_window))
        throw std::runtime_error(std::format("SDL_ClaimWindowForGPUDevice error: {}", SDL_GetError()));

    if (!SDL_SetGPUSwapchainParameters(m_GPUDevice, m_window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, SDL_GPU_PRESENTMODE_VSYNC))
        throw std::runtime_error(std::format("SDL_SetGPUSwapchainParameters error: {}", SDL_GetError()));

    m_mapCubeModel = std::make_unique<MapDisplacedBufferedModel>(m_GPUDevice, m_window, std::make_unique<Model>("cube1.obj"));

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
    ImGui_ImplSDLGPU3_Init(&initInfo);
}

void Renderer::doRender(GameMap& map, const Camera& camera)
{
    // do all rendering-related updates that don't require a command buffer pass first
    ImDrawData* drawData = ImGui::GetDrawData();
    const bool isMinimized = (drawData->DisplaySize.x <= 0.0f || drawData->DisplaySize.y <= 0.0f);

    // TODO instead have MapDisplacedBufferedModel do all the binding itself? and just call its render func which does this?
    glm::mat4 cameraView = camera.toViewProjection();

    SDL_GPUCommandBuffer* commandBuffer = SDL_AcquireGPUCommandBuffer(m_GPUDevice);

    SDL_GPUTexture* swapchainTexture = NULL;
    SDL_WaitAndAcquireGPUSwapchainTexture(commandBuffer, m_window, &swapchainTexture, NULL, NULL);

    if (swapchainTexture && !isMinimized) {
        // upload all data via copy passes
        if (!map.didRender()) {
            pushMapToGPU(map, commandBuffer);
            map.setDidRender(true);
        }

        ImGui_ImplSDLGPU3_PrepareDrawData(drawData, commandBuffer);

        // do actual rendering
        SDL_GPUColorTargetInfo targetInfo = { 0 };
        targetInfo.texture = swapchainTexture;
        targetInfo.clear_color = (SDL_FColor) { 0.0f, 0.0f, 0.0f, 1.0f };
        targetInfo.load_op = SDL_GPU_LOADOP_CLEAR;
        targetInfo.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass* renderPass = SDL_BeginGPURenderPass(commandBuffer, &targetInfo, 1, NULL);

        SDL_PushGPUVertexUniformData(commandBuffer, 0, glm::value_ptr(cameraView), sizeof(cameraView));

        m_mapCubeModel->draw(renderPass);

        ImGui_ImplSDLGPU3_RenderDrawData(drawData, commandBuffer, renderPass);

        SDL_EndGPURenderPass(renderPass);
    }
    SDL_SubmitGPUCommandBuffer(commandBuffer);
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

    ImGui_ImplSDL3_Shutdown();
    ImGui_ImplSDLGPU3_Shutdown();
    ImGui::DestroyContext();

    SDL_ReleaseWindowFromGPUDevice(m_GPUDevice, m_window);
    SDL_DestroyGPUDevice(m_GPUDevice);
    SDL_DestroyWindow(m_window);

    SDL_Quit();
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
}

BufferedModel::BufferedModel(SDL_GPUDevice* gpu, SDL_Window* window, std::unique_ptr<Model> model,
    ShaderParameters vertex, ShaderParameters fragment)
    : m_model { std::move(model) }
    , m_GPUDevice { gpu }
    , m_vertexBufSize { static_cast<Uint32>(sizeof(glm::vec3) * (m_model->vertices().size())) }
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

    glm::vec3* vertexTransfer = (glm::vec3*)SDL_MapGPUTransferBuffer(m_GPUDevice, tempVertexIndexTransfer, false);
    memcpy(vertexTransfer, m_model->vertices().data(), m_model->vertices().size() * sizeof(glm::vec3));

    Uint16* indexTransfer = (Uint16*)&vertexTransfer[m_model->vertices().size()];
    for (int i = 0; i < m_model->faces().size(); i++) {
        indexTransfer[3 * i] = m_model->faces()[i].vertexIndices[0];
        indexTransfer[3 * i + 1] = m_model->faces()[i].vertexIndices[1];
        indexTransfer[3 * i + 2] = m_model->faces()[i].vertexIndices[2];
    }
    SDL_UnmapGPUTransferBuffer(m_GPUDevice, tempVertexIndexTransfer);

    SDL_GPUIndexedIndirectDrawCommand* drawTransfer = (SDL_GPUIndexedIndirectDrawCommand*)SDL_MapGPUTransferBuffer(
        m_GPUDevice, m_drawTransferBuf, true);
    // TODO: for wall, have more num_instances
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
    // displacement, color info:
    DisplacementColorInfo* mappedDisplacementColor = (DisplacementColorInfo*)SDL_MapGPUTransferBuffer(
        m_GPUDevice, m_dataTransferBuf, true);
    for (auto idx = 0; const auto& [mapCoord, tile] : map.map()) {
        glm::vec2 renderCoords2D = mapCoordToRender(mapCoord);
        mappedDisplacementColor[idx].pos = { renderCoords2D.x, renderCoords2D.y, 0.f };
        mappedDisplacementColor[idx].type = std::to_underlying(tile.type());
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

    SDL_GPUVertexAttribute vertexAttributes[] = {
        { .location = 0,
            .buffer_slot = 0,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
            .offset = 0 }
    };
    SDL_GPUVertexBufferDescription vertexBufferDescriptions[] = {
        { .slot = 0,
            .pitch = sizeof(glm::vec3),
            .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
            .instance_step_rate = 0 }
    };
    // TODO add more vertex buffers here, one for map, one for each actor etc?
    SDL_GPUVertexInputState vertexInputState = {
        .vertex_buffer_descriptions = vertexBufferDescriptions,
        .num_vertex_buffers = 1,
        .vertex_attributes = vertexAttributes,
        .num_vertex_attributes = 1,
    };
    SDL_GPUColorTargetDescription colorTargetDescriptions[] = {
        { .format = SDL_GetGPUSwapchainTextureFormat(
              m_GPUDevice, window) }
    };
    SDL_GPURasterizerState rasterizer_state = {
        // .fill_mode = SDL_GPU_FILLMODE_FILL,
        .fill_mode = SDL_GPU_FILLMODE_LINE,
        .cull_mode = SDL_GPU_CULLMODE_FRONT
    };
    SDL_GPUGraphicsPipelineCreateInfo pipelineInfo = {
        .vertex_shader = vertexShader,
        .fragment_shader = fragShader,
        .vertex_input_state = vertexInputState,
        .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .rasterizer_state = rasterizer_state,
        .target_info = {
            .color_target_descriptions = colorTargetDescriptions,
            .num_color_targets = 1,
        }
    };

    SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(m_GPUDevice, &pipelineInfo);

    SDL_ReleaseGPUShader(m_GPUDevice, vertexShader);
    SDL_ReleaseGPUShader(m_GPUDevice, fragShader);

    return pipeline;
}

void BufferedModel::setupDraw(SDL_GPURenderPass* renderPass)
{
    // bind resources:
    SDL_BindGPUGraphicsPipeline(renderPass, m_pipeline);
    // vertices:
    SDL_GPUBufferBinding vertexBufferBinding = { .buffer = m_vertexBuffer, .offset = 0 };
    SDL_BindGPUVertexBuffers(
        renderPass, 0,
        &vertexBufferBinding,
        1);
    // vertex index:
    SDL_GPUBufferBinding indexBufferBinding = { .buffer = m_indexBuffer, .offset = 0 };
    SDL_BindGPUIndexBuffer(
        renderPass,
        &indexBufferBinding,
        SDL_GPU_INDEXELEMENTSIZE_16BIT);
}

void BufferedModel::draw(SDL_GPURenderPass* renderPass)
{
    // TODO not tested without map displacement...
    setupDraw(renderPass);

    SDL_DrawGPUIndexedPrimitivesIndirect(renderPass, m_drawBuffer, 0, 1);
}

void MapDisplacedBufferedModel::draw(SDL_GPURenderPass* renderPass)
{
    setupDraw(renderPass);

    // map pos data:
    SDL_BindGPUVertexStorageBuffers(renderPass, 0,
        &(m_mapDataBuffer), 1);

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
