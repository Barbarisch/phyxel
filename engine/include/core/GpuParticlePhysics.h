#pragma once

#include "core/Types.h"
#include "core/DebrisSettleAnalyzer.h"
#include "vulkan/ComputePipeline.h"
#include "solver_shared.h"   // shaders/: constants + push-constant layouts shared with GLSL (1b)
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <string>
#include <cstdint>
#include <functional>
#include <fstream>
#include <utility>

namespace Phyxel {

namespace Vulkan { class VulkanDevice; }
class GpuProfiler;

/**
 * GpuParticlePhysics — GPU AVBD rigid-body solver for voxel debris (cubes, subcubes,
 * microcubes). Gameplay bodies (furniture, items, fragments, characters) live on the CPU
 * VoxelDynamicsWorld.
 *
 * Per physics tick (recordComputeCommandsNew; docs/DynamicVoxelPhysics.md "GPU Compute Path"):
 *   sync_in → integrate → grid/scan/sort → narrowphase + voxel contacts → CSR → colouring
 *   → AVBD dual/primal → hard-contact → sync_out → warmstart save,
 *   then particle_expand.comp writes 6 DynamicSubcubeInstanceData faces per particle for
 *   dynamic_voxel.vert / dynamic_shadow.vert.
 * Settling is gated by tools/debris_settle_bench.py (docs/DebrisSettlingPlan.md §R).
 */
class GpuParticlePhysics {
public:
    static constexpr uint32_t MAX_PARTICLES  = DebrisShared::MAX_PARTICLES;
    static constexpr uint32_t MAX_FACE_SLOTS = MAX_PARTICLES * 6; // 60 000 face instances

    // 96 bytes, std430-compatible (vec3+float pairs at 16-byte boundaries)
    struct GpuParticle {
        glm::vec3 position;
        float     lifetime;
        glm::vec3 prevPosition;
        float     maxLifetime;
        glm::vec4 rotation;      // quaternion (x,y,z,w)
        glm::vec3 angularVel;
        uint32_t  flags;
        glm::vec3 scale;
        uint32_t  materialIndex;
        glm::vec4 color;
    };
    static_assert(sizeof(GpuParticle) == 96, "GpuParticle must be 96 bytes");

    // Spawn request (CPU → GPU)
    struct SpawnParams {
        glm::vec3   position;
        glm::vec3   velocity;           // converted to prevPosition = pos - vel*dt at spawn
        glm::quat   rotation    = glm::quat(1,0,0,0);
        glm::vec3   angularVel  = glm::vec3(0);
        glm::vec3   scale       = glm::vec3(1);
        std::string materialName= "Default";
        glm::vec4   color       = glm::vec4(1);
        float       lifetime    = 30.0f;
        uint32_t    typeFlags   = 0; // PARTICLE_TYPE_CUBE etc.
        /// Micro position inside the parent cube (mx*81+my*9+mz, 0..728): which slice of the
        /// parent texture the piece shows (DamageSystem::debrisSliceFor). Ignored for full cubes.
        uint32_t    slice       = 0;
    };

    GpuParticlePhysics();
    ~GpuParticlePhysics();

    // Non-copyable
    GpuParticlePhysics(const GpuParticlePhysics&)            = delete;
    GpuParticlePhysics& operator=(const GpuParticlePhysics&) = delete;

    /**
     * Initialize all GPU buffers and compute pipelines.
     * @param vulkanDevice  Phyxel VulkanDevice (must have initComputeResources() called)
     * @param shaderDir     Path to compiled .spv directory
     */
    bool initialize(Vulkan::VulkanDevice* vulkanDevice, const std::string& shaderDir);
    void cleanup();

    // ---- Per-frame interface ----

    /** Queue a particle to spawn next frame. Thread-safe within a single frame. */
    void queueSpawn(const SpawnParams& p);

    /** Phase 4c: sample the baked light field at a world pos so debris is lit like the world
     *  (darkens in unlit interiors, picks up glow). Returns vec4(sky, blockR, blockG, blockB),
     *  each 0..15. Sampled once per particle at spawn and carried via the (render-unused) color
     *  field → reserved2 → dynamic_voxel.vert. If unset, debris defaults to full sky (old look). */
    using LightSampler = std::function<glm::vec4(const glm::vec3& worldPos)>;
    void setLightSampler(LightSampler fn) { m_lightSampler = std::move(fn); }

