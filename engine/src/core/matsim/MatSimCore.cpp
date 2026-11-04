// docs/MaterialSimPlan.md §6a — MatSim P1a: the CPU reference material cellular automaton.
// See the header for the model. This file is the source of truth the P1b GPU port must match.
#include "core/matsim/MatSimCore.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Phyxel::Core::MatSim {

namespace {

constexpr uint64_t kMaxCells = 16777216ull;   // 2^24 (§6a create: > 16.7 M cells is refused)

int floorDiv(int a, int b) {
    int q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}

uint32_t fmix32(uint32_t h) {
    h ^= h >> 16;
    h *= 0x85ebca6bu;
    h ^= h >> 13;
    h *= 0xc2b2ae35u;
    h ^= h >> 16;
    return h;
}

std::string worldStr(int x, int y, int z) {
    char b[64];
    std::snprintf(b, sizeof(b), "(%d,%d,%d)", x, y, z);
    return b;
}

Phase phaseOf(uint8_t m) { return materialTable()[m].phase; }
bool isSolid(Cell c) { return cellMaterial(c) == kSolid; }
bool isMobile(uint8_t m) {
    const Phase p = phaseOf(m);
    return p == Phase::Liquid || p == Phase::Powder;
}

} // namespace

uint32_t fullUnits(int cpm) {
    switch (cpm) {
        case 9: return 256u;
        case 3: return 256u * 27u;
        case 1: return 256u * 729u;
        default: return 0u;
    }
}

bool validCellsPerM(int cpm) { return cpm == 1 || cpm == 3 || cpm == 9; }

const std::array<MaterialDef, 256>& materialTable() {
    static const std::array<MaterialDef, 256> table = [] {
        std::array<MaterialDef, 256> t{};
        for (auto& d : t) { d.name = ""; d.phase = Phase::Empty; }
        t[kEmpty] = {"Empty", Phase::Empty, 0};
        t[Mat::Water] = {"Water", Phase::Liquid, 0};
        // tan(34 deg) = 0.6745, tan(38) = 0.7813, tan(45) = 1.0 — §6a rule 3, Glover (1995).
        t[Mat::Sand] = {"Sand", Phase::Powder, 675};
        t[Mat::Snow] = {"Snow", Phase::Powder, 781};
        t[Mat::Gravel] = {"Gravel", Phase::Powder, 1000};
        t[kSolid] = {"SOLID", Phase::Solid, 0};
        return t;
    }();
    return table;
}

bool materialId(const std::string& name, uint8_t& out) {
    const auto& t = materialTable();
    for (int i = 0; i < 256; ++i)
        if (!t[i].name.empty() && t[i].name == name) { out = static_cast<uint8_t>(i); return true; }
    return false;
}

uint32_t cellHash(int x, int y, int z, uint64_t tick, uint32_t seed) {
    uint32_t h = fmix32(seed ^ 0x9E3779B9u);
    h = fmix32(h ^ static_cast<uint32_t>(x) * 0x27d4eb2du);
    h = fmix32(h ^ static_cast<uint32_t>(y) * 0x165667b1u);
    h = fmix32(h ^ static_cast<uint32_t>(z) * 0xd3a2646cu);
    h = fmix32(h ^ tickMix(tick));
    return h;
}

uint32_t slideThreshold(uint8_t m, uint32_t full, bool diag) {
    return static_cast<uint32_t>(uint64_t(materialTable()[m].reposeTanMilli) * (diag ? 1414u : 1000u) *
                                 uint64_t(full) / 1000000u);
}

bool MatSimCore::adoptStepped(std::vector<Cell> cells, uint64_t tick, std::vector<uint8_t> m1,
                              std::vector<uint8_t> m2) {
    if (!created_ || cells.size() != cur_.size() || m1.size() != marks1_.size() || m2.size() != marks2_.size())
        return false;
    cur_ = std::move(cells);
    nxt_ = cur_;
    tick_ = tick;
    marks1_ = std::move(m1);
    marks2_ = std::move(m2);
    return true;
}

// ---------------------------------------------------------------------------------------------

