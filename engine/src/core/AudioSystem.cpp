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
    AudioSystemConfig config;

    // Bus tree: Master -> {SFX, Music, Voice} -> endpoint. Master MUST be a
    // real parent group: it used to be aliased onto groupSFX in getGroup(),
    // which made "master volume" silently duck only SFX — measured red by
    // AudioSystemTest.MasterVolumeZeroSilencesMusicChannel (Music at RMS 0.278
    // with Master=0) before this tree existed.
    ma_sound_group groupMaster;
    ma_sound_group groupSFX;
    ma_sound_group groupMusic;
    ma_sound_group groupVoice;
    ma_sound_group groupAmbience;

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

    // Outgoing music tracks still fading out (crossfade). Reaped in update()
    // once they stop; uninited there, never leaked.
    std::vector<std::shared_ptr<PooledSound>> fadingOutMusic;

    ma_sound_group* getGroup(AudioChannel channel) {
        switch (channel) {
            case AudioChannel::Master:   return &groupMaster;
            case AudioChannel::Music:    return &groupMusic;
            case AudioChannel::Voice:    return &groupVoice;
            case AudioChannel::Ambience: return &groupAmbience;
            case AudioChannel::SFX:
            default: return &groupSFX;
        }
    }

    // Long-lived loops (ambience beds etc.), keyed by handle id.
    std::unordered_map<int, std::shared_ptr<PooledSound>> activeLoops;
    int nextLoopId = 1;
};

AudioSystem::AudioSystem() : impl(std::make_unique<Impl>()) {}

AudioSystem::~AudioSystem() {
    shutdown();
}

bool AudioSystem::initialize(const AudioSystemConfig& config) {
    impl->config = config;

    // A default-constructed ma_engine_config is what ma_engine_init(NULL, ...)
    // builds internally, so the device path is unchanged by going through one.
    ma_engine_config engineConfig = ma_engine_config_init();
    if (config.deviceless) {
        engineConfig.noDevice   = MA_TRUE;
        engineConfig.channels   = config.channels;    // required without a device
        engineConfig.sampleRate = config.sampleRate;  // required without a device
    }

    ma_result result = ma_engine_init(&engineConfig, &impl->engine);
    if (result != MA_SUCCESS) {
        LOG_ERROR("Audio", "Failed to initialize audio engine.");
        return false;
    }

    // Bus tree: Master at the endpoint, category buses parented under it so
    // Master volume genuinely scales everything.
    ma_sound_group_init(&impl->engine, 0, NULL, &impl->groupMaster);
    ma_sound_group_init(&impl->engine, 0, &impl->groupMaster, &impl->groupSFX);
    ma_sound_group_init(&impl->engine, 0, &impl->groupMaster, &impl->groupMusic);
    ma_sound_group_init(&impl->engine, 0, &impl->groupMaster, &impl->groupVoice);
    ma_sound_group_init(&impl->engine, 0, &impl->groupMaster, &impl->groupAmbience);

    impl->isInitialized = true;
    LOG_INFO("Audio", "Audio System Initialized.");
    return true;
}

void AudioSystem::shutdown() {
    if (impl->isInitialized) {
        // Stop music first
        stopMusic();

        // Clean up ALL sounds — active ones included. Clearing activeSounds
        // without ma_sound_uninit leaked every still-playing decoder on exit.
        for (auto& sound : impl->activeSounds) {
            ma_sound_uninit(&sound->sound);
        }
        impl->activeSounds.clear();
        for (auto& sound : impl->fadingOutMusic) {
            ma_sound_uninit(&sound->sound);
        }
        impl->fadingOutMusic.clear();
        for (auto& pair : impl->activeLoops) {
            ma_sound_uninit(&pair.second->sound);
        }
        impl->activeLoops.clear();
        for (auto& pair : impl->availableSounds) {
            for (auto& sound : pair.second) {
                ma_sound_uninit(&sound->sound);
            }
        }
        impl->availableSounds.clear();

        // Children before parent, groups before engine.
        ma_sound_group_uninit(&impl->groupSFX);
        ma_sound_group_uninit(&impl->groupMusic);
        ma_sound_group_uninit(&impl->groupVoice);
        ma_sound_group_uninit(&impl->groupAmbience);
        ma_sound_group_uninit(&impl->groupMaster);

        ma_engine_uninit(&impl->engine);
        impl->isInitialized = false;
    }
}

