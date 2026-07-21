#include "core/CoherentFragmentManager.h"
#include "core/CoherentFragmentService.h"
#include "core/KinematicVoxelManager.h"
#include "core/MaterialRegistry.h"
#include "physics/VoxelDynamicsWorld.h"
#include "physics/VoxelRigidBody.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include "utils/Logger.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace Phyxel {
namespace Core {

namespace {
glm::mat4 bodyToTransform(const Physics::VoxelRigidBody* b) {
    return glm::translate(glm::mat4(1.0f), b->position) * glm::mat4_cast(b->orientation);
}
}

uint32_t CoherentFragmentManager::spawn(
    const std::string& idHint,
    std::vector<KinematicVoxel> voxels,
    const glm::mat4& objectTransform,
    const glm::vec3& initialLinVel,
    const glm::vec3& initialAngVel,
    const std::function<float(const KinematicVoxel&)>& voxelMass)
{
    if (!ready()) return 0;

    auto pf = CoherentFragmentService::physicalize(
        m_world, m_kinematic, idHint, std::move(voxels), objectTransform,
        initialLinVel, initialAngVel, voxelMass, /*finalizeTotalMass*/ nullptr);
    if (!pf.ok()) return 0;

    // Persistent: never auto-expire (the 2026-07-14 "permanent settled object" decision).
    pf.body->lifetime = std::numeric_limits<float>::max();

    m_frags.push_back({ pf.body->id, pf.kineticObjId });
    return pf.body->id;
}

uint32_t CoherentFragmentManager::spawn(
    const std::string& idHint,
    std::vector<KinematicVoxel> renderVoxels,
    const std::vector<KinematicVoxel>& collisionVoxels,
    const glm::mat4& objectTransform,
    const glm::vec3& initialLinVel,
    const glm::vec3& initialAngVel,
    const std::function<float(const KinematicVoxel&)>& voxelMass)
{
    if (!ready()) return 0;

    auto pf = CoherentFragmentService::physicalize(
        m_world, m_kinematic, idHint, std::move(renderVoxels), collisionVoxels,
        objectTransform, initialLinVel, initialAngVel, voxelMass,
        /*finalizeTotalMass*/ nullptr);
    if (!pf.ok()) return 0;

    // Persistent: never auto-expire (the 2026-07-14 "permanent settled object" decision).
    pf.body->lifetime = std::numeric_limits<float>::max();

    m_frags.push_back({ pf.body->id, pf.kineticObjId });
    return pf.body->id;
}

void CoherentFragmentManager::update(float dt) {
    if (!m_world || !m_kinematic) return;

    // Diagnostics: fragment bodies are few and their behavior (topple/settle/jam) is
    // hard to observe externally — log state ~every 2 s while any fragment is awake.
    m_logTimer += dt;
    const bool logNow = (m_logTimer >= 2.0f);
    if (logNow) m_logTimer = 0.0f;

    // Sync live bodies; reap fragments whose body the world has removed.
    m_frags.erase(
        std::remove_if(m_frags.begin(), m_frags.end(), [&](const Frag& f) {
            Physics::VoxelRigidBody* b = m_world->getBodyById(f.bodyId);
            if (!b) {
                m_kinematic->remove(f.kinId);   // body gone -> drop its render
                return true;
            }
            m_kinematic->setTransform(f.kinId, bodyToTransform(b));
            if (logNow) {
                glm::vec3 bodyUp = b->orientation * glm::vec3(0.0f, 1.0f, 0.0f);
                float tiltDeg = glm::degrees(std::acos(glm::clamp(bodyUp.y, -1.0f, 1.0f)));
                LOG_INFO_FMT("CoherentFragment", "body " << f.bodyId
                             << " pos(" << b->position.x << "," << b->position.y << "," << b->position.z
                             << ") tilt=" << tiltDeg << "deg |w|=" << glm::length(b->angularVelocity)
                             << " |v|=" << glm::length(b->linearVelocity)
                             << (b->isAsleep ? " ASLEEP" : " awake"));
            }
            return false;
        }),
        m_frags.end());

    // U6 IMPACT FRACTURE. A hard landing (sharp velocity drop × mass = impulse) splits a
    // fragment at overloaded cross-sections. Runs AFTER the sync/reap so indices are stable;
    // tryImpactFracture may APPEND new fragments (so scan only the pre-existing prefix and use
    // indices, never references, across the call) and marks the fractured one dead (bodyId 0).
    const size_t nBefore = m_frags.size();
    for (size_t i = 0; i < nBefore; ++i) {
        Physics::VoxelRigidBody* b = m_world->getBodyById(m_frags[i].bodyId);
        if (!b) continue;
        if (!m_frags[i].primed) { m_frags[i].prevVel = b->linearVelocity; m_frags[i].primed = true; continue; }
        const glm::vec3 dv = b->linearVelocity - m_frags[i].prevVel;
        m_frags[i].prevVel = b->linearVelocity;
        if (b->isAsleep || m_frags[i].gen >= kMaxFractureGen) continue;
        const float mass    = (b->invMass > 0.0f) ? 1.0f / b->invMass : 0.0f;
        const float impulse = glm::length(dv) * mass;
        if (impulse >= kImpactImpulse) tryImpactFracture(i, b, impulse);
    }
    // Reap fragments that fracture consumed.
    m_frags.erase(std::remove_if(m_frags.begin(), m_frags.end(),
                                 [](const Frag& f) { return f.bodyId == 0; }),
                  m_frags.end());
}

void CoherentFragmentManager::tryImpactFracture(size_t index, Physics::VoxelRigidBody* b, float impulse) {
    // Copy what we need before anything mutates m_frags / removes the body.
    const std::string kinId = m_frags[index].kinId;
    const int newGen = m_frags[index].gen + 1;
    const auto& objs = m_kinematic->getObjects();
    auto oit = objs.find(kinId);
    if (oit == objs.end()) return;
    const std::vector<KinematicVoxel> voxels = oit->second.voxels;   // LOCAL frame
    if (voxels.size() < 24) return;                                  // too small to bother chunking

    // Impact point (local) ≈ the voxel that struck lowest in world; lever-arm is measured from it.
    const glm::mat4 xf = bodyToTransform(b);
    glm::vec3 impactLocal = voxels[0].localPos;
    float lowest = 1e9f;
    for (const auto& v : voxels) {
        const float wy = (xf * glm::vec4(v.localPos, 1.0f)).y;
        if (wy < lowest) { lowest = wy; impactLocal = v.localPos; }
    }

    auto shear = [](const std::string& m) -> float {
        const auto* def = Core::MaterialRegistry::instance().getMaterial(m);
        return def ? std::max(0.1f, def->physics.mass) : 1.0f;   // density as a shear-strength proxy
    };
    auto parts = CoherentFragmentService::computeImpactFracture(voxels, impactLocal, impulse, shear,
                                                                kFractureBreakK);
    if (parts.size() <= 1) return;   // not overloaded enough to break

    // Fracture: retire the whole body + render, then respawn each chunk where it was, inheriting
    // the parent's motion (the cut gap + solver push the chunks apart).
    const glm::vec3 lin = b->linearVelocity, ang = b->angularVelocity;
    m_world->removeBody(b);
    m_kinematic->remove(kinId);
    m_frags[index].bodyId = 0;   // mark dead (reaped after the loop)

    auto voxelMass = [](const KinematicVoxel& v) {
        const float vol = v.scale.x * v.scale.y * v.scale.z;
        const auto* def = Core::MaterialRegistry::instance().getMaterial(v.materialName);
        return std::max((def ? std::max(0.05f, def->physics.mass) : 1.0f) * vol, 0.001f);
    };
    int spawned = 0;
    for (auto& part : parts) {
        if (part.size() < 4) continue;   // dust — drop (would be a scatter candidate later)
        uint32_t id = spawn("frac", std::move(part), xf, lin, ang, voxelMass);
        if (id != 0) { m_frags.back().gen = newGen; ++spawned; }
    }
    LOG_INFO_FMT("CoherentFragment", "impact fracture: impulse=" << impulse
                 << " -> " << spawned << " chunks (gen " << newGen << ")");
}

void CoherentFragmentManager::clear() {
    for (const auto& f : m_frags) {
        if (m_world) {
            if (Physics::VoxelRigidBody* b = m_world->getBodyById(f.bodyId)) {
                m_world->removeBody(b);
            }
        }
        if (m_kinematic) m_kinematic->remove(f.kinId);
    }
    m_frags.clear();
}

} // namespace Core
} // namespace Phyxel
