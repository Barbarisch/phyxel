// WaterCoreGpu — docs/WaterCore.md §15.11, slice 3 (fills, device-local, one submission per call); §26.6 P2a (bricks).
#include "core/water/WaterCoreGpu.h"
#include "core/water/WaterSurfaceMesh.h"   // SurfaceColumn (the surface buffer layout)
#include "solver_shared.h"                  // 31: the pressure windows' budgets (DebrisShared)
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Phyxel { namespace Core { namespace Water {

namespace {
constexpr int32_t kFrozenBit = 0x40000000;   // 26.31: water_core.glsl WC_FROZEN_BIT

const char* kKernelFiles[WaterCoreGpu::KernelCount] = {
    "wc_fill_advect.comp.spv", "wc_vel_advect.comp.spv", "wc_vel_advect.comp.spv", "wc_vel_advect.comp.spv",
    "wc_face_ops.comp.spv", "wc_face_ops.comp.spv", "wc_face_ops.comp.spv",
    "wc_column_ops.comp.spv", "wc_classify.comp.spv", "wc_rbgs.comp.spv",
    "wc_extrap.comp.spv", "wc_extrap.comp.spv", "wc_extrap.comp.spv", "wc_reduce.comp.spv", "wc_sources.comp.spv", "wc_surface.comp.spv",
    "wc_bricks.comp.spv", "wc_flip_sort.comp.spv", "wc_flip_p2g.comp.spv", "wc_flip_g2p.comp.spv", "wc_momentum.comp.spv", "wc_tall.comp.spv", "wc_rbgs_line.comp.spv", "wc_surface_mesh.comp.spv", "wc_body_pressure.comp.spv"};
// every non-FLIP kernel ends with the Bricks buffer (26.6: water_core.glsl's WC_BRICKS_BINDING); FillAdvect 8 -> 9 with it
// (26.5's outflow scratch at 7); Surface = f, occ, surf, u, w, solid (+ bricks); 20 (M1): + solid on FillAdvect, FaceOps,
// ColumnOps, Classify, Surface
const uint32_t kKernelBindings[WaterCoreGpu::KernelCount] = {10, 5, 5, 5, 7, 7, 7, 11, 12, 6, 10, 10, 10, 12, 5, 12, 6, 7, 6, 9, 7, 7, 6, 12, 9};   // 31: + BodyPressure (p, bricks, surfDense, requests, windows, data, slot map, solid, occ)   // 29 S1: Surface + surfDense, solidTopD; + SurfaceMesh   // 26.26: + Momentum (f, occ, u, v, w, bricks, records); 27.22: + Tall (f, fLo, u, v, w, bricks)
constexpr int kOutSlots = 8;   // vec4 slots: 0 cells (ke, maxDeltaF, mass), 1-3 max speed per lattice, 4 residual max, 5 residue sum
constexpr int kB = 8;          // 26.6: cells per brick edge along x and z (26.17: the height is the volume's, Volume::by)
constexpr int kGround = 1;     // 26.18: the GROUND sentinel slot (slot 0 is the absent brick: dry air)
constexpr int kFirstSlot = 2;  // 26.18: the first slot a real brick can take
constexpr int kCounters = 6;   // Bricks[counterOff + k]: 0 flux into absent, 1 sentinel dirty, 2-5 the over-full carry (26.32); = water_core.glsl WC_COUNTERS
int floorMod(int v, int n) { return ((v % n) + n) % n; }
} // namespace

WaterCoreGpu::~WaterCoreGpu() { shutdown(); }

bool WaterCoreGpu::init(VkDevice device, VkPhysicalDevice physical, VkQueue queue, uint32_t queueFamily,
                        const std::string& shaderDir, std::string* err) {
    m_device = device; m_physical = physical; m_queue = queue; m_queueFamily = queueFamily; m_shaderDir = shaderDir;
    VkCommandPoolCreateInfo pci{}; pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pci.queueFamilyIndex = queueFamily; pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    if (vkCreateCommandPool(device, &pci, nullptr, &m_pool) != VK_SUCCESS) { if (err) *err = "vkCreateCommandPool failed"; m_pool = VK_NULL_HANDLE; return false; }
    VkFenceCreateInfo fci{}; fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (vkCreateFence(device, &fci, nullptr, &m_fence) != VK_SUCCESS) { if (err) *err = "vkCreateFence failed"; return false; }
    return true;
}

void WaterCoreGpu::shutdown() {
    if (m_device == VK_NULL_HANDLE) return;
    vkDeviceWaitIdle(m_device);
    for (Volume* v : std::vector<Volume*>(m_volumes)) destroyVolume(v);
    for (auto& spare : m_pipeSpare) { for (auto& p : spare) p->cleanup(); spare.clear(); }   // 39
    for (Buffer& b : m_pwTest) destroyBuffer(b);   // 31 TEST
    if (m_fence) vkDestroyFence(m_device, m_fence, nullptr);
    if (m_pool) vkDestroyCommandPool(m_device, m_pool, nullptr);
    m_fence = VK_NULL_HANDLE; m_pool = VK_NULL_HANDLE; m_device = VK_NULL_HANDLE;
}

uint32_t WaterCoreGpu::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags props) const {
    VkPhysicalDeviceMemoryProperties mp; vkGetPhysicalDeviceMemoryProperties(m_physical, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if ((typeFilter & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props) return i;
    return UINT32_MAX;
}

bool WaterCoreGpu::createBuffer(Buffer& b, VkDeviceSize bytes, bool hostVisible, std::string* err) {
    b.bytes = std::max<VkDeviceSize>(bytes, 16);
    VkBufferCreateInfo bi{}; bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = b.bytes; bi.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;   // 29 S1: + indirect (the mesh's draw args)
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(m_device, &bi, nullptr, &b.buf) != VK_SUCCESS) { if (err) *err = "vkCreateBuffer failed for " + std::to_string(b.bytes) + " bytes"; return false; }
    VkMemoryRequirements mr; vkGetBufferMemoryRequirements(m_device, b.buf, &mr);
    VkMemoryAllocateInfo ai{}; ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = mr.size;
    const VkMemoryPropertyFlags want = hostVisible ? (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    // 26.12: host memory the CPU READS back (staging: surface, births, stats, downloads) is CACHED when the device offers it -
    // reading uncached (write-combined) memory cost 5 ms for the creek's surface list and most of a 257 ms download
    ai.memoryTypeIndex = hostVisible ? findMemoryType(mr.memoryTypeBits, want | VK_MEMORY_PROPERTY_HOST_CACHED_BIT) : UINT32_MAX;
    if (ai.memoryTypeIndex == UINT32_MAX) ai.memoryTypeIndex = findMemoryType(mr.memoryTypeBits, want);
    if (ai.memoryTypeIndex == UINT32_MAX || vkAllocateMemory(m_device, &ai, nullptr, &b.mem) != VK_SUCCESS) {
        // the §15.11 refusal: the byte count is in the message, the failure never reaches a dispatch
        if (err) *err = "cannot allocate " + std::to_string(mr.size) + " bytes of " + (hostVisible ? "host-visible" : "device-local") + " memory for a WaterCore GPU volume";
        vkDestroyBuffer(m_device, b.buf, nullptr); b.buf = VK_NULL_HANDLE; return false;
    }
    vkBindBufferMemory(m_device, b.buf, b.mem, 0);
    if (hostVisible) { vkMapMemory(m_device, b.mem, 0, b.bytes, 0, &b.mapped); std::memset(b.mapped, 0, static_cast<size_t>(b.bytes)); }
    return true;
}

void WaterCoreGpu::destroyBuffer(Buffer& b) {
    if (b.mapped) vkUnmapMemory(m_device, b.mem);
    if (b.buf) vkDestroyBuffer(m_device, b.buf, nullptr);
    if (b.mem) vkFreeMemory(m_device, b.mem, nullptr);
    b = Buffer{};
}

bool WaterCoreGpu::createPipelines(Volume& vol, std::string* err) {
    for (int k = 0; k < KernelCount; ++k) {
        if (!vol.particles && k >= FlipSort && k <= FlipG2p) continue;   // 26.26: skip only the particle kernels (Momentum comes after them)
        if (!m_pipeSpare[k].empty()) {   // 39: a destroyed volume's - bindPipelines below rewrites every binding
            vol.pipes[k] = std::move(m_pipeSpare[k].back()); m_pipeSpare[k].pop_back(); ++m_pipesReused;
            continue;
        }
        ++m_pipesBuilt;
        auto pipe = std::make_unique<Vulkan::ComputePipeline>();
        if (!pipe->create(m_device, m_shaderDir + "/" + kKernelFiles[k], kKernelBindings[k], sizeof(WcPush), k == FlipP2g ? 3u : (k >= ExtrapU && k <= ExtrapW) ? 4u : 1u)) {
            if (err) *err = std::string("failed to create pipeline ") + kKernelFiles[k] + " from " + m_shaderDir; return false;
        }
        vol.pipes[k] = std::move(pipe);
    }
    bindPipelines(vol);
    return true;
}

void WaterCoreGpu::setPressureWindowTargets(VkBuffer wins, VkDeviceSize winsBytes, VkBuffer data, VkDeviceSize dataBytes, VkBuffer of, VkDeviceSize ofBytes) {
    m_pwWins = Buffer{}; m_pwWins.buf = wins; m_pwWins.bytes = winsBytes;
    m_pwData = Buffer{}; m_pwData.buf = data; m_pwData.bytes = dataBytes;
    m_pwOf = Buffer{};   m_pwOf.buf = of;     m_pwOf.bytes = ofBytes;
    for (Volume* v : m_volumes) { settle(*v); bindPipelines(*v); }   // nothing in flight reads the old descriptors
}

bool WaterCoreGpu::makePressureTargetsForTest(std::string* err) {
    const VkDeviceSize b[3] = {static_cast<VkDeviceSize>(DebrisShared::MAX_PRESSURE_WINDOWS) * 48, static_cast<VkDeviceSize>(DebrisShared::PRESSURE_WINDOW_FLOATS) * 4, 65536 * 4};
    for (int i = 0; i < 3; ++i) if (!m_pwTest[i].buf && !createBuffer(m_pwTest[i], b[i], true, err)) return false;
    setPressureWindowTargets(m_pwTest[0].buf, b[0], m_pwTest[1].buf, b[1], m_pwTest[2].buf, b[2]);
    return true;
}

bool WaterCoreGpu::readPressureWindowForTest(uint32_t k, int32_t origin[4], int32_t dims[4], float meta[4], std::vector<float>& map) const {
    if (!m_pwTest[0].mapped || k >= DebrisShared::MAX_PRESSURE_WINDOWS) return false;
    vkDeviceWaitIdle(m_device);
    const char* w = static_cast<const char*>(m_pwTest[0].mapped) + k * 48;
    std::memcpy(origin, w, 16); std::memcpy(dims, w + 16, 16); std::memcpy(meta, w + 32, 16);
    const size_t n = static_cast<size_t>(dims[0]) * dims[1] * dims[2];
    if (n == 0 || static_cast<size_t>(dims[3]) + n > DebrisShared::PRESSURE_WINDOW_FLOATS) return false;
    map.resize(n);
    std::memcpy(map.data(), static_cast<const float*>(m_pwTest[1].mapped) + dims[3], n * 4);
    return true;
}

void WaterCoreGpu::bindPipelines(Volume& vol) {
    auto bindAll = [](Vulkan::ComputePipeline& p, std::initializer_list<const Buffer*> bufs) {
        uint32_t i = 0; for (const Buffer* b : bufs) { p.bindBuffer(i++, b->buf, b->bytes); } p.updateDescriptors();
    };
    const Buffer* br = &vol.bricks;
    bindAll(*vol.pipes[FillAdvect], {&vol.f, &vol.fOrig, &vol.u, &vol.v, &vol.w, &vol.occ, &vol.solid, &vol.res, br, &vol.fLo});   // res: 26.5's scratch (the projection rewrites it later in the substep)
    bindAll(*vol.pipes[VelAdvectU], {&vol.uOld, &vol.vOld, &vol.wOld, &vol.u, br});
    bindAll(*vol.pipes[VelAdvectV], {&vol.uOld, &vol.vOld, &vol.wOld, &vol.v, br});
    bindAll(*vol.pipes[VelAdvectW], {&vol.uOld, &vol.vOld, &vol.wOld, &vol.w, br});
    bindAll(*vol.pipes[FaceOpsU],   {&vol.f, &vol.occ, &vol.u, &vol.p, &vol.liq, &vol.solid, br});
    bindAll(*vol.pipes[FaceOpsV],   {&vol.f, &vol.occ, &vol.v, &vol.p, &vol.liq, &vol.solid, br});
    bindAll(*vol.pipes[FaceOpsW],   {&vol.f, &vol.occ, &vol.w, &vol.p, &vol.liq, &vol.solid, br});
    bindAll(*vol.pipes[ColumnOps],  {&vol.f, &vol.occ, &vol.u, &vol.v, &vol.w, &vol.dropped, &vol.solid, &vol.births, &vol.deposit, br, &vol.fLo});   // 21: + births, deposit; 26.10: + fLo
    bindAll(*vol.pipes[Surface],    {&vol.f, &vol.occ, &vol.surf, &vol.u, &vol.w, &vol.solid, br, &vol.births, &vol.birthsPacked, &vol.lists, &vol.surfDense, &vol.solidTopD});   // Phase F (+ u, w: E2; + solid: 20; 26.11: packed lists; 29 S1: the dense field)
    // 29 S1: bindings 6 / 7 are the test expansion's (expandSurfaceForTest rebinds them); otherwise placeholders
    bindAll(*vol.pipes[SurfaceMesh], {&vol.surfDense, &vol.meshCorners, &vol.meshMeta, &vol.meshList, &vol.meshArgs, br, &vol.meshList, &vol.meshArgs, &vol.solidTopD, &vol.colAgg, &vol.motionRef, &vol.motionOut});
    bindAll(*vol.pipes[Classify],   {&vol.f, &vol.occ, &vol.u, &vol.v, &vol.w, &vol.src, &vol.liq, &vol.diag, &vol.rhs, &vol.p, &vol.solid, br});
    {   // 31: the debris solver's window buffers (placeholders - this volume's own request buffer - until they are set; nothing dispatches then)
        const bool t = m_pwWins.buf != VK_NULL_HANDLE;
        bindAll(*vol.pipes[BodyPressure], {&vol.p, br, &vol.surfDense, &vol.winReq, t ? &m_pwWins : &vol.winReq, t ? &m_pwData : &vol.winReq, t ? &m_pwOf : &vol.winReq, &vol.solid, &vol.occ});
    }
    bindAll(*vol.pipes[Rbgs],       {&vol.liq, &vol.diag, &vol.rhs, &vol.p, &vol.res, br});
    // binding 8 = the lattice before the halo pass: a dedicated scratch, because uOld/vOld/wOld are
    // the FLIP delta base and overwriting them here left the particles with a ~zero delta (they
    // crawled at 0.1 m/s under a 2 m/s grid, 2026-10-08)
    {   // 26.11: four descriptor sets per lattice instead of 33 buffer copies a substep - the layers ping-pong between A and B by set,
        // the masks read the lattice itself, and the finish writes the lattice in place (each thread reads its own orig = lattice
        // entry before writing it; orig and valOut alias one buffer, so the compiler keeps that order). Results are bit-identical.
        const Buffer* lat[3] = {&vol.u, &vol.v, &vol.w};
        for (int L = 0; L < 3; ++L) {
            Vulkan::ComputePipeline& p = *vol.pipes[ExtrapU + L];
            //                      val          known         valOut       knownOut
            const Buffer* sets[4][4] = {{lat[L],      &vol.knownA, &vol.valB,  &vol.knownB},    // 0 masks: lattice -> B
                                        {&vol.valB,   &vol.knownB, &vol.valA,  &vol.knownA},    // 1 a layer B -> A
                                        {&vol.valA,   &vol.knownA, &vol.valB,  &vol.knownB},    // 2 a layer A -> B
                                        {&vol.valA,   &vol.knownA, lat[L],     &vol.knownB}};   // 3 finish: A -> the lattice
            for (uint32_t s = 0; s < 4; ++s) {
                const Buffer* b[10] = {&vol.f, &vol.occ, sets[s][0], sets[s][1], sets[s][2], sets[s][3], &vol.first, &vol.keep, lat[L], br};
                for (uint32_t i = 0; i < 10; ++i) p.bindBufferInSet(s, i, b[i]->buf, b[i]->bytes);
            }
            p.updateDescriptors();
        }
    }
    bindAll(*vol.pipes[Reduce],  {&vol.f, &vol.fPrev, &vol.u, &vol.v, &vol.w, &vol.part, &vol.out, &vol.part2, &vol.res, &vol.dropped, br, vol.unitMap.buf ? &vol.unitMap : &vol.part2});   // 27.23 W1e: + the kept unit map (dense: unused, part2 stands in)
    bindAll(*vol.pipes[Sources], {&vol.f, &vol.occ, &vol.src, &vol.sources, br});
    bindAll(*vol.pipes[Momentum], {&vol.f, &vol.occ, &vol.u, &vol.v, &vol.w, br, &vol.momentum});   // 26.26
    bindAll(*vol.pipes[Bricks],  {&vol.f, &vol.u, &vol.v, &vol.w, &vol.p, br});
    bindAll(*vol.pipes[Tall],    {&vol.f, &vol.fLo, &vol.u, &vol.v, &vol.w, br, &vol.fPrev});   // 27.22 (27.22f: + fPrev)
    bindAll(*vol.pipes[RbgsLine], {&vol.liq, &vol.diag, &vol.rhs, &vol.p, &vol.res, br});   // 28
    if (vol.particles) {
        bindAll(*vol.pipes[FlipSort], {&vol.posA, &vol.velA, &vol.posB, &vol.velB, &vol.pcount, &vol.pstart, &vol.pcursor});
        // p2g gathers from the SORTED list (B); it writes f and ONE lattice per dispatch (rebound per lattice below)
        {   // p2g: three descriptor sets, one per output lattice (binding 5)
            Vulkan::ComputePipeline& p = *vol.pipes[FlipP2g];
            const Buffer* lat[3] = {&vol.u, &vol.v, &vol.w};
            for (uint32_t set = 0; set < 3; ++set) {
                p.bindBufferInSet(set, 0, vol.posB.buf, vol.posB.bytes); p.bindBufferInSet(set, 1, vol.velB.buf, vol.velB.bytes);
                p.bindBufferInSet(set, 2, vol.pstart.buf, vol.pstart.bytes); p.bindBufferInSet(set, 3, vol.pcount.buf, vol.pcount.bytes);
                p.bindBufferInSet(set, 4, vol.f.buf, vol.f.bytes); p.bindBufferInSet(set, 5, lat[set]->buf, lat[set]->bytes);
            }
            p.updateDescriptors();
        }
        bindAll(*vol.pipes[FlipG2p], {&vol.posB, &vol.velB, &vol.u, &vol.v, &vol.w, &vol.uOld, &vol.vOld, &vol.wOld, &vol.occ});
    }
}

WaterCoreGpu::Volume* WaterCoreGpu::createVolume(const WaterGrid& g, std::string* err, const BrickOptions& bricks) {
    Volume* vol = createVolume(g.spec(), err, false, 0, bricks);
    if (!vol) return nullptr;
    upload(*vol, g);
    if (vol->unstoredM3 > 0.0) {   // 26.6: a capacity smaller than the water itself is refused, never a silent drop
        if (err) *err = "the brick capacity (" + std::to_string(vol->capacity - 1) + ") cannot hold this water: "
                        + std::to_string(vol->unstoredM3) + " m^3 would not be stored";
        destroyVolume(vol);
        return nullptr;
    }
    return vol;
}

WaterCoreGpu::Volume* WaterCoreGpu::createVolume(const GridSpec& spec, std::string* err, bool particles, size_t particleCapacity, const BrickOptions& opts) {
    if (!ready()) { if (err) *err = "WaterCoreGpu not initialised"; return nullptr; }
    auto vol = std::make_unique<Volume>();
    vol->spec = spec;
    vol->particles = particles;
    const size_t nx = static_cast<size_t>(spec.dims.x), ny = static_cast<size_t>(spec.dims.y), nz = static_cast<size_t>(spec.dims.z);
    vol->dCells = nx * ny * nz; vol->dNu = (nx + 1) * ny * nz; vol->dNv = nx * (ny + 1) * nz; vol->dNw = nx * ny * (nz + 1);
    vol->dColumns = nx * nz;
    vol->bricked = !particles;   // 26.6: the FLIP kernels index the dense grid; every fill volume is bricked
    if (particles && opts.sparse) { if (err) *err = "a particle (FLIP) volume cannot be sparse (26.6: the particle transport stays dense)"; return nullptr; }
    if (vol->bricked) {
        vol->sparse = opts.sparse; vol->freezeForTest = opts.freezeForTest;
        vol->groundOn = opts.sparse && (opts.ground < 0 ? s_groundBricks : opts.ground != 0); vol->groundAsAir = opts.groundAsAirForTest;
        // 26.17: bricks 8 x by x 8 - a flat brick follows shallow water (and the bed under it) closer than a cube
        const int height = opts.height > 0 ? opts.height : s_brickHeight;
        if (height != 2 && height != 4 && height != 8) { if (err) *err = "brick height " + std::to_string(height) + " is not 2, 4 or 8 (26.17)"; return nullptr; }
        vol->by = height; vol->bly = height == 2 ? 1 : (height == 4 ? 2 : 3); vol->bc = static_cast<size_t>(64 * height);
        vol->o = glm::ivec3(floorMod(spec.origin.x, kB), floorMod(spec.origin.y, vol->by), floorMod(spec.origin.z, kB));
        // the brick box covers every cell: q = local + o in [0, n + o). A box's upper wall faces (lattice index n) lie in the
        // next brick when n + o is a multiple of 8 - absent, so they read 0, which a wall face always is (the dense grid
        // forced them to 0 every substep). An extra layer of bricks to hold them stepped 27 bricks for a 16^3 box (13,824
        // cells for 4,096) - measured 2026-10-13.
        vol->bn = (spec.dims + vol->o + vol->bs() - glm::ivec3(1)) / vol->bs();
        vol->nuy = static_cast<size_t>((spec.dims.y + (vol->o.y & 1) + 1) / 2);   // the reductions' unit map (x 8, y 2, z 8)
        vol->mapCount = static_cast<size_t>(vol->bn.x) * vol->bn.y * vol->bn.z;
        if (vol->mapCount > kMaxMapEntries || vol->bn.x > 1023 || vol->bn.y > 1023 || vol->bn.z > 1023) {
            // the 15.11 refusal shape: the reason AND the byte count (the brick map alone, and every brick of the box stored)
            const unsigned long long mapBytes = static_cast<unsigned long long>(vol->mapCount) * 4ull;
            const unsigned long long fullBytes = static_cast<unsigned long long>(vol->mapCount) * vol->bc * 100ull;   // ~25 floats a cell
            if (err) *err = "a bricked WaterCore volume's brick box holds at most " + std::to_string(kMaxMapEntries) + " bricks and 1023 a side ("
                            + std::to_string(vol->bn.x) + " x " + std::to_string(vol->bn.y) + " x " + std::to_string(vol->bn.z) + " requested: a "
                            + std::to_string(mapBytes) + " bytes map, " + std::to_string(fullBytes) + " bytes with every brick stored; world scale is 26.3 P5)";
            return nullptr;
        }
        // slot 0 = the absent brick, slot 1 = ground (26.18). 27.23 W1d.1: a sparse volume with no fixed capacity starts with kInitialSlots
        // and grows with its water (it reserved a slot for every brick of its box: ~123 B of GPU memory per cell of the box, measured)
        vol->growable = opts.sparse && opts.capacity == 0;
        vol->capacity = kFirstSlot + (opts.capacity > 0 ? std::min(opts.capacity, vol->mapCount) : (vol->growable ? std::min(kInitialSlots, vol->mapCount) : vol->mapCount));
        vol->colCap = static_cast<size_t>(vol->bn.x) * vol->bn.z;
        vol->cells = vol->capacity * vol->bc; vol->nu = vol->nv = vol->nw = vol->cells;
        vol->columns = vol->colCap * 64;
        vol->slotTableOff = vol->mapCount; vol->colTableOff = vol->slotTableOff + vol->capacity;
        vol->counterOff = vol->colTableOff + vol->colCap; vol->infoOff = vol->counterOff + kCounters;
        vol->brickInts = vol->infoOff + 2 * vol->capacity;   // two water records per slot (any water, liquid)
        vol->tallOff = vol->brickInts; vol->brickInts += vol->dColumns;   // 27.22: one tall height per dense column (0 / 1 = an ordinary cell)
        vol->map.assign(vol->mapCount, 0); vol->slotBrick.assign(vol->capacity, -1); vol->colEntries.clear();
    } else {
        vol->cells = vol->dCells; vol->nu = vol->dNu; vol->nv = vol->dNv; vol->nw = vol->dNw; vol->columns = vol->dColumns;
        vol->brickInts = 4;
    }
    vol->latticeMax = std::max({vol->nu, vol->nv, vol->nw});
    // partials: two per 256-wide group (26.17: mode 0 folds per 128-cell unit), and the bricked unit map they are gathered into
    vol->groups = std::max(2 * ((std::max({vol->cells, vol->latticeMax, vol->columns}) + 255) / 256),
                           static_cast<size_t>(vol->bn.x) * vol->nuy * static_cast<size_t>(vol->bn.z)) + 1;
    using CClock = std::chrono::steady_clock;   // 39: the parts' times (m_lastCreateSplit)
    auto cms = [](CClock::time_point a, CClock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    const auto ct0 = CClock::now();
    const VkDeviceSize F4 = sizeof(float);
    bool ok = createBuffer(vol->f, vol->cells * F4, false, err) && createBuffer(vol->fLo, vol->cells * F4, false, err) && createBuffer(vol->fOrig, vol->cells * F4, false, err) && createBuffer(vol->fPrev, vol->cells * F4, false, err)
           && createBuffer(vol->u, vol->nu * F4, false, err) && createBuffer(vol->v, vol->nv * F4, false, err) && createBuffer(vol->w, vol->nw * F4, false, err)
           && createBuffer(vol->uOld, vol->nu * F4, false, err) && createBuffer(vol->vOld, vol->nv * F4, false, err) && createBuffer(vol->wOld, vol->nw * F4, false, err)
           && createBuffer(vol->occ, vol->cells * 4, false, err) && createBuffer(vol->src, vol->cells * F4, false, err) && createBuffer(vol->liq, vol->cells * 4, false, err)
           && createBuffer(vol->diag, vol->cells * F4, false, err) && createBuffer(vol->rhs, vol->cells * F4, false, err) && createBuffer(vol->p, vol->cells * F4, false, err) && createBuffer(vol->res, vol->cells * F4, false, err)
           && createBuffer(vol->valA, vol->latticeMax * F4, false, err) && createBuffer(vol->valB, vol->latticeMax * F4, false, err)
           && createBuffer(vol->knownA, vol->latticeMax * 4, false, err) && createBuffer(vol->knownB, vol->latticeMax * 4, false, err)
           && createBuffer(vol->first, vol->latticeMax * 4, false, err) && createBuffer(vol->keep, vol->latticeMax * 4, false, err)
           && createBuffer(vol->dropped, vol->columns * F4, false, err) && createBuffer(vol->part, vol->groups * 16, false, err) && createBuffer(vol->part2, vol->groups * 16, false, err)
           && createBuffer(vol->out, kOutSlots * 16, false, err) && createBuffer(vol->sources, kMaxSources * sizeof(GpuSource), false, err)
           && createBuffer(vol->surf, vol->columns * (32 + sizeof(Core::Water::SurfaceColumn)), false, err)    // Phase F: the surface field (G2: 64 B)
           && createBuffer(vol->solid, vol->cells * 16, false, err)                                     // 20: (s, q, wake, ledger) per cell
           && createBuffer(vol->births, vol->columns * 32, false, err) && createBuffer(vol->birthsPacked, vol->columns * 32, false, err) && createBuffer(vol->lists, 16, false, err) && createBuffer(vol->deposit, vol->cells * F4, false, err)   // 21: droplets
           && createBuffer(vol->bricks, vol->brickInts * 4, false, err)                                 // 26.6
           && createBuffer(vol->momentum, kMaxMomentum * 32, true, err)                                  // 26.26: written by the host each call
           && (!vol->bricked || createBuffer(vol->unitMap, std::max<size_t>(1, static_cast<size_t>(vol->bn.x) * vol->nuy * static_cast<size_t>(vol->bn.z)) * 16, false, err))   // 27.23 W1e
           // 29 S1: the surface on the device - the dense field and its solid tops (wc_surface writes every volume's), the mesh (bricked)
           && createBuffer(vol->surfDense, vol->dColumns * sizeof(Core::Water::SurfaceColumn), false, err) && createBuffer(vol->solidTopD, vol->dColumns * 4, false, err)
           && createBuffer(vol->meshCorners, (vol->bricked ? vol->dColumns : 1) * 64, false, err) && createBuffer(vol->meshMeta, (vol->bricked ? vol->dColumns : 1) * 4, false, err)
           && createBuffer(vol->meshList, (vol->bricked ? vol->dColumns * 4 : 1) * 4, false, err) && createBuffer(vol->meshArgs, 32, false, err)
           && createBuffer(vol->meshArgsHost, 32, true, err)
           && createBuffer(vol->colAgg, (vol->bricked ? static_cast<size_t>(vol->bn.x) * vol->bn.z : 1) * 8, false, err)   // 29 S2
           && createBuffer(vol->motionRef, (vol->bricked ? vol->dColumns : 1) * 4, false, err) && createBuffer(vol->motionOut, 64, false, err)
           && createBuffer(vol->motionOutHost, 64, true, err)
           && createBuffer(vol->winReq, static_cast<VkDeviceSize>(DebrisShared::MAX_PRESSURE_WINDOWS) * sizeof(PressureWindowReq), true, err)   // 31
           && (!vol->bricked || createBuffer(vol->surfUp, vol->dColumns * (sizeof(Core::Water::SurfaceColumn) + 4), true, err));
    vol->meshListCap = vol->bricked ? static_cast<uint32_t>(vol->dColumns * 4) : 0u;
    { static uint64_t s_serial = 0; vol->serial = ++s_serial; }
    // staging: [f | u | v | w | occ | src | out | p | sources | surf | solid | births | deposit | bricks | particles]
    vol->offU = vol->cells * F4; vol->offV = vol->offU + vol->nu * F4; vol->offW = vol->offV + vol->nv * F4;
    vol->offOcc = vol->offW + vol->nw * F4; vol->offSrc = vol->offOcc + vol->cells * 4; vol->offOut = vol->offSrc + vol->cells * F4;
    vol->offP = vol->offOut + kOutSlots * 16; vol->offSources = vol->offP + vol->cells * F4;
    vol->offSurf = vol->offSources + kMaxSources * sizeof(GpuSource);
    vol->offSolid = vol->offSurf + 64 + vol->columns * (32 + sizeof(Core::Water::SurfaceColumn));   // 26.11: [counts | pad | one-run | multi-run]
    vol->offBirths = vol->offSolid + vol->cells * 16;
    vol->offDeposit = vol->offBirths + vol->columns * 32;
    vol->offBricks = vol->offDeposit + vol->cells * F4;
    vol->offParticles = vol->offBricks + vol->brickInts * 4;
    VkDeviceSize particleBytes = 0;
    if (particles) {
        vol->particleCapacity = particleCapacity > 0 ? particleCapacity : std::min<size_t>(FlipTransport::kMaxParticlesPerVolume, vol->cells * FlipTransport::kParticlesPerCell + 4096);
        particleBytes = vol->particleCapacity * 32;
        ok = ok && createBuffer(vol->posA, vol->particleCapacity * 16, false, err) && createBuffer(vol->velA, vol->particleCapacity * 16, false, err)
                && createBuffer(vol->posB, vol->particleCapacity * 16, false, err) && createBuffer(vol->velB, vol->particleCapacity * 16, false, err)
                && createBuffer(vol->pcount, vol->cells * 4, false, err) && createBuffer(vol->pstart, vol->cells * 4, false, err) && createBuffer(vol->pcursor, vol->cells * 4, false, err);
    }
    ok = ok && createBuffer(vol->staging, vol->offParticles + particleBytes, true, err);
    const auto ct1 = CClock::now();
    const long reused0 = m_pipesReused;
    if (!ok || !createPipelines(*vol, err)) { Volume* raw = vol.release(); destroyVolume(raw); return nullptr; }
    const auto ct2 = CClock::now();
    {   // every buffer starts all zero: no body, every rule exactly as before; 26.6: the absent brick (slot 0) reads as dry
        // air in EVERY buffer, scratch included (the extrapolation reads its neighbours' known flags there)
        VkCommandBuffer cmd = beginCommands();
        for (Buffer* b : {&vol->f, &vol->fOrig, &vol->fPrev, &vol->u, &vol->v, &vol->w, &vol->uOld, &vol->vOld, &vol->wOld, &vol->occ, &vol->src, &vol->liq,
                          &vol->diag, &vol->rhs, &vol->p, &vol->res, &vol->valA, &vol->valB, &vol->knownA, &vol->knownB, &vol->first, &vol->keep,
                          &vol->dropped, &vol->surf, &vol->solid, &vol->births, &vol->deposit, &vol->bricks, &vol->fLo, &vol->birthsPacked, &vol->lists, &vol->unitMap,
                          &vol->surfDense, &vol->solidTopD, &vol->meshCorners, &vol->meshMeta, &vol->meshList, &vol->meshArgs, &vol->colAgg})
            if (b->buf) vkCmdFillBuffer(cmd, b->buf, 0, b->bytes, 0);
        {   // 29 S1: the extras draw - vertexCount 6, instanceCount (counted each call), firstVertex 0, firstInstance 1 (the vertex shader's
            // "this is the list" mark)
            const uint32_t args[4] = {6u, 0u, 0u, 1u};
            VkMemoryBarrier mb{}; mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER; mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; mb.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
            vkCmdUpdateBuffer(cmd, vol->meshArgs.buf, 0, sizeof(args), args);
            if (vol->motionRef.buf) vkCmdFillBuffer(cmd, vol->motionRef.buf, 0, vol->motionRef.bytes, 0x7FC00000u);   // 29 S2: no reference (NaN)
        }
        if (vol->bricked && !vol->groundAsAir) vkCmdFillBuffer(cmd, vol->occ.buf, kGround * vol->bc * 4, vol->bc * 4, 1u);   // 26.18: the ground sentinel: Solid
        submitAndWait(cmd);
    }
    const auto ct3 = CClock::now();
    if (vol->bricked && !vol->sparse) {   // dense: every brick of the box is kept from the start
        std::vector<char> all(vol->mapCount, 1);
        applyResidency(*vol, all);
        writeTablesToStaging(*vol);
        VkCommandBuffer cmd = beginCommands();
        copy(cmd, vol->staging, vol->bricks, vol->counterOff * 4, vol->offBricks);
        submitAndWait(cmd);
        vol->lastAllocated = 0;
    }
    {   int pipes = 0; for (const auto& pp : vol->pipes) if (pp) ++pipes;
        m_lastCreateSplit = {cms(ct0, ct1), cms(ct1, ct2), cms(ct2, ct3), cms(ct3, CClock::now()), pipes, static_cast<int>(m_pipesReused - reused0)}; }
    Volume* raw = vol.release();
    m_volumes.push_back(raw);
    return raw;
}

void WaterCoreGpu::destroyVolume(Volume* vol) {
    if (!vol) return;
    settle(*vol);   // 26.16: its call in flight collected (and its command buffer freed)
    vkDeviceWaitIdle(m_device);
    for (size_t k = 0; k < vol->pipes.size(); ++k) {   // 39: kept for the next volume (the device is idle: no set is in use)
        auto& p = vol->pipes[k];
        if (!p) continue;
        if (m_pipeSpare[k].size() < kPipeSpareSets) m_pipeSpare[k].push_back(std::move(p));
        else { p->cleanup(); p.reset(); }
    }
    if (vol->tsPool) vkDestroyQueryPool(m_device, vol->tsPool, nullptr);
    if (vol->callTs) vkDestroyQueryPool(m_device, vol->callTs, nullptr);
    if (vol->callFence) vkDestroyFence(m_device, vol->callFence, nullptr);
    for (Buffer* b : {&vol->f, &vol->fOrig, &vol->fPrev, &vol->u, &vol->v, &vol->w, &vol->uOld, &vol->vOld, &vol->wOld, &vol->occ, &vol->src, &vol->liq,
                      &vol->diag, &vol->rhs, &vol->p, &vol->res, &vol->valA, &vol->valB, &vol->knownA, &vol->knownB, &vol->first, &vol->keep, &vol->dropped, &vol->part, &vol->part2, &vol->out, &vol->sources, &vol->surf, &vol->solid, &vol->births, &vol->deposit, &vol->bricks, &vol->posA, &vol->velA, &vol->posB, &vol->velB, &vol->pcount, &vol->pstart, &vol->pcursor, &vol->stageCap, &vol->unitCap, &vol->scratch, &vol->fLo, &vol->birthsPacked, &vol->lists, &vol->staging, &vol->momentum, &vol->unitMap,
                      &vol->surfDense, &vol->solidTopD, &vol->meshCorners, &vol->meshMeta, &vol->meshList, &vol->meshArgs, &vol->meshArgsHost, &vol->surfUp, &vol->colAgg,
                      &vol->motionRef, &vol->motionOut, &vol->motionOutHost, &vol->winReq})
        destroyBuffer(*b);
    m_volumes.erase(std::remove(m_volumes.begin(), m_volumes.end(), vol), m_volumes.end());
    delete vol;
}

VkCommandBuffer WaterCoreGpu::beginCommands() const {
    VkCommandBufferAllocateInfo cai{}; cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cai.commandPool = m_pool; cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cai.commandBufferCount = 1;
    VkCommandBuffer cmd; vkAllocateCommandBuffers(m_device, &cai, &cmd);
    VkCommandBufferBeginInfo bi{}; bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO; bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    return cmd;
}

void WaterCoreGpu::submitAndWait(VkCommandBuffer cmd) const {
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si{}; si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO; si.commandBufferCount = 1; si.pCommandBuffers = &cmd;
    vkResetFences(m_device, 1, &m_fence);
    const VkResult sub = vkQueueSubmit(m_queue, 1, &si, m_fence);
    const VkResult wait = vkWaitForFences(m_device, 1, &m_fence, VK_TRUE, UINT64_MAX);
    if (sub != VK_SUCCESS || wait != VK_SUCCESS) std::fprintf(stderr, "[WaterCoreGpu] submit %d wait %d (VK_ERROR_DEVICE_LOST = -4)\n", static_cast<int>(sub), static_cast<int>(wait));
    vkFreeCommandBuffers(m_device, m_pool, 1, &cmd);
}

void WaterCoreGpu::copy(VkCommandBuffer cmd, const Buffer& from, const Buffer& to, VkDeviceSize bytes, VkDeviceSize srcOff, VkDeviceSize dstOff) const {
    if (bytes == 0) return;
    VkBufferCopy region{}; region.srcOffset = srcOff; region.dstOffset = dstOff; region.size = bytes;
    vkCmdCopyBuffer(cmd, from.buf, to.buf, 1, &region);
}

// ── 26.6 P2a: bricks on the host ─────────────────────────────────────────────────────────────────────────────────────

long WaterCoreGpu::storageIndex(const Volume& vol, int x, int y, int z) const {
    if (!vol.bricked) return static_cast<long>(x + vol.spec.dims.x * (y + vol.spec.dims.y * z));
    const glm::ivec3 q = glm::ivec3(x, y, z) + vol.o;
    if (q.x < 0 || q.y < 0 || q.z < 0) return -1;
    const glm::ivec3 b = q / vol.bs();
    if (b.x >= vol.bn.x || b.y >= vol.bn.y || b.z >= vol.bn.z) return -1;
    const int slot = vol.map[static_cast<size_t>(b.x) + static_cast<size_t>(vol.bn.x) * (b.y + static_cast<size_t>(vol.bn.y) * b.z)];
    if (slot == 0) return -1;
    return static_cast<long>(slot) * static_cast<long>(vol.bc) + (q.x & 7) + 8 * (q.z & 7) + 64 * (q.y & (vol.by - 1));   // y slowest (26.17)
}

long WaterCoreGpu::columnStorage(const Volume& vol, int x, int z) const {
    if (!vol.bricked) return static_cast<long>(x + vol.spec.dims.x * z);
    const int qx = x + vol.o.x, qz = z + vol.o.z;
    if (qx < 0 || qz < 0) return -1;
    const int bx = qx / kB, bz = qz / kB;
    if (bx >= vol.bn.x || bz >= vol.bn.z) return -1;
    const int packed = bx | (bz << 10);
    // the column table is sorted by (bz, bx): binary search
    const auto it = std::lower_bound(vol.colEntries.begin(), vol.colEntries.end(), packed, [](int a, int b) {
        const int az = a >> 10, bz2 = b >> 10; return az != bz2 ? az < bz2 : (a & 1023) < (b & 1023); });
    if (it == vol.colEntries.end() || *it != packed) return -1;
    return static_cast<long>(it - vol.colEntries.begin()) * 64 + (qx & 7) + 8 * (qz & 7);
}

void WaterCoreGpu::fillPushBricks(const Volume& vol, WcPush& push) const {
    push.bricked = vol.bricked ? 1 : 0;
    push.slotsUsed = vol.slotsUsed;
    push.ox = vol.o.x; push.oy = vol.o.y; push.oz = vol.o.z;
    push.bnx = vol.bn.x; push.bny = vol.bn.y; push.bnz = vol.bn.z; push.bly = vol.bly;
    push.slotTableOff = static_cast<int32_t>(vol.slotTableOff); push.colTableOff = static_cast<int32_t>(vol.colTableOff);
    push.counterOff = static_cast<int32_t>(vol.counterOff); push.colCount = vol.colCount;
    push.setFrozenOn(vol.anyFrozen ? (s_partialWakeOpenInterfaceForTest ? 2 : 1) : 0);   // 26.31 (2: skipped, not a wall - the test's red control)
    push.setCarryMeasure(vol.bricked && s_carryMeasure);   // 26.32
    push.tallY = (vol.bricked && vol.tallActive) ? vol.tallY : -1;   // 27.22
    push.tallOff = static_cast<int32_t>(vol.tallOff);
}

// A brick is needed when water lies within `reach` cells of it: for each neighbour offset d, the gap along
// an axis is 0 (d = 0) or the water's distance to that face + 1 + size (|d| - 1) (size: 8 along x and z, the brick height along
// y - 26.17); the neighbour is needed when every axis's gap
// is <= reach (Chebyshev). The per-face distances are minima over the brick's water taken separately per face, so the test
// is conservative: it may keep a brick it did not need, never drop one it did.
void WaterCoreGpu::markNeed(const Volume& vol, std::vector<char>& need, const glm::ivec3& b, const int dist[6], int reach) const {
    // per axis, the offsets whose gap is within reach (0 always; -k / +k while face + 1 + 8 (k - 1) <= reach and in the box)
    int lo[3], hi[3];
    const int bmax[3] = {vol.bn.x - 1, vol.bn.y - 1, vol.bn.z - 1}, bc[3] = {b.x, b.y, b.z}, size[3] = {kB, vol.by, kB};
    for (int a = 0; a < 3; ++a) {
        lo[a] = 0; hi[a] = 0;
        // 26.17: as many rings as the reach needs ALONG THIS AXIS - a 2-tall brick needs three up and down for a reach of 6
        // (one ring count for all axes left the dam break's halo short by 42 bricks: FlatBricksEqualCubic's red run)
        for (int k = 1; dist[2 * a] + 1 + (k - 1) * size[a] <= reach && bc[a] - k >= 0; ++k) lo[a] = -k;
        for (int k = 1; dist[2 * a + 1] + 1 + (k - 1) * size[a] <= reach && bc[a] + k <= bmax[a]; ++k) hi[a] = k;
    }
    // Chebyshev: a neighbour is needed when EVERY axis's gap is within reach - the box of per-axis ranges
    const size_t sx = static_cast<size_t>(vol.bn.x), sxy = sx * static_cast<size_t>(vol.bn.y);
    for (int dz = lo[2]; dz <= hi[2]; ++dz) for (int dy = lo[1]; dy <= hi[1]; ++dy) {
        const size_t row = static_cast<size_t>(b.x) + sx * static_cast<size_t>(b.y + dy) + sxy * static_cast<size_t>(b.z + dz);
        for (int dx = lo[0]; dx <= hi[0]; ++dx) { char& n = need[row + dx]; if (n < 1) n = 1; }
    }
}

// The bricks of a list of dense cells (pumps), each as a point of water: stored, and their halo.
static void cellToRecord(const glm::ivec3& o, const glm::ivec3& bs, int x, int y, int z, glm::ivec3& b, int dist[6]) {
    const glm::ivec3 q = glm::ivec3(x, y, z) + o, l = q % bs;
    b = q / bs;
    dist[0] = l.x; dist[1] = bs.x - 1 - l.x; dist[2] = l.y; dist[3] = bs.y - 1 - l.y; dist[4] = l.z; dist[5] = bs.z - 1 - l.z;
}

void WaterCoreGpu::needFromDenseFill(const Volume& vol, const std::vector<float>& f, std::vector<char>& need, int reach) const {
    const glm::ivec3 n = vol.spec.dims;
    std::vector<int> rec(vol.mapCount * 7, -1);   // per map entry: wet flag + 6 distances (-1 = dry)
    for (int z = 0; z < n.z; ++z) for (int y = 0; y < n.y; ++y) for (int x = 0; x < n.x; ++x) {
        const size_t i = static_cast<size_t>(x) + static_cast<size_t>(n.x) * (y + static_cast<size_t>(n.y) * z);
        if (i >= f.size() || f[i] == 0.0f) continue;
        const glm::ivec3 q = glm::ivec3(x, y, z) + vol.o, b = q / vol.bs(), l = q % vol.bs();
        int* r = &rec[(static_cast<size_t>(b.x) + static_cast<size_t>(vol.bn.x) * (b.y + static_cast<size_t>(vol.bn.y) * b.z)) * 7];
        if (r[0] < 0) { r[0] = 1; for (int k = 1; k < 7; ++k) r[k] = 7; }
        const int dd[6] = {l.x, 7 - l.x, l.y, vol.by - 1 - l.y, l.z, 7 - l.z};
        for (int k = 0; k < 6; ++k) r[1 + k] = std::min(r[1 + k], dd[k]);
    }
    for (size_t m = 0; m < vol.mapCount; ++m) {
        const int* r = &rec[m * 7];
        if (r[0] < 0) continue;
        need[m] = 2;   // holds water: stored first
        const glm::ivec3 b(static_cast<int>(m % vol.bn.x), static_cast<int>((m / vol.bn.x) % vol.bn.y), static_cast<int>(m / (static_cast<size_t>(vol.bn.x) * vol.bn.y)));
        markNeed(vol, need, b, r + 1, reach);
    }
}

// After a call: every brick within 4 cells of water (the extrapolation's three layers + a face beside an absent cell) must
// be stored. Faces between a stored cell and an absent brick belong to the absent brick, carry no velocity, and so never
// trip the transport's count - a too-thin halo shows here instead (WaterBrickTest's control).
namespace {
// The halo a cell's water needs (26.6): LIQUID cells (f >= thr) - the extrapolation's three layers + a face beside an absent
// cell = 4 cells; any other water (films, residue) - the rules that read round it reach one cell (settle, compaction, the
// thin-film slope, the transport) + a face = 2 cells. Measured: one reach for both stored a whole brick layer above a
// resting creek for 2.7e-6 of film and churned it every tick (2026-10-13).
constexpr int kReachLiquid = 4, kReachFilm = 2;
}

long WaterCoreGpu::haloShortAfterCall(const Volume& vol) const {
    std::vector<char> need(vol.mapCount, 0);
    const int32_t* info = vol.hostInfo.data();   // staging is uncached: read once into hostInfo after the fence
    for (size_t s = 1; s < static_cast<size_t>(vol.slotsUsed); ++s) {
        const int packed = vol.slotBrick[s];
        if (packed < 0) continue;
        const glm::ivec3 b(packed & 1023, (packed >> 10) & 1023, (packed >> 20) & 1023);
        for (int r = 0; r < 2; ++r) {
            const int rec = info[2 * s + r];
            if (!(rec & 1)) continue;
            int dist[6]; for (int k = 0; k < 6; ++k) dist[k] = (rec >> (1 + 3 * k)) & 7;
            markNeed(vol, need, b, dist, r == 0 ? kReachFilm : kReachLiquid);
        }
    }
    long shortBy = 0;
    for (size_t m = 0; m < vol.mapCount; ++m) if (need[m] && vol.map[m] == 0) ++shortBy;
    if (shortBy > 0) {   // 26.23 diagnostic: which record needed each short brick
        std::vector<char> nOld(vol.mapCount, 0), nNew(vol.mapCount, 0), nFilm(vol.mapCount, 0);
        for (size_t s = 1; s < static_cast<size_t>(vol.slotsUsed); ++s) {
            const int packed = vol.slotBrick[s];
            if (packed < 0) continue;
            const glm::ivec3 b(packed & 1023, (packed >> 10) & 1023, (packed >> 20) & 1023);
            const bool wasLiquid = std::find(vol.liquidAtStartForTest.begin(), vol.liquidAtStartForTest.end(), packed) != vol.liquidAtStartForTest.end();
            for (int r = 0; r < 2; ++r) {
                const int rec = info[2 * s + r];
                if (!(rec & 1)) continue;
                int dist[6]; for (int k = 0; k < 6; ++k) dist[k] = (rec >> (1 + 3 * k)) & 7;
                markNeed(vol, r == 0 ? nFilm : (wasLiquid ? nOld : nNew), b, dist, r == 0 ? kReachFilm : kReachLiquid);
                if (r == 0) {   // which unstored bricks this film record alone needs, with its distances
                    std::vector<char> one(vol.mapCount, 0);
                    markNeed(vol, one, b, dist, kReachFilm);
                    for (size_t m = 0; m < vol.mapCount; ++m) if (one[m] && vol.map[m] == 0) {
                        const int mx = static_cast<int>(m % vol.bn.x), my = static_cast<int>((m / vol.bn.x) % vol.bn.y), mz = static_cast<int>(m / (static_cast<size_t>(vol.bn.x) * vol.bn.y));
                        char buf[200];
                        std::snprintf(buf, sizeof buf, "[film brick (%d,%d,%d) -> short (%+d,%+d,%+d); face dist -x%d +x%d -y%d +y%d -z%d +z%d; travel %d] ",
                                      b.x, b.y, b.z, mx - b.x, my - b.y, mz - b.z, dist[0], dist[1], dist[2], dist[3], dist[4], dist[5], vol.lastTravelForTest);
                        s_haloShortDetailForTest += buf;
                    }
                }
            }
        }
        for (size_t m = 0; m < vol.mapCount; ++m) if (need[m] && vol.map[m] == 0) {
            if (nOld[m]) ++s_haloShortLiquidOldForTest; else if (nNew[m]) ++s_haloShortLiquidNewForTest; else if (nFilm[m]) ++s_haloShortFilmForTest;
        }
    }
    return shortBy;
}

std::vector<int> WaterCoreGpu::applyResidency(Volume& vol, const std::vector<char>& need) {
    std::vector<int> fresh;
    int freed = 0; long held = 0;
    // 26.18: a brick wanted only for the halo (need 1) that is all ground at the last upload reads the GROUND sentinel; a brick
    // holding water, or that a pump or a deposit needs (need 2), is always stored for real
    const bool ground = vol.groundOn && vol.allSolid.size() == vol.mapCount;
    const std::vector<char>* needRef = &need;   // 27.22: re-pointed at the expand's wants below
    auto wantGround = [&](size_t m) { return ground && (*needRef)[m] == 1 && vol.allSolid[m]; };
    // §27 W1b: a RECORDED column's bricks are never stored - each reads the ground sentinel (a wall), whatever the halo asks
    const size_t ncol = static_cast<size_t>(vol.bn.x) * vol.bn.z;
    const bool anyRec = vol.anyRecorded && vol.colRecorded.size() == ncol;
    auto recorded = [&](size_t m) {
        if (!anyRec || s_recordedOpenForTest) return false;   // the red control: the halo stores them as air
        const size_t bx = m % static_cast<size_t>(vol.bn.x), bz = m / (static_cast<size_t>(vol.bn.x) * vol.bn.y);
        return vol.colRecorded[bx + static_cast<size_t>(vol.bn.x) * bz] != 0;
    };
    // 27.22: with tall cells, every brick under the tall layer's brick reads the ground sentinel (its water is in the tall cells);
    // while EXPANDING (tallPending 2) those bricks are wanted again - stored for real (water) or ground (all solid)
    const int tallBrick = (vol.tallY >= 0) ? (vol.tallY + vol.o.y) / vol.by : -1;
    auto underTall = [&](size_t m) { return vol.tallActive && vol.tallPending != 2 && tallBrick > 0 && static_cast<int>((m / static_cast<size_t>(vol.bn.x)) % vol.bn.y) < tallBrick; };
    std::vector<char> needX;
    const std::vector<char>* needP = &need;
    if (vol.tallPending == 2 && tallBrick > 0) {
        needX = need;
        for (size_t m = 0; m < vol.mapCount; ++m)
            if (static_cast<int>((m / static_cast<size_t>(vol.bn.x)) % vol.bn.y) < tallBrick) needX[m] = std::max<char>(needX[m], (ground && vol.allSolid[m]) ? 1 : 2);
        needP = &needX;
    }
    const std::vector<char>& needT = *needP;
    needRef = &needT;
    bool groundChanged = false;
    for (size_t m = 0; m < vol.mapCount; ++m) {
        if (underTall(m)) {
            if (vol.map[m] >= kFirstSlot) { vol.slotBrick[static_cast<size_t>(vol.map[m])] = -1; vol.map[m] = 0; ++freed; }
            if (vol.map[m] != kGround) { vol.map[m] = kGround; groundChanged = true; }
            continue;
        }
        if (recorded(m)) {
            if (vol.map[m] >= kFirstSlot) { vol.slotBrick[static_cast<size_t>(vol.map[m])] = -1; vol.map[m] = 0; ++freed; }
            if (vol.map[m] != kGround) { vol.map[m] = kGround; groundChanged = true; }
            continue;
        }
        if (vol.map[m] >= kFirstSlot && (!needT[m] || wantGround(m))) { vol.slotBrick[static_cast<size_t>(vol.map[m])] = -1; vol.map[m] = 0; ++freed; }
        else if (vol.map[m] == kGround && !wantGround(m)) { vol.map[m] = 0; groundChanged = true; }
        if (vol.map[m] == 0 && wantGround(m)) { vol.map[m] = kGround; groundChanged = true; }
    }
    vol.groundCount = 0;
    if (ground) for (size_t m = 0; m < vol.mapCount; ++m) vol.groundCount += vol.map[m] == kGround ? 1 : 0;
    if (groundChanged) vol.tablesDirty = true;
    size_t next = kFirstSlot;
    for (char pass : {char(2), char(1)}) {   // bricks holding water first, then the halo (a capacity refusal never strands water)
        for (size_t m = 0; m < vol.mapCount; ++m) {
            if (needT[m] != pass || vol.map[m] != 0 || recorded(m)) continue;
            while (next < vol.capacity && vol.slotBrick[next] >= 0) ++next;
            if (next >= vol.capacity) { ++held; continue; }   // 26.6 capacity: the brick stays absent, its faces closed (counted)
            const int bx = static_cast<int>(m % vol.bn.x), by = static_cast<int>((m / vol.bn.x) % vol.bn.y), bz = static_cast<int>(m / (static_cast<size_t>(vol.bn.x) * vol.bn.y));
            vol.map[m] = static_cast<int>(next);
            vol.slotBrick[next] = bx | (by << 10) | (bz << 20);
            fresh.push_back(static_cast<int>(next));
        }
    }
    vol.lastAllocated = static_cast<int>(fresh.size()); vol.lastFreed = freed; vol.lastHeld = held;
    if (held > 0 && vol.growable) vol.growWant = vol.storedCount + static_cast<size_t>(fresh.size()) + static_cast<size_t>(held) + static_cast<size_t>(held) / 4;   // 27.23 W1d.1
    vol.storedCount += static_cast<int>(fresh.size()) - freed;
    if (fresh.empty() && freed == 0 && !vol.colEntries.empty()) return fresh;   // the steady state: tables, range and columns unchanged
    int top = 0;
    for (size_t s = kFirstSlot; s < vol.capacity; ++s) if (vol.slotBrick[s] >= 0) top = static_cast<int>(s);
    vol.slotsUsed = std::max(top + 1, kFirstSlot);   // slots 0 and 1 always addressed (the sentinels' check)
    vol.colEntries.clear();
    for (int bz = 0; bz < vol.bn.z; ++bz) for (int bx = 0; bx < vol.bn.x; ++bx) {
        bool any = false;   // a REAL brick (26.18: ground alone holds no water - its column has nothing to do)
        for (int by = 0; by < vol.bn.y && !any; ++by) any = vol.map[static_cast<size_t>(bx) + static_cast<size_t>(vol.bn.x) * (by + static_cast<size_t>(vol.bn.y) * bz)] >= kFirstSlot;
        if (any) vol.colEntries.push_back(bx | (bz << 10));
    }
    vol.colCount = static_cast<int>(vol.colEntries.size());
    if (!fresh.empty() || freed > 0) vol.tablesDirty = true;
    return fresh;
}

void WaterCoreGpu::ensureCapacity(Volume& vol, const std::vector<char>& need) {
    if (!vol.growable || need.size() != vol.mapCount) return;
    const bool ground = vol.groundOn && vol.allSolid.size() == vol.mapCount;
    size_t want = 0;
    for (size_t m = 0; m < vol.mapCount; ++m) if (need[m] >= 1 && !(ground && need[m] == 1 && vol.allSolid[m])) ++want;
    if (want + kFirstSlot > vol.capacity) growPool(vol, want + want / 4);
}

bool WaterCoreGpu::growPool(Volume& vol, size_t slots) {
    vol.unitsFullNext = true;   // 27.23 W1e: this writes stored cells, frozen ones included - the next reduction computes every unit
    if (!vol.growable || !vol.bricked) return false;
    size_t newCap = std::max<size_t>(vol.capacity, kFirstSlot + 1);
    while (newCap < slots + kFirstSlot) newCap *= 2;
    newCap = std::min(newCap, vol.mapCount + kFirstSlot);
    if (newCap <= vol.capacity) return false;
    settle(vol);
    const size_t oldCap = vol.capacity, oldInfoOff = vol.infoOff, oldTallOff = vol.tallOff;
    // the capacity-dependent buffers: the new ones replace them, the old kept until their contents are copied
    Buffer* const keep[] = {&vol.f, &vol.fOrig, &vol.fPrev, &vol.u, &vol.v, &vol.w, &vol.uOld, &vol.vOld, &vol.wOld, &vol.occ, &vol.src, &vol.fLo,
                            &vol.solid, &vol.deposit, &vol.p, &vol.bricks};
    Buffer* const scratchB[] = {&vol.liq, &vol.diag, &vol.rhs, &vol.res, &vol.valA, &vol.valB, &vol.knownA, &vol.knownB, &vol.first, &vol.keep,
                                &vol.part, &vol.part2, &vol.staging, &vol.stageCap};
    std::vector<Buffer> oldKeep, oldScratch;
    for (Buffer* b : keep) { oldKeep.push_back(*b); *b = Buffer{}; }
    for (Buffer* b : scratchB) { oldScratch.push_back(*b); *b = Buffer{}; }
    // createVolume's layout at the new capacity
    vol.capacity = newCap;
    vol.cells = vol.capacity * vol.bc; vol.nu = vol.nv = vol.nw = vol.cells;
    vol.colTableOff = vol.slotTableOff + vol.capacity; vol.counterOff = vol.colTableOff + vol.colCap; vol.infoOff = vol.counterOff + kCounters;
    vol.brickInts = vol.infoOff + 2 * vol.capacity;
    vol.tallOff = vol.brickInts; vol.brickInts += vol.dColumns;
    vol.latticeMax = std::max({vol.nu, vol.nv, vol.nw});
    vol.groups = std::max(2 * ((std::max({vol.cells, vol.latticeMax, vol.columns}) + 255) / 256), static_cast<size_t>(vol.bn.x) * vol.nuy * static_cast<size_t>(vol.bn.z)) + 1;
    const VkDeviceSize F4 = sizeof(float);
    std::string err;
    bool ok = createBuffer(vol.f, vol.cells * F4, false, &err) && createBuffer(vol.fLo, vol.cells * F4, false, &err) && createBuffer(vol.fOrig, vol.cells * F4, false, &err) && createBuffer(vol.fPrev, vol.cells * F4, false, &err)
           && createBuffer(vol.u, vol.nu * F4, false, &err) && createBuffer(vol.v, vol.nv * F4, false, &err) && createBuffer(vol.w, vol.nw * F4, false, &err)
           && createBuffer(vol.uOld, vol.nu * F4, false, &err) && createBuffer(vol.vOld, vol.nv * F4, false, &err) && createBuffer(vol.wOld, vol.nw * F4, false, &err)
           && createBuffer(vol.occ, vol.cells * 4, false, &err) && createBuffer(vol.src, vol.cells * F4, false, &err) && createBuffer(vol.liq, vol.cells * 4, false, &err)
           && createBuffer(vol.diag, vol.cells * F4, false, &err) && createBuffer(vol.rhs, vol.cells * F4, false, &err) && createBuffer(vol.p, vol.cells * F4, false, &err) && createBuffer(vol.res, vol.cells * F4, false, &err)
           && createBuffer(vol.valA, vol.latticeMax * F4, false, &err) && createBuffer(vol.valB, vol.latticeMax * F4, false, &err)
           && createBuffer(vol.knownA, vol.latticeMax * 4, false, &err) && createBuffer(vol.knownB, vol.latticeMax * 4, false, &err)
           && createBuffer(vol.first, vol.latticeMax * 4, false, &err) && createBuffer(vol.keep, vol.latticeMax * 4, false, &err)
           && createBuffer(vol.part, vol.groups * 16, false, &err) && createBuffer(vol.part2, vol.groups * 16, false, &err)
           && createBuffer(vol.solid, vol.cells * 16, false, &err) && createBuffer(vol.deposit, vol.cells * F4, false, &err)
           && createBuffer(vol.bricks, vol.brickInts * 4, false, &err);
    vol.offU = vol.cells * F4; vol.offV = vol.offU + vol.nu * F4; vol.offW = vol.offV + vol.nv * F4;
    vol.offOcc = vol.offW + vol.nw * F4; vol.offSrc = vol.offOcc + vol.cells * 4; vol.offOut = vol.offSrc + vol.cells * F4;
    vol.offP = vol.offOut + kOutSlots * 16; vol.offSources = vol.offP + vol.cells * F4;
    vol.offSurf = vol.offSources + kMaxSources * sizeof(GpuSource);
    vol.offSolid = vol.offSurf + 64 + vol.columns * (32 + sizeof(Core::Water::SurfaceColumn));
    vol.offBirths = vol.offSolid + vol.cells * 16;
    vol.offDeposit = vol.offBirths + vol.columns * 32;
    vol.offBricks = vol.offDeposit + vol.cells * F4;
    vol.offParticles = vol.offBricks + vol.brickInts * 4;
    ok = ok && createBuffer(vol.staging, vol.offParticles, true, &err);
    if (!ok) {   // out of device memory: keep the old pool (the residency's capacity backstop holds the water), loudly
        std::fprintf(stderr, "[WaterCoreGpu] pool grow to %zu slots failed: %s - kept %zu\n", newCap, err.c_str(), oldCap);
        for (Buffer* b : keep) destroyBuffer(*b);
        for (Buffer* b : scratchB) destroyBuffer(*b);
        for (size_t i = 0; i < std::size(keep); ++i) *keep[i] = oldKeep[i];
        for (size_t i = 0; i < std::size(scratchB); ++i) *scratchB[i] = oldScratch[i];
        vol.capacity = oldCap; vol.cells = oldCap * vol.bc; vol.nu = vol.nv = vol.nw = vol.cells;
        vol.colTableOff = vol.slotTableOff + vol.capacity; vol.counterOff = vol.colTableOff + vol.colCap; vol.infoOff = vol.counterOff + kCounters;
        vol.brickInts = vol.infoOff + 2 * vol.capacity; vol.tallOff = vol.brickInts; vol.brickInts += vol.dColumns;
        vol.latticeMax = vol.cells;
        vol.groups = std::max(2 * ((std::max({vol.cells, vol.latticeMax, vol.columns}) + 255) / 256), static_cast<size_t>(vol.bn.x) * vol.nuy * static_cast<size_t>(vol.bn.z)) + 1;
        vol.offU = vol.cells * F4; vol.offV = vol.offU + vol.nu * F4; vol.offW = vol.offV + vol.nv * F4;
        vol.offOcc = vol.offW + vol.nw * F4; vol.offSrc = vol.offOcc + vol.cells * 4; vol.offOut = vol.offSrc + vol.cells * F4;
        vol.offP = vol.offOut + kOutSlots * 16; vol.offSources = vol.offP + vol.cells * F4;
        vol.offSurf = vol.offSources + kMaxSources * sizeof(GpuSource);
        vol.offSolid = vol.offSurf + 64 + vol.columns * (32 + sizeof(Core::Water::SurfaceColumn));
        vol.offBirths = vol.offSolid + vol.cells * 16; vol.offDeposit = vol.offBirths + vol.columns * 32; vol.offBricks = vol.offDeposit + vol.cells * F4;
        vol.offParticles = vol.offBricks + vol.brickInts * 4;
        return false;
    }
    VkCommandBuffer cmd = beginCommands();
    for (Buffer* b : keep) vkCmdFillBuffer(cmd, b->buf, 0, b->bytes, 0);
    for (Buffer* b : scratchB) if (b->buf && b != &vol.staging) vkCmdFillBuffer(cmd, b->buf, 0, b->bytes, 0);
    barrier(cmd);
    // every per-cell field keeps its slots (slot numbers unchanged: the old pool is the new one's prefix)
    for (size_t i = 0; i + 1 < std::size(keep); ++i) copy(cmd, oldKeep[i], *keep[i], oldKeep[i].bytes);
    // the Bricks buffer: the map and the slot table keep their offsets; the water records and the tall heights move
    const Buffer& ob = oldKeep.back();
    copy(cmd, ob, vol.bricks, (vol.slotTableOff + oldCap) * 4);
    copy(cmd, ob, vol.bricks, 2 * oldCap * 4, oldInfoOff * 4, vol.infoOff * 4);
    copy(cmd, ob, vol.bricks, vol.dColumns * 4, oldTallOff * 4, vol.tallOff * 4);
    submitAndWait(cmd);
    for (Buffer& b : oldKeep) destroyBuffer(b);
    for (Buffer& b : oldScratch) destroyBuffer(b);
    vol.slotBrick.resize(vol.capacity, -1);
    vol.hostInfo.resize(2 * vol.capacity, 0);
    writeTablesToStaging(vol);   // the column table and the counters move with the capacity
    cmd = beginCommands();
    copy(cmd, vol.staging, vol.bricks, vol.counterOff * 4, vol.offBricks);
    submitAndWait(cmd);
    bindPipelines(vol);
    vol.listsSized = false;
    ++vol.poolGrows;
    return true;
}

void WaterCoreGpu::writeTablesToStaging(Volume& vol) const {
    auto* st = reinterpret_cast<int32_t*>(static_cast<char*>(vol.staging.mapped) + vol.offBricks);
    std::memcpy(st, vol.map.data(), vol.mapCount * 4);
    std::memcpy(st + vol.slotTableOff, vol.slotBrick.data(), vol.capacity * 4);
    std::fill(st + vol.colTableOff, st + vol.colTableOff + vol.colCap, 0);
    if (!vol.colEntries.empty()) std::memcpy(st + vol.colTableOff, vol.colEntries.data(), vol.colEntries.size() * 4);
    if (vol.anyFrozen && vol.colFrozen.size() == static_cast<size_t>(vol.bn.x) * vol.bn.z) {   // 26.31: the frozen bit (bit 30; water_core.glsl)
        auto frozen = [&](int bx, int bz) { return bx >= 0 && bz >= 0 && bx < vol.bn.x && bz < vol.bn.z && vol.colFrozen[static_cast<size_t>(bx) + static_cast<size_t>(vol.bn.x) * bz]; };
        for (size_t s = 2; s < vol.capacity; ++s) {
            const int packed = vol.slotBrick[s];
            if (packed >= 0 && frozen(packed & 1023, (packed >> 20) & 1023)) st[vol.slotTableOff + s] = packed | kFrozenBit;
        }
        for (size_t e = 0; e < vol.colEntries.size(); ++e) {
            const int packed = vol.colEntries[e];
            if (frozen(packed & 1023, (packed >> 10) & 1023)) st[vol.colTableOff + e] = packed | kFrozenBit;
        }
    }
}

void WaterCoreGpu::setPartialWake(Volume& vol, bool on) {
    vol.partialWake = on && vol.bricked && vol.sparse;
    wakeAll(vol);
}

void WaterCoreGpu::wakeAll(Volume& vol) {
    ++vol.wakeAllCount;   // §27 W1b diagnostic: a whole-volume wake (every column moving -> every recorded tile in the margin wakes)
    vol.asleep = false; vol.quietTicks = 0; vol.quietRef = 0.0;
    vol.wakeAllPending = true;
}

void WaterCoreGpu::wakeBox(Volume& vol, const glm::ivec3& lo, const glm::ivec3& hi) {
    vol.asleep = false; vol.quietTicks = 0; vol.quietRef = 0.0;
    if (!vol.partialWake) return;
    const size_t nc = static_cast<size_t>(vol.bn.x) * vol.bn.z;
    if (vol.colWake.size() != nc) vol.colWake.assign(nc, 0);
    const int bx0 = std::max(0, (lo.x + vol.o.x) >> 3), bx1 = std::min(vol.bn.x - 1, (hi.x + vol.o.x) >> 3);
    const int bz0 = std::max(0, (lo.z + vol.o.z) >> 3), bz1 = std::min(vol.bn.z - 1, (hi.z + vol.o.z) >> 3);
    for (int bz = bz0; bz <= bz1; ++bz) for (int bx = bx0; bx <= bx1; ++bx) vol.colWake[static_cast<size_t>(bx) + static_cast<size_t>(vol.bn.x) * bz] = 1;
}

// §27 W1b: a tile's cells read back - only the bricks of its columns cross (a whole-volume download sent 1.9 M cells for a 46 x 28 m
// pond: 105 ms at a sleep, 27.8). The fills of stored bricks; ground and absent bricks hold none. Occupancy from the host copy.
bool WaterCoreGpu::readColumns(Volume& vol, const glm::ivec2& bLo, const glm::ivec2& bHi, WaterGrid& out) {
    if (!vol.bricked) return false;
    const auto tr0 = std::chrono::steady_clock::now();
    settle(vol);
    s_tileGpuMs.readSettle = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tr0).count();
    const glm::ivec2 lo = glm::max(bLo, glm::ivec2(0)), hi = glm::min(bHi, glm::ivec2(vol.bn.x - 1, vol.bn.z - 1));
    if (hi.x < lo.x || hi.y < lo.y) return false;
    const glm::ivec3 n = vol.spec.dims;
    const int x0 = std::max(0, lo.x * kB - vol.o.x), x1 = std::min(n.x - 1, hi.x * kB + kB - 1 - vol.o.x);
    const int z0 = std::max(0, lo.y * kB - vol.o.z), z1 = std::min(n.z - 1, hi.y * kB + kB - 1 - vol.o.z);
    if (x1 < x0 || z1 < z0) return false;
    std::vector<int> slots;
    for (int bz = lo.y; bz <= hi.y; ++bz) for (int by = 0; by < vol.bn.y; ++by) for (int bx = lo.x; bx <= hi.x; ++bx) {
        const int s = vol.map[static_cast<size_t>(bx) + static_cast<size_t>(vol.bn.x) * (by + static_cast<size_t>(vol.bn.y) * bz)];
        if (s >= kFirstSlot) slots.push_back(s);
    }
    std::sort(slots.begin(), slots.end());
    if (!slots.empty()) {
        VkCommandBuffer cmd = beginCommands();
        for (size_t a = 0; a < slots.size();) {
            size_t e = a + 1;
            while (e < slots.size() && slots[e] == slots[e - 1] + 1) ++e;
            const VkDeviceSize first = static_cast<VkDeviceSize>(slots[a]) * vol.bc * 4, bytes = static_cast<VkDeviceSize>(e - a) * vol.bc * 4;
            copy(cmd, vol.f, vol.staging, bytes, first, first);
            a = e;
        }
        const auto ts = std::chrono::steady_clock::now();
        submitAndWait(cmd);
        s_tileGpuMs.readSubmit = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - ts).count();
    }
    const auto th = std::chrono::steady_clock::now();
    out = WaterGrid(GridSpec{vol.spec.origin + glm::ivec3(x0, 0, z0), glm::ivec3(x1 - x0 + 1, n.y, z1 - z0 + 1), vol.spec.h});
    const float* sf = reinterpret_cast<const float*>(vol.staging.mapped);
    for (int z = z0; z <= z1; ++z) for (int y = 0; y < n.y; ++y) for (int x = x0; x <= x1; ++x) {
        const long i = storageIndex(vol, x, y, z);
        out.f(x - x0, y, z - z0) = i >= static_cast<long>(kFirstSlot * vol.bc) ? sf[i] : 0.0f;
        const size_t d = static_cast<size_t>(x) + static_cast<size_t>(n.x) * (y + static_cast<size_t>(n.y) * z);
        out.occ(x - x0, y, z - z0) = d < vol.hostOcc.size() ? static_cast<Occ>(vol.hostOcc[d]) : Occ::Solid;
    }
    // 27.22 TC4: a tall cell is its column's water from the bed up - spread back into cells for the record (as download does)
    if (vol.tallActive && vol.tallH.size() == vol.dColumns && vol.tallY >= 0 && vol.tallY < n.y) {
        const int T = vol.tallY;
        for (int z = z0; z <= z1; ++z) for (int x = x0; x <= x1; ++x) {
            const int Hc = vol.tallH[static_cast<size_t>(x) + static_cast<size_t>(n.x) * z];
            if (Hc <= 1) continue;
            const float share = out.f(x - x0, T, z - z0) / static_cast<float>(Hc);
            for (int y = std::max(0, T - Hc + 1); y <= T; ++y) out.f(x - x0, y, z - z0) = share;
        }
    }
    s_tileGpuMs.readHost = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - th).count();
    return true;
}

// §27 W1b measurement: the GROUND sentinel (slot 1) must never be written - every ground and recorded brick reads it. Its largest
// |f|, |u|, |v|, |w|, read back (a settle + one small copy). Debug only.
glm::vec4 WaterCoreGpu::sentinelMaxAbs(Volume& vol) {
    if (!vol.bricked) return glm::vec4(0.0f);
    settle(vol);
    const VkDeviceSize off = static_cast<VkDeviceSize>(kGround) * vol.bc * 4, bytes = vol.bc * 4;
    VkCommandBuffer cmd = beginCommands();
    const Buffer* bufs[4] = {&vol.f, &vol.u, &vol.v, &vol.w};
    for (int k = 0; k < 4; ++k) copy(cmd, *bufs[k], vol.staging, bytes, off, static_cast<VkDeviceSize>(k) * bytes);
    submitAndWait(cmd);
    glm::vec4 m(0.0f);
    const float* s = reinterpret_cast<const float*>(vol.staging.mapped);
    for (int k = 0; k < 4; ++k) for (size_t i = 0; i < vol.bc; ++i) m[k] = std::max(m[k], std::abs(s[static_cast<size_t>(k) * vol.bc + i]));
    return m;
}

void WaterCoreGpu::setColumnsRecorded(Volume& vol, const glm::ivec2& bLo, const glm::ivec2& bHi) {
    const size_t nc = static_cast<size_t>(vol.bn.x) * vol.bn.z;
    if (!vol.bricked || nc == 0) return;
    settle(vol);
    if (vol.colRecorded.size() != nc) vol.colRecorded.assign(nc, 0);
    for (int bz = std::max(0, bLo.y); bz <= std::min(vol.bn.z - 1, bHi.y); ++bz)
        for (int bx = std::max(0, bLo.x); bx <= std::min(vol.bn.x - 1, bHi.x); ++bx) vol.colRecorded[static_cast<size_t>(bx) + static_cast<size_t>(vol.bn.x) * bz] = 1;
    vol.anyRecorded = true;
    // freed NOW, not at the next call: a body whose every column is frozen is asleep and runs no residency - its recorded bricks
    // would keep their water (counted twice, drawn twice) until something woke it. Every other brick keeps its residency.
    std::vector<char> need(vol.mapCount, 0);
    for (size_t m = 0; m < vol.mapCount; ++m) need[m] = vol.map[m] >= kFirstSlot ? 2 : (vol.map[m] == kGround ? 1 : 0);
    applyResidency(vol, need);
    if (vol.tablesDirty) {
        VkCommandBuffer cmd = beginCommands();
        writeTablesToStaging(vol);
        copy(cmd, vol.staging, vol.bricks, vol.counterOff * 4, vol.offBricks);
        submitAndWait(cmd);
        vol.tablesDirty = false;
    }
}

// The tile's water goes into fresh bricks NOW (one small submission), not at the next call: the manager clears the record in the
// same breath, so the water is in exactly one place at every moment (deferred, it was in neither for a call - the books missed
// 192 m^3 for three tiles). The seeded columns are QUIET (still water at rest): they freeze unless the moving water's margin
// reaches them - a seed counted as motion woke its neighbours, which woke theirs (one push woke all 8 tiles).
void WaterCoreGpu::seedColumns(Volume& vol, WaterGrid cells, const glm::ivec2& bLo, const glm::ivec2& bHi) {
    const size_t nc = static_cast<size_t>(vol.bn.x) * vol.bn.z;
    if (!vol.bricked || vol.colRecorded.size() != nc) return;
    const auto tq0 = std::chrono::steady_clock::now();
    settle(vol);
    s_tileGpuMs.seedSettle = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tq0).count();
    const auto tq1 = std::chrono::steady_clock::now();
    if (vol.colQuiet.size() != nc) vol.colQuiet.assign(nc, 0);
    for (int bz = std::max(0, bLo.y); bz <= std::min(vol.bn.z - 1, bHi.y); ++bz)
        for (int bx = std::max(0, bLo.x); bx <= std::min(vol.bn.x - 1, bHi.x); ++bx) {
            const size_t c = static_cast<size_t>(bx) + static_cast<size_t>(vol.bn.x) * bz;
            vol.colRecorded[c] = 0;
            vol.colQuiet[c] = 1 << 20;   // at rest (the freeze test's count, far past restTicks)
        }
    vol.anyRecorded = std::find(vol.colRecorded.begin(), vol.colRecorded.end(), 1) != vol.colRecorded.end();
    // 27.22 TC4: with tall cells, a seeded column's water under the floor goes into its tall cell (the bricks there read the ground)
    if ((vol.tallActive || vol.tallPending == 1) && vol.tallH.size() == vol.dColumns && vol.tallY >= 0 && !s_seedSkipsTallCollapseForTest) {
        const glm::ivec3 nn = vol.spec.dims, o2 = cells.spec().origin - vol.spec.origin;
        const int T = vol.tallY - o2.y;
        for (int z = 0; z < cells.nz(); ++z) for (int x = 0; x < cells.nx(); ++x) {
            const int vx = x + o2.x, vz = z + o2.z;
            if (vx < 0 || vz < 0 || vx >= nn.x || vz >= nn.z || T < 0 || T >= cells.ny()) continue;
            const int Hc = vol.tallH[static_cast<size_t>(vx) + static_cast<size_t>(nn.x) * vz];
            if (Hc <= 1) continue;
            float sum = 0.0f;
            for (int y = std::max(0, T - Hc + 1); y <= T; ++y) { sum += cells.f(x, y, z); cells.f(x, y, z) = 0.0f; }
            cells.f(x, T, z) = sum;
        }
    }
    // the seed's bricks (and a ring of halo, as an upload's) stored, then its fills written into them - over the TILE's cells only
    // (27.19: a whole-volume dense fill, allocated, zeroed and walked twice, was 9-10 ms of a wake on the 46 x 28 m bench pond)
    const glm::ivec3 n = vol.spec.dims, off = cells.spec().origin - vol.spec.origin;
    auto inVol = [&](const glm::ivec3& c) { return c.x >= 0 && c.y >= 0 && c.z >= 0 && c.x < n.x && c.y < n.y && c.z < n.z; };
    std::vector<char> need(vol.mapCount, 0);
    for (size_t m = 0; m < vol.mapCount; ++m) need[m] = vol.map[m] >= kFirstSlot ? 2 : (vol.map[m] == kGround ? 1 : 0);   // the rest stays as it is
    {   // needFromDenseFill's rule, over the seed's cells: per brick holding seed water, the nearest wet cell to each face
        std::vector<int> rec(vol.mapCount * 7, -1);
        for (int z = 0; z < cells.nz(); ++z) for (int y = 0; y < cells.ny(); ++y) for (int x = 0; x < cells.nx(); ++x) {
            const glm::ivec3 c = off + glm::ivec3(x, y, z);
            if (cells.f(x, y, z) == 0.0f || !inVol(c)) continue;
            const glm::ivec3 q = c + vol.o, b = q / vol.bs(), l = q % vol.bs();
            int* r = &rec[(static_cast<size_t>(b.x) + static_cast<size_t>(vol.bn.x) * (b.y + static_cast<size_t>(vol.bn.y) * b.z)) * 7];
            if (r[0] < 0) { r[0] = 1; for (int k = 1; k < 7; ++k) r[k] = 7; }
            const int dd[6] = {l.x, 7 - l.x, l.y, vol.by - 1 - l.y, l.z, 7 - l.z};
            for (int k = 0; k < 6; ++k) r[1 + k] = std::min(r[1 + k], dd[k]);
        }
        for (size_t m = 0; m < vol.mapCount; ++m) {
            const int* r = &rec[m * 7];
            if (r[0] < 0) continue;
            need[m] = 2;
            const glm::ivec3 b(static_cast<int>(m % vol.bn.x), static_cast<int>((m / vol.bn.x) % vol.bn.y), static_cast<int>(m / (static_cast<size_t>(vol.bn.x) * vol.bn.y)));
            markNeed(vol, need, b, r + 1, kB);
        }
    }
    auto tp = std::chrono::steady_clock::now();
    auto part = [&](int k) { const auto n = std::chrono::steady_clock::now(); s_tileGpuMs.seedPart[k] = std::chrono::duration<double, std::milli>(n - tp).count(); tp = n; };
    s_tileGpuMs.seedPart[0] = std::chrono::duration<double, std::milli>(tp - tq1).count();
    ensureCapacity(vol, need);   // 27.23 W1d.1
    const std::vector<int> fresh = applyResidency(vol, need);
    part(1);
    VkCommandBuffer cmd = beginCommands();
    if (!fresh.empty()) { scatterSlots(vol, fresh, nullptr); recordSlotUploads(cmd, vol, fresh, true); }
    barrier(cmd);
    part(2);
    auto* sf = reinterpret_cast<float*>(static_cast<char*>(vol.staging.mapped));
    std::vector<int> touched;
    double unplaced = 0.0;
    const double h3 = static_cast<double>(vol.spec.h) * vol.spec.h * vol.spec.h;
    for (int z = 0; z < cells.nz(); ++z) for (int y = 0; y < cells.ny(); ++y) for (int x = 0; x < cells.nx(); ++x) {
        const glm::ivec3 c = off + glm::ivec3(x, y, z);
        if (cells.f(x, y, z) == 0.0f || !inVol(c)) continue;
        const long i = storageIndex(vol, c.x, c.y, c.z);
        if (i < static_cast<long>(kFirstSlot * vol.bc)) continue;
        const int slot = static_cast<int>(i / static_cast<long>(vol.bc));
        if (touched.empty() || touched.back() != slot) touched.push_back(slot);   // 27.19: a find per wet cell was ~1-2 ms a tile
    }
    std::sort(touched.begin(), touched.end());
    touched.erase(std::unique(touched.begin(), touched.end()), touched.end());
    for (int slot : touched) std::memset(sf + static_cast<size_t>(slot) * vol.bc, 0, vol.bc * sizeof(float));   // each touched slot written whole
    for (int z = 0; z < cells.nz(); ++z) for (int y = 0; y < cells.ny(); ++y) for (int x = 0; x < cells.nx(); ++x) {
        const glm::ivec3 c = off + glm::ivec3(x, y, z);
        const float f = cells.f(x, y, z);
        if (f == 0.0f || !inVol(c)) continue;
        const long i = storageIndex(vol, c.x, c.y, c.z);
        if (i < static_cast<long>(kFirstSlot * vol.bc)) { unplaced += f * h3; continue; }   // a full capacity: counted, never silent
        sf[i] = f;
    }
    vol.seedUnplacedM3 += unplaced;
    std::sort(touched.begin(), touched.end());
    for (size_t a = 0; a < touched.size();) {
        size_t e = a + 1;
        while (e < touched.size() && touched[e] == touched[e - 1] + 1) ++e;
        const VkDeviceSize first = static_cast<VkDeviceSize>(touched[a]) * vol.bc * 4, bytes = static_cast<VkDeviceSize>(e - a) * vol.bc * 4;
        copy(cmd, vol.staging, vol.f, bytes, first, first);
        // fPrev too: the freeze test's |f - fPrev| covers every stored brick, but fPrev is refreshed only for the STEPPED ones - a
        // seeded column is frozen, so its fPrev stayed at the allocation's 0 and it read |df| = 1 every call: moving, its margin woke
        // its neighbours, whose seeds did the same (one push woke all 24 tiles of the bench pond in under 2 s, live)
        if (!s_seedSkipsFPrevForTest) copy(cmd, vol.staging, vol.fPrev, bytes, first, first);
        a = e;
    }
    part(3);
    if (vol.tablesDirty) { writeTablesToStaging(vol); copy(cmd, vol.staging, vol.bricks, vol.counterOff * 4, vol.offBricks); vol.tablesDirty = false; }
    part(4);
    s_tileGpuMs.seedHost = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tq1).count();
    const auto tq2 = std::chrono::steady_clock::now();
    submitAndWait(cmd);
    s_tileGpuMs.seedSubmit = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tq2).count();
    vol.recordValid = false;   // the next residency keeps every stored brick (its record predates the seed)
    vol.surfaceFullNext = true;   // 27.15: the seeded columns are frozen, their surface must still be drawn
    vol.unitsFullNext = true;     // 27.23 W1e: ... and weighed
    vol.tablesDirty = true;    // the frozen bits follow at the next call
}