size_t AudioSystem::renderFrames(float* out, size_t frameCount) {
    if (!impl->isInitialized || !impl->config.deviceless || !out) return 0;
    ma_uint64 framesRead = 0;
    ma_result result = ma_engine_read_pcm_frames(&impl->engine, out,
                                                 static_cast<ma_uint64>(frameCount), &framesRead);
    // MA_AT_END is still a valid partial read; anything else is a real failure.
    if (result != MA_SUCCESS && result != MA_AT_END) {
        LOG_ERROR("Audio", "renderFrames failed: " + std::string(ma_result_description(result)));
        return 0;
    }
    return static_cast<size_t>(framesRead);
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

    // Reap crossfaded-out music tracks once their fade completes.
    for (auto it = impl->fadingOutMusic.begin(); it != impl->fadingOutMusic.end(); ) {
        if (!ma_sound_is_playing(&(*it)->sound)) {
            ma_sound_uninit(&(*it)->sound);
            it = impl->fadingOutMusic.erase(it);
        } else {
            ++it;
        }
    }
}

void AudioSystem::playSound(const std::string& filePath, AudioChannel channel, float volume, float pitch) {
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
    ma_sound_set_pitch(&soundPtr->sound, pitch);
    ma_sound_start(&soundPtr->sound);
    impl->activeSounds.push_back(soundPtr);
}

void AudioSystem::playSound3D(const std::string& filePath, const glm::vec3& position, AudioChannel channel, float volume, const glm::vec3& velocity, float pitch, float minDistance) {
    if (!impl->isInitialized) return;

    // Audibility cull. With the inverse attenuation model (gain =
    // minDistance/distance, minDistance = 1 world unit = 1 m — see
    // docs/FineVoxelItems.md: 81 fine cells x 1.23 cm ≈ 1 m per cube), a sound
    // whose effective gain lands below -60 dB is inaudible by the same
    // convention that defines reverberation time (RT60: a level 60 dB down is
    // "decayed to silence", Sabine). miniaudio's default maxDistance is
    // FLT_MAX, so without this check a sound 10 km away still claims a decoder
    // and mixes at ~-80 dB until it ends. Cull it instead of starting it:
    // effective gain = volume * (1/distance) < 10^(-60/20) = 0.001
    //   => inaudible beyond volume * 1000 m.
    {
        ma_vec3f lp = ma_engine_listener_get_position(&impl->engine, 0);
        glm::vec3 toSource = position - glm::vec3(lp.x, lp.y, lp.z);
        float dist = glm::length(toSource);
        constexpr float kInaudibleGain = 0.001f;  // -60 dB
        // Effective inverse-model gain: volume * minDistance / dist.
        if (dist > minDistance && (volume * minDistance / dist) < kInaudibleGain) {
            return;  // would be inaudible — don't burn a voice on it
        }
    }

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
    ma_sound_set_pitch(&soundPtr->sound, pitch);
    ma_sound_set_min_distance(&soundPtr->sound, minDistance);  // set ALWAYS: pooled sounds carry the last value
    ma_sound_set_spatialization_enabled(&soundPtr->sound, MA_TRUE);
    
    ma_sound_start(&soundPtr->sound);
    impl->activeSounds.push_back(soundPtr);
}

int AudioSystem::playLoop(const std::string& filePath, AudioChannel channel, float volume,
                          unsigned fadeInMs, const glm::vec3* position) {
    if (!impl->isInitialized) return -1;

    auto soundPtr = std::make_shared<Impl::PooledSound>();
    soundPtr->filePath = filePath;
    soundPtr->channel = channel;
    // STREAM: beds are minutes long; don't hold the encoded file in RAM.
    ma_result result = ma_sound_init_from_file(&impl->engine, filePath.c_str(),
                                               MA_SOUND_FLAG_STREAM,
                                               impl->getGroup(channel), NULL, &soundPtr->sound);
    if (result != MA_SUCCESS) {
        LOG_ERROR("Audio", "Failed to load loop: " + filePath);
        return -1;
    }

    ma_sound_set_looping(&soundPtr->sound, MA_TRUE);
    ma_sound_set_volume(&soundPtr->sound, volume);
    if (position) {
        ma_sound_set_position(&soundPtr->sound, position->x, position->y, position->z);
        ma_sound_set_spatialization_enabled(&soundPtr->sound, MA_TRUE);
    } else {
        ma_sound_set_spatialization_enabled(&soundPtr->sound, MA_FALSE);
    }
    if (fadeInMs > 0) {
        ma_sound_set_fade_in_milliseconds(&soundPtr->sound, 0.0f, 1.0f, fadeInMs);
    }
    ma_sound_start(&soundPtr->sound);

    int id = impl->nextLoopId++;
    impl->activeLoops.emplace(id, std::move(soundPtr));
    return id;
}

