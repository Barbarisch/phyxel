#include "core/GpuParticlePhysics.h"
#include "core/MaterialRegistry.h"
#include "vulkan/VulkanDevice.h"
#include "core/AssetManager.h"
#include "physics/Material.h"
#include "utils/Logger.h"
#include "utils/GpuProfiler.h"
#include <glm/gtc/quaternion.hpp>
#include <cstring>
#include <algorithm>
#include <functional>
#include <stdexcept>
#include <random>

namespace Phyxel {

// ============================================================
// Static data
// ============================================================

// materialNameToIndex uses MaterialRegistry — no hardcoded list needed.
uint32_t GpuParticlePhysics::materialNameToIndex(const std::string& name) {
    int id = Core::MaterialRegistry::instance().getMaterialID(name);
    return (id >= 0) ? static_cast<uint32_t>(id) : 3u; // 3 = Default fallback
}

// ============================================================
// Construction / Destruction
// ============================================================

GpuParticlePhysics::GpuParticlePhysics() {
    m_slots.resize(MAX_PARTICLES);
    m_freeSlots.reserve(MAX_PARTICLES);
    for (uint32_t i = 0; i < MAX_PARTICLES; ++i) m_freeSlots.push_back(i);
    std::make_heap(m_freeSlots.begin(), m_freeSlots.end(), std::greater<uint32_t>());
}

void GpuParticlePhysics::releaseSlot(uint32_t slot) {
    m_freeSlots.push_back(slot);
    std::push_heap(m_freeSlots.begin(), m_freeSlots.end(), std::greater<uint32_t>());
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
    if (!createSolverBuffers(vulkanDevice))       return false;
    if (!initMatTexTable(vulkanDevice))           return false;
    if (!initMaterialPhysicsTable())              return false;
    if (!createPipelines(std::string()))          return false; // uses AssetManager internally
    if (!createSolverPipelines(std::string()))    return false;

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
        m_particleBuffer, m_particleMem,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT); // position-log + settle-probe readback copies

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

    // 7. Material physics properties (host-coherent SSBO, 32 bytes × material count)
    {
        VkDeviceSize matPhysSize = static_cast<VkDeviceSize>(Core::MaterialRegistry::instance().getMaterialCount()) * sizeof(MaterialPhysicsGpu);
        VkBufferCreateInfo bi{};
        bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size        = matPhysSize;
        bi.usage       = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(m_device, &bi, nullptr, &m_materialPhysBuffer) != VK_SUCCESS) {
            LOG_ERROR("GpuParticlePhysics", "Failed to create material physics buffer");
            return false;
        }
        VkMemoryRequirements req;
        vkGetBufferMemoryRequirements(m_device, m_materialPhysBuffer, &req);
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
            LOG_ERROR("GpuParticlePhysics", "No host-coherent memory for material physics buffer");
            return false;
        }
        ai.memoryTypeIndex = memType;
        if (vkAllocateMemory(m_device, &ai, nullptr, &m_materialPhysMem) != VK_SUCCESS ||
            vkBindBufferMemory(m_device, m_materialPhysBuffer, m_materialPhysMem, 0) != VK_SUCCESS ||
            vkMapMemory(m_device, m_materialPhysMem, 0, matPhysSize, 0, &m_materialPhysMapped) != VK_SUCCESS) {
            LOG_ERROR("GpuParticlePhysics", "Failed to create/map material physics buffer");
            return false;
        }
    }

    // 8. Sorted grid — per-cell count (device-local)
    dev->createStorageBuffer(
        static_cast<VkDeviceSize>(GRID_CELLS) * sizeof(uint32_t),
        m_gridCellCountBuffer, m_gridCellCountMem);

    // 9. Sorted grid — per-cell write offset / END pointer (device-local)
    dev->createStorageBuffer(
        static_cast<VkDeviceSize>(GRID_CELLS) * sizeof(uint32_t),
        m_gridCellOffsetBuffer, m_gridCellOffsetMem);

    // 9b. Parallel-scan per-block totals (device-local), SCAN_BLOCKS entries
    dev->createStorageBuffer(
        static_cast<VkDeviceSize>(SCAN_BLOCKS) * sizeof(uint32_t),
        m_scanBlockSumsBuffer, m_scanBlockSumsMem);

    // 10a. Sorted grid — particles ordered by cell (device-local)
    dev->createStorageBuffer(
        static_cast<VkDeviceSize>(MAX_PARTICLES) * sizeof(GpuParticle),
        m_sortedParticleBuffer, m_sortedParticleMem);

    // 10b. Sorted grid — canonical index per sorted slot (device-local)
    dev->createStorageBuffer(
        static_cast<VkDeviceSize>(MAX_PARTICLES) * sizeof(uint32_t),
        m_sortedIndexBuffer, m_sortedIndexMem);

    // 11. Readback buffer for position logging (host-coherent, persistent map)
    {
        VkDeviceSize rbSize = static_cast<VkDeviceSize>(MAX_PARTICLES) * sizeof(GpuParticle);
        VkBufferCreateInfo bi{};
        bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size        = rbSize;
        bi.usage       = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(m_device, &bi, nullptr, &m_readbackBuffer) != VK_SUCCESS) {
            LOG_ERROR("GpuParticlePhysics", "Failed to create readback buffer");
            return false;
        }
        VkMemoryRequirements req;
        vkGetBufferMemoryRequirements(m_device, m_readbackBuffer, &req);
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
            LOG_ERROR("GpuParticlePhysics", "No host-coherent memory for readback buffer");
            return false;
        }
        ai.memoryTypeIndex = memType;
        if (vkAllocateMemory(m_device, &ai, nullptr, &m_readbackMem) != VK_SUCCESS ||
            vkBindBufferMemory(m_device, m_readbackBuffer, m_readbackMem, 0) != VK_SUCCESS ||
            vkMapMemory(m_device, m_readbackMem, 0, rbSize, 0, &m_readbackMapped) != VK_SUCCESS) {
            LOG_ERROR("GpuParticlePhysics", "Failed to create/map readback buffer");
            return false;
        }
    }

    return true;
}

// ============================================================
// Material texture table
// ============================================================