// 26.31 THE PARTIAL WAKE (docs/WaterCore.md 26.31). Per brick column: the volume's own sleep test on the last collected unit map
// (KE / mass < keWake and max |df| < maxDeltaFQuiet), counted in ticks; MOVING = not yet quiet for restTicks, or woken since
// the last call; AWAKE = moving grown by kPartialWakeMargin columns (Chebyshev); the rest FROZEN. A change re-uploads the tables
// (the frozen bit). No awake column: the volume is asleep (nothing steps) until a wake.
void WaterCoreGpu::updatePartialWake(Volume& vol, const SolverParams& params) {
    const size_t nc = static_cast<size_t>(vol.bn.x) * vol.bn.z;
    if (!vol.partialWake || !vol.bricked || !vol.sparse || nc == 0) {
        if (vol.anyFrozen) { std::fill(vol.colFrozen.begin(), vol.colFrozen.end(), 0); vol.anyFrozen = false; vol.tablesDirty = true; }
        vol.awakeCols = vol.movingCols = vol.colCount; vol.frozenCols = 0;
        return;
    }
    if (vol.colQuiet.size() != nc) { vol.colQuiet.assign(nc, 0); vol.colFrozen.assign(nc, 0); vol.anyFrozen = false; }
    if (vol.colWake.size() != nc) vol.colWake.assign(nc, 0);
    if (vol.unitsFresh && vol.hostUnits.size() == nc * vol.nuy) {
        for (int bz = 0; bz < vol.bn.z; ++bz) for (int bx = 0; bx < vol.bn.x; ++bx) {
            double m = 0.0, k = 0.0; float d = 0.0f;
            for (size_t uy = 0; uy < vol.nuy; ++uy) {
                const glm::vec4& q = vol.hostUnits[static_cast<size_t>(bx) + static_cast<size_t>(vol.bn.x) * (uy + vol.nuy * static_cast<size_t>(bz))];
                k += q.x; d = std::max(d, q.y); m += q.z;
            }
            const double qs = static_cast<double>(s_columnQuietScale);   // 26.34: the freeze test's thresholds (1 = the volume's own)
            const size_t ci = static_cast<size_t>(bx) + static_cast<size_t>(vol.bn.x) * bz;
            const double rate = s_freezePerTickForTest ? 1.0 : static_cast<double>(vol.unitsDt) * 60.0;   // 27.26: |df| a tick at this dt
            const bool quiet = m < 1e-3 || (k / m < params.keWake * qs && d < params.maxDeltaFQuiet * qs * rate);
            int& qt = vol.colQuiet[ci];
            const int add = s_freezePerTickForTest ? vol.unitsTicks * kQuietUnitsPerTick
                                                   : std::max(1, static_cast<int>(std::lround(static_cast<double>(vol.unitsTicks) * vol.unitsDt * 600.0)));
            qt = quiet ? std::min(qt + add, 1 << 30) : 0;
        }
        vol.unitsFresh = false;
    }
    // §27 W1b: a RECORDED column holds no cells - it never moves by itself (a whole-volume wake does not unrecord the body); an
    // explicit wake box or the awake margin reaching it asks the manager to seed its tile from the record
    const bool anyRec = vol.anyRecorded && vol.colRecorded.size() == nc;
    auto wantSeed = [&](size_t c) { if (std::find(vol.recordedWakes.begin(), vol.recordedWakes.end(), static_cast<int>(c)) == vol.recordedWakes.end()) vol.recordedWakes.push_back(static_cast<int>(c)); };
    if (vol.wakeAllPending) { std::fill(vol.colQuiet.begin(), vol.colQuiet.end(), 0); vol.wakeAllPending = false; }
    for (size_t c = 0; c < nc; ++c) if (vol.colWake[c]) { vol.colQuiet[c] = 0; vol.colWake[c] = 0; if (anyRec && vol.colRecorded[c]) wantSeed(c); }
    // moving -> awake: grown along x, then along z (separable Chebyshev dilation)
    const int M = kPartialWakeMargin;
    std::vector<char> moving(nc), rowGrown(nc, 0), awake(nc, 0);
    int nMoving = 0;
    const int restTicks = (s_columnRestTicks > 0 ? s_columnRestTicks : params.restTicks);   // 27.14 (1/60 s ticks)
    const long restUnits = static_cast<long>(restTicks) * kQuietUnitsPerTick;   // 27.26: in quiet units
    for (size_t c = 0; c < nc; ++c) { moving[c] = vol.colQuiet[c] < restUnits && !(anyRec && vol.colRecorded[c]); nMoving += moving[c]; }
    for (int bz = 0; bz < vol.bn.z; ++bz) for (int bx = 0; bx < vol.bn.x; ++bx) {
        if (!moving[static_cast<size_t>(bx) + static_cast<size_t>(vol.bn.x) * bz]) continue;
        for (int x = std::max(0, bx - M); x <= std::min(vol.bn.x - 1, bx + M); ++x) rowGrown[static_cast<size_t>(x) + static_cast<size_t>(vol.bn.x) * bz] = 1;
    }
    for (int bz = 0; bz < vol.bn.z; ++bz) for (int bx = 0; bx < vol.bn.x; ++bx) {
        if (!rowGrown[static_cast<size_t>(bx) + static_cast<size_t>(vol.bn.x) * bz]) continue;
        for (int z = std::max(0, bz - M); z <= std::min(vol.bn.z - 1, bz + M); ++z) awake[static_cast<size_t>(bx) + static_cast<size_t>(vol.bn.x) * z] = 1;
    }
    bool changed = false; int nFrozen = 0, nAwake = 0;
    for (size_t c = 0; c < nc; ++c) {
        if (anyRec && vol.colRecorded[c] && !s_recordedOpenForTest) {   // stays frozen (no cells) until its seed lands; the margin reaching it asks for one
            if (awake[c]) wantSeed(c);
            if (!vol.colFrozen[c]) { vol.colFrozen[c] = 1; changed = true; }
            continue;
        }
        const char fz = awake[c] ? 0 : 1;
        if (fz != vol.colFrozen[c]) { vol.colFrozen[c] = fz; changed = true; }
    }
    // counted over the STORED columns (the column table), not the box
    for (const int packed : vol.colEntries) {
        const size_t c = static_cast<size_t>(packed & 1023) + static_cast<size_t>(vol.bn.x) * static_cast<size_t>((packed >> 10) & 1023);
        if (c >= nc) continue;
        if (vol.colFrozen[c]) ++nFrozen; else ++nAwake;
    }
    vol.anyFrozen = nFrozen > 0;
    if (changed) vol.tablesDirty = true;
    vol.awakeCols = nAwake; vol.frozenCols = nFrozen; vol.movingCols = nMoving;
    if (nAwake == 0 && !vol.colEntries.empty()) vol.asleep = true;   // every stored column frozen: nothing to step
}

