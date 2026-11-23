// docs/VoxelFoundationPlan.md §6.7 "Rest" + D6 -- F5.4 the rest / bake-in hybrid (CPU reference).
// Prediction: docs/evidence/voxfound_f54_prediction.txt.
#include "core/voxfound/BodyRest.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <tuple>
#include <unordered_set>

#include <glm/gtc/quaternion.hpp>

namespace Phyxel::Core::VoxFound {

namespace {

struct AxisRot {
    int m[3][3];  // row r, column c
    glm::ivec3 apply(glm::ivec3 v) const {
        return {m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z, m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
                m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z};
    }
    glm::dvec3 apply(glm::dvec3 v) const {
        return {m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z, m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
                m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z};
    }
};

// the 24 rotations that map axes to axes (signed permutations with determinant +1)
const std::vector<AxisRot>& axisRotations() {
    static const std::vector<AxisRot> rots = [] {
        std::vector<AxisRot> out;
        const int perms[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
        for (const auto& pm : perms)
            for (int sgn = 0; sgn < 8; ++sgn) {
                AxisRot r{};
                for (int row = 0; row < 3; ++row) r.m[row][pm[row]] = (sgn >> row & 1) ? -1 : 1;
                const int det = r.m[0][0] * (r.m[1][1] * r.m[2][2] - r.m[1][2] * r.m[2][1]) -
                                r.m[0][1] * (r.m[1][0] * r.m[2][2] - r.m[1][2] * r.m[2][0]) +
                                r.m[0][2] * (r.m[1][0] * r.m[2][1] - r.m[1][1] * r.m[2][0]);
                if (det == 1) out.push_back(r);
            }
        return out;
    }();
    return rots;
}

glm::dvec3 comLocalM(const SimBody& b) {
    return (b.src.com - glm::dvec3(b.src.origin)) * double(b.src.grid.voxelM);
}

// world AABB of the body's grid box at its pose (metres)
void bodyAabb(const SimBody& b, glm::dvec3& lo, glm::dvec3& hi) {
    const double vm = b.src.grid.voxelM;
    const glm::dvec3 c = comLocalM(b), d = glm::dvec3(b.src.grid.dim) * vm;
    lo = glm::dvec3(1e300);
    hi = glm::dvec3(-1e300);
    for (int k = 0; k < 8; ++k) {
        const glm::dvec3 corner((k & 1) ? d.x : 0.0, (k & 2) ? d.y : 0.0, (k & 4) ? d.z : 0.0);
        const glm::dvec3 w = b.pos + b.quat * (corner - c);
        lo = glm::min(lo, w);
        hi = glm::max(hi, w);
    }
}

// bound: every point of the body is within this of its centre of mass
double bodyRadius(const SimBody& b) {
    const double vm = b.src.grid.voxelM;
    const glm::dvec3 c = comLocalM(b), d = glm::dvec3(b.src.grid.dim) * vm;
    return glm::length(glm::max(c, d - c));
}

bool sphereHitsBox(glm::dvec3 c, double r, glm::dvec3 lo, glm::dvec3 hi) {
    const glm::dvec3 q = glm::clamp(c, lo, hi);
    return glm::dot(q - c, q - c) <= r * r;
}

bool boxesTouch(glm::dvec3 alo, glm::dvec3 ahi, glm::dvec3 blo, glm::dvec3 bhi) {
    return alo.x <= bhi.x && blo.x <= ahi.x && alo.y <= bhi.y && blo.y <= ahi.y && alo.z <= bhi.z && blo.z <= ahi.z;
}

// is the point (metres) inside a solid voxel of body b
bool insideBody(const SimBody& b, glm::dvec3 x) {
    const double vm = b.src.grid.voxelM;
    const glm::dvec3 local = glm::conjugate(b.quat) * (x - b.pos) + comLocalM(b);
    const glm::ivec3 v(glm::floor(local / vm));
    return b.src.grid.in(v) && b.src.grid.mat[b.src.grid.idx(v)] != 0;
}

// F5.5c: one pass's broadphase -- the bodies' bounding spheres binned on a uniform grid (poses do not change within a
// pass). query() calls f(j) once for every body whose sphere's box overlaps [a, b] (a superset of the bodies whose
// sphere hits the box; callers keep their own exact test).
struct SphereGrid {
    double cell = 1.0;
    glm::dvec3 lo{0.0};
    glm::ivec3 dim{0};
    std::vector<uint32_t> start, items;   // bin b holds items[start[b] .. start[b + 1])
    std::vector<glm::ivec3> binLo, binHi;
    mutable std::vector<uint32_t> stamp;
    mutable uint32_t query_ = 0;
    glm::ivec3 binOf(glm::dvec3 x) const {
        return glm::clamp(glm::ivec3(glm::floor((x - lo) / cell)), glm::ivec3(0), dim - 1);
    }
    void build(const std::vector<SimBody>& bodies) {
        start.clear();
        items.clear();
        stamp.assign(bodies.size(), 0);
        query_ = 0;
        if (bodies.empty()) return;
        glm::dvec3 hi(-1e300);
        lo = glm::dvec3(1e300);
        std::vector<double> r(bodies.size());
        for (size_t j = 0; j < bodies.size(); ++j) {
            r[j] = bodyRadius(bodies[j]);
            lo = glm::min(lo, bodies[j].pos - r[j]);
            hi = glm::max(hi, bodies[j].pos + r[j]);
        }
        cell = 1.0;
        const glm::dvec3 ext = hi - lo;
        while (ext.x / cell > 64.0 || ext.y / cell > 64.0 || ext.z / cell > 64.0) cell *= 2.0;   // <= 65^3 bins
        dim = glm::ivec3(glm::floor(ext / cell)) + 1;
        const size_t nb = size_t(dim.x) * dim.y * dim.z;
        start.assign(nb + 1, 0);
        binLo.resize(bodies.size());
        binHi.resize(bodies.size());
        auto each = [&](size_t j, auto f) {
            for (int x = binLo[j].x; x <= binHi[j].x; ++x)
                for (int y = binLo[j].y; y <= binHi[j].y; ++y)
                    for (int z = binLo[j].z; z <= binHi[j].z; ++z) f((size_t(x) * dim.y + y) * dim.z + z);
        };
        for (size_t j = 0; j < bodies.size(); ++j) {   // a counting sort: sizes, offsets, then the items
            binLo[j] = binOf(bodies[j].pos - r[j]);
            binHi[j] = binOf(bodies[j].pos + r[j]);
            each(j, [&](size_t b) { ++start[b + 1]; });
        }
        for (size_t b = 0; b < nb; ++b) start[b + 1] += start[b];
        items.resize(start[nb]);
        std::vector<uint32_t> fill(start.begin(), start.end() - 1);
        for (size_t j = 0; j < bodies.size(); ++j) each(j, [&](size_t b) { items[fill[b]++] = uint32_t(j); });
    }
    template <class F>
    void query(glm::dvec3 qa, glm::dvec3 qb, F f) const {
        if (start.empty()) return;
        if (++query_ == 0) {
            std::fill(stamp.begin(), stamp.end(), 0u);
            query_ = 1;
        }
        const glm::ivec3 a = binOf(qa), b = binOf(qb);
        for (int x = a.x; x <= b.x; ++x)
            for (int y = a.y; y <= b.y; ++y)
                for (int z = a.z; z <= b.z; ++z)
                    for (uint32_t k = start[(size_t(x) * dim.y + y) * dim.z + z],
                                  e = start[(size_t(x) * dim.y + y) * dim.z + z + 1]; k < e; ++k) {
                        const uint32_t j = items[k];
                        if (stamp[j] == query_) continue;
                        stamp[j] = query_;
                        f(size_t(j));
                    }
    }
};

BakePlan planBakeImpl(const std::vector<SimBody>& bodies, size_t self, const VoxelGrid& world,
                      const std::vector<uint8_t>* skip, BakeParams prm, const SphereGrid* grid);

}  // namespace

glm::dvec3 bodyVoxelCentre(const SimBody& b, glm::ivec3 p) {
    const double vm = b.src.grid.voxelM;
    return b.pos + b.quat * ((glm::dvec3(p) + 0.5) * vm - comLocalM(b));
}

BakePlan planBake(const std::vector<SimBody>& bodies, size_t self, const VoxelGrid& world,
                  const std::vector<uint8_t>* skip, BakeParams prm) {
    return planBakeImpl(bodies, self, world, skip, prm, nullptr);
}

namespace {
BakePlan planBakeImpl(const std::vector<SimBody>& bodies, size_t self, const VoxelGrid& world,
                      const std::vector<uint8_t>* skip, BakeParams prm, const SphereGrid* grid) {
    BakePlan plan;
    const SimBody& b = bodies[self];
    const VoxelGrid& g = b.src.grid;
    const double vm = world.voxelM;
    // the other bodies that could hold a candidate cell
    glm::dvec3 lo, hi;
    bodyAabb(b, lo, hi);
    const double grow = (prm.reach + 1) * vm;
    lo -= grow;
    hi += grow;
    std::vector<size_t> others;
    auto consider = [&](size_t j) {
        if (j == self || (skip && (*skip)[j])) return;
        if (sphereHitsBox(bodies[j].pos, bodyRadius(bodies[j]), lo, hi)) others.push_back(j);
    };
    if (grid) {   // F5.5c: the pass's sphere grid (a superset), then the same exact test; ascending as before
        grid->query(lo, hi, consider);
        std::sort(others.begin(), others.end());
    } else {
        for (size_t j = 0; j < bodies.size(); ++j) consider(j);
    }
    // F5.5c: each near body's world box (grown by 1 um): a point outside it cannot be inside the body, so the exact
    // test below runs only for the boxes that hold the point -- the same answer, without the rotation for most
    std::vector<glm::dvec3> oLo(others.size()), oHi(others.size());
    for (size_t k = 0; k < others.size(); ++k) {
        bodyAabb(bodies[others[k]], oLo[k], oHi[k]);
        oLo[k] -= 1e-6;
        oHi[k] += 1e-6;
    }
    auto taken = [&](glm::ivec3 c) {   // F5.4's test: the cell's centre inside a solid voxel of a near body
        const glm::dvec3 x = (glm::dvec3(c) + 0.5) * vm;
        for (size_t k = 0; k < others.size(); ++k) {
            if (prm.cellSet && (x.x < oLo[k].x || x.y < oLo[k].y || x.z < oLo[k].z || x.x > oHi[k].x ||
                                x.y > oHi[k].y || x.z > oHi[k].z))
                continue;
            if (insideBody(bodies[others[k]], x)) return true;
        }
        return false;
    };
    // F5.5c: the neighbour cell set -- each cell of the plan's box is tested once (0 unknown, 1 free, 2 taken); cells
    // outside the box (a long rigid piece's far end) take the test directly. The answer per cell is the same.
    const glm::ivec3 boxLo(glm::floor(lo / vm)), boxHi(glm::floor(hi / vm));
    const glm::ivec3 boxDim = boxHi - boxLo + 1;
    std::vector<uint8_t> memo;
    if (prm.cellSet) memo.assign(size_t(boxDim.x) * boxDim.y * boxDim.z, 0);
    auto freeCell = [&](glm::ivec3 c) {
        if (!world.in(c) || world.mat[world.idx(c)] != 0) return false;
        if (!prm.cellSet) return !taken(c);
        const glm::ivec3 q = c - boxLo;
        if (q.x < 0 || q.y < 0 || q.z < 0 || q.x >= boxDim.x || q.y >= boxDim.y || q.z >= boxDim.z) return !taken(c);
        uint8_t& m = memo[(size_t(q.x) * boxDim.y + q.y) * boxDim.z + q.z];
        if (!m) m = taken(c) ? 2 : 1;
        return m == 1;
    };
    std::vector<uint32_t> src;
    std::vector<glm::dvec3> centre;  // true centres, voxel units
    for (size_t i = 0; i < g.size(); ++i) {
        if (!g.mat[i]) continue;
        src.push_back(uint32_t(i));
        centre.push_back(bodyVoxelCentre(b, g.pos(i)) / vm);
    }
    if (src.empty()) {
        plan.why = "empty body";
        return plan;
    }
    auto finish = [&](std::vector<glm::ivec3>& cells) {
        plan.cells.resize(src.size());
        double sum = 0.0;
        for (size_t k = 0; k < src.size(); ++k) {
            const double s = glm::length(glm::dvec3(cells[k]) + 0.5 - centre[k]) * vm;
            plan.cells[k] = {cells[k], src[k], s};
            plan.maxShiftM = std::max(plan.maxShiftM, s);
            sum += s;
        }
        plan.meanShiftM = sum / double(src.size());
        plan.ok = true;
    };

    // rigid: the nearest axis rotation
    const glm::dmat3 R = glm::mat3_cast(b.quat);
    double bestCos = -2.0;
    const AxisRot* best = nullptr;
    for (const AxisRot& a : axisRotations()) {
        double tr = 0.0;
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c) tr += a.m[r][c] * R[c][r];
        const double cosA = (tr - 1.0) * 0.5;
        if (cosA > bestCos) {
            bestCos = cosA;
            best = &a;
        }
    }
    plan.snapDeg = glm::degrees(std::acos(std::clamp(bestCos, -1.0, 1.0)));
    if (best && plan.snapDeg <= prm.rigidSnapDeg) {
        // centre(p) = pos/vm + M (p + 1/2 - comLocal/vm) => cell(p) = M p + floor(pos/vm - M comLocal/vm + M h)
        const glm::dvec3 base = b.pos / vm - best->apply(comLocalM(b) / vm) + best->apply(glm::dvec3(0.5));
        const glm::ivec3 k(glm::floor(base));
        std::vector<glm::ivec3> cells(src.size());
        bool ok = true;
        for (size_t i = 0; i < src.size() && ok; ++i) {
            cells[i] = best->apply(g.pos(src[i])) + k;
            ok = freeCell(cells[i]);
        }
        if (ok) {
            plan.rigid = true;
            finish(cells);
            return plan;
        }
    }

