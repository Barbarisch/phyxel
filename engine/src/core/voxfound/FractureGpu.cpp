// docs/VoxelFoundationPlan.md F4.3 -- the fracture core on the GPU. See the header.
#include "core/voxfound/FractureGpu.h"
#include "vulkan/ComputePipeline.h"

#include <algorithm>
#include <cstring>

namespace Phyxel::Core::VoxFound {

namespace {
constexpr uint32_t kGroup = 64;
struct FracPC {
    uint32_t pass;
    uint32_t round;
    uint32_t n;
    uint32_t pad;
};
static_assert(sizeof(FracPC) == 16, "voxfound_fracture.comp push constants");

// the parameter block's fixed slots (shaders/voxfound_fracture.comp P_*)
enum : uint32_t {
    P_DIMX = 0, P_DIMY, P_DIMZ, P_KMAX, P_L, P_CQ, P_HX, P_HY, P_HZ, P_SEED, P_SEED_HI, P_SECTORS, P_GU, P_GV,
    P_NRINGS, P_NMATS, P_NPARTS, P_LHS, P_LHS_HI, P_NUM2, P_NUM2_HI, P_OFF_CELLC, P_OFF_MAT, P_OFF_PART, P_OFF_RING,
    P_OFF_BOUND, P_VALID, P_GRAINBOND, P_HEADER
};
}  // namespace

FractureGpu::FractureGpu() = default;
FractureGpu::~FractureGpu() { shutdown(); }

bool FractureGpu::init(VkDevice device, VkPhysicalDevice physical, VkQueue queue, uint32_t family,
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
        if (err) *err = "FractureGpu: command pool / fence creation failed";
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
    if (!m_pipe->create(device, shaderDir + "/voxfound_fracture.comp.spv", 6, sizeof(FracPC))) {
        if (err) *err = "FractureGpu: cannot create the pipeline from " + shaderDir +
                        "/voxfound_fracture.comp.spv (run build_shaders.bat?)";
        shutdown();
        return false;
    }
    return true;
}

void FractureGpu::shutdown() {
    if (m_device == VK_NULL_HANDLE) return;
    vkDeviceWaitIdle(m_device);
    for (Buffer* b : {&m_vox, &m_params, &m_ids, &m_out, &m_label, &m_flags, &m_stageIn, &m_stageOut}) destroyBuffer(*b);
    m_bound = false;
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

uint32_t FractureGpu::findMemoryType(uint32_t filter, VkMemoryPropertyFlags props) const {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(m_physical, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if ((filter & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props) return i;
    return UINT32_MAX;
}

bool FractureGpu::createBuffer(Buffer& b, VkDeviceSize bytes, bool hostVisible, std::string* err) {
    b.bytes = std::max<VkDeviceSize>(bytes, 16);
    VkBufferCreateInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = b.bytes;
    bi.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(m_device, &bi, nullptr, &b.buf) != VK_SUCCESS) {
        if (err) *err = "FractureGpu: vkCreateBuffer failed for " + std::to_string(b.bytes) + " bytes";
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
        if (err) *err = "FractureGpu: cannot allocate " + std::to_string(mr.size) + " bytes";
        vkDestroyBuffer(m_device, b.buf, nullptr);
        b = Buffer{};
        return false;
    }
    vkBindBufferMemory(m_device, b.buf, b.mem, 0);
    if (hostVisible) vkMapMemory(m_device, b.mem, 0, b.bytes, 0, &b.mapped);
    return true;
}

void FractureGpu::destroyBuffer(Buffer& b) {
    if (m_device == VK_NULL_HANDLE) { b = Buffer{}; return; }
    if (b.mapped) vkUnmapMemory(m_device, b.mem);
    if (b.buf) vkDestroyBuffer(m_device, b.buf, nullptr);
    if (b.mem) vkFreeMemory(m_device, b.mem, nullptr);
    b = Buffer{};
}

bool FractureGpu::ensure(Buffer& b, Buffer* /*staging*/, VkDeviceSize bytes, bool hostVisible, std::string* err) {
    if (b.buf && b.bytes >= bytes) return true;
    vkDeviceWaitIdle(m_device);
    destroyBuffer(b);
    m_bound = false;
    return createBuffer(b, bytes, hostVisible, err);
}

bool FractureGpu::submit(const std::function<void(VkCommandBuffer)>& record, bool timed, double* ms, std::string* err) {
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
    const bool useTs = timed && m_queries;
    if (useTs) {
        vkCmdResetQueryPool(cmd, m_queries, 0, 2);
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, m_queries, 0);
    }
    record(cmd);
    if (useTs) vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_queries, 1);
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    vkResetFences(m_device, 1, &m_fence);
    const VkResult sub = vkQueueSubmit(m_queue, 1, &si, m_fence);
    const VkResult wait = sub == VK_SUCCESS ? vkWaitForFences(m_device, 1, &m_fence, VK_TRUE, UINT64_MAX) : sub;
    vkFreeCommandBuffers(m_device, m_pool, 1, &cmd);
    ++m_lastSubmits;
    if (sub != VK_SUCCESS || wait != VK_SUCCESS) {
        if (err) *err = "FractureGpu: submit " + std::to_string(int(sub)) + " wait " + std::to_string(int(wait));
        return false;
    }
    if (useTs && ms) {
        uint64_t ts[2] = {0, 0};
        if (vkGetQueryPoolResults(m_device, m_queries, 0, 2, sizeof(ts), ts, sizeof(uint64_t),
                                  VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT) == VK_SUCCESS)
            *ms += double(ts[1] - ts[0]) * m_timestampMs;
        else
            *ms = -1e30;
    }
    return true;
}

bool FractureGpu::fracture(const VoxelGrid& g, const Hit& hit, const std::vector<FractureMaterial>& mats,
                           FractureResult& out, std::string* err, const FractureKnobs& knobs) {
    out = FractureResult{};
    m_lastGpuMs = -1.0;
    m_lastRounds = 0;
    m_lastSubmits = 0;
    if (!initialized()) { if (err) *err = "FractureGpu: not initialised"; return false; }
    for (int a = 0; a < 3; ++a)
        if (g.dim[a] < 1 || g.dim[a] > kMaxSide) {
            if (err) *err = "FractureGpu: grid side " + std::to_string(g.dim[a]) + " outside 1.." + std::to_string(kMaxSide);
            return false;
        }
    const size_t n = g.size();
    if (n >= size_t(0xFFFFFFF0u)) { if (err) *err = "FractureGpu: grid too large"; return false; }
    const FracturePlan P = planFracture(g, hit, mats);
    for (size_t m = 0; m < P.tq.size(); ++m)
        if (uint64_t(P.tq[m]) * 4000u * 510u >= (uint64_t(1) << 32)) {
            if (err) *err = "FractureGpu: material " + std::to_string(m) + " toughness too large for 32-bit products";
            return false;
        }

    // the parameter block
    std::vector<uint32_t> prm(P_HEADER, 0);
    prm[P_DIMX] = uint32_t(g.dim.x); prm[P_DIMY] = uint32_t(g.dim.y); prm[P_DIMZ] = uint32_t(g.dim.z);
    prm[P_KMAX] = uint32_t(P.kmax); prm[P_L] = uint32_t(P.L); prm[P_CQ] = uint32_t(P.cq);
    for (int a = 0; a < 3; ++a) prm[P_HX + uint32_t(a)] = uint32_t(P.hit16[a]);
    prm[P_SEED] = uint32_t(P.seed); prm[P_SEED_HI] = uint32_t(P.seed >> 32);
    prm[P_SECTORS] = uint32_t(P.sectors); prm[P_GU] = uint32_t(P.glassU); prm[P_GV] = uint32_t(P.glassV);
    prm[P_NRINGS] = uint32_t(P.ringR2.size()); prm[P_NMATS] = uint32_t(P.tq.size());
    prm[P_NPARTS] = uint32_t(P.partWhole.size());
    const uint64_t L2 = uint64_t(P.L) * uint64_t(P.L);
    const uint64_t lhs = uint64_t(P.eq) * L2 * 1000u * 510u;
    const uint64_t num2 = 2u * 255u * uint64_t(P.eq) * L2;
    prm[P_LHS] = uint32_t(lhs); prm[P_LHS_HI] = uint32_t(lhs >> 32);
    prm[P_NUM2] = uint32_t(num2); prm[P_NUM2_HI] = uint32_t(num2 >> 32);
    prm[P_VALID] = P.valid ? 1u : 0u;
    prm[P_GRAINBOND] = uint32_t(FractureModel::kGrainBond * 1000.0f);
    prm[P_OFF_CELLC] = uint32_t(prm.size());
    for (int64_t c : P.cellC) prm.push_back(uint32_t(c));
    prm[P_OFF_MAT] = uint32_t(prm.size());
    for (size_t m = 0; m < P.tq.size(); ++m) {
        prm.push_back(uint32_t(P.tq[m]));
        prm.push_back(uint32_t(P.partQ[m]));
        prm.push_back(uint32_t(int32_t(P.grainAxis[m]) + 1));
        prm.push_back(P.glass[m]);
    }
    prm[P_OFF_PART] = uint32_t(prm.size());
    for (uint8_t w : P.partWhole) prm.push_back(w);
    prm[P_OFF_RING] = uint32_t(prm.size());
    for (int64_t r2 : P.ringR2) { prm.push_back(uint32_t(uint64_t(r2))); prm.push_back(uint32_t(uint64_t(r2) >> 32)); }
    prm[P_OFF_BOUND] = uint32_t(prm.size());
    for (int32_t b : P.bounds) prm.push_back(uint32_t(b));

    const VkDeviceSize voxBytes = VkDeviceSize(n) * 8, prmBytes = VkDeviceSize(prm.size()) * 4;
    const VkDeviceSize idBytes = VkDeviceSize(n) * VkDeviceSize(P.kmax + 1) * 4, wordBytes = VkDeviceSize(n) * 4;
    if (!ensure(m_vox, nullptr, voxBytes, false, err) || !ensure(m_params, nullptr, prmBytes, false, err) ||
        !ensure(m_ids, nullptr, idBytes, false, err) || !ensure(m_out, nullptr, wordBytes, false, err) ||
        !ensure(m_label, nullptr, wordBytes, false, err) ||
        !ensure(m_flags, nullptr, VkDeviceSize(kRoundsPerSubmit) * 4, true, err) ||
        !ensure(m_stageIn, nullptr, voxBytes + prmBytes, true, err) ||
        !ensure(m_stageOut, nullptr, 2 * wordBytes, true, err))
        return false;
    if (!m_bound) {
        m_pipe->bindBuffer(0, m_vox.buf, m_vox.bytes);
        m_pipe->bindBuffer(1, m_params.buf, m_params.bytes);
        m_pipe->bindBuffer(2, m_ids.buf, m_ids.bytes);
        m_pipe->bindBuffer(3, m_out.buf, m_out.bytes);
        m_pipe->bindBuffer(4, m_label.buf, m_label.bytes);
        m_pipe->bindBuffer(5, m_flags.buf, m_flags.bytes);
        m_pipe->updateDescriptors();
        m_bound = true;
    }
    auto* in = static_cast<uint32_t*>(m_stageIn.mapped);
    const bool haveDmg = g.damage.size() == n;
    for (size_t i = 0; i < n; ++i) {
        in[2 * i] = uint32_t(g.mat[i]) | (uint32_t(g.fixed[i]) << 8) | (uint32_t(haveDmg ? g.damage[i] : 0) << 16);
        in[2 * i + 1] = g.part[i];
    }
    std::memcpy(reinterpret_cast<uint8_t*>(m_stageIn.mapped) + voxBytes, prm.data(), size_t(prmBytes));

    const uint32_t groups = uint32_t((n + kGroup - 1) / kGroup);
    VkMemoryBarrier cb{};
    cb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    cb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    cb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    auto barrier = [&](VkCommandBuffer cmd) {
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &cb, 0, nullptr, 0, nullptr);
    };
    auto dispatch = [&](VkCommandBuffer cmd, uint32_t pass, uint32_t round) {
        FracPC pc{pass, round, uint32_t(n), 0};
        m_pipe->pushConstants(cmd, &pc, sizeof(pc));
        m_pipe->dispatch(cmd, groups);
        barrier(cmd);
    };
    auto rounds = [&](VkCommandBuffer cmd) {
        vkCmdFillBuffer(cmd, m_flags.buf, 0, VkDeviceSize(kRoundsPerSubmit) * 4, 0);
        barrier(cmd);
        for (int r = 0; r < kRoundsPerSubmit; ++r) {
            dispatch(cmd, 3, uint32_t(r));
            dispatch(cmd, 4, uint32_t(r));
        }
    };
    double ms = 0.0;
    // submit 1: upload, ids, bonds, labels, the first rounds
    if (!submit([&](VkCommandBuffer cmd) {
            VkBufferCopy up{0, 0, voxBytes};
            vkCmdCopyBuffer(cmd, m_stageIn.buf, m_vox.buf, 1, &up);
            VkBufferCopy upP{voxBytes, 0, prmBytes};
            vkCmdCopyBuffer(cmd, m_stageIn.buf, m_params.buf, 1, &upP);
            barrier(cmd);
            m_pipe->bind(cmd);
            dispatch(cmd, 0, 0);
            dispatch(cmd, 1, 0);
            dispatch(cmd, 2, 0);
            rounds(cmd);
        }, true, &ms, err))
        return false;
    auto flags = static_cast<const uint32_t*>(m_flags.mapped);
    auto settledAt = [&]() {
        for (int r = 0; r < kRoundsPerSubmit; ++r)
            if (flags[r] == 0) return r;
        return -1;
    };
    int done = settledAt();
    int total = kRoundsPerSubmit;
    while (done < 0) {
        if (total >= kMaxRounds) {
            if (err) *err = "FractureGpu: components did not settle in " + std::to_string(kMaxRounds) + " rounds";
            return false;
        }
        if (!submit([&](VkCommandBuffer cmd) { m_pipe->bind(cmd); rounds(cmd); }, true, &ms, err)) return false;
        done = settledAt();
        total += kRoundsPerSubmit;
    }
    m_lastRounds = total - kRoundsPerSubmit + done + 1;
    // read back cut + damage and the labels
    if (!submit([&](VkCommandBuffer cmd) {
            VkBufferCopy c0{0, 0, wordBytes};
            vkCmdCopyBuffer(cmd, m_out.buf, m_stageOut.buf, 1, &c0);
            VkBufferCopy c1{0, wordBytes, wordBytes};
            vkCmdCopyBuffer(cmd, m_label.buf, m_stageOut.buf, 1, &c1);
            VkMemoryBarrier hb{};
            hb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            hb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            hb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &hb, 0, nullptr, 0,
                                 nullptr);
        }, false, nullptr, err))
        return false;
    m_lastGpuMs = ms < 0.0 ? -1.0 : ms;

    const auto* o = static_cast<const uint32_t*>(m_stageOut.mapped);
    const uint32_t* lab = o + n;
    out.implemented = true;
    out.cut.resize(n);
    out.damage.resize(n);
    std::vector<uint32_t> par(n);
    for (size_t i = 0; i < n; ++i) {
        out.cut[i] = uint8_t(o[i] & 0xFFu);
        out.damage[i] = uint8_t((o[i] >> 8) & 0xFFu);
        par[i] = g.mat[i] ? lab[i] : uint32_t(i);
    }
    finishPieces(g, out, par, knobs);
    return true;
}

}  // namespace Phyxel::Core::VoxFound