bool MatSimCore::solidCell(const MicroSolidFn& fn, int wx, int wy, int wz, int cpm, bool& unknown) const {
    const int k = 9 / cpm;
    bool solid = false;
    unknown = false;
    for (int my = wy * k; my < wy * k + k; ++my)
        for (int mz = wz * k; mz < wz * k + k; ++mz)
            for (int mx = wx * k; mx < wx * k + k; ++mx) {
                const Occ o = fn(mx, my, mz);
                if (o == Occ::Solid) solid = true;
                else if (o == Occ::Unknown) { unknown = true; solid = true; }
            }
    return solid;
}

bool MatSimCore::create(IVec3 o, IVec3 sz, int cpm, const MicroSolidFn& solid, std::string* reason,
                        Config cfg) {
    auto refuse = [&](const std::string& r) { if (reason) *reason = r; return false; };
    if (!validCellsPerM(cpm)) return refuse("cells_per_m must be 1, 3 or 9");
    if (sz.x <= 0 || sz.y <= 0 || sz.z <= 0) return refuse("box size must be positive on every axis");
    const uint64_t n = uint64_t(sz.x) * uint64_t(sz.y) * uint64_t(sz.z);
    if (n > kMaxCells) return refuse("box has " + std::to_string(n) + " cells; the limit is 16,777,216");
    // Micro coordinates are cell x 9 and blocks reach one cell past the box: keep every one in int.
    const long long kLim = (0x7fffffffLL / 9) - 16;
    for (long long v : {(long long)o.x, (long long)o.y, (long long)o.z,
                        (long long)o.x + sz.x, (long long)o.y + sz.y, (long long)o.z + sz.z})
        if (v < -kLim || v > kLim) return refuse("box coordinates out of range (|cell| <= " + std::to_string(kLim) + ")");
    if (!solid) return refuse("no occupancy source");

    std::vector<Cell> cells(n, makeCell(kEmpty, 0));
    for (int y = 0; y < sz.y; ++y)
        for (int z = 0; z < sz.z; ++z)
            for (int x = 0; x < sz.x; ++x) {
                bool unk = false;
                const bool s = solidCell(solid, o.x + x, o.y + y, o.z + z, cpm, unk);
                if (unk) return refuse("box contains unknown occupancy at world cell " +
                                       worldStr(o.x + x, o.y + y, o.z + z));
                if (s) cells[size_t(x) + size_t(sz.x) * (size_t(z) + size_t(sz.z) * size_t(y))] = makeCell(kSolid, 0);
            }

    clear();
    created_ = true;
    cfg_ = cfg;
    cpm_ = cpm;
    full_ = fullUnits(cpm);
    origin_ = o;
    size_ = sz;
    cur_ = std::move(cells);
    nxt_ = cur_;
    wakeAll();
    if (reason) reason->clear();
    return true;
}

bool MatSimCore::createMetres(double minX, double minY, double minZ, double sizeX, double sizeY,
                              double sizeZ, int cpm, const MicroSolidFn& solid, std::string* reason,
                              Config cfg) {
    if (!validCellsPerM(cpm)) { if (reason) *reason = "cells_per_m must be 1, 3 or 9"; return false; }
    const double v[6] = {minX, minY, minZ, sizeX, sizeY, sizeZ};
    int c[6];
    for (int i = 0; i < 6; ++i) {
        const double s = v[i] * cpm;
        const double r = std::round(s);
        if (!std::isfinite(s) || std::fabs(s - r) > 1e-6 || std::fabs(r) > 1e8) {
            if (reason) *reason = "box min/size must lie on the " + std::to_string(cpm) + " cells/m lattice";
            return false;
        }
        c[i] = static_cast<int>(r);
    }
    return create({c[0], c[1], c[2]}, {c[3], c[4], c[5]}, cpm, solid, reason, cfg);
}

void MatSimCore::clear() {
    created_ = false;
    cfg_ = Config{};
    cpm_ = 0;
    full_ = 0;
    origin_ = {};
    size_ = {};
    cur_.clear();
    nxt_.clear();
    trapped_.clear();
    expected_ = {};
    tick_ = 0;
    unknownCells_ = 0;
    brickLo_ = {};
    brickDim_ = {};
    marks1_.clear();
    marks2_.clear();
}

bool MatSimCore::inBox(IVec3 w) const {
    return created_ && w.x >= origin_.x && w.y >= origin_.y && w.z >= origin_.z &&
           w.x < origin_.x + size_.x && w.y < origin_.y + size_.y && w.z < origin_.z + size_.z;
}

Cell MatSimCore::at(IVec3 w) const {
    if (!inBox(w)) return makeCell(kSolid, 0);
    return cur_[index(w.x - origin_.x, w.y - origin_.y, w.z - origin_.z)];
}

