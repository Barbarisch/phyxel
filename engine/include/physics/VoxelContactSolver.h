#pragma once

#include "physics/VoxelRigidBody.h"
#include "physics/VoxelOccupancyGrid.h"
#include <vector>
#include <glm/glm.hpp>

namespace Phyxel {
namespace Physics {

// One contact between a dynamic body and either another dynamic body or static terrain.
struct ContactPoint {
    glm::vec3 worldPos;     // contact point in world space
    glm::vec3 normal;       // points from B toward A (push-out direction for A)
    float     depth;        // penetration depth (positive = overlapping)

    VoxelRigidBody* bodyA = nullptr;   // always non-null
    VoxelRigidBody* bodyB = nullptr;   // null for terrain / kinematic obstacle contacts

    glm::vec3 rA{0.0f};    // worldPos - bodyA->position
    glm::vec3 rB{0.0f};    // worldPos - bodyB->position (zero if terrain)

    // Velocity of the B-side surface when bodyB == nullptr (kinematic obstacle).
    // Zero for static terrain, set to character velocity for segment box contacts.
    glm::vec3 obstacleVelocity{0.0f};

    // Solver state (filled during warmup / solve)
    glm::vec3 tangent1{0.0f};
    glm::vec3 tangent2{0.0f};
    float effectiveMassN  = 0.0f;
    float effectiveMassT1 = 0.0f;
    float effectiveMassT2 = 0.0f;
    float lambdaN  = 0.0f;   // accumulated normal impulse
    float lambdaT1 = 0.0f;   // accumulated tangent impulse 1
    float lambdaT2 = 0.0f;   // accumulated tangent impulse 2
    float targetVelocityN = 0.0f; // bias + restitution target
};

// Generates contacts and solves them with sequential impulses (PGS).
class VoxelContactSolver {
public:
    static constexpr int   SOLVER_ITERATIONS = 10;
    static constexpr float BAUMGARTE         = 0.2f;   // position correction factor
    static constexpr float SLOP              = 0.005f;  // penetration allowed before correction
    static constexpr float MAX_CONTACTS_PER_PAIR = 4;

    // ---- Contact generation ----

    // Test one OBB (from a dynamic body) against one AABB (terrain).
    // Appends contacts into 'out'. Returns number of contacts added.
    static int generateOBBvsAABB(VoxelRigidBody* body, size_t boxIdx,
                                  const OccupiedBox& terrain,
                                  std::vector<ContactPoint>& out);

    // Test one OBB (bodyA, boxIdxA) against one OBB (bodyB, boxIdxB).
    static int generateOBBvsOBB(VoxelRigidBody* bodyA, size_t boxIdxA,
                                 VoxelRigidBody* bodyB, size_t boxIdxB,
                                 std::vector<ContactPoint>& out);

    // ---- Solver ----

    // Prepare cached constraint data (effective masses, tangents, bias).
    static void prepareContacts(std::vector<ContactPoint>& contacts, float dt);

    // Run PGS iterations over all contacts.
    static void solveContacts(std::vector<ContactPoint>& contacts);

private:
    // SAT helpers

    struct SATAxis {
        glm::vec3 axis;
        float     overlap;  // positive = overlapping on this axis
    };

    // Project OBB onto axis, returns half-width of projection interval.
    static float projectOBB(const WorldBox& wb, const glm::vec3& axis);

    // Project AABB onto axis, returns half-width of projection interval.
    static float projectAABB(const glm::vec3& center, const glm::vec3& he,
                              const glm::vec3& axis);

    // Compute overlap along an axis between two projected intervals.
    // positive = overlapping, negative = separated.
    static float overlapOnAxis(const WorldBox& A, const WorldBox& B, const glm::vec3& axis);
    static float overlapOnAxisVsTerrain(const WorldBox& A,
                                         const glm::vec3& terrainCenter,
                                         const glm::vec3& terrainHE,
                                         const glm::vec3& axis);

    // Contact point generation via face clipping
    static void clipFaceVsAABB(const WorldBox& obbBox, const glm::vec3& normal,
                                 float depth,
                                 const glm::vec3& terrainCenter,
                                 const glm::vec3& terrainHE,
                                 VoxelRigidBody* body,
                                 std::vector<ContactPoint>& out);

    static void clipFaceVsOBB(const WorldBox& wbA, const WorldBox& wbB,
                               const glm::vec3& normal, float depth,
                               VoxelRigidBody* bodyA, VoxelRigidBody* bodyB,
                               std::vector<ContactPoint>& out);

    // Sutherland-Hodgman clip of polygon against a half-space (plane normal + distance).
    static std::vector<glm::vec3> clipPolygonByPlane(
        const std::vector<glm::vec3>& poly,
        const glm::vec3& planeNormal, float planeOffset);

    // Get the 4 vertices of the face of an OBB most aligned with a direction.
    static std::vector<glm::vec3> getOBBFaceVerts(const WorldBox& wb, const glm::vec3& dir);

    // Apply one PGS impulse iteration to a single contact.
    static void solveOneContact(ContactPoint& cp);
    static void solveFriction(ContactPoint& cp);

    // Build two tangent vectors perpendicular to n.
    static void buildTangents(const glm::vec3& n, glm::vec3& t1, glm::vec3& t2);
};

} // namespace Physics
} // namespace Phyxel
