// docs/VoxelFoundationPlan.md §3 + §8 — storage lab (F0 skeleton). See StorageLab.h.

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <unordered_map>
#include "core/voxfound/StorageLab.h"

#include <array>
#include <chrono>
#include <tuple>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>

#include "core/WorldGenerator.h"

namespace Phyxel::Core::VoxFound {

namespace {
std::atomic<bool> g_terrainLegacy{false};   // F7.9a
std::atomic<bool> g_roadGradeStub{false};   // F7.20 (tests): the graded height ignored (the red)
/// F7.20: the 1/8 m terrain's continuous ground -- the graded height on a graded road column, else bedHeight.
inline float groundOf(const WorldGenerator::ColumnSample& c) {
    return (c.graded && !g_roadGradeStub.load()) ? c.gradedHeight : c.bedHeight;
}
}
void setTerrainLegacy(bool on) { g_terrainLegacy = on; }
void setRoadGradeStub(bool on) { g_roadGradeStub = on; }

VoxelArea buildTerrainAreaReference(WorldGenerator& gen, glm::ivec3 o, glm::ivec3 sz) {
    VoxelArea a;
    a.originVoxel = o * kVoxelsPerM;
    a.grid.resize(sz * kVoxelsPerM);
    a.palette = {"air"};
    std::map<std::string, uint8_t> ids;
    auto idOf = [&](const std::string& name) -> uint8_t {
        auto it = ids.find(name);
        if (it != ids.end()) return it->second;
        const uint8_t id = uint8_t(a.palette.size());
        a.palette.push_back(name);
        ids.emplace(name, id);
        return id;
    };

    // Metre samples with a one-column margin for the bilinear interpolation.
    const int nx = sz.x + 2, nz = sz.z + 2;
    std::vector<WorldGenerator::ColumnSample> cols(size_t(nx) * size_t(nz));
    for (int i = 0; i < nx; ++i)
        for (int k = 0; k < nz; ++k) cols[size_t(i) * nz + k] = gen.sampleSurface(o.x - 1 + i, o.z - 1 + k);
    auto col = [&](int i, int k) -> const WorldGenerator::ColumnSample& { return cols[size_t(i + 1) * nz + (k + 1)]; };

    // The continuous ground: bedHeight is where today's cube terrain rounds from (surfaceY = floor(bedHeight),
    // solid up to surfaceY + 1, i.e. on average bedHeight + 0.5). At 1/8 m the voxel whose centre lies below
    // bedHeight + 0.5 is solid: the same mean ground level as today, followed at 1/8 m steps instead of 1 m steps
    // (at a column centre it is within half a metre of today's cube top). Between metre column centres
    // (m + 0.5) the height is bilinear. F7.20: on a graded road column the ground is its gradedHeight (groundOf).
    const glm::ivec3 d = a.grid.dim;
    for (int vx = 0; vx < d.x; ++vx) {
        const float wx = float(o.x) + (vx + 0.5f) / kVoxelsPerM;
        const float fx = wx - 0.5f - float(o.x);
        const int ix = int(std::floor(fx));
        const float tx = fx - float(ix);
        for (int vz = 0; vz < d.z; ++vz) {
            const float wz = float(o.z) + (vz + 0.5f) / kVoxelsPerM;
            const float fz = wz - 0.5f - float(o.z);
            const int iz = int(std::floor(fz));
            const float tz = fz - float(iz);
            const float h00 = groundOf(col(ix, iz)), h10 = groundOf(col(ix + 1, iz));
            const float h01 = groundOf(col(ix, iz + 1)), h11 = groundOf(col(ix + 1, iz + 1));
            const float h = (h00 * (1 - tx) + h10 * tx) * (1 - tz) + (h01 * (1 - tx) + h11 * tx) * tz;
            const auto& c = col(int(std::floor(wx)) - o.x, int(std::floor(wz)) - o.z);  // materials: own metre
            // Material layers count whole metres down from the metre that holds the column's TOP voxel (the last
            // centre below h + 0.5), so the top voxel is always the surface material, as today's top cube is, and
            // the layer boundaries stay on metre planes (block-aligned). F2.19: counting from floor(h + 0.5)
            // instead put the top voxels at depth 1 (Dirt) wherever the ground had just crossed a whole metre --
            // dirt stripes along every contour line.
            int topAbs = int(std::floor((h + 0.5f) * kVoxelsPerM - 0.5f));  // absolute voxel index, then exact
            while ((float(topAbs) + 0.5f) / kVoxelsPerM >= h + 0.5f) --topAbs;
            while ((float(topAbs + 1) + 0.5f) / kVoxelsPerM < h + 0.5f) ++topAbs;
            const int surfaceMetre = int(std::floor((float(topAbs) + 0.5f) / kVoxelsPerM));
            for (int vy = 0; vy < d.y; ++vy) {
                const float wy = float(o.y) + (vy + 0.5f) / kVoxelsPerM;
                if (wy >= h + 0.5f) break;
                const int my = int(std::floor(wy));
                WorldGenerator::ColumnSample cc = c;
                cc.surfaceY = surfaceMetre;
                a.grid.set({vx, vy, vz}, idOf(gen.columnMaterialAt(my, cc)));
            }
        }
    }
    return a;
}

VoxelArea buildTerrainArea(WorldGenerator& gen, glm::ivec3 o, glm::ivec3 sz) {
    if (g_terrainLegacy.load()) return buildTerrainAreaReference(gen, o, sz);   // F7.9a A/B
    VoxelArea a;
    a.originVoxel = o * kVoxelsPerM;
    a.grid.resize(sz * kVoxelsPerM);
    a.palette = {"air"};
    std::map<std::string, uint8_t> ids;
    auto idOf = [&](const std::string& name) -> uint8_t {
        auto it = ids.find(name);
        if (it != ids.end()) return it->second;
        const uint8_t id = uint8_t(a.palette.size());
        a.palette.push_back(name);
        ids.emplace(name, id);
        return id;
    };

    // Metre samples with a one-column margin for the bilinear interpolation.
    const int nx = sz.x + 2, nz = sz.z + 2;
    std::vector<WorldGenerator::ColumnSample> cols(size_t(nx) * size_t(nz));
    for (int i = 0; i < nx; ++i)
        for (int k = 0; k < nz; ++k) cols[size_t(i) * nz + k] = gen.sampleSurface(o.x - 1 + i, o.z - 1 + k);
    auto col = [&](int i, int k) -> const WorldGenerator::ColumnSample& { return cols[size_t(i + 1) * nz + (k + 1)]; };

    // The continuous ground: bedHeight is where today's cube terrain rounds from (surfaceY = floor(bedHeight),
    // solid up to surfaceY + 1, i.e. on average bedHeight + 0.5). At 1/8 m the voxel whose centre lies below
    // bedHeight + 0.5 is solid: the same mean ground level as today, followed at 1/8 m steps instead of 1 m steps
    // (at a column centre it is within half a metre of today's cube top). Between metre column centres
    // (m + 0.5) the height is bilinear. F7.20: on a graded road column the ground is its gradedHeight (groundOf).
    const glm::ivec3 d = a.grid.dim;
    std::unordered_map<uint64_t, std::vector<int16_t>> metreCache;   // F7.9a
    for (int vx = 0; vx < d.x; ++vx) {
        const float wx = float(o.x) + (vx + 0.5f) / kVoxelsPerM;
        const float fx = wx - 0.5f - float(o.x);
        const int ix = int(std::floor(fx));
        const float tx = fx - float(ix);
        for (int vz = 0; vz < d.z; ++vz) {
            const float wz = float(o.z) + (vz + 0.5f) / kVoxelsPerM;
            const float fz = wz - 0.5f - float(o.z);
            const int iz = int(std::floor(fz));
            const float tz = fz - float(iz);
            const float h00 = groundOf(col(ix, iz)), h10 = groundOf(col(ix + 1, iz));
            const float h01 = groundOf(col(ix, iz + 1)), h11 = groundOf(col(ix + 1, iz + 1));
            const float h = (h00 * (1 - tx) + h10 * tx) * (1 - tz) + (h01 * (1 - tx) + h11 * tx) * tz;
            const int ixm = int(std::floor(wx)) - o.x, izm = int(std::floor(wz)) - o.z;
            const auto& c = col(ixm, izm);  // materials: own metre
            // Material layers count whole metres down from the metre that holds the column's TOP voxel (the last
            // centre below h + 0.5), so the top voxel is always the surface material, as today's top cube is, and
            // the layer boundaries stay on metre planes (block-aligned). F2.19: counting from floor(h + 0.5)
            // instead put the top voxels at depth 1 (Dirt) wherever the ground had just crossed a whole metre --
            // dirt stripes along every contour line.
            int topAbs = int(std::floor((h + 0.5f) * kVoxelsPerM - 0.5f));  // absolute voxel index, then exact
            while ((float(topAbs) + 0.5f) / kVoxelsPerM >= h + 0.5f) --topAbs;
            while ((float(topAbs + 1) + 0.5f) / kVoxelsPerM < h + 0.5f) ++topAbs;
            const int surfaceMetre = int(std::floor((float(topAbs) + 0.5f) / kVoxelsPerM));
            // F7.9a: the generator's material is a function of (metre column, surface metre, metre): looked up once
            // per key and kept (was one string + map lookup per voxel)
            const uint64_t key = (uint64_t(uint32_t(ixm + 1)) << 40) | (uint64_t(uint32_t(izm + 1)) << 20) |
                                 uint64_t(uint32_t(surfaceMetre - o.y + 65536) & 0xFFFFFu);
            std::vector<int16_t>& mats = metreCache[key];
            if (mats.empty()) mats.assign(size_t(sz.y) + 1, int16_t(-1));
            for (int vy = 0; vy < d.y; ++vy) {
                const float wy = float(o.y) + (vy + 0.5f) / kVoxelsPerM;
                if (wy >= h + 0.5f) break;
                const int my = int(std::floor(wy));
                int16_t& id = mats[size_t(my - o.y)];
                if (id < 0) {
                    WorldGenerator::ColumnSample cc = c;
                    cc.surfaceY = surfaceMetre;
                    id = int16_t(idOf(gen.columnMaterialAt(my, cc)));
                }
                a.grid.set({vx, vy, vz}, uint8_t(id));
            }
        }
    }
    return a;
}

GridTemplate loadGridTemplate(const std::string& path) {
    GridTemplate t;
    std::ifstream in(path);
    if (!in) {
        t.error = "cannot open " + path;
        return t;
    }
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (line.rfind("# grid:", 0) == 0) {
            t.cellsPerM = std::atoi(line.c_str() + 7);
            continue;
        }
        if (line[0] == '#') continue;
        std::istringstream ss(line);
        char kind = 0;
        ss >> kind;
        GridTemplate::Box b;
        if (kind == 'V') {
            ss >> b.pos.x >> b.pos.y >> b.pos.z >> b.material;
            b.size = glm::ivec3(1);
        } else if (kind == 'B') {
            ss >> b.pos.x >> b.pos.y >> b.pos.z >> b.size.x >> b.size.y >> b.size.z >> b.material;
        } else {
            t.error = "line not on the foundation grid (only B/V allowed): " + line;
            return t;
        }
        if (!ss || b.material.empty()) {
            t.error = "malformed line: " + line;
            return t;
        }
        if (t.cellsPerM == 0) {
            t.error = "geometry before '# grid:'";
            return t;
        }
        t.bounds = glm::max(t.bounds, b.pos + b.size);
        t.cells += size_t(b.size.x) * size_t(b.size.y) * size_t(b.size.z);
        t.boxes.push_back(std::move(b));
    }
    if (t.cellsPerM != kVoxelsPerM && t.error.empty())
        t.error = "grid " + std::to_string(t.cellsPerM) + " is not the world grid (" + std::to_string(kVoxelsPerM) + ")";
    return t;
}