Cell MatSimCore::readPrev(int wx, int wy, int wz) const { return at({wx, wy, wz}); }

// ---------------------------------------------------------------------------------------------
// Edits.

FillResult MatSimCore::fillBox(uint8_t material, IVec3 lo, IVec3 sz, double fraction) {
    FillResult r;
    if (!created_) return r;
    if (material != kEmpty && !isMobile(material)) return r;
    if (!(fraction >= 0.0)) fraction = 0.0;   // NaN -> 0
    if (fraction > 1.0) fraction = 1.0;
    const uint32_t units = (material == kEmpty) ? 0u : static_cast<uint32_t>(std::llround(fraction * full_));
    const int x0 = std::max(lo.x, origin_.x), x1 = std::min(lo.x + sz.x, origin_.x + size_.x);
    const int y0 = std::max(lo.y, origin_.y), y1 = std::min(lo.y + sz.y, origin_.y + size_.y);
    const int z0 = std::max(lo.z, origin_.z), z1 = std::min(lo.z + sz.z, origin_.z + size_.z);
    if (x0 >= x1 || y0 >= y1 || z0 >= z1) return r;
    for (int y = y0; y < y1; ++y)
        for (int z = z0; z < z1; ++z)
            for (int x = x0; x < x1; ++x) {
                Cell& c = cur_[index(x - origin_.x, y - origin_.y, z - origin_.z)];
                if (isSolid(c)) { ++r.solidSkipped; continue; }
                expected_[cellMaterial(c)] -= cellFill(c);
                c = units ? makeCell(material, units) : makeCell(kEmpty, 0);
                expected_[cellMaterial(c)] += cellFill(c);
                ++r.cellsWritten;
                r.unitsWritten += units;
            }
    wakeCells({x0, y0, z0}, {x1 - 1, y1 - 1, z1 - 1});
    return r;
}

bool MatSimCore::setCell(IVec3 w, uint8_t material, uint32_t fill) {
    if (!inBox(w)) return false;
    if (material != kEmpty && !isMobile(material)) return false;
    if (fill > full_) return false;
    Cell& c = cur_[index(w.x - origin_.x, w.y - origin_.y, w.z - origin_.z)];
    if (isSolid(c)) return false;
    expected_[cellMaterial(c)] -= cellFill(c);
    c = (material == kEmpty || fill == 0) ? makeCell(kEmpty, 0) : makeCell(material, fill);
    expected_[cellMaterial(c)] += cellFill(c);
    wakeCells(w, w);
    return true;
}

void MatSimCore::resolidify(const MicroSolidFn& fn) {
    if (!created_ || !fn) return;
    unknownCells_ = 0;
    for (int y = 0; y < size_.y; ++y)
        for (int z = 0; z < size_.z; ++z)
            for (int x = 0; x < size_.x; ++x) {
                bool unk = false;
                const bool s = solidCell(fn, origin_.x + x, origin_.y + y, origin_.z + z, cpm_, unk);
                if (unk) ++unknownCells_;
                const size_t i = index(x, y, z);
                Cell& c = cur_[i];
                if (s && !isSolid(c)) {
                    if (cellFill(c) > 0) trapped_[i] = c;   // frozen inside the new solid, still counted
                    c = makeCell(kSolid, 0);
                } else if (!s && isSolid(c)) {
                    auto it = trapped_.find(i);
                    if (it != trapped_.end()) { c = it->second; trapped_.erase(it); }
                    else c = makeCell(kEmpty, 0);
                }
            }
    nxt_ = cur_;
    wakeAll();
}

// ---------------------------------------------------------------------------------------------
// Resolution switch (§3a): split bottom-up, merge = sum. Two-phase: nothing changes on refusal.

