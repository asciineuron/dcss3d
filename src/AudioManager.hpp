#pragma once
#include "MessageQueue.hpp"
#include <SDL3/SDL.h>
#include <string>
#include <unordered_map>
#include <vector>

class AudioManager : public MessageHandler {
public:
    AudioManager();
    ~AudioManager();

    AudioManager(AudioManager&) = delete;
    AudioManager(AudioManager&&) = delete;

    void handleMessage(const json& message) override;

    // Play a named sound effect. Safe to call from any thread.
    void triggerSound(const std::string& name);

    // For testing
    const std::vector<std::string>& triggeredSounds() const { return m_triggeredSounds; }
    void clearTriggeredSounds() { m_triggeredSounds.clear(); }

private:
    struct SoundClip {
        Uint8* data = nullptr;
        Uint32 length = 0;
        SDL_AudioSpec spec {};
    };

    const SoundClip* loadSound(const std::string& name);

    SDL_AudioStream* m_audioStream = nullptr;
    std::unordered_map<std::string, SoundClip> m_soundCache;
    std::vector<std::string> m_triggeredSounds;

    static std::string soundPath(const std::string& name);
};