bool GpuParticlePhysics::initMatTexTable(Vulkan::VulkanDevice* dev) {
    auto& reg = Core::MaterialRegistry::instance();
    const int matCount = reg.getMaterialCount();
    const uint32_t tableSize = matCount * 6;
    std::vector<uint32_t> table(tableSize);

    for (int mat = 0; mat < matCount; ++mat) {
        for (int face = 0; face < 6; ++face) {
            table[mat * 6 + face] = static_cast<uint32_t>(reg.getTextureIndex(mat, face));
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
// Material physics table (GPU SSBO from MaterialRegistry)
// ============================================================

bool GpuParticlePhysics::initMaterialPhysicsTable() {
    auto& reg = Phyxel::Core::MaterialRegistry::instance();
    auto* dst = static_cast<MaterialPhysicsGpu*>(m_materialPhysMapped);

    for (int i = 0; i < reg.getMaterialCount(); ++i) {
        const std::string& name = reg.getAllMaterials()[i].name;
        MaterialPhysicsGpu gpu{};

        if (reg.hasMaterial(name)) {
            const auto& mp = reg.getPhysics(name);
            gpu.mass            = mp.mass;
            gpu.restitution     = mp.restitution;
            // Coulomb mu for the AVBD friction cone (solver_dual/primal). The old mapping
            // (1 - f*0.36) was the dead legacy shader's velocity-RETENTION factor and inverted
            // friction under AVBD: Ice 0.96 (grippy), Stone 0.71.
            gpu.friction        = std::max(0.0f, mp.friction);
            gpu.linearDamp      = std::max(0.9f, 1.0f - mp.linearDamping * 0.05f);
            gpu.angularDamp     = std::max(0.97f, 1.0f - mp.angularDamping * 0.03f);
            gpu.breakForceScale = mp.breakForceMultiplier;
        } else {
            gpu.mass            = 1.0f;
            gpu.restitution     = 0.3f;
            gpu.friction        = 0.5f;
            gpu.linearDamp      = 0.995f;
            gpu.angularDamp     = 0.97f;
            gpu.breakForceScale = 1.0f;
        }
        gpu.pad0 = 0.0f;
        gpu.pad1 = 0.0f;
        dst[i] = gpu;
        if (m_materialMassCpu.size() <= static_cast<size_t>(i)) m_materialMassCpu.resize(i + 1, 1.0f);
        m_materialMassCpu[i] = gpu.mass;

        LOG_DEBUG_FMT("GpuParticlePhysics", "Material[" << i << "] " << name
            << ": mass=" << gpu.mass << " rest=" << gpu.restitution
            << " fric=" << gpu.friction << " lDamp=" << gpu.linearDamp
            << " aDamp=" << gpu.angularDamp);
    }

    return true;
}

// ============================================================
// Pipeline creation
// ============================================================

bool GpuParticlePhysics::createPipelines(const std::string& /*shaderDir*/) {
    auto shader = [](const char* name) {
        return Core::AssetManager::instance().resolveShader(name);
    };

    // Push-constant layouts: ONE definition each, shared with the shaders (solver_shared.h).
    using namespace DebrisShared;

    // grid clear: binding 0 = gridCellCount (rw)
    if (!m_gridClearPass.create(m_device, shader("particle_grid_clear.comp.spv"),
                                 1, sizeof(GridCellsPC)))
        return false;

    // grid build: binding 0 = particles (ro), binding 1 = gridCellCount (rw)
    if (!m_gridBuildPass.create(m_device, shader("particle_grid_build.comp.spv"),
                                 2, sizeof(GridCountPC)))
        return false;

    // sort scatter: binding 0 = particles (ro), binding 1 = gridCellOffset (rw atomic),
    //               binding 2 = sortedParticles (wo), binding 3 = sortedIndices (wo)
    if (!m_sortScatterPass.create(m_device, shader("particle_sort_scatter.comp.spv"),
                                   4, sizeof(GridCountPC)))
        return false;

    // expand: binding 0 = particles (ro), binding 1 = matTexTable (ro),
    //         binding 2 = faceBuffer (wo), binding 3 = indirectCmd (rw atomic)
    if (!m_expandPass.create(m_device, shader("particle_expand.comp.spv"),
                              4, sizeof(ExpandPC)))
        return false;

    // Wire buffers to each pipeline's descriptor set
    VkDeviceSize particleSize      = static_cast<VkDeviceSize>(MAX_PARTICLES) * sizeof(GpuParticle);
    VkDeviceSize faceSize          = static_cast<VkDeviceSize>(MAX_FACE_SLOTS) * 64;
    uint32_t     matCount          = static_cast<uint32_t>(Core::MaterialRegistry::instance().getMaterialCount());
    uint32_t     matTableSize      = matCount * 6 * sizeof(uint32_t);
    VkDeviceSize gridCellSize      = static_cast<VkDeviceSize>(GRID_CELLS) * sizeof(uint32_t);
    VkDeviceSize sortedParticleSize= static_cast<VkDeviceSize>(MAX_PARTICLES) * sizeof(GpuParticle);
    VkDeviceSize sortedIndexSize   = static_cast<VkDeviceSize>(MAX_PARTICLES) * sizeof(uint32_t);

    m_gridClearPass.bindBuffer(0, m_gridCellCountBuffer, gridCellSize);
    m_gridClearPass.updateDescriptors();

    m_gridBuildPass.bindBuffer(0, m_particleBuffer,      particleSize);
    m_gridBuildPass.bindBuffer(1, m_gridCellCountBuffer, gridCellSize);
    m_gridBuildPass.updateDescriptors();

    m_sortScatterPass.bindBuffer(0, m_particleBuffer,       particleSize);
    m_sortScatterPass.bindBuffer(1, m_gridCellOffsetBuffer, gridCellSize);
    m_sortScatterPass.bindBuffer(2, m_sortedParticleBuffer, sortedParticleSize);
    m_sortScatterPass.bindBuffer(3, m_sortedIndexBuffer,    sortedIndexSize);
    m_sortScatterPass.updateDescriptors();

    // Parallel prefix-sum passes — replace the serial sort scan in the live AVBD
    // pipeline: block scan -> block-sum scan -> add block offsets.
    VkDeviceSize scanBlockSumsSize = static_cast<VkDeviceSize>(SCAN_BLOCKS) * sizeof(uint32_t);
    if (!m_scanBlockPass.create(m_device, shader("particle_scan_block.comp.spv"), 3, sizeof(GridCellsPC)))
        return false;
    m_scanBlockPass.bindBuffer(0, m_gridCellCountBuffer,  gridCellSize);
    m_scanBlockPass.bindBuffer(1, m_gridCellOffsetBuffer, gridCellSize);
    m_scanBlockPass.bindBuffer(2, m_scanBlockSumsBuffer,  scanBlockSumsSize);
    m_scanBlockPass.updateDescriptors();

    if (!m_scanBlockSumsPass.create(m_device, shader("particle_scan_blocksums.comp.spv"), 1, sizeof(ScanBlocksPC)))
        return false;
    m_scanBlockSumsPass.bindBuffer(0, m_scanBlockSumsBuffer, scanBlockSumsSize);
    m_scanBlockSumsPass.updateDescriptors();

    if (!m_scanAddPass.create(m_device, shader("particle_scan_add.comp.spv"), 2, sizeof(GridCellsPC)))
        return false;
    m_scanAddPass.bindBuffer(0, m_gridCellOffsetBuffer, gridCellSize);
    m_scanAddPass.bindBuffer(1, m_scanBlockSumsBuffer,  scanBlockSumsSize);
    m_scanAddPass.updateDescriptors();

    m_expandPass.bindBuffer(0, m_particleBuffer,     particleSize);
    m_expandPass.bindBuffer(1, m_matTexBuffer,       matTableSize);
    m_expandPass.bindBuffer(2, m_faceBuffer,         faceSize);
    m_expandPass.bindBuffer(3, m_indirectDrawBuffer, 16);
    m_expandPass.updateDescriptors();

    return true;
}

// ============================================================
// Constraint solver buffer creation
// ============================================================

bool GpuParticlePhysics::createSolverBuffers(Vulkan::VulkanDevice* dev) {
    // SolverBody: 208 bytes per particle (AVBD primal solver fields)
    dev->createStorageBuffer(
        static_cast<VkDeviceSize>(MAX_PARTICLES) * 208,
        m_solverBodyBuffer, m_solverBodyMem);

    // Constraints: 128 bytes each (AVBD + warmstart fields: featureKey, wsKey, isNew, stick, C_init_t1/2)
    dev->createStorageBuffer(
        static_cast<VkDeviceSize>(MAX_CONSTRAINTS) * 128,
        m_constraintBuffer, m_constraintMem);

    // Solver state: counters (HASH_BASE uints) + open-addressed hash table (HASH_CAP uints).
    dev->createStorageBuffer(
        static_cast<VkDeviceSize>(SOLVER_STATE_UINTS) * sizeof(uint32_t),
        m_solverStateBuffer, m_solverStateMem,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT); // settle-probe header readback

    // Warmstart entries: HASH_CAP × 64 bytes. Indexed by hashInsert(wsKey).
    dev->createStorageBuffer(
        static_cast<VkDeviceSize>(HASH_CAP) * 64,
        m_warmstartBuffer, m_warmstartMem);

    // Body color buffer (one uint per body for Jones-Plassmann coloring)
    dev->createStorageBuffer(
        static_cast<VkDeviceSize>(MAX_PARTICLES) * sizeof(uint32_t),
        m_bodyColorBuffer, m_bodyColorMem,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT); // settle-probe colour readback

    dev->createStorageBuffer(
        static_cast<VkDeviceSize>(MAX_PARTICLES) * sizeof(uint32_t),
        m_bodyConstraintCountBuffer, m_bodyConstraintCountMem,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT); // settle-probe readback

    dev->createStorageBuffer(
        static_cast<VkDeviceSize>(MAX_PARTICLES) * sizeof(uint32_t),
        m_bodyConstraintOffsetBuffer, m_bodyConstraintOffsetMem);

    dev->createStorageBuffer(
        static_cast<VkDeviceSize>(MAX_PARTICLES) * sizeof(uint32_t),
        m_bodyConstraintCursorBuffer, m_bodyConstraintCursorMem);

    // Adjacency list: each constraint appears in at most 2 bodies → 2 × MAX_CONSTRAINTS entries
    dev->createStorageBuffer(
        static_cast<VkDeviceSize>(MAX_CONSTRAINTS) * 2 * sizeof(uint32_t),
        m_bodyConstraintListBuffer, m_bodyConstraintListMem);

    LOG_INFO_FMT("GpuParticlePhysics", "Solver buffers: "
        << "solverBody=" << (MAX_PARTICLES*208/1024) << "KB"
        << " constraints=" << (MAX_CONSTRAINTS*128/1024) << "KB"
        << " warmstart="   << (HASH_CAP*64/1024) << "KB"
        << " solverState=" << (SOLVER_STATE_UINTS*4/1024) << "KB"
        << " csrList=" << (MAX_CONSTRAINTS*2*4/1024) << "KB");
    return true;
}

// ============================================================
// Constraint solver pipeline creation
// ============================================================

bool GpuParticlePhysics::createSolverPipelines(const std::string& /*shaderDir*/) {
    auto shader = [](const char* name) {
        return Core::AssetManager::instance().resolveShader(name);
    };

    using namespace DebrisShared;   // push-constant layouts (solver_shared.h)

    uint32_t     matCount       = static_cast<uint32_t>(Core::MaterialRegistry::instance().getMaterialCount());
    VkDeviceSize particleSize   = static_cast<VkDeviceSize>(MAX_PARTICLES)   * sizeof(GpuParticle);
    VkDeviceSize bodySize       = static_cast<VkDeviceSize>(MAX_PARTICLES)   * 208;
    VkDeviceSize constrSize     = static_cast<VkDeviceSize>(MAX_CONSTRAINTS) * 128;
    VkDeviceSize stateSize      = static_cast<VkDeviceSize>(SOLVER_STATE_UINTS) * sizeof(uint32_t);
    VkDeviceSize warmstartSize  = static_cast<VkDeviceSize>(HASH_CAP)        * 64;
    VkDeviceSize matPhysSize    = static_cast<VkDeviceSize>(matCount)        * sizeof(MaterialPhysicsGpu);
    VkDeviceSize gridCellSize   = static_cast<VkDeviceSize>(GRID_CELLS)      * sizeof(uint32_t);
    VkDeviceSize sortedIdxSize  = static_cast<VkDeviceSize>(MAX_PARTICLES)   * sizeof(uint32_t);
    VkDeviceSize bodyUintSize   = static_cast<VkDeviceSize>(MAX_PARTICLES)   * sizeof(uint32_t);
    VkDeviceSize adjListSize    = static_cast<VkDeviceSize>(MAX_CONSTRAINTS) * 2 * sizeof(uint32_t);

    // solver_sync_in: particles(ro), bodies(rw), state(rw), materials(ro)
    if (!m_solverSyncInPass.create(m_device, shader("solver_sync_in.comp.spv"), 4, sizeof(SyncInPC))) return false;
    m_solverSyncInPass.bindBuffer(0, m_particleBuffer,     particleSize);
    m_solverSyncInPass.bindBuffer(1, m_solverBodyBuffer,   bodySize);
    m_solverSyncInPass.bindBuffer(2, m_solverStateBuffer,  stateSize);
    m_solverSyncInPass.bindBuffer(3, m_materialPhysBuffer, matPhysSize);
    m_solverSyncInPass.updateDescriptors();

    // solver_integrate: bodies, materials, particles, character collider, state (wake bits)
    if (!m_solverIntegratePass.create(m_device, shader("solver_integrate.comp.spv"), 5, sizeof(IntegratePC))) return false;
    m_solverIntegratePass.bindBuffer(0, m_solverBodyBuffer,   bodySize);
    m_solverIntegratePass.bindBuffer(1, m_materialPhysBuffer, matPhysSize);
    m_solverIntegratePass.bindBuffer(2, m_particleBuffer,     particleSize);
    m_solverIntegratePass.bindBuffer(3, m_characterBuffer,    static_cast<VkDeviceSize>(sizeof(CharacterCollider)));
    m_solverIntegratePass.bindBuffer(4, m_solverStateBuffer,  stateSize);
    m_solverIntegratePass.updateDescriptors();

    // solver_narrowphase: bodies, constraints, state, gridCount, gridOffset, sortedIndices, warmstarts
    if (!m_solverNarrowphasePass.create(m_device, shader("solver_narrowphase.comp.spv"), 7, sizeof(ContactsPC))) return false;
    m_solverNarrowphasePass.bindBuffer(0, m_solverBodyBuffer,    bodySize);
    m_solverNarrowphasePass.bindBuffer(1, m_constraintBuffer,    constrSize);
    m_solverNarrowphasePass.bindBuffer(2, m_solverStateBuffer,   stateSize);
    m_solverNarrowphasePass.bindBuffer(3, m_gridCellCountBuffer, gridCellSize);
    m_solverNarrowphasePass.bindBuffer(4, m_gridCellOffsetBuffer,gridCellSize);
    m_solverNarrowphasePass.bindBuffer(5, m_sortedIndexBuffer,   sortedIdxSize);
    m_solverNarrowphasePass.bindBuffer(6, m_warmstartBuffer,     warmstartSize);
    m_solverNarrowphasePass.updateDescriptors();

    // solver_voxel: bodies, constraints, state, occupancy DIRECTORY, warmstarts, occupancy POOL.
    // One descriptor set per frame slot: 3 and 5 are the shared micro occupancy (1c), bound per
    // slot by setStaticOccupancyBuffers. Until then they hold a placeholder the shader never
    // reads (occBox.w == 0 makes every lookup UNKNOWN before touching a buffer).
    if (!m_solverVoxelPass.create(m_device, shader("solver_voxel.comp.spv"), 6, sizeof(ContactsPC),
                                  OCC_FRAME_SLOTS)) return false;
    m_solverVoxelPass.bindBuffer(0, m_solverBodyBuffer,  bodySize);
    m_solverVoxelPass.bindBuffer(1, m_constraintBuffer,  constrSize);
    m_solverVoxelPass.bindBuffer(2, m_solverStateBuffer, stateSize);
    m_solverVoxelPass.bindBuffer(3, m_solverBodyBuffer,  bodySize);    // placeholder: occupancy dir
    m_solverVoxelPass.bindBuffer(4, m_warmstartBuffer,   warmstartSize);
    m_solverVoxelPass.bindBuffer(5, m_solverBodyBuffer,  bodySize);    // placeholder: occupancy pool
    m_solverVoxelPass.updateDescriptors();

    // solver_dual: bodies(ro), constraints(rw), state(ro)
    if (!m_solverDualPass.create(m_device, shader("solver_dual.comp.spv"), 3, sizeof(DualPC))) return false;
    m_solverDualPass.bindBuffer(0, m_solverBodyBuffer,  bodySize);
    m_solverDualPass.bindBuffer(1, m_constraintBuffer,  constrSize);
    m_solverDualPass.bindBuffer(2, m_solverStateBuffer, stateSize);
    m_solverDualPass.updateDescriptors();

    // solver_primal: bodies(rw), constraints(ro), state(ro), bodyColor(ro), count(ro), offset(ro), list(ro)
    if (!m_solverPrimalPass.create(m_device, shader("solver_primal.comp.spv"), 7, sizeof(PrimalPC))) return false;
    m_solverPrimalPass.bindBuffer(0, m_solverBodyBuffer,            bodySize);
    m_solverPrimalPass.bindBuffer(1, m_constraintBuffer,            constrSize);
    m_solverPrimalPass.bindBuffer(2, m_solverStateBuffer,           stateSize);
    m_solverPrimalPass.bindBuffer(3, m_bodyColorBuffer,             bodyUintSize);
    m_solverPrimalPass.bindBuffer(4, m_bodyConstraintCountBuffer,   bodyUintSize);
    m_solverPrimalPass.bindBuffer(5, m_bodyConstraintOffsetBuffer,  bodyUintSize);
    m_solverPrimalPass.bindBuffer(6, m_bodyConstraintListBuffer,    adjListSize);
    m_solverPrimalPass.updateDescriptors();

    // solver_sync_out: bodies, particles
    if (!m_solverSyncOutPass.create(m_device, shader("solver_sync_out.comp.spv"), 2, sizeof(SyncOutPC))) return false;
    m_solverSyncOutPass.bindBuffer(0, m_solverBodyBuffer, bodySize);
    m_solverSyncOutPass.bindBuffer(1, m_particleBuffer,   particleSize);
    m_solverSyncOutPass.updateDescriptors();

    // solver_warmstart_save: constraints(ro), warmstarts(rw), state(rw)
    if (!m_solverWarmstartSavePass.create(m_device, shader("solver_warmstart_save.comp.spv"), 3, sizeof(ConstraintsPC))) return false;
    m_solverWarmstartSavePass.bindBuffer(0, m_constraintBuffer,  constrSize);
    m_solverWarmstartSavePass.bindBuffer(1, m_warmstartBuffer,   warmstartSize);
    m_solverWarmstartSavePass.bindBuffer(2, m_solverStateBuffer, stateSize);
    m_solverWarmstartSavePass.updateDescriptors();

    // solver_hardcontact: bodies(rw), occupancy(ro)
    // Final positional safety pass — projects dynamic bodies out of static
    // voxel terrain in case AVBD couldn't fully resolve under heavy stacking.
    if (!m_solverHardContactPass.create(m_device, shader("solver_hardcontact.comp.spv"), 4,
                                        sizeof(HardContactPC), OCC_FRAME_SLOTS)) return false;
    m_solverHardContactPass.bindBuffer(0, m_solverBodyBuffer, bodySize);
    m_solverHardContactPass.bindBuffer(1, m_solverBodyBuffer, bodySize);   // placeholder: occupancy dir
    m_solverHardContactPass.bindBuffer(2, m_solverStateBuffer, stateSize); // settle-probe counters
    m_solverHardContactPass.bindBuffer(3, m_solverBodyBuffer, bodySize);   // placeholder: occupancy pool
    m_solverHardContactPass.updateDescriptors();

    // solver_csr_clear: bodyConstraintCount(rw), bodyColor(rw)
    if (!m_csrClearPass.create(m_device, shader("solver_csr_clear.comp.spv"), 2, sizeof(CsrClearPC))) return false;
    m_csrClearPass.bindBuffer(0, m_bodyConstraintCountBuffer, bodyUintSize);
    m_csrClearPass.bindBuffer(1, m_bodyColorBuffer,           bodyUintSize);
    m_csrClearPass.updateDescriptors();

    // solver_csr_count: constraints(ro), state(ro), bodyConstraintCount(rw)
    if (!m_csrCountPass.create(m_device, shader("solver_csr_count.comp.spv"), 3, sizeof(ConstraintsPC))) return false;
    m_csrCountPass.bindBuffer(0, m_constraintBuffer,         constrSize);
    m_csrCountPass.bindBuffer(1, m_solverStateBuffer,        stateSize);
    m_csrCountPass.bindBuffer(2, m_bodyConstraintCountBuffer,bodyUintSize);
    m_csrCountPass.updateDescriptors();

    // solver_prefix_sum: count(ro), offset(rw), cursor(rw)
    if (!m_prefixSumPass.create(m_device, shader("solver_prefix_sum.comp.spv"), 3, sizeof(BodiesPC))) return false;
    m_prefixSumPass.bindBuffer(0, m_bodyConstraintCountBuffer, bodyUintSize);
    m_prefixSumPass.bindBuffer(1, m_bodyConstraintOffsetBuffer,bodyUintSize);
    m_prefixSumPass.bindBuffer(2, m_bodyConstraintCursorBuffer,bodyUintSize);
    m_prefixSumPass.updateDescriptors();

    // solver_csr_scatter: constraints(ro), state(ro), cursor(rw), adjacencyList(rw)
    if (!m_csrScatterPass.create(m_device, shader("solver_csr_scatter.comp.spv"), 4, sizeof(ConstraintsPC))) return false;
    m_csrScatterPass.bindBuffer(0, m_constraintBuffer,          constrSize);
    m_csrScatterPass.bindBuffer(1, m_solverStateBuffer,         stateSize);
    m_csrScatterPass.bindBuffer(2, m_bodyConstraintCursorBuffer,bodyUintSize);
    m_csrScatterPass.bindBuffer(3, m_bodyConstraintListBuffer,  adjListSize);
    m_csrScatterPass.updateDescriptors();

    // solver_body_color: constraints(ro), state(ro), bodyColor(rw), count(ro), offset(ro), list(ro)
    if (!m_bodyColorPass.create(m_device, shader("solver_body_color.comp.spv"), 6, sizeof(BodiesPC))) return false;
    m_bodyColorPass.bindBuffer(0, m_constraintBuffer,          constrSize);
    m_bodyColorPass.bindBuffer(1, m_solverStateBuffer,         stateSize);
    m_bodyColorPass.bindBuffer(2, m_bodyColorBuffer,           bodyUintSize);
    m_bodyColorPass.bindBuffer(3, m_bodyConstraintCountBuffer, bodyUintSize);
    m_bodyColorPass.bindBuffer(4, m_bodyConstraintOffsetBuffer,bodyUintSize);
    m_bodyColorPass.bindBuffer(5, m_bodyConstraintListBuffer,  adjListSize);
    m_bodyColorPass.updateDescriptors();

    return true;
}

// ============================================================
// Constraint-solver compute recording (one physics tick)
// ============================================================

void GpuParticlePhysics::recordComputeCommandsNew(VkCommandBuffer cmd, uint32_t count, float lifetimeDt,
                                                  GpuProfiler* profiler, bool instrument) {
    const uint32_t groups        = (count + DebrisShared::WORKGROUP - 1u) / DebrisShared::WORKGROUP;
    const uint32_t maxConstrGrps = (MAX_CONSTRAINTS + DebrisShared::WORKGROUP - 1u) / DebrisShared::WORKGROUP;

    auto ssBarrier = [&](VkBuffer buf) {
        insertBarrier(cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            buf);
    };

    // Per-pass GPU timing. Nested under the caller's "GPU Particles" scope; emitted
    // only on the first tick of the frame (instrument) to stay under the profiler's
    // per-frame query budget. begin/end are balanced no-ops when not instrumenting.
    auto beginP = [&](const char* n) { if (instrument && profiler) profiler->startScope(cmd, n); };
    auto endP   = [&]()              { if (instrument && profiler) profiler->endScope(cmd); };

    using namespace DebrisShared;   // push-constant layouts (solver_shared.h)

    // Reset solver counters (first HASH_BASE uints) to 0 every frame so constraint counts,
    // warmstart hit/miss counters, etc. start fresh.
    vkCmdFillBuffer(cmd, m_solverStateBuffer, 0,
                    static_cast<VkDeviceSize>(HASH_BASE) * sizeof(uint32_t), 0u);
    // Initialize the warmstart hash table to HASH_EMPTY (0xFFFFFFFF) ONLY ONCE. It must
    // persist across frames so AVBD multipliers/penalties carry over — Shallot semantics.
    // Clearing per-frame here was the cause of slow gravitational sinking: λ never had a
    // chance to accumulate from frame to frame, so contacts kept restarting from cold.
    if (!m_hashInitialized) {
        vkCmdFillBuffer(cmd, m_solverStateBuffer,
                        static_cast<VkDeviceSize>(HASH_BASE) * sizeof(uint32_t),
                        static_cast<VkDeviceSize>(HASH_CAP) * sizeof(uint32_t),
                        0xFFFFFFFFu);
        // Wake bits (Phase 2 sleep) start clear. Like the hash table they persist
        // across frames — set on tick N, consumed by sync_in on tick N+1.
        vkCmdFillBuffer(cmd, m_solverStateBuffer,
                        static_cast<VkDeviceSize>(HASH_BASE + HASH_CAP) * sizeof(uint32_t),
                        static_cast<VkDeviceSize>(WAKE_WORDS) * sizeof(uint32_t),
                        0u);
        m_hashInitialized = true;
    }
    insertBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
        m_solverStateBuffer);

    // ---- 1. Sync in: GpuParticle → SolverBody ----
    {
        beginP("Setup");
        SyncInPC pc{ count, FIXED_DT };
        m_solverSyncInPass.bind(cmd);
        m_solverSyncInPass.pushConstants(cmd, &pc, sizeof(pc));
        m_solverSyncInPass.dispatch(cmd, groups);
    }
    ssBarrier(m_solverBodyBuffer);

    // ---- 2. Integrate: apply gravity + damping, predict position ----
    {
        IntegratePC pc{ count, FIXED_DT, GRAVITY, m_solverFlags };
        m_solverIntegratePass.bind(cmd);
        m_solverIntegratePass.pushConstants(cmd, &pc, sizeof(pc));
        m_solverIntegratePass.dispatch(cmd, groups);
    }
    ssBarrier(m_solverBodyBuffer);
    ssBarrier(m_solverStateBuffer);   // character-shove wake bits (Phase 2 sleep)

    // ---- 3. Grid sort (reads m_particleBuffer — previous-tick positions for broadphase) ----
    {
        endP(); beginP("GridClear");
        GridCellsPC gc{ static_cast<uint32_t>(GRID_CELLS) };
        m_gridClearPass.bind(cmd);
        m_gridClearPass.pushConstants(cmd, &gc, sizeof(gc));
        m_gridClearPass.dispatch(cmd, (GRID_CELLS + DebrisShared::WORKGROUP - 1u) / DebrisShared::WORKGROUP);
    }
    ssBarrier(m_gridCellCountBuffer);

    {
        endP(); beginP("GridBuild");
        GridCountPC gb{ count };
        m_gridBuildPass.bind(cmd);
        m_gridBuildPass.pushConstants(cmd, &gb, sizeof(gb));
        m_gridBuildPass.dispatch(cmd, groups);
    }
    insertBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
        m_gridCellCountBuffer);

    {
        endP(); beginP("SortScan");
        const uint32_t scanBlocks = static_cast<uint32_t>(SCAN_BLOCKS);
        GridCellsPC ss{ static_cast<uint32_t>(GRID_CELLS) };

        // Pass 1: per-block exclusive scan -> gridCellOffset; block totals -> blockSums
        m_scanBlockPass.bind(cmd);
        m_scanBlockPass.pushConstants(cmd, &ss, sizeof(ss));
        m_scanBlockPass.dispatch(cmd, scanBlocks);
        ssBarrier(m_gridCellOffsetBuffer);
        ssBarrier(m_scanBlockSumsBuffer);

        // Pass 2: exclusive scan of the block totals (small; ~1024 elements)
        m_scanBlockSumsPass.bind(cmd);
        ScanBlocksPC sb{ scanBlocks };
        m_scanBlockSumsPass.pushConstants(cmd, &sb, sizeof(sb));
        m_scanBlockSumsPass.dispatch(cmd, 1);
        ssBarrier(m_scanBlockSumsBuffer);

        // Pass 3: add each block's global offset -> final exclusive prefix sum
        m_scanAddPass.bind(cmd);
        m_scanAddPass.pushConstants(cmd, &ss, sizeof(ss));
        m_scanAddPass.dispatch(cmd, scanBlocks);
    }
    ssBarrier(m_gridCellOffsetBuffer);

    {
        endP(); beginP("SortScatter");
        GridCountPC sc{ count };
        m_sortScatterPass.bind(cmd);
        m_sortScatterPass.pushConstants(cmd, &sc, sizeof(sc));
        m_sortScatterPass.dispatch(cmd, groups);
    }
    insertBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, m_gridCellOffsetBuffer);
    insertBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, m_sortedIndexBuffer);

    // ---- 4a. Narrowphase: dynamic-dynamic contacts → constraints ----
    {
        endP(); beginP("NarrowVoxel");
        ContactsPC pc{ count, MAX_CONSTRAINTS, m_solverFlags, m_coldPenaltyScale, m_occBox };
        m_solverNarrowphasePass.bind(cmd);
        m_solverNarrowphasePass.pushConstants(cmd, &pc, sizeof(pc));
        m_solverNarrowphasePass.dispatch(cmd, groups);
    }
    ssBarrier(m_constraintBuffer);
    ssBarrier(m_solverStateBuffer);

    // ---- 4b. Voxel contacts → constraints (appended) ----
    {
        ContactsPC pc{ count, MAX_CONSTRAINTS, m_solverFlags, m_coldPenaltyScale, m_occBox };
        m_solverVoxelPass.bind(cmd, m_frameSlot);
        m_solverVoxelPass.pushConstants(cmd, &pc, sizeof(pc));
        m_solverVoxelPass.dispatch(cmd, groups);
    }
    ssBarrier(m_constraintBuffer);
    ssBarrier(m_solverStateBuffer);

    // ---- 5. Build CSR adjacency structure for graph coloring ----
    // 5a. Clear bodyConstraintCount[] and constraintColor[]
    {
        endP(); beginP("ColoringCSR");
        CsrClearPC pc{ count, MAX_CONSTRAINTS };
        m_csrClearPass.bind(cmd);
        m_csrClearPass.pushConstants(cmd, &pc, sizeof(pc));
        m_csrClearPass.dispatch(cmd, maxConstrGrps); // covers both arrays (MAX_CONSTRAINTS > count)
    }
    ssBarrier(m_bodyConstraintCountBuffer);
    ssBarrier(m_bodyColorBuffer);

    // 5b. Count how many constraints each body participates in
    {
        ConstraintsPC pc{ MAX_CONSTRAINTS };
        m_csrCountPass.bind(cmd);
        m_csrCountPass.pushConstants(cmd, &pc, sizeof(pc));
        m_csrCountPass.dispatch(cmd, maxConstrGrps);
    }
    ssBarrier(m_bodyConstraintCountBuffer);

    // 5c. Exclusive prefix sum → bodyConstraintOffset[] and bodyConstraintCursor[]
    {
        BodiesPC pc{ count };
        m_prefixSumPass.bind(cmd);
        m_prefixSumPass.pushConstants(cmd, &pc, sizeof(pc));
        m_prefixSumPass.dispatch(cmd, 1);
    }
    ssBarrier(m_bodyConstraintOffsetBuffer);
    ssBarrier(m_bodyConstraintCursorBuffer);

    // 5d. Scatter constraint indices into adjacency list using atomic cursors
    {
        ConstraintsPC pc{ MAX_CONSTRAINTS };
        m_csrScatterPass.bind(cmd);
        m_csrScatterPass.pushConstants(cmd, &pc, sizeof(pc));
        m_csrScatterPass.dispatch(cmd, maxConstrGrps);
    }
    ssBarrier(m_bodyConstraintListBuffer);

    // ---- 6. Body graph coloring: Jones-Plassmann, COLOR_ROUNDS passes ----
    // Colors BODIES: two bodies are adjacent if they share a constraint.
    // Same-color bodies have no shared constraints → safe for parallel primal writes.
    {
        BodiesPC pc{ count };
        for (int gc = 0; gc < COLOR_ROUNDS; ++gc) {
            m_bodyColorPass.bind(cmd);
            m_bodyColorPass.pushConstants(cmd, &pc, sizeof(pc));
            m_bodyColorPass.dispatch(cmd, groups);
            ssBarrier(m_bodyColorBuffer);
        }
    }

    const bool  postStab   = (m_solverFlags & SOLVER_FLAG_POST_STAB) != 0;
    const float solveAlpha = postStab ? 1.0f : SOLVER_ALPHA;

    // ---- 7. AVBD dual+primal solve loop ----
    // solveDual: per-constraint, updates lambda and grows penalty (fully parallel, no body writes)
    // solvePrimal: per-body, assembles 6×6 block system and solves via LDL; one color per pass
    endP(); beginP("Solve");
    for (int iter = 0; iter < SOLVE_ITERATIONS; ++iter) {
        // Dual: update all constraint lambdas/penalties
        {
            DualPC pc{ MAX_CONSTRAINTS, FIXED_DT, 0u, solveAlpha };
            m_solverDualPass.bind(cmd);
            m_solverDualPass.pushConstants(cmd, &pc, sizeof(pc));
            m_solverDualPass.dispatch(cmd, maxConstrGrps);
        }
        ssBarrier(m_constraintBuffer);

        // Primal: solve per body, one color at a time
        // Final sweep (ci == MAX_COLORS) solves bodies Jones-Plassmann left UNCOLORED, all in
        // one Jacobi-style pass: a colouring conflict degrades to a Jacobi update instead of
        // the body being SKIPPED (audit D1; paper §4 does the same via double buffering).
        for (uint32_t ci = 0; ci <= MAX_COLORS; ++ci) {
            const uint32_t color = (ci == MAX_COLORS) ? 0xFFFFFFFFu : ci;
            PrimalPC pc{ count, FIXED_DT, color, solveAlpha };
            m_solverPrimalPass.bind(cmd);
            m_solverPrimalPass.pushConstants(cmd, &pc, sizeof(pc));
            m_solverPrimalPass.dispatch(cmd, groups);
            ssBarrier(m_solverBodyBuffer);
        }
    }

    // ---- 7a. Post-stabilisation (avbd-demo2d) ----
    // The loop above ran at alpha = 1: it never chases pre-existing overlap, so the motion it
    // produced is physical. Record that as the velocity, THEN remove the overlap with one
    // position-only primal sweep at alpha = 0. With the default alpha = 0.99 instead, only 1%
    // of an overlap is corrected per tick (stacks visibly sink, then creep) and whatever IS
    // corrected becomes velocity (the bubbling). This breaks that trade-off.
    if (postStab) {
        {
            PrimalPC pc{ count, FIXED_DT, PRIMAL_STORE_VELOCITY, 1.0f };
            m_solverPrimalPass.bind(cmd);
            m_solverPrimalPass.pushConstants(cmd, &pc, sizeof(pc));
            m_solverPrimalPass.dispatch(cmd, groups);
            ssBarrier(m_solverBodyBuffer);
        }
        for (uint32_t ci = 0; ci <= MAX_COLORS; ++ci) {
            const uint32_t color = (ci == MAX_COLORS) ? 0xFFFFFFFFu : ci;
            PrimalPC pc{ count, FIXED_DT, color, 0.0f };
            m_solverPrimalPass.bind(cmd);
            m_solverPrimalPass.pushConstants(cmd, &pc, sizeof(pc));
            m_solverPrimalPass.dispatch(cmd, groups);
            ssBarrier(m_solverBodyBuffer);
        }
    }

    // ---- 7b. Hard-contact safety pass ----
    // Positional projection of dynamic bodies out of static-voxel overlap the AVBD solve
    // left behind (> 1 cm — genuine failures only). Same contact geometry as the voxel
    // contact pass (voxel_contact.glsl), and velocity-neutral under SOLVER_FLAG_HC_NEUTRAL
    // (docs/DebrisSettlingPlan.md §R defect 5: it used to turn its push into launch velocity).
    {
        endP(); beginP("Finalize");
        HardContactPC pc{ count, m_solverFlags, 0.0f, 0.0f, m_occBox };
        m_solverHardContactPass.bind(cmd, m_frameSlot);
        m_solverHardContactPass.pushConstants(cmd, &pc, sizeof(pc));
        m_solverHardContactPass.dispatch(cmd, groups);
    }
    ssBarrier(m_solverBodyBuffer);

    // ---- 8. Sync out: SolverBody → GpuParticle ----
    {
        SyncOutPC pc{ count, FIXED_DT, lifetimeDt, m_solverFlags };
        m_solverSyncOutPass.bind(cmd);
        m_solverSyncOutPass.pushConstants(cmd, &pc, sizeof(pc));
        m_solverSyncOutPass.dispatch(cmd, groups);
    }
    ssBarrier(m_particleBuffer);

    // ---- 9. Warmstart save: scatter final lambda/penalty/stick into hash table ----
    // Must run AFTER the dual+primal converge so that the persisted values are post-solve.
    {
        ConstraintsPC pc{ MAX_CONSTRAINTS };
        m_solverWarmstartSavePass.bind(cmd);
        m_solverWarmstartSavePass.pushConstants(cmd, &pc, sizeof(pc));
        m_solverWarmstartSavePass.dispatch(cmd, maxConstrGrps);
    }
    ssBarrier(m_warmstartBuffer);
    ssBarrier(m_solverStateBuffer);
    endP();
}

