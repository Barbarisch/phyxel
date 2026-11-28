#include "core/voxfound/RegionEdit.h"

#include <algorithm>
#include <chrono>
#include <limits>

namespace Phyxel::Core::VoxFound {

namespace {
constexpr int kN = kChunkM * kVoxelsPerM;   // voxels per chunk side
double msSince(std::chrono::steady_clock::time_point t) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
}
}  // namespace

size_t RegionEdit::connectBytes() const {
    size_t b = 0;
    for (const Chunk& c : m_chunks) b += c.conn.bytes();
    return b;
}

size_t RegionEdit::bytes() const {
    size_t b = 0;
    for (const Chunk& c : m_chunks) {
        b += c.store.memoryBytes() + c.faces.size() * sizeof(PackedFace) + c.groups.size() * sizeof(FaceCluster) +
             c.sprigs.size() * sizeof(PackedFace) + c.blocking.size();
        for (const auto& p : c.border.bits) b += p.size() * sizeof(uint32_t);
    }
    return b;
}

void RegionEdit::begin(glm::ivec3 chunkLo, glm::ivec3 chunkHi, int clusterBlocks, bool octantOrder,
                       uint32_t sprigThreshold24) {
    m_chunks.clear();
    m_pool.reset();   // F2.17
    m_in.clear();     // F3.1
    m_sprigSlack = 0.25;
    m_lo = chunkLo;
    m_hi = chunkHi;
    m_clusterBlocks = clusterBlocks;
    m_octant = octantOrder;
    m_threshold24 = sprigThreshold24;
    static uint64_t generations = 0;
    m_generation = ++generations;
}

int RegionEdit::index(glm::ivec3 c) const {
    for (size_t i = 0; i < m_chunks.size(); ++i)
        if (m_chunks[i].chunk == c) return int(i);
    return -1;
}

SprigParams RegionEdit::sprigParams(size_t i) const {
    SprigParams sp;
    sp.cards = m_cards;
    sp.worldOriginVoxel = m_chunks[i].originVoxel;
    sp.threshold24 = m_threshold24;
    return sp;
}

void RegionEdit::addChunk(const RegionChunk& rc, const std::vector<PackedFace>& builderOut) {
    m_chunks.push_back(makeChunk(rc, builderOut));
}

void RegionEdit::rebox(glm::ivec3 chunkLo, glm::ivec3 chunkHi) {   // F7.9c
    m_chunks.erase(std::remove_if(m_chunks.begin(), m_chunks.end(),
                                  [&](const Chunk& c) {
                                      return glm::any(glm::lessThan(c.chunk, chunkLo)) ||
                                             glm::any(glm::greaterThan(c.chunk, chunkHi));
                                  }),
                   m_chunks.end());
    m_lo = chunkLo;
    m_hi = chunkHi;
    m_pool.reset();
    m_in.clear();
    m_assembledRevision = ~0ull;
    static uint64_t reboxes = 0;
    m_generation = (uint64_t(1) << 40) + ++reboxes;   // fresh resident keys (begin's counter stays below)
}

SprigParams RegionEdit::sprigParamsFor(glm::ivec3 originVoxel) const {
    SprigParams sp;
    sp.cards = m_cards;
    sp.worldOriginVoxel = originVoxel;
    sp.threshold24 = m_threshold24;
    return sp;
}

RegionEdit::Chunk RegionEdit::makeChunk(const RegionChunk& rc, const std::vector<PackedFace>& builderOut) const {
    Chunk c;
    c.chunk = rc.chunk;
    c.originVoxel = rc.originVoxel;
    c.store = BlockStore::fromGrid(rc.grid);   // F3.2a: the edit store (materials only)
    c.border = rc.border;
    for (const PackedFace& f : builderOut) (isSprig(f) ? c.sprigs : c.faces).push_back(f);
    c.groups = splitByDirection(c.faces, clusterFaces(c.faces, m_clusterBlocks, m_octant));
    c.blocking = blockBlocking(c.store, m_seeThrough);
    c.conn.rebuild(c.store.dim() / 8, c.blocking);   // F3.5b: the flood, keeping its labels
    c.connect = c.conn.connect();
    return c;
}

void RegionEdit::replaceFaces(size_t i, const std::vector<PackedFace>& builderOut) {
    Chunk& c = m_chunks[i];
    c.faces.clear();
    c.sprigs.clear();
    for (const PackedFace& f : builderOut) (isSprig(f) ? c.sprigs : c.faces).push_back(f);
    c.groups = splitByDirection(c.faces, clusterFaces(c.faces, m_clusterBlocks, m_octant));
}