FloraStampStats stampFlora(VoxelArea& a, WorldGenerator& gen, const std::string& dir, int marginM,
                           const glm::ivec4* window) {
    FloraStampStats st;
    const glm::ivec3 oM = a.originVoxel / kVoxelsPerM;
    const glm::ivec3 szM = a.grid.dim / kVoxelsPerM;
    auto plan = gen.planFlora(oM.x - marginM, oM.z - marginM, oM.x + szM.x - 1 + marginM,
                              oM.z + szM.z - 1 + marginM, 0);
    // F2.8 "chunks must not be visible": overlapping trees overwrite in a fixed WORLD order, not in the order
    // this query rectangle happened to return them.
    std::sort(plan.begin(), plan.end(), [](const auto& a, const auto& b) {
        if (a.worldX != b.worldX) return a.worldX < b.worldX;
        if (a.worldZ != b.worldZ) return a.worldZ < b.worldZ;
        return a.templateName < b.templateName;
    });
    std::map<std::string, GridTemplate> cache;
    std::map<std::string, uint8_t> ids;
    for (size_t i = 1; i < a.palette.size(); ++i) ids[a.palette[i]] = uint8_t(i);
    auto idOf = [&](const std::string& n) -> uint8_t {
        auto it = ids.find(n);
        if (it != ids.end()) return it->second;
        const uint8_t id = uint8_t(a.palette.size());
        a.palette.push_back(n);
        ids.emplace(n, id);
        return id;
    };
    // Ground top at a voxel column (first air above the terrain), in area voxels, from the TERRAIN FUNCTION at that
    // world column -- the same bilinear bedHeight and "centre below h + 0.5" rule as buildTerrainArea. F2.8: it
    // used to scan the area's own column, clamped to the area, after earlier trees were stamped, so a tree
    // rooted outside the area (or under another canopy) sat at a height that depended on the area.
    auto groundTop = [&](int vx, int vz) {
        const float wx = float(oM.x) + (vx + 0.5f) / kVoxelsPerM, wz = float(oM.z) + (vz + 0.5f) / kVoxelsPerM;
        const int ix = int(std::floor(wx - 0.5f)), iz = int(std::floor(wz - 0.5f));
        const float tx = (wx - 0.5f) - float(ix), tz = (wz - 0.5f) - float(iz);
        const float h00 = groundOf(gen.sampleSurface(ix, iz)), h10 = groundOf(gen.sampleSurface(ix + 1, iz));
        const float h01 = groundOf(gen.sampleSurface(ix, iz + 1)), h11 = groundOf(gen.sampleSurface(ix + 1, iz + 1));
        const float h = (h00 * (1 - tx) + h10 * tx) * (1 - tz) + (h01 * (1 - tx) + h11 * tx) * tz;
        // first voxel (absolute) whose centre is at or above h + 0.5
        int vyAbs = int(std::floor((h + 0.5f) * kVoxelsPerM - 0.5f)) - 1;
        while ((float(vyAbs) + 0.5f) / kVoxelsPerM < h + 0.5f) ++vyAbs;
        while ((float(vyAbs - 1) + 0.5f) / kVoxelsPerM >= h + 0.5f) --vyAbs;
        return vyAbs - a.originVoxel.y;
    };
    for (const auto& p : plan) {
        if (window && (p.worldX < window->x || p.worldZ < window->y || p.worldX > window->z || p.worldZ > window->w))
            continue;
        ++st.placements;
        if (p.procedural) {  // in-engine ProceduralTree (a port of the deprecated gen_tree.py): not on the grid yet
            ++st.procedural;
            continue;
        }
        auto it = cache.find(p.templateName);
        if (it == cache.end()) it = cache.emplace(p.templateName, loadGridTemplate(dir + "/" + p.templateName + ".voxel")).first;
        const GridTemplate& t = it->second;
        if (!t.error.empty()) {
            ++st.missing;
            if (std::find(st.missingNames.begin(), st.missingNames.end(), p.templateName) == st.missingNames.end())
                st.missingNames.push_back(p.templateName);
            continue;
        }
        ++st.stamped;
        ++st.stampedByName[p.templateName];
        // column centre in area voxels; the trunk base sits on the ground there
        const int cx = (p.worldX - oM.x) * kVoxelsPerM + kVoxelsPerM / 2;
        const int cz = (p.worldZ - oM.z) * kVoxelsPerM + kVoxelsPerM / 2;
        const glm::ivec3 base(cx - t.bounds.x / 2, groundTop(cx, cz), cz - t.bounds.z / 2);
        for (const auto& b : t.boxes) {
            const uint8_t m = idOf(b.material);
            for (int x = 0; x < b.size.x; ++x)
                for (int y = 0; y < b.size.y; ++y)
                    for (int z = 0; z < b.size.z; ++z) {
                        const glm::ivec3 q = base + b.pos + glm::ivec3(x, y, z);
                        if (!a.grid.in(q)) continue;
                        a.grid.mat[a.grid.idx(q)] = m;
                        ++st.cellsWritten;
                    }
        }
    }
    return st;
}

