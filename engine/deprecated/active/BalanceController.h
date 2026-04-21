#pragma once

#include "scene/RagdollCharacter.h"
#include <glm/glm.hpp>
#include <vector>

class btRigidBody;

namespace Phyxel {
namespace Scene {

/// Tracks the center of mass of all ragdoll parts and computes a corrective
/// lateral force to keep the COM above the support polygon (midpoint of feet).
/// Applied to the root body each physics substep.
class BalanceController {
public:
    struct Config {
        float kp = 45.0f;            ///< Proportional gain for lateral COM correction
        float kd =  9.0f;            ///< Derivative gain (damps oscillation)
        float maxForce = 400.0f;     ///< Maximum corrective force magnitude
    };

    /// Compute corrective force to apply to the root body this substep.
    ///
    /// @param parts         All ragdoll parts (used to compute weighted COM)
    /// @param rootBody      The root rigid body (force is applied here)
    /// @param supportCenter XZ midpoint of planted foot positions (world space)
    /// @param dt            Physics substep duration
    /// @return              Force vector (world space) to pass to applyCentralForce()
    glm::vec3 computeCorrectiveForce(const std::vector<RagdollPart>& parts,
                                      btRigidBody* rootBody,
                                      const glm::vec3& supportCenter,
                                      float dt);

    glm::vec3 getCOM() const { return com_; }

    Config& config()             { return config_; }
    const Config& config() const { return config_; }

private:
    glm::vec3 computeCOM(const std::vector<RagdollPart>& parts);

    glm::vec3 com_{0.0f};
    glm::vec3 prevError_{0.0f};
    Config config_;
};

} // namespace Scene
} // namespace Phyxel
