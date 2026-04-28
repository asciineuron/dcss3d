#pragma once

#include <SDL3/SDL.h>
#include <string_view>

// Parameters for loading a compiled GPU shader.
struct ShaderParameters {
    std::string_view filename;
    Uint32 samplerCount {};
    Uint32 uniformBufferCount {};
    Uint32 storageBufferCount {};
    Uint32 storageTextureCount {};
};

// Load a compiled shader file from the build output directory.
// Detects the backend format (SPIR-V, Metal, DXIL) and loads
// the appropriate compiled artifact.
SDL_GPUShader* loadShader(SDL_GPUDevice* device, const ShaderParameters& parameters);
