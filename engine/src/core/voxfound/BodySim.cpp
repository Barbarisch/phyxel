// docs/VoxelFoundationPlan.md §6.7 -- the CPU reference body solver (F5.2a). See BodySim.h;
// docs/evidence/voxfound_f52_prediction.txt.
#include "core/voxfound/BodySim.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <cstdio>
#include <set>
#include <string>
#include <tuple>

namespace Phyxel::Core::VoxFound {

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();
static const glm::ivec3 kDir[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
// an aligned exit may be at most this much deeper than the shallowest (0.25 with 26 neighbours let a sliding box's
// front-face sample, a hair inside the ground, push it back through the diagonal: R4 tipped 12.9 deg and reversed)
constexpr double kAlignSlackVox = 0.05;

glm::dquat quatIntegrate(const glm::dquat& q, const glm::dvec3& v) {
    const glm::dquat dq = glm::dquat(0.0, v.x, v.y, v.z) * q;
    glm::dquat r(q.w + 0.5 * dq.w, q.x + 0.5 * dq.x, q.y + 0.5 * dq.y, q.z + 0.5 * dq.z);
    return glm::normalize(r);
}

glm::dvec3 angDisp(const glm::dquat& q, const glm::dquat& q0) {
    glm::dquat d = q * glm::conjugate(q0);
    if (d.w < 0.0) d = -d;
    return 2.0 * glm::dvec3(d.x, d.y, d.z);
}

uint64_t mix64(uint64_t x) {
    x += 0x9e3779b97f4a7c15ull;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
    return x ^ (x >> 31);
}

uint64_t keyOf(size_t a, size_t b, uint64_t feature) {
    return mix64(mix64(uint64_t(a) * 0x100000001b3ull ^ uint64_t(b)) ^ feature);
}

void tangentBasis(const glm::dvec3& n, glm::dvec3& t1, glm::dvec3& t2) {
    t1 = std::abs(n.x) < 0.9 ? glm::normalize(glm::cross(n, glm::dvec3(1, 0, 0)))
                             : glm::normalize(glm::cross(n, glm::dvec3(0, 1, 0)));
    t2 = glm::cross(n, t1);
}

// 6x6 symmetric solve (LDL^T), a full matrix
void solve6(double a[6][6], const double b[6], double x[6]) {
    double L[6][6] = {}, D[6] = {};
    for (int j = 0; j < 6; ++j) {
        double d = a[j][j];
        for (int k = 0; k < j; ++k) d -= L[j][k] * L[j][k] * D[k];
        D[j] = std::max(d, 1e-30);
        L[j][j] = 1.0;
        for (int i = j + 1; i < 6; ++i) {
            double s = a[i][j];
            for (int k = 0; k < j; ++k) s -= L[i][k] * L[j][k] * D[k];
            L[i][j] = s / D[j];
        }
    }
    double y[6];
    for (int i = 0; i < 6; ++i) {
        y[i] = b[i];
        for (int k = 0; k < i; ++k) y[i] -= L[i][k] * y[k];
    }
    for (int i = 0; i < 6; ++i) y[i] /= D[i];
    for (int i = 5; i >= 0; --i) {
        x[i] = y[i];
        for (int k = i + 1; k < 6; ++k) x[i] -= L[k][i] * x[k];
    }
}

// a grid seen through a rigid transform: body-local metres -> grid voxel coords
struct GridView {
    const VoxelGrid* g = nullptr;
    bool ground = false;                      // the world with y < 0 solid
    glm::dvec3 comLocal{0.0};                 // voxel coords of the body's centre (bodies)
    double vm = 0.125;

    bool solid(glm::ivec3 c) const {
        if (g->in(c)) return g->mat[g->idx(c)] != 0;
        return ground && c.y < 0;
    }
    uint8_t mat(glm::ivec3 c) const { return g->in(c) ? g->mat[g->idx(c)] : 0; }
};

struct Escape {
    int status = 0;                           // 0 none, 1 contact, 2 deep
    int dir = -1;                             // index into kNbr: which neighbour region (bucket / feature key)
    glm::dvec3 n{0.0};                        // unit outward normal of the grid there, in the grid's frame
    double pen = 0.0;                         // m, > 0 penetrating, <= 0 speculative gap
    glm::ivec3 cell{0};                       // the solid cell involved
};

// the 26 neighbour offsets (faces, edges, corners)
struct Nbrs {
    glm::ivec3 o[26];
    Nbrs() {
        int k = 0;
        for (int x = -1; x <= 1; ++x)
            for (int y = -1; y <= 1; ++y)
                for (int z = -1; z <= 1; ++z)
                    if (x || y || z) o[k++] = {x, y, z};
    }
};
const Nbrs kNbr;

// u: a point in the grid's voxel coordinates; prefer: the direction (grid frame, unit) the point's own face says it
// should be pushed (the opposite of its face's outward normal).
// Exits are searched over all 26 neighbours (F5.2a run-1 fix: with faces only, a point a millimetre past an inner
// edge of a jagged fracture surface -- its cell's six face neighbours solid, a diagonal one empty -- read as "deep"
// or as almost a voxel deep; the engine's voxel_contact.glsl also uses 26 directions). An exit through an edge or
// corner neighbour is at the distance to that edge / corner, along the normalised offset.
// Only exits within 60 deg of `prefer` count, and only if not more than kAlignSlackVox deeper than the shallowest
// exit; the shallowest such wins. A sample whose face is not the touching one makes NO contact -- the same corner is
// also sampled for each of its other faces, and the facing one covers it (F5.2a run-1 fix: a point that sank a hair
// into a flush face escaped sideways through the nearby edge; those 0.1 mm side contacts took 6 of a pair's 8 slots
// and stacks lost their vertical support; and a bottom-face sample nudged a few mm sideways into a neighbouring
// piece's cell has its aligned exit almost a voxel away -- that is the side face's contact, not a 10 cm one).
// Inside with no empty neighbour at all = deep (counted).
Escape escape(const GridView& v, glm::dvec3 u, double marginVox, glm::dvec3 prefer) {
    Escape e;
    const glm::ivec3 c(int(std::floor(u.x)), int(std::floor(u.y)), int(std::floor(u.z)));
    const glm::dvec3 f = u - glm::dvec3(c);
    const bool inside = v.solid(c);
    double bestA = kInf, bestAny = kInf;
    int kA = -1;
    glm::dvec3 nA(0.0);
    for (int k = 0; k < 26; ++k) {
        const glm::ivec3& o = kNbr.o[k];
        // speculative (outside) contacts use the 6 faces only: a ground point coplanar with a sliding box's bottom saw
        // its leading edge diagonally and pushed it back and up (R4 tipped 8.5 deg and reversed)
        if (!inside && std::abs(o.x) + std::abs(o.y) + std::abs(o.z) != 1) continue;
        if (v.solid(c + o) == inside) continue;   // inside: exits into empty neighbours; outside: solid ones near
        glm::dvec3 vec(0.0);                      // from the point to the neighbour's region
        for (int a = 0; a < 3; ++a)
            if (o[a]) vec[a] = o[a] > 0 ? 1.0 - f[a] : -f[a];
        const double dist = glm::length(vec);
        if (!inside && dist > marginVox) continue;
        // the grid's outward normal there: inside, toward the empty region; outside, from the solid toward the point
        glm::dvec3 n = dist > 1e-12 ? vec / dist : glm::normalize(glm::dvec3(o));
        if (!inside) n = -n;
        bestAny = std::min(bestAny, dist);
        if (glm::dot(n, prefer) >= 0.5 && dist < bestA) { bestA = dist; kA = k; nA = n; }
    }
    if (bestAny == kInf) {
        if (inside) e.status = 2;
        return e;
    }
    if (kA < 0 || bestA > bestAny + kAlignSlackVox) return e;
    e.status = 1;
    e.dir = kA;
    e.n = nA;
    e.pen = inside ? bestA * v.vm : -bestA * v.vm;
    e.cell = inside ? c : c + kNbr.o[kA];
    return e;
}

// F5.5 tunnelling fix: a corner that moved >= 1 voxel this tick and got no contact from escape() (deep: no empty
// neighbour; or in air past a thin slab) marches its path from the tick-start position a to the current position b
// (grid voxel coordinates; 3D DDA, ties to the lowest axis) and takes the first solid cell it enters: the normal is that
// face's outward normal, the depth how far b is past that face's plane, the feature that face's neighbour index. A path
// that starts inside solid or enters none gives nothing. Found by F5.5's ladder: a fast piece put every corner more than
// a voxel into y < 0 in one tick, got no contact, and fell for ever.
Escape sweep(const GridView& v, glm::dvec3 a, glm::dvec3 b) {
    Escape e;
    glm::ivec3 c(int(std::floor(a.x)), int(std::floor(a.y)), int(std::floor(a.z)));
    if (v.solid(c)) return e;
    const glm::dvec3 d = b - a;
    int step[3];
    double tMax[3], tDelta[3];
    for (int k = 0; k < 3; ++k) {
        if (d[k] > 0.0) { step[k] = 1; tMax[k] = (double(c[k]) + 1.0 - a[k]) / d[k]; tDelta[k] = 1.0 / d[k]; }
        else if (d[k] < 0.0) { step[k] = -1; tMax[k] = (a[k] - double(c[k])) / -d[k]; tDelta[k] = 1.0 / -d[k]; }
        else { step[k] = 0; tMax[k] = kInf; tDelta[k] = kInf; }
    }
    for (int it = 0; it < 1024; ++it) {
        int ax = 0;
        if (tMax[1] < tMax[ax]) ax = 1;
        if (tMax[2] < tMax[ax]) ax = 2;
        if (tMax[ax] > 1.0) return e;   // reached b without entering solid
        c[ax] += step[ax];
        if (v.solid(c)) {
            glm::ivec3 o(0);
            o[ax] = -step[ax];
            const double plane = step[ax] > 0 ? double(c[ax]) : double(c[ax]) + 1.0;
            e.status = 1;
            e.n = glm::dvec3(o);
            e.pen = (plane - b[ax]) * double(o[ax]) * v.vm;
            e.cell = c;
            for (int k = 0; k < 26; ++k)
                if (kNbr.o[k] == o) e.dir = k;
            return e;
        }
        tMax[ax] += tDelta[ax];
    }
    return e;
}

// Sample points sit a hair INSIDE their own solid (F5.2a run-1 fix): an exact lattice corner on a flush face is an
// escape tie between the face normal and a side normal (the side won, and zero-depth side contacts took the cap).
constexpr double kInsetVox = 1e-3;

template <class Solid>
glm::dvec3 insetCorner(glm::ivec3 L, const Solid& solid) {
    glm::dvec3 sum(0.0), any(0.0);
    for (int dx = -1; dx <= 0; ++dx)
        for (int dy = -1; dy <= 0; ++dy)
            for (int dz = -1; dz <= 0; ++dz) {
                const glm::ivec3 c = L + glm::ivec3(dx, dy, dz);
                if (!solid(c)) continue;
                const glm::dvec3 d = glm::dvec3(dx, dy, dz) + 0.5;
                sum += d;
                any = d;
            }
    if (glm::dot(sum, sum) < 1e-12) sum = any;
    return glm::dvec3(L) + kInsetVox * glm::normalize(sum);
}

struct Cand {
    glm::dvec3 n{0.0}, rA{0.0}, rB{0.0}, point{0.0};
    double pen = 0.0, mu = 0.5;
    int bucket = 0;
    uint64_t feature = 0;
};

}  // namespace

BodySimRef::BodySimRef(const VoxelGrid& world, const std::vector<FractureMaterial>& mats, BodySimParams p)
    : m_world(world), m_mats(mats), m_p(p) {}

bool BodySimRef::implemented() const { return true; }

void BodySimRef::remove(const std::vector<size_t>& indices) {
    if (indices.empty()) return;
    const size_t n = m_bodies.size();
    std::vector<uint8_t> gone(n, 0);
    for (size_t i : indices)
        if (i < n) gone[i] = 1;
    std::vector<size_t> to(n, kStatic);
    std::vector<SimBody> keep;
    keep.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        if (gone[i]) continue;
        to[i] = keep.size();
        keep.push_back(std::move(m_bodies[i]));
    }
    m_bodies.swap(keep);
    // survivors keep their warm start: the constraints are re-keyed with the new indices
    std::vector<Constraint> cons;
    for (Constraint k : m_cons) {
        if (gone[k.a] || (k.b != kStatic && gone[k.b])) continue;
        k.a = to[k.a];
        if (k.b != kStatic) k.b = to[k.b];
        k.key = keyOf(k.a, k.b, k.feature);
        cons.push_back(k);
    }
    m_cons.swap(cons);
    m_warm.clear();
    for (const Constraint& k : m_cons)
        m_warm[k.key] = {k.lambdaN, k.lambdaT1, k.lambdaT2, k.penN, k.penT1, k.penT2, k.anchor, k.stick};
}