bool MatSimCore::setCellSize(int ncpm, const MicroSolidFn& fn, std::string* reason) {
    auto refuse = [&](const std::string& r) { if (reason) *reason = r; return false; };
    if (!created_) return refuse("no box");
    if (!validCellsPerM(ncpm)) return refuse("cells_per_m must be 1, 3 or 9");
    if (!fn) return refuse("no occupancy source");
    if (ncpm == cpm_) { if (reason) reason->clear(); return true; }

    const int lo[3] = {origin_.x, origin_.y, origin_.z};
    const int sz[3] = {size_.x, size_.y, size_.z};
    int nlo[3], nsz[3];
    for (int i = 0; i < 3; ++i) {
        const long long a = (long long)lo[i] * ncpm, b = (long long)sz[i] * ncpm;
        if (a % cpm_ != 0 || b % cpm_ != 0)
            return refuse("the box is not on the " + std::to_string(ncpm) + " cells/m lattice");
        nlo[i] = int(a / cpm_);
        nsz[i] = int(b / cpm_);
    }
    const uint64_t n = uint64_t(nsz[0]) * uint64_t(nsz[1]) * uint64_t(nsz[2]);
    if (n > kMaxCells) return refuse("the switch would make " + std::to_string(n) + " cells; the limit is 16,777,216");

    const uint32_t nfull = fullUnits(ncpm);
    auto nidx = [&](int x, int y, int z) {
        return size_t(x) + size_t(nsz[0]) * (size_t(z) + size_t(nsz[2]) * size_t(y));
    };
    // New solid mask.
    std::vector<Cell> cells(n, makeCell(kEmpty, 0));
    for (int y = 0; y < nsz[1]; ++y)
        for (int z = 0; z < nsz[2]; ++z)
            for (int x = 0; x < nsz[0]; ++x) {
                bool unk = false;
                if (solidCell(fn, nlo[0] + x, nlo[1] + y, nlo[2] + z, ncpm, unk)) {
                    if (unk) return refuse("unknown occupancy at new world cell " +
                                           worldStr(nlo[0] + x, nlo[1] + y, nlo[2] + z));
                    cells[nidx(x, y, z)] = makeCell(kSolid, 0);
                }
            }
    std::map<size_t, Cell> trapped;

    // Contents of an old cell, including what is trapped inside it.
    auto contents = [&](size_t i) -> Cell {
        const Cell c = cur_[i];
        if (!isSolid(c)) return c;
        auto it = trapped_.find(i);
        return it == trapped_.end() ? makeCell(kEmpty, 0) : it->second;
    };

    if (ncpm > cpm_) {
        const int r = ncpm / cpm_;
        for (int y = 0; y < sz[1]; ++y)
            for (int z = 0; z < sz[2]; ++z)
                for (int x = 0; x < sz[0]; ++x) {
                    const Cell c = contents(index(x, y, z));
                    uint64_t left = cellFill(c);
                    if (!left) continue;
                    const uint8_t m = cellMaterial(c);
                    size_t firstSolid = SIZE_MAX;
                    for (int dy = 0; dy < r && left; ++dy) {   // bottom-up, layer by layer
                        std::vector<size_t> open;
                        for (int dz = 0; dz < r; ++dz)
                            for (int dx = 0; dx < r; ++dx) {
                                const size_t j = nidx(x * r + dx, y * r + dy, z * r + dz);
                                if (isSolid(cells[j])) { if (firstSolid == SIZE_MAX) firstSolid = j; }
                                else open.push_back(j);
                            }
                        if (open.empty()) continue;
                        const uint64_t take = std::min<uint64_t>(left, uint64_t(open.size()) * nfull);
                        const uint64_t q = take / open.size(), rem = take % open.size();
                        for (size_t k = 0; k < open.size(); ++k) {
                            const uint32_t f = uint32_t(q + (k < rem ? 1 : 0));
                            if (f) cells[open[k]] = makeCell(m, f);
                        }
                        left -= take;
                    }
                    if (left) {
                        // Only possible when the old cell was solid (trapped contents): the rest
                        // stays trapped in the first solid child, still in the ledger.
                        if (firstSolid == SIZE_MAX)
                            for (int dy = 0; dy < r && firstSolid == SIZE_MAX; ++dy)
                                for (int dz = 0; dz < r && firstSolid == SIZE_MAX; ++dz)
                                    for (int dx = 0; dx < r; ++dx) {
                                        const size_t j = nidx(x * r + dx, y * r + dy, z * r + dz);
                                        if (isSolid(cells[j])) { firstSolid = j; break; }
                                    }
                        if (firstSolid == SIZE_MAX || left > kFillMask)
                            return refuse("split could not place trapped contents of world cell " +
                                          worldStr(lo[0] + x, lo[1] + y, lo[2] + z));
                        trapped[firstSolid] = makeCell(m, uint32_t(left));
                    }
                }
    } else {
        const int r = cpm_ / ncpm;
        for (int y = 0; y < nsz[1]; ++y)
            for (int z = 0; z < nsz[2]; ++z)
                for (int x = 0; x < nsz[0]; ++x) {
                    uint64_t sum = 0;
                    uint8_t m = kEmpty;
                    for (int dy = 0; dy < r; ++dy)
                        for (int dz = 0; dz < r; ++dz)
                            for (int dx = 0; dx < r; ++dx) {
                                const Cell c = contents(index(x * r + dx, y * r + dy, z * r + dz));
                                if (!cellFill(c)) continue;
                                if (m != kEmpty && cellMaterial(c) != m)
                                    return refuse("merge would mix " + materialTable()[m].name + " and " +
                                                  materialTable()[cellMaterial(c)].name + " in new world cell " +
                                                  worldStr(nlo[0] + x, nlo[1] + y, nlo[2] + z));
                                m = cellMaterial(c);
                                sum += cellFill(c);
                            }
                    if (!sum) continue;   // sum <= r^3 * old FULL = new FULL, always fits
                    const size_t j = nidx(x, y, z);
                    if (isSolid(cells[j])) trapped[j] = makeCell(m, uint32_t(sum));
                    else cells[j] = makeCell(m, uint32_t(sum));
                }
    }

    // Commit.
    cpm_ = ncpm;
    full_ = nfull;
    origin_ = {nlo[0], nlo[1], nlo[2]};
    size_ = {nsz[0], nsz[1], nsz[2]};
    cur_ = std::move(cells);
    nxt_ = cur_;
    trapped_ = std::move(trapped);
    unknownCells_ = 0;
    wakeAll();
    if (reason) reason->clear();
    return true;
}