RegionEdit::Stats RegionEdit::apply(glm::ivec3 lo, glm::ivec3 hi, const std::function<bool(glm::ivec3)>& inside,
                                    uint8_t material, FaceBuilderGpu* gpu, std::string* err) {
    return applyImpl(lo, hi, &inside, material, gpu, err);
}

RegionEdit::Stats RegionEdit::applyCells(const std::vector<std::pair<glm::ivec3, uint8_t>>& cells, FaceBuilderGpu* gpu,
                                         std::string* err) {
    if (cells.empty()) return {};
    // the cells' box, and each cell's material looked up by a sorted key (x, y, z biased and packed)
    glm::ivec3 lo(std::numeric_limits<int>::max()), hi(std::numeric_limits<int>::min());
    std::vector<std::pair<uint64_t, uint8_t>> keyed;
    keyed.reserve(cells.size());
    static constexpr int kBias = 1 << 20;   // world voxels in [-2^20, 2^20) (+-131 km); keys of 21 bits per axis
    auto key = [](glm::ivec3 p) {
        const glm::uvec3 u(p + kBias);
        return (uint64_t(u.x) << 42) | (uint64_t(u.y) << 21) | uint64_t(u.z);
    };
    for (const auto& [p, m] : cells) {
        if (glm::any(glm::lessThan(p, glm::ivec3(-kBias))) || glm::any(glm::greaterThanEqual(p, glm::ivec3(kBias))))
            continue;   // beyond any region the engine builds
        lo = glm::min(lo, p);
        hi = glm::max(hi, p);
        keyed.push_back({key(p), m});
    }
    if (keyed.empty()) return {};
    std::sort(keyed.begin(), keyed.end());   // a repeated cell takes its largest material (a bake never lists a cell twice)
    auto find = [&](glm::ivec3 p) -> int {
        const uint64_t k = key(p);
        auto it = std::lower_bound(keyed.begin(), keyed.end(), std::make_pair(k, uint8_t(0)));
        if (it == keyed.end() || it->first != k) return -1;
        while (it + 1 != keyed.end() && (it + 1)->first == k) ++it;
        return it->second;
    };
    const std::function<bool(glm::ivec3)> inside = [&](glm::ivec3 p) { return find(p) >= 0; };
    const std::function<uint8_t(glm::ivec3)> materialAt = [&](glm::ivec3 p) { return uint8_t(find(p)); };
    return applyImpl(lo, hi, &inside, 0, gpu, err, nullptr, SIZE_MAX, &materialAt);
}

std::vector<RegionChunk> areaRegionChunks(const VoxelGrid& area, glm::ivec3 offset, glm::ivec3* chunkHi) {
    std::vector<RegionChunk> out;
    if (chunkHi) *chunkHi = glm::ivec3(-1);
    if (area.dim.x <= 0 || area.dim.y <= 0 || area.dim.z <= 0 || offset.x < 0 || offset.y < 0 || offset.z < 0) return out;
    const glm::ivec3 hi = (area.dim + offset - 1) / kN;
    if (chunkHi) *chunkHi = hi;
    auto solidAt = [&](glm::ivec3 r) {   // region voxel r
        const glm::ivec3 q = r - offset;
        return area.in(q) && area.mat[area.idx(q)] != 0;
    };
    for (int cx = 0; cx <= hi.x; ++cx)   // x-major, as forEachRegionChunk
        for (int cy = 0; cy <= hi.y; ++cy)
            for (int cz = 0; cz <= hi.z; ++cz) {
                RegionChunk rc;
                rc.chunk = {cx, cy, cz};
                rc.originVoxel = rc.chunk * kN;
                rc.grid.voxelM = area.voxelM;
                rc.grid.resize({kN, kN, kN});
                const glm::ivec3 base = rc.originVoxel - offset;   // area voxel of the chunk's (0,0,0)
                const glm::ivec3 a0 = glm::max(base, glm::ivec3(0)), a1 = glm::min(base + kN, area.dim) - 1;
                for (int x = a0.x; x <= a1.x; ++x)
                    for (int y = a0.y; y <= a1.y; ++y)
                        for (int z = a0.z; z <= a1.z; ++z) {
                            const uint8_t m = area.mat[area.idx({x, y, z})];
                            if (m) rc.grid.mat[rc.grid.idx(glm::ivec3(x, y, z) - base)] = m;
                        }
                rc.border.resize({kN, kN, kN}, false);
                for (int a = 0; a < 3; ++a) {
                    const int ua = (a + 1) % 3, va = (a + 2) % 3;
                    for (int s : {-1, 1})
                        for (int v = 0; v < kN; ++v)
                            for (int u = 0; u < kN; ++u) {
                                glm::ivec3 p;
                                p[a] = s < 0 ? -1 : kN;
                                p[ua] = u;
                                p[va] = v;
                                if (solidAt(rc.originVoxel + p)) rc.border.set(a, s, u, v, true);
                            }
                }
                out.push_back(std::move(rc));
            }
    return out;
}

