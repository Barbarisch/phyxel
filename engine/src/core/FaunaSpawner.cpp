#include "core/FaunaSpawner.h"
#include "core/WorldGenerator.h"
#include "core/NPCManager.h"
#include "core/ChunkManager.h"
#include "scene/behaviors/PatrolBehavior.h"
#include "scene/CharacterAppearance.h"
#include "utils/Logger.h"
#include <cmath>

namespace Phyxel {
namespace Core {

static constexpr int CS = 32;              // world columns per chunk (matches chunk footprint)
static constexpr float kRefreshInterval = 0.3f;   // seconds between populate/despawn sweeps
static constexpr float kWanderRadius = 6.0f;      // roam radius (small -> short, cheap A* paths)

static int64_t colKey(int cx, int cz) {
    return (static_cast<int64_t>(cx) << 32) | static_cast<uint32_t>(cz);
}

void FaunaSpawner::configure(WorldGenerator* gen, NPCManager* npcs, ChunkManager* chunks) {
    m_gen = gen;
    m_npcs = npcs;
    m_chunks = chunks;
}

// Per-species no-slide walk speed (finalize_quadruped 75th-pct measure). Horse/donkey are
// capped below their measured ~1.15-1.2 pending a visual slide check.
float FaunaSpawner::walkSpeedFor(const std::string& a) {
    auto has = [&](const char* s) { return a.find(s) != std::string::npos; };
    if (has("fox"))    return 0.226f;
    if (has("wolf"))   return 0.346f;
    if (has("husky"))  return 0.463f;
    if (has("ibex"))   return 0.790f;
    if (has("alpaca")) return 0.508f;
    if (has("cow"))    return 0.614f;
    if (has("deer"))   return 0.617f;
    if (has("bull"))   return 0.640f;
    if (has("stag"))   return 0.642f;
    if (has("donkey")) return 0.9f;
    if (has("horse"))  return 0.9f;
    return 0.6f;
}

void FaunaSpawner::update(const glm::vec3& center, float dt) {
    if (!m_enabled || !isConfigured()) return;
    m_throttle -= dt;
    if (m_throttle > 0.0f) return;
    m_throttle = kRefreshInterval;

    auto floordiv = [](int a, int b) { return (a >= 0) ? a / b : -((-a + b - 1) / b); };
    const int pcx = floordiv(static_cast<int>(std::floor(center.x)), CS);
    const int pcz = floordiv(static_cast<int>(std::floor(center.z)), CS);

    // Despawn columns that drifted beyond the despawn radius.
    std::vector<int64_t> stale;
    for (const auto& [key, names] : m_columnNPCs) {
        const int cx = static_cast<int>(key >> 32);
        const int cz = static_cast<int>(static_cast<uint32_t>(key));
        if (std::abs(cx - pcx) > m_despawnRadiusChunks ||
            std::abs(cz - pcz) > m_despawnRadiusChunks)
            stale.push_back(key);
    }
    for (int64_t k : stale) despawnColumn(k);

    // Populate at most ONE new column per tick, nearest-first. Each spawn loads an animal
    // .anim (heavy the first time each species appears); doing a whole 5x5 neighborhood in one
    // frame stalls the game loop. Spreading over ticks keeps frames smooth as animals fill in.
    if (activeCount() >= m_cap) return;
    bool found = false;
    int bestCx = 0, bestCz = 0;
    long bestD = -1;
    for (int cz = pcz - m_spawnRadiusChunks; cz <= pcz + m_spawnRadiusChunks; ++cz)
        for (int cx = pcx - m_spawnRadiusChunks; cx <= pcx + m_spawnRadiusChunks; ++cx) {
            if (m_columnNPCs.count(colKey(cx, cz))) continue;
            const long d = static_cast<long>(cx - pcx) * (cx - pcx)
                         + static_cast<long>(cz - pcz) * (cz - pcz);
            if (!found || d < bestD) { found = true; bestD = d; bestCx = cx; bestCz = cz; }
        }
    if (found) populateColumn(bestCx, bestCz);
}

void FaunaSpawner::populateColumn(int cx, int cz) {
    const int x0 = cx * CS, z0 = cz * CS, x1 = x0 + CS - 1, z1 = z0 + CS - 1;
    auto placements = m_gen->planFauna(x0, z0, x1, z1, 0);

    // A column with no fauna is "done" immediately (nothing to ground). But a column that HAS
    // fauna must wait until its chunk voxels exist, or the animals spawn into thin air and fall
    // to sea level (the chunk-physics-not-yet-built race in a streaming world). Defer by NOT
    // marking it populated, so the next tick retries once the chunk has streamed in.
    if (!placements.empty() && m_chunks) {
        const auto& g = placements.front();
        if (!m_chunks->hasVoxelAt(glm::ivec3(g.worldX, g.surfaceY, g.worldZ)))
            return;   // chunk not ground-ready yet — retry next tick
    }

    auto& names = m_columnNPCs[colKey(cx, cz)];   // insert marks the column populated
    for (const auto& pl : placements) {
        if (activeCount() >= m_cap) break;
        const std::string name = "fauna_" + std::to_string(m_nextId++);
        const glm::vec3 pos(pl.worldX + 0.5f, pl.surfaceY + 1.0f, pl.worldZ + 0.5f);
        auto beh = std::make_unique<Scene::PatrolBehavior>(
            std::vector<glm::vec3>{}, walkSpeedFor(pl.animFile), 3.0f);
        beh->setWanderMode(pos, kWanderRadius, 2.0f, 6.0f);
        auto* npc = m_npcs->spawnNPCWithBehavior(name, pl.animFile, pos, std::move(beh),
                                                 Scene::CharacterAppearance{});
        if (npc) names.push_back(name);
    }
    if (!names.empty())
        LOG_DEBUG("FaunaSpawner", "chunk ({},{}): spawned {} animals (total {})",
                  cx, cz, names.size(), activeCount());
}

void FaunaSpawner::despawnColumn(int64_t key) {
    auto it = m_columnNPCs.find(key);
    if (it == m_columnNPCs.end()) return;
    for (const auto& name : it->second) m_npcs->removeNPC(name);
    m_columnNPCs.erase(it);
}

void FaunaSpawner::clear() {
    if (m_npcs)
        for (const auto& [key, names] : m_columnNPCs)
            for (const auto& name : names) m_npcs->removeNPC(name);
    m_columnNPCs.clear();
}

int FaunaSpawner::activeCount() const {
    int n = 0;
    for (const auto& [key, names] : m_columnNPCs) n += static_cast<int>(names.size());
    return n;
}

} // namespace Core
} // namespace Phyxel