// ============================================================
// Per-frame CPU interface
// ============================================================

void GpuParticlePhysics::queueSpawn(const SpawnParams& p) {
    if (m_freeSlots.empty()) {
        LOG_WARN("GpuParticlePhysics", "Particle pool full, spawn ignored");
        return;
    }

    std::pop_heap(m_freeSlots.begin(), m_freeSlots.end(), std::greater<uint32_t>());
    uint32_t slot = m_freeSlots.back();   // the lowest free slot
    m_freeSlots.pop_back();

    m_slots[slot].lifetimeRemaining = p.lifetime;
    m_slots[slot].active            = true;
    ++m_activeCount;
    if (slot + 1 > m_highWaterSlot) m_highWaterSlot = slot + 1;

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
    gp.flags        = DebrisShared::PARTICLE_ACTIVE | p.typeFlags;
    gp.scale        = p.scale;
    gp.materialIndex= (materialNameToIndex(p.materialName) & DebrisShared::MATERIAL_MASK)
                    | ((p.slice & DebrisShared::SLICE_MASK) << DebrisShared::SLICE_SHIFT);
    // color is unused by debris rendering (debris is textured), so repurpose it to carry the baked
    // light sampled at the spawn position (sky, blockR, blockG, blockB, each 0..15). The expand
    // compute shader packs it into the instance's reserved2; dynamic_voxel.vert reads it. Default
    // = full sky (15,0,0,0) so debris without a sampler / outside loaded chunks looks as before.
    gp.color = m_lightSampler ? m_lightSampler(p.position) : glm::vec4(15.0f, 0.0f, 0.0f, 0.0f);

    m_pendingSpawns.push_back({ slot, gp });
}