RegionEdit::Stats RegionEdit::applyBox(glm::ivec3 lo, glm::ivec3 hi, uint8_t material, FaceBuilderGpu* gpu,
                                       std::string* err) {
    return applyImpl(lo, hi, nullptr, material, gpu, err);
}

bool RegionEdit::brush(glm::ivec3 at, int size, uint8_t material, Stats& st, FaceBuilderGpu* gpu, std::string* err) {
    if (!brushSizeOk(size)) {
        if (err) *err = "brush size must be 1, 2, 4 or 8 voxels (got " + std::to_string(size) + ")";
        return false;
    }
    const glm::ivec3 cell = brushCell(at, size);
    st = applyBox(cell, cell + (size - 1), material, gpu, err);
    return true;
}

RegionEdit::Stats RegionEdit::loadChunk(size_t i, const BlockStore& saved) {
    Stats total;
    if (i >= m_chunks.size() || saved.dim() != m_chunks[i].store.dim()) return total;
    // block by block where the saved voxels differ: each through the same exact incremental rebuild as an edit
    const glm::ivec3 nb = saved.dim() / 8;
    for (int bx = 0; bx < nb.x; ++bx)
        for (int by = 0; by < nb.y; ++by)
            for (int bz = 0; bz < nb.z; ++bz) {
                const glm::ivec3 b(bx, by, bz);
                uint8_t us = 0, uc = 0;
                const bool sUni = saved.uniformBlock(b, us), cUni = m_chunks[i].store.uniformBlock(b, uc);
                if (sUni && cUni && us == uc) continue;
                bool differs = sUni && cUni;
                for (int k = 0; k < 512 && !differs; ++k) {
                    const glm::ivec3 p = b * 8 + glm::ivec3(k >> 6, (k >> 3) & 7, k & 7);
                    differs = saved.at(p) != m_chunks[i].store.at(p);
                }
                if (!differs) continue;
                const glm::ivec3 lo = m_chunks[i].originVoxel + b * 8;
                const Stats st = applyImpl(lo, lo + 7, nullptr, 0, nullptr, nullptr, &saved, i);
                total.voxelsChanged += st.voxelsChanged;
                total.chunksRebuilt += st.chunksRebuilt;
                total.facesAdded += st.facesAdded;
                total.facesRemoved += st.facesRemoved;
                total.blocksFilled += st.blocksFilled;
                total.boxLo = glm::min(total.boxLo, st.boxLo);   // F7.6d
                total.boxHi = glm::max(total.boxHi, st.boxHi);
                total.rebuilt.insert(total.rebuilt.end(), st.rebuilt.begin(), st.rebuilt.end());
                total.changed.insert(total.changed.end(), st.changed.begin(), st.changed.end());
            }
    std::sort(total.rebuilt.begin(), total.rebuilt.end());
    total.rebuilt.erase(std::unique(total.rebuilt.begin(), total.rebuilt.end()), total.rebuilt.end());
    std::sort(total.changed.begin(), total.changed.end());
    total.changed.erase(std::unique(total.changed.begin(), total.changed.end()), total.changed.end());
    m_chunks[i].edited = true;   // a saved chunk keeps winning over the generator
    return total;
}

std::vector<size_t> RegionEdit::chunksNear(glm::ivec3 lo, glm::ivec3 hi, int margin) const {   // F7.6d
    std::vector<size_t> out;
    if (lo.x > hi.x || lo.y > hi.y || lo.z > hi.z) return out;
    lo -= margin;
    hi += margin;
    for (size_t i = 0; i < m_chunks.size(); ++i) {
        const glm::ivec3 o = m_chunks[i].originVoxel;
        if (glm::all(glm::lessThanEqual(lo, o + (kN - 1))) && glm::all(glm::greaterThanEqual(hi, o))) out.push_back(i);
    }
    return out;
}
glm::ivec3 RegionEdit::voxelLo() const { return m_lo * kN; }
glm::ivec3 RegionEdit::voxelHi() const { return (m_hi + 1) * kN - 1; }

uint8_t RegionEdit::voxelAt(glm::ivec3 p) const {
    const glm::ivec3 c(floorDiv(p.x, kN), floorDiv(p.y, kN), floorDiv(p.z, kN));
    const int i = index(c);
    if (i < 0) return 0;
    return m_chunks[size_t(i)].store.get(p - m_chunks[size_t(i)].originVoxel);
}

