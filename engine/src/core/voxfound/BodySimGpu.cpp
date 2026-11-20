// docs/VoxelFoundationPlan.md §6.7, F5.2b -- the body solver on the GPU. See the header and
// shaders/voxfound_bodies.comp (buffer layouts, passes).
#include "core/voxfound/BodySimGpu.h"
#include "vulkan/ComputePipeline.h"

#include <algorithm>
#include <chrono>
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
    K_WORLD_PAR, K_HARD_APPLY_PAR,                                  // F5.5a3: the world test in parallel
    K_COLOUR_COUNT, K_COLOUR_PREFIX, K_COLOUR_SCATTER, K_PRIMAL_SUB,    // F5.5a4: the primal in parallel
    K_PRIMAL_ONE   // 29 / 30 run on the primal pipeline (voxfound_bodies_primal.comp)
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
// F5.5c: one body's device record (load and edit); its voxels at voxOff in bodyVox, its corners at corOff in corners
double bodyRadiusOf(const SimBody& s) {   // the farthest corner from the centre of mass (fixed per body)
    double radius = 0.0;
    for (const glm::dvec3& c : s.corners) radius = std::max(radius, glm::length(c));
    return radius;
}
GBody gbodyOf(const SimBody& s, uint32_t voxOff, uint32_t corOff, double radius) {
    GBody g;
    std::memset(&g, 0, sizeof(g));
    set4(g.pos, s.pos.x, s.pos.y, s.pos.z, s.invMass);
    set4(g.quat, s.quat.x, s.quat.y, s.quat.z, s.quat.w);
    set4(g.vel, s.vel.x, s.vel.y, s.vel.z, 0);
    set4(g.angVel, s.angVel.x, s.angVel.y, s.angVel.z, 0);
    set4(g.initial, s.pos.x, s.pos.y, s.pos.z, 0);
    set4(g.initialQuat, s.quat.x, s.quat.y, s.quat.z, s.quat.w);
    set4(g.inertial, s.pos.x, s.pos.y, s.pos.z, 0);
    set4(g.inertialQuat, s.quat.x, s.quat.y, s.quat.z, s.quat.w);
    const glm::dvec3 comLocal = s.src.com - glm::dvec3(s.src.origin);
    set4(g.comLocal, comLocal.x, comLocal.y, comLocal.z, radius);
    set4(g.fixedProps, s.src.mass, s.friction, s.src.grid.voxelM, s.asleepTicks);   // w: F5.5b asleepTicks
    const glm::dmat3& I = s.inertiaBody;   // glm: I[column][row]; symmetric
    set4(g.I0, I[0][0], I[1][0], I[2][0], 0);
    set4(g.I1, I[0][1], I[1][1], I[2][1], 0);
    set4(g.I2, I[0][2], I[1][2], I[2][2], 0);
    g.grid[0] = voxOff;
    g.grid[1] = uint32_t(s.src.grid.dim.x);
    g.grid[2] = uint32_t(s.src.grid.dim.y);
    g.grid[3] = uint32_t(s.src.grid.dim.z);
    g.meta[0] = corOff;
    g.meta[1] = uint32_t(s.corners.size());
    g.meta[2] = s.asleep ? 1u : 0u;
    g.meta[3] = uint32_t(s.slowTicks);
    return g;
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
    m_pipePrimal = std::make_unique<Vulkan::ComputePipeline>();
    if (!m_pipePrimal->create(device, shaderDir + "/voxfound_bodies_primal.comp.spv", 13, sizeof(BodyPC))) {
        if (err) *err = "BodySimGpu: cannot create the pipeline from " + shaderDir +
                        "/voxfound_bodies_primal.comp.spv (run build_shaders.bat?)";
        shutdown();
        return false;
    }
    if (m_queries) {
        VkQueryPoolCreateInfo q{};
        q.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        q.queryType = VK_QUERY_TYPE_TIMESTAMP;
        q.queryCount = kProfMaxStamps;
        if (vkCreateQueryPool(device, &q, nullptr, &m_profQueries) != VK_SUCCESS) m_profQueries = VK_NULL_HANDLE;
    }
    return true;
}

