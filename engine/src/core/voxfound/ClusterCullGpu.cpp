// docs/VoxelFoundationPlan.md F2.11 — the GPU draw-building pass. See the header.
#include "core/voxfound/ClusterCullGpu.h"
#include "vulkan/ComputePipeline.h"

#include <algorithm>
#include <cstring>

namespace Phyxel::Core::VoxFound {

namespace {
struct CullPC {
    glm::vec4 planes[6];
    glm::vec4 lodCam;   // F2.14: xyz camera (box frame), w = c
    uint32_t count;
    uint32_t lod;       // F2.14: bit 8 = level test on, bits 0-7 = maxLevel (off: level-0 clusters only); F2.15:
                        // bit 9 = direction test on (camera in lodCam.xyz)
    uint32_t compact;   // F2.15f: bit 31 = compact + merge, bit 30 = its phase 2, bits 0-29 = the index count
    uint32_t solid;     // F2.15f: clusters [0, solid) are the solid multidraw's
};
static_assert(sizeof(CullPC) == 128, "voxfound_cull.comp push constants");
struct GpuCluster {
    glm::vec4 lo, hi;
};
}  // namespace

ClusterCullGpu::ClusterCullGpu() = default;
ClusterCullGpu::~ClusterCullGpu() { shutdown(); }

bool ClusterCullGpu::init(VkDevice device, VkPhysicalDevice physical, const std::string& shaderDir, uint32_t slots,
                          std::string* err) {
    shutdown();
    m_device = device;
    m_physical = physical;
    m_slots = std::max(1u, slots);
    m_pipe = std::make_unique<Vulkan::ComputePipeline>();
    if (!m_pipe->create(device, shaderDir + "/voxfound_cull.comp.spv", 9, sizeof(CullPC), m_slots)) {
        if (err) *err = "ClusterCullGpu: cannot create the pipeline from " + shaderDir + "/voxfound_cull.comp.spv";
        shutdown();
        return false;
    }
    m_commands.resize(m_slots);
    m_masks.resize(m_slots);
    m_origOut.resize(m_slots);
    m_counts.resize(m_slots);
    m_ccmds.resize(m_slots);
    m_groups.resize(m_slots);
    return true;
}

void ClusterCullGpu::shutdown() {
    if (m_device == VK_NULL_HANDLE) return;
    vkDeviceWaitIdle(m_device);
    destroyBuffer(m_clusters);
    destroyBuffer(m_firsts);
    destroyBuffer(m_origIn);
    for (auto& b : m_commands) destroyBuffer(b);
    for (auto& b : m_masks) destroyBuffer(b);
    for (auto& b : m_origOut) destroyBuffer(b);
    for (auto& b : m_counts) destroyBuffer(b);
    for (auto& b : m_ccmds) destroyBuffer(b);
    for (auto& b : m_groups) destroyBuffer(b);
    m_commands.clear();
    m_ccmds.clear();
    m_groups.clear();
    m_masks.clear();
    m_origOut.clear();
    m_counts.clear();
    m_compact = false;
    if (m_pipe) m_pipe->cleanup();
    m_pipe.reset();
    m_count = m_chunks = 0;
    m_device = VK_NULL_HANDLE;
}

uint32_t ClusterCullGpu::findMemoryType(uint32_t filter, VkMemoryPropertyFlags props) const {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(m_physical, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if ((filter & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props) return i;
    return UINT32_MAX;
}

bool ClusterCullGpu::createBuffer(Buffer& b, VkDeviceSize bytes, VkBufferUsageFlags usage, bool hostVisible,
                                  std::string* err) {
    b.bytes = std::max<VkDeviceSize>(bytes, 16);
    VkBufferCreateInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = b.bytes;
    bi.usage = usage | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(m_device, &bi, nullptr, &b.buf) != VK_SUCCESS) {
        if (err) *err = "ClusterCullGpu: vkCreateBuffer failed";
        b = Buffer{};
        return false;
    }
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(m_device, b.buf, &mr);
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = findMemoryType(mr.memoryTypeBits, hostVisible ? (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
                                                                       : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (ai.memoryTypeIndex == UINT32_MAX || vkAllocateMemory(m_device, &ai, nullptr, &b.mem) != VK_SUCCESS) {
        if (err) *err = "ClusterCullGpu: cannot allocate " + std::to_string(mr.size) + " bytes";
        vkDestroyBuffer(m_device, b.buf, nullptr);
        b = Buffer{};
        return false;
    }
    vkBindBufferMemory(m_device, b.buf, b.mem, 0);
    if (hostVisible) vkMapMemory(m_device, b.mem, 0, b.bytes, 0, &b.mapped);
    return true;
}

bool ClusterCullGpu::copy(VkQueue queue, uint32_t family, VkBuffer src, VkBuffer dst, VkDeviceSize bytes,
                          std::string* err) {
    VkCommandPoolCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pci.queueFamilyIndex = family;
    pci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    VkCommandPool pool = VK_NULL_HANDLE;
    if (vkCreateCommandPool(m_device, &pci, nullptr, &pool) != VK_SUCCESS) {
        if (err) *err = "ClusterCullGpu: command pool";
        return false;
    }
    VkCommandBufferAllocateInfo cai{};
    cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cai.commandPool = pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(m_device, &cai, &cmd);
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    const VkBufferCopy region{0, 0, bytes};
    vkCmdCopyBuffer(cmd, src, dst, 1, &region);
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    const bool ok = vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE) == VK_SUCCESS && vkQueueWaitIdle(queue) == VK_SUCCESS;
    vkDestroyCommandPool(m_device, pool, nullptr);
    if (!ok && err) *err = "ClusterCullGpu: copy submit failed";
    return ok;
}

void ClusterCullGpu::destroyBuffer(Buffer& b) {
    if (m_device != VK_NULL_HANDLE) {
        if (b.mapped) vkUnmapMemory(m_device, b.mem);
        if (b.buf) vkDestroyBuffer(m_device, b.buf, nullptr);
        if (b.mem) vkFreeMemory(m_device, b.mem, nullptr);
    }
    b = Buffer{};
}

bool ClusterCullGpu::upload(const std::vector<Cluster>& clusters, uint32_t indexCount, uint32_t chunkCount,
                            VkQueue queue, uint32_t family, std::string* err, const Compact* compact) {
    if (!initialized()) {
        if (err) *err = "ClusterCullGpu: not initialized";
        return false;
    }
    vkDeviceWaitIdle(m_device);
    destroyBuffer(m_clusters);
    destroyBuffer(m_firsts);
    destroyBuffer(m_origIn);
    for (auto& b : m_commands) destroyBuffer(b);
    for (auto& b : m_masks) destroyBuffer(b);
    for (auto& b : m_origOut) destroyBuffer(b);
    for (auto& b : m_counts) destroyBuffer(b);
    for (auto& b : m_ccmds) destroyBuffer(b);
    for (auto& b : m_groups) destroyBuffer(b);
    m_count = uint32_t(clusters.size());
    m_chunks = std::max(1u, chunkCount);
    m_indexCount = indexCount;
    // F2.15f: compaction needs one origin word per cluster
    m_compact = compact && compact->origins.size() == clusters.size();
    m_solid = m_compact ? std::min(compact->solid, m_count) : m_count;
    if (!createBuffer(m_clusters, VkDeviceSize(m_count) * sizeof(GpuCluster), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false,
                      err))
        return false;
    // staging: the clusters, then each slot's commands
    Buffer staging;
    const VkDeviceSize clusterBytes = VkDeviceSize(m_count) * sizeof(GpuCluster), cmdBytes = VkDeviceSize(m_count) * 20;
    // F2.15f: origins out = [0, solid) + [leafOriginBase, leafOriginBase + n - solid); in = one per cluster
    const VkDeviceSize origBytes = m_compact ? VkDeviceSize(m_count + 16) * 16 : 16;
    if (!createBuffer(staging, std::max({clusterBytes, cmdBytes, origBytes}), 0, true, err)) return false;
    // F2.15f: each cluster's first face and origin word (the compacting pass reads them)
    if (!createBuffer(m_firsts, VkDeviceSize(m_count) * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false, err) ||
        !createBuffer(m_origIn, origBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false, err)) {   // >= n entries
        destroyBuffer(staging);
        return false;
    }
    {
        auto* f = static_cast<uint32_t*>(staging.mapped);
        for (uint32_t i = 0; i < m_count; ++i) f[i] = clusters[i].firstInstance;
        if (m_count && !copy(queue, family, staging.buf, m_firsts.buf, VkDeviceSize(m_count) * 4, err)) {
            destroyBuffer(staging);
            return false;
        }
        if (m_compact && m_count) {
            std::memcpy(staging.mapped, compact->origins.data(), size_t(m_count) * 16);
            if (!copy(queue, family, staging.buf, m_origIn.buf, origBytes, err)) {
                destroyBuffer(staging);
                return false;
            }
        }
    }
    auto* gc = static_cast<GpuCluster*>(staging.mapped);
    for (uint32_t i = 0; i < m_count; ++i) {
        const Cluster& c = clusters[i];
        // F2.14: level in bits 24-27; F2.15: direction + 1 in bits 28-31 (0 = all directions)
        uint32_t ch = (c.chunk & 0xFFFFFFu) | (c.level & 0xFu) << 24 | uint32_t(c.dir < 0 ? 0 : c.dir + 1) << 28,
                 n = c.count;
        float fch, fn;
        std::memcpy(&fch, &ch, 4);
        std::memcpy(&fn, &n, 4);
        gc[i].lo = glm::vec4(c.lo, fch);
        gc[i].hi = glm::vec4(c.hi, fn);
    }
    if (m_count && !copy(queue, family, staging.buf, m_clusters.buf, clusterBytes, err)) {
        destroyBuffer(staging);
        return false;
    }
    for (uint32_t s = 0; s < m_slots; ++s) {
        if (!createBuffer(m_commands[s], cmdBytes,
                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, false, err) ||
            !createBuffer(m_masks[s], VkDeviceSize(m_chunks) * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true, err) ||
            !createBuffer(m_origOut[s], origBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false, err) ||
            !createBuffer(m_counts[s], 8, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, false,
                          err) ||
            !createBuffer(m_ccmds[s], m_compact ? cmdBytes : 20,
                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, false, err) ||
            !createBuffer(m_groups[s], m_compact ? VkDeviceSize((m_count + 63) / 64) * 8 : 8,
                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false, err)) {
            destroyBuffer(staging);
            return false;
        }
        auto* cmd = static_cast<uint32_t*>(staging.mapped);
        for (uint32_t i = 0; i < m_count; ++i) {
            cmd[i * 5 + 0] = indexCount;
            cmd[i * 5 + 1] = clusters[i].count;
            cmd[i * 5 + 2] = 0;   // firstIndex
            cmd[i * 5 + 3] = 0;   // vertexOffset
            cmd[i * 5 + 4] = clusters[i].firstInstance;
        }
        if (m_count && !copy(queue, family, staging.buf, m_commands[s].buf, cmdBytes, err)) {
            destroyBuffer(staging);
            return false;
        }
        auto* m = static_cast<uint32_t*>(m_masks[s].mapped);
        for (uint32_t i = 0; i < m_chunks; ++i) m[i] = 1u;
        m_pipe->bindBufferInSet(s, 0, m_clusters.buf, m_clusters.bytes);
        m_pipe->bindBufferInSet(s, 1, m_masks[s].buf, m_masks[s].bytes);
        m_pipe->bindBufferInSet(s, 2, m_commands[s].buf, m_commands[s].bytes);
        m_pipe->bindBufferInSet(s, 3, m_firsts.buf, m_firsts.bytes);
        m_pipe->bindBufferInSet(s, 4, m_origIn.buf, m_origIn.bytes);
        m_pipe->bindBufferInSet(s, 5, m_origOut[s].buf, m_origOut[s].bytes);
        m_pipe->bindBufferInSet(s, 6, m_counts[s].buf, m_counts[s].bytes);
        m_pipe->bindBufferInSet(s, 7, m_ccmds[s].buf, m_ccmds[s].bytes);
        m_pipe->bindBufferInSet(s, 8, m_groups[s].buf, m_groups[s].bytes);
    }
    m_pipe->updateDescriptors();
    destroyBuffer(staging);
    return true;
}

void ClusterCullGpu::setChunkVisible(uint32_t slot, const std::vector<uint32_t>& visible) {
    if (slot >= m_masks.size() || !m_masks[slot].mapped) return;
    auto* m = static_cast<uint32_t*>(m_masks[slot].mapped);
    for (uint32_t i = 0; i < m_chunks; ++i) m[i] = i < visible.size() ? visible[i] : 1u;
}

bool ClusterCullGpu::readCommands(VkQueue queue, uint32_t family, uint32_t slot, std::vector<uint32_t>& out,
                                  std::string* err) {
    out.clear();
    if (slot >= m_commands.size() || m_commands[slot].buf == VK_NULL_HANDLE || m_count == 0) return m_count == 0;
    Buffer staging;
    const VkDeviceSize bytes = VkDeviceSize(m_count) * 20;
    if (!createBuffer(staging, bytes, 0, true, err)) return false;
    const bool ok = copy(queue, family, m_commands[slot].buf, staging.buf, bytes, err);
    if (ok) {
        out.resize(size_t(m_count) * 5);
        std::memcpy(out.data(), staging.mapped, size_t(bytes));
    }
    destroyBuffer(staging);
    return ok;
}

bool ClusterCullGpu::readCompact(VkQueue queue, uint32_t family, uint32_t slot, std::vector<uint32_t>& cmds,
                                 std::vector<glm::ivec4>& origins, uint32_t counts[2], std::string* err) {
    counts[0] = counts[1] = 0;
    origins.clear();
    if (!m_compact || slot >= m_counts.size()) {
        if (err) *err = "ClusterCullGpu: no compaction table";
        return false;
    }
    Buffer staging;
    const VkDeviceSize ob = VkDeviceSize(m_count + 16) * 16, cb = VkDeviceSize(m_count) * 20;
    if (!createBuffer(staging, std::max(ob, cb) + 8, 0, true, err)) return false;
    bool ok = copy(queue, family, m_ccmds[slot].buf, staging.buf, cb, err);
    if (ok) {
        cmds.resize(size_t(m_count) * 5);
        std::memcpy(cmds.data(), staging.mapped, size_t(cb));
        ok = copy(queue, family, m_origOut[slot].buf, staging.buf, ob, err);
    }
    if (ok) {
        origins.resize(size_t(m_count) + 16);
        std::memcpy(origins.data(), staging.mapped, size_t(ob));
        ok = copy(queue, family, m_counts[slot].buf, staging.buf, 8, err);
        if (ok) std::memcpy(counts, staging.mapped, 8);
    }
    destroyBuffer(staging);
    return ok;
}

void ClusterCullGpu::record(VkCommandBuffer cmd, uint32_t slot, const glm::vec4 planes[6], const LodCull* lod,
                            const glm::vec3* dirCam, bool dirIsLight, bool compact) {
    if (!initialized() || m_count == 0 || slot >= m_slots || !m_pipe) return;
    compact = compact && m_compact;
    if (compact) {   // F2.15f: this slot's two append counters start at 0 (only transfer work waits here)
        vkCmdFillBuffer(cmd, m_counts[slot].buf, 0, 8, 0u);
        VkMemoryBarrier pre{};
        pre.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        pre.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        pre.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &pre, 0,
                             nullptr, 0, nullptr);
    }
    // No barrier BEFORE the dispatch: this slot's commands were last read by the frame that used this slot before, and
    // the CPU waited for that frame's fence before recording this one. (A first version had an indirect-read ->
    // shader-write barrier here; being global, it made the cull wait for every earlier indirect draw in the queue,
    // e.g. this frame's mid-shadow multidraw.)
    CullPC pc{};
    for (int k = 0; k < 6; ++k) pc.planes[k] = planes[k];
    pc.count = m_count;
    pc.compact = compact ? (0x80000000u | (m_indexCount & 0x3FFFFFFFu)) : 0u;
    pc.solid = m_solid;
    if (lod) {
        pc.lodCam = glm::vec4(lod->cam, lod->c);
        pc.lod = 0x100u | uint32_t(std::clamp(lod->maxLevel, 0, 15));
    }
    if (dirCam) {   // F2.15
        pc.lodCam = glm::vec4(*dirCam, pc.lodCam.w);
        pc.lod |= dirIsLight ? 0x400u : 0x200u;   // F2.15b: bit 10 = the light test
    }
    m_pipe->bind(cmd, slot);
    m_pipe->pushConstants(cmd, &pc, sizeof(pc));
    m_pipe->dispatch(cmd, (m_count + 63) / 64);
    if (compact) {   // F2.15f-2: phase 2 reads every workgroup's phase-1 counts
        VkMemoryBarrier mid{};
        mid.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        mid.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        mid.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &mid,
                             0, nullptr, 0, nullptr);
        pc.compact |= 0x40000000u;
        m_pipe->pushConstants(cmd, &pc, sizeof(pc));
        m_pipe->dispatch(cmd, (m_count + 63) / 64);
    }
    VkMemoryBarrier post{};
    post.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    post.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    post.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT;   // F2.15f: + the origins
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0, 1, &post, 0,
                         nullptr, 0, nullptr);
}

bool ClusterCullGpu::cullNow(VkQueue queue, uint32_t family, uint32_t slot, const glm::vec4 planes[6], std::string* err,
                             const LodCull* lod, const glm::vec3* dirCam, bool dirIsLight, bool compact) {
    VkCommandPoolCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pci.queueFamilyIndex = family;
    pci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    VkCommandPool pool = VK_NULL_HANDLE;
    if (vkCreateCommandPool(m_device, &pci, nullptr, &pool) != VK_SUCCESS) {
        if (err) *err = "ClusterCullGpu: command pool";
        return false;
    }
    VkCommandBufferAllocateInfo cai{};
    cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cai.commandPool = pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(m_device, &cai, &cmd);
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    record(cmd, slot, planes, lod, dirCam, dirIsLight, compact);
    VkMemoryBarrier mb{};
    mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, nullptr,
                         0, nullptr);
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    const bool ok = vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE) == VK_SUCCESS && vkQueueWaitIdle(queue) == VK_SUCCESS;
    vkDestroyCommandPool(m_device, pool, nullptr);
    if (!ok && err) *err = "ClusterCullGpu: submit failed";
    return ok;
}

}  // namespace Phyxel::Core::VoxFound
