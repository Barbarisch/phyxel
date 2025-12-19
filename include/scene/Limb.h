#pragma once

#include "physics/PhysicsWorld.h"
#include <vector>
#include <glm/glm.hpp>

namespace VulkanCube {
namespace Scene {

class RagdollCharacter;

class Limb {
public:
    Limb(RagdollCharacter* owner, int rootBodyIndex, const glm::vec3& localAttachmentPoint);
    virtual ~Limb() = default;

    // Add a segment to the limb chain
    // bodyIndex: index in owner's parts list
    // joint: constraint connecting this segment to the previous one (or root)
    // length: length of this segment (distance to next joint or end)
    void addSegment(int bodyIndex, btTypedConstraint* joint, float length);

    // Update IK and physics motors
    void update(float deltaTime);

    // Set the world space target for the end effector
    void setTarget(const glm::vec3& targetPos);
    
    // Get current end effector position (world space)
    glm::vec3 getEndEffectorPosition() const;

    // Get the ideal "home" position for the foot relative to the root body
    // This is where the foot "wants" to be when standing still
    void setHomePosition(const glm::vec3& localPos) { localHomePos = localPos; }
    glm::vec3 getHomePositionWorld() const;

    // State
    bool isGrounded() const { return m_isGrounded; }
    void setGrounded(bool grounded) { m_isGrounded = grounded; }

    // IK Settings
    void setIKEnabled(bool enabled) { ikEnabled = enabled; }

private:
    void solveTwoBoneIK(const glm::vec3& target);
    void solveThreeBoneIK(const glm::vec3& target); // Simple heuristic for 3 bones

    RagdollCharacter* owner;
    int rootBodyIndex;
    glm::vec3 localAttachmentPoint; // On the root body

    struct Segment {
        int bodyIndex;
        btTypedConstraint* joint;
        float length;
    };
    std::vector<Segment> segments;

    glm::vec3 targetPosition;
    glm::vec3 localHomePos;
    bool m_isGrounded = true;
    bool ikEnabled = true;
};

} // namespace Scene
} // namespace VulkanCube