RegionBuild forEachRegionChunk(
    WorldGenerator& gen, glm::ivec3 lo, glm::ivec3 hi, const std::string* floraDir,
    const std::function<void(const RegionChunk&, const std::vector<std::string>& palette)>& each,
    const glm::ivec4* plantWindow, int threads) {
    RegionBuild rb;
    std::map<std::string, uint8_t> ids{{"air", 0}};
    const glm::ivec3 inner(kChunkM * kVoxelsPerM);
    std::vector<glm::ivec3> order;
    for (int cx = lo.x; cx <= hi.x; ++cx)
        for (int cy = lo.y; cy <= hi.y; ++cy)
            for (int cz = lo.z; cz <= hi.z; ++cz) order.push_back({cx, cy, cz});
    // F7.9a: chunk areas (terrain + flora) on worker threads, one generator copy each (ChunkStreamingManager's
    // pattern), consumed here in order; at most 2 x threads areas in flight (~80 MB each)
    struct Slot {
        bool ready = false;
        VoxelArea area;
        FloraStampStats flora;
        VoxelGrid grid;      // F7.9a: the chunk cut and its border, made on the worker (palette-independent)
        Neighbours border;
        std::string error;
    };
    const int nThreads = std::clamp(threads, 1, 32);
    std::vector<Slot> slots(order.size());
    std::mutex mu;
    std::condition_variable cv;
    size_t consumed = 0;
    std::atomic<size_t> next{0};
    std::atomic<bool> stop{false};
    const size_t inFlight = size_t(nThreads) * 2;
    auto produce = [&](WorldGenerator& g, size_t k) {
        Slot s;
        try {
            s.area = buildTerrainArea(g, order[k] * kChunkM - 1, glm::ivec3(kChunkM + 2));
            if (floraDir) s.flora = stampFlora(s.area, g, *floraDir, 24, plantWindow);
            const glm::ivec3 off(kVoxelsPerM);
            s.grid = subGrid(s.area.grid, off, inner);
            s.border = neighboursOf(s.area.grid, off, inner, Outside::Air);  // the margin covers every border voxel
        } catch (const std::exception& e) {
            s.error = e.what();
        }
        s.ready = true;
        return s;
    };
    std::vector<std::thread> workers;
    if (nThreads > 1)
        for (int t = 0; t < nThreads; ++t)
            workers.emplace_back([&, gcopy = std::make_shared<WorldGenerator>(gen)]() mutable {
                for (;;) {
                    const size_t k = next.fetch_add(1);
                    if (k >= order.size() || stop) return;
                    {
                        std::unique_lock<std::mutex> lk(mu);
                        cv.wait(lk, [&] { return stop || k < consumed + inFlight; });
                        if (stop) return;
                    }
                    Slot s = produce(*gcopy, k);
                    {
                        std::lock_guard<std::mutex> lk(mu);
                        slots[k] = std::move(s);
                    }
                    cv.notify_all();
                }
            });
    struct Joiner {
        std::vector<std::thread>& w;
        std::atomic<bool>& stop;
        std::condition_variable& cv;
        ~Joiner() {
            stop = true;
            cv.notify_all();
            for (auto& t : w) t.join();
        }
    } joiner{workers, stop, cv};
    for (size_t k = 0; k < order.size(); ++k) {
                const glm::ivec3 c = order[k];
                Slot s;
                if (nThreads > 1) {
                    std::unique_lock<std::mutex> lk(mu);
                    cv.wait(lk, [&] { return slots[k].ready; });
                    s = std::move(slots[k]);
                    slots[k] = Slot{};
                } else {
                    s = produce(gen, k);
                }
                if (!s.error.empty()) throw std::runtime_error(s.error);
                VoxelArea& a = s.area;
                if (floraDir) {
                    const FloraStampStats& f = s.flora;
                    rb.flora.placements += f.placements;
                    rb.flora.stamped += f.stamped;
                    rb.flora.procedural += f.procedural;
                    rb.flora.missing += f.missing;
                    rb.flora.cellsWritten += f.cellsWritten;
                    for (const auto& [n, k] : f.stampedByName) rb.flora.stampedByName[n] += k;
                    for (const auto& n : f.missingNames)
                        if (std::find(rb.flora.missingNames.begin(), rb.flora.missingNames.end(), n) ==
                            rb.flora.missingNames.end())
                            rb.flora.missingNames.push_back(n);
                }
                uint8_t remap[256] = {};
                for (size_t m = 1; m < a.palette.size(); ++m) {
                    auto it = ids.find(a.palette[m]);
                    if (it == ids.end()) {
                        if (rb.palette.size() >= 256) throw std::runtime_error("region palette over 256 materials");
                        it = ids.emplace(a.palette[m], uint8_t(rb.palette.size())).first;
                        rb.palette.push_back(a.palette[m]);
                    }
                    remap[m] = it->second;
                }
                for (uint8_t& v : a.grid.mat) v = remap[v];
                for (uint8_t& v : s.grid.mat) v = remap[v];   // F7.9a: the worker's cut, in the region palette
                RegionChunk rc;
                rc.chunk = c;
                rc.originVoxel = c * kChunkM * kVoxelsPerM;
                rc.grid = std::move(s.grid);
                rc.border = std::move(s.border);
                rc.area = &a.grid;
                rc.areaMarginVoxels = kVoxelsPerM;
                ++rb.chunks;
                each(rc, rb.palette);
                {
                    std::lock_guard<std::mutex> lk(mu);
                    consumed = k + 1;
                }
                cv.notify_all();
            }
    return rb;
}

