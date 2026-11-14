// docs/VoxelFoundationPlan.md F2.14e — the GPU level build. See the header.
#include "core/voxfound/LodBuilderGpu.h"
#include "vulkan/ComputePipeline.h"

#include <algorithm>
#include <cstring>

namespace Phyxel::Core::VoxFound {

namespace {
constexpr uint32_t kGroup = 64;
struct LodPC {
    int32_t fine[4];     // xyz dims, w offset (cells)
    int32_t coarse[4];   // xyz dims, w offset (cells)
    int32_t job[4];      // pass, coarse level, coarse cells
};
static_assert(sizeof(LodPC) == 48, "voxfound_lod.comp push constants");
}  // namespace

LodBuilderGpu::LodBuilderGpu() = default;
LodBuilderGpu::~LodBuilderGpu() { shutdown(); }

bool LodBuilderGpu::init(VkDevice device, VkPhysicalDevice physical, VkQueue queue, uint32_t family,
                         const std::string& shaderDir, std::string* err) {
    shutdown();
    m_device = device;
    m_physical = physical;
    m_queue = queue;
    VkCommandPoolCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pci.queueFamilyIndex = family;
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (vkCreateCommandPool(device, &pci, nullptr, &m_pool) != VK_SUCCESS ||
        vkCreateFence(device, &fci, nullptr, &m_fence) != VK_SUCCESS) {
        if (err) *err = "LodBuilderGpu: command pool / fence creation failed";
        shutdown();
        return false;
    }
    uint32_t nf = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &nf, nullptr);
    std::vector<VkQueueFamilyProperties> fam(nf);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &nf, fam.data());
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(physical, &props);
    if (family < nf && fam[family].timestampValidBits > 0 && props.limits.timestampPeriod > 0.0f) {
        VkQueryPoolCreateInfo q{};
        q.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        q.queryType = VK_QUERY_TYPE_TIMESTAMP;
        q.queryCount = 2;
        if (vkCreateQueryPool(device, &q, nullptr, &m_queries) == VK_SUCCESS)
            m_timestampMs = double(props.limits.timestampPeriod) * 1e-6;
    }
    m_pipe = std::make_unique<Vulkan::ComputePipeline>();
    if (!m_pipe->create(device, shaderDir + "/voxfound_lod.comp.spv", 5, sizeof(LodPC))) {
        if (err) *err = "LodBuilderGpu: cannot create the pipeline from " + shaderDir +
                        "/voxfound_lod.comp.spv (run build_shaders.bat?)";
        shutdown();
        return false;
    }
    return true;
}

void LodBuilderGpu::shutdown() {
    if (m_device == VK_NULL_HANDLE) return;
    vkDeviceWaitIdle(m_device);
    for (Buffer* b : {&m_voxels, &m_stagingIn, &m_mat, &m_votes, &m_count, &m_surf, &m_stagingOut}) destroyBuffer(*b);
    m_voxelWords = 0;
    m_cells = 0;
    if (m_pipe) m_pipe->cleanup();
    m_pipe.reset();
    if (m_queries) vkDestroyQueryPool(m_device, m_queries, nullptr);
    if (m_fence) vkDestroyFence(m_device, m_fence, nullptr);
    if (m_pool) vkDestroyCommandPool(m_device, m_pool, nullptr);
    m_queries = VK_NULL_HANDLE;
    m_fence = VK_NULL_HANDLE;
    m_pool = VK_NULL_HANDLE;
    m_device = VK_NULL_HANDLE;
    m_timestampMs = 0.0;
}

