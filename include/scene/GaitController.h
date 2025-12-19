#pragma once

#include <vector>
#include <glm/glm.hpp>

namespace VulkanCube {
namespace Scene {

class Limb;
class RagdollCharacter;

class GaitController {
public:
    GaitController(RagdollCharacter* owner);
    virtual ~GaitController() = default;

    void addLimb(Limb* limb);
    void update(float deltaTime, const glm::vec3& desiredVelocity);

    // Gait parameters
    void setStepHeight(float height) { stepHeight = height; }
    void setStepDuration(float duration) { stepDuration = duration; }
    void setStepDistance(float distance) { stepDistance = distance; }

private:
    struct LimbState {
        Limb* limb;
        bool isSwinging;
        float swingTimer; // Counts up to stepDuration
        glm::vec3 swingStartPos;
        glm::vec3 swingTargetPos;
    };

    RagdollCharacter* owner;
    std::vector<LimbState> limbs;

    float stepHeight = 0.5f;
    float stepDuration = 0.25f;
    float stepDistance = 0.5f; // How far ahead to step
    
    // Gait timing
    int nextLimbToStep = 0;
    float timeSinceLastStep = 0.0f;
    float stepInterval = 0.1f; // Time between initiating steps for different legs

    glm::vec3 calculateIdealFootPosition(Limb* limb, const glm::vec3& velocity);
};

} // namespace Scene
} // namespace VulkanCube