// ---------------------------------------------------------------------------------------------
// Ledger.

Ledger MatSimCore::ledger() const {
    Ledger l{};
    for (Cell c : cur_)
        if (!isSolid(c)) l[cellMaterial(c)] += cellFill(c);
    for (const auto& [i, c] : trapped_) l[cellMaterial(c)] += cellFill(c);
    return l;
}

uint64_t MatSimCore::trappedUnits() const {
    uint64_t t = 0;
    for (const auto& [i, c] : trapped_) t += cellFill(c);
    return t;
}

// ---------------------------------------------------------------------------------------------
// Sleep bookkeeping: 8^3 world-aligned bricks.

int MatSimCore::brickOf(int wx, int wy, int wz) const {
    const int bx = floorDiv(wx, 8) - brickLo_.x, by = floorDiv(wy, 8) - brickLo_.y, bz = floorDiv(wz, 8) - brickLo_.z;
    return bx + brickDim_.x * (bz + brickDim_.z * by);
}

void MatSimCore::wakeAll() {
    brickLo_ = {floorDiv(origin_.x - 1, 8), floorDiv(origin_.y - 1, 8), floorDiv(origin_.z - 1, 8)};
    const IVec3 hi{floorDiv(origin_.x + size_.x, 8), floorDiv(origin_.y + size_.y, 8), floorDiv(origin_.z + size_.z, 8)};
    brickDim_ = {hi.x - brickLo_.x + 1, hi.y - brickLo_.y + 1, hi.z - brickLo_.z + 1};
    const size_t n = size_t(brickDim_.x) * size_t(brickDim_.y) * size_t(brickDim_.z);
    marks1_.assign(n, 1);
    marks2_.assign(n, 1);
}

void MatSimCore::wakeCells(IVec3 lo, IVec3 hi) {
    for (int by = floorDiv(lo.y, 8); by <= floorDiv(hi.y, 8); ++by)
        for (int bz = floorDiv(lo.z, 8); bz <= floorDiv(hi.z, 8); ++bz)
            for (int bx = floorDiv(lo.x, 8); bx <= floorDiv(hi.x, 8); ++bx) {
                const int i = brickOf(bx * 8, by * 8, bz * 8);
                marks1_[i] = 1;
                marks2_[i] = 1;
            }
}

void MatSimCore::markBricksOfBlock(int bx, int by, int bz, std::vector<uint8_t>& marks) const {
    for (int dy = 0; dy < 2; ++dy)
        for (int dz = 0; dz < 2; ++dz)
            for (int dx = 0; dx < 2; ++dx)
                if (inBox({bx + dx, by + dy, bz + dz})) marks[brickOf(bx + dx, by + dy, bz + dz)] = 1;
}

