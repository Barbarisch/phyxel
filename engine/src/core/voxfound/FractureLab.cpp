// docs/VoxelFoundationPlan.md §6 + §8 — fracture lab (F0 skeleton). See FractureLab.h.

#include "core/voxfound/FractureLab.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <unordered_map>

namespace Phyxel::Core::VoxFound {

// ---- F4.1 / F4.3: the CPU reference (model: voxfound_f41_prediction.txt; integer form: voxfound_f43_prediction.txt) --

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

double cellMOf(double cbrtE, double dM) {
    return FractureModel::kCellM * (1.0 + dM / FractureModel::kStressLengthM) / cbrtE;
}

// exact angle order of integer vectors: a before-or-at b, angles in [0, 2pi)
int halfOf(int64_t x, int64_t y) { return (y < 0 || (y == 0 && x < 0)) ? 1 : 0; }
bool angleLE(int64_t ax, int64_t ay, int64_t bx, int64_t by) {
    const int ha = halfOf(ax, ay), hb = halfOf(bx, by);
    if (ha != hb) return ha < hb;
    return ax * by - ay * bx >= 0;
}

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

// ---- the per-voxel / per-bond integer rules (the GPU shader mirrors these exactly) ----

uint32_t voronoiId(const FracturePlan& P, glm::ivec3 p, int k, int grainAxis) {
    int64_t X[3] = {int64_t(p.x) * 1024 + 512, int64_t(p.y) * 1024 + 512, int64_t(p.z) * 1024 + 512};
    if (grainAxis >= 0)
        for (int a = 0; a < 3; ++a)
            if (a != grainAxis) X[a] *= int64_t(FractureModel::kGrainStretch);
    const int64_t C = P.cellC[size_t(2 * k + (grainAxis >= 0 ? 1 : 0))];
    const int64_t q[3] = {floorDiv(X[0], C), floorDiv(X[1], C), floorDiv(X[2], C)};
    int64_t best = INT64_MAX;
    uint64_t bestH = 0;
    for (int dx = -1; dx <= 1; ++dx)
        for (int dy = -1; dy <= 1; ++dy)
            for (int dz = -1; dz <= 1; ++dz) {
                const int64_t c[3] = {q[0] + dx, q[1] + dy, q[2] + dz};
                const uint64_t h = hashCell(P.seed, k, c[0], c[1], c[2]);
                int64_t d2 = 0;
                for (int a = 0; a < 3; ++a) {
                    const int64_t jit = int64_t((h >> (a * 21)) & 0x1FFFFF);  // 21 bits
                    const int64_t s = c[a] * C + ((jit * C) >> 21);
                    const int64_t dd = X[a] - s;
                    d2 += dd * dd;
                }
                if (d2 < best || (d2 == best && h < bestH)) { best = d2; bestH = h; }
            }
    return uint32_t(bestH) | 1u;
}

uint32_t glassId(const FracturePlan& P, glm::ivec3 p) {
    const int64_t du = int64_t(p[P.glassU]) * 16 + 8 - P.hit16[P.glassU];
    const int64_t dv = int64_t(p[P.glassV]) * 16 + 8 - P.hit16[P.glassV];
    const int64_t rho2 = du * du + dv * dv;
    int ring = 0;
    for (int64_t r2 : P.ringR2) ring += (r2 <= rho2) ? 1 : 0;
    int sector = 0;
    if (ring > 0) {
        const int32_t* b = P.bounds.data() + size_t(ring - 1) * size_t(P.sectors) * 2;
        for (int j = 0; j < P.sectors; ++j) sector += angleLE(b[2 * j], b[2 * j + 1], du, dv) ? 1 : 0;
        sector %= P.sectors;
    }
    return uint32_t(hashCell(P.seed, 98, ring, sector, 0)) | 1u;
}

int64_t dist2q16(const FracturePlan& P, glm::ivec3 p, int axis /* -1: the voxel centre */) {
    int64_t d2 = 0;
    for (int a = 0; a < 3; ++a) {
        const int64_t m = int64_t(p[a]) * 16 + 8 + (a == axis ? 8 : 0) - P.hit16[a];
        d2 += m * m;
    }
    return d2;
}

int levelAt(const FracturePlan& P, int64_t d2) {
    int L = 0;
    for (int k = 0; k <= P.kmax; ++k) {
        const int64_t A = 2 * int64_t(P.cq) * P.L - (int64_t(256) << k) * P.L;
        if (A < 0) continue;
        const int64_t s = int64_t(256) << k;
        if (uint64_t(A) * uint64_t(A) >= uint64_t(s * s) * uint64_t(d2)) L = k;
    }
    return L;
}

}  // namespace