size_t BodySimRef::add(const Body& b, glm::dvec3 vel, glm::dvec3 angVel) {
    SimBody s;
    s.src = b;
    const double vm = b.grid.voxelM;
    s.pos = b.com * vm;
    s.vel = vel;
    s.angVel = angVel;
    s.invMass = b.mass > 0.0 ? 1.0 / b.mass : 0.0;
    s.inertiaBody = b.inertia;
    // majority material -> friction
    std::vector<size_t> count(m_mats.size(), 0);
    for (uint8_t m : b.grid.mat)
        if (m) ++count[m];
    const size_t maj = size_t(std::max_element(count.begin(), count.end()) - count.begin());
    s.friction = m_mats[maj].friction;
    // surface lattice corners (dedup, in lattice order), body-local metres
    const glm::dvec3 comLocal = b.com - glm::dvec3(b.origin);
    std::set<std::tuple<int, int, int, int>> lattice;
    const VoxelGrid& g = b.grid;
    for (size_t i = 0; i < g.size(); ++i) {
        if (!g.mat[i]) continue;
        const glm::ivec3 p = g.pos(i);
        for (int d = 0; d < 6; ++d) {
            const glm::ivec3 q = p + kDir[d];
            if (g.in(q) && g.mat[g.idx(q)]) continue;
            const int a = d / 2;
            const int base = kDir[d][a] > 0 ? p[a] + 1 : p[a];
            const int u = (a + 1) % 3, w = (a + 2) % 3;
            for (int du = 0; du < 2; ++du)
                for (int dw = 0; dw < 2; ++dw) {
                    glm::ivec3 c;
                    c[a] = base;
                    c[u] = p[u] + du;
                    c[w] = p[w] + dw;
                    lattice.insert({c.x, c.y, c.z, d});
                }
        }
    }
    auto bodySolid = [&](glm::ivec3 c) { return g.in(c) && g.mat[g.idx(c)] != 0; };
    for (const auto& [x, y, z, d] : lattice) {
        s.corners.push_back((insetCorner(glm::ivec3(x, y, z), bodySolid) - comLocal) * vm);
        s.cornerDir.push_back(uint8_t(d));
    }
    m_bodies.push_back(std::move(s));
    return m_bodies.size() - 1;
}