    // resample: own cell first (contested -> nearest voxel), then the rest by a global greedy within reach
    const glm::ivec3 none(INT32_MIN);
    std::vector<glm::ivec3> cells(src.size(), none);
    std::unordered_set<size_t> claimed;
    // F5.5c: with the cell set, the plan's box also keeps the claimed cells (the set only beyond the box)
    std::vector<uint8_t> claimedBox;
    if (prm.cellSet) claimedBox.assign(memo.size(), 0);
    auto boxIndex = [&](glm::ivec3 c) -> long long {
        const glm::ivec3 q = c - boxLo;
        if (q.x < 0 || q.y < 0 || q.z < 0 || q.x >= boxDim.x || q.y >= boxDim.y || q.z >= boxDim.z) return -1;
        return (long long)((size_t(q.x) * boxDim.y + q.y) * boxDim.z + q.z);
    };
    auto isClaimed = [&](glm::ivec3 c) {
        const long long q = prm.cellSet ? boxIndex(c) : -1;
        return q >= 0 ? claimedBox[size_t(q)] != 0 : claimed.count(world.idx(c)) != 0;
    };
    auto claim = [&](glm::ivec3 c) {   // true if it was free to claim
        const long long q = prm.cellSet ? boxIndex(c) : -1;
        if (q < 0) return claimed.insert(world.idx(c)).second;
        if (claimedBox[size_t(q)]) return false;
        claimedBox[size_t(q)] = 1;
        return true;
    };
    std::vector<std::tuple<double, size_t>> own;
    for (size_t k = 0; k < src.size(); ++k) {
        const glm::ivec3 c(glm::floor(centre[k]));
        own.emplace_back(glm::length(glm::dvec3(c) + 0.5 - centre[k]), k);
    }
    std::sort(own.begin(), own.end());
    for (const auto& [d, k] : own) {
        const glm::ivec3 c(glm::floor(centre[k]));
        if (!freeCell(c)) continue;
        if (!claim(c)) continue;
        cells[k] = c;
    }
    std::vector<std::tuple<double, size_t, size_t>> cand;  // distance, voxel, world index
    const int R2 = prm.reach;
    // F5.5c: with the cell set, each unplaced voxel keeps only its U nearest candidates (U = the unplaced voxels): the
    // greedy below gives a voxel its first unclaimed candidate in its own (distance, cell) order, and at most U - 1
    // other voxels claim a cell before it -- so its pick is among its first U, and the plan is the same.
    size_t unplaced = 0;
    for (size_t k = 0; k < src.size(); ++k) unplaced += cells[k] == none;
    std::vector<std::tuple<double, size_t, size_t>> mine;
    for (size_t k = 0; k < src.size(); ++k) {
        if (cells[k] != none) continue;
        const glm::ivec3 c0(glm::floor(centre[k]));
        mine.clear();
        for (int dx = -R2; dx <= R2; ++dx)
            for (int dy = -R2; dy <= R2; ++dy)
                for (int dz = -R2; dz <= R2; ++dz) {
                    const glm::ivec3 c = c0 + glm::ivec3(dx, dy, dz);
                    const long long q = prm.cellSet ? boxIndex(c) : -1;
                    if (q >= 0) {   // F5.5c: one box lookup -- the cell's free / taken memo and its claim
                        uint8_t& m = memo[size_t(q)];
                        if (!m) m = (!world.in(c) || world.mat[world.idx(c)] != 0 || taken(c)) ? 2 : 1;
                        if (m != 1 || claimedBox[size_t(q)]) continue;
                    } else if (!freeCell(c) || isClaimed(c)) {
                        continue;
                    }
                    mine.emplace_back(glm::length(glm::dvec3(c) + 0.5 - centre[k]), k, world.idx(c));
                }
        if (prm.cellSet && mine.size() > unplaced) {
            std::partial_sort(mine.begin(), mine.begin() + std::ptrdiff_t(unplaced), mine.end());
            mine.resize(unplaced);
        }
        cand.insert(cand.end(), mine.begin(), mine.end());
    }
    std::sort(cand.begin(), cand.end());
    for (const auto& [d, k, wi] : cand) {
        if (cells[k] != none) continue;
        if (!claim(world.pos(wi))) continue;
        cells[k] = world.pos(wi);
    }
    for (size_t k = 0; k < src.size(); ++k)
        if (cells[k] == none) {
            plan.why = !world.in(glm::ivec3(glm::floor(centre[k])))
                           ? "voxel " + std::to_string(src[k]) + " has its cell outside the world grid"
                           : "voxel " + std::to_string(src[k]) + " has no free cell within " +
                                 std::to_string(prm.reach) + " voxels";
            return plan;
        }
    finish(cells);
    return plan;
}
}  // namespace

size_t applyBake(VoxelGrid& world, const SimBody& b, const BakePlan& plan) {
    const VoxelGrid& g = b.src.grid;
    const bool dmg = !g.damage.empty();
    if (dmg && world.damage.empty()) world.damage.assign(world.size(), 0);
    size_t overwritten = 0;
    for (const BakeCell& c : plan.cells) {
        const size_t wi = world.idx(c.cell);
        overwritten += world.mat[wi] != 0;
        world.set(c.cell, g.mat[c.src], g.part[c.src], false);
        if (!world.damage.empty()) world.damage[wi] = dmg ? g.damage[c.src] : 0;
    }
    return overwritten;
}

std::vector<size_t> chooseBakes(const std::vector<SimBody>& bodies, glm::dvec3 player, const RestParams& p,
                                RestStats* st) {
    RestStats local;
    RestStats& s = st ? *st : local;
    s.considered = bodies.size();
    if (bodies.size() <= p.budget) return {};
    s.over = bodies.size() - p.budget;
    std::vector<std::pair<double, size_t>> elig;
    for (size_t i = 0; i < bodies.size(); ++i) {
        const SimBody& b = bodies[i];
        const double d = glm::length(b.pos - player);
        if (!b.asleep && !p.includeAwake) ++s.keptAwake;
        else if (b.asleep && b.asleepTicks < p.restTicks && !p.includeAwake) ++s.keptRecent;
        else if (d < p.keepRadiusM) ++s.keptNear;
        else elig.emplace_back(-d, i);
    }
    std::sort(elig.begin(), elig.end());
    std::vector<size_t> out;
    for (const auto& e : elig) out.push_back(e.second);
    return out;
}

std::vector<size_t> restPass(BodySimRef& sim, VoxelGrid& world, glm::dvec3 player, const RestParams& p,
                             RestStats* st, std::vector<BakePlan>* plans) {
    const auto t0 = std::chrono::steady_clock::now();
    RestStats local;
    RestStats& s = st ? *st : local;
    std::vector<SimBody>& bodies = sim.bodies();
    auto lap = [](std::chrono::steady_clock::time_point& t) {
        const auto now = std::chrono::steady_clock::now();
        const double ms = std::chrono::duration<double, std::milli>(now - t).count();
        t = now;
        return ms;
    };
    auto tl = t0;
    const std::vector<size_t> order = chooseBakes(bodies, player, p, &s);
    s.chooseMs = lap(tl);
    std::vector<size_t> baked;
    if (!order.empty()) {
        const size_t need = s.over;
        const double vm = world.voxelM;
        std::vector<uint8_t> gone(bodies.size(), 0);
        SphereGrid grid;   // F5.5c: poses are fixed within the pass
        grid.build(bodies);
        s.gridMs = lap(tl);
        for (size_t i : order) {
            if (baked.size() == need || (p.cap && baked.size() == p.cap)) break;   // F5.5c: the per-pass cap
            BakePlan plan = planBakeImpl(bodies, i, world, &gone, p.bake, &grid);
            s.planMs += lap(tl);
            if (!plan.ok) {
                ++s.refused;
                if (plans) plans->push_back(std::move(plan));
                continue;
            }
            s.overwritten += applyBake(world, bodies[i], plan);
            gone[i] = 1;
            baked.push_back(i);
            ++s.baked;
            s.rigid += plan.rigid;
            s.voxels += plan.cells.size();
            s.maxShiftM = std::max(s.maxShiftM, plan.maxShiftM);
            for (const BakeCell& c : plan.cells) s.sumShiftM += c.shiftM;
            if (plans) plans->push_back(std::move(plan));
            s.applyMs += lap(tl);
        }
        lap(tl);
        // after the pass (run 1 woke them per bake, which made the later candidates ineligible): the bodies that
        // stay and touch a baked one wake, to settle on its new cells
        for (size_t i : baked) {
            glm::dvec3 lo, hi;
            bodyAabb(bodies[i], lo, hi);
            lo -= vm;
            hi += vm;
            std::vector<size_t> near;   // F5.5c: the sphere grid (a superset), ascending as the loop it replaces
            grid.query(lo, hi, [&](size_t j) { near.push_back(j); });
            std::sort(near.begin(), near.end());
            for (size_t j : near) {
                if (gone[j] || !bodies[j].asleep) continue;
                if (!sphereHitsBox(bodies[j].pos, bodyRadius(bodies[j]), lo, hi)) continue;
                glm::dvec3 jl, jh;
                bodyAabb(bodies[j], jl, jh);
                if (!boxesTouch(lo, hi, jl, jh)) continue;
                bodies[j].asleep = false;
                bodies[j].slowTicks = 0;
                bodies[j].asleepTicks = 0;
                ++s.woken;
            }
        }
        s.wakeMs = lap(tl);
        std::vector<size_t> sorted = baked;
        std::sort(sorted.begin(), sorted.end());
        sim.remove(sorted);
        s.removeMs = lap(tl);
    }
    s.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return baked;
}

}  // namespace Phyxel::Core::VoxFound