size_t BlockStats::estimatedBytes() const { return blocks * 8 + detailed * (512 + 64); }

BlockStats blockStats(const VoxelGrid& g) {
    BlockStats s;
    const glm::ivec3 nb = g.dim / kBlock;
    s.blockColumns = size_t(nb.x) * size_t(nb.z);
    for (int bx = 0; bx < nb.x; ++bx)
        for (int by = 0; by < nb.y; ++by)
            for (int bz = 0; bz < nb.z; ++bz) {
                ++s.blocks;
                const uint8_t first = g.mat[g.idx({bx * kBlock, by * kBlock, bz * kBlock})];
                bool uniform = true;
                for (int x = 0; x < kBlock && uniform; ++x)
                    for (int y = 0; y < kBlock && uniform; ++y)
                        for (int z = 0; z < kBlock; ++z)
                            if (g.mat[g.idx({bx * kBlock + x, by * kBlock + y, bz * kBlock + z})] != first) {
                                uniform = false;
                                break;
                            }
                if (!uniform) ++s.detailed;
                else if (first == 0) ++s.uniformAir;
                else ++s.uniformSolid;
            }
    return s;
}

// ---- BlockStore (F1, CPU form) ------------------------------------------------------------------

BlockStore::BlockStore(glm::ivec3 dimVoxels) : m_dim(dimVoxels), m_nb(dimVoxels / kBlock) {
    m_dir.assign(size_t(m_nb.x) * size_t(m_nb.y) * size_t(m_nb.z), 0);  // every block uniform air
}

namespace {
inline int inBlock(glm::ivec3 l) { return l.z + kBlock * (l.y + kBlock * l.x); }  // engine order, Z-minor
}

void BlockStore::set(glm::ivec3 p, uint8_t m) {
    if (p.x < 0 || p.y < 0 || p.z < 0 || p.x >= m_dim.x || p.y >= m_dim.y || p.z >= m_dim.z) return;
    const glm::ivec3 b = p / kBlock;
    const size_t bi = blockIndex(b);
    uint64_t& e = m_dir[bi];
    if (!(e & kDetailed)) {
        const uint8_t u = uint8_t(e & 0xFF);
        if (u == m) return;  // no change: stays one entry
        uint32_t slot;
        if (!m_free.empty()) {
            slot = m_free.back();
            m_free.pop_back();
        } else {
            slot = uint32_t(m_pool.size());
            m_pool.emplace_back();
        }
        Detailed& d = m_pool[slot];
        std::memset(d.mat, u, sizeof d.mat);
        const uint64_t fill = u ? ~0ull : 0ull;
        for (uint64_t& w : d.solid) w = fill;
        e = kDetailed | (e & kChanged) | slot;
    }
    Detailed& d = m_pool[uint32_t(e & 0xFFFFFFFFull)];
    const int i = inBlock(p - b * kBlock);
    d.mat[i] = m;
    if (m) d.solid[i >> 6] |= (1ull << (i & 63));
    else d.solid[i >> 6] &= ~(1ull << (i & 63));
    if (!(e & kChanged)) {
        e |= kChanged;
        m_changed.push_back(uint32_t(bi));
    }
}

uint8_t BlockStore::get(glm::ivec3 p) const {
    if (p.x < 0 || p.y < 0 || p.z < 0 || p.x >= m_dim.x || p.y >= m_dim.y || p.z >= m_dim.z) return 0;
    const glm::ivec3 b = p / kBlock;
    const uint64_t e = m_dir[blockIndex(b)];
    if (!(e & kDetailed)) return uint8_t(e & 0xFF);
    return m_pool[uint32_t(e & 0xFFFFFFFFull)].mat[inBlock(p - b * kBlock)];
}

bool BlockStore::blockSolidAny(glm::ivec3 b) const {
    if (b.x < 0 || b.y < 0 || b.z < 0 || b.x >= m_nb.x || b.y >= m_nb.y || b.z >= m_nb.z) return false;
    const uint64_t e = m_dir[blockIndex(b)];
    if (!(e & kDetailed)) return (e & 0xFF) != 0;
    const Detailed& d = m_pool[uint32_t(e & 0xFFFFFFFFull)];
    uint64_t any = 0;
    for (uint64_t w : d.solid) any |= w;
    return any != 0;
}

int BlockStore::blockSolidCount(glm::ivec3 b) const {
    if (b.x < 0 || b.y < 0 || b.z < 0 || b.x >= m_nb.x || b.y >= m_nb.y || b.z >= m_nb.z) return 0;
    const uint64_t e = m_dir[blockIndex(b)];
    if (!(e & kDetailed)) return (e & 0xFF) != 0 ? 512 : 0;
    const Detailed& d = m_pool[uint32_t(e & 0xFFFFFFFFull)];
    int n = 0;
    for (uint64_t w : d.solid) {   // C++17 has no std::popcount: the constant-time SWAR count
        w = w - ((w >> 1) & 0x5555555555555555ull);
        w = (w & 0x3333333333333333ull) + ((w >> 2) & 0x3333333333333333ull);
        w = (w + (w >> 4)) & 0x0F0F0F0F0F0F0F0Full;
        n += int((w * 0x0101010101010101ull) >> 56);
    }
    return n;
}

void BlockStore::collapse(double slack) {
    // 1. changed detailed blocks that are uniform again return to one directory entry
    for (uint32_t bi : m_changed) {
        uint64_t& e = m_dir[bi];
        e &= ~kChanged;
        if (!(e & kDetailed)) continue;
        const uint32_t slot = uint32_t(e & 0xFFFFFFFFull);
        const Detailed& d = m_pool[slot];
        const uint8_t first = d.mat[0];
        bool uniform = true;
        for (int i = 1; i < 512; ++i)
            if (d.mat[i] != first) {
                uniform = false;
                break;
            }
        if (uniform) {
            e = first;
            m_free.push_back(slot);
        }
    }
    m_changed.clear();
    if (slack <= 0.0) m_changed.shrink_to_fit();
    // 2. compact the pool: move the live blocks at the end into the holes, then shrink. F3.2b: with slack > 0 the
    // free slots are kept for reuse until they exceed that fraction of the pool (set() takes them first).
    if (m_free.empty()) return;
    if (slack > 0.0 && double(m_free.size()) <= slack * double(m_pool.size())) return;
    std::vector<uint32_t> ownerOf(m_pool.size(), UINT32_MAX);  // pool slot -> directory index
    for (size_t bi = 0; bi < m_dir.size(); ++bi)
        if (m_dir[bi] & kDetailed) ownerOf[uint32_t(m_dir[bi] & 0xFFFFFFFFull)] = uint32_t(bi);
    const size_t live = m_pool.size() - m_free.size();
    std::sort(m_free.begin(), m_free.end());
    size_t tail = m_pool.size();
    for (uint32_t hole : m_free) {
        if (hole >= live) break;  // holes past the live range are simply cut off
        do {
            --tail;
        } while (ownerOf[tail] == UINT32_MAX);  // the last live block
        m_pool[hole] = m_pool[tail];
        const uint32_t owner = ownerOf[tail];
        m_dir[owner] = (m_dir[owner] & ~0xFFFFFFFFull) | hole;
        ownerOf[hole] = owner;
        ownerOf[tail] = UINT32_MAX;
    }
    m_pool.resize(live);
    m_pool.shrink_to_fit();
    m_free.clear();
    m_free.shrink_to_fit();
}

