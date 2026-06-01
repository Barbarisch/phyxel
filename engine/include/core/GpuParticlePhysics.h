#pragma once

#include "core/Types.h"
#include "vulkan/ComputePipeline.h"
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <string>
#include <cstdint>
#include <fstream>
#include <utility>

namespace Phyxel {

namespace Vulkan { class VulkanDevice; }
class GpuProfiler;

/**
 * GpuParticlePhysics — GPU-accelerated voxel debris physics.
 *
 * Replaces Bullet btRigidBody + CPU face rebuild for all broken-voxel debris
 * (dynamic cubes, subcubes, microcubes). Characters and interactive objects
 * remain on Bullet.
 *
 * Per-frame pipeline (all on GPU, no CPU readback):
 *   1. particle_integrate.comp  — XPBD position + quaternion angular step
 *   2. particle_collide.comp    — 3D occupancy-grid voxel collision
 *   3. particle_expand.comp     — write 6 DynamicSubcubeInstanceData faces per active particle
 *   4. dynamic_voxel.vert/frag  — render directly from face buffer (unchanged shaders)
 */
class GpuParticlePhysics {
public:
    static constexpr uint32_t MAX_PARTICLES  = 10000;
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

    // ---- 3D occupancy grid interface (called by ChunkManager) ----

    /** Set a single voxel's occupancy bit in the GPU grid. Host-coherent — auto-visible to GPU. */
    void setOccupied(int worldX, int worldY, int worldZ, bool solid);

    /** Clear entire occupancy grid (all voxels empty). */
    void clearOccupancy();

    /** Rebuild all occupancy data from scratch (called after initial chunk load). */
    // (ChunkManager calls setOccupied per-voxel during rebuildOccupancyFromChunks)

    // ---- Character collision interface ----

    // Max per-limb segment boxes uploaded per frame. Must cover the character's full
    // segment set — 12 boxes (4 torso + 4 arm + 4 leg). Setting this too low silently
    // drops the trailing boxes (e.g. the legs), leaving floor-height debris uncovered.
    // Keep in sync with charSeg[] in solver_integrate.comp.
    static constexpr uint32_t MAX_CHAR_SEGMENTS = 12;

    // One body-part box (std430: two vec4s = 32 bytes).
    struct CharSegmentGpu {
        glm::vec4 center;       // xyz = world center
        glm::vec4 halfExtents;  // xyz = world half-extents
    };
    static_assert(sizeof(CharSegmentGpu) == 32, "CharSegmentGpu must be 32 bytes");

    // std430 layout uploaded each frame for particle-vs-character collision.
    // The first 48 bytes are the broadphase union AABB + velocity, byte-compatible
    // with the original single-AABB layout (so the dead legacy particle_collide.comp
    // still reads sane values). The live AVBD solver (solver_integrate.comp) uses the
    // union for a cheap early-out, then tests the per-limb segments[].
    struct CharacterCollider {
        glm::vec3 center;       // union AABB center (broadphase)
        float     segmentCount; // number of active segments (0 = disabled)
        glm::vec3 halfExtents;  // union AABB half-extents
        float     pad0;
        glm::vec3 velocity;     // character velocity (imparted to pushed debris)
        float     legacyActive; // mirrors (segmentCount>0) for the legacy shader's charActive
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

    /** Switch between constraint solver (new, default) and legacy XPBD pipeline. */
    void setUseNewPipeline(bool use) { m_useNewPipeline = use; }
    bool getUseNewPipeline() const   { return m_useNewPipeline; }

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

    // ---- Material name → index lookup ----
    static uint32_t materialNameToIndex(const std::string& name);

private:
    // 3D occupancy grid constants — must match particle_collide.comp
    static constexpr int OCC_X        = 512;
    static constexpr int OCC_Y        = 256;
    static constexpr int OCC_Z        = 512;
    static constexpr int OCC_HALF_X   = 256;   // world X offset
    static constexpr int OCC_Y_OFFSET = 64;    // world Y offset (Y range: -64..+191)
    static constexpr int OCC_HALF_Z   = 256;   // world Z offset
    static constexpr int OCC_TOTAL_BITS  = OCC_X * OCC_Y * OCC_Z;        // 67,108,864 bits
    static constexpr int OCC_TOTAL_WORDS = OCC_TOTAL_BITS / 32;          // 2,097,152 uint32s

