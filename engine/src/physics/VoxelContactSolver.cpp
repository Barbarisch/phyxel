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
    clipFaceVsOBB(wbA, wbB, normal, best.overlap, bodyA, bodyB, out);
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
            out.push_back(cp);
            if (++contactCount >= MAX_CONTACTS_PER_PAIR) break;
        }
    }
}

void VoxelContactSolver::clipFaceVsOBB(const WorldBox& wbA, const WorldBox& wbB,
                                         const glm::vec3& normal, float depth,
                                         VoxelRigidBody* bodyA, VoxelRigidBody* bodyB,
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
            out.push_back(cp);
            if (++contactCount >= MAX_CONTACTS_PER_PAIR) break;
        }
    }
}

// ---- PGS Solver ----

void VoxelContactSolver::prepareContact(ContactPoint& cp, float dt) {
    buildTangents(cp.normal, cp.tangent1, cp.tangent2);

    VoxelRigidBody* A = cp.bodyA;
    VoxelRigidBody* B = cp.bodyB;

    auto computeEffectiveMass = [&](const glm::vec3& axis,
                                    const glm::vec3& rA_,
                                    const glm::vec3& rB_) -> float {
        float em = A->invMass;
        glm::vec3 rAxN = glm::cross(rA_, axis);
        em += glm::dot(rAxN, A->invInertiaTensorWorld * rAxN);
        if (B) {
            em += B->invMass;
            glm::vec3 rBxN = glm::cross(rB_, axis);
            em += glm::dot(rBxN, B->invInertiaTensorWorld * rBxN);
        }
        return em > 1e-8f ? 1.0f / em : 0.0f;
    };

    cp.effectiveMassN  = computeEffectiveMass(cp.normal,   cp.rA, cp.rB);
    cp.effectiveMassT1 = computeEffectiveMass(cp.tangent1, cp.rA, cp.rB);
    cp.effectiveMassT2 = computeEffectiveMass(cp.tangent2, cp.rA, cp.rB);

    glm::vec3 vA = A->linearVelocity + glm::cross(A->angularVelocity, cp.rA);
    glm::vec3 vB = B ? (B->linearVelocity + glm::cross(B->angularVelocity, cp.rB))
                     : cp.obstacleVelocity;
    float relVn = glm::dot(vA - vB, cp.normal);

    float e = A->restitution;
    if (B) e = (e + B->restitution) * 0.5f;
    float bounce = (relVn < -1.0f) ? -e * relVn : 0.0f;

    float bias = BAUMGARTE / dt * std::max(0.0f, cp.depth - SLOP);

    cp.targetVelocityN = bounce + bias;
    cp.lambdaN = cp.lambdaT1 = cp.lambdaT2 = 0.0f;
}

void VoxelContactSolver::prepareContacts(std::vector<ContactPoint>& contacts, float dt) {
    for (auto& cp : contacts)
        prepareContact(cp, dt);
}

void VoxelContactSolver::solveOneContact(ContactPoint& cp) {
    VoxelRigidBody* A = cp.bodyA;
    VoxelRigidBody* B = cp.bodyB;

    glm::vec3 vA = A->linearVelocity + glm::cross(A->angularVelocity, cp.rA);
    glm::vec3 vB = B ? (B->linearVelocity + glm::cross(B->angularVelocity, cp.rB))
                     : cp.obstacleVelocity;
    float relVn = glm::dot(vA - vB, cp.normal);

    float deltaLambda = cp.effectiveMassN * (cp.targetVelocityN - relVn);
    float prevLambda  = cp.lambdaN;
    cp.lambdaN = std::max(0.0f, prevLambda + deltaLambda);
    deltaLambda = cp.lambdaN - prevLambda;

    glm::vec3 impulse = deltaLambda * cp.normal;
    A->applyImpulse(impulse, cp.worldPos);
    if (B) B->applyImpulse(-impulse, cp.worldPos);
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
        A->applyImpulse(imp, cp.worldPos);
        if (B) B->applyImpulse(-imp, cp.worldPos);
    };

    solveTangent(cp.tangent1, cp.lambdaT1, cp.effectiveMassT1);
    solveTangent(cp.tangent2, cp.lambdaT2, cp.effectiveMassT2);
}

void VoxelContactSolver::solveContacts(std::vector<ContactPoint>& contacts) {
    for (int iter = 0; iter < SOLVER_ITERATIONS; ++iter) {
        for (auto& cp : contacts) {
            solveOneContact(cp);
            solveFriction(cp);
        }
    }
}

} // namespace Physics
} // namespace Phyxel
