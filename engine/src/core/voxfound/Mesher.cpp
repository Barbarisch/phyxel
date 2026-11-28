#include "core/voxfound/Mesher.h"
#include "core/voxfound/StorageLab.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace Phyxel::Core::VoxFound {

namespace {

// Material of the exposed face of voxel p looking along (axis, dir), or 0 when there is no face.
// A card voxel (leaf, F2.3) draws no faces, but its neighbours still see it as solid.
// D4 follow-up: a CLEAR neighbour (glass) does not hide an opaque voxel's face (CardSet::clear).
uint8_t faceAt(const VoxelGrid& g, Edges outside, glm::ivec3 p, int axis, int dir, const CardSet* cards) {
    const uint8_t m = g.mat[g.idx(p)];
    if (m == 0 || (cards && cards->has(m))) return 0;
    const bool pClear = cards && cards->isClear(m);
    glm::ivec3 q = p;
    q[axis] += dir;
    if (!g.in(q)) return outside.hidesOutside(p, axis, dir, pClear) ? 0 : m;
    const uint8_t qm = g.mat[g.idx(q)];
    return (qm == 0 || (!pClear && cards && cards->isClear(qm))) ? m : 0;
}

glm::ivec3 axisPoint(int axis, int s, int u, int v) {
    glm::ivec3 p;
    p[axis] = s;
    p[(axis + 1) % 3] = u;
    p[(axis + 2) % 3] = v;
    return p;
}

}  // namespace


// ---- F3.2a: the edit path's readers (a dense grid, or the BlockStore) -------------------------------------------
namespace {
struct GridRead {
    const VoxelGrid& g;
    glm::ivec3 dim;
    explicit GridRead(const VoxelGrid& grid) : g(grid), dim(grid.dim) {}
    bool in(glm::ivec3 p) const { return g.in(p); }
    uint8_t at(glm::ivec3 p) const { return g.mat[g.idx(p)]; }
    bool uniform(glm::ivec3, uint8_t&) const { return false; }
};
struct StoreRead {
    const BlockStore& s;
    glm::ivec3 dim;
    explicit StoreRead(const BlockStore& st) : s(st), dim(st.dim()) {}
    bool in(glm::ivec3 p) const { return s.in(p); }
    uint8_t at(glm::ivec3 p) const { return s.at(p); }
    bool uniform(glm::ivec3 b, uint8_t& m) const { return s.uniformBlock(b, m); }
};
template <class Rd>
uint8_t faceAtR(const Rd& r, Edges outside, glm::ivec3 p, int axis, int dir, const CardSet* cards) {
    const uint8_t m = r.at(p);
    if (m == 0 || (cards && cards->has(m))) return 0;
    const bool pClear = cards && cards->isClear(m);
    glm::ivec3 q = p;
    q[axis] += dir;
    if (!r.in(q)) return outside.hidesOutside(p, axis, dir, pClear) ? 0 : m;
    const uint8_t qm = r.at(q);
    return (qm == 0 || (!pClear && cards && cards->isClear(qm))) ? m : 0;
}
template <class Rd>
RunsDelta runsPlanLazy(const Rd& r, Edges outside, const RunsPlan& plan, const CardSet* cards) {
    // F3.1: the face mask is evaluated LAZILY. A face is added only if its run meets [uLo - 1, uHi + 1], so per head
    // row only the runs touching that window are walked (from the run holding its first cell to the last run starting
    // in it); the head / stack tests read the cells [u - 1, u + w] of rows m0..m1 only, as meshRunsPlanWholeRows does.
    // Same runs, same tests, same faces (VoxFoundMesher.RowRebuildReadsOnlyNearTheFootprint).
    RunsDelta d;
    d.removed = plan.removed;
    std::vector<int16_t> mask;
    for (const SliceJob& j : plan.slices) {
        const int a = j.axis;
        const int nu = r.dim[(a + 1) % 3], nv = r.dim[(a + 2) % 3];
        const int m0 = std::max(0, j.w0 - 1), m1 = std::min(nv - 1, j.w1 + 1);
        d.rowsMeshed += size_t(std::max(0, j.w1 - j.w0 + 1));
        const int wLo = std::max(0, j.uLo - 1), wHi = std::min(nu - 1, j.uHi + 1);
        if (j.w0 > j.w1 || wLo > wHi) continue;
        mask.assign(size_t(m1 - m0 + 1) * size_t(nu), int16_t(-1));
        auto at = [&](int u, int v) -> uint8_t {
            int16_t& c = mask[size_t(v - m0) * nu + u];
            if (c < 0) {
                c = int16_t(faceAtR(r, outside, axisPoint(a, j.s, u, v), a, j.dir, cards));
                ++d.cellsEvaluated;
            }
            return uint8_t(c);
        };
        auto sameRun = [&](int u, int w, int r, uint8_t m) {
            if (r < m0 || r > m1) return false;   // never reached for an exact `before` (see the header)
            if (u > 0 && at(u - 1, r) == m) return false;
            if (u + w < nu && at(u + w, r) == m) return false;
            for (int t = 0; t < w; ++t)
                if (at(u + t, r) != m) return false;
            return true;
        };
        for (int v = j.w0; v <= j.w1; ++v) {
            int u = wLo;
            if (const uint8_t first = at(u, v))
                while (u > 0 && at(u - 1, v) == first) --u;   // the run holding the window's first cell starts here
            while (u <= wHi) {
                const uint8_t m = at(u, v);
                if (!m) { ++u; continue; }
                int w = 1;
                while (u + w < nu && at(u + w, v) == m) ++w;
                if (!(v > 0 && sameRun(u, w, v - 1, m))) {
                    int h = 1;
                    while (v + h < nv && sameRun(u, w, v + h, m)) ++h;
                    if (u - 1 <= j.uHi && u + w >= j.uLo && v - 1 <= j.vHi && v + h >= j.vLo) {
                        Quad q;
                        q.o = axisPoint(a, j.s, u, v);
                        q.axis = uint8_t(a);
                        q.dir = int8_t(j.dir);
                        q.du = uint16_t(w);
                        q.dv = uint16_t(h);
                        q.material = m;
                        d.added.push_back(packFace(q));
                    }
                }
                u += w;
            }
        }
    }
    return d;
}

template <class Rd>
RunsDelta runsPlanWhole(const Rd& r, Edges outside, const RunsPlan& plan, const CardSet* cards) {
    RunsDelta d;
    d.removed = plan.removed;
    std::vector<uint8_t> mask;
    for (const SliceJob& j : plan.slices) {
        const int a = j.axis;
        const int nu = r.dim[(a + 1) % 3], nv = r.dim[(a + 2) % 3];
        const int m0 = std::max(0, j.w0 - 1), m1 = std::min(nv - 1, j.w1 + 1);   // mask rows (head + end tests)
        mask.assign(size_t(m1 - m0 + 1) * size_t(nu), 0);
        for (int v = m0; v <= m1; ++v)
            for (int u = 0; u < nu; ++u)
                mask[size_t(v - m0) * nu + u] = faceAtR(r, outside, axisPoint(a, j.s, u, v), a, j.dir, cards);
        d.rowsMeshed += size_t(j.w1 - j.w0 + 1);
        d.cellsEvaluated += size_t(m1 - m0 + 1) * size_t(nu);
        auto at = [&](int u, int v) { return mask[size_t(v - m0) * nu + u]; };
        auto sameRun = [&](int u, int w, int r, uint8_t m) {
            if (r < m0 || r > m1) return false;   // never reached for an exact `before` (see the header)
            if (u > 0 && at(u - 1, r) == m) return false;
            if (u + w < nu && at(u + w, r) == m) return false;
            for (int t = 0; t < w; ++t)
                if (at(u + t, r) != m) return false;
            return true;
        };
        for (int v = j.w0; v <= j.w1; ++v)
            for (int u = 0; u < nu;) {
                const uint8_t m = at(u, v);
                if (!m) { ++u; continue; }
                int w = 1;
                while (u + w < nu && at(u + w, v) == m) ++w;
                if (!(v > 0 && sameRun(u, w, v - 1, m))) {
                    int h = 1;
                    while (v + h < nv && sameRun(u, w, v + h, m)) ++h;
                    if (u - 1 <= j.uHi && u + w >= j.uLo && v - 1 <= j.vHi && v + h >= j.vLo) {
                        Quad q;
                        q.o = axisPoint(a, j.s, u, v);
                        q.axis = uint8_t(a);
                        q.dir = int8_t(j.dir);
                        q.du = uint16_t(w);
                        q.dv = uint16_t(h);
                        q.material = m;
                        d.added.push_back(packFace(q));
                    }
                }
                u += w;
            }
    }
    return d;
}

template <class Rd>
std::vector<PackedFace> sprigsBoxR(const Rd& r, Edges outside, const SprigParams& p, glm::ivec3 lo, glm::ivec3 hi) {
    std::vector<PackedFace> out;
    lo = glm::max(lo, glm::ivec3(0));
    hi = glm::min(hi, r.dim - 1);
    for (int x = lo.x; x <= hi.x; ++x)   // index order, as foliageSprigs
        for (int y = lo.y; y <= hi.y; ++y)
            for (int z = lo.z; z <= hi.z; ++z) {
                const glm::ivec3 c(x, y, z);
                const uint8_t m = r.at(c);
                if (m == 0 || !p.cards.has(m)) continue;
                if (sprigHash24(p.worldOriginVoxel + c) >= p.threshold24) continue;
                bool exposed = false;
                for (int a = 0; a < 3 && !exposed; ++a)
                    for (int dd : {-1, 1}) {
                        glm::ivec3 q = c;
                        q[a] += dd;
                        if (r.in(q) ? r.at(q) == 0 : !outside.solidOutside(c, a, dd)) { exposed = true; break; }
                    }
                if (exposed) out.push_back(packSprig(c, m));
            }
    return out;
}

template <class Rd>
size_t updateSprigsR(std::vector<PackedFace>& sprigs, const Rd& r, Edges outside, const SprigParams& p, glm::ivec3 lo,
                     glm::ivec3 hi, bool* changed = nullptr) {
    const glm::ivec3 rlo = lo - 1, rhi = hi + 1;
    auto inBox = [&](const PackedFace& f) {
        const glm::ivec3 c(int(f.a & 0xFF), int((f.a >> 8) & 0xFF), int((f.a >> 16) & 0xFF));
        return c.x >= rlo.x && c.y >= rlo.y && c.z >= rlo.z && c.x <= rhi.x && c.y <= rhi.y && c.z <= rhi.z;
    };
    const std::vector<PackedFace> add = sprigsBoxR(r, outside, p, rlo, rhi);
    if (changed) {
        // F3.5a: the box's sprigs as a set; the same set leaves the list (and its order) untouched
        std::vector<uint64_t> was, now;
        for (const PackedFace& f : sprigs)
            if (inBox(f)) was.push_back(f.key());
        *changed = was.size() != add.size();
        if (!*changed) {
            for (const PackedFace& f : add) now.push_back(f.key());
            std::sort(was.begin(), was.end());
            std::sort(now.begin(), now.end());
            *changed = was != now;
        }
        if (!*changed) return 0;
    }
    sprigs.erase(std::remove_if(sprigs.begin(), sprigs.end(), inBox), sprigs.end());
    sprigs.insert(sprigs.end(), add.begin(), add.end());
    return add.size();
}

template <class Rd>
uint8_t blockBlocksR(const Rd& r, const CardSet& seeThrough, glm::ivec3 b) {
    uint8_t u;
    if (r.uniform(b, u)) return (u != 0 && !seeThrough.has(u)) ? 1 : 0;   // F3.2a: a uniform block from its entry
    for (int x = 0; x < 8; ++x)
        for (int y = 0; y < 8; ++y)
            for (int z = 0; z < 8; ++z) {
                const uint8_t m = r.at({b.x * 8 + x, b.y * 8 + y, b.z * 8 + z});
                if (m == 0 || seeThrough.has(m)) return 0;
            }
    return 1;
}

template <class Rd>
bool updateBlockingR(const Rd& r, const CardSet& seeThrough, glm::ivec3 lo, glm::ivec3 hi, std::vector<uint8_t>& blocking,
                     std::vector<uint32_t>* flipped = nullptr) {
    if (flipped) flipped->clear();
    const glm::ivec3 nb = r.dim / 8;
    const glm::ivec3 blo = glm::max(lo, glm::ivec3(0)) / 8, bhi = glm::min(hi, r.dim - 1) / 8;
    if (lo.x > r.dim.x - 1 || lo.y > r.dim.y - 1 || lo.z > r.dim.z - 1 || hi.x < 0 || hi.y < 0 || hi.z < 0) return false;
    bool changed = false;
    for (int bx = blo.x; bx <= bhi.x; ++bx)
        for (int by = blo.y; by <= bhi.y; ++by)
            for (int bz = blo.z; bz <= bhi.z; ++bz) {
                const size_t k = size_t(bz + nb.z * (by + nb.y * bx));
                uint8_t& b = blocking[k];
                const uint8_t now = blockBlocksR(r, seeThrough, {bx, by, bz}) ? 1 : 0;
                if (now != b && flipped) flipped->push_back(uint32_t(k));
                changed |= now != b;
                b = now;
            }
    return changed;
}
}  // namespace