// staging <- the given slots' cells: fills and faces from `g` (when given), occupancy (out-of-box cells are Solid, as the
// dense grid's out-of-bounds), the bodies' fields. Storage indices ARE the staging offsets (regions sized by storage).
void WaterCoreGpu::scatterSlots(Volume& vol, const std::vector<int>& slots, const WaterGrid* g) const {
    char* st = static_cast<char*>(vol.staging.mapped);
    auto* sf = reinterpret_cast<float*>(st); auto* su = reinterpret_cast<float*>(st + vol.offU);
    auto* sv = reinterpret_cast<float*>(st + vol.offV); auto* sw = reinterpret_cast<float*>(st + vol.offW);
    auto* so = reinterpret_cast<uint32_t*>(st + vol.offOcc); auto* ss = reinterpret_cast<float*>(st + vol.offSolid);
    const glm::ivec3 n = vol.spec.dims;
    for (int slot : slots) {
        const int packed = vol.slotBrick[static_cast<size_t>(slot)];
        const glm::ivec3 b(packed & 1023, (packed >> 10) & 1023, (packed >> 20) & 1023);
        for (int l = 0; l < static_cast<int>(vol.bc); ++l) {
            const glm::ivec3 c = b * vol.bs() + glm::ivec3(l & 7, l >> 6, (l >> 3) & 7) - vol.o;   // local = x + 8 z + 64 y (26.17)
            const size_t i = static_cast<size_t>(slot) * vol.bc + l;
            const bool in = c.x >= 0 && c.y >= 0 && c.z >= 0 && c.x < n.x && c.y < n.y && c.z < n.z;
            const size_t d = in ? static_cast<size_t>(c.x) + static_cast<size_t>(n.x) * (c.y + static_cast<size_t>(n.y) * c.z) : 0;
            so[i] = in ? (d < vol.hostOcc.size() ? vol.hostOcc[d] : 0u) : 1u;
            for (int k = 0; k < 4; ++k) ss[i * 4 + k] = (in && d * 4 + k < vol.hostSolid.size()) ? vol.hostSolid[d * 4 + k] : 0.0f;
            if (g) {
                WaterGrid& gm = const_cast<WaterGrid&>(*g);
                sf[i] = in ? g->f(c.x, c.y, c.z) : 0.0f;
                const bool cyz = c.y >= 0 && c.y < n.y && c.z >= 0 && c.z < n.z, cxz = c.x >= 0 && c.x < n.x && c.z >= 0 && c.z < n.z, cxy = c.x >= 0 && c.x < n.x && c.y >= 0 && c.y < n.y;
                su[i] = (cyz && c.x >= 0 && c.x <= n.x) ? gm.u(c.x, c.y, c.z) : 0.0f;
                sv[i] = (cxz && c.y >= 0 && c.y <= n.y) ? gm.v(c.x, c.y, c.z) : 0.0f;
                sw[i] = (cxy && c.z >= 0 && c.z <= n.z) ? gm.w(c.x, c.y, c.z) : 0.0f;
            }
        }
    }
}