void MatSimCore::copyBlock(int bx, int by, int bz) {
    for (int dy = 0; dy < 2; ++dy)
        for (int dz = 0; dz < 2; ++dz)
            for (int dx = 0; dx < 2; ++dx) {
                const IVec3 w{bx + dx, by + dy, bz + dz};
                if (!inBox(w)) continue;
                const size_t i = index(w.x - origin_.x, w.y - origin_.y, w.z - origin_.z);
                nxt_[i] = cur_[i];
            }
}

// ---------------------------------------------------------------------------------------------
// The block rules (§6a): gravity, liquid level, slide (angle of repose; liquids at 0 deg).

bool MatSimCore::evalBlock(int bx, int by, int bz, int /*parity*/) {
    Cell c[2][2][2];   // [dy][dz][dx]
    Cell orig[2][2][2];
    for (int dy = 0; dy < 2; ++dy)
        for (int dz = 0; dz < 2; ++dz)
            for (int dx = 0; dx < 2; ++dx)
                orig[dy][dz][dx] = c[dy][dz][dx] = readPrev(bx + dx, by + dy, bz + dz);
    const uint32_t F = full_;
    auto setc = [](Cell& cell, uint8_t m, uint32_t f) { cell = f ? makeCell(m, f) : makeCell(kEmpty, 0); };

    // 1. Gravity, per column, top -> bottom.
    for (int dz = 0; dz < 2; ++dz)
        for (int dx = 0; dx < 2; ++dx) {
            Cell& top = c[1][dz][dx];
            Cell& bot = c[0][dz][dx];
            const uint8_t m = cellMaterial(top);
            if (!isMobile(m) || isSolid(bot)) continue;
            if (cellMaterial(bot) != kEmpty && cellMaterial(bot) != m) continue;
            const uint32_t mv = std::min(cellFill(top), F - cellFill(bot));
            if (!mv) continue;
            uint32_t land = mv;
            if (cfg_.testLeakOneUnit) { land -= 1; cfg_.testLeakOneUnit = false; }   // TEST-ONLY control
            setc(bot, m, cellFill(bot) + land);
            setc(top, m, cellFill(top) - mv);
        }

    // 2. Liquid level, per layer, over supported cells of one liquid (or empty), deadband 1 unit.
    //    Cells level only with cells they are FACE-connected to through the group: two cells that
    //    touch only at an edge (a staircased diagonal wall) are separate pools (MatSimWalls test).
    for (int dy = 0; dy < 2; ++dy) {
        bool cand[2][2] = {};
        for (int dz = 0; dz < 2; ++dz)
            for (int dx = 0; dx < 2; ++dx) {
                const Cell cell = c[dy][dz][dx];
                if (isSolid(cell)) continue;
                const uint8_t m = cellMaterial(cell);
                if (m != kEmpty && phaseOf(m) != Phase::Liquid) continue;
                const Cell below = dy ? c[0][dz][dx] : readPrev(bx + dx, by - 1, bz + dz);
                if (!(isSolid(below) || cellFill(below) == F)) continue;   // unsupported
                cand[dz][dx] = true;
            }
        // Face-connected components of the 2x2 layer: a cell's face neighbours differ in exactly
        // one of dx, dz. A component is the start cell plus its two face neighbours if present;
        // the opposite corner joins only through one of them.
        bool done[2][2] = {};
        for (int sz0 = 0; sz0 < 2; ++sz0)
            for (int sx0 = 0; sx0 < 2; ++sx0) {
                if (!cand[sz0][sx0] || done[sz0][sx0]) continue;
                Cell* group[4];
                int gx[4], gz[4];
                int n = 0;
                auto add = [&](int x, int z) {
                    done[z][x] = true;
                    group[n] = &c[dy][z][x]; gx[n] = x; gz[n] = z; ++n;
                };
                add(sx0, sz0);
                const bool nx = cand[sz0][1 - sx0], nz = cand[1 - sz0][sx0];
                if (nx) add(1 - sx0, sz0);
                if (nz) add(sx0, 1 - sz0);
                if ((nx || nz) && cand[1 - sz0][1 - sx0]) add(1 - sx0, 1 - sz0);
                uint8_t liquid = kEmpty;
                bool mixed = false;
                for (int i = 0; i < n; ++i) {
                    const uint8_t m = cellMaterial(*group[i]);
                    if (m == kEmpty) continue;
                    if (liquid != kEmpty && liquid != m) mixed = true;
                    liquid = m;
                }
                if (mixed || liquid == kEmpty || n < 2) continue;
                uint32_t mn = F, mx = 0;
                uint64_t total = 0;
                for (int i = 0; i < n; ++i) {
                    const uint32_t f = cellFill(*group[i]);
                    mn = std::min(mn, f); mx = std::max(mx, f); total += f;
                }
                if (mx - mn <= 1) continue;
                const uint32_t q = uint32_t(total / uint64_t(n)), rem = uint32_t(total % uint64_t(n));
                // Remainders go to the `rem` cells with the highest hash (ties by position), never by order.
                int order[4] = {0, 1, 2, 3};
                uint32_t h[4] = {};
                for (int i = 0; i < n; ++i) h[i] = cellHash(bx + gx[i], by + dy, bz + gz[i], tick_, cfg_.seed);
                std::sort(order, order + n, [&](int a, int b) { return h[a] != h[b] ? h[a] > h[b] : a < b; });
                for (int k = 0; k < n; ++k) setc(*group[order[k]], liquid, q + (uint32_t(k) < rem ? 1u : 0u));
            }
    }

    // 3. Slide: a column higher than its neighbour by more than tan(theta) x distance moves just
    //    enough fill down/sideways to bring the drop to tan(theta). Pair order is hashed.
    static const int kPairs[6][4] = {
        {0, 0, 1, 0}, {0, 1, 1, 1}, {0, 0, 0, 1}, {1, 0, 1, 1},   // orthogonal (dx,dz,dx,dz)
        {0, 0, 1, 1}, {1, 0, 0, 1},                               // diagonal
    };
    const uint32_t ph = cellHash(bx, by, bz, tick_, cfg_.seed ^ 0x51DEu);
    const int start = int(ph % 6u);
    const bool reverse = (ph >> 8) & 1u;
    auto colHeight = [&](int dx, int dz) {
        uint64_t s = 0;
        for (int dy = 0; dy < 2; ++dy) s += isSolid(c[dy][dz][dx]) ? F : cellFill(c[dy][dz][dx]);
        return s;
    };
    for (int k = 0; k < 6; ++k) {
        const int p = reverse ? (start + 6 - k) % 6 : (start + k) % 6;
        int ax = kPairs[p][0], az = kPairs[p][1], bxx = kPairs[p][2], bzz = kPairs[p][3];
        uint64_t sa = colHeight(ax, az), sb = colHeight(bxx, bzz);
        if (sa == sb) continue;
        if (sa < sb) { std::swap(ax, bxx); std::swap(az, bzz); std::swap(sa, sb); }
        // Source: the topmost non-empty, non-solid cell of the higher column.
        int sy = -1;
        for (int dy = 1; dy >= 0; --dy) {
            const Cell cell = c[dy][az][ax];
            if (isSolid(cell)) break;            // a solid above or as the top: nothing movable on top
            if (cellFill(cell)) { sy = dy; break; }
        }
        if (sy < 0) continue;
        const uint8_t m = cellMaterial(c[sy][az][ax]);
        if (!isMobile(m)) continue;
        // Destination: the lowest cell of the lower column, at or below the source, with room.
        int ty = -1;
        for (int dy = 0; dy <= sy; ++dy) {
            const Cell cell = c[dy][bzz][bxx];
            if (isSolid(cell)) continue;
            if (cellMaterial(cell) != kEmpty && cellMaterial(cell) != m) continue;
            if (cellFill(cell) >= F) continue;
            ty = dy;
            break;
        }
        if (ty < 0) continue;
        // Matter only moves along a FACE-connected path of non-solid cells inside the block: no
        // squeezing between two solids that meet at an edge or a corner (MatSimWalls test).
        {
            auto bit = [](int x, int y, int z) { return x + 2 * z + 4 * y; };
            unsigned open = 0;
            for (int y = 0; y < 2; ++y)
                for (int z = 0; z < 2; ++z)
                    for (int x = 0; x < 2; ++x)
                        if (!isSolid(c[y][z][x])) open |= 1u << bit(x, y, z);
            unsigned reach = 1u << bit(ax, sy, az);
            for (int it = 0; it < 4; ++it) {   // the block's diameter is 3 faces
                unsigned grow = reach;
                for (int i = 0; i < 8; ++i)
                    if (reach & (1u << i)) grow |= (1u << (i ^ 1)) | (1u << (i ^ 2)) | (1u << (i ^ 4));
                reach = grow & open;
            }
            if (!(reach & (1u << bit(bxx, ty, bzz)))) continue;
        }
        const bool diag = (ax != bxx) && (az != bzz);
        const uint64_t thr = slideThreshold(m, F, diag);
        const uint64_t drop = sa - sb;
        if (drop <= thr) continue;
        uint64_t mv = (drop - thr) / 2;
        mv = std::min<uint64_t>(mv, cellFill(c[sy][az][ax]));
        mv = std::min<uint64_t>(mv, F - cellFill(c[ty][bzz][bxx]));
        if (!mv) continue;
        Cell& src = c[sy][az][ax];
        Cell& dst = c[ty][bzz][bxx];
        setc(dst, m, cellFill(dst) + uint32_t(mv));
        setc(src, m, cellFill(src) - uint32_t(mv));
    }

    bool changed = false;
    for (int dy = 0; dy < 2; ++dy)
        for (int dz = 0; dz < 2; ++dz)
            for (int dx = 0; dx < 2; ++dx) {
                const IVec3 w{bx + dx, by + dy, bz + dz};
                if (!inBox(w)) continue;   // outside is solid and never written
                if (c[dy][dz][dx] != orig[dy][dz][dx]) changed = true;
                nxt_[index(w.x - origin_.x, w.y - origin_.y, w.z - origin_.z)] = c[dy][dz][dx];
            }
    return changed;
}

