#pragma once

#include "core/Types.h"
#include "core/DebrisSettleAnalyzer.h"
#include "vulkan/ComputePipeline.h"
#include "solver_shared.h"   // shaders/: constants + push-constant layouts shared with GLSL (1b)
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <map>
#include <unordered_map>
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
    /** The occupancy box's min chunk (the water directory covers its XZ); false until readable. */
    bool occupancyBoxMinChunk(glm::ivec3& out) const {
        out = glm::ivec3(m_occBox.x, m_occBox.y, m_occBox.z);
        return (m_occBox.w & 1) != 0;
    }
    /** Phase 6c water for the integrate pass (built by DebrisRuntime). `dir` has
     *  WATER_DIR_CHUNKS^2 entries (a tile index or WATER_TILE_NONE) for the window whose min chunk
     *  column is `dirMinChunkXZ`; `cells` holds the tiles (WATER_TILE_CELLS^2 cells of 2 uints:
     *  surface-Y bits, packHalf2x16(flow)). Columns with no tile read the background: `seaLevel`
     *  when `implicitSea`, else dry. Uploaded after the frame slot's fence like the impulses. */
    void setWater(const glm::ivec2& dirMinChunkXZ, bool implicitSea, float seaLevel,
                  std::vector<uint32_t> dir, std::vector<uint32_t> cells);
    uint32_t waterTiles() const { return static_cast<uint32_t>(m_waterCellStage.size() / (2u * 32u * 32u)); }
    bool staticOccupancyWired() const { return m_staticOccWired; }

    // ---- Character collision interface ----

    /** The player's body-part boxes for this frame: each becomes a kinematic (mover) body
     *  carrying `velocity` (Phase 2 - the D7 shove buffer they used to fill is deleted).
     *  Empty disables character collision. Called each frame from Application. */
    void setCharacterColliders(const std::vector<std::pair<glm::vec3, glm::vec3>>& boxes,
                               const glm::vec3& velocity);

    /** Convenience: single-box character collider (used as a fallback when segment
     *  boxes are unavailable). Delegates to setCharacterColliders. */
    void setCharacterAABB(const glm::vec3& center, const glm::vec3& halfExtents, const glm::vec3& velocity);

    /** Disable character collision (no character active). */
    void clearCharacterAABB();

    /** Every character mover for this frame (Phase 3a): oriented limb boxes, each with its own
     *  velocity. Replaces whatever setCharacterColliders/setMoverBoxes set before. The caller
     *  orders them by priority (nearest first): past MAX_KINEMATIC they are counted as overflow. */
    struct MoverBox {
        glm::vec3 center{0.0f};
        glm::vec3 halfExtents{0.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 velocity{0.0f};
        uint32_t  owner = 0;   // Phase 6a: whose push-back this box's contacts feed (0 = nobody)
    };
    void setMoverBoxes(std::vector<MoverBox> movers);
    uint32_t moverCount() const { return static_cast<uint32_t>(m_movers.size()); }
    /** CPU rigid bodies as movers (Phase 3c, DebrisMoverFeed::appendRigidBodies), fed after the
     *  CPU physics step. Staged AFTER the character limbs: the overflow drops bodies first. */
    void setBodyMoverBoxes(std::vector<MoverBox> movers);

    /** Phase 4: push EXISTING debris. Queued now, applied by sync_in on the first GPU tick that
     *  runs (so a frozen/stepped solver applies it on its next step). dv = J * w(d) / m with the
     *  linear falloff phxImpulseWeight, along the radial direction blended toward +Y by upBias,
     *  |dv| <= IMPULSE_MAX_DV. Debris spawned by the same blast is not kicked twice (spawn age 0).
     *  Clamps at entry (reasons in solver_shared.h): radius 0.1..IMPULSE_MAX_RADIUS, impulse >= 0,
     *  upBias 0..1, cone half angle 0..90 deg; past MAX_IMPULSES per frame = counted, dropped.
     *  Returns the values actually queued (`queued` false = dropped). */
    struct ImpulseQueued {
        bool  queued = false;
        float radius = 0.0f, impulse = 0.0f, upBias = 0.0f, halfAngleDeg = 0.0f;
    };
    ImpulseQueued applyRadialImpulse(const glm::vec3& center, float radius, float impulse, float upBias = 0.0f);
    ImpulseQueued applyConeImpulse(const glm::vec3& origin, const glm::vec3& dir, float halfAngleDeg,
                                   float range, float impulse, float upBias = 0.0f);
    /** Wake every SLEEPING body within `radius` of `center` without pushing it (dv = 0). A zero-strength
     *  impulse - the solver wakes sleepers within IMPULSE_WAKE_SCALE x an impulse's radius. Used by water
     *  that moves under resting floaters (docs/WaterCore.md 19.6). Shares the MAX_IMPULSES budget. */
    ImpulseQueued wakeSphere(const glm::vec3& center, float radius);
    /** Phase 6: debris events read back from the GPU (sleep / wake / impact), two frames after the
     *  ticks that produced them. takeEvents() drains what arrived since the last call. A slot is
     *  identified by `slot` + `serial` (the serial changes whenever the slot is (re)spawned, so a
     *  consumer can tell a recycled slot from the body an event described). */
    struct DebrisEvent {
        uint32_t    slot = 0;
        uint32_t    type = 0;            // DebrisShared::DEBRIS_EVENT_*
        uint32_t    materialIndex = 0;   // incl. the texture-slice bits
        float       speed = 0.0f;        // impact |dv| (m/s), 0 otherwise
        glm::vec3   position{0.0f};
        float       scale = 1.0f;        // largest axis (1, 1/3, 1/9 ...)
        uint32_t    serial = 0;          // slotSerial(slot) when the event was READ
    };
    std::vector<DebrisEvent> takeEvents() { std::vector<DebrisEvent> out; out.swap(m_events); return out; }
    /** WaterCore E2: the water's share of wet bodies' drag + current since the last call (two frames
     *  late, like events). mass = the body's mass in water units (m^3); the water receives -mass*dv. */
    struct WaterExchange { glm::vec3 position{0.0f}; float mass = 0.0f; glm::vec3 dv{0.0f}; float radius = 0.0f; };
    std::vector<WaterExchange> takeWaterExchange() { std::vector<WaterExchange> out; out.swap(m_waterExchange); return out; }
    uint64_t waterExchangeTotal() const { return m_exchangeTotal; }
    uint64_t waterExchangeDropped() const { return m_exchangeDropped; }
    /// WaterCore 20 (moving solids, docs/WaterCore.md 20.4): the water volumes' world boxes (min, max). Up to
    /// MAX_WATER_BOXES are passed to the integrator; more are counted (their bodies displace nothing).
    void setWaterVolumeBoxes(const std::vector<std::pair<glm::vec3, glm::vec3>>& boxes);
    /// The bodies inside those boxes, one per body (its last tick), as of the frame whose slot was last read -
    /// `ageSeconds` old (two frames: MAX_FRAMES_IN_FLIGHT). A frame with no physics tick keeps the previous
    /// list (no records is not "no bodies" - read naively, every body would vanish for a frame). `fresh` = the
    /// body (slot + serial) was not in the previous list.
    struct WetBody { glm::vec3 centre{0.0f}, halfExtents{0.0f}, velocity{0.0f}; uint32_t slot = 0; uint32_t serial = 0; float ageSeconds = 0.0f; bool fresh = false; };
    const std::vector<WetBody>& wetBodies() const { return m_wetBodies; }
    struct WetStats { uint64_t recordsTotal = 0, recordsDropped = 0; uint32_t boxes = 0, boxesDropped = 0; };
    const WetStats& wetStats() const { return m_wetStats; }
    /** Phase 6a: per owner tag, the impulse (N*s) debris contacts put on its mover boxes, summed
     *  over the frames read back since the last call (two frames late). Normal force only. */
    std::unordered_map<uint32_t, glm::vec3> takeMoverImpulses() {
        std::unordered_map<uint32_t, glm::vec3> out; out.swap(m_moverImpulses); return out;
    }
    uint64_t eventsTotal()   const { return m_eventsTotal; }
    uint64_t eventsDropped() const { return m_eventsDropped; }
    /** Bumped every time `slot` is spawned into (0 = never used). */
    uint32_t slotSerial(uint32_t slot) const { return slot < m_slotSerial.size() ? m_slotSerial[slot] : 0u; }
    bool     slotActive(uint32_t slot) const { return slot < m_slots.size() && m_slots[slot].active; }
    /** Remove one body (gathered rubble): it retires on the next update(), exactly like a lifetime
     *  expiry (GPU ACTIVE flag cleared, slot freed). False if the slot is not active. */
    bool     despawnSlot(uint32_t slot);
    /** Material name of a GPU material index (the low MATERIAL_MASK bits). */
    static std::string materialNameOf(uint32_t materialIndex);

    uint32_t pendingImpulses() const { return static_cast<uint32_t>(m_impulseStage.size()); }
    uint32_t impulseOverflow() const { return m_impulseOverflow; }
    uint64_t impulsesSubmitted() const { return m_impulsesSubmitted; }   // reached a GPU tick
    uint32_t bodyMoverCount() const { return static_cast<uint32_t>(m_bodyMovers.size()); }
    /** Doors, animated template parts and held items (Phase 3b, KinematicVoxelManager), staged
     *  after the character limbs and before the CPU bodies. */
    void setObjectMoverBoxes(std::vector<MoverBox> movers);
    uint32_t objectMoverCount() const { return static_cast<uint32_t>(m_objectMovers.size()); }
    /** Kinematic slots left for objects once the scripted boxes and character limbs are staged. */
    uint32_t objectMoverBudget() const {
        const size_t used = m_kinematicBoxesFrameStart.size() + m_movers.size();
        return used >= DebrisShared::MAX_KINEMATIC ? 0u : static_cast<uint32_t>(DebrisShared::MAX_KINEMATIC - used);
    }
    /** Kinematic slots left for bodies once the scripted boxes and character limbs are staged. */
    uint32_t bodyMoverBudget() const {
        const size_t used = m_kinematicBoxesFrameStart.size() + m_movers.size() + m_objectMovers.size();
        return used >= DebrisShared::MAX_KINEMATIC ? 0u : static_cast<uint32_t>(DebrisShared::MAX_KINEMATIC - used);
    }

    /** Scripted kinematic test box (DebrisInteractionPlan 1f): a mover the solver tests can drive
     *  without NPC AI. It advances by SIMULATED time (ticks x FIXED_DT, so a frozen, stepped solver
     *  moves it deterministically) and expires after `ttl` seconds. It is a kinematic AVBD body
     *  (Phase 2): axis-aligned for now, its own velocity, at most MAX_KINEMATIC movers in total
     *  with the player's boxes (the rest are counted as overflow). */
    struct KinematicBox {
        glm::vec3 center{0.0f};
        glm::vec3 half{0.5f};
        glm::vec3 velocity{0.0f};
        float     ttl = 5.0f;   // seconds left, <= 10
    };
    void setKinematicBox(const std::string& id, const KinematicBox& box);
    bool removeKinematicBox(const std::string& id);
    const std::map<std::string, KinematicBox>& kinematicBoxes() const { return m_kinematicBoxes; }
    uint32_t kinematicOverflow() const { return m_kinematicOverflow; }

    // GPU-side per-material physics (32 bytes, std430).
    // Populated at init from MaterialRegistry, indexed by GpuParticle::materialIndex.
    struct alignas(4) MaterialPhysicsGpu {
        float mass;            // Gravity scaling
        float restitution;     // Bounciness
        float friction;        // Surface grip
        float linearDamp;      // Per-frame velocity damping
        float angularDamp;     // Per-frame spin damping
        float breakForceScale; // Break impulse multiplier
        float buoyancy;        // water density / material density (Phase 6c; > 1 floats)
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


    // Kinematic (mover) boxes for the AVBD solver, Phase 2 - host-coherent, persistently mapped,
    // KinematicBoxGpu[MAX_KINEMATIC]. solver_kinematic_sync turns entry k into SolverBody
    // KINEMATIC_BASE + k each tick. Filled by writeColliderBuffer (player boxes + scripted boxes).
    // ONE BUFFER PER FRAME SLOT: the CPU writes slot s only in recordComputeCommands, after that
    // slot's fence (a single buffer rewritten in update() was read by the previous frame's
    // still-running ticks - the box jumped a whole frame ahead, 4 x 33 mm, frames-in-flight).
    VkBuffer         m_kinematicBoxBuffer[OCC_FRAME_SLOTS] = {};
    VkDeviceMemory   m_kinematicBoxMem[OCC_FRAME_SLOTS]    = {};
    void*            m_kinematicBoxMapped[OCC_FRAME_SLOTS] = {};
    std::vector<DebrisShared::KinematicBoxGpu> m_kinematicStage;   // this frame's frame-start poses
    // Phase 4 impulses: queued here, copied into the frame slot's buffer by the first frame whose
    // ticks run (recordComputeCommands), applied by sync_in on that frame's tick 0.
    VkBuffer         m_impulseBuffer[OCC_FRAME_SLOTS] = {};
    VkDeviceMemory   m_impulseMem[OCC_FRAME_SLOTS]    = {};
    void*            m_impulseMapped[OCC_FRAME_SLOTS] = {};
    std::vector<DebrisShared::ImpulseGpu> m_impulseStage;
    // Phase 6c water: per frame slot, host-visible (directory + tile pool), read by integrate.
    VkBuffer         m_waterDirBuffer[OCC_FRAME_SLOTS]  = {};
    VkDeviceMemory   m_waterDirMem[OCC_FRAME_SLOTS]     = {};
    void*            m_waterDirMapped[OCC_FRAME_SLOTS]  = {};
    VkBuffer         m_waterTileBuffer[OCC_FRAME_SLOTS] = {};
    VkDeviceMemory   m_waterTileMem[OCC_FRAME_SLOTS]    = {};
    void*            m_waterTileMapped[OCC_FRAME_SLOTS] = {};
    std::vector<uint32_t> m_waterDirStage;    // empty = no water (all columns background)
    std::vector<uint32_t> m_waterCellStage;
    DebrisShared::ivec4   m_waterPC{0, 0, 0, 0};
    bool createHostBuffer(VkDeviceSize size, VkBuffer& buf, VkDeviceMemory& mem, void*& mapped, const char* what);
    // Phase 6 event readback: per frame slot, host-visible, written by sync_in / sync_out.
    VkBuffer         m_eventBuffer[OCC_FRAME_SLOTS] = {};
    VkDeviceMemory   m_eventMem[OCC_FRAME_SLOTS]    = {};
    void*            m_eventMapped[OCC_FRAME_SLOTS] = {};
    bool             m_eventsWritten[OCC_FRAME_SLOTS] = {};   // ticks ran into this slot; read after its fence
    std::vector<DebrisEvent> m_events;
    // WaterCore E2: the water-exchange readback, per frame slot (written by solver_integrate)
    VkBuffer         m_exchangeBuffer[OCC_FRAME_SLOTS] = {};
    VkDeviceMemory   m_exchangeMem[OCC_FRAME_SLOTS]    = {};
    void*            m_exchangeMapped[OCC_FRAME_SLOTS] = {};
    std::vector<WaterExchange> m_waterExchange;
    VkBuffer         m_wetBuffer[OCC_FRAME_SLOTS] = {};   // WaterCore 20: boxes + wet-body records, per frame slot
    VkDeviceMemory   m_wetMem[OCC_FRAME_SLOTS]    = {};
    void*            m_wetMapped[OCC_FRAME_SLOTS] = {};
    bool             m_wetWritten[OCC_FRAME_SLOTS] = {};
    double           m_wetStampSec[OCC_FRAME_SLOTS] = {};
    std::vector<std::pair<glm::vec3, glm::vec3>> m_waterBoxes;
    std::vector<WetBody> m_wetBodies;
    WetStats         m_wetStats;
    uint64_t         m_exchangeTotal = 0, m_exchangeDropped = 0;
    // Phase 6a push-back readback: per frame slot, with the owner tag of every kinematic body as
    // staged for that slot (the order changes every frame).
    VkBuffer         m_pushBuffer[OCC_FRAME_SLOTS] = {};
    VkDeviceMemory   m_pushMem[OCC_FRAME_SLOTS]    = {};
    void*            m_pushMapped[OCC_FRAME_SLOTS] = {};
    std::vector<uint32_t> m_pushOwners[OCC_FRAME_SLOTS];
    std::vector<uint32_t> m_kinematicOwnerStage;    // parallel to m_kinematicStage
    std::unordered_map<uint32_t, glm::vec3> m_moverImpulses;
    uint64_t         m_eventsTotal = 0, m_eventsDropped = 0;
    std::vector<uint32_t> m_slotSerial;
    void consumeEventSlot(uint32_t slot);
    // Position-log readback: one buffer, so one copy in flight; read only after ITS slot's fence.
    uint32_t         m_readbackSlot  = 0;
    bool             m_readbackReady = false;
    uint32_t         m_impulseCountThisFrame = 0;
    uint32_t         m_impulseOverflow       = 0;   // dropped past MAX_IMPULSES (lifetime count)
    uint64_t         m_impulsesSubmitted     = 0;
    ImpulseQueued    queueImpulse(const glm::vec3& c, float radius, const glm::vec3& axis, float cosHalf,
                                  float impulse, float upBias, float halfAngleDeg, bool wakeOnly = false);
    uint32_t         m_kinematicCount     = 0;

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
    Vulkan::ComputePipeline m_solverKinematicSyncPass;   // movers -> SolverBody (Phase 2)
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
    // Collider feed: the player's boxes (setCharacterColliders) + the scripted boxes (1f), merged
    // into the collider buffer by writeColliderBuffer().
    std::vector<MoverBox> m_movers;   // character limbs (setMoverBoxes / setCharacterColliders)
    std::vector<MoverBox> m_objectMovers; // doors / animated parts / held items (setObjectMoverBoxes, 3b)
    std::vector<MoverBox> m_bodyMovers;   // CPU rigid-body boxes (setBodyMoverBoxes, Phase 3c)
    // Scripted boxes at the START of this frame: the feed can arrive after update() has advanced
    // m_kinematicBoxes to the frame-end pose, and the GPU ticks start from the frame-start pose.
    std::map<std::string, KinematicBox> m_kinematicBoxesFrameStart;
    std::map<std::string, KinematicBox> m_kinematicBoxes;
    uint32_t m_kinematicOverflow = 0;   // movers past MAX_KINEMATIC (last write), counted not dropped silently
    void writeColliderBuffer();
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
    static_assert(FIXED_DT == DebrisShared::SOLVER_TICK_DT, "the shaders' tick length (6a push cap) must match");
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
    static constexpr uint32_t PROBE_HDR_UINTS  = DebrisShared::HASH_BASE;  // the whole solver-state header
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
                                  GpuProfiler* profiler, bool instrument, uint32_t tick);

    static void insertBarrier(VkCommandBuffer cmd,
                              VkPipelineStageFlags src, VkPipelineStageFlags dst,
                              VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                              VkBuffer buffer, VkDeviceSize size = VK_WHOLE_SIZE);
};

} // namespace Phyxel
