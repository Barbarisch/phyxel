#pragma once

// ============================================================================
// ResidentSpawner — settlement residents as a STREAMING population (the
// FaunaSpawner pattern, driven by PERSISTED Locations instead of a noise
// planner). Residents exist wherever their settlement's chunks exist:
//
//   spawn   when a Home/Work/Tavern location's anchor ground is resident
//   despawn when it evicts (no more NPCs free-falling through unloaded
//           terrain — the WorldForge remote-site hazard, fixed by construction)
//   respawn identically on return/reload (plans are deterministic functions
//           of the location set, which persists in world.db world_meta)
//
// Settlements are CLUSTERED by anchor proximity and planned per cluster, so
// every settlement keeps its own tavern as the evening social target (a global
// planResidents pass would send every resident in the world to one tavern).
//
// Main-thread only. World-touching effects go through injectable hooks so the
// whole lifecycle is headless-testable (ResidentSpawnerTest).
// ============================================================================

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "core/LocationRegistry.h"
#include "core/ResidentPlanner.h"

namespace Phyxel {

class ChunkManager;

namespace Core {

class NPCManager;

class ResidentSpawner {
public:
    /// Wire the live world. All must outlive the spawner. The default hooks (built here)
    /// spawn Scheduled NPCs via NPCManager and gate on ChunkManager ground residency.
    void configure(LocationRegistry* locations, NPCManager* npcs, ChunkManager* chunks);
    bool isConfigured() const { return m_locations != nullptr && m_hooks.spawn != nullptr; }

    void setEnabled(bool e) { m_enabled = e; }
    bool isEnabled() const { return m_enabled; }

    /// Throttled scan over every Home/Work/Tavern location: spawn residents whose ground is
    /// resident, despawn those whose ground evicted, adopt same-named NPCs spawned by the
    /// settlement build. Safe to call every frame.
    void update(float dt);

    /// Despawn every resident this spawner owns (world/scene change).
    void clear();

    int activeCount() const { return static_cast<int>(m_active.size()); }

    /// Headless seams (tests): replace the world-touching effects. configure() installs the
    /// real ones; setHooks after configure overrides them.
    struct Hooks {
        std::function<bool(const glm::vec3& anchor)> groundReady;
        std::function<bool(const ResidentPlan& plan)> spawn;
        std::function<void(const std::string& name)> despawn;
        std::function<bool(const std::string& name)> exists;
    };
    void setHooks(Hooks h) { m_hooks = std::move(h); }

    /// Settlement clustering link distance (anchors closer than this share a settlement).
    /// Settlements are compact and WorldForge spaces sites >= 256 u apart; 64 u links every
    /// in-settlement anchor without bridging distinct settlements.
    static constexpr float kClusterLinkU = 64.0f;

private:
    void rebuildPlansIfDirty();
    uint64_t registryFingerprint() const;

    LocationRegistry* m_locations = nullptr;
    bool m_enabled = false;
    bool m_plansBuilt = false;
    float m_throttle = 0.0f;
    uint64_t m_planFingerprint = 0;
    std::vector<ResidentPlan> m_plans;                       // per-cluster, deterministic order
    std::unordered_map<std::string, glm::vec3> m_active;     // npc name -> anchor (evict checks)
    Hooks m_hooks;
};

}  // namespace Core
}  // namespace Phyxel
