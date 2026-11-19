// docs/VoxelFoundationPlan.md §6.7, F5.2b -- the body solver on the GPU. See the header and
// shaders/voxfound_bodies.comp (buffer layouts, passes).
#include "core/voxfound/BodySimGpu.h"
#include "vulkan/ComputePipeline.h"

#include <algorithm>
#include <cstring>

namespace Phyxel::Core::VoxFound {

namespace {
constexpr uint32_t kGroup = 64;
struct BodyPC {
    uint32_t pass, arg, parity, n;
};
static_assert(sizeof(BodyPC) == 16, "voxfound_bodies.comp push constants");

// the parameter block (voxfound_bodies.comp P_*)
enum : uint32_t {
    P_NB = 0, P_WX, P_WY, P_WZ, P_WVM, P_GROUND, P_DT, P_GX, P_GY, P_GZ, P_ALPHA, P_BETA, P_GAMMA, P_PENMIN, P_PENMAX,
    P_MARGIN, P_CAP, P_REST, P_SLEEPV, P_SLEEPT, P_WAKEV, P_COLD, P_COLDBODY, P_IMPACT, P_HARD, P_MAXCONS, P_NMATS,
    P_GROUNDMU, P_ITERS, P_SCRATCH, P_WARMCAP, P_MAXPAIRS, P_MATS = 32
};
// state words (voxfound_bodies.comp S_*)
enum : uint32_t {
    S_CAND = 0, S_CAPPED, S_DEEP, S_PAIRS, S_HARD, S_OVF_SCRATCH, S_OVF_CONS, S_UNCOL, S_MAXCOL, S_MAXCONS, S_AWAKE,
    S_TICKCOL = 11, S_NPAIRS = 12, S_NHPAIRS = 13, S_NAWAKE = 14, S_OVF_PAIRS = 15, S_NCONS = 16, S_BODY = 32
};
// passes
enum : uint32_t {
    K_INTEGRATE = 0, K_AABB, K_WORLD, K_PAIRS, K_PREFIX, K_SCATTER, K_SORT, K_COLOUR_INIT, K_COLOUR_ROUND,
    K_COLOUR_FINAL, K_PRIMAL, K_DUAL, K_HARD_BODY, K_HARD_APPLY, K_FINISH, K_WARM_CLEAR, K_WARM_SAVE,
    K_FUSED_COLOUR, K_FUSED_PRIMAL,  // F5.5a: 7-9, and 10 for every colour, each in one dispatch (the fused pipeline)
    K_AWAKE_LIST, K_BROAD, K_NARROW, K_HARD_NARROW, K_HARD_PAIRS,  // F5.5a2: the narrowphase in parallel
    K_WORLD_PAR, K_HARD_APPLY_PAR                                   // F5.5a3: the world test in parallel
};
constexpr uint32_t kFusedGroup = 1024;

struct GBody {   // voxfound_bodies.comp Body, std430: 18 x 16 bytes
    float pos[4], quat[4], vel[4], angVel[4], initial[4], initialQuat[4], inertial[4], inertialQuat[4], cumAng[4];
    float comLocal[4], fixedProps[4], I0[4], I1[4], I2[4];
    uint32_t grid[4], meta[4];
    float lo[4], hi[4];
};
static_assert(sizeof(GBody) == 18 * 16, "Body layout");
constexpr VkDeviceSize kConBytes = 9 * 16, kCandBytes = 5 * 16, kWarmBytes = 4 * 16;

uint32_t fbits(float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}
void set4(float* d, double x, double y, double z, double w) {
    d[0] = float(x); d[1] = float(y); d[2] = float(z); d[3] = float(w);
}
}  // namespace

BodySimGpu::BodySimGpu() = default;
BodySimGpu::~BodySimGpu() { shutdown(); }
bool BodySimGpu::implemented() const { return true; }

bool BodySimGpu::init(VkDevice device, VkPhysicalDevice physical, VkQueue queue, uint32_t family,
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
        if (err) *err = "BodySimGpu: command pool / fence creation failed";
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
    if (!m_pipe->create(device, shaderDir + "/voxfound_bodies.comp.spv", 13, sizeof(BodyPC))) {
        if (err) *err = "BodySimGpu: cannot create the pipeline from " + shaderDir +
                        "/voxfound_bodies.comp.spv (run build_shaders.bat?)";
        shutdown();
        return false;
    }
    m_pipeFused = std::make_unique<Vulkan::ComputePipeline>();
    if (!m_pipeFused->create(device, shaderDir + "/voxfound_bodies_fused.comp.spv", 13, sizeof(BodyPC))) {
        if (err) *err = "BodySimGpu: cannot create the pipeline from " + shaderDir +
                        "/voxfound_bodies_fused.comp.spv (run build_shaders.bat?)";
        shutdown();
        return false;
    }
    if (m_queries) {
        VkQueryPoolCreateInfo q{};
        q.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        q.queryType = VK_QUERY_TYPE_TIMESTAMP;
        q.queryCount = kProfGroups + 1;
        if (vkCreateQueryPool(device, &q, nullptr, &m_profQueries) != VK_SUCCESS) m_profQueries = VK_NULL_HANDLE;
    }
    return true;
}

const char* BodySimGpu::profileName(int g) {
    static const char* names[kProfGroups] = {"integrate", "aabb", "world", "pairs", "prefix+scatter", "sort", "solve",
                                             "hard_body", "hard_apply", "finish", "warm"};
    return g >= 0 && g < kProfGroups ? names[g] : "?";
}

void BodySimGpu::shutdown() {
    if (m_device == VK_NULL_HANDLE) return;
    vkDeviceWaitIdle(m_device);
    for (Buffer* b : {&m_params, &m_bodies, &m_bodyVox, &m_corners, &m_world, &m_cons, &m_scratch, &m_state, &m_lists,
                      &m_warm, &m_push, &m_stage, &m_counters, &m_aux, &m_hres})
        destroyBuffer(*b);
    if (m_pipe) m_pipe->cleanup();
    m_pipe.reset();
    if (m_pipeFused) m_pipeFused->cleanup();
    m_pipeFused.reset();
    if (m_profQueries) vkDestroyQueryPool(m_device, m_profQueries, nullptr);
    m_profQueries = VK_NULL_HANDLE;
    if (m_queries) vkDestroyQueryPool(m_device, m_queries, nullptr);
    if (m_fence) vkDestroyFence(m_device, m_fence, nullptr);
    if (m_pool) vkDestroyCommandPool(m_device, m_pool, nullptr);
    m_queries = VK_NULL_HANDLE;
    m_fence = VK_NULL_HANDLE;
    m_pool = VK_NULL_HANDLE;
    m_device = VK_NULL_HANDLE;
    m_timestampMs = 0.0;
    m_nb = 0;
}

uint32_t BodySimGpu::findMemoryType(uint32_t filter, VkMemoryPropertyFlags props) const {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(m_physical, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if ((filter & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props) return i;
    return UINT32_MAX;
}

bool BodySimGpu::createBuffer(Buffer& b, VkDeviceSize bytes, bool hostVisible, std::string* err) {
    b.bytes = std::max<VkDeviceSize>(bytes, 16);
    VkBufferCreateInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = b.bytes;
    bi.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(m_device, &bi, nullptr, &b.buf) != VK_SUCCESS) {
        if (err) *err = "BodySimGpu: vkCreateBuffer failed for " + std::to_string(b.bytes) + " bytes";
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
        if (err) *err = "BodySimGpu: cannot allocate " + std::to_string(mr.size) + " bytes";
        vkDestroyBuffer(m_device, b.buf, nullptr);
        b = Buffer{};
        return false;
    }
    vkBindBufferMemory(m_device, b.buf, b.mem, 0);
    if (hostVisible) vkMapMemory(m_device, b.mem, 0, b.bytes, 0, &b.mapped);
    return true;
}

void BodySimGpu::destroyBuffer(Buffer& b) {
    if (m_device == VK_NULL_HANDLE) { b = Buffer{}; return; }
    if (b.mapped) vkUnmapMemory(m_device, b.mem);
    if (b.buf) vkDestroyBuffer(m_device, b.buf, nullptr);
    if (b.mem) vkFreeMemory(m_device, b.mem, nullptr);
    b = Buffer{};
}

bool BodySimGpu::submit(const std::function<void(VkCommandBuffer)>& record, bool timed, double* ms, std::string* err) {
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
    if (sub != VK_SUCCESS || wait != VK_SUCCESS) {
        if (err) *err = "BodySimGpu: submit " + std::to_string(int(sub)) + " wait " + std::to_string(int(wait));
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

namespace {
void barrier(VkCommandBuffer cmd) {
    VkMemoryBarrier mb{};
    mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT |
                       VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &mb, 0, nullptr, 0,
                         nullptr);
}
}  // namespace

bool BodySimGpu::load(const VoxelGrid& world, const std::vector<FractureMaterial>& mats,
                      const std::vector<SimBody>& bodies, const BodySimParams& p, std::string* err) {
    if (!initialized()) { if (err) *err = "BodySimGpu: not initialised"; return false; }
    if (p.contactCap > 16) { if (err) *err = "BodySimGpu: contactCap > 16"; return false; }
    if (bodies.empty()) { if (err) *err = "BodySimGpu: no bodies"; return false; }
    vkDeviceWaitIdle(m_device);
    for (Buffer* b : {&m_params, &m_bodies, &m_bodyVox, &m_corners, &m_world, &m_cons, &m_scratch, &m_state, &m_lists,
                      &m_warm, &m_push, &m_stage, &m_counters, &m_aux, &m_hres})
        destroyBuffer(*b);
    m_nb = uint32_t(bodies.size());
    m_maxCons = std::max<uint32_t>(4096, m_nb * uint32_t(p.contactCap) * 16);
    m_iterations = uint32_t(p.iterations);
    m_parity = 0;
    m_tick = 0;
    m_lastAwake = m_nb;

    std::vector<uint32_t> prm(P_MATS + mats.size(), 0);
    prm[P_NB] = m_nb;
    prm[P_WX] = uint32_t(world.dim.x); prm[P_WY] = uint32_t(world.dim.y); prm[P_WZ] = uint32_t(world.dim.z);
    prm[P_WVM] = fbits(world.voxelM);
    prm[P_GROUND] = p.groundBelow ? 1u : 0u;
    prm[P_DT] = fbits(float(p.dt));
    prm[P_GX] = fbits(float(p.gravity.x)); prm[P_GY] = fbits(float(p.gravity.y)); prm[P_GZ] = fbits(float(p.gravity.z));
    prm[P_ALPHA] = fbits(float(p.alpha)); prm[P_BETA] = fbits(float(p.beta)); prm[P_GAMMA] = fbits(float(p.gamma));
    prm[P_PENMIN] = fbits(float(p.penaltyMin)); prm[P_PENMAX] = fbits(float(p.penaltyMax));
    prm[P_MARGIN] = fbits(float(p.margin));
    prm[P_CAP] = uint32_t(p.contactCap);
    prm[P_REST] = fbits(float(p.restSpeed));
    prm[P_SLEEPV] = fbits(float(p.sleepSpeed));
    prm[P_SLEEPT] = uint32_t(p.sleepTicks);
    prm[P_WAKEV] = fbits(float(p.wakeSpeed));
    prm[P_COLD] = fbits(float(p.coldScale));
    prm[P_COLDBODY] = fbits(float(p.coldScaleBody));
    prm[P_IMPACT] = fbits(float(p.impactSpeed));
    prm[P_HARD] = fbits(float(p.hardContact));
    prm[P_MAXCONS] = m_maxCons;
    prm[P_NMATS] = uint32_t(mats.size());
    prm[P_GROUNDMU] = fbits(0.8f);   // BodySim.cpp Detector::groundMu
    prm[P_ITERS] = m_iterations;
    prm[P_SCRATCH] = kScratch;
    prm[P_WARMCAP] = kWarmCap;
    m_maxPairs = 4096;   // a power of two (the hard-pair hash has 2x as many entries)
    while (m_maxPairs < m_nb * 32u) m_maxPairs <<= 1;
    prm[P_MAXPAIRS] = m_maxPairs;
    for (size_t m = 0; m < mats.size(); ++m) prm[P_MATS + m] = fbits(mats[m].friction);

    std::vector<GBody> gb(m_nb);
    std::vector<uint32_t> vox;
    std::vector<float> cor;
    for (uint32_t i = 0; i < m_nb; ++i) {
        const SimBody& s = bodies[i];
        GBody& g = gb[i];
        std::memset(&g, 0, sizeof(g));
        set4(g.pos, s.pos.x, s.pos.y, s.pos.z, s.invMass);
        set4(g.quat, s.quat.x, s.quat.y, s.quat.z, s.quat.w);
        set4(g.vel, s.vel.x, s.vel.y, s.vel.z, 0);
        set4(g.angVel, s.angVel.x, s.angVel.y, s.angVel.z, 0);
        set4(g.initial, s.pos.x, s.pos.y, s.pos.z, 0);
        set4(g.initialQuat, s.quat.x, s.quat.y, s.quat.z, s.quat.w);
        set4(g.inertial, s.pos.x, s.pos.y, s.pos.z, 0);
        set4(g.inertialQuat, s.quat.x, s.quat.y, s.quat.z, s.quat.w);
        double radius = 0.0;
        for (const glm::dvec3& c : s.corners) radius = std::max(radius, glm::length(c));
        const glm::dvec3 comLocal = s.src.com - glm::dvec3(s.src.origin);
        set4(g.comLocal, comLocal.x, comLocal.y, comLocal.z, radius);
        set4(g.fixedProps, s.src.mass, s.friction, s.src.grid.voxelM, 0);
        const glm::dmat3& I = s.inertiaBody;   // glm: I[column][row]; symmetric
        set4(g.I0, I[0][0], I[1][0], I[2][0], 0);
        set4(g.I1, I[0][1], I[1][1], I[2][1], 0);
        set4(g.I2, I[0][2], I[1][2], I[2][2], 0);
        g.grid[0] = uint32_t(vox.size());
        g.grid[1] = uint32_t(s.src.grid.dim.x); g.grid[2] = uint32_t(s.src.grid.dim.y);
        g.grid[3] = uint32_t(s.src.grid.dim.z);
        for (uint8_t m : s.src.grid.mat) vox.push_back(m);
        g.meta[0] = uint32_t(cor.size() / 4);
        g.meta[1] = uint32_t(s.corners.size());
        g.meta[2] = s.asleep ? 1u : 0u;
        g.meta[3] = uint32_t(s.slowTicks);
        for (size_t k = 0; k < s.corners.size(); ++k) {
            cor.push_back(float(s.corners[k].x));
            cor.push_back(float(s.corners[k].y));
            cor.push_back(float(s.corners[k].z));
            cor.push_back(float(s.cornerDir[k]));
        }
    }
    std::vector<uint32_t> wv(world.mat.begin(), world.mat.end());

    const VkDeviceSize bP = prm.size() * 4, bB = gb.size() * sizeof(GBody), bV = std::max<size_t>(vox.size(), 1) * 4,
                       bC = std::max<size_t>(cor.size(), 4) * 4, bW = std::max<size_t>(wv.size(), 1) * 4;
    const VkDeviceSize bState = (S_BODY + 5ull * m_nb) * 4;
    if (!createBuffer(m_params, bP, false, err) || !createBuffer(m_bodies, bB, false, err) ||
        !createBuffer(m_bodyVox, bV, false, err) || !createBuffer(m_corners, bC, false, err) ||
        !createBuffer(m_world, bW, false, err) || !createBuffer(m_cons, m_maxCons * kConBytes, false, err) ||
        !createBuffer(m_scratch, VkDeviceSize(m_nb) * kScratch * kCandBytes, false, err) ||
        !createBuffer(m_state, bState, false, err) || !createBuffer(m_lists, 2ull * m_maxCons * 4, false, err) ||
        !createBuffer(m_warm, 2ull * kWarmCap * kWarmBytes, false, err) ||
        !createBuffer(m_push, VkDeviceSize(m_nb) * 16, false, err) ||
        !createBuffer(m_stage, std::max<VkDeviceSize>(bP + bB + bV + bC + bW, bB), true, err) ||
        !createBuffer(m_counters, 32 * 4, true, err) ||
        !createBuffer(m_aux, (2ull * m_nb + 8ull * m_maxPairs) * 4, false, err) ||
        !createBuffer(m_hres, VkDeviceSize(m_maxPairs) * 16, false, err))
        return false;
    Buffer* bind[13] = {&m_params, &m_bodies, &m_bodyVox, &m_corners, &m_world, &m_cons, &m_scratch,
                        &m_state,  &m_lists,  &m_warm,    &m_push,    &m_aux,   &m_hres};
    for (uint32_t k = 0; k < 13; ++k) {
        m_pipe->bindBuffer(k, bind[k]->buf, bind[k]->bytes);
        m_pipeFused->bindBuffer(k, bind[k]->buf, bind[k]->bytes);
    }
    m_pipe->updateDescriptors();
    m_pipeFused->updateDescriptors();

    auto* st = static_cast<uint8_t*>(m_stage.mapped);
    VkDeviceSize off = 0;
    std::memcpy(st + off, prm.data(), size_t(bP)); off += bP;
    std::memcpy(st + off, gb.data(), size_t(bB)); off += bB;
    if (!vox.empty()) std::memcpy(st + off, vox.data(), vox.size() * 4);
    off += bV;
    if (!cor.empty()) std::memcpy(st + off, cor.data(), cor.size() * 4);
    off += bC;
    if (!wv.empty()) std::memcpy(st + off, wv.data(), wv.size() * 4);
    return submit([&](VkCommandBuffer cmd) {
        VkDeviceSize o = 0;
        auto up = [&](Buffer& dst, VkDeviceSize bytes) {
            VkBufferCopy c{o, 0, bytes};
            vkCmdCopyBuffer(cmd, m_stage.buf, dst.buf, 1, &c);
            o += bytes;
        };
        up(m_params, bP);
        up(m_bodies, bB);
        up(m_bodyVox, bV);
        up(m_corners, bC);
        up(m_world, bW);
        vkCmdFillBuffer(cmd, m_state.buf, 0, VK_WHOLE_SIZE, 0);
        vkCmdFillBuffer(cmd, m_warm.buf, 0, VK_WHOLE_SIZE, 0);
        vkCmdFillBuffer(cmd, m_push.buf, 0, VK_WHOLE_SIZE, 0);
        barrier(cmd);
    }, false, nullptr, err);
}

void BodySimGpu::recordTick(VkCommandBuffer cmd) {
    const uint32_t parity = m_tick & 1u;
    auto run = [&](uint32_t pass, uint32_t arg, uint32_t threads) {
        BodyPC pc{pass, arg, parity, threads};
        m_pipe->pushConstants(cmd, &pc, sizeof(pc));
        m_pipe->dispatch(cmd, (threads + kGroup - 1) / kGroup);
        barrier(cmd);
    };
    const bool prof = m_profile && m_profQueries;
    auto stamp = [&]() {
        if (prof) vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_profQueries, m_profStamp++);
    };
    if (prof) {
        vkCmdResetQueryPool(cmd, m_profQueries, 0, kProfGroups + 1);
        m_profStamp = 0;
    }
    vkCmdFillBuffer(cmd, m_state.buf, S_AWAKE * 4, 4, 0);
    vkCmdFillBuffer(cmd, m_state.buf, S_NCONS * 4, 4, 0);
    if (m_narrowPar || m_worldPar) vkCmdFillBuffer(cmd, m_state.buf, S_NPAIRS * 4, 3 * 4, 0);   // + S_NAWAKE
    if (m_narrowPar) {   // F5.5a2: the hard-pair hash and the per-body flags
        const VkDeviceSize hashOff = (VkDeviceSize(m_nb) + 4ull * m_maxPairs) * 4;
        vkCmdFillBuffer(cmd, m_aux.buf, hashOff, m_aux.bytes - hashOff, 0);
    }
    barrier(cmd);
    stamp();
    m_pipe->bind(cmd);
    run(K_INTEGRATE, 0, m_nb);
    stamp();
    run(K_AABB, 0, m_nb);
    stamp();
    const uint32_t groups = std::min<uint32_t>(m_nb, 512);   // persistent workgroups, one scratch region each
    auto perPair = [&](uint32_t pass) {   // one workgroup per pair / per awake body
        BodyPC pc{pass, 0, parity, 0};
        m_pipe->pushConstants(cmd, &pc, sizeof(pc));
        m_pipe->dispatch(cmd, groups);
        barrier(cmd);
    };
    if (m_narrowPar || m_worldPar) run(K_AWAKE_LIST, 0, m_nb);
    if (m_worldPar) perPair(K_WORLD_PAR);   // F5.5a3
    else run(K_WORLD, 0, m_nb);
    stamp();
    if (m_narrowPar) {   // F5.5a2
        run(K_BROAD, 0, m_nb);
        perPair(K_NARROW);
    } else {
        run(K_PAIRS, 0, m_nb);
    }
    stamp();
    run(K_PREFIX, 0, 1);
    run(K_SCATTER, 0, m_maxCons);
    stamp();
    run(K_SORT, 0, m_nb);
    stamp();
    if (m_fused) {   // F5.5a: the colour rounds and every iteration, one workgroup
        auto one = [&](uint32_t pass) {
            m_pipeFused->bind(cmd);
            BodyPC pc{pass, 0, parity, kFusedGroup};
            m_pipeFused->pushConstants(cmd, &pc, sizeof(pc));
            m_pipeFused->dispatch(cmd, 1);
            barrier(cmd);
            m_pipe->bind(cmd);
        };
        one(K_FUSED_COLOUR);
        for (uint32_t it = 0; it < m_iterations; ++it) {
            one(K_FUSED_PRIMAL);
            run(K_DUAL, 0, m_maxCons);
        }
    } else {
        run(K_COLOUR_INIT, 0, m_nb);
        for (uint32_t r = 0; r < kMaxColours; ++r) run(K_COLOUR_ROUND, r, m_nb);
        run(K_COLOUR_FINAL, 0, m_nb);
        for (uint32_t it = 0; it < m_iterations; ++it) {
            for (uint32_t c = 0; c < kMaxColours; ++c) run(K_PRIMAL, c, m_nb);
            run(K_DUAL, 0, m_maxCons);
        }
    }
    stamp();
    run(K_AABB, 0, m_nb);
    if (m_narrowPar) {   // F5.5a2: hard pairs at the post-solve poses, one workgroup each, then the per-body sums
        run(K_BROAD, 1, m_nb);
        perPair(K_HARD_NARROW);
        run(K_HARD_PAIRS, 0, m_nb);
    } else {
        run(K_HARD_BODY, 0, m_nb);
    }
    stamp();
    if (m_worldPar) perPair(K_HARD_APPLY_PAR);   // F5.5a3
    else run(K_HARD_APPLY, 0, m_nb);
    stamp();
    run(K_FINISH, 0, m_nb);
    stamp();
    run(K_WARM_CLEAR, 0, kWarmCap);
    run(K_WARM_SAVE, 0, m_maxCons);
    stamp();
    ++m_tick;
}

bool BodySimGpu::step(int ticks, std::string* err, int ticksPerSubmit) {
    if (!initialized() || m_nb == 0) { if (err) *err = "BodySimGpu: not loaded"; return false; }
    m_lastGpuMs = 0.0;
    m_fused = m_lastAwake <= m_fusedMaxAwake;
    bool first = true;
    for (int done = 0; done < ticks;) {
        const int n = m_profile ? 1 : std::min(std::max(ticksPerSubmit, 1), ticks - done);
        if (!submit([&](VkCommandBuffer cmd) {
                if (first) {   // the summed counters start at 0 for this call
                    vkCmdFillBuffer(cmd, m_state.buf, 0, 16 * 4, 0);
                    barrier(cmd);
                }
                for (int t = 0; t < n; ++t) recordTick(cmd);
                if (done + n == ticks) {
                    VkBufferCopy c{0, 0, 32 * 4};
                    vkCmdCopyBuffer(cmd, m_state.buf, m_counters.buf, 1, &c);
                    VkMemoryBarrier hb{};
                    hb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
                    hb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                    hb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
                    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &hb, 0,
                                         nullptr, 0, nullptr);
                }
            }, true, &m_lastGpuMs, err))
            return false;
        if (m_profile && m_profQueries) {
            uint64_t ts[kProfGroups + 1] = {};
            if (vkGetQueryPoolResults(m_device, m_profQueries, 0, kProfGroups + 1, sizeof(ts), ts, sizeof(uint64_t),
                                      VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT) == VK_SUCCESS) {
                for (int g = 0; g < kProfGroups; ++g) {
                    const double ms = double(ts[g + 1] - ts[g]) * m_timestampMs;
                    m_prof.sumMs[g] += ms;
                    m_prof.minMs[g] = m_prof.ticks == 0 ? ms : std::min(m_prof.minMs[g], ms);
                }
                ++m_prof.ticks;
            }
        }
        first = false;
        done += n;
    }
    if (m_lastGpuMs < 0.0) m_lastGpuMs = -1.0;
    const auto* s = static_cast<const uint32_t*>(m_counters.mapped);
    m_stats = {};
    m_stats.contacts = std::min(s[S_NCONS], m_maxCons);
    m_stats.maxContacts = s[S_MAXCONS];
    m_stats.candidates = s[S_CAND];
    m_stats.capped = s[S_CAPPED];
    m_stats.deepPoints = s[S_DEEP];
    m_stats.pairsTested = s[S_PAIRS];
    m_stats.hardFires = s[S_HARD];
    m_stats.scratchOverflow = s[S_OVF_SCRATCH];
    m_stats.constraintOverflow = s[S_OVF_CONS];
    m_stats.uncoloured = s[S_UNCOL];
    m_stats.pairOverflow = s[S_OVF_PAIRS];
    m_stats.colours = s[S_MAXCOL];
    m_stats.awake = s[S_AWAKE];
    m_lastAwake = m_stats.awake;
    return true;
}

bool BodySimGpu::read(std::vector<SimBody>& bodies, std::string* err) {
    if (!initialized() || m_nb == 0 || bodies.size() != m_nb) {
        if (err) *err = "BodySimGpu: read needs the loaded body count";
        return false;
    }
    const VkDeviceSize bB = VkDeviceSize(m_nb) * sizeof(GBody);
    if (!submit([&](VkCommandBuffer cmd) {
            VkBufferCopy c{0, 0, bB};
            vkCmdCopyBuffer(cmd, m_bodies.buf, m_stage.buf, 1, &c);
            VkMemoryBarrier hb{};
            hb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            hb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            hb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &hb, 0, nullptr,
                                 0, nullptr);
        }, false, nullptr, err))
        return false;
    const auto* gb = static_cast<const GBody*>(m_stage.mapped);
    for (uint32_t i = 0; i < m_nb; ++i) {
        SimBody& s = bodies[i];
        const GBody& g = gb[i];
        s.pos = glm::dvec3(g.pos[0], g.pos[1], g.pos[2]);
        s.quat = glm::dquat(g.quat[3], g.quat[0], g.quat[1], g.quat[2]);
        s.vel = glm::dvec3(g.vel[0], g.vel[1], g.vel[2]);
        s.angVel = glm::dvec3(g.angVel[0], g.angVel[1], g.angVel[2]);
        s.asleep = g.meta[2] != 0;
        s.slowTicks = int(g.meta[3]);
    }
    return true;
}

}  // namespace Phyxel::Core::VoxFound
