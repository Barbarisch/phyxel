#pragma once

#include "core/DamageSystem.h"
#include "core/GpuParticlePhysics.h"
#include <glm/glm.hpp>
#include <climits>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Phyxel {
class ChunkManager;
namespace Vulkan   { class VulkanDevice; }
namespace Graphics { class RenderCoordinator; }
namespace Physics  { class PhysicsWorld; }
namespace Scene    { class AnimatedVoxelCharacter; }
namespace Core     { class KinematicVoxelManager; struct SpellDefinition; class WaterManager; }

// DebrisInteractionPlan Phase 5: everything a host (the editor's Application, or a shipped game)
// does to run GPU debris, in ONE place - so the editor and shipped games run the same code path
// instead of two hand-synced copies.
//   initialize   - create the solver unless disabled; wire it to the RenderCoordinator (compute,
//                  draw, shared static occupancy) and the ChunkManager (debris light sampler). A
//                  missing solver is LOUD: one ERROR and a reason, never a silent half-state.
//   beginFrame   - the solver's per-frame CPU work (slot tracking, staging upload).
//   feed*        - the movers (Phase 3), in the order the frame produces them:
//                  characters after the entity/NPC updates, kinematic objects (doors, animated
//                  parts, held items) before the CPU physics step, CPU bodies after it.
class DebrisRuntime {
public:
    struct Config {
        // game.json "debris.enabled" (shipped games) - default ON (user decision 2026-10-07).
        // PHYXEL_DISABLE_GPU_DEBRIS=1 / --disable-gpu-debris also turn it off, for tests.
        bool enabled = true;
    };

    /// Command-line switch shared by the editor and every game main: `--disable-gpu-debris` sets
    /// PHYXEL_DISABLE_GPU_DEBRIS=1 (launchers pass arguments, not environment). Returns true when
    /// `arg` was the switch.
    static bool handleArg(const std::string& arg);

    DebrisRuntime() = default;
    ~DebrisRuntime();
    DebrisRuntime(const DebrisRuntime&) = delete;
    DebrisRuntime& operator=(const DebrisRuntime&) = delete;

    /// Returns true when GPU debris is live. False is a supported state (disabled or no GPU
    /// support): gpu() is null, disabledReason() says why, and every break refuses its pieces
    /// (DamageSystem::refusedDebrisTotal).
    bool initialize(Vulkan::VulkanDevice* device, Graphics::RenderCoordinator* renderer,
                    ChunkManager* chunks, Physics::PhysicsWorld* physics, const Config& cfg = {});

    /// Release the solver while the Vulkan device is still alive (a shipped game calls this first
    /// in onShutdown; GameShell's member would otherwise outlive the device). Detaches it from the
    /// RenderCoordinator and ChunkManager it was wired to. Safe to call twice.
    void shutdown();

    GpuParticlePhysics* gpu() const { return m_gpu.get(); }
    bool enabled() const { return m_gpu != nullptr; }
    const std::string& disabledReason() const { return m_disabledReason; }

    void beginFrame(float dt);

    /// Every animated character this frame (any order, duplicates and nulls allowed): oriented
    /// limb boxes with per-limb velocity, nearest `eye` first (the MAX_KINEMATIC overflow drops
    /// the farthest). Call after all characters updated this frame.
    void feedCharacters(const std::vector<Scene::AnimatedVoxelCharacter*>& characters, const glm::vec3& eye);

    /// Doors / animated template parts / held items (KinematicVoxelObject.pushesDebris): runs
    /// KinematicVoxelManager::syncCollidersToPhysics(dt) (which also registers the CPU kinematic
    /// obstacles) and feeds the same boxes, nearest `eye` first, trimmed to the slots the limbs
    /// left. Call after every owner set its transform, before the CPU physics step. Works with
    /// debris disabled too (doors still block CPU bodies).
    void feedKinematicObjects(Core::KinematicVoxelManager* kvm, float dt, const glm::vec3& eye);

    /// CPU rigid bodies (VoxelDynamicsWorld) at their post-step pose, whole bodies only, in the
    /// slots left. Call after the CPU physics step.
    void feedRigidBodies(const glm::vec3& eye);

    /// The game-code break entry (Phase 5, user decision 2026-10-07): a voxel blast through the
    /// same DamageSystem the editor uses - breaks static voxels into GPU debris and pushes what
    /// already moves (Phase 4). With debris disabled the pieces are refused and counted.
    DamageResult applyDamage(const glm::vec3& center, float radius, float energy,
                             const glm::vec3& direction = glm::vec3(0.0f));

