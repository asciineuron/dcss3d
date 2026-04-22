#pragma once
#include "GameMap.hpp"
#include <SDL3/SDL.h>
#include <atomic>
#include <glm/glm.hpp>
#include <iostream>
#include <memory>
#include <vector>

struct Camera {
    glm::vec3 pos { 0.5f, 0.f, -0.5f };
    float theta { 0.f };
    float phi { 0.f };
    float fov { 0.785398f };
    float aspectRatio { 1.777777f };

    glm::mat4 toViewProjection() const;
};

struct Face {
    std::array<Uint16, 3> vertexIndices;
    std::array<Uint16, 3> textureIndices;
};

struct LightUniforms {
    glm::vec4 lightPos;    // xyz = camera world position
    glm::vec4 cameraPos;   // xyz = camera world position
};

class Model {
public:
    Model(std::string_view filename);

    const std::vector<glm::vec3>& vertices() const { return m_vertices; }
    const std::vector<glm::vec2>& uvs() const { return m_uvs; }
    const std::vector<glm::vec3>& normals() const { return m_normals; }
    const std::vector<Face>& faces() const { return m_faces; }

    friend std::ostream& operator<<(std::ostream&, const Model&);

private:
    std::vector<glm::vec3> m_vertices;
    std::vector<glm::vec2> m_uvs;
    std::vector<glm::vec3> m_normals;
    std::vector<Face> m_faces;
    std::string m_name;
    const std::string m_resourcePath;

    void loadObj(std::string_view filename);
};

struct ShaderParameters {
    std::string_view filename;
    Uint32 samplerCount;
    Uint32 uniformBufferCount;
    Uint32 storageBufferCount;
    Uint32 storageTextureCount;
};

// this base class can draw a single model at its designated position
class BufferedModel {
public:
    BufferedModel(SDL_GPUDevice*, SDL_Window*, std::unique_ptr<Model>,
        ShaderParameters vertex = { "position.vert", 0, 1, 0, 0 },
        ShaderParameters fragment = { "color.frag", 0, 0, 0, 0 });
    ~BufferedModel();

    virtual void release();

    virtual void draw(SDL_GPURenderPass*);

protected:
    SDL_GPUDevice* m_GPUDevice;
    std::unique_ptr<Model> m_model; // TODO no need for ptr, just Model;
    SDL_GPUBuffer* m_drawBuffer {}; // receives from m_drawTransferBuf, # indices + instances
    SDL_GPUTransferBuffer* m_drawTransferBuf {}; // SDL_GPUIndexedIndirectDrawCommand
    Uint32 m_drawBufSize;

    SDL_GPUGraphicsPipeline* m_pipeline {};
    SDL_GPUBuffer* m_vertexBuffer {}; // vertex and index only need an initial upload
    SDL_GPUBuffer* m_indexBuffer {}; // ^ so don't need to store their temp transfer buffer here
    Uint32 m_vertexBufSize;
    Uint32 m_indexBufSize;
    bool m_hasReleased;

    SDL_GPUGraphicsPipeline* createGraphicsPipelineWithShaders(SDL_Window* window, ShaderParameters vertex, ShaderParameters fragment);
    void uploadModel();
};

// has special buffers to handle drawing the map model at each chosen index
class MapDisplacedBufferedModel : public BufferedModel {
public:
    MapDisplacedBufferedModel(SDL_GPUDevice*, SDL_Window*, std::unique_ptr<Model>,
        ShaderParameters vertex = { std::string_view("position_color_shifted.vert"), 0, 1, 1, 0 },
        ShaderParameters fragment = { std::string_view("lit.frag"), 0, 1, 0, 0 });
    ~MapDisplacedBufferedModel();

    void release() override;

    void draw(SDL_GPURenderPass*) override;
    void pushMapData(const GameMap&, SDL_GPUCommandBuffer*); // update with map data

private:
    SDL_GPUBuffer* m_mapDataBuffer {};
    SDL_GPUTransferBuffer* m_dataTransferBuf {};
    bool m_hasReleased;

    struct DisplacementColorInfo {
        float shiftX;     // offset 0
        float shiftY;     // offset 4
        float tileType;   // offset 8
        float padding;    // offset 12 (required padding so color aligns to 16 bytes)
        glm::vec4 color;  // offset 16
        // sizeof = 32 bytes (matches slot 1 pitch perfectly)
    };

    static constexpr unsigned s_maxRenderCopies = 289; // 15^2 standard, 17^2 max, for Barachi
};

std::ostream& operator<<(std::ostream& os, const Model& model);

class Renderer {
public:
    Renderer();
    ~Renderer();

    Renderer(Renderer&) = delete;
    Renderer(Renderer&&) = delete;

    void doRender(GameMap&, const Camera&);

    const uint64_t renderCount() const;

    SDL_Window* window() { return m_window; }

    SDL_GPUDevice* gpu_device() { return m_GPUDevice; }

    void setRenderUI(bool renderUI) { m_renderUI = renderUI; };
    const bool renderUI() const { return m_renderUI; }

    // e.g. render_info stuff
private:
    SDL_Window* m_window;
    SDL_GPUDevice* m_GPUDevice;

    std::unique_ptr<MapDisplacedBufferedModel> m_mapCubeModel;

    SDL_WindowID m_windowID {};
    int m_windowWidth {};
    int m_windowHeight {};

    std::atomic_uint64_t m_renderCount;
    bool m_renderUI; // toggle to disable imgui overlay

    // Depth buffer for proper filled polygon rendering
    SDL_GPUTexture* m_depthTexture {};

    void pushMapToGPU(const GameMap&, SDL_GPUCommandBuffer*);
    void createDepthTexture();
    void releaseDepthTexture();

    static constexpr unsigned s_winW = 1920;
    static constexpr unsigned s_winH = 1080;
};