const char* BodySimGpu::profileName(int g) {
    static const char* names[kProfGroups] = {"integrate", "aabb",      "world",      "pairs",  "prefix+scatter",
                                             "sort",      "colour",    "primal",     "dual",   "hard_body",
                                             "hard_apply", "finish",   "warm"};
    return g >= 0 && g < kProfGroups ? names[g] : "?";
}

void BodySimGpu::shutdown() {
    if (m_device == VK_NULL_HANDLE) return;
    finishPending();
    vkDeviceWaitIdle(m_device);
    for (Buffer* b : {&m_params, &m_bodies, &m_bodyVox, &m_corners, &m_world, &m_cons, &m_scratch, &m_state, &m_lists,
                      &m_warm, &m_push, &m_stage, &m_counters, &m_aux, &m_hres})
        destroyBuffer(*b);
    if (m_pipe) m_pipe->cleanup();
    m_pipe.reset();
    if (m_pipeFused) m_pipeFused->cleanup();
    m_pipeFused.reset();
    if (m_pipePrimal) m_pipePrimal->cleanup();
    m_pipePrimal.reset();
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
    bi.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
               VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;   // F5.5a4: the per-colour dispatches live in aux
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
// indirect: also make the writes visible to indirect dispatches (F5.5a4: only after the pass that writes the per-colour
// dispatch arguments -- on every barrier it cost the serial path's all-asleep tick 0.11 -> 0.25 ms)
void barrier(VkCommandBuffer cmd, bool indirect = false) {
    VkMemoryBarrier mb{};
    mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT |
                       VK_ACCESS_TRANSFER_WRITE_BIT | (indirect ? VK_ACCESS_INDIRECT_COMMAND_READ_BIT : 0u);
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT |
                             (indirect ? VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT : 0u),
                         0, 1, &mb, 0, nullptr, 0, nullptr);
}
}  // namespace