BlockStore BlockStore::fromGrid(const VoxelGrid& g) {
    BlockStore s(g.dim);
    for (int bx = 0; bx < s.m_nb.x; ++bx)
        for (int by = 0; by < s.m_nb.y; ++by)
            for (int bz = 0; bz < s.m_nb.z; ++bz) {
                Detailed d;
                bool uniform = true;
                for (int x = 0; x < kBlock; ++x)
                    for (int y = 0; y < kBlock; ++y) {
                        const size_t row = g.idx({bx * kBlock + x, by * kBlock + y, bz * kBlock});
                        std::memcpy(d.mat + inBlock({x, y, 0}), &g.mat[row], kBlock);
                    }
                for (int i = 1; i < 512 && uniform; ++i) uniform = d.mat[i] == d.mat[0];
                uint64_t& e = s.m_dir[s.blockIndex({bx, by, bz})];
                if (uniform) {
                    e = d.mat[0];
                    continue;
                }
                for (uint64_t& w : d.solid) w = 0;
                for (int i = 0; i < 512; ++i)
                    if (d.mat[i]) d.solid[i >> 6] |= 1ull << (i & 63);
                e = kDetailed | uint32_t(s.m_pool.size());
                s.m_pool.push_back(d);
            }
    s.m_pool.shrink_to_fit();
    return s;
}

void BlockStore::toGrid(VoxelGrid& g) const {
    g.dim = m_dim;
    g.mat.assign(size_t(m_dim.x) * size_t(m_dim.y) * size_t(m_dim.z), 0);
    g.part.clear();
    g.fixed.clear();
    for (int bx = 0; bx < m_nb.x; ++bx)
        for (int by = 0; by < m_nb.y; ++by)
            for (int bz = 0; bz < m_nb.z; ++bz) {
                const uint64_t e = m_dir[blockIndex({bx, by, bz})];
                const Detailed* d = (e & kDetailed) ? &m_pool[uint32_t(e & 0xFFFFFFFFull)] : nullptr;
                for (int x = 0; x < kBlock; ++x)
                    for (int y = 0; y < kBlock; ++y) {
                        uint8_t* row = &g.mat[g.idx({bx * kBlock + x, by * kBlock + y, bz * kBlock})];
                        if (d) std::memcpy(row, d->mat + inBlock({x, y, 0}), kBlock);
                        else std::memset(row, int(e & 0xFF), kBlock);
                    }
            }
}

int BlockStore::fillBlock(glm::ivec3 b, uint8_t m) {
    if (b.x < 0 || b.y < 0 || b.z < 0 || b.x >= m_nb.x || b.y >= m_nb.y || b.z >= m_nb.z) return 0;
    uint64_t& e = m_dir[blockIndex(b)];
    const uint64_t pending = e & kChanged;   // still in m_changed: collapse() clears the bit
    if (!(e & kDetailed)) {
        if (uint8_t(e & 0xFF) == m) return 0;
        e = pending | m;
        return 512;
    }
    const uint32_t slot = uint32_t(e & 0xFFFFFFFFull);
    const Detailed& d = m_pool[slot];
    int n = 0;
    for (int i = 0; i < 512; ++i) n += d.mat[i] != m ? 1 : 0;
    m_free.push_back(slot);
    e = pending | m;
    return n;
}

// ---- F3.3: codec v3 ------------------------------------------------------------------------------------------
namespace {
constexpr uint32_t kMagicV3 = 0x33465850u;   // "PXF3" little-endian
constexpr uint8_t kVersionV3 = 1;
constexpr uint8_t kDetailedTag = 255;
void put8(std::vector<uint8_t>& o, uint32_t v) { o.push_back(uint8_t(v)); }
void put16(std::vector<uint8_t>& o, uint32_t v) {
    o.push_back(uint8_t(v));
    o.push_back(uint8_t(v >> 8));
}
void put32(std::vector<uint8_t>& o, uint32_t v) {
    for (int k = 0; k < 4; ++k) o.push_back(uint8_t(v >> (8 * k)));
}
struct Reader {
    const uint8_t* p;
    size_t n, at = 0;
    bool ok = true;
    bool need(size_t k) {
        if (!ok || n - at < k) ok = false;
        return ok;
    }
    uint32_t u8() { return need(1) ? p[at++] : 0u; }
    uint32_t u16() {
        if (!need(2)) return 0;
        const uint32_t v = uint32_t(p[at]) | uint32_t(p[at + 1]) << 8;
        at += 2;
        return v;
    }
    uint32_t u32() {
        if (!need(4)) return 0;
        uint32_t v = 0;
        for (int k = 0; k < 4; ++k) v |= uint32_t(p[at + size_t(k)]) << (8 * k);
        at += 4;
        return v;
    }
};
}  // namespace

std::vector<uint8_t> BlockStore::encodeV3(const std::vector<std::string>& palette, std::string* err) const {
    auto fail = [&](const std::string& why) {
        if (err) *err = why;
        return std::vector<uint8_t>{};
    };
    const size_t nBlocks = m_dir.size();
    // per block: its uniform material, or -1 (detailed); the materials used
    std::vector<int> uni(nBlocks, -1);
    bool used[256] = {};
    used[0] = true;
    for (size_t bi = 0; bi < nBlocks; ++bi) {
        const uint64_t e = m_dir[bi];
        if (!(e & kDetailed)) {
            uni[bi] = int(e & 0xFF);
            used[e & 0xFF] = true;
            continue;
        }
        const Detailed& d = m_pool[uint32_t(e & 0xFFFFFFFFull)];
        bool one = true;
        for (int i = 0; i < 512; ++i) {
            used[d.mat[i]] = true;
            one = one && d.mat[i] == d.mat[0];
        }
        if (one) uni[bi] = d.mat[0];   // canonical: a detailed block of one material is written uniform
    }
    // the chunk-local palette: air first, then the used materials in ascending store order (canonical)
    int local[256];
    std::vector<uint8_t> order;
    for (int m = 0; m < 256; ++m) {
        local[m] = -1;
        if (!used[m]) continue;
        local[m] = int(order.size());
        order.push_back(uint8_t(m));
    }
    if (order.size() > 255) return fail("codec v3: more than 255 materials in one chunk");
    std::vector<uint8_t> o;
    put32(o, kMagicV3);
    put8(o, kVersionV3);
    put8(o, 0);   // flags: no optional channels yet
    put16(o, uint32_t(m_dim.x));
    put16(o, uint32_t(m_dim.y));
    put16(o, uint32_t(m_dim.z));
    put16(o, uint32_t(order.size()));
    for (uint8_t m : order) {
        if (m != 0 && m >= palette.size()) return fail("codec v3: material " + std::to_string(m) + " has no name");
        const std::string name = m == 0 ? std::string() : palette[m];
        if (name.size() > 255) return fail("codec v3: material name longer than 255 bytes");
        put8(o, uint32_t(name.size()));
        o.insert(o.end(), name.begin(), name.end());
    }
    // the directory, RLE in block index order
    const size_t runCountAt = o.size();
    put32(o, 0);
    uint32_t runs = 0;
    for (size_t bi = 0; bi < nBlocks;) {
        const uint8_t tag = uni[bi] < 0 ? kDetailedTag : uint8_t(local[uni[bi]]);
        size_t len = 1;
        while (bi + len < nBlocks && len < 65535 &&
               (uni[bi + len] < 0 ? kDetailedTag : uint8_t(local[uni[bi + len]])) == tag)
            ++len;
        put16(o, uint32_t(len));
        put8(o, tag);
        ++runs;
        bi += len;
    }
    for (int k = 0; k < 4; ++k) o[runCountAt + size_t(k)] = uint8_t(runs >> (8 * k));
    // the detailed blocks, in directory order: (runLen - 1, local material) pairs over the 512 voxels
    for (size_t bi = 0; bi < nBlocks; ++bi) {
        if (uni[bi] >= 0) continue;
        const Detailed& d = m_pool[uint32_t(m_dir[bi] & 0xFFFFFFFFull)];
        for (int i = 0; i < 512;) {
            int len = 1;
            while (i + len < 512 && len < 256 && d.mat[i + len] == d.mat[i]) ++len;
            put8(o, uint32_t(len - 1));
            put8(o, uint32_t(local[d.mat[i]]));
            i += len;
        }
    }
    return o;
}