RegionEdit::PickHit RegionEdit::pick(glm::dvec3 o, glm::dvec3 dir, double maxT) const {
    PickHit h;
    if (m_chunks.empty() || glm::length(dir) <= 0.0) return h;
    dir = glm::normalize(dir);
    const glm::dvec3 rlo = glm::dvec3(m_lo * kN), rhi = glm::dvec3((m_hi + 1) * kN);
    // clip the ray to the region's box
    double t0 = 0.0, t1 = maxT;
    int clipAxis = -1;
    for (int a = 0; a < 3; ++a) {
        if (dir[a] == 0.0) {
            if (o[a] < rlo[a] || o[a] >= rhi[a]) return h;
            continue;
        }
        double ta = (rlo[a] - o[a]) / dir[a], tb = (rhi[a] - o[a]) / dir[a];
        if (ta > tb) std::swap(ta, tb);
        if (ta > t0) {
            t0 = ta;
            clipAxis = a;
        }
        t1 = std::min(t1, tb);
    }
    if (t0 > t1) return h;
    const glm::ivec3 step(dir.x > 0 ? 1 : -1, dir.y > 0 ? 1 : -1, dir.z > 0 ? 1 : -1);
    const glm::ivec3 vlo = m_lo * kN, vhi = (m_hi + 1) * kN - 1;
    glm::ivec3 v = glm::clamp(glm::ivec3(glm::floor(o + dir * t0)), vlo, vhi);
    glm::ivec3 n(0);
    if (clipAxis >= 0) {   // entered the region through a face: the voxel on the inside of it
        v[clipAxis] = step[clipAxis] > 0 ? vlo[clipAxis] : vhi[clipAxis];
        n[clipAxis] = -step[clipAxis];
    }
    auto tNext = [&](int a) {   // distance to the next voxel boundary along axis a from voxel v
        if (dir[a] == 0.0) return std::numeric_limits<double>::infinity();
        return (double(v[a] + (step[a] > 0 ? 1 : 0)) - o[a]) / dir[a];
    };
    double t = t0;
    int ci = -1;
    glm::ivec3 cc(std::numeric_limits<int>::min());
    while (t <= maxT) {
        if (v.x < vlo.x || v.y < vlo.y || v.z < vlo.z || v.x > vhi.x || v.y > vhi.y || v.z > vhi.z) return h;
        ++h.steps;
        const glm::ivec3 c(floorDiv(v.x, kN), floorDiv(v.y, kN), floorDiv(v.z, kN));
        if (c != cc) {
            cc = c;
            ci = index(c);
        }
        if (ci < 0) return h;
        const Chunk& ch = m_chunks[size_t(ci)];
        const glm::ivec3 local = v - ch.originVoxel;
        uint8_t um = 0;
        if (ch.store.uniformBlock(local / 8, um) && um == 0) {
            // a uniform air block: leave it in one step (exit face = the nearest of its three far planes)
            const glm::ivec3 blo = ch.originVoxel + (local / 8) * 8;
            int axis = 0;
            double te = std::numeric_limits<double>::infinity();
            for (int a = 0; a < 3; ++a) {
                if (dir[a] == 0.0) continue;
                const double tb = (double(blo[a] + (step[a] > 0 ? 8 : 0)) - o[a]) / dir[a];
                if (tb < te) {
                    te = tb;
                    axis = a;
                }
            }
            const glm::dvec3 q = o + dir * te;
            for (int a = 0; a < 3; ++a)
                v[a] = a == axis ? (step[a] > 0 ? blo[a] + 8 : blo[a] - 1)
                                 : std::clamp(int(std::floor(q[a])), blo[a], blo[a] + 7);
            n = glm::ivec3(0);
            n[axis] = -step[axis];
            t = te;
            continue;
        }
        const uint8_t m = ch.store.get(local);
        if (m) {
            h.hit = true;
            h.voxel = v;
            h.normal = n;
            h.distance = t;
            h.material = m;
            return h;
        }
        int axis = 0;
        double best = tNext(0);
        for (int a = 1; a < 3; ++a)
            if (tNext(a) < best) {
                best = tNext(a);
                axis = a;
            }
        t = best;
        v[axis] += step[axis];
        n = glm::ivec3(0);
        n[axis] = -step[axis];
    }
    return h;
}