void AudioSystem::stopLoop(int id, unsigned fadeOutMs) {
    if (!impl->isInitialized) return;
    auto it = impl->activeLoops.find(id);
    if (it == impl->activeLoops.end()) return;

    if (fadeOutMs > 0) {
        // Reuses the fading-music reaper in update(): the loop keeps playing
        // through its fade, then is uninited once ma reports it stopped.
        ma_sound_stop_with_fade_in_milliseconds(&it->second->sound, fadeOutMs);
        impl->fadingOutMusic.push_back(it->second);
    } else {
        ma_sound_stop(&it->second->sound);
        ma_sound_uninit(&it->second->sound);
    }
    impl->activeLoops.erase(it);
}

size_t AudioSystem::activeLoopCount() const {
    return impl->activeLoops.size();
}

void AudioSystem::playMusic(const std::string& filePath, float volume, unsigned fadeMs) {
    if (!impl->isInitialized) return;

    // Stop current music if playing (fades out under a crossfade)
    stopMusic(fadeMs);

    auto soundPtr = std::make_shared<Impl::PooledSound>();
    soundPtr->filePath = filePath;
    soundPtr->channel = AudioChannel::Music;
    // STREAM: music tracks are minutes long — page-stream from disk instead of
    // holding the whole encoded file in memory (the flags=0 default).
    ma_result result = ma_sound_init_from_file(&impl->engine, filePath.c_str(), MA_SOUND_FLAG_STREAM, impl->getGroup(AudioChannel::Music), NULL, &soundPtr->sound);
    if (result != MA_SUCCESS) {
        LOG_ERROR("Audio", "Failed to load music: " + filePath);
        return;
    }

    ma_sound_set_looping(&soundPtr->sound, MA_TRUE);
    ma_sound_set_spatialization_enabled(&soundPtr->sound, MA_FALSE);
    ma_sound_set_volume(&soundPtr->sound, volume);
    if (fadeMs > 0) {
        // Fade multiplier ramps 0 -> 1 on top of the volume set above.
        ma_sound_set_fade_in_milliseconds(&soundPtr->sound, 0.0f, 1.0f, fadeMs);
    }
    ma_sound_start(&soundPtr->sound);

    impl->currentMusic = soundPtr;
    impl->currentMusicPath = filePath;
    LOG_INFO("Audio", "Playing music: " + filePath);
}

void AudioSystem::stopMusic(unsigned fadeMs) {
    if (!impl->isInitialized) return;
    if (impl->currentMusic) {
        if (fadeMs > 0) {
            // Fade out, keep the handle alive until it actually stops —
            // update() reaps + uninits it. Hard-uniniting here is the old
            // pop-on-every-transition behavior.
            ma_sound_stop_with_fade_in_milliseconds(&impl->currentMusic->sound, fadeMs);
            impl->fadingOutMusic.push_back(impl->currentMusic);
        } else {
            ma_sound_stop(&impl->currentMusic->sound);
            ma_sound_uninit(&impl->currentMusic->sound);
        }
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

glm::vec3 AudioSystem::getListenerPosition() const {
    if (!impl->isInitialized) return glm::vec3(0.0f);
    ma_vec3f p = ma_engine_listener_get_position(&impl->engine, 0);
    return glm::vec3(p.x, p.y, p.z);
}

size_t AudioSystem::activeSoundCount() const {
    return impl->activeSounds.size();
}

size_t AudioSystem::pooledSoundCount() const {
    size_t n = 0;
    for (const auto& pair : impl->availableSounds) n += pair.second.size();
    return n;
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
