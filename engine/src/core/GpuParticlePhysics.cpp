#include "core/GpuParticlePhysics.h"
#include "vulkan/VulkanDevice.h"
#include "core/AssetManager.h"
#include "utils/Logger.h"
#include <glm/gtc/quaternion.hpp>
#include <cstring>
#include <algorithm>
#include <stdexcept>
#include <random>

namespace Phyxel {

// ============================================================
// Static data
// ============================================================

// Maps GPU materialIndex → TextureConstants material ID (same value — we reuse the ID directly)
const std::vector<std::string> GpuParticlePhysics::MATERIAL_NAMES = {
    "placeholder", "grassdirt", "Cork", "Default",
    "Glass", "glow", "hover", "Ice",
    "Metal", "Rubber", "Stone", "Wood"
};

uint32_t GpuParticlePhysics::materialNameToIndex(const std::string& name) {
    for (uint32_t i = 0; i < static_cast<uint32_t>(MATERIAL_NAMES.size()); ++i) {
        if (MATERIAL_NAMES[i] == name) return i;
    }
    return 3u; // "Default" fallback
}

// ============================================================
// Construction / Destruction
// ============================================================

GpuParticlePhysics::GpuParticlePhysics() {
    m_slots.resize(MAX_PARTICLES);
    m_freeSlots.reserve(MAX_PARTICLES);
    for (uint32_t i = MAX_PARTICLES; i > 0; --i) {
        m_freeSlots.push_back(i - 1); // push in reverse so slot 0 is popped first
    }
}

GpuParticlePhysics::~GpuParticlePhysics() {
    cleanup();
}

// ============================================================
// Initialize
// ============================================================

bool GpuParticlePhysics::initialize(Vulkan::VulkanDevice* vulkanDevice, const std::string& /*shaderDir*/) {
    m_device    = vulkanDevice->getDevice();
    m_physDevice = vulkanDevice->getPhysicalDevice();

    if (!createBuffers(vulkanDevice))             return false;
    if (!initMatTexTable(vulkanDevice))           return false;
    if (!createPipelines(std::string()))          return false; // uses AssetManager internally

    // Initialize VkDrawIndirectCommand: {vertexCount=6, instanceCount=0, firstVertex=0, firstInstance=0}
    //
    // vertexCount=6: each face instance draws 6 vertices (2 triangles in TRIANGLE_LIST).
    // The vertex shader remaps IDs 0-5 to 4 quad corners via cornerRemap[].
    // instanceCount is zeroed each frame (offset 4, size 4) then atomically
    // incremented by particle_expand.comp. vertexCount at offset 0 is NEVER
    // modified after this init — only instanceCount is touched per-frame.
    //
    // If you change topology or vertices-per-face, update:
    //   1. This vertexCount value
    //   2. cornerRemap[] in dynamic_voxel.vert
    //   3. The vkCmdFillBuffer call that zeros instanceCount (offset/size)
    {
        struct IndirectCmd { uint32_t v, i, fv, fi; } initCmd = {6, 0, 0, 0};
        VkBuffer       stageBuf  = VK_NULL_HANDLE;
        VkDeviceMemory stageMem  = VK_NULL_HANDLE;
        void*          stageMapped = nullptr;
        vulkanDevice->createPersistentStagingBuffer(sizeof(initCmd), stageBuf, stageMem, &stageMapped);
        memcpy(stageMapped, &initCmd, sizeof(initCmd));
        vkUnmapMemory(m_device, stageMem);

        VkCommandBuffer cmd = vulkanDevice->beginSingleTimeCommands();
        VkBufferCopy region{0, 0, sizeof(initCmd)};
        vkCmdCopyBuffer(cmd, stageBuf, m_indirectDrawBuffer, 1, &region);
        vulkanDevice->endSingleTimeCommands(cmd);

        vkDestroyBuffer(m_device, stageBuf, nullptr);
        vkFreeMemory(m_device, stageMem, nullptr);
    }

    // Initialize occupancy grid: all zeros (empty world)
    memset(m_occupancyMapped, 0, static_cast<size_t>(OCC_TOTAL_WORDS) * sizeof(uint32_t));

    // Initialize character collider: disabled (active = 0)
    memset(m_characterMapped, 0, sizeof(CharacterCollider));

    m_initialized = true;
    LOG_INFO_FMT("GpuParticlePhysics", "Initialized: MAX_PARTICLES=" << MAX_PARTICLES
        << " particleBuffer=" << (MAX_PARTICLES * sizeof(GpuParticle) / 1024) << "KB"
        << " faceBuffer=" << (MAX_FACE_SLOTS * 64 / 1024) << "KB");
    return true;
}

// ============================================================
// Buffer creation
// ============================================================

bool GpuParticlePhysics::createBuffers(Vulkan::VulkanDevice* dev) {
    // 1. Particle SSBO (device-local)
    dev->createStorageBuffer(
        static_cast<VkDeviceSize>(MAX_PARTICLES) * sizeof(GpuParticle),
        m_particleBuffer, m_particleMem);

    // 2. Face output buffer (device-local, also bound as vertex buffer)
    dev->createStorageBuffer(
        static_cast<VkDeviceSize>(MAX_FACE_SLOTS) * 64, // 64 bytes per DynamicSubcubeInstanceData
        m_faceBuffer, m_faceMem,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

    // 3. Staging buffer for particle spawns (host-coherent, persistent map)
    dev->createPersistentStagingBuffer(
        static_cast<VkDeviceSize>(MAX_PARTICLES) * sizeof(GpuParticle),
        m_stagingBuffer, m_stagingMem, &m_stagingMapped);

    // 4. Indirect draw command buffer (device-local, indirect usage)
    {
        VkBufferCreateInfo bi{};
        bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size        = 16; // sizeof(VkDrawIndirectCommand)
        bi.usage       = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT
                       | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
                       | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(m_device, &bi, nullptr, &m_indirectDrawBuffer) != VK_SUCCESS) {
            LOG_ERROR("GpuParticlePhysics", "Failed to create indirect draw buffer");
            return false;
        }
        VkMemoryRequirements req;
        vkGetBufferMemoryRequirements(m_device, m_indirectDrawBuffer, &req);
        VkMemoryAllocateInfo ai{};
        ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize  = req.size;
        // Try device-local first, fall back to host-visible if needed
        uint32_t memType = UINT32_MAX;
        VkPhysicalDeviceMemoryProperties props;
        vkGetPhysicalDeviceMemoryProperties(m_physDevice, &props);
        for (uint32_t j = 0; j < props.memoryTypeCount; ++j) {
            if ((req.memoryTypeBits & (1u << j)) &&
                (props.memoryTypes[j].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
                memType = j; break;
            }
        }
        if (memType == UINT32_MAX) {
            LOG_ERROR("GpuParticlePhysics", "No device-local memory for indirect buffer");
            return false;
        }
        ai.memoryTypeIndex = memType;
        if (vkAllocateMemory(m_device, &ai, nullptr, &m_indirectDrawMem) != VK_SUCCESS ||
            vkBindBufferMemory(m_device, m_indirectDrawBuffer, m_indirectDrawMem, 0) != VK_SUCCESS) {
            LOG_ERROR("GpuParticlePhysics", "Failed to allocate/bind indirect draw memory");
            return false;
        }
    }

    // 5. 3D occupancy bitfield (host-coherent, persistent map)
    //    512×256×512 bits = 2,097,152 uint32 words = 8 MB
    {
        VkDeviceSize occSize = static_cast<VkDeviceSize>(OCC_TOTAL_WORDS) * sizeof(uint32_t);
        VkBufferCreateInfo bi{};
        bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size        = occSize;
        bi.usage       = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(m_device, &bi, nullptr, &m_occupancyBuffer) != VK_SUCCESS) {
            LOG_ERROR("GpuParticlePhysics", "Failed to create occupancy buffer");
            return false;
        }
        VkMemoryRequirements req;
        vkGetBufferMemoryRequirements(m_device, m_occupancyBuffer, &req);
        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = req.size;
        VkPhysicalDeviceMemoryProperties props;
        vkGetPhysicalDeviceMemoryProperties(m_physDevice, &props);
        uint32_t memType = UINT32_MAX;
        for (uint32_t j = 0; j < props.memoryTypeCount; ++j) {
            if ((req.memoryTypeBits & (1u << j)) &&
                ((props.memoryTypes[j].propertyFlags &
                  (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
                  (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))) {
                memType = j; break;
            }
        }
        if (memType == UINT32_MAX) {
            LOG_ERROR("GpuParticlePhysics", "No host-coherent memory for occupancy buffer");
            return false;
        }
        ai.memoryTypeIndex = memType;
        if (vkAllocateMemory(m_device, &ai, nullptr, &m_occupancyMem) != VK_SUCCESS ||
            vkBindBufferMemory(m_device, m_occupancyBuffer, m_occupancyMem, 0) != VK_SUCCESS ||
            vkMapMemory(m_device, m_occupancyMem, 0, occSize, 0, &m_occupancyMapped) != VK_SUCCESS) {
            LOG_ERROR("GpuParticlePhysics", "Failed to create/map occupancy buffer");
            return false;
        }
    }

    // 6. Character collider AABB (host-coherent SSBO, persistent map, 48 bytes)
    {
        VkDeviceSize charSize = static_cast<VkDeviceSize>(sizeof(CharacterCollider));
        VkBufferCreateInfo bi{};
        bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size        = charSize;
        bi.usage       = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(m_device, &bi, nullptr, &m_characterBuffer) != VK_SUCCESS) {
            LOG_ERROR("GpuParticlePhysics", "Failed to create character buffer");
            return false;
        }
        VkMemoryRequirements req;
        vkGetBufferMemoryRequirements(m_device, m_characterBuffer, &req);
        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = req.size;
        VkPhysicalDeviceMemoryProperties props;
        vkGetPhysicalDeviceMemoryProperties(m_physDevice, &props);
        uint32_t memType = UINT32_MAX;
        for (uint32_t j = 0; j < props.memoryTypeCount; ++j) {
            if ((req.memoryTypeBits & (1u << j)) &&
                ((props.memoryTypes[j].propertyFlags &
                  (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
                  (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))) {
                memType = j; break;
            }
        }
        if (memType == UINT32_MAX) {
            LOG_ERROR("GpuParticlePhysics", "No host-coherent memory for character buffer");
            return false;
        }
        ai.memoryTypeIndex = memType;
        if (vkAllocateMemory(m_device, &ai, nullptr, &m_characterMem) != VK_SUCCESS ||
            vkBindBufferMemory(m_device, m_characterBuffer, m_characterMem, 0) != VK_SUCCESS ||
            vkMapMemory(m_device, m_characterMem, 0, charSize, 0, &m_characterMapped) != VK_SUCCESS) {
            LOG_ERROR("GpuParticlePhysics", "Failed to create/map character buffer");
            return false;
        }
        // Initialize to inactive
        std::memset(m_characterMapped, 0, sizeof(CharacterCollider));
    }

    return true;
}

// ============================================================
// Material texture table
// ============================================================

bool GpuParticlePhysics::initMatTexTable(Vulkan::VulkanDevice* dev) {
    using namespace TextureConstants;
    const uint32_t tableSize = MATERIAL_COUNT * 6;
    std::vector<uint32_t> table(tableSize);

    for (int mat = 0; mat < MATERIAL_COUNT; ++mat) {
        for (int face = 0; face < 6; ++face) {
            table[mat * 6 + face] = static_cast<uint32_t>(MATERIAL_FACE_INDEX[mat][face]);
        }
    }

    // Create a device-local SSBO and upload via single-time command
    dev->createStorageBuffer(
        static_cast<VkDeviceSize>(tableSize) * sizeof(uint32_t),
        m_matTexBuffer, m_matTexMem);

    uploadMatTexTable(dev, table);
    return true;
}

void GpuParticlePhysics::uploadMatTexTable(Vulkan::VulkanDevice* dev, const std::vector<uint32_t>& table) {
    VkDeviceSize size = static_cast<VkDeviceSize>(table.size()) * sizeof(uint32_t);
    VkBuffer       stageBuf  = VK_NULL_HANDLE;
    VkDeviceMemory stageMem  = VK_NULL_HANDLE;
    void*          stageMapped = nullptr;
    dev->createPersistentStagingBuffer(size, stageBuf, stageMem, &stageMapped);
    memcpy(stageMapped, table.data(), static_cast<size_t>(size));
    vkUnmapMemory(m_device, stageMem);

    VkCommandBuffer cmd = dev->beginSingleTimeCommands();
    VkBufferCopy region{0, 0, size};
    vkCmdCopyBuffer(cmd, stageBuf, m_matTexBuffer, 1, &region);
    dev->endSingleTimeCommands(cmd);

    vkDestroyBuffer(m_device, stageBuf, nullptr);
    vkFreeMemory(m_device, stageMem, nullptr);
}

// ============================================================
// Pipeline creation
// ============================================================

bool GpuParticlePhysics::createPipelines(const std::string& /*shaderDir*/) {
    auto shader = [](const char* name) {
        return Core::AssetManager::instance().resolveShader(name);
    };

    // Push constant sizes (must match the shader PC blocks)
    struct IntegratePC { float dt; float gravity; uint32_t count; float linearDamp; float angularDamp; float sleepThreshSq; };
    struct CollidePC   { uint32_t count; float restitution; float friction; float dt; float sleepThreshSq; };
    struct ExpandPC    { uint32_t count; uint32_t maxFaceSlots; };

    // integrate: binding 0 = particles (rw)
    if (!m_integratePass.create(m_device, shader("particle_integrate.comp.spv"),
                                 1, sizeof(IntegratePC)))
        return false;

    // collide: binding 0 = particles (rw), binding 1 = occupancy grid (ro), binding 2 = character AABB (ro)
    if (!m_collidePass.create(m_device, shader("particle_collide.comp.spv"),
                               3, sizeof(CollidePC)))
        return false;

    // expand: binding 0 = particles (ro), binding 1 = matTexTable (ro),
    //         binding 2 = faceBuffer (wo), binding 3 = indirectCmd (rw atomic)
    if (!m_expandPass.create(m_device, shader("particle_expand.comp.spv"),
                              4, sizeof(ExpandPC)))
        return false;

    // Wire buffers to each pipeline's descriptor set
    VkDeviceSize particleSize = static_cast<VkDeviceSize>(MAX_PARTICLES) * sizeof(GpuParticle);
    VkDeviceSize faceSize     = static_cast<VkDeviceSize>(MAX_FACE_SLOTS) * 64;
    VkDeviceSize occSize      = static_cast<VkDeviceSize>(OCC_TOTAL_WORDS) * sizeof(uint32_t);
    uint32_t     matTableSize = static_cast<uint32_t>(MATERIAL_NAMES.size()) * 6 * sizeof(uint32_t);

    m_integratePass.bindBuffer(0, m_particleBuffer, particleSize);
    m_integratePass.updateDescriptors();

    m_collidePass.bindBuffer(0, m_particleBuffer, particleSize);
    m_collidePass.bindBuffer(1, m_occupancyBuffer, occSize);
    m_collidePass.bindBuffer(2, m_characterBuffer, static_cast<VkDeviceSize>(sizeof(CharacterCollider)));
    m_collidePass.updateDescriptors();

    m_expandPass.bindBuffer(0, m_particleBuffer, particleSize);
    m_expandPass.bindBuffer(1, m_matTexBuffer,   matTableSize);
    m_expandPass.bindBuffer(2, m_faceBuffer,     faceSize);
    m_expandPass.bindBuffer(3, m_indirectDrawBuffer, 16);
    m_expandPass.updateDescriptors();

    return true;
}

// ============================================================
// Per-frame CPU interface
// ============================================================

void GpuParticlePhysics::queueSpawn(const SpawnParams& p) {
    if (m_freeSlots.empty()) {
        LOG_WARN("GpuParticlePhysics", "Particle pool full, spawn ignored");
        return;
    }

    uint32_t slot = m_freeSlots.back();
    m_freeSlots.pop_back();

    m_slots[slot].lifetimeRemaining = p.lifetime;
    m_slots[slot].active            = true;
    ++m_activeCount;

    // Build GPU particle. prevPosition = position - velocity*dt approximation.
    // We don't have dt here, so use a small fixed step (physics will correct quickly).
    constexpr float DT_APPROX = 1.0f / 60.0f;
    GpuParticle gp{};
    gp.position     = p.position;
    gp.lifetime     = p.lifetime;
    gp.prevPosition = p.position - p.velocity * DT_APPROX;
    gp.maxLifetime  = p.lifetime;
    gp.rotation     = glm::vec4(p.rotation.x, p.rotation.y, p.rotation.z, p.rotation.w);
    gp.angularVel   = p.angularVel;
    gp.flags        = 1u | p.typeFlags; // PARTICLE_ACTIVE
    gp.scale        = p.scale;
    gp.materialIndex= materialNameToIndex(p.materialName);
    gp.color        = p.color;

    m_pendingSpawns.push_back({ slot, gp });
}

void GpuParticlePhysics::update(float dt) {
    // Age CPU-side slots; reclaim expired ones
    for (uint32_t i = 0; i < MAX_PARTICLES; ++i) {
        if (!m_slots[i].active) continue;
        m_slots[i].lifetimeRemaining -= dt;
        if (m_slots[i].lifetimeRemaining <= 0.0f) {
            m_slots[i].active = false;
            m_freeSlots.push_back(i);
            --m_activeCount;
        }
    }

    // Write pending spawns into the staging buffer
    for (const auto& pc : m_pendingSpawns) {
        GpuParticle* staging = static_cast<GpuParticle*>(m_stagingMapped);
        staging[pc.slotIndex] = pc.data;
    }

    // Snapshot pending list for this frame, clear for next
    m_pendingThisFrame = std::move(m_pendingSpawns);
    m_pendingSpawns.clear();
}

// ============================================================
// Compute command recording
// ============================================================

void GpuParticlePhysics::recordComputeCommands(VkCommandBuffer cmd, uint32_t /*frameIndex*/) {
    if (!m_initialized || m_activeCount == 0 && m_pendingThisFrame.empty()) return;

    // ---- 1. Upload pending spawns from staging buffer ----
    for (const auto& pc : m_pendingThisFrame) {
        VkBufferCopy region{};
        region.srcOffset = static_cast<VkDeviceSize>(pc.slotIndex) * sizeof(GpuParticle);
        region.dstOffset = region.srcOffset;
        region.size      = sizeof(GpuParticle);
        vkCmdCopyBuffer(cmd, m_stagingBuffer, m_particleBuffer, 1, &region);
    }
    m_pendingThisFrame.clear();

    // Barrier: TRANSFER_WRITE → COMPUTE_SHADER_READ/WRITE (particle buffer)
    if (m_activeCount > 0) {
        insertBarrier(cmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            m_particleBuffer);
    }

    const uint32_t count  = MAX_PARTICLES; // always dispatch full range; inactive slots are skipped in shader
    const uint32_t groups = (count + 255u) / 256u;

    // ---- 2. Integrate pass ----
    struct IntegratePC {
        float    dt;
        float    gravity;
        uint32_t count;
        float    linearDamp;
        float    angularDamp;
        float    sleepThreshSq;
    } ipc;
    ipc.dt           = 1.0f / 60.0f; // fixed physics step (TODO: pass real dt from caller)
    ipc.gravity      = GRAVITY;
    ipc.count        = count;
    ipc.linearDamp   = LINEAR_DAMP;
    ipc.angularDamp  = ANGULAR_DAMP;
    ipc.sleepThreshSq= SLEEP_THRESH_SQ;

    m_integratePass.bind(cmd);
    m_integratePass.pushConstants(cmd, &ipc, sizeof(ipc));
    m_integratePass.dispatch(cmd, groups);

    // Barrier: COMPUTE_SHADER_WRITE → COMPUTE_SHADER_READ (particle buffer, between passes)
    insertBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_SHADER_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
        m_particleBuffer);

    // ---- 3. Collide pass ----
    struct CollidePC {
        uint32_t count;
        float    restitution;
        float    friction;
        float    dt;
        float    sleepThreshSq;
    } cpc;
    cpc.count        = count;
    cpc.restitution  = RESTITUTION;
    cpc.friction     = FRICTION;
    cpc.dt           = 1.0f / 60.0f;
    cpc.sleepThreshSq= SLEEP_THRESH_SQ;

    m_collidePass.bind(cmd);
    m_collidePass.pushConstants(cmd, &cpc, sizeof(cpc));
    m_collidePass.dispatch(cmd, groups);

    // Barrier: particles COMPUTE_WRITE → COMPUTE_READ (for expand)
    insertBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_SHADER_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT,
        m_particleBuffer);

    // ---- 4. Reset instanceCount in indirect draw buffer ----
    // Zero only the instanceCount field (offset 4, size 4) so vertexCount stays 6
    vkCmdFillBuffer(cmd, m_indirectDrawBuffer, 4, 4, 0);
    insertBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
        m_indirectDrawBuffer, 16);

    // ---- 5. Expand pass ----
    struct ExpandPC {
        uint32_t count;
        uint32_t maxFaceSlots;
    } epc;
    epc.count        = count;
    epc.maxFaceSlots = MAX_FACE_SLOTS;

    m_expandPass.bind(cmd);
    m_expandPass.pushConstants(cmd, &epc, sizeof(epc));
    m_expandPass.dispatch(cmd, groups);

    // ---- 6. Final barriers: compute → vertex input ----
    // Face buffer: COMPUTE_WRITE → VERTEX_INPUT
    insertBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
        VK_ACCESS_SHADER_WRITE_BIT,
        VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
        m_faceBuffer);