// F5.5c: after a bake -- the survivors keep their voxel and corner data where it is (offsets carried); their records,
// the changed world cells and the params are written; state, warm start and push are cleared and the tick counter
// reset, exactly as load() leaves them. No device idle, no buffer re-made, no world upload.
bool BodySimGpu::edit(const std::vector<std::pair<uint32_t, uint8_t>>& cells, const std::vector<size_t>& removed,
                      const std::vector<SimBody>& survivors, std::string* err) {
    const auto t0 = std::chrono::steady_clock::now();
    if (!initialized() || m_nb == 0 || m_prm.empty()) {
        if (err) *err = "BodySimGpu::edit: nothing loaded";
        return false;
    }
    if (survivors.empty()) {
        if (err) *err = "BodySimGpu::edit: no bodies left (nothing to move)";
        return false;
    }
    if (removed.size() + survivors.size() != m_nb) {
        if (err)
            *err = "BodySimGpu::edit: " + std::to_string(removed.size()) + " removed + " +
                   std::to_string(survivors.size()) + " survivors != " + std::to_string(m_nb) + " loaded";
        return false;
    }
    for (size_t k = 0; k < removed.size(); ++k)
        if (removed[k] >= m_nb || (k && removed[k] <= removed[k - 1])) {
            if (err) *err = "BodySimGpu::edit: removed must be ascending indices below the body count";
            return false;
        }
    const uint64_t worldCells = uint64_t(m_prm[P_WX]) * m_prm[P_WY] * m_prm[P_WZ];
    for (const auto& c : cells)
        if (c.first >= worldCells) {
            if (err) *err = "BodySimGpu::edit: a cell outside the world";
            return false;
        }
    finishPending();
    std::vector<uint32_t> vo, co;
    std::vector<double> ra;
    vo.reserve(survivors.size());
    co.reserve(survivors.size());
    ra.reserve(survivors.size());
    for (size_t i = 0, k = 0; i < m_nb; ++i) {
        if (k < removed.size() && removed[k] == i) {
            ++k;
            continue;
        }
        vo.push_back(m_voxOff[i]);
        co.push_back(m_corOff[i]);
        ra.push_back(m_radius[i]);
    }
    m_voxOff.swap(vo);
    m_corOff.swap(co);
    m_radius.swap(ra);
    // the sizes load() derives from the body count (they only shrink: the buffers made for the larger count hold them)
    m_nb = uint32_t(survivors.size());
    m_maxCons = std::max<uint32_t>(4096, m_nb * m_prm[P_CAP] * 16);
    m_maxPairs = 4096;
    while (m_maxPairs < m_nb * 32u) m_maxPairs <<= 1;
    m_prm[P_NB] = m_nb;
    m_prm[P_MAXCONS] = m_maxCons;
    m_prm[P_MAXPAIRS] = m_maxPairs;
    m_parity = 0;
    m_tick = 0;
    m_lastAwake = m_nb;
    std::vector<GBody> gb(m_nb);
    for (uint32_t i = 0; i < m_nb; ++i) gb[i] = gbodyOf(survivors[i], m_voxOff[i], m_corOff[i], m_radius[i]);
    const VkDeviceSize bP = m_prm.size() * 4, bB = gb.size() * sizeof(GBody), bC = cells.size() * 4;
    if (m_editOff + bP + bB + bC > m_stage.bytes) {
        if (err) *err = "BodySimGpu::edit: stage too small for this edit (reload instead)";
        return false;
    }
    auto* st = static_cast<uint8_t*>(m_stage.mapped) + m_editOff;
    std::memcpy(st, m_prm.data(), size_t(bP));
    std::memcpy(st + bP, gb.data(), size_t(bB));
    // the cells in index order, consecutive indices merged into one copy region (run 1: one 4-byte region per cell cost
    // 2.5 ms for 28,372 cells); a repeated index keeps the last value listed
    std::vector<std::pair<uint32_t, uint8_t>> sorted(cells);
    std::stable_sort(sorted.begin(), sorted.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
    std::vector<VkBufferCopy> regions;
    size_t w = 0;
    for (size_t k = 0; k < sorted.size(); ++k) {
        if (k + 1 < sorted.size() && sorted[k + 1].first == sorted[k].first) continue;   // the last of a repeat
        const uint32_t v = sorted[k].second;
        std::memcpy(st + bP + bB + w * 4, &v, 4);
        if (!regions.empty() && regions.back().dstOffset + regions.back().size == VkDeviceSize(sorted[k].first) * 4)
            regions.back().size += 4;
        else
            regions.push_back({m_editOff + bP + bB + w * 4, VkDeviceSize(sorted[k].first) * 4, 4});
        ++w;
    }
    m_lastEditPrepMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    m_editParams = {VkBufferCopy{m_editOff, 0, bP}};
    m_editBodies = {VkBufferCopy{m_editOff + bP, 0, bB}};
    m_editWorld.swap(regions);
    m_editPending = true;
    m_lastEditMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return true;
}

// F5.5c: a staged edit, recorded at the start of the next command buffer (then cleared; edit() waits for the step in
// flight before it writes the stage again)
void BodySimGpu::recordEdit(VkCommandBuffer cmd) {
    if (!m_editPending) return;
    vkCmdCopyBuffer(cmd, m_stage.buf, m_params.buf, uint32_t(m_editParams.size()), m_editParams.data());
    vkCmdCopyBuffer(cmd, m_stage.buf, m_bodies.buf, uint32_t(m_editBodies.size()), m_editBodies.data());
    if (!m_editWorld.empty())
        vkCmdCopyBuffer(cmd, m_stage.buf, m_world.buf, uint32_t(m_editWorld.size()), m_editWorld.data());
    vkCmdFillBuffer(cmd, m_state.buf, 0, VK_WHOLE_SIZE, 0);
    vkCmdFillBuffer(cmd, m_warm.buf, 0, VK_WHOLE_SIZE, 0);
    vkCmdFillBuffer(cmd, m_push.buf, 0, VK_WHOLE_SIZE, 0);
    barrier(cmd);
    m_editPending = false;
}

bool BodySimGpu::load(const VoxelGrid& world, const std::vector<FractureMaterial>& mats,
                      const std::vector<SimBody>& bodies, const BodySimParams& p, std::string* err) {
    if (!initialized()) { if (err) *err = "BodySimGpu: not initialised"; return false; }
    if (p.contactCap > 16) { if (err) *err = "BodySimGpu: contactCap > 16"; return false; }
    if (bodies.empty()) { if (err) *err = "BodySimGpu: no bodies"; return false; }
    finishPending();
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
    m_voxOff.assign(m_nb, 0);
    m_corOff.assign(m_nb, 0);
    m_radius.clear();
    for (uint32_t i = 0; i < m_nb; ++i) {   // F5.5c: the record is gbodyOf (shared with edit)
        const SimBody& s = bodies[i];
        m_voxOff[i] = uint32_t(vox.size());
        m_corOff[i] = uint32_t(cor.size() / 4);
        m_radius.push_back(bodyRadiusOf(s));
        gb[i] = gbodyOf(s, m_voxOff[i], m_corOff[i], m_radius.back());
        for (uint8_t m : s.src.grid.mat) vox.push_back(m);
        for (size_t k = 0; k < s.corners.size(); ++k) {
            cor.push_back(float(s.corners[k].x));
            cor.push_back(float(s.corners[k].y));
            cor.push_back(float(s.corners[k].z));
            cor.push_back(float(s.cornerDir[k]));
        }
    }
    m_prm = prm;
    m_editPending = false;
    m_editOff = (VkDeviceSize(m_nb) * sizeof(GBody) + 255) & ~VkDeviceSize(255);
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
        !createBuffer(m_aux, (2ull * m_nb + 8ull * m_maxPairs + 192ull + m_nb) * 4, false, err) ||
        !createBuffer(m_hres, VkDeviceSize(m_maxPairs) * 16, false, err))
        return false;
    Buffer* bind[13] = {&m_params, &m_bodies, &m_bodyVox, &m_corners, &m_world, &m_cons, &m_scratch,
                        &m_state,  &m_lists,  &m_warm,    &m_push,    &m_aux,   &m_hres};
    for (uint32_t k = 0; k < 13; ++k) {
        m_pipe->bindBuffer(k, bind[k]->buf, bind[k]->bytes);
        m_pipeFused->bindBuffer(k, bind[k]->buf, bind[k]->bytes);
        m_pipePrimal->bindBuffer(k, bind[k]->buf, bind[k]->bytes);
    }
    m_pipe->updateDescriptors();
    m_pipeFused->updateDescriptors();
    m_pipePrimal->updateDescriptors();

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
    enum { G_INT, G_AABB, G_WORLD, G_PAIRS, G_PREFIX, G_SORT, G_COLOUR, G_PRIMAL, G_DUAL, G_HARDB, G_HARDA, G_FIN, G_WARM };
    auto stamp = [&](int g) {   // closes the time since the previous stamp into group g
        if (!prof || m_profStamp >= uint32_t(kProfMaxStamps)) return;
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_profQueries, m_profStamp++);
        m_profLabels.push_back(g);
    };
    if (prof) {
        vkCmdResetQueryPool(cmd, m_profQueries, 0, kProfMaxStamps);
        m_profStamp = 0;
        m_profLabels.clear();
    }
    vkCmdFillBuffer(cmd, m_state.buf, S_AWAKE * 4, 4, 0);
    vkCmdFillBuffer(cmd, m_state.buf, S_NCONS * 4, 4, 0);
    if (m_narrowPar || m_worldPar) vkCmdFillBuffer(cmd, m_state.buf, S_NPAIRS * 4, 3 * 4, 0);   // + S_NAWAKE
    if (m_narrowPar || m_primalPar) {   // F5.5a2 / a4: the hard-pair hash, the per-body flags, the colour lists
        const VkDeviceSize hashOff = (VkDeviceSize(m_nb) + 4ull * m_maxPairs) * 4;
        vkCmdFillBuffer(cmd, m_aux.buf, hashOff, m_aux.bytes - hashOff, 0);
    }
    barrier(cmd);
    stamp(-1);
    m_pipe->bind(cmd);
    run(K_INTEGRATE, 0, m_nb);
    stamp(G_INT);
    run(K_AABB, 0, m_nb);
    stamp(G_AABB);
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
    stamp(G_WORLD);
    if (m_narrowPar) {   // F5.5a2
        run(K_BROAD, 0, m_nb);
        perPair(K_NARROW);
    } else {
        run(K_PAIRS, 0, m_nb);
    }
    stamp(G_PAIRS);
    run(K_PREFIX, 0, 1);
    run(K_SCATTER, 0, m_maxCons);
    stamp(G_PREFIX);
    run(K_SORT, 0, m_nb);
    stamp(G_SORT);
    auto one = [&](uint32_t pass) {   // a fused-pipeline pass: one workgroup of 1024
        m_pipeFused->bind(cmd);
        BodyPC pc{pass, 0, parity, kFusedGroup};
        m_pipeFused->pushConstants(cmd, &pc, sizeof(pc));
        m_pipeFused->dispatch(cmd, 1);
        barrier(cmd);
        m_pipe->bind(cmd);
    };
    if (m_primalPar) {   // F5.5a4: one-dispatch colouring, per-colour lists, a workgroup per body per colour
        one(K_FUSED_COLOUR);
        run(K_COLOUR_COUNT, 0, m_nb);
        {   // the per-colour dispatch arguments: the one barrier that feeds indirect dispatches
            BodyPC pc{K_COLOUR_PREFIX, 0, parity, 1};
            m_pipe->pushConstants(cmd, &pc, sizeof(pc));
            m_pipe->dispatch(cmd, 1);
            barrier(cmd, true);
        }
        run(K_COLOUR_SCATTER, 0, m_nb);
        stamp(G_COLOUR);
        const VkDeviceSize args = (2ull * m_nb + 8ull * m_maxPairs + 96ull) * 4;
        for (uint32_t it = 0; it < m_iterations; ++it) {
            m_pipePrimal->bind(cmd);
            if (m_fused) {   // few awake: every colour in one workgroup (the same numbers)
                BodyPC pc{K_PRIMAL_ONE, 0, parity, 0};
                m_pipePrimal->pushConstants(cmd, &pc, sizeof(pc));
                m_pipePrimal->dispatch(cmd, 1);
                barrier(cmd);
            } else {
                for (uint32_t c = 0; c < kMaxColours; ++c) {
                    BodyPC pc{K_PRIMAL_SUB, c, parity, 0};
                    m_pipePrimal->pushConstants(cmd, &pc, sizeof(pc));
                    vkCmdDispatchIndirect(cmd, m_aux.buf, args + 12ull * c);
                    barrier(cmd);
                }
            }
            m_pipe->bind(cmd);
            stamp(G_PRIMAL);
            run(K_DUAL, 0, m_maxCons);
            stamp(G_DUAL);
        }
    } else if (m_fused) {   // F5.5a: the colour rounds and every iteration, one workgroup
        one(K_FUSED_COLOUR);
        stamp(G_COLOUR);
        for (uint32_t it = 0; it < m_iterations; ++it) {
            one(K_FUSED_PRIMAL);
            stamp(G_PRIMAL);
            run(K_DUAL, 0, m_maxCons);
            stamp(G_DUAL);
        }
    } else {
        run(K_COLOUR_INIT, 0, m_nb);
        for (uint32_t r = 0; r < kMaxColours; ++r) run(K_COLOUR_ROUND, r, m_nb);
        run(K_COLOUR_FINAL, 0, m_nb);
        stamp(G_COLOUR);
        for (uint32_t it = 0; it < m_iterations; ++it) {
            for (uint32_t c = 0; c < kMaxColours; ++c) run(K_PRIMAL, c, m_nb);
            stamp(G_PRIMAL);
            run(K_DUAL, 0, m_maxCons);
            stamp(G_DUAL);
        }
    }
    run(K_AABB, 0, m_nb);
    if (m_narrowPar) {   // F5.5a2: hard pairs at the post-solve poses, one workgroup each, then the per-body sums
        run(K_BROAD, 1, m_nb);
        perPair(K_HARD_NARROW);
        run(K_HARD_PAIRS, 0, m_nb);
    } else {
        run(K_HARD_BODY, 0, m_nb);
    }
    stamp(G_HARDB);
    if (m_worldPar) perPair(K_HARD_APPLY_PAR);   // F5.5a3
    else run(K_HARD_APPLY, 0, m_nb);
    stamp(G_HARDA);
    run(K_FINISH, 0, m_nb);
    stamp(G_FIN);
    run(K_WARM_CLEAR, 0, kWarmCap);
    run(K_WARM_SAVE, 0, m_maxCons);
    stamp(G_WARM);
    ++m_tick;
}