uint32_t LodBuilderGpu::findMemoryType(uint32_t filter, VkMemoryPropertyFlags props) const {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(m_physical, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if ((filter & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props) return i;
    return UINT32_MAX;
}

bool LodBuilderGpu::createBuffer(Buffer& b, VkDeviceSize bytes, bool hostVisible, std::string* err) {
    b.bytes = std::max<VkDeviceSize>(bytes, 16);
    VkBufferCreateInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = b.bytes;
    bi.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(m_device, &bi, nullptr, &b.buf) != VK_SUCCESS) {
        if (err) *err = "LodBuilderGpu: vkCreateBuffer failed for " + std::to_string(b.bytes) + " bytes";
        b = Buffer{};
        return false;
    }
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(m_device, b.buf, &mr);
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = mr.size;
    const VkMemoryPropertyFlags want = hostVisible
        ? (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
        : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    ai.memoryTypeIndex = hostVisible ? findMemoryType(mr.memoryTypeBits, want | VK_MEMORY_PROPERTY_HOST_CACHED_BIT)
                                     : UINT32_MAX;
    if (ai.memoryTypeIndex == UINT32_MAX) ai.memoryTypeIndex = findMemoryType(mr.memoryTypeBits, want);
    if (ai.memoryTypeIndex == UINT32_MAX || vkAllocateMemory(m_device, &ai, nullptr, &b.mem) != VK_SUCCESS) {
        if (err) *err = "LodBuilderGpu: cannot allocate " + std::to_string(mr.size) + " bytes";
        vkDestroyBuffer(m_device, b.buf, nullptr);
        b = Buffer{};
        return false;
    }
    vkBindBufferMemory(m_device, b.buf, b.mem, 0);
    if (hostVisible) vkMapMemory(m_device, b.mem, 0, b.bytes, 0, &b.mapped);
    return true;
}

void LodBuilderGpu::destroyBuffer(Buffer& b) {
    if (m_device == VK_NULL_HANDLE) { b = Buffer{}; return; }
    if (b.mapped) vkUnmapMemory(m_device, b.mem);
    if (b.buf) vkDestroyBuffer(m_device, b.buf, nullptr);
    if (b.mem) vkFreeMemory(m_device, b.mem, nullptr);
    b = Buffer{};
}

bool LodBuilderGpu::ensureBuffers(size_t voxelWords, size_t cells, std::string* err) {
    bool rebind = false;
    if (voxelWords > m_voxelWords) {
        vkDeviceWaitIdle(m_device);
        destroyBuffer(m_voxels);
        destroyBuffer(m_stagingIn);
        if (!createBuffer(m_voxels, voxelWords * 4, false, err) || !createBuffer(m_stagingIn, voxelWords * 4, true, err))
            return false;
        m_voxelWords = voxelWords;
        rebind = true;
    }
    if (cells > m_cells) {
        vkDeviceWaitIdle(m_device);
        for (Buffer* b : {&m_mat, &m_votes, &m_count, &m_surf, &m_stagingOut}) destroyBuffer(*b);
        if (!createBuffer(m_mat, cells * 4, false, err) || !createBuffer(m_votes, cells * 4, false, err) ||
            !createBuffer(m_count, cells * 4, false, err) || !createBuffer(m_surf, cells * 4, false, err) ||
            !createBuffer(m_stagingOut, cells * 16, true, err))
            return false;
        m_cells = cells;
        rebind = true;
    }
    if (rebind) {
        m_pipe->bindBuffer(0, m_voxels.buf, m_voxels.bytes);
        m_pipe->bindBuffer(1, m_mat.buf, m_mat.bytes);
        m_pipe->bindBuffer(2, m_votes.buf, m_votes.bytes);
        m_pipe->bindBuffer(3, m_count.buf, m_count.bytes);
        m_pipe->bindBuffer(4, m_surf.buf, m_surf.bytes);
        m_pipe->updateDescriptors();
    }
    return true;
}

bool LodBuilderGpu::build(const VoxelGrid& g, int levels, std::vector<LodGrid>& out, std::string* err) {
    out.clear();
    if (!initialized()) { if (err) *err = "LodBuilderGpu: not initialised"; return false; }
    if (levels < 1 || levels > kMaxLevels) {
        if (err) *err = "LodBuilderGpu: levels " + std::to_string(levels) + " outside 1.." + std::to_string(kMaxLevels);
        return false;
    }
    const int div = 1 << levels;
    for (int a = 0; a < 3; ++a)
        if (g.dim[a] < div || g.dim[a] > kMaxSide || g.dim[a] % div) {
            if (err) *err = "LodBuilderGpu: grid side " + std::to_string(g.dim[a]) + " must be a multiple of " +
                            std::to_string(div) + " in " + std::to_string(div) + ".." + std::to_string(kMaxSide);
            return false;
        }
    // the levels' dims and offsets in the concatenated channel buffers
    std::vector<glm::ivec3> dims(size_t(levels) + 1);
    std::vector<size_t> offs(size_t(levels) + 1, 0);
    dims[0] = g.dim;
    size_t cells = 0;
    for (int k = 1; k <= levels; ++k) {
        dims[size_t(k)] = dims[size_t(k - 1)] / 2;
        offs[size_t(k)] = cells;
        cells += size_t(dims[size_t(k)].x) * size_t(dims[size_t(k)].y) * size_t(dims[size_t(k)].z);
    }
    const size_t n = g.size(), words = (n + 3) / 4;
    if (!ensureBuffers(words, cells, err)) return false;
    std::memset(m_stagingIn.mapped, 0, words * 4);
    std::memcpy(m_stagingIn.mapped, g.mat.data(), n);

    VkCommandBufferAllocateInfo cai{};
    cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cai.commandPool = m_pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(m_device, &cai, &cmd);
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    VkBufferCopy up{0, 0, VkDeviceSize(words * 4)};
    vkCmdCopyBuffer(cmd, m_stagingIn.buf, m_voxels.buf, 1, &up);
    VkMemoryBarrier mb{};
    mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &mb, 0,
                         nullptr, 0, nullptr);
    if (m_queries) {
        vkCmdResetQueryPool(cmd, m_queries, 0, 2);
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, m_queries, 0);
    }
    m_pipe->bind(cmd);
    VkMemoryBarrier cb{};
    cb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    cb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    cb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    for (int k = 1; k <= levels; ++k) {
        const glm::ivec3 fd = dims[size_t(k - 1)], d = dims[size_t(k)];
        const int c = d.x * d.y * d.z;
        for (int pass : {k == 1 ? 0 : 1, 2}) {
            LodPC pc{};
            pc.fine[0] = fd.x; pc.fine[1] = fd.y; pc.fine[2] = fd.z; pc.fine[3] = int32_t(offs[size_t(k - 1)]);
            pc.coarse[0] = d.x; pc.coarse[1] = d.y; pc.coarse[2] = d.z; pc.coarse[3] = int32_t(offs[size_t(k)]);
            pc.job[0] = pass; pc.job[1] = k; pc.job[2] = c;
            m_pipe->pushConstants(cmd, &pc, sizeof(pc));
            m_pipe->dispatch(cmd, (uint32_t(c) + kGroup - 1) / kGroup);
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
                                 &cb, 0, nullptr, 0, nullptr);
        }
    }
    if (m_queries) vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, m_queries, 1);
    mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &mb, 0,
                         nullptr, 0, nullptr);
    const VkDeviceSize chan = VkDeviceSize(cells * 4);
    VkBufferCopy cp{0, 0, chan};
    Buffer* src[4] = {&m_mat, &m_votes, &m_count, &m_surf};
    for (int i = 0; i < 4; ++i) {
        cp.dstOffset = chan * VkDeviceSize(i);
        vkCmdCopyBuffer(cmd, src[i]->buf, m_stagingOut.buf, 1, &cp);
    }
    mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, nullptr, 0,
                         nullptr);
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    vkResetFences(m_device, 1, &m_fence);
    const VkResult sub = vkQueueSubmit(m_queue, 1, &si, m_fence);
    const VkResult wait = sub == VK_SUCCESS ? vkWaitForFences(m_device, 1, &m_fence, VK_TRUE, UINT64_MAX) : sub;
    vkFreeCommandBuffers(m_device, m_pool, 1, &cmd);
    if (sub != VK_SUCCESS || wait != VK_SUCCESS) {
        if (err) *err = "LodBuilderGpu: submit " + std::to_string(int(sub)) + " wait " + std::to_string(int(wait));
        return false;
    }
    m_lastGpuMs = -1.0;
    if (m_queries) {
        uint64_t ts[2] = {0, 0};
        if (vkGetQueryPoolResults(m_device, m_queries, 0, 2, sizeof(ts), ts, sizeof(uint64_t),
                                  VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT) == VK_SUCCESS)
            m_lastGpuMs = double(ts[1] - ts[0]) * m_timestampMs;
    }
    const auto* base = static_cast<const uint32_t*>(m_stagingOut.mapped);
    const uint32_t* mat = base;
    const uint32_t* votes = base + cells;
    const uint32_t* count = base + 2 * cells;
    const uint32_t* surf = base + 3 * cells;
    out.resize(size_t(levels));
    for (int k = 1; k <= levels; ++k) {
        LodGrid& l = out[size_t(k - 1)];
        l.level = k;
        l.grid.resize(dims[size_t(k)]);
        const size_t m = l.grid.size(), o = offs[size_t(k)];
        l.count.assign(count + o, count + o + m);
        l.surf.assign(surf + o, surf + o + m);
        l.surfMat.resize(m);
        l.volMat.resize(m);
        for (size_t i = 0; i < m; ++i) {
            l.grid.mat[i] = uint8_t(mat[o + i]);
            l.surfMat[i] = uint8_t(votes[o + i] & 0xFFu);
            l.volMat[i] = uint8_t((votes[o + i] >> 8) & 0xFFu);
        }
    }
    return true;
}

}  // namespace Phyxel::Core::VoxFound