// New slots: zero every per-cell and per-face buffer over them (a brick is born as dry air), then copy their occupancy and
// bodies from staging. Contiguous slots are merged into one range per buffer.
void WaterCoreGpu::recordSlotUploads(VkCommandBuffer cmd, Volume& vol, const std::vector<int>& slots, bool zero) const {
    std::vector<int> s(slots); std::sort(s.begin(), s.end());
    for (size_t a = 0; a < s.size();) {
        size_t e = a + 1;
        while (e < s.size() && s[e] == s[e - 1] + 1) ++e;
        const VkDeviceSize first = static_cast<VkDeviceSize>(s[a]) * vol.bc, count = static_cast<VkDeviceSize>(e - a) * vol.bc;
        if (zero)
            for (Buffer* b : {&vol.f, &vol.fOrig, &vol.fPrev, &vol.u, &vol.v, &vol.w, &vol.uOld, &vol.vOld, &vol.wOld, &vol.src, &vol.liq,
                              &vol.diag, &vol.rhs, &vol.p, &vol.res, &vol.valA, &vol.valB, &vol.knownA, &vol.knownB, &vol.first, &vol.keep,
                              &vol.deposit, &vol.fLo})
                vkCmdFillBuffer(cmd, b->buf, first * 4, count * 4, 0);
        copy(cmd, vol.staging, vol.occ, count * 4, vol.offOcc + first * 4, first * 4);
        copy(cmd, vol.staging, vol.solid, count * 16, vol.offSolid + first * 16, first * 16);
        a = e;
    }
}

// Fragmentation repair (26.6): every dispatch walks slots 1..slotsUsed-1, so bricks left in high slots after the halo shrank
// cost as if the freed ones were still there (the diagonal creek kept 1,184 addressed slots for 592 stored, 2026-10-13).
// When the addressed range exceeds the stored count by a quarter, the highest bricks move into the lowest holes: their
// persistent state (fill, faces, pressure, occupancy, bodies, source rates) is copied on the device, the map and slot table
// follow. Scratch buffers are rebuilt every substep and need no move. Cell values are untouched: storage only.
int WaterCoreGpu::compactSlots(VkCommandBuffer cmd, Volume& vol) {
    const int stored = vol.storedCount;
    if (vol.slotsUsed - 1 <= stored + stored / 4) return 0;
    int moved = 0;
    size_t hole = kFirstSlot, top = static_cast<size_t>(vol.slotsUsed) - 1;
    std::vector<VkBufferCopy> c4, c16;
    while (true) {
        while (hole < top && vol.slotBrick[hole] >= 0) ++hole;
        while (top > hole && vol.slotBrick[top] < 0) --top;
        if (hole >= top) break;
        const int packed = vol.slotBrick[top];
        const glm::ivec3 b(packed & 1023, (packed >> 10) & 1023, (packed >> 20) & 1023);
        vol.map[static_cast<size_t>(b.x) + static_cast<size_t>(vol.bn.x) * (b.y + static_cast<size_t>(vol.bn.y) * b.z)] = static_cast<int>(hole);
        vol.slotBrick[hole] = packed; vol.slotBrick[top] = -1;
        const VkDeviceSize bc = vol.bc;
        VkBufferCopy r{}; r.srcOffset = top * bc * 4; r.dstOffset = hole * bc * 4; r.size = bc * 4; c4.push_back(r);
        r.srcOffset = top * bc * 16; r.dstOffset = hole * bc * 16; r.size = bc * 16; c16.push_back(r);
        ++moved;
    }
    for (Buffer* bf : {&vol.f, &vol.fLo, &vol.u, &vol.v, &vol.w, &vol.p, &vol.occ, &vol.src, &vol.fPrev})
        vkCmdCopyBuffer(cmd, bf->buf, bf->buf, static_cast<uint32_t>(c4.size()), c4.data());
    vkCmdCopyBuffer(cmd, vol.solid.buf, vol.solid.buf, static_cast<uint32_t>(c16.size()), c16.data());
    // the vacated slots go back to dry air (a later allocation zeroes them again; the absent-brick check must never see them)
    for (const VkBufferCopy& r : c4) {
        const VkDeviceSize off = r.srcOffset;
        for (Buffer* bf : {&vol.f, &vol.fLo, &vol.u, &vol.v, &vol.w, &vol.p, &vol.occ, &vol.src, &vol.fPrev}) vkCmdFillBuffer(cmd, bf->buf, off, vol.bc * 4, 0);
        vkCmdFillBuffer(cmd, vol.solid.buf, off * 4, vol.bc * 16, 0);
    }
    int topUsed = 0;
    for (size_t s = kFirstSlot; s < vol.capacity; ++s) if (vol.slotBrick[s] >= 0) topUsed = static_cast<int>(s);
    vol.slotsUsed = std::max(topUsed + 1, kFirstSlot);
    vol.tablesDirty = true;
    return moved;
}

// 26.31 step 2: every awake brick into slots [kFirstSlot, kFirstSlot + A) (A = awake bricks), so the step dispatches and copies
// only those - a frozen brick then costs no thread at all. Bricks are SWAPPED through a scratch buffer (the same per-cell buffers
// compactSlots moves); a frozen brick takes the awake one's old slot. The awake column entries go first in the column table.
int WaterCoreGpu::partitionSlots(VkCommandBuffer cmd, Volume& vol) {
    vol.stepSlots = 0; vol.stepCols = 0;
    if (!vol.anyFrozen || !vol.bricked) return 0;
    const size_t nc = static_cast<size_t>(vol.bn.x) * vol.bn.z;
    if (vol.colFrozen.size() != nc) return 0;
    auto frozenPacked = [&](int packed) { return vol.colFrozen[static_cast<size_t>(packed & 1023) + static_cast<size_t>(vol.bn.x) * static_cast<size_t>((packed >> 20) & 1023)] != 0; };
    int awake = 0;
    for (size_t s = kFirstSlot; s < static_cast<size_t>(vol.slotsUsed); ++s) if (vol.slotBrick[s] >= 0 && !frozenPacked(vol.slotBrick[s])) ++awake;
    const size_t end = kFirstSlot + static_cast<size_t>(awake);
    std::vector<size_t> into, from;   // region slots needing an awake brick; awake bricks outside the region
    for (size_t s = kFirstSlot; s < end; ++s) if (vol.slotBrick[s] < 0 || frozenPacked(vol.slotBrick[s])) into.push_back(s);
    for (size_t s = end; s < static_cast<size_t>(vol.slotsUsed); ++s) if (vol.slotBrick[s] >= 0 && !frozenPacked(vol.slotBrick[s])) from.push_back(s);
    const size_t k = std::min(into.size(), from.size());   // equal by construction
    if (k > 0) {
        // 26.31 step 2: fPrev too - the step refreshes it for the awake bricks only, and the rest test reads |f - fPrev| of every
        // brick (a moved brick without its fPrev read as moving and woke its neighbours: the whole pond awake in 1 s, measured)
        Buffer* per4[9] = {&vol.f, &vol.fLo, &vol.u, &vol.v, &vol.w, &vol.p, &vol.occ, &vol.src, &vol.fPrev};
        const VkDeviceSize bc = vol.bc, brickBytes = bc * (9 * 4 + 16);
        if (vol.scratch.bytes < k * brickBytes) { destroyBuffer(vol.scratch); if (!createBuffer(vol.scratch, k * brickBytes, false, nullptr)) { vol.scratch = Buffer{}; vol.anyFrozen = false; return 0; } }
        // 1. the awake bricks outside the region -> scratch
        for (int b = 0; b < 10; ++b) {
            const Buffer& src = b < 9 ? *per4[b] : vol.solid;
            const VkDeviceSize bytes = b < 9 ? bc * 4 : bc * 16, base = b < 9 ? k * bc * 4 * b : k * bc * 36;
            std::vector<VkBufferCopy> c;
            for (size_t i = 0; i < k; ++i) { VkBufferCopy r{}; r.srcOffset = from[i] * bytes; r.dstOffset = base + i * bytes; r.size = bytes; c.push_back(r); }
            vkCmdCopyBuffer(cmd, src.buf, vol.scratch.buf, static_cast<uint32_t>(c.size()), c.data());
        }
        barrier(cmd);
        // 2. the frozen bricks in the region -> the awake bricks' old slots (a free region slot leaves the old slot dry)
        for (int b = 0; b < 10; ++b) {
            const Buffer& bf = b < 9 ? *per4[b] : vol.solid;
            const VkDeviceSize bytes = b < 9 ? bc * 4 : bc * 16;
            std::vector<VkBufferCopy> c;
            for (size_t i = 0; i < k; ++i) {
                if (vol.slotBrick[into[i]] >= 0) { VkBufferCopy r{}; r.srcOffset = into[i] * bytes; r.dstOffset = from[i] * bytes; r.size = bytes; c.push_back(r); }
                else vkCmdFillBuffer(cmd, bf.buf, from[i] * bytes, bytes, 0);
            }
            if (!c.empty()) vkCmdCopyBuffer(cmd, bf.buf, bf.buf, static_cast<uint32_t>(c.size()), c.data());
        }
        barrier(cmd);
        // 3. scratch -> the region
        for (int b = 0; b < 10; ++b) {
            const Buffer& dst = b < 9 ? *per4[b] : vol.solid;
            const VkDeviceSize bytes = b < 9 ? bc * 4 : bc * 16, base = b < 9 ? k * bc * 4 * b : k * bc * 36;
            std::vector<VkBufferCopy> c;
            for (size_t i = 0; i < k; ++i) { VkBufferCopy r{}; r.srcOffset = base + i * bytes; r.dstOffset = into[i] * bytes; r.size = bytes; c.push_back(r); }
            vkCmdCopyBuffer(cmd, vol.scratch.buf, dst.buf, static_cast<uint32_t>(c.size()), c.data());
        }
        barrier(cmd);
        // the host's map and slot table follow
        for (size_t i = 0; i < k; ++i) {
            const int a = vol.slotBrick[from[i]], z = vol.slotBrick[into[i]];
            auto mapOf = [&](int packed) -> int& { return vol.map[static_cast<size_t>(packed & 1023) + static_cast<size_t>(vol.bn.x) * (static_cast<size_t>((packed >> 10) & 1023) + static_cast<size_t>(vol.bn.y) * static_cast<size_t>((packed >> 20) & 1023))]; };
            vol.slotBrick[into[i]] = a; mapOf(a) = static_cast<int>(into[i]);
            vol.slotBrick[from[i]] = z; if (z >= 0) mapOf(z) = static_cast<int>(from[i]);
        }
        int topUsed = 0;
        for (size_t s = kFirstSlot; s < vol.capacity; ++s) if (vol.slotBrick[s] >= 0) topUsed = static_cast<int>(s);
        vol.slotsUsed = std::max(topUsed + 1, kFirstSlot);
        vol.tablesDirty = true;
    }
    // the column table: awake entries first (column data - births, residue, the surface - is rebuilt every call)
    std::stable_partition(vol.colEntries.begin(), vol.colEntries.end(), [&](int packed) {
        return vol.colFrozen[static_cast<size_t>(packed & 1023) + static_cast<size_t>(vol.bn.x) * static_cast<size_t>((packed >> 10) & 1023)] == 0; });
    int awakeCols = 0;
    for (int packed : vol.colEntries) if (!vol.colFrozen[static_cast<size_t>(packed & 1023) + static_cast<size_t>(vol.bn.x) * static_cast<size_t>((packed >> 10) & 1023)]) ++awakeCols;
    vol.tablesDirty = true;
    vol.stepSlots = static_cast<int>(end); vol.stepCols = awakeCols;
    vol.lastPartitionMoves = static_cast<int>(k);
    return static_cast<int>(k);
}