void GpuParticlePhysics::update(float dt) {
    // ---- Position logging: read back previous frame's particle data ----
    if (m_readbackPending && m_positionLogging && m_posLogFile.is_open()) {
        m_readbackPending = false;
        const GpuParticle* particles = static_cast<const GpuParticle*>(m_readbackMapped);
        const CharacterCollider* cc = m_characterMapped ?
            static_cast<const CharacterCollider*>(m_characterMapped) : nullptr;

        // Frame header: F,frame,dt,ticks,alpha,activeCount,char_cx,char_cy,char_cz,char_vx,char_vy,char_vz,char_active
        m_posLogFile << "F," << m_posLogFrameCounter
                     << "," << m_lastRealDt
                     << "," << m_physicsTicks
                     << "," << (m_timeAccumulator / FIXED_DT)
                     << "," << m_activeCount;
        if (cc) {
            m_posLogFile << "," << cc->center.x << "," << cc->center.y << "," << cc->center.z
                         << "," << cc->velocity.x << "," << cc->velocity.y << "," << cc->velocity.z
                         << "," << cc->segmentCount;
        } else {
            m_posLogFile << ",0,0,0,0,0,0,0";
        }
        m_posLogFile << "\n";

        // Particle lines: P,frame,slot,pos_x,pos_y,pos_z,prev_x,prev_y,prev_z,flags,material
        uint32_t logged = 0;
        for (uint32_t i = 0; i < m_highWaterSlot && logged < 200; ++i) {
            if (!(particles[i].flags & 1u)) continue; // not active
            const auto& p = particles[i];
            m_posLogFile << "P," << m_posLogFrameCounter
                         << "," << i
                         << "," << p.position.x << "," << p.position.y << "," << p.position.z
                         << "," << p.prevPosition.x << "," << p.prevPosition.y << "," << p.prevPosition.z
                         << "," << p.flags << "," << p.materialIndex << "\n";
            ++logged;
        }
        m_posLogFile.flush();
        ++m_posLogFrameCounter;
    }

    // Clamp dt to prevent spiral-of-death
    float realDt = std::min(dt, 0.25f);
    m_lastRealDt = realDt;

    // Fixed-timestep accumulator: step physics only when enough real time has passed.
    // This prevents the simulation from running too fast at high frame rates
    // (e.g., 4x speed at 240 FPS) or too slow at low frame rates.
    m_timeAccumulator += realDt;
    m_physicsTicks = 0;
    while (m_timeAccumulator >= FIXED_DT) {
        m_timeAccumulator -= FIXED_DT;
        ++m_physicsTicks;
    }
    // Cap to prevent spiral-of-death (e.g., after a long stall)
    if (m_physicsTicks > 4) {
        m_physicsTicks = 4;
        m_timeAccumulator = 0.0f;
    }
    // Debug freeze / single-step: only budgeted ticks run, and lifetimes age by exactly the
    // simulated time (so a frozen pile neither moves nor expires).
    if (m_frozen) {
        m_timeAccumulator = 0.0f;
        m_physicsTicks = std::min<uint32_t>(m_stepBudget, 4u);
        m_stepBudget  -= m_physicsTicks;
        realDt         = m_physicsTicks * FIXED_DT;
        m_lastRealDt   = realDt;
    }
    m_totalTicks += m_physicsTicks;

    // Scripted kinematic boxes (1f) move and age by SIMULATED time, so a frozen solver stepped
    // tick by tick drives them deterministically.
    if (!m_kinematicBoxes.empty()) {
        const float simDt = static_cast<float>(m_physicsTicks) * FIXED_DT;
        for (auto it = m_kinematicBoxes.begin(); it != m_kinematicBoxes.end(); ) {
            it->second.center += it->second.velocity * simDt;
            it->second.ttl    -= simDt;
            it = (it->second.ttl <= 0.0f) ? m_kinematicBoxes.erase(it) : std::next(it);
        }
        writeColliderBuffer();
    }

    // Age CPU-side slots using real elapsed time (frame-rate independent).
    // The GPU integrate shader does the same via lifetimeDt push constant.
    uint32_t newHigh = 0;
    for (uint32_t i = 0; i < m_highWaterSlot; ++i) {
        if (!m_slots[i].active) continue;
        m_slots[i].lifetimeRemaining -= realDt;
        if (m_slots[i].lifetimeRemaining <= 0.0f) {
            m_slots[i].active = false;
            releaseSlot(i);
            m_pendingDeactivations.push_back(i); // clear its GPU ACTIVE flag this frame
            --m_activeCount;
        } else {
            newHigh = i + 1;
        }
    }
    m_highWaterSlot = newHigh;
    if (m_activeCount == 0 && m_pendingSpawns.empty()) m_hashInitialized = false;   // as despawnAll

    // Auto-stop logging when all particles have died
    if (m_positionLogging && m_activeCount == 0 && m_pendingSpawns.empty() && m_posLogFrameCounter > 0) {
        stopPositionLog();
    }

    // Write pending spawns into the staging buffer
    for (const auto& pc : m_pendingSpawns) {
        GpuParticle* staging = static_cast<GpuParticle*>(m_stagingMapped);
        staging[pc.slotIndex] = pc.data;
        if (pc.slotIndex + 1 > m_highWaterSlot)
            m_highWaterSlot = pc.slotIndex + 1;
    }

    // Snapshot pending list for this frame, clear for next
    m_pendingThisFrame = std::move(m_pendingSpawns);
    m_pendingSpawns.clear();

    // Record timing stats into ring buffer
    if (m_timingRing.size() < TIMING_RING_SIZE)
        m_timingRing.resize(TIMING_RING_SIZE);
    FrameTimingEntry& te = m_timingRing[m_timingRingHead % TIMING_RING_SIZE];
    te.dt           = realDt;
    te.accumulator  = m_timeAccumulator;
    te.interpAlpha  = m_timeAccumulator / FIXED_DT;
    te.physicsTicks = m_physicsTicks;
    te.activeCount  = m_activeCount;
    te.frameNumber  = m_timingFrameCounter;
    ++m_timingRingHead;
    ++m_timingFrameCounter;
}

