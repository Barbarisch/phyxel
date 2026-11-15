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

size_t RegionEdit::bytes() const {
    size_t b = 0;
    for (const Chunk& c : m_chunks) {
        b += c.grid.mat.size() + c.faces.size() * sizeof(PackedFace) + c.groups.size() * sizeof(FaceCluster) +
             c.sprigs.size() * sizeof(PackedFace) + c.blocking.size();
        for (const auto& p : c.border.bits) b += p.size() * sizeof(uint32_t);
    }
    return b;
}

void RegionEdit::begin(glm::ivec3 chunkLo, glm::ivec3 chunkHi, int clusterBlocks, bool octantOrder,
                       uint32_t sprigThreshold24) {
    m_chunks.clear();
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
    Chunk c;
    c.chunk = rc.chunk;
    c.originVoxel = rc.originVoxel;
    c.grid.dim = rc.grid.dim;
    c.grid.mat = rc.grid.mat;   // materials only
    c.border = rc.border;
    for (const PackedFace& f : builderOut) (isSprig(f) ? c.sprigs : c.faces).push_back(f);
    c.groups = splitByDirection(c.faces, clusterFaces(c.faces, m_clusterBlocks, m_octant));
    c.blocking = blockBlocking(c.grid, m_seeThrough);
    c.connect = connectivityFromBlocking(c.grid.dim / 8, c.blocking);
    m_chunks.push_back(std::move(c));
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
    Stats st;
    lo = glm::max(lo, m_lo * kN);
    hi = glm::min(hi, (m_hi + 1) * kN - 1);
    if (lo.x > hi.x || lo.y > hi.y || lo.z > hi.z) return st;
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
        for (int x = clo.x; x <= chi.x; ++x)
            for (int y = clo.y; y <= chi.y; ++y)
                for (int z = clo.z; z <= chi.z; ++z) {
                    const glm::ivec3 p(x, y, z);
                    if (!inside(c.originVoxel + p)) continue;
                    uint8_t& m = c.grid.mat[c.grid.idx(p)];
                    if (m == material) continue;
                    m = material;
                    ++st.voxelsChanged;
                    eLo = glm::min(eLo, p);
                    eHi = glm::max(eHi, p);
                }
        if (eLo.x > eHi.x) continue;
        voxelEdited[i] = 1;
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
                for (int v = eLo[va]; v <= eHi[va]; ++v)
                    for (int u = eLo[ua]; u <= eHi[ua]; ++u) {
                        glm::ivec3 p;
                        p[a] = layer;
                        p[ua] = u;
                        p[va] = v;
                        nb.border.set(a, -s, u, v, c.grid.mat[c.grid.idx(p)] != 0);
                    }
                glm::ivec3 pl = eLo, ph = eHi;
                pl[a] = ph[a] = s < 0 ? kN : -1;   // the plane lies just outside the neighbour
                grow(size_t(j), pl, ph);
            }
    }
    st.setMs = msSince(tSet);
    for (size_t i = 0; i < n; ++i) {
        if (bLo[i].x > bHi[i].x) continue;
        Chunk& c = m_chunks[i];
        const Edges e(c.border);
        auto t = std::chrono::steady_clock::now();
        const RunsPlan plan = planRunsDelta(c.grid.dim, c.faces, bLo[i], bHi[i]);
        st.planMs += msSince(t);
        RunsDelta d;
        const auto tr = std::chrono::steady_clock::now();
        bool done = false;
        if (gpu && gpu->initialized()) {
            // the device grid needs this chunk's changed voxels only (a border-plane-only chunk: none)
            const glm::ivec3 dLo = voxelEdited[i] ? vLo[i] : glm::ivec3(1), dHi = voxelEdited[i] ? vHi[i] : glm::ivec3(0);
            std::string gerr;
            if (gpu->rowsDelta(residentKey(i), c.grid, c.border, dLo, dHi, plan, &m_cards, d.added,
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
            d = meshRunsPlan(c.grid, e, plan, &m_cards);
            if (gpu) gpu->forgetResident(residentKey(i));   // the fallback changed voxels behind the device's copy
        }
        st.rowsWallMs += msSince(tr);
        st.facesMs += msSince(t);
        t = std::chrono::steady_clock::now();
        applyClusteredDelta(c.faces, c.groups, d, m_clusterBlocks, m_octant);
        st.clusterMs += msSince(t);
        t = std::chrono::steady_clock::now();
        st.sprigsAdded += updateSprigs(c.sprigs, c.grid, e, sprigParams(i), bLo[i], bHi[i]);
        st.sprigMs += msSince(t);
        if (voxelEdited[i]) {
            t = std::chrono::steady_clock::now();
            updateBlockBlocking(c.grid, m_seeThrough, vLo[i], vHi[i], c.blocking);
            c.connect = connectivityFromBlocking(c.grid.dim / 8, c.blocking);
            st.connectMs += msSince(t);
        }
        st.facesRemoved += d.removed.size();
        st.facesAdded += d.added.size();
        st.rowsMeshed += d.rowsMeshed;
        st.rebuilt.push_back(i);
        ++st.chunksRebuilt;
    }
    return st;
}

void RegionEdit::assemble(Preview& pv, const std::vector<uint16_t>& leafTex) const {
    if (pv.levels.empty()) pv.levels.resize(1);
    Preview::Level& L = pv.levels[0];
    L.faces.clear();
    L.firstFace.clear();
    L.faceCount.clear();
    L.leafCount.clear();
    L.clusters.clear();
    L.clustersDir.clear();
    pv.sprigs.clear();
    for (size_t i = 0; i < m_chunks.size() && i < pv.chunks.size(); ++i) {
        const Chunk& c = m_chunks[i];
        const uint32_t first = uint32_t(L.faces.size());
        const glm::vec3 base = glm::vec3((c.chunk - m_lo) * kChunkM);
        auto toPreview = [&](const FaceCluster& fc) {
            PreviewCluster p;
            p.lo = base + glm::vec3(fc.lo) / float(kVoxelsPerM);
            p.hi = base + glm::vec3(fc.hi) / float(kVoxelsPerM);
            p.firstFace = first + fc.first;
            p.count = fc.count;
            p.chunk = uint32_t(i);
            p.dir = fc.dir;
            return p;
        };
        for (const FaceCluster& fc : cellsOfGroups(c.faces, c.groups, m_clusterBlocks)) L.clusters.push_back(toPreview(fc));
        for (const FaceCluster& fc : c.groups) L.clustersDir.push_back(toPreview(fc));
        L.faces.insert(L.faces.end(), c.faces.begin(), c.faces.end());
        L.firstFace.push_back(first);
        L.faceCount.push_back(uint32_t(c.faces.size()));
        L.leafCount.push_back(0u);
        PreviewChunk& pc = pv.chunks[i];
        pc.firstSprig = uint32_t(pv.sprigs.size());
        for (const PackedFace& s : c.sprigs) {
            const uint32_t m = (s.b >> 16) & 0xFFu;
            pv.sprigs.push_back({(s.a & 0xFFFFFFu) | (15u << 24), m < leafTex.size() ? uint32_t(leafTex[m]) : 0u});
        }
        pc.sprigCount = uint32_t(pv.sprigs.size()) - pc.firstSprig;
        pc.connect = c.connect;
    }
    pv.selectLevel(0);
}

}  // namespace Phyxel::Core::VoxFound
