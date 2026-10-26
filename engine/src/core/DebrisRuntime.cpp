#include "core/DebrisRuntime.h"
#include "core/ChunkManager.h"
#include "core/DebrisMoverFeed.h"
#include "core/KinematicVoxelManager.h"
#include "core/SpellDefinition.h"
#include <chrono>
#include "core/WaterManager.h"
#include <glm/gtc/packing.hpp>
#include "graphics/RenderCoordinator.h"
#include "physics/PhysicsWorld.h"
#include "physics/VoxelDynamicsWorld.h"
#include "scene/AnimatedVoxelCharacter.h"
#include "utils/Logger.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
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
    using BClock = std::chrono::steady_clock;
    const auto t0 = BClock::now();
    m_gpu->update(dt);
    const auto t1 = BClock::now();
    pumpEvents();
    const auto t2 = BClock::now();
    updateWater();
    m_waterStats.gpuUpdateMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    m_waterStats.waterMs = std::chrono::duration<double, std::milli>(BClock::now() - t2).count();
    // Phase 6a: the push-back read back this frame waits for the next feedCharacters (which has
    // the live character list to apply it to).
    for (const auto& [owner, j] : m_gpu->takeMoverImpulses()) {
        m_pendingPush[owner] += j;
        ++m_pushStats.readOwners;
        m_pushStats.readImpulseTotal += glm::length(j);
    }
}

DebrisRuntime::WaterColumnFn DebrisRuntime::composeColumn(WaterColumnFn volume, WaterColumnFn fallback) {
    return [volume = std::move(volume), fallback = std::move(fallback)](int wx, int wz, float& s, glm::vec2& f) {
        if (volume && volume(wx, wz, s, f)) return true;
        return fallback ? fallback(wx, wz, s, f) : false;
    };
}

DebrisRuntime::WaterRunsFn DebrisRuntime::runsOf(WaterColumnFn column) {
    if (!column) return {};
    return [column = std::move(column)](int wx, int wz, WaterRuns& r) {
        r = WaterRuns{};
        return column(wx, wz, r.top1, r.flow);   // one run, no bottom: the old law exactly
    };
}

DebrisRuntime::WaterRunsFn DebrisRuntime::composeRuns(WaterRunsFn volume, WaterRunsFn fallback) {
    return [volume = std::move(volume), fallback = std::move(fallback)](int wx, int wz, WaterRuns& r) {
        if (volume && volume(wx, wz, r)) return true;
        return fallback ? fallback(wx, wz, r) : false;
    };
}

bool DebrisRuntime::buildWaterTile(int chunkX, int chunkZ, const WaterRunsFn& runs, bool implicitSea,
                                   float seaLevel, std::vector<uint32_t>& out) {
    using namespace DebrisShared;
    const int n = WATER_TILE_CELLS;
    const size_t W = static_cast<size_t>(WATER_CELL_WORDS);
    out.assign(static_cast<size_t>(n) * n * W, 0u);
    const float bg = implicitSea ? seaLevel : WATER_DRY;
    bool any = false;
    auto put = [&out](size_t at, float v) { std::memcpy(&out[at], &v, sizeof(float)); };
    for (int lz = 0; lz < n; ++lz)
        for (int lx = 0; lx < n; ++lx) {
            const int wx = chunkX * n + lx, wz = chunkZ * n + lz;
            WaterRuns r;
            if (!runs || !runs(wx, wz, r)) r = WaterRuns{};
            if (r.top1 <= WATER_DRY) r = WaterRuns{};   // dry: no runs, no flow
            // Note: a dry column inside a sea world stays DRY (the sim says so), which differs from
            // the background - so it needs a tile. A column with a bottom, or a second run, is not the background either.
            if (r.top1 != bg || r.flow.x != 0.0f || r.flow.y != 0.0f || r.bottom1 != WATER_NO_BOTTOM || r.top2 > WATER_DRY) any = true;
            const size_t c = static_cast<size_t>(phxWaterCellIndex(wx, wz)) * W;
            put(c, r.top1); put(c + 1, r.bottom1);
            out[c + 2] = glm::packHalf2x16(r.flow);
            put(c + 3, r.top2); put(c + 4, r.bottom2);
            out[c + 5] = r.more;
        }
    if (!any) out.clear();
    return any;
}