StepStats MatSimCore::step(int n) {
    StepStats st;
    if (!created_ || n <= 0) return st;
    const size_t nb = marks1_.size();
    std::vector<uint8_t> process(nb), fresh(nb);
    for (int t = 0; t < n; ++t) {
        const int p = int(tick_ & 1u);
        // Process mask: bricks with a moved brick (tick-1 or tick-2) in their 3^3 neighbourhood.
        if (cfg_.sleep) {
            std::fill(process.begin(), process.end(), uint8_t(0));
            for (int y = 0; y < brickDim_.y; ++y)
                for (int z = 0; z < brickDim_.z; ++z)
                    for (int x = 0; x < brickDim_.x; ++x) {
                        const size_t i = size_t(x) + size_t(brickDim_.x) * (size_t(z) + size_t(brickDim_.z) * size_t(y));
                        if (!(marks1_[i] | marks2_[i])) continue;
                        for (int dy = -1; dy <= 1; ++dy)
                            for (int dz = -1; dz <= 1; ++dz)
                                for (int dx = -1; dx <= 1; ++dx) {
                                    const int X = x + dx, Y = y + dy, Z = z + dz;
                                    if (X < 0 || Y < 0 || Z < 0 || X >= brickDim_.x || Y >= brickDim_.y || Z >= brickDim_.z) continue;
                                    process[size_t(X) + size_t(brickDim_.x) * (size_t(Z) + size_t(brickDim_.z) * size_t(Y))] = 1;
                                }
                    }
        }
        std::fill(fresh.begin(), fresh.end(), uint8_t(0));
        uint64_t eligible = 0;
        // World-aligned parity: blocks start where (world cell - p) is even.
        const int x0 = origin_.x - ((origin_.x - p) & 1);
        const int y0 = origin_.y - ((origin_.y - p) & 1);
        const int z0 = origin_.z - ((origin_.z - p) & 1);
        for (int by = y0; by < origin_.y + size_.y; by += 2)
            for (int bz = z0; bz < origin_.z + size_.z; bz += 2)
                for (int bx = x0; bx < origin_.x + size_.x; bx += 2) {
                    if (cfg_.sleep && !process[brickOf(bx, by, bz)]) { copyBlock(bx, by, bz); continue; }
                    ++st.evaluatedBlocks;
                    if (evalBlock(bx, by, bz, p)) {
                        ++eligible;
                        markBricksOfBlock(bx, by, bz, fresh);
                    }
                }
        std::swap(cur_, nxt_);
        marks2_.swap(marks1_);
        marks1_.swap(fresh);   // marks1_ = this tick's, marks2_ = last tick's
        ++tick_;
        ++st.ticks;
        st.eligibleBlocks += eligible;
        st.lastTickEligible = eligible;
    }
    return st;
}

} // namespace Phyxel::Core::MatSim
