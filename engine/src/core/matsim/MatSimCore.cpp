// docs/MaterialSimPlan.md §6a — MatSim P1a: the CPU reference material cellular automaton.
// See the header for the model. This file is the source of truth the P1b GPU port must match.
#include "core/matsim/MatSimCore.h"

#include <algorithm>
#include <chrono>
#include <cstring>
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

uint32_t isqrt32(uint32_t v) {
    uint32_t r = 0, bit = 1u << 30;
    while (bit > v) bit >>= 2;
    while (bit != 0) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    return r;
}

uint32_t orificeConstant(uint32_t full, int cpm, uint32_t tickHz) {
    // Torricelli through one cell face: v = Cd sqrt(2 g dh). Per tick that face passes
    // v * dt * h^2 m^3 = Cd * sqrt(2 g F cpm) / tickHz * sqrt(dU) fill units, where dU is the head
    // difference in fill units (F units = one cell height). x1024 fixed point, computed once.
    if (!full || cpm <= 0 || !tickHz) return 0;
    const double k = 1024.0 * (kDischargeMilli / 1000.0) * std::sqrt(2.0 * 9.81 * double(full) * double(cpm)) /
                     double(tickHz);
    return static_cast<uint32_t>(std::llround(k));
}

bool MatSimCore::adoptStepped(std::vector<Cell> cells, uint64_t tick, std::vector<uint8_t> m1,
                              std::vector<uint8_t> m2, std::vector<uint32_t> q, std::vector<uint32_t> h,
                              std::vector<uint32_t> mx) {
    if (!created_ || cells.size() != cur_.size() || m1.size() != marks1_.size() || m2.size() != marks2_.size() ||
        q.size() != q_.size() || h.size() != h_.size() || mx.size() != m_.size())
        return false;
    for (const auto* v : {&q, &mx})   // audit R3: a pointer from elsewhere must name a cell of this box
        for (uint32_t p : *v)
            if (p != kNoHead && ((p >> 8) >= v->size() || (p & 0xFFu) >= 255u)) return false;
    cur_ = std::move(cells);
    nxt_ = cur_;
    tick_ = tick;
    marks1_ = std::move(m1);
    marks2_ = std::move(m2);
    q_ = std::move(q);
    h_ = std::move(h);
    m_ = std::move(mx);
    srcBricks_.clear();   // 6l: the pointers came from elsewhere; the next incremental pass starts full
    bumpGen();
    return true;
}

bool MatSimCore::adoptSteppedFrom(const Cell* cells, uint64_t tick, const uint32_t* m1, const uint32_t* m2,
                                  const uint32_t* q, const uint32_t* h, const uint32_t* mx) {
    if (!created_ || !cells || !m1 || !m2 || !q || !h || !mx) return false;
    const size_t n = cur_.size();
    for (const uint32_t* v : {q, mx})   // audit R3: a pointer from elsewhere must name a cell of this box
        for (size_t i = 0; i < n; ++i)
            if (v[i] != kNoHead && ((v[i] >> 8) >= n || (v[i] & 0xFFu) >= 255u)) return false;
    std::memcpy(cur_.data(), cells, n * sizeof(Cell));
    std::memcpy(nxt_.data(), cells, n * sizeof(Cell));
    std::memcpy(q_.data(), q, n * 4);
    std::memcpy(h_.data(), h, n * 4);
    std::memcpy(m_.data(), mx, n * 4);
    for (size_t i = 0; i < marks1_.size(); ++i) { marks1_[i] = m1[i] ? 1 : 0; marks2_[i] = m2[i] ? 1 : 0; }
    tick_ = tick;
    srcBricks_.clear();   // 6l: the pointers came from elsewhere; the next incremental pass starts full
    bumpGen();
    return true;
}

