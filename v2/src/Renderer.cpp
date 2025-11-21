#include "Renderer.hpp"
#include "GameMap.hpp"
#include <any>
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

glm::vec4 Camera::toViewProjection()
{
    float cosPhi = std::cos(phi);
    glm::vec3 lookDir = glm::normalize(glm::vec3(
        cosPhi * std::cos(-theta),
        std::sin(phi),
        cosPhi * std::sin(-theta)));

    glm::mat4 lookAt = glm::lookAt(pos, pos + lookDir, glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 perspective = glm::perspective(fov, aspect_ratio, 0.1f, 100.0f);
    return perspective * lookAt;
}

SDL_GPUShader* loadShader(SDL_GPUDevice* device, const ShaderParameters& parameters)
{
    SDL_GPUShaderStage stage;
    if (parameters.filename.contains(".vert")) {
        stage = SDL_GPU_SHADERSTAGE_VERTEX;
    } else if (parameters.filename.contains(".vert")) {
        stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    } else {
        throw std::runtime_error("invalid shader extension");
    }

    SDL_GPUShaderFormat backendFormats = SDL_GetGPUShaderFormats(device);
    SDL_GPUShaderFormat format = SDL_GPU_SHADERFORMAT_INVALID;
    const char* entrypoint;
    std::string_view extension;
    if (backend_formats & SDL_GPU_SHADERFORMAT_SPIRV) {
        format = SDL_GPU_SHADERFORMAT_SPIRV;
        extension = ".spv";
        entrypoint = "main";
    } else if (backend_formats & SDL_GPU_SHADERFORMAT_MSL) {
        format = SDL_GPU_SHADERFORMAT_MSL;
        extension = ".msl";
        entrypoint = "main0";
    } else if (backend_formats & SDL_GPU_SHADERFORMAT_DXIL) {
        format = SDL_GPU_SHADERFORMAT_DXIL;
        extension = ".dxil";
        entrypoint = "main";
    } else {
        throw std::runtime_error("unrecognized backend shader format");
    }
    std::string shaderPath = std::format("{}{}", parameters.filename, extension);

    auto codeLen = fs::file_size(shaderPath);
    std::ifstream shaderFile(shaderPath);
    std::string code;
    code.resize_and_overwrite(codeLen, [](char* buf, std::size_t len) { shaderFile.read(buf, len); });

    SDL_GPUShaderCreateInfo shaderInfo = {
        .code = (Uint8*)code.c_str(),
        .code_size = codeLen,
        .entrypoint = entrypoint,
        .format = format,
        .stage = stage,
        .num_samplers = parameters.samplerCount,
        .num_uniform_buffers = parameters.uniformBufferCount,
        .num_storage_buffers = parameters.storageBufferCount,
        .num_storage_textures = parameters.storageTextureCount
    };
    SDL_GPUShader* shader = SDL_CreateGPUShader(device, &shaderInfo);
    if (!shader) {
        throw std::runtime_error("failed to create shader");
    }
    return shader;
}

Renderer::Renderer()
{
    // TODO add '/' ?
    m_resourcePath = std::format("{}{}", SDL_GetBasePath(), "resources");
    m_shaderPath = std::format("{}{}", SDL_GetBasePath(), "shaders");

    float displayScale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    SDL_WindowFlags windowFlags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;

    m_window = std::shared_ptr<SDL_Window>(
        SDL_CreateWindow("dcss3d", std::floor(s_winW * displayScale),
            std::floor(s_winH * displayScale), windowFlags),
        [](auto p) { SDL_DestroyWindow(p); });

    if (!m_window)
        throw std::runtime_error(std::format("SDL_CreateWindow failed: {}", SDL_GetError()));
    m_windowID = SDL_GetWindowID(m_window.get());

    if (!SDL_GetWindowSize(m_window, &m_windowWidth, &m_windowWidth))
        throw std::runtime_error(std::format("SDL_GetWindowSize error: {}", SDL_GetError()));

    m_GPUDevice = std::shared_ptr<SDL_GPUDevice>(
        SDL_CreateGPUDevice(
            SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL,
            true, nullptr),
        [win = m_window](auto p) { SDL_ReleaseWindowFromGPUDevice(p, win.get()); SDL_DestroyGPUDevice(p); });

    if (!m_GPUDevice)
        throw std::runtime_error(std::format("SDL_CreateGPUDevice error: {}", SDL_GetError()));

    if (!SDL_ClaimWindowForGPUDevice(m_GPUDevice.get(), m_window.get()))
        throw std::runtime_error(std::format("SDL_ClaimWindowForGPUDevice error: {}", SDL_GetError()));

    if (!SDL_SetGPUSwapchainParameters(m_GPUDevice.get(), m_window.get(),
            SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
            SDL_GPU_PRESENTMODE_VSYNC))
        throw std::runtime_error(std::format("SDL_SetGPUSwapchainParameters error: {}", SDL_GetError()));

    m_mapCubeModel = std::make_unique<MapDisplacedBufferedModel>(m_GPUDevice, std::make_unique<Model>("cube1.obj"));
}

void Renderer::doRender(const GameMap& map, const Camera& camera)
{
    // TODO instead have MapDisplacedBufferedModel do all the binding itself? and just call its render func which does this?

    glm::vec4 cameraView = camera->toViewProjection();

    SDL_GPUCommandBuffer* commandBuffer = SDL_AcquireGPUCommandBuffer(m_GPUDevice.get());

    if (!map.didRender()) {
        pushMapToGPU(map);
        map.setDidRender(true);
    }

    SDL_GPUTexture* swapchainTexture = NULL;
    SDL_WaitAndAcquireGPUSwapchainTexture(commandBuffer,
        m_window.get(),
        &swapchainTexture, NULL, NULL);

    if (swapchainTexture) {
        SDL_GPUColorTargetInfo colorTargetInfo = { 0 };
        colorTargetInfo.texture = swapchain_texture;
        colorTargetInfo.clear_color = (SDL_FColor) { 0.0f, 0.0f, 0.0f, 1.0f };
        colorTargetInfo.load_op = SDL_GPU_LOADOP_CLEAR;
        colorTargetInfo.store_op = SDL_GPU_STOREOP_STORE;

        SDL_GPURenderPass* renderPass = SDL_BeginGPURenderPass(
            commandBuffer, &colorTargetInfo, 1, NULL);

        SDL_PushGPUVertexUniformData(commandBuffer, 0, glm::value_ptr(cameraView), sizeof(cameraView));

        m_mapCubeModel->draw(renderPass);

        // TODO: can add e.g. imgui UI draws here

        SDL_EndGPURenderPass(renderPass);
    }
    SDL_SubmitGPUCommandBuffer(commandBuffer);
}

// delegate to the MapDisplacedBufferedModel?
void Renderer::pushMapToGPU(const GameMap& map)
{
    m_mapCubeModel.pushMapData(map);
}

Renderer::~Renderer()
{
    // shared ptrs automatically deleted with proper funcs
}

Model::Model(std::string_view filename)
{
    loadObj(filename);
}

void Model::loadObj(std::string_view filename)
{
    m_name = filename;
    std::string fullFilename = std::format("{}/{}", m_resourcePath, filename);
    std::ifstream inFile(fullFilename);
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

BufferedModel::BufferedModel(std::shared_ptr<SDL_GPUDevice> gpu, std::unique_ptr<Model> model,
    ShaderParameters vertex, ShaderParameters fragment)
    : m_model { model }
    , m_GPUDevice { gpu }
    , m_vertexBufSize { m_model->vertices().size() }
    , m_indexBufSize { sizeof(Uint16) * 3 * m_model->faces().size() }
    , m_drawBufSize { sizeof(SDL_GPUIndexedIndirectDrawCommand) * 1 }
{
    m_pipeline = createGraphicsPipelineWithShaders(vertex, fragment);

    // set up buffers next:
    m_vertexBuffer = SDL_CreateGPUBuffer(m_GPUDevice.get(),
        &(SDL_GPUBufferCreateInfo) { .usage = SDL_GPU_BUFFERUSAGE_VERTEX,
            .size = m_vertexBufSize });
    m_indexBuffer = SDL_CreateGPUBuffer(m_GPUDevice.get(),
        &(SDL_GPUBufferCreateInfo) { .usage = SDL_GPU_BUFFERUSAGE_INDEX,
            .size = m_indexBufSize });
    m_drawBuffer = SDL_CreateGPUBuffer(m_GPUDevice.get(),
        &(SDL_GPUBufferCreateInfo) { .usage = SDL_GPU_BUFFERUSAGE_INDIRECT,
            .size = m_drawBufSize });
    m_drawTransferBuf = SDL_CreateGPUTransferBuffer(m_GPUDevice.get(),
        &(SDL_GPUTransferBufferCreateInfo) { .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
            .size = (Uint32)sizeof(
                SDL_GPUIndexedIndirectDrawCommand) });

    // then map model data to gpu:
    uploadModel();
}

void BufferedModel::uploadModel()
{
    SDL_GPUTransferBuffer* tempVertexIndexTransfer = SDL_CreateGPUTransferBuffer(
        m_GPUDevice.get(), &(SDL_GPUTransferBufferCreateInfo) { .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = m_vertexBufSize + m_indexBufSize });
    
    glm::vec3* vertexTransfer = (glm::vec3*)SDL_MapGPUTransferBuffer(m_GPUDevice.get(),
        tempVertexIndexTransfer, false);
    memcpy(vertexTransfer, m_model->vertices().data(),
        m_model->vertices().size() * sizeof(glm::vec3));
    Uint16* indexTransfer = (Uint16*)&vertexTransfer[m_model->vertices().size()];
    for (int i = 0; i < model->faces().size(); i++) {
        indexTransfer[3 * i] = model->faces[i].vertexIndices[0];
        indexTransfer[3 * i + 1] = model->faces[i].vertexIndices[1];
        indexTransfer[3 * i + 2] = model->faces[i].vertexIndices[2];
    }
    SDL_UnmapGPUTransferBuffer(m_GPUDevice.get(), tempVertexIndexTransfer);

    SDL_GPUIndexedIndirectDrawCommand* drawTransfer = (SDL_GPUIndexedIndirectDrawCommand*)SDL_MapGPUTransferBuffer(
        m_GPUDevice.get(), m_drawTransferBuf, true);
    // TODO: for wall, have more num_instances
    drawTransfer[0] = (SDL_GPUIndexedIndirectDrawCommand) {
        .num_indices = (Uint32)(3 * model->faces().size()),
        .num_instances = 1,
        .first_index = 0,
        .vertex_offset = 0,
        .first_instance = 0
    };
    SDL_UnmapGPUTransferBuffer(m_GPUDevice.get(), m_drawTransferBuf);

    // then actually upload the mapping to gpu:
    SDL_GPUCommandBuffer* cmdBuf = SDL_AcquireGPUCommandBuffer(m_GPUDevice.get());
    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmdBuf);

    SDL_UploadToGPUBuffer(
        copyPass,
        &(SDL_GPUTransferBufferLocation) { .transfer_buffer = tempVertexIndexTransfer,
            .offset = 0 },
        &(SDL_GPUBufferRegion) { .buffer = m_vertexBuffer,
            .offset = 0,
            .size = m_vertexBufSize },
        false);
    SDL_UploadToGPUBuffer(
        copyPass,
        &(SDL_GPUTransferBufferLocation) { .transfer_buffer = tempVertexIndexTransfer,
            .offset = vertex_buf_size },
        &(SDL_GPUBufferRegion) { .buffer = m_indexBuffer,
            .offset = 0,
            .size = m_indexBufSize },
        false);

    SDL_UploadToGPUBuffer(copyPass,
        &(SDL_GPUTransferBufferLocation) {
            .transfer_buffer = m_drawTransferBuf,
            .offset = 0 },
        &(SDL_GPUBufferRegion) { .buffer = m_drawBuffer,
            .offset = 0,
            .size = m_drawBufSize },
        false);

    SDL_EndGPUCopyPass(copyPass);
    SDL_SubmitGPUCommandBuffer(cmdBuf);
    SDL_ReleaseGPUTransferBuffer(m_GPUDevice.get(), tempVertexIndexTransfer); // not used again
}

BufferedModel::BufferedModel()
{
    SDL_ReleaseGPUGraphicsPipeline(m_GPUDevice.get(), m_pipeline);
    SDL_ReleaseGPUBuffer(m_GPUDevice.get(), m_vertexBuffer);
    SDL_ReleaseGPUBuffer(m_GPUDevice.get(), m_indexBuffer);
    SDL_ReleaseGPUBuffer(m_GPUDevice.get(), m_drawBuffer);
    SDL_ReleaseGPUTransferBuffer(m_GPUDevice.get(), m_drawTransferBuf);
}

MapDisplacedBufferedModel::MapDisplacedBufferedModel(std::shared_ptr<SDL_GPUDevice> gpu, std::unique_ptr<Model> model,
    ShaderParameters vertex, ShaderParameters fragment)
    : BufferedModel(gpu, model, vertex, fragment)
{
    m_mapDataBuffer = SDL_CreateGPUBuffer(m_GPUDevice.get(),
        &(SDL_GPUBufferCreateInfo) { .usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
            .size = (Uint32)(s_maxRenderCopies * sizeof(DisplacementColorInfo)) });

    m_dataTransferBuf = SDL_CreateGPUTransferBuffer(m_GPUDevice.get(),
        &(SDL_GPUTransferBufferCreateInfo) { .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
            .size = (Uint32)(s_maxRenderCopies * sizeof(DisplacementColorInfo)) });

}

void MapDisplacedBufferedModel::pushMapData(const GameMap& map, SDL_GPUCommandBuffer* cmdBuf)
{
    // displacement, color info:
    DisplacementColorInfo* mappedDisplacementColor = SDL_MapGPUTransferBuffer(
        m_GPUDevice.get(), m_dataTransferBuf, true);
    for (auto idx = 0; const auto& [mapCoord, tile] : map.map()) {
        glm::vec2 renderCoords2D = mapCoordToRender(mapCoord);
        mappedDisplacementColor[idx].pos = { renderCoords2D.x, renderCoords2D.y, 0.f };
        mappedDisplacementColor[idx].type = std::to_underlying(tile.type());
        mappedDisplacementColor[idx].color = mapTypeToColor(tile.type());
        // TODO: hard limit on # map entries
        if (++idx >= s_maxRenderCopies)
            break;
    }
    SDL_UnmapGPUTransferBuffer(m_GPUDevice.get(), m_dataTransferBuf);

    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmdBuf);
    SDL_UploadToGPUBuffer(copyPass,
        &(SDL_GPUTransferBufferLocation) {
            .transfer_buffer = m_dataTransferBuf,
            .offset = 0 },
        &(SDL_GPUBufferRegion) {
            .buffer = m_mapDataBuffer,
            .offset = 0,
            .size = s_maxRenderCopies * sizeof(DisplacementColorInfo) },
        true);
    SDL_EndGPUCopyPass(copyPass);

    // draw commands to set the # instances of map tiles:
    SDL_GPUIndexedIndirectDrawCommand* drawTransfer = (SDL_GPUIndexedIndirectDrawCommand*)SDL_MapGPUTransferBuffer(
        m_GPUDevice.get(), m_drawTransferBuf, true);

    drawTransfer[0] = (SDL_GPUIndexedIndirectDrawCommand) {
        .num_indices = (Uint32)(3 * m_model->faces.size()),
        // NOTE setting num_instances:
        .num_instances = map.map().size(),
        .first_index = 0,
        .vertex_offset = 0,
        .first_instance = 0
    };
    SDL_UnmapGPUTransferBuffer(m_GPUDevice.get(), m_drawTransferBuf);

    copyPass = SDL_BeginGPUCopyPass(cmdBuf);
    SDL_UploadToGPUBuffer(
        copyPass,
        &(SDL_GPUTransferBufferLocation) {
            .transfer_buffer = m_drawTransferBuf,
            .offset = 0 },
        &(SDL_GPUBufferRegion) {
            .buffer = m_drawBuffer,
            .offset = 0,
            .size = m_drawBufSize },
        true);
    SDL_EndGPUCopyPass(copyPass);
}