void WaterCoreGpu::upload(Volume& vol, const WaterGrid& g) {
    using UClock = std::chrono::steady_clock;   // 39.8: the parts' times (m_lastUploadSplit)
    const auto u0 = UClock::now();
    UClock::time_point u2, u3, u4, u5, u6;
    vol.unitsFullNext = true;   // 27.23 W1e: this writes stored cells, frozen ones included - the next reduction computes every unit
    settle(vol);   // 26.16
    const auto u1 = UClock::now();
    // 27.22: the grid is ordinary cells - no tall cells from here (the heights region is zeroed with the counters below) until asked
    // again. FIRST: with the tall state still set, the residency below read every brick under the floor as ground and the grid's
    // water there was never stored - each write to a tall pond deleted one brick layer of it (27.22d, 128 m^3 in a 24 x 24 m pond)
    if (!s_uploadKeepsTallForTest) { vol.tallActive = false; vol.tallPending = 0; vol.tallY = -1; vol.tallH.clear(); vol.tallColumns = 0; }
    vol.surfaceValid = false;   // Phase F: the staging field predates this grid
    vol.haveLast = false;       // 26.16
    WaterGrid& gm = const_cast<WaterGrid&>(g);
    auto* st = static_cast<char*>(vol.staging.mapped);
    float mx = 0.0f;
    for (float v : gm.uData()) mx = std::max(mx, std::abs(v));
    for (float v : gm.vData()) mx = std::max(mx, std::abs(v));
    for (float v : gm.wData()) mx = std::max(mx, std::abs(v));
    vol.lastMaxSpeed = mx;
    vol.hostOcc.resize(vol.dCells);
    vol.hostSolidTop.assign(vol.dColumns, -1e30f);   // 26.11: a dry column's record is rebuilt from this (wc_surface's solidTop, whole column)
    vol.solidTopDirty = true;   // 29 S1: the device's copy is re-sent
    for (int z = 0; z < g.nz(); ++z) for (int x = 0; x < g.nx(); ++x) for (int y = g.ny() - 1; y >= 0; --y)
        if (g.occ(x, y, z) == Occ::Solid) { vol.hostSolidTop[static_cast<size_t>(x) + static_cast<size_t>(g.nx()) * z] = (static_cast<float>(g.spec().origin.y) + static_cast<float>(y) + 1.0f) * g.spec().h; break; }
    vol.listsSized = false;
    for (int z = 0; z < g.nz(); ++z) for (int y = 0; y < g.ny(); ++y) for (int x = 0; x < g.nx(); ++x)
        vol.hostOcc[g.idx(x, y, z)] = static_cast<uint32_t>(g.occ(x, y, z));
    u2 = u3 = u4 = u5 = u6 = UClock::now();
    VkCommandBuffer cmd = beginCommands();
    if (vol.bricked) {
        // residency from the grid: dense - every brick; sparse - the water and one full ring round it (the first call's reach is
        // not known yet; a ring of 8 cells is more than any one tick moves at the CFL the step will use)
        std::vector<char> need(vol.mapCount, vol.sparse ? 0 : 1);
        if (vol.sparse) {
            needFromDenseFill(vol, gm.fData(), need, kB);
            for (int c : vol.sourceCells) {
                if (c < 0 || static_cast<size_t>(c) >= vol.dCells) continue;
                glm::ivec3 b; int dist[6];
                cellToRecord(vol.o, vol.bs(), c % vol.spec.dims.x, (c / vol.spec.dims.x) % vol.spec.dims.y, c / (vol.spec.dims.x * vol.spec.dims.y), b, dist);
                need[static_cast<size_t>(b.x) + static_cast<size_t>(vol.bn.x) * (b.y + static_cast<size_t>(vol.bn.y) * b.z)] = 2;
                markNeed(vol, need, b, dist, kB);
            }
            if (!vol.hostDeposit.empty()) needFromDenseFill(vol, vol.hostDeposit, need, kB);
        }
        const auto ur1 = UClock::now();   // 39.8: the residency's parts
        // 26.18: which bricks are all ground - from THIS upload's occupancy (an upload follows every occupancy change: a dig
        // opens its brick here, before the next step reads it). Out-of-box cells count as Solid (scatterSlots writes them so).
        if (vol.groundOn) {
            vol.allSolid.assign(vol.mapCount, 1);
            const glm::ivec3 n = vol.spec.dims;
            for (int z = 0; z < n.z; ++z) for (int y = 0; y < n.y; ++y) for (int x = 0; x < n.x; ++x) {
                if (vol.hostOcc[g.idx(x, y, z)] == static_cast<uint32_t>(Occ::Solid)) continue;
                const glm::ivec3 b = (glm::ivec3(x, y, z) + vol.o) / vol.bs();
                vol.allSolid[static_cast<size_t>(b.x) + static_cast<size_t>(vol.bn.x) * (b.y + static_cast<size_t>(vol.bn.y) * b.z)] = 0;
            }
        }
        const auto ur2 = UClock::now();
        std::fill(vol.map.begin(), vol.map.end(), 0);
        std::fill(vol.slotBrick.begin(), vol.slotBrick.end(), -1);
        vol.storedCount = 0; vol.colEntries.clear();
        ensureCapacity(vol, need);   // 27.23 W1d.1 (the command buffer above holds nothing yet)
        st = static_cast<char*>(vol.staging.mapped);   // a grow replaced the staging buffer (the pointer taken above is freed)
        const auto ur3 = UClock::now();
        applyResidency(vol, need);
        u3 = UClock::now();
        {   auto rms = [](UClock::time_point a, UClock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
            m_lastResidencySplit = {rms(u2, ur1), rms(ur1, ur2), rms(ur2, ur3), rms(ur3, u3)}; }
        vol.unstoredM3 = 0.0;   // water in a brick the capacity could not store: counted (createVolume refuses this up front)
        for (int z = 0; z < g.nz(); ++z) for (int y = 0; y < g.ny(); ++y) for (int x = 0; x < g.nx(); ++x)
            if (g.f(x, y, z) != 0.0f && storageIndex(vol, x, y, z) < 0) vol.unstoredM3 += static_cast<double>(g.f(x, y, z)) * g.cellVolume();
        std::memset(st, 0, static_cast<size_t>(vol.offOcc + vol.cells * 4));   // f, u, v, w, occ regions
        std::memset(st + vol.offSolid, 0, vol.cells * 16);
        if (!vol.groundAsAir) { auto* so = reinterpret_cast<uint32_t*>(st + vol.offOcc); std::fill(so + kGround * vol.bc, so + (kGround + 1) * vol.bc, 1u); }   // 26.18: the ground sentinel
        std::vector<int> all;
        for (size_t s = kFirstSlot; s < vol.capacity; ++s) if (vol.slotBrick[s] >= 0) all.push_back(static_cast<int>(s));
        u4 = UClock::now();
        scatterSlots(vol, all, &g);
        u5 = UClock::now();
        writeTablesToStaging(vol);
        u6 = UClock::now();
        for (Buffer* b : {&vol.fOrig, &vol.uOld, &vol.vOld, &vol.wOld, &vol.liq, &vol.diag, &vol.rhs, &vol.res, &vol.valA, &vol.valB,
                          &vol.knownA, &vol.knownB, &vol.first, &vol.keep, &vol.deposit})
            vkCmdFillBuffer(cmd, b->buf, 0, b->bytes, 0);
        copy(cmd, vol.staging, vol.solid, vol.cells * 16, vol.offSolid);
        copy(cmd, vol.staging, vol.bricks, vol.counterOff * 4, vol.offBricks);
        vkCmdFillBuffer(cmd, vol.bricks.buf, vol.counterOff * 4, (vol.brickInts - vol.counterOff) * 4, 0);
        vol.recordValid = false; vol.tablesDirty = false;
    } else {
        std::memcpy(st, gm.fData().data(), vol.cells * sizeof(float));
        std::memcpy(st + vol.offU, gm.uData().data(), vol.nu * sizeof(float));
        std::memcpy(st + vol.offV, gm.vData().data(), vol.nv * sizeof(float));
        std::memcpy(st + vol.offW, gm.wData().data(), vol.nw * sizeof(float));
        std::memcpy(st + vol.offOcc, vol.hostOcc.data(), vol.cells * 4);
    }
    copy(cmd, vol.staging, vol.f, vol.cells * sizeof(float));
    copy(cmd, vol.staging, vol.fPrev, vol.cells * sizeof(float));
    copy(cmd, vol.staging, vol.u, vol.nu * sizeof(float), vol.offU);
    copy(cmd, vol.staging, vol.v, vol.nv * sizeof(float), vol.offV);
    copy(cmd, vol.staging, vol.w, vol.nw * sizeof(float), vol.offW);
    copy(cmd, vol.staging, vol.occ, vol.cells * 4, vol.offOcc);
    vkCmdFillBuffer(cmd, vol.src.buf, 0, vol.src.bytes, 0);
    vkCmdFillBuffer(cmd, vol.p.buf, 0, vol.p.bytes, 0);   // 26.5: a replaced state starts its solve fresh (deterministic, as a new volume)
    vkCmdFillBuffer(cmd, vol.fLo.buf, 0, vol.fLo.bytes, 0);   // 26.10: the remainders belonged to the water that was there
    if (!vol.bricked) u3 = u4 = u5 = u6 = UClock::now();
    submitAndWait(cmd);
    {   auto ms = [](UClock::time_point a, UClock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
        long stored = 0; for (int sb : vol.slotBrick) if (sb >= 0) ++stored;
        m_lastUploadSplit = {ms(u0, u1), ms(u1, u2), ms(u2, u3), ms(u3, u4), ms(u4, u5), ms(u5, u6), ms(u6, UClock::now()), stored}; }
    vol.tallActive = false; vol.tallPending = 0; vol.tallY = -1; vol.tallH.clear(); vol.tallColumns = 0;   // (the red control's late reset)
}

void WaterCoreGpu::queueMomentum(Volume& vol, const glm::vec3& pos, float radius, const glm::vec3& momentum, float maxSpeed) {
    if (vol.pendingMomentum.size() / 8 >= static_cast<size_t>(kMaxMomentum)) { ++vol.momentumDropped; return; }
    const glm::vec3 local = pos - glm::vec3(vol.spec.origin) * vol.spec.h;   // the kernel works in metres from the grid's origin
    const float r[8] = {local.x, local.y, local.z, radius, momentum.x, momentum.y, momentum.z, maxSpeed};
    vol.pendingMomentum.insert(vol.pendingMomentum.end(), r, r + 8);
    vol.asleep = false; vol.quietTicks = 0; vol.quietRef = 0.0;   // moving water wakes, as the CPU path's re-upload did
    {   // 26.31: the sphere's columns (the margin grows round them)
        const float h = vol.spec.h;
        wakeBox(vol, glm::ivec3(glm::floor((local - glm::vec3(radius)) / h)), glm::ivec3(glm::floor((local + glm::vec3(radius)) / h)));
    }
}

void WaterCoreGpu::queueImpulse(Volume& vol, const glm::vec3& worldPos, float radius, const glm::vec3& deltaV) {
    if (vol.pendingMomentum.size() / 8 >= static_cast<size_t>(kMaxMomentum)) { ++vol.momentumDropped; return; }
    const glm::vec3 local = worldPos - glm::vec3(vol.spec.origin) * vol.spec.h;
    const float r[8] = {local.x, local.y, local.z, radius, deltaV.x, deltaV.y, deltaV.z, -1.0f};
    vol.pendingMomentum.insert(vol.pendingMomentum.end(), r, r + 8);
    vol.asleep = false; vol.quietTicks = 0; vol.quietRef = 0.0;
    const float h = vol.spec.h;
    wakeBox(vol, glm::ivec3(glm::floor((local - glm::vec3(radius)) / h)), glm::ivec3(glm::floor((local + glm::vec3(radius)) / h)));
}

void WaterCoreGpu::queueRadialImpulse(Volume& vol, const glm::vec3& worldCentre, float reach, float speedAtCentre, float upBias, float maxSpeed) {
    if (vol.pendingMomentum.size() / 8 >= static_cast<size_t>(kMaxMomentum)) { ++vol.momentumDropped; return; }
    const glm::vec3 local = worldCentre - glm::vec3(vol.spec.origin) * vol.spec.h;
    const float r[8] = {local.x, local.y, local.z, reach, speedAtCentre, upBias, maxSpeed, -2.0f};
    vol.pendingMomentum.insert(vol.pendingMomentum.end(), r, r + 8);
    vol.asleep = false; vol.quietTicks = 0; vol.quietRef = 0.0;
    const float h = vol.spec.h;
    wakeBox(vol, glm::ivec3(glm::floor((local - glm::vec3(reach)) / h)), glm::ivec3(glm::floor((local + glm::vec3(reach)) / h)));
}

int WaterCoreGpu::applyQueuedMomentumForTest(Volume& vol) {
    settle(vol);
    const size_t n = vol.pendingMomentum.size() / 8;
    if (n == 0) return 0;
    WcPush push; push.nx = vol.spec.dims.x; push.ny = vol.spec.dims.y; push.nz = vol.spec.dims.z; push.h = vol.spec.h;
    push.thr = 0.5f; push.gravity = 9.81f;
    fillPushBricks(vol, push);
    std::memcpy(vol.momentum.mapped, vol.pendingMomentum.data(), vol.pendingMomentum.size() * sizeof(float));
    VkCommandBuffer cmd = beginCommands();
    barrier(cmd);
    for (size_t i = 0; i < n; ++i) { push.iparam = static_cast<int32_t>(i); dispatch(cmd, *vol.pipes[Momentum], push, 64); }
    submitAndWait(cmd);
    vol.momentumApplied += static_cast<long>(n);
    vol.pendingMomentum.clear();
    return static_cast<int>(n);
}

bool WaterCoreGpu::setSources(Volume& vol, const std::vector<GpuSource>& sources, std::string* err) {
    settle(vol);   // 26.16
    if (sources.size() > static_cast<size_t>(kMaxSources)) { if (err) *err = "a GPU volume holds at most " + std::to_string(kMaxSources) + " sources"; return false; }
    auto* st = static_cast<char*>(vol.staging.mapped) + vol.offSources;
    std::memset(st, 0, kMaxSources * sizeof(GpuSource));
    if (!sources.empty()) std::memcpy(st, sources.data(), sources.size() * sizeof(GpuSource));
    vol.sourceCount = static_cast<int>(sources.size());
    vol.hostSources = sources;   // 26.16: readSources answers from this until a call is collected
    vol.sourceCells.clear();
    for (const GpuSource& s : sources) vol.sourceCells.push_back(s.cell);   // 26.6: a pump's brick is always stored
    if (!sources.empty()) wakeAll(vol);   // 26.13: a pump added to a sleeping (e.g. empty) volume must run it - it never woke (the creek bench)
    VkCommandBuffer cmd = beginCommands();
    copy(cmd, vol.staging, vol.sources, kMaxSources * sizeof(GpuSource), vol.offSources);
    vkCmdFillBuffer(cmd, vol.src.buf, 0, vol.src.bytes, 0);   // the projection's per-cell rates are rebuilt by the kernel
    submitAndWait(cmd);
    return true;
}

void WaterCoreGpu::setSolids(Volume& vol, const std::vector<float>& s, const std::vector<float>& wake, const std::vector<uint8_t>& fresh, float frameSeconds) {
    settle(vol);   // 26.16
    bool any = false;
    for (size_t i = 0; i < vol.dCells && !any; ++i) any = (i < s.size() && s[i] > 0.0f) || (i < wake.size() && wake[i] > 0.0f);
    if (!any && !vol.hasSolids) return;   // nothing there and nothing was: the buffer is already zero
    vol.solidLowY = 1 << 30;   // 27.22e
    if (any) for (size_t i = 0; i < vol.dCells; ++i)
        if ((i < s.size() && s[i] > 0.0f) || (i < wake.size() && wake[i] > 0.0f)) vol.solidLowY = std::min(vol.solidLowY, static_cast<int>((i / static_cast<size_t>(vol.spec.dims.x)) % static_cast<size_t>(vol.spec.dims.y)));
    vol.hostSolid.assign(vol.dCells * 4, 0.0f);
    for (size_t i = 0; i < vol.dCells; ++i) {
        vol.hostSolid[i * 4 + 0] = i < s.size() ? s[i] : 0.0f;
        vol.hostSolid[i * 4 + 1] = 0.0f;   // q: computed on the device from its own fill (wc_classify mode 1)
        vol.hostSolid[i * 4 + 2] = i < wake.size() ? wake[i] : 0.0f;
        vol.hostSolid[i * 4 + 3] = (i < fresh.size() && fresh[i]) ? 1.0f : 0.0f;
    }
    auto* st = reinterpret_cast<float*>(static_cast<char*>(vol.staging.mapped) + vol.offSolid);
    if (vol.bricked) {
        std::memset(st, 0, vol.cells * 16);
        std::vector<int> all;
        for (size_t sl = kFirstSlot; sl < vol.capacity; ++sl) if (vol.slotBrick[sl] >= 0) all.push_back(static_cast<int>(sl));
        // scatter the solid field only (occupancy staging is rewritten from hostOcc - unchanged)
        scatterSlots(vol, all, nullptr);
    } else {
        std::memcpy(st, vol.hostSolid.data(), vol.cells * 16);
    }
    VkCommandBuffer cmd = beginCommands();
    copy(cmd, vol.staging, vol.solid, vol.cells * 16, vol.offSolid);
    submitAndWait(cmd);
    if (!any) vol.hostSolid.clear();
    vol.hasSolids = any;
    vol.solidRatesPending = any;
    vol.solidFrame = std::max(frameSeconds, 1e-4f);   // T = 0 would ask for an infinite rate
    wakeAll(vol);           // a body is there (or just left): the water must move
}

void WaterCoreGpu::setSolidsBox(Volume& vol, const std::vector<float>& s, const std::vector<float>& wake, const std::vector<uint8_t>& fresh, float frameSeconds,
                                const glm::ivec3& lo, const glm::ivec3& hi) {
    settle(vol);   // 26.16
    const glm::ivec3 n = vol.spec.dims;
    const bool emptyBox = hi.x < lo.x || hi.y < lo.y || hi.z < lo.z;
    bool any = false;   // outside the box everything is 0 (the caller's contract), so the box decides
    if (!emptyBox) for (int z = lo.z; z <= hi.z && !any; ++z) for (int y = lo.y; y <= hi.y && !any; ++y) for (int x = lo.x; x <= hi.x && !any; ++x) {
        const size_t i = static_cast<size_t>(x) + static_cast<size_t>(n.x) * (y + static_cast<size_t>(n.y) * z);
        any = (i < s.size() && s[i] > 0.0f) || (i < wake.size() && wake[i] > 0.0f);
    }
    if (!any && !vol.hasSolids) return;   // nothing there and nothing was: the buffer is already zero
    if (!vol.bricked) { setSolids(vol, s, wake, fresh, frameSeconds); return; }   // the dense (FLIP) layout: the whole upload
    vol.solidLowY = 1 << 30;   // 27.22e: the box holds every body (outside it all is 0 - the caller's contract)
    if (any) for (int y = lo.y; y <= hi.y && vol.solidLowY == (1 << 30); ++y) for (int z = lo.z; z <= hi.z; ++z) for (int x = lo.x; x <= hi.x; ++x) {
        const size_t i = static_cast<size_t>(x) + static_cast<size_t>(n.x) * (y + static_cast<size_t>(n.y) * z);
        if ((i < s.size() && s[i] > 0.0f) || (i < wake.size() && wake[i] > 0.0f)) { vol.solidLowY = y; break; }
    }
    if (vol.hostSolid.size() != vol.dCells * 4) vol.hostSolid.assign(vol.dCells * 4, 0.0f);
    if (!emptyBox) for (int z = lo.z; z <= hi.z; ++z) for (int y = lo.y; y <= hi.y; ++y) for (int x = lo.x; x <= hi.x; ++x) {
        const size_t i = static_cast<size_t>(x) + static_cast<size_t>(n.x) * (y + static_cast<size_t>(n.y) * z);
        vol.hostSolid[i * 4 + 0] = i < s.size() ? s[i] : 0.0f;
        vol.hostSolid[i * 4 + 1] = 0.0f;   // q: computed on the device from its own fill (wc_classify mode 1)
        vol.hostSolid[i * 4 + 2] = i < wake.size() ? wake[i] : 0.0f;
        vol.hostSolid[i * 4 + 3] = (i < fresh.size() && fresh[i]) ? 1.0f : 0.0f;
    }
    std::vector<int> slots;   // the stored bricks the box touches (an absent brick takes its bodies from hostSolid when it is stored)
    if (!emptyBox) {
        const glm::ivec3 b0 = (lo + vol.o) / vol.bs(), b1 = (hi + vol.o) / vol.bs();
        for (int bz = b0.z; bz <= b1.z; ++bz) for (int by = b0.y; by <= b1.y; ++by) for (int bx = b0.x; bx <= b1.x; ++bx) {
            if (bx < 0 || by < 0 || bz < 0 || bx >= vol.bn.x || by >= vol.bn.y || bz >= vol.bn.z) continue;
            const int slot = vol.map[static_cast<size_t>(bx) + static_cast<size_t>(vol.bn.x) * (by + static_cast<size_t>(vol.bn.y) * bz)];
            if (slot >= kFirstSlot) slots.push_back(slot);   // 26.18: never the sentinels
        }
    }
    std::sort(slots.begin(), slots.end());
    if (!slots.empty()) {
        scatterSlots(vol, slots, nullptr);
        VkCommandBuffer cmd = beginCommands();
        for (size_t a = 0; a < slots.size();) {   // contiguous slots in one copy
            size_t e = a + 1;
            while (e < slots.size() && slots[e] == slots[e - 1] + 1) ++e;
            const VkDeviceSize first = static_cast<VkDeviceSize>(slots[a]) * vol.bc * 16, bytes = static_cast<VkDeviceSize>(e - a) * vol.bc * 16;
            copy(cmd, vol.staging, vol.solid, bytes, vol.offSolid + first, first);
            a = e;
        }
        submitAndWait(cmd);
    }
    vol.hasSolids = any;
    vol.solidRatesPending = any;
    vol.solidFrame = std::max(frameSeconds, 1e-4f);   // T = 0 would ask for an infinite rate
    // a body is there (or just left): the water must move. 26.31: with the partial wake, the box's columns (it holds the bodies and
    // the cells they left - the caller's contract); a stone in the pond woke all of it every frame it moved (measured: 1,664 of
    // 1,664 columns awake for the whole 15 s of the owner's video take)
    if (vol.partialWake) { if (!emptyBox) wakeBox(vol, lo, hi); else { vol.asleep = false; vol.quietTicks = 0; vol.quietRef = 0.0; } }
    else wakeAll(vol);
}

void WaterCoreGpu::setDeposits(Volume& vol, const std::vector<float>& cellFractions) {
    if (cellFractions.size() != vol.dCells || !vol.staging.mapped) return;
    if (vol.depositPending && vol.hostDeposit.size() == vol.dCells) { for (size_t i = 0; i < vol.dCells; ++i) vol.hostDeposit[i] += cellFractions[i]; }   // two frames' deposits before a step: add
    else vol.hostDeposit = cellFractions;
    vol.depositPending = true;
    // water arrived: the volume must step. 26.31: with the partial wake only the columns it landed in wake (the caller's wakeBox per
    // landing; a stone's splash raining back woke the whole pond 1 s after the stone, measured) - without it, the whole volume
    if (vol.partialWake) { vol.asleep = false; vol.quietTicks = 0; vol.quietRef = 0.0; } else wakeAll(vol);
}

bool WaterCoreGpu::readBirths(Volume& vol, std::vector<float>& out) const {
    if (!vol.birthsFresh || !vol.staging.mapped) return false;
    vol.birthsFresh = false;
    out.assign(vol.dColumns * 8, 0.0f);
    // 26.11: the packed list - 8 floats per column with births, its dense index in slot 7 (unused in the record: written back as 0)
    const auto* st = reinterpret_cast<const float*>(vol.hostBirths.data());   // 26.16: the collected call's copy
    for (uint32_t k = 0; k < vol.hostCount[1]; ++k) {
        uint32_t c; std::memcpy(&c, st + k * 8 + 7, 4);
        if (c < vol.dColumns) { std::memcpy(&out[static_cast<size_t>(c) * 8], st + k * 8, 28); }
    }
    return true;
}

bool WaterCoreGpu::readBirthRecords(Volume& vol, std::vector<float>& out) const {
    if (!vol.birthsFresh || !vol.staging.mapped) return false;
    vol.birthsFresh = false;
    const auto* st = reinterpret_cast<const float*>(vol.hostBirths.data());   // 26.16: the collected call's copy
    out.assign(st, st + static_cast<size_t>(vol.hostCount[1]) * 8);
    return true;
}

bool WaterCoreGpu::readUnits(const Volume& vol, std::vector<glm::vec4>& out, glm::ivec3& dims, glm::ivec3& cellOffset) const {
    if (!vol.bricked || vol.hostUnits.empty()) return false;
    out = vol.hostUnits;
    dims = glm::ivec3(vol.bn.x, static_cast<int>(vol.nuy), vol.bn.z);
    cellOffset = -vol.o;
    return true;
}

bool WaterCoreGpu::readSurface(const Volume& vol, float* out, bool keep) const {
    if (!vol.surfaceValid || !vol.staging.mapped) return false;
    // 26.33: a call that covered only its awake columns updates those and keeps the rest of `out` - which must be this volume's
    // field as of the call before (the caller's last read: `keep`, and no collected call skipped)
    const bool partial = !vol.hostSurfCols.empty();
    if (partial && (!keep || vol.surfReadAt + 1 < vol.surfCollected)) return false;
    vol.surfReadAt = vol.surfCollected;
    // 26.11: every column starts dry - no water, ground at the host's solid top (a pure function of the grid: a bricked volume reads
    // what the dense box reads, absent bricks included) - and the packed wet columns are scattered over it
    // 26.30 step 3: one pass over the columns - each wet column is written whole and marked, then only the unmarked (dry) ones are
    // reset (it reset all, then wrote the wet ones over: twice the 4.6 MB of a 32 x 28 m pond, every step)
    auto* cols = reinterpret_cast<Core::Water::SurfaceColumn*>(out);
    auto solidTop = [&](size_t c) { return c < vol.hostSolidTop.size() ? vol.hostSolidTop[c] : -1e30f; };
    std::vector<uint8_t> wet(vol.dColumns, 0);
    const char* st = vol.hostSurf.data();   // 26.16: the collected call's copy: [one-run records | multi-run records]
    for (uint32_t k = 0; k < vol.hostCount[0]; ++k) {   // one run: top, bottom, u, w, body, -, -, column
        float r[8]; std::memcpy(r, st + static_cast<size_t>(k) * 32, 32);
        uint32_t c; std::memcpy(&c, &r[7], 4);
        if (c >= vol.dColumns) continue;
        Core::Water::SurfaceColumn o;
        o.solidTopY = solidTop(c);
        o.top[0] = r[0]; o.bottom[0] = r[1]; o.runs = 1.0f; o.u = r[2]; o.w = r[3]; o.bodyAtSurface = r[4];
        cols[c] = o; wet[c] = 1;
    }
    const char* sm = st + static_cast<size_t>(vol.hostCount[0]) * 32;
    for (uint32_t k = 0; k < vol.hostCount[2]; ++k) {   // several runs: the full record, the column in pad2
        Core::Water::SurfaceColumn rec; std::memcpy(&rec, sm + static_cast<size_t>(k) * sizeof(rec), sizeof(rec));
        uint32_t c; std::memcpy(&c, &rec.pad2, 4);
        if (c >= vol.dColumns) continue;
        rec.pad2 = 0.0f; rec.solidTopY = solidTop(c);
        cols[c] = rec; wet[c] = 1;
    }
    if (partial && !s_partialSurfaceResetAllForTest) {   // 26.33: only the covered columns that are dry now are reset; a frozen column keeps what it was
        ++s_partialSurfaceReads;
        const int nx = vol.spec.dims.x, nz = vol.spec.dims.z;
        for (int e : vol.hostSurfCols) {
            const int bx = e & 1023, bz = (e >> 10) & 1023;
            for (int lz = 0; lz < 8; ++lz) for (int lx = 0; lx < 8; ++lx) {
                const int x = bx * 8 + lx - vol.o.x, z = bz * 8 + lz - vol.o.z;
                if (x < 0 || z < 0 || x >= nx || z >= nz) continue;
                const size_t c = static_cast<size_t>(x) + static_cast<size_t>(nx) * static_cast<size_t>(z);
                if (c < vol.dColumns && !wet[c]) { cols[c] = Core::Water::SurfaceColumn{}; cols[c].solidTopY = solidTop(c); }
            }
        }
        return true;
    }
    for (size_t c = 0; c < vol.dColumns; ++c)
        if (!wet[c]) { cols[c] = Core::Water::SurfaceColumn{}; cols[c].solidTopY = solidTop(c); }
    return true;
}

void WaterCoreGpu::queueSurfaceColumns(Volume& vol, const Core::Water::WaterSurfaceField& field, int x0, int z0, int x1, int z1) {
    if (!vol.bricked || field.nx != vol.spec.dims.x || field.nz != vol.spec.dims.z || field.cols.size() != vol.dColumns) return;
    x0 = std::max(x0, 0); z0 = std::max(z0, 0); x1 = std::min(x1, field.nx); z1 = std::min(z1, field.nz);
    for (int z = z0; z < z1; ++z) for (int x = x0; x < x1; ++x) {
        const uint32_t c = static_cast<uint32_t>(x + field.nx * z);
        vol.surfPending.push_back({c, field.cols[c]});
    }
}

WaterCoreGpu::SurfaceView WaterCoreGpu::surfaceView(const Volume& vol) const {
    SurfaceView v;
    if (!s_gpuSurface || !vol.bricked || !vol.meshValid) return v;
    v.surf = vol.surfDense.buf; v.surfBytes = vol.surfDense.bytes; v.corners = vol.meshCorners.buf; v.cornersBytes = vol.meshCorners.bytes;
    v.meta = vol.meshMeta.buf; v.metaBytes = vol.meshMeta.bytes; v.list = vol.meshList.buf; v.listBytes = vol.meshList.bytes; v.args = vol.meshArgs.buf;
    v.origin = vol.spec.origin; v.nx = vol.spec.dims.x; v.nz = vol.spec.dims.z; v.h = vol.spec.h; v.serial = vol.serial; v.ok = true;
    return v;
}

bool WaterCoreGpu::expandSurfaceForTest(Volume& vol, std::vector<float>& vertices) {
    vertices.clear();
    settle(vol);
    if (!s_gpuSurface || !vol.bricked || !vol.meshValid) return false;
    const uint32_t entries = std::min(vol.lastMeshEntries, vol.meshListCap);
    const size_t quadsMax = vol.dColumns + entries + 16;
    Buffer out, count;
    if (!createBuffer(out, quadsMax * 4 * 12 * 4, true, nullptr) || !createBuffer(count, 16, true, nullptr)) { destroyBuffer(out); destroyBuffer(count); return false; }
    Vulkan::ComputePipeline& p = *vol.pipes[SurfaceMesh];
    p.bindBuffer(6, out.buf, out.bytes); p.bindBuffer(7, count.buf, count.bytes); p.updateDescriptors();
    WcPush mp{}; mp.nx = vol.spec.dims.x; mp.ny = vol.spec.dims.y; mp.nz = vol.spec.dims.z; mp.h = vol.spec.h; mp.mode = 2;
    mp.param0 = static_cast<float>(vol.spec.origin.x); mp.param1 = static_cast<float>(vol.spec.origin.z); mp.iparam = 0;   // the ripple layer + 1: none
    fillPushBricks(vol, mp);
    VkCommandBuffer cmd = beginCommands();
    dispatch(cmd, p, mp, vol.dColumns + entries);
    submitAndWait(cmd);
    const uint32_t q = std::min<uint32_t>(*static_cast<const uint32_t*>(count.mapped), static_cast<uint32_t>(quadsMax));
    const auto* v = static_cast<const float*>(out.mapped);
    vertices.assign(v, v + static_cast<size_t>(q) * 4 * 12);
    p.bindBuffer(6, vol.meshList.buf, vol.meshList.bytes); p.bindBuffer(7, vol.meshArgs.buf, vol.meshArgs.bytes); p.updateDescriptors();
    destroyBuffer(out); destroyBuffer(count);
    return true;
}

void WaterCoreGpu::readSources(Volume& vol, std::vector<GpuSource>& out) const {
    // 26.16: the call copies the sources out with its other results; the copy taken when it was collected (or the set list)
    out = vol.hostSources;
    out.resize(static_cast<size_t>(vol.sourceCount));
}

void WaterCoreGpu::download(Volume& vol, WaterGrid& g) const {
    const_cast<WaterCoreGpu*>(this)->settle(vol);   // 26.16
    VkCommandBuffer cmd = beginCommands();
    copy(cmd, vol.f, vol.staging, liveCells(vol) * sizeof(float));
    copy(cmd, vol.u, vol.staging, liveLattice(vol, 0) * sizeof(float), 0, vol.offU);
    copy(cmd, vol.v, vol.staging, liveLattice(vol, 1) * sizeof(float), 0, vol.offV);
    copy(cmd, vol.w, vol.staging, liveLattice(vol, 2) * sizeof(float), 0, vol.offW);
    submitAndWait(cmd);
    const auto* st = static_cast<const char*>(vol.staging.mapped);
    if (!vol.bricked) {
        std::memcpy(g.fData().data(), st, vol.cells * sizeof(float));
        std::memcpy(g.uData().data(), st + vol.offU, vol.nu * sizeof(float));
        std::memcpy(g.vData().data(), st + vol.offV, vol.nv * sizeof(float));
        std::memcpy(g.wData().data(), st + vol.offW, vol.nw * sizeof(float));
        return;
    }
    const auto* sf = reinterpret_cast<const float*>(st); const auto* su = reinterpret_cast<const float*>(st + vol.offU);
    const auto* sv = reinterpret_cast<const float*>(st + vol.offV); const auto* sw = reinterpret_cast<const float*>(st + vol.offW);
    const glm::ivec3 n = vol.spec.dims;
    // an absent brick is dry air: zero first, then every stored site
    std::fill(g.fData().begin(), g.fData().end(), 0.0f); std::fill(g.uData().begin(), g.uData().end(), 0.0f);
    std::fill(g.vData().begin(), g.vData().end(), 0.0f); std::fill(g.wData().begin(), g.wData().end(), 0.0f);
    for (size_t slot = 1; slot < static_cast<size_t>(vol.slotsUsed); ++slot) {
        const int packed = vol.slotBrick[slot];
        if (packed < 0) continue;
        const glm::ivec3 b(packed & 1023, (packed >> 10) & 1023, (packed >> 20) & 1023);
        for (int l = 0; l < static_cast<int>(vol.bc); ++l) {
            const glm::ivec3 c = b * vol.bs() + glm::ivec3(l & 7, l >> 6, (l >> 3) & 7) - vol.o;   // local = x + 8 z + 64 y (26.17)
            const size_t i = slot * vol.bc + l;
            if (c.x < 0 || c.y < 0 || c.z < 0) continue;
            if (c.x < n.x && c.y < n.y && c.z < n.z) g.f(c.x, c.y, c.z) = sf[i];
            if (c.x <= n.x && c.y < n.y && c.z < n.z) g.u(c.x, c.y, c.z) = su[i];
            if (c.x < n.x && c.y <= n.y && c.z < n.z) g.v(c.x, c.y, c.z) = sv[i];
            if (c.x < n.x && c.y < n.y && c.z <= n.z) g.w(c.x, c.y, c.z) = sw[i];
        }
    }
    // 27.22: a tall cell is its column's water from its bed up - the host's dense grid gets it back as cells (the volume spread
    // evenly, the side velocities down the open overlap, vertical velocity 0 inside): the CPU readers (write-back, census, the
    // surface fallback) see ordinary water
    if (vol.tallActive && vol.tallH.size() == vol.dColumns) {
        const int T = vol.tallY;
        for (int z = 0; z < n.z; ++z) for (int x = 0; x < n.x; ++x) {
            const int Hc = vol.tallH[static_cast<size_t>(x) + static_cast<size_t>(n.x) * z];
            if (Hc <= 1 || T < 0 || T >= n.y) continue;
            const int ox = x > 0 ? std::min(Hc, vol.tallH[static_cast<size_t>(x - 1) + static_cast<size_t>(n.x) * z]) : 1;
            const int oz = z > 0 ? std::min(Hc, vol.tallH[static_cast<size_t>(x) + static_cast<size_t>(n.x) * (z - 1)]) : 1;
            const float share = g.f(x, T, z) / static_cast<float>(Hc), uT = g.u(x, T, z), wT = g.w(x, T, z);
            for (int y = std::max(0, T - Hc + 1); y <= T; ++y) {
                g.f(x, y, z) = share;
                g.u(x, y, z) = y >= T - ox + 1 ? uT : 0.0f;
                g.w(x, y, z) = y >= T - oz + 1 ? wT : 0.0f;
                if (y < T) g.v(x, y + 1, z) = 0.0f;
            }
        }
    }
}

double WaterCoreGpu::diagLaunchCost(Volume& vol, int n, int groups, bool barriers) {
    settle(vol);
    VkQueryPool qp = VK_NULL_HANDLE;
    VkQueryPoolCreateInfo qi{}; qi.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO; qi.queryType = VK_QUERY_TYPE_TIMESTAMP; qi.queryCount = 2;
    if (vkCreateQueryPool(m_device, &qi, nullptr, &qp) != VK_SUCCESS) return -1.0;
    WcPush push; push.nx = vol.spec.dims.x; push.ny = vol.spec.dims.y; push.nz = vol.spec.dims.z; fillPushBricks(vol, push);
    push.slotsUsed = 0;   // every workgroup returns at once: the launch alone
    VkCommandBuffer cmd = beginCommands();
    vkCmdResetQueryPool(cmd, qp, 0, 2);
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, qp, 0);
    for (int k = 0; k < n; ++k) { dispatchNoBarrier(cmd, *vol.pipes[Bricks], push, static_cast<size_t>(groups) * 64, 64); if (barriers) barrier(cmd); }
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, qp, 1);
    submitAndWait(cmd);
    uint64_t q[2] = {0, 0};
    double ms = -1.0;
    if (vkGetQueryPoolResults(m_device, qp, 0, 2, sizeof(q), q, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT) == VK_SUCCESS) {
        VkPhysicalDeviceProperties pp; vkGetPhysicalDeviceProperties(m_physical, &pp);
        ms = static_cast<double>(q[1] - q[0]) * pp.limits.timestampPeriod * 1e-6;
    }
    vkDestroyQueryPool(m_device, qp, nullptr);
    return ms;
}