bool BodySimGpu::step(int ticks, std::string* err, int ticksPerSubmit) {
    if (!initialized() || m_nb == 0) { if (err) *err = "BodySimGpu: not loaded"; return false; }
    finishPending();
    m_lastGpuMs = 0.0;
    m_fused = m_lastAwake <= m_fusedMaxAwake;
    bool first = true;
    for (int done = 0; done < ticks;) {
        const int n = m_profile ? 1 : std::min(std::max(ticksPerSubmit, 1), ticks - done);
        if (!submit([&](VkCommandBuffer cmd) {
                if (first) {   // the summed counters start at 0 for this call
                    recordEdit(cmd);   // F5.5c: a staged edit first
                    vkCmdFillBuffer(cmd, m_state.buf, 0, 16 * 4, 0);
                    barrier(cmd);
                }
                for (int t = 0; t < n; ++t) recordTick(cmd);
                if (done + n == ticks) recordCounters(cmd);
            }, true, &m_lastGpuMs, err))
            return false;
        if (m_profile && m_profQueries) {
            uint64_t ts[kProfMaxStamps] = {};
            const uint32_t ns = m_profStamp;
            if (ns > 1 && vkGetQueryPoolResults(m_device, m_profQueries, 0, ns, sizeof(ts), ts, sizeof(uint64_t),
                                                VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT) == VK_SUCCESS) {
                double tick[kProfGroups] = {};
                for (uint32_t k = 1; k < ns; ++k)
                    if (m_profLabels[k] >= 0) tick[m_profLabels[k]] += double(ts[k] - ts[k - 1]) * m_timestampMs;
                for (int g = 0; g < kProfGroups; ++g) {
                    m_prof.sumMs[g] += tick[g];
                    m_prof.minMs[g] = m_prof.ticks == 0 ? tick[g] : std::min(m_prof.minMs[g], tick[g]);
                }
                ++m_prof.ticks;
            }
        }
        first = false;
        done += n;
    }
    if (m_lastGpuMs < 0.0) m_lastGpuMs = -1.0;
    unpackCounters();
    return true;
}