// ============================================================
// Compute command recording
// ============================================================

void GpuParticlePhysics::setStaticOccupancyBuffers(const VkBuffer dir[OCC_FRAME_SLOTS], VkDeviceSize dirBytes,
                                                   const VkBuffer pool[OCC_FRAME_SLOTS], VkDeviceSize poolBytes) {
    if (!m_initialized) return;
    for (uint32_t s = 0; s < OCC_FRAME_SLOTS; ++s) {
        if (dir[s] == VK_NULL_HANDLE || pool[s] == VK_NULL_HANDLE) {
            LOG_ERROR("GpuParticlePhysics", "shared occupancy slot has no buffer: debris will be HELD (frozen_unknown)");
            return;
        }
        m_solverVoxelPass.bindBufferInSet(s, 3, dir[s], dirBytes);
        m_solverVoxelPass.bindBufferInSet(s, 5, pool[s], poolBytes);
        m_solverHardContactPass.bindBufferInSet(s, 1, dir[s], dirBytes);
        m_solverHardContactPass.bindBufferInSet(s, 3, pool[s], poolBytes);
    }
    m_solverVoxelPass.updateDescriptors();
    m_solverHardContactPass.updateDescriptors();
    m_staticOccWired = true;
    LOG_INFO("GpuParticlePhysics", "debris collides against the shared micro occupancy pool");
}

