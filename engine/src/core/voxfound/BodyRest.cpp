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

}  // namespace

glm::dvec3 bodyVoxelCentre(const SimBody& b, glm::ivec3 p) {
    const double vm = b.src.grid.voxelM;
    return b.pos + b.quat * ((glm::dvec3(p) + 0.5) * vm - comLocalM(b));
}

BakePlan planBake(const std::vector<SimBody>& bodies, size_t self, const VoxelGrid& world,
                  const std::vector<uint8_t>* skip, BakeParams prm) {
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
    for (size_t j = 0; j < bodies.size(); ++j) {
        if (j == self || (skip && (*skip)[j])) continue;
        if (sphereHitsBox(bodies[j].pos, bodyRadius(bodies[j]), lo, hi)) others.push_back(j);
    }
    auto freeCell = [&](glm::ivec3 c) {
        if (!world.in(c) || world.mat[world.idx(c)] != 0) return false;
        const glm::dvec3 x = (glm::dvec3(c) + 0.5) * vm;
        for (size_t j : others)
            if (insideBody(bodies[j], x)) return false;
        return true;
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
    std::vector<std::tuple<double, size_t>> own;
    for (size_t k = 0; k < src.size(); ++k) {
        const glm::ivec3 c(glm::floor(centre[k]));
        own.emplace_back(glm::length(glm::dvec3(c) + 0.5 - centre[k]), k);
    }
    std::sort(own.begin(), own.end());
    for (const auto& [d, k] : own) {
        const glm::ivec3 c(glm::floor(centre[k]));
        if (!freeCell(c)) continue;
        if (!claimed.insert(world.idx(c)).second) continue;
        cells[k] = c;
    }
    std::vector<std::tuple<double, size_t, size_t>> cand;  // distance, voxel, world index
    const int R2 = prm.reach;
    for (size_t k = 0; k < src.size(); ++k) {
        if (cells[k] != none) continue;
        const glm::ivec3 c0(glm::floor(centre[k]));
        for (int dx = -R2; dx <= R2; ++dx)
            for (int dy = -R2; dy <= R2; ++dy)
                for (int dz = -R2; dz <= R2; ++dz) {
                    const glm::ivec3 c = c0 + glm::ivec3(dx, dy, dz);
                    if (!freeCell(c) || claimed.count(world.idx(c))) continue;
                    cand.emplace_back(glm::length(glm::dvec3(c) + 0.5 - centre[k]), k, world.idx(c));
                }
    }
    std::sort(cand.begin(), cand.end());
    for (const auto& [d, k, wi] : cand) {
        if (cells[k] != none) continue;
        if (!claimed.insert(wi).second) continue;
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
        if (!b.asleep) ++s.keptAwake;
        else if (b.asleepTicks < p.restTicks) ++s.keptRecent;
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
    const std::vector<size_t> order = chooseBakes(bodies, player, p, &s);
    std::vector<size_t> baked;
    if (!order.empty()) {
        const size_t need = s.over;
        const double vm = world.voxelM;
        std::vector<uint8_t> gone(bodies.size(), 0);
        for (size_t i : order) {
            if (baked.size() == need) break;
            BakePlan plan = planBake(bodies, i, world, &gone, p.bake);
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
        }
        // after the pass (run 1 woke them per bake, which made the later candidates ineligible): the bodies that
        // stay and touch a baked one wake, to settle on its new cells
        for (size_t i : baked) {
            glm::dvec3 lo, hi;
            bodyAabb(bodies[i], lo, hi);
            lo -= vm;
            hi += vm;
            for (size_t j = 0; j < bodies.size(); ++j) {
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
        std::vector<size_t> sorted = baked;
        std::sort(sorted.begin(), sorted.end());
        sim.remove(sorted);
    }
    s.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return baked;
}

}  // namespace Phyxel::Core::VoxFound
