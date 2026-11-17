// docs/VoxelFoundationPlan.md §6 + §8 — fracture lab (F0 skeleton). See FractureLab.h.

#include "core/voxfound/FractureLab.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <unordered_map>

namespace Phyxel::Core::VoxFound {

// ---- F4.1: the CPU reference (model fixed in docs/evidence/voxfound_f41_prediction.txt) ------------------

namespace {

constexpr double kTwoPi = 6.28318530717958647692;

uint64_t splitmix(uint64_t x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

uint64_t hashCell(uint64_t seed, int level, int64_t qx, int64_t qy, int64_t qz) {
    uint64_t h = splitmix(seed ^ (uint64_t(uint32_t(level)) << 40));
    h = splitmix(h ^ uint64_t(qx));
    h = splitmix(h ^ uint64_t(qy));
    return splitmix(h ^ uint64_t(qz));
}

int64_t floorDiv(int64_t a, int64_t b) {
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}

// The pattern's view of one material: Voronoi (brittle / fibrous, optional grain axis) or polar (glass).
struct MatInfo {
    float toughness = 1.0f;
    float partScale = 0.3f;
    bool glass = false;
    int grainAxis = -1;   // fibrous: the dominant axis of the grain (the lab's grains are axis-aligned)
};

class Pattern {
public:
    Pattern(const VoxelGrid& g, const Hit& hit, float cbrtE) : m_g(g), m_hit(hit), m_cbrtE(cbrtE) {
        // level k spacing kLevel0M / 2^k; finest level still >= one voxel
        m_kmax = 0;
        while (FractureModel::kLevel0M / float(1 << (m_kmax + 1)) >= g.voxelM - 1e-6f && m_kmax < 12) ++m_kmax;
        m_ids.assign(g.size() * size_t(m_kmax + 1), 0);
        // glass: the plane across the hit direction
        glm::vec3 d = glm::length(hit.dir) > 0 ? glm::normalize(hit.dir) : glm::vec3(0, 0, 1);
        glm::vec3 t = std::fabs(d.x) < 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
        m_u = glm::normalize(glm::cross(d, t));
        m_v = glm::cross(d, m_u);
        m_sectors = std::max(4, int(std::lround(4.0 * cbrtE)));
        m_r0 = 0.5 * cellM(0.0);
    }
    int kmax() const { return m_kmax; }

    /// h(d): the local cell size in metres.
    double cellM(double dM) const {
        return FractureModel::kCellM * (1.0 + dM / FractureModel::kStressLengthM) / m_cbrtE;
    }
    /// The finest level whose spacing is still >= h(d).
    int level(double dM) const {
        const double h = cellM(dM);
        int L = -1;
        for (int k = 0; k <= m_kmax; ++k)
            if (FractureModel::kLevel0M / double(1 << k) >= h) L = k;
        return std::max(L, 0);
    }

    uint32_t voronoi(size_t i, int k, int grainAxis) {
        uint32_t& slot = m_ids[i * size_t(m_kmax + 1) + size_t(k)];
        if (slot) return slot;
        // fixed point: 1024 units per voxel; a voxel centre is p * 1024 + 512 (exact)
        const glm::ivec3 p = m_g.pos(i);
        int64_t X[3] = {int64_t(p.x) * 1024 + 512, int64_t(p.y) * 1024 + 512, int64_t(p.z) * 1024 + 512};
        double spacing = double(FractureModel::kLevel0M) / double(1 << k) / double(m_g.voxelM) * 1024.0;
        if (grainAxis >= 0) {
            // fibrous: cells of the same volume, kGrainStretch (4) times longer along the grain than across.
            // Across-grain coordinates are multiplied by 4 (exact) and the spacing by 4^(2/3): a cell is then
            // 4^(2/3) h along the grain and 4^(2/3) / 4 = 4^(-1/3) h across it.
            for (int a = 0; a < 3; ++a)
                if (a != grainAxis) X[a] *= int64_t(FractureModel::kGrainStretch);
            spacing *= std::cbrt(double(FractureModel::kGrainStretch) * double(FractureModel::kGrainStretch));
        }
        const int64_t C = std::max<int64_t>(1, std::llround(spacing));
        const int64_t q[3] = {floorDiv(X[0], C), floorDiv(X[1], C), floorDiv(X[2], C)};
        int64_t best = INT64_MAX;
        uint64_t bestH = 0;
        for (int dx = -1; dx <= 1; ++dx)
            for (int dy = -1; dy <= 1; ++dy)
                for (int dz = -1; dz <= 1; ++dz) {
                    const int64_t c[3] = {q[0] + dx, q[1] + dy, q[2] + dz};
                    const uint64_t h = hashCell(m_hit.seed, k, c[0], c[1], c[2]);
                    int64_t d2 = 0;
                    for (int a = 0; a < 3; ++a) {
                        const int64_t jit = int64_t((h >> (a * 21)) & 0x1FFFFF);  // 21 bits
                        const int64_t s = c[a] * C + ((jit * C) >> 21);
                        const int64_t dd = X[a] - s;
                        d2 += dd * dd;
                    }
                    if (d2 < best || (d2 == best && h < bestH)) { best = d2; bestH = h; }
                }
        slot = uint32_t(bestH) | 1u;
        return slot;
    }

    uint32_t glassId(size_t i) {
        uint32_t& slot = m_ids[i * size_t(m_kmax + 1)];
        if (slot) return slot;
        const glm::dvec3 r = (glm::dvec3(m_g.pos(i)) + 0.5 - glm::dvec3(m_hit.point)) * double(m_g.voxelM);
        const double ru = glm::dot(r, glm::dvec3(m_u)), rv = glm::dot(r, glm::dvec3(m_v));
        const double rho = std::sqrt(ru * ru + rv * rv);
        int ring = 0, sector = 0;
        if (rho >= m_r0) {
            ring = 1 + int(std::floor(std::log(rho / m_r0) / std::log(double(FractureModel::kGlassRing))));
            double th = std::atan2(rv, ru);
            if (th < 0) th += kTwoPi;
            const uint64_t wh = hashCell(m_hit.seed, 99, ring, 0, 0);
            const double wobble = 0.3 * (double(wh & 0xFFFF) / 65536.0 - 0.5);  // +-0.15 sector per ring
            sector = int(std::floor(th / kTwoPi * m_sectors + wobble));
            sector = ((sector % m_sectors) + m_sectors) % m_sectors;
        }
        slot = uint32_t(hashCell(m_hit.seed, 98, ring, sector, 0)) | 1u;
        return slot;
    }

private:
    const VoxelGrid& m_g;
    const Hit& m_hit;
    double m_cbrtE;
    int m_kmax = 0;
    std::vector<uint32_t> m_ids;
    glm::vec3 m_u{1, 0, 0}, m_v{0, 1, 0};
    int m_sectors = 4;
    double m_r0 = 0.1;
};

uint8_t hitMaterial(const VoxelGrid& g, const Hit& hit) {
    const glm::vec3 d = glm::length(hit.dir) > 0 ? glm::normalize(hit.dir) : glm::vec3(0, 0, 1);
    for (int s = 0; s <= 16; ++s) {
        const glm::ivec3 p = glm::ivec3(glm::floor(hit.point + d * (0.5f * float(s))));
        if (g.in(p) && g.mat[g.idx(p)]) return g.mat[g.idx(p)];
    }
    // nearest solid within 2 voxels (lowest index on ties)
    const glm::ivec3 c = glm::ivec3(glm::floor(hit.point));
    float best = 1e9f;
    uint8_t m = 0;
    for (int dx = -2; dx <= 2; ++dx)
        for (int dy = -2; dy <= 2; ++dy)
            for (int dz = -2; dz <= 2; ++dz) {
                const glm::ivec3 p = c + glm::ivec3(dx, dy, dz);
                if (!g.in(p) || !g.mat[g.idx(p)]) continue;
                const float dd = glm::length(glm::vec3(p) + 0.5f - hit.point);
                if (dd < best) { best = dd; m = g.mat[g.idx(p)]; }
            }
    return m;
}

uint32_t ufFind(std::vector<uint32_t>& par, uint32_t a) {
    while (par[a] != a) { par[a] = par[par[a]]; a = par[a]; }
    return a;
}

}  // namespace

FractureResult fractureReference(const VoxelGrid& g, const Hit& hit, const std::vector<FractureMaterial>& mats,
                                 const FractureKnobs& knobs) {
    FractureResult r;
    r.implemented = true;
    const size_t n = g.size();
    r.cut.assign(n, 0);
    r.damage = g.damage.size() == n ? g.damage : std::vector<uint8_t>(n, 0);

    std::vector<MatInfo> mi(mats.size());
    for (size_t m = 0; m < mats.size(); ++m) {
        mi[m].toughness = std::max(mats[m].toughness, 1e-6f);
        mi[m].partScale = mats[m].partBondScale;
        mi[m].glass = mats[m].cls == FractureClass::Glass;
        if (mats[m].cls == FractureClass::Fibrous) {
            const glm::vec3 a = glm::abs(mats[m].grain);
            const int ax = a.x >= a.y && a.x >= a.z ? 0 : (a.y >= a.z ? 1 : 2);
            if (a[ax] > 0.5f) mi[m].grainAxis = ax;
        }
    }

    const uint8_t hm = hitMaterial(g, hit);
    const double E = hit.energy;
    const double e = hm ? E / double(mi[hm].toughness) : 0.0;
    const double cbrtE = std::cbrt(std::max(e, 1e-9));
    const double l = FractureModel::kStressLengthM;
    const double vm = g.voxelM;
    auto stressAt = [&](glm::dvec3 vox) {
        const glm::dvec3 dv = (vox - glm::dvec3(hit.point)) * vm;
        const double d2 = glm::dot(dv, dv);
        return std::pair<double, double>(E / (1.0 + d2 / (l * l)), std::sqrt(d2));
    };

    Pattern pat(g, hit, cbrtE);

    // parts: a part no bigger than the local cell moves as a unit (§6.4 ingredient 3)
    std::unordered_map<uint16_t, std::pair<size_t, glm::dvec3>> parts;
    for (size_t i = 0; i < n; ++i)
        if (g.mat[i] && g.part[i]) {
            auto& pp = parts[g.part[i]];
            ++pp.first;
            pp.second += glm::dvec3(g.pos(i)) + 0.5;
        }
    std::unordered_map<uint16_t, bool> snapped;
    for (const auto& [id, pp] : parts) {
        const glm::dvec3 c = pp.second / double(pp.first);
        const double dM = glm::length((c - glm::dvec3(hit.point)) * vm);
        snapped[id] = std::cbrt(double(pp.first)) * vm <= pat.cellM(dM);
    }

    if (hm) {
        static const glm::ivec3 kAx[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        for (size_t i = 0; i < n; ++i) {
            const uint8_t ma = g.mat[i];
            if (!ma) continue;
            const glm::ivec3 p = g.pos(i);
            for (int a = 0; a < 3; ++a) {
                const glm::ivec3 q = p + kAx[a];
                if (!g.in(q)) continue;
                const size_t j = g.idx(q);
                const uint8_t mb = g.mat[j];
                if (!mb) continue;
                ++r.bondsTested;
                const auto [s, dM] = stressAt(glm::dvec3(p) + 0.5 + 0.5 * glm::dvec3(kAx[a]));
                const bool partsDiffer = g.part[i] != g.part[j];
                const bool matsDiffer = ma != mb;
                double k = 1.0;
                if (partsDiffer) k = std::min(mi[ma].partScale, mi[mb].partScale);
                else if (!matsDiffer && mi[ma].grainAxis == a) k = FractureModel::kGrainBond;
                const uint8_t dmg = g.damage.size() == n ? std::max(g.damage[i], g.damage[j]) : uint8_t(0);
                const double S = std::min(mi[ma].toughness, mi[mb].toughness) * k *
                                 (1.0 - FractureModel::kDamageWeaken * double(dmg) / 255.0);
                if (s < S) continue;
                bool candidate = partsDiffer || matsDiffer;
                if (!candidate && !(g.part[i] && snapped[g.part[i]])) {
                    if (mi[ma].glass) {
                        candidate = pat.glassId(i) != pat.glassId(j);
                    } else {
                        const int L = pat.level(dM);
                        for (int lv = 0; lv <= L && !candidate; ++lv)
                            candidate = pat.voronoi(i, lv, mi[ma].grainAxis) != pat.voronoi(j, lv, mi[ma].grainAxis);
                    }
                }
                if (!candidate) continue;
                ++r.candidates;
                r.cut[i] |= uint8_t(1u << a);
            }
        }
        // damage (§6.4): every solid voxel gains its share of the stress, cut or not
        for (size_t i = 0; i < n; ++i) {
            if (!g.mat[i]) continue;
            const double s = stressAt(glm::dvec3(g.pos(i)) + 0.5).first / double(mi[g.mat[i]].toughness);
            const int add = int(std::lround(255.0 * std::min(1.0, s)));
            r.damage[i] = uint8_t(std::min(255, int(r.damage[i]) + add));
        }
    }

    // pieces: union-find over uncut bonds; ids in order of each piece's first voxel (deterministic)
    std::vector<uint32_t> par(n);
    for (size_t i = 0; i < n; ++i) par[i] = uint32_t(i);
    for (size_t i = 0; i < n; ++i) {
        if (!g.mat[i]) continue;
        const glm::ivec3 p = g.pos(i);
        for (int a = 0; a < 3; ++a) {
            if (r.cut[i] & (1u << a)) continue;
            glm::ivec3 q = p;
            q[a] += 1;
            if (!g.in(q)) continue;
            const size_t j = g.idx(q);
            if (!g.mat[j]) continue;
            const uint32_t ra = ufFind(par, uint32_t(i)), rb = ufFind(par, uint32_t(j));
            if (ra != rb) par[std::max(ra, rb)] = std::min(ra, rb);
        }
    }
    r.piece.assign(n, 0);
    r.pieceAnchored.assign(1, 0);
    r.pieceSize.assign(1, 0);
    std::vector<uint32_t> rootId(n, 0);
    for (size_t i = 0; i < n; ++i) {
        if (!g.mat[i]) continue;
        const uint32_t root = ufFind(par, uint32_t(i));
        if (!rootId[root]) {
            rootId[root] = uint32_t(r.pieceSize.size());
            r.pieceSize.push_back(0);
            r.pieceAnchored.push_back(0);
        }
        const uint32_t id = rootId[root];
        r.piece[i] = id;
        ++r.pieceSize[id];
        if (g.fixed[i]) r.pieceAnchored[id] = 1;
    }
    r.pieceKind.assign(r.pieceSize.size(), PieceKind::Anchored);
    for (size_t id = 1; id < r.pieceSize.size(); ++id)
        if (!r.pieceAnchored[id])
            r.pieceKind[id] = r.pieceSize[id] < knobs.crumbVoxels ? PieceKind::Crumb : PieceKind::Body;
    return r;
}

void applyDamage(VoxelGrid& g, const FractureResult& r) {
    if (r.damage.size() == g.size()) g.damage = r.damage;
}

// ---- F4.2 anchoring at scale ---------------------------------------------------------------------------

namespace {
constexpr int kB = BlockSummary::kB;
const glm::ivec3 kDir6[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
}  // namespace

BlockSummary summarizeBlocks(const VoxelGrid& g) {
    BlockSummary s;
    s.nb = (g.dim + glm::ivec3(kB - 1)) / kB;
    s.local.assign(g.size(), 0);
    const size_t nBlocks = size_t(s.nb.x) * size_t(s.nb.y) * size_t(s.nb.z);
    s.compBase.assign(nBlocks + 1, 0);
    std::vector<size_t> q;
    for (int bx = 0; bx < s.nb.x; ++bx)
        for (int by = 0; by < s.nb.y; ++by)
            for (int bz = 0; bz < s.nb.z; ++bz) {
                const glm::ivec3 b(bx, by, bz), lo = b * kB, hi = glm::min(lo + glm::ivec3(kB), g.dim);
                const size_t bi = s.blockIndex(b);
                s.compBase[bi] = uint32_t(s.compFixed.size());
                uint16_t label = 0;
                for (int x = lo.x; x < hi.x; ++x)
                    for (int y = lo.y; y < hi.y; ++y)
                        for (int z = lo.z; z < hi.z; ++z) {
                            const size_t i0 = g.idx({x, y, z});
                            if (!g.mat[i0] || s.local[i0]) continue;
                            ++label;
                            uint8_t fixed = 0;
                            s.local[i0] = label;
                            q.assign(1, i0);
                            while (!q.empty()) {
                                const size_t i = q.back();
                                q.pop_back();
                                fixed |= g.fixed[i];
                                const glm::ivec3 p = g.pos(i);
                                for (const glm::ivec3& d : kDir6) {
                                    const glm::ivec3 r = p + d;
                                    if (glm::any(glm::lessThan(r, lo)) || glm::any(glm::greaterThanEqual(r, hi))) continue;
                                    const size_t j = g.idx(r);
                                    if (!g.mat[j] || s.local[j]) continue;
                                    s.local[j] = label;
                                    q.push_back(j);
                                }
                            }
                            s.compFixed.push_back(fixed);
                        }
            }
    s.compBase[nBlocks] = uint32_t(s.compFixed.size());
    s.components = s.compFixed.size();
    return s;
}

std::vector<uint32_t> findLoose(const VoxelGrid& g, const BlockSummary& s, const std::vector<uint8_t>& cut,
                                AnchorStats* stats) {
    AnchorStats st;
    const size_t n = g.size();
    std::vector<uint32_t> loose(n, 0);
    if (cut.size() != n || s.local.size() != n) return loose;
    const size_t nBlocks = s.compBase.size() - 1;
    auto blockOf = [&](glm::ivec3 p) { return s.blockIndex(p / kB); };
    // the bond between face neighbours i (at p) and the voxel at p + d; owned by the lower voxel
    auto bondCut = [&](size_t i, const glm::ivec3& /*p*/, int dirIdx, size_t j) {
        const int a = dirIdx / 2;
        return ((dirIdx % 2 == 0) ? (cut[i] >> a) : (cut[j] >> a)) & 1;
    };

    // dirty blocks and seeds: the endpoints of cut bonds
    std::vector<uint8_t> dirty(nBlocks, 0);
    std::vector<size_t> seeds;
    for (size_t i = 0; i < n; ++i) {
        if (!cut[i] || !g.mat[i]) continue;
        const glm::ivec3 p = g.pos(i);
        for (int a = 0; a < 3; ++a) {
            if (!((cut[i] >> a) & 1)) continue;
            glm::ivec3 r = p;
            r[a] += 1;
            if (!g.in(r) || !g.mat[g.idx(r)]) continue;
            dirty[blockOf(p)] = 1;
            dirty[blockOf(r)] = 1;
            seeds.push_back(i);
            seeds.push_back(g.idx(r));
        }
    }
    for (uint8_t d : dirty) st.dirtyBlocks += d;

    // nodes: a voxel (index < n) in a dirty block, or a (block, component) node n + flat component index
    const size_t nNodes = n + s.components;
    std::vector<int32_t> mark(nNodes, 0);     // -1 anchored, > 0 loose piece id
    std::vector<uint32_t> stamp(nNodes, 0);
    uint32_t cur = 0;
    std::vector<size_t> queue, visited;
    auto nodeOf = [&](size_t j, const glm::ivec3& r) -> size_t {
        const size_t b = blockOf(r);
        return dirty[b] ? j : n + s.compBase[b] + s.local[j] - 1;
    };
    int32_t pieces = 0;
    for (size_t seed : seeds) {
        if (mark[seed] != 0) continue;
        ++st.searches;
        ++cur;
        queue.assign(1, seed);
        visited.assign(1, seed);
        stamp[seed] = cur;
        bool anchored = false;
        auto visit = [&](size_t node) {
            if (stamp[node] == cur) return;
            stamp[node] = cur;
            queue.push_back(node);
            visited.push_back(node);
        };
        for (size_t qi = 0; qi < queue.size() && !anchored; ++qi) {
            const size_t u = queue[qi];
            if (mark[u] == -1) { anchored = true; break; }
            if (u < n) {
                ++st.voxelNodes;
                if (g.fixed[u]) { anchored = true; break; }
                const glm::ivec3 p = g.pos(u);
                for (int d = 0; d < 6; ++d) {
                    const glm::ivec3 r = p + kDir6[d];
                    if (!g.in(r)) continue;
                    const size_t j = g.idx(r);
                    if (!g.mat[j] || bondCut(u, p, d, j)) continue;
                    visit(nodeOf(j, r));
                }
            } else {
                ++st.compNodes;
                const size_t c = u - n;
                if (s.compFixed[c]) { anchored = true; break; }
                // its block: the one whose component range holds c
                const size_t b = size_t(std::upper_bound(s.compBase.begin(), s.compBase.end(), uint32_t(c)) -
                                        s.compBase.begin()) - 1;
                const uint16_t label = uint16_t(c - s.compBase[b] + 1);
                const glm::ivec3 bc(int(b / (size_t(s.nb.y) * size_t(s.nb.z))), int((b / size_t(s.nb.z)) % size_t(s.nb.y)),
                                    int(b % size_t(s.nb.z)));
                const glm::ivec3 lo = bc * kB, hi = glm::min(lo + glm::ivec3(kB), g.dim);
                // walk the block's six faces: the component's voxels there and their solid neighbours outside
                for (int d = 0; d < 6; ++d) {
                    const int a = d / 2;
                    const bool plus = d % 2 == 0;
                    const int fixedCoord = plus ? hi[a] - 1 : lo[a];
                    const int b1 = (a + 1) % 3, b2 = (a + 2) % 3;
                    for (int u1 = lo[b1]; u1 < hi[b1]; ++u1)
                        for (int u2 = lo[b2]; u2 < hi[b2]; ++u2) {
                            glm::ivec3 p;
                            p[a] = fixedCoord;
                            p[b1] = u1;
                            p[b2] = u2;
                            const size_t i = g.idx(p);
                            if (s.local[i] != label) continue;
                            const glm::ivec3 r = p + kDir6[d];
                            if (!g.in(r)) continue;
                            ++st.faceScans;
                            const size_t j = g.idx(r);
                            if (!g.mat[j] || bondCut(i, p, d, j)) continue;
                            visit(nodeOf(j, r));
                        }
                }
            }
        }
        if (anchored) {
            for (size_t v : visited) mark[v] = -1;
            continue;
        }
        ++pieces;
        for (size_t v : visited) {
            mark[v] = pieces;
            if (v < n) {
                loose[v] = uint32_t(pieces);
                ++st.looseVoxels;
                continue;
            }
            const size_t c = v - n;
            const size_t b = size_t(std::upper_bound(s.compBase.begin(), s.compBase.end(), uint32_t(c)) -
                                    s.compBase.begin()) - 1;
            const uint16_t label = uint16_t(c - s.compBase[b] + 1);
            const glm::ivec3 bc(int(b / (size_t(s.nb.y) * size_t(s.nb.z))), int((b / size_t(s.nb.z)) % size_t(s.nb.y)),
                                int(b % size_t(s.nb.z)));
            const glm::ivec3 lo = bc * kB, hi = glm::min(lo + glm::ivec3(kB), g.dim);
            for (int x = lo.x; x < hi.x; ++x)
                for (int y = lo.y; y < hi.y; ++y)
                    for (int z = lo.z; z < hi.z; ++z) {
                        const size_t i = g.idx({x, y, z});
                        if (s.local[i] == label) { loose[i] = uint32_t(pieces); ++st.looseVoxels; }
                    }
        }
    }
    st.loosePieces = size_t(pieces);
    if (stats) *stats = st;
    return loose;
}

PieceLabels labelPieces(const VoxelGrid& g, const std::vector<uint8_t>& cut) {
    PieceLabels L;
    const size_t n = g.size();
    L.label.assign(n, 0);
    L.size.assign(1, 0);
    L.anchored.assign(1, 0);
    L.centroid.assign(1, glm::vec3(0.0f));
    const bool haveCut = cut.size() == n;
    std::deque<size_t> q;
    for (size_t s = 0; s < n; ++s) {
        if (g.mat[s] == 0 || L.label[s] != 0) continue;
        const uint32_t id = uint32_t(L.size.size());
        L.size.push_back(0);
        L.anchored.push_back(0);
        glm::dvec3 sum(0.0);
        L.label[s] = id;
        q.push_back(s);
        while (!q.empty()) {
            const size_t i = q.front();
            q.pop_front();
            const glm::ivec3 p = g.pos(i);
            ++L.size[id];
            sum += glm::dvec3(p) + 0.5;
            if (g.fixed[i]) L.anchored[id] = 1;
            static const glm::ivec3 kDir[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
            for (int a = 0; a < 3; ++a) {
                for (int sgn = -1; sgn <= 1; sgn += 2) {
                    const glm::ivec3 q3 = p + kDir[a] * sgn;
                    if (!g.in(q3)) continue;
                    const size_t j = g.idx(q3);
                    if (g.mat[j] == 0 || L.label[j] != 0) continue;
                    // the bond lives on the lower voxel of the pair
                    const size_t owner = sgn > 0 ? i : j;
                    if (haveCut && (cut[owner] & uint8_t(1u << a))) continue;
                    L.label[j] = id;
                    q.push_back(j);
                }
            }
        }
        L.centroid.push_back(glm::vec3(sum / double(L.size[id])));
    }
    return L;
}

CutStats cutStats(const VoxelGrid& g, const std::vector<uint8_t>& cut) {
    CutStats s;
    if (cut.size() != g.size()) return s;
    static const glm::ivec3 kDir[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    for (size_t i = 0; i < g.size(); ++i) {
        if (!cut[i] || g.mat[i] == 0) continue;
        const glm::ivec3 p = g.pos(i);
        for (int a = 0; a < 3; ++a) {
            if (!(cut[i] & uint8_t(1u << a))) continue;
            const glm::ivec3 q = p + kDir[a];
            if (!g.in(q) || g.mat[g.idx(q)] == 0) continue;  // not a bond
            ++s.cut;
            const uint16_t pa = g.part[i], pb = g.part[g.idx(q)];
            if (pa != 0 && pb != 0 && pa != pb) ++s.betweenParts; else ++s.withinPart;
            (a == 0 ? s.alongX : a == 1 ? s.alongY : s.alongZ)++;
        }
    }
    return s;
}

bool resultMatchesChecker(const VoxelGrid& g, const FractureResult& r, std::string* why) {
    auto fail = [&](const std::string& m) { if (why) *why = m; return false; };
    if (!r.implemented) return fail("fractureReference not implemented (F4)");
    if (r.cut.size() != g.size() || r.piece.size() != g.size()) return fail("result vectors wrong size");
    const PieceLabels L = labelPieces(g, r.cut);
    std::unordered_map<uint32_t, uint32_t> fwd, back;
    for (size_t i = 0; i < g.size(); ++i) {
        const uint32_t a = r.piece[i], b = L.label[i];
        if ((a == 0) != (b == 0)) return fail("air/solid mismatch at voxel " + std::to_string(i));
        if (a == 0) continue;
        auto f = fwd.emplace(a, b).first;
        auto k = back.emplace(b, a).first;
        if (f->second != b || k->second != a) return fail("piece partition differs at voxel " + std::to_string(i));
    }
    for (auto [a, b] : fwd) {
        if (a >= r.pieceAnchored.size()) return fail("pieceAnchored too short");
        if ((r.pieceAnchored[a] != 0) != (L.anchored[b] != 0)) return fail("anchoring differs for piece " + std::to_string(a));
    }
    return true;
}

// ---- rigs ------------------------------------------------------------------------------------

std::vector<FractureMaterial> labMaterials() {
    std::vector<FractureMaterial> m(6);
    m[kAir] = {"air"};
    m[kStone] = {"Stone", FractureClass::Brittle, 10.0f, glm::vec3(0.0f), 0.3f};
    m[kBricks] = {"Bricks", FractureClass::Brittle, 6.0f, glm::vec3(0.0f), 0.3f};
    m[kWood] = {"Wood", FractureClass::Fibrous, 4.0f, glm::vec3(1, 0, 0), 0.3f};
    m[kGlass] = {"Glass", FractureClass::Glass, 1.0f, glm::vec3(0.0f), 0.3f};
    m[kWoodItem] = {"WoodItem", FractureClass::Fibrous, 4.0f, glm::vec3(0, 1, 0), 0.3f};
    return m;
}

namespace {
Rig base(const std::string& name, glm::ivec3 dim, float voxelM) {
    Rig r;
    r.name = name;
    r.voxelM = voxelM;
    r.grid.resize(dim);
    r.grid.voxelM = voxelM;
    r.mats = labMaterials();
    return r;
}
}  // namespace

Rig rigStoneWall(glm::vec3 hitPoint, float e) {
    Rig r = base("stone_wall", {24, 16, 3}, 0.125f);
    for (int x = 0; x < 24; ++x)
        for (int y = 0; y < 16; ++y)
            for (int z = 0; z < 3; ++z) r.grid.set({x, y, z}, kStone, 0, y == 0);
    r.hit = {hitPoint, {0, 0, 1}, e * r.mats[kStone].toughness, 101};
    r.prediction = "loose voxels never decrease as energy rises; more loose pieces and a smaller mean loose piece at "
                   "high energy than at low; an edge hit loosens at least as much as a centre hit at the same energy";
    return r;
}

Rig rigBrickWall(float e) {
    Rig r = base("brick_wall", {32, 16, 2}, 0.125f);
    for (int x = 0; x < 32; ++x)
        for (int y = 0; y < 16; ++y)
            for (int z = 0; z < 2; ++z) {
                const int course = y / 2;
                const int offset = (course % 2) * 2;          // running bond: half-brick offset
                const int bx = (x + offset) / 4;
                r.grid.set({x, y, z}, kBricks, uint16_t(1 + course * 100 + bx), y == 0);
            }
    r.hit = {{16, 8, 0}, {0, 0, 1}, e * r.mats[kBricks].toughness, 202};
    r.prediction = "breaks along mortar: > 80 % of cut bonds lie between parts (bricks)";
    return r;
}

Rig rigBeam(float e) {
    Rig r = base("beam", {32, 2, 2}, 0.125f);
    for (int x = 0; x < 32; ++x)
        for (int y = 0; y < 2; ++y)
            for (int z = 0; z < 2; ++z) r.grid.set({x, y, z}, kWood, 1, x < 2 || x >= 30);
    r.hit = {{16, 2, 1}, {0, -1, 0}, e * r.mats[kWood].toughness, 303};
    r.prediction = "splits along the grain: cut bonds in y and z (cracks running along x) are > 3x the cut bonds in x";
    return r;
}

Rig rigGlassPane(float e) {
    Rig r = base("glass_pane", {24, 24, 1}, 0.125f);
    for (int x = 0; x < 24; ++x)
        for (int y = 0; y < 24; ++y)
            r.grid.set({x, y, 0}, kGlass, 0, x == 0 || y == 0 || x == 23 || y == 23);
    r.hit = {{12, 12, 0}, {0, 0, 1}, e * r.mats[kGlass].toughness, 404};
    r.prediction = "radial + concentric shards: >= 6 loose shards whose centroids cover >= 6 of 8 angular sectors "
                   "around the hit";
    return r;
}

Rig rigColumnLintel(float e) {
    Rig r = base("column_lintel", {16, 14, 2}, 0.125f);
    for (int z = 0; z < 2; ++z) {
        for (int y = 0; y < 12; ++y)
            for (int x = 7; x <= 8; ++x) r.grid.set({x, y, z}, kStone, 1, y == 0);   // column
        for (int y = 12; y < 14; ++y)
            for (int x = 0; x < 16; ++x) r.grid.set({x, y, z}, kStone, 2, false);    // lintel
    }
    r.hit = {{8, 6, 0}, {0, 0, 1}, e * r.mats[kStone].toughness, 505};
    r.prediction = "a hit that severs the column leaves every lintel voxel in a loose piece; the column's base stays "
                   "anchored";
    return r;
}

Rig rigChair(float e) {
    // item voxels, 1/64 m: seat 24x3x24 (37.5 cm) at y 24..26, legs 3x24x3, back 24x24x3 at z 21..23
    Rig r = base("chair", {24, 51, 24}, 1.0f / 64.0f);
    auto box = [&](glm::ivec3 lo, glm::ivec3 hi, uint16_t part) {
        for (int x = lo.x; x <= hi.x; ++x)
            for (int y = lo.y; y <= hi.y; ++y)
                for (int z = lo.z; z <= hi.z; ++z) r.grid.set({x, y, z}, kWoodItem, part, false);
    };
    box({0, 0, 0}, {2, 23, 2}, 2);
    box({21, 0, 0}, {23, 23, 2}, 3);
    box({0, 0, 21}, {2, 23, 23}, 4);
    box({21, 0, 21}, {23, 23, 23}, 5);
    box({0, 24, 0}, {23, 26, 23}, 1);
    box({0, 27, 21}, {23, 50, 23}, 6);
    r.hit = {{0, 12, 1}, {1, 0, 0}, e * r.mats[kWoodItem].toughness, 606};
    r.prediction = "breaks at its joints: > 80 % of cut bonds lie between parts, and >= 2 pieces";
    return r;
}

}  // namespace Phyxel::Core::VoxFound