    /**
     * Advance CPU-side lifetime tracking and upload pending spawns to staging.
     * Must be called before recordComputeCommands().
     */
    void update(float dt);

    /**
     * Record integrate → collide → expand compute dispatches into cmd.
     * Call this BEFORE vkCmdBeginRenderPass.
     * Includes pipeline barriers from compute → vertex input.
     */
    void recordComputeCommands(VkCommandBuffer cmd, uint32_t frameIndex, GpuProfiler* profiler = nullptr);

    // ---- The static world: the SHARED micro occupancy (DebrisInteractionPlan 1c) ----
    // Debris collides against the same packed pool CPU physics and lighting use
    // (VoxelLightOccupancyGpu), not a private bitfield. The pool has one buffer pair per frame
    // in flight, so the contact passes keep one descriptor set per slot and bind the slot of
    // the frame being recorded (the CPU rewrites the OTHER slot meanwhile).
    static constexpr uint32_t OCC_FRAME_SLOTS = 2;   // == VoxelLightOccupancyGpu::kSlots
    /// Once, before the first frame is recorded (descriptor sets must not change in flight).
    void setStaticOccupancyBuffers(const VkBuffer dir[OCC_FRAME_SLOTS], VkDeviceSize dirBytes,
                                   const VkBuffer pool[OCC_FRAME_SLOTS], VkDeviceSize poolBytes);
    /// Every frame, before recordComputeCommands: the box the slot's pack was built with, and
    /// whether the pool is readable. Not ready = every sample UNKNOWN = every body held (counted).
    void setStaticOccupancyBox(const glm::ivec3& boxMinChunk, bool ready);
    bool staticOccupancyWired() const { return m_staticOccWired; }

    // ---- Character collision interface ----

    // Max per-limb segment boxes uploaded per frame. Must cover the character's full
    // segment set — 12 boxes (4 torso + 4 arm + 4 leg). Setting this too low silently
    // drops the trailing boxes (e.g. the legs), leaving floor-height debris uncovered.
    static constexpr uint32_t MAX_CHAR_SEGMENTS = DebrisShared::MAX_CHAR_SEGMENTS;  // sizes charSeg[] in solver_integrate.comp

    // One body-part box (std430: two vec4s = 32 bytes).
    struct CharSegmentGpu {
        glm::vec4 center;       // xyz = world center
        glm::vec4 halfExtents;  // xyz = world half-extents
    };
    static_assert(sizeof(CharSegmentGpu) == 32, "CharSegmentGpu must be 32 bytes");

    // std430 layout uploaded each frame for particle-vs-character collision (the player
    // "shove", solver_integrate.comp): union AABB for a cheap early-out, then the per-limb
    // segments[]. The whole struct is retired by real kinematic contacts
    // (docs/DebrisInteractionPlan.md D7, Phase 2) — until then its layout stays fixed.
    struct CharacterCollider {
        glm::vec3 center;       // union AABB center (broadphase)
        float     segmentCount; // number of active segments (0 = disabled)
        glm::vec3 halfExtents;  // union AABB half-extents
        float     pad0;
        glm::vec3 velocity;     // character velocity (imparted to pushed debris)
        float     legacyActive; // unused padding since D4 (its only reader, particle_collide.comp, is deleted)
        CharSegmentGpu segments[MAX_CHAR_SEGMENTS];
    };
    static_assert(sizeof(CharacterCollider) == 48 + 32 * MAX_CHAR_SEGMENTS,
                  "CharacterCollider layout mismatch");

    /** Update per-limb character colliders for the live solver. `boxes` = (center,
     *  halfExtents) of each body segment; the union AABB is computed internally.
     *  Empty disables character collision. Called each frame from Application. */
    void setCharacterColliders(const std::vector<std::pair<glm::vec3, glm::vec3>>& boxes,
                               const glm::vec3& velocity);

    /** Convenience: single-box character collider (used as a fallback when segment
     *  boxes are unavailable). Delegates to setCharacterColliders. */
    void setCharacterAABB(const glm::vec3& center, const glm::vec3& halfExtents, const glm::vec3& velocity);

    /** Disable character collision (no character active). */
    void clearCharacterAABB();