RegionEdit::Stats RegionEdit::applyImpl(glm::ivec3 lo, glm::ivec3 hi, const std::function<bool(glm::ivec3)>* inside,
                                        uint8_t material, FaceBuilderGpu* gpu, std::string* err,
                                        const BlockStore* source, size_t sourceChunk,
                                        const std::function<uint8_t(glm::ivec3)>* materialAt) {
    Stats st;
    lo = glm::max(lo, m_lo * kN);
    hi = glm::min(hi, (m_hi + 1) * kN - 1);
    if (lo.x > hi.x || lo.y > hi.y || lo.z > hi.z) return st;
    st.boxLo = lo;   // F7.6d
    st.boxHi = hi;
    // per chunk: the box of changed voxels and the box of changed border planes (chunk-local, inclusive)
    const size_t n = m_chunks.size();
    std::vector<glm::ivec3> bLo(n, glm::ivec3(std::numeric_limits<int>::max())),
        bHi(n, glm::ivec3(std::numeric_limits<int>::min()));
    std::vector<char> voxelEdited(n, 0);
    auto grow = [&](size_t i, glm::ivec3 a, glm::ivec3 b) {
        bLo[i] = glm::min(bLo[i], a);
        bHi[i] = glm::max(bHi[i], b);
    };
    const auto tSet = std::chrono::steady_clock::now();
    std::vector<glm::ivec3> vLo(n), vHi(n);   // the voxel part alone (for the blocking bits)
    for (size_t i = 0; i < n; ++i) {
        Chunk& c = m_chunks[i];
        const glm::ivec3 clo = glm::max(lo - c.originVoxel, glm::ivec3(0));
        const glm::ivec3 chi = glm::min(hi - c.originVoxel, glm::ivec3(kN - 1));
        if (clo.x > chi.x || clo.y > chi.y || clo.z > chi.z) continue;
        glm::ivec3 eLo(std::numeric_limits<int>::max()), eHi(std::numeric_limits<int>::min());
        // F3.2b: per block; a block a BOX edit covers entirely is written whole (one directory entry, no per-voxel
        // set, nothing left to collapse). Its changed-voxel box is taken as the whole block: a superset, which the
        // incremental rebuild handles exactly.
        const glm::ivec3 b0 = clo / 8, b1 = chi / 8;
        for (int bx = b0.x; bx <= b1.x; ++bx)
            for (int by = b0.y; by <= b1.y; ++by)
                for (int bz = b0.z; bz <= b1.z; ++bz) {
                    const glm::ivec3 bl(bx * 8, by * 8, bz * 8);
                    const glm::ivec3 sLo = glm::max(clo, bl), sHi = glm::min(chi, bl + 7);
                    uint8_t srcUniform = 0;   // F3.3: a load writes the saved block; uniform = one entry
                    const bool fromSource = source && i == sourceChunk;
                    if (source && !fromSource) continue;
                    if (!inside && !materialAt && sLo == bl && sHi == bl + 7 &&
                        (!fromSource || source->uniformBlock({bx, by, bz}, srcUniform))) {
                        const int n = c.store.fillBlock({bx, by, bz}, fromSource ? srcUniform : material);
                        if (n) {
                            st.voxelsChanged += size_t(n);
                            eLo = glm::min(eLo, bl);
                            eHi = glm::max(eHi, bl + 7);
                            ++st.blocksFilled;
                        }
                        continue;
                    }
                    for (int x = sLo.x; x <= sHi.x; ++x)
                        for (int y = sLo.y; y <= sHi.y; ++y)
                            for (int z = sLo.z; z <= sHi.z; ++z) {
                                const glm::ivec3 p(x, y, z);
                                if (inside && !(*inside)(c.originVoxel + p)) continue;
                                const uint8_t want = fromSource ? source->at(p)
                                                     : materialAt ? (*materialAt)(c.originVoxel + p)
                                                                  : material;
                                if (c.store.at(p) == want) continue;
                                c.store.set(p, want);
                                ++st.voxelsChanged;
                                eLo = glm::min(eLo, p);
                                eHi = glm::max(eHi, p);
                            }
                }
        if (eLo.x > eHi.x) continue;
        voxelEdited[i] = 1;
        c.edited = true;   // F3.3: differs from the generator now (saved by {"save":true})
        vLo[i] = eLo;
        vHi[i] = eHi;
        grow(i, eLo, eHi);
        ++st.chunksEdited;
        // the neighbour across each side this box touches: its border plane there = these voxels' solidity
        for (int a = 0; a < 3; ++a)
            for (int s : {-1, 1}) {
                if (s < 0 ? eLo[a] != 0 : eHi[a] != kN - 1) continue;
                glm::ivec3 nc = c.chunk;
                nc[a] += s;
                const int j = index(nc);
                if (j < 0) continue;   // the region's edge: that border came from the generated margin
                Chunk& nb = m_chunks[size_t(j)];
                const int ua = (a + 1) % 3, va = (a + 2) % 3;
                const int layer = s < 0 ? 0 : kN - 1;
                bool flipped = false;   // F3.5a: the neighbour is re-planned only if one of its border bits flips
                for (int v = eLo[va]; v <= eHi[va]; ++v)
                    for (int u = eLo[ua]; u <= eHi[ua]; ++u) {
                        glm::ivec3 p;
                        p[a] = layer;
                        p[ua] = u;
                        p[va] = v;
                        const uint8_t bm = c.store.at(p);
                        const bool solid = bm != 0, clr = solid && m_cards.isClear(bm);   // D4 follow-up: glass
                        if (nb.border.solid(a, -s, u, v) == solid && nb.border.clear(a, -s, u, v) == clr) continue;
                        nb.border.set(a, -s, u, v, solid);
                        nb.border.setClear(a, -s, u, v, clr);
                        flipped = true;
                    }
                if (!flipped && !m_legacyBorder) continue;
                glm::ivec3 pl = eLo, ph = eHi;
                pl[a] = ph[a] = s < 0 ? kN : -1;   // the plane lies just outside the neighbour
                grow(size_t(j), pl, ph);
            }
    }
    st.setMs = msSince(tSet);
    // F3.2a: the changed blocks that are uniform again return to one directory entry, the pool is compacted
    const auto tCollapse = std::chrono::steady_clock::now();
    for (size_t i = 0; i < n; ++i)
        if (voxelEdited[i]) m_chunks[i].store.collapse(0.25);   // F3.2b: free slots reused before compacting
    st.collapseMs = msSince(tCollapse);
    for (size_t i = 0; i < n; ++i) {
        if (bLo[i].x > bHi[i].x) continue;
        Chunk& c = m_chunks[i];
        const Edges e(c.border);
        auto t = std::chrono::steady_clock::now();
        const RunsPlan plan = m_legacyBorder ? planRunsDelta(c.store.dim(), c.faces, bLo[i], bHi[i])
                                             : planRunsDelta(c.store.dim(), c.faces, c.groups, bLo[i], bHi[i]);
        st.planMs += msSince(t);
        RunsDelta d;
        const auto tr = std::chrono::steady_clock::now();
        bool done = false;
        if (gpu && gpu->initialized()) {
            // the device grid needs this chunk's changed voxels only (a border-plane-only chunk: none)
            const glm::ivec3 dLo = voxelEdited[i] ? vLo[i] : glm::ivec3(1), dHi = voxelEdited[i] ? vHi[i] : glm::ivec3(0);
            std::string gerr;
            VoxelGrid dense;   // F3.2a: the device path takes a dense grid (opt-in; the expansion is in rows wall)
            c.store.toGrid(dense);
            if (gpu->rowsDelta(residentKey(i), dense, c.border, dLo, dHi, plan, &m_cards, d.added,
                               &gerr)) {
                d.removed = plan.removed;
                d.rowsMeshed = plan.rows();
                st.rowsGpuMs += std::max(0.0, gpu->lastGpuMs());
                st.uploadBytes += gpu->lastUploadBytes();
                st.residentMisses += gpu->lastResidentHit() ? 0 : 1;
                st.onDevice = true;
                done = true;
            } else if (err) {
                *err = gerr;   // fall back to the CPU executor, and say so
            }
        }
        if (!done) {
            d = m_legacyF31 ? meshRunsPlanWholeRows(c.store, e, plan, &m_cards) : meshRunsPlan(c.store, e, plan, &m_cards);
            if (gpu) gpu->forgetResident(residentKey(i));   // the fallback changed voxels behind the device's copy
        }
        st.rowsWallMs += msSince(tr);
        st.facesMs += msSince(t);
        t = std::chrono::steady_clock::now();
        // F3.5a: a delta that removes exactly the faces it adds (as a set) leaves the list as it is
        bool facesChanged = true;
        if (!m_legacyBorder && d.removed.size() == d.added.size()) {
            std::vector<uint64_t> was, now;
            was.reserve(d.removed.size());
            now.reserve(d.added.size());
            for (uint32_t k : d.removed) was.push_back(c.faces[k].key());
            for (const PackedFace& f : d.added) now.push_back(f.key());
            std::sort(was.begin(), was.end());
            std::sort(now.begin(), now.end());
            facesChanged = was != now;
        }
        if (facesChanged) applyClusteredDelta(c.faces, c.groups, d, m_clusterBlocks, m_octant);
        st.clusterMs += msSince(t);
        t = std::chrono::steady_clock::now();
        bool sprigsChanged = true;
        st.sprigsAdded += updateSprigs(c.sprigs, c.store, e, sprigParams(i), bLo[i], bHi[i],
                                       m_legacyBorder ? nullptr : &sprigsChanged);
        st.sprigMs += msSince(t);
        bool connectChanged = false;
        if (voxelEdited[i]) {
            t = std::chrono::steady_clock::now();
            // F3.1: connectivity is a function of the blocking bits alone -- flood only when one changed
            std::vector<uint32_t> flips;
            if (updateBlockBlocking(c.store, m_seeThrough, vLo[i], vHi[i], c.blocking, &flips) || m_legacyF31) {
                const FaceConnect was = c.connect;
                if (m_legacyF31 || m_legacyConnect) {   // F3.5a's flood; the labels are rebuilt on the next update
                    c.connect = connectivityFromBlocking(c.store.dim() / 8, c.blocking);
                    c.connStale = true;
                    ++st.connectFloods;
                } else {
                    const size_t f0 = c.conn.floods();
                    if (c.connStale) c.conn.rebuild(c.store.dim() / 8, c.blocking);
                    else c.conn.update(c.blocking, flips);   // F3.5b: floods only when a close may split
                    c.connStale = false;
                    c.connect = c.conn.connect();
                    st.connectFloods += c.conn.floods() - f0;
                }
                connectChanged = c.connect != was;
            }
            st.connectMs += msSince(t);
        }
        st.facesRemoved += d.removed.size();
        st.facesAdded += d.added.size();
        st.rowsMeshed += d.rowsMeshed;
        st.rebuilt.push_back(i);
        if (facesChanged || sprigsChanged || connectChanged || m_legacyBorder) st.changed.push_back(i);
        ++st.chunksRebuilt;
    }
    return st;
}

