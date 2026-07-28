#pragma once

#include <glm/glm.hpp>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Phyxel {

class WorldGenerator;   // lives in Phyxel (not Phyxel::Core)
class ChunkManager;     // lives in Phyxel

namespace Core {

class NPCManager;

/// Runtime wildlife population. Scatters biome-appropriate wandering animals in the
/// chunk-columns around the player and despawns them as the player moves away. Placement
/// comes from the deterministic WorldGenerator::planFauna planner, so a column always yields
/// the same herds — leaving and re-entering an area respawns identical wildlife.
///
/// Main-thread only: spawning creates NPC entities + physics bodies via NPCManager. The
/// animals themselves are ordinary wander NPCs (PatrolBehavior in wander mode).
class FaunaSpawner {
public:
    /// Wire the streaming world generator (source of planFauna + surface Y), the NPC manager
    /// (spawn/despawn), and the chunk manager (ground-ready guard). All must outlive the spawner.
    void configure(WorldGenerator* gen, NPCManager* npcs, ChunkManager* chunks);
    bool isConfigured() const { return m_gen && m_npcs; }

    void setEnabled(bool e) { m_enabled = e; }
    bool isEnabled() const { return m_enabled; }
    void setCap(int cap) { m_cap = cap; }
    void setRadii(int spawnChunks, int despawnChunks) {
        m_spawnRadiusChunks = spawnChunks; m_despawnRadiusChunks = despawnChunks;
    }

    /// Populate columns near `center` (world position), despawn far ones. Throttled internally,
    /// so it is safe to call every frame.
    void update(const glm::vec3& center, float dt);

    /// Despawn every fauna NPC (e.g. on world/scene change).
    void clear();

    int activeCount() const;

private:
    void populateColumn(int cx, int cz);
    void despawnColumn(int64_t key);
    static float walkSpeedFor(const std::string& animFile);

    WorldGenerator* m_gen = nullptr;
    NPCManager* m_npcs = nullptr;
    ChunkManager* m_chunks = nullptr;
    bool m_enabled = false;
    float m_throttle = 0.0f;
    int m_cap = 60;
    int m_nextId = 0;
    int m_spawnRadiusChunks = 2;
    int m_despawnRadiusChunks = 4;
    // Chunk-column key -> the fauna NPC names spawned for it (empty vector = populated, no fauna).
    std::unordered_map<int64_t, std::vector<std::string>> m_columnNPCs;
};

} // namespace Core
} // namespace Phyxel