    // GPU-side per-material physics (32 bytes, std430).
    // Populated at init from MaterialRegistry, indexed by GpuParticle::materialIndex.
    struct alignas(4) MaterialPhysicsGpu {
        float mass;            // Gravity scaling
        float restitution;     // Bounciness
        float friction;        // Surface grip
        float linearDamp;      // Per-frame velocity damping
        float angularDamp;     // Per-frame spin damping
        float breakForceScale; // Break impulse multiplier
        float pad0;
        float pad1;
    };
    static_assert(sizeof(MaterialPhysicsGpu) == 32, "MaterialPhysicsGpu must be 32 bytes");

    // ---- Render pipeline interface ----

    /** Bind this as vertex buffer binding 1 for the dynamic voxel pipeline. */
    VkBuffer getFaceBuffer()        const { return m_faceBuffer; }
    VkBuffer getIndirectDrawBuffer() const { return m_indirectDrawBuffer; }

    uint32_t getActiveParticleCount() const { return m_activeCount; }
    bool     isInitialized()          const { return m_initialized; }

    /** Immediately mark all active particles as dead and reset tracking state. */
    void despawnAll();

    // ---- Debug timing stats (ring buffer) ----
    struct FrameTimingEntry {
        float dt;             // raw delta time passed to update()
        float accumulator;    // timeAccumulator AFTER adding dt
        float interpAlpha;    // accumulator / FIXED_DT sent to expand shader
        uint32_t physicsTicks;// number of physics ticks this frame
        uint32_t activeCount; // active particles
        uint32_t frameNumber; // monotonic frame counter
    };
    static constexpr size_t TIMING_RING_SIZE = 300; // ~5 seconds at 60fps
    const std::vector<FrameTimingEntry>& getTimingRing() const { return m_timingRing; }
    size_t getTimingRingHead() const { return m_timingRingHead; }
    uint32_t getTimingFrameCounter() const { return m_timingFrameCounter; }
    float getFixedDt() const { return FIXED_DT; }

    // ---- Position logging (GPU readback to CSV file) ----
    /** Start logging particle positions to a file. Returns true if logging started. */
    bool startPositionLog(const std::string& filePath);
    /** Stop logging and close the file. */
    void stopPositionLog();
    bool isPositionLogging() const { return m_positionLogging; }

    // ---- Settle probe (docs/DebrisSettlingPlan.md §3) ----
    // Measures whether debris actually loses energy and comes to rest. While running, every
    // physics tick's particle state + solver telemetry counters + graph colours are copied to
    // a host-visible ring (one slot per frame in flight) and fed to a DebrisSettleAnalyzer
    // when that frame's fence has retired (≈2 frames of lag). Off by default — zero cost.
    void startSettleProbe(const Core::DebrisSettleAnalyzer::Config& cfg);
    void stopSettleProbe();
    bool isSettleProbing() const { return m_probeActive; }

    // Freeze / single-step (debug rigs + exact-time captures). While frozen no physics tick
    // runs and lifetimes do not drain; stepTicks(n) lets exactly n more ticks run (≤ 4 per
    // frame, the normal catch-up cap). Rendering continues, so a frozen pile can be captured.
    void setFrozen(bool frozen) { m_frozen = frozen; m_stepBudget = 0; m_timeAccumulator = 0.0f; }
    bool isFrozen() const { return m_frozen; }
    void stepTicks(uint32_t n) { m_stepBudget += n; }
    uint32_t pendingStepTicks() const { return m_stepBudget; }
    uint64_t totalTicks() const { return m_totalTicks; }

