// docs/VoxelFoundationPlan.md §3 + §8 — storage lab (F0 skeleton). See StorageLab.h.

#include "core/voxfound/StorageLab.h"

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

VoxelArea buildTerrainArea(WorldGenerator& gen, glm::ivec3 o, glm::ivec3 sz) {
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
    // (m + 0.5) the height is bilinear. Road grading (which moves surfaceY, not bedHeight) is not reproduced.
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
            const float h00 = col(ix, iz).bedHeight, h10 = col(ix + 1, iz).bedHeight;
            const float h01 = col(ix, iz + 1).bedHeight, h11 = col(ix + 1, iz + 1).bedHeight;
            const float h = (h00 * (1 - tx) + h10 * tx) * (1 - tz) + (h01 * (1 - tx) + h11 * tx) * tz;
            const auto& c = col(int(std::floor(wx)) - o.x, int(std::floor(wz)) - o.z);  // materials: own metre
            for (int vy = 0; vy < d.y; ++vy) {
                const float wy = float(o.y) + (vy + 0.5f) / kVoxelsPerM;
                if (wy >= h + 0.5f) break;
                const int my = int(std::floor(wy));
                // material layers: depth below the column's own top metre, as today's generator counts it
                WorldGenerator::ColumnSample cc = c;
                cc.surfaceY = int(std::floor(h + 0.5f - 1e-4f));
                if (my > cc.surfaceY) cc.surfaceY = my;
                a.grid.set({vx, vy, vz}, idOf(gen.columnMaterialAt(my, cc)));
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
        const float h00 = gen.sampleSurface(ix, iz).bedHeight, h10 = gen.sampleSurface(ix + 1, iz).bedHeight;
        const float h01 = gen.sampleSurface(ix, iz + 1).bedHeight, h11 = gen.sampleSurface(ix + 1, iz + 1).bedHeight;
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
    const glm::ivec4* plantWindow) {
    RegionBuild rb;
    std::map<std::string, uint8_t> ids{{"air", 0}};
    const glm::ivec3 inner(kChunkM * kVoxelsPerM);
    for (int cx = lo.x; cx <= hi.x; ++cx)
        for (int cy = lo.y; cy <= hi.y; ++cy)
            for (int cz = lo.z; cz <= hi.z; ++cz) {
                const glm::ivec3 c(cx, cy, cz);
                // the chunk plus a 1 m margin: the margin's inner voxel layer is the chunk's border
                VoxelArea a = buildTerrainArea(gen, c * kChunkM - 1, glm::ivec3(kChunkM + 2));
                if (floraDir) {
                    const FloraStampStats f = stampFlora(a, gen, *floraDir, 24, plantWindow);
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
                RegionChunk rc;
                rc.chunk = c;
                rc.originVoxel = c * kChunkM * kVoxelsPerM;
                const glm::ivec3 off(kVoxelsPerM);
                rc.grid = subGrid(a.grid, off, inner);
                rc.border = neighboursOf(a.grid, off, inner, Outside::Air);  // the margin covers every border voxel
                rc.area = &a.grid;
                rc.areaMarginVoxels = kVoxelsPerM;
                ++rb.chunks;
                each(rc, rb.palette);
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

void BlockStore::collapse() {
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
    m_changed.shrink_to_fit();
    // 2. compact the pool: move the live blocks at the end into the holes, then shrink
    if (m_free.empty()) return;
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