    // Indirect draw buffer: COMPUTE_WRITE → INDIRECT_COMMAND_READ
    insertBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
        VK_ACCESS_SHADER_WRITE_BIT,
        VK_ACCESS_INDIRECT_COMMAND_READ_BIT,
        m_indirectDrawBuffer, 16);
}

// ============================================================
// 3D Occupancy grid
// ============================================================

void GpuParticlePhysics::setOccupied(int worldX, int worldY, int worldZ, bool solid) {
    int lx = worldX + OCC_HALF_X;
    int ly = worldY + OCC_Y_OFFSET;
    int lz = worldZ + OCC_HALF_Z;
    if (lx < 0 || lx >= OCC_X) return;
    if (ly < 0 || ly >= OCC_Y) return;
    if (lz < 0 || lz >= OCC_Z) return;

    int linearIdx = lx + ly * OCC_X + lz * OCC_X * OCC_Y;
    uint32_t wordIdx = static_cast<uint32_t>(linearIdx) >> 5u;
    uint32_t bitIdx  = static_cast<uint32_t>(linearIdx) & 31u;

    uint32_t* words = static_cast<uint32_t*>(m_occupancyMapped);
    if (solid)
        words[wordIdx] |= (1u << bitIdx);
    else
        words[wordIdx] &= ~(1u << bitIdx);
}

