#pragma once
#include "GameMap.hpp"
#include "ShaderUtil.hpp"
#include "Skybox.hpp"
#include <SDL3/SDL.h>
#include <atomic>
#include <glm/glm.hpp>
#include <iostream>
#include <memory>
#include <vector>

struct Camera {
    glm::vec3 pos { 0.5f, 0.f, -0.5f };
    float theta { 1.570796f }; // pi/2 to look forward (-Z direction)
    float phi { 0.f };
    float fov { 0.785398f };
    float aspectRatio { 1.777777f };

    glm::mat4 toViewProjection() const;

    // View-projection with translation stripped (rotation only).
    // Used for skybox rendering so the skybox stays centered on the camera.
    glm::mat4 toSkyViewProjection() const;
};

struct Face {
    std::array<Uint16, 3> vertexIndices;
    std::array<Uint16, 3> textureIndices;
    std::array<Uint16, 3> normalIndices {};  // 0 = absent, 1+ = 1-based index into rawNormals
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

    // Scale all vertices so the model fits within a unit cube centered at origin,
    // with its base on the floor plane (y=0). Normals are re-normalized.
    void scaleToUnitCube();

    friend std::ostream& operator<<(std::ostream&, const Model&);

private:
    std::vector<glm::vec3> m_vertices;
    std::vector<glm::vec2> m_uvs;
    std::vector<glm::vec3> m_rawNormals; // OBJ vn lines — empty if model has no vertex normals
    std::vector<glm::vec3> m_normals;   // expanded per-vertex normals (indexed from raw or computed)
    std::vector<Face> m_faces;
    std::string m_name;
    const std::string m_resourcePath;

    void loadObj(std::string_view filename);
};

// this base class can draw a single model at its designated position
class BufferedModel {
public:
    BufferedModel(SDL_GPUDevice*, SDL_Window*, std::unique_ptr<Model>,
        ShaderParameters vertex = { "position.vert", 0, 1, 0, 0 },
        ShaderParameters fragment = { "color.frag", 0, 0, 0, 0 },
        std::string_view textureFilename = {});
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

    // Texture members
    SDL_GPUTexture* m_texture {};
    SDL_GPUSampler* m_sampler {};
    std::string m_textureFilename;

    bool m_hasReleased {};

    SDL_GPUGraphicsPipeline* createGraphicsPipelineWithShaders(SDL_Window* window, ShaderParameters vertex, ShaderParameters fragment);
    void uploadModel();
    void loadTexture(SDL_GPUCommandBuffer* cmdBuf, const std::string& filename);
};

// has special buffers to handle drawing the map model at each chosen index
class MapDisplacedBufferedModel : public BufferedModel {
public:
    MapDisplacedBufferedModel(SDL_GPUDevice*, SDL_Window*, std::unique_ptr<Model>,
        ShaderParameters vertex = { std::string_view("position_color_shifted.vert"), 0, 1, 1, 0 },
        ShaderParameters fragment = { std::string_view("lit.frag"), 0, 1, 0, 0 },
        std::string_view textureFilename = {});
    ~MapDisplacedBufferedModel();

    void release() override;

    void draw(SDL_GPURenderPass*) override;
    void pushMapData(const GameMap&, Pos2<int> playerPos, SDL_GPUCommandBuffer*);

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

    static constexpr unsigned s_maxRenderCopies = 1024; // 32x32 max visible area (15-cell radius)
};

// Draws a shared model (e.g. monkey) at each monster position on the map.
// Same pattern as MapDisplacedBufferedModel: slot 0 = per-vertex (position/normal/UV),
// slot 1 = per-instance (DisplacementColorInfo: shift + color).
class MonsterBufferedModel : public BufferedModel {
public:
    MonsterBufferedModel(SDL_GPUDevice*, SDL_Window*, std::unique_ptr<Model>,
        ShaderParameters vertex = { std::string_view("position_monster.vert"), 0, 2, 0, 0 },
        ShaderParameters fragment = { std::string_view("solid.frag"), 0, 1, 0, 0 },
        std::string_view textureFilename = {});
    ~MonsterBufferedModel();

    void release() override;

    void draw(SDL_GPURenderPass*) override;

    // Push instance data for a filtered subset of monsters (pre-grouped by model type).
    // Accepts a vector of (position, monster-pointer) pairs.
    void pushMonsterData(const std::vector<std::pair<Pos2<int>, const Monster*>>&,
                         SDL_GPUCommandBuffer*);

private:
    SDL_GPUBuffer* m_monsterInstanceBuffer {};
    SDL_GPUTransferBuffer* m_monsterTransferBuf {};
    bool m_hasReleased;

    // Reusing the same 32-byte struct as MapDisplacedBufferedModel
    struct DisplacementColorInfo {
        float shiftX;
        float shiftY;
        float tileType;  // sentinel 999 = "don't sink, it's a monster"
        float padding;
        glm::vec4 color;
    };

    static constexpr unsigned s_maxMonsterInstances = 256;
};


std::ostream& operator<<(std::ostream& os, const Model& model);

class Renderer {
public:
    Renderer();
    ~Renderer();

    Renderer(Renderer&) = delete;
    Renderer(Renderer&&) = delete;

    void doRender(GameMap&, const Camera&, Pos2<int> playerPos);

    const uint64_t renderCount() const;

    SDL_Window* window() { return m_window; }

    SDL_GPUDevice* gpu_device() { return m_GPUDevice; }


    // Set the world-space position of the target cell for highlighting.
    // w component: 1.0 = enabled, 0.0 = disabled (no cell to highlight).
    void setTargetHighlight(glm::vec4 targetPos) { m_targetHighlightPos = targetPos; }

    // e.g. render_info stuff
private:
    SDL_Window* m_window;
    SDL_GPUDevice* m_GPUDevice;

    std::unique_ptr<MapDisplacedBufferedModel> m_mapCubeModel;
    std::unique_ptr<Skybox> m_skybox;

    // Model cache: keyed by OBJ filename, so multiple monster types can share the same geometry.
    // Lazily populated via getOrCreateMonsterModel().
    std::unordered_map<std::string, std::unique_ptr<MonsterBufferedModel>> m_monsterModelCache;
    MonsterBufferedModel* getOrCreateMonsterModel(const std::string& modelFile);

    SDL_WindowID m_windowID {};
    int m_windowWidth {};
    int m_windowHeight {};

    std::atomic_uint64_t m_renderCount;

    // Depth buffer for proper filled polygon rendering
    SDL_GPUTexture* m_depthTexture {};

    // Target highlight position in render coords: x=worldX, y=worldY(0), z=worldZ, w=enabled
    glm::vec4 m_targetHighlightPos {0.0f, 0.0f, 0.0f, 0.0f};

    void pushMapToGPU(const GameMap&, Pos2<int> playerPos, SDL_GPUCommandBuffer*);
    void pushMonsterToGPU(const GameMap&, SDL_GPUCommandBuffer*);
    void createDepthTexture();
    void releaseDepthTexture();

    static constexpr unsigned s_winW = 1920;
    static constexpr unsigned s_winH = 1080;
};