namespace {

struct Frame {
    glm::dvec3 pos;
    glm::dquat q;
    glm::dmat3 R, Rt;
};

Frame frameOf(const SimBody& b) {
    Frame f{b.pos, b.quat, glm::mat3_cast(b.quat), glm::dmat3(1.0)};
    f.Rt = glm::transpose(f.R);
    return f;
}

GridView viewOf(const SimBody& b) {
    GridView v;
    v.g = &b.src.grid;
    v.comLocal = b.src.com - glm::dvec3(b.src.origin);
    v.vm = b.src.grid.voxelM;
    return v;
}

// the body's world AABB from its corners
void aabbOf(const SimBody& b, const Frame& f, glm::dvec3& lo, glm::dvec3& hi) {
    lo = glm::dvec3(kInf);
    hi = glm::dvec3(-kInf);
    for (const glm::dvec3& c : b.corners) {
        const glm::dvec3 w = f.pos + f.R * c;
        lo = glm::min(lo, w);
        hi = glm::max(hi, w);
    }
}

// Candidate contacts of one dynamic body A against the world (static), or against body B.
// A's corners into the other's grid, and the other's surface corners into A's grid.
struct Detector {
    const VoxelGrid& world;
    const std::vector<FractureMaterial>& mats;
    const BodySimParams& p;
    BodySimStats* stats;
    double groundMu = 0.8;
    bool sweepFast = false;   // F5.5: the tick's detection sweeps fast corners (initial pose valid); measuring does not

    double muOf(const GridView& v, glm::ivec3 cell) const {
        const uint8_t m = v.mat(cell);
        return m ? mats[m].friction : groundMu;
    }

