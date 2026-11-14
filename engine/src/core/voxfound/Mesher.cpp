#include "core/voxfound/Mesher.h"

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
uint8_t faceAt(const VoxelGrid& g, Edges outside, glm::ivec3 p, int axis, int dir, const CardSet* cards) {
    const uint8_t m = g.mat[g.idx(p)];
    if (m == 0 || (cards && cards->has(m))) return 0;
    glm::ivec3 q = p;
    q[axis] += dir;
    if (!g.in(q)) return outside.solidOutside(p, axis, dir) ? 0 : m;
    return g.mat[g.idx(q)] == 0 ? m : 0;
}

glm::ivec3 axisPoint(int axis, int s, int u, int v) {
    glm::ivec3 p;
    p[axis] = s;
    p[(axis + 1) % 3] = u;
    p[(axis + 2) % 3] = v;
    return p;
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
        for (int k = 0; k < 2; ++k) bits[2 * a + k].assign((planeBits(d, a) + 31) / 32, s ? ~0u : 0u);
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

Neighbours neighboursOf(const VoxelGrid& world, glm::ivec3 lo, glm::ivec3 size, Outside beyond) {
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
    std::vector<PackedFace> out;
    for (size_t i = 0; i < g.size(); ++i) {
        const uint8_t m = g.mat[i];
        if (m == 0 || !p.cards.has(m)) continue;
        const glm::ivec3 c = g.pos(i);
        if (sprigHash24(p.worldOriginVoxel + c) >= p.threshold24) continue;
        bool exposed = false;
        for (int a = 0; a < 3 && !exposed; ++a)
            for (int d : {-1, 1}) {
                glm::ivec3 q = c;
                q[a] += d;
                if (g.in(q) ? g.mat[g.idx(q)] == 0 : !outside.solidOutside(c, a, d)) { exposed = true; break; }
            }
        if (exposed) out.push_back(packSprig(c, m));
    }
    return out;
}
}  // namespace Phyxel::Core::VoxFound

namespace Phyxel::Core::VoxFound {

// ---- F2.10 occlusion (§4.6 item 4) ----------------------------------------------------------------------------
FaceConnect blockFaceConnectivity(const VoxelGrid& g, const CardSet& seeThrough) {
    const glm::ivec3 nb = g.dim / 8;
    const int total = nb.x * nb.y * nb.z;
    auto bi = [&](glm::ivec3 b) { return b.z + nb.z * (b.y + nb.y * b.x); };
    // a block blocks sight only if all 512 voxels are solid and not see-through
    std::vector<uint8_t> blocking(size_t(total), 0);
    for (int bx = 0; bx < nb.x; ++bx)
        for (int by = 0; by < nb.y; ++by)
            for (int bz = 0; bz < nb.z; ++bz) {
                bool all = true;
                for (int x = 0; x < 8 && all; ++x)
                    for (int y = 0; y < 8 && all; ++y)
                        for (int z = 0; z < 8; ++z) {
                            const uint8_t m = g.mat[g.idx({bx * 8 + x, by * 8 + y, bz * 8 + z})];
                            if (m == 0 || seeThrough.has(m)) {
                                all = false;
                                break;
                            }
                        }
                blocking[size_t(bi({bx, by, bz}))] = all ? 1 : 0;
            }
    FaceConnect out{0, 0, 0, 0, 0, 0};
    std::vector<int> comp(size_t(total), -1);
    std::vector<glm::ivec3> stack;
    const glm::ivec3 dir[6] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
    int id = 0;
    for (int bx = 0; bx < nb.x; ++bx)
        for (int by = 0; by < nb.y; ++by)
            for (int bz = 0; bz < nb.z; ++bz) {
                const glm::ivec3 s(bx, by, bz);
                if (blocking[size_t(bi(s))] || comp[size_t(bi(s))] != -1) continue;
                uint8_t faces = 0;
                comp[size_t(bi(s))] = id;
                stack.assign(1, s);
                while (!stack.empty()) {
                    const glm::ivec3 c = stack.back();
                    stack.pop_back();
                    if (c.x == 0) faces |= 1u << 0;
                    if (c.x == nb.x - 1) faces |= 1u << 1;
                    if (c.y == 0) faces |= 1u << 2;
                    if (c.y == nb.y - 1) faces |= 1u << 3;
                    if (c.z == 0) faces |= 1u << 4;
                    if (c.z == nb.z - 1) faces |= 1u << 5;
                    for (const glm::ivec3& d : dir) {
                        const glm::ivec3 n = c + d;
                        if (n.x < 0 || n.y < 0 || n.z < 0 || n.x >= nb.x || n.y >= nb.y || n.z >= nb.z) continue;
                        const size_t ni = size_t(bi(n));
                        if (blocking[ni] || comp[ni] != -1) continue;
                        comp[ni] = id;
                        stack.push_back(n);
                    }
                }
                for (int f = 0; f < 6; ++f)
                    if (faces & (1u << f)) out[size_t(f)] |= faces;
                ++id;
            }
    return out;
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

std::vector<FaceCluster> clusterFaces(std::vector<PackedFace>& faces, int clusterBlocks) {
    const int cellV = std::max(1, clusterBlocks) * 8;
    auto cellKey = [&](PackedFace f) {
        const glm::ivec3 c = unpackFace(f).o / cellV;   // chunk-local, non-negative
        return (uint32_t(c.x) << 16) | (uint32_t(c.y) << 8) | uint32_t(c.z);
    };
    std::stable_sort(faces.begin(), faces.end(),
                     [&](PackedFace a, PackedFace b) { return cellKey(a) < cellKey(b); });
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
glm::ivec2 lodLevelsForBox(glm::vec3 lo, glm::vec3 hi, glm::vec3 cam, float c, int maxLevel) {
    const glm::vec3 nearest = glm::clamp(cam, lo, hi);
    const glm::vec3 farthest(std::abs(cam.x - lo.x) > std::abs(cam.x - hi.x) ? lo.x : hi.x,
                             std::abs(cam.y - lo.y) > std::abs(cam.y - hi.y) ? lo.y : hi.y,
                             std::abs(cam.z - lo.z) > std::abs(cam.z - hi.z) ? lo.z : hi.z);
    // the extreme Bayer values are 0.5/16 and 15.5/16
    return {lodPickLevel(lodLevelAt(glm::length(nearest - cam), c) - kLodSlack, 0.5f / 16.0f, maxLevel),
            lodPickLevel(lodLevelAt(glm::length(farthest - cam), c) + kLodSlack, 15.5f / 16.0f, maxLevel)};
}
uint32_t lodFlagBits(int level, int maxLevel, int code) {
    return (uint32_t(level) & 0xFu) << 16 | (uint32_t(maxLevel) & 0x7u) << 20 | 1u << 23 | (uint32_t(code) & 0xFFu) << 24;
}

}  // namespace Phyxel::Core::VoxFound
