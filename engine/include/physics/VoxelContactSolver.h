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

    // Manifold identity for cross-step impulse warm starting (docs/PhysicsRestOverhaul.md):
    // hashes the body pair (+ box indices) or body + terrain cell. Set at generation.
    uint64_t pairKey = 0;

    // Solver state (filled during warmup / solve)
    glm::vec3 tangent1{0.0f};
    glm::vec3 tangent2{0.0f};
    float effectiveMassN  = 0.0f;
    float effectiveMassT1 = 0.0f;
    float effectiveMassT2 = 0.0f;
    float lambdaN  = 0.0f;   // accumulated normal impulse
    float lambdaT1 = 0.0f;   // accumulated tangent impulse 1
    float lambdaT2 = 0.0f;   // accumulated tangent impulse 2
    float separation = 0.0f; // signed slop-adjusted separation: <0 penetrating, >0 speculative
    float relVn0     = 0.0f; // approach speed captured at prepare (restitution pass input)
};

// Generates contacts and solves them with sequential impulses (PGS), using Box3D-style
// "soft step" contact softness: penetration is recovered by a critically-damped soft
// bias (massScale/impulseScale blend) inside the biased pass, then a bias-free relax
// pass removes the injected correction energy so resting bodies carry ~zero residual
// velocity. Restitution is a separate final pass. See docs/PhysicsRestOverhaul.md.
class VoxelContactSolver {
public:
    static constexpr int   SOLVER_ITERATIONS = 10;   // biased velocity iterations
    static constexpr int   RELAX_ITERATIONS  = 4;    // bias-free iterations (jitter removal)
    static constexpr float SLOP              = 0.005f;  // penetration allowed before correction
    static constexpr float MAX_CONTACTS_PER_PAIR = 4;
    static constexpr float CONTACT_HERTZ     = 30.0f;   // clamped to 0.25/h in makeParams
    static constexpr float CONTACT_DAMPING   = 10.0f;   // damping ratio zeta
    static constexpr float MAX_PUSH_SPEED    = 3.0f;    // push-out speed cap (m/s)
    static constexpr float RESTITUTION_THRESHOLD = 1.0f; // approach speed that may bounce (m/s)

    // Soft-constraint coefficients (Box3D b3MakeSoft): critically-damped penetration
    // recovery expressed as a velocity-solver bias + impulse blend.
    struct Softness {
        float biasRate     = 0.0f;
        float massScale    = 1.0f;
        float impulseScale = 0.0f;
    };
    struct SolveParams {
        float    invH = 60.0f;
        Softness dyn;       // dynamic vs dynamic
        Softness stat;      // vs terrain / kinematic / sleeping body (2x hertz, stiffer)
    };
    static Softness    makeSoft(float hertz, float zeta, float h);
    static SolveParams makeParams(float dt);

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

    // Prepare cached constraint data (effective masses, tangents, separation).
    // A sleeping body on either side is treated as static (infinite mass) so awake
    // bodies can rest against sleepers without waking them.
    static void prepareContacts(std::vector<ContactPoint>& contacts, float dt);

    // Prepare a single contact — called per-contact from the parallel prepare path.
    static void prepareContact(ContactPoint& cp, float dt);

    // One solver pass over all contacts (normal + friction), `iterations` times.
    // useBias=true: biased pass (soft penetration recovery + speculative), run BEFORE
    // position integration. useBias=false: relax pass (removes bias energy), run AFTER.
    static void solvePass(std::vector<ContactPoint>& contacts, const SolveParams& sp,
                          bool useBias, int iterations);

    // Separate restitution pass (after relax): bounce only contacts whose captured
    // approach speed exceeded RESTITUTION_THRESHOLD and that carry normal impulse.
    static void applyRestitution(std::vector<ContactPoint>& contacts);

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
                               size_t boxIdxA, size_t boxIdxB,
                               std::vector<ContactPoint>& out);

    // Sutherland-Hodgman clip of polygon against a half-space (plane normal + distance).
    static std::vector<glm::vec3> clipPolygonByPlane(
        const std::vector<glm::vec3>& poly,
        const glm::vec3& planeNormal, float planeOffset);

    // Get the 4 vertices of the face of an OBB most aligned with a direction.
    static std::vector<glm::vec3> getOBBFaceVerts(const WorldBox& wb, const glm::vec3& dir);

    // Apply one PGS impulse iteration to a single contact.
    static void solveOneContact(ContactPoint& cp, const SolveParams& sp, bool useBias);
    static void solveFriction(ContactPoint& cp);

    // Build two tangent vectors perpendicular to n.
    static void buildTangents(const glm::vec3& n, glm::vec3& t1, glm::vec3& t2);
};

} // namespace Physics
} // namespace Phyxel