    // Physics constants
    static constexpr float GRAVITY             = -9.81f;  // match Bullet physics
    static constexpr float SLEEP_THRESH_SQ     = 5e-4f;   // settle faster
    static constexpr int   COLLISION_ITERATIONS = 3;       // constraint solver passes per tick

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

    // 3D occupancy bitfield — host-coherent, persistently mapped, uint32[OCC_TOTAL_WORDS]
    VkBuffer         m_occupancyBuffer = VK_NULL_HANDLE;
    VkDeviceMemory   m_occupancyMem    = VK_NULL_HANDLE;
    void*            m_occupancyMapped = nullptr;

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
    static constexpr int    GRID_SIZE  = 64;
    static constexpr int    GRID_CELLS = GRID_SIZE * GRID_SIZE * GRID_SIZE; // 262,144
    VkBuffer         m_gridCellCountBuffer  = VK_NULL_HANDLE;  // uint[GRID_CELLS] — particles per cell
    VkDeviceMemory   m_gridCellCountMem     = VK_NULL_HANDLE;
    VkBuffer         m_gridCellOffsetBuffer = VK_NULL_HANDLE;  // uint[GRID_CELLS] — END of each cell's sorted range
    VkDeviceMemory   m_gridCellOffsetMem    = VK_NULL_HANDLE;
    VkBuffer         m_sortedParticleBuffer = VK_NULL_HANDLE;  // GpuParticle[MAX_PARTICLES] — sorted by cell
    VkDeviceMemory   m_sortedParticleMem    = VK_NULL_HANDLE;
    VkBuffer         m_sortedIndexBuffer    = VK_NULL_HANDLE;  // uint[MAX_PARTICLES] — canonical index per sorted slot
    VkDeviceMemory   m_sortedIndexMem       = VK_NULL_HANDLE;

    // ---- Compute pipelines (legacy XPBD) ----
    Vulkan::ComputePipeline m_gridClearPass;
    Vulkan::ComputePipeline m_gridBuildPass;
    Vulkan::ComputePipeline m_sortScanPass;
    Vulkan::ComputePipeline m_sortScatterPass;
    Vulkan::ComputePipeline m_integratePass;
    Vulkan::ComputePipeline m_collidePass;
    Vulkan::ComputePipeline m_expandPass;

    // ---- Constraint-based solver pipeline (the live pipeline) ----
    // Pipeline switch: true = AVBD constraint solver (solver_*.comp), false =
    // legacy XPBD (particle_integrate/collide.comp). Hardcoded true and never
    // toggled off anywhere — the legacy path is dead code kept for reference.
    // All particle physics (gravity, voxel + character collision) lives in the
    // solver_*.comp shaders; do NOT add new behaviour to the legacy shaders.
    bool m_useNewPipeline = true;

    static constexpr uint32_t MAX_CONSTRAINTS = 60000;
    static constexpr uint32_t MAX_COLORS      = 12;
    static constexpr int      SOLVE_ITERATIONS = 8;

    // Warmstart hash table sizing (must match shaders/solver_types.glsl).
    // HASH_CAP must be a power of two and >= ~2 * MAX_CONSTRAINTS.
    static constexpr uint32_t HASH_CAP        = 131072;
    static constexpr uint32_t HASH_BASE       = 8;
    static constexpr uint32_t SOLVER_STATE_UINTS = HASH_BASE + HASH_CAP;

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
    std::vector<uint32_t> m_freeSlots;     // freelist of inactive particle indices
    uint32_t              m_activeCount = 0;
    uint32_t              m_highWaterSlot = 0; // highest active slot index + 1 (dispatch range)

    struct PendingCopy {
        uint32_t slotIndex;    // particle slot to overwrite
        GpuParticle data;
    };
    std::vector<PendingCopy> m_pendingSpawns;
    std::vector<PendingCopy> m_pendingThisFrame; // snapshot used during recordCompute

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
