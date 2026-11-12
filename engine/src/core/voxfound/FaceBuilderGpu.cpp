// docs/VoxelFoundationPlan.md F2.1 — the GPU face builder. See the header.
#include "core/voxfound/FaceBuilderGpu.h"
#include "vulkan/ComputePipeline.h"

#include <algorithm>
#include <cstring>

namespace Phyxel::Core::VoxFound {

namespace {
constexpr uint32_t kGroup = 64;
struct FacesPC {
    int32_t dim[4];     // xyz, w = outside (0 air, 1 skip)
    int32_t job[4];     // axis (3 = sprig pass), invocations, capacity, -
    uint32_t cards[8];  // CardSet bits (F2.3)
    int32_t origin[4];  // absolute cell of voxel 0; w = sprig keep threshold
};
static_assert(sizeof(FacesPC) == 80, "voxfound_faces.comp push constants");
}  // namespace

FaceBuilderGpu::FaceBuilderGpu() = default;
FaceBuilderGpu::~FaceBuilderGpu() { shutdown(); }

bool FaceBuilderGpu::init(VkDevice device, VkPhysicalDevice physical, VkQueue queue, uint32_t family,
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
        if (err) *err = "FaceBuilderGpu: command pool / fence creation failed";
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
    m_faces = std::make_unique<Vulkan::ComputePipeline>();
    if (!m_faces->create(device, shaderDir + "/voxfound_faces.comp.spv", 3, sizeof(FacesPC))) {
        if (err) *err = "FaceBuilderGpu: cannot create the pipeline from " + shaderDir +
                        "/voxfound_faces.comp.spv (run build_shaders.bat?)";
        shutdown();
        return false;
    }
    return true;
}

void FaceBuilderGpu::shutdown() {
    if (m_device == VK_NULL_HANDLE) return;
    vkDeviceWaitIdle(m_device);
    destroyBuffer(m_voxels);
    destroyBuffer(m_out);
    destroyBuffer(m_counter);
    destroyBuffer(m_stagingIn);
    destroyBuffer(m_stagingOut);
    m_voxelWords = 0;
    m_capacity = 0;
    if (m_faces) m_faces->cleanup();
    m_faces.reset();
    if (m_queries) vkDestroyQueryPool(m_device, m_queries, nullptr);
    if (m_fence) vkDestroyFence(m_device, m_fence, nullptr);
    if (m_pool) vkDestroyCommandPool(m_device, m_pool, nullptr);
    m_queries = VK_NULL_HANDLE;
    m_fence = VK_NULL_HANDLE;
    m_pool = VK_NULL_HANDLE;
    m_device = VK_NULL_HANDLE;
    m_timestampMs = 0.0;
}

uint32_t FaceBuilderGpu::findMemoryType(uint32_t filter, VkMemoryPropertyFlags props) const {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(m_physical, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if ((filter & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props) return i;
    return UINT32_MAX;
}

bool FaceBuilderGpu::createBuffer(Buffer& b, VkDeviceSize bytes, bool hostVisible, std::string* err) {
    b.bytes = std::max<VkDeviceSize>(bytes, 16);
    VkBufferCreateInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = b.bytes;
    bi.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(m_device, &bi, nullptr, &b.buf) != VK_SUCCESS) {
        if (err) *err = "FaceBuilderGpu: vkCreateBuffer failed for " + std::to_string(b.bytes) + " bytes";
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
        if (err) *err = "FaceBuilderGpu: cannot allocate " + std::to_string(mr.size) + " bytes";
        vkDestroyBuffer(m_device, b.buf, nullptr);
        b = Buffer{};
        return false;
    }
    vkBindBufferMemory(m_device, b.buf, b.mem, 0);
    if (hostVisible) vkMapMemory(m_device, b.mem, 0, b.bytes, 0, &b.mapped);
    return true;
}

void FaceBuilderGpu::destroyBuffer(Buffer& b) {
    if (m_device == VK_NULL_HANDLE) { b = Buffer{}; return; }
    if (b.mapped) vkUnmapMemory(m_device, b.mem);
    if (b.buf) vkDestroyBuffer(m_device, b.buf, nullptr);
    if (b.mem) vkFreeMemory(m_device, b.mem, nullptr);
    b = Buffer{};
}

bool FaceBuilderGpu::ensureBuffers(size_t voxelWords, size_t faces, std::string* err) {
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
    if (faces > m_capacity) {
        vkDeviceWaitIdle(m_device);
        destroyBuffer(m_out);
        destroyBuffer(m_stagingOut);
        if (!createBuffer(m_out, faces * 8, false, err) || !createBuffer(m_stagingOut, faces * 8 + 16, true, err))
            return false;
        m_capacity = faces;
        rebind = true;
    }
    if (!m_counter.buf) {
        if (!createBuffer(m_counter, 16, false, err)) return false;
        rebind = true;
    }
    if (rebind) {
        m_faces->bindBuffer(0, m_voxels.buf, m_voxels.bytes);
        m_faces->bindBuffer(1, m_out.buf, m_out.bytes);
        m_faces->bindBuffer(2, m_counter.buf, m_counter.bytes);
        m_faces->updateDescriptors();
    }
    return true;
}

bool FaceBuilderGpu::build(const VoxelGrid& g, Outside outside, std::vector<PackedFace>& out, std::string* err,
                           const SprigParams* sprigs) {
    out.clear();
    if (!initialized()) { if (err) *err = "FaceBuilderGpu: not initialised"; return false; }
    for (int a = 0; a < 3; ++a)
        if (g.dim[a] < 1 || g.dim[a] > kMaxChunkVoxels) {
            if (err) *err = "FaceBuilderGpu: grid side " + std::to_string(g.dim[a]) + " outside 1.." +
                            std::to_string(kMaxChunkVoxels) + " (one chunk per build)";
            return false;
        }
    const size_t n = g.size();
    const size_t words = (n + 3) / 4;
    if (!ensureBuffers(words, std::max(m_capacity, m_initialCapacity), err)) return false;
    std::memset(m_stagingIn.mapped, 0, words * 4);
    std::memcpy(m_stagingIn.mapped, g.mat.data(), n);

    for (int attempt = 0; attempt < 2; ++attempt) {
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
        vkCmdFillBuffer(cmd, m_counter.buf, 0, 16, 0);
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
        m_faces->bind(cmd);
        for (int axis = 0; axis < (sprigs ? 4 : 3); ++axis) {
            // axis 3 = the sprig pass: one invocation per voxel.
            const int rows = axis < 3 ? 2 * g.dim[axis] * g.dim[(axis + 2) % 3] : int(n);
            FacesPC pc{};
            pc.dim[0] = g.dim.x; pc.dim[1] = g.dim.y; pc.dim[2] = g.dim.z;
            pc.dim[3] = outside == Outside::Air ? 0 : 1;
            pc.job[0] = axis; pc.job[1] = rows; pc.job[2] = int32_t(m_capacity);
            if (sprigs) {
                std::memcpy(pc.cards, sprigs->cards.bits, sizeof(pc.cards));
                pc.origin[0] = sprigs->worldOriginVoxel.x;
                pc.origin[1] = sprigs->worldOriginVoxel.y;
                pc.origin[2] = sprigs->worldOriginVoxel.z;
                pc.origin[3] = int32_t(sprigs->threshold24);
            }
            m_faces->pushConstants(cmd, &pc, sizeof(pc));
            m_faces->dispatch(cmd, (uint32_t(rows) + kGroup - 1) / kGroup);
        }
        if (m_queries) vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, m_queries, 1);
        mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        mb.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &mb, 0,
                             nullptr, 0, nullptr);
        // Read back the counter and the whole face buffer (a test/reference path; the draw path keeps the
        // faces on the device).
        VkBufferCopy c0{0, 0, 4};
        vkCmdCopyBuffer(cmd, m_counter.buf, m_stagingOut.buf, 1, &c0);
        VkBufferCopy c1{0, 16, VkDeviceSize(m_capacity * 8)};
        vkCmdCopyBuffer(cmd, m_out.buf, m_stagingOut.buf, 1, &c1);
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
            if (err) *err = "FaceBuilderGpu: submit " + std::to_string(int(sub)) + " wait " + std::to_string(int(wait));
            return false;
        }
        m_lastGpuMs = -1.0;
        if (m_queries) {
            uint64_t ts[2] = {0, 0};
            if (vkGetQueryPoolResults(m_device, m_queries, 0, 2, sizeof(ts), ts, sizeof(uint64_t),
                                      VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT) == VK_SUCCESS)
                m_lastGpuMs = double(ts[1] - ts[0]) * m_timestampMs;
        }
        const auto* base = static_cast<const uint8_t*>(m_stagingOut.mapped);
        uint32_t count = 0;
        std::memcpy(&count, base, 4);
        if (count <= m_capacity) {
            out.resize(count);
            std::memcpy(out.data(), base + 16, size_t(count) * 8);
            return true;
        }
        // Overflow: grow to fit and build again (the kernel counts every face even when it cannot store it).
        size_t grow = m_capacity;
        while (grow < count) grow *= 2;
        if (!ensureBuffers(words, grow, err)) return false;
    }
    if (err) *err = "FaceBuilderGpu: face buffer still overflowed after regrowing";
    return false;
}

}  // namespace Phyxel::Core::VoxFound