std::vector<Quad> meshCulled(const VoxelGrid& g, Edges outside, const CardSet* cards) {
    std::vector<Quad> out;
    for (size_t i = 0; i < g.size(); ++i) {
        if (g.mat[i] == 0) continue;
        const glm::ivec3 p = g.pos(i);
        for (int axis = 0; axis < 3; ++axis)
            for (int dir : {-1, 1}) {
                const uint8_t m = faceAt(g, outside, p, axis, dir, cards);
                if (!m) continue;
                Quad q;
                q.o = p; q.axis = uint8_t(axis); q.dir = int8_t(dir); q.material = m;
                out.push_back(q);
            }
    }
    return out;
}

std::vector<Quad> meshGreedy(const VoxelGrid& g, Edges outside, int blockLimit, const CardSet* cards) {
    std::vector<Quad> out;
    std::vector<uint8_t> mask;
    for (int axis = 0; axis < 3; ++axis) {
        const int ua = (axis + 1) % 3, va = (axis + 2) % 3;
        const int ns = g.dim[axis], nu = g.dim[ua], nv = g.dim[va];
        mask.assign(size_t(nu) * nv, 0);
        for (int dir : {-1, 1})
            for (int s = 0; s < ns; ++s) {
                for (int v = 0; v < nv; ++v)
                    for (int u = 0; u < nu; ++u)
                        mask[size_t(v) * nu + u] = faceAt(g, outside, axisPoint(axis, s, u, v), axis, dir, cards);
                for (int v = 0; v < nv; ++v)
                    for (int u = 0; u < nu;) {
                        const uint8_t m = mask[size_t(v) * nu + u];
                        if (!m) { ++u; continue; }
                        const int uEnd = blockLimit > 0 ? std::min(nu, (u / blockLimit + 1) * blockLimit) : nu;
                        const int vEnd = blockLimit > 0 ? std::min(nv, (v / blockLimit + 1) * blockLimit) : nv;
                        int w = 1;
                        while (u + w < uEnd && mask[size_t(v) * nu + u + w] == m) ++w;
                        int h = 1;
                        for (; v + h < vEnd; ++h) {
                            bool row = true;
                            for (int k = 0; k < w && row; ++k) row = mask[size_t(v + h) * nu + u + k] == m;
                            if (!row) break;
                        }
                        for (int dv = 0; dv < h; ++dv)
                            for (int k = 0; k < w; ++k) mask[size_t(v + dv) * nu + u + k] = 0;
                        Quad q;
                        q.o = axisPoint(axis, s, u, v);
                        q.axis = uint8_t(axis); q.dir = int8_t(dir);
                        q.du = uint16_t(w); q.dv = uint16_t(h); q.material = m;
                        out.push_back(q);
                        u += w;
                    }
            }
    }
    return out;
}

std::vector<Quad> meshRuns(const VoxelGrid& g, Edges outside, const CardSet* cards) {
    std::vector<Quad> out;
    std::vector<uint8_t> mask;
    for (int axis = 0; axis < 3; ++axis) {
        const int ua = (axis + 1) % 3, va = (axis + 2) % 3;
        const int ns = g.dim[axis], nu = g.dim[ua], nv = g.dim[va];
        mask.assign(size_t(nu) * nv, 0);
        auto at = [&](int u, int v) { return mask[size_t(v) * nu + u]; };
        // Row r holds exactly the maximal run [u, u+w) of material m.
        auto sameRun = [&](int u, int w, int r, uint8_t m) {
            if (u > 0 && at(u - 1, r) == m) return false;
            if (u + w < nu && at(u + w, r) == m) return false;
            for (int k = 0; k < w; ++k)
                if (at(u + k, r) != m) return false;
            return true;
        };
        for (int dir : {-1, 1})
            for (int s = 0; s < ns; ++s) {
                for (int v = 0; v < nv; ++v)
                    for (int u = 0; u < nu; ++u)
                        mask[size_t(v) * nu + u] = faceAt(g, outside, axisPoint(axis, s, u, v), axis, dir, cards);
                for (int v = 0; v < nv; ++v)
                    for (int u = 0; u < nu;) {
                        const uint8_t m = at(u, v);
                        if (!m) { ++u; continue; }
                        int w = 1;
                        while (u + w < nu && at(u + w, v) == m) ++w;
                        if (!(v > 0 && sameRun(u, w, v - 1, m))) {
                            int h = 1;
                            while (v + h < nv && sameRun(u, w, v + h, m)) ++h;
                            Quad q;
                            q.o = axisPoint(axis, s, u, v);
                            q.axis = uint8_t(axis); q.dir = int8_t(dir);
                            q.du = uint16_t(w); q.dv = uint16_t(h); q.material = m;
                            out.push_back(q);
                        }
                        u += w;
                    }
            }
    }
    return out;
}

PackedFace packFace(const Quad& q) {
    PackedFace f;
    f.a = uint32_t(q.o.x & 0xFF) | (uint32_t(q.o.y & 0xFF) << 8) | (uint32_t(q.o.z & 0xFF) << 16) |
          (uint32_t(q.axis & 3) << 24) | (q.dir > 0 ? (1u << 26) : 0u);
    f.b = uint32_t((q.du - 1) & 0xFF) | (uint32_t((q.dv - 1) & 0xFF) << 8) | (uint32_t(q.material) << 16);
    return f;
}

