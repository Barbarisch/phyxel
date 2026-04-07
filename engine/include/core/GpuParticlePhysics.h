#pragma once

#include "core/Types.h"
#include "vulkan/ComputePipeline.h"
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <string>
#include <cstdint>

namespace Phyxel {

namespace Vulkan { class VulkanDevice; }

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
        float       lifetime    = 8.0f;
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
    void recordComputeCommands(VkCommandBuffer cmd, uint32_t frameIndex);

    // ---- 3D occupancy grid interface (called by ChunkManager) ----

    /** Set a single voxel's occupancy bit in the GPU grid. Host-coherent — auto-visible to GPU. */
    void setOccupied(int worldX, int worldY, int worldZ, bool solid);

    /** Clear entire occupancy grid (all voxels empty). */
    void clearOccupancy();

    /** Rebuild all occupancy data from scratch (called after initial chunk load). */
    // (ChunkManager calls setOccupied per-voxel during rebuildOccupancyFromChunks)

    // ---- Character collision interface ----

    // std430 layout (48 bytes) — uploaded each frame for particle-vs-character AABB collision.
    struct CharacterCollider {
        glm::vec3 center;      // World-space AABB center
        float     pad0;
        glm::vec3 halfExtents; // Half-size on each axis
        float     pad1;
        glm::vec3 velocity;    // Character velocity (transferred to particles on hit)
        float     active;      // 1.0 = enabled, 0.0 = disabled (no character spawned)
    };
    static_assert(sizeof(CharacterCollider) == 48, "CharacterCollider must be 48 bytes");

    /** Update character AABB for particle collision. Called each frame from Application. */
    void setCharacterAABB(const glm::vec3& center, const glm::vec3& halfExtents, const glm::vec3& velocity);

    /** Disable character collision (no character active). */
    void clearCharacterAABB();

    // GPU-side per-material physics (32 bytes, std430).
    // Populated at init from MaterialManager, indexed by GpuParticle::materialIndex.
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
    static constexpr float GRAVITY         = -9.81f;
    static constexpr float SLEEP_THRESH_SQ = 1e-5f; // per-frame velocity²

    // Material names in index order (index 0 = Default)
    static const std::vector<std::string> MATERIAL_NAMES;

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

    // Spatial hash grid for inter-particle collision
    static constexpr int    GRID_SIZE  = 64;
    static constexpr int    GRID_CELLS = GRID_SIZE * GRID_SIZE * GRID_SIZE; // 262,144
    VkBuffer         m_gridCellHeadBuffer  = VK_NULL_HANDLE;  // uint[GRID_CELLS] — per-cell head pointer
    VkDeviceMemory   m_gridCellHeadMem     = VK_NULL_HANDLE;
    VkBuffer         m_gridNextBuffer      = VK_NULL_HANDLE;  // uint[MAX_PARTICLES] — per-particle next link
    VkDeviceMemory   m_gridNextMem         = VK_NULL_HANDLE;

    // ---- Compute pipelines ----
    Vulkan::ComputePipeline m_gridClearPass;
    Vulkan::ComputePipeline m_gridBuildPass;
    Vulkan::ComputePipeline m_integratePass;
    Vulkan::ComputePipeline m_collidePass;
    Vulkan::ComputePipeline m_expandPass;

    // ---- CPU-side state ----
    struct SlotInfo {
        float lifetimeRemaining = 0.0f;
        bool  active            = false;
    };
    std::vector<SlotInfo> m_slots;         // per-particle CPU tracking
    std::vector<uint32_t> m_freeSlots;     // freelist of inactive particle indices
    uint32_t              m_activeCount = 0;

    struct PendingCopy {
        uint32_t slotIndex;    // particle slot to overwrite
        GpuParticle data;
    };
    std::vector<PendingCopy> m_pendingSpawns;
    std::vector<PendingCopy> m_pendingThisFrame; // snapshot used during recordCompute

    bool m_initialized = false;

    // ---- Helpers ----
    bool createBuffers(Vulkan::VulkanDevice* vulkanDevice);
    bool initMatTexTable(Vulkan::VulkanDevice* vulkanDevice);
    bool initMaterialPhysicsTable();
    bool createPipelines(const std::string& shaderDir);
    void uploadMatTexTable(Vulkan::VulkanDevice* vulkanDevice, const std::vector<uint32_t>& table);

    static void insertBarrier(VkCommandBuffer cmd,
                              VkPipelineStageFlags src, VkPipelineStageFlags dst,
                              VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                              VkBuffer buffer, VkDeviceSize size = VK_WHOLE_SIZE);
};

} // namespace Phyxel
