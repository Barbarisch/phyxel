#include "core/DebrisRuntime.h"
#include "core/ChunkManager.h"
#include "core/DebrisMoverFeed.h"
#include "core/KinematicVoxelManager.h"
#include "core/SpellDefinition.h"
#include "graphics/RenderCoordinator.h"
#include "physics/PhysicsWorld.h"
#include "physics/VoxelDynamicsWorld.h"
#include "scene/AnimatedVoxelCharacter.h"
#include "utils/Logger.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <unordered_set>
#include <utility>

namespace Phyxel {

DebrisRuntime::~DebrisRuntime() = default;

bool DebrisRuntime::handleArg(const std::string& arg) {
    if (arg != "--disable-gpu-debris") return false;
#ifdef _WIN32
    _putenv_s("PHYXEL_DISABLE_GPU_DEBRIS", "1");
#else
    setenv("PHYXEL_DISABLE_GPU_DEBRIS", "1", 1);
#endif
    return true;
}

bool DebrisRuntime::initialize(Vulkan::VulkanDevice* device, Graphics::RenderCoordinator* renderer,
                               ChunkManager* chunks, Physics::PhysicsWorld* physics, const Config& cfg) {
    m_physics  = physics;
    m_renderer = renderer;
    m_chunks   = chunks;
    m_gpu.reset();
    m_disabledReason.clear();
    // All debris lives in the GPU solver. Without it breaks, blasts and derez make NO debris, so a
    // missing solver must be LOUD: one ERROR, a reason the host echoes (/api/debug/gpu_physics),
    // and every refused piece counted (DamageSystem::refusedDebrisTotal).
    const char* off = std::getenv("PHYXEL_DISABLE_GPU_DEBRIS");
    if (off && *off && std::string(off) != "0") {
        m_disabledReason = "disabled by PHYXEL_DISABLE_GPU_DEBRIS";
    } else if (!cfg.enabled) {
        m_disabledReason = "disabled by configuration (debris.enabled = false)";
    } else {
        m_gpu = std::make_unique<GpuParticlePhysics>();
    }
    if (m_gpu && device && m_gpu->initialize(device, "")) {
        // RenderCoordinator: compute dispatch, the debris draw and the shared static occupancy.
        if (renderer) renderer->setGpuParticlePhysics(m_gpu.get());
        // ChunkManager: the debris light sampler.
        if (chunks) chunks->setGpuParticlePhysics(m_gpu.get());
        LOG_INFO("DebrisRuntime", "GpuParticlePhysics initialized successfully!");
        return true;
    }
    if (m_disabledReason.empty())
        m_disabledReason = device ? "GpuParticlePhysics::initialize failed" : "no Vulkan device";
    m_gpu.reset();
    LOG_ERROR("DebrisRuntime", "GPU debris DISABLED (" + m_disabledReason +
              "): breaks, blasts and derez will produce NO debris");
    return false;
}

void DebrisRuntime::shutdown() {
    if (!m_gpu) return;
    if (m_renderer) m_renderer->setGpuParticlePhysics(nullptr);
    if (m_chunks)   m_chunks->setGpuParticlePhysics(nullptr);
    m_gpu.reset();
    if (m_disabledReason.empty()) m_disabledReason = "shut down";
}

DamageResult DebrisRuntime::applyDamage(const glm::vec3& center, float radius, float energy,
                                        const glm::vec3& direction) {
    if (!m_chunks) return {};
    DamageSystem dmg(m_chunks, m_gpu.get());
    return dmg.applyDamage(center, radius, energy, "force", direction);
}

DebrisRuntime::SpellBlast DebrisRuntime::spellBlast(const Core::SpellDefinition& spell) {
    SpellBlast b;
    if (spell.baseDamage.count <= 0) return b;   // heals / buffs / utility: no blast
    const float avgDie = (static_cast<float>(static_cast<int>(spell.baseDamage.die)) + 1.0f) * 0.5f;
    const float avg = spell.baseDamage.count * avgDie + static_cast<float>(spell.baseDamage.modifier);
    if (avg <= 0.0f) return b;
    b.blast  = true;
    b.radius = spell.isAreaSpell() ? spell.areaSizeFeet * 0.3048f : SPELL_SINGLE_TARGET_RADIUS;
    b.energy = SPELL_ENERGY_PER_DAMAGE * avg;
    return b;
}

void DebrisRuntime::beginFrame(float dt) {
    if (!m_gpu) return;
    m_gpu->update(dt);
    pumpEvents();
}

bool DebrisRuntime::settledValid(const SettledPiece& p) const {
    return m_gpu && m_gpu->slotActive(p.slot) && m_gpu->slotSerial(p.slot) == p.serial;
}

void DebrisRuntime::pumpEvents() {
    auto events = m_gpu->takeEvents();
    if (events.empty()) return;
    std::vector<const GpuParticlePhysics::DebrisEvent*> impacts, settles;
    for (const auto& e : events) {
        switch (e.type) {
        case DebrisShared::DEBRIS_EVENT_SLEEP:
            ++m_stats.sleep;
            // The serial is the slot's CURRENT one; a slot recycled between the event and now
            // would hold a fresh body that cannot be asleep yet (SLEEP_GRACE), so a stale entry
            // is caught by the WAKE/despawn path or by settledValid() at gather time.
            m_settled[e.slot] = SettledPiece{e.slot, e.serial, e.materialIndex, e.position, e.scale};
            settles.push_back(&e);
            break;
        case DebrisShared::DEBRIS_EVENT_WAKE:
            ++m_stats.wake;
            m_settled.erase(e.slot);
            break;
        case DebrisShared::DEBRIS_EVENT_IMPACT:
            ++m_stats.impact;
            impacts.push_back(&e);
            break;
        default: break;
        }
        m_recent.push_back(e);
        if (m_recent.size() > 512) m_recent.pop_front();
    }
    if (!m_sound) return;
    // Loudest impacts first; volume from the hit speed (1.5 m/s threshold .. ~8 m/s full) and the
    // piece size (a 1/9 microcube ticks, a full cube thuds).
    std::sort(impacts.begin(), impacts.end(), [](auto* a, auto* b) { return a->speed > b->speed; });
    for (int k = 0; k < static_cast<int>(impacts.size()) && k < MAX_IMPACT_SOUNDS_PER_FRAME; ++k) {
        const auto* e = impacts[k];
        const float speedVol = std::clamp((e->speed - DebrisShared::IMPACT_EVENT_DV) / 6.5f, 0.15f, 1.0f);
        const float sizeVol  = std::clamp(std::sqrt(std::max(e->scale, 0.0f)), 0.25f, 1.0f);
        m_sound(SOUND_IMPACT, e->position, speedVol * sizeVol);
        ++m_stats.soundsImpact;
    }
    for (int k = 0; k < static_cast<int>(settles.size()) && k < MAX_SETTLE_SOUNDS_PER_FRAME; ++k) {
        const auto* e = settles[k];
        m_sound(SOUND_SETTLE, e->position, std::clamp(std::sqrt(std::max(e->scale, 0.0f)), 0.2f, 0.6f));
        ++m_stats.soundsSettle;
    }
}

size_t DebrisRuntime::settledCount() const {
    size_t n = 0;
    for (const auto& [slot, p] : m_settled) n += settledValid(p) ? 1u : 0u;
    return n;
}

std::vector<DebrisRuntime::SettledPiece> DebrisRuntime::settledNear(const glm::vec3& center, float radius) const {
    std::vector<SettledPiece> out;
    for (const auto& [slot, p] : m_settled)
        if (settledValid(p) && glm::length(p.position - center) <= radius) out.push_back(p);
    std::sort(out.begin(), out.end(), [&](const SettledPiece& a, const SettledPiece& b) {
        return glm::length(a.position - center) < glm::length(b.position - center);
    });
    return out;
}

DebrisRuntime::GatherResult DebrisRuntime::gather(const glm::vec3& center, float radius, int maxPieces) {
    GatherResult r;
    if (!m_gpu) return r;
    // Drop stale entries first (despawned / recycled slots).
    for (auto it = m_settled.begin(); it != m_settled.end();)
        it = settledValid(it->second) ? std::next(it) : m_settled.erase(it);
    const auto near = settledNear(center, std::clamp(radius, 0.0f, 16.0f));
    for (const auto& p : near) {
        if (r.pieces >= std::max(maxPieces, 0)) break;
        if (!m_gpu->despawnSlot(p.slot)) continue;
        m_settled.erase(p.slot);
        ++r.pieces;
        const float volume = p.scale * p.scale * p.scale;   // in full-cube units
        m_gatherRemainder[GpuParticlePhysics::materialNameOf(p.materialIndex)] += volume;
    }
    for (auto& [mat, owed] : m_gatherRemainder) {
        const int whole = takeWholeUnits(owed);
        if (whole > 0) r.items[mat] += whole;
        if (owed > 1e-4f) r.carried[mat] = owed;
    }
    return r;
}

int DebrisRuntime::takeWholeUnits(float& owed) {
    // 1e-4: a cube shattered into 27 subcubes sums to 0.99999 in float; it is one cube.
    const int whole = static_cast<int>(std::floor(owed + 1e-4f));
    if (whole <= 0) return 0;
    owed = std::max(owed - static_cast<float>(whole), 0.0f);
    return whole;
}

void DebrisRuntime::feedCharacters(const std::vector<Scene::AnimatedVoxelCharacter*>& characters,
                                   const glm::vec3& eye) {
    if (!m_gpu || !m_gpu->isInitialized()) return;
    // Phase 3a: EVERY animated character - the player, spawned entities, NPCs, monsters, fauna -
    // as oriented limb boxes with per-limb velocity, fed after all of them updated this frame. A
    // derezzing character feeds nothing; an update-LOD-deferred one is extrapolated
    // (collectMoverBoxes). Nearest the camera first, so the MAX_KINEMATIC overflow (counted) drops
    // the farthest.
    std::vector<std::pair<float, Scene::AnimatedVoxelCharacter*>> movers;
    std::unordered_set<Scene::AnimatedVoxelCharacter*> seen;
    movers.reserve(characters.size());
    for (auto* c : characters) {
        if (!c || !seen.insert(c).second) continue;
        const glm::vec3 d = c->getPosition() - eye;
        movers.push_back({glm::dot(d, d), c});
    }
    std::sort(movers.begin(), movers.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
    std::vector<Scene::AnimatedVoxelCharacter::MoverBox> limbs;
    for (auto& [d2, c] : movers) c->collectMoverBoxes(limbs);
    std::vector<GpuParticlePhysics::MoverBox> boxes;
    boxes.reserve(limbs.size());
    for (const auto& l : limbs) boxes.push_back({l.center, l.halfExtents, l.rotation, l.velocity});
    m_gpu->setMoverBoxes(std::move(boxes));
}

void DebrisRuntime::feedKinematicObjects(Core::KinematicVoxelManager* kvm, float dt, const glm::vec3& eye) {
    if (!kvm) return;
    // Phase 3b: doors, animated template parts and held items - every KinematicVoxelObject flagged
    // pushesDebris - as oriented sub-boxes with per-box velocity from this frame's transform delta.
    // The same boxes become CPU kinematic obstacles (doors block furniture, even with debris off).
    kvm->syncCollidersToPhysics(dt);
    if (!m_gpu || !m_gpu->isInitialized()) return;
    // Nearest the camera first, trimmed to the slots the limbs left (a settlement has ~100 doors x
    // 8 boxes): the far ones are dropped, counted and logged once.
    const auto& km = kvm->lastMoverBoxes();
    std::vector<std::pair<float, const Core::KinematicVoxelManager::MoverBox*>> order;
    order.reserve(km.size());
    for (const auto& m : km) order.push_back({glm::dot(m.center - eye, m.center - eye), &m});
    std::sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    const uint32_t budget = m_gpu->objectMoverBudget();
    std::vector<GpuParticlePhysics::MoverBox> boxes;
    boxes.reserve(std::min<size_t>(order.size(), budget));
    for (const auto& [d2, m] : order) {
        if (boxes.size() >= budget) break;
        boxes.push_back({m->center, m->halfExtents, m->rotation, m->velocity});
    }
    if (order.size() > boxes.size() && !m_loggedObjectSkip) {
        m_loggedObjectSkip = true;
        LOG_WARN("GpuParticlePhysics", "debris movers: {} door/part/held-item boxes past the {}-box budget "
                 "were not fed (the farthest from the camera); logged once",
                 order.size() - boxes.size(), DebrisShared::MAX_KINEMATIC);
    }
    m_gpu->setObjectMoverBoxes(std::move(boxes));
}

void DebrisRuntime::feedRigidBodies(const glm::vec3& eye) {
    if (!m_gpu || !m_gpu->isInitialized()) return;
    // Phase 3c: every CPU rigid body (furniture incl. grabbed/thrown, fragments, felled trees, item
    // props) at its post-step pose, compound boxes with point velocities; sleepers too, as
    // supports. One-way: debris does not push them back (Phase 4 impulses do). Nearest the camera
    // first, whole bodies only, in the slots the limbs and objects left; a body that does not fit
    // is counted and logged once.
    auto* vw = m_physics ? m_physics->getVoxelWorld() : nullptr;
    if (vw && m_gpu->getActiveParticleCount() > 0) {
        std::vector<GpuParticlePhysics::MoverBox> boxes;
        const auto fed = DebrisMoverFeed::appendRigidBodies(*vw, eye, m_gpu->bodyMoverBudget(), boxes);
        if (fed.bodiesSkipped > 0 && !m_loggedBodySkip) {
            m_loggedBodySkip = true;
            LOG_WARN("GpuParticlePhysics", "debris movers: {} CPU bodies past the {}-box budget were not fed "
                     "(debris passes through them); logged once", fed.bodiesSkipped, DebrisShared::MAX_KINEMATIC);
        }
        m_gpu->setBodyMoverBoxes(std::move(boxes));
    } else if (m_gpu->bodyMoverCount() > 0) {
        m_gpu->setBodyMoverBoxes({});   // no debris to push: free the slots
    }
}

}  // namespace Phyxel