void MatSimCore::resetPressure() {
    bumpGen();
    q_.assign(cur_.size(), kNoHead);
    qn_.assign(cur_.size(), kNoHead);
    m_.assign(cur_.size(), kNoHead);
    mn_.assign(cur_.size(), kNoHead);
    h_.assign(cur_.size(), 0u);
    srcBricks_.clear();
    orificeK_ = orificeConstant(full_, cpm_, cfg_.tickHz);
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
    bumpGen();
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
    // Audit R1: the Torricelli cap's 32-bit product (K x isqrt(head)) stays in range for 30-960 Hz.
    if (cfg.tickHz < 30 || cfg.tickHz > 960) return refuse("tickHz must be within 30-960");
    if (uint64_t(sz.y + 1) * fullUnits(cpm) >= kHeadMask)
        return refuse("box is " + std::to_string(sz.y) + " cells tall; the pressure head field needs "
                      "(height + 1) x FULL < 2^30");

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
    resetPressure();
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
    bumpGen();
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
    q_.clear();
    qn_.clear();
    m_.clear();
    mn_.clear();
    h_.clear();
    srcBricks_.clear();
    orificeK_ = 0;
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

size_t MatSimCore::blockEmpty(const std::vector<IVec3>& ws) {
    size_t n = 0;
    for (const IVec3& w : ws) {
        if (!inBox(w)) continue;
        Cell& c = cur_[index(w.x - origin_.x, w.y - origin_.y, w.z - origin_.z)];
        if (cellMaterial(c) != kEmpty) continue;   // also skips a cell already blocked (now solid)
        c = makeCell(kSolid, 0);
        blocked_.push_back(w);
        wakeCells(w, w);
        ++n;
    }
    return n;
}

void MatSimCore::unblockAll() {
    for (const IVec3& w : blocked_) {
        Cell& c = cur_[index(w.x - origin_.x, w.y - origin_.y, w.z - origin_.z)];
        c = makeCell(kEmpty, 0);   // stepping never writes a solid cell, so it is still the wall we made
        wakeCells(w, w);
    }
    blocked_.clear();
}

void MatSimCore::resolidify(const MicroSolidFn& fn) {
    bumpGen();
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
    bumpGen();
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
    if (uint64_t(nsz[1] + 1) * fullUnits(ncpm) >= kHeadMask)
        return refuse("the switch would make the box too tall for the pressure head field");

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
    resetPressure();
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
    bumpGen();
    brickLo_ = {floorDiv(origin_.x - 1, 8), floorDiv(origin_.y - 1, 8), floorDiv(origin_.z - 1, 8)};
    const IVec3 hi{floorDiv(origin_.x + size_.x, 8), floorDiv(origin_.y + size_.y, 8), floorDiv(origin_.z + size_.z, 8)};
    brickDim_ = {hi.x - brickLo_.x + 1, hi.y - brickLo_.y + 1, hi.z - brickLo_.z + 1};
    const size_t n = size_t(brickDim_.x) * size_t(brickDim_.y) * size_t(brickDim_.z);
    marks1_.assign(n, 1);
    marks2_.assign(n, 1);
}

void MatSimCore::wakeCells(IVec3 lo, IVec3 hi) {
    bumpGen();   // every cell edit (setCell, fillBox, blockEmpty, unblockAll) marks through here
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
// §6d — the pressure (head) field. Every cell, every tick, a gather from the previous tick:
//  - A liquid cell open above (no liquid, no solid over it) is a FREE SURFACE at elevation
//    E = (y - box floor) * FULL + fill and offers itself (hops 0). A partial free surface is
//    atmospheric: it adopts nothing. Every other liquid cell (full, or closed above) and every
//    air cell adopts its liquid neighbours' sources (hops + 1); air cells never pass them on.
//  - The source's E is read NOW from the source cell (valid only if it was a free surface last
//    tick), so a dropping surface is never stale.
//  - Two pointers. MAX: highest E, then fewest hops, then lowest box index (= world (y, z, x)
//    order); its E is the cell's head, uniform through a connected body. ROUTE: among the
//    candidates whose E stands within the deadband of that head (the surfaces that can absorb a
//    hole), the fewest hops, then lowest index. Pushes and the pressurised test use the head;
//    hole routing uses the route's hops.
//  - Flags: closed above (bit 31), a liquid face neighbour full to within the deadband (bit 30).
//  - A cell whose pointer or head word changed marks its brick, so sleep wakes on a far change.
uint64_t MatSimCore::pressurePass(std::vector<uint8_t>& fresh) {
    const uint32_t F = full_;
    uint64_t changedCells = 0;
    const size_t layer = size_t(size_.x) * size_t(size_.z);
    auto liquidCell = [](Cell c) {
        const uint8_t m = cellMaterial(c);
        return m != kSolid && m != kEmpty && phaseOf(m) == Phase::Liquid && cellFill(c) > 0;
    };
    static const int kD[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    auto cellPass = [&](int lx, int ly, int lz) {
                const size_t i = index(lx, ly, lz);
                const IVec3 w{origin_.x + lx, origin_.y + ly, origin_.z + lz};
                const Cell c = cur_[i];
                uint32_t qv = kNoHead, hv = 0, mv = kNoHead;
                const uint8_t m = cellMaterial(c);
                const bool liq = liquidCell(c);
                if (liq || m == kEmpty) {
                    const uint32_t f = cellFill(c);
                    const Cell above = at({w.x, w.y + 1, w.z});
                    const bool solidAbove = isSolid(above), liqAbove = liquidCell(above);
                    const bool openAbove = !solidAbove && !liqAbove;
                    bool fullNbr = false;
                    for (const auto& d : kD) {
                        const Cell n = at({w.x + d[0], w.y + d[1], w.z + d[2]});
                        // "Full" here = within the deadband of full: level's remainders leave such cells.
                        if (liquidCell(n) && cellFill(n) + pressureDeadband() >= F && (!liq || cellMaterial(n) == m)) {
                            fullNbr = true;
                            break;
                        }
                    }
                    // Free surfaces (liquid, open above) offer themselves; a PARTIAL one is atmospheric
                    // and adopts nothing. Everything else that is liquid or air carries heads.
                    const bool surface = liq && openAbove;
                    const bool adopts = !(surface && f < F);
                    // 1. MAX: the highest surface reachable (ties: fewest hops, lowest index).
                    const uint32_t db = pressureDeadband();
                    bool have = false;
                    uint32_t bE = 0, bAge = 0, bPtr = 0;
                    auto offerMax = [&](uint32_t e, uint32_t age, uint32_t ptr) {
                        if (!have || e > bE || (e == bE && (age < bAge || (age == bAge && ptr < bPtr)))) {
                            have = true; bE = e; bAge = age; bPtr = ptr;
                        }
                    };
                    // 2. ROUTE: the nearest surface standing within the deadband of that max.
                    bool haveR = false;
                    uint32_t rAge = 0, rPtr = 0;
                    struct Cand { uint32_t e, age, ptr; };
                    Cand rc[7];
                    int nrc = 0;
                    const uint32_t selfE = uint32_t(ly) * F + f;
                    if (surface) { offerMax(selfE, 0, uint32_t(i)); rc[nrc++] = {selfE, 0, uint32_t(i)}; }
                    if (adopts) {
                        for (const auto& d : kD) {
                            const IVec3 nw{w.x + d[0], w.y + d[1], w.z + d[2]};
                            if (!inBox(nw)) continue;
                            const size_t ni = index(nw.x - origin_.x, nw.y - origin_.y, nw.z - origin_.z);
                            const Cell n = cur_[ni];
                            if (!liquidCell(n) || (liq && cellMaterial(n) != m)) continue;
                            for (int field = 0; field < 2; ++field) {
                                const uint32_t qn = field == 0 ? m_[ni] : q_[ni];
                                if (qn == kNoHead) continue;
                                const uint32_t age = qn & 0xFFu;
                                if (age + 1 >= 255u) continue;
                                const uint32_t src = qn >> 8;
                                // The source was a free surface last tick (it pointed at itself in this field).
                                if ((field == 0 ? m_[src] : q_[src]) != (src << 8)) continue;
                                const Cell sc = cur_[src];
                                if (!liquidCell(sc) || cellMaterial(sc) != cellMaterial(n)) continue;
                                const uint32_t e = uint32_t(src / layer) * F + cellFill(sc);
                                if (field == 0) offerMax(e, age + 1, src);
                                else rc[nrc++] = {e, age + 1, src};
                            }
                        }
                    }
                    for (int k = 0; k < nrc; ++k) {
                        if (!have || rc[k].e + db < bE) continue;   // not an absorber for this head
                        if (!haveR || rc[k].age < rAge || (rc[k].age == rAge && rc[k].ptr < rPtr)) {
                            haveR = true; rAge = rc[k].age; rPtr = rc[k].ptr;
                        }
                    }
                    if (have) { mv = (bPtr << 8) | bAge; hv = bE + 1; }
                    if (haveR) qv = (rPtr << 8) | rAge;
                    if (!openAbove) hv |= kHeadClosed;
                    if (fullNbr) hv |= kHeadFullNbr;
                }
                qn_[i] = qv;
                mn_[i] = mv;
                if (qv != q_[i] || mv != m_[i] || hv != h_[i]) { fresh[brickOf(w.x, w.y, w.z)] = 1; ++changedCells; }
                h_[i] = hv;   // no other cell reads h_ in this pass
    };
    // 6l: a cell's words are a function of its own cell and its 6 face neighbours (cells, q_, m_), and of the cells and
    // words of the sources its neighbours point at. If none of those changed since the last pass, it would write what it
    // holds. "Changed since the last pass" = marks1_: the bricks the last pass changed heads in, the last block step
    // moved cells in, and every edit since (wakeCells). So recompute only bricks face-adjacent to a marked brick, or
    // face-adjacent to a cell pointing into one; copy the rest. Exact (PressureIncrementalEqualsTheFullPass).
    const bool incremental = cfg_.sleep && cfg_.pressureIncremental && marks1_.size() == fresh.size();
    if (!incremental) {
        for (int ly = 0; ly < size_.y; ++ly)
            for (int lz = 0; lz < size_.z; ++lz)
                for (int lx = 0; lx < size_.x; ++lx) cellPass(lx, ly, lz);
        lastPressureBricks_ = marks1_.size();
        srcBricks_.clear();   // stale once the incremental pass runs again: it starts with a full pass
        q_.swap(qn_);
        m_.swap(mn_);
        return changedCells;
    }
    const IVec3 bd = brickDim_;
    const size_t nb = marks1_.size();
    auto bidx = [&](int x, int y, int z) { return size_t(x) + size_t(bd.x) * (size_t(z) + size_t(bd.z) * size_t(y)); };
    std::vector<uint8_t> dirty(nb, 0);
    auto dilate = [&](size_t B) {
        const int x = int(B % size_t(bd.x)), z = int((B / size_t(bd.x)) % size_t(bd.z)), y = int(B / (size_t(bd.x) * size_t(bd.z)));
        dirty[B] = 1;
        if (x > 0) dirty[bidx(x - 1, y, z)] = 1;
        if (x + 1 < bd.x) dirty[bidx(x + 1, y, z)] = 1;
        if (y > 0) dirty[bidx(x, y - 1, z)] = 1;
        if (y + 1 < bd.y) dirty[bidx(x, y + 1, z)] = 1;
        if (z > 0) dirty[bidx(x, y, z - 1)] = 1;
        if (z + 1 < bd.z) dirty[bidx(x, y, z + 1)] = 1;
    };
    // srcBricks_[B]: the bricks the q_/m_ pointers of B's cells name. A clean brick's pointers do not change, so its
    // list stays valid; a computed brick rebuilds its list below. No list yet (first pass, resized box): everything.
    const bool haveLists = srcBricks_.size() == nb;
    if (!haveLists) std::fill(dirty.begin(), dirty.end(), uint8_t(1));
    else
        for (size_t B = 0; B < nb; ++B) {
            if (marks1_[B]) { dilate(B); continue; }
            for (const uint32_t S : srcBricks_[B])
                if (marks1_[S]) { dilate(B); break; }
        }
    srcBricks_.resize(nb);
    auto brickRange = [&](size_t B, int& lx0, int& lx1, int& ly0, int& ly1, int& lz0, int& lz1) {
        const int x = int(B % size_t(bd.x)), z = int((B / size_t(bd.x)) % size_t(bd.z)), y = int(B / (size_t(bd.x) * size_t(bd.z)));
        const int wx0 = (brickLo_.x + x) * 8, wy0 = (brickLo_.y + y) * 8, wz0 = (brickLo_.z + z) * 8;
        lx0 = std::max(0, wx0 - origin_.x); lx1 = std::min(size_.x, wx0 + 8 - origin_.x);
        ly0 = std::max(0, wy0 - origin_.y); ly1 = std::min(size_.y, wy0 + 8 - origin_.y);
        lz0 = std::max(0, wz0 - origin_.z); lz1 = std::min(size_.z, wz0 + 8 - origin_.z);
        return lx0 < lx1 && ly0 < ly1 && lz0 < lz1;
    };
    // Jacobi: compute every dirty brick into qn_/mn_ from the old q_/m_ first, then publish them. Clean bricks keep q_/m_.
    size_t computed = 0;
    int lx0, lx1, ly0, ly1, lz0, lz1;
    for (size_t B = 0; B < nb; ++B) {
        if (!dirty[B] || !brickRange(B, lx0, lx1, ly0, ly1, lz0, lz1)) continue;
        ++computed;
        for (int ly = ly0; ly < ly1; ++ly)
            for (int lz = lz0; lz < lz1; ++lz)
                for (int lx = lx0; lx < lx1; ++lx) cellPass(lx, ly, lz);
    }
    const size_t sx = size_t(size_.x), sz = size_t(size_.z);
    for (size_t B = 0; B < nb; ++B) {
        if (!dirty[B]) continue;
        std::vector<uint32_t>& list = srcBricks_[B];
        list.clear();
        if (!brickRange(B, lx0, lx1, ly0, ly1, lz0, lz1)) continue;
        uint32_t lastSrc = 0xFFFFFFFFu;
        for (int ly = ly0; ly < ly1; ++ly)
            for (int lz = lz0; lz < lz1; ++lz) {
                const size_t i0 = index(lx0, ly, lz);
                for (size_t i = i0; i < i0 + size_t(lx1 - lx0); ++i) {
                    q_[i] = qn_[i];
                    m_[i] = mn_[i];
                    for (const uint32_t p : {q_[i], m_[i]}) {
                        if (p == kNoHead || (p >> 8) == lastSrc) continue;
                        lastSrc = p >> 8;
                        const int x = int(lastSrc % sx), z = int((lastSrc / sx) % sz), y = int(lastSrc / (sx * sz));
                        const uint32_t S = uint32_t(brickOf(origin_.x + x, origin_.y + y, origin_.z + z));
                        if (std::find(list.begin(), list.end(), S) == list.end()) list.push_back(S);
                    }
                }
            }
    }
    lastPressureBricks_ = computed;
    return changedCells;
}

// ---------------------------------------------------------------------------------------------
// The block rules (§6a): gravity, liquid level, slide (angle of repose; liquids at 0 deg), and
// (§6d) pressure: flow from a full cell to a neighbour whose surface is below the full cell's head.

bool MatSimCore::evalBlock(int bx, int by, int bz, int /*parity*/) {
    Cell c[2][2][2];   // [dy][dz][dx]
    Cell orig[2][2][2];
    for (int dy = 0; dy < 2; ++dy)
        for (int dz = 0; dz < 2; ++dz)
            for (int dx = 0; dx < 2; ++dx)
                orig[dy][dz][dx] = c[dy][dz][dx] = readPrev(bx + dx, by + dy, bz + dz);
    const uint32_t F = full_;
    auto setc = [](Cell& cell, uint8_t m, uint32_t f) { cell = f ? makeCell(m, f) : makeCell(kEmpty, 0); };

    // §6d: this tick's head field for the block's cells (bit i = dx + 2 dz + 4 dy).
    const bool P = cfg_.pressure;
    const uint32_t db = pressureDeadband();
    uint32_t hd[8] = {}, ag[8] = {};
    bool inb[8] = {}, intr[8] = {}, fnb[8] = {};
    if (P)
        for (int i = 0; i < 8; ++i) {
            const IVec3 w{bx + (i & 1), by + (i >> 2), bz + ((i >> 1) & 1)};
            if (!inBox(w)) { ag[i] = 255; continue; }
            const size_t j = index(w.x - origin_.x, w.y - origin_.y, w.z - origin_.z);
            inb[i] = true;
            hd[i] = h_[j] & kHeadMask;
            intr[i] = (h_[j] & kHeadClosed) != 0;   // closed above: liquid or solid over it
            fnb[i] = (h_[j] & kHeadFullNbr) != 0;   // a full liquid face neighbour: a possible donor
            ag[i] = q_[j] & 0xFFu;
        }
    auto cellRef = [&](int i) -> Cell& { return c[i >> 2][(i >> 1) & 1][i & 1]; };
    auto elev = [&](int i) { return uint32_t(by + (i >> 2) - origin_.y) * F + cellFill(cellRef(i)); };
    // A pressurised hole: a closed, not-full cell beside a full one, whose head stands above
    // `e` + deadband. Only such holes leave gravity / level / slide to the pressure rule.
    auto pressurised = [&](int i, uint32_t e) {
        return P && inb[i] && intr[i] && fnb[i] && hd[i] != 0 && hd[i] - 1 > e + db;
    };

    // 1. Gravity, per column, top -> bottom.
    bool fell[4] = {};   // §6d: a liquid fall in this column this evaluation (the pressure rule never undoes it)
    for (int dz = 0; dz < 2; ++dz)
        for (int dx = 0; dx < 2; ++dx) {
            Cell& top = c[1][dz][dx];
            Cell& bot = c[0][dz][dx];
            const uint8_t m = cellMaterial(top);
            if (!isMobile(m) || isSolid(bot)) continue;
            if (cellMaterial(bot) != kEmpty && cellMaterial(bot) != m) continue;
            // §6d: under pressure from elsewhere, a hole is filled by the pressure rule, not by a fall.
            if (phaseOf(m) == Phase::Liquid && pressurised(dx + 2 * dz, elev(dx + 2 * dz + 4))) continue;
            const uint32_t mv = std::min(cellFill(top), F - cellFill(bot));
            if (!mv) continue;
            uint32_t land = mv;
            if (cfg_.testLeakOneUnit) { land -= 1; cfg_.testLeakOneUnit = false; }   // TEST-ONLY control
            setc(bot, m, cellFill(bot) + land);
            setc(top, m, cellFill(top) - mv);
            if (phaseOf(m) == Phase::Liquid) fell[dx + 2 * dz] = true;
        }

    // 2. Liquid level, per layer, over supported cells of one liquid (or empty), deadband 1 unit.
    //    Cells level only with cells they are FACE-connected to through the group: two cells that
    //    touch only at an edge (a staircased diagonal wall) are separate pools (MatSimWalls test).
    // §6j: the thin-film floor in fill units (0 = off).
    const uint32_t fu = cfg_.filmFloor > 0.f ? std::min<uint32_t>(F, uint32_t(double(cfg_.filmFloor) * F)) : 0u;
    for (int dy = 0; dy < 2; ++dy) {
        bool cand[2][2] = {};
        bool onSolid[2][2] = {};
        for (int dz = 0; dz < 2; ++dz)
            for (int dx = 0; dx < 2; ++dx) {
                const Cell cell = c[dy][dz][dx];
                if (isSolid(cell)) continue;
                const uint8_t m = cellMaterial(cell);
                if (m != kEmpty && phaseOf(m) != Phase::Liquid) continue;
                const Cell below = dy ? c[0][dz][dx] : readPrev(bx + dx, by - 1, bz + dz);
                onSolid[dz][dx] = isSolid(below);
                if (!(isSolid(below) || cellFill(below) == F)) continue;   // unsupported
                const int bi = dx + 2 * dz + 4 * dy;
                if (cellFill(cell) < F && pressurised(bi, elev(bi))) continue;   // §6d: a hole under pressure
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
                bool film = fu > 0;
                for (int i = 0; i < n; ++i) {
                    const uint32_t f = cellFill(*group[i]);
                    mn = std::min(mn, f); mx = std::max(mx, f); total += f;
                    film = film && onSolid[gz[i]][gx[i]];
                }
                // §6j: a layer resting on solid whose level would fall below the floor gathers into
                // its k = total / floor fullest cells (at least one) instead: a bead, not a film.
                // Fullest first, ties by hash: once gathered it is a fixed point (no jitter).
                if (film && total < uint64_t(fu) * uint64_t(n)) {
                    const int k = std::max(1, int(total / fu));
                    int order[4] = {0, 1, 2, 3};
                    uint32_t f[4] = {}, h[4] = {};
                    for (int i = 0; i < n; ++i) {
                        f[i] = cellFill(*group[i]);
                        h[i] = cellHash(bx + gx[i], by + dy, bz + gz[i], tick_, cfg_.seed);
                    }
                    std::sort(order, order + n, [&](int a, int b) {
                        return f[a] != f[b] ? f[a] > f[b] : h[a] != h[b] ? h[a] > h[b] : a < b;
                    });
                    const uint32_t q = uint32_t(total / uint64_t(k)), rem = uint32_t(total % uint64_t(k));
                    bool moved = false;
                    for (int j = 0; j < n; ++j) {
                        const uint32_t want = j < k ? q + (uint32_t(j) < rem ? 1u : 0u) : 0u;
                        moved = moved || cellFill(*group[order[j]]) != want;
                        setc(*group[order[j]], liquid, want);
                    }
                    if (moved) ++filmGathers_;
                    continue;
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
            const int di = bxx + 2 * bzz + 4 * dy;
            if (phaseOf(m) == Phase::Liquid && pressurised(di, elev(di))) continue;   // §6d
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
        // §6j: a slide never makes a new liquid cell thinner than the floor (no creep onto a
        // surface, no fan of slivers off a falling cell), and never splits a falling cell into a
        // remnant thinner than the floor.
        if (fu && phaseOf(m) == Phase::Liquid) {
            if (cellFill(c[ty][bzz][bxx]) == 0 && mv < fu) { ++filmHeld_; continue; }
            const Cell under = sy ? c[0][az][ax] : readPrev(bx + ax, by - 1, bz + az);
            const uint64_t left = cellFill(c[sy][az][ax]) - mv;
            if (cellMaterial(under) == kEmpty && left && left < fu) { ++filmHeld_; continue; }
        }
        Cell& src = c[sy][az][ax];
        Cell& dst = c[ty][bzz][bxx];
        setc(dst, m, cellFill(dst) + uint32_t(mv));
        setc(src, m, cellFill(src) - uint32_t(mv));
    }

    // 4. Pressure (§6d). For each not-full cell X in fixed order, a full liquid face neighbour Y in
    //    the block whose head stands above X's surface by more than the deadband pushes fill into X:
    //  - into an INTERIOR X (a hole under water or in a pipe) only from a Y nearer the source
    //    (fewer hops): holes travel upstream to the high surface instead of diffusing;
    //  - into an OPEN X from below (a rising column, an outlet), or sideways if X is unsupported
    //    (a jet from a wall); open X sideways and supported is the level rule's;
    //  - from above: everything that fits (a fall). Otherwise half the head excess, capped by
    //    Torricelli through one face (orificeK_).
    //  Ties between donors: fewest hops, then vertical, x, z.
    if (P)
        for (int i = 0; i < 8; ++i) {
            if (!inb[i]) continue;
            Cell& X = cellRef(i);
            if (isSolid(X)) continue;
            const uint8_t mx = cellMaterial(X);
            if (mx != kEmpty && phaseOf(mx) != Phase::Liquid) continue;
            const uint32_t fx = cellFill(X);
            if (fx >= F) continue;
            const uint32_t ex = elev(i);
            const int dy = i >> 2;
            int best = -1;
            for (int k = 0; k < 3; ++k) {
                const int j = i ^ (k == 0 ? 4 : (k == 1 ? 1 : 2));
                if (!inb[j]) continue;
                const Cell Y = cellRef(j);
                const uint8_t my = cellMaterial(Y);
                // A donor is full to within the deadband.
                if (my == kEmpty || my == kSolid || phaseOf(my) != Phase::Liquid || cellFill(Y) + db < F) continue;
                if (mx != kEmpty && my != mx) continue;
                const bool fromAbove = (k == 0 && dy == 0);
                // A fall needs head > surface + deadband; any other push needs > 2 deadbands, so
                // the excess left after a half-step push still keeps gravity off (no push/fall cycle).
                if (hd[j] == 0 || hd[j] - 1 <= ex + (fromAbove ? db : 2 * db)) continue;
                if (k == 0 && dy == 1 && fell[i & 3]) continue;   // never push back what just fell
                if (intr[i]) {
                    if (ag[j] >= ag[i]) continue;
                } else {
                    if (k == 0 && dy == 0) continue;   // Y above an open X cannot happen; keep it out
                    if (k != 0 && fx + db < F) {   // open, sideways: only unsupported, or a top-off
                        const Cell below = dy ? cellRef(i - 4) : readPrev(bx + (i & 1), by - 1, bz + ((i >> 1) & 1));
                        if (isSolid(below) || cellFill(below) == F) continue;
                    }
                }
                if (best < 0 || ag[j] < ag[best]) best = j;
            }
            if (best < 0) continue;
            const uint32_t fy = cellFill(cellRef(best));
            uint32_t mv = std::min(F - fx, fy);
            if (!(best == (i ^ 4) && dy == 0)) {
                const uint32_t d = hd[best] - 1 - ex;
                mv = std::min(mv, std::max(1u, d / 2));
                mv = std::min(mv, std::max(1u, (orificeK_ * isqrt32(d)) >> 10));
            }
            const uint8_t mm = cellMaterial(cellRef(best));
            setc(cellRef(best), mm, fy - mv);
            setc(X, mm, fx + mv);
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
    bumpGen();
    StepStats st;
    if (!created_ || n <= 0) return st;
    const size_t nb = marks1_.size();
    std::vector<uint8_t> process(nb), fresh(nb);
    for (int t = 0; t < n; ++t) {
        const int p = int(tick_ & 1u);
        // §6d: the head field first; a cell whose head changed marks its brick in this tick's set.
        std::fill(fresh.begin(), fresh.end(), uint8_t(0));
        // 6k: quiet last tick (no move, no head change, no edit: marks1_ all clear) means the head field's inputs are
        // what the last pass read, so it would rebuild the same field. Skip it (exact: QuietTicksSkipThePressurePassExactly).
        auto clear = [](const std::vector<uint8_t>& v) { return std::all_of(v.begin(), v.end(), [](uint8_t m) { return m == 0; }); };
        const bool quiet1 = cfg_.sleep && clear(marks1_);
        const bool quiet2 = quiet1 && clear(marks2_);
        uint64_t headChanged = 0;
        if (cfg_.pressure && !quiet1) {
            const auto t0 = std::chrono::steady_clock::now();
            headChanged = pressurePass(fresh);
            st.lastTickPressureBricks = lastPressureBricks_;
            st.pressureMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        } else {
            if (cfg_.pressure) ++st.pressurePassesSkipped;
            st.lastTickPressureBricks = 0;
        }
        st.bricks = nb;
        st.lastTickHeadBricks = uint64_t(std::count_if(fresh.begin(), fresh.end(), [](uint8_t m) { return m != 0; }));
        if (quiet2) {
            // Quiet for two ticks: every brick sleeps and every block would be copied unchanged. Same tick bookkeeping.
            marks2_.swap(marks1_);
            marks1_.swap(fresh);
            ++tick_;
            ++st.ticks;
            st.lastTickEligible = 0;
            st.lastTickHeadChanges = 0;
            continue;
        }
        // Process mask: bricks with a moved brick (tick-1, tick-2, or a head change now) in their
        // 3^3 neighbourhood.
        if (cfg_.sleep) {
            std::fill(process.begin(), process.end(), uint8_t(0));
            for (int y = 0; y < brickDim_.y; ++y)
                for (int z = 0; z < brickDim_.z; ++z)
                    for (int x = 0; x < brickDim_.x; ++x) {
                        const size_t i = size_t(x) + size_t(brickDim_.x) * (size_t(z) + size_t(brickDim_.z) * size_t(y));
                        if (!(marks1_[i] | marks2_[i] | fresh[i])) continue;
                        for (int dy = -1; dy <= 1; ++dy)
                            for (int dz = -1; dz <= 1; ++dz)
                                for (int dx = -1; dx <= 1; ++dx) {
                                    const int X = x + dx, Y = y + dy, Z = z + dz;
                                    if (X < 0 || Y < 0 || Z < 0 || X >= brickDim_.x || Y >= brickDim_.y || Z >= brickDim_.z) continue;
                                    process[size_t(X) + size_t(brickDim_.x) * (size_t(Z) + size_t(brickDim_.z) * size_t(Y))] = 1;
                                }
                    }
        }
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
        st.headChanges += headChanged;
        st.lastTickHeadChanges = headChanged;
        st.lastTickMovedBricks = uint64_t(std::count_if(marks1_.begin(), marks1_.end(), [](uint8_t m) { return m != 0; }));
    }
    return st;
}

} // namespace Phyxel::Core::MatSim