Quad unpackFace(PackedFace f) {
    Quad q;
    q.o = {int(f.a & 0xFF), int((f.a >> 8) & 0xFF), int((f.a >> 16) & 0xFF)};
    q.axis = uint8_t((f.a >> 24) & 3);
    q.dir = (f.a >> 26) & 1 ? 1 : -1;
    q.du = uint16_t((f.b & 0xFF) + 1);
    q.dv = uint16_t(((f.b >> 8) & 0xFF) + 1);
    q.material = uint8_t((f.b >> 16) & 0xFF);
    return q;
}

size_t coverageErrors(const VoxelGrid& g, Edges outside, const std::vector<Quad>& quads, std::string* why,
                      const CardSet* cards) {
    // key = voxel index * 6 + face slot
    auto key = [&](glm::ivec3 p, int axis, int dir) { return g.idx(p) * 6 + size_t(axis * 2 + (dir > 0)); };
    std::unordered_map<size_t, int> hits;
    size_t errors = 0;
    std::ostringstream os;
    int shown = 0;
    auto note = [&](const std::string& s) { if (shown++ < 5) os << s << "\n"; };
    for (const Quad& q : quads) {
        const int ua = (q.axis + 1) % 3, va = (q.axis + 2) % 3;
        for (int dv = 0; dv < q.dv; ++dv)
            for (int du = 0; du < q.du; ++du) {
                glm::ivec3 p = q.o;
                p[ua] += du;
                p[va] += dv;
                if (!g.in(p) || faceAt(g, outside, p, q.axis, q.dir, cards) != q.material) {
                    ++errors;
                    note("quad covers a face that is not an exposed face of its material");
                    continue;
                }
                if (++hits[key(p, q.axis, q.dir)] == 2) {
                    ++errors;
                    note("unit face covered twice");
                }
            }
    }
    for (size_t i = 0; i < g.size(); ++i) {
        if (g.mat[i] == 0) continue;
        const glm::ivec3 p = g.pos(i);
        for (int axis = 0; axis < 3; ++axis)
            for (int dir : {-1, 1})
                if (faceAt(g, outside, p, axis, dir, cards) && !hits.count(key(p, axis, dir))) {
                    ++errors;
                    note("exposed face not covered");
                }
    }
    if (why) *why = os.str();
    return errors;
}

}  // namespace Phyxel::Core::VoxFound

namespace Phyxel::Core::VoxFound {
// F2.3 leaf sprigs (Mesher.h).
uint32_t sprigThreshold24(double sprigsPerM2, int voxelsPerM) {
    const double p = sprigsPerM2 / (double(voxelsPerM) * double(voxelsPerM));
    if (!(p > 0.0)) return 0;
    if (p >= 1.0) return 1u << 24;
    return uint32_t(std::lround(p * 16777216.0));
}

uint32_t sprigHash24(glm::ivec3 c) {
    uint32_t h = uint32_t(c.x) * 0x8DA6B343u ^ uint32_t(c.y) * 0xD8163841u ^ uint32_t(c.z) * 0xCB1AB31Fu;
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
    return h & 0xFFFFFFu;
}

void Neighbours::resize(glm::ivec3 d, bool s) {
    dim = d;
    for (int a = 0; a < 3; ++a)
        for (int k = 0; k < 2; ++k) {
            bits[2 * a + k].assign((planeBits(d, a) + 31) / 32, s ? ~0u : 0u);
            clearBits[2 * a + k].assign((planeBits(d, a) + 31) / 32, 0u);
        }
}

void Neighbours::setClear(int axis, int dir, int u, int v, bool c) {
    auto& plane = clearBits[2 * axis + (dir > 0)];
    if (plane.size() != bits[2 * axis + (dir > 0)].size()) plane.assign(bits[2 * axis + (dir > 0)].size(), 0u);
    const size_t b = size_t(u) + size_t(dim[(axis + 1) % 3]) * size_t(v);
    uint32_t& w = plane[b >> 5];
    w = c ? (w | (1u << (b & 31))) : (w & ~(1u << (b & 31)));
}

void Neighbours::set(int axis, int dir, int u, int v, bool s) {
    const size_t b = size_t(u) + size_t(dim[(axis + 1) % 3]) * size_t(v);
    uint32_t& w = bits[2 * axis + (dir > 0)][b >> 5];
    w = s ? (w | (1u << (b & 31))) : (w & ~(1u << (b & 31)));
}

VoxelGrid subGrid(const VoxelGrid& world, glm::ivec3 lo, glm::ivec3 size) {
    VoxelGrid g;
    g.resize(size);
    for (int x = 0; x < size.x; ++x)
        for (int y = 0; y < size.y; ++y)
            for (int z = 0; z < size.z; ++z) {
                const glm::ivec3 w = lo + glm::ivec3(x, y, z);
                if (!world.in(w)) continue;
                const size_t i = world.idx(w), j = g.idx({x, y, z});
                g.mat[j] = world.mat[i];
                g.part[j] = world.part[i];
                g.fixed[j] = world.fixed[i];
            }
    return g;
}

Neighbours neighboursOf(const VoxelGrid& world, glm::ivec3 lo, glm::ivec3 size, Outside beyond,
                        const CardSet* cards) {
    Neighbours n;
    n.resize(size, false);
    for (int a = 0; a < 3; ++a) {
        const int ua = (a + 1) % 3, va = (a + 2) % 3;
        for (int dir : {-1, 1})
            for (int v = 0; v < size[va]; ++v)
                for (int u = 0; u < size[ua]; ++u) {
                    glm::ivec3 w = lo;
                    w[a] += dir < 0 ? -1 : size[a];
                    w[ua] += u;
                    w[va] += v;
                    const bool s = world.in(w) ? world.mat[world.idx(w)] != 0 : beyond == Outside::Skip;
                    n.set(a, dir, u, v, s);
                    if (s && cards && world.in(w) && cards->isClear(world.mat[world.idx(w)])) n.setClear(a, dir, u, v, true);
                }
    }
    return n;
}

PackedFace packSprig(glm::ivec3 c, uint8_t material) {
    PackedFace f;
    f.a = uint32_t(c.x & 0xFF) | (uint32_t(c.y & 0xFF) << 8) | (uint32_t(c.z & 0xFF) << 16) | (3u << 24);
    f.b = uint32_t(material) << 16;
    return f;
}

std::vector<PackedFace> foliageSprigs(const VoxelGrid& g, Edges outside, const SprigParams& p) {
    return foliageSprigsBox(g, outside, p, glm::ivec3(0), g.dim - 1);   // F2.16: the same rule, over the grid
}
}  // namespace Phyxel::Core::VoxFound

namespace Phyxel::Core::VoxFound {

// ---- F2.10 occlusion (§4.6 item 4) ----------------------------------------------------------------------------
FaceConnect blockFaceConnectivity(const VoxelGrid& g, const CardSet& seeThrough) {
    // F2.16: kept as data (blockBlocking) so an edit redoes only its blocks before the flood
    return connectivityFromBlocking(g.dim / 8, blockBlocking(g, seeThrough));
}

std::vector<char> occlusionReach(const std::vector<OccChunk>& known, const std::vector<glm::ivec3>& candidates,
                                 glm::ivec3 cam, const std::function<bool(glm::ivec3)>& openSpace, size_t maxNodes,
                                 bool* budgetExceeded) {
    if (budgetExceeded) *budgetExceeded = false;
    auto key = [](glm::ivec3 c) -> int64_t {
        return (int64_t(c.x + (1 << 20)) << 42) | (int64_t(c.y + (1 << 20)) << 21) | int64_t(c.z + (1 << 20));
    };
    std::unordered_map<int64_t, const FaceConnect*> conn;
    for (const auto& k : known) conn[key(k.coord)] = &k.connect;
    std::unordered_map<int64_t, size_t> cand;
    for (size_t i = 0; i < candidates.size(); ++i) cand[key(candidates[i])] = i;
    std::vector<char> reached(candidates.size(), 0);
    size_t left = candidates.size();
    std::unordered_map<int64_t, char> seen;
    std::vector<std::pair<glm::ivec3, int>> q;   // BFS queue (index walk)
    const glm::ivec3 dir[6] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
    seen[key(cam)] = 1;
    q.push_back({cam, -1});
    if (auto it = cand.find(key(cam)); it != cand.end()) {
        reached[it->second] = 1;
        --left;
    }
    for (size_t head = 0; head < q.size() && left > 0; ++head) {
        if (seen.size() > maxNodes) {
            if (budgetExceeded) *budgetExceeded = true;
            return std::vector<char>(candidates.size(), 1);   // never a hole
        }
        const auto [c, entry] = q[head];
        const auto ci = conn.find(key(c));
        for (int d = 0; d < 6; ++d) {
            if (entry >= 0 && ci != conn.end() && !facesConnected(*ci->second, entry, d)) continue;
            const glm::ivec3 n = c + dir[d];
            const int64_t k = key(n);
            if (seen.count(k)) continue;
            const auto cit = cand.find(k);
            if (cit == cand.end() && !openSpace(n)) continue;
            seen[k] = 1;
            if (cit != cand.end()) {
                reached[cit->second] = 1;
                --left;
            }
            q.push_back({n, d ^ 1});
        }
    }
    return reached;
}

}  // namespace Phyxel::Core::VoxFound