    // Debris-settling fix switches (solver_types.glsl SOLVER_FLAG_*), pushed to the solver
    // every tick. Default = all shipped fixes on; the API exposes them for A/B runs.
    static constexpr uint32_t SOLVER_FLAG_MASS_PENALTY  = DebrisShared::SOLVER_FLAG_MASS_PENALTY;
    static constexpr uint32_t SOLVER_FLAG_START_AT_REST = DebrisShared::SOLVER_FLAG_START_AT_REST;
    static constexpr uint32_t SOLVER_FLAG_HC_NEUTRAL    = DebrisShared::SOLVER_FLAG_HC_NEUTRAL;
    static constexpr uint32_t SOLVER_FLAG_POST_STAB     = DebrisShared::SOLVER_FLAG_POST_STAB;
    // POST_STAB (8) is implemented but OFF: measured worse on the bench (more forced sleeps in
    // drop_layer/crater/crater_subcube — docs/evidence/debris_settle/fix4-tickstart-ps).
    static constexpr uint32_t SOLVER_FLAG_STATIC_FRICTION = DebrisShared::SOLVER_FLAG_STATIC_FRICTION;
    static constexpr uint32_t SOLVER_FLAGS_DEFAULT      = DebrisShared::SOLVER_FLAGS_DEFAULT;  // all but POST_STAB
    static constexpr float    SOLVER_ALPHA              = DebrisShared::SOLVER_ALPHA;
    static constexpr uint32_t PRIMAL_STORE_VELOCITY     = DebrisShared::PRIMAL_STORE_VELOCITY;
    void     setSolverFlags(uint32_t f) { m_solverFlags = f; }
    uint32_t solverFlags() const { return m_solverFlags; }
    // Cold-contact stiffness multiplier (x m/dt^2) when SOLVER_FLAG_MASS_PENALTY is set.
    void  setColdPenaltyScale(float s) { m_coldPenaltyScale = s; }
    float coldPenaltyScale() const { return m_coldPenaltyScale; }
    const Core::DebrisSettleAnalyzer& settleAnalyzer() const { return m_settle; }

    // ---- Material name → index lookup ----
    static uint32_t materialNameToIndex(const std::string& name);

private:
    // Physics constants
    static constexpr float GRAVITY             = -9.81f;

    // ---- Vulkan resources ----
    VkDevice         m_device         = VK_NULL_HANDLE;
    VkPhysicalDevice m_physDevice     = VK_NULL_HANDLE;

    // Particle SSBO — device-local, MAX_PARTICLES × 96 bytes
    VkBuffer         m_particleBuffer = VK_NULL_HANDLE;
    VkDeviceMemory   m_particleMem    = VK_NULL_HANDLE;

    // Face output buffer — device-local, STORAGE + VERTEX, MAX_FACE_SLOTS × 64 bytes
    VkBuffer         m_faceBuffer     = VK_NULL_HANDLE;
    VkDeviceMemory   m_faceMem        = VK_NULL_HANDLE;

    // Staging buffer — host-coherent, persistently mapped, for new particle uploads
    VkBuffer         m_stagingBuffer  = VK_NULL_HANDLE;
    VkDeviceMemory   m_stagingMem     = VK_NULL_HANDLE;
    void*            m_stagingMapped  = nullptr;

    // Indirect draw command buffer — VkDrawIndirectCommand (16 bytes), device-local
    VkBuffer         m_indirectDrawBuffer = VK_NULL_HANDLE;
    VkDeviceMemory   m_indirectDrawMem    = VK_NULL_HANDLE;

    // Material→texture lookup table — device-local SSBO
    VkBuffer         m_matTexBuffer   = VK_NULL_HANDLE;
    VkDeviceMemory   m_matTexMem      = VK_NULL_HANDLE;

    // Character collider AABB — host-coherent, persistently mapped, 48 bytes
    VkBuffer         m_characterBuffer = VK_NULL_HANDLE;
    VkDeviceMemory   m_characterMem    = VK_NULL_HANDLE;
    void*            m_characterMapped = nullptr;

    // Per-material physics properties — host-coherent, persistently mapped
    VkBuffer         m_materialPhysBuffer = VK_NULL_HANDLE;
    VkDeviceMemory   m_materialPhysMem    = VK_NULL_HANDLE;
    void*            m_materialPhysMapped = nullptr;

