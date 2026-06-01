#include "core/GpuParticlePhysics.h"
#include "core/MaterialRegistry.h"
#include "vulkan/VulkanDevice.h"
#include "core/AssetManager.h"
#include "physics/Material.h"
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
            gpu.friction        = std::max(0.0f, 1.0f - mp.friction * 0.36f);
            gpu.linearDamp      = std::max(0.9f, 1.0f - mp.linearDamping * 0.05f);
            gpu.angularDamp     = std::max(0.97f, 1.0f - mp.angularDamping * 0.03f);
            gpu.breakForceScale = mp.breakForceMultiplier;
        } else {
            gpu.mass            = 1.0f;
            gpu.restitution     = 0.3f;
            gpu.friction        = 0.82f;
            gpu.linearDamp      = 0.995f;
            gpu.angularDamp     = 0.97f;
            gpu.breakForceScale = 1.0f;
        }
        gpu.pad0 = 0.0f;
        gpu.pad1 = 0.0f;
        dst[i] = gpu;

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

    // Push constant sizes (must match the shader PC blocks)
    struct IntegratePC  { float dt; float gravity; uint32_t count; float sleepThreshSq; float lifetimeDt; };
    struct CollidePC    { uint32_t count; float dt; float sleepThreshSq; float gravity; uint32_t iteration; };
    struct ExpandPC     { uint32_t count; uint32_t maxFaceSlots; float interpAlpha; };
    struct GridClearPC  { uint32_t cellCount; };
    struct GridBuildPC  { uint32_t count; };
    struct SortScanPC   { uint32_t cellCount; };
    struct SortScatterPC{ uint32_t count; };

    // grid clear: binding 0 = gridCellCount (rw)
    if (!m_gridClearPass.create(m_device, shader("particle_grid_clear.comp.spv"),
                                 1, sizeof(GridClearPC)))
        return false;

    // grid build: binding 0 = particles (ro), binding 1 = gridCellCount (rw)
    if (!m_gridBuildPass.create(m_device, shader("particle_grid_build.comp.spv"),
                                 2, sizeof(GridBuildPC)))
        return false;

    // sort scan: binding 0 = gridCellCount (ro), binding 1 = gridCellOffset (rw)
    if (!m_sortScanPass.create(m_device, shader("particle_sort_scan.comp.spv"),
                                2, sizeof(SortScanPC)))
        return false;

    // sort scatter: binding 0 = particles (ro), binding 1 = gridCellOffset (rw atomic),
    //               binding 2 = sortedParticles (wo), binding 3 = sortedIndices (wo)
    if (!m_sortScatterPass.create(m_device, shader("particle_sort_scatter.comp.spv"),
                                   4, sizeof(SortScatterPC)))
        return false;

    // integrate: binding 0 = particles (rw), binding 1 = material physics (ro)
    if (!m_integratePass.create(m_device, shader("particle_integrate.comp.spv"),
                                 2, sizeof(IntegratePC)))
        return false;

    // collide: binding 0 = particles (rw), binding 1 = occupancy grid (ro),
    //          binding 2 = character AABB (ro), binding 3 = material physics (ro),
    //          binding 4 = gridCellCount (ro), binding 5 = gridCellOffset (ro),
    //          binding 6 = sortedParticles (ro), binding 7 = sortedIndices (ro)
    if (!m_collidePass.create(m_device, shader("particle_collide.comp.spv"),
                               8, sizeof(CollidePC)))
        return false;

    // expand: binding 0 = particles (ro), binding 1 = matTexTable (ro),
    //         binding 2 = faceBuffer (wo), binding 3 = indirectCmd (rw atomic)
    if (!m_expandPass.create(m_device, shader("particle_expand.comp.spv"),
                              4, sizeof(ExpandPC)))
        return false;

    // Wire buffers to each pipeline's descriptor set
    VkDeviceSize particleSize      = static_cast<VkDeviceSize>(MAX_PARTICLES) * sizeof(GpuParticle);
    VkDeviceSize faceSize          = static_cast<VkDeviceSize>(MAX_FACE_SLOTS) * 64;
    VkDeviceSize occSize           = static_cast<VkDeviceSize>(OCC_TOTAL_WORDS) * sizeof(uint32_t);
    uint32_t     matCount          = static_cast<uint32_t>(Core::MaterialRegistry::instance().getMaterialCount());
    uint32_t     matTableSize      = matCount * 6 * sizeof(uint32_t);
    VkDeviceSize matPhysSize       = static_cast<VkDeviceSize>(matCount) * sizeof(MaterialPhysicsGpu);
    VkDeviceSize gridCellSize      = static_cast<VkDeviceSize>(GRID_CELLS) * sizeof(uint32_t);
    VkDeviceSize sortedParticleSize= static_cast<VkDeviceSize>(MAX_PARTICLES) * sizeof(GpuParticle);
    VkDeviceSize sortedIndexSize   = static_cast<VkDeviceSize>(MAX_PARTICLES) * sizeof(uint32_t);

    m_gridClearPass.bindBuffer(0, m_gridCellCountBuffer, gridCellSize);
    m_gridClearPass.updateDescriptors();

    m_gridBuildPass.bindBuffer(0, m_particleBuffer,      particleSize);
    m_gridBuildPass.bindBuffer(1, m_gridCellCountBuffer, gridCellSize);
    m_gridBuildPass.updateDescriptors();

    m_sortScanPass.bindBuffer(0, m_gridCellCountBuffer,  gridCellSize);
    m_sortScanPass.bindBuffer(1, m_gridCellOffsetBuffer, gridCellSize);
    m_sortScanPass.updateDescriptors();

    m_sortScatterPass.bindBuffer(0, m_particleBuffer,       particleSize);
    m_sortScatterPass.bindBuffer(1, m_gridCellOffsetBuffer, gridCellSize);
    m_sortScatterPass.bindBuffer(2, m_sortedParticleBuffer, sortedParticleSize);
    m_sortScatterPass.bindBuffer(3, m_sortedIndexBuffer,    sortedIndexSize);
    m_sortScatterPass.updateDescriptors();

    m_integratePass.bindBuffer(0, m_particleBuffer,    particleSize);
    m_integratePass.bindBuffer(1, m_materialPhysBuffer, matPhysSize);
    m_integratePass.updateDescriptors();

    m_collidePass.bindBuffer(0, m_particleBuffer,       particleSize);
    m_collidePass.bindBuffer(1, m_occupancyBuffer,      occSize);
    m_collidePass.bindBuffer(2, m_characterBuffer,      static_cast<VkDeviceSize>(sizeof(CharacterCollider)));
    m_collidePass.bindBuffer(3, m_materialPhysBuffer,   matPhysSize);
    m_collidePass.bindBuffer(4, m_gridCellCountBuffer,  gridCellSize);
    m_collidePass.bindBuffer(5, m_gridCellOffsetBuffer, gridCellSize);
    m_collidePass.bindBuffer(6, m_sortedParticleBuffer, sortedParticleSize);
    m_collidePass.bindBuffer(7, m_sortedIndexBuffer,    sortedIndexSize);
    m_collidePass.updateDescriptors();

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
        m_solverStateBuffer, m_solverStateMem);

    // Warmstart entries: HASH_CAP × 64 bytes. Indexed by hashInsert(wsKey).
    dev->createStorageBuffer(
        static_cast<VkDeviceSize>(HASH_CAP) * 64,
        m_warmstartBuffer, m_warmstartMem);

    // Body color buffer (one uint per body for Jones-Plassmann coloring)
    dev->createStorageBuffer(
        static_cast<VkDeviceSize>(MAX_PARTICLES) * sizeof(uint32_t),
        m_bodyColorBuffer, m_bodyColorMem);

    dev->createStorageBuffer(
        static_cast<VkDeviceSize>(MAX_PARTICLES) * sizeof(uint32_t),
        m_bodyConstraintCountBuffer, m_bodyConstraintCountMem);

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

    struct SyncInPC      { uint32_t count; float dt; };
    struct IntegratePC   { uint32_t count; float dt; float gravity; float pad; };
    struct NpPC          { uint32_t count; uint32_t maxConstraints; float p0; float p1; };
    struct DualPC        { uint32_t maxConstraints; float dt; uint32_t pad0; float pad1; };
    struct PrimalPC      { uint32_t bodyCount; float dt; uint32_t targetColor; float pad; };
    struct SyncOutPC     { uint32_t count; float dt; float lifetimeDt; float pad; };
    struct WarmstartSavePC { uint32_t maxConstraints; };
    struct HardContactPC { uint32_t count; float pad0; float pad1; float pad2; };
    struct CsrClearPC    { uint32_t bodyCount; uint32_t maxConstraints; };
    struct CsrCountPC    { uint32_t maxConstraints; };
    struct PrefixSumPC   { uint32_t bodyCount; };
    struct BodyColorPC   { uint32_t bodyCount; };

    uint32_t     matCount       = static_cast<uint32_t>(Core::MaterialRegistry::instance().getMaterialCount());
    VkDeviceSize particleSize   = static_cast<VkDeviceSize>(MAX_PARTICLES)   * sizeof(GpuParticle);
    VkDeviceSize bodySize       = static_cast<VkDeviceSize>(MAX_PARTICLES)   * 208;
    VkDeviceSize constrSize     = static_cast<VkDeviceSize>(MAX_CONSTRAINTS) * 128;
    VkDeviceSize stateSize      = static_cast<VkDeviceSize>(SOLVER_STATE_UINTS) * sizeof(uint32_t);
    VkDeviceSize warmstartSize  = static_cast<VkDeviceSize>(HASH_CAP)        * 64;
    VkDeviceSize matPhysSize    = static_cast<VkDeviceSize>(matCount)        * sizeof(MaterialPhysicsGpu);
    VkDeviceSize occSize        = static_cast<VkDeviceSize>(OCC_TOTAL_WORDS) * sizeof(uint32_t);
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

    // solver_integrate: bodies, materials, particles, character collider
    if (!m_solverIntegratePass.create(m_device, shader("solver_integrate.comp.spv"), 4, sizeof(IntegratePC))) return false;
    m_solverIntegratePass.bindBuffer(0, m_solverBodyBuffer,   bodySize);
    m_solverIntegratePass.bindBuffer(1, m_materialPhysBuffer, matPhysSize);
    m_solverIntegratePass.bindBuffer(2, m_particleBuffer,     particleSize);
    m_solverIntegratePass.bindBuffer(3, m_characterBuffer,    static_cast<VkDeviceSize>(sizeof(CharacterCollider)));
    m_solverIntegratePass.updateDescriptors();

    // solver_narrowphase: bodies, constraints, state, gridCount, gridOffset, sortedIndices, warmstarts
    if (!m_solverNarrowphasePass.create(m_device, shader("solver_narrowphase.comp.spv"), 7, sizeof(NpPC))) return false;
    m_solverNarrowphasePass.bindBuffer(0, m_solverBodyBuffer,    bodySize);
    m_solverNarrowphasePass.bindBuffer(1, m_constraintBuffer,    constrSize);
    m_solverNarrowphasePass.bindBuffer(2, m_solverStateBuffer,   stateSize);
    m_solverNarrowphasePass.bindBuffer(3, m_gridCellCountBuffer, gridCellSize);
    m_solverNarrowphasePass.bindBuffer(4, m_gridCellOffsetBuffer,gridCellSize);
    m_solverNarrowphasePass.bindBuffer(5, m_sortedIndexBuffer,   sortedIdxSize);
    m_solverNarrowphasePass.bindBuffer(6, m_warmstartBuffer,     warmstartSize);
    m_solverNarrowphasePass.updateDescriptors();

    // solver_voxel: bodies, constraints, state, occupancy, warmstarts
    if (!m_solverVoxelPass.create(m_device, shader("solver_voxel.comp.spv"), 5, sizeof(NpPC))) return false;
    m_solverVoxelPass.bindBuffer(0, m_solverBodyBuffer,  bodySize);
    m_solverVoxelPass.bindBuffer(1, m_constraintBuffer,  constrSize);
    m_solverVoxelPass.bindBuffer(2, m_solverStateBuffer, stateSize);
    m_solverVoxelPass.bindBuffer(3, m_occupancyBuffer,   occSize);
    m_solverVoxelPass.bindBuffer(4, m_warmstartBuffer,   warmstartSize);
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
    if (!m_solverWarmstartSavePass.create(m_device, shader("solver_warmstart_save.comp.spv"), 3, sizeof(WarmstartSavePC))) return false;
    m_solverWarmstartSavePass.bindBuffer(0, m_constraintBuffer,  constrSize);
    m_solverWarmstartSavePass.bindBuffer(1, m_warmstartBuffer,   warmstartSize);
    m_solverWarmstartSavePass.bindBuffer(2, m_solverStateBuffer, stateSize);
    m_solverWarmstartSavePass.updateDescriptors();

    // solver_hardcontact: bodies(rw), occupancy(ro)
    // Final positional safety pass — projects dynamic bodies out of static
    // voxel terrain in case AVBD couldn't fully resolve under heavy stacking.
    if (!m_solverHardContactPass.create(m_device, shader("solver_hardcontact.comp.spv"), 2, sizeof(HardContactPC))) return false;
    m_solverHardContactPass.bindBuffer(0, m_solverBodyBuffer, bodySize);
    m_solverHardContactPass.bindBuffer(1, m_occupancyBuffer,  occSize);
    m_solverHardContactPass.updateDescriptors();

    // solver_csr_clear: bodyConstraintCount(rw), bodyColor(rw)
    if (!m_csrClearPass.create(m_device, shader("solver_csr_clear.comp.spv"), 2, sizeof(CsrClearPC))) return false;
    m_csrClearPass.bindBuffer(0, m_bodyConstraintCountBuffer, bodyUintSize);
    m_csrClearPass.bindBuffer(1, m_bodyColorBuffer,           bodyUintSize);
    m_csrClearPass.updateDescriptors();

    // solver_csr_count: constraints(ro), state(ro), bodyConstraintCount(rw)
    if (!m_csrCountPass.create(m_device, shader("solver_csr_count.comp.spv"), 3, sizeof(CsrCountPC))) return false;
    m_csrCountPass.bindBuffer(0, m_constraintBuffer,         constrSize);
    m_csrCountPass.bindBuffer(1, m_solverStateBuffer,        stateSize);
    m_csrCountPass.bindBuffer(2, m_bodyConstraintCountBuffer,bodyUintSize);
    m_csrCountPass.updateDescriptors();

    // solver_prefix_sum: count(ro), offset(rw), cursor(rw)
    if (!m_prefixSumPass.create(m_device, shader("solver_prefix_sum.comp.spv"), 3, sizeof(PrefixSumPC))) return false;
    m_prefixSumPass.bindBuffer(0, m_bodyConstraintCountBuffer, bodyUintSize);
    m_prefixSumPass.bindBuffer(1, m_bodyConstraintOffsetBuffer,bodyUintSize);
    m_prefixSumPass.bindBuffer(2, m_bodyConstraintCursorBuffer,bodyUintSize);
    m_prefixSumPass.updateDescriptors();

    // solver_csr_scatter: constraints(ro), state(ro), cursor(rw), adjacencyList(rw)
    if (!m_csrScatterPass.create(m_device, shader("solver_csr_scatter.comp.spv"), 4, sizeof(CsrCountPC))) return false;
    m_csrScatterPass.bindBuffer(0, m_constraintBuffer,          constrSize);
    m_csrScatterPass.bindBuffer(1, m_solverStateBuffer,         stateSize);
    m_csrScatterPass.bindBuffer(2, m_bodyConstraintCursorBuffer,bodyUintSize);
    m_csrScatterPass.bindBuffer(3, m_bodyConstraintListBuffer,  adjListSize);
    m_csrScatterPass.updateDescriptors();

    // solver_body_color: constraints(ro), state(ro), bodyColor(rw), count(ro), offset(ro), list(ro)
    if (!m_bodyColorPass.create(m_device, shader("solver_body_color.comp.spv"), 6, sizeof(BodyColorPC))) return false;
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

