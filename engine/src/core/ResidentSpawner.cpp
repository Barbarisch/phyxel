#include "core/ResidentSpawner.h"

#include "core/ChunkManager.h"
#include "core/NPCManager.h"
#include "scene/behaviors/ScheduledBehavior.h"
#include "utils/Logger.h"

#include <algorithm>
#include <functional>
#include <map>
#include <set>

namespace Phyxel {
namespace Core {

void ResidentSpawner::configure(LocationRegistry* locations, NPCManager* npcs,
                                ChunkManager* chunks) {
    m_locations = locations;
    // Real-world hooks. Tests override via setHooks.
    m_hooks.groundReady = [chunks](const glm::vec3& anchor) {
        if (!chunks) return false;
        const glm::ivec3 a(static_cast<int>(std::floor(anchor.x)),
                           static_cast<int>(std::floor(anchor.y)),
                           static_cast<int>(std::floor(anchor.z)));
        // The anchor is an outdoor ground-level marker: ready when the ground voxel under it
        // is actually present (the FaunaSpawner hasVoxelAt guard — chunk residency alone can
        // race the column fill).
        return chunks->hasVoxelAt(a + glm::ivec3(0, -1, 0)) ||
               chunks->hasVoxelAt(a + glm::ivec3(0, -2, 0));
    };
    m_hooks.exists = [npcs](const std::string& name) {
        return npcs && npcs->getNPC(name) != nullptr;
    };
    m_hooks.despawn = [npcs](const std::string& name) {
        if (npcs) npcs->removeNPC(name);
    };
    m_hooks.spawn = [npcs](const ResidentPlan& pl) {
        if (!npcs) return false;
        auto* npc = npcs->spawnProceduralNPC(
            pl.name, "resources/animated_characters/humanoid.anim",
            pl.spawnPos + glm::vec3(0.0f, 1.0f, 0.0f), NPCBehaviorType::Scheduled, pl.role);
        if (!npc) return false;
        if (auto* sb = dynamic_cast<Scene::ScheduledBehavior*>(npc->getBehavior()))
            sb->setSchedule(pl.schedule);
        return true;
    };
}

uint64_t ResidentSpawner::registryFingerprint() const {
    // Order-independent content hash: plans must rebuild when locations appear/move/vanish.
    uint64_t h = 1469598103934665603ull;
    for (const auto& [id, loc] : m_locations->getAllLocations()) {
        uint64_t e = 1099511628211ull;
        for (const char c : id) e = (e ^ static_cast<unsigned char>(c)) * 31ull;
        e ^= static_cast<uint64_t>(static_cast<int64_t>(loc.position.x * 8.0f)) * 0x9E3779B97F4A7C15ull;
        e ^= static_cast<uint64_t>(static_cast<int64_t>(loc.position.z * 8.0f)) * 0xC2B2AE3D27D4EB4Full;
        e ^= static_cast<uint64_t>(loc.type) << 40;
        h ^= e;   // XOR-fold: iteration order of the unordered_map doesn't matter
    }
    return h ^ (static_cast<uint64_t>(m_locations->size()) << 1);
}

void ResidentSpawner::rebuildPlansIfDirty() {
    const uint64_t fp = registryFingerprint();
    if (m_plansBuilt && fp == m_planFingerprint) return;
    m_plansBuilt = true;
    m_planFingerprint = fp;
    m_plans.clear();
    // Resident-bearing locations only (the ResidentPlanner contract: Home/Work/Tavern).
    std::vector<Location> locs;
    for (const auto& [id, loc] : m_locations->getAllLocations())
        if (loc.type == LocationType::Home || loc.type == LocationType::Work ||
            loc.type == LocationType::Tavern)
            locs.push_back(loc);
    // Deterministic base order (unordered_map iteration is not).
    std::sort(locs.begin(), locs.end(),
              [](const Location& a, const Location& b) { return a.id < b.id; });
    // Cluster into settlements by anchor proximity (union-find; transitive links chain a
    // whole compact settlement together while distinct settlements stay separate), then
    // plan EACH cluster independently — so every settlement keeps its own tavern as the
    // evening target instead of the whole world sharing one.
    std::vector<int> parent(locs.size());
    for (size_t i = 0; i < parent.size(); ++i) parent[i] = static_cast<int>(i);
    std::function<int(int)> find = [&](int v) {
        return parent[v] == v ? v : parent[v] = find(parent[v]);
    };
    for (size_t i = 0; i < locs.size(); ++i)
        for (size_t j = i + 1; j < locs.size(); ++j) {
            const glm::vec2 d(locs[i].position.x - locs[j].position.x,
                              locs[i].position.z - locs[j].position.z);
            if (glm::length(d) <= kClusterLinkU)
                parent[find(static_cast<int>(i))] = find(static_cast<int>(j));
        }
    std::map<int, std::vector<Location>> clusters;   // ordered map → deterministic order
    for (size_t i = 0; i < locs.size(); ++i)
        clusters[find(static_cast<int>(i))].push_back(locs[i]);
    for (auto& [root, cluster] : clusters) {
        auto plans = ResidentPlanner::planResidents(cluster);
        m_plans.insert(m_plans.end(), std::make_move_iterator(plans.begin()),
                       std::make_move_iterator(plans.end()));
    }
}

void ResidentSpawner::update(float dt) {
    if (!m_enabled || !m_locations || !m_hooks.spawn) return;
    m_throttle -= dt;
    if (m_throttle > 0.0f) return;
    m_throttle = 0.5f;
    rebuildPlansIfDirty();
    // Orphan sweep: an active resident whose location vanished (demolition) despawns.
    std::set<std::string> planned;
    for (const auto& p : m_plans) planned.insert(p.name);
    for (auto it = m_active.begin(); it != m_active.end();) {
        if (!planned.count(it->first)) {
            if (m_hooks.despawn) m_hooks.despawn(it->first);
            it = m_active.erase(it);
        } else {
            ++it;
        }
    }
    for (const auto& pl : m_plans) {
        const bool ready = m_hooks.groundReady && m_hooks.groundReady(pl.spawnPos);
        const auto it = m_active.find(pl.name);
        if (ready && it == m_active.end()) {
            if (m_hooks.exists && m_hooks.exists(pl.name)) {
                m_active.emplace(pl.name, pl.spawnPos);   // adopt the build's own spawn
            } else if (m_hooks.spawn(pl)) {
                m_active.emplace(pl.name, pl.spawnPos);
                LOG_DEBUG_FMT("Resident", "resident spawned: " << pl.name << " (" << pl.role << ")");
            }
        } else if (!ready && it != m_active.end()) {
            // Ground evicted under the resident: despawn BEFORE it free-falls (the
            // WorldForge remote-site hazard, closed by construction).
            if (m_hooks.despawn) m_hooks.despawn(pl.name);
            m_active.erase(it);
        }
    }
}

void ResidentSpawner::clear() {
    for (const auto& [name, anchor] : m_active)
        if (m_hooks.despawn) m_hooks.despawn(name);
    m_active.clear();
}

}  // namespace Core
}  // namespace Phyxel
