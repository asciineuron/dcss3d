#pragma once

#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>

// Renders a cubemap skybox — a large cube textured with 6 environment images
// that surrounds the entire scene, giving the illusion of a distant environment.
class Skybox {
public:
    Skybox(SDL_GPUDevice* device, SDL_Window* window);
    ~Skybox();

    Skybox(Skybox&) = delete;
    Skybox(Skybox&&) = delete;

    // Draw the skybox. Pass the viewproj matrix with translation stripped
    // (rotation only) so the skybox stays centered on the camera.
    void draw(SDL_GPURenderPass* renderPass, const glm::mat4& viewProjNoTranslation);

    bool isLoaded() const { return m_loaded; }

private:
    void release();

    // Load the 6 face images, create cubemap texture & sampler
    void loadCubemap(SDL_GPUCommandBuffer* cmdBuf);

    // Create the skybox-specific graphics pipeline
    SDL_GPUGraphicsPipeline* createSkyboxPipeline(SDL_Window* window);

    // Upload the skybox cube mesh (unit cube vertices)
    void uploadCubeMesh();

    SDL_GPUDevice* m_GPUDevice;
    SDL_Window* m_window;
    bool m_loaded = false;
    bool m_released = false;

    // Cubemap texture and sampler
    SDL_GPUTexture* m_cubemapTexture = nullptr;
    SDL_GPUSampler* m_cubemapSampler = nullptr;

    // Pipeline
    SDL_GPUGraphicsPipeline* m_pipeline = nullptr;

    // Cube mesh
    SDL_GPUBuffer* m_vertexBuffer = nullptr;
    SDL_GPUBuffer* m_indexBuffer = nullptr;
    Uint32 m_vertexCount = 0;
    Uint32 m_indexCount = 0;

    // Cube face order (SDL3_GPU / Metal convention):
    // Layer 0: +X (right), 1: -X (left), 2: +Y (top), 3: -Y (bottom),
    // Layer 4: +Z (away from viewer = back), 5: -Z (toward viewer = front).
    // Metal uses left-handed cubemaps: +Z = away, -Z = toward viewer,
    // opposite of OpenGL convention.
    static inline const std::vector<std::string> s_faceFilenames = {
        "resources/skybox/right.jpg",   // +X
        "resources/skybox/left.jpg",    // -X
        "resources/skybox/top.jpg",     // +Y
        "resources/skybox/bottom.jpg",  // -Y
        "resources/skybox/front.jpg",   // +Z
        "resources/skybox/back.jpg",    // -Z
    };
};