void GpuParticlePhysics::recordComputeCommandsNew(VkCommandBuffer cmd, uint32_t count, float lifetimeDt) {
    const uint32_t groups        = (count + 255u) / 256u;
    const uint32_t maxConstrGrps = (MAX_CONSTRAINTS + 255u) / 256u;

    auto ssBarrier = [&](VkBuffer buf) {
        insertBarrier(cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            buf);
    };

    struct SyncInPC      { uint32_t count; float dt; };
    struct IntegratePC   { uint32_t count; float dt; float gravity; float pad; };
    struct NpPC          { uint32_t count; uint32_t maxConstraints; float p0; float p1; };
    struct DualPC        { uint32_t maxConstraints; float dt; uint32_t pad0; float pad1; };
    struct PrimalPC      { uint32_t bodyCount; float dt; uint32_t targetColor; float pad; };
    struct SyncOutPC     { uint32_t count; float dt; float lifetimeDt; float pad; };
    struct WarmstartSavePC { uint32_t maxConstraints; };
    struct HardContactPC { uint32_t count; float pad0; float pad1; float pad2; };
    struct GridClearPC   { uint32_t cellCount; };
    struct GridBuildPC   { uint32_t count; };
    struct SortScanPC    { uint32_t cellCount; };
    struct SortScatterPC { uint32_t count; };
    struct CsrClearPC    { uint32_t bodyCount; uint32_t maxConstraints; };
    struct CsrCountPC    { uint32_t maxConstraints; };
    struct PrefixSumPC   { uint32_t bodyCount; };
    struct BodyColorPC   { uint32_t bodyCount; };

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
        m_hashInitialized = true;
    }
    insertBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
        m_solverStateBuffer);

    // ---- 1. Sync in: GpuParticle → SolverBody ----
    {
        SyncInPC pc{ count, FIXED_DT };
        m_solverSyncInPass.bind(cmd);
        m_solverSyncInPass.pushConstants(cmd, &pc, sizeof(pc));
        m_solverSyncInPass.dispatch(cmd, groups);
    }
    ssBarrier(m_solverBodyBuffer);

    // ---- 2. Integrate: apply gravity + damping, predict position ----
    {
        IntegratePC pc{ count, FIXED_DT, GRAVITY, 0.0f };
        m_solverIntegratePass.bind(cmd);
        m_solverIntegratePass.pushConstants(cmd, &pc, sizeof(pc));
        m_solverIntegratePass.dispatch(cmd, groups);
    }
    ssBarrier(m_solverBodyBuffer);

    // ---- 3. Grid sort (reads m_particleBuffer — previous-tick positions for broadphase) ----
    {
        GridClearPC gc{ static_cast<uint32_t>(GRID_CELLS) };
        m_gridClearPass.bind(cmd);
        m_gridClearPass.pushConstants(cmd, &gc, sizeof(gc));
        m_gridClearPass.dispatch(cmd, (GRID_CELLS + 255u) / 256u);
    }
    ssBarrier(m_gridCellCountBuffer);

    {
        GridBuildPC gb{ count };
        m_gridBuildPass.bind(cmd);
        m_gridBuildPass.pushConstants(cmd, &gb, sizeof(gb));
        m_gridBuildPass.dispatch(cmd, groups);
    }
    insertBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
        m_gridCellCountBuffer);

    {
        SortScanPC ss{ static_cast<uint32_t>(GRID_CELLS) };
        m_sortScanPass.bind(cmd);
        m_sortScanPass.pushConstants(cmd, &ss, sizeof(ss));
        m_sortScanPass.dispatch(cmd, 1);
    }
    ssBarrier(m_gridCellOffsetBuffer);

    {
        SortScatterPC sc{ count };
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
        NpPC pc{ count, MAX_CONSTRAINTS, 0.0f, 0.0f };
        m_solverNarrowphasePass.bind(cmd);
        m_solverNarrowphasePass.pushConstants(cmd, &pc, sizeof(pc));
        m_solverNarrowphasePass.dispatch(cmd, groups);
    }
    ssBarrier(m_constraintBuffer);
    ssBarrier(m_solverStateBuffer);

    // ---- 4b. Voxel contacts → constraints (appended) ----
    {
        NpPC pc{ count, MAX_CONSTRAINTS, 0.0f, 0.0f };
        m_solverVoxelPass.bind(cmd);
        m_solverVoxelPass.pushConstants(cmd, &pc, sizeof(pc));
        m_solverVoxelPass.dispatch(cmd, groups);
    }
    ssBarrier(m_constraintBuffer);
    ssBarrier(m_solverStateBuffer);

    // ---- 5. Build CSR adjacency structure for graph coloring ----
    // 5a. Clear bodyConstraintCount[] and constraintColor[]
    {
        CsrClearPC pc{ count, MAX_CONSTRAINTS };
        m_csrClearPass.bind(cmd);
        m_csrClearPass.pushConstants(cmd, &pc, sizeof(pc));
        m_csrClearPass.dispatch(cmd, maxConstrGrps); // covers both arrays (MAX_CONSTRAINTS > count)
    }
    ssBarrier(m_bodyConstraintCountBuffer);
    ssBarrier(m_bodyColorBuffer);

    // 5b. Count how many constraints each body participates in
    {
        CsrCountPC pc{ MAX_CONSTRAINTS };
        m_csrCountPass.bind(cmd);
        m_csrCountPass.pushConstants(cmd, &pc, sizeof(pc));
        m_csrCountPass.dispatch(cmd, maxConstrGrps);
    }
    ssBarrier(m_bodyConstraintCountBuffer);

    // 5c. Exclusive prefix sum → bodyConstraintOffset[] and bodyConstraintCursor[]
    {
        PrefixSumPC pc{ count };
        m_prefixSumPass.bind(cmd);
        m_prefixSumPass.pushConstants(cmd, &pc, sizeof(pc));
        m_prefixSumPass.dispatch(cmd, 1);
    }
    ssBarrier(m_bodyConstraintOffsetBuffer);
    ssBarrier(m_bodyConstraintCursorBuffer);

    // 5d. Scatter constraint indices into adjacency list using atomic cursors
    {
        CsrCountPC pc{ MAX_CONSTRAINTS };
        m_csrScatterPass.bind(cmd);
        m_csrScatterPass.pushConstants(cmd, &pc, sizeof(pc));
        m_csrScatterPass.dispatch(cmd, maxConstrGrps);
    }
    ssBarrier(m_bodyConstraintListBuffer);

    // ---- 6. Body graph coloring: Jones-Plassmann, 16 passes ----
    // Colors BODIES: two bodies are adjacent if they share a constraint.
    // Same-color bodies have no shared constraints → safe for parallel primal writes.
    {
        BodyColorPC pc{ count };
        for (int gc = 0; gc < 16; ++gc) {
            m_bodyColorPass.bind(cmd);
            m_bodyColorPass.pushConstants(cmd, &pc, sizeof(pc));
            m_bodyColorPass.dispatch(cmd, groups);
            ssBarrier(m_bodyColorBuffer);
        }
    }

    // ---- 7. AVBD dual+primal solve loop ----
    // solveDual: per-constraint, updates lambda and grows penalty (fully parallel, no body writes)
    // solvePrimal: per-body, assembles 6×6 block system and solves via LDL; one color per pass
    for (int iter = 0; iter < SOLVE_ITERATIONS; ++iter) {
        // Dual: update all constraint lambdas/penalties
        {
            DualPC pc{ MAX_CONSTRAINTS, FIXED_DT, 0u, 0.0f };
            m_solverDualPass.bind(cmd);
            m_solverDualPass.pushConstants(cmd, &pc, sizeof(pc));
            m_solverDualPass.dispatch(cmd, maxConstrGrps);
        }
        ssBarrier(m_constraintBuffer);

        // Primal: solve per body, one color at a time
        for (uint32_t color = 0; color < MAX_COLORS; ++color) {
            PrimalPC pc{ count, FIXED_DT, color, 0.0f };
            m_solverPrimalPass.bind(cmd);
            m_solverPrimalPass.pushConstants(cmd, &pc, sizeof(pc));
            m_solverPrimalPass.dispatch(cmd, groups);
            ssBarrier(m_solverBodyBuffer);
        }
    }

    // ---- 7b. Hard-contact safety pass ----
    // Pure positional projection of dynamic bodies out of any remaining
    // static-voxel overlap left by the AVBD solver. Acts only when AVBD
    // failed to fully resolve (e.g. deep stacks). Per body, samples the
    // static occupancy grid in the body AABB and pushes out along MTV.
    {
        HardContactPC pc{ count, 0.0f, 0.0f, 0.0f };
        m_solverHardContactPass.bind(cmd);
        m_solverHardContactPass.pushConstants(cmd, &pc, sizeof(pc));
        m_solverHardContactPass.dispatch(cmd, groups);
    }
    ssBarrier(m_solverBodyBuffer);

    // ---- 8. Sync out: SolverBody → GpuParticle ----
    {
        SyncOutPC pc{ count, FIXED_DT, lifetimeDt, 0.0f };
        m_solverSyncOutPass.bind(cmd);
        m_solverSyncOutPass.pushConstants(cmd, &pc, sizeof(pc));
        m_solverSyncOutPass.dispatch(cmd, groups);
    }
    ssBarrier(m_particleBuffer);

    // ---- 9. Warmstart save: scatter final lambda/penalty/stick into hash table ----
    // Must run AFTER the dual+primal converge so that the persisted values are post-solve.
    {
        WarmstartSavePC pc{ MAX_CONSTRAINTS };
        m_solverWarmstartSavePass.bind(cmd);
        m_solverWarmstartSavePass.pushConstants(cmd, &pc, sizeof(pc));
        m_solverWarmstartSavePass.dispatch(cmd, maxConstrGrps);
    }
    ssBarrier(m_warmstartBuffer);
    ssBarrier(m_solverStateBuffer);
}

