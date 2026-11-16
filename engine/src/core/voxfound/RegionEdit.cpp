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
            d = m_legacyF31 ? meshRunsPlanWholeRows(c.grid, e, plan, &m_cards) : meshRunsPlan(c.grid, e, plan, &m_cards);
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
            // F3.1: connectivity is a function of the blocking bits alone -- flood only when one changed
            if (updateBlockBlocking(c.grid, m_seeThrough, vLo[i], vHi[i], c.blocking) || m_legacyF31) {
                c.connect = connectivityFromBlocking(c.grid.dim / 8, c.blocking);
                ++st.connectFloods;
            }
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
        pv.selectLevel(0);   // one revision step
        pv.lastCopyBytes += sb;
    }
    pv.pooled = true;
    pv.poolDirty = std::move(dirty);
    pv.poolBaseRevision = base;
    pv.poolLayoutId = (m_generation << 32) | m_pool->layouts();
    m_assembledRevision = pv.revision;
}

}  // namespace Phyxel::Core::VoxFound
