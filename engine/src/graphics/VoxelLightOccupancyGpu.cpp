#include "graphics/VoxelLightOccupancyGpu.h"

#include <chrono>
#include <cstring>

#include "utils/Logger.h"

namespace Phyxel {
namespace Graphics {

bool VoxelLightOccupancyGpu::createHostBuffer(VkPhysicalDevice phys, VkDeviceSize size,
                                              VkBuffer& buf, VkDeviceMemory& mem, void*& mapped,
                                              const char* what) {
    VkBufferCreateInfo bi{};
    bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size        = size;
    bi.usage       = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(m_device, &bi, nullptr, &buf) != VK_SUCCESS) {
        LOG_ERROR_FMT("VoxelLightOcc", "failed to create the " << what << " buffer");
        return false;
    }

    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(m_device, buf, &req);

    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties(phys, &props);
    const VkMemoryPropertyFlags want =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    uint32_t type = UINT32_MAX;
    for (uint32_t i = 0; i < props.memoryTypeCount; ++i)
        if ((req.memoryTypeBits & (1u << i)) &&
            (props.memoryTypes[i].propertyFlags & want) == want) { type = i; break; }
    if (type == UINT32_MAX) {
        LOG_ERROR_FMT("VoxelLightOcc", "no host-coherent memory for the " << what << " buffer");
        return false;
    }

    VkMemoryAllocateInfo ai{};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = req.size;
    ai.memoryTypeIndex = type;
    if (vkAllocateMemory(m_device, &ai, nullptr, &mem) != VK_SUCCESS ||
        vkBindBufferMemory(m_device, buf, mem, 0) != VK_SUCCESS ||
        vkMapMemory(m_device, mem, 0, size, 0, &mapped) != VK_SUCCESS) {
        LOG_ERROR_FMT("VoxelLightOcc", "failed to allocate/map the " << what << " buffer");
        return false;
    }
    std::memset(mapped, 0, static_cast<size_t>(size));
    return true;
}

bool VoxelLightOccupancyGpu::initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                                        VkDeviceSize poolBytes) {
    m_device = device;
    m_poolBytes = poolBytes;

    for (uint32_t s = 0; s < kSlots; ++s) {
        if (!createHostBuffer(physicalDevice, directoryBytes(), m_dirBuffer[s], m_dirMemory[s],
                              m_dirMapped[s], "directory")) return false;
        if (!createHostBuffer(physicalDevice, poolBytes, m_poolBuffer[s], m_poolMemory[s],
                              m_poolMapped[s], "pool")) return false;

        // An empty directory must read as "no chunk", not as offset 0 — otherwise every unmapped
        // chunk would sample whatever blob happens to sit at the start of the pool.
        auto* dir = static_cast<uint32_t*>(m_dirMapped[s]);
        for (int i = 0; i < PackedOccupancyPool::kDirEntries; ++i)
            dir[i] = PackedOccupancyPool::kNoChunk;
        m_slotRevision[s] = m_packRevision;   // both hold "nothing", which is pack revision 0
    }

    m_stats.poolCapacityWords = static_cast<size_t>(poolBytes / sizeof(uint32_t));
    LOG_INFO_FMT("VoxelLightOcc", "ready: directory " << (directoryBytes() / 1024) << " KB, pool "
                 << (poolBytes / (1024 * 1024)) << " MB, x" << kSlots << " (one per frame in flight)");
    return true;
}

void VoxelLightOccupancyGpu::cleanup() {
    for (uint32_t s = 0; s < kSlots; ++s) {
        if (m_dirMapped[s])  { vkUnmapMemory(m_device, m_dirMemory[s]);  m_dirMapped[s]  = nullptr; }
        if (m_poolMapped[s]) { vkUnmapMemory(m_device, m_poolMemory[s]); m_poolMapped[s] = nullptr; }
        if (m_dirBuffer[s])  { vkDestroyBuffer(m_device, m_dirBuffer[s], nullptr);  m_dirBuffer[s]  = VK_NULL_HANDLE; }
        if (m_poolBuffer[s]) { vkDestroyBuffer(m_device, m_poolBuffer[s], nullptr); m_poolBuffer[s] = VK_NULL_HANDLE; }
        if (m_dirMemory[s])  { vkFreeMemory(m_device, m_dirMemory[s], nullptr);  m_dirMemory[s]  = VK_NULL_HANDLE; }
        if (m_poolMemory[s]) { vkFreeMemory(m_device, m_poolMemory[s], nullptr); m_poolMemory[s] = VK_NULL_HANDLE; }
        m_slotRevision[s] = 0;
    }
    m_packRevision = 0;
    m_chunks.clear();
    m_packed = {};
    m_dirty = false;
}

