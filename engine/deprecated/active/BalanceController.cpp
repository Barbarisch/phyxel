#include "scene/active/BalanceController.h"
#include <btBulletDynamicsCommon.h>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>

namespace Phyxel {
namespace Scene {

// ============================================================================
// COM computation (mass-weighted average of all part positions)
// ============================================================================

glm::vec3 BalanceController::computeCOM(const std::vector<RagdollPart>& parts) {
    glm::vec3 sum(0.0f);
    float totalMass = 0.0f;

    for (const auto& p : parts) {
        if (!p.rigidBody) continue;
        float invMass = p.rigidBody->getInvMass();
        float mass    = (invMass > 0.00001f) ? (1.0f / invMass) : 0.0f;
        btVector3 pos = p.rigidBody->getCenterOfMassPosition();
        sum       += glm::vec3(pos.x(), pos.y(), pos.z()) * mass;
        totalMass += mass;
    }

    return (totalMass > 0.0f) ? (sum / totalMass) : glm::vec3(0.0f);
}

// ============================================================================
// Main: compute corrective lateral force for the root body
// ============================================================================

glm::vec3 BalanceController::computeCorrectiveForce(
    const std::vector<RagdollPart>& parts,
    btRigidBody* rootBody,
    const glm::vec3& supportCenter,
    float dt)
{
    if (!rootBody) return glm::vec3(0.0f);

    com_ = computeCOM(parts);

    // Lateral error: how far the COM is from directly above the support polygon
    glm::vec2 comXZ(com_.x, com_.z);
    glm::vec2 supXZ(supportCenter.x, supportCenter.z);
    glm::vec2 error2D = supXZ - comXZ;

    // Derivative: change in error from last frame
    glm::vec3 error3D(error2D.x, 0.0f, error2D.y);
    glm::vec3 dError  = (error3D - prevError_) / std::max(dt, 0.0001f);
    prevError_ = error3D;

    // PD corrective force (lateral XZ only)
    glm::vec3 force = config_.kp * error3D + config_.kd * dError;

    // Clamp magnitude
    float mag = glm::length(force);
    if (mag > config_.maxForce) force *= (config_.maxForce / mag);

    return force;
}

} // namespace Scene
} // namespace Phyxel