void BodySimGpu::unpackCounters() {
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
}

bool BodySimGpu::read(std::vector<SimBody>& bodies, std::string* err) {
    if (!initialized() || m_nb == 0 || bodies.size() != m_nb) {
        if (err) *err = "BodySimGpu: read needs the loaded body count";
        return false;
    }
    finishPending();
    if (!submit([&](VkCommandBuffer cmd) {
            recordEdit(cmd);   // F5.5c: a staged edit first
            recordBodiesToStage(cmd);
        }, false, nullptr, err))
        return false;
    unpackBodies(bodies);
    return true;
}

void BodySimGpu::unpackBodies(std::vector<SimBody>& bodies) const {
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
        s.asleepTicks = int(g.fixedProps[3]);   // F5.5b: an exact float count
    }
}

// F5.5b, the live path -- the same recording as step(ticks, err, ticks) + read(), submitted without waiting.
void BodySimGpu::recordCounters(VkCommandBuffer cmd) {
    VkBufferCopy c{0, 0, 32 * 4};
    vkCmdCopyBuffer(cmd, m_state.buf, m_counters.buf, 1, &c);
    VkMemoryBarrier hb{};
    hb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    hb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    hb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &hb, 0, nullptr, 0,
                         nullptr);
}

void BodySimGpu::recordBodiesToStage(VkCommandBuffer cmd) {
    VkBufferCopy c{0, 0, VkDeviceSize(m_nb) * sizeof(GBody)};
    vkCmdCopyBuffer(cmd, m_bodies.buf, m_stage.buf, 1, &c);
    VkMemoryBarrier hb{};
    hb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    hb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    hb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &hb, 0, nullptr, 0,
                         nullptr);
}