    void againstWorld(const SimBody& A, std::vector<Cand>& out, double* maxPen) const {
        const Frame fa = frameOf(A);
        GridView wv;
        wv.g = &world;
        wv.ground = p.groundBelow;
        wv.vm = world.voxelM;
        const double vm = world.voxelM;
        const double mVox = p.margin / vm;
        // A's corners into the world
        for (size_t k = 0; k < A.corners.size(); ++k) {
            const glm::dvec3 w = fa.pos + fa.R * A.corners[k];
            const glm::dvec3 prefer = -(fa.R * glm::dvec3(kDir[A.cornerDir[k]]));
            Escape e = escape(wv, w / vm, mVox, prefer);
            if (sweepFast) {   // F5.5: a fast corner's contact comes from its path (see sweep())
                const glm::dvec3 w0 = A.initial + glm::mat3_cast(A.initialQuat) * A.corners[k];
                const glm::dvec3 d = w - w0;
                if (glm::length(d) >= vm) {
                    Escape s;
                    const glm::dvec3 u0 = w0 / vm;
                    if (!wv.solid(glm::ivec3(int(std::floor(u0.x)), int(std::floor(u0.y)), int(std::floor(u0.z))))) {
                        s = sweep(wv, u0, w / vm);
                    } else {
                        // it started the tick resting a hair inside a surface (run 1: a piece resting 1.4 mm into a
                        // 1-voxel slab moved 0.37 m down through it): that surface, deepened by the move into it
                        const Escape e0 = escape(wv, u0, mVox, prefer);
                        if (e0.status == 1 && e0.pen > 0.0 && glm::dot(d, e0.n) < 0.0) {
                            s = e0;
                            s.pen = e0.pen - glm::dot(d, e0.n);
                        }
                    }
                    // the path wins over the end point's escape (run 1: a corner that crossed a slab and ended a hair
                    // inside its far face escaped through that face, and pushed the piece on through)
                    if (s.status == 1) e = s;
                }
            }
            if (e.status == 2) { if (stats) ++stats->deepPoints; continue; }
            if (e.status != 1) continue;
            Cand c;
            c.n = e.n;
            c.pen = e.pen;
            c.rA = A.corners[k];
            c.rB = w;
            c.point = w;
            c.mu = 0.5 * (A.friction + muOf(wv, e.cell));
            c.bucket = e.dir;
            c.feature = (uint64_t(k) << 5) | uint64_t(e.dir);
            if (maxPen) *maxPen = std::max(*maxPen, c.pen);
            out.push_back(c);
        }
        // the world's surface corners near A into A's grid
        glm::dvec3 lo, hi;
        aabbOf(A, fa, lo, hi);
        lo -= p.margin;
        hi += p.margin;
        const glm::ivec3 cl(int(std::floor(lo.x / vm)) - 1, std::max(int(std::floor(lo.y / vm)) - 1, -1),
                            int(std::floor(lo.z / vm)) - 1);
        const glm::ivec3 ch(int(std::floor(hi.x / vm)) + 1, int(std::floor(hi.y / vm)) + 1,
                            int(std::floor(hi.z / vm)) + 1);
        if (ch.y < -1) return;
        const GridView av = viewOf(A);
        const double aVm = av.vm;
        std::set<std::tuple<int, int, int, int>> seen;
        for (int x = cl.x; x <= ch.x; ++x)
            for (int y = cl.y; y <= ch.y; ++y)
                for (int z = cl.z; z <= ch.z; ++z) {
                    const glm::ivec3 cell(x, y, z);
                    if (!wv.solid(cell)) continue;
                    for (int d = 0; d < 6; ++d) {
                        if (wv.solid(cell + kDir[d])) continue;
                        const int a = d / 2;
                        const int base = kDir[d][a] > 0 ? cell[a] + 1 : cell[a];
                        const int u = (a + 1) % 3, ww = (a + 2) % 3;
                        for (int du = 0; du < 2; ++du)
                            for (int dw = 0; dw < 2; ++dw) {
                                glm::ivec3 L;
                                L[a] = base;
                                L[u] = cell[u] + du;
                                L[ww] = cell[ww] + dw;
                                if (!seen.insert({L.x, L.y, L.z, d}).second) continue;
                                const glm::dvec3 w =
                                    insetCorner(L, [&](glm::ivec3 q) { return wv.solid(q); }) * vm;
                                if (w.x < lo.x || w.y < lo.y || w.z < lo.z || w.x > hi.x || w.y > hi.y || w.z > hi.z)
                                    continue;
                                const glm::dvec3 local = fa.Rt * (w - fa.pos);
                                const glm::dvec3 prefer = fa.Rt * -glm::dvec3(kDir[d]);
                                const Escape e = escape(av, local / aVm + av.comLocal, p.margin / aVm, prefer);
                                if (e.status == 2) { if (stats) ++stats->deepPoints; continue; }
                                if (e.status != 1) continue;
                                Cand c;
                                c.n = -(fa.R * e.n);
                                c.pen = e.pen;
                                c.rA = local;
                                c.rB = w;
                                c.point = w;
                                c.mu = 0.5 * (A.friction + muOf(wv, cell));
                                c.bucket = 26 + e.dir;
                                const uint64_t lk = mix64((uint64_t(uint32_t(L.x)) << 42) ^
                                                          (uint64_t(uint32_t(L.y)) << 21) ^ uint64_t(uint32_t(L.z)));
                                c.feature = (1ull << 63) | ((lk & ((1ull << 54) - 1)) << 8) | (uint64_t(d) << 5) |
                                            uint64_t(e.dir);
                                if (maxPen) *maxPen = std::max(*maxPen, c.pen);
                                out.push_back(c);
                            }
                    }
                }
    }

