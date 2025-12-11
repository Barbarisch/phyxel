#pragma once

#include <glm/glm.hpp>
#include <string>
#include <memory>

namespace VulkanCube {
namespace Core {

class AudioSystem {
public:
    AudioSystem();
    ~AudioSystem();

    bool initialize();
    void shutdown();
    
    // Update listener (player) position for 3D audio
    void update(const glm::vec3& listenerPos, const glm::vec3& listenerForward, const glm::vec3& listenerUp);

    // Play a sound file (fire and forget)
    void playSound(const std::string& filePath);

    // Play a 3D sound at a specific position
    // Note: For MVP this might just play 2D until we implement a sound pool
    void playSound3D(const std::string& filePath, const glm::vec3& position);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace Core
} // namespace VulkanCube
