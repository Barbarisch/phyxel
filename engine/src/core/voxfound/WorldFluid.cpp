// docs/VoxelFoundationPlan.md F6.2 -- see WorldFluid.h.
#include "core/voxfound/WorldFluid.h"

#include <algorithm>

#include "core/voxfound/MatSimWorld.h"

namespace Phyxel::Core::VoxFound {

using namespace Phyxel::Core::MatSim;

namespace {
constexpr int kCpm = 8;   // the world lattice's finest cell = one voxel

bool mobile(uint8_t m) {
    const Phase p = materialTable()[m].phase;
    return p == Phase::Liquid || p == Phase::Powder;
}
}  // namespace

bool WorldFluid::attach(VoxelGrid* grid, std::string* err, Params p) {
    auto refuse = [&](const std::string& r) {
        if (err) *err = r;
        return false;
    };
    if (!grid) return refuse("no grid");
    if (!grid->fill.empty()) {
        if (grid->fill.size() != grid->size()) return refuse("grid.fill has the wrong size");
        const uint32_t full = fullUnits(kCpm, 8);
        for (size_t i = 0; i < grid->fill.size(); ++i) {
            const Cell c = grid->fill[i];
            if (!c) continue;
            const glm::ivec3 v = grid->pos(i);
            const std::string at = " at voxel (" + std::to_string(v.x) + "," + std::to_string(v.y) + "," +
                                   std::to_string(v.z) + ")";
            if (grid->mat[i] != 0) return refuse("fill in a solid voxel" + at);
            if (!mobile(cellMaterial(c))) return refuse("fill of a material that is not liquid or powder" + at);
            if (cellFill(c) == 0 || cellFill(c) > full) return refuse("fill units outside 1.." + std::to_string(full) + at);
        }
    }
    g_ = grid;
    p_ = p;
    p_.cfg.lattice = 8;
    have_ = false;
    tick_ = 0;
    grows_ = 0;
    if (err) err->clear();
    return true;
}

bool WorldFluid::fillBounds(glm::ivec3& lo, glm::ivec3& hi) const {
    if (g_->fill.empty()) return false;
    bool any = false;
    for (size_t i = 0; i < g_->fill.size(); ++i) {
        if (!g_->fill[i]) continue;
        const glm::ivec3 v = g_->pos(i);
        lo = any ? glm::min(lo, v) : v;
        hi = any ? glm::max(hi, v) : v;
        any = true;
    }
    return any;
}

bool WorldFluid::makeBox(bool keepOld) {
    glm::ivec3 lo, hi;
    if (!fillBounds(lo, hi)) {
        have_ = false;
        return false;
    }
    lo = glm::max(lo - p_.margin, glm::ivec3(0));
    hi = glm::min(hi + p_.margin, g_->dim - 1);
    if (keepOld && have_) {   // a growth never shrinks: the old box stays inside the new one
        lo = glm::min(lo, lo_);
        hi = glm::max(hi, lo_ + size_ - 1);
    }
    std::string why;
    MatSimCore fresh;
    if (!fresh.create({lo.x, lo.y, lo.z}, {hi.x - lo.x + 1, hi.y - lo.y + 1, hi.z - lo.z + 1}, kCpm,
                      matSimOccupancy(*g_), &why, p_.cfg)) {
        have_ = false;   // cannot happen for a box inside the grid; nothing is lost (grid.fill still holds it all)
        return false;
    }
    for (int y = lo.y; y <= hi.y; ++y)
        for (int z = lo.z; z <= hi.z; ++z)
            for (int x = lo.x; x <= hi.x; ++x) {
                const Cell c = g_->fill[g_->idx({x, y, z})];
                if (c) fresh.setCell({x, y, z}, cellMaterial(c), cellFill(c));
            }
    fresh.setTick(tick_);
    core_ = std::move(fresh);
    lo_ = lo;
    size_ = hi - lo + 1;
    have_ = true;
    return true;
}

void WorldFluid::writeBack() {
    if (!have_) return;
    const glm::ivec3 hi = lo_ + size_ - 1;
    for (int y = lo_.y; y <= hi.y; ++y)
        for (int z = lo_.z; z <= hi.z; ++z)
            for (int x = lo_.x; x <= hi.x; ++x) {
                const Cell c = core_.at({x, y, z});
                const size_t i = g_->idx({x, y, z});
                // a solid cell's trapped contents stay in the box's ledger (a wall built over fluid), not in the grid
                g_->fill[i] = (cellMaterial(c) == kSolid || cellFill(c) == 0) ? 0u : c;
            }
}

bool WorldFluid::nearOpenFace() const {
    const glm::ivec3 hi = lo_ + size_ - 1;
    const int w = p_.growWithin;
    for (int axis = 0; axis < 3; ++axis) {
        const bool lowOpen = lo_[axis] > 0, highOpen = hi[axis] < g_->dim[axis] - 1;
        if (!lowOpen && !highOpen) continue;
        for (int y = lo_.y; y <= hi.y; ++y)
            for (int z = lo_.z; z <= hi.z; ++z)
                for (int x = lo_.x; x <= hi.x; ++x) {
                    const int c = axis == 0 ? x : axis == 1 ? y : z;
                    const bool inLow = lowOpen && c - lo_[axis] < w, inHigh = highOpen && hi[axis] - c < w;
                    if (!inLow && !inHigh) continue;
                    const Cell cell = core_.at({x, y, z});
                    if (cellMaterial(cell) != kSolid && cellFill(cell) != 0) return true;
                }
    }
    return false;
}

StepStats WorldFluid::step(int n) {
    StepStats sum;
    if (!g_) return sum;
    if (!have_ && !g_->fill.empty()) makeBox();
    for (int t = 0; t < n; ++t) {
        // contents trapped under a wall built over fluid live only in the box's ledger (grid.fill holds open voxels
        // only), so a box holding any is not re-made (owed: carry them over)
        if (have_ && p_.grow && core_.trappedUnits() == 0 && nearOpenFace()) {
            const glm::ivec3 oldLo = lo_, oldSize = size_;
            regrow();
            if (!(lo_ == oldLo && size_ == oldSize)) ++grows_;
        }
        if (have_) {
            const StepStats s = core_.step(1);
            sum.ticks += s.ticks;
            sum.eligibleBlocks += s.eligibleBlocks;
            sum.evaluatedBlocks += s.evaluatedBlocks;
            sum.lastTickEligible = s.lastTickEligible;
            sum.headChanges += s.headChanges;
            sum.lastTickHeadChanges = s.lastTickHeadChanges;
        } else {
            ++sum.ticks;
        }
        ++tick_;
    }
    writeBack();
    return sum;
}

// The pressure rule's head field is state carried across ticks (route / max pointers relax tick by tick), so a box
// re-made from the cells alone runs differently (measured, F6.2: equal with the pressure rule off, not with it on).
// The old box's pointers and head words are carried into the new box by world cell; every block is woken (the next
// pressure pass is a full one).
void WorldFluid::regrow() {
    const MatSimCore old = core_;
    const glm::ivec3 oLo = lo_, oSize = size_;
    writeBack();
    if (!makeBox(true)) return;
    if (!p_.cfg.pressure) return;
    const glm::ivec3 nLo = lo_, nSize = size_;
    auto idxIn = [](glm::ivec3 lo, glm::ivec3 size, glm::ivec3 w) {
        const glm::ivec3 l = w - lo;
        return size_t(l.x) + size_t(size.x) * (size_t(l.z) + size_t(size.z) * size_t(l.y));
    };
    auto posIn = [](glm::ivec3 lo, glm::ivec3 size, size_t i) {
        const int x = int(i % size_t(size.x));
        const size_t r = i / size_t(size.x);
        return lo + glm::ivec3(x, int(r / size_t(size.z)), int(r % size_t(size.z)));
    };
    const size_t n = core_.cells().size(), on = old.cells().size();
    std::vector<uint32_t> q(n, kNoHead), m(n, kNoHead), h(n, 0u);
    auto remap = [&](uint32_t v) {
        if (v == kNoHead) return v;
        const glm::ivec3 src = posIn(oLo, oSize, v >> 8);
        return uint32_t(idxIn(nLo, nSize, src) << 8) | (v & 0xFFu);
    };
    const uint32_t dyUnits = uint32_t(oLo.y - nLo.y) * core_.full();   // head words count from the box floor
    for (size_t i = 0; i < on; ++i) {
        const size_t j = idxIn(nLo, nSize, posIn(oLo, oSize, i));
        q[j] = remap(old.headPtr()[i]);
        m[j] = remap(old.headMaxPtr()[i]);
        const uint32_t hv = old.head()[i], val = hv & kHeadMask;
        h[j] = (hv & ~kHeadMask) | (val ? val + dyUnits : 0u);
    }
    std::vector<uint32_t> awake(core_.marksTick1().size(), 1u);
    const std::vector<Cell> cells = core_.cells();
    core_.adoptSteppedFrom(cells.data(), tick_, awake.data(), awake.data(), q.data(), h.data(), m.data());
}

void WorldFluid::wallsChanged() {
    if (have_) core_.resolidify(matSimOccupancy(*g_));
}

bool WorldFluid::setFill(glm::ivec3 v, uint8_t material, uint32_t units, std::string* err) {
    auto refuse = [&](const std::string& r) {
        if (err) *err = r;
        return false;
    };
    if (!g_) return refuse("not attached");
    if (!g_->in(v)) return refuse("outside the grid");
    if (g_->mat[g_->idx(v)] != 0) return refuse("a solid voxel");
    if (units > fullUnits(kCpm, 8)) return refuse("units over FULL");
    if (units && !mobile(material)) return refuse("not a liquid or powder");
    if (g_->fill.empty()) g_->fill.assign(g_->size(), 0u);
    const glm::ivec3 hi = lo_ + size_ - 1;
    const bool inBox = have_ && glm::all(glm::greaterThanEqual(v, lo_)) && glm::all(glm::lessThanEqual(v, hi));
    if (inBox) {   // straight into the box (its expected ledger follows the edit)
        core_.setCell({v.x, v.y, v.z}, units ? material : kEmpty, units);
        g_->fill[g_->idx(v)] = units ? makeCell(material, units) : 0u;
    } else {
        if (have_ && core_.trappedUnits() != 0) return refuse("the box holds trapped contents and cannot be re-made");
        writeBack();
        g_->fill[g_->idx(v)] = units ? makeCell(material, units) : 0u;
        makeBox();   // the box re-made around the new fill, the tick carried over
    }
    if (err) err->clear();
    return true;
}

Ledger WorldFluid::ledger() const {
    Ledger l{};
    if (!g_) return l;
    if (have_) l = core_.ledger();
    const glm::ivec3 hi = lo_ + size_ - 1;
    for (size_t i = 0; i < g_->fill.size(); ++i) {
        const Cell c = g_->fill[i];
        if (!c) continue;
        const glm::ivec3 v = g_->pos(i);
        const bool inBox = have_ && v.x >= lo_.x && v.y >= lo_.y && v.z >= lo_.z && v.x <= hi.x && v.y <= hi.y &&
                           v.z <= hi.z;
        if (!inBox) l[cellMaterial(c)] += cellFill(c);
    }
    return l;
}

}  // namespace Phyxel::Core::VoxFound