bool BlockStore::decodeV3(const uint8_t* data, size_t size, BlockStore& out, std::vector<std::string>& names,
                          std::string* err) {
    auto fail = [&](const std::string& why) {
        if (err) *err = "codec v3: " + why;
        return false;
    };
    if (!data) return fail("no data");
    Reader r{data, size};
    if (r.u32() != kMagicV3 || !r.ok) return fail("not a PXF3 blob");
    const uint32_t version = r.u8(), flags = r.u8();
    if (!r.ok || version != kVersionV3) return fail("unknown version");
    if (flags != 0) return fail("unknown optional channels");
    glm::ivec3 dim;   // read one at a time: argument evaluation order is unspecified (MSVC reads right to left)
    dim.x = int(r.u16());
    dim.y = int(r.u16());
    dim.z = int(r.u16());
    if (!r.ok) return fail("truncated header");
    for (int a = 0; a < 3; ++a)
        if (dim[a] <= 0 || dim[a] % kBlock != 0 || dim[a] > 1024) return fail("bad dims");
    const glm::ivec3 nb = dim / kBlock;
    const size_t nBlocks = size_t(nb.x) * size_t(nb.y) * size_t(nb.z);
    const uint32_t nPal = r.u16();
    if (!r.ok || nPal < 1 || nPal > 255) return fail("bad palette count");
    std::vector<std::string> pal(nPal);
    for (uint32_t k = 0; k < nPal; ++k) {
        const uint32_t len = r.u8();
        if (!r.need(len)) return fail("truncated palette");
        pal[k].assign(reinterpret_cast<const char*>(data + r.at), len);
        r.at += len;
    }
    if (!pal[0].empty()) return fail("palette entry 0 must be air");
    const uint32_t runs = r.u32();
    if (!r.ok || runs > nBlocks) return fail("bad run count");
    BlockStore s(dim);
    size_t bi = 0, detailed = 0;
    std::vector<uint32_t> detailedAt;
    for (uint32_t k = 0; k < runs; ++k) {
        const uint32_t len = r.u16(), tag = r.u8();
        if (!r.ok || len == 0 || bi + len > nBlocks) return fail("bad directory run");
        if (tag != kDetailedTag && tag >= nPal) return fail("directory material outside the palette");
        for (uint32_t q = 0; q < len; ++q, ++bi) {
            if (tag == kDetailedTag) {
                detailedAt.push_back(uint32_t(bi));
                ++detailed;
            } else {
                s.m_dir[bi] = tag;
            }
        }
    }
    if (bi != nBlocks) return fail("directory does not cover every block");
    s.m_pool.resize(detailed);
    for (size_t k = 0; k < detailed; ++k) {
        Detailed& d = s.m_pool[k];
        for (uint64_t& w : d.solid) w = 0;
        int i = 0;
        while (i < 512) {
            const uint32_t len = r.u8() + 1, m = r.u8();
            if (!r.ok) return fail("truncated detailed block");
            if (m >= nPal || i + int(len) > 512) return fail("bad detailed run");
            for (uint32_t q = 0; q < len; ++q, ++i) {
                d.mat[i] = uint8_t(m);
                if (m) d.solid[i >> 6] |= 1ull << (i & 63);
            }
        }
        s.m_dir[detailedAt[k]] = kDetailed | uint32_t(k);
    }
    if (r.at != size) return fail("trailing bytes");
    out = std::move(s);
    names = std::move(pal);
    return true;
}

void BlockStore::remapMaterials(const std::vector<uint8_t>& map) {
    auto to = [&](uint8_t m) { return m < map.size() ? map[m] : m; };
    for (uint64_t& e : m_dir)
        if (!(e & kDetailed)) e = (e & ~0xFFull) | to(uint8_t(e & 0xFF));
    for (Detailed& d : m_pool) {
        for (uint64_t& w : d.solid) w = 0;
        for (int i = 0; i < 512; ++i) {
            d.mat[i] = to(d.mat[i]);
            if (d.mat[i]) d.solid[i >> 6] |= 1ull << (i & 63);
        }
    }
}