    // Sorted spatial grid for cache-coherent inter-particle collision
    static constexpr int    GRID_SIZE  = DebrisShared::GRID_SIZE;
    static constexpr int    GRID_CELLS = DebrisShared::GRID_CELLS;   // 262,144
    // Work-efficient parallel prefix sum over gridCellCount. SCAN_BLOCK is the scan
    // shaders' workgroup size (solver_shared.h PHX_SCAN_BLOCK).
    static constexpr int    SCAN_BLOCK  = DebrisShared::SCAN_BLOCK;
    static constexpr int    SCAN_BLOCKS = DebrisShared::SCAN_BLOCKS; // 1024
    VkBuffer         m_gridCellCountBuffer  = VK_NULL_HANDLE;  // uint[GRID_CELLS] — particles per cell
    VkDeviceMemory   m_gridCellCountMem     = VK_NULL_HANDLE;
    VkBuffer         m_gridCellOffsetBuffer = VK_NULL_HANDLE;  // uint[GRID_CELLS] — END of each cell's sorted range
    VkDeviceMemory   m_gridCellOffsetMem    = VK_NULL_HANDLE;
    VkBuffer         m_sortedParticleBuffer = VK_NULL_HANDLE;  // GpuParticle[MAX_PARTICLES] — sorted by cell
    VkDeviceMemory   m_sortedParticleMem    = VK_NULL_HANDLE;
    VkBuffer         m_sortedIndexBuffer    = VK_NULL_HANDLE;  // uint[MAX_PARTICLES] — canonical index per sorted slot
    VkDeviceMemory   m_sortedIndexMem       = VK_NULL_HANDLE;
    VkBuffer         m_scanBlockSumsBuffer  = VK_NULL_HANDLE;  // uint[SCAN_BLOCKS] — per-block totals for the parallel scan
    VkDeviceMemory   m_scanBlockSumsMem     = VK_NULL_HANDLE;

    // ---- Broadphase + render-feed compute pipelines ----
    Vulkan::ComputePipeline m_gridClearPass;
    Vulkan::ComputePipeline m_gridBuildPass;
    Vulkan::ComputePipeline m_sortScatterPass;
    // Parallel prefix sum over the grid cells (block scan → block-sum scan → add offsets).
    Vulkan::ComputePipeline m_scanBlockPass;
    Vulkan::ComputePipeline m_scanBlockSumsPass;
    Vulkan::ComputePipeline m_scanAddPass;
    Vulkan::ComputePipeline m_expandPass;

    // ---- AVBD constraint solver (solver_*.comp) ----

    static constexpr uint32_t MAX_CONSTRAINTS = DebrisShared::MAX_CONSTRAINTS;
    static constexpr uint32_t MAX_COLORS      = DebrisShared::MAX_COLORS;
    static constexpr int      COLOR_ROUNDS    = 32;  // Jones-Plassmann rounds (16 left bodies uncoloured = skipped)
    static constexpr int      SOLVE_ITERATIONS = 8;

    // Warmstart hash table sizing (solver_shared.h asserts pow2 and >= 2 * MAX_CONSTRAINTS).
    static constexpr uint32_t HASH_CAP        = DebrisShared::HASH_CAP;
    static constexpr uint32_t HASH_BASE       = DebrisShared::HASH_BASE;
    // Sleep wake-bits (docs/PhysicsRestOverhaul.md Phase 2): one bit per body appended
    // after the hash table. Set by narrowphase/integrate, consumed by sync_in next tick.
    static constexpr uint32_t WAKE_WORDS      = DebrisShared::WAKE_WORDS;
    static constexpr uint32_t SOLVER_STATE_UINTS = HASH_BASE + HASH_CAP + WAKE_WORDS;

    // SolverBody buffer — device-local, MAX_PARTICLES × 208 bytes
    VkBuffer       m_solverBodyBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_solverBodyMem    = VK_NULL_HANDLE;

    // Constraint buffer — device-local, MAX_CONSTRAINTS × 128 bytes
    VkBuffer       m_constraintBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_constraintMem    = VK_NULL_HANDLE;

    // Solver state buffer — device-local, SOLVER_STATE_UINTS × uint (counters + warmstart hash table)
    VkBuffer       m_solverStateBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_solverStateMem    = VK_NULL_HANDLE;
    // Warmstart hash table is initialized to HASH_EMPTY once at first solve, then PERSISTS
    // across frames so AVBD multipliers/penalties accumulate (Shallot semantics). Clearing
    // the hash per-frame would reduce AVBD to pure penalty → bodies sink under gravity.
    bool           m_hashInitialized   = false;

    // Warmstart entries — device-local, HASH_CAP × 64 bytes (persists penalties across frames)
    VkBuffer       m_warmstartBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_warmstartMem    = VK_NULL_HANDLE;

