#include "physics/VoxelContactSolver.h"
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/norm.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <array>

namespace Phyxel {
namespace Physics {

// ---- Utility ----

namespace {

// splitmix64 finalizer — cheap, well-mixed 64-bit hash for manifold pair keys.
uint64_t mix64(uint64_t x) {
    x ^= x >> 33; x *= 0xff51afd7ed558ccdull;
    x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ull;
    x ^= x >> 33;
    return x;
}

// Terrain manifold identity: body + occupied cell. Cell centers land on the 1/18 grid
// (cube 0.5, subcube 1/6, microcube 1/18), so x*18 quantizes all three levels exactly.
uint64_t terrainPairKey(uint32_t bodyId, const glm::vec3& cellCenter) {
    const uint32_t qx = static_cast<uint32_t>(std::lround(cellCenter.x * 18.0f));
    const uint32_t qy = static_cast<uint32_t>(std::lround(cellCenter.y * 18.0f));
    const uint32_t qz = static_cast<uint32_t>(std::lround(cellCenter.z * 18.0f));
    const uint64_t cell = (uint64_t(qx) * 73856093u) ^ (uint64_t(qy) * 19349663u)
                        ^ (uint64_t(qz) * 83492791u);
    return mix64((uint64_t(bodyId) << 32) ^ cell ^ 0x9E3779B97F4A7C15ull);
}

// Body-body manifold identity: canonical pair order + the box indices (a multi-box
// body pair produces one manifold per box pair, not one shared 4-point cap).
uint64_t bodyPairKey(uint32_t idA, uint32_t idB, size_t boxA, size_t boxB) {
    uint32_t lo = idA, hi = idB;
    uint64_t bl = boxA, bh = boxB;
    if (idA > idB) { lo = idB; hi = idA; bl = boxB; bh = boxA; }
    return mix64(((uint64_t(lo) << 32) | hi) ^ mix64((bl << 32) | bh));
}

} // namespace

void VoxelContactSolver::buildTangents(const glm::vec3& n, glm::vec3& t1, glm::vec3& t2) {
    if (std::abs(n.x) > 0.57735f)
        t1 = glm::normalize(glm::vec3(n.y, -n.x, 0.0f));
    else
        t1 = glm::normalize(glm::vec3(0.0f, n.z, -n.y));
    t2 = glm::cross(n, t1);
}

float VoxelContactSolver::projectOBB(const WorldBox& wb, const glm::vec3& axis) {
    // Project the three half-extents along each OBB axis, accumulate onto world axis
    return wb.halfExtents.x * std::abs(glm::dot(wb.axes[0], axis))
         + wb.halfExtents.y * std::abs(glm::dot(wb.axes[1], axis))
         + wb.halfExtents.z * std::abs(glm::dot(wb.axes[2], axis));
}

float VoxelContactSolver::projectAABB(const glm::vec3& /*center*/, const glm::vec3& he,
                                       const glm::vec3& axis) {
    return he.x * std::abs(axis.x) + he.y * std::abs(axis.y) + he.z * std::abs(axis.z);
}

float VoxelContactSolver::overlapOnAxisVsTerrain(const WorldBox& A,
                                                   const glm::vec3& terrainCenter,
                                                   const glm::vec3& terrainHE,
                                                   const glm::vec3& axis) {
    float projA = projectOBB(A, axis);
    float projB = projectAABB(terrainCenter, terrainHE, axis);
    float dist  = std::abs(glm::dot(A.center - terrainCenter, axis));
    return projA + projB - dist;
}

float VoxelContactSolver::overlapOnAxis(const WorldBox& A, const WorldBox& B,
                                         const glm::vec3& axis) {
    float projA = projectOBB(A, axis);
    float projB = projectOBB(B, axis);
    float dist  = std::abs(glm::dot(A.center - B.center, axis));
    return projA + projB - dist;
}

// ---- OBB vs AABB (terrain) ----

int VoxelContactSolver::generateOBBvsAABB(VoxelRigidBody* body, size_t boxIdx,
                                            const OccupiedBox& terrain,
                                            std::vector<ContactPoint>& out) {
    WorldBox wb = body->getWorldBox(boxIdx);
    const glm::vec3& tc = terrain.center;
    const glm::vec3& th = terrain.halfExtents;

    // 15 SAT axes: 3 OBB axes + 3 AABB axes + 9 cross products
    struct CandidateAxis { glm::vec3 axis; float overlap; };
    CandidateAxis best{glm::vec3(0,1,0), std::numeric_limits<float>::max()};

    auto test = [&](glm::vec3 ax) -> bool {
        float len = glm::length(ax);
        if (len < 1e-6f) return true; // degenerate — skip
        ax /= len;
        float ov = overlapOnAxisVsTerrain(wb, tc, th, ax);
        if (ov < 0.0f) return false; // separating
        if (ov < best.overlap) best = {ax, ov};
        return true;
    };

    // OBB face axes
    for (int i = 0; i < 3; ++i)
        if (!test(wb.axes[i])) return 0;

    // AABB face axes (world X, Y, Z)
    if (!test({1,0,0})) return 0;
    if (!test({0,1,0})) return 0;
    if (!test({0,0,1})) return 0;

    // Edge cross products
    glm::vec3 bAxes[3] = {{1,0,0},{0,1,0},{0,0,1}};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            if (!test(glm::cross(wb.axes[i], bAxes[j]))) return 0;

    if (best.overlap < 0.0f) return 0;

    // Ensure normal points from terrain toward body
    glm::vec3 normal = best.axis;
    if (glm::dot(wb.center - tc, normal) < 0.0f) normal = -normal;

    int before = static_cast<int>(out.size());
    clipFaceVsAABB(wb, normal, best.overlap, tc, th, body, out);
    return static_cast<int>(out.size()) - before;
}

// ---- OBB vs OBB ----

int VoxelContactSolver::generateOBBvsOBB(VoxelRigidBody* bodyA, size_t boxIdxA,
                                           VoxelRigidBody* bodyB, size_t boxIdxB,
                                           std::vector<ContactPoint>& out) {
    WorldBox wbA = bodyA->getWorldBox(boxIdxA);
    WorldBox wbB = bodyB->getWorldBox(boxIdxB);

    struct CandidateAxis { glm::vec3 axis; float overlap; bool fromA; };
    CandidateAxis best{{0,1,0}, std::numeric_limits<float>::max(), true};

    auto test = [&](glm::vec3 ax, bool fromA) -> bool {
        float len = glm::length(ax);
        if (len < 1e-6f) return true;
        ax /= len;
        float ov = overlapOnAxis(wbA, wbB, ax);
        if (ov < 0.0f) return false;
        if (ov < best.overlap) best = {ax, ov, fromA};
        return true;
    };

    for (int i = 0; i < 3; ++i) if (!test(wbA.axes[i], true))  return 0;
    for (int i = 0; i < 3; ++i) if (!test(wbB.axes[i], false)) return 0;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            if (!test(glm::cross(wbA.axes[i], wbB.axes[j]), true)) return 0;

    if (best.overlap < 0.0f) return 0;

    glm::vec3 normal = best.axis;
    if (glm::dot(wbA.center - wbB.center, normal) < 0.0f) normal = -normal;

    int before = static_cast<int>(out.size());
    clipFaceVsOBB(wbA, wbB, normal, best.overlap, bodyA, bodyB, boxIdxA, boxIdxB, out);
    return static_cast<int>(out.size()) - before;
}

// ---- Sutherland-Hodgman clipping ----

std::vector<glm::vec3> VoxelContactSolver::clipPolygonByPlane(
    const std::vector<glm::vec3>& poly,
    const glm::vec3& planeNormal, float planeOffset) {

    std::vector<glm::vec3> result;
    if (poly.empty()) return result;

    for (size_t i = 0; i < poly.size(); ++i) {
        const glm::vec3& a = poly[i];
        const glm::vec3& b = poly[(i + 1) % poly.size()];
        float da = glm::dot(a, planeNormal) - planeOffset;
        float db = glm::dot(b, planeNormal) - planeOffset;

        if (da >= 0.0f) result.push_back(a);
        if ((da > 0.0f) != (db > 0.0f)) {
            float t = da / (da - db);
            result.push_back(a + t * (b - a));
        }
    }
    return result;
}

std::vector<glm::vec3> VoxelContactSolver::getOBBFaceVerts(const WorldBox& wb,
                                                              const glm::vec3& dir) {
    // Find which face is most aligned with dir (largest dot product)
    float best = -std::numeric_limits<float>::max();
    int   bestFace = 0;
    int   bestSign = 1;
    for (int i = 0; i < 3; ++i) {
        float d = glm::dot(wb.axes[i], dir);
        if (d > best)  { best = d; bestFace = i; bestSign = +1; }
        if (-d > best) { best = -d; bestFace = i; bestSign = -1; }
    }

    // The face center is: center ± halfExtents[bestFace] * axes[bestFace]
    glm::vec3 faceCenter = wb.center + float(bestSign) * wb.halfExtents[bestFace] * wb.axes[bestFace];

    // Compute the two tangent axes of the face
    int u = (bestFace + 1) % 3;
    int v = (bestFace + 2) % 3;
    glm::vec3 au = wb.halfExtents[u] * wb.axes[u];
    glm::vec3 av = wb.halfExtents[v] * wb.axes[v];

    return {
        faceCenter - au - av,
        faceCenter + au - av,
        faceCenter + au + av,
        faceCenter - au + av
    };
}

// ---- Face-clipping contact generation ----

void VoxelContactSolver::clipFaceVsAABB(const WorldBox& obbBox, const glm::vec3& normal,
                                          float depth,
                                          const glm::vec3& terrainCenter,
                                          const glm::vec3& terrainHE,
                                          VoxelRigidBody* body,
                                          std::vector<ContactPoint>& out) {
    // Get the incident face (OBB face most anti-aligned with normal — deepest into terrain)
    std::vector<glm::vec3> incidentFace = getOBBFaceVerts(obbBox, -normal);

    // Clip against the 4 side planes of the terrain AABB reference face
    // The reference face of the AABB is the face most aligned with normal
    // Its side planes are the 4 edges of the AABB face perpendicular to normal
    glm::vec3 n = normal;

    // Build AABB face clip planes: for each AABB axis not parallel to normal,
    // clip to ±halfExtent on that axis
    glm::vec3 worldAxes[3] = {{1,0,0},{0,1,0},{0,0,1}};
    for (int i = 0; i < 3; ++i) {
        glm::vec3 ax = worldAxes[i];
        if (std::abs(glm::dot(ax, n)) > 0.9f) continue; // skip the normal axis
        float pos = terrainCenter[i] + terrainHE[i];
        float neg = terrainCenter[i] - terrainHE[i];
        incidentFace = clipPolygonByPlane(incidentFace,  ax, neg);   // keep p[i] >= neg
        incidentFace = clipPolygonByPlane(incidentFace, -ax, -pos);  // keep p[i] <= pos
        if (incidentFace.empty()) return;
    }

    // The terrain reference face plane
    float refOffset = glm::dot(terrainCenter + terrainHE * glm::abs(n), n);
    // Actually: find which AABB face is most aligned with normal
    // The reference plane is: dot(p, normal) = dot(terrainCenter, normal) + max_proj
    float maxProj = terrainHE.x * std::abs(n.x)
                  + terrainHE.y * std::abs(n.y)
                  + terrainHE.z * std::abs(n.z);
    // But we need the face toward the OBB: normal points obbCenter - terrainCenter
    float refPlaneD = glm::dot(terrainCenter, n) + maxProj;

    int contactCount = 0;
    for (const auto& p : incidentFace) {
        float dist = refPlaneD - glm::dot(p, n);
        if (dist >= -SLOP * 2.0f) {
            // Clamp to reference face
            glm::vec3 contactPt = p + n * std::max(0.0f, dist);

            ContactPoint cp;
            cp.worldPos = contactPt;
            cp.normal   = normal;
            cp.depth    = dist;
            cp.bodyA    = body;
            cp.bodyB    = nullptr;
            cp.rA       = contactPt - body->position;
            cp.rB       = glm::vec3(0.0f);
            cp.pairKey  = terrainPairKey(body->id, terrainCenter);
            out.push_back(cp);
            if (++contactCount >= MAX_CONTACTS_PER_PAIR) break;
        }
    }
}

void VoxelContactSolver::clipFaceVsOBB(const WorldBox& wbA, const WorldBox& wbB,
                                         const glm::vec3& normal, float depth,
                                         VoxelRigidBody* bodyA, VoxelRigidBody* bodyB,
                                         size_t boxIdxA, size_t boxIdxB,
                                         std::vector<ContactPoint>& out) {
    // Incident face: A face most anti-aligned with normal
    std::vector<glm::vec3> incidentFace = getOBBFaceVerts(wbA, -normal);

    // Reference face: B face most aligned with normal
    // Clip incident face against B's 4 side planes
    glm::vec3 refNorm  = normal;
    int refFace = 0;
    float bestD = -1e9f;
    int refSign = 1;
    for (int i = 0; i < 3; ++i) {
        float d =  glm::dot(wbB.axes[i], normal);
        if (d > bestD) { bestD = d; refFace = i; refSign = 1; }
        d = -glm::dot(wbB.axes[i], normal);
        if (d > bestD) { bestD = d; refFace = i; refSign = -1; }
    }

    glm::vec3 refCenter = wbB.center + float(refSign) * wbB.halfExtents[refFace] * wbB.axes[refFace];
    int u = (refFace + 1) % 3;
    int v = (refFace + 2) % 3;

    // Clip by 4 side planes of reference face (inward-facing normals keep inside footprint)
    auto clip4 = [&](int axis, int sign) {
        glm::vec3 sideN = -float(sign) * wbB.axes[axis];
        float d = glm::dot(refCenter + float(sign) * wbB.halfExtents[axis] * wbB.axes[axis], sideN);
        incidentFace = clipPolygonByPlane(incidentFace, sideN, d);
    };
    clip4(u, +1); clip4(u, -1);
    clip4(v, +1); clip4(v, -1);
    if (incidentFace.empty()) return;

    glm::vec3 refFaceNorm = float(refSign) * wbB.axes[refFace];
    float refD = glm::dot(refCenter, refFaceNorm);

    int contactCount = 0;
    for (const auto& p : incidentFace) {
        float dist = refD - glm::dot(p, refFaceNorm);
        if (dist >= -SLOP * 2.0f) {
            glm::vec3 contactPt = p + refFaceNorm * std::max(0.0f, dist);

            ContactPoint cp;
            cp.worldPos = contactPt;
            cp.normal   = normal;
            cp.depth    = dist;
            cp.bodyA    = bodyA;
            cp.bodyB    = bodyB;
            cp.rA       = contactPt - bodyA->position;
            cp.rB       = contactPt - bodyB->position;
            cp.pairKey  = bodyPairKey(bodyA->id, bodyB->id, boxIdxA, boxIdxB);
            out.push_back(cp);
            if (++contactCount >= MAX_CONTACTS_PER_PAIR) break;
        }
    }
}

// ---- Soft-step PGS solver (docs/PhysicsRestOverhaul.md) ----

VoxelContactSolver::Softness VoxelContactSolver::makeSoft(float hertz, float zeta, float h) {
    // Box3D b3MakeSoft: critically-damped soft constraint coefficients.
    if (hertz <= 0.0f) return {};
    constexpr float kPi = 3.14159265358979f;
    const float omega = 2.0f * kPi * hertz;
    const float a1 = 2.0f * zeta + h * omega;
    const float a2 = h * omega * a1;
    const float a3 = 1.0f / (1.0f + a2);
    Softness s;
    s.biasRate     = omega / a1;
    s.massScale    = a2 * a3;
    s.impulseScale = a3;
    return s;
}

VoxelContactSolver::SolveParams VoxelContactSolver::makeParams(float dt) {
    SolveParams sp;
    sp.invH = dt > 0.0f ? 1.0f / dt : 0.0f;
    // Contact stiffness may not exceed a quarter of the step rate (Box2D v3 rule) or the
    // soft constraint overshoots. Static/sleeping contacts are 2x stiffer than dynamic.
    const float hertz = std::min(CONTACT_HERTZ, 0.25f * sp.invH);
    sp.dyn  = makeSoft(hertz, CONTACT_DAMPING, dt);
    sp.stat = makeSoft(2.0f * hertz, CONTACT_DAMPING, dt);
    return sp;
}

void VoxelContactSolver::prepareContact(ContactPoint& cp, float /*dt*/) {
    buildTangents(cp.normal, cp.tangent1, cp.tangent2);

    VoxelRigidBody* A = cp.bodyA;
    VoxelRigidBody* B = cp.bodyB;

    // A sleeping side is treated as STATIC (no inverse mass, receives no impulses):
    // awake bodies can rest on a sleeper without waking it. Impacts hard enough to
    // matter wake the sleeper BEFORE prepare (VoxelDynamicsWorld::wakeFromImpacts).
    const bool aStatic = A->isAsleep;
    const bool bStatic = (B == nullptr) || B->isAsleep;

    auto computeEffectiveMass = [&](const glm::vec3& axis) -> float {
        float em = 0.0f;
        if (!aStatic) {
            em += A->invMass;
            glm::vec3 rAxN = glm::cross(cp.rA, axis);
            em += glm::dot(rAxN, A->invInertiaTensorWorld * rAxN);
        }
        if (B && !bStatic) {
            em += B->invMass;
            glm::vec3 rBxN = glm::cross(cp.rB, axis);
            em += glm::dot(rBxN, B->invInertiaTensorWorld * rBxN);
        }
        return em > 1e-8f ? 1.0f / em : 0.0f;
    };

    cp.effectiveMassN  = computeEffectiveMass(cp.normal);
    cp.effectiveMassT1 = computeEffectiveMass(cp.tangent1);
    cp.effectiveMassT2 = computeEffectiveMass(cp.tangent2);

    glm::vec3 vA = A->linearVelocity + glm::cross(A->angularVelocity, cp.rA);
    glm::vec3 vB = B ? (B->linearVelocity + glm::cross(B->angularVelocity, cp.rB))
                     : cp.obstacleVelocity;
    cp.relVn0 = glm::dot(vA - vB, cp.normal);   // captured for the restitution pass

    // Slop-adjusted signed separation: < 0 penetrating beyond slop (soft push-out),
    // > 0 separated (speculative — resist closing faster than separation/h).
    cp.separation = SLOP - cp.depth;

    // Accumulators start at zero; the warm-start pass restores last step's impulses.
    cp.lambdaN = cp.lambdaT1 = cp.lambdaT2 = 0.0f;
}

void VoxelContactSolver::prepareContacts(std::vector<ContactPoint>& contacts, float dt) {
    for (auto& cp : contacts)
        prepareContact(cp, dt);
}

void VoxelContactSolver::solveOneContact(ContactPoint& cp, const SolveParams& sp, bool useBias) {
    VoxelRigidBody* A = cp.bodyA;
    VoxelRigidBody* B = cp.bodyB;

    glm::vec3 vA = A->linearVelocity + glm::cross(A->angularVelocity, cp.rA);
    glm::vec3 vB = B ? (B->linearVelocity + glm::cross(B->angularVelocity, cp.rB))
                     : cp.obstacleVelocity;
    float vn = glm::dot(vA - vB, cp.normal);

    const bool vsStatic = (B == nullptr) || B->isAsleep || A->isAsleep;
    const Softness& so = vsStatic ? sp.stat : sp.dyn;

    float bias = 0.0f, massScale = 1.0f, impulseScale = 0.0f;
    if (cp.separation > 0.0f) {
        bias = cp.separation * sp.invH;   // speculative: may close at most separation this step
    } else if (useBias) {
        bias         = std::max(so.biasRate * cp.separation, -MAX_PUSH_SPEED);
        massScale    = so.massScale;
        impulseScale = so.impulseScale;
    }
    // useBias=false (relax pass): plain velocity constraint — removes the energy the
    // biased pass injected, so resting bodies end the step with ~zero velocity.

    float delta = -cp.effectiveMassN * massScale * (vn + bias) - impulseScale * cp.lambdaN;
    float newLambda = std::max(cp.lambdaN + delta, 0.0f);
    delta = newLambda - cp.lambdaN;
    cp.lambdaN = newLambda;

    glm::vec3 impulse = delta * cp.normal;
    if (!A->isAsleep) A->applyImpulse(impulse, cp.worldPos);
    if (B && !B->isAsleep) B->applyImpulse(-impulse, cp.worldPos);
}

void VoxelContactSolver::solveFriction(ContactPoint& cp) {
    VoxelRigidBody* A = cp.bodyA;
    VoxelRigidBody* B = cp.bodyB;

    float mu = A->friction;
    if (B) mu = std::sqrt(mu * B->friction);
    float limit = mu * cp.lambdaN;

    auto solveTangent = [&](const glm::vec3& t, float& accLambda, float em) {
        glm::vec3 vA = A->linearVelocity + glm::cross(A->angularVelocity, cp.rA);
        glm::vec3 vB = B ? (B->linearVelocity + glm::cross(B->angularVelocity, cp.rB))
                         : cp.obstacleVelocity;
        float relVt = glm::dot(vA - vB, t);

        float delta = -em * relVt;
        float prev  = accLambda;
        accLambda   = std::clamp(prev + delta, -limit, limit);
        delta       = accLambda - prev;

        glm::vec3 imp = delta * t;
        if (!A->isAsleep) A->applyImpulse(imp, cp.worldPos);
        if (B && !B->isAsleep) B->applyImpulse(-imp, cp.worldPos);
    };

    solveTangent(cp.tangent1, cp.lambdaT1, cp.effectiveMassT1);
    solveTangent(cp.tangent2, cp.lambdaT2, cp.effectiveMassT2);
}

void VoxelContactSolver::solvePass(std::vector<ContactPoint>& contacts, const SolveParams& sp,
                                   bool useBias, int iterations) {
    for (int iter = 0; iter < iterations; ++iter) {
        for (auto& cp : contacts) {
            solveOneContact(cp, sp, useBias);
            solveFriction(cp);
        }
    }
}

void VoxelContactSolver::applyRestitution(std::vector<ContactPoint>& contacts) {
    for (auto& cp : contacts) {
        float e = cp.bodyA->restitution;
        if (cp.bodyB) e = (e + cp.bodyB->restitution) * 0.5f;
        // Bounce only real impacts: captured approach speed past the threshold AND the
        // contact actually carries normal impulse. Resting contacts never bounce.
        if (e <= 0.0f || cp.relVn0 > -RESTITUTION_THRESHOLD || cp.lambdaN <= 0.0f) continue;

        VoxelRigidBody* A = cp.bodyA;
        VoxelRigidBody* B = cp.bodyB;
        glm::vec3 vA = A->linearVelocity + glm::cross(A->angularVelocity, cp.rA);
        glm::vec3 vB = B ? (B->linearVelocity + glm::cross(B->angularVelocity, cp.rB))
                         : cp.obstacleVelocity;
        float vn = glm::dot(vA - vB, cp.normal);

        float delta = -cp.effectiveMassN * (vn + e * cp.relVn0);
        float newLambda = std::max(cp.lambdaN + delta, 0.0f);
        delta = newLambda - cp.lambdaN;
        cp.lambdaN = newLambda;

        glm::vec3 impulse = delta * cp.normal;
        if (!A->isAsleep) A->applyImpulse(impulse, cp.worldPos);
        if (B && !B->isAsleep) B->applyImpulse(-impulse, cp.worldPos);
    }
}

} // namespace Physics
} // namespace Phyxel
