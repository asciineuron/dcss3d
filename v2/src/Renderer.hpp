#pragma once
#include <glm/glm.hpp>
#include <SDL3/SDL.h>
#include <iostream>

struct Camera {
    glm::vec3 pos { 0.5f, 0.f, -0.5f };
    float theta { 0.f };
    float phi { 0.f };
    float fov { 0.785398f };
    float aspectRatio { 1.777777f };

    glm::vec4 toViewProjection();
};

struct Face {
    std::array<Uint16, 3> vertexIndices;
    std::array<Uint16, 3> textureIndices;
};

class Model {
public:
    Model(std::string_view filename);

    const std::vector<glm::vec3>& vertices() const { return m_vertices; }
    const std::vector<glm::vec2>& uvs() const { return m_uvs; }
    const std::vector<Face>& faces() const { return m_faces; }

    friend std::ostream& operator<<(std::ostream&, const Model&);

private:
    std::vector<glm::vec3> m_vertices;
    std::vector<glm::vec2> m_uvs;
    std::vector<Face> m_faces;
    std::string m_name;

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
    BufferedModel(std::shared_ptr<SDL_GPUDevice>, std::unique_ptr<Model>,
        ShaderParameters vertex = { "position.vert", 0, 1, 0, 0 },
        ShaderParameters fragment = { "color.frag", 0, 0, 0, 0 });
    ~BufferedModel();

    virtual void draw(SDL_GPURenderPass*);

protected: // needed by MapDisplacedBufferedModel too
    std::unique_ptr<Model> m_model;
    std::shared_ptr<SDL_GPUDevice> m_GPUDevice;
    SDL_GPUBuffer* m_drawBuffer {}; // receives from m_drawTransferBuf, # indices + instances
    SDL_GPUTransferBuffer* m_drawTransferBuf {}; // SDL_GPUIndexedIndirectDrawCommand
    Uint32 m_drawBufSize;

    void setupDraw(SDL_GPURenderPass*); // binds buffers and pipelines always needed

private:
    SDL_GPUGraphicsPipeline* m_pipeline {};
    SDL_GPUBuffer* m_vertexBuffer {}; // vertex and index only need an initial upload
    SDL_GPUBuffer* m_indexBuffer {}; // ^ so don't need to store their temp transfer buffer here
    Uint32 m_vertexBufSize;
    Uint32 m_indexBufSize;
    
    SDL_GPUGraphicsPipeline* createGraphicsPipelineWithShaders(ShaderParameters vertex, ShaderParameters fragment);
    void uploadModel();
};

class GameMap;
// has special buffers to handle drawing the map model at each chosen index
class MapDisplacedBufferedModel : public BufferedModel {
public:
    MapDisplacedBufferedModel(std::shared_ptr<SDL_GPUDevice>, std::unique_ptr<Model>,
        ShaderParameters vertex = { std::string_view("position_color_shifted.vert"), 0, 1, 1, 0 },
        ShaderParameters fragment = { std::string_view("vert_input_color.frag"), 0, 0, 0, 0 });
    ~MapDisplacedBufferedModel();

    void draw(SDL_GPURenderPass*) override;
    void pushMapData(const GameMap&, SDL_GPUCommandBuffer*); // update with map data

private:
    SDL_GPUBuffer* m_mapDataBuffer {};
    SDL_GPUTransferBuffer* m_dataTransferBuf {};

    struct DisplacementColorInfo {
        glm::vec3 pos;
        uint32_t type;
        glm::vec4 color;
    };

    static constexpr unsigned s_maxRenderCopies = 289; // 15^2 standard, 17^2 max, for Barachi
};

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

class GameMap;
class Renderer {
public:
    Renderer();
    ~Renderer();
    void doRender(const GameMap&, const Camera&);

    Renderer(Renderer&) = delete;
    Renderer(Renderer&&) = delete;

    // e.g. render_info stuff
private:
    std::shared_ptr<SDL_Window> m_window;
    std::shared_ptr<SDL_GPUDevice> m_GPUDevice;

    std::unique_ptr<MapDisplacedBufferedModel> m_mapCubeModel;

    SDL_WindowID m_windowID {};
    int m_windowWidth {};
    int m_windowHeight {};

    std::string m_resourcePath;
    std::string m_shaderPath;

    void pushMapToGPU(const GameMap&);

    static constexpr unsigned s_winW = 1920;
    static constexpr unsigned s_winH = 1080;
};