    // Graph-coloring CSR buffers — device-local, MAX_CONSTRAINTS / MAX_PARTICLES × uint32
    VkBuffer       m_bodyColorBuffer            = VK_NULL_HANDLE;
    VkDeviceMemory m_bodyColorMem               = VK_NULL_HANDLE;
    VkBuffer       m_bodyConstraintCountBuffer  = VK_NULL_HANDLE;
    VkDeviceMemory m_bodyConstraintCountMem     = VK_NULL_HANDLE;
    VkBuffer       m_bodyConstraintOffsetBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_bodyConstraintOffsetMem    = VK_NULL_HANDLE;
    VkBuffer       m_bodyConstraintCursorBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_bodyConstraintCursorMem    = VK_NULL_HANDLE;
    VkBuffer       m_bodyConstraintListBuffer   = VK_NULL_HANDLE;
    VkDeviceMemory m_bodyConstraintListMem      = VK_NULL_HANDLE;

    // Compute pipelines for new solver
    Vulkan::ComputePipeline m_solverSyncInPass;
    Vulkan::ComputePipeline m_solverIntegratePass;
    Vulkan::ComputePipeline m_solverNarrowphasePass;
    Vulkan::ComputePipeline m_solverVoxelPass;
    Vulkan::ComputePipeline m_solverDualPass;
    Vulkan::ComputePipeline m_solverPrimalPass;
    Vulkan::ComputePipeline m_solverSyncOutPass;
    Vulkan::ComputePipeline m_solverWarmstartSavePass;
    Vulkan::ComputePipeline m_solverHardContactPass;
    Vulkan::ComputePipeline m_csrClearPass;
    Vulkan::ComputePipeline m_csrCountPass;
    Vulkan::ComputePipeline m_prefixSumPass;
    Vulkan::ComputePipeline m_csrScatterPass;
    Vulkan::ComputePipeline m_bodyColorPass;

    // ---- CPU-side state ----
    struct SlotInfo {
        float lifetimeRemaining = 0.0f;
        bool  active            = false;
    };
    std::vector<SlotInfo> m_slots;         // per-particle CPU tracking
    // Inactive particle indices as a MIN-heap (std::greater): a spawn always takes the LOWEST free
    // slot, so slot assignment is a function of current occupancy, never of history. A plain
    // stack handed slots out in reverse after despawnAll, and the solver's colouring/processing
    // order follows slots: the same scenario gave 11.2 / 81.4 / 47.1 mm hard-contact depth on its
    // 1st/2nd/3rd run in one session (DebrisInteractionPlan 1c, session-state dependence).
    std::vector<uint32_t> m_freeSlots;
    void releaseSlot(uint32_t slot);
    LightSampler          m_lightSampler;  // Phase 4c: baked-light sampler for spawned debris (null = full sky)
    uint32_t              m_activeCount = 0;
    uint32_t              m_highWaterSlot = 0; // highest active slot index + 1 (dispatch range)

    struct PendingCopy {
        uint32_t slotIndex;    // particle slot to overwrite
        GpuParticle data;
    };
    std::vector<PendingCopy> m_pendingSpawns;
    std::vector<PendingCopy> m_pendingThisFrame; // snapshot used during recordCompute

    // Slots the CPU retired (lifetime expired / despawned) since the last
    // recordComputeCommands. Their GPU PARTICLE_ACTIVE flag must be cleared
    // explicitly: the GPU lifetime drain only runs on physics-tick frames and
    // only for slots below the dispatch high-water mark, so a retired slot can
    // keep a stale ACTIVE flag and reappear once a later spawn grows the
    // high-water mark back over it. The CPU free list is the source of truth.
    std::vector<uint32_t> m_pendingDeactivations;

    // Fixed-timestep accumulator: physics runs at exactly FIXED_DT intervals
    // regardless of render frame rate. Prevents speed-up at high FPS.
    static constexpr float FIXED_DT = 1.0f / 60.0f;
    float    m_timeAccumulator = 0.0f;  // accumulated real time awaiting physics steps
    uint32_t m_physicsTicks    = 0;     // number of physics steps to run this frame
    float    m_lastRealDt      = 0.0f;  // real elapsed time for lifetime drain

    bool m_initialized = false;

    // ---- Position logging (GPU readback) ----
    VkBuffer         m_readbackBuffer = VK_NULL_HANDLE;
    VkDeviceMemory   m_readbackMem    = VK_NULL_HANDLE;
    void*            m_readbackMapped = nullptr;
    bool             m_positionLogging       = false;
    bool             m_readbackPending       = false; // true after copy cmd recorded
    std::ofstream    m_posLogFile;
    uint32_t         m_posLogFrameCounter    = 0;