MapDisplacedBufferedModel::~MapDisplacedBufferedModel()
{
    SDL_ReleaseGPUBuffer(m_GPUDevice.get(), m_mapDataBuffer);
    SDL_ReleaseGPUTransferBuffer(m_GPUDevice.get(), m_dataTransferBuf);
}

SDL_GPUGraphicsPipeline* BufferedModel::createGraphicsPipelineWithShaders(ShaderParameters vertex, ShaderParameters fragment)
{
    SDL_GPUShader* vertexShader = loadShader(m_GPUDevice.get(), vertex);
    SDL_GPUShader* fragShader = loadShader(m_GPUDevice.get(), fragment);

    SDL_GPUVertexAttribute vertexAttributes[] = {
        { .buffer_slot = 0,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
            .location = 0,
            .offset = 0 }
    };
    SDL_GPUVertexBufferDescription vertexBufferDescriptions[] = {
        { .slot = 0,
            .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
            .instance_step_rate = 0,
            .pitch = sizeof(vec3) }
    };
    // TODO add more vertex buffers here, one for map, one for each actor etc?
    SDL_GPUVertexInputState vertexInputState = {
        .num_vertex_buffers = 1,
        .vertex_buffer_descriptions = vertexBufferDescriptions,
        .num_vertex_attributes = 1,
        .vertex_attributes = vertexAttributes,
    };
    SDL_GPUColorTargetDescription colorTargetDescriptions[] = {
        { .format = SDL_GetGPUSwapchainTextureFormat(
              rend_ctx.gpu_dev, rend_ctx.rend_info->window) }
    };
    SDL_GPURasterizerState rasterizer_state = {
        // .fill_mode = SDL_GPU_FILLMODE_FILL,
        .fill_mode = SDL_GPU_FILLMODE_LINE,
        .cull_mode = SDL_GPU_CULLMODE_FRONT
    };
    SDL_GPUGraphicsPipelineCreateInfo pipelineInfo = {
        .vertexShader = vertexShader,
        .fragment_shader = fragShader,
        .vertex_input_state = vertexInputState,
        .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .rasterizer_state = rasterizer_state,
        .target_info = {
            .color_target_descriptions = colorTargetDescriptions,
            .num_color_targets = 1,
        }
    };

    SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(device, &pipelineInfo);

    SDL_ReleaseGPUShader(m_GPUDevice.get(), vertexShader);
    SDL_ReleaseGPUShader(m_GPUDevice.get(), fragShader);

    return pipeline;
}