FracturePlan planFracture(const VoxelGrid& g, const Hit& hit, const std::vector<FractureMaterial>& mats) {
    FracturePlan P;
    P.seed = hit.seed;
    const size_t nm = mats.size();
    P.tq.assign(nm, 16);
    P.partQ.assign(nm, 300);
    P.grainAxis.assign(nm, -1);
    P.glass.assign(nm, 0);
    for (size_t m = 0; m < nm; ++m) {
        P.tq[m] = int32_t(std::max<int64_t>(1, std::llround(double(mats[m].toughness) * 16.0)));
        P.partQ[m] = int32_t(std::llround(double(mats[m].partBondScale) * 1000.0));
        P.glass[m] = mats[m].cls == FractureClass::Glass ? 1 : 0;
        if (mats[m].cls == FractureClass::Fibrous) {
            const glm::vec3 a = glm::abs(mats[m].grain);
            const int ax = a.x >= a.y && a.x >= a.z ? 0 : (a.y >= a.z ? 1 : 2);
            if (a[ax] > 0.5f) P.grainAxis[m] = int8_t(ax);
        }
    }
    for (int a = 0; a < 3; ++a) P.hit16[a] = int32_t(std::llround(double(hit.point[a]) * 16.0));
    P.L = int32_t(std::llround(FractureModel::kStressLengthM / double(g.voxelM) * 16.0));
    P.kmax = 0;
    while (FractureModel::kLevel0M / float(1 << (P.kmax + 1)) >= g.voxelM - 1e-6f && P.kmax < 12) ++P.kmax;
    P.cellC.assign(size_t(2 * (P.kmax + 1)), 1);
    for (int k = 0; k <= P.kmax; ++k) {
        const double spacing = double(FractureModel::kLevel0M) / double(1 << k) / double(g.voxelM) * 1024.0;
        P.cellC[size_t(2 * k)] = std::max<int64_t>(1, std::llround(spacing));
        P.cellC[size_t(2 * k + 1)] = std::max<int64_t>(
            1, std::llround(spacing * std::cbrt(double(FractureModel::kGrainStretch) * double(FractureModel::kGrainStretch))));
    }

    P.hitMat = hitMaterial(g, hit);
    if (!P.hitMat) return P;
    P.valid = true;
    P.eq = std::llround(double(hit.energy) * 16.0);
    P.eOverT = double(P.eq) / double(P.tq[P.hitMat]);
    const double cbrtE = std::cbrt(std::max(P.eOverT, 1e-9));
    P.cq = int32_t(std::llround(cbrtE * 256.0));
    const double vm = g.voxelM;

    // parts: a part no bigger than the local cell moves as a unit (§6.4 ingredient 3)
    uint16_t maxPart = 0;
    for (size_t i = 0; i < g.size(); ++i)
        if (g.mat[i]) maxPart = std::max(maxPart, g.part[i]);
    std::vector<size_t> cnt(size_t(maxPart) + 1, 0);
    std::vector<glm::dvec3> sum(size_t(maxPart) + 1, glm::dvec3(0.0));
    for (size_t i = 0; i < g.size(); ++i)
        if (g.mat[i] && g.part[i]) {
            ++cnt[g.part[i]];
            sum[g.part[i]] += glm::dvec3(g.pos(i)) + 0.5;
        }
    P.partWhole.assign(size_t(maxPart) + 1, 0);
    for (size_t id = 1; id <= maxPart; ++id) {
        if (!cnt[id]) continue;
        const glm::dvec3 c = sum[id] / double(cnt[id]);
        const double dM = glm::length((c - glm::dvec3(hit.point)) * vm);
        P.partWhole[id] = std::cbrt(double(cnt[id])) * vm <= cellMOf(cbrtE, dM) ? 1 : 0;
    }

    // glass: the plane across the hit's dominant axis; rings and sector boundaries as integer tables
    const glm::vec3 ad = glm::abs(hit.dir);
    P.glassAxis = ad.x >= ad.y && ad.x >= ad.z ? 0 : (ad.y >= ad.z ? 1 : 2);
    P.glassU = (P.glassAxis + 1) % 3;
    P.glassV = (P.glassAxis + 2) % 3;
    if (P.glassU > P.glassV) std::swap(P.glassU, P.glassV);
    P.sectors = std::max(4, int(std::lround(4.0 * cbrtE)));
    const double r0q = 0.5 * cellMOf(cbrtE, 0.0) / vm * 16.0;
    const double diag = 16.0 * std::sqrt(double(g.dim.x) * g.dim.x + double(g.dim.y) * g.dim.y + double(g.dim.z) * g.dim.z);
    for (double R = r0q; R <= 2.0 * diag + 16.0; R *= double(FractureModel::kGlassRing))
        P.ringR2.push_back(int64_t(std::ceil(R * R)));
    for (size_t ring = 1; ring <= P.ringR2.size(); ++ring) {
        const uint64_t wh = hashCell(P.seed, 99, int64_t(ring), 0, 0);
        const double wobble = 0.3 * (double(wh & 0xFFFF) / 65536.0 - 0.5);  // +-0.15 sector per ring
        std::vector<std::pair<int32_t, int32_t>> b;
        for (int j = 0; j < P.sectors; ++j) {
            const double th = kTwoPi * (double(j) - wobble) / double(P.sectors);
            b.emplace_back(int32_t(std::lround(std::cos(th) * 16384.0)), int32_t(std::lround(std::sin(th) * 16384.0)));
        }
        std::sort(b.begin(), b.end(), [](const auto& x, const auto& y) {
            return angleLE(x.first, x.second, y.first, y.second) && !angleLE(y.first, y.second, x.first, x.second);
        });
        for (const auto& [x, y] : b) { P.bounds.push_back(x); P.bounds.push_back(y); }
    }
    return P;
}