void GpuParticlePhysics::setStaticOccupancyBox(const glm::ivec3& boxMinChunk, bool ready) {
    m_occBox = { boxMinChunk.x, boxMinChunk.y, boxMinChunk.z, (ready && m_staticOccWired) ? 1 : 0 };
}

void GpuParticlePhysics::recordComputeCommands(VkCommandBuffer cmd, uint32_t frameIndex, GpuProfiler* profiler) {
    // Settle probe: this frame slot's fence has retired (we are recording into its command
    // buffer), so the ticks copied into its ring slot PROBE_FRAMES frames ago are readable.
    // Consumed before any early-out so no tick is lost when the pool empties.
    if (m_initialized) {
        m_probeFrame = frameIndex % PROBE_FRAMES;
        consumeProbeSlot(m_probeFrame);
    }
    // The shared occupancy's slot for this frame (uploaded after this slot's fence, before now).
    m_frameSlot = frameIndex % OCC_FRAME_SLOTS;

    // Enter if anything is alive, spawning, or being retired this frame. The
    // deactivation check is required: when the last particles die, activeCount
    // is 0 and there are no spawns, but we still must clear their GPU flags.
    if (!m_initialized ||
        (m_activeCount == 0 && m_pendingThisFrame.empty() && m_pendingDeactivations.empty()))
        return;

    const bool didTransfer = !m_pendingThisFrame.empty() || !m_pendingDeactivations.empty();

    // ---- 1. Upload pending spawns from staging buffer ----
    for (const auto& pc : m_pendingThisFrame) {
        VkBufferCopy region{};
        region.srcOffset = static_cast<VkDeviceSize>(pc.slotIndex) * sizeof(GpuParticle);
        region.dstOffset = region.srcOffset;
        region.size      = sizeof(GpuParticle);
        vkCmdCopyBuffer(cmd, m_stagingBuffer, m_particleBuffer, 1, &region);
    }
    m_pendingThisFrame.clear();

    // ---- 1b. Clear slots the CPU retired this frame ----
    // Zero each retired slot so its GPU ACTIVE flag is cleared. Without this a
    // retired slot keeps its flag and gets re-counted by the expand shader the
    // moment a later spawn raises the high-water mark back over it, making
    // previously-vanished debris reappear. (The GPU lifetime drain can't be
    // relied on: it only runs on physics-tick frames and only for slots below
    // the dispatch high-water mark.) Zeroing the whole slot is safe — a re-used
    // slot is fully overwritten by its spawn copy before the next dispatch.
    //
    // Slots from a single break wave are allocated contiguously (LIFO free
    // list), so coalesce contiguous runs into one fill — a mass simultaneous
    // death (one shatter, uniform lifetime) collapses to a single command.
    if (!m_pendingDeactivations.empty()) {
        std::sort(m_pendingDeactivations.begin(), m_pendingDeactivations.end());
        size_t r = 0;
        while (r < m_pendingDeactivations.size()) {
            uint32_t runStart = m_pendingDeactivations[r];
            uint32_t runEnd   = runStart; // inclusive
            while (r + 1 < m_pendingDeactivations.size() &&
                   m_pendingDeactivations[r + 1] == runEnd + 1) {
                runEnd = m_pendingDeactivations[++r];
            }
            ++r;
            vkCmdFillBuffer(cmd, m_particleBuffer,
                static_cast<VkDeviceSize>(runStart) * sizeof(GpuParticle),
                static_cast<VkDeviceSize>(runEnd - runStart + 1) * sizeof(GpuParticle),
                0u);
        }
        m_pendingDeactivations.clear();
    }

    // Barrier: TRANSFER_WRITE → COMPUTE_SHADER_READ/WRITE (particle buffer)
    if (didTransfer) {
        insertBarrier(cmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            m_particleBuffer);
    }

    // Dispatch only up to the highest active slot, not the full 10K pool.
    const uint32_t count  = m_highWaterSlot;
    const uint32_t groups = (count + DebrisShared::WORKGROUP - 1u) / DebrisShared::WORKGROUP;
    if (groups == 0) return; // nothing to simulate

    // ---- Fixed-timestep physics loop (AVBD — the only pipeline; the legacy XPBD
    //      particle_integrate/collide path was deleted 2026-10-04, DebrisInteractionPlan D4) ----
    for (uint32_t tick = 0; tick < m_physicsTicks; ++tick) {
        const float lifetimeDtThisTick = (tick == 0) ? m_lastRealDt : 0.0f;
        // Per-pass GPU timing only on the first tick (query-budget safe).
        recordComputeCommandsNew(cmd, count, lifetimeDtThisTick, profiler, tick == 0);
        if (m_probeActive) recordProbeCopy(cmd, count, tick);
    }

    // ---- 4. Reset instanceCount in indirect draw buffer ----
    // Always expand for rendering (even if 0 physics ticks — new spawns need faces)
    vkCmdFillBuffer(cmd, m_indirectDrawBuffer, 4, 4, 0);
    insertBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
        m_indirectDrawBuffer, 16);

    // ---- 5. Expand pass ----
    // Smooth rendering between fixed-timestep physics ticks.
    // interpAlpha = fraction of FIXED_DT elapsed since the last completed tick.
    // The expand shader extrapolates: renderPos = position + velocity * alpha.
    DebrisShared::ExpandPC epc;   // layout: solver_shared.h PHX_PC_EXPAND
    epc.count        = count;
    epc.maxFaceSlots = MAX_FACE_SLOTS;
    epc.interpAlpha  = m_timeAccumulator / FIXED_DT;

    m_expandPass.bind(cmd);
    m_expandPass.pushConstants(cmd, &epc, sizeof(epc));
    m_expandPass.dispatch(cmd, groups);

    // ---- 6. Final barriers: compute → vertex input ----
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

    // ---- 7. Position logging readback (if active) ----
    if (m_positionLogging && m_readbackBuffer != VK_NULL_HANDLE) {
        // Barrier: particle buffer COMPUTE_WRITE → TRANSFER_READ
        insertBarrier(cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_TRANSFER_READ_BIT,
            m_particleBuffer);

        VkBufferCopy region{};
        region.srcOffset = 0;
        region.dstOffset = 0;
        region.size      = static_cast<VkDeviceSize>(m_highWaterSlot) * sizeof(GpuParticle);
        if (region.size > 0) {
            vkCmdCopyBuffer(cmd, m_particleBuffer, m_readbackBuffer, 1, &region);
        }
        m_readbackPending = true;
    }
}