void RegionEdit::poolInput(size_t i) {
    const Chunk& c = m_chunks[i];
    const glm::vec3 base = glm::vec3((c.chunk - m_lo) * kChunkM);
    auto toPreview = [&](const FaceCluster& fc) {
        PreviewCluster p;
        p.lo = base + glm::vec3(fc.lo) / float(kVoxelsPerM);
        p.hi = base + glm::vec3(fc.hi) / float(kVoxelsPerM);
        p.firstFace = fc.first;
        p.count = fc.count;
        p.chunk = uint32_t(i);
        p.dir = fc.dir;
        return p;
    };
    PoolChunk& in = m_in[i];
    in.faces = &c.faces;
    in.clusters.clear();
    in.clustersDir.clear();
    for (const FaceCluster& fc : cellsOfGroups(c.faces, c.groups, m_clusterBlocks)) in.clusters.push_back(toPreview(fc));
    for (const FaceCluster& fc : c.groups) in.clustersDir.push_back(toPreview(fc));
}

void RegionEdit::assemble(Preview& pv, const std::vector<uint16_t>& leafTex, const std::vector<size_t>* changed) {
    if (pv.levels.empty()) pv.levels.resize(1);
    Preview::Level& L = pv.levels[0];
    const size_t n = std::min(m_chunks.size(), pv.chunks.size());
    // F2.17: every chunk's lists (cluster firstFace relative to the chunk; the pool places them). F3.1: kept between
    // edits; only the rebuilt chunks' are recomputed.
    const bool incremental = changed && m_in.size() == n && !m_legacyF31;
    if (!incremental) {
        m_in.assign(n, PoolChunk{});
        for (size_t i = 0; i < n; ++i) poolInput(i);
    } else {
        for (size_t i : *changed)
            if (i < n) poolInput(i);
    }
    if (!m_pool) m_pool = std::make_unique<FacePool>();
    const uint64_t layoutId = (m_generation << 32) | m_pool->layouts();
    PoolDirty dirty;
    if (changed && m_pool->laidOut() && pv.pooled && pv.poolLayoutId == layoutId)
        m_pool->update(m_in, *changed, L, dirty);
    else
        m_pool->layout(m_in, L, dirty);
    const uint64_t base = pv.revision;
    // F3.1: the drawn arrays move only over the dirty ranges when they still hold exactly the image this store wrote
    // last (same layout, level 0 drawn, nothing appended, nothing else bumped the revision); else selectLevel copies
    // the whole image.
    const size_t extra = pv.casterFaces.size() + pv.seamFaces.size();
    const bool inPlace = !dirty.full && incremental && pv.revision == m_assembledRevision && pv.drawLevel == 0 &&
                         pv.lodClusters.empty() && extra == 0 && pv.faces.size() == L.faces.size() &&
                         pv.clusters.size() == L.clusters.size() && pv.clustersDir.size() == L.clustersDir.size();
    auto sprigsOf = [&](size_t i, std::vector<PreviewSprig>& out) {
        for (const PackedFace& sp : m_chunks[i].sprigs) {
            const uint32_t m = (sp.b >> 16) & 0xFFu;
            out.push_back({(sp.a & 0xFFFFFFu) | (15u << 24), m < leafTex.size() ? uint32_t(leafTex[m]) : 0u});
        }
    };
    // F3.1: every chunk's sprigs in a span of its own with slack (count x m_sprigSlack, at least 64; gaps are zero and
    // never drawn: the renderer draws [firstSprig, firstSprig + sprigCount) per chunk). Returns the bytes written.
    auto layoutSprigs = [&]() {
        pv.sprigs.clear();
        for (size_t i = 0; i < n; ++i) {
            PreviewChunk& pc = pv.chunks[i];
            pc.firstSprig = uint32_t(pv.sprigs.size());
            sprigsOf(i, pv.sprigs);
            pc.sprigCount = uint32_t(pv.sprigs.size()) - pc.firstSprig;
            pv.sprigs.resize(pv.sprigs.size() + std::max<size_t>(size_t(double(pc.sprigCount) * m_sprigSlack), 64),
                             PreviewSprig{});
        }
        ++m_sprigLayouts;
        return pv.sprigs.size() * sizeof(PreviewSprig);
    };
    if (inPlace) {
        size_t bytes = 0;
        for (const PoolRange& r : dirty.faces) {
            std::copy(L.faces.begin() + r.first, L.faces.begin() + r.first + r.count, pv.faces.begin() + r.first);
            bytes += r.count * sizeof(PackedFace);
        }
        for (const PoolRange& r : dirty.clusters) {
            std::copy(L.clusters.begin() + r.first, L.clusters.begin() + r.first + r.count, pv.clusters.begin() + r.first);
            bytes += r.count * sizeof(PreviewCluster);
        }
        for (const PoolRange& r : dirty.clustersDir) {
            std::copy(L.clustersDir.begin() + r.first, L.clustersDir.begin() + r.first + r.count,
                      pv.clustersDir.begin() + r.first);
            bytes += r.count * sizeof(PreviewCluster);
        }
        for (size_t i = 0; i < n; ++i) {
            pv.chunks[i].firstFace = L.firstFace[i];
            pv.chunks[i].faceCount = L.faceCount[i];
        }
        // the rebuilt chunks' sprigs, each in its own span (capacity = up to the next chunk's first sprig): a count
        // that fits is rewritten in place (the freed tail zeroed: never drawn either way); one that does not re-places
        // every span
        std::vector<PreviewSprig> mine;
        bool overflow = false;
        for (size_t i : *changed) {
            if (i >= n) continue;
            mine.clear();
            sprigsOf(i, mine);
            PreviewChunk& pc = pv.chunks[i];
            const size_t end = i + 1 < n ? pv.chunks[i + 1].firstSprig : pv.sprigs.size();
            if (mine.size() > end - pc.firstSprig) {
                overflow = true;
                break;
            }
            std::copy(mine.begin(), mine.end(), pv.sprigs.begin() + pc.firstSprig);
            if (mine.size() < pc.sprigCount)
                std::fill(pv.sprigs.begin() + pc.firstSprig + mine.size(), pv.sprigs.begin() + pc.firstSprig + pc.sprigCount,
                          PreviewSprig{});
            bytes += std::max<size_t>(mine.size(), pc.sprigCount) * sizeof(PreviewSprig);
            pc.sprigCount = uint32_t(mine.size());
        }
        if (overflow) {
            m_sprigSlack *= 1.5;   // as the face pool: an overflow places everything again, with more slack
            bytes += layoutSprigs();
        }
        for (size_t i = 0; i < n; ++i) pv.chunks[i].connect = m_chunks[i].connect;
        pv.drawLevel = 0;
        ++pv.revision;   // one revision step, as selectLevel
        pv.lastCopyBytes = bytes;
    } else {
        const size_t sb = layoutSprigs();
        for (size_t i = 0; i < n; ++i) pv.chunks[i].connect = m_chunks[i].connect;
        // F7.6d: re-select what was drawn -- the LOD selection (level 0 = this image) or a drawn coarse level -- not
        // level 0 (which silently dropped both after any edit); one revision step either way
        const bool hadLod = !pv.lodFirst.empty();
        const int drawn = pv.drawLevel;
        if (!(hadLod && pv.selectLod()) && !(drawn > 0 && pv.selectLevel(drawn))) pv.selectLevel(0);
        pv.lastCopyBytes += sb;
    }
    pv.pooled = true;
    pv.poolDirty = std::move(dirty);
    pv.poolBaseRevision = base;
    pv.poolLayoutId = (m_generation << 32) | m_pool->layouts();
    m_assembledRevision = pv.revision;
}

}  // namespace Phyxel::Core::VoxFound