// 27.22 TALL CELLS (docs/WaterCore.md 27.22)
bool WaterCoreGpu::setTall(Volume& vol, int tallY, const std::vector<int>& heights, std::string* err) {
    auto fail = [&](const std::string& m) { if (err) *err = m; return false; };
    if (!vol.bricked || !vol.sparse || vol.particles) return fail("tall cells need a sparse bricked fill volume");
    if (!vol.groundOn || vol.groundAsAir) return fail("tall cells need ground bricks (26.18): the cells under the tall layer read the ground sentinel");
    if (vol.tallActive || vol.tallPending) return fail("tall cells are active or pending - clear them first");
    if (tallY < 1 || tallY >= vol.spec.dims.y || ((tallY + vol.o.y) % vol.by) != 0) return fail("the tall layer must be the bottom cell of a brick inside the box");
    if (heights.size() != vol.dColumns) return fail("one height per column of the box");
    settle(vol);
    vol.tallY = tallY; vol.tallH = heights; vol.tallPending = 1;
    vol.tallColumns = 0;
    for (int hgt : heights) vol.tallColumns += hgt > 1 ? 1 : 0;
    return true;
}

void WaterCoreGpu::clearTall(Volume& vol) {
    if (!vol.tallActive && vol.tallPending != 1) return;
    settle(vol);
    if (vol.tallPending == 1) { vol.tallPending = 0; vol.tallY = -1; vol.tallH.clear(); vol.tallColumns = 0; return; }   // never collapsed
    vol.tallPending = 2;
}

void WaterCoreGpu::readPressure(Volume& vol, std::vector<float>& p) const { readCells(vol, "p", p); }

bool WaterCoreGpu::setParticles(Volume& vol, const std::vector<FlipParticle>& ps, std::string* err) {
    settle(vol);   // 26.16
    if (!vol.particles) { if (err) *err = "not a particle volume"; return false; }
    if (ps.size() > vol.particleCapacity) { if (err) *err = "the volume holds at most " + std::to_string(vol.particleCapacity) + " particles (" + std::to_string(ps.size()) + " requested)"; return false; }
    auto* st = static_cast<char*>(vol.staging.mapped) + vol.offParticles;
    auto* pos = reinterpret_cast<glm::vec4*>(st);
    auto* vel = reinterpret_cast<glm::vec4*>(st + vol.particleCapacity * 16);
    for (size_t i = 0; i < ps.size(); ++i) {
        pos[i] = glm::vec4(ps[i].pos, ps[i].mass);
        float idBits; std::memcpy(&idBits, &ps[i].id, 4);
        vel[i] = glm::vec4(ps[i].vel, idBits);
    }
    vol.particleCount = ps.size();
    vol.haveOldGrid = false;
    VkCommandBuffer cmd = beginCommands();
    if (!ps.empty()) {
        copy(cmd, vol.staging, vol.posA, ps.size() * 16, vol.offParticles);
        copy(cmd, vol.staging, vol.velA, ps.size() * 16, vol.offParticles + vol.particleCapacity * 16);
        barrier(cmd);
        // sort and p2g so the grid's f and faces reflect the particles (as FlipTransport::seed does)
        WcPush push; push.nx = vol.spec.dims.x; push.ny = vol.spec.dims.y; push.nz = vol.spec.dims.z; push.h = vol.spec.h;
        fillPushBricks(vol, push);
        recordParticleTransport(cmd, vol, push);   // with iparam = count, mode sequence inside (no move: dt = 0)
    } else {
        vkCmdFillBuffer(cmd, vol.f.buf, 0, vol.f.bytes, 0);
    }
    submitAndWait(cmd);
    return true;
}

void WaterCoreGpu::readParticles(Volume& vol, std::vector<FlipParticle>& out) const {
    const_cast<WaterCoreGpu*>(this)->settle(vol);   // 26.16
    out.resize(vol.particleCount);
    if (!vol.particles || vol.particleCount == 0) return;
    VkCommandBuffer cmd = beginCommands();
    copy(cmd, vol.posB, vol.staging, vol.particleCount * 16, 0, vol.offParticles);
    copy(cmd, vol.velB, vol.staging, vol.particleCount * 16, 0, vol.offParticles + vol.particleCapacity * 16);
    submitAndWait(cmd);
    const auto* st = static_cast<const char*>(vol.staging.mapped) + vol.offParticles;
    const auto* pos = reinterpret_cast<const glm::vec4*>(st);
    const auto* vel = reinterpret_cast<const glm::vec4*>(st + vol.particleCapacity * 16);
    for (size_t i = 0; i < vol.particleCount; ++i) {
        out[i].pos = glm::vec3(pos[i]); out[i].mass = pos[i].w; out[i].vel = glm::vec3(vel[i]);
        std::memcpy(&out[i].id, &vel[i].w, 4);
    }
}

// Sort (A -> B, by cell then id) and gather p2g into f, u, v, w; then the base copies for the next
// FLIP delta. The particles live in B afterwards; the next g2p/move reads and writes B in place,
// then the sort runs B -> A -> ... : the host swaps the roles by rebinding, so a step's input list
// is always posA/velA for the sort and posB/velB for everything else.
void WaterCoreGpu::recordParticleTransport(VkCommandBuffer cmd, Volume& vol, WcPush push) {
    push.iparam = static_cast<int32_t>(vol.particleCount);
    Vulkan::ComputePipeline& sort = *vol.pipes[FlipSort];
    push.mode = 0; dispatch(cmd, sort, push, vol.cells, 1024);
    push.mode = 1; dispatch(cmd, sort, push, vol.particleCount, 1024);
    push.mode = 2; dispatch(cmd, sort, push, 1024, 1024);
    push.mode = 3; dispatch(cmd, sort, push, vol.particleCount, 1024);
    push.mode = 4; dispatch(cmd, sort, push, vol.cells, 1024);
    Vulkan::ComputePipeline& p2g = *vol.pipes[FlipP2g];
    push.mode = 0; dispatch(cmd, p2g, push, vol.cells);
    const Buffer* lat[3] = {&vol.u, &vol.v, &vol.w};
    const size_t n[3] = {vol.nu, vol.nv, vol.nw};
    for (int L = 0; L < 3; ++L) {
        p2g.bind(cmd, static_cast<uint32_t>(L));   // set L binds lattice L at binding 5
        p2g.pushConstants(cmd, &push, sizeof(push));
        push.mode = 1 + L; p2g.pushConstants(cmd, &push, sizeof(push));
        p2g.dispatch(cmd, static_cast<uint32_t>((n[L] + 63) / 64));
        barrier(cmd);
    }
    copy(cmd, vol.u, vol.uOld, vol.nu * sizeof(float)); copy(cmd, vol.v, vol.vOld, vol.nv * sizeof(float)); copy(cmd, vol.w, vol.wOld, vol.nw * sizeof(float));
    barrier(cmd);
    vol.haveOldGrid = true;
}

void WaterCoreGpu::readCells(Volume& vol, const char* which, std::vector<float>& out) const {
    const_cast<WaterCoreGpu*>(this)->settle(vol);   // 26.16
    const std::string w(which);
    const bool solidComp = w == "solid_s" || w == "solid_q" || w == "solid_wake" || w == "solid_ledger";
    const int comp = w == "solid_s" ? 0 : w == "solid_q" ? 1 : w == "solid_wake" ? 2 : 3;
    const Buffer* b = solidComp ? &vol.solid : w == "f" ? &vol.f : w == "p" ? &vol.p : w == "liq" ? &vol.liq : w == "rhs" ? &vol.rhs : w == "diag" ? &vol.diag : w == "res" ? &vol.res : &vol.occ;
    const VkDeviceSize stride = solidComp ? 16 : 4, region = solidComp ? vol.offSolid : vol.offP;
    VkCommandBuffer cmd = beginCommands();
    copy(cmd, *b, vol.staging, liveCells(vol) * stride, 0, region);
    submitAndWait(cmd);
    const char* st = static_cast<const char*>(vol.staging.mapped) + region;
    const bool asUint = w == "liq" || w == "occ";
    auto valueAt = [&](size_t i) -> float {
        if (solidComp) return reinterpret_cast<const float*>(st)[i * 4 + comp];
        if (asUint) return static_cast<float>(reinterpret_cast<const uint32_t*>(st)[i]);
        return reinterpret_cast<const float*>(st)[i];
    };
    out.assign(vol.dCells, 0.0f);
    const glm::ivec3 n = vol.spec.dims;
    for (int z = 0; z < n.z; ++z) for (int y = 0; y < n.y; ++y) for (int x = 0; x < n.x; ++x) {
        const long i = storageIndex(vol, x, y, z);
        if (i >= 0) out[static_cast<size_t>(x) + static_cast<size_t>(n.x) * (y + static_cast<size_t>(n.y) * z)] = valueAt(static_cast<size_t>(i));
    }
}

void WaterCoreGpu::barrier(VkCommandBuffer cmd) const {
    ++m_barrierCount;
    VkMemoryBarrier mb{}; mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
}

void WaterCoreGpu::dispatchNoBarrier(VkCommandBuffer cmd, Vulkan::ComputePipeline& pipe, const WcPush& push, size_t threads, uint32_t local) const {
    if (threads == 0) return;
    ++m_dispatchCount;
    pipe.bind(cmd);
    pipe.pushConstants(cmd, &push, sizeof(push));
    pipe.dispatch(cmd, static_cast<uint32_t>((threads + local - 1) / local));
}
void WaterCoreGpu::stamp(VkCommandBuffer cmd, Volume& vol, int label) const {
    if (vol.tsCount < 0 || vol.tsCount >= static_cast<int>(vol.tsLabel.size())) return;   // < 0: not armed this call
    barrier(cmd);
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, vol.tsPool, static_cast<uint32_t>(vol.tsCount));
    vol.tsLabel[static_cast<size_t>(vol.tsCount++)] = label;
}

void WaterCoreGpu::dispatch(VkCommandBuffer cmd, Vulkan::ComputePipeline& pipe, const WcPush& push, size_t threads, uint32_t local) const {
    dispatchNoBarrier(cmd, pipe, push, threads, local);
    barrier(cmd);
}

void WaterCoreGpu::recordExtrapolate(VkCommandBuffer cmd, Volume& vol, WcPush push, bool particles) {
    const Buffer* lat[3] = {&vol.u, &vol.v, &vol.w};
    const size_t n[3] = {liveLattice(vol, 0), liveLattice(vol, 1), liveLattice(vol, 2)};
    for (int L = 0; L < 3; ++L) {
        Vulkan::ComputePipeline& pipe = *vol.pipes[ExtrapU + L];
        auto run = [&](uint32_t set) {   // 26.11: the set picks the ping-pong direction (see createPipelines)
            pipe.bind(cmd, set); pipe.pushConstants(cmd, &push, sizeof(push));
            pipe.dispatch(cmd, static_cast<uint32_t>((n[L] + 63) / 64)); barrier(cmd);
        };
        push.mode = L; push.iparam = particles ? 1 : 0;
        run(0);                                                // masks: knownB, keep; valB = the lattice
        for (int layer = 0; layer < 3; ++layer) {              // B -> A, A -> B, B -> A
            push.mode = 3 + L; push.iparam = layer == 0 ? 100 : 0;
            run(layer == 1 ? 2u : 1u);
        }
        push.mode = 6 + L; push.iparam = particles ? 1 : 0;
        run(3);                                                // finish: A -> the lattice, in place
    }
    // extrapolation must not write into solid faces (three lattices, disjoint writes: one barrier)
    for (int L = 0; L < 3; ++L) { push.mode = 3 + L; dispatchNoBarrier(cmd, *vol.pipes[FaceOpsU + L], push, n[L]); }
    barrier(cmd);
}

void WaterCoreGpu::recordSubstep(VkCommandBuffer cmd, Volume& vol, WcPush push, const SolverParams& params, int sweeps, bool quietBefore) {
    const size_t n[3] = {liveLattice(vol, 0), liveLattice(vol, 1), liveLattice(vol, 2)};
    const size_t cells = liveCells(vol), cols = liveColumns(vol);
    auto enforceSolids = [&]() { for (int L = 0; L < 3; ++L) { push.mode = 3 + L; dispatchNoBarrier(cmd, *vol.pipes[FaceOpsU + L], push, n[L]); } barrier(cmd); };
    auto cap = [&](int k) { if (!vol.stageCap.buf) return; barrier(cmd); copy(cmd, vol.f, vol.stageCap, cells * sizeof(float), 0, k * cells * sizeof(float)); barrier(cmd); };   // 26.10 test hook
    for (int i = 0; i < vol.sourceCount; ++i) { push.iparam = i; dispatch(cmd, *vol.pipes[Sources], push, 1, 1); }   // one source per dispatch: no shared-neighbour races
    cap(1);
    stamp(cmd, vol, TsSources);
    enforceSolids();
    if (vol.particles) {
        // Phase B2 on the GPU: g2p + move (in the sorted list B), then B -> A, sort A -> B, gather p2g
        push.iparam = static_cast<int32_t>(vol.particleCount); push.param0 = m_flipBlend; push.param1 = vol.haveOldGrid ? 0.0f : 1.0f;
        dispatch(cmd, *vol.pipes[FlipG2p], push, vol.particleCount);
        copy(cmd, vol.posB, vol.posA, vol.particleCount * 16); copy(cmd, vol.velB, vol.velA, vol.particleCount * 16);
        barrier(cmd);
        push.param0 = 0.0f; push.param1 = 0.0f;
        recordParticleTransport(cmd, vol, push);
        enforceSolids();
    } else {
        // transport: fills (6 checkerboard passes, each a hazard on f), then velocities from the old copies
        copy(cmd, vol.f, vol.fOrig, cells * sizeof(float));
        copy(cmd, vol.u, vol.uOld, n[0] * sizeof(float)); copy(cmd, vol.v, vol.vOld, n[1] * sizeof(float)); copy(cmd, vol.w, vol.wOld, n[2] * sizeof(float));
        barrier(cmd);
        if (EulerianTransport::s_allFacesAtOnce) {   // 26.5: the CPU's rule - all faces at once, gathered, the excess carried up
            push.mode = 3; dispatch(cmd, *vol.pipes[FillAdvect], push, cells);
            push.mode = 4; dispatch(cmd, *vol.pipes[FillAdvect], push, cells); cap(2);
            push.mode = 5; dispatch(cmd, *vol.pipes[FillAdvect], push, cols); cap(3);
        } else
        for (int dir = 0; dir < 3; ++dir) for (int parity = 0; parity < 2; ++parity) {
            push.mode = dir; push.iparam = parity;
            dispatch(cmd, *vol.pipes[FillAdvect], push, n[dir]);
        }
        stamp(cmd, vol, TsTransport);
        for (int L = 0; L < 3; ++L) { push.mode = L; dispatchNoBarrier(cmd, *vol.pipes[VelAdvectU + L], push, n[L]); }   // read old, write new: disjoint
        barrier(cmd);
        stamp(cmd, vol, TsVelAdvect);
        if (vol.hasSolids) { push.mode = 3; dispatch(cmd, *vol.pipes[ColumnOps], push, cols); }   // 20.5 ledger: fresh / walled-in water up its column
        cap(4);
        enforceSolids();
        {   // compaction (fills only, 24.11): voids under liquid always - slowly (param1 m/s) in moving water, at free fall while
            // settling; under a thin film only while settling (param0 = 1)
            const float p0 = push.param0, p1 = push.param1;
            push.mode = 1; push.param0 = quietBefore ? 1.0f : 0.0f; push.param1 = params.compactionSpeed;
            dispatch(cmd, *vol.pipes[ColumnOps], push, cols);
            push.param0 = p0; push.param1 = p1;
        }
        cap(5);
        stamp(cmd, vol, TsCompaction);
    }
    push.mode = 0; dispatch(cmd, *vol.pipes[FaceOpsV], push, n[1]);                                         // gravity
    push.mode = 1; dispatchNoBarrier(cmd, *vol.pipes[FaceOpsU], push, n[0]);                                // film slope x
    push.mode = 2; dispatchNoBarrier(cmd, *vol.pipes[FaceOpsW], push, n[2]);                                // film slope z (disjoint lattices)
    barrier(cmd);
    push.mode = 0; dispatch(cmd, *vol.pipes[ColumnOps], push, cols);                                        // settle
    enforceSolids();
    // projection
    stamp(cmd, vol, TsForces);
    push.mode = 0; push.param0 = vol.particles ? 1.0f : 0.0f; push.param1 = params.volumeCorrectionTime; push.vcBand = params.volumeCorrectionBand;   // 26.21: the volume correction
    dispatch(cmd, *vol.pipes[Classify], push, cells); push.param0 = 0.0f; push.param1 = 0.0f;
    if (s_stillWaterGuess) { push.mode = 2; dispatch(cmd, *vol.pipes[Classify], push, cols); }   // 26.9: the still-water first guess
    stamp(cmd, vol, TsClassify);
    if (s_lineSweeps > 0 && vol.bricked && vol.spec.dims.y <= kLineMaxNy) {   // 28: zebra line sweeps (one workgroup per brick column)
        for (int s = 0; s < s_lineSweeps; ++s) for (int colour = 0; colour < 2; ++colour) {
            push.mode = 0; push.iparam = colour; push.param1 = s_lineOmega; dispatch(cmd, *vol.pipes[RbgsLine], push, cols);
        }
    } else
    for (int s = 0; s < sweeps; ++s) for (int colour = 0; colour < 2; ++colour) {
        push.mode = 0; push.iparam = colour; push.param1 = m_omega; dispatch(cmd, *vol.pipes[Rbgs], push, cells);
    }
    if (push.tallY >= 0)   // 27.22: the tall layer converged on its own after the band's sweeps (cheap: one cell a column)
        for (int s = 0; s < s_tallSweeps; ++s) for (int colour = 0; colour < 2; ++colour) {
            push.mode = 2; push.iparam = colour; push.param1 = m_omega; dispatch(cmd, *vol.pipes[Rbgs], push, cols);
        }
    stamp(cmd, vol, TsSweeps);
    push.mode = 1; dispatch(cmd, *vol.pipes[Rbgs], push, cells);                                            // residual
    for (int L = 0; L < 3; ++L) { push.mode = 6 + L; dispatchNoBarrier(cmd, *vol.pipes[FaceOpsU + L], push, n[L]); }   // velocity update (disjoint)
    barrier(cmd);
    stamp(cmd, vol, TsVelUpdate);
    recordExtrapolate(cmd, vol, push, vol.particles);
    stamp(cmd, vol, TsExtrapolate);
    push.mode = 0; dispatch(cmd, *vol.pipes[ColumnOps], push, cols);                                        // settle again
    // friction and rest damping (FaceOps mode 9): dense - iparam = the lattice's length; bricked - iparam = the lattice (26.6)
    if (params.bedManning > 0.0f) {   // 26.13: Manning at the bed replaces pond friction (WaterSolver::applyBedManning), u and w faces
        push.mode = 10; push.param0 = params.bedManning * params.bedManning;
        push.iparam = 0; dispatchNoBarrier(cmd, *vol.pipes[FaceOpsU], push, n[0]);
        push.iparam = 2; dispatchNoBarrier(cmd, *vol.pipes[FaceOpsW], push, n[2]);
        barrier(cmd);
    } else if (params.pondFriction > 0.0f) {   // 24.7 pond friction: all motion, every substep (WaterSolver::applyPondFriction)
        push.mode = 9; push.param0 = std::max(0.0f, 1.0f - params.pondFriction * push.dt);
        for (int L = 0; L < 3; ++L) { push.iparam = vol.bricked ? L : static_cast<int32_t>(n[L]); dispatchNoBarrier(cmd, *vol.pipes[FaceOpsU + L], push, n[L]); }
        barrier(cmd);
    }
    if (quietBefore) {
        const float k = std::max(0.0f, 1.0f - params.restDamping * push.dt);
        push.mode = 9; push.param0 = k;
        for (int L = 0; L < 3; ++L) { push.iparam = vol.bricked ? L : static_cast<int32_t>(n[L]); dispatchNoBarrier(cmd, *vol.pipes[FaceOpsU + L], push, n[L]); }
        barrier(cmd);
    }
    cap(6);
    stamp(cmd, vol, TsSettle);
}