void GpuParticlePhysics::despawnAll() {
    for (uint32_t i = 0; i < m_highWaterSlot; ++i) {
        if (m_slots[i].active) {
            m_slots[i].active = false;
            m_slots[i].lifetimeRemaining = 0.0f;
            releaseSlot(i);
            m_pendingDeactivations.push_back(i); // clear its GPU ACTIVE flag this frame
        }
    }
    m_activeCount = 0;
    m_highWaterSlot = 0;
    m_pendingSpawns.clear();
    // An empty pool has no contacts to warm-start: re-clear the hash + wake bits on the next
    // step. They were cleared ONCE per engine, so later bodies inherited stale keys/lambdas and
    // the table filled across a session — the same scenario gave a different result on every
    // run (DebrisInteractionPlan 1c, session-state dependence).
    m_hashInitialized = false;
    if (m_positionLogging) stopPositionLog();
}

// ============================================================
// Character collider
// ============================================================

void GpuParticlePhysics::setCharacterColliders(
    const std::vector<std::pair<glm::vec3, glm::vec3>>& boxes, const glm::vec3& velocity) {
    m_charBoxes = boxes;
    m_charVelocity = velocity;
    writeColliderBuffer();
}

void GpuParticlePhysics::setCharacterAABB(const glm::vec3& center, const glm::vec3& halfExtents, const glm::vec3& velocity) {
    setCharacterColliders({ { center, halfExtents } }, velocity);
}

void GpuParticlePhysics::clearCharacterAABB() {
    m_charBoxes.clear();
    writeColliderBuffer();
}

void GpuParticlePhysics::setKinematicBox(const std::string& id, const KinematicBox& box) {
    KinematicBox b = box;
    b.half = glm::max(b.half, glm::vec3(0.01f));          // a degenerate box collides with nothing
    b.ttl  = std::clamp(b.ttl, 0.0f, 10.0f);              // a forgotten box must not live forever
    m_kinematicBoxes[id] = b;
    writeColliderBuffer();
}

bool GpuParticlePhysics::removeKinematicBox(const std::string& id) {
    const bool had = m_kinematicBoxes.erase(id) > 0;
    writeColliderBuffer();
    return had;
}

void GpuParticlePhysics::writeColliderBuffer() {
    if (!m_characterMapped) return;
    CharacterCollider* cc = static_cast<CharacterCollider*>(m_characterMapped);

    std::vector<std::pair<glm::vec3, glm::vec3>> all = m_charBoxes;
    glm::vec3 velocity = m_charVelocity;
    for (const auto& [id, b] : m_kinematicBoxes) {
        all.emplace_back(b.center, b.half);
        velocity = b.velocity;    // one shared velocity until Phase 2: the scripted box wins
    }
    uint32_t n = static_cast<uint32_t>(all.size());
    m_kinematicOverflow = n > MAX_CHAR_SEGMENTS ? n - MAX_CHAR_SEGMENTS : 0u;
    if (n > MAX_CHAR_SEGMENTS) n = MAX_CHAR_SEGMENTS;
    if (n == 0) {
        cc->segmentCount = 0.0f;
        cc->legacyActive = 0.0f;
        return;
    }

    // Write segments and accumulate the union AABB (the solver's broadphase box).
    glm::vec3 mn( 1e30f);
    glm::vec3 mx(-1e30f);
    for (uint32_t i = 0; i < n; ++i) {
        const glm::vec3& c = all[i].first;
        const glm::vec3& h = all[i].second;
        cc->segments[i].center      = glm::vec4(c, 0.0f);
        cc->segments[i].halfExtents = glm::vec4(h, 0.0f);
        mn = glm::min(mn, c - h);
        mx = glm::max(mx, c + h);
    }
    cc->center       = (mn + mx) * 0.5f;
    cc->halfExtents  = (mx - mn) * 0.5f;
    cc->velocity     = velocity;
    cc->segmentCount = static_cast<float>(n);
    cc->legacyActive = static_cast<float>(n);
}

// ============================================================
// Cleanup
// ============================================================

// ============================================================
// Position logging
// ============================================================

bool GpuParticlePhysics::startPositionLog(const std::string& filePath) {
    if (m_positionLogging) stopPositionLog();
    m_posLogFile.open(filePath, std::ios::out | std::ios::trunc);
    if (!m_posLogFile.is_open()) {
        LOG_ERROR("GpuParticlePhysics", "Failed to open position log: {}", filePath);
        return false;
    }
    // Write CSV header comment
    m_posLogFile << "# Phyxel GPU Particle Position Log\n"
                 << "# F,frame,dt,ticks,alpha,activeCount,char_cx,char_cy,char_cz,char_vx,char_vy,char_vz,char_active\n"
                 << "# P,frame,slot,pos_x,pos_y,pos_z,prev_x,prev_y,prev_z,flags,material\n";
    m_posLogFile.flush();
    m_posLogFrameCounter = 0;
    m_readbackPending = false;
    m_positionLogging = true;
    LOG_INFO("GpuParticlePhysics", "Position logging started: {}", filePath);
    return true;
}

void GpuParticlePhysics::stopPositionLog() {
    if (!m_positionLogging) return;
    m_positionLogging = false;
    m_readbackPending = false;
    if (m_posLogFile.is_open()) {
        m_posLogFile.close();
    }
    LOG_INFO("GpuParticlePhysics", "Position logging stopped ({} frames captured)", m_posLogFrameCounter);
}

// ============================================================
// Settle probe (docs/DebrisSettlingPlan.md §3)
// ============================================================

bool GpuParticlePhysics::createProbeBuffers() {
    if (m_probe[0].buffer != VK_NULL_HANDLE) return true;
    const VkDeviceSize size = PROBE_TICK_STRIDE * PROBE_MAX_TICKS;
    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties(m_physDevice, &props);
    for (ProbeSlot& s : m_probe) {
        VkBufferCreateInfo bi{};
        bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size        = size;
        bi.usage       = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(m_device, &bi, nullptr, &s.buffer) != VK_SUCCESS) return false;
        VkMemoryRequirements req;
        vkGetBufferMemoryRequirements(m_device, s.buffer, &req);
        const VkMemoryPropertyFlags want =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        uint32_t memType = UINT32_MAX;
        for (uint32_t j = 0; j < props.memoryTypeCount; ++j) {
            if ((req.memoryTypeBits & (1u << j)) &&
                (props.memoryTypes[j].propertyFlags & want) == want) { memType = j; break; }
        }
        if (memType == UINT32_MAX) return false;
        VkMemoryAllocateInfo ai{};
        ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize  = req.size;
        ai.memoryTypeIndex = memType;
        if (vkAllocateMemory(m_device, &ai, nullptr, &s.memory) != VK_SUCCESS ||
            vkBindBufferMemory(m_device, s.buffer, s.memory, 0) != VK_SUCCESS ||
            vkMapMemory(m_device, s.memory, 0, size, 0, &s.mapped) != VK_SUCCESS) return false;
        s.ticks = 0;
    }
    LOG_INFO_FMT("GpuParticlePhysics", "Settle probe ring: " << PROBE_FRAMES << " x "
        << (size / 1024) << " KB host-visible");
    return true;
}

void GpuParticlePhysics::startSettleProbe(const Core::DebrisSettleAnalyzer::Config& cfg) {
    if (!m_initialized || !createProbeBuffers()) {
        LOG_ERROR("GpuParticlePhysics", "Settle probe: readback ring unavailable");
        return;
    }
    Core::DebrisSettleAnalyzer::Config c = cfg;
    c.dt = FIXED_DT;
    c.gravity = -GRAVITY;
    m_settle.setConfig(c);
    m_settle.reset();
    ++m_probeGeneration;     // ticks already in flight belong to the previous run
    m_probeActive = true;
    LOG_INFO("GpuParticlePhysics", "Settle probe started");
}

