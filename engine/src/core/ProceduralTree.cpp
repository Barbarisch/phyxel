// Branch-driven tree generator — C++ port of tools/gen_tree.py (same algorithm/look).
// Geometry is computed in SUB space (1 unit = 1/3 cube), pruned of floaters, then compressed
// to cubes/subcubes on emit. Microcube sprigs are omitted: gen_tree.py's prune drops them all
// (center-positioned or embedded), so the visible result is identical and the port is simpler.
#include "core/ProceduralTree.h"
#include "core/PlacedObjectManager.h"   // complete InteractionPointDef (VoxelTemplate holds it by value)
#include <cmath>
#include <vector>
#include <array>
#include <map>
#include <set>
#include <random>
#include <functional>
#include <glm/glm.hpp>

namespace Phyxel {

namespace {

constexpr float SOLID = 0.92f, FUZZ = 1.06f;

struct Vec3 { float x, y, z; };
static Vec3 norm(Vec3 v) {
    float m = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (m == 0.0f) m = 1.0f;
    return {v.x / m, v.y / m, v.z / m};
}
static int iround(float f) { return static_cast<int>(std::lround(f)); }
static int ifloordiv(int a, int b) { return (a >= 0) ? a / b : -((-a + b - 1) / b); }
static int imod3(int a) { int m = a % 3; return m < 0 ? m + 3 : m; }

// Sub-space accumulator: logs always win over leaves at a given sub cell.
struct SubTree {
    std::map<std::array<int, 3>, std::pair<std::string, bool>> sub;  // key -> (material, isLog)

