#include "core/AudioSystem.h"
#include "utils/Logger.h"

#include <unordered_map>
#include <vector>
#include <algorithm>

// Define implementation only in one cpp file
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

namespace Phyxel {
namespace Core {

struct AudioSystem::Impl {
    ma_engine engine;
    bool isInitialized = false;

    ma_sound_group groupSFX;
    ma_sound_group groupMusic;
    ma_sound_group groupVoice;

    struct PooledSound {
        ma_sound sound; // Not a pointer, the struct itself
        std::string filePath;
        AudioChannel channel;
    };

    // Sounds currently playing
    std::vector<std::shared_ptr<PooledSound>> activeSounds;
    
    // Sounds ready to be reused, keyed by file path
    std::unordered_map<std::string, std::vector<std::shared_ptr<PooledSound>>> availableSounds;

    // Background music (one track at a time, looping)
    std::shared_ptr<PooledSound> currentMusic;
    std::string currentMusicPath;

    ma_sound_group* getGroup(AudioChannel channel) {
        switch (channel) {
            case AudioChannel::Music: return &groupMusic;
            case AudioChannel::Voice: return &groupVoice;
            case AudioChannel::SFX: 
            default: return &groupSFX;
        }
    }
};

AudioSystem::AudioSystem() : impl(std::make_unique<Impl>()) {}

AudioSystem::~AudioSystem() {
    shutdown();
}

bool AudioSystem::initialize() {
    ma_result result = ma_engine_init(NULL, &impl->engine);
    if (result != MA_SUCCESS) {
        LOG_ERROR("Audio", "Failed to initialize audio engine.");
        return false;
    }

    // Initialize groups
    ma_sound_group_init(&impl->engine, 0, NULL, &impl->groupSFX);
    ma_sound_group_init(&impl->engine, 0, NULL, &impl->groupMusic);
    ma_sound_group_init(&impl->engine, 0, NULL, &impl->groupVoice);

    impl->isInitialized = true;
    LOG_INFO("Audio", "Audio System Initialized.");
    return true;
}

void AudioSystem::shutdown() {
    if (impl->isInitialized) {
        // Stop music first
        stopMusic();

        // Clean up all sounds
        impl->activeSounds.clear();
        for (auto& pair : impl->availableSounds) {
            for (auto& sound : pair.second) {
                ma_sound_uninit(&sound->sound);
            }
        }
        impl->availableSounds.clear();

        ma_engine_uninit(&impl->engine);
        impl->isInitialized = false;
    }
}

void AudioSystem::update(const glm::vec3& listenerPos, const glm::vec3& listenerForward, const glm::vec3& listenerUp, const glm::vec3& listenerVelocity) {
    if (!impl->isInitialized) return;
    
    // Update listener 0 (default listener)
    ma_engine_listener_set_position(&impl->engine, 0, listenerPos.x, listenerPos.y, listenerPos.z);
    ma_engine_listener_set_direction(&impl->engine, 0, listenerForward.x, listenerForward.y, listenerForward.z);
    ma_engine_listener_set_world_up(&impl->engine, 0, listenerUp.x, listenerUp.y, listenerUp.z);
    ma_engine_listener_set_velocity(&impl->engine, 0, listenerVelocity.x, listenerVelocity.y, listenerVelocity.z);

    // Recycle finished sounds
    for (auto it = impl->activeSounds.begin(); it != impl->activeSounds.end(); ) {
        if (!ma_sound_is_playing(&(*it)->sound)) {
            // Sound finished, move to available
            impl->availableSounds[(*it)->filePath].push_back(*it);
            it = impl->activeSounds.erase(it);
        } else {
            ++it;
        }
    }
}

void AudioSystem::playSound(const std::string& filePath, AudioChannel channel, float volume) {
    if (!impl->isInitialized) return;

    std::shared_ptr<Impl::PooledSound> soundPtr;
    auto& pool = impl->availableSounds[filePath];
    
    // Find one with matching channel
    for (auto it = pool.begin(); it != pool.end(); ++it) {
        if ((*it)->channel == channel) {
            soundPtr = *it;
            pool.erase(it);
            break;
        }
    }
    
    if (soundPtr) {
        ma_sound_seek_to_pcm_frame(&soundPtr->sound, 0);
    } else {
        soundPtr = std::make_shared<Impl::PooledSound>();
        soundPtr->filePath = filePath;
        soundPtr->channel = channel;
        ma_result result = ma_sound_init_from_file(&impl->engine, filePath.c_str(), 0, impl->getGroup(channel), NULL, &soundPtr->sound);
        if (result != MA_SUCCESS) {
            LOG_ERROR("Audio", "Failed to load sound: " + filePath);
            return;
        }
    }

    ma_sound_set_spatialization_enabled(&soundPtr->sound, MA_FALSE);
    ma_sound_set_volume(&soundPtr->sound, volume);
    ma_sound_start(&soundPtr->sound);
    impl->activeSounds.push_back(soundPtr);
}

void AudioSystem::playSound3D(const std::string& filePath, const glm::vec3& position, AudioChannel channel, float volume, const glm::vec3& velocity) {
    if (!impl->isInitialized) return;

    std::shared_ptr<Impl::PooledSound> soundPtr;

    // Try to get from pool
    auto& pool = impl->availableSounds[filePath];
    
    // Find one with matching channel
    for (auto it = pool.begin(); it != pool.end(); ++it) {
        if ((*it)->channel == channel) {
            soundPtr = *it;
            pool.erase(it);
            break;
        }
    }

    if (soundPtr) {
        // Reset and configure
        ma_sound_seek_to_pcm_frame(&soundPtr->sound, 0);
    } else {
        // Create new
        soundPtr = std::make_shared<Impl::PooledSound>();
        soundPtr->filePath = filePath;
        soundPtr->channel = channel;
        
        ma_result result = ma_sound_init_from_file(&impl->engine, filePath.c_str(), 0, impl->getGroup(channel), NULL, &soundPtr->sound);
        if (result != MA_SUCCESS) {
            LOG_ERROR("Audio", "Failed to load sound: " + filePath);
            return;
        }
    }

    // Configure 3D
    ma_sound_set_position(&soundPtr->sound, position.x, position.y, position.z);
    ma_sound_set_velocity(&soundPtr->sound, velocity.x, velocity.y, velocity.z);
    ma_sound_set_volume(&soundPtr->sound, volume);
    ma_sound_set_spatialization_enabled(&soundPtr->sound, MA_TRUE);
    
    ma_sound_start(&soundPtr->sound);
    impl->activeSounds.push_back(soundPtr);
}

void AudioSystem::playMusic(const std::string& filePath, float volume) {
    if (!impl->isInitialized) return;

    // Stop current music if playing
    stopMusic();

    auto soundPtr = std::make_shared<Impl::PooledSound>();
    soundPtr->filePath = filePath;
    soundPtr->channel = AudioChannel::Music;
    ma_result result = ma_sound_init_from_file(&impl->engine, filePath.c_str(), 0, impl->getGroup(AudioChannel::Music), NULL, &soundPtr->sound);
    if (result != MA_SUCCESS) {
        LOG_ERROR("Audio", "Failed to load music: " + filePath);
        return;
    }

    ma_sound_set_looping(&soundPtr->sound, MA_TRUE);
    ma_sound_set_spatialization_enabled(&soundPtr->sound, MA_FALSE);
    ma_sound_set_volume(&soundPtr->sound, volume);
    ma_sound_start(&soundPtr->sound);

    impl->currentMusic = soundPtr;
    impl->currentMusicPath = filePath;
    LOG_INFO("Audio", "Playing music: " + filePath);
}

void AudioSystem::stopMusic() {
    if (!impl->isInitialized) return;
    if (impl->currentMusic) {
        ma_sound_stop(&impl->currentMusic->sound);
        ma_sound_uninit(&impl->currentMusic->sound);
        impl->currentMusic.reset();
        impl->currentMusicPath.clear();
    }
}

bool AudioSystem::isMusicPlaying() const {
    if (!impl->isInitialized || !impl->currentMusic) return false;
    return ma_sound_is_playing(&impl->currentMusic->sound) == MA_TRUE;
}

std::string AudioSystem::getMusicTrack() const {
    return impl->currentMusicPath;
}

void AudioSystem::setChannelVolume(AudioChannel channel, float volume) {
    if (!impl->isInitialized) return;
    ma_sound_group_set_volume(impl->getGroup(channel), volume);
}

void AudioSystem::preloadSound(const std::string& filePath) {
    if (!impl->isInitialized) return;
    
    auto soundPtr = std::make_shared<Impl::PooledSound>();
    soundPtr->filePath = filePath;
    soundPtr->channel = AudioChannel::SFX; // Default to SFX for preload
    // Use DECODE flag to force loading into memory if we are preloading
    ma_result result = ma_sound_init_from_file(&impl->engine, filePath.c_str(), MA_SOUND_FLAG_DECODE, impl->getGroup(AudioChannel::SFX), NULL, &soundPtr->sound);
    if (result == MA_SUCCESS) {
        impl->availableSounds[filePath].push_back(soundPtr);
    }
}

} // namespace Core
} // namespace Phyxel