FractureResult fractureReference(const VoxelGrid& g, const Hit& hit, const std::vector<FractureMaterial>& mats,
                                 const FractureKnobs& knobs) {
    FractureResult r;
    r.implemented = true;
    const size_t n = g.size();
    r.cut.assign(n, 0);
    r.damage = g.damage.size() == n ? g.damage : std::vector<uint8_t>(n, 0);
    const FracturePlan P = planFracture(g, hit, mats);

    if (P.valid) {
        const uint64_t L2 = uint64_t(P.L) * uint64_t(P.L);
        const uint64_t lhs = uint64_t(P.eq) * L2 * 1000u * 510u;
        std::vector<uint32_t> ids(n * size_t(P.kmax + 1), 0);
        auto vid = [&](size_t i, int k, int grain) {
            uint32_t& s = ids[i * size_t(P.kmax + 1) + size_t(k)];
            if (!s) s = voronoiId(P, g.pos(i), k, grain);
            return s;
        };
        auto gid = [&](size_t i) {
            uint32_t& s = ids[i * size_t(P.kmax + 1)];
            if (!s) s = glassId(P, g.pos(i));
            return s;
        };
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
                const bool partsDiffer = g.part[i] != g.part[j];
                const bool matsDiffer = ma != mb;
                uint64_t kq = 1000;
                if (partsDiffer) kq = uint64_t(std::min(P.partQ[ma], P.partQ[mb]));
                else if (!matsDiffer && P.grainAxis[ma] == a) kq = uint64_t(FractureModel::kGrainBond * 1000.0f);
                const uint64_t dmg = g.damage.size() == n ? std::max(g.damage[i], g.damage[j]) : 0u;
                const int64_t d2 = dist2q16(P, p, a);
                const uint64_t rhs = uint64_t(std::min(P.tq[ma], P.tq[mb])) * kq * (510u - dmg) * (L2 + uint64_t(d2));
                if (lhs < rhs) continue;
                bool candidate = partsDiffer || matsDiffer;
                if (!candidate && !(g.part[i] && P.partWhole[g.part[i]])) {
                    if (P.glass[ma]) {
                        candidate = gid(i) != gid(j);
                    } else {
                        const int L = levelAt(P, d2);
                        for (int lv = 0; lv <= L && !candidate; ++lv)
                            candidate = vid(i, lv, P.grainAxis[ma]) != vid(j, lv, P.grainAxis[ma]);
                    }
                }
                if (!candidate) continue;
                ++r.candidates;
                r.cut[i] |= uint8_t(1u << a);
            }
        }
        // damage (§6.4): every solid voxel gains round(255 * min(1, s / T)), saturating
        const uint64_t num2 = 2u * 255u * uint64_t(P.eq) * L2;
        for (size_t i = 0; i < n; ++i) {
            if (!g.mat[i]) continue;
            const uint64_t den = uint64_t(P.tq[g.mat[i]]) * (L2 + uint64_t(dist2q16(P, g.pos(i), -1)));
            const uint64_t add = std::min<uint64_t>(255u, (num2 + den) / (2u * den));
            r.damage[i] = uint8_t(std::min<uint64_t>(255u, uint64_t(r.damage[i]) + add));
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
    finishPieces(g, r, par, knobs);
    return r;
}

void finishPieces(const VoxelGrid& g, FractureResult& r, std::vector<uint32_t>& par, const FractureKnobs& knobs) {
    const size_t n = g.size();
    r.piece.assign(n, 0);
    r.pieceAnchored.assign(1, 0);
    r.pieceSize.assign(1, 0);
    std::vector<uint32_t> rootId(n, 0);
    for (size_t i = 0; i < n; ++i) {
        if (!g.mat[i]) continue;
        uint32_t root = uint32_t(i);
        while (par[root] != root) root = par[root];
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