void WaterCoreGpu::recordReductions(VkCommandBuffer cmd, Volume& vol, WcPush push) {
    auto fold = [&](size_t partials, int slot, int startMode) {
        int mode = startMode;
        while (true) {
            const size_t groups = (partials + 255) / 256;
            push.mode = mode; push.param0 = static_cast<float>(partials); push.param1 = groups == 1 ? 1.0f : 0.0f; push.iparam = slot;
            dispatch(cmd, *vol.pipes[Reduce], push, partials, 256);
            if (groups == 1) break;
            partials = groups; mode = mode == 2 ? 3 : 2;
        }
    };
    // 27.23 W1e: a frozen column is not stepped (no kernel writes its cells), so its units are the ones the kept map holds - the
    // CACHED call reduces the awake bricks only (slots [0, stepSlots) after the partition) and mode 6 reads the frozen units from
    // the map. Anything that writes frozen cells (upload, seed, a regrid, a pool grow, occupancy) sets unitsFullNext: every unit.
    const int fullSlots = vol.slotsUsed;
    const bool cached = s_unitCache && vol.bricked && vol.unitMap.buf && vol.unitMapValid && (!vol.unitsFullNext || s_unitCacheStaleForTest)
                        && vol.stepSlots > 0 && vol.stepSlots < fullSlots;
    const bool speedLane = s_unitCache && vol.bricked && vol.unitMap.buf;
    auto prefix = [&](bool on) { vol.slotsUsed = on ? vol.stepSlots : fullSlots; fillPushBricks(vol, push); push.setFrozenOn(0); };
    if (cached) prefix(true);
    const size_t cells = liveCells(vol);
    vol.lastReduceSlots = vol.slotsUsed; vol.lastReduceCached = cached; vol.lastSpeedLane = speedLane;
    push.mode = 0; dispatch(cmd, *vol.pipes[Reduce], push, cells, 256);
    if (cached) prefix(false);
    if (vol.bricked) {
        // 26.6: the per-half-brick partials into MAP order (absent bricks as zero), then the fold - the same tree whichever
        // bricks are stored, so the sums the rest and settle decisions read are the dense run's bit for bit
        // 26.17: per 128-cell unit, gathered into the world-aligned unit map - the same tree for every brick height
        const size_t units = static_cast<size_t>(vol.bn.x) * vol.nuy * static_cast<size_t>(vol.bn.z);
        push.mode = 6; push.param0 = static_cast<float>(vol.nuy); push.param1 = cached ? 1.0f : 0.0f;
        dispatch(cmd, *vol.pipes[Reduce], push, units, 256);
        push.param1 = 0.0f;
        if (vol.unitMap.buf) { vol.unitMapValid = true; if (!cached) vol.unitsFullNext = false; }
        vol.unitPending = false;
        if (s_unitCapture || vol.partialWake) {   // 26.31: the map before the fold overwrites its head (the partial wake reads it every call)
            if (vol.unitCap.buf && vol.unitCap.bytes < units * 16) destroyBuffer(vol.unitCap);
            if (!vol.unitCap.buf && !createBuffer(vol.unitCap, units * 16, true, nullptr)) vol.unitCap = Buffer{};
            if (vol.unitCap.buf) { copy(cmd, vol.part2, vol.unitCap, units * 16); barrier(cmd); vol.unitPending = true; }
        }
        fold(units, 0, 3);
    } else {
        fold(2 * ((cells + 255) / 256), 0, 2);   // 26.17: mode 0 writes a partial per 128-cell half
    }
    if (!speedLane)   // 27.23 W1e: with the speed lane the three maxima came with mode 0 (out[0].w)
        for (int L = 0; L < 3; ++L) {
            const size_t n = liveLattice(vol, L);
            push.mode = 1; push.iparam = L; dispatch(cmd, *vol.pipes[Reduce], push, n, 256);
            fold((n + 255) / 256, 1 + L, 2);
        }
    // the residual: of the cells the sweeps solved (a frozen cell's residual is its last awake call's - reported, never decided on)
    if (cached) prefix(true);
    push.mode = 4; dispatch(cmd, *vol.pipes[Reduce], push, liveCells(vol), 256); fold((liveCells(vol) + 255) / 256, 4, 2);
    if (cached) prefix(false);
    const size_t cols = std::max<size_t>(liveColumns(vol), 1);
    push.mode = 5; dispatch(cmd, *vol.pipes[Reduce], push, cols, 256); fold((cols + 255) / 256, 5, 2);
}

void WaterCoreGpu::updateOccupancy(Volume& vol, const WaterGrid& g, const std::vector<int>& cells) {
    vol.unitsFullNext = true;   // 27.23 W1e: this writes stored cells, frozen ones included - the next reduction computes every unit
    if (cells.empty() || vol.hostOcc.size() != vol.dCells) return;
    settle(vol);   // 26.16: the call in flight reads the occupancy we are about to write
    const glm::ivec3 n = vol.spec.dims;
    std::vector<size_t> bricks; std::vector<size_t> cols;
    for (int d : cells) {
        if (d < 0 || static_cast<size_t>(d) >= vol.dCells) continue;
        const int x = d % n.x, y = (d / n.x) % n.y, z = d / (n.x * n.y);
        vol.hostOcc[static_cast<size_t>(d)] = static_cast<uint32_t>(g.occ(x, y, z));
        cols.push_back(static_cast<size_t>(x) + static_cast<size_t>(n.x) * z);
        if (vol.bricked) {
            const glm::ivec3 b = (glm::ivec3(x, y, z) + vol.o) / vol.bs();
            bricks.push_back(static_cast<size_t>(b.x) + static_cast<size_t>(vol.bn.x) * (b.y + static_cast<size_t>(vol.bn.y) * b.z));
        }
    }
    std::sort(cols.begin(), cols.end()); cols.erase(std::unique(cols.begin(), cols.end()), cols.end());
    std::sort(bricks.begin(), bricks.end()); bricks.erase(std::unique(bricks.begin(), bricks.end()), bricks.end());
    // the solid top of each touched column, as upload computes it (a dry column's record is rebuilt from it)
    if (vol.hostSolidTop.size() == vol.dColumns)
        for (size_t c : cols) {
            const int x = static_cast<int>(c % static_cast<size_t>(n.x)), z = static_cast<int>(c / static_cast<size_t>(n.x));
            float top = -1e30f;
            for (int y = n.y - 1; y >= 0; --y)
                if (g.occ(x, y, z) == Occ::Solid) { top = (static_cast<float>(g.spec().origin.y) + static_cast<float>(y) + 1.0f) * g.spec().h; break; }
            vol.hostSolidTop[c] = top;
        }
    vol.solidTopDirty = true;   // 29 S1
    auto* st = static_cast<char*>(vol.staging.mapped);
    VkCommandBuffer cmd = beginCommands();
    if (vol.bricked) {
        // 26.18: which bricks are all ground, re-read for the touched ones (out-of-box cells count as Solid, as upload's)
        if (vol.groundOn && vol.allSolid.size() == vol.mapCount && !s_patchStaleGroundForTest)
            for (size_t m : bricks) {
                const glm::ivec3 b(static_cast<int>(m % static_cast<size_t>(vol.bn.x)), static_cast<int>((m / static_cast<size_t>(vol.bn.x)) % static_cast<size_t>(vol.bn.y)),
                                   static_cast<int>(m / (static_cast<size_t>(vol.bn.x) * static_cast<size_t>(vol.bn.y))));
                char all = 1;
                for (int l = 0; l < static_cast<int>(vol.bc) && all; ++l) {
                    const glm::ivec3 c = b * vol.bs() + glm::ivec3(l & 7, l >> 6, (l >> 3) & 7) - vol.o;   // local = x + 8 z + 64 y (26.17)
                    if (c.x < 0 || c.y < 0 || c.z < 0 || c.x >= n.x || c.y >= n.y || c.z >= n.z) continue;
                    if (vol.hostOcc[static_cast<size_t>(c.x) + static_cast<size_t>(n.x) * (c.y + static_cast<size_t>(n.y) * c.z)] != static_cast<uint32_t>(Occ::Solid)) all = 0;
                }
                vol.allSolid[m] = all;
            }
        // the stored bricks among them: their occupancy, staged and copied (a ground or absent brick takes its occupancy from the
        // host copy when the next call's residency stores it)
        std::vector<int> slots;
        for (size_t m : bricks) if (vol.map[m] >= kFirstSlot) slots.push_back(vol.map[m]);
        std::sort(slots.begin(), slots.end());
        if (!slots.empty()) {
            scatterSlots(vol, slots, nullptr);
            for (size_t a = 0; a < slots.size();) {   // contiguous slots in one copy
                size_t e = a + 1;
                while (e < slots.size() && slots[e] == slots[e - 1] + 1) ++e;
                const VkDeviceSize first = static_cast<VkDeviceSize>(slots[a]) * vol.bc * 4, bytes = static_cast<VkDeviceSize>(e - a) * vol.bc * 4;
                copy(cmd, vol.staging, vol.occ, bytes, vol.offOcc + first, first);
                a = e;
            }
        }
    } else {
        // the dense layout (FLIP volumes): the whole occupancy buffer
        std::memcpy(st + vol.offOcc, vol.hostOcc.data(), vol.cells * 4);
        copy(cmd, vol.staging, vol.occ, vol.cells * 4, vol.offOcc);
    }
    submitAndWait(cmd);
    wakeAll(vol);   // the ground moved: the water must look again
}

GpuStepStats WaterCoreGpu::step(Volume& vol, const SolverParams& params, float dt, int ticks, int sweeps) {
    auto hp = WaterHostProfile::Clock::now();   // 27.28
    settle(vol);   // 26.16: the call in flight is collected first (its record decides this call's residency, its speed the substeps)
    WaterHostProfile::lap("gpu.01 settle (collect the call before)", hp);
    if (vol.growWant > 0) { const size_t want = vol.growWant; vol.growWant = 0; growPool(vol, want); }   // 27.23 W1d.1: before any recording
    GpuStepStats st; st.ticks = ticks; st.sweeps = sweeps;
    m_dispatchCount = 0; m_barrierCount = 0;   // 28: this call's launches
    st.capacity = vol.bricked ? static_cast<int>(vol.capacity) - 1 : 0;
    st.brickHeight = vol.by;   // 26.17
    WaterHostProfile::lap("gpu.02 grow pool", hp);
    if (!vol.asleep) updatePartialWake(vol, params);   // 26.31: may freeze every column (asleep)
    WaterHostProfile::lap("gpu.03 partial wake (freeze test)", hp);
    st.awakeColumns = vol.awakeCols; st.frozenColumns = vol.frozenCols; st.movingColumns = vol.movingCols;
    if (vol.asleep || ticks <= 0) {
        st.asleep = vol.asleep; st.quietTicks = vol.quietTicks;
        if (vol.bricked) { st.bricks = vol.storedCount; st.groundBricks = vol.groundCount; }
        return st;
    }
    WcPush push; push.nx = vol.spec.dims.x; push.ny = vol.spec.dims.y; push.nz = vol.spec.dims.z;
    push.h = vol.spec.h; push.thr = params.liquidThreshold; push.gravity = params.gravity; push.filmHold = params.filmHoldDepth / vol.spec.h; push.thetaMin = static_cast<float>(params.thetaMin);
    push.setSurfaceGhost(WaterSolver::s_surfaceGhost);   // 26.9: one switch for both backends
    push.quietRise = EulerianTransport::s_quietRiseFraction * std::sqrt(2.0f * 9.81f * vol.spec.h);   // 26.9: the CPU's gate, the CPU's g
    // CFL from the last known max speed plus the gravity a few ticks can add. The margin is capped at
    // six ticks' worth: a 60-tick benchmark call otherwise carried +9.8 m/s and doubled its substeps
    // (2.39 ms at 1/3 m with 2 substeps, 2026-10-08). The fill move is bounded (min of donor, room)
    // and the velocity advection is semi-Lagrangian, so a late-call speed-up past the bound degrades
    // accuracy, not stability; realtime calls are one tick and exact.
    const double vmax = vol.lastMaxSpeed + params.gravity * dt * std::min(ticks, 6);
    const double limit = params.cflFraction * vol.spec.h;
    const int n = std::clamp(static_cast<int>(std::ceil(vmax * dt / limit)), 1, params.maxSubsteps);
    const float ds = dt / static_cast<float>(n);
    push.dt = ds;
    const auto t0 = std::chrono::steady_clock::now();
    VkCommandBuffer cmd = beginCommands();
    const bool gpuSurf = s_gpuSurface && vol.bricked;   // 29 S1
    if (!gpuSurf) vol.meshValid = false;   // the switch off: turned on again, the field is built whole first
    if (gpuSurf) {   // 29 S1: the frame before may still be drawing the mesh this call rewrites (one queue: an execution dependency)
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 0, nullptr);
    }
    vol.tsCount = -1;   // 26.11 test hook: armed only for single-tick, single-substep calls
    if (!vol.callTs) { VkQueryPoolCreateInfo qi{}; qi.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO; qi.queryType = VK_QUERY_TYPE_TIMESTAMP; qi.queryCount = 2;
                       if (vkCreateQueryPool(m_device, &qi, nullptr, &vol.callTs) != VK_SUCCESS) vol.callTs = VK_NULL_HANDLE; }
    if (vol.callTs) { vkCmdResetQueryPool(cmd, vol.callTs, 0, 2); vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, vol.callTs, 0); }
    if (s_kernelTiming && ticks == 1 && n == 1) {
        if (!vol.tsPool) { VkQueryPoolCreateInfo qi{}; qi.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO; qi.queryType = VK_QUERY_TYPE_TIMESTAMP; qi.queryCount = static_cast<uint32_t>(vol.tsLabel.size());
                           if (vkCreateQueryPool(m_device, &qi, nullptr, &vol.tsPool) != VK_SUCCESS) vol.tsPool = VK_NULL_HANDLE; }
        if (vol.tsPool) { vkCmdResetQueryPool(cmd, vol.tsPool, 0, static_cast<uint32_t>(vol.tsLabel.size())); vol.tsCount = 0; stamp(cmd, vol, TsStart); }
    }
    // 27.22: COLLAPSE - the heights to the device, the water under the tall layer gathered into it, while its bricks are still
    // stored; the residency below then reads them as ground (tallActive)
    if (vol.tallPending == 1 && vol.bricked) {
        auto* hs = reinterpret_cast<int32_t*>(static_cast<char*>(vol.staging.mapped) + vol.offBricks) + vol.tallOff;
        for (size_t c = 0; c < vol.dColumns; ++c) hs[c] = vol.tallH[c];
        copy(cmd, vol.staging, vol.bricks, vol.dColumns * 4, vol.offBricks + vol.tallOff * 4, vol.tallOff * 4);
        barrier(cmd);
        WcPush tp = push; fillPushBricks(vol, tp); tp.tallY = vol.tallY; tp.tallOff = static_cast<int32_t>(vol.tallOff); tp.mode = 0; tp.setFrozenOn(0);
        vol.unitsFullNext = true;   // 27.23 W1e: the regrid writes every column, frozen ones too
        dispatch(cmd, *vol.pipes[Tall], tp, liveColumns(vol));
        vol.tallActive = true; vol.tallPending = 0; ++vol.tallRegrids;
    }
    if (vol.bricked && vol.sparse) {
        // 26.6 RESIDENCY - a pure function of the water: the bricks holding water and those within `reach` cells of it, reach =
        // the extrapolation's three layers + one (a face beside an absent cell) + the farthest this call can move water
        // (vmax over every tick of the call, + 1). Needs > one ring (8 cells) only for fast water.
        const int travel = static_cast<int>(std::ceil(vmax * dt * ticks / vol.spec.h)) + 1;
        const int reach = kReachLiquid + travel;                 // the larger of the two (liquid); films get kReachFilm + travel
        vol.lastTravelForTest = travel;
        const int rings = std::max(1, (reach + vol.by - 2) / vol.by);   // reported only: the rings up/down (markNeed walks each axis to the reach)
        std::vector<char> need(vol.mapCount, 0);
        vol.liquidAtStartForTest.clear();
        if (vol.recordValid) for (size_t s = 1; s < static_cast<size_t>(vol.slotsUsed); ++s)   // 26.23 diagnostic
            if (vol.slotBrick[s] >= 0 && (vol.hostInfo[2 * s + 1] & 1)) vol.liquidAtStartForTest.push_back(vol.slotBrick[s]);
        if (vol.recordValid && !vol.freezeForTest) {
            const int32_t* info = vol.hostInfo.data();
            const size_t ncol = static_cast<size_t>(vol.bn.x) * vol.bn.z;
            // 29 S3: round the awake water only - `active` columns are recomputed, the marks come from the slots in `src` (active dilated
            // by the rings a mark reaches); every other entry keeps its state (a frozen column's water does not move)
            const bool restrict = s_residencyAwakeOnly && vol.partialWake && vol.anyFrozen && vol.colFrozen.size() == ncol;
            std::vector<char> active, src;
            if (restrict) {
                const int R = (reach + kB - 1) / kB;
                auto dilate = [&](const std::vector<char>& in, int r) {
                    if (r <= 0) return in;
                    std::vector<char> rowOut(ncol, 0), out(ncol, 0);
                    for (int bz = 0; bz < vol.bn.z; ++bz) for (int bx = 0; bx < vol.bn.x; ++bx) {   // separable: x, then z
                        bool any = false;
                        for (int k = std::max(0, bx - r); k <= std::min(vol.bn.x - 1, bx + r) && !any; ++k) any = in[static_cast<size_t>(k) + static_cast<size_t>(vol.bn.x) * bz] != 0;
                        rowOut[static_cast<size_t>(bx) + static_cast<size_t>(vol.bn.x) * bz] = any;
                    }
                    for (int bz = 0; bz < vol.bn.z; ++bz) for (int bx = 0; bx < vol.bn.x; ++bx) {
                        bool any = false;
                        for (int k = std::max(0, bz - r); k <= std::min(vol.bn.z - 1, bz + r) && !any; ++k) any = rowOut[static_cast<size_t>(bx) + static_cast<size_t>(vol.bn.x) * k] != 0;
                        out[static_cast<size_t>(bx) + static_cast<size_t>(vol.bn.x) * bz] = any;
                    }
                    return out;
                };
                std::vector<char> awake(ncol, 0);
                for (size_t c = 0; c < ncol; ++c) awake[c] = (vol.colFrozen[c] || s_residencyCopyAllForTest) ? 0 : 1;
                active = dilate(awake, R);
                src = dilate(active, R);
                ++s_residencyRestrictedCallsForTest;
            }
            auto marks = [&](std::vector<char>& nd, bool onlySrc) {
                for (size_t s = 1; s < static_cast<size_t>(vol.slotsUsed); ++s) {
                    const int packed = vol.slotBrick[s];
                    if (packed < 0 || !(info[2 * s] & 1)) continue;   // record 0: any water
                    const glm::ivec3 b(packed & 1023, (packed >> 10) & 1023, (packed >> 20) & 1023);
                    if (onlySrc && !src[static_cast<size_t>(b.x) + static_cast<size_t>(vol.bn.x) * b.z]) continue;
                    nd[static_cast<size_t>(b.x) + static_cast<size_t>(vol.bn.x) * (b.y + static_cast<size_t>(vol.bn.y) * b.z)] = 2;
                    for (int r = 0; r < 2; ++r) {
                        const int rec = info[2 * s + r];
                        if (!(rec & 1)) continue;
                        int dist[6]; for (int k = 0; k < 6; ++k) dist[k] = (rec >> (1 + 3 * k)) & 7;
                        markNeed(vol, nd, b, dist, (r == 0 ? kReachFilm : kReachLiquid) + travel);
                    }
                }
            };
            marks(need, restrict);
            if (restrict) {
                const size_t sx = static_cast<size_t>(vol.bn.x), sxy = sx * static_cast<size_t>(vol.bn.y);
                for (size_t m = 0; m < vol.mapCount; ++m)
                    if (!active[(m % sx) + sx * (m / sxy)]) need[m] = vol.map[m] >= kFirstSlot ? 2 : (vol.map[m] == kGround ? 1 : 0);
                if (s_residencyCheckForTest) {   // TEST ONLY: the full residency, outcome by outcome
                    std::vector<char> full(vol.mapCount, 0);
                    marks(full, false);
                    const bool ground = vol.groundOn && vol.allSolid.size() == vol.mapCount;
                    auto outcome = [&](size_t m, char nv) { return nv == 0 ? 0 : ((ground && nv == 1 && vol.allSolid[m]) ? 1 : 2); };
                    for (size_t m = 0; m < vol.mapCount; ++m) {
                        const size_t c = (m % sx) + sx * (m / sxy);
                        if (vol.colRecorded.size() == ncol && vol.colRecorded[c]) continue;   // applyResidency's own rule there
                        if (vol.tallActive && vol.tallPending != 2 && vol.tallY >= 0 && static_cast<int>((m / sx) % static_cast<size_t>(vol.bn.y)) < (vol.tallY + vol.o.y) / vol.by) continue;   // under the tall layer: its rule
                        if (outcome(m, need[m]) != outcome(m, full[m])) ++s_residencyMismatchForTest;
                    }
                    ++s_residencyCallsCheckedForTest;
                }
            }
        } else {
            for (size_t m = 0; m < vol.mapCount; ++m) need[m] = vol.map[m] != 0 ? 1 : 0;   // the upload's residency stands for its first call (nothing is freed: all stored bricks stay)
        }
        for (int c : vol.sourceCells) {   // a pump's cell is water from the start of the call (marked from the cell, never a grid scan)
            if (c < 0 || static_cast<size_t>(c) >= vol.dCells) continue;
            const int nx = vol.spec.dims.x, ny = vol.spec.dims.y;
            glm::ivec3 b; int dist[6];
            cellToRecord(vol.o, vol.bs(), c % nx, (c / nx) % ny, c / (nx * ny), b, dist);
            need[static_cast<size_t>(b.x) + static_cast<size_t>(vol.bn.x) * (b.y + static_cast<size_t>(vol.bn.y) * b.z)] = 2;
            markNeed(vol, need, b, dist, reach);
        }
        if (vol.depositPending && !vol.hostDeposit.empty()) needFromDenseFill(vol, vol.hostDeposit, need, reach);
        WaterHostProfile::lap("gpu.04 residency: which bricks", hp);
        const std::vector<int> fresh = applyResidency(vol, need);
        vol.lastRings = rings;
        if (!fresh.empty()) { scatterSlots(vol, fresh, nullptr); recordSlotUploads(cmd, vol, fresh, true); }
        WaterHostProfile::lap("gpu.05 residency: apply", hp);
        barrier(cmd);
        vol.lastCompacted = compactSlots(cmd, vol);
        if (vol.lastCompacted > 0) barrier(cmd);
        WaterHostProfile::lap("gpu.06 compact", hp);
        vol.lastPartitionMoves = partitionSlots(cmd, vol);   // 26.31 step 2: the awake bricks first (after residency + compaction)
        if (vol.tablesDirty) {   // the steady state (nothing allocated, freed or moved) uploads nothing
            writeTablesToStaging(vol);
            copy(cmd, vol.staging, vol.bricks, vol.counterOff * 4, vol.offBricks);
            vol.tablesDirty = false;
        }
        WaterHostProfile::lap("gpu.07 partition + tables", hp);
    } else if (vol.bricked) {
        vol.lastAllocated = 0; vol.lastFreed = 0; vol.lastHeld = 0; vol.lastRings = 0; vol.lastCompacted = 0;
    }
    // 27.22: EXPAND - the bricks under the floor are stored again (the residency above, tallPending 2); the tall cells spread into them
    if (vol.tallPending == 2 && vol.bricked) {
        WcPush tp = push; fillPushBricks(vol, tp); tp.tallY = vol.tallY; tp.tallOff = static_cast<int32_t>(vol.tallOff); tp.mode = 1; tp.setFrozenOn(0);
        vol.unitsFullNext = true;   // 27.23 W1e
        barrier(cmd);
        dispatch(cmd, *vol.pipes[Tall], tp, liveColumns(vol));
        vkCmdFillBuffer(cmd, vol.bricks.buf, vol.tallOff * 4, vol.dColumns * 4, 0);
        barrier(cmd);
        vol.tallActive = false; vol.tallPending = 0; vol.tallY = -1; vol.tallH.clear(); vol.tallColumns = 0; ++vol.tallRegrids;
    }
    stamp(cmd, vol, TsResidency);
    const auto tResidency = std::chrono::steady_clock::now();
    // 26.31 step 2: the step covers the awake bricks only (slots [0, stepSlots), the awake column entries) - restored before the
    // record, the reductions and the surface, which cover every stored brick
    const int fullSlots = vol.slotsUsed, fullCols = vol.colCount;
    if (vol.stepSlots > 0) {
        // the frozen bricks' liq to 0 (they lie in [stepSlots, slotsUsed) after the partition): the sweeps read no pressure from them
        if (fullSlots > vol.stepSlots) vkCmdFillBuffer(cmd, vol.liq.buf, static_cast<VkDeviceSize>(vol.stepSlots) * vol.bc * 4, static_cast<VkDeviceSize>(fullSlots - vol.stepSlots) * vol.bc * 4, 0);
        vol.slotsUsed = vol.stepSlots; vol.colCount = vol.stepCols;
    }
    st.stepSlots = vol.stepSlots; st.partitionMoves = vol.lastPartitionMoves;
    fillPushBricks(vol, push);
    if (!vol.pendingMomentum.empty()) {
        // 26.26: the debris exchange - before the first substep (where the CPU path's change landed), one workgroup per record, in
        // order (each dispatch ends with a barrier: deterministic). The call before has settled, so the host buffer is free.
        const size_t n = vol.pendingMomentum.size() / 8;
        std::memcpy(vol.momentum.mapped, vol.pendingMomentum.data(), vol.pendingMomentum.size() * sizeof(float));
        barrier(cmd);
        for (size_t i = 0; i < n; ++i) { push.iparam = static_cast<int32_t>(i); dispatch(cmd, *vol.pipes[Momentum], push, 64); }
        push.iparam = 0;
        st.momentumRecords = static_cast<int>(n); vol.momentumApplied += static_cast<long>(n);
        vol.pendingMomentum.clear();
    }
    if (vol.bricked) vkCmdFillBuffer(cmd, vol.bricks.buf, vol.counterOff * 4, kCounters * 4, 0);   // this call's counters
    vkCmdFillBuffer(cmd, vol.dropped.buf, 0, vol.dropped.bytes, 0);
    vkCmdFillBuffer(cmd, vol.births.buf, 0, vol.births.bytes, 0);   // 21: this call's birth records
    vkCmdFillBuffer(cmd, vol.lists.buf, 0, vol.lists.bytes, 0);     // 26.11: the packed lists start empty
    const size_t cols = liveColumns(vol);
    if (vol.depositPending) {   // 21: droplets that landed since the last step
        auto* sd = reinterpret_cast<float*>(static_cast<char*>(vol.staging.mapped) + vol.offDeposit);
        if (vol.bricked) {
            std::memset(sd, 0, liveCells(vol) * sizeof(float));
            const glm::ivec3 dn = vol.spec.dims;
            for (int z = 0; z < dn.z; ++z) for (int y = 0; y < dn.y; ++y) for (int x = 0; x < dn.x; ++x) {
                const size_t d = static_cast<size_t>(x) + static_cast<size_t>(dn.x) * (y + static_cast<size_t>(dn.y) * z);
                if (d >= vol.hostDeposit.size() || vol.hostDeposit[d] == 0.0f) continue;
                const long i = storageIndex(vol, x, y, z);
                if (i >= 0) sd[i] = vol.hostDeposit[d];   // the residency above stored every deposit's brick
            }
        } else if (vol.hostDeposit.size() == vol.cells) {
            std::memcpy(sd, vol.hostDeposit.data(), vol.cells * sizeof(float));
        }
        copy(cmd, vol.staging, vol.deposit, liveCells(vol) * sizeof(float), vol.offDeposit);
        barrier(cmd);
        push.mode = 5; dispatch(cmd, *vol.pipes[ColumnOps], push, cols); push.mode = 0;
        vol.depositPending = false; vol.hostDeposit.clear();
    } else barrier(cmd);
    if (vol.solidRatesPending) {   // 20 (M1): q and the ledger marks from the device's own fill, once per upload
        push.mode = 1; push.param0 = vol.solidFrame; dispatch(cmd, *vol.pipes[Classify], push, liveCells(vol)); push.mode = 0; push.param0 = 0.0f;
        vol.solidRatesPending = false;
    }
    const bool capture = s_stageCapture && ticks == 1 && n == 1;   // 26.10 test hook
    if (capture && !vol.stageCap.buf && !createBuffer(vol.stageCap, kStages * vol.cells * sizeof(float), true, nullptr)) vol.stageCap = Buffer{};
    Buffer capBuf; if (!capture) std::swap(capBuf, vol.stageCap);   // the substep captures only when the buffer is there
    auto capAt = [&](int k) { if (!vol.stageCap.buf) return; barrier(cmd); copy(cmd, vol.f, vol.stageCap, liveCells(vol) * sizeof(float), 0, k * liveCells(vol) * sizeof(float)); barrier(cmd); };
    for (int t = 0; t < ticks; ++t) {
        copy(cmd, vol.f, vol.fPrev, liveCells(vol) * sizeof(float));
        barrier(cmd);
        capAt(0);
        for (int s = 0; s < n; ++s) recordSubstep(cmd, vol, push, params, sweeps, vol.quietBefore);
        if (!vol.particles) { push.mode = 2; push.param0 = 1e-6f; dispatch(cmd, *vol.pipes[ColumnOps], push, cols); push.param0 = 0.0f; }   // sweep (fills only)
        capAt(7);
        if (vol.dropletsOn) { push.mode = 4; push.param0 = dt; dispatch(cmd, *vol.pipes[ColumnOps], push, cols); push.param0 = 0.0f; push.mode = 0; }   // 21: droplet births, once per tick
        capAt(8);
        stamp(cmd, vol, TsSweepBirths);
    }
    if (!capture) std::swap(capBuf, vol.stageCap);   // the capture buffer is back in place (only the recording needed it hidden)
    vol.slotsUsed = fullSlots; vol.colCount = fullCols;   // 26.31 step 2: every stored brick from here
    fillPushBricks(vol, push);
    push.setFrozenOn(0);   // 26.31: the record, the reductions and the surface cover every stored brick (a frozen column is still drawn and weighed)
    const size_t colsAll = liveColumns(vol);
    if (vol.bricked) {   // 26.6: the water record residency reads next call, and the absent brick's check
        dispatch(cmd, *vol.pipes[Bricks], push, static_cast<size_t>(vol.slotsUsed) * 64, 64);
        copy(cmd, vol.bricks, vol.staging, (vol.infoOff + 2 * static_cast<size_t>(vol.slotsUsed) - vol.counterOff) * 4, vol.counterOff * 4, vol.offBricks + vol.counterOff * 4);
    }
    stamp(cmd, vol, TsBrickRecord);
    recordReductions(cmd, vol, push);
    copy(cmd, vol.out, vol.staging, kOutSlots * 16, 0, vol.offOut);
    stamp(cmd, vol, TsReduce);
    // Phase F: the surface field rides the same submission (one float per sub-column of top/bottom/
    // solid - never the grid), so the renderer sees every step's surface
    push.param0 = WaterGrid::kSurfaceMinDepth / vol.spec.h; push.iparam = vol.spec.origin.y;
    push.param1 = static_cast<float>(vol.columns);   // 26.11: where the multi-run records start in surf (x 8 floats)
    // 26.33: with frozen columns the surface covers the awake column entries only (they lie first after the partition); the host
    // keeps the frozen columns' last surface (readSurface) - it cannot change while they are frozen
    const bool surfPartial = s_partialSurface && !vol.surfaceFullNext && vol.stepSlots > 0 && vol.stepCols > 0 && vol.stepCols < fullCols
                             && !(gpuSurf && !vol.meshValid);   // 29 S1: the device field is built whole once first
    vol.surfaceFullNext = false;
    vol.pend.surfCols.clear();
    if (surfPartial) {
        vol.pend.surfCols.assign(vol.colEntries.begin(), vol.colEntries.begin() + vol.stepCols);
        vol.colCount = vol.stepCols; fillPushBricks(vol, push); push.setFrozenOn(0);
    }
    const size_t colsSurf = surfPartial ? liveColumns(vol) : colsAll;
    WcPush mp = push;   // 29 S1: the mesh kernel's push (the field's lattice: origin x / z, every dense column)
    mp.param0 = static_cast<float>(vol.spec.origin.x); mp.param1 = static_cast<float>(vol.spec.origin.z); mp.dt = s_meshNoRingForTest ? 0.0f : 1.0f;
    const size_t denseCols = vol.dColumns;
    bool meshFull = !surfPartial || vol.meshFullNext;
    if (gpuSurf) {   // 29 S1: before the surface pass - the solid tops, the dry template (a full pass), the host's writes
        if (vol.solidTopDirty && vol.hostSolidTop.size() == vol.dColumns) {
            auto* up = reinterpret_cast<float*>(static_cast<char*>(vol.surfUp.mapped) + vol.dColumns * sizeof(Core::Water::SurfaceColumn));
            std::memcpy(up, vol.hostSolidTop.data(), vol.dColumns * 4);
            copy(cmd, vol.surfUp, vol.solidTopD, vol.dColumns * 4, vol.dColumns * sizeof(Core::Water::SurfaceColumn), 0);
            barrier(cmd);
            vol.solidTopDirty = false;
        }
        if (!surfPartial) {   // as the host's full read: every column not holding water is dry over its solid top (a recorded one kept)
            mp.mode = 4; dispatch(cmd, *vol.pipes[SurfaceMesh], mp, denseCols);
        }
        if (!vol.surfPending.empty()) {
            auto* rec = static_cast<Core::Water::SurfaceColumn*>(vol.surfUp.mapped);
            std::vector<uint32_t> cols; cols.reserve(vol.surfPending.size());
            for (const auto& [c, r] : vol.surfPending) if (c < vol.dColumns) { rec[c] = r; cols.push_back(c); }   // later writes win
            std::sort(cols.begin(), cols.end()); cols.erase(std::unique(cols.begin(), cols.end()), cols.end());
            std::vector<VkBufferCopy> regions;
            const VkDeviceSize R = sizeof(Core::Water::SurfaceColumn);
            for (size_t i = 0; i < cols.size();) {   // runs of consecutive columns, one region each
                size_t j = i + 1; while (j < cols.size() && cols[j] == cols[j - 1] + 1) ++j;
                regions.push_back({cols[i] * R, cols[i] * R, (j - i) * R});
                i = j;
            }
            vkCmdCopyBuffer(cmd, vol.surfUp.buf, vol.surfDense.buf, static_cast<uint32_t>(regions.size()), regions.data());
            barrier(cmd);
            vol.surfPending.clear();
            meshFull = true;
        }
    }
    dispatch(cmd, *vol.pipes[Surface], push, colsSurf);
    stamp(cmd, vol, TsSurfaceKernel);
    if (gpuSurf) {   // 29 S1: the mesh - the covered columns and a ring round them (or every column), then the extras list
        if (meshFull) { mp.mode = 3; dispatch(cmd, *vol.pipes[SurfaceMesh], mp, denseCols); }
        else { mp.mode = 0; dispatch(cmd, *vol.pipes[SurfaceMesh], mp, static_cast<size_t>(vol.colCount) * 100); }
        // 29 S2: the tall floor's numbers - per brick column (the covered ones, or all after host writes / a full pass), folded
        if (meshFull && !s_tallAggNoFullForTest) { mp.mode = 7; dispatch(cmd, *vol.pipes[SurfaceMesh], mp, static_cast<size_t>(vol.bn.x) * vol.bn.z); }
        else { mp.mode = 5; dispatch(cmd, *vol.pipes[SurfaceMesh], mp, static_cast<size_t>(vol.colCount)); }
        mp.mode = 6; dispatch(cmd, *vol.pipes[SurfaceMesh], mp, 64);
        {   // 29 S2: motion - the covered columns (or every column after host writes / a full pass)
            const int32_t init[10] = {0, 0, 0, 0, INT32_MAX, INT32_MIN, INT32_MAX, INT32_MIN, INT32_MAX, INT32_MIN};
            vkCmdUpdateBuffer(cmd, vol.motionOut.buf, 0, sizeof(init), init);
            barrier(cmd);
            mp.vcBand = s_motionMoveM; mp.dt = s_motionHalfThresholdForTest ? 2.0f : 1.0f;
            if (meshFull) { mp.mode = 9; dispatch(cmd, *vol.pipes[SurfaceMesh], mp, denseCols); }
            else { mp.mode = 8; dispatch(cmd, *vol.pipes[SurfaceMesh], mp, static_cast<size_t>(vol.colCount) * 64); }
            copy(cmd, vol.motionOut, vol.motionOutHost, 40);
        }
        vkCmdFillBuffer(cmd, vol.meshArgs.buf, 4, 4, 0);    // instanceCount
        vkCmdFillBuffer(cmd, vol.meshArgs.buf, 16, 8, 0);   // wanted, dropped
        barrier(cmd);
        mp.mode = 1; mp.iparam = static_cast<int32_t>(vol.meshListCap); dispatch(cmd, *vol.pipes[SurfaceMesh], mp, denseCols);
        copy(cmd, vol.meshArgs, vol.meshArgsHost, 32);
        vol.meshFullNext = false; vol.meshValid = true;
    }
    // 26.11: only the packed lists cross to the host - the counts, then as many entries as the last call's lists + 25 % + 64 (all of
    // them on a first call); a longer list is completed after the fence. Copying every column cost 0.57 ms of the creek's 1.6.
    const VkDeviceSize kRec[3] = {32, 32, sizeof(Core::Water::SurfaceColumn)};                       // one-run, births, multi-run
    const VkDeviceSize srcOff[3] = {0, 0, vol.columns * 32};                                            // in surf / birthsPacked / surf
    const VkDeviceSize dstOff[3] = {vol.offSurf + 64, vol.offBirths, vol.offSurf + 64 + vol.columns * 32};
    const Buffer* src[3] = {&vol.surf, &vol.birthsPacked, &vol.surf};
    for (int l = 0; l < 3; ++l) {
        vol.copied[l] = vol.listsSized ? std::min<uint32_t>(static_cast<uint32_t>(colsSurf), vol.lastCount[l] + vol.lastCount[l] / 4 + 64) : static_cast<uint32_t>(colsSurf);
        copy(cmd, *src[l], vol.staging, vol.copied[l] * kRec[l], srcOff[l], dstOff[l]);
    }
    copy(cmd, vol.lists, vol.staging, 16, 0, vol.offSurf);
    if (surfPartial) { vol.colCount = fullCols; fillPushBricks(vol, push); push.setFrozenOn(0); }
    stamp(cmd, vol, TsSurfaceCopy);
    if (gpuSurf && m_pwWins.buf != VK_NULL_HANDLE && !vol.windows.empty() && vol.winReq.mapped) {
        // 31 (M4): the pressure windows for the debris solver - after the last substep (its p), with the dense field (the recorded
        // columns' levels) current. The frame before's debris ticks may still read them (one queue: an execution dependency).
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 0, nullptr);
        const size_t nw = std::min<size_t>(vol.windows.size(), DebrisShared::MAX_PRESSURE_WINDOWS);
        std::memcpy(vol.winReq.mapped, vol.windows.data(), nw * sizeof(PressureWindowReq));
        WcPush wp = push; fillPushBricks(vol, wp); wp.setFrozenOn(0);
        wp.iparam = static_cast<int32_t>(nw);
        dispatch(cmd, *vol.pipes[BodyPressure], wp, nw * 64);
        vol.lastWindows = static_cast<uint32_t>(nw);
    } else vol.lastWindows = 0;
    push.param0 = 0.0f; push.param1 = 0.0f; push.iparam = 0;
    stamp(cmd, vol, TsReduceSurface);
    if (vol.sourceCount > 0) copy(cmd, vol.sources, vol.staging, vol.sourceCount * sizeof(GpuSource), 0, vol.offSources);   // 26.16: the totals ride the call
    if (vol.callTs) vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, vol.callTs, 1);
    // 26.16: submit on the volume's own fence; SYNC collects at once, ASYNC at the next call (or the next host entry: settle)
    WaterHostProfile::lap("gpu.08 record the commands", hp);
    vkEndCommandBuffer(cmd);
    if (!vol.callFence) { VkFenceCreateInfo fi{}; fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO; vkCreateFence(m_device, &fi, nullptr, &vol.callFence); }
    vkResetFences(m_device, 1, &vol.callFence);
    VkSubmitInfo si{}; si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO; si.commandBufferCount = 1; si.pCommandBuffers = &cmd;
    vol.pend.tSubmit = std::chrono::steady_clock::now();
    vol.lastDispatches = static_cast<int>(m_dispatchCount); vol.lastBarriers = static_cast<int>(m_barrierCount);   // 28
    const VkResult sub = vkQueueSubmit(m_queue, 1, &si, vol.callFence);
    if (sub != VK_SUCCESS) std::fprintf(stderr, "[WaterCoreGpu] step submit %d\n", static_cast<int>(sub));
    WaterHostProfile::lap("gpu.09 submit", hp);
    vol.callCmd = cmd; vol.inFlight = true; vol.pend.callId = ++vol.callsSubmitted;   // 27.15
    vol.pend.n = n; vol.pend.ticks = ticks; vol.pend.capture = capture; vol.pend.params = params; vol.pend.dt = dt;
    vol.pend.speedLane = vol.lastSpeedLane; st.reduceSlots = vol.lastReduceSlots; st.reduceCached = vol.lastReduceCached;   // 27.23 W1e
    vol.pend.t0 = t0; vol.pend.tResidency = tResidency;
    st.hostResidencyMs = std::chrono::duration<double, std::milli>(tResidency - t0).count();
    vol.pend.st = st;
    if (!vol.async) { finishCall(vol); return vol.lastStats; }
    if (vol.haveLast) return vol.lastStats;   // the call before this one (collected at the top)
    st.asleep = vol.asleep; st.quietTicks = vol.quietTicks;   // the first call: nothing collected yet
    if (vol.bricked) { st.bricks = vol.storedCount; st.groundBricks = vol.groundCount; }
    return st;
}