float DebrisRuntime::sampleWaterTiles(int x, int z, const glm::ivec2& dirMinChunk, const std::vector<uint32_t>& dir,
                                      const std::vector<uint32_t>& cells, bool implicitSea, float seaLevel,
                                      glm::vec2* flow, float bodyTop) {
    using namespace DebrisShared;
    if (flow) *flow = glm::vec2(0.0f);
    const float bg = implicitSea ? seaLevel : WATER_DRY;
    const int d = phxWaterDirIndex(x, z, dirMinChunk.x, dirMinChunk.y);
    if (d < 0 || static_cast<size_t>(d) >= dir.size()) return bg;
    const uint32_t t = dir[d];
    if (t == WATER_TILE_NONE) return bg;
    const size_t W = static_cast<size_t>(WATER_CELL_WORDS);
    const size_t c = (static_cast<size_t>(t) * WATER_TILE_CELLS * WATER_TILE_CELLS +
                      static_cast<size_t>(phxWaterCellIndex(x, z))) * W;
    if (c + W > cells.size()) return bg;
    auto get = [&cells](size_t at) { float v; std::memcpy(&v, &cells[at], sizeof(float)); return v; };
    const float top1 = get(c);
    const float surf = phxWaterRunSurface(top1, get(c + 1), get(c + 3), get(c + 4), bodyTop);
    if (flow && surf == top1 && surf > WATER_DRY) *flow = glm::unpackHalf2x16(cells[c + 2]);   // the top run's flow only (as the shader)
    return surf;
}

