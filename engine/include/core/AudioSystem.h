#pragma once

#include <glm/glm.hpp>
#include <string>
#include <memory>
#include <vector>

namespace Phyxel {
namespace Core {

enum class AudioChannel {
    Master,
    SFX,
    Music,
    Voice
};

class AudioSystem {
public:
    AudioSystem();
    ~AudioSystem();

    bool initialize();
    void shutdown();
    
    // Update listener (player) position and velocity for 3D audio
    void update(const glm::vec3& listenerPos, const glm::vec3& listenerForward, const glm::vec3& listenerUp, const glm::vec3& listenerVelocity = glm::vec3(0.0f));

    // Play a sound file (fire and forget) - 2D
    void playSound(const std::string& filePath, AudioChannel channel = AudioChannel::SFX, float volume = 1.0f);

    // Play a 3D sound at a specific position
    void playSound3D(const std::string& filePath, const glm::vec3& position, AudioChannel channel = AudioChannel::SFX, float volume = 1.0f, const glm::vec3& velocity = glm::vec3(0.0f));

    // Background music (looping, one track at a time)
    void playMusic(const std::string& filePath, float volume = 1.0f);
    void stopMusic();
    bool isMusicPlaying() const;
    std::string getMusicTrack() const;

    // Set volume for a specific channel (0.0 to 1.0)
    void setChannelVolume(AudioChannel channel, float volume);

    // Preload a sound into memory (optional optimization)
    void preloadSound(const std::string& filePath);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace Core
} // namespace Phyxel