// 26.16: everything a call's results feed on the host, once its fence has signalled - the timings, the test hooks, the list
// completion, the copies the host reads between calls (surface, births, sources), the stats, the brick record, the sleep rule.
void WaterCoreGpu::finishCall(Volume& vol) {
    if (!vol.inFlight) return;
    const auto tWait = std::chrono::steady_clock::now();
    auto hp = tWait;   // 27.28
    const VkResult wait = vkWaitForFences(m_device, 1, &vol.callFence, VK_TRUE, UINT64_MAX);
    WaterHostProfile::lap("collect.1 fence wait", hp);
    if (wait != VK_SUCCESS) std::fprintf(stderr, "[WaterCoreGpu] step wait %d (VK_ERROR_DEVICE_LOST = -4)\n", static_cast<int>(wait));
    const auto tFence = std::chrono::steady_clock::now();
    vkFreeCommandBuffers(m_device, m_pool, 1, &vol.callCmd);
    vol.callCmd = VK_NULL_HANDLE; vol.inFlight = false;
    GpuStepStats st = vol.pend.st;
    const SolverParams& params = vol.pend.params;
    const int n = vol.pend.n, ticks = vol.pend.ticks;
    const bool capture = vol.pend.capture;
    const VkDeviceSize kRec[3] = {32, 32, sizeof(Core::Water::SurfaceColumn)};                       // one-run, births, multi-run
    const VkDeviceSize srcOff[3] = {0, 0, vol.columns * 32};                                            // in surf / birthsPacked / surf
    const VkDeviceSize dstOff[3] = {vol.offSurf + 64, vol.offBirths, vol.offSurf + 64 + vol.columns * 32};
    const Buffer* src[3] = {&vol.surf, &vol.birthsPacked, &vol.surf};
    if (vol.callTs) {   // 26.15: the device's own time (the fence has signalled: the results are ready)
        uint64_t q[2] = {0, 0};
        if (vkGetQueryPoolResults(m_device, vol.callTs, 0, 2, sizeof(q), q, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS && q[1] >= q[0]) {
            VkPhysicalDeviceProperties pp; vkGetPhysicalDeviceProperties(m_physical, &pp);
            st.deviceMs = static_cast<double>(q[1] - q[0]) * pp.limits.timestampPeriod * 1e-6;
        }
    }
    if (vol.tsCount > 1) {   // 26.11 test hook: per-stage GPU time
        std::array<uint64_t, 64> q{};
        if (vkGetQueryPoolResults(m_device, vol.tsPool, 0, static_cast<uint32_t>(vol.tsCount), vol.tsCount * sizeof(uint64_t), q.data(), sizeof(uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT) == VK_SUCCESS) {
            VkPhysicalDeviceProperties pp; vkGetPhysicalDeviceProperties(m_physical, &pp);
            for (int k = 1; k < vol.tsCount; ++k) s_tsMs[vol.tsLabel[static_cast<size_t>(k)]] += static_cast<double>(q[static_cast<size_t>(k)] - q[static_cast<size_t>(k - 1)]) * pp.limits.timestampPeriod * 1e-6;
            ++s_tsCalls;
        }
    }
    if (vol.unitPending && vol.unitCap.mapped) {   // 26.31 measurement
        const size_t units = static_cast<size_t>(vol.bn.x) * vol.nuy * static_cast<size_t>(vol.bn.z);
        const auto* u = static_cast<const glm::vec4*>(vol.unitCap.mapped);
        vol.hostUnits.assign(u, u + units);
        vol.unitPending = false;
        vol.unitsFresh = true; vol.unitsTicks = ticks; vol.unitsDt = vol.pend.dt;   // 26.31; 27.26 the tick
    }
    if (capture && vol.stageCap.buf) {   // 26.10 test hook: exact stage sums (double on the host)
        const auto* sc = static_cast<const float*>(vol.stageCap.mapped);
        const size_t lc = liveCells(vol);
        double sum[kStages];
        for (int k = 0; k < kStages; ++k) { double a = 0.0; for (size_t i = 0; i < lc; ++i) a += sc[k * lc + i]; sum[k] = a; }
        if (vol.stageLast >= 0.0) s_stageDm[0] += sum[0] - vol.stageLast;
        for (int k = 1; k < kStages; ++k) s_stageDm[k] += sum[k] - sum[k - 1];
        vol.stageLast = sum[kStages - 1]; ++s_stageCalls;
    } else vol.stageLast = -1.0;
    // 26.16: SYNC - submit to fence (the GPU's time + the queue ahead of it); ASYNC - only the wait at collection
    st.submitWaitMs = std::chrono::duration<double, std::milli>(tFence - (vol.async ? tWait : vol.pend.tSubmit)).count();
    {   // 26.11: complete a list that outgrew its predicted copy (one more fenced copy, rare: the list grew by > 25 % + 64)
        uint32_t counts[3]; std::memcpy(counts, static_cast<const char*>(vol.staging.mapped) + vol.offSurf, sizeof(counts));
        if (counts[0] > vol.copied[0] || counts[1] > vol.copied[1] || counts[2] > vol.copied[2]) {
            VkCommandBuffer more = beginCommands();
            for (int l = 0; l < 3; ++l) if (counts[l] > vol.copied[l])
                copy(more, *src[l], vol.staging, (counts[l] - vol.copied[l]) * kRec[l], srcOff[l] + vol.copied[l] * kRec[l], dstOff[l] + vol.copied[l] * kRec[l]);
            submitAndWait(more);
        }
        for (int l = 0; l < 3; ++l) { vol.lastCount[l] = counts[l]; vol.copied[l] = counts[l]; }
        vol.listsSized = true;
        // 26.16: the host's copies - the next call writes the staging lists while the renderer reads these
        const char* sm = static_cast<const char*>(vol.staging.mapped);
        vol.hostCount = {counts[0], counts[1], counts[2]};
        vol.hostSurfCols = std::move(vol.pend.surfCols); vol.pend.surfCols.clear(); ++vol.surfCollected;   // 26.33
        vol.surfCallId = vol.pend.callId;   // 27.15
        vol.hostSurf.resize(static_cast<size_t>(counts[0]) * 32 + static_cast<size_t>(counts[2]) * kRec[2]);
        if (counts[0]) std::memcpy(vol.hostSurf.data(), sm + dstOff[0], static_cast<size_t>(counts[0]) * 32);
        if (counts[2]) std::memcpy(vol.hostSurf.data() + static_cast<size_t>(counts[0]) * 32, sm + dstOff[2], static_cast<size_t>(counts[2]) * kRec[2]);
        vol.hostBirths.resize(static_cast<size_t>(counts[1]) * 32);
        if (counts[1]) std::memcpy(vol.hostBirths.data(), sm + dstOff[1], static_cast<size_t>(counts[1]) * 32);
        if (vol.sourceCount > 0) {
            vol.hostSources.resize(static_cast<size_t>(vol.sourceCount));
            std::memcpy(vol.hostSources.data(), sm + vol.offSources, vol.sourceCount * sizeof(GpuSource));
        }
    }
    WaterHostProfile::lap("collect.2 lists + copies", hp);
    if (vol.meshArgsHost.mapped) {   // 29 S1: the extras list as the call left it
        const auto* a = static_cast<const uint32_t*>(vol.meshArgsHost.mapped);
        vol.lastMeshEntries = a[4]; vol.lastMeshDropped = a[5];
        float mt, ws; std::memcpy(&mt, &a[6], 4); std::memcpy(&ws, &a[7], 4);   // 29 S2
        vol.fieldMinTop = mt; vol.fieldWaterSum = ws; vol.fieldStatsValid = vol.meshValid && s_gpuSurface;
        if (vol.motionOutHost.mapped) {   // 29 S2: the call's motion
            const auto* m = static_cast<const int32_t*>(vol.motionOutHost.mapped);
            auto unorder = [](int32_t i) { const int32_t b = i >= 0 ? i : (i ^ 0x7FFFFFFF); float f; std::memcpy(&f, &b, 4); return f; };
            auto bitsF = [](int32_t i) { float f; std::memcpy(&f, &i, 4); return f; };
            Volume::Motion mo; mo.fresh = true; mo.riseCols = m[0]; mo.flowCols = m[1]; mo.maxRise = bitsF(m[2]); mo.maxFlow = bitsF(m[3]);
            mo.x0 = m[4]; mo.x1 = m[5]; mo.z0 = m[6]; mo.z1 = m[7];
            if (mo.riseCols > 0) { mo.y0 = unorder(m[8]); mo.y1 = unorder(m[9]); }
            vol.motion = mo;
        }
    }
    vol.surfaceValid = true;
    vol.birthsFresh = true;
    // 26.16: the host's share of the call - SYNC: start to here; ASYNC: recording + this collection
    st.gpuMs = vol.async ? std::chrono::duration<double, std::milli>((vol.pend.tSubmit - vol.pend.t0) + (std::chrono::steady_clock::now() - tWait)).count()
                         : std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - vol.pend.t0).count();
    const auto* out = reinterpret_cast<const float*>(static_cast<const char*>(vol.staging.mapped) + vol.offOut);
    st.substepsLast = n;
    st.kineticEnergy = out[0]; st.maxDeltaF = out[1]; st.totalMass = static_cast<double>(out[2]) * vol.spec.h * vol.spec.h * vol.spec.h;
    st.maxSpeed = vol.pend.speedLane ? out[3] : std::max({out[4 + 3], out[8 + 3], out[12 + 3]});   // 27.23 W1e: the speed lane
    st.rbgsResidualMax = out[16 + 3];
    st.residueDropped = out[20];
    if (vol.bricked) {
        const auto* bi = reinterpret_cast<const int32_t*>(static_cast<const char*>(vol.staging.mapped) + vol.offBricks);
        int32_t counters[kCounters]; std::memcpy(counters, bi + vol.counterOff, sizeof(counters));
        const double cellM3 = static_cast<double>(vol.spec.h) * vol.spec.h * vol.spec.h * 1e-3;   // the carry counters are 1e-3 cells
        st.carryBodyM3 = counters[2] * cellM3; st.carryFlowM3 = counters[3] * cellM3; st.carryParkedM3 = counters[4] * cellM3; st.carryRateM3 = counters[5] * cellM3;
        vol.hostInfo.resize(2 * vol.capacity);
        std::memcpy(vol.hostInfo.data(), bi + vol.infoOff, 2 * static_cast<size_t>(vol.slotsUsed) * 4);   // one bulk read of uncached memory
        st.fluxIntoAbsent = counters[0]; st.sentinelDirty = counters[1] != 0;
        st.bricks = vol.storedCount; st.groundBricks = vol.groundCount;
        st.allocated = vol.lastAllocated; st.freed = vol.lastFreed; st.heldByCapacity = vol.lastHeld; st.haloRings = vol.lastRings;
        st.compacted = vol.lastCompacted;
        st.unstoredWater = vol.unstoredM3;
        if (vol.sparse) {   // 29 S3: every synchronous call; a live one once a second (the last value between)
            const auto now = std::chrono::steady_clock::now();
            if (!vol.async || s_haloCheckEveryCall || now - vol.lastHaloCheck >= std::chrono::seconds(1)) { vol.lastHaloShort = haloShortAfterCall(vol); vol.lastHaloCheck = now; }
            st.haloShort = vol.lastHaloShort;
        }
        st.hostAfterMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tFence).count();
        vol.recordValid = vol.sparse;
    }
    vol.lastMaxSpeed = st.maxSpeed;
    const double specificKE = st.kineticEnergy / std::max(static_cast<double>(out[2]), 1e-9);
    const double rate = s_freezePerTickForTest ? 1.0 : static_cast<double>(vol.pend.dt) * 60.0;   // 27.26: the sleep test in time
    const bool quiet = specificKE < params.keWake && st.maxDeltaF < params.maxDeltaFQuiet * rate;
    vol.quietRef = quiet ? vol.quietRef + (s_freezePerTickForTest ? static_cast<double>(ticks) : static_cast<double>(ticks) * rate) : 0.0;
    vol.quietTicks = static_cast<int>(std::min(vol.quietRef, 1e9));
    vol.quietBefore = specificKE < params.keSettle;   // the settle band (SolverParams::keSettle), as on the CPU: rest damping + compaction only below it
    if (vol.quietTicks >= params.restTicks) vol.asleep = true;
    st.quietTicks = vol.quietTicks; st.asleep = vol.asleep;
    vol.lastStats = st; vol.haveLast = true;
    WaterHostProfile::lap("collect.3 stats + record + sleep", hp);
}

}}} // namespace Phyxel::Core::Water
