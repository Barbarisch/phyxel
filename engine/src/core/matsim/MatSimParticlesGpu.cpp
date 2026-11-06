// docs/MaterialSimPlan.md §6h — the GPU port of MatSimParticles::step. See the header.
#include "core/matsim/MatSimParticlesGpu.h"
#include "core/matsim/MatSimParticles.h"
#include "vulkan/ComputePipeline.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace Phyxel::Core::MatSim {

namespace {
constexpr uint32_t kGroup = 256;
constexpr float kPi = 3.14159265358979f;

enum Mode : int32_t {
    M_PREDICT = 0, M_COUNT = 1, M_SCAN_LOCAL = 2, M_SCAN_BLOCKS = 3, M_SCAN_ADD = 4, M_SCATTER = 5,
    M_SORT_BINS = 6, M_LAMBDA = 7, M_DELTA = 8, M_APPLY_DELTA = 9, M_VELOCITY = 10, M_KAPPA = 11,
    M_DV = 12, M_APPLY_DV = 13, M_COMMIT = 14
};

struct PC {
    int32_t origin[4], size[4], gLo[4], gDim[4];
    float k0[4], k1[4], k2[4];
};
static_assert(sizeof(PC) == 112, "matsim_pbf.comp push constants");

struct GpuParticle { float pos[4]; float vel[4]; };
static_assert(sizeof(GpuParticle) == 32, "matsim_pbf.comp struct P");
constexpr VkDeviceSize kWorkBytes = 64;   // struct Wk

void groupsFor(uint64_t count, uint32_t& gx, uint32_t& gy) {
    const uint64_t groups = std::max<uint64_t>(1, (count + kGroup - 1) / kGroup);
    gx = static_cast<uint32_t>(std::min<uint64_t>(groups, 65535));
    gy = static_cast<uint32_t>((groups + gx - 1) / gx);
}
int floorI(float v) { return int(std::floor(v)); }
} // namespace

MatSimParticlesGpu::MatSimParticlesGpu() = default;
MatSimParticlesGpu::~MatSimParticlesGpu() { shutdown(); }

bool MatSimParticlesGpu::init(VkDevice device, VkPhysicalDevice physical, VkQueue queue, uint32_t family,
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
        if (err) *err = "MatSimParticlesGpu: command pool / fence creation failed";
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
    if (!m_pipe->create(device, shaderDir + "/matsim_pbf.comp.spv", 9, sizeof(PC), 1)) {
        if (err) *err = "MatSimParticlesGpu: cannot create the pipeline from " + shaderDir +
                        "/matsim_pbf.comp.spv (run build_shaders.bat?)";
        shutdown();
        return false;
    }
    return true;
}