    // ---- Settle probe readback ring ----
    // Per tick: GpuParticle[MAX_PARTICLES] | header uint[PROBE_HDR_UINTS] | color uint[MAX_PARTICLES]
    //           | constraintCount uint[MAX_PARTICLES]. Only `count` entries of each array are copied.
    static constexpr uint32_t PROBE_FRAMES     = 2;  // == MAX_FRAMES_IN_FLIGHT
    static constexpr uint32_t PROBE_MAX_TICKS  = 4;  // == physics tick cap per frame
    static constexpr uint32_t PROBE_HDR_UINTS  = 8;  // == HASH_BASE (solver-state header)
    static constexpr VkDeviceSize PROBE_PARTICLES_OFF = 0;
    static constexpr VkDeviceSize PROBE_HDR_OFF   = static_cast<VkDeviceSize>(MAX_PARTICLES) * sizeof(GpuParticle);
    static constexpr VkDeviceSize PROBE_COLOR_OFF = PROBE_HDR_OFF + PROBE_HDR_UINTS * sizeof(uint32_t);
    static constexpr VkDeviceSize PROBE_CCOUNT_OFF = PROBE_COLOR_OFF + static_cast<VkDeviceSize>(MAX_PARTICLES) * sizeof(uint32_t);
    static constexpr VkDeviceSize PROBE_TICK_STRIDE = PROBE_CCOUNT_OFF + static_cast<VkDeviceSize>(MAX_PARTICLES) * sizeof(uint32_t);
    struct ProbeSlot {
        VkBuffer       buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void*          mapped = nullptr;
        uint32_t       ticks  = 0;
        uint32_t       generation = 0;
        uint32_t       count[PROBE_MAX_TICKS] = {};
    };
    ProbeSlot            m_probe[PROBE_FRAMES];
    bool                 m_probeActive     = false;
    bool                 m_frozen          = false;
    uint32_t             m_stepBudget      = 0;
    uint64_t             m_totalTicks      = 0;
    uint32_t             m_solverFlags     = SOLVER_FLAGS_DEFAULT;
    float                m_coldPenaltyScale = 1.0f;
    // Shared static occupancy (1c): pushed to solver_voxel / solver_hardcontact as `occBox`.
    DebrisShared::ivec4  m_occBox{0, 0, 0, 0};   // xyz = box min chunk, w bit0 = readable
    uint32_t             m_frameSlot = 0;         // frame slot being recorded (descriptor set)
    bool                 m_staticOccWired = false;
    uint32_t             m_probeGeneration = 0;
    uint32_t             m_probeFrame      = 0;   // frame-in-flight slot being recorded
    Core::DebrisSettleAnalyzer m_settle;
    std::vector<float>   m_materialMassCpu;       // CPU mirror of MaterialPhysicsGpu::mass
    bool createProbeBuffers();
    void consumeProbeSlot(uint32_t slot);
    void recordProbeCopy(VkCommandBuffer cmd, uint32_t count, uint32_t tick);

    // ---- Debug timing ring buffer ----
    std::vector<FrameTimingEntry> m_timingRing;
    size_t   m_timingRingHead    = 0;
    uint32_t m_timingFrameCounter = 0;

    // ---- Helpers ----
    bool createBuffers(Vulkan::VulkanDevice* vulkanDevice);
    bool initMatTexTable(Vulkan::VulkanDevice* vulkanDevice);
    bool initMaterialPhysicsTable();
    bool createPipelines(const std::string& shaderDir);
    bool createSolverBuffers(Vulkan::VulkanDevice* vulkanDevice);
    bool createSolverPipelines(const std::string& shaderDir);
    void uploadMatTexTable(Vulkan::VulkanDevice* vulkanDevice, const std::vector<uint32_t>& table);
    void recordComputeCommandsNew(VkCommandBuffer cmd, uint32_t count, float lifetimeDt,
                                  GpuProfiler* profiler, bool instrument);

    static void insertBarrier(VkCommandBuffer cmd,
                              VkPipelineStageFlags src, VkPipelineStageFlags dst,
                              VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                              VkBuffer buffer, VkDeviceSize size = VK_WHOLE_SIZE);
};

} // namespace Phyxel
