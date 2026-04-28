#include "ShaderUtil.hpp"
#include <SDL3/SDL_gpu.h>
#include <filesystem>
#include <format>
#include <fstream>
#include <stdexcept>

namespace fs = std::filesystem;

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
    code.resize_and_overwrite(codeLen, [&](char* buf, std::size_t len) {
        shaderFile.read(buf, len);
        return shaderFile.gcount();
    });

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
        throw std::runtime_error(std::format("failed to create shader {}", parameters.filename));
    }
    return shader;
}
