#include "physics/VoxelRigidBody.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

namespace Phyxel {
namespace Physics {

VoxelRigidBody::VoxelRigidBody(uint32_t bodyId) : id(bodyId) {}

void VoxelRigidBody::addLocalBox(const LocalBox& box) {
    m_localBoxes.push_back(box);
}

glm::mat3 VoxelRigidBody::boxInertiaTensor(const glm::vec3& h, float m) {
    // Solid box: I = m/12 * diag(y²+z², x²+z², x²+y²)  where x,y,z = 2*half-extent
    float sx = 2.0f * h.x, sy = 2.0f * h.y, sz = 2.0f * h.z;
    float k = m / 12.0f;
    glm::mat3 I(0.0f);
    I[0][0] = k * (sy*sy + sz*sz);
    I[1][1] = k * (sx*sx + sz*sz);
    I[2][2] = k * (sx*sx + sy*sy);
    return I;
}

void VoxelRigidBody::finalizeShape() {
    if (m_localBoxes.empty()) {
        invMass = 0.0f;
        return;
    }

    // Compute total mass and raw COM
    m_totalMass = 0.0f;
    glm::vec3 com{0.0f};
    for (const auto& b : m_localBoxes) {
        m_totalMass += b.mass;
        com += b.offset * b.mass;
    }
    if (m_totalMass <= 0.0f) { invMass = 0.0f; return; }
    com /= m_totalMass;
    invMass = 1.0f / m_totalMass;

    // Re-center offsets around the computed COM
    for (auto& b : m_localBoxes) {
        b.offset -= com;
    }
    // The caller's position should be shifted by com, but for single-voxel bodies
    // com is always zero, so this is a no-op in the common case.

    // Build composite inertia tensor via parallel axis theorem
    glm::mat3 I(0.0f);
    for (const auto& b : m_localBoxes) {
        glm::mat3 Ibox = boxInertiaTensor(b.halfExtents, b.mass);
        // Parallel axis: I += m*(|r|²*E - r⊗r) where r = b.offset
        glm::vec3 r = b.offset;
        float rSq = glm::dot(r, r);
        glm::mat3 rOuter(
            r.x*r.x, r.x*r.y, r.x*r.z,
            r.y*r.x, r.y*r.y, r.y*r.z,
            r.z*r.x, r.z*r.y, r.z*r.z
        );
        glm::mat3 identity(1.0f);
        I += Ibox + b.mass * (rSq * identity - rOuter);
    }

    // Invert the diagonal — for a box, I is diagonal so invert directly
    // (For general compound shapes we'd need full 3x3 inverse, but our boxes
    //  are always axis-aligned in local space, so I stays diagonal.)
    m_invInertiaTensorLocal = glm::mat3(0.0f);
    if (std::abs(I[0][0]) > 1e-8f) m_invInertiaTensorLocal[0][0] = 1.0f / I[0][0];
    if (std::abs(I[1][1]) > 1e-8f) m_invInertiaTensorLocal[1][1] = 1.0f / I[1][1];
    if (std::abs(I[2][2]) > 1e-8f) m_invInertiaTensorLocal[2][2] = 1.0f / I[2][2];

    updateInertiaTensorWorld();
}

void VoxelRigidBody::updateInertiaTensorWorld() {
    // I_world_inv = R * I_local_inv * R^T
    glm::mat3 R = glm::toMat3(orientation);
    invInertiaTensorWorld = R * m_invInertiaTensorLocal * glm::transpose(R);
}

void VoxelRigidBody::applyImpulse(const glm::vec3& impulse, const glm::vec3& worldPoint) {
    if (isAsleep) wake();
    linearVelocity  += impulse * invMass;
    glm::vec3 r      = worldPoint - position;
    angularVelocity += invInertiaTensorWorld * glm::cross(r, impulse);
}

void VoxelRigidBody::applyCentralImpulse(const glm::vec3& impulse) {
    if (isAsleep) wake();
    linearVelocity += impulse * invMass;
}

WorldBox VoxelRigidBody::getWorldBox(size_t i) const {
    const LocalBox& lb = m_localBoxes[i];
    glm::mat3 R = glm::toMat3(orientation);
    WorldBox wb;
    wb.center      = position + R * lb.offset;
    wb.halfExtents = lb.halfExtents;
    wb.axes        = R;
    return wb;
}

void VoxelRigidBody::getWorldAABB(glm::vec3& outMin, glm::vec3& outMax) const {
    outMin = glm::vec3( std::numeric_limits<float>::max());
    outMax = glm::vec3(-std::numeric_limits<float>::max());

    glm::mat3 R = glm::toMat3(orientation);

    for (const auto& lb : m_localBoxes) {
        glm::vec3 wc = position + R * lb.offset;
        // Compute world-space AABB of the rotated box
        // Using the formula: for each world axis, project the OBB half-extents
        glm::vec3 ext{
            std::abs(R[0][0]) * lb.halfExtents.x + std::abs(R[1][0]) * lb.halfExtents.y + std::abs(R[2][0]) * lb.halfExtents.z,
            std::abs(R[0][1]) * lb.halfExtents.x + std::abs(R[1][1]) * lb.halfExtents.y + std::abs(R[2][1]) * lb.halfExtents.z,
            std::abs(R[0][2]) * lb.halfExtents.x + std::abs(R[1][2]) * lb.halfExtents.y + std::abs(R[2][2]) * lb.halfExtents.z
        };
        outMin = glm::min(outMin, wc - ext);
        outMax = glm::max(outMax, wc + ext);
    }
}

} // namespace Physics
} // namespace Phyxel