    void put(int sx, int sy, int sz, const std::string& mat, bool isLog) {
        if (sy < 0) return;
        std::array<int, 3> k{sx, sy, sz};
        auto it = sub.find(k);
        if (it == sub.end() || (isLog && !it->second.second)) sub[k] = {mat, isLog};
    }
    void log(int sx, int sy, int sz, const std::string& m) { put(sx, sy, sz, m, true); }
    void leaf(int sx, int sy, int sz, const std::string& m) { if (!m.empty()) put(sx, sy, sz, m, false); }
    void fillCube(int cx, int cy, int cz, const std::string& m) {
        for (int dx = 0; dx < 3; ++dx)
            for (int dy = 0; dy < 3; ++dy)
                for (int dz = 0; dz < 3; ++dz)
                    put(cx * 3 + dx, cy * 3 + dy, cz * 3 + dz, m, true);
    }
};

// Deterministic RNG mirroring gen_tree.py's per-tree seeding (same algorithm, not bit-identical).
struct Rng {
    std::mt19937 gen;
    explicit Rng(const std::string& key) {
        std::seed_seq seq(key.begin(), key.end());
        gen.seed(seq);
    }
    float rnd() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(gen); }
    float range(float a, float b) { return a + rnd() * (b - a); }
    int irange(int a, int b) { return a + static_cast<int>(rnd() * (b - a + 1)); }  // inclusive
    bool chance(float p) { return rnd() < p; }
};

bool shellKeep(Rng& r, float d, float fullness) {
    if (d <= SOLID) return true;
    float t = (d - SOLID) / (FUZZ - SOLID);
    return r.rnd() < fullness * (1.0f - 0.8f * t);
}

Vec3 perturbDir(Rng& r, Vec3 d, float spread, float upBias) {
    return norm({d.x + r.range(-spread, spread),
                 d.y + r.range(-spread, spread) + upBias,
                 d.z + r.range(-spread, spread)});
}

void leafCluster(SubTree& t, Rng& r, int cx, int cy, int cz, float rad,
                 const std::string& leaf, float fullness) {
    int ir = static_cast<int>(rad) + 2;
    for (int sx = cx - ir; sx <= cx + ir; ++sx)
        for (int sy = cy - ir; sy <= cy + ir; ++sy)
            for (int sz = cz - ir; sz <= cz + ir; ++sz) {
                float d = std::sqrt(float((sx - cx) * (sx - cx) + (sy - cy) * (sy - cy) +
                                          (sz - cz) * (sz - cz))) / std::max(rad, 0.5f);
                if (d > FUZZ) continue;
                if (shellKeep(r, d, fullness)) t.leaf(sx, sy, sz, leaf);
            }
}

void ellipsoidCanopy(SubTree& t, Rng& r, int cx, int cy, int cz, float rx, float ry,
                     const std::string& leaf, float fullness) {
    for (int sx = int(cx - rx - 2); sx <= int(cx + rx + 2); ++sx)
        for (int sy = int(cy - ry - 2); sy <= int(cy + ry + 2); ++sy)
            for (int sz = int(cz - rx - 2); sz <= int(cz + rx + 2); ++sz) {
                float d = std::sqrt(((sx - cx) / rx) * ((sx - cx) / rx) +
                                    ((sy - cy) / ry) * ((sy - cy) / ry) +
                                    ((sz - cz) / rx) * ((sz - cz) / rx));
                if (d > FUZZ) continue;
                if (shellKeep(r, d, fullness)) t.leaf(sx, sy, sz, leaf);
            }
}

void discCanopy(SubTree& t, Rng& r, int cy, float rad, const std::string& leaf, float fullness,
                int thick, int cx, int cz) {
    for (int sx = int(cx - rad - 2); sx <= int(cx + rad + 2); ++sx)
        for (int sz = int(cz - rad - 2); sz <= int(cz + rad + 2); ++sz) {
            float d = std::hypot(float(sx - cx), float(sz - cz)) / rad;
            if (d > FUZZ) continue;
            for (int dy = 0; dy < thick; ++dy)
                if (shellKeep(r, d, fullness)) t.leaf(sx, cy + dy, sz, leaf);
        }
}

int cubeTrunk(SubTree& t, Rng& r, int hCubes, const std::string& mat, float taperFrom,
              bool rootFlare) {
    int taperY = std::max(1, iround(hCubes * taperFrom));
    for (int cy = 0; cy < taperY; ++cy) t.fillCube(0, cy, 0, mat);
    const int plus[5][2] = {{1, 1}, {0, 1}, {2, 1}, {1, 0}, {1, 2}};
    for (int sy = taperY * 3; sy < hCubes * 3; ++sy)
        for (auto& d : plus) t.log(d[0], sy, d[1], mat);
    if (rootFlare) {
        const int flare[8][2] = {{-1, 0}, {3, 0}, {0, -1}, {0, 3}, {-1, -1}, {3, 3}, {-1, 3}, {3, -1}};
        for (auto& f : flare)
            if (r.chance(0.6f)) {
                int hgt = r.irange(1, 2);
                for (int sy = 0; sy < hgt; ++sy)
                    t.log(f[0] >= 0 ? f[0] : -1, sy, f[1] >= 0 ? f[1] : -1, mat);
            }
    }
    return hCubes * 3;
}

void growBranch(SubTree& t, Rng& r, Vec3 pos, Vec3 dir, int length, int thickness, int depth,
                const std::string& log, std::vector<glm::ivec3>& clusters,
                float spread = 0.7f, float upBias = 0.12f, float taper = 0.66f) {
    float x = pos.x, y = pos.y, z = pos.z;
    for (int i = 0; i < length; ++i) {
        x += dir.x; y += dir.y; z += dir.z;
        int bx = iround(x), by = iround(y), bz = iround(z);
        for (int ox = 0; ox < thickness; ++ox)
            for (int oz = 0; oz < thickness; ++oz) t.log(bx + ox, by, bz + oz, log);
        if (i >= length * 0.45f && i % 2 == 0) clusters.push_back({bx, by, bz});
    }
    glm::ivec3 end{iround(x), iround(y), iround(z)};
    if (depth <= 0 || length <= 2) { clusters.push_back(end); return; }
    clusters.push_back(end);
    int forks = r.irange(2, 3);
    for (int f = 0; f < forks; ++f) {
        Vec3 cdir = perturbDir(r, dir, spread, upBias);
        int clen = std::max(2, static_cast<int>(length * r.range(taper - 0.1f, taper + 0.12f)));
        growBranch(t, r, {float(end.x), float(end.y), float(end.z)}, cdir, clen,
                   std::max(1, thickness - 1), depth - 1, log, clusters, spread, upBias, taper);
    }
}

void branchedCrown(SubTree& t, Rng& r, int bx, int bz, int top, int rc, const std::string& log,
                   const std::string& leaf, float fullness, int nlA, int nlB, float crownLo,
                   float upA, float upB, float outA, float outB, int depth, float blobA, float blobB) {
    int crownLoY = static_cast<int>(top * crownLo);
    std::vector<glm::ivec3> clusters;
    int nl = r.irange(nlA, nlB);
    for (int k = 0; k < nl; ++k) {
        int by = r.irange(crownLoY, top - 1);
        float ang = 2.0f * 3.14159265f * k / nl + r.range(-0.5f, 0.5f);
        float o = r.range(outA, outB);
        Vec3 d0 = norm({std::cos(ang) * o, r.range(upA, upB), std::sin(ang) * o});
        growBranch(t, r, {float(bx), float(by), float(bz)}, d0,
                   r.irange(int(rc * 1.4f), rc * 2 + 1), 3, depth, log, clusters);
    }
    growBranch(t, r, {float(bx), float(top), float(bz)},
               norm({r.range(-0.25f, 0.25f), 1.0f, r.range(-0.25f, 0.25f)}),
               std::max(2, int(rc * 1.5f)), 2, std::max(1, depth - 1), log, clusters);
    for (auto& c : clusters) leafCluster(t, r, c.x, c.y, c.z, r.range(blobA, blobB), leaf, fullness);
}

glm::ivec3 subBranch(SubTree& t, Rng& r, glm::ivec3 start, int dx, int dz, int length,
                     const std::string& log, int thickness, float rise) {
    int x = start.x, y = start.y, z = start.z;
    for (int i = 0; i < length; ++i) {
        float rr = r.rnd();
        if (rr < 0.42f) x += dx;
        else if (rr < 0.84f) z += dz;
        else y += r.chance(rise) ? 1 : -1;
        int th = (i < length * 0.6f) ? thickness : 1;
        for (int bx = 0; bx < th; ++bx)
            for (int by = 0; by < th; ++by) t.log(x + bx, y + by, z, log);
    }
    return {x, y, z};
}

// ---- archetypes ----
void genOak(SubTree& t, Rng& r, int h, int radius, float fullness, const std::string& log, const std::string& leaf) {
    int rc = radius > 0 ? radius : std::max(2, iround(h * 0.5f));
    int top = cubeTrunk(t, r, h, log, 0.45f, true);
    branchedCrown(t, r, 1, 1, top, rc, log, leaf, fullness, 4, 6, 0.5f, 0.35f, 0.7f, 0.9f, 1.4f, 2, 2.6f, 4.0f);
}
void genBirch(SubTree& t, Rng& r, int h, int radius, float fullness, const std::string& log, const std::string& leaf) {
    int rc = radius > 0 ? radius : std::max(2, iround(h * 0.32f));
    int top = cubeTrunk(t, r, h, log, 0.4f, false);
    branchedCrown(t, r, 1, 1, top, rc, log, leaf, fullness, 2, 3, 0.6f, 0.6f, 0.95f, 0.6f, 1.0f, 2, 2.2f, 3.2f);
}
void genBush(SubTree& t, Rng& r, int radius, float fullness, const std::string& leaf) {
    int rc = (radius > 0 ? radius : 2) * 3;
    std::vector<glm::ivec3> centers{{0, std::max(2, rc - 1), 0}};
    int extra = r.irange(2, 4);
    for (int i = 0; i < extra; ++i)
        centers.push_back({r.irange(-rc + 1, rc - 1), r.irange(std::max(1, rc - 3), rc), r.irange(-rc + 1, rc - 1)});
    for (auto& c : centers) leafCluster(t, r, c.x, c.y, c.z, r.range(rc * 0.45f, rc * 0.7f), leaf, fullness);
}
void genSpruce(SubTree& t, Rng& r, int h, int radius, float fullness, const std::string& log, const std::string& leaf) {
    float baseR = (radius > 0 ? radius : std::max(2, iround(h * 0.38f))) * 3.0f;
    int top = cubeTrunk(t, r, h, log, 0.5f, true);
    int lo = std::max(5, iround(top * 0.22f));
    for (int sy = lo; sy <= top + 2; ++sy) {
        float f = (sy - lo) / float(std::max(1, top + 2 - lo));
        float rad = std::max(1.4f, baseR * std::pow(1.0f - f, 1.1f)) + r.range(-0.4f, 0.4f);
        for (int sx = int(1 - rad - 1); sx <= int(1 + rad + 1); ++sx)
            for (int sz = int(1 - rad - 1); sz <= int(1 + rad + 1); ++sz) {
                float d = std::hypot(float(sx - 1), float(sz - 1)) / std::max(rad, 0.1f);
                if (d <= FUZZ && shellKeep(r, d, fullness)) t.leaf(sx, sy, sz, leaf);
            }
    }
    t.leaf(1, top + 3, 1, leaf);
}
void genAcacia(SubTree& t, Rng& r, int h, int radius, float fullness, const std::string& log, const std::string& leaf) {
    float rad = (radius > 0 ? radius : std::max(4, iround(h * 0.85f))) * 3.0f;
    int x = 0, z = 0, top = h * 3;
    int driftX = r.chance(0.5f) ? 1 : -1;
    int driftZ = r.irange(-1, 1);
    int kink = r.irange(5, std::max(6, h * 3 - 8));
    for (int sy = 0; sy < top; ++sy) {
        if (sy >= kink && sy % 2 == 0) { x += driftX; z += driftZ; }
        for (int bx = 0; bx < 2; ++bx)
            for (int bz = 0; bz < 2; ++bz) t.log(x + bx, sy, z + bz, log);
    }
    for (int bx = -1; bx < 3; ++bx)
        for (int bz = -1; bz < 3; ++bz) t.log(bx, 0, bz, log);
    discCanopy(t, r, top, rad, leaf, fullness, 2, x + 1, z + 1);
    if (r.chance(0.5f))
        discCanopy(t, r, std::max(6, top - 7), std::max(5.0f, rad / 2), leaf, fullness, 1, -x, -z);
}
void genDead(SubTree& t, Rng& r, int h, const std::string& log) {
    int top = cubeTrunk(t, r, h, log, 0.4f, true);
    int branches = r.irange(3, 5);
    for (int b = 0; b < branches; ++b) {
        int by = r.irange(iround(top * 0.4f), top - 1);
        glm::ivec3 end = subBranch(t, r, {1, by, 1}, r.chance(0.5f) ? 1 : -1, r.chance(0.5f) ? 1 : -1,
                                   r.irange(4, 8), log, 2, 0.55f);
        subBranch(t, r, end, r.chance(0.5f) ? 1 : -1, r.chance(0.5f) ? 1 : -1, r.irange(2, 3), log, 1, 0.5f);
    }
    t.log(1, top, 1, log);
}

// 26-connectivity prune from the trunk base + face-erosion of lonely leaves (see gen_tree.py).
void pruneFloaters(SubTree& t) {
    if (t.sub.empty()) return;
    int miny = t.sub.begin()->first[1];
    for (auto& kv : t.sub) miny = std::min(miny, kv.first[1]);
    std::set<std::array<int, 3>> keep;
    std::vector<std::array<int, 3>> stack;
    for (auto& kv : t.sub)
        if (kv.first[1] <= miny + 1) { keep.insert(kv.first); stack.push_back(kv.first); }
    while (!stack.empty()) {
        auto k = stack.back(); stack.pop_back();
        for (int dx = -1; dx <= 1; ++dx)
            for (int dy = -1; dy <= 1; ++dy)
                for (int dz = -1; dz <= 1; ++dz) {
                    if (!dx && !dy && !dz) continue;
                    std::array<int, 3> n{k[0] + dx, k[1] + dy, k[2] + dz};
                    if (t.sub.count(n) && !keep.count(n)) { keep.insert(n); stack.push_back(n); }
                }
    }
    for (auto it = t.sub.begin(); it != t.sub.end();)
        it = keep.count(it->first) ? std::next(it) : t.sub.erase(it);

    const int face[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    std::vector<std::array<int, 3>> lonely;
    for (auto& kv : t.sub) {
        if (kv.second.second) continue;  // logs always kept
        bool touch = false;
        for (auto& f : face)
            if (t.sub.count({kv.first[0] + f[0], kv.first[1] + f[1], kv.first[2] + f[2]})) { touch = true; break; }
        if (!touch) lonely.push_back(kv.first);
    }
    for (auto& k : lonely) t.sub.erase(k);
}

VoxelTemplate emitTemplate(const SubTree& t) {
    VoxelTemplate out;
    if (t.sub.empty()) return out;
    // Group subs by cube cell.
    std::map<std::array<int, 3>, std::map<std::array<int, 3>, std::string>> byCube;
    for (auto& kv : t.sub) {
        std::array<int, 3> cell{ifloordiv(kv.first[0], 3), ifloordiv(kv.first[1], 3), ifloordiv(kv.first[2], 3)};
        byCube[cell][{imod3(kv.first[0]), imod3(kv.first[1]), imod3(kv.first[2])}] = kv.second.first;
    }
    int ox = byCube.begin()->first[0], oy = byCube.begin()->first[1], oz = byCube.begin()->first[2];
    for (auto& c : byCube) {
        ox = std::min(ox, c.first[0]); oy = std::min(oy, c.first[1]); oz = std::min(oz, c.first[2]);
    }
    for (auto& c : byCube) {
        glm::ivec3 rel{c.first[0] - ox, c.first[1] - oy, c.first[2] - oz};
        const auto& subs = c.second;
        bool uniform = subs.size() == 27;
        if (uniform) {
            const std::string& m0 = subs.begin()->second;
            for (auto& s : subs) if (s.second != m0) { uniform = false; break; }
            if (uniform) { out.addCube(rel, m0); continue; }
        }
        for (auto& s : subs)
            out.addSubcube(rel, {s.first[0], s.first[1], s.first[2]}, s.second);
    }
    return out;
}

}  // namespace

VoxelTemplate ProceduralTree::generate(const std::string& type, int height, float fullness,
                                       uint32_t seed, const std::string& log, const std::string& leaf) {
    Rng r(type + ":" + std::to_string(height) + ":" + std::to_string(fullness) + ":" + std::to_string(seed));
    SubTree t;
    // New species map to the nearest ported shape (materials come from the caller's archetype
    // table): pine/fir -> conical conifer, palm -> kinked trunk + disc canopy, jungle/willow/
    // redwood/elder_oak -> broad branched crown. Dedicated shapes live in gen_tree.py only.
    if (type == "spruce" || type == "pine" || type == "fir")
        genSpruce(t, r, height, 0, fullness, log, leaf);
    else if (type == "acacia" || type == "palm") genAcacia(t, r, height, 0, fullness, log, leaf);
    else if (type == "dead") genDead(t, r, height, log);
    else if (type == "bush") genBush(t, r, height > 0 ? height : 2, fullness, leaf);
    else if (type == "birch") genBirch(t, r, height, 0, fullness, log, leaf);
    else genOak(t, r, height, 0, fullness, log, leaf);  // oak/autumn/jungle/willow/redwood/unknown
    pruneFloaters(t);
    return emitTemplate(t);
}

}  // namespace Phyxel