bool BodySimGpu::stepBegin(int ticks, std::string* err) {
    if (!initialized() || m_nb == 0) { if (err) *err = "BodySimGpu: not loaded"; return false; }
    if (m_pendingCmd != VK_NULL_HANDLE) { if (err) *err = "BodySimGpu: a step is already in flight"; return false; }
    if (ticks < 1) { if (err) *err = "BodySimGpu: stepBegin needs ticks >= 1"; return false; }
    if (m_profile) { if (err) *err = "BodySimGpu: the profile needs step() (one tick per submit)"; return false; }
    m_fused = m_lastAwake <= m_fusedMaxAwake;   // as step(): the path from the awake count the last call ended with
    VkCommandBufferAllocateInfo cai{};
    cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cai.commandPool = m_pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(m_device, &cai, &cmd) != VK_SUCCESS) {
        if (err) *err = "BodySimGpu: command buffer allocation failed";
        return false;
    }
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    m_pendingTimed = m_queries != VK_NULL_HANDLE;
    if (m_pendingTimed) {
        vkCmdResetQueryPool(cmd, m_queries, 0, 2);
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, m_queries, 0);
    }
    recordEdit(cmd);   // F5.5c: a staged edit first
    vkCmdFillBuffer(cmd, m_state.buf, 0, 16 * 4, 0);   // the summed counters start at 0 for this call (as step())
    barrier(cmd);
    for (int t = 0; t < ticks; ++t) recordTick(cmd);
    recordCounters(cmd);
    recordBodiesToStage(cmd);
    if (m_pendingTimed) vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_queries, 1);
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    vkResetFences(m_device, 1, &m_fence);
    const VkResult sub = vkQueueSubmit(m_queue, 1, &si, m_fence);
    if (sub != VK_SUCCESS) {
        vkFreeCommandBuffers(m_device, m_pool, 1, &cmd);
        if (err) *err = "BodySimGpu: submit " + std::to_string(int(sub));
        return false;
    }
    m_pendingCmd = cmd;
    m_pendingTicks = ticks;
    return true;
}