void MatSimParticlesGpu::shutdown() {
    if (m_device == VK_NULL_HANDLE) return;
    vkDeviceWaitIdle(m_device);
    release();
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

void MatSimParticlesGpu::release() {
    for (Buffer* b : {&m_parts, &m_work, &m_keys, &m_sorted, &m_count, &m_start, &m_blocks, &m_cells, &m_mat, &m_staging})
        destroyBuffer(*b);
}

uint32_t MatSimParticlesGpu::findMemoryType(uint32_t filter, VkMemoryPropertyFlags props) const {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(m_physical, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if ((filter & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props) return i;
    return UINT32_MAX;
}

bool MatSimParticlesGpu::createBuffer(Buffer& b, VkDeviceSize bytes, bool hostVisible, std::string* err) {
    b.bytes = std::max<VkDeviceSize>(bytes, 16);
    VkBufferCreateInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = b.bytes;
    bi.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(m_device, &bi, nullptr, &b.buf) != VK_SUCCESS) {
        if (err) *err = "MatSimParticlesGpu: vkCreateBuffer failed for " + std::to_string(b.bytes) + " bytes";
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
    ai.memoryTypeIndex = hostVisible ? findMemoryType(mr.memoryTypeBits, want | VK_MEMORY_PROPERTY_HOST_CACHED_BIT) : UINT32_MAX;
    if (ai.memoryTypeIndex == UINT32_MAX) ai.memoryTypeIndex = findMemoryType(mr.memoryTypeBits, want);
    if (ai.memoryTypeIndex == UINT32_MAX || vkAllocateMemory(m_device, &ai, nullptr, &b.mem) != VK_SUCCESS) {
        if (err) *err = "MatSimParticlesGpu: cannot allocate " + std::to_string(mr.size) + " bytes";
        vkDestroyBuffer(m_device, b.buf, nullptr);
        b = Buffer{};
        return false;
    }
    vkBindBufferMemory(m_device, b.buf, b.mem, 0);
    if (hostVisible) vkMapMemory(m_device, b.mem, 0, b.bytes, 0, &b.mapped);
    return true;
}

void MatSimParticlesGpu::destroyBuffer(Buffer& b) {
    if (m_device == VK_NULL_HANDLE) { b = Buffer{}; return; }
    if (b.mapped) vkUnmapMemory(m_device, b.mem);
    if (b.buf) vkDestroyBuffer(m_device, b.buf, nullptr);
    if (b.mem) vkFreeMemory(m_device, b.mem, nullptr);
    b = Buffer{};
}

// Grow-only: reallocate when too small (1.5x headroom), keep otherwise.
bool MatSimParticlesGpu::ensure(Buffer& b, VkDeviceSize bytes, bool hostVisible, bool& changed, std::string* err) {
    if (b.buf && b.bytes >= bytes) return true;
    destroyBuffer(b);
    changed = true;
    return createBuffer(b, bytes + bytes / 2, hostVisible, err);
}

bool MatSimParticlesGpu::step(MatSimParticles& ps, const MatSimCore& core, double dtFrame, std::string* err) {
    const auto wall0 = std::chrono::steady_clock::now();
    if (!initialized()) { if (err) *err = "not initialised"; return false; }
    const ParticleConfig& cfg = ps.config();
    if (cfg.tensileK > 0.f) { if (err) *err = "tensileK > 0 is not implemented on the GPU"; return false; }
    std::vector<Particle>& p = ps.mutableParticles();
    const size_t n = p.size();
    if (!n || dtFrame <= 0) { ps.setLastSubsteps(0); return true; }

    // The CPU step's constants, computed the same way.
    const float h = cfg.kernelCells, h2 = h * h;
    const float poly6 = 315.f / (64.f * kPi * std::pow(h, 9.f));
    const float spiky = -45.f / (kPi * std::pow(h, 6.f));
    const float rho0 = ps.restDensity();
    const float g = cfg.gravity * float(core.cellsPerM());
    const int ns = ps.substepsFor(core, dtFrame);
    const float dt = float(dtFrame) / float(ns);
    const IVec3 o = core.origin(), s = core.size();
    const IVec3 gLo{floorI(o.x / h) - 1, floorI(o.y / h) - 1, floorI(o.z / h) - 1};
    const IVec3 gDim{floorI((o.x + s.x) / h) - gLo.x + 2, floorI((o.y + s.y) / h) - gLo.y + 2,
                     floorI((o.z + s.z) / h) - gLo.z + 2};
    const size_t nb = size_t(gDim.x) * gDim.y * gDim.z;
    const size_t nBlocks = (nb + kGroup - 1) / kGroup;
    const size_t nCells = core.cells().size();
    const uint32_t F = core.full();

    // Buffers (grow-only); rebind when any was reallocated.
    const VkDeviceSize partBytes = n * sizeof(GpuParticle), cellBytes = nCells * 4, matBytes = 256 * 16;
    bool changed = false;
    bool ok = ensure(m_parts, partBytes, false, changed, err) && ensure(m_work, n * kWorkBytes, false, changed, err) &&
              ensure(m_keys, n * 4, false, changed, err) && ensure(m_sorted, n * 4, false, changed, err) &&
              ensure(m_count, nb * 4, false, changed, err) && ensure(m_start, (nb + 1) * 4, false, changed, err) &&
              ensure(m_blocks, nBlocks * 4, false, changed, err) && ensure(m_cells, cellBytes, false, changed, err) &&
              ensure(m_mat, matBytes, false, changed, err) &&
              ensure(m_staging, partBytes + cellBytes + matBytes, true, changed, err);
    if (!ok) { release(); return false; }
    if (changed) {
        const Buffer* bind[9] = {&m_parts, &m_work, &m_keys, &m_sorted, &m_count, &m_start, &m_blocks, &m_cells, &m_mat};
        for (uint32_t b = 0; b < 9; ++b) m_pipe->bindBuffer(b, bind[b]->buf, bind[b]->bytes);
        m_pipe->updateDescriptors();
    }

    // Staging: [particles | cells | material table].
    auto* base = static_cast<uint8_t*>(m_staging.mapped);
    auto* gp = reinterpret_cast<GpuParticle*>(base);
    for (size_t i = 0; i < n; ++i) {
        const Particle& q = p[i];
        gp[i] = {{q.x, q.y, q.z, float(q.units) / float(F)}, {q.vx, q.vy, q.vz, 0.f}};
    }
    std::memcpy(base + partBytes, core.cells().data(), cellBytes);
    auto* mt = reinterpret_cast<uint32_t*>(base + partBytes + cellBytes);
    for (int m = 0; m < 256; ++m) {
        mt[4 * m] = static_cast<uint32_t>(materialTable()[m].phase);
        mt[4 * m + 1] = mt[4 * m + 2] = mt[4 * m + 3] = 0;
    }

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
    auto barrier = [&](VkPipelineStageFlags src, VkAccessFlags sa, VkPipelineStageFlags dst, VkAccessFlags da) {
        VkMemoryBarrier mb{};
        mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        mb.srcAccessMask = sa;
        mb.dstAccessMask = da;
        vkCmdPipelineBarrier(cmd, src, dst, 0, 1, &mb, 0, nullptr, 0, nullptr);
    };
    const bool timed = m_queries != VK_NULL_HANDLE && m_timestampMs > 0.0;
    if (timed) vkCmdResetQueryPool(cmd, m_queries, 0, 2);
    {
        VkBufferCopy r0{0, 0, partBytes}, r1{partBytes, 0, cellBytes}, r2{partBytes + cellBytes, 0, matBytes};
        vkCmdCopyBuffer(cmd, m_staging.buf, m_parts.buf, 1, &r0);
        vkCmdCopyBuffer(cmd, m_staging.buf, m_cells.buf, 1, &r1);
        vkCmdCopyBuffer(cmd, m_staging.buf, m_mat.buf, 1, &r2);
    }
    const VkPipelineStageFlags CS = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, TR = VK_PIPELINE_STAGE_TRANSFER_BIT;
    const VkAccessFlags RW = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    barrier(TR, VK_ACCESS_TRANSFER_WRITE_BIT, CS, RW);
    if (timed) vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, m_queries, 0);

    PC pc{};
    pc.origin[0] = o.x; pc.origin[1] = o.y; pc.origin[2] = o.z;
    pc.size[0] = s.x; pc.size[1] = s.y; pc.size[2] = s.z; pc.size[3] = int32_t(n);
    pc.gLo[0] = gLo.x; pc.gLo[1] = gLo.y; pc.gLo[2] = gLo.z; pc.gLo[3] = int32_t(nb);
    pc.gDim[0] = gDim.x; pc.gDim[1] = gDim.y; pc.gDim[2] = gDim.z; pc.gDim[3] = int32_t(F);
    pc.k0[0] = h; pc.k0[1] = h2; pc.k0[2] = poly6; pc.k0[3] = spiky;
    pc.k1[0] = rho0; pc.k1[1] = cfg.relaxation; pc.k1[2] = dt; pc.k1[3] = g;
    pc.k2[0] = cfg.omega; pc.k2[1] = cfg.viscosity; pc.k2[2] = cfg.minC; pc.k2[3] = float(int(std::ceil(h)));
    m_pipe->bind(cmd);
    auto run = [&](int32_t mode, uint64_t count) {
        pc.origin[3] = mode;
        uint32_t gx, gy;
        groupsFor(count, gx, gy);
        m_pipe->pushConstants(cmd, &pc, sizeof(pc));
        m_pipe->dispatch(cmd, gx, gy);
        barrier(CS, VK_ACCESS_SHADER_WRITE_BIT, CS | TR, RW | VK_ACCESS_TRANSFER_WRITE_BIT);
    };
    // MATSIM_GPU_STUB=1: record no substeps (the particles freeze). A negative control: the
    // MatSimParticlesGpu tests must fail with it set (shown red in 6h).
    const int nsRun = std::getenv("MATSIM_GPU_STUB") ? 0 : ns;
    for (int sub = 0; sub < nsRun; ++sub) {
        run(M_PREDICT, n);
        vkCmdFillBuffer(cmd, m_count.buf, 0, VK_WHOLE_SIZE, 0);
        barrier(TR, VK_ACCESS_TRANSFER_WRITE_BIT, CS, RW);
        run(M_COUNT, n);
        run(M_SCAN_LOCAL, nb);
        run(M_SCAN_BLOCKS, 1);
        run(M_SCAN_ADD, nb);
        run(M_SCATTER, n);
        run(M_SORT_BINS, nb);
        for (int it = 0; it < cfg.iterations; ++it) {
            run(M_LAMBDA, n);
            run(M_DELTA, n);
            run(M_APPLY_DELTA, n);
        }
        run(M_VELOCITY, n);
        for (int it = 0; it < cfg.divergenceIterations; ++it) {
            run(M_KAPPA, n);
            run(M_DV, n);
            run(M_APPLY_DV, n);
        }
        run(M_COMMIT, n);
    }
    if (timed) vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_queries, 1);
    barrier(CS, VK_ACCESS_SHADER_WRITE_BIT, TR, VK_ACCESS_TRANSFER_READ_BIT);
    {
        VkBufferCopy r{0, 0, partBytes};
        vkCmdCopyBuffer(cmd, m_parts.buf, m_staging.buf, 1, &r);
    }
    barrier(TR, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
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
        if (err) *err = "submit " + std::to_string(int(sub)) + " wait " + std::to_string(int(wait));
        return false;
    }
    m_lastMs = -1.0;
    if (timed) {
        uint64_t ts[2] = {0, 0};
        if (vkGetQueryPoolResults(m_device, m_queries, 0, 2, sizeof(ts), ts, sizeof(uint64_t),
                                  VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT) == VK_SUCCESS)
            m_lastMs = double(ts[1] - ts[0]) * m_timestampMs;
    }
    for (size_t i = 0; i < n; ++i) {
        Particle& q = p[i];
        q.x = gp[i].pos[0]; q.y = gp[i].pos[1]; q.z = gp[i].pos[2];
        q.vx = gp[i].vel[0]; q.vy = gp[i].vel[1]; q.vz = gp[i].vel[2];
    }
    ps.setLastSubsteps(ns);
    ps.endFrame(dtFrame);
    m_lastWallMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - wall0).count();
    return true;
}

} // namespace Phyxel::Core::MatSim