void GpuParticlePhysics::clearOccupancy() {
    if (m_occupancyMapped)
        memset(m_occupancyMapped, 0, static_cast<size_t>(OCC_TOTAL_WORDS) * sizeof(uint32_t));
}

// ============================================================
// Character collider
// ============================================================

void GpuParticlePhysics::setCharacterAABB(const glm::vec3& center, const glm::vec3& halfExtents, const glm::vec3& velocity) {
    if (!m_characterMapped) return;
    CharacterCollider* cc = static_cast<CharacterCollider*>(m_characterMapped);
    cc->center      = center;
    cc->halfExtents = halfExtents;
    cc->velocity    = velocity;
    cc->active      = 1.0f;
}

void GpuParticlePhysics::clearCharacterAABB() {
    if (!m_characterMapped) return;
    static_cast<CharacterCollider*>(m_characterMapped)->active = 0.0f;
}

// ============================================================
// Cleanup
// ============================================================

void GpuParticlePhysics::cleanup() {
    if (m_device == VK_NULL_HANDLE) return;

    m_integratePass.cleanup();
    m_collidePass.cleanup();
    m_expandPass.cleanup();

    auto destroyBuf = [&](VkBuffer& buf, VkDeviceMemory& mem) {
        if (buf  != VK_NULL_HANDLE) { vkDestroyBuffer(m_device, buf, nullptr);  buf = VK_NULL_HANDLE; }
        if (mem  != VK_NULL_HANDLE) { vkFreeMemory(m_device, mem, nullptr);     mem = VK_NULL_HANDLE; }
    };

    // Unmap before freeing
    if (m_stagingMapped)    { vkUnmapMemory(m_device, m_stagingMem);    m_stagingMapped   = nullptr; }
    if (m_occupancyMapped)  { vkUnmapMemory(m_device, m_occupancyMem); m_occupancyMapped = nullptr; }
    if (m_characterMapped)  { vkUnmapMemory(m_device, m_characterMem); m_characterMapped = nullptr; }

    destroyBuf(m_particleBuffer,      m_particleMem);
    destroyBuf(m_faceBuffer,          m_faceMem);
    destroyBuf(m_stagingBuffer,       m_stagingMem);
    destroyBuf(m_indirectDrawBuffer,  m_indirectDrawMem);
    destroyBuf(m_occupancyBuffer,     m_occupancyMem);
    destroyBuf(m_characterBuffer,     m_characterMem);
    destroyBuf(m_matTexBuffer,        m_matTexMem);

    m_device     = VK_NULL_HANDLE;
    m_physDevice = VK_NULL_HANDLE;
    m_initialized = false;
}

// ============================================================
// Barrier helper
// ============================================================

void GpuParticlePhysics::insertBarrier(VkCommandBuffer cmd,
                                        VkPipelineStageFlags src, VkPipelineStageFlags dst,
                                        VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                                        VkBuffer buffer, VkDeviceSize size) {
    VkBufferMemoryBarrier b{};
    b.sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    b.srcAccessMask       = srcAccess;
    b.dstAccessMask       = dstAccess;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.buffer              = buffer;
    b.offset              = 0;
    b.size                = size;
    vkCmdPipelineBarrier(cmd, src, dst, 0, 0, nullptr, 1, &b, 0, nullptr);
}

} // namespace Phyxel