void DebrisRuntime::updateWater() {
    using namespace DebrisShared;
    ++m_waterFrame;
    glm::ivec3 box;
    const bool ready = m_gpu && (m_water || m_volumeColumn) && m_gpu->occupancyBoxMinChunk(box);   // E2: a volume alone is a water source
    m_waterStats.ready = ready;
    if (!ready) {
        if (m_waterUploaded) { m_gpu->setWater(glm::ivec2(0), false, 0.0f, {}, {}); m_waterUploaded = false; }
        return;
    }
    const glm::ivec2 minChunk(box.x, box.z);
    if (minChunk != m_waterMinChunk) {
        // The window moved: forget tiles it no longer covers (re-built if it comes back).
        for (auto it = m_waterCache.begin(); it != m_waterCache.end();) {
            const int cx = static_cast<int32_t>(it->first >> 32), cz = static_cast<int32_t>(it->first & 0xffffffffu);
            const bool inside = cx >= minChunk.x && cz >= minChunk.y &&
                                cx < minChunk.x + WATER_DIR_CHUNKS && cz < minChunk.y + WATER_DIR_CHUNKS;
            it = inside ? std::next(it) : m_waterCache.erase(it);
        }
        m_waterMinChunk = minChunk;
    }
    if (m_waterOrder.empty()) {
        for (int k = 0; k < WATER_DIR_CHUNKS * WATER_DIR_CHUNKS; ++k) m_waterOrder.push_back(k);
        const float c = 0.5f * (WATER_DIR_CHUNKS - 1);
        std::sort(m_waterOrder.begin(), m_waterOrder.end(), [c](int a, int b) {
            const float ax = a % WATER_DIR_CHUNKS - c, az = a / WATER_DIR_CHUNKS - c;
            const float bx = b % WATER_DIR_CHUNKS - c, bz = b / WATER_DIR_CHUNKS - c;
            return ax * ax + az * az < bx * bx + bz * bz;
        });
    }
    const bool  sea   = m_water ? m_water->implicitSea() : false;
    const float seaY  = m_water ? m_water->seaLevel() : 0.0f;
    const glm::ivec3 ro = m_water ? m_water->regionOrigin() : glm::ivec3(0), rd = m_water ? m_water->regionDims() : glm::ivec3(0);
    int volumeTiles = 0, volumeKept = 0;
    auto underVolume = [&](int cx, int cz) {   // E2: a chunk column an active volume's box touches
        if (!m_volumeColumn || !m_volumeBoxes) return false;
        const int x0 = cx * WATER_TILE_CELLS, z0 = cz * WATER_TILE_CELLS;
        for (const auto& b : *m_volumeBoxes)
            if (b.first.x < x0 + WATER_TILE_CELLS && b.second.x >= x0 && b.first.z < z0 + WATER_TILE_CELLS && b.second.z >= z0) return true;
        return false;
    };
    auto inRegion = [&](int cx, int cz) {   // the old water's live region
        const int x0 = cx * WATER_TILE_CELLS, z0 = cz * WATER_TILE_CELLS;
        return x0 < ro.x + rd.x && x0 + WATER_TILE_CELLS > ro.x && z0 < ro.z + rd.z && z0 + WATER_TILE_CELLS > ro.z;
    };
    auto inSim = [&](int cx, int cz) {
        if (underVolume(cx, cz)) { ++volumeTiles; return true; }   // the volume moves: rebuilt when its water changes (below)
        return inRegion(cx, cz);
    };
    // the old water has no bottoms (a flat level per column): one run reaching down, its law unchanged (37)
    const WaterRunsFn fallback = m_water ? runsOf([this](int wx, int wz, float& s, glm::vec2& f) { return m_water->columnWater(wx, wz, s, f); }) : WaterRunsFn();
    const WaterRunsFn column = composeRuns(m_volumeColumn, fallback);
    auto key = [](int cx, int cz) { return (static_cast<uint64_t>(static_cast<uint32_t>(cx)) << 32) | static_cast<uint32_t>(cz); };
    // 27.21: a tile under a volume reads the volumes there and, where they hold no column, the old water: outside its region
    // the static table, inside it the live cells - which change unless the region holds no water at all (anyWet false: every
    // column there reads dry). So the signature = what the volumes hold, valid outside the region or while it is dry; 0 =
    // unknown: rebuilt.
    // The same holds for the old water's region tiles with no volume over them: dry region = every column dry, kept.
    const bool regionDry = !m_water || !m_water->anyWet();
    auto fieldsSig = [&](int cx, int cz) -> uint64_t {
        if (!m_volumeSignature) return 0;
        const int x0 = cx * WATER_TILE_CELLS, z0 = cz * WATER_TILE_CELLS;
        return m_volumeSignature(x0, z0, x0 + WATER_TILE_CELLS - 1, z0 + WATER_TILE_CELLS - 1);
    };
    auto volumeSig = [&](int cx, int cz) -> uint64_t { return (inRegion(cx, cz) && !regionDry) ? 0 : fieldsSig(cx, cz); };
    m_waterStats.waterBuildMs = 0.0; m_waterStats.tilesBuilt = 0;
    auto build = [&](int cx, int cz, uint64_t sig = 0) {
        const auto tb = std::chrono::steady_clock::now();
        ++m_waterStats.tilesBuilt;
        CachedWaterTile t;
        // 27.21: no volume field over the tile - the volumes answer none of its columns; read the fallback alone (the composed
        // source asked every field first, for every column: 64 sleeping ponds made the old water's 10-13 tiles cost 3.6 ms a frame)
        const bool fallbackOnly = (sig ? sig : fieldsSig(cx, cz)) == kNoVolumeWater;
        t.background = !buildWaterTile(cx, cz, fallbackOnly ? fallback : column, sea, seaY, t.cells);   // an empty fn reads dry
        t.builtFrame = m_waterFrame;
        t.volumeSig = sig;
        m_waterCache[key(cx, cz)] = std::move(t);
        m_waterStats.waterBuildMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tb).count();
    };
    int built = 0, pending = 0;
    for (int slot : m_waterOrder) {
        const int cx = minChunk.x + slot % WATER_DIR_CHUNKS, cz = minChunk.y + slot / WATER_DIR_CHUNKS;
        if (inSim(cx, cz)) {                                      // live water: every frame, unless what it reads there stands
            const uint64_t sig = volumeSig(cx, cz);
            const auto it = sig ? m_waterCache.find(key(cx, cz)) : m_waterCache.end();
            if (it != m_waterCache.end() && it->second.volumeSig == sig) {
                ++volumeKept;
                if (s_checkKeptWaterTiles) {
                    std::vector<uint32_t> fresh;
                    const bool bgFresh = !buildWaterTile(cx, cz, column, sea, seaY, fresh);
                    ++m_waterStats.keptChecked;
                    if (bgFresh != it->second.background || fresh != it->second.cells) ++m_waterStats.keptMismatch;
                }
                continue;
            }
            build(cx, cz, sig);
            continue;
        }
        if (m_waterCache.count(key(cx, cz))) continue;
        if (built < WATER_TILE_BUILD_BUDGET) { build(cx, cz); ++built; } else ++pending;
    }
    // Static tiles drift (a drained pond, a terrain edit): re-check a few per frame, in rotation.
    for (int r = 0; r < WATER_TILE_REFRESH_BUDGET && pending == 0; ++r) {
        const int slot = m_waterOrder[m_waterRefreshCursor++ % m_waterOrder.size()];
        const int cx = minChunk.x + slot % WATER_DIR_CHUNKS, cz = minChunk.y + slot / WATER_DIR_CHUNKS;
        if (!inSim(cx, cz)) build(cx, cz);
    }
    // Assemble the directory + pool, nearest first, capped at WATER_MAX_TILES.
    std::vector<uint32_t> dir(static_cast<size_t>(WATER_DIR_CHUNKS) * WATER_DIR_CHUNKS, WATER_TILE_NONE);
    std::vector<uint32_t> cells;
    uint32_t tiles = 0;
    uint64_t over = 0;
    for (int slot : m_waterOrder) {
        const int cx = minChunk.x + slot % WATER_DIR_CHUNKS, cz = minChunk.y + slot / WATER_DIR_CHUNKS;
        auto it = m_waterCache.find(key(cx, cz));
        if (it == m_waterCache.end() || it->second.background) continue;
        if (tiles >= WATER_MAX_TILES) { ++over; continue; }
        dir[slot] = tiles++;
        cells.insert(cells.end(), it->second.cells.begin(), it->second.cells.end());
    }
    if (over > 0 && !m_waterOverflowLogged) {
        m_waterOverflowLogged = true;
        LOG_WARN("DebrisRuntime", "water: {} wet chunk columns past the {}-tile cap read as background "
                 "(the farthest from the viewer); logged once", over, WATER_MAX_TILES);
    }
    m_waterStats.overflow += over;
    m_waterStats.tilesUploaded = static_cast<int>(tiles);
    m_waterStats.tilesCached   = static_cast<int>(m_waterCache.size());
    m_waterStats.tilesPending  = pending;
    m_waterStats.volumeTiles   = volumeTiles;
    m_waterStats.volumeTilesKept = volumeKept;
    m_waterStats.minChunk      = minChunk;
    m_gpu->setWater(minChunk, sea, seaY, std::move(dir), std::move(cells));
    m_waterUploaded = true;
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

    // Phase 6a: apply the push-back read back since the last feed - to LIVE characters only (this
    // list); an owner that is no longer fed is dropped with its push.
    m_pushStats.lastOwners = 0;
    m_pushStats.lastMaxImpulse = 0.0f;
    std::unordered_map<const Scene::AnimatedVoxelCharacter*, uint32_t> liveIds;
    for (auto& [d2, c] : movers) {
        auto it = m_ownerIds.find(c);
        const uint32_t id = (it != m_ownerIds.end()) ? it->second : m_nextOwnerId++;
        liveIds[c] = id;
        if (!m_pushBack) continue;
        auto p = m_pendingPush.find(id);
        if (p == m_pendingPush.end()) continue;
        const float mag = glm::length(glm::vec2(p->second.x, p->second.z));
        if (mag > 1e-4f) {
            c->applyDebrisPush(p->second);
            ++m_pushStats.applied;
            ++m_pushStats.lastOwners;
            m_pushStats.lastMaxImpulse = std::max(m_pushStats.lastMaxImpulse, mag);
            m_pushStats.maxImpulse     = std::max(m_pushStats.maxImpulse, mag);
        }
    }
    for (const auto& [id, j] : m_pendingPush) {
        bool live = false;
        for (const auto& [c, lid] : liveIds) if (lid == id) { live = true; break; }
        if (!live) ++m_pushStats.droppedNotLive;
    }
    m_pendingPush.clear();
    m_ownerIds.swap(liveIds);   // forget characters no longer fed (their ids are never reused)

    std::vector<Scene::AnimatedVoxelCharacter::MoverBox> limbs;
    std::vector<GpuParticlePhysics::MoverBox> boxes;
    for (auto& [d2, c] : movers) {
        limbs.clear();
        c->collectMoverBoxes(limbs);
        const uint32_t owner = m_ownerIds[c];
        for (const auto& l : limbs) boxes.push_back({l.center, l.halfExtents, l.rotation, l.velocity, owner});
    }
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