    /// How a spell's impact blasts the voxels it lands on (the scaffold's spell hook). Spells that
    /// deal no damage (heals, buffs) do not blast. Radius: the area spell's size (feet -> m), else
    /// 1 m for a single-target bolt. Energy: SPELL_ENERGY_PER_DAMAGE x the average base damage -
    /// a fireball (8d6, average 28) lands at ~336, the scale of the editor's test-spell blast
    /// (cast_test_spell default 350 at power 1).
    struct SpellBlast { bool blast = false; float radius = 0.0f; float energy = 0.0f; };
    static constexpr float SPELL_ENERGY_PER_DAMAGE = 12.0f;
    static constexpr float SPELL_SINGLE_TARGET_RADIUS = 1.0f;
    static SpellBlast spellBlast(const Core::SpellDefinition& spell);

    // ---- Phase 6b: debris events (sleep / wake / impact, read back from the GPU) --------------
    // beginFrame drains them: a SLEEP adds the piece to the settled registry (gatherable rubble),
    // a WAKE removes it; impacts and settles become sounds through the host's sound callback.

    /// The host's positional one-shot (e.g. SoundRegistry::playEvent): event name, world position,
    /// volume scale 0..1. Unset = silent (the events are still counted).
    using SoundCallback = std::function<void(const std::string& event, const glm::vec3& pos, float volume)>;
    void setSoundCallback(SoundCallback cb) { m_sound = std::move(cb); }
    static constexpr const char* SOUND_IMPACT = "debris.impact";
    static constexpr const char* SOUND_SETTLE = "debris.settle";
    // Per-frame caps: a collapsing wall produces hundreds of impacts in a frame; the loudest few
    // carry the sound, the rest would only stack into noise (and voices).
    static constexpr int MAX_IMPACT_SOUNDS_PER_FRAME = 3;
    static constexpr int MAX_SETTLE_SOUNDS_PER_FRAME = 2;

    struct SettledPiece {
        uint32_t  slot = 0, serial = 0, materialIndex = 0;
        glm::vec3 position{0.0f};
        float     scale = 1.0f;
    };
    /// Settled (asleep) pieces still alive in their slot. Pieces whose slot was despawned or
    /// recycled are dropped lazily (slot serial check).
    /// Settled pieces still alive (despawned / recycled slots are not counted, even before the
    /// registry prunes them - measured: a cleared pool left 9 stale entries counted as settled).
    size_t settledCount() const;
    std::vector<SettledPiece> settledNear(const glm::vec3& center, float radius) const;

    /// Gather settled rubble (finite physical items): every settled piece within `radius` (up to
    /// maxPieces, nearest first) is removed from the world and its VOLUME is credited to its
    /// material - a full cube is 1 unit, a 1/3 piece 1/27, a 1/9 piece 1/729 - so shattering a
    /// cube cannot multiply it. Whole units are returned; the fraction carries over per material.
    struct GatherResult {
        int pieces = 0;
        std::map<std::string, int> items;      // material -> whole units gained now
        std::map<std::string, float> carried;  // material -> fraction still owed (< 1)
    };
    GatherResult gather(const glm::vec3& center, float radius, int maxPieces = 64);
    /// The whole units in `owed` (full-cube volume); leaves the fraction. Tolerates float
    /// summation error: 27 x (1/3)^3 sums to 0.99999... and is ONE cube.
    static int takeWholeUnits(float& owed);

    // ---- Phase 6c: water for GPU debris ---------------------------------------------------------
    // The water source (null = no water: debris stays dry). beginFrame builds per-chunk-column water
    // tiles over the occupancy window (solver_shared.h WATER_*): sim-region tiles every frame,
    // table/sea tiles once (nearest first, WATER_TILE_BUILD_BUDGET per frame) then refreshed in
    // rotation. A column with no tile reads the background (implicit sea level, or dry).
    void setWaterSource(Core::WaterManager* water) { m_water = water; }
    static constexpr int WATER_TILE_BUILD_BUDGET   = 16;   // new static tiles per frame
    static constexpr int WATER_TILE_REFRESH_BUDGET = 4;    // old static tiles re-checked per frame
    struct WaterStats {
        bool ready = false; int tilesUploaded = 0, tilesCached = 0, tilesPending = 0;
        uint64_t overflow = 0; glm::ivec2 minChunk{0};
        int volumeTiles = 0;   ///< E2: chunk columns under an active volume (rebuilt every frame)
    };
    const WaterStats& waterStats() const { return m_waterStats; }