// ---- F3.4: the converter from PXB2 (§3.6, §7a) --------------------------------------------------------------------
bool convertPxb2(const uint8_t* data, size_t size, BlockStore& out, std::vector<std::string>& names,
                 Pxb2ConvertStats* stats, std::string* err) {
    auto fail = [&](const std::string& why) {
        if (err) *err = "PXB2 converter: " + why;
        return false;
    };
    if (!data) return fail("no data");
    Reader r{data, size};
    if (r.u32() != 0x32425850u || !r.ok) return fail("not a PXB2 blob");
    Pxb2ConvertStats st;
    st.version = int(r.u8());
    const uint32_t flags = r.u8();
    if (!r.ok || st.version < 1 || st.version > 2) return fail("unknown version");
    const bool wide = (flags & 1u) != 0;
    const uint32_t nPal = r.u16(), cubeCount = r.u32(), subCount = r.u32(), microCount = r.u32();
    if (!r.ok || nPal == 0) return fail("truncated header");
    if (nPal > 256) return fail("more than 255 materials in one chunk");
    std::vector<std::string> pal(nPal);
    for (uint32_t k = 0; k < nPal; ++k) {
        const uint32_t len = r.u8();
        if (!r.need(len)) return fail("truncated palette");
        pal[k].assign(reinterpret_cast<const char*>(data + r.at), len);
        r.at += len;
    }
    auto idx = [&]() { return wide ? r.u16() : r.u8(); };
    constexpr int kCubes = 32 * 32 * 32;
    // cubes (z-minor: index = z + 32 y + 1024 x)
    std::vector<uint8_t> cube(kCubes, 0);
    const uint32_t runs = r.u32();
    if (!r.ok) return fail("truncated cube section");
    size_t cell = 0;
    for (uint32_t k = 0; k < runs; ++k) {
        const uint32_t len = r.u16(), m = idx();
        if (!r.ok || len == 0 || m >= nPal || cell + len > size_t(kCubes)) return fail("bad cube run");
        for (uint32_t q = 0; q < len; ++q) cube[cell + q] = uint8_t(m);
        if (m) st.cubes += len;
        cell += len;
    }
    if (cell != size_t(kCubes) || st.cubes != cubeCount) return fail("cube runs do not cover the chunk");
    // subcubes and microcubes: per cube cell a 9^3 grid of ninths (slot x + 3 y + 9 z, as ChunkBlobCodec writes it)
    std::vector<int32_t> gridOf(kCubes, -1);
    std::vector<std::array<uint8_t, 729>> grids;
    auto gridFor = [&](uint32_t ci) -> std::array<uint8_t, 729>& {
        if (gridOf[ci] < 0) {
            gridOf[ci] = int32_t(grids.size());
            grids.emplace_back();
            grids.back().fill(0);
        }
        return grids[size_t(gridOf[ci])];
    };
    auto stateAndTint = [&]() {
        const uint32_t f = r.u8();
        if (f & 0x7Fu) ++st.withState;
        if (f & 0x80u) {
            ++st.tinted;
            r.need(3);
            r.at += r.ok ? 3 : 0;
        }
    };
    if (subCount > 27u * kCubes || microCount > 729u * kCubes) return fail("bad counts");
    for (uint32_t k = 0; k < subCount; ++k) {
        const uint32_t ci = r.u16(), sl = r.u8(), m = idx();
        stateAndTint();
        if (!r.ok || ci >= uint32_t(kCubes) || sl >= 27 || m >= nPal) return fail("bad subcube");
        const int sx = int(sl % 3), sy = int((sl / 3) % 3), sz = int(sl / 9);
        auto& g = gridFor(ci);
        for (int a = 0; a < 3; ++a)
            for (int b = 0; b < 3; ++b)
                for (int d = 0; d < 3; ++d) g[size_t((3 * sx + a) * 81 + (3 * sy + b) * 9 + (3 * sz + d))] = uint8_t(m);
        ++st.subcubes;
    }
    for (uint32_t k = 0; k < microCount; ++k) {
        const uint32_t ci = r.u16(), sl = r.u8(), ml = r.u8(), m = idx();
        stateAndTint();
        if (!r.ok || ci >= uint32_t(kCubes) || sl >= 27 || ml >= 27 || m >= nPal) return fail("bad microcube");
        const int nx = 3 * int(sl % 3) + int(ml % 3), ny = 3 * int((sl / 3) % 3) + int((ml / 3) % 3),
                  nz = 3 * int(sl / 9) + int(ml / 9);
        gridFor(ci)[size_t(nx * 81 + ny * 9 + nz)] = uint8_t(m);
        ++st.microcubes;
    }
    if (st.version >= 2) {   // water spans: not carried by the store (the water runtime keeps its own)
        const uint32_t spans = r.u32();
        if (!r.ok || spans > 32u * 32u * 8u || !r.need(size_t(spans) * 10)) return fail("bad water section");
        r.at += size_t(spans) * 10;
        st.waterSpans = spans;
    }
    if (r.at != size) return fail("trailing bytes");
    // write the store: cubes whole, subdivided cells resampled by volume per material
    BlockStore s(glm::ivec3(256));
    for (int ci = 0; ci < kCubes; ++ci)
        if (cube[size_t(ci)]) s.fillBlock({ci / 1024, (ci / 32) % 32, ci % 32}, cube[size_t(ci)]);
    // overlap of voxel i (1/8) and ninth n (1/9) along one axis, in 1/72 of a cube
    int ov[8][9];
    for (int i = 0; i < 8; ++i)
        for (int n = 0; n < 9; ++n) ov[i][n] = std::max(0, std::min(9 * i + 9, 8 * n + 8) - std::max(9 * i, 8 * n));
    for (int ci = 0; ci < kCubes; ++ci) {
        if (gridOf[size_t(ci)] < 0 || cube[size_t(ci)]) continue;   // a present cube wins (a subdivided cube is absent)
        const auto& g = grids[size_t(gridOf[size_t(ci)])];
        const glm::ivec3 base = glm::ivec3(ci / 1024, (ci / 32) % 32, ci % 32) * 8;
        ++st.cellsResampled;
        // 1. the vote: each voxel takes the material with the most volume inside it (air counted)
        auto votes = [&](int x, int y, int z, int* vol, uint8_t* seen) {
            int nSeen = 0;
            for (int a = x * 9 / 8; a <= std::min(8, (x * 9 + 8) / 8); ++a)
                for (int b = y * 9 / 8; b <= std::min(8, (y * 9 + 8) / 8); ++b)
                    for (int d = z * 9 / 8; d <= std::min(8, (z * 9 + 8) / 8); ++d) {
                        const int w = ov[x][a] * ov[y][b] * ov[z][d];
                        if (!w) continue;
                        const uint8_t m = g[size_t(a * 81 + b * 9 + d)];
                        int k = 0;
                        while (k < nSeen && seen[k] != m) ++k;
                        if (k == nSeen) {
                            seen[nSeen++] = m;
                            vol[m] = 0;
                        }
                        vol[m] += w;
                    }
            return nSeen;
        };
        uint8_t cellOut[512];
        int count[256] = {};
        for (int i = 0; i < 512; ++i) {
            int vol[256];
            uint8_t seen[8];
            const int nSeen = votes(i >> 6, (i >> 3) & 7, i & 7, vol, seen);
            uint8_t best = seen[0];
            for (int k = 1; k < nSeen; ++k) {
                const uint8_t m = seen[k];
                if (vol[m] > vol[best] ||
                    (vol[m] == vol[best] && ((m != 0 && best == 0) || (m != 0 && best != 0 && m < best))))
                    best = m;
            }
            cellOut[i] = best;
            ++count[best];
        }
        // 2. §7a step 4, "no detail vanishes": a material present in the ninths with no voxel left takes the voxel
        // where it has the most volume (ties: lower x, y, z), skipping the last voxel of another material and voxels
        // already taken; materials in ascending palette order
        bool present[256] = {};
        for (uint8_t m : g) present[m] = true;
        bool taken[512] = {};
        for (int m = 1; m < 256; ++m) {
            if (!present[m] || count[m] > 0) continue;
            int bestI = -1, bestV = 0;
            // candidates in index order, best volume first: two passes keep the tie rule (lower index wins)
            std::vector<std::pair<int, int>> cand;   // (volume, voxel)
            for (int i = 0; i < 512; ++i) {
                int vol[256];
                uint8_t seen[8];
                const int nSeen = votes(i >> 6, (i >> 3) & 7, i & 7, vol, seen);
                for (int k = 0; k < nSeen; ++k)
                    if (seen[k] == m) cand.push_back({vol[m], i});
            }
            std::stable_sort(cand.begin(), cand.end(), [](const auto& l, const auto& r) { return l.first > r.first; });
            for (const auto& c : cand) {
                const uint8_t cur = cellOut[c.second];
                if (taken[c.second] || (cur != 0 && count[cur] == 1)) continue;
                bestI = c.second;
                bestV = c.first;
                break;
            }
            (void)bestV;
            if (bestI < 0) continue;   // nowhere to put it (counted below as lost: it is not in the output)
            --count[cellOut[bestI]];
            cellOut[bestI] = uint8_t(m);
            count[m] = 1;
            taken[bestI] = true;
            ++st.rescued;
        }
        for (int i = 0; i < 512; ++i)
            if (cellOut[i]) s.set(base + glm::ivec3(i >> 6, (i >> 3) & 7, i & 7), cellOut[i]);
    }
    s.collapse();
    // the volume accounting (§7a: measured, not assumed)
    st.volumeIn.assign(nPal, 0.0);
    st.voxelsOut.assign(nPal, 0);
    for (int ci = 0; ci < kCubes; ++ci) st.volumeIn[cube[size_t(ci)]] += 1.0;
    for (int ci = 0; ci < kCubes; ++ci)
        if (gridOf[size_t(ci)] >= 0 && !cube[size_t(ci)])
            for (uint8_t m : grids[size_t(gridOf[size_t(ci)])]) st.volumeIn[m] += 1.0 / 729.0;
    for (int bx = 0; bx < 32; ++bx)
        for (int by = 0; by < 32; ++by)
            for (int bz = 0; bz < 32; ++bz) {
                uint8_t m = 0;
                if (s.uniformBlock({bx, by, bz}, m)) {
                    st.voxelsOut[m] += 512;
                    continue;
                }
                for (int k = 0; k < 512; ++k)
                    ++st.voxelsOut[s.at(glm::ivec3(bx, by, bz) * 8 + glm::ivec3(k >> 6, (k >> 3) & 7, k & 7))];
            }
    out = std::move(s);
    names = std::move(pal);
    if (stats) *stats = st;
    return true;
}