    // A's corners into B and B's corners into A; constraints are written with A = a, B = b
    void againstBody(const SimBody& A, const SimBody& B, std::vector<Cand>& out, double* maxPen) const {
        const Frame fa = frameOf(A), fb = frameOf(B);
        const GridView av = viewOf(A), bv = viewOf(B);
        const double mu = 0.5 * (A.friction + B.friction);
        for (int side = 0; side < 2; ++side) {
            const SimBody& X = side ? B : A;       // the corners' owner
            const Frame& fx = side ? fb : fa;
            const Frame& fy = side ? fa : fb;
            const GridView& yv = side ? av : bv;
            for (size_t k = 0; k < X.corners.size(); ++k) {
                const glm::dvec3 w = fx.pos + fx.R * X.corners[k];
                const glm::dvec3 local = fy.Rt * (w - fy.pos);
                const glm::dvec3 prefer = fy.Rt * -(fx.R * glm::dvec3(kDir[X.cornerDir[k]]));
                const Escape e = escape(yv, local / yv.vm + yv.comLocal, p.margin / yv.vm, prefer);
                if (e.status == 2) { if (stats) ++stats->deepPoints; continue; }
                if (e.status != 1) continue;
                Cand c;
                const glm::dvec3 nOut = fy.R * e.n;   // out of Y
                c.n = side ? -nOut : nOut;                                  // pushes A out of B
                c.pen = e.pen;
                c.rA = side ? local : X.corners[k];
                c.rB = side ? X.corners[k] : local;
                c.point = w;
                c.mu = mu;
                c.bucket = side * 26 + e.dir;
                c.feature = (uint64_t(side) << 62) | (uint64_t(k) << 5) | uint64_t(e.dir);
                if (maxPen) *maxPen = std::max(*maxPen, c.pen);
                out.push_back(c);
            }
        }
    }
};

// the cap: the deepest per bucket (by depth), then farthest-point fill; returns the kept indices in a stable order.
// Persistence (F5.2a run-1 fix): a candidate that was a contact last tick (warm) wins near-ties -- 1 mm of depth in
// its bucket, 2 cm of spread in the fill -- so the kept set stays put and the warm-started forces survive (without it
// a 5-box stack sank 0.58 m; uncapped it held to 0.5 mm).
constexpr double kWarmDepthBonus = 0.001, kWarmSpreadBonus = 0.02;

template <class IsWarm>
std::vector<size_t> capContacts(const std::vector<Cand>& c, int cap, const IsWarm& isWarm) {
    std::vector<size_t> keep;
    if (c.empty()) return keep;
    if (int(c.size()) <= cap) {
        for (size_t i = 0; i < c.size(); ++i) keep.push_back(i);
        return keep;
    }
    std::vector<char> warm(c.size(), 0);
    for (size_t i = 0; i < c.size(); ++i) warm[i] = isWarm(c[i].feature) ? 1 : 0;
    std::vector<size_t> winners;
    for (int b = 0; b < 52; ++b) {
        size_t best = SIZE_MAX;
        double bestScore = -kInf;
        for (size_t i = 0; i < c.size(); ++i) {
            if (c[i].bucket != b) continue;
            const double score = c[i].pen + (warm[i] ? kWarmDepthBonus : 0.0);
            if (score > bestScore) { bestScore = score; best = i; }
        }
        if (best != SIZE_MAX) winners.push_back(best);
    }
    std::stable_sort(winners.begin(), winners.end(), [&](size_t a, size_t b) { return c[a].pen > c[b].pen; });
    std::vector<char> used(c.size(), 0);
    for (size_t w : winners) {
        if (int(keep.size()) >= cap) break;
        keep.push_back(w);
        used[w] = 1;
    }
    while (int(keep.size()) < cap) {
        double bestScore = -1.0;
        size_t best = SIZE_MAX;
        for (size_t i = 0; i < c.size(); ++i) {
            if (used[i]) continue;
            double dmin = kInf;
            for (size_t k : keep) dmin = std::min(dmin, glm::length(c[i].point - c[k].point));
            const double score = dmin + (warm[i] ? kWarmSpreadBonus : 0.0);
            if (score > bestScore) { bestScore = score; best = i; }
        }
        if (best == SIZE_MAX) break;
        keep.push_back(best);
        used[best] = 1;
    }
    return keep;
}

}  // namespace

