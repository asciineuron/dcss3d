#include "AudioManager.hpp"
#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_log.h>
#include <spdlog/spdlog.h>
#include <stdexcept>

// Custom deleters aren't needed since we manage WAV buffers manually with SDL_free,
// and stream via SDL_DestroyAudioStream.

AudioManager::AudioManager()
{
    if (!SDL_WasInit(SDL_INIT_AUDIO)) {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            throw std::runtime_error(std::format("AudioManager: SDL_InitSubSystem(AUDIO) failed: {}", SDL_GetError()));
        }
    }

    m_audioStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr, nullptr, nullptr);
    if (!m_audioStream) {
        throw std::runtime_error(std::format("AudioManager: SDL_OpenAudioDeviceStream failed: {}", SDL_GetError()));
    }

    if (!SDL_ResumeAudioStreamDevice(m_audioStream)) {
        SDL_DestroyAudioStream(m_audioStream);
        m_audioStream = nullptr;
        throw std::runtime_error(std::format("AudioManager: SDL_ResumeAudioStreamDevice failed: {}", SDL_GetError()));
    }

    spdlog::info("AudioManager initialized");
}

AudioManager::~AudioManager()
{
    for (auto& [name, clip] : m_soundCache) {
        if (clip.data) {
            SDL_free(clip.data);
        }
    }
    m_soundCache.clear();

    if (m_audioStream) {
        SDL_DestroyAudioStream(m_audioStream);
        m_audioStream = nullptr;
    }

    spdlog::info("AudioManager destroyed");
}

void AudioManager::handleMessage(const json& message)
{
    if (message.contains("msg") && message["msg"] == "map") {
        triggerSound("map_update");
    } else if (message.contains("msg") && message["msg"] == "game_ended") {
        triggerSound("game_over");
    }
}

void AudioManager::triggerSound(const std::string& name)
{
    // Record for testability
    m_triggeredSounds.push_back(name);

    const SoundClip* clip = loadSound(name);
    if (!clip || !clip->data || clip->length == 0) {
        spdlog::error("AudioManager: sound '{}' not loaded", name);
        return;
    }

    // Configure stream to match the WAV format for this push
    if (!SDL_SetAudioStreamFormat(m_audioStream, &clip->spec, nullptr)) {
        spdlog::error("AudioManager: SDL_SetAudioStreamFormat failed: {}", SDL_GetError());
        return;
    }

    if (!SDL_PutAudioStreamData(m_audioStream, clip->data, static_cast<int>(clip->length))) {
        spdlog::error("AudioManager: SDL_PutAudioStreamData failed: {}", SDL_GetError());
        return;
    }

    spdlog::debug("AudioManager: triggered sound '{}'", name);
}

const AudioManager::SoundClip* AudioManager::loadSound(const std::string& name)
{
    auto it = m_soundCache.find(name);
    if (it != m_soundCache.end()) {
        return &it->second;
    }

    std::string path = soundPath(name);
    SoundClip clip;

    if (!SDL_LoadWAV(path.c_str(), &clip.spec, &clip.data, &clip.length)) {
        spdlog::error("AudioManager: SDL_LoadWAV('{}') failed: {}", path, SDL_GetError());
        // Insert an empty clip so we don't retry on every trigger
        m_soundCache[name] = { };
        return nullptr;
    }

    spdlog::info("AudioManager: loaded sound '{}' from '{}' ({} bytes, {}Hz, {} ch)",
        name, path, clip.length, clip.spec.freq, clip.spec.channels);

    m_soundCache[name] = clip;
    return &m_soundCache[name];
}

std::string AudioManager::soundPath(const std::string& name)
{
    // Sounds are in resources/sounds/ relative to the binary directory
    return std::format("{}resources/sounds/{}.wav", SDL_GetBasePath(), name);
}