RegionBuild forEachStoredChunk(const std::function<bool(glm::ivec3, std::vector<uint8_t>&)>& readBlob,
                               glm::ivec3 lo, glm::ivec3 hi,
                               const std::function<void(const RegionChunk&, const std::vector<std::string>&)>& each,
                               StoredRegionStats* stats, std::string* err) {
    RegionBuild rb;
    StoredRegionStats st;
    std::map<std::tuple<int, int, int>, BlockStore> stores;
    auto key = [](glm::ivec3 c) { return std::make_tuple(c.x, c.y, c.z); };
    auto inRegion = [&](glm::ivec3 c) {
        return c.x >= lo.x && c.y >= lo.y && c.z >= lo.z && c.x <= hi.x && c.y <= hi.y && c.z <= hi.z;
    };
    for (int x = lo.x - 1; x <= hi.x + 1; ++x)
        for (int y = lo.y - 1; y <= hi.y + 1; ++y)
            for (int z = lo.z - 1; z <= hi.z + 1; ++z) {
                const glm::ivec3 c(x, y, z);
                std::vector<uint8_t> blob;
                if (!readBlob(c, blob)) {
                    if (inRegion(c)) ++st.chunksMissing;
                    continue;
                }
                const auto t0 = std::chrono::steady_clock::now();
                BlockStore s;
                std::vector<std::string> names;
                Pxb2ConvertStats cs;
                std::string e;
                if (!convertPxb2(blob.data(), blob.size(), s, names, &cs, &e)) {
                    if (err) *err = "chunk " + std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(z) + ": " + e;
                    return rb;
                }
                std::vector<uint8_t> map(names.size(), 0);
                for (size_t k = 1; k < names.size(); ++k) {
                    size_t m = 1;
                    while (m < rb.palette.size() && rb.palette[m] != names[k]) ++m;
                    if (m == rb.palette.size()) {
                        if (m >= 256) {
                            if (err) *err = "region palette full (256)";
                            return rb;
                        }
                        rb.palette.push_back(names[k]);
                    }
                    map[k] = uint8_t(m);
                }
                s.remapMaterials(map);
                st.convertMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                if (inRegion(c)) {
                    ++st.chunksStored;
                    st.sum.cubes += cs.cubes;
                    st.sum.subcubes += cs.subcubes;
                    st.sum.microcubes += cs.microcubes;
                    st.sum.cellsResampled += cs.cellsResampled;
                    st.sum.tinted += cs.tinted;
                    st.sum.withState += cs.withState;
                    st.sum.waterSpans += cs.waterSpans;
                    st.sum.rescued += cs.rescued;
                    for (size_t k = 1; k < names.size(); ++k) {
                        st.volumeIn[names[k]] += cs.volumeIn[k];
                        st.volumeOut[names[k]] += double(cs.voxelsOut[k]) / 512.0;
                    }
                }
                stores.emplace(key(c), std::move(s));
            }
    constexpr int N = kChunkM * kVoxelsPerM;
    for (int x = lo.x; x <= hi.x; ++x)
        for (int y = lo.y; y <= hi.y; ++y)
            for (int z = lo.z; z <= hi.z; ++z) {
                const glm::ivec3 c(x, y, z);
                RegionChunk rc;
                rc.chunk = c;
                rc.originVoxel = c * N;
                const auto it = stores.find(key(c));
                if (it != stores.end()) it->second.toGrid(rc.grid);
                else {
                    rc.grid.dim = glm::ivec3(N);
                    rc.grid.mat.assign(size_t(N) * N * N, 0);
                }
                rc.border.resize(glm::ivec3(N), false);
                for (int a = 0; a < 3; ++a)
                    for (int sd : {-1, 1}) {
                        glm::ivec3 nc = c;
                        nc[a] += sd;
                        const auto nb = stores.find(key(nc));
                        if (nb == stores.end()) continue;   // no stored neighbour: air
                        const int ua = (a + 1) % 3, va = (a + 2) % 3;
                        for (int v = 0; v < N; ++v)
                            for (int u = 0; u < N; ++u) {
                                glm::ivec3 p;
                                p[a] = sd < 0 ? N - 1 : 0;
                                p[ua] = u;
                                p[va] = v;
                                if (nb->second.at(p) != 0) rc.border.set(a, sd, u, v, true);
                            }
                    }
                each(rc, rb.palette);
                ++rb.chunks;
            }
    if (stats) *stats = st;
    return rb;
}

size_t BlockStore::uniformBlocks() const {
    size_t n = 0;
    for (uint64_t e : m_dir) n += !(e & kDetailed);
    return n;
}
size_t BlockStore::detailedBlocks() const { return m_dir.size() - uniformBlocks(); }
size_t BlockStore::memoryBytes() const {
    return m_dir.capacity() * sizeof(uint64_t) + m_pool.capacity() * sizeof(Detailed) +
           m_free.capacity() * sizeof(uint32_t) + m_changed.capacity() * sizeof(uint32_t);
}

size_t compareToReference(const BlockStore& s, const VoxelGrid& ref, std::string* why) {
    size_t bad = 0;
    std::string msg;
    for (size_t i = 0; i < ref.size(); ++i) {
        const glm::ivec3 p = ref.pos(i);
        const uint8_t got = s.get(p);
        if (got != ref.mat[i]) {
            if (bad < 3)
                msg += "(" + std::to_string(p.x) + "," + std::to_string(p.y) + "," + std::to_string(p.z) +
                       ") store " + std::to_string(got) + " ref " + std::to_string(ref.mat[i]) + "; ";
            ++bad;
        }
    }
    if (why) *why = msg;
    return bad;
}

}  // namespace Phyxel::Core::VoxFound
