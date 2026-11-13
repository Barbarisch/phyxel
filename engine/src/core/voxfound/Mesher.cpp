#include "core/voxfound/Mesher.h"

#include <algorithm>
#include <cmath>
#include <sstream>
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
