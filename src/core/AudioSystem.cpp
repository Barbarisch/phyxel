#include "core/AudioSystem.h"
#include "utils/Logger.h"

// Define implementation only in one cpp file
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

namespace VulkanCube {
namespace Core {

struct AudioSystem::Impl {
    ma_engine engine;
    bool isInitialized = false;
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
    impl->isInitialized = true;
    LOG_INFO("Audio", "Audio System Initialized.");
    return true;
}

void AudioSystem::shutdown() {
    if (impl->isInitialized) {
        ma_engine_uninit(&impl->engine);
        impl->isInitialized = false;
    }
}

void AudioSystem::update(const glm::vec3& listenerPos, const glm::vec3& listenerForward, const glm::vec3& listenerUp) {
    if (!impl->isInitialized) return;
    
    // Update listener 0 (default listener)
    ma_engine_listener_set_position(&impl->engine, 0, listenerPos.x, listenerPos.y, listenerPos.z);
    ma_engine_listener_set_direction(&impl->engine, 0, listenerForward.x, listenerForward.y, listenerForward.z);
    ma_engine_listener_set_world_up(&impl->engine, 0, listenerUp.x, listenerUp.y, listenerUp.z);
}

void AudioSystem::playSound(const std::string& filePath) {
    if (!impl->isInitialized) return;
    
    ma_result result = ma_engine_play_sound(&impl->engine, filePath.c_str(), NULL);
    if (result != MA_SUCCESS) {
        LOG_ERROR("Audio", "Failed to play sound: " + filePath);
    }
}

void AudioSystem::playSound3D(const std::string& filePath, const glm::vec3& position) {
    if (!impl->isInitialized) return;

    // For a proper 3D sound in miniaudio, we need to create a ma_sound object,
    // set its position, start it, and then manage its lifecycle (destroy when done).
    // For this MVP, we will just play it 2D to verify the system works, 
    // as implementing a full SoundManager with pooling is a larger task.
    // TODO: Implement SoundManager for 3D sound pooling.
    
    playSound(filePath);
}

} // namespace Core
} // namespace VulkanCube