// ============================================================
// Per-frame CPU interface
// ============================================================

void GpuParticlePhysics::queueSpawn(const SpawnParams& p) {
    if (m_freeSlots.empty()) {
        LOG_WARN("GpuParticlePhysics", "Particle pool full, spawn ignored");
        return;
    }

    // Auto-start position logging on first particle spawn
    if (!m_positionLogging && m_activeCount == 0) {
        startPositionLog("particle_positions.csv");
    }

    uint32_t slot = m_freeSlots.back();
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
    gp.flags        = 1u | p.typeFlags; // PARTICLE_ACTIVE
    gp.scale        = p.scale;
    gp.materialIndex= materialNameToIndex(p.materialName);
    gp.color        = p.color;

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
                         << "," << cc->active;
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

    // Age CPU-side slots using real elapsed time (frame-rate independent).
    // The GPU integrate shader does the same via lifetimeDt push constant.
    uint32_t newHigh = 0;
    for (uint32_t i = 0; i < m_highWaterSlot; ++i) {
        if (!m_slots[i].active) continue;
        m_slots[i].lifetimeRemaining -= realDt;
        if (m_slots[i].lifetimeRemaining <= 0.0f) {
            m_slots[i].active = false;
            m_freeSlots.push_back(i);
            --m_activeCount;
        } else {
            newHigh = i + 1;
        }
    }
    m_highWaterSlot = newHigh;

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

    // Dispatch only up to the highest active slot, not the full 10K pool.
    const uint32_t count  = m_highWaterSlot;
    const uint32_t groups = (count + 255u) / 256u;
    if (groups == 0) return; // nothing to simulate

    // ---- Fixed-timestep physics loop ----
    for (uint32_t tick = 0; tick < m_physicsTicks; ++tick) {
        const float lifetimeDtThisTick = (tick == 0) ? m_lastRealDt : 0.0f;

        if (m_useNewPipeline) {
            recordComputeCommandsNew(cmd, count, lifetimeDtThisTick);
            continue;
        }

        // ===================================================================
        // LEGACY XPBD PIPELINE (NOT used by default — m_useNewPipeline is true
        // and is never toggled off). Retained for reference/fallback only.
        // Everything below until "end physics tick loop" runs ONLY in the
        // legacy path: m_integratePass (particle_integrate.comp) and
        // m_collidePass (particle_collide.comp). The AVBD pipeline above
        // (recordComputeCommandsNew) is the live path; character collision,
        // voxel collision, etc. must be maintained THERE, not here.
        // (The expand pass + grid-sort passes below the loop are SHARED and
        // are NOT legacy.)
        // ===================================================================

        // ---- 2. Integrate pass (legacy XPBD) ----
        struct IntegratePC {
            float    dt;
            float    gravity;
            uint32_t count;
            float    sleepThreshSq;
            float    lifetimeDt;
        } ipc;
        ipc.dt           = FIXED_DT;
        ipc.gravity      = GRAVITY;
        ipc.count        = count;
        ipc.sleepThreshSq= SLEEP_THRESH_SQ;
        ipc.lifetimeDt   = lifetimeDtThisTick;

        m_integratePass.bind(cmd);
        m_integratePass.pushConstants(cmd, &ipc, sizeof(ipc));
        m_integratePass.dispatch(cmd, groups);

        // Barrier: particles COMPUTE_WRITE → COMPUTE_READ/WRITE
        insertBarrier(cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            m_particleBuffer);

        // ---- 2b. Grid clear — zero per-cell counts ----
        {
            struct GridClearPC { uint32_t cellCount; } gcpc;
            gcpc.cellCount = GRID_CELLS;
            const uint32_t gridClearGroups = (GRID_CELLS + 255u) / 256u;
            m_gridClearPass.bind(cmd);
            m_gridClearPass.pushConstants(cmd, &gcpc, sizeof(gcpc));
            m_gridClearPass.dispatch(cmd, gridClearGroups);
        }

        insertBarrier(cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            m_gridCellCountBuffer);

        // ---- 2c. Grid build — count particles per cell ----
        {
            struct GridBuildPC { uint32_t count; } gbpc;
            gbpc.count = count;
            m_gridBuildPass.bind(cmd);
            m_gridBuildPass.pushConstants(cmd, &gbpc, sizeof(gbpc));
            m_gridBuildPass.dispatch(cmd, groups);
        }

        insertBarrier(cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            m_gridCellCountBuffer);

        // ---- 2d. Sort scan — exclusive prefix sum → per-cell write offsets ----
        {
            struct SortScanPC { uint32_t cellCount; } sspc;
            sspc.cellCount = GRID_CELLS;
            m_sortScanPass.bind(cmd);
            m_sortScanPass.pushConstants(cmd, &sspc, sizeof(sspc));
            m_sortScanPass.dispatch(cmd, 1); // single invocation — sequential scan
        }

        insertBarrier(cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            m_gridCellOffsetBuffer);

        // ---- 2e. Sort scatter — fill sortedParticles / sortedIndices by cell ----
        // After scatter, gridCellOffset[c] = exclusiveStart[c] + count[c] = END.
        // Collide reads [gridCellOffset[c]-gridCellCount[c], gridCellOffset[c]).
        {
            struct SortScatterPC { uint32_t count; } scpc;
            scpc.count = count;
            m_sortScatterPass.bind(cmd);
            m_sortScatterPass.pushConstants(cmd, &scpc, sizeof(scpc));
            m_sortScatterPass.dispatch(cmd, groups);
        }

        insertBarrier(cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            m_gridCellOffsetBuffer);
        insertBarrier(cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            m_sortedParticleBuffer);
        insertBarrier(cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            m_sortedIndexBuffer);

        // ---- 3. Collide pass (multi-iteration) ----
        // Terrain collision re-reads particles[i] each iteration and converges
        // correctly. Particle-particle is gated to iteration 0 in the shader
        // because sortedParticles holds a pre-tick snapshot — running it on
        // iterations 1+ would double-count corrections and cause jitter.
        struct CollidePC {
            uint32_t count;
            float    dt;
            float    sleepThreshSq;
            float    gravity;
            uint32_t iteration;
        } cpc;
        cpc.count        = count;
        cpc.dt           = FIXED_DT;
        cpc.sleepThreshSq= SLEEP_THRESH_SQ;
        cpc.gravity      = GRAVITY;

        for (int iter = 0; iter < COLLISION_ITERATIONS; ++iter) {
            cpc.iteration = static_cast<uint32_t>(iter);
            m_collidePass.bind(cmd);
            m_collidePass.pushConstants(cmd, &cpc, sizeof(cpc));
            m_collidePass.dispatch(cmd, groups);

            insertBarrier(cmd,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_ACCESS_SHADER_WRITE_BIT,
                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                m_particleBuffer);
        }

    } // end physics tick loop

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
    struct ExpandPC {
        uint32_t count;
        uint32_t maxFaceSlots;
        float    interpAlpha;
    } epc;
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

void GpuParticlePhysics::despawnAll() {
    for (uint32_t i = 0; i < m_highWaterSlot; ++i) {
        if (m_slots[i].active) {
            m_slots[i].active = false;
            m_slots[i].lifetimeRemaining = 0.0f;
            m_freeSlots.push_back(i);
        }
    }
    m_activeCount = 0;
    m_highWaterSlot = 0;
    m_pendingSpawns.clear();
    if (m_positionLogging) stopPositionLog();
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

// ============================================================
// Position logging
// ============================================================

bool GpuParticlePhysics::startPositionLog(const std::string& filePath) {
    if (m_positionLogging) stopPositionLog();
    m_posLogFile.open(filePath, std::ios::out | std::ios::trunc);
    if (!m_posLogFile.is_open()) {
        LOG_ERROR("GpuParticlePhysics", "Failed to open position log: %s", filePath.c_str());
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
    LOG_INFO("GpuParticlePhysics", "Position logging started: %s", filePath.c_str());
    return true;
}

void GpuParticlePhysics::stopPositionLog() {
    if (!m_positionLogging) return;
    m_positionLogging = false;
    m_readbackPending = false;
    if (m_posLogFile.is_open()) {
        m_posLogFile.close();
    }
    LOG_INFO("GpuParticlePhysics", "Position logging stopped (%u frames captured)", m_posLogFrameCounter);
}

// ============================================================
// Cleanup
// ============================================================

void GpuParticlePhysics::cleanup() {
    stopPositionLog();
    if (m_device == VK_NULL_HANDLE) return;

    m_gridClearPass.cleanup();
    m_gridBuildPass.cleanup();
    m_sortScanPass.cleanup();
    m_sortScatterPass.cleanup();
    m_integratePass.cleanup();
    m_collidePass.cleanup();
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
    if (m_occupancyMapped)     { vkUnmapMemory(m_device, m_occupancyMem);     m_occupancyMapped    = nullptr; }
    if (m_characterMapped)     { vkUnmapMemory(m_device, m_characterMem);     m_characterMapped    = nullptr; }
    if (m_materialPhysMapped)  { vkUnmapMemory(m_device, m_materialPhysMem);  m_materialPhysMapped = nullptr; }
    if (m_readbackMapped)      { vkUnmapMemory(m_device, m_readbackMem);      m_readbackMapped     = nullptr; }

    destroyBuf(m_particleBuffer,      m_particleMem);
    destroyBuf(m_faceBuffer,          m_faceMem);
    destroyBuf(m_stagingBuffer,       m_stagingMem);
    destroyBuf(m_indirectDrawBuffer,  m_indirectDrawMem);
    destroyBuf(m_occupancyBuffer,     m_occupancyMem);
    destroyBuf(m_characterBuffer,     m_characterMem);
    destroyBuf(m_materialPhysBuffer,  m_materialPhysMem);
    destroyBuf(m_gridCellCountBuffer,  m_gridCellCountMem);
    destroyBuf(m_gridCellOffsetBuffer, m_gridCellOffsetMem);
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