bool VoxelLightOccupancyGpu::setChunk(const glm::ivec3& chunkWorldOrigin,
                                      ChunkLightOccupancy blob) {
    if (PackedOccupancyPool::directoryIndex(chunkWorldOrigin, m_boxMinChunk) < 0)
        return false;                        // outside the covered box
    m_chunks[chunkWorldOrigin] = std::move(blob);
    m_dirty = true;
    return true;
}

void VoxelLightOccupancyGpu::removeChunk(const glm::ivec3& chunkWorldOrigin) {
    if (m_chunks.erase(chunkWorldOrigin) > 0) m_dirty = true;
}

std::vector<glm::ivec3> VoxelLightOccupancyGpu::sampleMixedCubes(size_t maxN) const {
    std::vector<glm::ivec3> out;
    for (const auto& [origin, blob] : m_chunks) {
        for (uint32_t ci : blob.mixedCubeIdx) {
            if (out.size() >= maxN) return out;
            // Inverse of ChunkLightOccupancy::cubeIndex: z + y*32 + x*1024.
            const int x = static_cast<int>(ci) / 1024;
            const int y = (static_cast<int>(ci) / 32) % 32;
            const int z = static_cast<int>(ci) % 32;
            out.push_back(origin + glm::ivec3{x, y, z});
        }
    }
    return out;
}

void VoxelLightOccupancyGpu::setViewCentre(const glm::vec3& worldPos) {
    const glm::ivec3 box = PackedOccupancyPool::boxMinChunkFor(worldPos);
    if (box == m_boxMinChunk) return;        // still inside the same chunk — nothing to do
    m_boxMinChunk = box;

    // Forget what the new box no longer covers, so the pool does not carry chunks the shader can
    // never address. They are re-added by the normal residency scan if the box comes back.
    for (auto it = m_chunks.begin(); it != m_chunks.end(); ) {
        if (PackedOccupancyPool::directoryIndex(it->first, m_boxMinChunk) < 0)
            it = m_chunks.erase(it);
        else
            ++it;
    }
    m_dirty = true;
}

void VoxelLightOccupancyGpu::clear() {
    if (m_chunks.empty()) return;
    m_chunks.clear();
    m_dirty = true;
}

void VoxelLightOccupancyGpu::flushIfDirty() {
    if (!m_dirty) return;
    const auto t0 = std::chrono::steady_clock::now();

    std::vector<std::pair<glm::ivec3, ChunkLightOccupancy>> list;
    list.reserve(m_chunks.size());
    for (const auto& [origin, blob] : m_chunks) list.emplace_back(origin, blob);

    // Drop whole chunks that will not fit rather than truncating a blob. The policy is a pure
    // function (selectChunksThatFit) so it is unit-tested without a device, and so this is one
    // pack rather than a repack-per-dropped-chunk loop.
    const size_t capacity = static_cast<size_t>(m_poolBytes / sizeof(uint32_t));
    size_t dropped = 0;
    m_packed = packOccupancyPool(selectChunksThatFit(list, capacity, dropped), m_boxMinChunk);
    if (dropped > 0) {
        LOG_WARN_FMT("VoxelLightOcc", "pool full: dropped " << dropped << " chunk(s); "
                     << "their lighting degrades to no-occlusion. Raise the pool size.");
    }

    ++m_packRevision;   // uploaded to each slot after that slot's fence (uploadToSlot)

    size_t mixed = 0;
    for (const auto& [origin, blob] : m_chunks) mixed += blob.mixedCubeIdx.size();

    m_stats.boxMinChunk    = m_boxMinChunk;
    m_stats.residentChunks = m_chunks.size();
    m_stats.mixedCubes     = mixed;
    m_stats.poolWords      = m_packed.pool.size();
    m_stats.droppedChunks  = dropped;
    m_stats.lastPackMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - t0).count();
    m_dirty = false;
}

void VoxelLightOccupancyGpu::uploadToSlot(uint32_t slot) {
    if (slot >= kSlots || !m_dirMapped[slot] || !m_poolMapped[slot]) return;
    if (m_slotRevision[slot] == m_packRevision) return;   // already holds the latest pack
    std::memcpy(m_dirMapped[slot], m_packed.directory.data(),
                m_packed.directory.size() * sizeof(uint32_t));
    if (!m_packed.pool.empty())
        std::memcpy(m_poolMapped[slot], m_packed.pool.data(), m_packed.pool.size() * sizeof(uint32_t));
    m_slotRevision[slot] = m_packRevision;
}

}  // namespace Graphics
}  // namespace Phyxel