namespace Phyxel::Core::VoxFound {

// ---- F2.11 face clusters (§4.6 item 5 + item 3's per-block part) ------------------------------------------------
void faceBox(PackedFace f, glm::ivec3& lo, glm::ivec3& hi) {
    const Quad q = unpackFace(f);
    lo = q.o;
    if (q.dir > 0) lo[q.axis] += 1;   // a + face sits on the voxel's far plane
    hi = lo;
    hi[(q.axis + 1) % 3] += q.du;
    hi[(q.axis + 2) % 3] += q.dv;
}

std::vector<FaceCluster> clusterFaces(std::vector<PackedFace>& faces, int clusterBlocks, bool octantOrder) {
    const int cellV = std::max(1, clusterBlocks) * 8;
    auto cellKey = [&](PackedFace f) {
        const glm::ivec3 c = unpackFace(f).o / cellV;   // chunk-local, non-negative
        return (uint32_t(c.x) << 16) | (uint32_t(c.y) << 8) | uint32_t(c.z);
    };
    // F2.15: (cell, direction), so a cell's direction groups are contiguous sub-ranges (splitByDirection)
    std::stable_sort(faces.begin(), faces.end(), [&](PackedFace a, PackedFace b) {
        const uint32_t ka = cellKey(a), kb = cellKey(b);
        if (ka != kb) return ka < kb;
        const int da = faceDirIndex(a), db = faceDirIndex(b);
        return octantOrder ? faceDirRank(da) < faceDirRank(db) : da < db;   // F2.15f
    });
    std::vector<FaceCluster> out;
    for (size_t i = 0; i < faces.size();) {
        const uint32_t k = cellKey(faces[i]);
        FaceCluster c;
        c.first = uint32_t(i);
        c.lo = glm::ivec3(std::numeric_limits<int>::max());
        c.hi = glm::ivec3(std::numeric_limits<int>::min());
        for (; i < faces.size() && cellKey(faces[i]) == k; ++i) {
            glm::ivec3 lo, hi;
            faceBox(faces[i], lo, hi);
            c.lo = glm::min(c.lo, lo);
            c.hi = glm::max(c.hi, hi);
        }
        c.count = uint32_t(i) - c.first;
        out.push_back(c);
    }
    return out;
}

int faceDirIndex(PackedFace f) {
    const Quad q = unpackFace(f);
    return int(q.axis) * 2 + (q.dir > 0 ? 1 : 0);
}
std::vector<FaceCluster> splitByDirection(const std::vector<PackedFace>& faces, const std::vector<FaceCluster>& cells) {
    std::vector<FaceCluster> out;
    for (const FaceCluster& c : cells)
        for (uint32_t i = c.first; i < c.first + c.count;) {
            FaceCluster g;
            g.first = i;
            g.dir = faceDirIndex(faces[i]);
            g.lo = glm::ivec3(std::numeric_limits<int>::max());
            g.hi = glm::ivec3(std::numeric_limits<int>::min());
            for (; i < c.first + c.count && faceDirIndex(faces[i]) == g.dir; ++i) {
                glm::ivec3 lo, hi;
                faceBox(faces[i], lo, hi);
                g.lo = glm::min(g.lo, lo);
                g.hi = glm::max(g.hi, hi);
            }
            g.count = i - g.first;
            out.push_back(g);
        }
    return out;
}
bool faceGroupMayFaceCamera(int dirIndex, glm::vec3 lo, glm::vec3 hi, glm::vec3 cam, float margin) {
    if (dirIndex < 0) return true;
    dirIndex %= kAwayGroup;   // F2.15d-2: an away group is its direction for the camera
    const int axis = dirIndex / 2;
    return (dirIndex & 1) ? cam[axis] > lo[axis] - margin : cam[axis] < hi[axis] + margin;
}

bool faceDirFacesLight(int dirIndex, glm::vec3 toLight) {
    if (dirIndex < 0) return true;
    if (dirIndex >= kAwayGroup) return !faceDirFacesLight(dirIndex - kAwayGroup, toLight);   // F2.15d-2
    const float n = (dirIndex & 1) ? toLight[dirIndex / 2] : -toLight[dirIndex / 2];   // dot(normal, toLight)
    return n > -1e-4f;
}

std::vector<DrawRun> mergeVisibleRuns(const std::vector<uint32_t>& first, const std::vector<uint32_t>& count,
                                      const std::vector<uint32_t>& key, const std::vector<uint8_t>& visible,
                                      uint32_t solid, uint32_t block) {
    std::vector<DrawRun> out;
    const size_t n = std::min({first.size(), count.size(), key.size(), visible.size()});
    block = std::max(1u, block);
    for (size_t i = 0; i < n; ++i) {
        if (!visible[i]) continue;
        const bool joins = i > 0 && i % block != 0 && visible[i - 1] && key[i - 1] == key[i] &&
                           ((i - 1) < solid) == (i < solid) && first[i - 1] + count[i - 1] == first[i];
        if (joins && !out.empty())
            out.back().count += count[i];
        else
            out.push_back({first[i], count[i], uint32_t(i)});
    }
    return out;
}

bool boxInFrustum(const glm::vec4 planes[6], glm::vec3 lo, glm::vec3 hi) {
    for (int i = 0; i < 6; ++i) {
        const glm::vec3 n(planes[i]);
        const glm::vec3 p(n.x >= 0.0f ? hi.x : lo.x, n.y >= 0.0f ? hi.y : lo.y, n.z >= 0.0f ? hi.z : lo.z);
        if (glm::dot(n, p) + planes[i].w < 0.0f) return false;
    }
    return true;
}

void clipPlanesZeroToOne(const glm::mat4& clip, glm::vec3 frameOrigin, glm::vec4 planes[6]) {
    // p_cameraRelative = p_frame + frameOrigin, so the matrix in the frame is clip * translate(frameOrigin); its rows
    // give the planes (Gribb-Hartmann, zero-to-one depth: near = row 2, far = row 3 - row 2).
    const glm::dmat4 m = glm::dmat4(clip) * glm::dmat4(1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0,
                                                       double(frameOrigin.x), double(frameOrigin.y),
                                                       double(frameOrigin.z), 1.0);
    auto row = [&](int i) { return glm::dvec4(m[0][i], m[1][i], m[2][i], m[3][i]); };
    const glm::dvec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
    const glm::dvec4 p[6] = {r3 + r0, r3 - r0, r3 + r1, r3 - r1, r2, r3 - r2};
    for (int i = 0; i < 6; ++i) {
        const double len = glm::length(glm::dvec3(p[i]));
        planes[i] = len > 0.0 ? glm::vec4(p[i] / len) : glm::vec4(0, 0, 0, 1);
    }
}

LodGrid lodBase(const VoxelGrid& g) {
    LodGrid l;
    l.grid = g;
    l.count.resize(g.size());
    l.surf.assign(g.size(), 0u);
    l.surfMat.assign(g.size(), 0);
    l.volMat = g.mat;
    static const glm::ivec3 kDirs[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (size_t i = 0; i < g.size(); ++i) {
        l.count[i] = g.mat[i] != 0 ? 1u : 0u;
        if (!g.mat[i]) continue;
        const glm::ivec3 p = g.pos(i);
        for (const glm::ivec3& d : kDirs) {
            const glm::ivec3 q = p + d;
            if (g.in(q) && g.mat[g.idx(q)] == 0) {   // outside the grid is not air (unknown)
                l.surf[i] = 1u;
                l.surfMat[i] = g.mat[i];
                break;
            }
        }
    }
    return l;
}

LodGrid lodDown(const LodGrid& fine) {
    const glm::ivec3 d = fine.grid.dim;
    if (d.x % 2 || d.y % 2 || d.z % 2) throw std::invalid_argument("lodDown: every side must be even");
    LodGrid l;
    l.level = fine.level + 1;
    l.grid.resize(d / 2);
    l.count.assign(l.grid.size(), 0u);
    l.surf.assign(l.grid.size(), 0u);
    l.surfMat.assign(l.grid.size(), 0);
    l.volMat.assign(l.grid.size(), 0);
    const uint64_t half = (uint64_t(1) << (3 * l.level)) / 2;   // half of the 8^k level-0 voxels covered
    // a small vote: (material, weight) pairs, winner = largest weight, ties to the lowest material index
    struct Vote {
        uint8_t mats[8];
        uint32_t w[8];
        int n = 0;
        void add(uint8_t m, uint32_t weight) {
            int j = 0;
            while (j < n && mats[j] != m) ++j;
            if (j == n) {
                mats[n] = m;
                w[n++] = 0;
            }
            w[j] += weight;
        }
        uint8_t winner() const {
            int best = 0;
            for (int j = 1; j < n; ++j)
                if (w[j] > w[best] || (w[j] == w[best] && mats[j] < mats[best])) best = j;
            return n ? mats[best] : uint8_t(0);
        }
    };
    for (int x = 0; x < l.grid.dim.x; ++x)
        for (int y = 0; y < l.grid.dim.y; ++y)
            for (int z = 0; z < l.grid.dim.z; ++z) {
                uint32_t total = 0, surfTotal = 0;
                Vote vol, surf;
                for (int c = 0; c < 8; ++c) {
                    const glm::ivec3 p(2 * x + (c & 1), 2 * y + ((c >> 1) & 1), 2 * z + ((c >> 2) & 1));
                    const size_t i = fine.grid.idx(p);
                    total += fine.count[i];
                    // every child votes its surface (air children too: the surface of the coarse cell runs through
                    // them); only solid children vote by volume (one always exists when the cell is solid)
                    if (fine.surf[i]) {
                        surf.add(fine.surfMat[i], fine.surf[i]);
                        surfTotal += fine.surf[i];
                    }
                    if (fine.grid.mat[i]) vol.add(fine.volMat[i], fine.count[i]);
                }
                const size_t o = l.grid.idx({x, y, z});
                l.count[o] = total;
                l.surf[o] = surfTotal;
                l.surfMat[o] = surf.winner();
                if (uint64_t(total) < half || vol.n == 0) continue;
                l.volMat[o] = vol.winner();
                l.grid.mat[o] = surfTotal ? surf.winner() : l.volMat[o];
            }
    // a solid cell that covers no surface voxel: the surface lies in its air face neighbours (same level)
    static const glm::ivec3 kDirs[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (size_t o = 0; o < l.grid.size(); ++o) {
        if (!l.grid.mat[o] || l.surf[o]) continue;
        const glm::ivec3 p = l.grid.pos(o);
        Vote nb;
        for (const glm::ivec3& d : kDirs) {
            const glm::ivec3 q = p + d;
            if (!l.grid.in(q)) continue;
            const size_t j = l.grid.idx(q);
            if (l.count[j] * 2 >= (uint32_t(1) << (3 * l.level))) continue;   // a solid neighbour (by count)
            if (l.surf[j]) nb.add(l.surfMat[j], l.surf[j]);
        }
        if (nb.n) l.grid.mat[o] = nb.winner();
    }
    return l;
}

PackedFace lodScaleFace(PackedFace f, int level) {
    Quad q = unpackFace(f);
    const int s = 1 << level;
    q.o *= s;
    if (q.dir > 0) q.o[q.axis] += s - 1;   // a +side face lies past its voxel: the last level-0 voxel of the cell
    q.du = uint16_t(q.du * s);
    q.dv = uint16_t(q.dv * s);
    return packFace(q);
}

LodGrid lodEmpty(glm::ivec3 dim, int level) {
    LodGrid l;
    l.level = level;
    l.grid.resize(dim);
    l.count.assign(l.grid.size(), 0u);
    l.surf.assign(l.grid.size(), 0u);
    l.surfMat.assign(l.grid.size(), 0);
    l.volMat.assign(l.grid.size(), 0);
    return l;
}
LodGrid lodCut(const LodGrid& g, glm::ivec3 lo, glm::ivec3 size) {
    LodGrid l = lodEmpty(size, g.level);
    lodPaste(l, g, -lo);   // the part of g that lands inside [0, size)
    return l;
}
void lodPaste(LodGrid& region, const LodGrid& part, glm::ivec3 at) {
    const glm::ivec3 d = part.grid.dim;
    for (int x = 0; x < d.x; ++x)
        for (int y = 0; y < d.y; ++y)
            for (int z = 0; z < d.z; ++z) {
                const glm::ivec3 q = at + glm::ivec3(x, y, z);
                if (!region.grid.in(q)) continue;
                const size_t i = part.grid.idx({x, y, z}), j = region.grid.idx(q);
                region.grid.mat[j] = part.grid.mat[i];
                region.grid.part[j] = part.grid.part[i];
                region.grid.fixed[j] = part.grid.fixed[i];
                region.count[j] = part.count[i];
                region.surf[j] = part.surf[i];
                region.surfMat[j] = part.surfMat[i];
                region.volMat[j] = part.volMat[i];
            }
}

LodTopStats lodTopStats(const LodGrid& ref, const LodGrid& coarse, const CardSet& cards) {
    // per column: top (level-0 voxels) + material of the highest solid non-card cell
    struct Top {
        std::vector<int> h;
        std::vector<uint8_t> m;
    };
    auto tops = [&](const LodGrid& l) {
        const glm::ivec3 d = l.grid.dim;
        const int cell = 1 << l.level;
        Top t;
        t.h.assign(size_t(d.x) * size_t(d.z), 0);
        t.m.assign(t.h.size(), 0);
        for (int x = 0; x < d.x; ++x)
            for (int z = 0; z < d.z; ++z)
                for (int y = d.y - 1; y >= 0; --y) {
                    const uint8_t m = l.grid.mat[l.grid.idx({x, y, z})];
                    if (!m || cards.has(m)) continue;
                    t.h[size_t(x) * size_t(d.z) + size_t(z)] = (y + 1) * cell;
                    t.m[size_t(x) * size_t(d.z) + size_t(z)] = m;
                    break;
                }
        return t;
    };
    auto shares = [](const Top& t) {
        std::vector<double> s(256, 0.0);
        double n = 0.0;
        for (uint8_t m : t.m)
            if (m) {
                s[m] += 1.0;
                n += 1.0;
            }
        if (n > 0.0)
            for (double& v : s) v /= n;
        return s;
    };
    const Top r = tops(ref), c = tops(coarse);
    LodTopStats st;
    st.shareRef = shares(r);
    st.share = shares(c);
    const int ratio = 1 << (coarse.level - ref.level), cell = 1 << coarse.level;
    const glm::ivec3 cd = coarse.grid.dim, rd = ref.grid.dim;
    double sum = 0.0;
    size_t n = 0;
    for (int x = 0; x < cd.x; ++x)
        for (int z = 0; z < cd.z; ++z) {
            double mean = 0.0;
            int k = 0;
            for (int i = 0; i < ratio; ++i)
                for (int j = 0; j < ratio; ++j) {
                    const int rx = x * ratio + i, rz = z * ratio + j;
                    if (rx >= rd.x || rz >= rd.z) continue;
                    mean += r.h[size_t(rx) * size_t(rd.z) + size_t(rz)];
                    ++k;
                }
            if (!k) continue;
            const double e = std::abs(double(c.h[size_t(x) * size_t(cd.z) + size_t(z)]) - mean / k) / cell;
            sum += e;
            st.maxErrCells = std::max(st.maxErrCells, e);
            ++n;
        }
    st.meanErrCells = n ? sum / double(n) : 0.0;
    st.shareHidden.assign(256, 0.0);
    double withTop = 0.0;
    for (int x = 0; x < cd.x; ++x)
        for (int z = 0; z < cd.z; ++z) {
            const size_t ci = size_t(x) * size_t(cd.z) + size_t(z);
            if (!c.m[ci]) continue;
            withTop += 1.0;
            const int y = c.h[ci] / cell;   // the cell right above the top cell
            if (y < cd.y && cards.has(coarse.grid.mat[coarse.grid.idx({x, y, z})])) {
                st.hiddenTop += 1.0;
                st.shareHidden[c.m[ci]] += 1.0;
            }
        }
    if (st.hiddenTop > 0.0)
        for (double& v : st.shareHidden) v /= st.hiddenTop;
    if (withTop > 0.0) st.hiddenTop /= withTop;
    return st;
}

void lodSplitCards(const VoxelGrid& g, const CardSet& cards, VoxelGrid& solid, VoxelGrid& leaves) {
    solid = g;
    leaves.resize(g.dim);
    for (size_t i = 0; i < g.size(); ++i)
        if (g.mat[i] && cards.has(g.mat[i])) {
            leaves.mat[i] = g.mat[i];
            leaves.part[i] = g.part[i];
            leaves.fixed[i] = g.fixed[i];
            solid.mat[i] = 0;
            solid.part[i] = 0;
            solid.fixed[i] = 0;
        }
}

VoxelGrid casterSeamGrid(const VoxelGrid& g, const CardSet& cards, int maxDepth, uint8_t hide) {
    VoxelGrid out;
    out.resize(g.dim);
    const size_t n = g.size();
    // leaf depth by breadth-first search from the air voxels through card voxels (0 = not reached / not a leaf)
    std::vector<int> depth(n, 0);
    std::vector<size_t> frontier, next;
    const bool every = maxDepth < 0;
    if (!every) {
        static const glm::ivec3 n6[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        for (size_t i = 0; i < n; ++i)
            if (g.mat[i] == 0) frontier.push_back(i);
        for (int d = 1; d <= maxDepth && !frontier.empty(); ++d) {
            next.clear();
            for (size_t i : frontier) {
                const glm::ivec3 p = g.pos(i);
                for (const glm::ivec3& s : n6) {
                    const glm::ivec3 q = p + s;
                    if (!g.in(q)) continue;
                    const size_t j = g.idx(q);
                    if (depth[j] || !g.mat[j] || !cards.has(g.mat[j])) continue;
                    depth[j] = d;
                    next.push_back(j);
                }
            }
            frontier.swap(next);
        }
    }
    for (size_t i = 0; i < n; ++i) {
        const uint8_t m = g.mat[i];
        if (m && !cards.has(m)) {   // solid: kept
            out.mat[i] = m;
            out.part[i] = g.part[i];
            out.fixed[i] = g.fixed[i];
        } else if (m && (every || depth[i])) {
            out.mat[i] = 0;   // a shallow leaf: air, so the solid face behind it is meshed
        } else {
            out.mat[i] = hide;   // air or a deeper leaf: hides, emits nothing
        }
    }
    return out;
}

VoxelGrid casterExitGrid(const VoxelGrid& g, const CardSet& cards, int reach, uint8_t hide) {
    static const glm::ivec3 n6[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    const size_t n = g.size();
    auto solidAt = [&](size_t i) { return g.mat[i] != 0 && !cards.has(g.mat[i]); };
    // breadth-first through solid from the solids with a card neighbour (distance 0)
    std::vector<int> dist(n, -1);
    std::vector<size_t> frontier, next;
    for (size_t i = 0; i < n; ++i) {
        if (!solidAt(i)) continue;
        const glm::ivec3 p = g.pos(i);
        for (const glm::ivec3& s : n6) {
            const glm::ivec3 q = p + s;
            if (g.in(q) && g.mat[g.idx(q)] && cards.has(g.mat[g.idx(q)])) {
                dist[i] = 0;
                frontier.push_back(i);
                break;
            }
        }
    }
    for (int d = 1; (reach < 0 || d <= reach) && !frontier.empty(); ++d) {
        next.clear();
        for (size_t i : frontier) {
            const glm::ivec3 p = g.pos(i);
            for (const glm::ivec3& s : n6) {
                const glm::ivec3 q = p + s;
                if (!g.in(q)) continue;
                const size_t j = g.idx(q);
                if (dist[j] >= 0 || !solidAt(j)) continue;
                dist[j] = d;
                next.push_back(j);
            }
        }
        frontier.swap(next);
    }
    VoxelGrid out;
    out.resize(g.dim);
    for (size_t i = 0; i < n; ++i) {
        if (dist[i] >= 0) {
            out.mat[i] = g.mat[i];
            out.part[i] = g.part[i];
            out.fixed[i] = g.fixed[i];
        } else if (g.mat[i]) {
            out.mat[i] = hide;   // a far solid or a leaf: hides, emits nothing
        }
    }
    return out;
}

VoxelGrid lodLeafMass(const LodGrid& leaves, double fill, uint8_t fallback) {
    VoxelGrid g;
    g.resize(leaves.grid.dim);
    const double need = std::max(1.0, fill * double(uint64_t(1) << (3 * leaves.level)));
    for (size_t i = 0; i < g.size(); ++i) {
        if (double(leaves.count[i]) < need) continue;
        const uint8_t m = leaves.surfMat[i] ? leaves.surfMat[i] : leaves.volMat[i] ? leaves.volMat[i] : fallback;
        g.mat[i] = m;
    }
    return g;
}

bool casterBoxKept(glm::vec3 lo, glm::vec3 hi, glm::vec3 cullCenter, float reach, const glm::vec4 lightPlanes[6]) {
    if (glm::length((lo + hi) * 0.5f - cullCenter) > reach) return false;   // today's distance test
    return boxInFrustum(lightPlanes, lo, hi);                                // the light frustum
}

// ---- F2.14: the level by size on screen (the shaders mirror these: voxel.frag phxVoxFoundLodKeeps, voxfound_cull) ----
int lodScaleCode(float pxPerVoxel, float focalPx) {
    if (!(pxPerVoxel > 0.0f) || !(focalPx > 0.0f)) return 0;
    const float c = std::log2(8.0f * pxPerVoxel / focalPx);
    return std::clamp(int(std::lround((c + 16.0f) * 8.0f)), 0, 255);
}
float lodScaleFromCode(int code) { return float(code) / 8.0f - 16.0f; }
float lodLevelAt(float distance, float c) { return std::log2(std::max(distance, 1e-3f)) + c; }
float lodBayer(int px, int py) {
    static constexpr int kBayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
    return (float(kBayer[py & 3][px & 3]) + 0.5f) / 16.0f;
}
int lodPickLevel(float L, float u, int maxLevel) {
    return std::clamp(int(std::floor(L + kLodBand * (u - 0.5f))), 0, maxLevel);
}
bool lodKeeps(int level, float L, float u, int maxLevel, float slack) {
    return lodPickLevel(L - slack, u, maxLevel) <= level && level <= lodPickLevel(L + slack, u, maxLevel);
}
bool lodSprigKeeps(float L, float u, int handoff) { return lodPickLevel(L - kLodSlack, u, 7) < handoff; }
// pick(L - 1/4, u) < h  <=>  L - 1/4 + (u - 1/2)/2 < h  <=>  u < 2(h + 1/2 - L)
float lodSprigFade(float L, int handoff) { return 2.0f * (float(handoff) + 0.5f - L); }
bool lodCasterKeeps(int level, float L, float u, int maxLevel, float floor) {
    return lodKeeps(level, std::max(L - kLodCasterLag, floor), u, maxLevel);
}
float lodTexelFloor(float texelMetres) { return std::log2(std::max(texelMetres, 1e-6f) * 8.0f); }
glm::ivec2 lodLevelsForBox(glm::vec3 lo, glm::vec3 hi, glm::vec3 cam, float c, int maxLevel, float floor) {
    const glm::vec3 nearest = glm::clamp(cam, lo, hi);
    const glm::vec3 farthest(std::abs(cam.x - lo.x) > std::abs(cam.x - hi.x) ? lo.x : hi.x,
                             std::abs(cam.y - lo.y) > std::abs(cam.y - hi.y) ? lo.y : hi.y,
                             std::abs(cam.z - lo.z) > std::abs(cam.z - hi.z) ? lo.z : hi.z);
    // the extreme Bayer values are 0.5/16 and 15.5/16
    return {lodPickLevel(std::max(lodLevelAt(glm::length(nearest - cam), c), floor) - kLodSlack, 0.5f / 16.0f, maxLevel),
            lodPickLevel(std::max(lodLevelAt(glm::length(farthest - cam), c), floor) + kLodSlack, 15.5f / 16.0f, maxLevel)};
}
uint32_t lodFlagBits(int level, int maxLevel, int code) {
    return (uint32_t(level) & 0xFu) << 16 | (uint32_t(maxLevel) & 0x7u) << 20 | 1u << 23 | (uint32_t(code) & 0xFFu) << 24;
}

// ---- F2.16 (§4.6 item 9): incremental rebuild -------------------------------------------------------------------
size_t RunsPlan::rows() const {
    size_t n = 0;
    for (const SliceJob& j : slices) n += size_t(std::max(0, j.w1 - j.w0 + 1));
    return n;
}

namespace {
RunsPlan planRunsDeltaImpl(glm::ivec3 dim, const std::vector<PackedFace>& before, const std::vector<FaceCluster>* groups,
                           glm::ivec3 lo, glm::ivec3 hi) {
    RunsPlan plan;
    // Per axis and direction: the slices that can change, the (u, v) footprint, and per slice the removed faces' rows.
    struct Side {
        int sLo = 0, sHi = -1;
        int uLo = 0, uHi = -1, vLo = 0, vHi = -1;
        std::vector<int> rowLo, rowHi;
    };
    Side side[3][2];
    for (int a = 0; a < 3; ++a) {
        const int ua = (a + 1) % 3, va = (a + 2) % 3;
        for (int db = 0; db < 2; ++db) {
            Side& sd = side[a][db];
            // a dir + face in slice s reads layers s and s + 1; a dir - face layers s and s - 1
            sd.sLo = std::max(0, db ? lo[a] - 1 : lo[a]);
            sd.sHi = std::min(dim[a] - 1, db ? hi[a] : hi[a] + 1);
            sd.uLo = std::max(0, lo[ua]);
            sd.uHi = std::min(dim[ua] - 1, hi[ua]);
            sd.vLo = std::max(0, lo[va]);
            sd.vHi = std::min(dim[va] - 1, hi[va]);
            if (sd.uLo > sd.uHi || sd.vLo > sd.vHi) sd.sHi = sd.sLo - 1;   // the footprint misses this grid
            const int n = std::max(0, sd.sHi - sd.sLo + 1);
            sd.rowLo.assign(size_t(n), std::numeric_limits<int>::max());
            sd.rowHi.assign(size_t(n), std::numeric_limits<int>::min());
        }
    }
    // faces whose rectangle grown by one meets the footprint of a slice that can change
    auto examine = [&](size_t i) {
        const PackedFace f = before[i];
        const int a = int((f.a >> 24) & 3u), db = int((f.a >> 26) & 1u);
        Side& sd = side[a][db];
        const glm::ivec3 o(int(f.a & 0xFF), int((f.a >> 8) & 0xFF), int((f.a >> 16) & 0xFF));
        const int s = o[a];
        if (s < sd.sLo || s > sd.sHi) return;
        const int u = o[(a + 1) % 3], v = o[(a + 2) % 3];
        const int du = int(f.b & 0xFF) + 1, dv = int((f.b >> 8) & 0xFF) + 1;
        if (u - 1 > sd.uHi || u + du < sd.uLo || v - 1 > sd.vHi || v + dv < sd.vLo) return;
        plan.removed.push_back(uint32_t(i));
        const size_t k = size_t(s - sd.sLo);
        sd.rowLo[k] = std::min(sd.rowLo[k], v);
        sd.rowHi[k] = std::max(sd.rowHi[k], v + dv - 1);
    };
    if (!groups) {
        for (size_t i = 0; i < before.size(); ++i) examine(i);
        plan.examined = before.size();
    } else {
        // F3.5a: a face that qualifies has its plane within [lo - 1, hi + 2] along its axis and its rectangle within
        // one of the footprint across it, so its box (and its group's, the union of its faces' boxes) meets
        // [lo - 2, hi + 2]
        const glm::ivec3 glo = lo - 2, ghi = hi + 2;
        for (const FaceCluster& g : *groups) {
            if (g.hi.x < glo.x || g.hi.y < glo.y || g.hi.z < glo.z || g.lo.x > ghi.x || g.lo.y > ghi.y || g.lo.z > ghi.z)
                continue;
            for (uint32_t i = g.first; i < g.first + g.count; ++i) examine(i);
            plan.examined += g.count;
        }
        std::sort(plan.removed.begin(), plan.removed.end());   // groups come in face order; kept ascending regardless
    }
    // the rows to re-mesh: the footprint rows + the removed faces' rows (see the header)
    for (int a = 0; a < 3; ++a)
        for (int db = 0; db < 2; ++db) {
            const Side& sd = side[a][db];
            for (int s = sd.sLo; s <= sd.sHi; ++s) {
                const size_t k = size_t(s - sd.sLo);
                SliceJob j;
                j.axis = a;
                j.dir = db ? 1 : -1;
                j.s = s;
                j.w0 = std::min(sd.vLo, sd.rowLo[k]);
                j.w1 = std::max(sd.vHi, sd.rowHi[k]);
                j.uLo = sd.uLo;
                j.uHi = sd.uHi;
                j.vLo = sd.vLo;
                j.vHi = sd.vHi;
                plan.slices.push_back(j);
            }
        }
    return plan;
}
}  // namespace

RunsPlan planRunsDelta(glm::ivec3 dim, const std::vector<PackedFace>& before, glm::ivec3 lo, glm::ivec3 hi) {
    return planRunsDeltaImpl(dim, before, nullptr, lo, hi);
}

RunsPlan planRunsDelta(glm::ivec3 dim, const std::vector<PackedFace>& before, const std::vector<FaceCluster>& groups,
                       glm::ivec3 lo, glm::ivec3 hi) {
    return planRunsDeltaImpl(dim, before, &groups, lo, hi);
}

RunsDelta meshRunsPlan(const VoxelGrid& g, Edges outside, const RunsPlan& plan, const CardSet* cards) {
    return runsPlanLazy(GridRead{g}, outside, plan, cards);
}
RunsDelta meshRunsPlan(const BlockStore& st, Edges outside, const RunsPlan& plan, const CardSet* cards) {
    return runsPlanLazy(StoreRead{st}, outside, plan, cards);
}

RunsDelta meshRunsPlanWholeRows(const VoxelGrid& g, Edges outside, const RunsPlan& plan, const CardSet* cards) {
    return runsPlanWhole(GridRead{g}, outside, plan, cards);
}
RunsDelta meshRunsPlanWholeRows(const BlockStore& st, Edges outside, const RunsPlan& plan, const CardSet* cards) {
    return runsPlanWhole(StoreRead{st}, outside, plan, cards);
}

RunsDelta meshRunsDelta(const VoxelGrid& g, Edges outside, const std::vector<PackedFace>& before, glm::ivec3 lo,
                        glm::ivec3 hi, const CardSet* cards) {
    return meshRunsPlan(g, outside, planRunsDelta(g.dim, before, lo, hi), cards);
}

std::vector<PackedFace> foliageSprigsBox(const VoxelGrid& g, Edges outside, const SprigParams& p, glm::ivec3 lo,
                                         glm::ivec3 hi) {
    return sprigsBoxR(GridRead{g}, outside, p, lo, hi);
}

size_t updateSprigs(std::vector<PackedFace>& sprigs, const VoxelGrid& g, Edges outside, const SprigParams& p,
                    glm::ivec3 lo, glm::ivec3 hi) {
    return updateSprigsR(sprigs, GridRead{g}, outside, p, lo, hi);
}
size_t updateSprigs(std::vector<PackedFace>& sprigs, const BlockStore& st, Edges outside, const SprigParams& p,
                    glm::ivec3 lo, glm::ivec3 hi, bool* changed) {
    return updateSprigsR(sprigs, StoreRead{st}, outside, p, lo, hi, changed);
}


std::vector<uint8_t> blockBlocking(const VoxelGrid& g, const CardSet& seeThrough) {
    std::vector<uint8_t> out;
    out.assign(size_t(g.dim.x / 8) * size_t(g.dim.y / 8) * size_t(g.dim.z / 8), 0);
    updateBlockBlocking(g, seeThrough, glm::ivec3(0), g.dim - 1, out);
    return out;
}

bool updateBlockBlocking(const VoxelGrid& g, const CardSet& seeThrough, glm::ivec3 lo, glm::ivec3 hi,
                         std::vector<uint8_t>& blocking) {
    return updateBlockingR(GridRead{g}, seeThrough, lo, hi, blocking);
}
bool updateBlockBlocking(const BlockStore& st, const CardSet& seeThrough, glm::ivec3 lo, glm::ivec3 hi,
                         std::vector<uint8_t>& blocking, std::vector<uint32_t>* flipped) {
    return updateBlockingR(StoreRead{st}, seeThrough, lo, hi, blocking, flipped);
}
std::vector<uint8_t> blockBlocking(const BlockStore& st, const CardSet& seeThrough) {
    std::vector<uint8_t> out;
    const glm::ivec3 d = st.dim();
    out.assign(size_t(d.x / 8) * size_t(d.y / 8) * size_t(d.z / 8), 0);
    updateBlockBlocking(st, seeThrough, glm::ivec3(0), d - 1, out);
    return out;
}

// F2.10's flood over the blocking bits, written with flat block indices and buffers kept between calls (F3.1). Measured
// live: ~0.16-0.19 ms per flood on a 32^3-block chunk in every form tried (this one, the stack of ivec3 before it, and a
// bit-row flood with a log-step fill, 0.184 vs 0.185 ms in one session). F3.1's saving is SKIPPING it when no blocking
// bit changed (RegionEdit::apply); a cheaper per-flood cost needs an incremental connectivity (owed).
FaceConnect connectivityFromBlocking(glm::ivec3 nb, const std::vector<uint8_t>& blocking) {
    FaceConnect out{0, 0, 0, 0, 0, 0};
    const int total = nb.x * nb.y * nb.z;
    if (total <= 0) return out;
    thread_local std::vector<uint8_t> seen;
    thread_local std::vector<int32_t> stack;
    seen.assign(size_t(total), 0);
    stack.clear();
    stack.reserve(size_t(total));
    const int sy = nb.z, sx = nb.z * nb.y;
    for (int i = 0; i < total; ++i) {
        if (blocking[size_t(i)] || seen[size_t(i)]) continue;
        uint8_t faces = 0;
        seen[size_t(i)] = 1;
        stack.push_back(i);
        while (!stack.empty()) {
            const int c = stack.back();
            stack.pop_back();
            const int x = c / sx, r = c - x * sx, y = r / sy, z = r - y * sy;
            if (x == 0) faces |= 1u << 0;
            if (x == nb.x - 1) faces |= 1u << 1;
            if (y == 0) faces |= 1u << 2;
            if (y == nb.y - 1) faces |= 1u << 3;
            if (z == 0) faces |= 1u << 4;
            if (z == nb.z - 1) faces |= 1u << 5;
            auto visit = [&](int k) {
                if (blocking[size_t(k)] || seen[size_t(k)]) return;
                seen[size_t(k)] = 1;
                stack.push_back(k);
            };
            if (x > 0) visit(c - sx);
            if (x + 1 < nb.x) visit(c + sx);
            if (y > 0) visit(c - sy);
            if (y + 1 < nb.y) visit(c + sy);
            if (z > 0) visit(c - 1);
            if (z + 1 < nb.z) visit(c + 1);
        }
        for (int f = 0; f < 6; ++f)
            if (faces & (1u << f)) out[size_t(f)] |= faces;
    }
    return out;
}

uint8_t BlockConnectivity::faceBits(int i) const {
    const int sy = m_nb.z, sx = m_nb.z * m_nb.y;
    const int x = i / sx, r = i - x * sx, y = r / sy, z = r - y * sy;
    return uint8_t((x == 0 ? 1u : 0u) | (x == m_nb.x - 1 ? 2u : 0u) | (y == 0 ? 4u : 0u) | (y == m_nb.y - 1 ? 8u : 0u) |
                   (z == 0 ? 16u : 0u) | (z == m_nb.z - 1 ? 32u : 0u));
}
int32_t BlockConnectivity::root(int32_t l) const {
    while (m_parent[size_t(l)] != l) {
        m_parent[size_t(l)] = m_parent[size_t(m_parent[size_t(l)])];   // path halving
        l = m_parent[size_t(l)];
    }
    return l;
}
void BlockConnectivity::rebuild(glm::ivec3 nb, const std::vector<uint8_t>& blocking) {
    // the flood of connectivityFromBlocking, keeping each component's label and face mask
    m_nb = nb;
    ++m_floods;
    const int total = nb.x * nb.y * nb.z;
    m_label.assign(size_t(std::max(0, total)), -1);
    m_parent.clear();
    m_mask.clear();
    std::fill(std::begin(m_count), std::end(m_count), 0u);
    thread_local std::vector<int32_t> stack;
    const int sy = nb.z, sx = nb.z * nb.y;
    for (int i = 0; i < total; ++i) {
        if (blocking[size_t(i)] || m_label[size_t(i)] >= 0) continue;
        const int32_t id = int32_t(m_parent.size());
        uint8_t faces = 0;
        m_label[size_t(i)] = id;
        stack.clear();
        stack.push_back(i);
        while (!stack.empty()) {
            const int c = stack.back();
            stack.pop_back();
            faces |= faceBits(c);
            const int x = c / sx, r = c - x * sx, y = r / sy, z = r - y * sy;
            auto visit = [&](int k) {
                if (blocking[size_t(k)] || m_label[size_t(k)] >= 0) return;
                m_label[size_t(k)] = id;
                stack.push_back(k);
            };
            if (x > 0) visit(c - sx);
            if (x + 1 < nb.x) visit(c + sx);
            if (y > 0) visit(c - sy);
            if (y + 1 < nb.y) visit(c + sy);
            if (z > 0) visit(c - 1);
            if (z + 1 < nb.z) visit(c + 1);
        }
        m_parent.push_back(id);
        m_mask.push_back(faces);
        ++m_count[faces];
    }
}
void BlockConnectivity::openBlock(int i) {
    // a new root; every neighbour component joins it (opening a cell can only merge)
    const int32_t id = int32_t(m_parent.size());
    m_parent.push_back(id);
    uint8_t mask = faceBits(i);
    const int sy = m_nb.z, sx = m_nb.z * m_nb.y;
    const int x = i / sx, r = i - x * sx, y = r / sy, z = r - y * sy;
    auto join = [&](int k) {
        const int32_t l = m_label[size_t(k)];
        if (l < 0) return;
        const int32_t rt = root(l);
        if (rt == id) return;
        --m_count[m_mask[size_t(rt)]];
        mask |= m_mask[size_t(rt)];
        m_parent[size_t(rt)] = id;
    };
    if (x > 0) join(i - sx);
    if (x + 1 < m_nb.x) join(i + sx);
    if (y > 0) join(i - sy);
    if (y + 1 < m_nb.y) join(i + sy);
    if (z > 0) join(i - 1);
    if (z + 1 < m_nb.z) join(i + 1);
    m_mask.push_back(mask);
    ++m_count[mask];
    m_label[size_t(i)] = id;
}
void BlockConnectivity::closeBlock(int i, bool& needFlood) {
    const int32_t rt = root(m_label[size_t(i)]);
    m_label[size_t(i)] = -1;
    const int sy = m_nb.z, sx = m_nb.z * m_nb.y;
    const glm::ivec3 b(i / sx, (i % sx) / sy, i % sy);
    // the open cells of the 3x3x3 neighbourhood (clipped), the block itself excluded; slot = (dx+1)*9 + (dy+1)*3 + dz+1
    int cell[27];
    bool open[27] = {}, seen[27] = {};
    int nbr[6], nn = 0;
    for (int dx = -1; dx <= 1; ++dx)
        for (int dy = -1; dy <= 1; ++dy)
            for (int dz = -1; dz <= 1; ++dz) {
                const int s = (dx + 1) * 9 + (dy + 1) * 3 + dz + 1;
                const glm::ivec3 p = b + glm::ivec3(dx, dy, dz);
                if (s == 13 || p.x < 0 || p.y < 0 || p.z < 0 || p.x >= m_nb.x || p.y >= m_nb.y || p.z >= m_nb.z) continue;
                cell[s] = p.z + sy * p.y + sx * p.x;
                open[s] = m_label[size_t(cell[s])] >= 0;
                if (open[s] && std::abs(dx) + std::abs(dy) + std::abs(dz) == 1) nbr[nn++] = s;
            }
    if (nn == 0) {   // no open neighbour: the block was a component alone
        --m_count[m_mask[size_t(rt)]];
        return;
    }
    ++m_local;
    // from the first open neighbour through open cells of the neighbourhood
    int q[27], qn = 0;
    q[qn++] = nbr[0];
    seen[nbr[0]] = true;
    uint8_t reachedFaces = 0;
    for (int h = 0; h < qn; ++h) {
        const int s = q[h];
        reachedFaces |= faceBits(cell[s]);
        const int sx3 = s / 9, sy3 = (s / 3) % 3, sz3 = s % 3;
        const int st[6][3] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
        for (const auto& d : st) {
            const int a = sx3 + d[0], c = sy3 + d[1], e = sz3 + d[2];
            if (a < 0 || a > 2 || c < 0 || c > 2 || e < 0 || e > 2) continue;
            const int t = a * 9 + c * 3 + e;
            if (!open[t] || seen[t]) continue;
            seen[t] = true;
            q[qn++] = t;
        }
    }
    for (int k = 1; k < nn; ++k)
        if (!seen[nbr[k]]) {
            needFlood = true;   // the neighbours may be split apart: only the flood can tell
            return;
        }
    const uint8_t own = faceBits(i);
    if ((reachedFaces & own) != own) needFlood = true;   // a chunk face the component may have touched only here
}
void BlockConnectivity::update(const std::vector<uint8_t>& blocking, const std::vector<uint32_t>& flipped) {
    if (m_label.size() != blocking.size()) {
        rebuild(m_nb, blocking);
        return;
    }
    bool flood = false;
    for (uint32_t k : flipped) {   // one at a time: a flip not applied yet still reads as its old bit (its label)
        const bool want = blocking[k] != 0, now = m_label[k] < 0;
        if (want == now) continue;
        if (want) closeBlock(int(k), flood);
        else openBlock(int(k));
        if (flood) break;
    }
    if (flood || m_parent.size() > 4 * m_label.size() + 64) rebuild(m_nb, blocking);   // or the labels grew too many
}
FaceConnect BlockConnectivity::connect() const {
    FaceConnect c{0, 0, 0, 0, 0, 0};
    for (uint32_t m = 1; m < 64; ++m)
        if (m_count[m])
            for (int f = 0; f < 6; ++f)
                if (m & (1u << f)) c[size_t(f)] |= uint8_t(m);
    return c;
}
size_t BlockConnectivity::bytes() const {
    return m_label.capacity() * 4 + m_parent.capacity() * 4 + m_mask.capacity();
}

namespace {
// clusterFaces' order as one number: the cell, then the direction's place in it
uint32_t groupKey(PackedFace f, int cellV, bool octantOrder) {
    const glm::ivec3 c = unpackFace(f).o / cellV;
    const int dI = faceDirIndex(f);
    return (((uint32_t(c.x) << 16) | (uint32_t(c.y) << 8) | uint32_t(c.z)) << 3) |
           uint32_t(octantOrder ? faceDirRank(dI) : dI);
}
uint32_t cellOf(PackedFace f, int cellV) { return groupKey(f, cellV, true) >> 3; }
void boxOf(const std::vector<PackedFace>& faces, FaceCluster& c) {
    c.lo = glm::ivec3(std::numeric_limits<int>::max());
    c.hi = glm::ivec3(std::numeric_limits<int>::min());
    for (uint32_t i = c.first; i < c.first + c.count; ++i) {
        glm::ivec3 lo, hi;
        faceBox(faces[i], lo, hi);
        c.lo = glm::min(c.lo, lo);
        c.hi = glm::max(c.hi, hi);
    }
}
}  // namespace

void applyClusteredDelta(std::vector<PackedFace>& faces, std::vector<FaceCluster>& groups, const RunsDelta& d,
                         int clusterBlocks, bool octantOrder) {
    const int cellV = std::max(1, clusterBlocks) * 8;
    size_t ri = 0;   // d.removed is ascending and the groups tile `faces` in order: one cursor walks both
    std::vector<std::pair<uint32_t, PackedFace>> add;
    add.reserve(d.added.size());
    for (const PackedFace& f : d.added) add.push_back({groupKey(f, cellV, octantOrder), f});
    std::stable_sort(add.begin(), add.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
    std::vector<PackedFace> nf;
    nf.reserve(faces.size() + add.size());
    std::vector<FaceCluster> ng;
    ng.reserve(groups.size() + 8);
    size_t gi = 0, ai = 0;
    while (gi < groups.size() || ai < add.size()) {
        const uint32_t gk = gi < groups.size() ? groupKey(faces[groups[gi].first], cellV, octantOrder)
                                               : std::numeric_limits<uint32_t>::max();
        const uint32_t ak = ai < add.size() ? add[ai].first : std::numeric_limits<uint32_t>::max();
        const uint32_t key = std::min(gk, ak);
        FaceCluster c;
        c.first = uint32_t(nf.size());
        bool touched = false;
        if (gk == key) {
            const FaceCluster& o = groups[gi++];
            const uint32_t end = o.first + o.count;
            if (ri < d.removed.size() && d.removed[ri] < end) {
                touched = true;
                for (uint32_t i = o.first; i < end; ++i) {
                    if (ri < d.removed.size() && d.removed[ri] == i) {
                        ++ri;
                        continue;
                    }
                    nf.push_back(faces[i]);
                }
            } else {
                nf.insert(nf.end(), faces.begin() + o.first, faces.begin() + end);   // F3.5a: an untouched group whole
            }
            c.dir = o.dir;
            c.lo = o.lo;
            c.hi = o.hi;
        }
        for (; ai < add.size() && add[ai].first == key; ++ai) {
            nf.push_back(add[ai].second);
            touched = true;
        }
        c.count = uint32_t(nf.size()) - c.first;
        if (c.count == 0) continue;
        if (touched) {
            c.dir = faceDirIndex(nf[c.first]);
            boxOf(nf, c);
        }
        ng.push_back(c);
    }
    faces.swap(nf);
    groups.swap(ng);
}

std::vector<FaceCluster> cellsOfGroups(const std::vector<PackedFace>& faces, const std::vector<FaceCluster>& groups,
                                       int clusterBlocks) {
    const int cellV = std::max(1, clusterBlocks) * 8;
    std::vector<FaceCluster> out;
    uint32_t cur = std::numeric_limits<uint32_t>::max();
    for (const FaceCluster& g : groups) {
        const uint32_t k = cellOf(faces[g.first], cellV);
        if (out.empty() || k != cur) {
            FaceCluster c;
            c.first = g.first;
            c.count = 0;
            c.lo = g.lo;
            c.hi = g.hi;
            out.push_back(c);
            cur = k;
        }
        FaceCluster& c = out.back();
        c.count += g.count;
        c.lo = glm::min(c.lo, g.lo);
        c.hi = glm::max(c.hi, g.hi);
    }
    return out;
}

}  // namespace Phyxel::Core::VoxFound