int BodySimGpu::stepPoll(std::vector<SimBody>* bodies, bool wait, std::string* err) {
    if (m_pendingCmd == VK_NULL_HANDLE) { if (err) *err = "BodySimGpu: no step in flight"; return -1; }
    const VkResult r =
        wait ? vkWaitForFences(m_device, 1, &m_fence, VK_TRUE, UINT64_MAX) : vkGetFenceStatus(m_device, m_fence);
    if (r == VK_NOT_READY || r == VK_TIMEOUT) return 0;
    vkFreeCommandBuffers(m_device, m_pool, 1, &m_pendingCmd);
    m_pendingCmd = VK_NULL_HANDLE;
    if (r != VK_SUCCESS) {
        if (err) *err = "BodySimGpu: fence " + std::to_string(int(r));
        return -1;
    }
    m_lastGpuMs = -1.0;
    if (m_pendingTimed) {
        uint64_t ts[2] = {0, 0};
        if (vkGetQueryPoolResults(m_device, m_queries, 0, 2, sizeof(ts), ts, sizeof(uint64_t),
                                  VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT) == VK_SUCCESS)
            m_lastGpuMs = double(ts[1] - ts[0]) * m_timestampMs;
    }
    unpackCounters();
    if (bodies) {
        if (bodies->size() != m_nb) {
            if (err) *err = "BodySimGpu: stepPoll needs the loaded body count";
            return -1;
        }
        unpackBodies(*bodies);
    }
    return 1;
}

void BodySimGpu::finishPending() {
    if (m_pendingCmd != VK_NULL_HANDLE) stepPoll(nullptr, true, nullptr);
}

}  // namespace Phyxel::Core::VoxFound