void BufferedModel::setupDraw(SDL_GPURenderPass*)
{
    // bind resources:
    SDL_BindGPUGraphicsPipeline(renderPass, m_pipeline);
    // vertices:
    SDL_BindGPUVertexBuffers(
        renderPass, 0,
        &(SDL_GPUBufferBinding) { .buffer = m_vertexBuffer, .offset = 0 },
        1);
    // vertex index:
    SDL_BindGPUIndexBuffer(
        renderPass,
        &(SDL_GPUBufferBinding) { .buffer = m_indexBuffer, .offset = 0 },
        SDL_GPU_INDEXELEMENTSIZE_16BIT);
}

void BufferedModel::draw(SDL_GPURenderPass* renderPass)
{
    // TODO not tested without map displacement...
    setupDraw(renderPass);

    SDL_DrawGPUIndexedPrimitivesIndirect(renderPass, m_mapCubeModel.drawBuffer().get(), 0, 1);
}

void MapDisplacedBufferedModel::draw(SDL_GPURenderPass* renderPass)
{
    setupDraw(renderPass);

    // map pos data:
    SDL_BindGPUVertexStorageBuffers(renderPass, 0,
        &(m_mapDataBuffer), 1);

    SDL_DrawGPUIndexedPrimitivesIndirect(renderPass, m_mapCubeModel.drawBuffer().get(), 0, 1);
}