void BodySimRef::step() {
    const auto t0 = std::chrono::steady_clock::now();
    const BodySimParams& P = m_p;
    const double dt = P.dt, dt2 = dt * dt;
    m_stats = {};
    const size_t nb = m_bodies.size();
    auto dynamic = [&](size_t i) { return i != kStatic && !m_bodies[i].asleep && m_bodies[i].invMass > 0.0; };

    // 1. integrate: the inertial target and the starting iterate
    std::vector<double> radius(nb, 0.0);
    for (size_t i = 0; i < nb; ++i) {
        SimBody& b = m_bodies[i];
        for (const glm::dvec3& c : b.corners) radius[i] = std::max(radius[i], glm::length(c));
        b.initial = b.pos;
        b.initialQuat = b.quat;
        b.cumAng = glm::dvec3(0.0);
        if (!dynamic(i)) { b.inertial = b.pos; b.inertialQuat = b.quat; continue; }
        const double prevSpeed = glm::length(b.vel) + glm::length(b.angVel) * radius[i];
        b.vel += P.gravity * dt;
        b.inertial = b.pos + b.vel * dt;
        b.inertialQuat = quatIntegrate(b.quat, b.angVel * dt);
        if (prevSpeed < P.restSpeed) continue;   // start at rest: solve from where it is
        b.pos = b.inertial;
        b.quat = b.inertialQuat;
        b.cumAng = angDisp(b.inertialQuat, b.initialQuat);
    }

    // 2. contacts at the predicted iterate
    m_cons.clear();
    Detector det{m_world, m_mats, P, &m_stats};
    det.sweepFast = true;
    std::vector<char> wake(nb, 0);
    auto emit = [&](size_t a, size_t b, std::vector<Cand>& cands) {
        m_stats.candidates += cands.size();
        const std::vector<size_t> keep = capContacts(
            cands, P.contactCap, [&](uint64_t f) { return m_warm.count(keyOf(a, b, f)) != 0; });
        m_stats.capped += cands.size() - keep.size();
        SimBody& A = m_bodies[a];
        const Frame fa = frameOf(A);
        for (size_t ki : keep) {
            const Cand& c = cands[ki];
            Constraint k;
            k.a = a;
            k.b = b;
            k.n = c.n;
            tangentBasis(c.n, k.t1, k.t2);
            k.rA = c.rA;
            k.rB = c.rB;
            k.mu = c.mu;
            k.key = keyOf(a, b, c.feature);
            k.feature = c.feature;
            m_stats.maxPenetration = std::max(m_stats.maxPenetration, c.pen);
            // convert the depth found at the predicted iterate back to tick start: pen0 = pen + J dq
            const glm::dvec3 rAW = fa.R * c.rA;
            double jdq = glm::dot(c.n, A.pos - A.initial) + glm::dot(glm::cross(rAW, c.n), A.cumAng);
            if (b != kStatic) {
                const SimBody& B = m_bodies[b];
                const glm::dvec3 rBW = glm::mat3_cast(B.quat) * c.rB;
                jdq += -glm::dot(c.n, B.pos - B.initial) + glm::dot(glm::cross(rBW, -c.n), B.cumAng);
            }
            k.cInitN = c.pen + jdq;
            const auto it = m_warm.find(k.key);
            const bool cold = it == m_warm.end();
            // a new contact's stiffness: the engine's m / dt^2 -- except an IMPACT between two moving bodies (closing
            // faster than impactSpeed at the contact) starts soft on the pair's reduced mass (F5.2a run-1 lever:
            // Gauss-Seidel hands the first-solved body the whole push when a new contact starts stiff -- head-on at
            // 8 iterations the momentum error was 35 % of m v at 1 x m / dt^2, 1 % at 0.3 x, 0.14 % at 0.1 x; but a
            // soft start for RESTING contacts let a flush 5-stack sink 10 mm, so resting contacts keep the stiff start)
            double coldPen = P.coldScale * A.src.mass / dt2;
            if (b != kStatic && !m_bodies[b].asleep) {
                const SimBody& B = m_bodies[b];
                const glm::dvec3 rBW = glm::mat3_cast(B.quat) * c.rB;
                const glm::dvec3 vRel = (A.vel + glm::cross(A.angVel, rAW)) - (B.vel + glm::cross(B.angVel, rBW));
                if (-glm::dot(vRel, c.n) > P.impactSpeed) {
                    const double mB = B.src.mass;
                    coldPen = P.coldScaleBody * (A.src.mass * mB / (A.src.mass + mB)) / dt2;
                }
            }
            if (cold) {
                k.penN = std::clamp(coldPen, P.penaltyMin, P.penaltyMax);
                k.penT1 = k.penT2 = k.penN;
            } else {
                const Warm& w = it->second;
                k.lambdaN = w.lambdaN * P.alpha;
                k.lambdaT1 = w.lambdaT1 * P.alpha;
                k.lambdaT2 = w.lambdaT2 * P.alpha;
                k.penN = std::clamp(w.penN * P.gamma, P.penaltyMin, P.penaltyMax);
                k.penT1 = std::clamp(w.penT1 * P.gamma, P.penaltyMin, P.penaltyMax);
                k.penT2 = std::clamp(w.penT2 * P.gamma, P.penaltyMin, P.penaltyMax);
                k.stick = w.stick;
            }
            if (b == kStatic) {   // static friction anchor (the engine's SOLVER_FLAG_STATIC_FRICTION)
                const glm::dvec3 ptStart = A.initial + glm::mat3_cast(A.initialQuat) * c.rA;
                k.anchor = (!cold && it->second.stick) ? it->second.anchor : ptStart;
                k.cInitT1 = glm::dot(k.t1, ptStart - k.anchor);
                k.cInitT2 = glm::dot(k.t2, ptStart - k.anchor);
            }
            m_cons.push_back(k);
        }
    };
    std::vector<Cand> cands;
    for (size_t i = 0; i < nb; ++i) {
        if (!dynamic(i)) continue;
        cands.clear();
        det.againstWorld(m_bodies[i], cands, nullptr);
        emit(i, kStatic, cands);
    }
    {
        std::vector<glm::dvec3> lo(nb), hi(nb);
        for (size_t i = 0; i < nb; ++i) aabbOf(m_bodies[i], frameOf(m_bodies[i]), lo[i], hi[i]);
        for (size_t i = 0; i < nb; ++i)
            for (size_t j = i + 1; j < nb; ++j) {
                if (!dynamic(i) && !dynamic(j)) continue;
                bool overlap = true;
                for (int a = 0; a < 3; ++a)
                    overlap &= lo[i][a] <= hi[j][a] + P.margin && lo[j][a] <= hi[i][a] + P.margin;
                if (!overlap) continue;
                ++m_stats.pairsTested;
                cands.clear();
                // the dynamic body is A (a sleeper is the static-like side B)
                const size_t a = dynamic(i) ? i : j, b = a == i ? j : i;
                det.againstBody(m_bodies[a], m_bodies[b], cands, nullptr);
                if (cands.empty()) continue;
                if (m_bodies[b].asleep && glm::length(m_bodies[a].vel) > P.wakeSpeed) wake[b] = 1;
                emit(a, b, cands);
            }
    }
    m_stats.contacts = m_cons.size();

    // 3. colours (greedy, ascending body index) over the dynamic-dynamic constraint graph
    std::vector<std::vector<size_t>> adj(nb), consOf(nb);
    for (size_t k = 0; k < m_cons.size(); ++k) {
        const Constraint& c = m_cons[k];
        consOf[c.a].push_back(k);
        if (c.b != kStatic) {
            consOf[c.b].push_back(k);
            if (dynamic(c.a) && dynamic(c.b)) { adj[c.a].push_back(c.b); adj[c.b].push_back(c.a); }
        }
    }
    std::vector<int> colour(nb, -1);
    int nColours = 0;
    for (size_t i = 0; i < nb; ++i) {
        if (!dynamic(i)) continue;
        std::vector<char> usedC(size_t(nColours) + 1, 0);
        for (size_t j : adj[i])
            if (colour[j] >= 0) usedC[size_t(colour[j])] = 1;
        int c = 0;
        while (usedC[size_t(c)]) ++c;
        colour[i] = c;
        nColours = std::max(nColours, c + 1);
    }

    // constraint value at the current iterate
    auto evalC = [&](const Constraint& k, double& Cn, double& Ct1, double& Ct2, glm::dvec3& rAW, glm::dvec3& rBW) {
        const SimBody& A = m_bodies[k.a];
        rAW = glm::mat3_cast(A.quat) * k.rA;
        glm::dvec3 dB(0.0), aB(0.0);
        rBW = glm::dvec3(0.0);
        if (k.b != kStatic) {
            const SimBody& B = m_bodies[k.b];
            dB = B.pos - B.initial;
            aB = B.cumAng;
            rBW = glm::mat3_cast(B.quat) * k.rB;
        }
        const glm::dvec3 dA = A.pos - A.initial;
        const double c0 = k.cInitN > 0.0 ? (1.0 - P.alpha) * k.cInitN : k.cInitN;
        Cn = -c0 + glm::dot(k.n, dA - dB) + glm::dot(glm::cross(rAW, k.n), A.cumAng) +
             glm::dot(glm::cross(rBW, -k.n), aB);
        Ct1 = (1.0 - P.alpha) * k.cInitT1 + glm::dot(k.t1, dA - dB) + glm::dot(glm::cross(rAW, k.t1), A.cumAng) +
              glm::dot(glm::cross(rBW, -k.t1), aB);
        Ct2 = (1.0 - P.alpha) * k.cInitT2 + glm::dot(k.t2, dA - dB) + glm::dot(glm::cross(rAW, k.t2), A.cumAng) +
              glm::dot(glm::cross(rBW, -k.t2), aB);
    };

    // 4. iterations: primal per colour (Gauss-Seidel, ascending index), then dual per constraint
    for (int it = 0; it < P.iterations; ++it) {
        for (int col = 0; col < nColours; ++col)
            for (size_t i = 0; i < nb; ++i) {
                if (colour[i] != col) continue;
                SimBody& b = m_bodies[i];
                const glm::dmat3 R = glm::mat3_cast(b.quat);
                const glm::dmat3 Iw = R * b.inertiaBody * glm::transpose(R) / dt2;
                const double mdt2 = b.src.mass / dt2;
                double lhs[6][6] = {};
                double rhs[6];
                for (int a = 0; a < 3; ++a) lhs[a][a] = mdt2;
                for (int r = 0; r < 3; ++r)
                    for (int c = 0; c < 3; ++c) lhs[3 + r][3 + c] = Iw[c][r];
                const glm::dvec3 dp = b.pos - b.inertial;
                const glm::dvec3 da = Iw * angDisp(b.quat, b.inertialQuat);
                for (int a = 0; a < 3; ++a) { rhs[a] = mdt2 * dp[a]; rhs[3 + a] = da[a]; }
                for (size_t ki : consOf[i]) {
                    const Constraint& k = m_cons[ki];
                    double Cn, Ct1, Ct2;
                    glm::dvec3 rAW, rBW;
                    evalC(k, Cn, Ct1, Ct2, rAW, rBW);
                    const double fn = std::min(k.penN * Cn + k.lambdaN, 0.0);
                    double ft1 = k.penT1 * Ct1 + k.lambdaT1, ft2 = k.penT2 * Ct2 + k.lambdaT2;
                    const double bound = std::abs(fn) * k.mu, fs = std::sqrt(ft1 * ft1 + ft2 * ft2);
                    if (fs > bound && fs > 0.0) { ft1 *= bound / fs; ft2 *= bound / fs; }
                    const bool isA = k.a == i;
                    const double sJ = isA ? 1.0 : -1.0;
                    const glm::dvec3 arm = isA ? rAW : rBW;
                    const glm::dvec3 dirs[3] = {k.n, k.t1, k.t2};
                    const double F[3] = {fn, ft1, ft2}, K[3] = {k.penN, k.penT1, k.penT2};
                    for (int r = 0; r < 3; ++r) {
                        const glm::dvec3 jl = dirs[r] * sJ, ja = glm::cross(arm, jl);
                        const double J[6] = {jl.x, jl.y, jl.z, ja.x, ja.y, ja.z};
                        for (int p = 0; p < 6; ++p) {
                            rhs[p] += J[p] * F[r];
                            for (int q = 0; q < 6; ++q) lhs[p][q] += K[r] * J[p] * J[q];
                        }
                    }
                }
                for (double& v : rhs) v = -v;
                double d[6];
                solve6(lhs, rhs, d);
                const glm::dvec3 dAng(d[3], d[4], d[5]);
                b.pos += glm::dvec3(d[0], d[1], d[2]);
                b.quat = quatIntegrate(b.quat, dAng);
                b.cumAng += dAng;
            }
        for (Constraint& k : m_cons) {
            double Cn, Ct1, Ct2;
            glm::dvec3 rAW, rBW;
            evalC(k, Cn, Ct1, Ct2, rAW, rBW);
            const double ln = std::min(k.penN * Cn + k.lambdaN, 0.0);
            if (ln < 0.0) k.penN = std::min(k.penN + P.beta * std::abs(Cn), P.penaltyMax);
            const double bound = std::abs(ln) * k.mu;
            double ft1 = k.penT1 * Ct1 + k.lambdaT1, ft2 = k.penT2 * Ct2 + k.lambdaT2;
            const double fs = std::sqrt(ft1 * ft1 + ft2 * ft2);
            if (fs > bound && fs > 0.0) { ft1 *= bound / fs; ft2 *= bound / fs; }
            if (fs <= bound) {
                if (std::abs(Ct1) > 1e-5) k.penT1 = std::min(k.penT1 + P.beta * std::abs(Ct1), P.penaltyMax);
                if (std::abs(Ct2) > 1e-5) k.penT2 = std::min(k.penT2 + P.beta * std::abs(Ct2), P.penaltyMax);
            }
            k.lambdaN = ln;
            k.lambdaT1 = ft1;
            k.lambdaT2 = ft2;
            k.stick = fs <= bound;
        }
    }

    // 4b. hard contact (the engine's solver_hardcontact.comp, extended to body-body): AVBD corrects only 1 - alpha =
    // 1 % of an existing overlap per tick, so overlap beyond kHardContactM is projected out positionally; the push
    // moves `initial` too (velocity-neutral) and drops the tick's motion INTO the surface. Body-body: split by inverse
    // mass (a sleeper does not move). F5.2a run-1 fix: without it a body-body overlap that built up during an impact
    // stayed (R7: 27-37 mm, never resolved).
    {
        auto pushOut = [&](SimBody& b, const glm::dvec3& n, double d) {
            glm::dvec3 disp = b.pos - b.initial;
            const double vn = glm::dot(disp, n);
            if (vn < 0.0) disp -= n * vn;
            b.pos += n * d;
            b.initial = b.pos - disp;
        };
        auto deepest = [](const std::vector<Cand>& cs, glm::dvec3& n) {
            double best = 0.0;
            for (const Cand& c : cs)
                if (c.pen > best) { best = c.pen; n = c.n; }
            return best;
        };
        std::vector<Cand> hc;
        std::vector<glm::dvec3> lo(nb), hi(nb);
        for (size_t i = 0; i < nb; ++i) aabbOf(m_bodies[i], frameOf(m_bodies[i]), lo[i], hi[i]);
        for (size_t i = 0; i < nb; ++i)
            for (size_t j = i + 1; j < nb; ++j) {
                if (!dynamic(i) && !dynamic(j)) continue;
                bool overlap = true;
                for (int a = 0; a < 3; ++a) overlap &= lo[i][a] <= hi[j][a] && lo[j][a] <= hi[i][a];
                if (!overlap) continue;
                const size_t a = dynamic(i) ? i : j, b = a == i ? j : i;
                hc.clear();
                det.againstBody(m_bodies[a], m_bodies[b], hc, nullptr);
                glm::dvec3 n(0.0);
                const double d = deepest(hc, n);
                if (d <= P.hardContact) continue;
                const double ia = m_bodies[a].invMass, ib = dynamic(b) ? m_bodies[b].invMass : 0.0;
                pushOut(m_bodies[a], n, d * ia / (ia + ib));
                if (ib > 0.0) pushOut(m_bodies[b], -n, d * ib / (ia + ib));
                ++m_stats.hardFires;
            }
        // the world last: it is authoritative (F5.2a: a body-body push after the world pass left a slow piece
        // 16.5 mm inside the wall, and it fell asleep there)
        for (size_t i = 0; i < nb; ++i) {
            if (!dynamic(i)) continue;
            hc.clear();
            det.againstWorld(m_bodies[i], hc, nullptr);
            glm::dvec3 n(0.0);
            const double d = deepest(hc, n);
            if (d > P.hardContact) { pushOut(m_bodies[i], n, d); ++m_stats.hardFires; }
        }
    }

    // 5. velocities, warm start, sleep
    for (size_t i = 0; i < nb; ++i) {
        SimBody& b = m_bodies[i];
        if (!dynamic(i)) continue;
        b.vel = (b.pos - b.initial) / dt;
        b.angVel = angDisp(b.quat, b.initialQuat) / dt;
    }
    m_warm.clear();
    for (const Constraint& k : m_cons)
        m_warm[k.key] = {k.lambdaN, k.lambdaT1, k.lambdaT2, k.penN, k.penT1, k.penT2, k.anchor, k.stick};
    for (size_t i = 0; i < nb; ++i) {
        SimBody& b = m_bodies[i];
        if (wake[i]) { b.asleep = false; b.slowTicks = 0; b.asleepTicks = 0; }
        if (b.asleep) {
            ++b.asleepTicks;
            continue;
        }
        const bool slow = glm::length(b.vel) < P.sleepSpeed && glm::length(b.angVel) * radius[i] < P.sleepSpeed;
        b.slowTicks = slow ? b.slowTicks + 1 : 0;
        if (b.slowTicks >= P.sleepTicks) {
            b.asleep = true;
            b.asleepTicks = 0;
            b.vel = glm::dvec3(0.0);
            b.angVel = glm::dvec3(0.0);
        }
        ++m_stats.awake;
    }
    m_stats.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

double BodySimRef::currentMaxPenetration() const {
    Detector det{m_world, m_mats, m_p, nullptr};
    double worst = 0.0;
    std::vector<Cand> c;
    for (size_t i = 0; i < m_bodies.size(); ++i) {
        c.clear();
        det.againstWorld(m_bodies[i], c, &worst);
        for (size_t j = i + 1; j < m_bodies.size(); ++j) {
            c.clear();
            det.againstBody(m_bodies[i], m_bodies[j], c, &worst);
        }
    }
    return worst;
}

std::string BodySimRef::worstPenetrationReport() const {
    Detector det{m_world, m_mats, m_p, nullptr};
    double worst = 0.0;
    std::string out = "none";
    std::vector<Cand> c;
    auto scan = [&](size_t i, size_t j) {
        for (const Cand& k : c) {
            if (k.pen <= worst) continue;
            worst = k.pen;
            char buf[400];
            std::snprintf(buf, sizeof buf,
                          "%.2f mm: body %zu (%u voxels, %s) vs %s%s; n (%.3f %.3f %.3f) rA (%.3f %.3f %.3f) point "
                          "(%.3f %.3f %.3f) bucket %d",
                          k.pen * 1000, i, m_bodies[i].src.voxels, m_bodies[i].asleep ? "asleep" : "awake",
                          j == kStatic ? "world" : "body ", j == kStatic ? "" : std::to_string(j).c_str(), k.n.x, k.n.y,
                          k.n.z, k.rA.x, k.rA.y, k.rA.z, k.point.x, k.point.y, k.point.z, k.bucket);
            out = buf;
        }
    };
    for (size_t i = 0; i < m_bodies.size(); ++i) {
        c.clear();
        det.againstWorld(m_bodies[i], c, nullptr);
        scan(i, kStatic);
        for (size_t j = i + 1; j < m_bodies.size(); ++j) {
            c.clear();
            det.againstBody(m_bodies[i], m_bodies[j], c, nullptr);
            scan(i, j);
        }
    }
    return out;
}

double BodySimRef::lowestCornerY() const {
    double y = kInf;
    for (const SimBody& b : m_bodies) {
        const glm::dmat3 R = glm::mat3_cast(b.quat);
        for (const glm::dvec3& c : b.corners) y = std::min(y, (b.pos + R * c).y);
    }
    return y;
}

}  // namespace Phyxel::Core::VoxFound