    /// One column's water: true + surface Y + flow (m/s) when wet (WaterManager::columnWater).
    using WaterColumnFn = std::function<bool(int wx, int wz, float& surfaceY, glm::vec2& flow)>;
    /// WaterCore E2 (docs/WaterCore.md 19): the active volumes are a water source AHEAD of the old one -
    /// a column a volume holds water in reads the volume's surface + surface velocity; tiles under a
    /// volume box are rebuilt every frame (the volume moves). `boxes` = WaterCoreManager::volumeBoxes().
    void setVolumeWater(WaterColumnFn column, const std::vector<std::pair<glm::ivec3, glm::ivec3>>* boxes) { m_volumeColumn = std::move(column); m_volumeBoxes = boxes; }
    /// The column source debris reads: the volume where it answers, else the fallback. Pure.
    static WaterColumnFn composeColumn(WaterColumnFn volume, WaterColumnFn fallback);
    /// E2: the water's share of wet debris' drag + current since the last call (from the GPU readback).
    std::vector<GpuParticlePhysics::WaterExchange> takeWaterExchange() { return m_gpu ? m_gpu->takeWaterExchange() : std::vector<GpuParticlePhysics::WaterExchange>{}; }
    /// Build chunk column (chunkX, chunkZ)'s tile (WATER_TILE_CELLS^2 cells x 2 words: surface-Y
    /// bits, packHalf2x16(flow)). Returns false - and leaves `out` empty - when every cell is just
    /// the background (implicit sea at seaLevel with no flow, or dry), so it needs no tile.
    static bool buildWaterTile(int chunkX, int chunkZ, const WaterColumnFn& column, bool implicitSea,
                               float seaLevel, std::vector<uint32_t>& out);
    /// CPU mirror of solver_integrate's waterSurface() lookup (same phxWaterDirIndex /
    /// phxWaterCellIndex): the surface over world column (x, z) through the directory + tiles.
    static float sampleWaterTiles(int x, int z, const glm::ivec2& dirMinChunk, const std::vector<uint32_t>& dir,
                                  const std::vector<uint32_t>& cells, bool implicitSea, float seaLevel,
                                  glm::vec2* flow = nullptr);

    // ---- Phase 6a: debris pushes back on characters -----------------------------------------------
    // The contacts' force on a character's limb boxes (read back two frames late) is applied to that
    // character (AnimatedVoxelCharacter::applyDebrisPush) the next time it is fed - only if it is
    // still in the fed list, so a character removed meanwhile is never touched. On by default.
    void setPushBack(bool on) { m_pushBack = on; }
    bool pushBack() const { return m_pushBack; }
    struct PushStats { uint64_t applied = 0; float lastMaxImpulse = 0.0f; float maxImpulse = 0.0f; int lastOwners = 0;
                       uint64_t readOwners = 0; double readImpulseTotal = 0.0; uint64_t droppedNotLive = 0; };
    const PushStats& pushStats() const { return m_pushStats; }

    struct EventStats { uint64_t sleep = 0, wake = 0, impact = 0, soundsImpact = 0, soundsSettle = 0; };
    const EventStats& eventStats() const { return m_stats; }
    /// The last few hundred events (newest last) for the API / tests.
    const std::deque<GpuParticlePhysics::DebrisEvent>& recentEvents() const { return m_recent; }

private:
    std::unique_ptr<GpuParticlePhysics> m_gpu;
    std::string             m_disabledReason;
    Physics::PhysicsWorld*  m_physics = nullptr;
    Graphics::RenderCoordinator* m_renderer = nullptr;
    ChunkManager*           m_chunks = nullptr;
    bool m_loggedObjectSkip = false;
    bool m_loggedBodySkip   = false;
    // Phase 6b
    void pumpEvents();
    bool settledValid(const SettledPiece& p) const;
    SoundCallback m_sound;
    std::unordered_map<uint32_t, SettledPiece> m_settled;
    std::map<std::string, float> m_gatherRemainder;
    EventStats m_stats;
    std::deque<GpuParticlePhysics::DebrisEvent> m_recent;
    // Phase 6a
    bool m_pushBack = true;
    PushStats m_pushStats;
    std::unordered_map<const Scene::AnimatedVoxelCharacter*, uint32_t> m_ownerIds;
    uint32_t m_nextOwnerId = 1;
    std::unordered_map<uint32_t, glm::vec3> m_pendingPush;   // owner id -> impulse (N*s)
    // Phase 6c
    void updateWater();
    struct CachedWaterTile { bool background = true; std::vector<uint32_t> cells; uint64_t builtFrame = 0; };
    Core::WaterManager* m_water = nullptr;
    WaterColumnFn m_volumeColumn;                                              // E2
    const std::vector<std::pair<glm::ivec3, glm::ivec3>>* m_volumeBoxes = nullptr;   // E2
    std::unordered_map<uint64_t, CachedWaterTile> m_waterCache;
    std::vector<int> m_waterOrder;           // directory slots, nearest the window centre first
    glm::ivec2 m_waterMinChunk{INT32_MIN, INT32_MIN};
    size_t     m_waterRefreshCursor = 0;
    uint64_t   m_waterFrame = 0;
    bool       m_waterUploaded = false;
    bool       m_waterOverflowLogged = false;
    WaterStats m_waterStats;
};

}  // namespace Phyxel