void GpuParticlePhysics::stopSettleProbe() {
    if (!m_probeActive) return;
    m_probeActive = false;
    LOG_INFO("GpuParticlePhysics", "Settle probe stopped ({} ticks analysed)", m_settle.tickCount());
}

void GpuParticlePhysics::recordProbeCopy(VkCommandBuffer cmd, uint32_t count, uint32_t tick) {
    ProbeSlot& s = m_probe[m_probeFrame];
    if (!s.buffer || tick >= PROBE_MAX_TICKS || count == 0) return;
    if (s.ticks == 0) s.generation = m_probeGeneration;

    // Compute writes (sync_out / warmstart save / colouring / CSR count) → transfer reads.
    VkMemoryBarrier toTransfer{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    toTransfer.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 1, &toTransfer, 0, nullptr, 0, nullptr);

    const VkDeviceSize base = PROBE_TICK_STRIDE * tick;
    VkBufferCopy cp{};
    cp.srcOffset = 0;
    cp.dstOffset = base + PROBE_PARTICLES_OFF;
    cp.size      = static_cast<VkDeviceSize>(count) * sizeof(GpuParticle);
    vkCmdCopyBuffer(cmd, m_particleBuffer, s.buffer, 1, &cp);
    cp.dstOffset = base + PROBE_HDR_OFF;
    cp.size      = PROBE_HDR_UINTS * sizeof(uint32_t);
    vkCmdCopyBuffer(cmd, m_solverStateBuffer, s.buffer, 1, &cp);
    cp.dstOffset = base + PROBE_COLOR_OFF;
    cp.size      = static_cast<VkDeviceSize>(count) * sizeof(uint32_t);
    vkCmdCopyBuffer(cmd, m_bodyColorBuffer, s.buffer, 1, &cp);
    cp.dstOffset = base + PROBE_CCOUNT_OFF;
    vkCmdCopyBuffer(cmd, m_bodyConstraintCountBuffer, s.buffer, 1, &cp);

    // The next tick's header fill (transfer) and compute passes overwrite what we just read;
    // the host reads the slot after the frame fence.
    VkMemoryBarrier after{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    after.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    after.dstAccessMask = VK_ACCESS_HOST_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT |
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 1, &after, 0, nullptr, 0, nullptr);

    s.count[tick] = count;
    s.ticks = tick + 1;
}

void GpuParticlePhysics::consumeProbeSlot(uint32_t slot) {
    ProbeSlot& s = m_probe[slot];
    const uint32_t ticks = s.ticks;
    s.ticks = 0;
    if (!m_probeActive || !s.mapped || ticks == 0 || s.generation != m_probeGeneration) return;

    std::vector<Core::SettleBodySample> samples;
    for (uint32_t t = 0; t < ticks; ++t) {
        const auto* bytes = static_cast<const uint8_t*>(s.mapped) + PROBE_TICK_STRIDE * t;
        const auto* parts = reinterpret_cast<const GpuParticle*>(bytes + PROBE_PARTICLES_OFF);
        const auto* hdr   = reinterpret_cast<const uint32_t*>(bytes + PROBE_HDR_OFF);
        const auto* color = reinterpret_cast<const uint32_t*>(bytes + PROBE_COLOR_OFF);
        const auto* ccnt  = reinterpret_cast<const uint32_t*>(bytes + PROBE_CCOUNT_OFF);
        const uint32_t n  = s.count[t];

        samples.assign(n, Core::SettleBodySample{});
        for (uint32_t i = 0; i < n; ++i) {
            const GpuParticle& p = parts[i];
            Core::SettleBodySample& b  = samples[i];
            b.position        = p.position;
            b.prevPosition    = p.prevPosition;
            b.angularVel      = p.angularVel;
            b.rotation        = p.rotation;
            b.flags           = p.flags;
            const uint32_t mi = p.materialIndex & DebrisShared::MATERIAL_MASK;
            b.mass            = mi < m_materialMassCpu.size() ? m_materialMassCpu[mi] : 1.0f;
            b.radius          = 0.5f * std::max(p.scale.x, std::max(p.scale.y, p.scale.z));
            b.color           = color[i];
            b.constraintCount = ccnt[i];
        }
        Core::SettleSolverCounters c;
        c.constraintsEmitted  = hdr[DebrisShared::SS_CONSTRAINT_COUNT];
        c.constraintCap       = MAX_CONSTRAINTS;
        c.warmstartHits       = hdr[DebrisShared::SS_WARMSTART_HITS];
        c.hardContactFires    = hdr[DebrisShared::SS_HARDCONTACT_FIRES];
        c.hardContactMaxDepth = static_cast<float>(hdr[DebrisShared::SS_HARDCONTACT_DEPTH_UM]) * 1.0e-6f;
        c.wakeRequests        = hdr[DebrisShared::SS_WAKE_REQUESTS];
        c.frozenUnknown       = hdr[DebrisShared::SS_FROZEN_UNKNOWN];
        c.kinematicContacts   = hdr[DebrisShared::SS_KINEMATIC_CONTACTS];
        c.impulsesApplied     = hdr[DebrisShared::SS_IMPULSES_APPLIED];
        c.maxColors           = MAX_COLORS;
        c.uncoloredSolved     = true;   // recordComputeCommandsNew's final UNCOLORED sweep
        m_settle.addTick(samples, c);
    }
}

// ============================================================
// Cleanup
// ============================================================

void GpuParticlePhysics::cleanup() {
    stopPositionLog();
    if (m_device == VK_NULL_HANDLE) return;

    m_probeActive = false;
    for (ProbeSlot& s : m_probe) {
        if (s.mapped) { vkUnmapMemory(m_device, s.memory); s.mapped = nullptr; }
        if (s.buffer) { vkDestroyBuffer(m_device, s.buffer, nullptr); s.buffer = VK_NULL_HANDLE; }
        if (s.memory) { vkFreeMemory(m_device, s.memory, nullptr); s.memory = VK_NULL_HANDLE; }
    }

    m_gridClearPass.cleanup();
    m_gridBuildPass.cleanup();
    m_sortScatterPass.cleanup();
    m_scanBlockPass.cleanup();
    m_scanBlockSumsPass.cleanup();
    m_scanAddPass.cleanup();
    m_expandPass.cleanup();

    m_solverSyncInPass.cleanup();
    m_solverIntegratePass.cleanup();
    m_solverNarrowphasePass.cleanup();
    m_solverVoxelPass.cleanup();
    m_solverDualPass.cleanup();
    m_solverPrimalPass.cleanup();
    m_solverSyncOutPass.cleanup();
    m_solverWarmstartSavePass.cleanup();
    m_solverHardContactPass.cleanup();
    m_csrClearPass.cleanup();
    m_csrCountPass.cleanup();
    m_prefixSumPass.cleanup();
    m_csrScatterPass.cleanup();
    m_bodyColorPass.cleanup();

    auto destroyBuf = [&](VkBuffer& buf, VkDeviceMemory& mem) {
        if (buf  != VK_NULL_HANDLE) { vkDestroyBuffer(m_device, buf, nullptr);  buf = VK_NULL_HANDLE; }
        if (mem  != VK_NULL_HANDLE) { vkFreeMemory(m_device, mem, nullptr);     mem = VK_NULL_HANDLE; }
    };

    // Unmap before freeing
    if (m_stagingMapped)       { vkUnmapMemory(m_device, m_stagingMem);       m_stagingMapped      = nullptr; }
    if (m_characterMapped)     { vkUnmapMemory(m_device, m_characterMem);     m_characterMapped    = nullptr; }
    if (m_materialPhysMapped)  { vkUnmapMemory(m_device, m_materialPhysMem);  m_materialPhysMapped = nullptr; }
    if (m_readbackMapped)      { vkUnmapMemory(m_device, m_readbackMem);      m_readbackMapped     = nullptr; }

    destroyBuf(m_particleBuffer,      m_particleMem);
    destroyBuf(m_faceBuffer,          m_faceMem);
    destroyBuf(m_stagingBuffer,       m_stagingMem);
    destroyBuf(m_indirectDrawBuffer,  m_indirectDrawMem);
    destroyBuf(m_characterBuffer,     m_characterMem);
    destroyBuf(m_materialPhysBuffer,  m_materialPhysMem);
    destroyBuf(m_gridCellCountBuffer,  m_gridCellCountMem);
    destroyBuf(m_gridCellOffsetBuffer, m_gridCellOffsetMem);
    destroyBuf(m_scanBlockSumsBuffer,  m_scanBlockSumsMem);
    destroyBuf(m_sortedParticleBuffer, m_sortedParticleMem);
    destroyBuf(m_sortedIndexBuffer,    m_sortedIndexMem);
    destroyBuf(m_matTexBuffer,        m_matTexMem);
    destroyBuf(m_readbackBuffer,      m_readbackMem);
    destroyBuf(m_solverBodyBuffer,          m_solverBodyMem);
    destroyBuf(m_constraintBuffer,          m_constraintMem);
    destroyBuf(m_solverStateBuffer,         m_solverStateMem);
    destroyBuf(m_warmstartBuffer,           m_warmstartMem);
    destroyBuf(m_bodyColorBuffer,           m_bodyColorMem);
    destroyBuf(m_bodyConstraintCountBuffer, m_bodyConstraintCountMem);
    destroyBuf(m_bodyConstraintOffsetBuffer,m_bodyConstraintOffsetMem);
    destroyBuf(m_bodyConstraintCursorBuffer,m_bodyConstraintCursorMem);
    destroyBuf(m_bodyConstraintListBuffer,  m_bodyConstraintListMem);

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
